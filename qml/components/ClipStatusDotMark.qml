import QtQuick
import Unisic.Kit

// The status dot itself, shared by ClipStatusDot.qml and the Settings preview.
// A dark ring around the red so it reads on a bright scene and a dark one.
Rectangle {
    width: 16; height: 16; radius: 8
    color: Theme.recBadgeBg
    border.width: 1
    border.color: Theme.alpha(Theme.recBadgeText, 0.18)
    Rectangle {
        anchors.centerIn: parent
        width: 8; height: 8; radius: 4
        color: Theme.recDot
    }
}
