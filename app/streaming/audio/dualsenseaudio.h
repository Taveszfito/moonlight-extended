#pragma once

#include "settings/streamingpreferences.h"

#include <SDL.h>
#include <QMutex>
#include <array>
#include <deque>
#include <string>

struct OpusEncoder;
struct DualSenseOutputReport;

class DualSenseAudioRenderer
{
public:
    static DualSenseAudioRenderer& instance();

    void configure(StreamingPreferences::DualSenseAudioMode mode);
    void setController(SDL_GameController* controller, bool bluetooth, const char* path);
    bool isBluetooth();
    std::string bluetoothPath();
    void setBluetoothMicrophoneEnabled(bool enabled);
    bool refreshBluetoothMicrophone();
    bool isBluetoothHeadsetActive();
    void setBluetoothHeadsetActive(bool active);
    void submitMainAudio(const float* pcm, int frameCount, int channels);
    void setBluetoothRumble(uint16_t lowFrequency, uint16_t highFrequency);
    void setBluetoothLed(uint8_t red, uint8_t green, uint8_t blue);
    void setBluetoothMicLed(bool enabled);
    void setBluetoothTriggers(const DualSenseOutputReport* report);
    void receive(uint16_t controllerNumber, uint16_t sequence,
                 uint16_t frameCount, uint8_t channels,
                 uint8_t flags, const uint8_t* pcm, uint16_t pcmLength);
    void close();

private:
    DualSenseAudioRenderer() = default;
    bool openDeviceLocked();
    void closeBluetoothLocked();
    bool openBluetoothLocked(SDL_GameController* controller, const char* path);
    bool sendBluetoothWakeLocked();
    bool sendBluetoothStateLocked();
    bool sendBluetoothAudioLocked();
    bool sendBluetoothMicrophoneKeepaliveLocked();
    void fillBluetoothStateLocked(std::array<uint8_t, 63>& state) const;
    bool sendBluetoothHeadsetAudioLocked(const std::array<uint8_t, 200>& firstOpus,
                                          const std::array<int8_t, 64>& firstHaptics);
    static uint32_t bluetoothCrc(const uint8_t* data, size_t length);

    QMutex m_Mutex;
    SDL_AudioDeviceID m_Device = 0;
    SDL_GameController* m_BluetoothController = nullptr;
    OpusEncoder* m_BluetoothEncoder = nullptr;
    bool m_Bluetooth = false;
    bool m_BluetoothWakeSent = false;
    uint8_t m_BluetoothSequence = 0;
    uint8_t m_BluetoothPacketCounter = 0;
    uint8_t m_BluetoothLeftRumble = 0;
    uint8_t m_BluetoothRightRumble = 0;
    uint8_t m_BluetoothRed = 0;
    uint8_t m_BluetoothGreen = 80;
    uint8_t m_BluetoothBlue = 255;
    uint8_t m_BluetoothPlayerLeds = 0x04;
    bool m_BluetoothMicLed = false;
    bool m_BluetoothHeadsetActive = false;
    bool m_BluetoothMicrophoneEnabled = false;
    std::string m_BluetoothPath;
    std::array<uint8_t, 11> m_BluetoothLeftTrigger = {};
    std::array<uint8_t, 11> m_BluetoothRightTrigger = {};
    std::array<int16_t, 512 * 2> m_SpeakerPcm = {};
    int m_SpeakerFrames = 0;
    std::array<uint8_t, 200> m_SpeakerOpus = {};
    bool m_SpeakerOpusReady = false;
    std::array<uint8_t, 200> m_HeadsetPendingOpus = {};
    std::array<int8_t, 64> m_HeadsetPendingHaptics = {};
    bool m_HeadsetFramePending = false;
    std::deque<int16_t> m_MainAudioPcm;
    std::array<double, 127> m_HapticLeft = {};
    std::array<double, 127> m_HapticRight = {};
    std::array<double, 127> m_HapticCoefficients = {};
    std::array<int8_t, 64> m_HapticReport = {};
    int m_HapticRingPosition = 0;
    int m_HapticDecimationPhase = 0;
    int m_HapticReportPosition = 0;
    StreamingPreferences::DualSenseAudioMode m_Mode = StreamingPreferences::DSAM_AUTO;
    int m_ExpectedSequence = -1;
    uint64_t m_PacketsReceived = 0;
    uint64_t m_PacketsLost = 0;
};
