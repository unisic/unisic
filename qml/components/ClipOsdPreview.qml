import QtQuick
import Unisic
import Unisic.Kit

// Settings > Clips: the clip indicator in the chosen style, in its three
// usual states, on a mock screen, with the status dot in its corner. The pills are the real ClipOsdPill and the
// texts the ones AppContext shows, so what is seen here is what a game gets.
Item {
    id: root
    property string look: "pill"
    // The status dot's corner, "" while it is off.
    property string dotCorner: ""
    implicitHeight: Math.max(132, pills.implicitHeight + 2 * Theme.spacingL)
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

    ClipStatusDotMark {
        visible: root.dotCorner !== ""
        x: root.dotCorner.endsWith("left") ? 10 : root.width - width - 10
        y: root.dotCorner.startsWith("top") ? 10 : root.height - height - 10
        Accessible.ignored: true
    }

    // One row when the three titles fit whole (the file name gives way,
    // elided as in the real indicator), else stacked: shrinking each pill to
    // a third cut the longer titles (most languages), which a game never sees.
    Grid {
        id: pills
        visible: root.look !== ""
        anchors.centerIn: parent
        spacing: Theme.spacingM
        horizontalItemAlignment: Grid.AlignHCenter
        verticalItemAlignment: Grid.AlignVCenter
        readonly property real each: root.width - 2 * Theme.spacingM
        columns: pill1.width + pill2.width + pill3.titleFitWidth + 2 * spacing <= each ? 3 : 1
        ClipOsdPill {
            id: pill1
            look: root.look
            Accessible.ignored: true
            maxWidth: Math.min(420, pills.each)
            tone: "rec"
            title: qsTranslate("AppContext", "Instant replay on")
            detail: qsTranslate("AppContext", "Keeping the last %1").arg(qsTranslate("AppContext", "%1 min").arg(5))
        }
        ClipOsdPill {
            id: pill2
            look: root.look
            Accessible.ignored: true
            maxWidth: Math.min(420, pills.each)
            tone: "busy"
            title: qsTranslate("AppContext", "Saving clip…")
            animate: root.visible
        }
        ClipOsdPill {
            id: pill3
            look: root.look
            Accessible.ignored: true
            maxWidth: Math.min(420, pills.columns === 3 ? pills.each - pill1.width - pill2.width - 2 * pills.spacing
                                                        : pills.each)
            tone: "ok"
            title: qsTranslate("AppContext", "Clip saved")
            detail: "Unisic_Replay_2026-05-14_21-08-33_Hades II.mp4"
        }
    }
}
