import QtQuick
import Unisic.Kit

// One key cap of a shortcut, as the Capture page and the header search show
// it. Sunk into the window colour; a theme whose window colour equals its card
// colour (the built-in light one) would lose it, so that one takes the
// background instead.
Rectangle {
    property alias text: label.text
    width: Math.max(22, label.implicitWidth + 12)
    height: 22
    radius: 5
    color: Qt.colorEqual(Theme.primary, Theme.surface) ? Theme.background : Theme.primary
    border.width: 1
    border.color: Theme.alpha(Theme.accent, 0.18)
    Accessible.ignored: true
    Text {
        id: label
        anchors.centerIn: parent
        color: Theme.alpha(Theme.textPrimary, 0.85)
        font.pixelSize: 11
        font.weight: Font.Medium
        font.family: "monospace"
    }
}
