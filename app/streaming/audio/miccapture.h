#pragma once

/*
 * MicCapture -- SDL2-based microphone capture with Opus encoding.
 *
 * Logan's architecture (logabell/moonlight-qt-mic):
 *
 *   audioCallback   (RT thread)  : ONLY try-catch + delegate to handleAudioData
 *   handleAudioData (RT thread)  : std::mutex + insert samples + 12-frame cap + notify
 *   encoderLoop     (normal thread): wait + drain frame + sleep_until pacer + encode + send
 *
 * std::mutex in handleAudioData IS safe -- PipeWire only forbids SDL calls
 * and sleep in the RT callback, not mutex locks.
 * sleep_until in encoderLoop is safe -- it is a normal std::thread.
 */

#include <SDL.h>
#include <opus.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class MicCapture
{
public:
    MicCapture();
    ~MicCapture();

    static void setMuted(bool muted);
    static bool isMuted();

    void setDeviceName(const std::string& name) { m_DeviceName = name; }
    void setBitrate(int bitrate);

    bool start();
    void stop();

private:
    // Constants
    static constexpr int kSampleRate    = 48000;
    static constexpr int kChannels      = 1;   // mono -- must match Vibepollo MIC_CHANNELS=1 decoder
    static constexpr int kFrameSize     = 960;   // 20ms at 48kHz (per channel)
    // moonlight-common-c sendMessageEnet uses a fixed char tempBuffer[256] when
    // encryptedControlStream is true. sizeof(NVCTL_ENET_PACKET_HEADER_V2)=4, leaving
    // exactly 252 bytes of payload. If paylen > 252, __memcpy_chk calls abort() -> SIGABRT.
    // Use 248 (the true safe ceiling: 252 - 4-byte header) so VBR bitrate spikes don't
    // silently drop frames. Opus at 128kbps/20ms averages ~320 bytes WITHOUT VBR — with
    // VBR the per-packet size stays well below 248 on voice content.
    static constexpr int kMaxPacketSize  = 248;
    static constexpr int kDefaultBitrate = 64000; // 64kbps — well-tuned Opus VOIP mode, matches logabell/Apollo

    // Internal methods
    static void SDLCALL audioCallback(void* userdata, Uint8* stream, int len);
    void handleAudioData(const Uint8* stream, int len);
    void encoderLoop();
    void bluetoothDualSenseLoop();
    static void SDLCALL bluetoothMicrophoneCallback(void* userdata,
                                                    const Uint8* opus, int length);
    bool registerBluetoothMicrophoneCallback();
    void unregisterBluetoothMicrophoneCallback();
    void clearBufferedSamples();

    // State
    // Tracks a successful SDL_InitSubSystem(SDL_INIT_AUDIO) so it is balanced exactly
    // once by SDL_QuitSubSystem in the destructor. start() runs again on every
    // reconnect; without this the subsystem refcount grew per session.
    bool m_AudioSubsystemInit = false;
    uint16_t m_MicSeq = 0;
    bool m_FirstPacketLogged = false;
    SDL_AudioDeviceID m_DeviceId = 0;
    SDL_AudioSpec m_ObtainedSpec = {};
    OpusEncoder* m_Encoder = nullptr;
    bool m_BluetoothDualSense = false;
    bool m_WiredDualSenseGain = false;
    std::deque<std::array<uint8_t, 71>> m_BluetoothPackets;
    using BluetoothMicCallback = void (SDLCALL *)(void*, const Uint8*, int);
    using GetGlobalProperties = Uint32 (SDLCALL *)();
    using SetPointerProperty = bool (SDLCALL *)(Uint32, const char*, void*);
    using ClearProperty = bool (SDLCALL *)(Uint32, const char*);
    using GetNumberProperty = Sint64 (SDLCALL *)(Uint32, const char*, Sint64);
    Uint32 m_SdlGlobalProperties = 0;
    SetPointerProperty m_SetPointerProperty = nullptr;
    ClearProperty m_ClearProperty = nullptr;
    GetNumberProperty m_GetNumberProperty = nullptr;
    std::atomic_uint32_t m_BluetoothCallbackCount{0};

    // Buffer and sync
    std::vector<opus_int16> m_SampleBuffer;
    std::array<unsigned char, kMaxPacketSize> m_EncodedPacket = {};
    std::vector<uint8_t> m_FrameBuffer = std::vector<uint8_t>(4 + kMaxPacketSize);
    std::mutex m_BufferMutex;
    std::condition_variable m_BufferCondition;

    // Thread
    std::thread m_EncoderThread;
    std::atomic_bool m_StopEncoderThread{false};
    std::atomic_bool m_Streaming{false};

    std::string m_DeviceName;
    int m_Bitrate{kDefaultBitrate};
};
