#include "sdl.h"
#include "streaming/audio/dualsenseaudio.h"

#include <Limelight.h>

#include <string>

std::atomic_bool SdlAudioRenderer::s_DualSenseHeadsetRequested { false };

SdlAudioRenderer::SdlAudioRenderer()
    : m_AudioDevice(0),
      m_AudioBuffer(nullptr),
      m_DualSenseHeadsetActive(false)
{
    SDL_assert(!SDL_WasInit(SDL_INIT_AUDIO));

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s",
                     SDL_GetError());
        SDL_assert(SDL_WasInit(SDL_INIT_AUDIO));
    }
}

bool SdlAudioRenderer::prepareForPlayback(const OPUS_MULTISTREAM_CONFIGURATION* opusConfig)
{
    s_DualSenseHeadsetRequested.store(false, std::memory_order_release);
    SDL_AudioSpec want;

    SDL_zero(want);
    want.freq = opusConfig->sampleRate;
    want.format = AUDIO_F32SYS;
    want.channels = opusConfig->channelCount;

    // On PulseAudio systems, setting a value too small can cause underruns for other
    // applications sharing this output device. We impose a floor of 480 samples (10 ms)
    // to mitigate this issue. Otherwise, we will buffer up to 3 frames of audio which
    // is 15 ms at regular 5 ms frames and 30 ms at 10 ms frames for slow connections.
    // The buffering helps avoid audio underruns due to network jitter.
    want.samples = SDL_max(480, opusConfig->samplesPerFrame * 3);

    m_FrameDurationMs = opusConfig->samplesPerFrame / (opusConfig->sampleRate / 1000);
    m_FrameSize = opusConfig->samplesPerFrame *
                  opusConfig->channelCount *
                  getAudioBufferSampleSize();

    // SDL's first enumerated device is not necessarily the Windows default
    // endpoint. Open the system default explicitly instead of accidentally
    // pinning the stream to an arbitrary virtual device (for example Steam
    // Streaming Speakers).
    m_StartupDeviceName.clear();

    m_WantedSpec = want;
    if (!openAudioDevice(false)) {
        return false;
    }

    m_AudioBuffer = SDL_malloc(m_FrameSize);
    if (m_AudioBuffer == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to allocate audio buffer");
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Desired audio buffer: %u samples (%u bytes)",
                want.samples,
                want.samples * want.channels * getAudioBufferSampleSize());

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "SDL audio driver: %s",
                SDL_GetCurrentAudioDriver());

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Main stream audio pinned to startup device: %s",
                m_StartupDeviceName.empty() ? "<system default>" : m_StartupDeviceName.c_str());

    return true;
}

void SdlAudioRenderer::setDualSenseHeadsetActive(bool active)
{
    s_DualSenseHeadsetRequested.store(active, std::memory_order_release);
}

const char* SdlAudioRenderer::findDualSenseAudioDevice() const
{
    const int deviceCount = SDL_GetNumAudioDevices(0);
    for (int i = 0; i < deviceCount; i++) {
        const char* candidate = SDL_GetAudioDeviceName(i, 0);
        if (candidate != nullptr &&
                SDL_strcasestr(candidate, "Apollo Extended") == nullptr &&
                (SDL_strcasestr(candidate, "DualSense") != nullptr ||
                 SDL_strcasestr(candidate, "Wireless Controller") != nullptr)) {
            return candidate;
        }
    }
    return nullptr;
}

bool SdlAudioRenderer::openAudioDevice(bool useDualSenseHeadset)
{
    const char* deviceName = useDualSenseHeadset ? findDualSenseAudioDevice() :
        (m_StartupDeviceName.empty() ? nullptr : m_StartupDeviceName.c_str());
    if (useDualSenseHeadset && deviceName == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "DualSense headset connected, but its Windows audio endpoint was not found");
        return false;
    }

    SDL_AudioSpec have = {};
    SDL_AudioDeviceID newDevice = SDL_OpenAudioDevice(deviceName, 0, &m_WantedSpec, &have, 0);
    if (newDevice == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to open %s audio device '%s': %s",
                     useDualSenseHeadset ? "DualSense headset" : "startup",
                     deviceName != nullptr ? deviceName : "<system default>", SDL_GetError());
        return false;
    }

    SDL_PauseAudioDevice(newDevice, 0);
    if (m_AudioDevice != 0) {
        SDL_ClearQueuedAudio(m_AudioDevice);
        SDL_PauseAudioDevice(m_AudioDevice, 1);
        SDL_CloseAudioDevice(m_AudioDevice);
    }
    m_AudioDevice = newDevice;
    m_DualSenseHeadsetActive = useDualSenseHeadset;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Main stream audio switched to %s: %s",
                useDualSenseHeadset ? "DualSense headset" : "startup output",
                deviceName != nullptr ? deviceName : "<system default>");
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Obtained audio buffer: %u samples (%u bytes)",
                have.samples,
                have.size);
    return true;
}

SdlAudioRenderer::~SdlAudioRenderer()
{
    if (m_AudioDevice != 0) {
        // Stop playback
        SDL_PauseAudioDevice(m_AudioDevice, 1);
        SDL_CloseAudioDevice(m_AudioDevice);
    }

    if (m_AudioBuffer != nullptr) {
        SDL_free(m_AudioBuffer);
    }

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    SDL_assert(!SDL_WasInit(SDL_INIT_AUDIO));
}

void* SdlAudioRenderer::getAudioBuffer(int*)
{
    return m_AudioBuffer;
}

bool SdlAudioRenderer::submitAudio(int bytesWritten)
{
    if (bytesWritten > 0 && DualSenseAudioRenderer::instance().isBluetoothHeadsetActive()) {
        const int frameCount = bytesWritten / (m_WantedSpec.channels * sizeof(float));
        DualSenseAudioRenderer::instance().submitMainAudio(
            static_cast<const float*>(m_AudioBuffer), frameCount, m_WantedSpec.channels);
        return true;
    }

    const bool headsetRequested = s_DualSenseHeadsetRequested.load(std::memory_order_acquire);
    if (headsetRequested != m_DualSenseHeadsetActive) {
        if (!openAudioDevice(headsetRequested)) {
            if (headsetRequested) {
                // Keep the current output and wait for a new physical jack
                // transition instead of retrying an unavailable endpoint for
                // every decoded audio packet.
                s_DualSenseHeadsetRequested.store(false, std::memory_order_release);
            }
            else {
                return false;
            }
        }
    }

    if (bytesWritten == 0) {
        // Nothing to do
        return true;
    }

    // Don't queue if there's already more than 30 ms of audio data waiting
    // in Moonlight's audio queue.
    if (LiGetPendingAudioDuration() > 30) {
        return true;
    }

    // Provide backpressure on the queue to ensure too many frames don't build up
    // in SDL's audio queue, but don't wait forever to avoid a deadlock if the
    // audio device fails.
    for (int i = 0; i < 100; i++) {
        // Our device may enter a permanent error status upon removal, so we need
        // to recreate the audio device to pick up the new default audio device.
        if (SDL_GetAudioDeviceStatus(m_AudioDevice) == SDL_AUDIO_STOPPED) {
            return false;
        }

        // Only queue more samples where there is 50 ms or less in SDL's queue
        if (SDL_GetQueuedAudioSize(m_AudioDevice) / m_FrameSize * m_FrameDurationMs <= 50) {
            break;
        }

        SDL_Delay(1);
    }

    if (SDL_QueueAudio(m_AudioDevice, m_AudioBuffer, bytesWritten) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to queue audio sample: %s",
                     SDL_GetError());
    }

    return true;
}

IAudioRenderer::AudioFormat SdlAudioRenderer::getAudioBufferFormat()
{
    return AudioFormat::Float32NE;
}
