import QtQuick
import Unisic.Kit

// The settings column on the right of the Capture, Record and Edit pages: 300 px,
// the card colour, a hairline on its left edge, scrolling on its own when a
// short window cannot hold it. It holds settings, not content, and is the
// sanctioned exception to "one full-width grid per page" on those three
// pages only (AGENTS.md §8).
// The page anchors it to its right edge and fills `content` with sections.
Rectangle {
    id: root

    property alias flickable: flick
    property alias flickName: flick.objectName
    property string accessibleName: ""
    default property alias content: col.data

    width: 300
    color: Theme.surface

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: Theme.alpha(Theme.accent, 0.10)
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.leftMargin: 1
        contentHeight: col.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Accessible.role: Accessible.Pane
        Accessible.name: root.accessibleName

        MiddleScroll { flickable: flick }
        WheelBoost { flickable: flick }

        Column {
            id: col
            width: flick.width
        }
    }
}
