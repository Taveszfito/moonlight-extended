#include "dualsenseaudio.h"
#include "streaming/input/input.h"

#include <QByteArray>
#include <QMutexLocker>
#include <opus.h>
#include <algorithm>
#include <cmath>
#include <cstring>

static constexpr int kDualSenseSampleRate = 48000;
static constexpr int kDualSenseChannels = 4;
static constexpr int kBytesPerFrame = kDualSenseChannels * sizeof(int16_t);
static constexpr uint32_t kMaximumQueuedBytes = kDualSenseSampleRate * kBytesPerFrame / 4;
static constexpr double kPi = 3.14159265358979323846;

DualSenseAudioRenderer& DualSenseAudioRenderer::instance()
{
    static DualSenseAudioRenderer renderer;
    return renderer;
}

void DualSenseAudioRenderer::configure(StreamingPreferences::DualSenseAudioMode mode)
{
    QMutexLocker locker(&m_Mutex);
    if (m_Mode != mode && mode == StreamingPreferences::DSAM_OFF && m_Device != 0) {
        SDL_ClearQueuedAudio(m_Device);
    }
    m_Mode = mode;
    SDL_LogInfo(SDL_LOG_CATEGORY_AUDIO, "DualSense Extended audio mode: %d", static_cast<int>(mode));
}

uint32_t DualSenseAudioRenderer::bluetoothCrc(const uint8_t* data, size_t length)
{
    uint32_t crc = 0xffffffffU;
    auto update = [&crc](uint8_t value) {
        crc ^= value;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
        }
    };
    update(0xa2);
    for (size_t i = 0; i < length; i++) update(data[i]);
    return crc ^ 0xffffffffU;
}

void DualSenseAudioRenderer::closeBluetoothLocked()
{
    m_BluetoothController = nullptr;
    if (m_BluetoothEncoder != nullptr) {
        opus_encoder_destroy(m_BluetoothEncoder);
        m_BluetoothEncoder = nullptr;
    }
    m_Bluetooth = false;
    m_BluetoothWakeSent = false;
    m_SpeakerFrames = 0;
    m_SpeakerOpusReady = false;
    m_HapticRingPosition = m_HapticDecimationPhase = m_HapticReportPosition = 0;
    m_HapticLeft.fill(0.0);
    m_HapticRight.fill(0.0);
    m_HapticReport.fill(0);
}

bool DualSenseAudioRenderer::openBluetoothLocked(SDL_GameController* controller, const char* path)
{
    closeBluetoothLocked();
    if (controller == nullptr) {
        return false;
    }
    m_BluetoothController = controller;

    int opusError = OPUS_OK;
    m_BluetoothEncoder = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &opusError);
    if (m_BluetoothEncoder == nullptr || opusError != OPUS_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "DualSense Bluetooth Opus encoder failed: %d", opusError);
        closeBluetoothLocked();
        return false;
    }
    opus_encoder_ctl(m_BluetoothEncoder, OPUS_SET_BITRATE(160000));
    opus_encoder_ctl(m_BluetoothEncoder, OPUS_SET_VBR(0));
    opus_encoder_ctl(m_BluetoothEncoder, OPUS_SET_COMPLEXITY(0));
    opus_encoder_ctl(m_BluetoothEncoder, OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_10_MS));

    const double middle = (m_HapticCoefficients.size() - 1) / 2.0;
    const double normalizedCutoff = 1400.0 / 48000.0;
    double sum = 0.0;
    for (size_t i = 0; i < m_HapticCoefficients.size(); i++) {
        const double x = static_cast<double>(i) - middle;
        const double sinc = x == 0.0 ? 2.0 * normalizedCutoff :
            std::sin(2.0 * kPi * normalizedCutoff * x) / (kPi * x);
        const double window = 0.42 - 0.5 * std::cos(2.0 * kPi * i / (m_HapticCoefficients.size() - 1)) +
            0.08 * std::cos(4.0 * kPi * i / (m_HapticCoefficients.size() - 1));
        m_HapticCoefficients[i] = sinc * window;
        sum += m_HapticCoefficients[i];
    }
    for (double& coefficient : m_HapticCoefficients) coefficient /= sum;

    m_Bluetooth = true;
    m_BluetoothSequence = m_BluetoothPacketCounter = 0;
    SDL_LogInfo(SDL_LOG_CATEGORY_AUDIO, "DualSense native Bluetooth audio transport opened: %s", path);
    if (!sendBluetoothWakeLocked()) return false;
    SDL_Delay(10);
    return sendBluetoothStateLocked();
}

void DualSenseAudioRenderer::setController(SDL_GameController* controller, bool bluetooth, const char* path)
{
    QMutexLocker locker(&m_Mutex);
    if (bluetooth) {
        openBluetoothLocked(controller, path);
    }
    else {
        closeBluetoothLocked();
    }
}

static bool sendArtemisBluetoothReport(SDL_GameController* controller,
                                       const uint8_t* report, size_t reportSize)
{
    static const uint8_t magic[8] = { 'A', 'R', 'T', 'B', 'T', '0', '1', 0 };
    QByteArray envelope(static_cast<int>(sizeof(magic) + reportSize), 0);
    std::memcpy(envelope.data(), magic, sizeof(magic));
    std::memcpy(envelope.data() + sizeof(magic), report, reportSize);
    return SDL_GameControllerSendEffect(controller, envelope.constData(), envelope.size()) == 0;
}

bool DualSenseAudioRenderer::sendBluetoothWakeLocked()
{
    if (m_BluetoothController == nullptr) return false;
    std::array<uint8_t, 142> report = {};
    report[0] = 0x32;
    report[1] = (m_BluetoothSequence++ & 0x0f) << 4;
    report[2] = 0x90; report[3] = 63; report[4] = 0xfd; report[5] = 0xf7;
    report[8] = 0x7f; report[9] = 0x7f; report[10] = 0xff; report[11] = 0x09;
    report[13] = 0x0f; report[39] = 0x0a; report[40] = 0x07;
    report[43] = 0x02; report[44] = 0x01; report[46] = 0xff; report[47] = 0xd7;
    const uint32_t crc = bluetoothCrc(report.data(), report.size() - 4);
    for (int i = 0; i < 4; i++) report[report.size() - 4 + i] = (crc >> (i * 8)) & 0xff;
    m_BluetoothWakeSent = sendArtemisBluetoothReport(m_BluetoothController, report.data(), report.size());
    SDL_LogInfo(SDL_LOG_CATEGORY_AUDIO, "DualSense Bluetooth DSP wake: %s",
                m_BluetoothWakeSent ? "ok" : "failed");
    return m_BluetoothWakeSent;
}

bool DualSenseAudioRenderer::sendBluetoothStateLocked()
{
    if (m_BluetoothController == nullptr) return false;
    std::array<uint8_t, 78> report = {};
    report[0] = 0x31;
    report[1] = (m_BluetoothSequence++ & 0x0f) << 4;
    report[2] = 0x10;
    const bool compatibleRumble = m_BluetoothLeftRumble != 0 || m_BluetoothRightRumble != 0;
    // Enable triggers, volume controls and the internal speaker route. 0xd5
    // also makes player/lightbar and mute-LED fields writable.
    report[3] = compatibleRumble ? 0xff : 0xfc;
    report[4] = 0xd5;
    report[5] = m_BluetoothRightRumble;
    report[6] = m_BluetoothLeftRumble;
    report[7] = 0x00; // AUX/headphone volume
    report[8] = 0x64; // internal speaker volume
    report[9] = 0xff; // microphone volume
    report[10] = 0x09; // select the internal speaker DSP route
    report[11] = m_BluetoothMicLed ? 1 : 0;
    std::copy(m_BluetoothRightTrigger.begin(), m_BluetoothRightTrigger.end(), report.begin() + 13);
    std::copy(m_BluetoothLeftTrigger.begin(), m_BluetoothLeftTrigger.end(), report.begin() + 24);
    report[39] = 0x0a; // internal speaker pre-gain
    report[40] = 0x03;
    report[41] = 0x03;
    report[44] = 0x02;
    report[46] = (m_BluetoothPlayerLeds & 0x1f) | 0x20;
    report[47] = m_BluetoothRed;
    report[48] = m_BluetoothGreen;
    report[49] = m_BluetoothBlue;
    const uint32_t crc = bluetoothCrc(report.data(), 74);
    for (int i = 0; i < 4; i++) report[74 + i] = (crc >> (i * 8)) & 0xff;
    return sendArtemisBluetoothReport(m_BluetoothController, report.data(), report.size());
}

bool DualSenseAudioRenderer::isBluetooth()
{
    QMutexLocker locker(&m_Mutex);
    return m_Bluetooth && m_BluetoothController != nullptr;
}

void DualSenseAudioRenderer::setBluetoothRumble(uint16_t lowFrequency, uint16_t highFrequency)
{
    QMutexLocker locker(&m_Mutex);
    if (!m_Bluetooth) return;
    m_BluetoothLeftRumble = static_cast<uint8_t>(std::min(255U, (lowFrequency >> 8) * 2U));
    m_BluetoothRightRumble = static_cast<uint8_t>(std::min(255U, (highFrequency >> 8) * 2U));
    sendBluetoothStateLocked();
}

void DualSenseAudioRenderer::setBluetoothLed(uint8_t red, uint8_t green, uint8_t blue)
{
    QMutexLocker locker(&m_Mutex);
    if (!m_Bluetooth) return;
    m_BluetoothRed = red;
    m_BluetoothGreen = green;
    m_BluetoothBlue = blue;
    sendBluetoothStateLocked();
}

void DualSenseAudioRenderer::toggleBluetoothMicLed()
{
    QMutexLocker locker(&m_Mutex);
    if (!m_Bluetooth) return;
    m_BluetoothMicLed = !m_BluetoothMicLed;
    sendBluetoothStateLocked();
}

void DualSenseAudioRenderer::setBluetoothTriggers(const DualSenseOutputReport* effect)
{
    QMutexLocker locker(&m_Mutex);
    if (!m_Bluetooth || effect == nullptr) return;
    if (effect->validFlag0 & DS_EFFECT_RIGHT_TRIGGER) {
        m_BluetoothRightTrigger[0] = effect->rightTriggerEffectType;
        std::copy(effect->rightTriggerEffect, effect->rightTriggerEffect + DS_EFFECT_PAYLOAD_SIZE,
                  m_BluetoothRightTrigger.begin() + 1);
    }
    if (effect->validFlag0 & DS_EFFECT_LEFT_TRIGGER) {
        m_BluetoothLeftTrigger[0] = effect->leftTriggerEffectType;
        std::copy(effect->leftTriggerEffect, effect->leftTriggerEffect + DS_EFFECT_PAYLOAD_SIZE,
                  m_BluetoothLeftTrigger.begin() + 1);
    }
    if (effect->validFlag2 & 0x01) {
        m_BluetoothPlayerLeds = effect->playerLeds & 0x1f;
        SDL_LogInfo(SDL_LOG_CATEGORY_INPUT,
                    "DualSense Bluetooth player LEDs: mask=0x%02x",
                    m_BluetoothPlayerLeds);
    }
    sendBluetoothStateLocked();
}

bool DualSenseAudioRenderer::sendBluetoothAudioLocked()
{
    if (!m_BluetoothWakeSent && !sendBluetoothWakeLocked()) return false;
    const bool includeSpeaker = m_Mode != StreamingPreferences::DSAM_HAPTICS_ONLY;
    // Report 0x36 is the native combined DualSense BT media container. It must
    // include a complete 63-byte controller state before the haptics and Opus
    // sub-packets. Sending media at offset 13 (as the old 0x35 implementation
    // did) makes the controller interpret PCM as trigger and LED state.
    constexpr size_t reportSize = 398;
    std::array<uint8_t, reportSize> report = {};
    report[0] = 0x36;
    report[1] = (m_BluetoothSequence++ & 0x0f) << 4;
    report[2] = 0x91; report[3] = 7; report[4] = 0xfe;
    std::fill(report.begin() + 5, report.begin() + 10, 96);
    report[10] = m_BluetoothPacketCounter++;

    std::array<uint8_t, 63> state = {};
    const bool compatibleRumble = m_BluetoothLeftRumble != 0 || m_BluetoothRightRumble != 0;
    state[0] = compatibleRumble ? 0xff : 0xfc;
    state[1] = 0xd5;
    state[2] = m_BluetoothRightRumble;
    state[3] = m_BluetoothLeftRumble;
    state[4] = 0x00;
    state[5] = 0x64;
    state[6] = 0xff;
    state[7] = 0x09;
    state[8] = m_BluetoothMicLed ? 1 : 0;
    std::copy(m_BluetoothRightTrigger.begin(), m_BluetoothRightTrigger.end(), state.begin() + 10);
    std::copy(m_BluetoothLeftTrigger.begin(), m_BluetoothLeftTrigger.end(), state.begin() + 21);
    state[36] = 0x0a;
    state[37] = 0x03;
    state[38] = 0x03;
    state[41] = 0x02;
    state[43] = (m_BluetoothPlayerLeds & 0x1f) | 0x20;
    state[44] = m_BluetoothRed;
    state[45] = m_BluetoothGreen;
    state[46] = m_BluetoothBlue;

    report[11] = 0x90; report[12] = 63;
    std::copy(state.begin(), state.end(), report.begin() + 13);
    report[76] = 0x92; report[77] = 64;
    std::memcpy(report.data() + 78, m_HapticReport.data(), m_HapticReport.size());
    report[142] = 0x93; report[143] = 200;
    if (includeSpeaker) {
        std::copy(m_SpeakerOpus.begin(), m_SpeakerOpus.end(), report.begin() + 144);
    }
    const uint32_t crc = bluetoothCrc(report.data(), 394);
    for (int i = 0; i < 4; i++) report[394 + i] = (crc >> (i * 8)) & 0xff;
    return sendArtemisBluetoothReport(m_BluetoothController, report.data(), reportSize);
}

bool DualSenseAudioRenderer::openDeviceLocked()
{
    if (m_Device != 0) {
        return true;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "DualSense audio init failed: %s", SDL_GetError());
        return false;
    }

    const char* deviceName = nullptr;
    const int deviceCount = SDL_GetNumAudioDevices(0);
    for (int i = 0; i < deviceCount; i++) {
        const char* candidate = SDL_GetAudioDeviceName(i, 0);
        if (candidate != nullptr &&
                (SDL_strcasestr(candidate, "DualSense") != nullptr ||
                 SDL_strcasestr(candidate, "Wireless Controller") != nullptr)) {
            deviceName = candidate;
            break;
        }
    }

    if (deviceName == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "DualSense audio endpoint not found among %d devices", deviceCount);
        return false;
    }

    SDL_AudioSpec desired = {};
    desired.freq = kDualSenseSampleRate;
    desired.format = AUDIO_S16LSB;
    desired.channels = kDualSenseChannels;
    desired.samples = 144;

    SDL_AudioSpec obtained = {};
    m_Device = SDL_OpenAudioDevice(deviceName, 0, &desired, &obtained,
                                  SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (m_Device == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                     "Failed to open DualSense audio endpoint '%s' as 4x48k S16: %s",
                     deviceName, SDL_GetError());
        return false;
    }

    if (obtained.freq != desired.freq || obtained.format != desired.format ||
            obtained.channels != desired.channels) {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                     "DualSense endpoint returned unsupported format: %d Hz format=0x%x channels=%d",
                     obtained.freq, obtained.format, obtained.channels);
        SDL_CloseAudioDevice(m_Device);
        m_Device = 0;
        return false;
    }

    SDL_PauseAudioDevice(m_Device, 0);
    SDL_LogInfo(SDL_LOG_CATEGORY_AUDIO,
                "DualSense native audio route active: '%s' 4x48k S16 samples=%d",
                deviceName, obtained.samples);
    return true;
}

void DualSenseAudioRenderer::receive(uint16_t controllerNumber, uint16_t sequence,
                                     uint16_t frameCount, uint8_t channels,
                                     uint8_t flags, const uint8_t* pcm, uint16_t pcmLength)
{
    Q_UNUSED(controllerNumber);
    Q_UNUSED(flags);

    if (pcm == nullptr || channels != kDualSenseChannels || frameCount == 0 ||
            pcmLength != frameCount * kBytesPerFrame) {
        return;
    }

    QMutexLocker locker(&m_Mutex);
    if (m_Mode == StreamingPreferences::DSAM_OFF) {
        return;
    }

    m_PacketsReceived++;
    if (m_ExpectedSequence >= 0 && sequence != static_cast<uint16_t>(m_ExpectedSequence)) {
        m_PacketsLost += static_cast<uint16_t>(sequence - m_ExpectedSequence);
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                    "DualSense audio sequence gap: expected=%u received=%u lost=%llu",
                    static_cast<unsigned>(m_ExpectedSequence), sequence,
                    static_cast<unsigned long long>(m_PacketsLost));
    }
    m_ExpectedSequence = static_cast<uint16_t>(sequence + 1);

    if (m_Bluetooth && m_BluetoothController != nullptr) {
        const bool includeSpeaker = m_Mode != StreamingPreferences::DSAM_HAPTICS_ONLY;
        auto readS16 = [](const uint8_t* bytes) -> int16_t {
            return static_cast<int16_t>(bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8));
        };
        auto filterHaptic = [this](const std::array<double, 127>& channel) {
            double value = 0.0;
            int index = m_HapticRingPosition == 0 ? 126 : m_HapticRingPosition - 1;
            for (size_t tap = 0; tap < m_HapticCoefficients.size(); tap++) {
                value += channel[index] * m_HapticCoefficients[tap];
                if (--index < 0) index = 126;
            }
            return static_cast<int8_t>(std::clamp(static_cast<int>(std::lround(value * 127.0 / 32768.0)),
                                                   -128, 127));
        };

        for (uint16_t frame = 0; frame < frameCount; frame++) {
            const uint8_t* source = pcm + frame * kBytesPerFrame;
            m_SpeakerPcm[m_SpeakerFrames * 2] = includeSpeaker ? readS16(source) : 0;
            m_SpeakerPcm[m_SpeakerFrames * 2 + 1] = includeSpeaker ? readS16(source + 2) : 0;
            m_SpeakerFrames++;

            m_HapticLeft[m_HapticRingPosition] = readS16(source + 4);
            m_HapticRight[m_HapticRingPosition] = readS16(source + 6);
            m_HapticRingPosition = (m_HapticRingPosition + 1) % 127;
            if (++m_HapticDecimationPhase == 16) {
                m_HapticDecimationPhase = 0;
                m_HapticReport[m_HapticReportPosition++] = filterHaptic(m_HapticLeft);
                m_HapticReport[m_HapticReportPosition++] = filterHaptic(m_HapticRight);
            }

            if (m_SpeakerFrames == 512) {
                if (includeSpeaker) {
                    std::array<int16_t, 480 * 2> resampled = {};
                    for (int outputFrame = 0; outputFrame < 480; outputFrame++) {
                        const int numerator = outputFrame * 16;
                        const int sourceFrame = numerator / 15;
                        const int fraction = numerator % 15;
                        const int nextFrame = std::min(sourceFrame + 1, 511);
                        for (int channel = 0; channel < 2; channel++) {
                            const int32_t first = m_SpeakerPcm[sourceFrame * 2 + channel];
                            const int32_t second = m_SpeakerPcm[nextFrame * 2 + channel];
                            resampled[outputFrame * 2 + channel] =
                                static_cast<int16_t>((first * (15 - fraction) + second * fraction) / 15);
                        }
                    }
                    const int encoded = opus_encode(m_BluetoothEncoder, resampled.data(), 480,
                                                    m_SpeakerOpus.data(), m_SpeakerOpus.size());
                    m_SpeakerOpusReady = encoded > 0;
                    if (encoded > 0 && encoded < static_cast<int>(m_SpeakerOpus.size())) {
                        std::fill(m_SpeakerOpus.begin() + encoded, m_SpeakerOpus.end(), 0);
                    }
                }
                else {
                    m_SpeakerOpusReady = false;
                }

                const bool hasHaptics = std::any_of(m_HapticReport.begin(), m_HapticReport.end(),
                                                    [](int8_t value) { return value != 0; });
                if (hasHaptics || m_SpeakerOpusReady) {
                    if (!sendBluetoothAudioLocked()) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "DualSense Bluetooth audio HID write failed");
                    }
                }
                m_SpeakerFrames = 0;
                m_HapticReportPosition = 0;
                m_HapticReport.fill(0);
            }
        }
        return;
    }

    if (!openDeviceLocked()) {
        return;
    }

    QByteArray output(reinterpret_cast<const char*>(pcm), pcmLength);
    for (int offset = 0; offset < output.size(); offset += kBytesPerFrame) {
        if (m_Mode == StreamingPreferences::DSAM_HAPTICS_ONLY) {
            output[offset] = output[offset + 1] = 0;
            output[offset + 2] = output[offset + 3] = 0;
        }
        // Speaker channels 1/2 and native haptics channels 3/4 otherwise pass
        // through bit-identically. The old 30% speaker calibration made the
        // mirrored stream unnecessarily quieter than direct Windows playback.
    }

    if (SDL_GetQueuedAudioSize(m_Device) > kMaximumQueuedBytes) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "DualSense audio queue overrun; dropping stale PCM");
        SDL_ClearQueuedAudio(m_Device);
    }

    if (SDL_QueueAudio(m_Device, output.constData(), output.size()) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "DualSense audio queue failed: %s", SDL_GetError());
    }
}

void DualSenseAudioRenderer::close()
{
    QMutexLocker locker(&m_Mutex);
    if (m_Device != 0) {
        SDL_ClearQueuedAudio(m_Device);
        SDL_CloseAudioDevice(m_Device);
        m_Device = 0;
    }
    closeBluetoothLocked();
    m_ExpectedSequence = -1;
}
