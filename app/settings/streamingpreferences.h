#pragma once

#include <QObject>
#include <QRect>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>

class StreamingPreferences : public QObject
{
    Q_OBJECT

public:
    static StreamingPreferences* get(QQmlEngine *qmlEngine = nullptr);

    Q_INVOKABLE static int
    getDefaultBitrate(int width, int height, int fps, bool yuv444);

    Q_INVOKABLE void save();

    Q_INVOKABLE QStringList microphoneDeviceNames() const;

    Q_INVOKABLE QString controllerKbmAction(const QString& source) const;
    Q_INVOKABLE void setControllerKbmAction(const QString& source, const QString& action);
    Q_INVOKABLE QString controllerKbmActionDescription(const QString& action) const;
    Q_INVOKABLE void resetControllerKbmMappings();
    Q_INVOKABLE bool saveControllerKbmPreset(const QString& requestedName);
    Q_INVOKABLE bool updateControllerKbmPreset(int index);
    Q_INVOKABLE bool loadControllerKbmPreset(int index);
    Q_INVOKABLE bool deleteControllerKbmPreset(int index);
    Q_INVOKABLE bool exportControllerKbmPreset(int index, const QUrl& destination) const;
    Q_INVOKABLE bool importControllerKbmPreset(const QUrl& source);
    Q_INVOKABLE void requestControllerKbmConfiguration() { emit controllerKbmConfigurationRequested(); }
    Q_INVOKABLE QString quickMenuKeyboardShortcutDescription() const;
    Q_INVOKABLE QString microphoneMuteKeyboardShortcutDescription() const;
    Q_INVOKABLE QString quickMenuControllerShortcutDescription() const;
    Q_INVOKABLE void commitControllerKbmSettings();
    Q_INVOKABLE void assignControllerKbmKeyboardKey(const QString& source, int qtKey,
                                                     int nativeVirtualKey, int nativeScanCode);
    Q_INVOKABLE void requestStreamQuickAction(const QString& action);
    Q_INVOKABLE void refreshControllerStatus();
    Q_INVOKABLE bool saveGyroStickProfile(const QString& name);
    Q_INVOKABLE bool updateGyroStickProfile(int index);
    Q_INVOKABLE bool loadGyroStickProfile(int index);
    Q_INVOKABLE bool deleteGyroStickProfile(int index);
    Q_INVOKABLE bool exportGyroStickProfile(int index, const QUrl& destination) const;
    Q_INVOKABLE bool importGyroStickProfile(const QUrl& source);

    void reload();

    enum AudioConfig
    {
        AC_STEREO,
        AC_51_SURROUND,
        AC_71_SURROUND
    };
    Q_ENUM(AudioConfig)

    enum ControllerEmulationMode
    {
        CEM_AUTO,
        CEM_XBOX,
        CEM_DUALSHOCK4,
        CEM_DUALSENSE
    };
    Q_ENUM(ControllerEmulationMode)

    enum DualSenseAudioMode
    {
        DSAM_AUTO,
        DSAM_USB_SPEAKER,
        DSAM_USB_HEADSET,
        DSAM_HAPTICS_ONLY,
        DSAM_OFF
    };
    Q_ENUM(DualSenseAudioMode)

    enum VideoCodecConfig
    {
        VCC_AUTO,
        VCC_FORCE_H264,
        VCC_FORCE_HEVC,
        VCC_FORCE_HEVC_HDR_DEPRECATED, // Kept for backwards compatibility
        VCC_FORCE_AV1
    };
    Q_ENUM(VideoCodecConfig)

    enum VideoDecoderSelection
    {
        VDS_AUTO,
        VDS_FORCE_HARDWARE,
        VDS_FORCE_SOFTWARE
    };
    Q_ENUM(VideoDecoderSelection)

    // Mac only (for now)
    enum RendererSelection
    {
        RS_PROBE_ONLY = -1, // Only valid for probing decoder properties
        RS_AUTO,
        RS_VULKAN,
        RS_METAL,
        RS_AVSBDL
    };
    Q_ENUM(RendererSelection)

    enum WindowMode
    {
        WM_FULLSCREEN,
        WM_FULLSCREEN_DESKTOP,
        WM_WINDOWED
    };
    Q_ENUM(WindowMode)

    enum UIDisplayMode
    {
        UI_WINDOWED,
        UI_MAXIMIZED,
        UI_FULLSCREEN
    };
    Q_ENUM(UIDisplayMode)

    // New entries must go at the end of the enum
    // to avoid renumbering existing entries (which
    // would affect existing user preferences).
    enum Language
    {
        LANG_AUTO,
        LANG_EN,
        LANG_FR,
        LANG_ZH_CN,
        LANG_DE,
        LANG_NB_NO,
        LANG_RU,
        LANG_ES,
        LANG_JA,
        LANG_VI,
        LANG_TH,
        LANG_KO,
        LANG_HU,
        LANG_NL,
        LANG_SV,
        LANG_TR,
        LANG_UK,
        LANG_ZH_TW,
        LANG_PT,
        LANG_PT_BR,
        LANG_EL,
        LANG_IT,
        LANG_HI,
        LANG_PL,
        LANG_CS,
        LANG_HE,
        LANG_CKB,
        LANG_LT,
        LANG_ET,
        LANG_BG,
        LANG_EO,
        LANG_TA,
    };
    Q_ENUM(Language);

    enum CaptureSysKeysMode
    {
        CSK_OFF,
        CSK_FULLSCREEN,
        CSK_ALWAYS,
    };
    Q_ENUM(CaptureSysKeysMode);

    Q_PROPERTY(int width MEMBER width NOTIFY displayModeChanged)
    Q_PROPERTY(int height MEMBER height NOTIFY displayModeChanged)
    Q_PROPERTY(int fps MEMBER fps NOTIFY displayModeChanged)
    Q_PROPERTY(int bitrateKbps MEMBER bitrateKbps NOTIFY bitrateChanged)
    Q_PROPERTY(bool unlockBitrate MEMBER unlockBitrate NOTIFY unlockBitrateChanged)
    Q_PROPERTY(bool autoAdjustBitrate MEMBER autoAdjustBitrate NOTIFY autoAdjustBitrateChanged)
    Q_PROPERTY(bool enableVsync MEMBER enableVsync NOTIFY enableVsyncChanged)
    Q_PROPERTY(bool gameOptimizations MEMBER gameOptimizations NOTIFY gameOptimizationsChanged)
    Q_PROPERTY(bool playAudioOnHost MEMBER playAudioOnHost NOTIFY playAudioOnHostChanged)
    Q_PROPERTY(bool micCapture MEMBER micCapture NOTIFY micCaptureChanged)
    Q_PROPERTY(QString micDevice MEMBER micDevice NOTIFY micDeviceChanged)
    Q_PROPERTY(bool stopSteamForDualSense MEMBER stopSteamForDualSense NOTIFY stopSteamForDualSenseChanged)
    Q_PROPERTY(bool multiController MEMBER multiController NOTIFY multiControllerChanged)
    Q_PROPERTY(bool enableMdns MEMBER enableMdns NOTIFY enableMdnsChanged)
    Q_PROPERTY(bool quitAppAfter MEMBER quitAppAfter NOTIFY quitAppAfterChanged)
    Q_PROPERTY(bool absoluteMouseMode MEMBER absoluteMouseMode NOTIFY absoluteMouseModeChanged)
    Q_PROPERTY(bool absoluteTouchMode MEMBER absoluteTouchMode NOTIFY absoluteTouchModeChanged)
    Q_PROPERTY(bool framePacing MEMBER framePacing NOTIFY framePacingChanged)
    Q_PROPERTY(bool connectionWarnings MEMBER connectionWarnings NOTIFY connectionWarningsChanged)
    Q_PROPERTY(bool configurationWarnings MEMBER configurationWarnings NOTIFY configurationWarningsChanged)
    Q_PROPERTY(bool richPresence MEMBER richPresence NOTIFY richPresenceChanged)
    Q_PROPERTY(bool gamepadMouse MEMBER gamepadMouse NOTIFY gamepadMouseChanged)
    Q_PROPERTY(bool detectNetworkBlocking MEMBER detectNetworkBlocking NOTIFY detectNetworkBlockingChanged)
    Q_PROPERTY(bool showPerformanceOverlay MEMBER showPerformanceOverlay NOTIFY showPerformanceOverlayChanged)
    Q_PROPERTY(AudioConfig audioConfig MEMBER audioConfig NOTIFY audioConfigChanged)
    Q_PROPERTY(ControllerEmulationMode controllerEmulationMode MEMBER controllerEmulationMode NOTIFY controllerEmulationModeChanged)
    Q_PROPERTY(DualSenseAudioMode dualSenseAudioMode MEMBER dualSenseAudioMode NOTIFY dualSenseAudioModeChanged)
    Q_PROPERTY(VideoCodecConfig videoCodecConfig MEMBER videoCodecConfig NOTIFY videoCodecConfigChanged)
    Q_PROPERTY(bool enableHdr MEMBER enableHdr NOTIFY enableHdrChanged)
    Q_PROPERTY(bool enableYUV444 MEMBER enableYUV444 NOTIFY enableYUV444Changed)
    Q_PROPERTY(VideoDecoderSelection videoDecoderSelection MEMBER videoDecoderSelection NOTIFY videoDecoderSelectionChanged)
    Q_PROPERTY(RendererSelection rendererSelection MEMBER rendererSelection NOTIFY rendererSelectionChanged)
    Q_PROPERTY(WindowMode windowMode MEMBER windowMode NOTIFY windowModeChanged)
    Q_PROPERTY(WindowMode recommendedFullScreenMode MEMBER recommendedFullScreenMode CONSTANT)
    Q_PROPERTY(UIDisplayMode uiDisplayMode MEMBER uiDisplayMode NOTIFY uiDisplayModeChanged)
    Q_PROPERTY(bool swapMouseButtons MEMBER swapMouseButtons NOTIFY mouseButtonsChanged)
    Q_PROPERTY(bool muteOnFocusLoss MEMBER muteOnFocusLoss NOTIFY muteOnFocusLossChanged)
    Q_PROPERTY(bool backgroundGamepad MEMBER backgroundGamepad NOTIFY backgroundGamepadChanged)
    Q_PROPERTY(bool reverseScrollDirection MEMBER reverseScrollDirection NOTIFY reverseScrollDirectionChanged)
    Q_PROPERTY(bool swapFaceButtons MEMBER swapFaceButtons NOTIFY swapFaceButtonsChanged)
    Q_PROPERTY(bool gyroOverrideEnabled MEMBER gyroOverrideEnabled NOTIFY gyroOverrideChanged)
    Q_PROPERTY(int gyroXAxisSource MEMBER gyroXAxisSource NOTIFY gyroOverrideChanged)
    Q_PROPERTY(int gyroYAxisSource MEMBER gyroYAxisSource NOTIFY gyroOverrideChanged)
    Q_PROPERTY(int gyroZAxisSource MEMBER gyroZAxisSource NOTIFY gyroOverrideChanged)
    Q_PROPERTY(bool gyroXAxisInverted MEMBER gyroXAxisInverted NOTIFY gyroOverrideChanged)
    Q_PROPERTY(bool gyroYAxisInverted MEMBER gyroYAxisInverted NOTIFY gyroOverrideChanged)
    Q_PROPERTY(bool gyroZAxisInverted MEMBER gyroZAxisInverted NOTIFY gyroOverrideChanged)
    Q_PROPERTY(bool gyroXAxisDisabled MEMBER gyroXAxisDisabled NOTIFY gyroOverrideChanged)
    Q_PROPERTY(bool gyroYAxisDisabled MEMBER gyroYAxisDisabled NOTIFY gyroOverrideChanged)
    Q_PROPERTY(bool gyroZAxisDisabled MEMBER gyroZAxisDisabled NOTIFY gyroOverrideChanged)
    Q_PROPERTY(bool controllerKbmMode MEMBER controllerKbmMode NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int controllerKbmStickSpeed MEMBER controllerKbmStickSpeed NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(bool controllerKbmContinuousStickMouse MEMBER controllerKbmContinuousStickMouse NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int controllerKbmTriggerThreshold MEMBER controllerKbmTriggerThreshold NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(QString controllerKbmTriggerBehavior MEMBER controllerKbmTriggerBehavior NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int controllerKbmTriggerRepeatRate MEMBER controllerKbmTriggerRepeatRate NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int controllerKbmLeftTriggerThreshold MEMBER controllerKbmLeftTriggerThreshold NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(QString controllerKbmLeftTriggerBehavior MEMBER controllerKbmLeftTriggerBehavior NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int controllerKbmLeftTriggerRepeatRate MEMBER controllerKbmLeftTriggerRepeatRate NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int controllerKbmRightTriggerThreshold MEMBER controllerKbmRightTriggerThreshold NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(QString controllerKbmRightTriggerBehavior MEMBER controllerKbmRightTriggerBehavior NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int controllerKbmRightTriggerRepeatRate MEMBER controllerKbmRightTriggerRepeatRate NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(QStringList controllerKbmPresetNames MEMBER controllerKbmPresetNames NOTIFY controllerKbmPresetsChanged)
    Q_PROPERTY(int controllerKbmActivePresetIndex MEMBER controllerKbmActivePresetIndex NOTIFY controllerKbmPresetsChanged)
    Q_PROPERTY(QString controllerKbmShortcut MEMBER controllerKbmShortcut NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(bool controllerKbmGyroEnabled MEMBER controllerKbmGyroEnabled NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int controllerKbmGyroSensitivity MEMBER controllerKbmGyroSensitivity NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(QString controllerKbmGyroShortcut MEMBER controllerKbmGyroShortcut NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(bool controllerKbmGyroHoldMode MEMBER controllerKbmGyroHoldMode NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(QString controllerKbmGyroActivationButtons MEMBER controllerKbmGyroActivationButtons NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int dualSenseControllerVolume MEMBER dualSenseControllerVolume NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(bool triggerOverrideEnabled MEMBER triggerOverrideEnabled NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int leftTriggerOverrideThreshold MEMBER leftTriggerOverrideThreshold NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int rightTriggerOverrideThreshold MEMBER rightTriggerOverrideThreshold NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int controllerKbmGyroXSensitivity MEMBER controllerKbmGyroXSensitivity NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int controllerKbmGyroYSensitivity MEMBER controllerKbmGyroYSensitivity NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int controllerKbmGyroZSensitivity MEMBER controllerKbmGyroZSensitivity NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(bool controllerKbmGyroXInverted MEMBER controllerKbmGyroXInverted NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(bool controllerKbmGyroYInverted MEMBER controllerKbmGyroYInverted NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(bool controllerKbmGyroZInverted MEMBER controllerKbmGyroZInverted NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int quickMenuKeyboardKey MEMBER quickMenuKeyboardKey NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int quickMenuKeyboardModifiers MEMBER quickMenuKeyboardModifiers NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int microphoneMuteKeyboardKey MEMBER microphoneMuteKeyboardKey NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int microphoneMuteKeyboardModifiers MEMBER microphoneMuteKeyboardModifiers NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(bool gyroStickEnabled MEMBER gyroStickEnabled NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int gyroStickSensitivity MEMBER gyroStickSensitivity NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int gyroStickXSensitivity MEMBER gyroStickXSensitivity NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int gyroStickYSensitivity MEMBER gyroStickYSensitivity NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int gyroStickZSensitivity MEMBER gyroStickZSensitivity NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(bool gyroStickXInverted MEMBER gyroStickXInverted NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(bool gyroStickYInverted MEMBER gyroStickYInverted NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(bool gyroStickZInverted MEMBER gyroStickZInverted NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(bool gyroStickSmoothing MEMBER gyroStickSmoothing NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int gyroStickDeadzone MEMBER gyroStickDeadzone NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(QString gyroStickShortcut MEMBER gyroStickShortcut NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(bool gyroStickHoldMode MEMBER gyroStickHoldMode NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(QString gyroStickActivationButtons MEMBER gyroStickActivationButtons NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(bool gyroStickPrecisionEnabled MEMBER gyroStickPrecisionEnabled NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int gyroStickPrecisionSensitivity MEMBER gyroStickPrecisionSensitivity NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(QString gyroStickPrecisionButtons MEMBER gyroStickPrecisionButtons NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(QStringList gyroStickProfileNames MEMBER gyroStickProfileNames NOTIFY gyroStickProfilesChanged)
    Q_PROPERTY(int gyroStickActiveProfileIndex MEMBER gyroStickActiveProfileIndex NOTIFY gyroStickProfilesChanged)
    Q_PROPERTY(bool controllerBatteryWarningEnabled MEMBER controllerBatteryWarningEnabled NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(int controllerBatteryWarningThreshold MEMBER controllerBatteryWarningThreshold NOTIFY controllerKbmSettingsChanged)
    Q_PROPERTY(QVariantList controllerStatus MEMBER controllerStatus NOTIFY controllerStatusChanged)
    Q_PROPERTY(bool anyControllerHeadsetConnected MEMBER anyControllerHeadsetConnected NOTIFY controllerStatusChanged)
    Q_PROPERTY(bool keepAwake MEMBER keepAwake NOTIFY keepAwakeChanged)
    Q_PROPERTY(CaptureSysKeysMode captureSysKeysMode MEMBER captureSysKeysMode NOTIFY captureSysKeysModeChanged)
    Q_PROPERTY(Language language MEMBER language NOTIFY languageChanged);

    Q_INVOKABLE bool retranslate();

    // Directly accessible members for preferences
    int width;
    int height;
    int fps;
    int bitrateKbps;
    bool unlockBitrate;
    bool autoAdjustBitrate;
    bool enableVsync;
    bool gameOptimizations;
    bool playAudioOnHost;
    bool micCapture;
    QString micDevice;
    bool stopSteamForDualSense;
    bool multiController;
    bool enableMdns;
    bool quitAppAfter;
    bool absoluteMouseMode;
    bool absoluteTouchMode;
    bool framePacing;
    bool connectionWarnings;
    bool configurationWarnings;
    bool richPresence;
    bool gamepadMouse;
    bool detectNetworkBlocking;
    bool showPerformanceOverlay;
    bool swapMouseButtons;
    bool muteOnFocusLoss;
    bool backgroundGamepad;
    bool reverseScrollDirection;
    bool swapFaceButtons;
    bool gyroOverrideEnabled;
    int gyroXAxisSource;
    int gyroYAxisSource;
    int gyroZAxisSource;
    bool gyroXAxisInverted;
    bool gyroYAxisInverted;
    bool gyroZAxisInverted;
    bool gyroXAxisDisabled;
    bool gyroYAxisDisabled;
    bool gyroZAxisDisabled;
    bool controllerKbmMode;
    int controllerKbmStickSpeed;
    bool controllerKbmContinuousStickMouse;
    int controllerKbmTriggerThreshold;
    QString controllerKbmTriggerBehavior;
    int controllerKbmTriggerRepeatRate;
    int controllerKbmLeftTriggerThreshold;
    QString controllerKbmLeftTriggerBehavior;
    int controllerKbmLeftTriggerRepeatRate;
    int controllerKbmRightTriggerThreshold;
    QString controllerKbmRightTriggerBehavior;
    int controllerKbmRightTriggerRepeatRate;
    QVariantMap controllerKbmMappings;
    QVariantList controllerKbmPresets;
    QStringList controllerKbmPresetNames;
    int controllerKbmActivePresetIndex;
    QString controllerKbmShortcut;
    bool controllerKbmGyroEnabled;
    int controllerKbmGyroSensitivity;
    QString controllerKbmGyroShortcut;
    bool controllerKbmGyroHoldMode;
    QString controllerKbmGyroActivationButtons;
    int dualSenseControllerVolume;
    bool triggerOverrideEnabled;
    int leftTriggerOverrideThreshold, rightTriggerOverrideThreshold;
    int controllerKbmGyroXSensitivity, controllerKbmGyroYSensitivity, controllerKbmGyroZSensitivity;
    bool controllerKbmGyroXInverted, controllerKbmGyroYInverted, controllerKbmGyroZInverted;
    int quickMenuKeyboardKey;
    int quickMenuKeyboardModifiers;
    int microphoneMuteKeyboardKey, microphoneMuteKeyboardModifiers;
    bool gyroStickEnabled;
    int gyroStickSensitivity, gyroStickXSensitivity, gyroStickYSensitivity, gyroStickZSensitivity;
    bool gyroStickXInverted, gyroStickYInverted, gyroStickZInverted, gyroStickSmoothing;
    int gyroStickDeadzone;
    QString gyroStickShortcut;
    bool gyroStickHoldMode;
    QString gyroStickActivationButtons;
    bool gyroStickPrecisionEnabled;
    int gyroStickPrecisionSensitivity;
    QString gyroStickPrecisionButtons;
    QVariantList gyroStickProfiles;
    QStringList gyroStickProfileNames;
    int gyroStickActiveProfileIndex;
    bool controllerBatteryWarningEnabled;
    int controllerBatteryWarningThreshold;
    QVariantList controllerStatus;
    bool anyControllerHeadsetConnected;
    bool keepAwake;
    int packetSize;
    AudioConfig audioConfig;
    ControllerEmulationMode controllerEmulationMode;
    DualSenseAudioMode dualSenseAudioMode;
    VideoCodecConfig videoCodecConfig;
    bool enableHdr;
    bool enableYUV444;
    VideoDecoderSelection videoDecoderSelection;
    WindowMode windowMode;
    WindowMode recommendedFullScreenMode;
    UIDisplayMode uiDisplayMode;
    Language language;
    CaptureSysKeysMode captureSysKeysMode;
    RendererSelection rendererSelection;

signals:
    void displayModeChanged();
    void bitrateChanged();
    void unlockBitrateChanged();
    void autoAdjustBitrateChanged();
    void enableVsyncChanged();
    void gameOptimizationsChanged();
    void playAudioOnHostChanged();
    void micCaptureChanged();
    void micDeviceChanged();
    void stopSteamForDualSenseChanged();
    void multiControllerChanged();
    void unsupportedFpsChanged();
    void enableMdnsChanged();
    void quitAppAfterChanged();
    void absoluteMouseModeChanged();
    void absoluteTouchModeChanged();
    void audioConfigChanged();
    void controllerEmulationModeChanged();
    void dualSenseAudioModeChanged();
    void videoCodecConfigChanged();
    void enableHdrChanged();
    void enableYUV444Changed();
    void videoDecoderSelectionChanged();
    void uiDisplayModeChanged();
    void windowModeChanged();
    void framePacingChanged();
    void connectionWarningsChanged();
    void configurationWarningsChanged();
    void richPresenceChanged();
    void gamepadMouseChanged();
    void detectNetworkBlockingChanged();
    void showPerformanceOverlayChanged();
    void mouseButtonsChanged();
    void muteOnFocusLossChanged();
    void backgroundGamepadChanged();
    void reverseScrollDirectionChanged();
    void swapFaceButtonsChanged();
    void gyroOverrideChanged();
    void controllerKbmSettingsChanged();
    void controllerKbmMappingsChanged();
    void controllerKbmPresetsChanged();
    void gyroStickProfilesChanged();
    void controllerStatusChanged();
    void controllerKbmConfigurationRequested();
    void streamQuickActionRequested(QString action);
    void captureSysKeysModeChanged();
    void keepAwakeChanged();
    void languageChanged();
    void rendererSelectionChanged();

private:
    explicit StreamingPreferences(QQmlEngine *qmlEngine);

    QString getSuffixFromLanguage(Language lang);
    void refreshControllerKbmPresetNames();
    void persistControllerKbmData();
    QVariantMap currentGyroStickProfile(const QString& name = QString()) const;
    void refreshGyroStickProfileNames();

    QQmlEngine* m_QmlEngine;
};

