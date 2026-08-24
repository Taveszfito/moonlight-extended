#include "streamingpreferences.h"
#include "utils.h"

#include <QSettings>
#include <QTranslator>
#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLocale>
#include <QReadWriteLock>
#include <QUuid>
#include <QtMath>
#include <utility>
#include <SDL.h>
#include "streaming/session.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QtDebug>

#define SER_STREAMSETTINGS "streamsettings"
#define SER_WIDTH "width"
#define SER_HEIGHT "height"
#define SER_FPS "fps"
#define SER_BITRATE "bitrate"
#define SER_UNLOCK_BITRATE "unlockbitrate"
#define SER_AUTOADJUSTBITRATE "autoadjustbitrate"
#define SER_FULLSCREEN "fullscreen"
#define SER_VSYNC "vsync"
#define SER_GAMEOPTS "gameopts"
#define SER_HOSTAUDIO "hostaudio"
#define SER_MICCAPTURE "micCapture"
#define SER_MICDEVICE "micDevice"
#define SER_STOPSTEAMFORDUALSENSE "stopSteamForDualSense"
#define SER_MULTICONT "multicontroller"
#define SER_AUDIOCFG "audiocfg"
#define SER_CONTROLLEREMULATION "controlleremulation"
#define SER_DUALSENSEAUDIOMODE "dualsenseaudiomode"
#define SER_VIDEOCFG "videocfg"
#define SER_HDR "hdr"
#define SER_YUV444 "yuv444"
#define SER_VIDEODEC "videodec"
#define SER_WINDOWMODE "windowmode"
#define SER_MDNS "mdns"
#define SER_QUITAPPAFTER "quitAppAfter"
#define SER_ABSMOUSEMODE "mouseacceleration"
#define SER_ABSTOUCHMODE "abstouchmode"
#define SER_STARTWINDOWED "startwindowed"
#define SER_FRAMEPACING "framepacing"
#define SER_CONNWARNINGS "connwarnings"
#define SER_CONFWARNINGS "confwarnings"
#define SER_UIDISPLAYMODE "uidisplaymode"
#define SER_RICHPRESENCE "richpresence"
#define SER_GAMEPADMOUSE "gamepadmouse"
#define SER_DEFAULTVER "defaultver"
#define SER_PACKETSIZE "packetsize"
#define SER_DETECTNETBLOCKING "detectnetblocking"
#define SER_SHOWPERFOVERLAY "showperfoverlay"
#define SER_SWAPMOUSEBUTTONS "swapmousebuttons"
#define SER_MUTEONFOCUSLOSS "muteonfocusloss"
#define SER_BACKGROUNDGAMEPAD "backgroundgamepad"
#define SER_REVERSESCROLL "reversescroll"
#define SER_SWAPFACEBUTTONS "swapfacebuttons"
#define SER_GYROOVERRIDEENABLED "gyrooverrideenabled"
#define SER_GYROXSOURCE "gyroxsource"
#define SER_GYROYSOURCE "gyroysource"
#define SER_GYROZSOURCE "gyrozsource"
#define SER_GYROXINVERTED "gyroxinverted"
#define SER_GYROYINVERTED "gyroyinverted"
#define SER_GYROZINVERTED "gyrozinverted"
#define SER_GYROXDISABLED "gyroxdisabled"
#define SER_GYROYDISABLED "gyroydisabled"
#define SER_GYROZDISABLED "gyrozdisabled"
#define SER_CONTROLLERKBMMODE "controllerkbmmode"
#define SER_CONTROLLERKBMSTICKSPEED "controllerkbmstickspeed"
#define SER_CONTROLLERKBMCONTINUOUS "controllerkbmcontinuous"
#define SER_CONTROLLERKBMTRIGGERTHRESHOLD "controllerkbmtriggerthreshold"
#define SER_CONTROLLERKBMTRIGGERBEHAVIOR "controllerkbmtriggerbehavior"
#define SER_CONTROLLERKBMTRIGGERREPEATRATE "controllerkbmtriggerrepeatrate"
#define SER_CONTROLLERKBMLEFTTRIGGERTHRESHOLD "controllerkbmlefttriggerthreshold"
#define SER_CONTROLLERKBMLEFTTRIGGERBEHAVIOR "controllerkbmlefttriggerbehavior"
#define SER_CONTROLLERKBMLEFTTRIGGERREPEATRATE "controllerkbmlefttriggerrepeatrate"
#define SER_CONTROLLERKBMRIGHTTRIGGERTHRESHOLD "controllerkbmrighttriggerthreshold"
#define SER_CONTROLLERKBMRIGHTTRIGGERBEHAVIOR "controllerkbmrighttriggerbehavior"
#define SER_CONTROLLERKBMRIGHTTRIGGERREPEATRATE "controllerkbmrighttriggerrepeatrate"
#define SER_CONTROLLERKBMMAPPINGS "controllerkbmmappings"
#define SER_CONTROLLERKBMPRESETS "controllerkbmpresets"
#define SER_CONTROLLERKBMACTIVEPRESET "controllerkbmactivepreset"
#define SER_CONTROLLERKBMSHORTCUT "controllerkbmshortcut"
#define SER_CONTROLLERKBMGYROENABLED "controllerkbmgyroenabled"
#define SER_CONTROLLERKBMGYROSENSITIVITY "controllerkbmgyrosensitivity"
#define SER_CONTROLLERKBMGYROSHORTCUT "controllerkbmgyroshortcut"
#define SER_CONTROLLERKBMGYROHOLDMODE "controllerkbmgyroholdmode"
#define SER_CONTROLLERKBMGYROACTIVATIONBUTTONS "controllerkbmgyroactivationbuttons"
#define SER_DUALSENSECONTROLLERVOLUME "dualsensecontrollervolume"
#define SER_TRIGGEROVERRIDEENABLED "triggeroverrideenabled"
#define SER_LEFTTRIGGEROVERRIDETHRESHOLD "lefttriggeroverridethreshold"
#define SER_RIGHTTRIGGEROVERRIDETHRESHOLD "righttriggeroverridethreshold"
#define SER_MICMUTEKEYBOARDKEY "micmutekeyboardkey"
#define SER_MICMUTEKEYBOARDMODIFIERS "micmutekeyboardmodifiers"
#define SER_CONTROLLERKBMGYROXSENSITIVITY "controllerkbmgyroxsensitivity"
#define SER_CONTROLLERKBMGYROYSENSITIVITY "controllerkbmgyroysensitivity"
#define SER_CONTROLLERKBMGYROZSENSITIVITY "controllerkbmgyrozsensitivity"
#define SER_CONTROLLERKBMGYROXINVERTED "controllerkbmgyroxinverted"
#define SER_CONTROLLERKBMGYROYINVERTED "controllerkbmgyroyinverted"
#define SER_CONTROLLERKBMGYROZINVERTED "controllerkbmgyrozinverted"
#define SER_QUICKMENUKEYBOARDKEY "quickmenukeyboardkey"
#define SER_QUICKMENUKEYBOARDMODIFIERS "quickmenukeyboardmodifiers"
#define SER_CAPTURESYSKEYS "capturesyskeys"
#define SER_KEEPAWAKE "keepawake"
#define SER_LANGUAGE "language"
#define SER_RENDERER "renderer"

#define CURRENT_DEFAULT_VER 2

static StreamingPreferences* s_GlobalPrefs;

Q_GLOBAL_STATIC(QReadWriteLock, s_GlobalPrefsLock)

StreamingPreferences::StreamingPreferences(QQmlEngine *qmlEngine)
    : m_QmlEngine(qmlEngine)
{
    reload();
}

StreamingPreferences* StreamingPreferences::get(QQmlEngine *qmlEngine)
{
    {
        QReadLocker readGuard(s_GlobalPrefsLock);

        // If we have a preference object and it's associated with a QML engine or
        // if the caller didn't specify a QML engine, return the existing object.
        if (s_GlobalPrefs && (s_GlobalPrefs->m_QmlEngine || !qmlEngine)) {
            // The lifetime logic here relies on the QML engine also being a singleton.
            Q_ASSERT(!qmlEngine || s_GlobalPrefs->m_QmlEngine == qmlEngine);
            return s_GlobalPrefs;
        }
    }

    {
        QWriteLocker writeGuard(s_GlobalPrefsLock);

        // If we already have an preference object but the QML engine is now available,
        // associate the QML engine with the preferences.
        if (s_GlobalPrefs) {
            if (!s_GlobalPrefs->m_QmlEngine) {
                s_GlobalPrefs->m_QmlEngine = qmlEngine;
            }
            else {
                // We could reach this codepath if another thread raced with us
                // and created the object while we were outside the pref lock.
                Q_ASSERT(!qmlEngine || s_GlobalPrefs->m_QmlEngine == qmlEngine);
            }
        }
        else {
            s_GlobalPrefs = new StreamingPreferences(qmlEngine);
        }

        return s_GlobalPrefs;
    }
}

void StreamingPreferences::reload()
{
    QSettings settings;

    int defaultVer = settings.value(SER_DEFAULTVER, 0).toInt();

#ifdef Q_OS_DARWIN
    recommendedFullScreenMode = WindowMode::WM_FULLSCREEN_DESKTOP;
#else
    // Wayland doesn't support modesetting, so use fullscreen desktop mode
    // unless we have a slow GPU (which can take advantage of wp_viewporter
    // to reduce GPU load with lower resolution video streams).
    if (WMUtils::isRunningWayland() && !WMUtils::isGpuSlow()) {
        recommendedFullScreenMode = WindowMode::WM_FULLSCREEN_DESKTOP;
    }
    else {
        recommendedFullScreenMode = WindowMode::WM_FULLSCREEN;
    }
#endif

    width = settings.value(SER_WIDTH, 1280).toInt();
    height = settings.value(SER_HEIGHT, 720).toInt();
    fps = settings.value(SER_FPS, 60).toInt();
    enableYUV444 = settings.value(SER_YUV444, false).toBool();
    bitrateKbps = settings.value(SER_BITRATE, getDefaultBitrate(width, height, fps, enableYUV444)).toInt();
    unlockBitrate = settings.value(SER_UNLOCK_BITRATE, false).toBool();
    autoAdjustBitrate = settings.value(SER_AUTOADJUSTBITRATE, true).toBool();
    enableVsync = settings.value(SER_VSYNC, true).toBool();
    gameOptimizations = settings.value(SER_GAMEOPTS, true).toBool();
    playAudioOnHost = settings.value(SER_HOSTAUDIO, false).toBool();
    micCapture = settings.value(SER_MICCAPTURE, false).toBool();
    micDevice = settings.value(SER_MICDEVICE, QString()).toString();
    stopSteamForDualSense = settings.value(SER_STOPSTEAMFORDUALSENSE, true).toBool();
    multiController = settings.value(SER_MULTICONT, true).toBool();
    enableMdns = settings.value(SER_MDNS, true).toBool();
    quitAppAfter = settings.value(SER_QUITAPPAFTER, false).toBool();
    absoluteMouseMode = settings.value(SER_ABSMOUSEMODE, false).toBool();
    absoluteTouchMode = settings.value(SER_ABSTOUCHMODE, true).toBool();
    framePacing = settings.value(SER_FRAMEPACING, false).toBool();
    connectionWarnings = settings.value(SER_CONNWARNINGS, true).toBool();
    configurationWarnings = settings.value(SER_CONFWARNINGS, true).toBool();
    richPresence = settings.value(SER_RICHPRESENCE, true).toBool();
    gamepadMouse = settings.value(SER_GAMEPADMOUSE, true).toBool();
    detectNetworkBlocking = settings.value(SER_DETECTNETBLOCKING, true).toBool();
    showPerformanceOverlay = settings.value(SER_SHOWPERFOVERLAY, false).toBool();
    packetSize = settings.value(SER_PACKETSIZE, 0).toInt();
    swapMouseButtons = settings.value(SER_SWAPMOUSEBUTTONS, false).toBool();
    muteOnFocusLoss = settings.value(SER_MUTEONFOCUSLOSS, false).toBool();
    backgroundGamepad = settings.value(SER_BACKGROUNDGAMEPAD, false).toBool();
    reverseScrollDirection = settings.value(SER_REVERSESCROLL, false).toBool();
    swapFaceButtons = settings.value(SER_SWAPFACEBUTTONS, false).toBool();
    gyroOverrideEnabled = settings.value(SER_GYROOVERRIDEENABLED, false).toBool();
    gyroXAxisSource = qBound(0, settings.value(SER_GYROXSOURCE, 0).toInt(), 2);
    gyroYAxisSource = qBound(0, settings.value(SER_GYROYSOURCE, 1).toInt(), 2);
    gyroZAxisSource = qBound(0, settings.value(SER_GYROZSOURCE, 2).toInt(), 2);
    gyroXAxisInverted = settings.value(SER_GYROXINVERTED, false).toBool();
    gyroYAxisInverted = settings.value(SER_GYROYINVERTED, false).toBool();
    gyroZAxisInverted = settings.value(SER_GYROZINVERTED, false).toBool();
    gyroXAxisDisabled = settings.value(SER_GYROXDISABLED, false).toBool();
    gyroYAxisDisabled = settings.value(SER_GYROYDISABLED, false).toBool();
    gyroZAxisDisabled = settings.value(SER_GYROZDISABLED, false).toBool();
    controllerKbmMode = settings.value(SER_CONTROLLERKBMMODE, false).toBool();
    controllerKbmStickSpeed = qBound(10, settings.value(SER_CONTROLLERKBMSTICKSPEED, 100).toInt(), 400);
    controllerKbmContinuousStickMouse = settings.value(SER_CONTROLLERKBMCONTINUOUS, true).toBool();
    controllerKbmTriggerThreshold = qBound(1, settings.value(SER_CONTROLLERKBMTRIGGERTHRESHOLD, 15).toInt(), 100);
    controllerKbmTriggerBehavior = settings.value(SER_CONTROLLERKBMTRIGGERBEHAVIOR, QStringLiteral("hold")).toString();
    if (controllerKbmTriggerBehavior != QStringLiteral("single") &&
            controllerKbmTriggerBehavior != QStringLiteral("repeat")) {
        controllerKbmTriggerBehavior = QStringLiteral("hold");
    }
    controllerKbmTriggerRepeatRate = qBound(1, settings.value(SER_CONTROLLERKBMTRIGGERREPEATRATE, 8).toInt(), 30);
    controllerKbmLeftTriggerThreshold = qBound(1, settings.value(SER_CONTROLLERKBMLEFTTRIGGERTHRESHOLD, controllerKbmTriggerThreshold).toInt(), 100);
    controllerKbmLeftTriggerBehavior = settings.value(SER_CONTROLLERKBMLEFTTRIGGERBEHAVIOR, controllerKbmTriggerBehavior).toString();
    controllerKbmLeftTriggerRepeatRate = qBound(1, settings.value(SER_CONTROLLERKBMLEFTTRIGGERREPEATRATE, controllerKbmTriggerRepeatRate).toInt(), 30);
    controllerKbmRightTriggerThreshold = qBound(1, settings.value(SER_CONTROLLERKBMRIGHTTRIGGERTHRESHOLD, controllerKbmTriggerThreshold).toInt(), 100);
    controllerKbmRightTriggerBehavior = settings.value(SER_CONTROLLERKBMRIGHTTRIGGERBEHAVIOR, controllerKbmTriggerBehavior).toString();
    controllerKbmRightTriggerRepeatRate = qBound(1, settings.value(SER_CONTROLLERKBMRIGHTTRIGGERREPEATRATE, controllerKbmTriggerRepeatRate).toInt(), 30);
    controllerKbmShortcut = settings.value(SER_CONTROLLERKBMSHORTCUT, QStringLiteral("4,5")).toString();
    controllerKbmGyroEnabled = settings.value(SER_CONTROLLERKBMGYROENABLED, false).toBool();
    controllerKbmGyroSensitivity = qBound(10, settings.value(SER_CONTROLLERKBMGYROSENSITIVITY, 100).toInt(), 500);
    controllerKbmGyroShortcut = settings.value(SER_CONTROLLERKBMGYROSHORTCUT, QStringLiteral("7,8")).toString();
    controllerKbmGyroHoldMode = settings.value(SER_CONTROLLERKBMGYROHOLDMODE, false).toBool();
    controllerKbmGyroActivationButtons = settings.value(SER_CONTROLLERKBMGYROACTIVATIONBUTTONS, QString()).toString();
    dualSenseControllerVolume=settings.value(SER_DUALSENSECONTROLLERVOLUME,100).toInt(); triggerOverrideEnabled=settings.value(SER_TRIGGEROVERRIDEENABLED,false).toBool(); leftTriggerOverrideThreshold=settings.value(SER_LEFTTRIGGEROVERRIDETHRESHOLD,50).toInt(); rightTriggerOverrideThreshold=settings.value(SER_RIGHTTRIGGEROVERRIDETHRESHOLD,50).toInt();
    controllerKbmGyroXSensitivity = qBound(10, settings.value(SER_CONTROLLERKBMGYROXSENSITIVITY, 100).toInt(), 500);
    controllerKbmGyroYSensitivity = qBound(10, settings.value(SER_CONTROLLERKBMGYROYSENSITIVITY, 100).toInt(), 500);
    controllerKbmGyroZSensitivity = qBound(10, settings.value(SER_CONTROLLERKBMGYROZSENSITIVITY, 100).toInt(), 500);
    controllerKbmGyroXInverted = settings.value(SER_CONTROLLERKBMGYROXINVERTED, false).toBool();
    controllerKbmGyroYInverted = settings.value(SER_CONTROLLERKBMGYROYINVERTED, false).toBool();
    controllerKbmGyroZInverted = settings.value(SER_CONTROLLERKBMGYROZINVERTED, false).toBool();
    quickMenuKeyboardKey = settings.value(SER_QUICKMENUKEYBOARDKEY, 0x41).toInt();
    quickMenuKeyboardModifiers = settings.value(SER_QUICKMENUKEYBOARDMODIFIERS, 0x07).toInt();
    microphoneMuteKeyboardKey=settings.value(SER_MICMUTEKEYBOARDKEY,0x4D).toInt(); microphoneMuteKeyboardModifiers=settings.value(SER_MICMUTEKEYBOARDMODIFIERS,0x05).toInt();
    controllerKbmMappings = QJsonDocument::fromJson(
                settings.value(SER_CONTROLLERKBMMAPPINGS, QByteArray("{}")).toByteArray()).object().toVariantMap();
    controllerKbmPresets = QJsonDocument::fromJson(
                settings.value(SER_CONTROLLERKBMPRESETS, QByteArray("[]")).toByteArray()).array().toVariantList();
    refreshControllerKbmPresetNames();
    controllerKbmActivePresetIndex = qBound(-1, settings.value(SER_CONTROLLERKBMACTIVEPRESET, -1).toInt(), controllerKbmPresets.size() - 1);
    keepAwake = settings.value(SER_KEEPAWAKE, true).toBool();
    enableHdr = settings.value(SER_HDR, false).toBool();
    captureSysKeysMode = static_cast<CaptureSysKeysMode>(settings.value(SER_CAPTURESYSKEYS,
                                                         static_cast<int>(CaptureSysKeysMode::CSK_OFF)).toInt());
    audioConfig = static_cast<AudioConfig>(settings.value(SER_AUDIOCFG,
                                                  static_cast<int>(AudioConfig::AC_STEREO)).toInt());
    controllerEmulationMode = static_cast<ControllerEmulationMode>(settings.value(SER_CONTROLLEREMULATION,
                                                  static_cast<int>(ControllerEmulationMode::CEM_AUTO)).toInt());
    dualSenseAudioMode = static_cast<DualSenseAudioMode>(settings.value(SER_DUALSENSEAUDIOMODE,
                                                  static_cast<int>(DualSenseAudioMode::DSAM_AUTO)).toInt());
    videoCodecConfig = static_cast<VideoCodecConfig>(settings.value(SER_VIDEOCFG,
                                                  static_cast<int>(VideoCodecConfig::VCC_AUTO)).toInt());
    videoDecoderSelection = static_cast<VideoDecoderSelection>(settings.value(SER_VIDEODEC,
                                                  static_cast<int>(VideoDecoderSelection::VDS_AUTO)).toInt());
    rendererSelection = static_cast<RendererSelection>(settings.value(SER_RENDERER,
                                                  static_cast<int>(RendererSelection::RS_AUTO)).toInt());
    windowMode = static_cast<WindowMode>(settings.value(SER_WINDOWMODE,
                                                        // Try to load from the old preference value too
                                                        static_cast<int>(settings.value(SER_FULLSCREEN, true).toBool() ?
                                                                             recommendedFullScreenMode : WindowMode::WM_WINDOWED)).toInt());
    uiDisplayMode = static_cast<UIDisplayMode>(settings.value(SER_UIDISPLAYMODE,
                                               static_cast<int>(settings.value(SER_STARTWINDOWED, true).toBool() ? UIDisplayMode::UI_WINDOWED
                                                                                                                 : UIDisplayMode::UI_MAXIMIZED)).toInt());
    language = static_cast<Language>(settings.value(SER_LANGUAGE,
                                                    static_cast<int>(Language::LANG_AUTO)).toInt());


    // Perform default settings updates as required based on last default version
    if (defaultVer < 1) {
#ifdef Q_OS_DARWIN
        // Update window mode setting on macOS from full-screen (old default) to borderless windowed (new default)
        if (windowMode == WindowMode::WM_FULLSCREEN) {
            windowMode = WindowMode::WM_FULLSCREEN_DESKTOP;
        }
#endif
    }
    if (defaultVer < 2) {
        if (windowMode == WindowMode::WM_FULLSCREEN && WMUtils::isRunningWayland()) {
            windowMode = WindowMode::WM_FULLSCREEN_DESKTOP;
        }
    }

    // Fixup VCC value to the new settings format with codec and HDR separate
    if (videoCodecConfig == VCC_FORCE_HEVC_HDR_DEPRECATED) {
        videoCodecConfig = VCC_AUTO;
        enableHdr = true;
    }
}

bool StreamingPreferences::retranslate()
{
    static QTranslator* translator = nullptr;

#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
    if (m_QmlEngine != nullptr) {
        // Dynamic retranslation is not supported until Qt 5.10
        return false;
    }
#endif

    QTranslator* newTranslator = new QTranslator();
    QString languageSuffix = getSuffixFromLanguage(language);

    // Remove the old translator, even if we can't load a new one.
    // Otherwise we'll be stuck with the old translated values instead
    // of defaulting to English.
    if (translator != nullptr) {
        QCoreApplication::removeTranslator(translator);
        delete translator;
        translator = nullptr;
    }

    if (newTranslator->load(QString(":/languages/qml_") + languageSuffix)) {
        qInfo() << "Successfully loaded translation for" << languageSuffix;

        translator = newTranslator;
        QCoreApplication::installTranslator(translator);
    }
    else {
        qInfo() << "No translation available for" << languageSuffix;
        delete newTranslator;
    }

    if (m_QmlEngine != nullptr) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
        // This is a dynamic retranslation from the settings page.
        // We have to kick the QML engine into reloading our text.
        m_QmlEngine->retranslate();
#else
        // Unreachable below Qt 5.10 due to the check above
        Q_ASSERT(false);
#endif
    }
    else {
        // This is a translation from a non-QML context, which means
        // it is probably app startup. There's nothing to refresh.
    }

    return true;
}

QString StreamingPreferences::getSuffixFromLanguage(StreamingPreferences::Language lang)
{
    switch (lang)
    {
    case LANG_DE:
        return "de";
    case LANG_EN:
        return "en";
    case LANG_FR:
        return "fr";
    case LANG_ZH_CN:
        return "zh_CN";
    case LANG_NB_NO:
        return "nb_NO";
    case LANG_RU:
        return "ru";
    case LANG_ES:
        return "es";
    case LANG_JA:
        return "ja";
    case LANG_VI:
        return "vi";
    case LANG_TH:
        return "th";
    case LANG_KO:
        return "ko";
    case LANG_HU:
        return "hu";
    case LANG_NL:
        return "nl";
    case LANG_SV:
        return "sv";
    case LANG_TR:
        return "tr";
    case LANG_UK:
        return "uk";
    case LANG_ZH_TW:
        return "zh_TW";
    case LANG_PT:
        return "pt";
    case LANG_PT_BR:
        return "pt_BR";
    case LANG_EL:
        return "el";
    case LANG_IT:
        return "it";
    case LANG_HI:
        return "hi";
    case LANG_PL:
        return "pl";
    case LANG_CS:
        return "cs";
    case LANG_HE:
        return "he";
    case LANG_CKB:
        return "ckb";
    case LANG_LT:
        return "lt";
    case LANG_ET:
        return "et";
    case LANG_BG:
        return "bg";
    case LANG_EO:
        return "eo";
    case LANG_TA:
        return "ta";
    case LANG_AUTO:
    default:
        return QLocale::system().name();
    }
}

void StreamingPreferences::save()
{
    QSettings settings;

    settings.setValue(SER_WIDTH, width);
    settings.setValue(SER_HEIGHT, height);
    settings.setValue(SER_FPS, fps);
    settings.setValue(SER_BITRATE, bitrateKbps);
    settings.setValue(SER_UNLOCK_BITRATE, unlockBitrate);
    settings.setValue(SER_AUTOADJUSTBITRATE, autoAdjustBitrate);
    settings.setValue(SER_VSYNC, enableVsync);
    settings.setValue(SER_GAMEOPTS, gameOptimizations);
    settings.setValue(SER_HOSTAUDIO, playAudioOnHost);
    settings.setValue(SER_MICCAPTURE, micCapture);
    settings.setValue(SER_MICDEVICE, micDevice);
    settings.setValue(SER_STOPSTEAMFORDUALSENSE, stopSteamForDualSense);
    settings.setValue(SER_MULTICONT, multiController);
    settings.setValue(SER_MDNS, enableMdns);
    settings.setValue(SER_QUITAPPAFTER, quitAppAfter);
    settings.setValue(SER_ABSMOUSEMODE, absoluteMouseMode);
    settings.setValue(SER_ABSTOUCHMODE, absoluteTouchMode);
    settings.setValue(SER_FRAMEPACING, framePacing);
    settings.setValue(SER_CONNWARNINGS, connectionWarnings);
    settings.setValue(SER_CONFWARNINGS, configurationWarnings);
    settings.setValue(SER_RICHPRESENCE, richPresence);
    settings.setValue(SER_GAMEPADMOUSE, gamepadMouse);
    settings.setValue(SER_PACKETSIZE, packetSize);
    settings.setValue(SER_DETECTNETBLOCKING, detectNetworkBlocking);
    settings.setValue(SER_SHOWPERFOVERLAY, showPerformanceOverlay);
    settings.setValue(SER_AUDIOCFG, static_cast<int>(audioConfig));
    settings.setValue(SER_CONTROLLEREMULATION, static_cast<int>(controllerEmulationMode));
    settings.setValue(SER_DUALSENSEAUDIOMODE, static_cast<int>(dualSenseAudioMode));
    settings.setValue(SER_HDR, enableHdr);
    settings.setValue(SER_YUV444, enableYUV444);
    settings.setValue(SER_VIDEOCFG, static_cast<int>(videoCodecConfig));
    settings.setValue(SER_VIDEODEC, static_cast<int>(videoDecoderSelection));
    settings.setValue(SER_RENDERER, static_cast<int>(rendererSelection));
    settings.setValue(SER_WINDOWMODE, static_cast<int>(windowMode));
    settings.setValue(SER_UIDISPLAYMODE, static_cast<int>(uiDisplayMode));
    settings.setValue(SER_LANGUAGE, static_cast<int>(language));
    settings.setValue(SER_DEFAULTVER, CURRENT_DEFAULT_VER);
    settings.setValue(SER_SWAPMOUSEBUTTONS, swapMouseButtons);
    settings.setValue(SER_MUTEONFOCUSLOSS, muteOnFocusLoss);
    settings.setValue(SER_BACKGROUNDGAMEPAD, backgroundGamepad);
    settings.setValue(SER_REVERSESCROLL, reverseScrollDirection);
    settings.setValue(SER_SWAPFACEBUTTONS, swapFaceButtons);
    settings.setValue(SER_GYROOVERRIDEENABLED, gyroOverrideEnabled);
    settings.setValue(SER_GYROXSOURCE, gyroXAxisSource);
    settings.setValue(SER_GYROYSOURCE, gyroYAxisSource);
    settings.setValue(SER_GYROZSOURCE, gyroZAxisSource);
    settings.setValue(SER_GYROXINVERTED, gyroXAxisInverted);
    settings.setValue(SER_GYROYINVERTED, gyroYAxisInverted);
    settings.setValue(SER_GYROZINVERTED, gyroZAxisInverted);
    settings.setValue(SER_GYROXDISABLED, gyroXAxisDisabled);
    settings.setValue(SER_GYROYDISABLED, gyroYAxisDisabled);
    settings.setValue(SER_GYROZDISABLED, gyroZAxisDisabled);
    settings.setValue(SER_CONTROLLERKBMMODE, controllerKbmMode);
    settings.setValue(SER_CONTROLLERKBMSTICKSPEED, controllerKbmStickSpeed);
    settings.setValue(SER_CONTROLLERKBMCONTINUOUS, controllerKbmContinuousStickMouse);
    settings.setValue(SER_CONTROLLERKBMTRIGGERTHRESHOLD, controllerKbmTriggerThreshold);
    settings.setValue(SER_CONTROLLERKBMTRIGGERBEHAVIOR, controllerKbmTriggerBehavior);
    settings.setValue(SER_CONTROLLERKBMTRIGGERREPEATRATE, controllerKbmTriggerRepeatRate);
    settings.setValue(SER_CONTROLLERKBMLEFTTRIGGERTHRESHOLD, controllerKbmLeftTriggerThreshold);
    settings.setValue(SER_CONTROLLERKBMLEFTTRIGGERBEHAVIOR, controllerKbmLeftTriggerBehavior);
    settings.setValue(SER_CONTROLLERKBMLEFTTRIGGERREPEATRATE, controllerKbmLeftTriggerRepeatRate);
    settings.setValue(SER_CONTROLLERKBMRIGHTTRIGGERTHRESHOLD, controllerKbmRightTriggerThreshold);
    settings.setValue(SER_CONTROLLERKBMRIGHTTRIGGERBEHAVIOR, controllerKbmRightTriggerBehavior);
    settings.setValue(SER_CONTROLLERKBMRIGHTTRIGGERREPEATRATE, controllerKbmRightTriggerRepeatRate);
    settings.setValue(SER_CONTROLLERKBMSHORTCUT, controllerKbmShortcut);
    settings.setValue(SER_CONTROLLERKBMGYROENABLED, controllerKbmGyroEnabled);
    settings.setValue(SER_CONTROLLERKBMGYROSENSITIVITY, controllerKbmGyroSensitivity);
    settings.setValue(SER_CONTROLLERKBMGYROSHORTCUT, controllerKbmGyroShortcut);
    settings.setValue(SER_CONTROLLERKBMGYROHOLDMODE, controllerKbmGyroHoldMode);
    settings.setValue(SER_CONTROLLERKBMGYROACTIVATIONBUTTONS, controllerKbmGyroActivationButtons);
    settings.setValue(SER_DUALSENSECONTROLLERVOLUME,dualSenseControllerVolume);settings.setValue(SER_TRIGGEROVERRIDEENABLED,triggerOverrideEnabled);settings.setValue(SER_LEFTTRIGGEROVERRIDETHRESHOLD,leftTriggerOverrideThreshold);settings.setValue(SER_RIGHTTRIGGEROVERRIDETHRESHOLD,rightTriggerOverrideThreshold);
    settings.setValue(SER_CONTROLLERKBMGYROXSENSITIVITY, controllerKbmGyroXSensitivity); settings.setValue(SER_CONTROLLERKBMGYROYSENSITIVITY, controllerKbmGyroYSensitivity); settings.setValue(SER_CONTROLLERKBMGYROZSENSITIVITY, controllerKbmGyroZSensitivity);
    settings.setValue(SER_CONTROLLERKBMGYROXINVERTED, controllerKbmGyroXInverted); settings.setValue(SER_CONTROLLERKBMGYROYINVERTED, controllerKbmGyroYInverted); settings.setValue(SER_CONTROLLERKBMGYROZINVERTED, controllerKbmGyroZInverted);
    settings.setValue(SER_QUICKMENUKEYBOARDKEY, quickMenuKeyboardKey);
    settings.setValue(SER_QUICKMENUKEYBOARDMODIFIERS, quickMenuKeyboardModifiers);
    settings.setValue(SER_MICMUTEKEYBOARDKEY,microphoneMuteKeyboardKey);settings.setValue(SER_MICMUTEKEYBOARDMODIFIERS,microphoneMuteKeyboardModifiers);
    settings.setValue(SER_CONTROLLERKBMMAPPINGS,
                      QJsonDocument::fromVariant(controllerKbmMappings).toJson(QJsonDocument::Compact));
    settings.setValue(SER_CONTROLLERKBMPRESETS,
                      QJsonDocument::fromVariant(controllerKbmPresets).toJson(QJsonDocument::Compact));
    settings.setValue(SER_CONTROLLERKBMACTIVEPRESET, controllerKbmActivePresetIndex);
    settings.setValue(SER_CAPTURESYSKEYS, captureSysKeysMode);
    settings.setValue(SER_KEEPAWAKE, keepAwake);
}

QString StreamingPreferences::controllerKbmAction(const QString& source) const
{
    return controllerKbmMappings.value(source).toString();
}

QString StreamingPreferences::quickMenuKeyboardShortcutDescription() const
{
    QStringList parts;
    if (quickMenuKeyboardModifiers & 0x01) parts << QStringLiteral("Ctrl");
    if (quickMenuKeyboardModifiers & 0x02) parts << QStringLiteral("Alt");
    if (quickMenuKeyboardModifiers & 0x04) parts << QStringLiteral("Shift");
    if (quickMenuKeyboardModifiers & 0x08) parts << QStringLiteral("Win");
    parts << QKeySequence(quickMenuKeyboardKey).toString(QKeySequence::NativeText);
    return parts.join(QStringLiteral(" + "));
}

QString StreamingPreferences::microphoneMuteKeyboardShortcutDescription() const
{
    QStringList parts;
    if(microphoneMuteKeyboardModifiers&1)parts<<"Ctrl";if(microphoneMuteKeyboardModifiers&2)parts<<"Alt";if(microphoneMuteKeyboardModifiers&4)parts<<"Shift";if(microphoneMuteKeyboardModifiers&8)parts<<"Win";
    parts<<QKeySequence(microphoneMuteKeyboardKey).toString(QKeySequence::NativeText);return parts.join(" + ");
}

QString StreamingPreferences::quickMenuControllerShortcutDescription() const
{
    static const QHash<int, QString> names = {
        {0, QStringLiteral("A / Cross")}, {1, QStringLiteral("B / Circle")},
        {2, QStringLiteral("X / Square")}, {3, QStringLiteral("Y / Triangle")},
        {4, QStringLiteral("Share / Create")}, {5, QStringLiteral("Home / PS")},
        {6, QStringLiteral("Start / Options")}, {9, QStringLiteral("LB")},
        {10, QStringLiteral("RB")}, {15, QStringLiteral("Mute")},
        {20, QStringLiteral("Touchpad")}
    };
    QStringList parts;
    for (const QString& value : controllerKbmShortcut.split(',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const int index = value.toInt(&ok);
        if (ok) parts << names.value(index, QString::number(index));
    }
    return parts.join(QStringLiteral(" + "));
}

void StreamingPreferences::setControllerKbmAction(const QString& source, const QString& action)
{
    if (source.isEmpty() || controllerKbmMappings.value(source).toString() == action) {
        return;
    }
    if (action.isEmpty()) {
        controllerKbmMappings.remove(source);
    }
    else {
        controllerKbmMappings.insert(source, action);
    }
    persistControllerKbmData();
    emit controllerKbmMappingsChanged();
}

QString StreamingPreferences::controllerKbmActionDescription(const QString& action) const
{
    if (action.isEmpty()) return tr("Unassigned");
    if (action.startsWith(QStringLiteral("key:"))) {
        bool ok = false;
        const int key = action.mid(4).toInt(&ok);
        if (ok) {
            QString name;
#ifdef Q_OS_WIN
            const UINT scanCode = MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
            wchar_t buffer[128] = {};
            LONG keyNameParam = static_cast<LONG>(scanCode << 16);
            if (key == VK_LEFT || key == VK_UP || key == VK_RIGHT || key == VK_DOWN ||
                    key == VK_PRIOR || key == VK_NEXT || key == VK_END || key == VK_HOME ||
                    key == VK_INSERT || key == VK_DELETE || key == VK_DIVIDE || key == VK_NUMLOCK) {
                keyNameParam |= (1 << 24);
            }
            if (GetKeyNameTextW(keyNameParam, buffer, static_cast<int>(std::size(buffer))) > 0) {
                name = QString::fromWCharArray(buffer);
            }
#else
            name = QKeySequence(key).toString(QKeySequence::NativeText);
#endif
            return tr("Keyboard: %1").arg(name.isEmpty() ? QString::number(key) : name);
        }
    }
    if (action.startsWith(QStringLiteral("directed_flick:"))) {
        const QStringList parts = action.split(':');
        if (parts.size() == 3) {
            return tr("Directed flick: %1, %2 px").arg(parts[1], parts[2]);
        }
    }
    static const QHash<QString, QString> labels = {
        { QStringLiteral("mouse_left"), tr("Left mouse click") },
        { QStringLiteral("mouse_right"), tr("Right mouse click") },
        { QStringLiteral("mouse_middle"), tr("Middle mouse click") },
        { QStringLiteral("mouse_back"), tr("Mouse back button") },
        { QStringLiteral("mouse_forward"), tr("Mouse forward button") },
        { QStringLiteral("wheel_up"), tr("Mouse wheel up") },
        { QStringLiteral("wheel_down"), tr("Mouse wheel down") },
        { QStringLiteral("mouse_move"), tr("Mouse movement") },
        { QStringLiteral("scroll"), tr("Two-axis scrolling") },
        { QStringLiteral("basic_wasd"), tr("Basic movement: WASD") },
        { QStringLiteral("basic_arrows"), tr("Basic movement: arrow keys") },
    };
    return labels.value(action, action);
}

void StreamingPreferences::resetControllerKbmMappings()
{
    if (controllerKbmMappings.isEmpty()) return;
    controllerKbmMappings.clear();
    persistControllerKbmData();
    emit controllerKbmMappingsChanged();
}

void StreamingPreferences::refreshControllerKbmPresetNames()
{
    controllerKbmPresetNames.clear();
    for (const QVariant& value : std::as_const(controllerKbmPresets)) {
        controllerKbmPresetNames.append(value.toMap().value(QStringLiteral("name")).toString());
    }
}

void StreamingPreferences::persistControllerKbmData()
{
    QSettings settings;
    settings.setValue(SER_CONTROLLERKBMMODE, controllerKbmMode);
    settings.setValue(SER_CONTROLLERKBMSTICKSPEED, controllerKbmStickSpeed);
    settings.setValue(SER_CONTROLLERKBMCONTINUOUS, controllerKbmContinuousStickMouse);
    settings.setValue(SER_CONTROLLERKBMLEFTTRIGGERTHRESHOLD, controllerKbmLeftTriggerThreshold);
    settings.setValue(SER_CONTROLLERKBMLEFTTRIGGERBEHAVIOR, controllerKbmLeftTriggerBehavior);
    settings.setValue(SER_CONTROLLERKBMLEFTTRIGGERREPEATRATE, controllerKbmLeftTriggerRepeatRate);
    settings.setValue(SER_CONTROLLERKBMRIGHTTRIGGERTHRESHOLD, controllerKbmRightTriggerThreshold);
    settings.setValue(SER_CONTROLLERKBMRIGHTTRIGGERBEHAVIOR, controllerKbmRightTriggerBehavior);
    settings.setValue(SER_CONTROLLERKBMRIGHTTRIGGERREPEATRATE, controllerKbmRightTriggerRepeatRate);
    settings.setValue(SER_CONTROLLERKBMGYROENABLED, controllerKbmGyroEnabled);
    settings.setValue(SER_CONTROLLERKBMGYROSENSITIVITY, controllerKbmGyroSensitivity);
    settings.setValue(SER_CONTROLLERKBMGYROSHORTCUT, controllerKbmGyroShortcut);
    settings.setValue(SER_CONTROLLERKBMGYROHOLDMODE, controllerKbmGyroHoldMode);
    settings.setValue(SER_CONTROLLERKBMGYROACTIVATIONBUTTONS, controllerKbmGyroActivationButtons);
    settings.setValue(SER_CONTROLLERKBMGYROXSENSITIVITY, controllerKbmGyroXSensitivity); settings.setValue(SER_CONTROLLERKBMGYROYSENSITIVITY, controllerKbmGyroYSensitivity); settings.setValue(SER_CONTROLLERKBMGYROZSENSITIVITY, controllerKbmGyroZSensitivity);
    settings.setValue(SER_CONTROLLERKBMGYROXINVERTED, controllerKbmGyroXInverted); settings.setValue(SER_CONTROLLERKBMGYROYINVERTED, controllerKbmGyroYInverted); settings.setValue(SER_CONTROLLERKBMGYROZINVERTED, controllerKbmGyroZInverted);
    settings.setValue(SER_CONTROLLERKBMMAPPINGS,
                      QJsonDocument::fromVariant(controllerKbmMappings).toJson(QJsonDocument::Compact));
    settings.setValue(SER_CONTROLLERKBMPRESETS,
                      QJsonDocument::fromVariant(controllerKbmPresets).toJson(QJsonDocument::Compact));
    settings.setValue(SER_CONTROLLERKBMACTIVEPRESET, controllerKbmActivePresetIndex);
}

void StreamingPreferences::commitControllerKbmSettings()
{
    persistControllerKbmData();
}

void StreamingPreferences::assignControllerKbmKeyboardKey(const QString& source, int qtKey,
                                                           int nativeVirtualKey, int nativeScanCode)
{
    int virtualKey = nativeVirtualKey;
#ifdef Q_OS_WIN
    if ((virtualKey <= 0 || virtualKey > 0xFF) && nativeScanCode > 0) {
        virtualKey = static_cast<int>(MapVirtualKeyW(static_cast<UINT>(nativeScanCode),
                                                     MAPVK_VSC_TO_VK_EX));
    }
    if (virtualKey <= 0 || virtualKey > 0xFF) {
        switch (qtKey) {
        case Qt::Key_Shift: virtualKey = VK_SHIFT; break;
        case Qt::Key_Control: virtualKey = VK_CONTROL; break;
        case Qt::Key_Alt: virtualKey = VK_MENU; break;
        case Qt::Key_Meta: virtualKey = VK_LWIN; break;
        case Qt::Key_Backspace: virtualKey = VK_BACK; break;
        case Qt::Key_Delete: virtualKey = VK_DELETE; break;
        case Qt::Key_Insert: virtualKey = VK_INSERT; break;
        case Qt::Key_Tab: virtualKey = VK_TAB; break;
        case Qt::Key_Return: case Qt::Key_Enter: virtualKey = VK_RETURN; break;
        case Qt::Key_Escape: virtualKey = VK_ESCAPE; break;
        case Qt::Key_Home: virtualKey = VK_HOME; break;
        case Qt::Key_End: virtualKey = VK_END; break;
        case Qt::Key_PageUp: virtualKey = VK_PRIOR; break;
        case Qt::Key_PageDown: virtualKey = VK_NEXT; break;
        case Qt::Key_Left: virtualKey = VK_LEFT; break;
        case Qt::Key_Right: virtualKey = VK_RIGHT; break;
        case Qt::Key_Up: virtualKey = VK_UP; break;
        case Qt::Key_Down: virtualKey = VK_DOWN; break;
        default:
            if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24)
                virtualKey = VK_F1 + (qtKey - Qt::Key_F1);
            else if (qtKey >= 0x20 && qtKey <= 0x7E)
                virtualKey = qtKey;
            break;
        }
    }
#else
    if (virtualKey <= 0) virtualKey = qtKey;
#endif
    if (virtualKey > 0)
        setControllerKbmAction(source, QStringLiteral("key:%1").arg(virtualKey));
}

static QVariantMap currentControllerKbmPresetData(const StreamingPreferences* prefs)
{
    QVariantMap preset;
    preset.insert(QStringLiteral("mappings"), prefs->controllerKbmMappings);
    preset.insert(QStringLiteral("stickSpeed"), prefs->controllerKbmStickSpeed);
    preset.insert(QStringLiteral("continuousStickMouse"), prefs->controllerKbmContinuousStickMouse);
    preset.insert(QStringLiteral("leftTriggerThreshold"), prefs->controllerKbmLeftTriggerThreshold);
    preset.insert(QStringLiteral("leftTriggerBehavior"), prefs->controllerKbmLeftTriggerBehavior);
    preset.insert(QStringLiteral("leftTriggerRepeatRate"), prefs->controllerKbmLeftTriggerRepeatRate);
    preset.insert(QStringLiteral("rightTriggerThreshold"), prefs->controllerKbmRightTriggerThreshold);
    preset.insert(QStringLiteral("rightTriggerBehavior"), prefs->controllerKbmRightTriggerBehavior);
    preset.insert(QStringLiteral("rightTriggerRepeatRate"), prefs->controllerKbmRightTriggerRepeatRate);
    preset.insert(QStringLiteral("gyroEnabled"), prefs->controllerKbmGyroEnabled);
    preset.insert(QStringLiteral("gyroSensitivity"), prefs->controllerKbmGyroSensitivity);
    preset.insert(QStringLiteral("gyroShortcut"), prefs->controllerKbmGyroShortcut);
    preset.insert(QStringLiteral("gyroHoldMode"), prefs->controllerKbmGyroHoldMode);
    preset.insert(QStringLiteral("gyroActivationButtons"), prefs->controllerKbmGyroActivationButtons);
    preset.insert(QStringLiteral("gyroXSensitivity"), prefs->controllerKbmGyroXSensitivity); preset.insert(QStringLiteral("gyroYSensitivity"), prefs->controllerKbmGyroYSensitivity); preset.insert(QStringLiteral("gyroZSensitivity"), prefs->controllerKbmGyroZSensitivity);
    preset.insert(QStringLiteral("gyroXInverted"), prefs->controllerKbmGyroXInverted); preset.insert(QStringLiteral("gyroYInverted"), prefs->controllerKbmGyroYInverted); preset.insert(QStringLiteral("gyroZInverted"), prefs->controllerKbmGyroZInverted);
    return preset;
}

bool StreamingPreferences::saveControllerKbmPreset(const QString& requestedName)
{
    const QString baseName = requestedName.trimmed();
    if (baseName.isEmpty()) return false;
    QString name = baseName;
    int duplicate = 1;
    while (controllerKbmPresetNames.contains(name, Qt::CaseInsensitive)) {
        name = QStringLiteral("%1 (%2)").arg(baseName).arg(duplicate++);
    }
    QVariantMap preset;
    preset.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    preset.insert(QStringLiteral("name"), name);
    const QVariantMap data = currentControllerKbmPresetData(this);
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) preset.insert(it.key(), it.value());
    controllerKbmPresets.append(preset);
    controllerKbmActivePresetIndex = controllerKbmPresets.size() - 1;
    refreshControllerKbmPresetNames();
    persistControllerKbmData();
    emit controllerKbmPresetsChanged();
    return true;
}

bool StreamingPreferences::updateControllerKbmPreset(int index)
{
    if (index < 0 || index >= controllerKbmPresets.size()) return false;
    QVariantMap preset = controllerKbmPresets.at(index).toMap();
    const QVariantMap data = currentControllerKbmPresetData(this);
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) preset.insert(it.key(), it.value());
    controllerKbmPresets[index] = preset;
    controllerKbmActivePresetIndex = index;
    persistControllerKbmData();
    emit controllerKbmPresetsChanged();
    return true;
}

bool StreamingPreferences::loadControllerKbmPreset(int index)
{
    if (index < 0 || index >= controllerKbmPresets.size()) return false;
    controllerKbmMappings = controllerKbmPresets.at(index).toMap()
            .value(QStringLiteral("mappings")).toMap();
    const QVariantMap preset = controllerKbmPresets.at(index).toMap();
    controllerKbmStickSpeed = preset.value(QStringLiteral("stickSpeed"), controllerKbmStickSpeed).toInt();
    controllerKbmContinuousStickMouse = preset.value(QStringLiteral("continuousStickMouse"), controllerKbmContinuousStickMouse).toBool();
    controllerKbmLeftTriggerThreshold = preset.value(QStringLiteral("leftTriggerThreshold"), controllerKbmLeftTriggerThreshold).toInt();
    controllerKbmLeftTriggerBehavior = preset.value(QStringLiteral("leftTriggerBehavior"), controllerKbmLeftTriggerBehavior).toString();
    controllerKbmLeftTriggerRepeatRate = preset.value(QStringLiteral("leftTriggerRepeatRate"), controllerKbmLeftTriggerRepeatRate).toInt();
    controllerKbmRightTriggerThreshold = preset.value(QStringLiteral("rightTriggerThreshold"), controllerKbmRightTriggerThreshold).toInt();
    controllerKbmRightTriggerBehavior = preset.value(QStringLiteral("rightTriggerBehavior"), controllerKbmRightTriggerBehavior).toString();
    controllerKbmRightTriggerRepeatRate = preset.value(QStringLiteral("rightTriggerRepeatRate"), controllerKbmRightTriggerRepeatRate).toInt();
    controllerKbmGyroEnabled = preset.value(QStringLiteral("gyroEnabled"), controllerKbmGyroEnabled).toBool();
    controllerKbmGyroSensitivity = preset.value(QStringLiteral("gyroSensitivity"), controllerKbmGyroSensitivity).toInt();
    controllerKbmGyroShortcut = preset.value(QStringLiteral("gyroShortcut"), controllerKbmGyroShortcut).toString();
    controllerKbmGyroHoldMode = preset.value(QStringLiteral("gyroHoldMode"), controllerKbmGyroHoldMode).toBool();
    controllerKbmGyroActivationButtons = preset.value(QStringLiteral("gyroActivationButtons"), controllerKbmGyroActivationButtons).toString();
    controllerKbmGyroXSensitivity = preset.value(QStringLiteral("gyroXSensitivity"), controllerKbmGyroXSensitivity).toInt(); controllerKbmGyroYSensitivity = preset.value(QStringLiteral("gyroYSensitivity"), controllerKbmGyroYSensitivity).toInt(); controllerKbmGyroZSensitivity = preset.value(QStringLiteral("gyroZSensitivity"), controllerKbmGyroZSensitivity).toInt();
    controllerKbmGyroXInverted = preset.value(QStringLiteral("gyroXInverted"), controllerKbmGyroXInverted).toBool(); controllerKbmGyroYInverted = preset.value(QStringLiteral("gyroYInverted"), controllerKbmGyroYInverted).toBool(); controllerKbmGyroZInverted = preset.value(QStringLiteral("gyroZInverted"), controllerKbmGyroZInverted).toBool();
    controllerKbmActivePresetIndex = index;
    persistControllerKbmData();
    emit controllerKbmMappingsChanged();
    emit controllerKbmSettingsChanged();
    emit controllerKbmPresetsChanged();
    return true;
}

bool StreamingPreferences::deleteControllerKbmPreset(int index)
{
    if (index < 0 || index >= controllerKbmPresets.size()) return false;
    controllerKbmPresets.removeAt(index);
    if (controllerKbmActivePresetIndex == index) controllerKbmActivePresetIndex = -1;
    else if (controllerKbmActivePresetIndex > index) --controllerKbmActivePresetIndex;
    refreshControllerKbmPresetNames();
    persistControllerKbmData();
    emit controllerKbmPresetsChanged();
    return true;
}

bool StreamingPreferences::exportControllerKbmPreset(int index, const QUrl& destination) const
{
    if (index < 0 || index >= controllerKbmPresets.size() || !destination.isLocalFile()) return false;
    const QVariantMap preset = controllerKbmPresets.at(index).toMap();
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("moonlight-extended-controller-kbm-preset"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("name"), preset.value(QStringLiteral("name")).toString());
    root.insert(QStringLiteral("mappings"), QJsonObject::fromVariantMap(
                    preset.value(QStringLiteral("mappings")).toMap()));
    const QStringList fields = {QStringLiteral("stickSpeed"), QStringLiteral("continuousStickMouse"),
        QStringLiteral("leftTriggerThreshold"), QStringLiteral("leftTriggerBehavior"), QStringLiteral("leftTriggerRepeatRate"),
        QStringLiteral("rightTriggerThreshold"), QStringLiteral("rightTriggerBehavior"), QStringLiteral("rightTriggerRepeatRate"),
        QStringLiteral("gyroEnabled"), QStringLiteral("gyroSensitivity"), QStringLiteral("gyroShortcut"), QStringLiteral("gyroHoldMode"), QStringLiteral("gyroActivationButtons"),
        QStringLiteral("gyroXSensitivity"),QStringLiteral("gyroYSensitivity"),QStringLiteral("gyroZSensitivity"),QStringLiteral("gyroXInverted"),QStringLiteral("gyroYInverted"),QStringLiteral("gyroZInverted")};
    for (const QString& field : fields)
        if (preset.contains(field)) root.insert(field, QJsonValue::fromVariant(preset.value(field)));
    QFile file(destination.toLocalFile());
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) >= 0;
}

bool StreamingPreferences::importControllerKbmPreset(const QUrl& source)
{
    if (!source.isLocalFile()) return false;
    QFile file(source.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("format")).toString() !=
            QStringLiteral("moonlight-extended-controller-kbm-preset") ||
            root.value(QStringLiteral("version")).toInt() != 1 ||
            !root.value(QStringLiteral("mappings")).isObject()) return false;
    const QString baseName = root.value(QStringLiteral("name")).toString().trimmed();
    if (baseName.isEmpty()) return false;
    QString name = baseName;
    int duplicate = 1;
    while (controllerKbmPresetNames.contains(name, Qt::CaseInsensitive)) {
        name = QStringLiteral("%1 (%2)").arg(baseName).arg(duplicate++);
    }
    QVariantMap preset;
    preset.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    preset.insert(QStringLiteral("name"), name);
    preset.insert(QStringLiteral("mappings"), root.value(QStringLiteral("mappings")).toObject().toVariantMap());
    const QStringList fields = {QStringLiteral("stickSpeed"), QStringLiteral("continuousStickMouse"),
        QStringLiteral("leftTriggerThreshold"), QStringLiteral("leftTriggerBehavior"), QStringLiteral("leftTriggerRepeatRate"),
        QStringLiteral("rightTriggerThreshold"), QStringLiteral("rightTriggerBehavior"), QStringLiteral("rightTriggerRepeatRate"),
        QStringLiteral("gyroEnabled"), QStringLiteral("gyroSensitivity"), QStringLiteral("gyroShortcut"), QStringLiteral("gyroHoldMode"), QStringLiteral("gyroActivationButtons"),
        QStringLiteral("gyroXSensitivity"),QStringLiteral("gyroYSensitivity"),QStringLiteral("gyroZSensitivity"),QStringLiteral("gyroXInverted"),QStringLiteral("gyroYInverted"),QStringLiteral("gyroZInverted")};
    for (const QString& field : fields)
        if (root.contains(field)) preset.insert(field, root.value(field).toVariant());
    controllerKbmPresets.append(preset);
    controllerKbmActivePresetIndex = controllerKbmPresets.size() - 1;
    refreshControllerKbmPresetNames();
    persistControllerKbmData();
    emit controllerKbmPresetsChanged();
    return true;
}

QStringList StreamingPreferences::microphoneDeviceNames() const
{
    QStringList devices;
    const bool initializedHere = (SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0;
    if (initializedHere && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        qWarning() << "Unable to initialize SDL audio while enumerating microphones:" << SDL_GetError();
        return devices;
    }
    const int count = SDL_GetNumAudioDevices(1);
    for (int i = 0; i < count; i++) {
        const char* name = SDL_GetAudioDeviceName(i, 1);
        if (name != nullptr) {
            devices.append(QString::fromUtf8(name));
        }
    }
    devices.removeDuplicates();
    if (initializedHere) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    return devices;
}

void StreamingPreferences::requestStreamQuickAction(const QString& action)
{
    if (Session* session = Session::get()) {
        session->handleStreamQuickAction(action);
    }
}

int StreamingPreferences::getDefaultBitrate(int width, int height, int fps, bool yuv444)
{
    // Don't scale bitrate linearly beyond 60 FPS. It's definitely not a linear
    // bitrate increase for frame rate once we get to values that high.
    float frameRateFactor = (fps <= 60 ? fps : (qSqrt(fps / 60.f) * 60.f)) / 30.f;

    // TODO: Collect some empirical data to see if these defaults make sense.
    // We're just using the values that the Shield used, as we have for years.
    static const struct resTable {
        int pixels;
        int factor;
    } resTable[] {
        { 640 * 360, 1 },
        { 854 * 480, 2 },
        { 1280 * 720, 5 },
        { 1920 * 1080, 10 },
        { 2560 * 1440, 20 },
        { 3840 * 2160, 40 },
        { -1, -1 },
    };

    // Calculate the resolution factor by linear interpolation of the resolution table
    float resolutionFactor;
    int pixels = width * height;
    for (int i = 0;; i++) {
        if (pixels == resTable[i].pixels) {
            // We can bail immediately for exact matches
            resolutionFactor = resTable[i].factor;
            break;
        }
        else if (pixels < resTable[i].pixels) {
            if (i == 0) {
                // Never go below the lowest resolution entry
                resolutionFactor = resTable[i].factor;
            }
            else {
                // Interpolate between the entry greater than the chosen resolution (i) and the entry less than the chosen resolution (i-1)
                resolutionFactor = ((float)(pixels - resTable[i-1].pixels) / (resTable[i].pixels - resTable[i-1].pixels)) * (resTable[i].factor - resTable[i-1].factor) + resTable[i-1].factor;
            }
            break;
        }
        else if (resTable[i].pixels == -1) {
            // Never go above the highest resolution entry
            resolutionFactor = resTable[i-1].factor;
            break;
        }
    }

    if (yuv444) {
        // This is rough estimation based on the fact that 4:4:4 doubles the amount of raw YUV data compared to 4:2:0
        resolutionFactor *= 2;
    }

    return qRound(resolutionFactor * frameRateFactor) * 1000;
}
