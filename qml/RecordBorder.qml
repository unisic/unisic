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
    // NOT WindowTransparentForInput: the badge carries clickable stop/pause
    // controls, so pointer input is instead masked (recordBorderCtl.setInputRect)
    // down to just the badge rect — everything else stays click-through.
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
           | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false   // C++ sizes then showFullScreen()s it

    // Clip the window's input region to the badge (or nothing while the countdown
    // hides it), so the controls are clickable but the frame never blocks the app
    // being recorded. Re-run whenever the badge geometry or visibility changes.
    function updateMask() {
        if (typeof recordBorderCtl === "undefined" || !recordBorderCtl)
            return
        // countdownOnly: the window is WindowTransparentForInput (set in C++), so
        // it must stay fully click-through — never set a non-empty input region.
        if (borderWindow.countdownOnly) {
            recordBorderCtl.setInputRect(0, 0, 0, 0)
            return
        }
        if (badge.visible)
            recordBorderCtl.setInputRect(badge.x, badge.y, badge.width, badge.height)
        else
            recordBorderCtl.setInputRect(0, 0, 0, 0)
    }
    onWidthChanged: updateMask()
    onHeightChanged: updateMask()
    onCountdownChanged: updateMask()

    // Compact clickable control for the badge (small dot-sized icon button).
    component BadgeButton: Rectangle {
        id: bb
        property string iconName
        signal clicked()
        width: 22; height: 22; radius: 11
        color: bbMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.18) : "transparent"
        Behavior on color { ColorAnimation { duration: 90 } }
        UIcon {
            anchors.centerIn: parent
            name: bb.iconName
            color: "white"
            size: 14
        }
        MouseArea {
            id: bbMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: bb.clicked()
        }
    }

    // Accent frame thickness (drawn outside the region), with a 1px dark line on
    // each side so it reads over both light and dark content underneath.
    readonly property int bw: 3
    readonly property color contrast: Qt.rgba(0, 0, 0, 0.55)

    // Pre-recording countdown value, driven from C++ (0 = not counting). While
    // >0 the frame is up but recording has not begun: a big number ticks inside
    // the region and the REC badge is hidden.
    property int countdown: 0

    // Full-screen / window recordings have no region to frame: show ONLY the
    // countdown number, centered on the whole surface, with no border and no
    // REC badge. C++ tears this window down the moment recording begins, so a
    // persistent frame is neither drawn nor needed.
    property bool countdownOnly: false

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
        visible: !borderWindow.countdownOnly
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
        visible: !borderWindow.countdownOnly
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
        visible: !borderWindow.countdownOnly
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
        // Region frame: centered in the region. countdownOnly: centered on the
        // whole surface, since there is no region to sit inside.
        x: borderWindow.countdownOnly ? 0 : regionX
        y: borderWindow.countdownOnly ? 0 : regionY
        width: borderWindow.countdownOnly ? borderWindow.width : regionW
        height: borderWindow.countdownOnly ? borderWindow.height : regionH
        visible: borderWindow.countdown > 0
        // Diameter tracks the region for a framed recording, but is capped for a
        // full-screen countdown (0.42 × a 1440px screen would be a 600px disc).
        readonly property int disc: borderWindow.countdownOnly
            ? Math.min(220, Math.min(parent.width, parent.height) * 0.42)
            : Math.min(parent.width, parent.height) * 0.42
        Rectangle {
            anchors.centerIn: parent
            width: parent.disc
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
            font.pixelSize: Math.max(24, parent.disc * 0.62)
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
        // Never in countdownOnly mode: that overlay is torn down at commit, so a
        // REC badge would only ever flash for one frame at the 3→0 transition.
        visible: (roomAbove || roomBelow) && borderWindow.countdown <= 0
                 && !borderWindow.countdownOnly
        width: badgeRow.width + 14
        height: 28
        radius: 14
        color: Qt.rgba(0, 0, 0, 0.78)
        x: Math.max(0, Math.min(borderWindow.width - width, regionX))
        y: roomAbove ? regionY - height - 6 : regionY + regionH + 6

        onXChanged: borderWindow.updateMask()
        onYChanged: borderWindow.updateMask()
        onWidthChanged: borderWindow.updateMask()
        onHeightChanged: borderWindow.updateMask()
        onVisibleChanged: borderWindow.updateMask()
        Component.onCompleted: borderWindow.updateMask()

        Row {
            id: badgeRow
            anchors.centerIn: parent
            spacing: 6
            Rectangle {
                id: badgeDot
                anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 8; radius: 4
                color: "#ff4d4d"
                opacity: 1
                SequentialAnimation on opacity {
                    loops: Animation.Infinite; running: badge.visible && !App.recordingPaused
                    onStopped: badgeDot.opacity = 1
                    NumberAnimation { from: 1.0; to: 0.25; duration: 700; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 0.25; to: 1.0; duration: 700; easing.type: Easing.InOutSine }
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: (App.recordingPaused ? qsTr("PAUSED") : qsTr("REC")) + "  " + borderWindow.fmt(App.recordSeconds)
                color: "white"
                font.pixelSize: 12
                font.bold: true
            }
            // Thin divider before the controls.
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 1; height: 16
                color: Qt.rgba(1, 1, 1, 0.22)
            }
            BadgeButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: App.recordingPaused ? "play" : "pause"
                onClicked: App.togglePauseRecording()
            }
            BadgeButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "stop"
                onClicked: App.stopRecording()
            }
        }
    }
}
