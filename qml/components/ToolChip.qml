import QtQuick
import Unisic

// Tool selector chip for editor/overlay toolbars.
Rectangle {
    id: root
    property string icon: ""
    property string iconName: ""
    property string iconStyle: ""   // "", "custom" or "system" (editor tools)
    property string label: ""
    property bool active: false
    signal clicked()

    width: 40; height: 40
    radius: Theme.radiusM
    opacity: root.enabled ? 1 : 0.35
    color: active ? Theme.accent
         : mouse.containsMouse ? Theme.alpha(Theme.accent, 0.18)
         : "transparent"
    Behavior on color { ColorAnimation { duration: Theme.animFast } }
    scale: mouse.pressed ? 0.9 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutBack } }

    UIcon {
        visible: root.iconName !== ""
        anchors.centerIn: parent
        name: root.iconName
        iconStyle: root.iconStyle
        color: root.active ? Theme.textOnAccent : Theme.textPrimary
        size: 18
    }
    Text {
        visible: root.iconName === "" && root.icon !== ""
        anchors.centerIn: parent
        text: root.icon
        font.pixelSize: 17
        color: root.active ? Theme.textOnAccent : Theme.textPrimary
    }

    UHoverTip {
        anchor: root
        text: root.label
        show: mouse.containsMouse
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: if (root.enabled) root.clicked()
    }
}
