#include "streaming/session.h"

#include <Limelight.h>
#include "SDL_compat.h"
#include "settings/mappingmanager.h"
#include "streaming/audio/dualsenseaudio.h"
#include "streaming/audio/miccapture.h"
#include "streaming/audio/renderers/sdl.h"

#include <QtMath>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <cstring>
#include <cmath>

// How long the Start button must be pressed to toggle mouse emulation
#define MOUSE_EMULATION_LONG_PRESS_TIME 750

// How long between polling the gamepad to send virtual mouse input
#define MOUSE_EMULATION_POLLING_INTERVAL 50

// Determines how fast the mouse will move each interval
#define MOUSE_EMULATION_MOTION_MULTIPLIER 4

// Determines the maximum motion amount before allowing movement
#define MOUSE_EMULATION_DEADZONE 2

// Haptic capabilities (in addition to those from SDL_HapticQuery())
#define ML_HAPTIC_GC_RUMBLE         (1U << 16)
#define ML_HAPTIC_SIMPLE_RUMBLE     (1U << 17)
#define ML_HAPTIC_GC_TRIGGER_RUMBLE (1U << 18)

// Apollo Extended controller-generation request encoded in the legacy
// controller capability field. Older hosts safely ignore these upper bits.
#define APOLLO_EXTENDED_EMULATION_MAGIC 0xEC00
#define APOLLO_EXTENDED_EMULATION_XBOX  1
#define APOLLO_EXTENDED_EMULATION_DS4   2
#define APOLLO_EXTENDED_EMULATION_DS5   3

static bool sendDualSenseEffect(SDL_GameController* controller, const uint8_t* effect)
{
    return SDL_GameControllerSendEffect(controller, effect, sizeof(DualSenseOutputReport)) == 0;
}

static bool queryDualSenseHeadset(SDL_GameController* controller)
{
    static const uint8_t query[8] = { 'A', 'R', 'T', 'J', 'A', 'C', 'K', '?' };
    return SDL_GameControllerSendEffect(controller, query, sizeof(query)) == 0;
}

static void activateDualSenseAudio(SDL_GameController* controller,
                                   StreamingPreferences::DualSenseAudioMode mode)
{
    if (mode == StreamingPreferences::DSAM_OFF) {
        return;
    }

    static_assert(sizeof(DualSenseOutputReport) > 43,
                  "DualSense output report must contain the DSP wake fields");

    // SDL takes the native USB payload without report ID 0x02. Consequently,
    // these offsets are one less than the corresponding raw HID report offsets.
    const bool headset = mode == StreamingPreferences::DSAM_USB_HEADSET;
    uint8_t route[sizeof(DualSenseOutputReport)] = {};
    route[0] = 0xf3;
    route[4] = 0xff;
    if (!headset) {
        route[5] = 0xff;
        // The DualSense microphone gain is a separate 0x00-0x40 field.
        // Keep the audio-control byte limited to internal-mic routing: 0xff
        // also enables the microphone attenuation bits and makes USB capture
        // extremely quiet before our software gain is ever applied.
        route[6] = 0x40;
        route[7] = 0x01;
    }

    bool routeOk = sendDualSenseEffect(controller, route);
    if (!headset) {
        SDL_Delay(40);
        routeOk = sendDualSenseEffect(controller, route) && routeOk;
    }

    SDL_Delay(35);
    uint8_t musicRumble[sizeof(DualSenseOutputReport)] = {};
    musicRumble[0] = 0xe0;
    musicRumble[4] = 0x7f;
    musicRumble[5] = headset ? 0x00 : 0xff;
    musicRumble[6] = 0x40;
    musicRumble[7] = headset ? 0x00 : 0x30;
    const bool musicOk = sendDualSenseEffect(controller, musicRumble);

    SDL_Delay(35);
    uint8_t wake[sizeof(DualSenseOutputReport)] = {};
    wake[1] = 0x15;
    wake[38] = 0x03;
    wake[41] = 0x02;
    wake[43] = 0x24;
    const bool wakeOk = sendDualSenseEffect(controller, wake);

    if (!headset) {
        SDL_Delay(35);
        routeOk = sendDualSenseEffect(controller, route) && routeOk;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "DualSense audio activation: mode=%d route=%s music-rumble=%s dsp-wake=%s",
                static_cast<int>(mode),
                routeOk ? "ok" : "failed",
                musicOk ? "ok" : "failed",
                wakeOk ? "ok" : "failed");
}

const int SdlInputHandler::k_ButtonMap[] = {
    A_FLAG, B_FLAG, X_FLAG, Y_FLAG,
    BACK_FLAG, SPECIAL_FLAG, PLAY_FLAG,
    LS_CLK_FLAG, RS_CLK_FLAG,
    LB_FLAG, RB_FLAG,
    UP_FLAG, DOWN_FLAG, LEFT_FLAG, RIGHT_FLAG,
    MISC_FLAG,
    PADDLE1_FLAG, PADDLE2_FLAG, PADDLE3_FLAG, PADDLE4_FLAG,
    TOUCHPAD_FLAG,
};

GamepadState*
SdlInputHandler::findStateForGamepad(SDL_JoystickID id)
{
    int i;

    for (i = 0; i < MAX_GAMEPADS; i++) {
        if (m_GamepadState[i].jsId == id) {
            SDL_assert(!m_MultiController || m_GamepadState[i].index == i);
            return &m_GamepadState[i];
        }
    }

    // We can get a spurious removal event if the device is removed
    // before or during SDL_GameControllerOpen(). This is fine to ignore.
    return nullptr;
}

void SdlInputHandler::sendGamepadState(GamepadState* state)
{
    if (m_ControllerKbmMode) {
        return;
    }
    SDL_assert(m_GamepadMask == 0x1 || m_MultiController);

    // Handle Select+PS as the clickpad button on PS4/5 controllers without a clickpad mapping
    int buttons = state->buttons;
    if (state->clickpadButtonEmulationEnabled) {
        if (state->buttons == (BACK_FLAG | SPECIAL_FLAG)) {
            buttons = MISC_FLAG;
            state->emulatedClickpadButtonDown = true;
        }
        else if (state->emulatedClickpadButtonDown) {
            buttons &= ~MISC_FLAG;
            state->emulatedClickpadButtonDown = false;
        }
    }

    unsigned char lt = state->lt;
    unsigned char rt = state->rt;
    short lsX = state->lsX;
    short lsY = state->lsY;
    short rsX = state->rsX;
    short rsY = state->rsY;

    // When in single controller mode, merge all gamepad state together
    if (!m_MultiController) {
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (m_GamepadState[i].index == state->index) {
                buttons |= m_GamepadState[i].buttons;
                if (lt < m_GamepadState[i].lt) {
                    lt = m_GamepadState[i].lt;
                }
                if (rt < m_GamepadState[i].rt) {
                    rt = m_GamepadState[i].rt;
                }

                // We use abs() here instead of qAbs() for get proper integer promotion to
                // correctly handle abs(-32768), which is not representable in a short.
                if (abs(lsX) < abs(m_GamepadState[i].lsX) || abs(lsY) < abs(m_GamepadState[i].lsY)) {
                    lsX = m_GamepadState[i].lsX;
                    lsY = m_GamepadState[i].lsY;
                }
                if (abs(rsX) < abs(m_GamepadState[i].rsX) || abs(rsY) < abs(m_GamepadState[i].rsY)) {
                    rsX = m_GamepadState[i].rsX;
                    rsY = m_GamepadState[i].rsY;
                }
            }
        }
    }

    LiSendMultiControllerEvent(state->index,
                               m_GamepadMask,
                               buttons,
                               lt,
                               rt,
                               lsX,
                               lsY,
                               rsX,
                               rsY);
}

static float kbmDeadzone(float value)
{
    const float magnitude = qAbs(value);
    return magnitude <= 0.08f ? 0.0f : std::copysign((magnitude - 0.08f) / 0.92f, value);
}

QString SdlInputHandler::controllerKbmSourceForButton(unsigned button) const
{
    static const char* const names[] = {
        "button_a", "button_b", "button_x", "button_y", "button_back", "button_guide",
        "button_start", "button_l3", "button_r3", "button_lb", "button_rb", "dpad_up",
        "dpad_down", "dpad_left", "dpad_right", "button_misc", "paddle_1", "paddle_2",
        "paddle_3", "paddle_4", "button_touchpad"
    };
    return button < SDL_arraysize(names) ? QString::fromLatin1(names[button]) : QString();
}

void SdlInputHandler::announceControllerAfterKbm(GamepadState* state)
{
    if (state == nullptr || state->controller == nullptr) return;
    uint8_t type = LI_CTYPE_UNKNOWN;
    switch (SDL_GameControllerGetType(state->controller)) {
    case SDL_CONTROLLER_TYPE_XBOX360: case SDL_CONTROLLER_TYPE_XBOXONE: type = LI_CTYPE_XBOX; break;
    case SDL_CONTROLLER_TYPE_PS3: case SDL_CONTROLLER_TYPE_PS4: case SDL_CONTROLLER_TYPE_PS5: type = LI_CTYPE_PS; break;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO: type = LI_CTYPE_NINTENDO; break;
    default: break;
    }
    uint32_t buttons = 0;
    for (int i = 0; i < static_cast<int>(SDL_arraysize(k_ButtonMap)); ++i) {
        if (SDL_GameControllerHasButton(state->controller, static_cast<SDL_GameControllerButton>(i))) buttons |= k_ButtonMap[i];
    }
    uint32_t capabilities = APOLLO_EXTENDED_EMULATION_MAGIC;
    int mode = 0;
    if (m_ControllerEmulationMode == StreamingPreferences::CEM_XBOX) mode = APOLLO_EXTENDED_EMULATION_XBOX;
    else if (m_ControllerEmulationMode == StreamingPreferences::CEM_DUALSHOCK4) mode = APOLLO_EXTENDED_EMULATION_DS4;
    else if (m_ControllerEmulationMode == StreamingPreferences::CEM_DUALSENSE) mode = APOLLO_EXTENDED_EMULATION_DS5;
    else if (SDL_GameControllerGetType(state->controller) == SDL_CONTROLLER_TYPE_PS5) mode = APOLLO_EXTENDED_EMULATION_DS5;
    else if (type == LI_CTYPE_PS) mode = APOLLO_EXTENDED_EMULATION_DS4;
    else if (type == LI_CTYPE_XBOX) mode = APOLLO_EXTENDED_EMULATION_XBOX;
    capabilities |= static_cast<uint32_t>(mode << 8);
    if (SDL_GameControllerGetBindForAxis(state->controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT).bindType == SDL_CONTROLLER_BINDTYPE_AXIS ||
            SDL_GameControllerGetBindForAxis(state->controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT).bindType == SDL_CONTROLLER_BINDTYPE_AXIS)
        capabilities |= LI_CCAP_ANALOG_TRIGGERS;
    if (SDL_GameControllerGetNumTouchpads(state->controller) > 0) capabilities |= LI_CCAP_TOUCHPAD;
    if (SDL_GameControllerHasSensor(state->controller, SDL_SENSOR_ACCEL)) capabilities |= LI_CCAP_ACCEL;
    if (SDL_GameControllerHasSensor(state->controller, SDL_SENSOR_GYRO)) capabilities |= LI_CCAP_GYRO;
    if (SDL_GameControllerHasLED(state->controller)) capabilities |= LI_CCAP_RGB_LED;
    LiSendControllerArrivalEvent(state->index, m_GamepadMask, type, buttons, capabilities);
    sendGamepadState(state);
}

void SdlInputHandler::syncControllerKbmSettings()
{
    QSettings settings;
    const bool wasEnabled = m_ControllerKbmMode;
    const bool enabled = settings.value(QStringLiteral("controllerkbmmode"), false).toBool();
    m_ControllerKbmStickSpeed = settings.value(QStringLiteral("controllerkbmstickspeed"), 100).toInt();
    m_ControllerKbmContinuous = settings.value(QStringLiteral("controllerkbmcontinuous"), true).toBool();
    m_ControllerKbmTriggerThresholds[0] = settings.value(QStringLiteral("controllerkbmlefttriggerthreshold"), 15).toInt();
    m_ControllerKbmTriggerThresholds[1] = settings.value(QStringLiteral("controllerkbmrighttriggerthreshold"), 15).toInt();
    m_ControllerKbmTriggerBehaviors[0] = settings.value(QStringLiteral("controllerkbmlefttriggerbehavior"), QStringLiteral("hold")).toString();
    m_ControllerKbmTriggerBehaviors[1] = settings.value(QStringLiteral("controllerkbmrighttriggerbehavior"), QStringLiteral("hold")).toString();
    m_ControllerKbmTriggerRepeatRates[0] = settings.value(QStringLiteral("controllerkbmlefttriggerrepeatrate"), 8).toInt();
    m_ControllerKbmTriggerRepeatRates[1] = settings.value(QStringLiteral("controllerkbmrighttriggerrepeatrate"), 8).toInt();
    m_ControllerKbmMappings.clear();
    const QVariantMap mappings = QJsonDocument::fromJson(settings.value(QStringLiteral("controllerkbmmappings"), QByteArray("{}")).toByteArray()).object().toVariantMap();
    for (auto it = mappings.constBegin(); it != mappings.constEnd(); ++it) m_ControllerKbmMappings.insert(it.key(), it.value().toString());
    const QString shortcut = settings.value(QStringLiteral("controllerkbmshortcut"), QStringLiteral("4,5")).toString();
    m_ControllerKbmShortcutMask = 0;
    for (const QString& button : shortcut.split(',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const int index = button.toInt(&ok);
        if (ok && index >= 0 && index < 32) m_ControllerKbmShortcutMask |= (1u << index);
    }
    m_ControllerKbmGyroEnabled = settings.value(QStringLiteral("controllerkbmgyroenabled"), false).toBool();
    m_ControllerKbmGyroHoldMode = settings.value(QStringLiteral("controllerkbmgyroholdmode"), false).toBool();
    m_ControllerKbmGyroActivationMask = 0;
    m_ControllerKbmGyroActivationTriggers[0] = m_ControllerKbmGyroActivationTriggers[1] = false;
    const QString activationButtons = settings.value(QStringLiteral("controllerkbmgyroactivationbuttons"), QString()).toString();
    for (const QString& button : activationButtons.split(',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const int index = button.toInt(&ok);
        if (!ok) continue;
        if (index >= 0 && index < 32) m_ControllerKbmGyroActivationMask |= (1u << index);
        else if (index == 100) m_ControllerKbmGyroActivationTriggers[0] = true;
        else if (index == 101) m_ControllerKbmGyroActivationTriggers[1] = true;
    }
    m_ControllerKbmGyroSensitivity = settings.value(QStringLiteral("controllerkbmgyrosensitivity"), 100).toInt();
    m_ControllerKbmGyroAxisSensitivity[0]=settings.value(QStringLiteral("controllerkbmgyroxsensitivity"),100).toInt(); m_ControllerKbmGyroAxisSensitivity[1]=settings.value(QStringLiteral("controllerkbmgyroysensitivity"),100).toInt(); m_ControllerKbmGyroAxisSensitivity[2]=settings.value(QStringLiteral("controllerkbmgyrozsensitivity"),100).toInt();
    m_ControllerKbmGyroAxisInverted[0]=settings.value(QStringLiteral("controllerkbmgyroxinverted"),false).toBool(); m_ControllerKbmGyroAxisInverted[1]=settings.value(QStringLiteral("controllerkbmgyroyinverted"),false).toBool(); m_ControllerKbmGyroAxisInverted[2]=settings.value(QStringLiteral("controllerkbmgyrozinverted"),false).toBool();
    m_GyroOverrideEnabled = settings.value(QStringLiteral("gyrooverrideenabled"), false).toBool();
    m_GyroAxisSource[0] = settings.value(QStringLiteral("gyroxsource"), 0).toInt();
    m_GyroAxisSource[1] = settings.value(QStringLiteral("gyroysource"), 1).toInt();
    m_GyroAxisSource[2] = settings.value(QStringLiteral("gyrozsource"), 2).toInt();
    m_GyroAxisInverted[0] = settings.value(QStringLiteral("gyroxinverted"), false).toBool();
    m_GyroAxisInverted[1] = settings.value(QStringLiteral("gyroyinverted"), false).toBool();
    m_GyroAxisInverted[2] = settings.value(QStringLiteral("gyrozininverted"), false).toBool();
    m_GyroAxisDisabled[0] = settings.value(QStringLiteral("gyroxdisabled"), false).toBool();
    m_GyroAxisDisabled[1] = settings.value(QStringLiteral("gyroydisabled"), false).toBool();
    m_GyroAxisDisabled[2] = settings.value(QStringLiteral("gyrozdisabled"), false).toBool();
    m_TriggerOverrideEnabled=settings.value(QStringLiteral("triggeroverrideenabled"),false).toBool(); m_TriggerOverrideThresholds[0]=settings.value(QStringLiteral("lefttriggeroverridethreshold"),50).toInt(); m_TriggerOverrideThresholds[1]=settings.value(QStringLiteral("righttriggeroverridethreshold"),50).toInt();
    m_ControllerKbmGyroShortcutMask = 0;
    const QString gyroShortcut = settings.value(QStringLiteral("controllerkbmgyroshortcut"), QStringLiteral("7,8")).toString();
    for (const QString& button : gyroShortcut.split(',', Qt::SkipEmptyParts)) {
        bool ok = false; const int index = button.toInt(&ok);
        if (ok && index >= 0 && index < 32) m_ControllerKbmGyroShortcutMask |= (1u << index);
    }
    if (wasEnabled == enabled) {
        for (GamepadState& state : m_GamepadState) {
            if (state.controller == nullptr) continue;
            if (enabled && m_ControllerKbmGyroEnabled && SDL_GameControllerHasSensor(state.controller, SDL_SENSOR_GYRO)) {
                state.gyroReportPeriodMs = 4;
                SDL_GameControllerSetSensorEnabled(state.controller, SDL_SENSOR_GYRO, SDL_TRUE);
            }
            if (enabled && m_ControllerKbmContinuous && state.kbmTimer == 0)
                state.kbmTimer = SDL_AddTimer(4, controllerKbmTimerCallback, &state);
            else if ((!enabled || !m_ControllerKbmContinuous) && state.kbmTimer != 0) {
                SDL_RemoveTimer(state.kbmTimer);
                state.kbmTimer = 0;
            }
        }
        return;
    }
    if (!enabled) {
        for (GamepadState& state : m_GamepadState) {
            if (state.kbmTimer != 0) { SDL_RemoveTimer(state.kbmTimer); state.kbmTimer = 0; }
            if (state.controller != nullptr) releaseControllerKbmState(&state);
        }
        m_ControllerKbmMode = false;
        for (GamepadState& state : m_GamepadState) if (state.controller != nullptr) announceControllerAfterKbm(&state);
    }
    else {
        m_ControllerKbmMode = true;
        for (GamepadState& state : m_GamepadState) {
            if (state.controller == nullptr) continue;
            state.kbmOwner = this;
            if (m_ControllerKbmGyroEnabled && SDL_GameControllerHasSensor(state.controller, SDL_SENSOR_GYRO)) {
                state.gyroReportPeriodMs = 4;
                SDL_GameControllerSetSensorEnabled(state.controller, SDL_SENSOR_GYRO, SDL_TRUE);
            }
            if (m_ControllerKbmContinuous && state.kbmTimer == 0) state.kbmTimer = SDL_AddTimer(4, controllerKbmTimerCallback, &state);
        }
    }
}

void SdlInputHandler::setControllerKbmKey(int virtualKey, bool pressed)
{
    int refs = m_ControllerKbmKeyRefs.value(virtualKey);
    if (pressed) {
        if (refs++ == 0) {
            switch (virtualKey) {
            case 0x10: case 0xA0: case 0xA1: m_ControllerKbmModifiers |= MODIFIER_SHIFT; break;
            case 0x11: case 0xA2: case 0xA3: m_ControllerKbmModifiers |= MODIFIER_CTRL; break;
            case 0x12: case 0xA4: case 0xA5: m_ControllerKbmModifiers |= MODIFIER_ALT; break;
            case 0x5B: case 0x5C: m_ControllerKbmModifiers |= MODIFIER_META; break;
            }
            LiSendKeyboardEvent2(static_cast<short>(0x8000 | virtualKey), KEY_ACTION_DOWN,
                                 m_ControllerKbmModifiers, 0);
        }
        m_ControllerKbmKeyRefs.insert(virtualKey, refs);
    }
    else if (refs > 0) {
        if (--refs == 0) {
            LiSendKeyboardEvent2(static_cast<short>(0x8000 | virtualKey), KEY_ACTION_UP,
                                 m_ControllerKbmModifiers, 0);
            m_ControllerKbmKeyRefs.remove(virtualKey);
            switch (virtualKey) {
            case 0x10: case 0xA0: case 0xA1: m_ControllerKbmModifiers &= ~MODIFIER_SHIFT; break;
            case 0x11: case 0xA2: case 0xA3: m_ControllerKbmModifiers &= ~MODIFIER_CTRL; break;
            case 0x12: case 0xA4: case 0xA5: m_ControllerKbmModifiers &= ~MODIFIER_ALT; break;
            case 0x5B: case 0x5C: m_ControllerKbmModifiers &= ~MODIFIER_META; break;
            }
        }
        else m_ControllerKbmKeyRefs.insert(virtualKey, refs);
    }
}

void SdlInputHandler::setControllerKbmMouse(int button, bool pressed)
{
    int refs = m_ControllerKbmMouseRefs.value(button);
    if (pressed) {
        if (refs++ == 0) LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, button);
        m_ControllerKbmMouseRefs.insert(button, refs);
    }
    else if (refs > 0) {
        if (--refs == 0) {
            LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, button);
            m_ControllerKbmMouseRefs.remove(button);
        }
        else m_ControllerKbmMouseRefs.insert(button, refs);
    }
}

void SdlInputHandler::updateControllerKbmGyroHoldState(GamepadState* state)
{
    if (!m_ControllerKbmGyroHoldMode) {
        m_ControllerKbmGyroHoldActive = false;
        return;
    }
    const bool buttonActive = (m_ControllerKbmButtonsDown & m_ControllerKbmGyroActivationMask) != 0;
    const bool leftTriggerActive = m_ControllerKbmGyroActivationTriggers[0] && state->lt >= 15 * 255 / 100;
    const bool rightTriggerActive = m_ControllerKbmGyroActivationTriggers[1] && state->rt >= 15 * 255 / 100;
    const bool active = buttonActive || leftTriggerActive || rightTriggerActive;
    if (active != m_ControllerKbmGyroHoldActive) {
        m_ControllerKbmGyroHoldActive = active;
        m_ControllerKbmLastGyroTime = 0;
        Session::get()->notifyControllerKbmGyro(active);
    }
}

void SdlInputHandler::handleControllerKbmAction(GamepadState*, const QString& source, bool pressed)
{
    const QString action = m_ControllerKbmMappings.value(source);
    if (action.startsWith(QStringLiteral("key:"))) {
        bool ok = false; const int key = action.mid(4).toInt(&ok);
        if (ok) setControllerKbmKey(key, pressed);
    }
    else if (action == QStringLiteral("mouse_left")) setControllerKbmMouse(BUTTON_LEFT, pressed);
    else if (action == QStringLiteral("mouse_right")) setControllerKbmMouse(BUTTON_RIGHT, pressed);
    else if (action == QStringLiteral("mouse_middle")) setControllerKbmMouse(BUTTON_MIDDLE, pressed);
    else if (action == QStringLiteral("mouse_back")) setControllerKbmMouse(BUTTON_X1, pressed);
    else if (action == QStringLiteral("mouse_forward")) setControllerKbmMouse(BUTTON_X2, pressed);
    else if (pressed && action == QStringLiteral("wheel_up")) LiSendHighResScrollEvent(120);
    else if (pressed && action == QStringLiteral("wheel_down")) LiSendHighResScrollEvent(-120);
    else if (pressed && action.startsWith(QStringLiteral("directed_flick:"))) {
        const QStringList parts = action.split(':');
        if (parts.size() == 3) {
            const int distance = qBound(50, parts[2].toInt(), 4000);
            int x = 0, y = 0;
            if (parts[1] == QStringLiteral("left")) x = -distance;
            else if (parts[1] == QStringLiteral("right")) x = distance;
            else if (parts[1] == QStringLiteral("up")) y = -distance;
            else if (parts[1] == QStringLiteral("down")) y = distance;
            for (int i = 0; i < 4; ++i) LiSendMouseMoveEvent(x / 4, y / 4);
        }
    }
}

void SdlInputHandler::updateControllerKbmStick(GamepadState* state, const QString& source,
                                                float x, float y, int slot)
{
    const QString action = m_ControllerKbmMappings.value(source);
    if (action != QStringLiteral("basic_wasd") && action != QStringLiteral("basic_arrows")) return;
    const int keys[2][4] = {{0x41, 0x44, 0x57, 0x53}, {0x25, 0x27, 0x26, 0x28}};
    const int row = action == QStringLiteral("basic_wasd") ? 0 : 1;
    const bool targets[4] = {x < -0.45f, x > 0.45f, y < -0.45f, y > 0.45f};
    const float releases[4] = {x, -x, y, -y};
    for (int d = 0; d < 4; ++d) {
        const int index = slot * 8 + row * 4 + d;
        bool next = state->kbmDirectional[index];
        if (!next && targets[d]) next = true;
        else if (next && releases[d] > -0.30f) next = false;
        if (next != state->kbmDirectional[index]) {
            state->kbmDirectional[index] = next;
            setControllerKbmKey(keys[row][d], next);
        }
    }
}

void SdlInputHandler::pollControllerKbm(GamepadState* state)
{
    const float axes[2][2] = {
        {kbmDeadzone(state->lsX / 32767.0f), kbmDeadzone(-state->lsY / 32767.0f)},
        {kbmDeadzone(state->rsX / 32767.0f), kbmDeadzone(-state->rsY / 32767.0f)}
    };
    const QString sources[2] = {QStringLiteral("left_stick"), QStringLiteral("right_stick")};
    for (int stick = 0; stick < 2; ++stick) {
        const QString action = m_ControllerKbmMappings.value(sources[stick]);
        updateControllerKbmStick(state, sources[stick], axes[stick][0], axes[stick][1], stick);
        if (action == QStringLiteral("mouse_move")) {
            const float scale = (m_ControllerKbmStickSpeed / 100.0f) * 18.0f * (m_ControllerKbmContinuous ? 0.25f : 1.0f);
            state->kbmMouseRemainderX += std::copysign(axes[stick][0] * axes[stick][0], axes[stick][0]) * scale;
            state->kbmMouseRemainderY += std::copysign(axes[stick][1] * axes[stick][1], axes[stick][1]) * scale;
            const int dx = static_cast<int>(state->kbmMouseRemainderX);
            const int dy = static_cast<int>(state->kbmMouseRemainderY);
            state->kbmMouseRemainderX -= dx; state->kbmMouseRemainderY -= dy;
            if (dx || dy) LiSendMouseMoveEvent(dx, dy);
        }
        else if (action == QStringLiteral("scroll")) {
            state->kbmScrollRemainderX += std::copysign(axes[stick][0] * axes[stick][0], axes[stick][0]) * 60.0f;
            state->kbmScrollRemainderY += -std::copysign(axes[stick][1] * axes[stick][1], axes[stick][1]) * 60.0f;
            const int dx = static_cast<int>(state->kbmScrollRemainderX);
            const int dy = static_cast<int>(state->kbmScrollRemainderY);
            state->kbmScrollRemainderX -= dx; state->kbmScrollRemainderY -= dy;
            if (dx) LiSendHighResHScrollEvent(dx);
            if (dy) LiSendHighResScrollEvent(dy);
        }
    }
    if (m_ControllerKbmTriggerBehaviors[0] == QStringLiteral("repeat") ||
            m_ControllerKbmTriggerBehaviors[1] == QStringLiteral("repeat")) {
        const uint32_t now = SDL_GetTicks();
        for (int trigger = 0; trigger < 2; ++trigger) {
            const uint32_t period = 1000 / qMax(1, m_ControllerKbmTriggerRepeatRates[trigger]);
            if (m_ControllerKbmTriggerBehaviors[trigger] == QStringLiteral("repeat") &&
                    state->kbmTriggerDown[trigger] && SDL_TICKS_PASSED(now, state->kbmTriggerRepeatAt[trigger])) {
                const QString source = trigger == 0 ? QStringLiteral("trigger_left") : QStringLiteral("trigger_right");
                handleControllerKbmAction(state, source, true);
                handleControllerKbmAction(state, source, false);
                state->kbmTriggerRepeatAt[trigger] = now + period;
            }
        }
    }
}

Uint32 SdlInputHandler::controllerKbmTimerCallback(Uint32 interval, void* param)
{
    GamepadState* state = static_cast<GamepadState*>(param);
    // SDL timer callbacks run on a worker thread. Never touch Qt containers or
    // the mutable controller state here. Queue a synthetic axis event so the
    // regular SDL input thread performs the poll in-order with real input.
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_CONTROLLERAXISMOTION;
    event.caxis.timestamp = SDL_GetTicks();
    event.caxis.which = state->jsId;
    event.caxis.axis = SDL_CONTROLLER_AXIS_MAX;
    SDL_PushEvent(&event);
    return interval;
}

void SdlInputHandler::releaseControllerKbmState(GamepadState* state)
{
    if (!m_ControllerKbmMode) return;
    for (auto it = m_ControllerKbmKeyRefs.constBegin(); it != m_ControllerKbmKeyRefs.constEnd(); ++it)
        LiSendKeyboardEvent2(static_cast<short>(0x8000 | it.key()), KEY_ACTION_UP, 0, 0);
    for (auto it = m_ControllerKbmMouseRefs.constBegin(); it != m_ControllerKbmMouseRefs.constEnd(); ++it)
        LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, it.key());
    m_ControllerKbmKeyRefs.clear(); m_ControllerKbmMouseRefs.clear(); m_ControllerKbmModifiers = 0;
    SDL_memset(state->kbmDirectional, 0, sizeof(state->kbmDirectional));
}

void SdlInputHandler::pollDualSenseHeadsets()
{
    if (m_DualSenseAudioMode != StreamingPreferences::DSAM_AUTO) {
        return;
    }

    const uint32_t now = SDL_GetTicks();
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        GamepadState* state = &m_GamepadState[i];
        if (state->controller == nullptr ||
                SDL_GameControllerGetType(state->controller) != SDL_CONTROLLER_TYPE_PS5 ||
                (state->dualSenseHeadsetStateKnown &&
                 !SDL_TICKS_PASSED(now, state->lastDualSenseHeadsetPollTime + 100))) {
            continue;
        }

        state->lastDualSenseHeadsetPollTime = now;
        const bool connected = queryDualSenseHeadset(state->controller);
        if (!state->dualSenseHeadsetStateKnown || connected != state->dualSenseHeadsetConnected) {
            state->dualSenseHeadsetStateKnown = true;
            state->dualSenseHeadsetConnected = connected;
            if (state->dualSenseBluetooth) {
                DualSenseAudioRenderer::instance().setBluetoothHeadsetActive(connected);
            }
            else {
                activateDualSenseAudio(state->controller,
                    connected ? StreamingPreferences::DSAM_USB_HEADSET : StreamingPreferences::DSAM_AUTO);
                SdlAudioRenderer::setDualSenseHeadsetActive(connected);
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "DualSense %s headset %s; automatic main audio mirror %s",
                        state->dualSenseBluetooth ? "Bluetooth" : "wired",
                        connected ? "connected" : "disconnected",
                        connected ? "enabled" : "disabled");
        }
    }
}

void SdlInputHandler::sendGamepadBatteryState(GamepadState* state, SDL_JoystickPowerLevel level)
{
    uint8_t batteryPercentage;
    uint8_t batteryState;

    // SDL's battery reporting capabilities are quite limited. Notably, we cannot
    // tell the battery level while charging (or even if a battery is present).
    // We also cannot tell the percentage of charge exactly in any case.
    switch (level)
    {
    case SDL_JOYSTICK_POWER_UNKNOWN:
        batteryState = LI_BATTERY_STATE_UNKNOWN;
        batteryPercentage = LI_BATTERY_PERCENTAGE_UNKNOWN;
        break;
    case SDL_JOYSTICK_POWER_WIRED:
        batteryState = LI_BATTERY_STATE_CHARGING;
        batteryPercentage = LI_BATTERY_PERCENTAGE_UNKNOWN;
        break;
    case SDL_JOYSTICK_POWER_EMPTY:
        batteryState = LI_BATTERY_STATE_DISCHARGING;
        batteryPercentage = 5;
        break;
    case SDL_JOYSTICK_POWER_LOW:
        batteryState = LI_BATTERY_STATE_DISCHARGING;
        batteryPercentage = 20;
        break;
    case SDL_JOYSTICK_POWER_MEDIUM:
        batteryState = LI_BATTERY_STATE_DISCHARGING;
        batteryPercentage = 50;
        break;
    case SDL_JOYSTICK_POWER_FULL:
        batteryState = LI_BATTERY_STATE_DISCHARGING;
        batteryPercentage = 90;
        break;
    default:
        return;
    }

    LiSendControllerBatteryEvent(state->index, batteryState, batteryPercentage);
}

Uint32 SdlInputHandler::mouseEmulationTimerCallback(Uint32 interval, void *param)
{
    auto gamepad = reinterpret_cast<GamepadState*>(param);

    int rawX;
    int rawY;

    // Determine which analog stick is currently receiving the strongest input
    if (abs(gamepad->lsX) + abs(gamepad->lsY) > abs(gamepad->rsX) + abs(gamepad->rsY)) {
        rawX = gamepad->lsX;
        rawY = -gamepad->lsY;
    }
    else {
        rawX = gamepad->rsX;
        rawY = -gamepad->rsY;
    }

    float deltaX;
    float deltaY;

    // Produce a base vector for mouse movement with increased speed as we deviate further from center
    deltaX = qPow(rawX / 32766.0f * MOUSE_EMULATION_MOTION_MULTIPLIER, 3);
    deltaY = qPow(rawY / 32766.0f * MOUSE_EMULATION_MOTION_MULTIPLIER, 3);

    // Enforce deadzones
    deltaX = qAbs(deltaX) > MOUSE_EMULATION_DEADZONE ? deltaX - MOUSE_EMULATION_DEADZONE : 0;
    deltaY = qAbs(deltaY) > MOUSE_EMULATION_DEADZONE ? deltaY - MOUSE_EMULATION_DEADZONE : 0;

    if (deltaX != 0 || deltaY != 0) {
        LiSendMouseMoveEvent((short)deltaX, (short)deltaY);
    }

    return interval;
}

void SdlInputHandler::handleControllerAxisEvent(SDL_ControllerAxisEvent* event)
{
    SDL_JoystickID gameControllerId = event->which;
    GamepadState* state = findStateForGamepad(gameControllerId);
    if (state == NULL) {
        return;
    }

    syncControllerKbmSettings();
    if (m_ControllerKbmMode) {
        switch (event->axis) {
        case SDL_CONTROLLER_AXIS_LEFTX: state->lsX = event->value; break;
        case SDL_CONTROLLER_AXIS_LEFTY: state->lsY = -qMax(event->value, static_cast<short>(-32767)); break;
        case SDL_CONTROLLER_AXIS_RIGHTX: state->rsX = event->value; break;
        case SDL_CONTROLLER_AXIS_RIGHTY: state->rsY = -qMax(event->value, static_cast<short>(-32767)); break;
        case SDL_CONTROLLER_AXIS_TRIGGERLEFT: state->lt = static_cast<unsigned char>(event->value * 255UL / 32767); break;
        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: state->rt = static_cast<unsigned char>(event->value * 255UL / 32767); break;
        default: break;
        }
        if (m_ControllerKbmGyroHoldMode)
            updateControllerKbmGyroHoldState(state);
        const unsigned char triggers[2] = {state->lt, state->rt};
        for (int trigger = 0; trigger < 2; ++trigger) {
            const bool down = triggers[trigger] >= (m_ControllerKbmTriggerThresholds[trigger] * 255 / 100);
            if (down != state->kbmTriggerDown[trigger]) {
                state->kbmTriggerDown[trigger] = down;
                const QString source = trigger == 0 ? QStringLiteral("trigger_left") : QStringLiteral("trigger_right");
                if (m_ControllerKbmTriggerBehaviors[trigger] == QStringLiteral("hold"))
                    handleControllerKbmAction(state, source, down);
                else if (down) {
                    handleControllerKbmAction(state, source, true);
                    handleControllerKbmAction(state, source, false);
                    state->kbmTriggerRepeatAt[trigger] = SDL_GetTicks() + 1000 / qMax(1, m_ControllerKbmTriggerRepeatRates[trigger]);
                }
            }
        }
        if (!m_ControllerKbmContinuous || event->axis == SDL_CONTROLLER_AXIS_MAX)
            pollControllerKbm(state);
        return;
    }

    // Batch all pending axis motion events for this gamepad to save CPU time
    SDL_Event nextEvent;
    for (;;) {
        switch (event->axis)
        {
            case SDL_CONTROLLER_AXIS_LEFTX:
                state->lsX = event->value;
                break;
            case SDL_CONTROLLER_AXIS_LEFTY:
                // Signed values have one more negative value than
                // positive value, so inverting the sign on -32768
                // could actually cause the value to overflow and
                // wrap around to be negative again. Avoid that by
                // capping the value at 32767.
                state->lsY = -qMax(event->value, (short)-32767);
                break;
            case SDL_CONTROLLER_AXIS_RIGHTX:
                state->rsX = event->value;
                break;
            case SDL_CONTROLLER_AXIS_RIGHTY:
                state->rsY = -qMax(event->value, (short)-32767);
                break;
            case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
                state->lt = (unsigned char)(event->value * 255UL / 32767);
                break;
            case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
                state->rt = (unsigned char)(event->value * 255UL / 32767);
                break;
            default:
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Unhandled controller axis: %d",
                            event->axis);
                return;
        }

        // Check for another event to batch with
        if (SDL_PeepEvents(&nextEvent, 1, SDL_PEEKEVENT, SDL_CONTROLLERAXISMOTION, SDL_CONTROLLERAXISMOTION) <= 0) {
            break;
        }

        event = &nextEvent.caxis;
        if (event->which != gameControllerId) {
            // Stop batching if a different gamepad interrupts us
            break;
        }

        // Remove the next event to batch
        SDL_PeepEvents(&nextEvent, 1, SDL_GETEVENT, SDL_CONTROLLERAXISMOTION, SDL_CONTROLLERAXISMOTION);
    }

    if (m_ControllerKbmMode && m_ControllerKbmGyroHoldMode)
        updateControllerKbmGyroHoldState(state);

    // Only send the gamepad state to the host if it's not in mouse emulation mode
    if (state->mouseEmulationTimer == 0) {
        if (m_TriggerOverrideEnabled) {
            if (state->lt >= m_TriggerOverrideThresholds[0] * 255 / 100) state->lt = 255;
            if (state->rt >= m_TriggerOverrideThresholds[1] * 255 / 100) state->rt = 255;
        }
        sendGamepadState(state);
    }
}

void SdlInputHandler::handleControllerButtonEvent(SDL_ControllerButtonEvent* event)
{
    syncControllerKbmSettings();
    if (event->button >= SDL_arraysize(k_ButtonMap)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "No mapping for gamepad button: %u",
                    event->button);
        return;
    }

    GamepadState* state = findStateForGamepad(event->which);
    if (state == NULL) {
        return;
    }

    if (m_SwapFaceButtons) {
        switch (event->button) {
        case SDL_CONTROLLER_BUTTON_A:
            event->button = SDL_CONTROLLER_BUTTON_B;
            break;
        case SDL_CONTROLLER_BUTTON_B:
            event->button = SDL_CONTROLLER_BUTTON_A;
            break;
        case SDL_CONTROLLER_BUTTON_X:
            event->button = SDL_CONTROLLER_BUTTON_Y;
            break;
        case SDL_CONTROLLER_BUTTON_Y:
            event->button = SDL_CONTROLLER_BUTTON_X;
            break;
        }
    }

    if (event->button < 32) {
        if (event->state == SDL_PRESSED) m_ControllerKbmButtonsDown |= (1u << event->button);
        else m_ControllerKbmButtonsDown &= ~(1u << event->button);
    }
    if (m_ControllerKbmMode && m_ControllerKbmGyroHoldMode)
        updateControllerKbmGyroHoldState(state);

    // The DualSense mute button always controls the global forwarded microphone,
    // including while Controller KBM mode reserves all other controller inputs.
    if (event->button == SDL_CONTROLLER_BUTTON_MISC1 &&
            SDL_GameControllerGetType(state->controller) == SDL_CONTROLLER_TYPE_PS5) {
        if (event->state == SDL_PRESSED) {
            const bool muted = !MicCapture::isMuted();
            MicCapture::setMuted(muted);
            if (state->dualSenseBluetooth) {
                DualSenseAudioRenderer::instance().setBluetoothMicLed(muted);
            }
            else {
                DualSenseOutputReport micLed = {};
                micLed.validFlag1 = 0x01;
                micLed.muteButtonLed = muted ? 0x01 : 0x00;
                SDL_GameControllerSendEffect(state->controller, &micLed, sizeof(micLed));
            }
            Session::get()->notifyMicrophoneMute(muted);
        }
        return;
    }

    if (!m_ControllerKbmGyroHoldMode && m_ControllerKbmMode && m_ControllerKbmGyroShortcutMask != 0 &&
            (m_ControllerKbmButtonsDown & m_ControllerKbmGyroShortcutMask) == m_ControllerKbmGyroShortcutMask) {
        if (!m_ControllerKbmGyroShortcutLatched) {
            m_ControllerKbmGyroShortcutLatched = true;
            m_ControllerKbmGyroEnabled = !m_ControllerKbmGyroEnabled;
            QSettings().setValue(QStringLiteral("controllerkbmgyroenabled"), m_ControllerKbmGyroEnabled);
            m_ControllerKbmLastGyroTime = 0;
            Session::get()->notifyControllerKbmGyro(m_ControllerKbmGyroEnabled);
        }
        return;
    }
    if ((m_ControllerKbmButtonsDown & m_ControllerKbmGyroShortcutMask) == 0)
        m_ControllerKbmGyroShortcutLatched = false;

    if (event->button < 32 && (m_ControllerKbmShortcutMask & (1u << event->button))) {
        if (m_ControllerKbmShortcutMask != 0 &&
                (m_ControllerKbmButtonsDown & m_ControllerKbmShortcutMask) == m_ControllerKbmShortcutMask) {
            if (!m_ControllerKbmShortcutLatched) {
                m_ControllerKbmShortcutLatched = true;
                releaseControllerKbmState(state);
                setCaptureActive(false);
                QMetaObject::invokeMethod(StreamingPreferences::get(),
                                          "requestControllerKbmConfiguration",
                                          Qt::QueuedConnection);
            }
        }
        else if ((m_ControllerKbmButtonsDown & m_ControllerKbmShortcutMask) == 0) {
            m_ControllerKbmShortcutLatched = false;
        }
        return;
    }

    if (m_ControllerKbmMode) {
        handleControllerKbmAction(state, controllerKbmSourceForButton(event->button), event->state == SDL_PRESSED);
        return;
    }

    if (event->state == SDL_PRESSED) {
        state->buttons |= k_ButtonMap[event->button];

        if (event->button == SDL_CONTROLLER_BUTTON_START) {
            state->lastStartDownTime = SDL_GetTicks();
        }
        else if (state->mouseEmulationTimer != 0) {
            if (event->button == SDL_CONTROLLER_BUTTON_A) {
                LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
            }
            else if (event->button == SDL_CONTROLLER_BUTTON_B) {
                LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_RIGHT);
            }
            else if (event->button == SDL_CONTROLLER_BUTTON_X) {
                LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_MIDDLE);
            }
            else if (event->button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
                LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_X1);
            }
            else if (event->button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_X2);
            }
            else if (event->button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                LiSendScrollEvent(1);
            }
            else if (event->button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                LiSendScrollEvent(-1);
            }
            else if (event->button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
                LiSendHScrollEvent(1);
            }
            else if (event->button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
                LiSendHScrollEvent(-1);
            }
        }
    }
    else {
        state->buttons &= ~k_ButtonMap[event->button];

        if (event->button == SDL_CONTROLLER_BUTTON_START) {
            if (SDL_GetTicks() - state->lastStartDownTime > MOUSE_EMULATION_LONG_PRESS_TIME) {
                if (state->mouseEmulationTimer != 0) {
                    SDL_RemoveTimer(state->mouseEmulationTimer);
                    state->mouseEmulationTimer = 0;

                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "Mouse emulation deactivated");
                    Session::get()->notifyMouseEmulationMode(false);
                }
                else if (m_GamepadMouse) {
                    // Send the start button up event to the host, since we won't do it below
                    sendGamepadState(state);

                    state->mouseEmulationTimer = SDL_AddTimer(MOUSE_EMULATION_POLLING_INTERVAL, SdlInputHandler::mouseEmulationTimerCallback, state);

                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "Mouse emulation active");
                    Session::get()->notifyMouseEmulationMode(true);
                }
            }
        }
        else if (state->mouseEmulationTimer != 0) {
            if (event->button == SDL_CONTROLLER_BUTTON_A) {
                LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
            }
            else if (event->button == SDL_CONTROLLER_BUTTON_B) {
                LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_RIGHT);
            }
            else if (event->button == SDL_CONTROLLER_BUTTON_X) {
                LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_MIDDLE);
            }
            else if (event->button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
                LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_X1);
            }
            else if (event->button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_X2);
            }
        }
    }

    // Handle Start+Select+L1+R1 as a gamepad quit combo
    if (state->buttons == (PLAY_FLAG | BACK_FLAG | LB_FLAG | RB_FLAG) && qgetenv("NO_GAMEPAD_QUIT") != "1") {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected quit gamepad button combo");

        // Push a quit event to the main loop
        SDL_Event event;
        event.type = SDL_QUIT;
        event.quit.timestamp = SDL_GetTicks();
        SDL_PushEvent(&event);

        // Clear buttons down on this gamepad
        LiSendMultiControllerEvent(state->index, m_GamepadMask,
                                   0, 0, 0, 0, 0, 0, 0);
        return;
    }

    // Handle Select+L1+R1+X as a gamepad overlay combo
    if (state->buttons == (BACK_FLAG | LB_FLAG | RB_FLAG | X_FLAG)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected stats toggle gamepad combo");

        // Toggle the stats overlay
        Session::get()->getOverlayManager().setOverlayState(Overlay::OverlayDebug,
                                                            !Session::get()->getOverlayManager().isOverlayEnabled(Overlay::OverlayDebug));

        // Clear buttons down on this gamepad
        LiSendMultiControllerEvent(state->index, m_GamepadMask,
                                   0, 0, 0, 0, 0, 0, 0);
        return;
    }

    // Only send the gamepad state to the host if it's not in mouse emulation mode
    if (state->mouseEmulationTimer == 0) {
        sendGamepadState(state);
    }
}

#if SDL_VERSION_ATLEAST(2, 0, 14)

void SdlInputHandler::handleControllerSensorEvent(SDL_ControllerSensorEvent* event)
{
    // Sensor events can continue without any accompanying button/axis event.
    // Refresh here so Quick Menu gyro changes also take effect in a stationary stream.
    static uint32_t lastSettingsSync = 0;
    if (SDL_TICKS_PASSED(event->timestamp, lastSettingsSync + 100)) {
        syncControllerKbmSettings();
        lastSettingsSync = event->timestamp;
    }
    GamepadState* state = findStateForGamepad(event->which);
    if (state == NULL) {
        return;
    }

    switch (event->sensor) {
    case SDL_SENSOR_ACCEL:
        if (state->accelReportPeriodMs &&
                SDL_TICKS_PASSED(event->timestamp, state->lastAccelEventTime + state->accelReportPeriodMs) &&
                memcmp(event->data, state->lastAccelEventData, sizeof(event->data)) != 0) {
            memcpy(state->lastAccelEventData, event->data, sizeof(event->data));
            state->lastAccelEventTime = event->timestamp;

            LiSendControllerMotionEvent((uint8_t)state->index, LI_MOTION_TYPE_ACCEL, event->data[0], event->data[1], event->data[2]);
        }
        break;
    case SDL_SENSOR_GYRO:
        if (m_ControllerKbmMode && m_ControllerKbmGyroEnabled &&
                (!m_ControllerKbmGyroHoldMode || m_ControllerKbmGyroHoldActive)) {
            const uint32_t now = event->timestamp;
            if (m_ControllerKbmLastGyroTime != 0 && now - m_ControllerKbmLastGyroTime < 100) {
                const float dt = (now - m_ControllerKbmLastGyroTime) / 1000.0f;
                float axis[3]={event->data[0],event->data[1],event->data[2]};
                for(int i=0;i<3;i++){if(m_ControllerKbmGyroAxisInverted[i])axis[i]=-axis[i];axis[i]*=m_ControllerKbmGyroAxisSensitivity[i]/100.0f;}
                const float yaw = std::abs(axis[1]) >= std::abs(axis[2]) ? axis[1] : axis[2];
                const float scale = 57.2957795f * dt;
                m_ControllerKbmGyroRemainderX += yaw * scale;
                m_ControllerKbmGyroRemainderY += axis[0] * scale;
                const int dx = static_cast<int>(m_ControllerKbmGyroRemainderX);
                const int dy = static_cast<int>(m_ControllerKbmGyroRemainderY);
                m_ControllerKbmGyroRemainderX -= dx; m_ControllerKbmGyroRemainderY -= dy;
                if (dx || dy) LiSendMouseMoveEvent(dx, dy);
            }
            m_ControllerKbmLastGyroTime = now;
            return;
        }
        if (state->gyroReportPeriodMs &&
                SDL_TICKS_PASSED(event->timestamp, state->lastGyroEventTime + state->gyroReportPeriodMs) &&
                memcmp(event->data, state->lastGyroEventData, sizeof(event->data)) != 0) {
            memcpy(state->lastGyroEventData, event->data, sizeof(event->data));
            state->lastGyroEventTime = event->timestamp;

            float gyro[3] = { event->data[0], event->data[1], event->data[2] };
            // This menu can be changed while streaming, so use the live
            // preferences instead of the snapshot captured at stream startup.
            const auto* prefs = StreamingPreferences::get();
            const bool gyroOverrideEnabled = prefs->gyroOverrideEnabled;
            const int gyroAxisSource[3] = {
                prefs->gyroXAxisSource, prefs->gyroYAxisSource, prefs->gyroZAxisSource
            };
            const bool gyroAxisInverted[3] = {
                prefs->gyroXAxisInverted, prefs->gyroYAxisInverted, prefs->gyroZAxisInverted
            };
            const bool gyroAxisDisabled[3] = {
                prefs->gyroXAxisDisabled, prefs->gyroYAxisDisabled, prefs->gyroZAxisDisabled
            };
            if (gyroOverrideEnabled) {
                const float rawGyro[3] = { event->data[0], event->data[1], event->data[2] };
                for (int axis = 0; axis < 3; axis++) {
                    if (gyroAxisDisabled[axis]) {
                        gyro[axis] = 0.0f;
                    }
                    else {
                        gyro[axis] = rawGyro[gyroAxisSource[axis]];
                        if (gyroAxisInverted[axis]) {
                            gyro[axis] = -gyro[axis];
                        }
                    }
                }
            }

            // Convert rad/s to deg/s after applying the optional axis override.
            LiSendControllerMotionEvent((uint8_t)state->index, LI_MOTION_TYPE_GYRO,
                                        gyro[0] * 57.2957795f,
                                        gyro[1] * 57.2957795f,
                                        gyro[2] * 57.2957795f);
        }
        break;
    }
}

void SdlInputHandler::handleControllerTouchpadEvent(SDL_ControllerTouchpadEvent* event)
{
    GamepadState* state = findStateForGamepad(event->which);
    if (state == NULL) {
        return;
    }

    uint8_t eventType;
    switch (event->type) {
    case SDL_CONTROLLERTOUCHPADDOWN:
        eventType = LI_TOUCH_EVENT_DOWN;
        break;
    case SDL_CONTROLLERTOUCHPADUP:
        eventType = LI_TOUCH_EVENT_UP;
        break;
    case SDL_CONTROLLERTOUCHPADMOTION:
        eventType = LI_TOUCH_EVENT_MOVE;
        break;
    default:
        return;
    }

    LiSendControllerTouchEvent2((uint8_t)state->index, eventType,
                                (uint8_t)event->touchpad, event->finger,
                                event->x, event->y, event->pressure);
}

#endif

#if SDL_VERSION_ATLEAST(2, 24, 0)

void SdlInputHandler::handleJoystickBatteryEvent(SDL_JoyBatteryEvent* event)
{
    GamepadState* state = findStateForGamepad(event->which);
    if (state == NULL) {
        return;
    }

    sendGamepadBatteryState(state, event->level);
}

#endif

void SdlInputHandler::handleControllerDeviceEvent(SDL_ControllerDeviceEvent* event)
{
    GamepadState* state;

    if (event->type == SDL_CONTROLLERDEVICEADDED) {
        int i;
        const char* name;
        SDL_GameController* controller;
        const char* mapping;
        char guidStr[33];
        uint32_t hapticCaps;

        controller = SDL_GameControllerOpen(event->which);
        if (controller == NULL) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to open gamepad: %s",
                         SDL_GetError());
            return;
        }

        // SDL_CONTROLLERDEVICEADDED can be reported multiple times for the same
        // gamepad in rare cases, because SDL doesn't fixup the device index in
        // the SDL_CONTROLLERDEVICEADDED event if an unopened gamepad disappears
        // before we've processed the add event.
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (m_GamepadState[i].controller == controller) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Received duplicate add event for controller index: %d",
                            event->which);
                SDL_GameControllerClose(controller);
                return;
            }
        }

        // We used to use SDL_GameControllerGetPlayerIndex() here but that
        // can lead to strange issues due to bugs in Windows where an Xbox
        // controller will join as player 2, even though no player 1 controller
        // is connected at all. This pretty much screws any attempt to use
        // the gamepad in single player games, so just assign them in order from 0.
        i = 0;

        for (; i < MAX_GAMEPADS; i++) {
            SDL_assert(m_GamepadState[i].controller != controller);
            if (m_GamepadState[i].controller == NULL) {
                // Found an empty slot
                break;
            }
        }

        if (i == MAX_GAMEPADS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "No open gamepad slots found!");
            SDL_GameControllerClose(controller);
            return;
        }

        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(controller)),
                                  guidStr, sizeof(guidStr));
        if (m_IgnoreDeviceGuids.contains(guidStr, Qt::CaseInsensitive))
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Skipping ignored device with GUID: %s",
                        guidStr);
            SDL_GameControllerClose(controller);
            return;
        }

        state = &m_GamepadState[i];
        if (m_MultiController) {
            state->index = i;

#if SDL_VERSION_ATLEAST(2, 0, 12)
            // This will change indicators on the controller to show the assigned
            // player index. For Xbox 360 controllers, that means updating the LED
            // ring to light up the corresponding quadrant for this player.
            SDL_GameControllerSetPlayerIndex(controller, state->index);
#endif
        }
        else {
            // Always player 1 in single controller mode
            state->index = 0;
        }

        state->controller = controller;
        state->jsId = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(state->controller));

        hapticCaps = 0;
#if SDL_VERSION_ATLEAST(2, 0, 18)
        hapticCaps |= SDL_GameControllerHasRumble(controller) ? ML_HAPTIC_GC_RUMBLE : 0;
        hapticCaps |= SDL_GameControllerHasRumbleTriggers(controller) ? ML_HAPTIC_GC_TRIGGER_RUMBLE : 0;
#elif SDL_VERSION_ATLEAST(2, 0, 9)
        // Perform a tiny rumbles to see if haptics are supported.
        // NB: We cannot use zeros for rumble intensity or SDL will not actually call the JS driver
        // and we'll get a (potentially false) success value returned.
        hapticCaps |= SDL_GameControllerRumble(controller, 1, 1, 1) == 0 ? ML_HAPTIC_GC_RUMBLE : 0;
#if SDL_VERSION_ATLEAST(2, 0, 14)
        hapticCaps |= SDL_GameControllerRumbleTriggers(controller, 1, 1, 1) == 0 ? ML_HAPTIC_GC_TRIGGER_RUMBLE : 0;
#endif
#else
        state->haptic = SDL_HapticOpenFromJoystick(SDL_GameControllerGetJoystick(state->controller));
        state->hapticEffectId = -1;
        state->hapticMethod = GAMEPAD_HAPTIC_METHOD_NONE;
        if (state->haptic != nullptr) {
            // Query for supported haptic effects
            hapticCaps = SDL_HapticQuery(state->haptic);
            hapticCaps |= SDL_HapticRumbleSupported(state->haptic) ?
                            ML_HAPTIC_SIMPLE_RUMBLE : 0;

            if ((SDL_HapticQuery(state->haptic) & SDL_HAPTIC_LEFTRIGHT) == 0) {
                if (SDL_HapticRumbleSupported(state->haptic)) {
                    if (SDL_HapticRumbleInit(state->haptic) == 0) {
                        state->hapticMethod = GAMEPAD_HAPTIC_METHOD_SIMPLERUMBLE;
                    }
                }
                if (state->hapticMethod == GAMEPAD_HAPTIC_METHOD_NONE) {
                    SDL_HapticClose(state->haptic);
                    state->haptic = nullptr;
                }
            } else {
                state->hapticMethod = GAMEPAD_HAPTIC_METHOD_LEFTRIGHT;
            }
        }
        else {
            hapticCaps = 0;
        }
#endif

        mapping = SDL_GameControllerMapping(state->controller);
        name = SDL_GameControllerName(state->controller);

        uint16_t vendorId = SDL_GameControllerGetVendor(state->controller);
        uint16_t productId = SDL_GameControllerGetProduct(state->controller);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Gamepad %d (player %d) is: %s (VID/PID: 0x%.4x/0x%.4x) (haptic capabilities: 0x%x) (mapping: %s -> %s)",
                    i,
                    state->index,
                    name != nullptr ? name : "<null>",
                    vendorId,
                    productId,
                    hapticCaps,
                    guidStr,
                    mapping != nullptr ? mapping : "<null>");

        if (SDL_GameControllerGetType(state->controller) == SDL_CONTROLLER_TYPE_PS5) {
            SDL_Joystick* joystick = SDL_GameControllerGetJoystick(state->controller);
            const char* path = SDL_JoystickPath(joystick);
            const bool bluetoothPath = path != nullptr &&
                (std::strstr(path, "00001124-0000-1000-8000-00805f9b34fb") != nullptr ||
                 std::strstr(path, "VID&0002054c") != nullptr);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "DualSense transport classification: %s path=%s",
                        bluetoothPath ? "Bluetooth" : "USB", path != nullptr ? path : "<null>");
            DualSenseAudioRenderer::instance().setController(state->controller, bluetoothPath, path);
            state->dualSenseBluetooth = bluetoothPath;
            state->dualSenseHeadsetStateKnown = false;
            state->dualSenseHeadsetConnected = false;
            state->lastDualSenseHeadsetPollTime = 0;
            if (!bluetoothPath) {
                activateDualSenseAudio(state->controller, m_DualSenseAudioMode);
            }
            const char* serial = SDL_GameControllerGetSerial(state->controller);
            const uint16_t firmware = SDL_GameControllerGetFirmwareVersion(state->controller);

            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "DualSense %d native HID ready: path=%s serial=%s firmware=0x%.4x "
                        "touchpads=%d accel=%d gyro=%d battery=%d rumble=%d triggerRumble=%d led=%d",
                        state->index,
                        path != nullptr ? path : "<unavailable>",
                        serial != nullptr ? serial : "<unavailable>",
                        firmware,
                        SDL_GameControllerGetNumTouchpads(state->controller),
                        SDL_GameControllerHasSensor(state->controller, SDL_SENSOR_ACCEL),
                        SDL_GameControllerHasSensor(state->controller, SDL_SENSOR_GYRO),
                        SDL_JoystickCurrentPowerLevel(joystick),
                        (hapticCaps & ML_HAPTIC_GC_RUMBLE) != 0,
                        (hapticCaps & ML_HAPTIC_GC_TRIGGER_RUMBLE) != 0,
                        SDL_GameControllerHasLED(state->controller));
        }
        if (mapping != nullptr) {
            SDL_free((void*)mapping);
        }

        if (m_ControllerKbmMode) {
            state->kbmOwner = this;
#if SDL_VERSION_ATLEAST(2, 0, 14)
            if (SDL_GameControllerHasSensor(state->controller, SDL_SENSOR_GYRO)) {
                state->gyroReportPeriodMs = 4;
                SDL_GameControllerSetSensorEnabled(state->controller, SDL_SENSOR_GYRO, SDL_TRUE);
            }
#endif
            if (m_ControllerKbmContinuous) {
                state->kbmTimer = SDL_AddTimer(4, SdlInputHandler::controllerKbmTimerCallback, state);
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Controller %d is active in Extended keyboard and mouse mode", state->index);
            return;
        }

        // Add this gamepad to the gamepad mask
        if (m_MultiController) {
            // NB: Don't assert that it's unset here because we will already
            // have the mask set for initially attached gamepads to avoid confusing
            // apps running on the host.
            m_GamepadMask |= (1 << state->index);
        }
        else {
            SDL_assert(m_GamepadMask == 0x1);
        }

        SDL_JoystickPowerLevel powerLevel = SDL_JoystickCurrentPowerLevel(SDL_GameControllerGetJoystick(state->controller));

#if SDL_VERSION_ATLEAST(2, 0, 14)
        // On SDL 2.0.14 and later, we can provide enhanced controller information to the host PC
        // for it to use as a hint for the type of controller to emulate.
        uint32_t supportedButtonFlags = 0;
        for (int i = 0; i < (int)SDL_arraysize(k_ButtonMap); i++) {
            if (SDL_GameControllerHasButton(state->controller, (SDL_GameControllerButton)i)) {
                supportedButtonFlags |= k_ButtonMap[i];
            }
        }

        uint32_t capabilities = 0;
        if (SDL_GameControllerGetBindForAxis(state->controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT).bindType == SDL_CONTROLLER_BINDTYPE_AXIS ||
            SDL_GameControllerGetBindForAxis(state->controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT).bindType == SDL_CONTROLLER_BINDTYPE_AXIS) {
            // We assume these are analog triggers if the binding is to an axis rather than a button
            capabilities |= LI_CCAP_ANALOG_TRIGGERS;
        }
        if (hapticCaps & ML_HAPTIC_GC_RUMBLE) {
            capabilities |= LI_CCAP_RUMBLE;
        }
        if (hapticCaps & ML_HAPTIC_GC_TRIGGER_RUMBLE) {
            capabilities |= LI_CCAP_TRIGGER_RUMBLE;
        }
        if (SDL_GameControllerGetNumTouchpads(state->controller) > 0) {
            capabilities |= LI_CCAP_TOUCHPAD;
            if (SDL_GameControllerGetNumTouchpads(state->controller) > 1) {
                capabilities |= LI_CCAP_DUAL_TOUCHPAD;
            }
        }
        if (SDL_GameControllerHasSensor(state->controller, SDL_SENSOR_ACCEL)) {
            capabilities |= LI_CCAP_ACCEL;
        }
        if (SDL_GameControllerHasSensor(state->controller, SDL_SENSOR_GYRO)) {
            capabilities |= LI_CCAP_GYRO;
        }
        if (powerLevel != SDL_JOYSTICK_POWER_UNKNOWN || SDL_VERSION_ATLEAST(2, 24, 0)) {
            capabilities |= LI_CCAP_BATTERY_STATE;
        }
        if (SDL_GameControllerHasLED(state->controller)) {
            capabilities |= LI_CCAP_RGB_LED;
        }

        uint8_t type;
        switch (SDL_GameControllerGetType(state->controller)) {
        case SDL_CONTROLLER_TYPE_XBOX360:
        case SDL_CONTROLLER_TYPE_XBOXONE:
            type = LI_CTYPE_XBOX;
            break;
        case SDL_CONTROLLER_TYPE_PS3:
        case SDL_CONTROLLER_TYPE_PS4:
        case SDL_CONTROLLER_TYPE_PS5:
            type = LI_CTYPE_PS;
            break;
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
#if SDL_VERSION_ATLEAST(2, 24, 0)
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
#endif
            type = LI_CTYPE_NINTENDO;
            break;
        default:
            type = LI_CTYPE_UNKNOWN;
            break;
        }

        // If this is a PlayStation controller that doesn't have a touchpad button mapped,
        // we'll allow the Select+PS button combo to act as the touchpad.
        state->clickpadButtonEmulationEnabled =
#if SDL_VERSION_ATLEAST(2, 0, 14)
            SDL_GameControllerGetBindForButton(state->controller, SDL_CONTROLLER_BUTTON_TOUCHPAD).bindType == SDL_CONTROLLER_BINDTYPE_NONE &&
#endif
            type == LI_CTYPE_PS;

        int extendedMode = 0;
        switch (m_ControllerEmulationMode) {
        case StreamingPreferences::CEM_XBOX:
            extendedMode = APOLLO_EXTENDED_EMULATION_XBOX;
            break;
        case StreamingPreferences::CEM_DUALSHOCK4:
            extendedMode = APOLLO_EXTENDED_EMULATION_DS4;
            break;
        case StreamingPreferences::CEM_DUALSENSE:
            extendedMode = APOLLO_EXTENDED_EMULATION_DS5;
            break;
        case StreamingPreferences::CEM_AUTO:
            if (SDL_GameControllerGetType(state->controller) == SDL_CONTROLLER_TYPE_PS5) {
                extendedMode = APOLLO_EXTENDED_EMULATION_DS5;
            }
            else if (type == LI_CTYPE_PS) {
                extendedMode = APOLLO_EXTENDED_EMULATION_DS4;
            }
            else if (type == LI_CTYPE_XBOX) {
                extendedMode = APOLLO_EXTENDED_EMULATION_XBOX;
            }
            break;
        }

        capabilities |= APOLLO_EXTENDED_EMULATION_MAGIC | (extendedMode << 8);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Apollo Extended emulation request: controller=%d mode=%d capabilities=0x%x",
                    state->index, extendedMode, capabilities);
        LiSendControllerArrivalEvent(state->index, m_GamepadMask, type, supportedButtonFlags, capabilities);
#else

        // Send an empty event to tell the PC we've arrived
        sendGamepadState(state);
#endif

        // Send a power level if it's known at this time
        if (powerLevel != SDL_JOYSTICK_POWER_UNKNOWN) {
            sendGamepadBatteryState(state, powerLevel);
        }
    }
    else if (event->type == SDL_CONTROLLERDEVICEREMOVED) {
        state = findStateForGamepad(event->which);
        if (state != NULL) {
            if (state->dualSenseHeadsetConnected) {
                SdlAudioRenderer::setDualSenseHeadsetActive(false);
            }
            if (state->mouseEmulationTimer != 0) {
                Session::get()->notifyMouseEmulationMode(false);
                SDL_RemoveTimer(state->mouseEmulationTimer);
            }
            if (state->kbmTimer != 0) {
                SDL_RemoveTimer(state->kbmTimer);
                state->kbmTimer = 0;
            }
            releaseControllerKbmState(state);

            SDL_GameControllerClose(state->controller);

#if !SDL_VERSION_ATLEAST(2, 0, 9)
            if (state->haptic != nullptr) {
                SDL_HapticClose(state->haptic);
            }
#endif

            // Remove this from the gamepad mask in MC-mode
            if (m_ControllerKbmMode) {
                // This controller was intentionally never exposed as a gamepad.
            }
            else if (m_MultiController) {
                SDL_assert(m_GamepadMask & (1 << state->index));
                m_GamepadMask &= ~(1 << state->index);
            }
            else {
                SDL_assert(m_GamepadMask == 0x1);
            }

            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Gamepad %d is gone",
                        state->index);

            // Send a final event to let the PC know this gamepad is gone
            if (!m_ControllerKbmMode) {
                LiSendMultiControllerEvent(state->index, m_GamepadMask,
                                           0, 0, 0, 0, 0, 0, 0);
            }

            // Clear all remaining state from this slot
            SDL_memset(state, 0, sizeof(*state));
        }
    }
}

void SdlInputHandler::handleJoystickArrivalEvent(SDL_JoyDeviceEvent* event)
{
    SDL_assert(event->type == SDL_JOYDEVICEADDED);

    if (!SDL_IsGameController(event->which)) {
        char guidStr[33];
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(event->which),
                                  guidStr, sizeof(guidStr));
        const char* name = SDL_JoystickNameForIndex(event->which);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Joystick discovered with no mapping: %s %s",
                    name ? name : "<UNKNOWN>",
                    guidStr);
        SDL_Joystick* joy = SDL_JoystickOpen(event->which);
        if (joy != nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Number of axes: %d | Number of buttons: %d | Number of hats: %d",
                        SDL_JoystickNumAxes(joy), SDL_JoystickNumButtons(joy),
                        SDL_JoystickNumHats(joy));
            SDL_JoystickClose(joy);
        }
        else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Unable to open joystick for query: %s",
                        SDL_GetError());
        }
    }
}

void SdlInputHandler::rumble(unsigned short controllerNumber, unsigned short lowFreqMotor, unsigned short highFreqMotor)
{
    // Make sure the controller number is within our supported count
    if (controllerNumber >= MAX_GAMEPADS) {
        return;
    }

#if SDL_VERSION_ATLEAST(2, 0, 9)
    if (m_GamepadState[controllerNumber].controller != nullptr) {
        if (SDL_GameControllerGetType(m_GamepadState[controllerNumber].controller) == SDL_CONTROLLER_TYPE_PS5 &&
                DualSenseAudioRenderer::instance().isBluetooth()) {
            DualSenseAudioRenderer::instance().setBluetoothRumble(lowFreqMotor, highFreqMotor);
        }
        else {
            SDL_GameControllerRumble(m_GamepadState[controllerNumber].controller, lowFreqMotor, highFreqMotor, 30000);
        }
    }
#else
    // Check if the controller supports haptics (and if the controller exists at all)
    SDL_Haptic* haptic = m_GamepadState[controllerNumber].haptic;
    if (haptic == nullptr) {
        return;
    }

    // Stop the last effect we played
    if (m_GamepadState[controllerNumber].hapticMethod == GAMEPAD_HAPTIC_METHOD_LEFTRIGHT) {
        if (m_GamepadState[controllerNumber].hapticEffectId >= 0) {
            SDL_HapticDestroyEffect(haptic, m_GamepadState[controllerNumber].hapticEffectId);
        }
    } else if (m_GamepadState[controllerNumber].hapticMethod == GAMEPAD_HAPTIC_METHOD_SIMPLERUMBLE) {
        SDL_HapticRumbleStop(haptic);
    }

    // If this callback is telling us to stop both motors, don't bother queuing a new effect
    if (lowFreqMotor == 0 && highFreqMotor == 0) {
        return;
    }

    if (m_GamepadState[controllerNumber].hapticMethod == GAMEPAD_HAPTIC_METHOD_LEFTRIGHT) {
        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(effect));
        effect.type = SDL_HAPTIC_LEFTRIGHT;

        // The effect should last until we are instructed to stop or change it
        effect.leftright.length = SDL_HAPTIC_INFINITY;

        // SDL haptics range from 0-32767 but XInput uses 0-65535, so divide by 2 to correct for SDL's scaling
        effect.leftright.large_magnitude = lowFreqMotor / 2;
        effect.leftright.small_magnitude = highFreqMotor / 2;

        // Play the new effect
        m_GamepadState[controllerNumber].hapticEffectId = SDL_HapticNewEffect(haptic, &effect);
        if (m_GamepadState[controllerNumber].hapticEffectId >= 0) {
            SDL_HapticRunEffect(haptic, m_GamepadState[controllerNumber].hapticEffectId, 1);
        }
    } else if (m_GamepadState[controllerNumber].hapticMethod == GAMEPAD_HAPTIC_METHOD_SIMPLERUMBLE) {
        SDL_HapticRumblePlay(haptic,
                             std::min(1.0, (GAMEPAD_HAPTIC_SIMPLE_HIFREQ_MOTOR_WEIGHT*highFreqMotor +
                                            GAMEPAD_HAPTIC_SIMPLE_LOWFREQ_MOTOR_WEIGHT*lowFreqMotor) / 65535.0),
                             SDL_HAPTIC_INFINITY);
    }
#endif
}

void SdlInputHandler::rumbleTriggers(uint16_t controllerNumber, uint16_t leftTrigger, uint16_t rightTrigger)
{
    // Make sure the controller number is within our supported count
    if (controllerNumber >= MAX_GAMEPADS) {
        return;
    }

#if SDL_VERSION_ATLEAST(2, 0, 14)
    if (m_GamepadState[controllerNumber].controller != nullptr) {
        SDL_GameControllerRumbleTriggers(m_GamepadState[controllerNumber].controller, leftTrigger, rightTrigger, 30000);
    }
#endif
}

void SdlInputHandler::setMotionEventState(uint16_t controllerNumber, uint8_t motionType, uint16_t reportRateHz)
{
    // Make sure the controller number is within our supported count
    if (controllerNumber >= MAX_GAMEPADS) {
        return;
    }

#if SDL_VERSION_ATLEAST(2, 0, 14)
    if (m_GamepadState[controllerNumber].controller != nullptr) {
        uint8_t reportPeriodMs = reportRateHz ? (1000 / reportRateHz) : 0;

        switch (motionType) {
        case LI_MOTION_TYPE_ACCEL:
            m_GamepadState[controllerNumber].accelReportPeriodMs = reportPeriodMs;
            SDL_GameControllerSetSensorEnabled(m_GamepadState[controllerNumber].controller, SDL_SENSOR_ACCEL, reportRateHz ? SDL_TRUE : SDL_FALSE);
            break;

        case LI_MOTION_TYPE_GYRO:
            m_GamepadState[controllerNumber].gyroReportPeriodMs = reportPeriodMs;
            SDL_GameControllerSetSensorEnabled(m_GamepadState[controllerNumber].controller, SDL_SENSOR_GYRO, reportRateHz ? SDL_TRUE : SDL_FALSE);
            break;
        }
    }
#endif
}

void SdlInputHandler::setControllerLED(uint16_t controllerNumber, uint8_t r, uint8_t g, uint8_t b)
{
    // Make sure the controller number is within our supported count
    if (controllerNumber >= MAX_GAMEPADS) {
        return;
    }

#if SDL_VERSION_ATLEAST(2, 0, 14)
    if (m_GamepadState[controllerNumber].controller != nullptr) {
        if (SDL_GameControllerGetType(m_GamepadState[controllerNumber].controller) == SDL_CONTROLLER_TYPE_PS5 &&
                DualSenseAudioRenderer::instance().isBluetooth()) {
            DualSenseAudioRenderer::instance().setBluetoothLed(r, g, b);
        }
        else {
            SDL_GameControllerSetLED(m_GamepadState[controllerNumber].controller, r, g, b);
        }
    }
#endif
}

void SdlInputHandler::setAdaptiveTriggers(uint16_t controllerNumber, DualSenseOutputReport *report){

#if SDL_VERSION_ATLEAST(2, 0, 16)
        // Make sure the controller number is within our supported count
    if (controllerNumber < MAX_GAMEPADS &&
        // and we have a valid controller
        m_GamepadState[controllerNumber].controller != nullptr &&
        // and it's a PS5 controller
        SDL_GameControllerGetType(m_GamepadState[controllerNumber].controller) == SDL_CONTROLLER_TYPE_PS5) {
        if (DualSenseAudioRenderer::instance().isBluetooth()) {
            DualSenseAudioRenderer::instance().setBluetoothTriggers(report);
        }
        else {
            // Player LEDs are transported by Artemis only on Bluetooth for
            // now. Preserve the pre-existing wired output byte-for-byte.
            DualSenseOutputReport wiredReport = *report;
            wiredReport.validFlag2 = 0;
            wiredReport.playerLeds = 0;
            SDL_GameControllerSendEffect(m_GamepadState[controllerNumber].controller, &wiredReport, sizeof(wiredReport));
        }
    }
#endif

    SDL_free(report);
}

QString SdlInputHandler::getUnmappedGamepads()
{
    QString ret;

    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) failed: %s",
                     SDL_GetError());
    }

    MappingManager mappingManager;
    mappingManager.applyMappings();

    int numJoysticks = SDL_NumJoysticks();
    for (int i = 0; i < numJoysticks; i++) {
        if (!SDL_IsGameController(i)) {
            char guidStr[33];
            SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i),
                                      guidStr, sizeof(guidStr));
            const char* name = SDL_JoystickNameForIndex(i);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Unmapped joystick: %s %s",
                        name ? name : "<UNKNOWN>",
                        guidStr);
            SDL_Joystick* joy = SDL_JoystickOpen(i);
            if (joy != nullptr) {
                int numButtons = SDL_JoystickNumButtons(joy);
                int numHats = SDL_JoystickNumHats(joy);
                int numAxes = SDL_JoystickNumAxes(joy);

                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Number of axes: %d | Number of buttons: %d | Number of hats: %d",
                            numAxes, numButtons, numHats);

                if ((numAxes >= 4 && numAxes <= 8) && numButtons >= 8 && numHats <= 1) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "Joystick likely to be an unmapped game controller");
                    if (!ret.isEmpty()) {
                        ret += ", ";
                    }

                    ret += name;
                }

                SDL_JoystickClose(joy);
            }
            else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Unable to open joystick for query: %s",
                            SDL_GetError());
            }
        }
    }

    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);

    // Flush stale events so they aren't processed by the main session event loop
    SDL_FlushEvents(SDL_JOYDEVICEADDED, SDL_JOYDEVICEREMOVED);
    SDL_FlushEvents(SDL_CONTROLLERDEVICEADDED, SDL_CONTROLLERDEVICEREMAPPED);

    return ret;
}

int SdlInputHandler::getAttachedGamepadMask()
{
    int count;
    int mask;

    if (m_ControllerKbmMode) {
        return 0;
    }
    if (!m_MultiController) {
        // Player 1 is always present in non-MC mode
        return 0x1;
    }

    count = mask = 0;
    int numJoysticks = SDL_NumJoysticks();
    for (int i = 0; i < numJoysticks; i++) {
        if (SDL_IsGameController(i)) {
            char guidStr[33];
            SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i),
                                      guidStr, sizeof(guidStr));

            if (!m_IgnoreDeviceGuids.contains(guidStr, Qt::CaseInsensitive))
            {
                mask |= (1 << count++);
            }
        }
    }

    return mask;
}
