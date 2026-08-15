/*
 * MicCapture implementation -- Logan's exact architecture.
 *
 * Threading model:
 *   audioCallback   (RT thread)  : try-catch + delegate to handleAudioData ONLY
 *   handleAudioData (RT thread)  : std::mutex + insert + 12-frame cap + notify_one
 *   encoderLoop     (normal thread): wait + drain + sleep_until pacer + encode + send
 *
 * Root cause of SIGABRT on Steam Deck: sleep_until was in SDL callback or
 * handleAudioData (RT thread). Must be in encoderLoop (normal std::thread).
 * std::mutex in handleAudioData IS safe -- PipeWire only forbids SDL calls
 * and sleep in the RT callback.
 */
#include "miccapture.h"
#include "dualsenseaudio.h"
#include <cctype>
#include <cstring>
#include <fstream>

#include <Limelight.h>
#include <SDL_log.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define MIC_PACKET_TYPE 0x3003

static std::atomic_bool s_MicrophoneMuted{false};

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

MicCapture::MicCapture()
{
}

MicCapture::~MicCapture()
{
    stop();
    m_StopEncoderThread.store(true, std::memory_order_release);
    m_BufferCondition.notify_all();
    if (m_EncoderThread.joinable()) m_EncoderThread.join();
    if (m_DeviceId != 0) { SDL_CloseAudioDevice(m_DeviceId); m_DeviceId = 0; }
    if (m_Encoder != nullptr) { opus_encoder_destroy(m_Encoder); m_Encoder = nullptr; }
    // Balance the SDL_InitSubSystem from start(). Safe here: the device is closed and
    // the encoder thread has been joined above.
    if (m_AudioSubsystemInit) { SDL_QuitSubSystem(SDL_INIT_AUDIO); m_AudioSubsystemInit = false; }
}

// ---------------------------------------------------------------------------
// setBitrate
// ---------------------------------------------------------------------------

void MicCapture::setBitrate(int bitrate)
{
    if (bitrate < 6000) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] micBitrate %d clamped to minimum 6000", bitrate);
        bitrate = 6000;
    } else if (bitrate > 96000) {
        // The ceiling comes from the wire format, not Opus: kMaxPacketSize is 248 bytes
        // and a frame is 20 ms, so 248 * 8 / 0.02 = 99.2 kbps is the most that can fit.
        // Anything higher was silently capped by the encode buffer, so clamp to a round
        // 96 kbps and advertise that same number in the UI and docs.
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] micBitrate %d clamped to maximum 96000 (248-byte packet limit at 20ms)", bitrate);
        bitrate = 96000;
    }
    m_Bitrate = bitrate;
}

// ---------------------------------------------------------------------------
// start  (combines initialize + start from Logan's design)
// ---------------------------------------------------------------------------

bool MicCapture::start()
{
    try {
        // Defensively close any previously open SDL device before opening a new one.
        // Handles the zombie case: if the Deck lid closed mid-stream without a clean
        // disconnect, SDL_CloseAudioDevice was never called. On reconnect, the stale
        // device handle causes capture failures or silent mic.
        if (m_DeviceId != 0) {
            SDL_CloseAudioDevice(m_DeviceId);
            m_DeviceId = 0;
        }

        // Init once per object and release once in the destructor. start() is called
        // again on every reconnect, so an unguarded init leaked a subsystem reference
        // per session.
        if (!m_AudioSubsystemInit) {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "[mic] SDL_InitSubSystem: %s -- streaming without mic", SDL_GetError());
                return false;
            }
            m_AudioSubsystemInit = true;
        }

        // Stop and join any previous encoder thread BEFORE destroying the encoder.
        // The encoder thread uses m_Encoder for opus_encode(); destroying m_Encoder
        // while the thread is still running is a data race. Joining first ensures the
        // thread has exited before we touch the encoder allocation.
        if (m_EncoderThread.joinable()) {
            m_StopEncoderThread.store(true, std::memory_order_release);
            m_BufferCondition.notify_all();
            m_EncoderThread.join();
        }

        // Destroy any previous encoder before creating a new one.
        // start() is called on every reconnect; without this, each reconnect leaks
        // the previous OpusEncoder allocation.
        if (m_Encoder != nullptr) {
            opus_encoder_destroy(m_Encoder);
            m_Encoder = nullptr;
        }

        m_BluetoothDualSense = false;
        m_WiredDualSenseGain = false;
        if (m_DeviceName == "__dualsense__" &&
                DualSenseAudioRenderer::instance().isBluetooth()) {
            if (!registerBluetoothMicrophoneCallback()) {
                return false;
            }

            clearBufferedSamples();
            m_FirstPacketLogged = false;
            m_MicSeq = 0;
            m_BluetoothDualSense = true;
            m_StopEncoderThread.store(false, std::memory_order_release);
            m_Streaming.store(true, std::memory_order_release);
            m_EncoderThread = std::thread(&MicCapture::bluetoothDualSenseLoop, this);
            DualSenseAudioRenderer::instance().setBluetoothMicrophoneEnabled(true);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic-bt] DualSense raw Bluetooth microphone capture started");
            return true;
        }

        // The Windows USB audio endpoint of a wired DualSense is noticeably
        // quieter than ordinary PC microphones. Apply a modest +6 dB boost in
        // the non-real-time encoder thread. Bluetooth packets are forwarded
        // bit-identically above and never enter this path.
        m_WiredDualSenseGain = m_DeviceName == "__dualsense__";

        int opusError = OPUS_OK;
        m_Encoder = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_VOIP, &opusError);
        if (!m_Encoder || opusError != OPUS_OK) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] opus_encoder_create: %s -- streaming without mic",
                        opus_strerror(opusError));
            return false;
        }

        // Apply encoder settings; log any ctl failures rather than silently ignoring them.
        auto applyCtl = [&](int result, const char* name) {
            if (result != OPUS_OK)
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "[mic] opus_encoder_ctl %s failed: %s", name, opus_strerror(result));
        };
        applyCtl(opus_encoder_ctl(m_Encoder, OPUS_SET_BITRATE(m_Bitrate)), "BITRATE");
        applyCtl(opus_encoder_ctl(m_Encoder, OPUS_SET_VBR(1)),             "VBR");
        applyCtl(opus_encoder_ctl(m_Encoder, OPUS_SET_COMPLEXITY(10)),     "COMPLEXITY");
        applyCtl(opus_encoder_ctl(m_Encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE)), "SIGNAL");
        applyCtl(opus_encoder_ctl(m_Encoder, OPUS_SET_LSB_DEPTH(16)),      "LSB_DEPTH");
        // DTX: Opus sends no packets during silence; server-side PLC handles gaps gracefully.
        applyCtl(opus_encoder_ctl(m_Encoder, OPUS_SET_DTX(1)),             "DTX");
        applyCtl(opus_encoder_ctl(m_Encoder, OPUS_SET_INBAND_FEC(1)),      "INBAND_FEC");
        applyCtl(opus_encoder_ctl(m_Encoder, OPUS_SET_PACKET_LOSS_PERC(5)),"PACKET_LOSS_PERC");
        applyCtl(opus_encoder_ctl(m_Encoder, OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_20_MS)),
                                                                            "FRAME_DURATION");

        SDL_AudioSpec desired = {};
        desired.freq     = kSampleRate;
        desired.format   = AUDIO_S16SYS;
        desired.channels = kChannels;
        desired.samples  = kFrameSize;
        desired.callback = &MicCapture::audioCallback;
        desired.userdata = this;

        // Log all available capture devices at DEBUG so stream logs show what PipeWire sees.
        {
            int numDevices = SDL_GetNumAudioDevices(1);
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                         "[mic] %d capture device(s) available at stream start:", numDevices);
            for (int i = 0; i < numDevices; i++) {
                SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                             "[mic] available capture device %d: %s",
                             i, SDL_GetAudioDeviceName(i, 1));
            }
        }

        std::string resolvedDeviceName = m_DeviceName;
        if (resolvedDeviceName == "__dualsense__") {
            resolvedDeviceName.clear();
            const int count = SDL_GetNumAudioDevices(1);
            for (int i = 0; i < count; i++) {
                const char* candidate = SDL_GetAudioDeviceName(i, 1);
                if (candidate == nullptr) continue;
                std::string lowered(candidate);
                std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                               [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (lowered.find("dualsense") != std::string::npos ||
                    lowered.find("wireless controller") != std::string::npos) {
                    resolvedDeviceName = candidate;
                    break;
                }
            }
            if (resolvedDeviceName.empty()) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "[mic] DualSense microphone endpoint not found -- streaming without mic");
                return false;
            }
        }
        const char* dev = resolvedDeviceName.empty() ? nullptr : resolvedDeviceName.c_str();
        m_DeviceId = SDL_OpenAudioDevice(dev, 1, &desired, &m_ObtainedSpec, 0);
        if (m_DeviceId == 0 && dev != nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] Named device '%s' failed -- falling back to default", dev);
            m_DeviceId = SDL_OpenAudioDevice(nullptr, 1, &desired, &m_ObtainedSpec, 0);
        }
        if (m_DeviceId == 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] SDL_OpenAudioDevice: %s -- streaming without mic", SDL_GetError());
            opus_encoder_destroy(m_Encoder);
            m_Encoder = nullptr;
            return false;
        }

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] Opened %s @ %dHz %dch fmt=0x%x samples=%d",
                    dev ? dev : "<default>",
                    m_ObtainedSpec.freq, m_ObtainedSpec.channels,
                    m_ObtainedSpec.format, m_ObtainedSpec.samples);

        if (m_ObtainedSpec.freq != kSampleRate || m_ObtainedSpec.channels != kChannels ||
            m_ObtainedSpec.format != AUDIO_S16SYS) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] Format mismatch: got %dHz %dch fmt=0x%x -- continuing anyway",
                        m_ObtainedSpec.freq, m_ObtainedSpec.channels, m_ObtainedSpec.format);
        }
        if (m_ObtainedSpec.samples != kFrameSize) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] Backend %d-sample callbacks, Opus paced at %d (%dms)",
                        m_ObtainedSpec.samples, kFrameSize, (kFrameSize * 1000) / kSampleRate);
        }

        // Pause device until streaming is armed
        SDL_PauseAudioDevice(m_DeviceId, 1);
        m_SampleBuffer.reserve(kFrameSize * 4);

        // (Encoder thread already joined above, before encoder was destroyed.)
        m_StopEncoderThread.store(false, std::memory_order_release);
        m_EncoderThread = std::thread(&MicCapture::encoderLoop, this);

        // Arm streaming and unpause SDL
        clearBufferedSamples();
        m_FirstPacketLogged = false;
        m_MicSeq = 0;
        m_Streaming.store(true, std::memory_order_release);
        m_BufferCondition.notify_all();
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[mic] Streaming started");
        SDL_PauseAudioDevice(m_DeviceId, 0);
        return true;

    } catch (...) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] init threw exception -- streaming without mic");
        if (m_EncoderThread.joinable()) {
            m_StopEncoderThread.store(true, std::memory_order_release);
            m_BufferCondition.notify_all();
            m_EncoderThread.join();
        }
        if (m_Encoder) { opus_encoder_destroy(m_Encoder); m_Encoder = nullptr; }
        if (m_DeviceId != 0) { SDL_CloseAudioDevice(m_DeviceId); m_DeviceId = 0; }
        return false;
    }
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------

void MicCapture::stop()
{
    if (m_DeviceId != 0) SDL_PauseAudioDevice(m_DeviceId, 1);
    m_Streaming.store(false, std::memory_order_release);
    m_StopEncoderThread.store(true, std::memory_order_release);
    clearBufferedSamples();
    m_BufferCondition.notify_all();
    if (m_EncoderThread.joinable()) {
        m_EncoderThread.join();
    }
    if (m_BluetoothDualSense) {
        DualSenseAudioRenderer::instance().setBluetoothMicrophoneEnabled(false);
        unregisterBluetoothMicrophoneCallback();
        m_BluetoothDualSense = false;
    }
    // Close the audio device explicitly at stream end so that a subsequent start()
    // re-opens it at stream-start time, picking up hotplugged or newly-active devices.
    if (m_DeviceId != 0) {
        SDL_CloseAudioDevice(m_DeviceId);
        m_DeviceId = 0;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[mic] Stopped");
}

void MicCapture::bluetoothDualSenseLoop()
{
    // Bluetooth DualSense microphone reports already contain a 48 kHz mono,
    // 10 ms Opus packet. Forward it bit-identically instead of decoding and
    // re-encoding it, which preserves quality and avoids extra latency.
    std::array<uint8_t, 4 + 71> frame = {};
    uint32_t packetsSent = 0;
    bool diagnosticsWritten = false;

    for (;;) {
        std::array<uint8_t, 71> opus = {};
        {
            std::unique_lock<std::mutex> lock(m_BufferMutex);
            m_BufferCondition.wait_for(lock, std::chrono::seconds(1), [this] {
                return m_StopEncoderThread.load(std::memory_order_acquire) ||
                       !m_BluetoothPackets.empty();
            });
            if (m_StopEncoderThread.load(std::memory_order_acquire)) break;
            if (m_BluetoothPackets.empty()) {
                if (!diagnosticsWritten && m_GetNumberProperty != nullptr) {
                    diagnosticsWritten = true;
                    std::ofstream diagnostic("C:\\ArtemisDev\\logs\\bt-mic-live.txt",
                                             std::ios::trunc);
                    diagnostic << "raw31=" << m_GetNumberProperty(m_SdlGlobalProperties,
                        "Artemis.DualSenseBluetoothMicrophone.Raw31Count", -1) << '\n';
                    diagnostic << "lastSize=" << m_GetNumberProperty(m_SdlGlobalProperties,
                        "Artemis.DualSenseBluetoothMicrophone.Last31Size", -1) << '\n';
                    diagnostic << "lastTag=" << m_GetNumberProperty(m_SdlGlobalProperties,
                        "Artemis.DualSenseBluetoothMicrophone.Last31Tag", -1) << '\n';
                    diagnostic << "callbacks=" << m_BluetoothCallbackCount.load() << '\n';
                }
                continue;
            }
            opus = m_BluetoothPackets.front();
            m_BluetoothPackets.pop_front();
        }

            if (isMuted()) {
                continue;
            }

            frame[0] = static_cast<uint8_t>(m_MicSeq >> 8);
            frame[1] = static_cast<uint8_t>(m_MicSeq & 0xff);
            frame[2] = 1;
            frame[3] = 0;
            std::copy(opus.begin(), opus.end(), frame.begin() + 4);
            m_MicSeq++;

            const int result = LiSendRawControlStreamPacket(
                MIC_PACKET_TYPE, reinterpret_cast<char*>(frame.data()),
                static_cast<int>(frame.size()));
            if (result == 0) {
                packetsSent++;
                if (!m_FirstPacketLogged) {
                    m_FirstPacketLogged = true;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "[mic-bt] First 71-byte controller Opus packet forwarded");
                }
                if ((packetsSent % 100) == 0) {
                    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                                 "[mic-bt] forwarded=%u seq=%u", packetsSent,
                                 static_cast<unsigned>(m_MicSeq));
                }
        }
    }
}

bool MicCapture::registerBluetoothMicrophoneCallback()
{
#ifdef _WIN32
    static constexpr const char* CallbackProperty =
        "Artemis.DualSenseBluetoothMicrophone.Callback";
    static constexpr const char* UserdataProperty =
        "Artemis.DualSenseBluetoothMicrophone.Userdata";
    HMODULE sdl3 = GetModuleHandleW(L"SDL3.dll");
    if (sdl3 == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[mic-bt] SDL3.dll is not loaded");
        return false;
    }
    auto getGlobalProperties = reinterpret_cast<GetGlobalProperties>(
        GetProcAddress(sdl3, "SDL_GetGlobalProperties"));
    m_SetPointerProperty = reinterpret_cast<SetPointerProperty>(
        GetProcAddress(sdl3, "SDL_SetPointerProperty"));
    m_ClearProperty = reinterpret_cast<ClearProperty>(
        GetProcAddress(sdl3, "SDL_ClearProperty"));
    m_GetNumberProperty = reinterpret_cast<GetNumberProperty>(
        GetProcAddress(sdl3, "SDL_GetNumberProperty"));
    if (getGlobalProperties == nullptr || m_SetPointerProperty == nullptr ||
            m_ClearProperty == nullptr || m_GetNumberProperty == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic-bt] SDL property API is unavailable");
        return false;
    }
    m_SdlGlobalProperties = getGlobalProperties();
    if (m_SdlGlobalProperties == 0 ||
            !m_SetPointerProperty(m_SdlGlobalProperties, UserdataProperty, this) ||
            !m_SetPointerProperty(m_SdlGlobalProperties, CallbackProperty,
                                  reinterpret_cast<void*>(&MicCapture::bluetoothMicrophoneCallback))) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic-bt] Failed to register SDL microphone properties");
        unregisterBluetoothMicrophoneCallback();
        return false;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[mic-bt] Registered on SDL's existing DualSense HID reader");
    return true;
#else
    return false;
#endif
}

void MicCapture::setMuted(bool muted)
{
    s_MicrophoneMuted.store(muted, std::memory_order_release);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[mic] Client forwarding %s",
                muted ? "muted by DualSense button" : "unmuted");
}

bool MicCapture::isMuted()
{
    return s_MicrophoneMuted.load(std::memory_order_acquire);
}

void MicCapture::unregisterBluetoothMicrophoneCallback()
{
    static constexpr const char* CallbackProperty =
        "Artemis.DualSenseBluetoothMicrophone.Callback";
    static constexpr const char* UserdataProperty =
        "Artemis.DualSenseBluetoothMicrophone.Userdata";
    if (m_ClearProperty != nullptr && m_SdlGlobalProperties != 0) {
        // Remove the callable pointer first, so the SDL input thread can no
        // longer enter this object while teardown clears its userdata.
        m_ClearProperty(m_SdlGlobalProperties, CallbackProperty);
        m_ClearProperty(m_SdlGlobalProperties, UserdataProperty);
    }
    m_SdlGlobalProperties = 0;
    m_SetPointerProperty = nullptr;
    m_ClearProperty = nullptr;
    m_GetNumberProperty = nullptr;
}

void SDLCALL MicCapture::bluetoothMicrophoneCallback(void* userdata,
                                                     const Uint8* opus, int length)
{
    auto* capture = static_cast<MicCapture*>(userdata);
    if (capture == nullptr || opus == nullptr || length != 71 ||
            !capture->m_Streaming.load(std::memory_order_acquire)) return;
    capture->m_BluetoothCallbackCount.fetch_add(1, std::memory_order_relaxed);
    std::array<uint8_t, 71> packet = {};
    std::copy_n(opus, packet.size(), packet.begin());
    {
        std::lock_guard<std::mutex> lock(capture->m_BufferMutex);
        if (capture->m_BluetoothPackets.size() >= 12) {
            capture->m_BluetoothPackets.pop_front();
        }
        capture->m_BluetoothPackets.push_back(packet);
    }
    capture->m_BufferCondition.notify_one();
}

// ---------------------------------------------------------------------------
// audioCallback -- RT THREAD -- 3 lines only: try-catch + delegate
// Forbidden here: SDL calls, sleep, malloc, mutex lock (except via handleAudioData),
// opus_encode, LiSendRawControlStreamPacket, logging.
// ---------------------------------------------------------------------------

// static
void SDLCALL MicCapture::audioCallback(void* userdata, Uint8* stream, int len)
{
    try {
        auto* capture = static_cast<MicCapture*>(userdata);
        if (capture != nullptr) capture->handleAudioData(stream, len);
    } catch (...) { /* never let exceptions escape SDL audio callback */ }
}

// ---------------------------------------------------------------------------
// handleAudioData -- called from RT thread
// Only: mutex + insert + 12-frame cap + notify
// std::mutex IS safe here -- PipeWire only forbids SDL calls and sleep
// ---------------------------------------------------------------------------

void MicCapture::handleAudioData(const Uint8* stream, int len)
{
    if (!m_Streaming.load(std::memory_order_acquire) || !stream || len <= 0) return;
    const auto* samples = reinterpret_cast<const opus_int16*>(stream);
    const int raw_count = len / (int)sizeof(opus_int16);

    std::lock_guard<std::mutex> lock(m_BufferMutex);
    if (m_ObtainedSpec.channels == 2 && kChannels == 1) {
        // PipeWire may deliver stereo even when mono was requested.
        // Downmix L+R to mono by averaging each pair before encoding.
        for (int i = 0; i + 1 < raw_count; i += 2) {
            opus_int16 mono = (opus_int16)(((int)samples[i] + (int)samples[i + 1]) / 2);
            m_SampleBuffer.push_back(mono);
        }
    } else {
        m_SampleBuffer.insert(m_SampleBuffer.end(), samples, samples + raw_count);
    }
    constexpr size_t maxSamples = kFrameSize * kChannels * 12;
    if (m_SampleBuffer.size() > maxSamples) {
        auto trim = m_SampleBuffer.size() - maxSamples;
        m_SampleBuffer.erase(m_SampleBuffer.begin(),
                             m_SampleBuffer.begin() + (int)trim);
    }
    m_BufferCondition.notify_one();
}

// ---------------------------------------------------------------------------
// encoderLoop -- normal priority thread
// All encoding / sending / sleeping live here.
// sleep_until is SAFE here (normal std::thread, not RT callback).
// ---------------------------------------------------------------------------

void MicCapture::encoderLoop()
{
    // Wrap entire body: any uncaught exception would call std::terminate -> SIGABRT.
    // Known crash path: oversized Opus packet -> LiSendRawControlStreamPacket ->
    // sendMessageEnet -> __memcpy_chk(tempBuffer[256], payload, paylen, 252) -> abort()
    // when paylen > 252. Guard below prevents that, but try-catch is a second layer.
    try {

    if (!m_Encoder || m_DeviceId == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[mic] encoderLoop: encoder or device not ready, exiting");
        return;
    }

    const int kFrameElements = kFrameSize * kChannels;
    std::vector<opus_int16> frame((size_t)kFrameElements);
    const auto frameDuration =
        std::chrono::milliseconds((kFrameSize * 1000) / kSampleRate);
    auto nextSendDeadline = std::chrono::steady_clock::now();
    bool pacingActive = false;

    uint32_t packetsSent = 0;
    uint32_t packetsDropped = 0;
    uint32_t encodeErrors = 0;

    for (;;) {
        {
            std::unique_lock<std::mutex> lock(m_BufferMutex);
            m_BufferCondition.wait(lock, [this, kFrameElements] {
                return m_StopEncoderThread.load(std::memory_order_acquire) ||
                       (m_Streaming.load(std::memory_order_acquire) &&
                        (int)m_SampleBuffer.size() >= kFrameElements);
            });
            if (m_StopEncoderThread.load(std::memory_order_acquire)) break;
            if (!m_Streaming.load(std::memory_order_acquire) ||
                (int)m_SampleBuffer.size() < kFrameElements) {
                pacingActive = false;
                continue;
            }
            std::copy_n(m_SampleBuffer.begin(), kFrameElements, frame.begin());
            m_SampleBuffer.erase(m_SampleBuffer.begin(),
                                 m_SampleBuffer.begin() + kFrameElements);
        }

        if (m_WiredDualSenseGain) {
            for (opus_int16& sample : frame) {
                const int amplified = static_cast<int>(sample) * 4;
                sample = static_cast<opus_int16>(std::clamp(amplified, -32768, 32767));
            }
        }

        // Pacer -- SAFE: encoderLoop is a normal std::thread, not RT callback
        const auto now = std::chrono::steady_clock::now();
        if (!pacingActive) {
            nextSendDeadline = now;
            pacingActive = true;
        } else if (now > nextSendDeadline + (frameDuration * 2)) {
            nextSendDeadline = now; // re-sync after gap
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                         "[mic] pacer re-sync at seq=%u (gap detected)", (unsigned)m_MicSeq);
        }
        if (nextSendDeadline > now) {
            std::this_thread::sleep_until(nextSendDeadline);
        }
        nextSendDeadline += frameDuration;

        if (isMuted()) {
            continue;
        }

        int encodedBytes = opus_encode(m_Encoder,
                                       frame.data(),
                                       kFrameSize,
                                       m_EncodedPacket.data(),
                                       (opus_int32)m_EncodedPacket.size());
        if (encodedBytes <= 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] opus_encode error: %s -- skipping frame",
                        opus_strerror(encodedBytes));
            encodeErrors++;
            continue;
        }

        // Hard guard: sendMessageEnet uses a fixed char tempBuffer[256].
        // sizeof(NVCTL_ENET_PACKET_HEADER_V2)=4 leaves 252 bytes for payload.
        // __memcpy_chk calls abort() if paylen > 252. kMaxPacketSize=248 limits
        // what opus_encode produces, but this runtime check is the safety net.
        if (encodedBytes > kMaxPacketSize) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] Oversized Opus packet %d bytes > %d limit -- dropping frame",
                        encodedBytes, kMaxPacketSize);
            packetsDropped++;
            continue;
        }

        // Reuse m_FrameBuffer (pre-sized member) to avoid per-packet heap allocation.
        // 4-byte wire format header: [seq_hi, seq_lo, ch=1, flags=0] + Opus payload.
        m_FrameBuffer[0] = static_cast<uint8_t>(m_MicSeq >> 8);
        m_FrameBuffer[1] = static_cast<uint8_t>(m_MicSeq & 0xFF);
        m_FrameBuffer[2] = 1;  // ch = 1 (mono)
        m_FrameBuffer[3] = 0;  // flags = 0 (reserved)
        std::memcpy(m_FrameBuffer.data() + 4, m_EncodedPacket.data(), encodedBytes);
        m_MicSeq++;

        int result = LiSendRawControlStreamPacket(
            MIC_PACKET_TYPE,
            (char*)m_FrameBuffer.data(),
            static_cast<int>(4 + encodedBytes));
        if (result == 0) {
            packetsSent++;
            if (!m_FirstPacketLogged) {
                m_FirstPacketLogged = true;
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "[mic] Sent first microphone packet (%d bytes Opus + 4 header)", encodedBytes);
            }
            if (packetsSent % 50 == 0) {
                SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                             "[mic] send stats: sent=%u dropped=%u encodeErr=%u seq=%u",
                             packetsSent, packetsDropped, encodeErrors, (unsigned)m_MicSeq);
            }
        } else if (result != 0 && result != LI_ERR_UNSUPPORTED) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[mic] LiSendRawControlStreamPacket returned %d", result);
        }
    }

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[mic] encoderLoop exception: %s -- thread exiting", e.what());
    } catch (...) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[mic] encoderLoop unknown exception -- thread exiting");
    }
}

// ---------------------------------------------------------------------------
// clearBufferedSamples
// ---------------------------------------------------------------------------

void MicCapture::clearBufferedSamples()
{
    std::lock_guard<std::mutex> lock(m_BufferMutex);
    m_SampleBuffer.clear();
    m_BluetoothPackets.clear();
}
