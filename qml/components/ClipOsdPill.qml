import QtQuick
import Unisic
import Unisic.Kit

// The clip indicator's pill, shared by the real indicator (ClipOsd.qml) and
// its preview in Settings > Clips, so the preview can never drift from it.
Rectangle {
    id: pill

    property string title
    property string detail
    // "rec" (replay running), "busy" (saving), "ok" (saved), "error"
    property string tone: "rec"
    // Settings::clipIndicatorStyle: "pill" (icon, title, detail), "compact"
    // (no detail), "dot" (the icon alone, in a small circle) or "tinted"
    // (the pill's edge and title take the tone's colour).
    property string look: "pill"
    property real maxWidth: 420
    // Off while hidden, so the saving dot does not animate an invisible pill.
    property bool animate: true
    // What the texts may take: maxWidth less the padding, the icon and the gap.
    readonly property real textMax: maxWidth - 3 * Theme.spacingM - 18
    // The narrowest pill that still shows the whole title (the detail elides).
    readonly property real titleFitWidth: look === "dot" ? height : titleText.implicitWidth + 3 * Theme.spacingM + 18
    readonly property color toneColor: tone === "rec" ? Theme.recDot
                                     : tone === "busy" ? Theme.accent
                                     : tone === "ok" ? Theme.success : Theme.danger

    width: look === "dot" ? height : Math.min(maxWidth, row.implicitWidth + 2 * Theme.spacingM)
    height: row.implicitHeight + 2 * Theme.spacingS + 4
    radius: height / 2
    color: Theme.recBadgeBg
    // Edge for a dark game scene, where the dark pill alone disappears.
    border.width: look === "tinted" ? 2 : 1
    border.color: look === "tinted" ? toneColor : Theme.alpha(Theme.recBadgeText, 0.18)
    Accessible.role: Accessible.AlertMessage
    Accessible.name: detail ? title + ". " + detail : title

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacingM
        Item {
            width: 18; height: 18
            anchors.verticalCenter: parent.verticalCenter
            Rectangle {
                visible: pill.tone === "rec" || pill.tone === "busy"
                anchors.centerIn: parent
                width: 10; height: 10; radius: 5
                color: pill.tone === "rec" ? Theme.recDot : Theme.accent
                SequentialAnimation on opacity {
                    running: pill.tone === "busy" && pill.animate
                    loops: Animation.Infinite
                    alwaysRunToEnd: true
                    NumberAnimation { to: 0.25; duration: 450 }
                    NumberAnimation { to: 1; duration: 450 }
                }
            }
            UIcon {
                visible: pill.tone === "ok" || pill.tone === "error"
                anchors.centerIn: parent
                name: pill.tone === "ok" ? "checkmark" : "close"
                color: pill.tone === "ok" ? Theme.success : Theme.danger
                size: 18
            }
        }
        Column {
            visible: pill.look !== "dot"
            anchors.verticalCenter: parent.verticalCenter
            Text {
                id: titleText
                width: Math.min(implicitWidth, pill.textMax)
                text: pill.title
                elide: Text.ElideRight
                color: pill.look === "tinted" ? pill.toneColor : Theme.recBadgeText
                font.pixelSize: Theme.fontM
                font.weight: Font.DemiBold
                Accessible.ignored: true
            }
            Text {
                visible: pill.detail !== "" && pill.look !== "compact"
                width: Math.min(implicitWidth, pill.textMax)
                text: pill.detail
                elide: Text.ElideMiddle
                color: Theme.alpha(Theme.recBadgeText, 0.75)
                font.pixelSize: Theme.fontS
                Accessible.ignored: true
            }
        }
    }
}
