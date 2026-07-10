import QtQuick
import Unisic

Rectangle {
    id: root
    property string icon: ""
    property string iconName: ""
    property string tooltip: ""
    property bool active: false
    property int iconSize: 18
    // Press state for hold-to-repeat consumers (USpinBox).
    readonly property alias pressed: mouse.pressed
    signal clicked()

    width: 38; height: 38
    radius: Theme.radiusM
    opacity: root.enabled ? 1 : 0.35
    color: active ? Theme.accent
         : mouse.containsMouse ? Theme.alpha(Theme.accent, 0.16)
         : "transparent"
    Behavior on color { ColorAnimation { duration: Theme.animFast } }
    scale: mouse.pressed && root.enabled ? 0.92 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutBack } }

    UIcon {
        visible: root.iconName !== ""
        anchors.centerIn: parent
        name: root.iconName
        color: root.active ? Theme.textOnAccent : Theme.textPrimary
        size: root.iconSize
    }
    Text {
        visible: root.iconName === "" && root.icon !== ""
        anchors.centerIn: parent
        text: root.icon
        font.pixelSize: 17
        color: root.active ? Theme.textOnAccent : Theme.textPrimary
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: if (root.enabled) root.clicked()
    }
}
