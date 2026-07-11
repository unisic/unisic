import QtQuick
import Unisic

Rectangle {
    id: root
    property color dotColor: "#FF4757"
    property bool active: false
    signal clicked()

    width: 26; height: 26
    radius: 13
    color: dotColor
    border.width: active ? 3 : 1
    border.color: active ? Theme.accent : Theme.alpha(Theme.textPrimary, 0.25)
    scale: mouse.containsMouse || active ? 1.12 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutBack } }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
