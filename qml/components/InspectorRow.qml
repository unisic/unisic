import QtQuick
import Unisic.Kit

// One option in a page's settings column: the label (and an optional line
// under it) on the left, its control on the right, centred on the row. The
// control is the row's child; name it after `label` for a screen reader,
// since the label itself is ignored to keep it from being read twice. The
// same goes for `sub`: hand it to the control's accessible description.
Item {
    id: row

    property string label
    property string sub: ""
    // Two lines of `sub` at most, as the compact Capture tiles do, so a long
    // translation cannot push the column into scrolling at 1060x700. The cut
    // text shows in full on hover.
    property bool compact: false
    default property alias control: slot.data

    width: parent ? parent.width : 0
    height: Math.max(texts.height, slot.height)
    opacity: enabled ? 1.0 : 0.5

    Column {
        id: texts
        anchors.left: parent.left
        anchors.right: slot.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: row.label
            color: Theme.textPrimary
            font.pixelSize: 13
            font.weight: Font.Medium
            Accessible.ignored: true
        }
        Text {
            id: subText
            width: parent.width
            visible: row.sub !== ""
            wrapMode: Text.Wrap
            // undefined resets to Qt's own "no limit".
            maximumLineCount: row.compact ? 2 : undefined
            elide: row.compact ? Text.ElideRight : Text.ElideNone
            text: row.sub
            color: Theme.textSecondary
            font.pixelSize: Theme.fontS
            Accessible.ignored: true
        }
    }
    Item {
        id: slot
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: childrenRect.width
        height: childrenRect.height
    }

    // A handler, not a MouseArea: it watches the pointer without taking the
    // control's clicks.
    HoverHandler { id: textHover }
    UHoverTip {
        anchor: subText
        text: row.sub
        show: textHover.hovered && subText.truncated
    }
}
