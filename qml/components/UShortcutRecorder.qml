import QtQuick
import Unisic

Rectangle {
    id: root

    property string shortcut: ""
    property string placeholder: qsTr("Click to record")
    property bool recording: false
    // Ghost-button look for the "+ Add shortcut" affordance in UShortcutList:
    // accent text, hugging width, no filled background until recording.
    property bool addStyle: false

    signal recorded(string shortcut)

    implicitWidth: addStyle ? label.implicitWidth + 28 : 220
    implicitHeight: addStyle ? 32 : 40
    radius: Theme.radiusM
    color: recording ? Theme.alpha(Theme.accent, 0.16)
         : addStyle ? (hoverArea.containsMouse ? Theme.alpha(Theme.accent, 0.10) : "transparent")
         : Theme.surfaceHi
    border.width: activeFocus || recording ? 2 : 1
    border.color: recording ? Theme.accent
                : (activeFocus ? Theme.accent
                : (addStyle ? Theme.alpha(Theme.accent, 0.55) : Theme.divider))
    activeFocusOnTab: true

    function beginRecording() {
        recording = true
        forceActiveFocus()
        App.setShortcutRecording(true)
    }

    function endRecording() {
        if (!recording)
            return
        recording = false
        App.setShortcutRecording(false)
    }

    Behavior on color { ColorAnimation { duration: Theme.animFast } }
    Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

    Text {
        id: label
        anchors.fill: parent
        anchors.leftMargin: root.addStyle ? 0 : 14
        anchors.rightMargin: root.addStyle ? 0 : 14
        horizontalAlignment: root.addStyle ? Text.AlignHCenter : Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        text: root.recording ? qsTr("Press shortcut...")
                            : (root.shortcut.length > 0 ? root.shortcut : root.placeholder)
        color: root.recording ? Theme.accent
             : root.addStyle ? Theme.accent
             : (root.shortcut.length > 0 ? Theme.textPrimary : Theme.textTertiary)
        font.pixelSize: root.addStyle ? Theme.fontS + 1 : Theme.fontM
        elide: Text.ElideRight
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.beginRecording()
    }

    Keys.onPressed: (event) => {
        if (!root.recording)
            return
        event.accepted = true
        if (event.key === Qt.Key_Escape) {
            root.endRecording()
            return
        }
        if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
            root.recorded("")
            root.endRecording()
            return
        }

        // nativeScanCode lets the Shift+symbol remap check the PHYSICAL key
        // (layout-independent) — see ShortcutFormat.h.
        const value = App.formatShortcut(event.key, event.modifiers, event.nativeScanCode)
        if (value.length > 0) {
            root.recorded(value)
            root.endRecording()
        }
    }

    onActiveFocusChanged: {
        if (!activeFocus)
            endRecording()
    }

    Component.onDestruction: App.setShortcutRecording(false)
}
