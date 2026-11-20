import QtQuick
import Unisic.Kit
import "Hotkeys.js" as Hotkeys

// The small one-line tile under the source tiles: "More ways to capture" on
// the Capture page, the recording controls on the Record page. Icon in a
// square, title over a subtitle, the hotkey at the end. Colour-only hover;
// a control that does not apply right now disables in place.
Rectangle {
    id: tile

    property string iconName
    property color iconColor: Theme.accent
    property string title
    property string sub
    property string hotkey
    property bool compact: false
    property string accessibleDescription: ""
    signal activated()

    function activate() { if (tile.enabled) tile.activated() }

    height: 56
    radius: 10
    opacity: enabled ? 1.0 : 0.5
    color: moreMouse.containsMouse ? Theme.surfaceTop : Theme.surface
    border.width: 1
    border.color: Theme.alpha(Theme.accent, moreMouse.containsMouse ? 0.32 : 0.10)
    Behavior on color { ColorAnimation { duration: Theme.animFast } }
    Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

    Rectangle {
        id: moreIcon
        x: tile.compact ? 8 : 10
        anchors.verticalCenter: parent.verticalCenter
        width: tile.compact ? 28 : 34; height: width
        radius: 8
        color: Theme.secondary
        UIcon {
            anchors.centerIn: parent
            name: tile.iconName
            size: 18
            color: tile.iconColor
        }
    }
    Column {
        anchors.left: moreIcon.right
        anchors.leftMargin: tile.compact ? 8 : 12
        anchors.right: moreKey.visible ? moreKey.left : parent.right
        anchors.rightMargin: moreKey.visible ? 12 : 10
        anchors.verticalCenter: parent.verticalCenter
        Text {
            id: moreTitle
            width: parent.width
            elide: Text.ElideRight
            text: tile.title
            color: Theme.textPrimary
            font.pixelSize: 13
            font.weight: Font.DemiBold
            Accessible.ignored: true
        }
        Text {
            width: parent.width
            elide: Text.ElideRight
            text: tile.sub
            color: Theme.textSecondary
            font.pixelSize: Theme.fontS
            Accessible.ignored: true
        }
    }
    Kbd {
        id: moreKey
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        // The key gives way before the title would be cut; the shortcut stays
        // in the accessible name.
        visible: tile.hotkey !== ""
                 && tile.width - moreIcon.x - moreIcon.width - (tile.compact ? 8 : 12) - 32 - width
                    >= moreTitle.implicitWidth
        text: Hotkeys.keysOf(tile.hotkey).join(" ")
    }

    MouseArea {
        id: moreMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: tile.activate()
    }

    activeFocusOnTab: enabled
    Keys.onSpacePressed: (e) => UKeys.activate(e, tile.activate)
    Keys.onReturnPressed: (e) => UKeys.activate(e, tile.activate)
    Keys.onEnterPressed: (e) => UKeys.activate(e, tile.activate)

    Accessible.role: Accessible.Button
    // Context kept as CapturePage: the strings moved out of that file together
    // with their translations.
    Accessible.name: hotkey !== ""
                     ? qsTranslate("CapturePage", "%1, %2, shortcut %3").arg(title).arg(sub).arg(Hotkeys.first(hotkey))
                     : qsTranslate("CapturePage", "%1, %2").arg(title).arg(sub)
    Accessible.description: accessibleDescription
    Accessible.focusable: tile.activeFocusOnTab
    Accessible.onPressAction: tile.activate()
    UFocusRing { inset: -4 }
}
