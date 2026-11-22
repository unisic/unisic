import QtQuick
import QtQuick.Window
import Unisic
import Unisic.Kit
import "components"

// The steady sign that instant replay is recording: a small red dot in a
// corner of the screen for as long as the ring runs (Settings::clipStatusDot).
// Static on purpose: it sits there for hours, so it costs no frame after the
// first. C++ (ClipOsd::setStatusDot) owns the surface and its corner.
Window {
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
           | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
    color: "transparent"
    visible: false
    width: 22
    height: 22

    ClipStatusDotMark {
        anchors.centerIn: parent
        Accessible.role: Accessible.Indicator
        Accessible.name: qsTr("Instant replay is recording")
    }
}
