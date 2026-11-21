import QtQuick
import QtQuick.Window
import Unisic
import Unisic.Kit
import "components"

// The clip indicator: a small pill that says instant replay started or
// stopped, a clip is being saved, or where it went. Click-through and never
// focused, so it can sit over a fullscreen game. C++ (ClipOsd) owns where it
// lives: a layer-shell overlay, a topmost window on Windows, or the
// --osd-helper override-redirect window on X11 / GNOME. Each flash() restarts
// the hide timer; `done` fires once it has faded out.
Window {
    id: osd
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
           | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
    color: "transparent"
    visible: false
    width: 420
    height: 76

    property string title
    property string detail
    property string tone: "rec"
    property string look: "pill"
    signal done()

    function flash(t, d, k, ms, l) {
        title = t
        detail = d
        tone = k
        look = l || "pill"
        hideTimer.interval = Math.max(800, ms)
        hideTimer.restart()
        pill.opacity = 1
    }

    Timer {
        id: hideTimer
        onTriggered: pill.opacity = 0
    }

    ClipOsdPill {
        id: pill
        anchors.centerIn: parent
        title: osd.title
        detail: osd.detail
        tone: osd.tone
        look: osd.look
        maxWidth: osd.width
        animate: opacity > 0
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        onOpacityChanged: if (opacity === 0 && !hideTimer.running) osd.done()
    }
}
