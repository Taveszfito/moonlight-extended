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
    m_BluetoothHeadsetActive = false;
    m_BluetoothMicrophoneEnabled = false;
    m_BluetoothPath.clear();
    m_MainAudioPcm.clear();
    m_SpeakerFrames = 0;
    m_SpeakerOpusReady = false;
    m_HeadsetFramePending = false;
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
    m_BluetoothPath = path != nullptr ? path : "";
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
    report[7] = m_BluetoothHeadsetActive ? 0x7f : 0x00; // AUX/headphone volume
    report[8] = 0x64; // internal speaker volume
    report[9] = 0xff; // microphone volume
    report[10] = m_BluetoothHeadsetActive ? 0x00 : 0x09;
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

std::string DualSenseAudioRenderer::bluetoothPath()
{
    QMutexLocker locker(&m_Mutex);
    return m_Bluetooth ? m_BluetoothPath : std::string();
}

void DualSenseAudioRenderer::setBluetoothMicrophoneEnabled(bool enabled)
{
    QMutexLocker locker(&m_Mutex);
    if (!m_Bluetooth || m_BluetoothMicrophoneEnabled == enabled) return;
    m_BluetoothMicrophoneEnabled = enabled;
    // The microphone uplink is controlled by its own 0x32 media-status report.
    // Do not use a combined 0x36 audio packet here: the controller treats that
    // as a media frame and may reset the Bluetooth session if it contains no
    // correctly framed media payload.
    sendBluetoothMicrophoneKeepaliveLocked();
}

bool DualSenseAudioRenderer::refreshBluetoothMicrophone()
{
    QMutexLocker locker(&m_Mutex);
    return m_Bluetooth && m_BluetoothMicrophoneEnabled;
}

bool DualSenseAudioRenderer::isBluetoothHeadsetActive()
{
    QMutexLocker locker(&m_Mutex);
    return m_Bluetooth && m_BluetoothController != nullptr && m_BluetoothHeadsetActive;
}

void DualSenseAudioRenderer::setBluetoothHeadsetActive(bool active)
{
    QMutexLocker locker(&m_Mutex);
    if (!m_Bluetooth || m_BluetoothController == nullptr || m_BluetoothHeadsetActive == active) {
        return;
    }
    m_BluetoothHeadsetActive = active;
    if (!active) {
        m_MainAudioPcm.clear();
        m_HeadsetFramePending = false;
    }
    sendBluetoothStateLocked();
    SDL_LogInfo(SDL_LOG_CATEGORY_AUDIO, "DualSense Bluetooth headset route %s",
                active ? "enabled" : "disabled");
}

void DualSenseAudioRenderer::submitMainAudio(const float* pcm, int frameCount, int channels)
{
    if (pcm == nullptr || frameCount <= 0 || channels < 1) return;
    QMutexLocker locker(&m_Mutex);
    if (!m_Bluetooth || !m_BluetoothHeadsetActive) return;

    constexpr size_t maximumFrames = 4800;
    while (m_MainAudioPcm.size() / 2 + static_cast<size_t>(frameCount) > maximumFrames) {
        m_MainAudioPcm.pop_front();
        m_MainAudioPcm.pop_front();
    }
    for (int frame = 0; frame < frameCount; frame++) {
        const float* source = pcm + frame * channels;
        float left = source[0];
        float right = channels > 1 ? source[1] : source[0];
        if (channels >= 3) left += source[2] * 0.7071f, right += source[2] * 0.7071f;
        if (channels >= 4) left += source[3] * 0.35f, right += source[3] * 0.35f;
        if (channels >= 6) left += source[4] * 0.5f, right += source[5] * 0.5f;
        if (channels >= 8) left += source[6] * 0.5f, right += source[7] * 0.5f;
        m_MainAudioPcm.push_back(static_cast<int16_t>(std::clamp(left, -1.0f, 1.0f) * 32767.0f));
        m_MainAudioPcm.push_back(static_cast<int16_t>(std::clamp(right, -1.0f, 1.0f) * 32767.0f));
    }

    // The controller consumes each 480-frame Opus block at the cadence of 512
    // host PCM frames. Resampling 512 -> 480 is required to match its wireless
    // media clock; feeding native 480-frame blocks overruns the controller's
    // jitter buffer and produces periodic stutter.
    while (m_MainAudioPcm.size() >= 512 * 2) {
        std::array<int16_t, 512 * 2> sourcePcm = {};
        for (int sample = 0; sample < 512 * 2; sample++) {
            sourcePcm[sample] = m_MainAudioPcm.front();
            m_MainAudioPcm.pop_front();
        }
        std::array<int16_t, 480 * 2> headsetPcm = {};
        for (int outputFrame = 0; outputFrame < 480; outputFrame++) {
            const int numerator = outputFrame * 16;
            const int sourceFrame = numerator / 15;
            const int fraction = numerator % 15;
            const int nextFrame = std::min(sourceFrame + 1, 511);
            for (int channel = 0; channel < 2; channel++) {
                const int32_t first = sourcePcm[sourceFrame * 2 + channel];
                const int32_t second = sourcePcm[nextFrame * 2 + channel];
                headsetPcm[outputFrame * 2 + channel] =
                    static_cast<int16_t>((first * (15 - fraction) + second * fraction) / 15);
            }
        }
        const int encoded = opus_encode(m_BluetoothEncoder, headsetPcm.data(), 480,
                                        m_SpeakerOpus.data(), m_SpeakerOpus.size());
        m_SpeakerOpusReady = encoded > 0;
        if (encoded > 0 && encoded < static_cast<int>(m_SpeakerOpus.size())) {
            std::fill(m_SpeakerOpus.begin() + encoded, m_SpeakerOpus.end(), 0);
        }
        if (m_SpeakerOpusReady) {
            if (!m_HeadsetFramePending) {
                m_HeadsetPendingOpus = m_SpeakerOpus;
                m_HeadsetPendingHaptics = m_HapticReport;
                m_HeadsetFramePending = true;
            }
            else {
                if (!sendBluetoothHeadsetAudioLocked(m_HeadsetPendingOpus,
                                                     m_HeadsetPendingHaptics)) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                                "DualSense Bluetooth headset audio HID write failed");
                }
                m_HeadsetFramePending = false;
            }
        }
        m_HapticReportPosition = 0;
        m_HapticReport.fill(0);
    }
}

bool DualSenseAudioRenderer::sendBluetoothHeadsetAudioLocked(
        const std::array<uint8_t, 200>& firstOpus,
        const std::array<int8_t, 64>& firstHaptics)
{
    if (!m_BluetoothWakeSent && !sendBluetoothWakeLocked()) return false;

    // Native BT headset media container: two 10 ms haptics blocks and two
    // 10 ms Opus blocks in one 20 ms report. Payload type 0x16 selects the
    // controller's 3.5 mm output; 0x13 would select the internal speaker.
    constexpr size_t reportSize = 547;
    std::array<uint8_t, reportSize> report = {};
    report[0] = 0x39;
    report[1] = (m_BluetoothSequence++ & 0x0f) << 4;
    report[2] = 0x91;
    report[3] = 6;
    // Preserve the microphone-uplink bit in every headset media frame.
    // Sending 0x7e after the one-shot 0x32/0x03 enable immediately disables
    // the mic again; the controller then tears down the oscillating BT media
    // session. DS5Dongle uses 0x7f while mic capture is active.
    report[4] = static_cast<uint8_t>(0x7e |
                                     (m_BluetoothMicrophoneEnabled ? 0x01 : 0x00));
    std::fill(report.begin() + 5, report.begin() + 9, 96);
    m_BluetoothPacketCounter = static_cast<uint8_t>(m_BluetoothPacketCounter + 2);
    report[9] = m_BluetoothPacketCounter;

    report[10] = 0xd2;
    report[11] = 64;
    std::memcpy(report.data() + 12, firstHaptics.data(), firstHaptics.size());
    std::memcpy(report.data() + 76, m_HapticReport.data(), m_HapticReport.size());

    report[140] = 0xd6;
    report[141] = 200;
    std::copy(firstOpus.begin(), firstOpus.end(), report.begin() + 142);
    std::copy(m_SpeakerOpus.begin(), m_SpeakerOpus.end(), report.begin() + 342);

    const uint32_t crc = bluetoothCrc(report.data(), report.size() - 4);
    for (int i = 0; i < 4; i++) {
        report[report.size() - 4 + i] = (crc >> (i * 8)) & 0xff;
    }
    return sendArtemisBluetoothReport(m_BluetoothController, report.data(), report.size());
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

void DualSenseAudioRenderer::setBluetoothMicLed(bool enabled)
{
    QMutexLocker locker(&m_Mutex);
    if (!m_Bluetooth) return;
    if (m_BluetoothMicLed == enabled) return;
    m_BluetoothMicLed = enabled;
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
    const bool includeSpeaker = m_Mode != StreamingPreferences::DSAM_HAPTICS_ONLY ||
                                m_BluetoothHeadsetActive;
    // Report 0x36 is the native combined DualSense BT media container. It must
    // include a complete 63-byte controller state before the haptics and Opus
    // sub-packets. Sending media at offset 13 (as the old 0x35 implementation
    // did) makes the controller interpret PCM as trigger and LED state.
    constexpr size_t reportSize = 398;
    std::array<uint8_t, reportSize> report = {};
    report[0] = 0x36;
    report[1] = (m_BluetoothSequence++ & 0x0f) << 4;
    report[2] = 0x91; report[3] = 7;
    report[4] = 0xfe;
    std::fill(report.begin() + 5, report.begin() + 10, 96);
    report[10] = m_BluetoothPacketCounter++;

    std::array<uint8_t, 63> state = {};
    fillBluetoothStateLocked(state);

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

void DualSenseAudioRenderer::fillBluetoothStateLocked(std::array<uint8_t, 63>& state) const
{
    state.fill(0);
    const bool compatibleRumble = m_BluetoothLeftRumble != 0 || m_BluetoothRightRumble != 0;
    state[0] = compatibleRumble ? 0xff : 0xfc;
    state[1] = 0xd5;
    state[2] = m_BluetoothRightRumble;
    state[3] = m_BluetoothLeftRumble;
    state[4] = m_BluetoothHeadsetActive ? 0x7f : 0x00;
    state[5] = 0x64;
    state[6] = 0xff;
    state[7] = m_BluetoothHeadsetActive ? 0x00 : 0x09;
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
}

bool DualSenseAudioRenderer::sendBluetoothMicrophoneKeepaliveLocked()
{
    if (m_BluetoothController == nullptr) return false;
    // Reference DualSense BT implementations enable the microphone with this
    // dedicated 0x32 status report: 0x91 media header, one-byte payload, with
    // bit 0 added to the normal 0x02 status. This changes no controller-state,
    // LED, trigger, speaker or headset fields.
    constexpr size_t reportSize = 142;
    std::array<uint8_t, reportSize> report = {};
    report[0] = 0x32;
    report[1] = (m_BluetoothSequence++ & 0x0f) << 4;
    report[2] = 0x91;
    report[3] = 1;
    report[4] = m_BluetoothMicrophoneEnabled ? 0x03 : 0x02;

    const uint32_t crc = bluetoothCrc(report.data(), reportSize - 4);
    for (int i = 0; i < 4; i++) report[reportSize - 4 + i] = (crc >> (i * 8)) & 0xff;
    return sendArtemisBluetoothReport(m_BluetoothController, report.data(), report.size());
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
        // Haptics-only suppresses the controller-speaker program channel, but
        // a connected headset still needs an Opus carrier for Moonlight's main
        // stereo stream mixed in below.
        const bool includeControllerSpeaker = !m_BluetoothHeadsetActive &&
                                              m_Mode != StreamingPreferences::DSAM_HAPTICS_ONLY;
        const bool includeSpeaker = includeControllerSpeaker || m_BluetoothHeadsetActive;
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
            m_SpeakerPcm[m_SpeakerFrames * 2] = includeControllerSpeaker ? readS16(source) : 0;
            m_SpeakerPcm[m_SpeakerFrames * 2 + 1] = includeControllerSpeaker ? readS16(source + 2) : 0;
            m_SpeakerFrames++;

            m_HapticLeft[m_HapticRingPosition] = readS16(source + 4);
            m_HapticRight[m_HapticRingPosition] = readS16(source + 6);
            m_HapticRingPosition = (m_HapticRingPosition + 1) % 127;
            if (++m_HapticDecimationPhase == 16) {
                m_HapticDecimationPhase = 0;
                if (m_HapticReportPosition + 1 < static_cast<int>(m_HapticReport.size())) {
                    m_HapticReport[m_HapticReportPosition++] = filterHaptic(m_HapticLeft);
                    m_HapticReport[m_HapticReportPosition++] = filterHaptic(m_HapticRight);
                }
            }

            if (m_SpeakerFrames == 512) {
                if (m_BluetoothHeadsetActive) {
                    // Main stream audio owns the BT Opus clock in headset mode.
                    // Apollo contributes only channels 3/4 to m_HapticReport.
                    m_SpeakerFrames = 0;
                    continue;
                }
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
