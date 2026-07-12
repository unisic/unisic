import QtQuick
import QtQuick.Window
import Unisic

// Persistent visual marker of the area being recorded (region GIF/video).
// Fills the screen (geometry set from C++) and is transparent + click-through,
// so it never blocks the app underneath. The frame is drawn strictly OUTSIDE
// the recorded rect — the ffmpeg crop is exactly [regionX..regionX+regionW),
// so nothing painted here lands inside the recording. C++ injects regionX/Y/W/H
// (window-local logical pixels, snapped OUTWARD so it can only ever sit outside
// the true region, never one sub-pixel inside it).
Window {
    id: borderWindow
    // WindowStaysOnTopHint has no xdg-shell equivalent on Wayland; on KWin the
    // frame stays visible because it is shown fullscreen (fullscreen surfaces
    // sit in a high stacking layer, same as the capture popup/overlay). On a
    // compositor that both ignores the hint AND lets a raised window cover an
    // inactive fullscreen surface, the frame may be occluded — acceptable
    // degradation; recording itself is unaffected.
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
           | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
    color: "transparent"
    visible: false   // C++ sizes then showFullScreen()s it

    // Accent frame thickness (drawn outside the region), with a 1px dark line on
    // each side so it reads over both light and dark content underneath.
    readonly property int bw: 3
    readonly property color contrast: Qt.rgba(0, 0, 0, 0.55)

    // Pre-recording countdown value, driven from C++ (0 = not counting). While
    // >0 the frame is up but recording has not begun: a big number ticks inside
    // the region and the REC badge is hidden.
    property int countdown: 0

    // Manual h:mm:ss — Qt.formatTime wraps at 60 minutes (matches Main.qml).
    function fmt(s) {
        var h = Math.floor(s / 3600);
        var m = Math.floor((s % 3600) / 60);
        var sec = s % 60;
        function p(v) { return (v < 10 ? "0" : "") + v; }
        return (h > 0 ? h + ":" + p(m) : p(m)) + ":" + p(sec);
    }

    // Outer contrast line.
    Rectangle {
        x: regionX - borderWindow.bw - 1
        y: regionY - borderWindow.bw - 1
        width: regionW + 2 * (borderWindow.bw + 1)
        height: regionH + 2 * (borderWindow.bw + 1)
        color: "transparent"
        border.width: 1
        border.color: borderWindow.contrast
    }
    // Accent frame — its inner edge lands exactly on the region boundary, so the
    // stroke occupies only pixels outside the crop.
    Rectangle {
        x: regionX - borderWindow.bw
        y: regionY - borderWindow.bw
        width: regionW + 2 * borderWindow.bw
        height: regionH + 2 * borderWindow.bw
        color: "transparent"
        border.width: borderWindow.bw
        border.color: Theme.accent
    }
    // Inner contrast line, hugging the region edge (still one pixel outside it).
    Rectangle {
        x: regionX - 1
        y: regionY - 1
        width: regionW + 2
        height: regionH + 2
        color: "transparent"
        border.width: 1
        border.color: borderWindow.contrast
    }

    // "REC m:ss" badge, placed just outside the region (above if there is room,
    // else below); hidden when the region leaves no room on either side so the
    // badge itself never overlaps — and so never enters — the recording.
    // Pre-recording countdown, centered inside the region. The frame shows
    // immediately and the number ticks down (set from C++) before recording.
    Item {
        x: regionX; y: regionY
        width: regionW; height: regionH
        visible: borderWindow.countdown > 0
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height) * 0.42
            height: width
            radius: width / 2
            color: Qt.rgba(0, 0, 0, 0.55)
            border.width: 2
            border.color: Theme.accent
        }
        Text {
            id: cdText
            anchors.centerIn: parent
            text: borderWindow.countdown
            color: Theme.accent
            font.pixelSize: Math.max(24, Math.min(parent.width, parent.height) * 0.26)
            font.bold: true
            onTextChanged: { scale = 1.4; cdPulse.restart() }
            NumberAnimation {
                id: cdPulse; target: cdText; property: "scale"
                from: 1.4; to: 1.0; duration: 320; easing.type: Easing.OutCubic
            }
        }
    }

    Rectangle {
        id: badge
        readonly property bool roomAbove: regionY - height - 6 >= 0
        readonly property bool roomBelow: regionY + regionH + 6 + height <= borderWindow.height
        visible: (roomAbove || roomBelow) && borderWindow.countdown <= 0
        width: badgeRow.width + 16
        height: 24
        radius: 12
        color: Qt.rgba(0, 0, 0, 0.72)
        x: Math.max(0, Math.min(borderWindow.width - width, regionX))
        y: roomAbove ? regionY - height - 6 : regionY + regionH + 6

        Row {
            id: badgeRow
            anchors.centerIn: parent
            spacing: 6
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 8; radius: 4
                color: "#ff4d4d"
                SequentialAnimation on opacity {
                    loops: Animation.Infinite; running: badge.visible
                    NumberAnimation { from: 1.0; to: 0.25; duration: 700; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 0.25; to: 1.0; duration: 700; easing.type: Easing.InOutSine }
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "REC  " + borderWindow.fmt(App.recordSeconds)
                color: "white"
                font.pixelSize: 12
                font.bold: true
            }
        }
    }
}
