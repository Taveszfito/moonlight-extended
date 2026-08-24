import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import QtQuick.Dialogs
import StreamingPreferences 1.0

Window {
    id: root
    title: qsTr("Extended Quick Menu")
    width: 1180; height: 760; minimumWidth: 920; minimumHeight: 600
    color: "transparent"; flags: Qt.Window | Qt.FramelessWindowHint
    property int revision: 0
    property int selectedPage: 2
    property bool menuOpen: false
    property double lastToggleTime: 0
    function sourceLabel(source) {
        var labels = {"button_a":"A / Cross","button_b":"B / Circle","button_x":"X / Square","button_y":"Y / Triangle","dpad_up":"D-pad Up","dpad_down":"D-pad Down","dpad_left":"D-pad Left","dpad_right":"D-pad Right","button_lb":"Left Shoulder","button_rb":"Right Shoulder","button_l3":"Left Stick Click","button_r3":"Right Stick Click","button_start":"Start / Options","button_back":"Back / Share / Create","button_guide":"Guide / PS","button_misc":"Misc / Mute","button_touchpad":"Touchpad Click","paddle_1":"Paddle 1","paddle_2":"Paddle 2","paddle_3":"Paddle 3","paddle_4":"Paddle 4","trigger_left":"Left Trigger","trigger_right":"Right Trigger","left_stick":"Left Stick","right_stick":"Right Stick"}
        return labels[source] || source
    }
    function openWindow(page) {
        var now = Date.now()
        // The streaming SDL input path and this Qt window can both observe the
        // same shortcut. Treat those duplicate events as a single toggle.
        if (now - lastToggleTime < 250) return
        lastToggleTime = now
        if (menuOpen) { closeWindow(); return }
        if (page !== undefined) selectedPage = page
        refreshMicDevices(); menuOpen = true; show(); raise(); requestActivate()
    }
    function closeWindow() { menuOpen = false; hide() }
    function refreshMicDevices() {
        micDeviceModel.clear()
        micDeviceModel.append({text: qsTr("Windows default microphone"), value: ""})
        micDeviceModel.append({text: qsTr("DualSense controller microphone"), value: "__dualsense__"})
        var devices = StreamingPreferences.microphoneDeviceNames()
        for (var i = 0; i < devices.length; ++i)
            micDeviceModel.append({text: devices[i], value: devices[i]})
        var selected = 0
        for (var j = 0; j < micDeviceModel.count; ++j)
            if (micDeviceModel.get(j).value === StreamingPreferences.micDevice) selected = j
        micCombo.currentIndex = selected
    }
    function shortcutDescription(value, separator) { var n={0:"A / Cross",1:"B / Circle",2:"X / Square",3:"Y / Triangle",4:"Back / Share",5:"Guide / PS",6:"Start / Options",7:"L3",8:"R3",9:"LB",10:"RB",15:"Mute",20:"Touchpad",100:"Left Trigger",101:"Right Trigger"}; var p=value.split(","); var out=[]; for(var i=0;i<p.length;i++) if(n[p[i]]!==undefined) out.push(n[p[i]]); return out.length ? out.join(separator || " + ") : qsTr("None selected") }

    Connections { target: StreamingPreferences; function onControllerKbmMappingsChanged() { root.revision++ } }
    Shortcut {
        sequence: StreamingPreferences.quickMenuKeyboardShortcutDescription()
        context: Qt.ApplicationShortcut
        enabled: root.menuOpen
        onActivated: root.openWindow()
    }

    Rectangle {
        anchors.fill: parent; anchors.margins: 12; radius: 16
        color: "#2d2d2d"; border.color: "#555"; border.width: 1
        Item {
            anchors.fill: parent; anchors.margins: 16
            RowLayout {
                id: windowHeader
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                height: 48
                MouseArea {
                    Layout.fillWidth: true; Layout.fillHeight: true; onPressed: root.startSystemMove()
                    Label { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Extended Quick Menu"); font.pixelSize: 23; font.bold: true }
                }
                Button { text: "✕"; flat: true; font.pixelSize: 18; onClicked: root.closeWindow(); ToolTip.visible: hovered; ToolTip.text: qsTr("Close") }
            }
            Rectangle {
                id: headerDivider
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: windowHeader.bottom
                anchors.topMargin: 5; height: 1; color: "#555"
            }
            RowLayout {
                id: pageArea
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: headerDivider.bottom; anchors.bottom: parent.bottom
                anchors.topMargin: 10; spacing: 14
                Rectangle {
                    Layout.fillHeight: true; Layout.minimumHeight: 0; Layout.preferredWidth: 205; Layout.minimumWidth: 205; Layout.maximumWidth: 205
                    radius: 10; color: "#272727"; border.color: "#484848"
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 10; spacing: 6
                        Label { text: qsTr("SETTINGS"); color: "#aaa"; font.bold: true; leftPadding: 10; bottomPadding: 5 }
                        Button { Layout.fillWidth:true; Layout.preferredHeight:48; text:qsTr("Stream"); highlighted:root.selectedPage===0; onClicked:root.selectedPage=0 }
                        Button { Layout.fillWidth:true; Layout.preferredHeight:48; text:qsTr("DualSense Features"); highlighted:root.selectedPage===1; onClicked:root.selectedPage=1 }
                        Button { Layout.fillWidth: true; Layout.preferredHeight: 48; text: qsTr("Controller KBM"); highlighted: root.selectedPage === 2; onClicked: root.selectedPage = 2 }
                        Item { Layout.fillHeight: true }
                    }
                }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: 0; Layout.minimumHeight: 0; visible: root.selectedPage === 2
                    ColumnLayout {
                        anchors.fill: parent; spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Controller KBM Mode"); font.pixelSize: 22; font.bold: true; Layout.fillWidth: true }
                            CheckBox { text: qsTr("Enabled"); checked: StreamingPreferences.controllerKbmMode; onToggled: { StreamingPreferences.controllerKbmMode = checked; StreamingPreferences.commitControllerKbmSettings() } }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#555" }
                        RowLayout {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: 0; Layout.minimumHeight: 0; spacing: 16
                            ColumnLayout {
                                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: 0; Layout.minimumHeight: 0; Layout.preferredWidth: 560
                                Label { text: qsTr("Buttons Manager"); font.pixelSize: 18; font.bold: true }
                                ScrollView {
                                    id: mappingScroll; Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0; clip: true; contentWidth: availableWidth
                                    ColumnLayout {
                                        width: mappingScroll.availableWidth; spacing: 5
                                        Repeater {
                                            model: [
                                                {id:"button_a",label:"A / Cross"},{id:"button_b",label:"B / Circle"},{id:"button_x",label:"X / Square"},{id:"button_y",label:"Y / Triangle"},
                                                {id:"dpad_up",label:"D-pad Up"},{id:"dpad_down",label:"D-pad Down"},{id:"dpad_left",label:"D-pad Left"},{id:"dpad_right",label:"D-pad Right"},
                                                {id:"button_lb",label:"Left Shoulder"},{id:"button_rb",label:"Right Shoulder"},{id:"button_l3",label:"Left Stick Click"},{id:"button_r3",label:"Right Stick Click"},
                                                {id:"button_start",label:"Start / Options"},{id:"button_back",label:"Back / Share / Create"},{id:"button_guide",label:"Guide / PS"},{id:"button_misc",label:"Misc / Mute"},
                                                {id:"button_touchpad",label:"Touchpad Click"},{id:"paddle_1",label:"Paddle 1"},{id:"paddle_2",label:"Paddle 2"},{id:"paddle_3",label:"Paddle 3"},{id:"paddle_4",label:"Paddle 4"},
                                                {id:"trigger_left",label:"Left Trigger"},{id:"trigger_right",label:"Right Trigger"},{id:"left_stick",label:"Left Stick",axis:true},{id:"right_stick",label:"Right Stick",axis:true}
                                            ]
                                            delegate: Rectangle {
                                                Layout.fillWidth: true; height: 48; radius: 7; color: index % 2 ? "#343434" : "#393939"
                                                property string action: { root.revision; return StreamingPreferences.controllerKbmAction(modelData.id) }
                                                RowLayout {
                                                    anchors.fill: parent; anchors.margins: 8
                                                    Label { text:modelData.label; Layout.preferredWidth:145; elide:Text.ElideRight }
                                                    Label { text:parent.parent.action.indexOf("directed_flick:")===0?qsTr("Directed flick"):StreamingPreferences.controllerKbmActionDescription(parent.parent.action);color:"#bbb";Layout.fillWidth:true;elide:Text.ElideRight }
                                                    Button { visible:parent.parent.action.indexOf("directed_flick:")===0;text:"−";Layout.preferredWidth:38;Layout.preferredHeight:32;onClicked:{var p=parent.parent.action.split(":");var d=Math.max(50,parseInt(p[2])-50);StreamingPreferences.setControllerKbmAction(modelData.id,"directed_flick:"+p[1]+":"+d)} }
                                                    TextField { visible:parent.parent.action.indexOf("directed_flick:")===0;readOnly:true;text:visible?parent.parent.action.split(":")[2]+" px":"";Layout.preferredWidth:72;Layout.preferredHeight:32;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter }
                                                    Button { visible:parent.parent.action.indexOf("directed_flick:")===0;text:"+";Layout.preferredWidth:38;Layout.preferredHeight:32;onClicked:{var p=parent.parent.action.split(":");var d=Math.min(4000,parseInt(p[2])+50);StreamingPreferences.setControllerKbmAction(modelData.id,"directed_flick:"+p[1]+":"+d)} }
                                                    Button { text:qsTr("Assign");Layout.preferredWidth:76;Layout.preferredHeight:34;onClicked:mappingPopup.openFor(modelData.id,modelData.axis===true) }
                                                }
                                            }
                                        }
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: "#555" }
                                Label { text: qsTr("Presets"); font.pixelSize: 17; font.bold: true }
                                RowLayout {
                                    Layout.fillWidth: true
                                    ComboBox { id: presetCombo; Layout.fillWidth: true; model: StreamingPreferences.controllerKbmPresetNames; currentIndex: StreamingPreferences.controllerKbmActivePresetIndex }
                                    Button { text: qsTr("Load"); enabled: presetCombo.currentIndex >= 0; onClicked: StreamingPreferences.loadControllerKbmPreset(presetCombo.currentIndex) }
                                    Button { text: qsTr("Delete"); enabled: presetCombo.currentIndex >= 0; onClicked: StreamingPreferences.deleteControllerKbmPreset(presetCombo.currentIndex) }
                                }
                                RowLayout {
                                    Button { text: qsTr("Save new"); onClicked: presetPopup.open() }
                                    Button { text: qsTr("Save current"); enabled: presetCombo.currentIndex >= 0; onClicked: StreamingPreferences.updateControllerKbmPreset(presetCombo.currentIndex) }
                                    Button { text: qsTr("Import"); onClicked: importDialog.open() }
                                    Button { text: qsTr("Export"); enabled: presetCombo.currentIndex >= 0; onClicked: exportDialog.open() }
                                    Button { text: qsTr("Reset"); onClicked: StreamingPreferences.resetControllerKbmMappings() }
                                }
                            }
                            Rectangle { Layout.fillHeight: true; width: 1; color: "#555" }
                            ScrollView {
                                id: inputSettingsScroll
                                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: 0; Layout.minimumHeight: 0; Layout.preferredWidth: 440
                                clip: true; contentWidth: availableWidth
                              ColumnLayout {
                                width: inputSettingsScroll.availableWidth; spacing: 14
                                Label { text: qsTr("Mouse"); font.pixelSize: 18; font.bold: true }
                                Label { text: qsTr("Mouse stick speed") }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Slider { Layout.fillWidth: true; from:10; to:400; stepSize:5; value:StreamingPreferences.controllerKbmStickSpeed; onMoved:{StreamingPreferences.controllerKbmStickSpeed=Math.round(value);StreamingPreferences.commitControllerKbmSettings()} }
                                    SpinBox { Layout.preferredWidth:120; editable:true; from:10; to:400; value:StreamingPreferences.controllerKbmStickSpeed; onValueModified:{StreamingPreferences.controllerKbmStickSpeed=value;StreamingPreferences.commitControllerKbmSettings()} }
                                }
                                CheckBox { text: qsTr("Continuous stick mouse movement"); checked: StreamingPreferences.controllerKbmContinuousStickMouse; onToggled: {StreamingPreferences.controllerKbmContinuousStickMouse=checked;StreamingPreferences.commitControllerKbmSettings()} }
                                Rectangle { Layout.fillWidth:true; height:1; color:"#555" }
                                Label { text:qsTr("Gyro mouse"); font.pixelSize:18; font.bold:true }
                                CheckBox { text:qsTr("Gyro mouse enabled"); checked:StreamingPreferences.controllerKbmGyroEnabled; onToggled:{StreamingPreferences.controllerKbmGyroEnabled=checked;StreamingPreferences.commitControllerKbmSettings()} }
                                CheckBox { text:qsTr("Only active while an activation button is held"); checked:StreamingPreferences.controllerKbmGyroHoldMode; onToggled:{StreamingPreferences.controllerKbmGyroHoldMode=checked;StreamingPreferences.commitControllerKbmSettings()} }
                                Label { text:qsTr("Per-axis sensitivity and inversion") }
                                RowLayout {Layout.fillWidth:true;Label{text:"X";Layout.preferredWidth:18} Slider{Layout.fillWidth:true;Layout.minimumWidth:80;from:10;to:500;stepSize:5;value:StreamingPreferences.controllerKbmGyroXSensitivity;onMoved:{StreamingPreferences.controllerKbmGyroXSensitivity=Math.round(value);StreamingPreferences.commitControllerKbmSettings()}} SpinBox{Layout.preferredWidth:132;Layout.minimumWidth:132;Layout.maximumWidth:132;editable:true;from:10;to:500;stepSize:5;value:StreamingPreferences.controllerKbmGyroXSensitivity;onValueModified:{StreamingPreferences.controllerKbmGyroXSensitivity=value;StreamingPreferences.commitControllerKbmSettings()}} CheckBox{text:qsTr("Invert");checked:StreamingPreferences.controllerKbmGyroXInverted;onToggled:{StreamingPreferences.controllerKbmGyroXInverted=checked;StreamingPreferences.commitControllerKbmSettings()}}}
                                RowLayout {Layout.fillWidth:true;Label{text:"Y";Layout.preferredWidth:18} Slider{Layout.fillWidth:true;Layout.minimumWidth:80;from:10;to:500;stepSize:5;value:StreamingPreferences.controllerKbmGyroYSensitivity;onMoved:{StreamingPreferences.controllerKbmGyroYSensitivity=Math.round(value);StreamingPreferences.commitControllerKbmSettings()}} SpinBox{Layout.preferredWidth:132;Layout.minimumWidth:132;Layout.maximumWidth:132;editable:true;from:10;to:500;stepSize:5;value:StreamingPreferences.controllerKbmGyroYSensitivity;onValueModified:{StreamingPreferences.controllerKbmGyroYSensitivity=value;StreamingPreferences.commitControllerKbmSettings()}} CheckBox{text:qsTr("Invert");checked:StreamingPreferences.controllerKbmGyroYInverted;onToggled:{StreamingPreferences.controllerKbmGyroYInverted=checked;StreamingPreferences.commitControllerKbmSettings()}}}
                                RowLayout {Layout.fillWidth:true;Label{text:"Z";Layout.preferredWidth:18} Slider{Layout.fillWidth:true;Layout.minimumWidth:80;from:10;to:500;stepSize:5;value:StreamingPreferences.controllerKbmGyroZSensitivity;onMoved:{StreamingPreferences.controllerKbmGyroZSensitivity=Math.round(value);StreamingPreferences.commitControllerKbmSettings()}} SpinBox{Layout.preferredWidth:132;Layout.minimumWidth:132;Layout.maximumWidth:132;editable:true;from:10;to:500;stepSize:5;value:StreamingPreferences.controllerKbmGyroZSensitivity;onValueModified:{StreamingPreferences.controllerKbmGyroZSensitivity=value;StreamingPreferences.commitControllerKbmSettings()}} CheckBox{text:qsTr("Invert");checked:StreamingPreferences.controllerKbmGyroZInverted;onToggled:{StreamingPreferences.controllerKbmGyroZInverted=checked;StreamingPreferences.commitControllerKbmSettings()}}}
                                RowLayout { Layout.fillWidth:true; visible:!StreamingPreferences.controllerKbmGyroHoldMode
                                    Label { text:qsTr("Toggle shortcut"); Layout.fillWidth:true }
                                    Button { text:root.shortcutDescription(StreamingPreferences.controllerKbmGyroShortcut); onClicked:gyroShortcutPopup.openForCurrent() }
                                }
                                ColumnLayout { Layout.fillWidth:true; visible:StreamingPreferences.controllerKbmGyroHoldMode
                                    Label { text:qsTr("Gyro activation buttons"); font.bold:true }
                                    Label { Layout.fillWidth:true; text:qsTr("Gyro mouse stays active while any selected button is held. Selected buttons keep their normal assigned action."); wrapMode:Text.Wrap; color:"#bbb" }
                                    RowLayout { Layout.fillWidth:true
                                        Label { Layout.fillWidth:true; text:root.shortcutDescription(StreamingPreferences.controllerKbmGyroActivationButtons, ", "); wrapMode:Text.Wrap }
                                        Button { text:qsTr("Configure"); onClicked:gyroActivationPopup.openForCurrent() }
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; height:1; color:"#555" }
                                Label { text: qsTr("Left trigger"); font.pixelSize: 18; font.bold: true }
                                Label { text: qsTr("Threshold (%)") }
                                RowLayout {
                                    Layout.fillWidth:true
                                    Slider { Layout.fillWidth:true; from:1; to:100; stepSize:1; value:StreamingPreferences.controllerKbmLeftTriggerThreshold; onMoved:{StreamingPreferences.controllerKbmLeftTriggerThreshold=Math.round(value);StreamingPreferences.commitControllerKbmSettings()} }
                                    SpinBox { Layout.preferredWidth:120; editable:true; from:1; to:100; value:StreamingPreferences.controllerKbmLeftTriggerThreshold; onValueModified:{StreamingPreferences.controllerKbmLeftTriggerThreshold=value;StreamingPreferences.commitControllerKbmSettings()} }
                                }
                                Label { text: qsTr("Trigger behavior") }
                                ComboBox { Layout.fillWidth:true; model:[qsTr("Hold"),qsTr("Single press"),qsTr("Repeat")]; currentIndex:StreamingPreferences.controllerKbmLeftTriggerBehavior==="single"?1:StreamingPreferences.controllerKbmLeftTriggerBehavior==="repeat"?2:0; onActivated:{StreamingPreferences.controllerKbmLeftTriggerBehavior=["hold","single","repeat"][index];StreamingPreferences.commitControllerKbmSettings()} }
                                Label { text: qsTr("Trigger repeat speed"); enabled: StreamingPreferences.controllerKbmLeftTriggerBehavior==="repeat" }
                                RowLayout {
                                    Layout.fillWidth:true; enabled:StreamingPreferences.controllerKbmLeftTriggerBehavior==="repeat"; opacity:enabled?1:0.35
                                    Slider { Layout.fillWidth:true; from:1; to:30; stepSize:1; value:StreamingPreferences.controllerKbmLeftTriggerRepeatRate; onMoved:{StreamingPreferences.controllerKbmLeftTriggerRepeatRate=Math.round(value);StreamingPreferences.commitControllerKbmSettings()} }
                                    SpinBox { Layout.preferredWidth:120; editable:true; from:1; to:30; value:StreamingPreferences.controllerKbmLeftTriggerRepeatRate; onValueModified:{StreamingPreferences.controllerKbmLeftTriggerRepeatRate=value;StreamingPreferences.commitControllerKbmSettings()} }
                                }
                                Rectangle { Layout.fillWidth: true; height:1; color:"#555" }
                                Label { text: qsTr("Right trigger"); font.pixelSize: 18; font.bold: true }
                                Label { text: qsTr("Threshold (%)") }
                                RowLayout { Layout.fillWidth:true
                                    Slider { Layout.fillWidth:true; from:1; to:100; stepSize:1; value:StreamingPreferences.controllerKbmRightTriggerThreshold; onMoved:{StreamingPreferences.controllerKbmRightTriggerThreshold=Math.round(value);StreamingPreferences.commitControllerKbmSettings()} }
                                    SpinBox { Layout.preferredWidth:120; editable:true; from:1; to:100; value:StreamingPreferences.controllerKbmRightTriggerThreshold; onValueModified:{StreamingPreferences.controllerKbmRightTriggerThreshold=value;StreamingPreferences.commitControllerKbmSettings()} }
                                }
                                Label { text: qsTr("Trigger behavior") }
                                ComboBox { Layout.fillWidth:true; model:[qsTr("Hold"),qsTr("Single press"),qsTr("Repeat")]; currentIndex:StreamingPreferences.controllerKbmRightTriggerBehavior==="single"?1:StreamingPreferences.controllerKbmRightTriggerBehavior==="repeat"?2:0; onActivated:{StreamingPreferences.controllerKbmRightTriggerBehavior=["hold","single","repeat"][index];StreamingPreferences.commitControllerKbmSettings()} }
                                Label { text: qsTr("Trigger repeat speed"); enabled: StreamingPreferences.controllerKbmRightTriggerBehavior==="repeat" }
                                RowLayout { Layout.fillWidth:true; enabled:StreamingPreferences.controllerKbmRightTriggerBehavior==="repeat"; opacity:enabled?1:0.35
                                    Slider { Layout.fillWidth:true; from:1; to:30; stepSize:1; value:StreamingPreferences.controllerKbmRightTriggerRepeatRate; onMoved:{StreamingPreferences.controllerKbmRightTriggerRepeatRate=Math.round(value);StreamingPreferences.commitControllerKbmSettings()} }
                                    SpinBox { Layout.preferredWidth:120; editable:true; from:1; to:30; value:StreamingPreferences.controllerKbmRightTriggerRepeatRate; onValueModified:{StreamingPreferences.controllerKbmRightTriggerRepeatRate=value;StreamingPreferences.commitControllerKbmSettings()} }
                                }
                                Item { Layout.fillHeight: true }
                              }
                            }
                        }
                    }
                }
                Item {Layout.fillWidth:true;Layout.fillHeight:true;visible:root.selectedPage===0
                    ScrollView{anchors.fill:parent;clip:true;contentWidth:availableWidth
                        ColumnLayout{width:parent.width;spacing:16
                            Label{text:qsTr("Stream");font.pixelSize:22;font.bold:true}
                            Rectangle{Layout.fillWidth:true;height:1;color:"#555"}
                            RowLayout{Button{text:qsTr("Disconnect");onClicked:{root.closeWindow();StreamingPreferences.requestStreamQuickAction("disconnect")}}Button{text:qsTr("Quit session");onClicked:{root.closeWindow();StreamingPreferences.requestStreamQuickAction("quit")}}Button{text:qsTr("Toggle performance overlay");onClicked:StreamingPreferences.requestStreamQuickAction("performance")}}
                            Label{text:qsTr("Clipboard");font.pixelSize:18;font.bold:true}
                            RowLayout{Button{text:qsTr("Upload clipboard");onClicked:StreamingPreferences.requestStreamQuickAction("uploadClipboard")}Button{text:qsTr("Fetch clipboard");onClicked:StreamingPreferences.requestStreamQuickAction("fetchClipboard")}}
                            Rectangle{Layout.fillWidth:true;height:1;color:"#555"}
                            Label{text:qsTr("Microphone forwarding");font.pixelSize:18;font.bold:true}
                            CheckBox{text:qsTr("Forward microphone to host");checked:StreamingPreferences.micCapture;onToggled:{StreamingPreferences.micCapture=checked;StreamingPreferences.save();StreamingPreferences.requestStreamQuickAction("microphone")}}
                            ListModel{id:micDeviceModel}
                            ComboBox{id:micCombo;Layout.fillWidth:true;model:micDeviceModel;textRole:"text";valueRole:"value";onActivated:{StreamingPreferences.micDevice=currentValue;StreamingPreferences.save();StreamingPreferences.requestStreamQuickAction("microphone")}}
                            RowLayout{Layout.fillWidth:true;Label{text:qsTr("Microphone mute shortcut");Layout.fillWidth:true}Button{text:StreamingPreferences.microphoneMuteKeyboardShortcutDescription();onClicked:micMuteShortcutPopup.open()}}
                            Label{text:qsTr("Keyboard mute shortcut uses the same global client microphone mute state as the DualSense mute button.");wrapMode:Text.Wrap;color:"#bbb"}
                            Item{Layout.fillHeight:true}
                        }
                    }
                }
                Item {Layout.fillWidth:true;Layout.fillHeight:true;visible:root.selectedPage===1
                    ScrollView{anchors.fill:parent;clip:true;contentWidth:availableWidth
                        ColumnLayout{width:parent.width;spacing:14
                            Label{text:qsTr("DualSense Features");font.pixelSize:22;font.bold:true}
                            Label{text:qsTr("Gyroscope axis override");font.pixelSize:18;font.bold:true}
                            CheckBox{text:qsTr("Enable gyroscope axis override");checked:StreamingPreferences.gyroOverrideEnabled;onToggled:{StreamingPreferences.gyroOverrideEnabled=checked;StreamingPreferences.save()}}
                            GridLayout{columns:5;enabled:StreamingPreferences.gyroOverrideEnabled;opacity:enabled?1:0.4
                                Label{text:"X"} ComboBox{model:["X","Y","Z"];currentIndex:StreamingPreferences.gyroXAxisSource;onActivated:{StreamingPreferences.gyroXAxisSource=index;StreamingPreferences.save()}} CheckBox{text:qsTr("Invert");checked:StreamingPreferences.gyroXAxisInverted;onToggled:{StreamingPreferences.gyroXAxisInverted=checked;StreamingPreferences.save()}} CheckBox{text:qsTr("Disable");checked:StreamingPreferences.gyroXAxisDisabled;onToggled:{StreamingPreferences.gyroXAxisDisabled=checked;StreamingPreferences.save()}} Item{Layout.fillWidth:true}
                                Label{text:"Y"} ComboBox{model:["X","Y","Z"];currentIndex:StreamingPreferences.gyroYAxisSource;onActivated:{StreamingPreferences.gyroYAxisSource=index;StreamingPreferences.save()}} CheckBox{text:qsTr("Invert");checked:StreamingPreferences.gyroYAxisInverted;onToggled:{StreamingPreferences.gyroYAxisInverted=checked;StreamingPreferences.save()}} CheckBox{text:qsTr("Disable");checked:StreamingPreferences.gyroYAxisDisabled;onToggled:{StreamingPreferences.gyroYAxisDisabled=checked;StreamingPreferences.save()}} Item{Layout.fillWidth:true}
                                Label{text:"Z"} ComboBox{model:["X","Y","Z"];currentIndex:StreamingPreferences.gyroZAxisSource;onActivated:{StreamingPreferences.gyroZAxisSource=index;StreamingPreferences.save()}} CheckBox{text:qsTr("Invert");checked:StreamingPreferences.gyroZAxisInverted;onToggled:{StreamingPreferences.gyroZAxisInverted=checked;StreamingPreferences.save()}} CheckBox{text:qsTr("Disable");checked:StreamingPreferences.gyroZAxisDisabled;onToggled:{StreamingPreferences.gyroZAxisDisabled=checked;StreamingPreferences.save()}} Item{Layout.fillWidth:true}
                            }
                            Button{text:qsTr("Save gyroscope override");onClicked:StreamingPreferences.save()}
                            Rectangle{Layout.fillWidth:true;height:1;color:"#555"}
                            Label{text:qsTr("Controller built-in speaker volume");font.pixelSize:18;font.bold:true}
                            RowLayout{Layout.fillWidth:true;Slider{Layout.fillWidth:true;from:0;to:100;stepSize:1;value:StreamingPreferences.dualSenseControllerVolume;onMoved:{StreamingPreferences.dualSenseControllerVolume=Math.round(value);StreamingPreferences.save();StreamingPreferences.requestStreamQuickAction("controllerVolume")}}SpinBox{from:0;to:100;value:StreamingPreferences.dualSenseControllerVolume;onValueModified:{StreamingPreferences.dualSenseControllerVolume=value;StreamingPreferences.save();StreamingPreferences.requestStreamQuickAction("controllerVolume")}}}
                            Rectangle{Layout.fillWidth:true;height:1;color:"#555"}
                            Label{text:qsTr("Trigger override");font.pixelSize:18;font.bold:true}
                            CheckBox{text:qsTr("Convert each trigger to fully pressed after its threshold");checked:StreamingPreferences.triggerOverrideEnabled;onToggled:{StreamingPreferences.triggerOverrideEnabled=checked;StreamingPreferences.save()}}
                            Label{text:qsTr("Left trigger threshold (%)")}
                            RowLayout{Layout.fillWidth:true;Slider{Layout.fillWidth:true;from:1;to:100;value:StreamingPreferences.leftTriggerOverrideThreshold;onMoved:{StreamingPreferences.leftTriggerOverrideThreshold=Math.round(value);StreamingPreferences.save()}}SpinBox{from:1;to:100;value:StreamingPreferences.leftTriggerOverrideThreshold;onValueModified:{StreamingPreferences.leftTriggerOverrideThreshold=value;StreamingPreferences.save()}}}
                            Label{text:qsTr("Right trigger threshold (%)")}
                            RowLayout{Layout.fillWidth:true;Slider{Layout.fillWidth:true;from:1;to:100;value:StreamingPreferences.rightTriggerOverrideThreshold;onMoved:{StreamingPreferences.rightTriggerOverrideThreshold=Math.round(value);StreamingPreferences.save()}}SpinBox{from:1;to:100;value:StreamingPreferences.rightTriggerOverrideThreshold;onValueModified:{StreamingPreferences.rightTriggerOverrideThreshold=value;StreamingPreferences.save()}}}
                            Item{Layout.fillHeight:true}
                        }
                    }
                }
            }
        }
    }

    Popup {
        id:micMuteShortcutPopup;modal:true;dim:true;focus:true;width:500;height:220;x:(root.width-width)/2;y:(root.height-height)/2;closePolicy:Popup.CloseOnEscape
        onOpened:micMuteKeyCapture.forceActiveFocus()
        Overlay.modal:Rectangle{color:"#99000000"}
        background:Rectangle{radius:14;color:"#303030";border.color:"#555"}
        contentItem:ColumnLayout{RowLayout{Layout.fillWidth:true;Label{text:qsTr("Microphone mute keyboard shortcut");font.pixelSize:20;font.bold:true;Layout.fillWidth:true}Button{text:"✕";flat:true;onClicked:micMuteShortcutPopup.close()}}Label{text:qsTr("Press a shortcut containing at least two keys.");color:"#bbb"}TextField{id:micMuteKeyCapture;Layout.fillWidth:true;readOnly:true;placeholderText:qsTr("Waiting for shortcut...");Keys.onPressed:function(e){var k=e.nativeVirtualKey>0?e.nativeVirtualKey:e.key;var m=0;var c=1;if(e.modifiers&Qt.ControlModifier){m|=1;c++}if(e.modifiers&Qt.AltModifier){m|=2;c++}if(e.modifiers&Qt.ShiftModifier){m|=4;c++}if(e.modifiers&Qt.MetaModifier){m|=8;c++}if(c>=2&&k!==Qt.Key_Control&&k!==Qt.Key_Alt&&k!==Qt.Key_Shift&&k!==Qt.Key_Meta){StreamingPreferences.microphoneMuteKeyboardKey=k;StreamingPreferences.microphoneMuteKeyboardModifiers=m;StreamingPreferences.save();micMuteShortcutPopup.close();e.accepted=true}}}Item{Layout.fillHeight:true}}
    }
    Popup {
        id:gyroShortcutPopup;modal:true;dim:true;focus:true;width:600;height:360;x:(root.width-width)/2;y:(root.height-height)/2;closePolicy:Popup.CloseOnEscape
        property string selection:""
        function openForCurrent(){selection=StreamingPreferences.controllerKbmGyroShortcut;open()}
        function selected(i){return selection.split(",").indexOf(String(i))>=0}
        function toggle(i,on){var a=selection.length?selection.split(","):[];var s=String(i);var p=a.indexOf(s);if(on&&p<0)a.push(s);if(!on&&p>=0)a.splice(p,1);selection=a.join(",")}
        Overlay.modal:Rectangle{color:"#99000000"} background:Rectangle{radius:14;color:"#303030";border.color:"#555"}
        contentItem:ColumnLayout{RowLayout{Layout.fillWidth:true;Label{text:qsTr("Gyro mouse toggle shortcut");font.pixelSize:20;font.bold:true;Layout.fillWidth:true}Button{text:"✕";flat:true;onClicked:gyroShortcutPopup.close()}}
            Label{text:qsTr("Select at least two controller buttons.");color:"#bbb"}
            GridLayout{columns:3;Layout.fillWidth:true
                Repeater{model:[{i:0,n:"A / Cross"},{i:1,n:"B / Circle"},{i:2,n:"X / Square"},{i:3,n:"Y / Triangle"},{i:4,n:"Back / Share"},{i:5,n:"Guide / PS"},{i:6,n:"Start / Options"},{i:7,n:"L3"},{i:8,n:"R3"},{i:9,n:"LB"},{i:10,n:"RB"},{i:15,n:"Mute"},{i:20,n:"Touchpad"}];delegate:CheckBox{text:modelData.n;checked:gyroShortcutPopup.selected(modelData.i);onToggled:gyroShortcutPopup.toggle(modelData.i,checked)}}
            }
            Item{Layout.fillHeight:true}RowLayout{Layout.alignment:Qt.AlignRight;Button{text:qsTr("Cancel");onClicked:gyroShortcutPopup.close()}Button{text:qsTr("Apply");enabled:gyroShortcutPopup.selection.split(",").filter(function(x){return x.length>0}).length>=2;onClicked:{StreamingPreferences.controllerKbmGyroShortcut=gyroShortcutPopup.selection;StreamingPreferences.commitControllerKbmSettings();gyroShortcutPopup.close()}}}
        }
    }
    Popup {
        id:gyroActivationPopup;modal:true;dim:true;focus:true;width:620;height:420;x:(root.width-width)/2;y:(root.height-height)/2;closePolicy:Popup.CloseOnEscape
        property string selection:""
        function openForCurrent(){selection=StreamingPreferences.controllerKbmGyroActivationButtons;open()}
        function selected(i){return selection.split(",").indexOf(String(i))>=0}
        function toggle(i,on){var a=selection.length?selection.split(","):[];var s=String(i);var p=a.indexOf(s);if(on&&p<0)a.push(s);if(!on&&p>=0)a.splice(p,1);selection=a.join(",")}
        Overlay.modal:Rectangle{color:"#99000000"} background:Rectangle{radius:14;color:"#303030";border.color:"#555"}
        contentItem:ColumnLayout{
            RowLayout{Layout.fillWidth:true;Label{text:qsTr("Gyro activation buttons");font.pixelSize:20;font.bold:true;Layout.fillWidth:true}Button{text:"✕";flat:true;onClicked:gyroActivationPopup.close()}}
            Label{Layout.fillWidth:true;text:qsTr("Holding any one of the selected buttons activates gyro mouse. The buttons continue performing their normal actions.");wrapMode:Text.Wrap;color:"#bbb"}
            GridLayout{columns:3;Layout.fillWidth:true
                Repeater{model:[{i:0,n:"A / Cross"},{i:1,n:"B / Circle"},{i:2,n:"X / Square"},{i:3,n:"Y / Triangle"},{i:4,n:"Back / Share"},{i:5,n:"Guide / PS"},{i:6,n:"Start / Options"},{i:7,n:"L3"},{i:8,n:"R3"},{i:9,n:"LB"},{i:10,n:"RB"},{i:15,n:"Mute"},{i:20,n:"Touchpad"},{i:100,n:"Left Trigger"},{i:101,n:"Right Trigger"}];delegate:CheckBox{text:modelData.n;checked:gyroActivationPopup.selected(modelData.i);onToggled:gyroActivationPopup.toggle(modelData.i,checked)}}
            }
            Item{Layout.fillHeight:true}
            RowLayout{Layout.alignment:Qt.AlignRight;Button{text:qsTr("Cancel");onClicked:gyroActivationPopup.close()}Button{text:qsTr("Apply");enabled:gyroActivationPopup.selection.length>0;onClicked:{StreamingPreferences.controllerKbmGyroActivationButtons=gyroActivationPopup.selection;StreamingPreferences.commitControllerKbmSettings();gyroActivationPopup.close()}}}
        }
    }
    Popup {
        id: mappingPopup; modal:true; dim:true; focus:true; width:500; height:330; x:(root.width-width)/2; y:(root.height-height)/2; closePolicy:Popup.CloseOnEscape
        property string sourceId:""; property bool axis:false
        function openFor(source,isAxis){sourceId=source;axis=isAxis;actionCombo.currentIndex=0;open()}
        onOpened: if (!axis) assignKeyCapture.forceActiveFocus()
        Overlay.modal: Rectangle { color:"#99000000" }
        background: Rectangle { radius:14; color:"#303030"; border.color:"#555" }
        contentItem: ColumnLayout {
            RowLayout { Layout.fillWidth:true; Label{text:qsTr("Assign action for: %1").arg(root.sourceLabel(mappingPopup.sourceId));font.pixelSize:20;font.bold:true;Layout.fillWidth:true} Button{text:"✕";flat:true;onClicked:mappingPopup.close()} }
            TextField {
                id: assignKeyCapture; Layout.fillWidth:true; visible:!mappingPopup.axis
                readOnly:true; placeholderText:qsTr("Waiting for a key...")
                Keys.onPressed:function(e){StreamingPreferences.assignControllerKbmKeyboardKey(mappingPopup.sourceId,e.key,e.nativeVirtualKey||0,e.nativeScanCode||0);mappingPopup.close();e.accepted=true}
            }
            Label { visible:!mappingPopup.axis; text:qsTr("or choose an action from below"); color:"#bbb" }
            ComboBox { id:actionCombo; Layout.fillWidth:true; model:mappingPopup.axis?[qsTr("Unassigned"),qsTr("Mouse movement"),qsTr("Two-axis scrolling"),qsTr("WASD"),qsTr("Arrow keys")]:[qsTr("Unassigned"),qsTr("Left mouse click"),qsTr("Right mouse click"),qsTr("Middle mouse click"),qsTr("Mouse back button"),qsTr("Mouse forward button"),qsTr("Mouse wheel up"),qsTr("Mouse wheel down"),qsTr("Directed flick")] }
            Item { Layout.fillHeight:true }
            RowLayout { Layout.alignment:Qt.AlignRight; Button{text:qsTr("Cancel");onClicked:mappingPopup.close()} Button{text:qsTr("Apply");onClicked:{var a=(mappingPopup.axis?["","mouse_move","scroll","basic_wasd","basic_arrows"]:["","mouse_left","mouse_right","mouse_middle","mouse_back","mouse_forward","wheel_up","wheel_down","directed_flick"])[actionCombo.currentIndex];mappingPopup.close();if(a==="directed_flick"){flickPopup.sourceId=mappingPopup.sourceId;flickPopup.open()}else StreamingPreferences.setControllerKbmAction(mappingPopup.sourceId,a)}} }
        }
    }
    Popup {
        id:flickPopup;modal:true;dim:true;width:480;height:230;x:(root.width-width)/2;y:(root.height-height)/2;closePolicy:Popup.CloseOnEscape;property string sourceId:""
        Overlay.modal:Rectangle{color:"#99000000"}
        background:Rectangle{radius:14;color:"#303030";border.color:"#555"}
        contentItem:ColumnLayout{RowLayout{Layout.fillWidth:true;Label{text:qsTr("Directed flick");font.pixelSize:20;font.bold:true;Layout.fillWidth:true} Button{text:"✕";flat:true;onClicked:flickPopup.close()}} ComboBox{id:flickDir;Layout.fillWidth:true;model:[qsTr("Left"),qsTr("Right"),qsTr("Up"),qsTr("Down")]} SpinBox{id:flickDistance;Layout.fillWidth:true;editable:true;from:50;to:4000;stepSize:50;value:900} RowLayout{Layout.alignment:Qt.AlignRight;Button{text:qsTr("Cancel");onClicked:flickPopup.close()} Button{text:qsTr("Apply");onClicked:{StreamingPreferences.setControllerKbmAction(flickPopup.sourceId,"directed_flick:"+["left","right","up","down"][flickDir.currentIndex]+":"+flickDistance.value);flickPopup.close()}}}}
    }
    Popup {
        id:presetPopup;modal:true;dim:true;width:460;height:210;x:(root.width-width)/2;y:(root.height-height)/2;closePolicy:Popup.CloseOnEscape
        Overlay.modal:Rectangle{color:"#99000000"}
        background:Rectangle{radius:14;color:"#303030";border.color:"#555"}
        contentItem:ColumnLayout{RowLayout{Layout.fillWidth:true;Label{text:qsTr("Save preset");font.pixelSize:20;font.bold:true;Layout.fillWidth:true} Button{text:"✕";flat:true;onClicked:presetPopup.close()}} TextField{id:presetName;Layout.fillWidth:true;placeholderText:qsTr("Preset name")} RowLayout{Layout.alignment:Qt.AlignRight;Button{text:qsTr("Cancel");onClicked:presetPopup.close()} Button{text:qsTr("Save");onClicked:{StreamingPreferences.saveControllerKbmPreset(presetName.text);presetName.text="";presetPopup.close()}}}}
    }
    FileDialog{id:importDialog;fileMode:FileDialog.OpenFile;nameFilters:["JSON (*.json)"];onAccepted:StreamingPreferences.importControllerKbmPreset(selectedFile)}
    FileDialog{id:exportDialog;fileMode:FileDialog.SaveFile;defaultSuffix:"json";nameFilters:["JSON (*.json)"];onAccepted:StreamingPreferences.exportControllerKbmPreset(presetCombo.currentIndex,selectedFile)}
}
