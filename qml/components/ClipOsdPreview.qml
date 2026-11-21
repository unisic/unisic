import QtQuick
import Unisic
import Unisic.Kit

// Settings > Clips: the clip indicator in the chosen style, in its three
// usual states, on a mock screen. The pills are the real ClipOsdPill and the
// texts the ones AppContext shows, so what is seen here is what a game gets.
Item {
    id: root
    property string look: "pill"
    implicitHeight: 132
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Indicator preview")

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusM
        color: Theme.backgroundDeep
        border.width: 1
        border.color: Theme.divider
        // A game-like scene: the pill must hold up on a dark, busy picture.
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Theme.radiusM
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(Theme.tertiary.r, Theme.tertiary.g, Theme.tertiary.b, 0.30) }
                GradientStop { position: 1.0; color: Qt.rgba(Theme.secondary.r, Theme.secondary.g, Theme.secondary.b, 0.16) }
            }
        }
    }

    Row {
        id: pills
        anchors.centerIn: parent
        spacing: Theme.spacingM
        readonly property real each: (root.width - 2 * Theme.spacingM - 2 * spacing) / 3
        ClipOsdPill {
            anchors.verticalCenter: parent.verticalCenter
            look: root.look
            Accessible.ignored: true
            maxWidth: pills.each
            tone: "rec"
            title: qsTranslate("AppContext", "Instant replay on")
            detail: qsTranslate("AppContext", "Keeping the last %1").arg(qsTranslate("AppContext", "%1 min").arg(5))
        }
        ClipOsdPill {
            anchors.verticalCenter: parent.verticalCenter
            look: root.look
            Accessible.ignored: true
            maxWidth: pills.each
            tone: "busy"
            title: qsTranslate("AppContext", "Saving clip…")
            animate: root.visible
        }
        ClipOsdPill {
            anchors.verticalCenter: parent.verticalCenter
            look: root.look
            Accessible.ignored: true
            maxWidth: pills.each
            tone: "ok"
            title: qsTranslate("AppContext", "Clip saved")
            detail: "Unisic_Replay_2026-05-14_21-08-33_Hades II.mp4"
        }
    }
}
