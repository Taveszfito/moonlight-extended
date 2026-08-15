#pragma once

#include "renderer.h"
#include "SDL_compat.h"

#include <atomic>
#include <string>

class SdlAudioRenderer : public IAudioRenderer
{
public:
    SdlAudioRenderer();

    virtual ~SdlAudioRenderer();

    virtual bool prepareForPlayback(const OPUS_MULTISTREAM_CONFIGURATION* opusConfig);

    virtual void* getAudioBuffer(int* size);

    virtual bool submitAudio(int bytesWritten);

    virtual AudioFormat getAudioBufferFormat();

    // Requests that Moonlight's main stream audio move to/from the physical
    // DualSense endpoint. The audio thread performs the actual device reopen.
    static void setDualSenseHeadsetActive(bool active);

private:
    bool openAudioDevice(bool useDualSenseHeadset);
    const char* findDualSenseAudioDevice() const;

    static std::atomic_bool s_DualSenseHeadsetRequested;

    SDL_AudioDeviceID m_AudioDevice;
    void* m_AudioBuffer;
    Uint32 m_FrameSize;
    Uint32 m_FrameDurationMs;
    SDL_AudioSpec m_WantedSpec;
    std::string m_StartupDeviceName;
    bool m_DualSenseHeadsetActive;
};
