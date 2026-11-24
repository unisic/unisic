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
    // Opt-in: the control's natural width. The row then lays out side by side
    // or stacked (label above, control at full width): stacked once the
    // label's longest word cannot fit beside the control, so a long compound
    // ("Standardbildausschnitt") is never broken mid-word and the control's
    // text is never cut. Bind the control's width to slotWidth.
    property real controlWidth: 0
    readonly property bool stacked: controlWidth > 0 && controlWidth + 12 + Math.max(80, _longestWord) > width
    readonly property real slotWidth: stacked ? width : controlWidth
    readonly property real _longestWord: {
        var words = label.split(/\s+/), m = 0
        for (var i = 0; i < words.length; ++i)
            m = Math.max(m, labelMetrics.advanceWidth(words[i]))
        return Math.ceil(m)
    }
    FontMetrics { id: labelMetrics; font.pixelSize: 13; font.weight: Font.Medium }

    width: parent ? parent.width : 0
    height: stacked ? texts.height + 8 + slot.height : Math.max(texts.height, slot.height)
    opacity: enabled ? 1.0 : 0.5

    Column {
        id: texts
        width: row.stacked ? row.width : row.width - slot.width - 12
        y: row.stacked ? 0 : (row.height - height) / 2
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
        x: row.stacked ? 0 : row.width - width
        y: row.stacked ? texts.height + 8 : (row.height - height) / 2
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
