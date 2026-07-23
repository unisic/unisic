import QtQuick
import Unisic

// One option drawn as its own bordered surface card — the same visual
// language as the Settings rows, shared by the main pages so every menu
// speaks it: label (+ optional sub line) on the left, the control on the
// right, an optional full-width footer below (sliders, chip editors).
Rectangle {
    id: row
    property alias label: labelText.text
    property alias sub: subText.text
    default property alias control: slot.data
    property alias footer: footerSlot.data

    // Slimmer than the Settings rows (8px vertical padding, 40px head): the
    // main pages must fit their whole content above the fold at the default
    // window size — Settings keeps its roomier internal SettingRow.
    width: parent ? parent.width : 0
    implicitHeight: inner.implicitHeight + 16
    radius: Theme.radiusM
    color: Theme.surface
    border.width: 1
    border.color: Theme.divider
    opacity: enabled ? 1.0 : 0.5

    Column {
        id: inner
        x: Theme.spacingM
        y: 8
        width: parent.width - 2 * Theme.spacingM
        spacing: footerSlot.childrenRect.height > 0 ? Theme.spacingS : 0

        Item {
            width: parent.width
            // Independent of slot.height on purpose: every control is ≤44px and
            // the label column governs the rest — reading slot.height here (it
            // hugs childrenRect and is vCenter-anchored to this head) would be
            // a size↔position binding loop. Same rule as the Settings rows.
            height: Math.max(40, labelCol.implicitHeight)

            Column {
                id: labelCol
                anchors.left: parent.left
                anchors.right: slot.left
                anchors.rightMargin: Theme.spacingM
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                Text {
                    id: labelText
                    width: parent.width
                    elide: Text.ElideRight
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontM
                }
                Text {
                    id: subText
                    visible: text !== ""
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontS
                }
            }
            Item {
                id: slot
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: childrenRect.width
                height: childrenRect.height
            }
        }
        Item {
            id: footerSlot
            width: parent.width
            height: childrenRect.height
        }
    }
}
