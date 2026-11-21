import QtQuick
import QtQuick.Window
import Unisic
import Unisic.Kit

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
    // "rec" (replay running), "busy" (saving), "ok" (saved), "error"
    property string tone: "rec"
    // Settings::clipIndicatorStyle: "pill" (icon, title, detail), "compact"
    // (no detail), "dot" (the icon alone, in a small circle) or "tinted"
    // (the pill's edge and title take the tone's colour).
    property string look: "pill"
    readonly property color toneColor: tone === "rec" ? Theme.recDot
                                     : tone === "busy" ? Theme.accent
                                     : tone === "ok" ? Theme.success : Theme.danger
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

    Rectangle {
        id: pill
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: osd.look === "dot" ? height : Math.min(osd.width, row.implicitWidth + 2 * Theme.spacingM)
        height: row.implicitHeight + 2 * Theme.spacingS + 4
        radius: height / 2
        color: Theme.recBadgeBg
        // Edge for a dark game scene, where the dark pill alone disappears.
        border.width: osd.look === "tinted" ? 2 : 1
        border.color: osd.look === "tinted" ? osd.toneColor : Theme.alpha(Theme.recBadgeText, 0.18)
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        onOpacityChanged: if (opacity === 0 && !hideTimer.running) osd.done()
        Accessible.role: Accessible.AlertMessage
        Accessible.name: osd.detail ? osd.title + ". " + osd.detail : osd.title

        Row {
            id: row
            anchors.centerIn: parent
            spacing: Theme.spacingM
            Item {
                width: 18; height: 18
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    visible: osd.tone === "rec" || osd.tone === "busy"
                    anchors.centerIn: parent
                    width: 10; height: 10; radius: 5
                    color: osd.tone === "rec" ? Theme.recDot : Theme.accent
                    SequentialAnimation on opacity {
                        running: osd.tone === "busy" && pill.opacity > 0
                        loops: Animation.Infinite
                        alwaysRunToEnd: true
                        NumberAnimation { to: 0.25; duration: 450 }
                        NumberAnimation { to: 1; duration: 450 }
                    }
                }
                UIcon {
                    visible: osd.tone === "ok" || osd.tone === "error"
                    anchors.centerIn: parent
                    name: osd.tone === "ok" ? "checkmark" : "close"
                    color: osd.tone === "ok" ? Theme.success : Theme.danger
                    size: 18
                }
            }
            Column {
                visible: osd.look !== "dot"
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    text: osd.title
                    color: osd.look === "tinted" ? osd.toneColor : Theme.recBadgeText
                    font.pixelSize: Theme.fontM
                    font.weight: Font.DemiBold
                    Accessible.ignored: true
                }
                Text {
                    visible: osd.detail !== "" && osd.look !== "compact"
                    width: Math.min(implicitWidth, osd.width - 80)
                    text: osd.detail
                    elide: Text.ElideMiddle
                    color: Theme.alpha(Theme.recBadgeText, 0.75)
                    font.pixelSize: Theme.fontS
                    Accessible.ignored: true
                }
            }
        }
    }
}
