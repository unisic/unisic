import QtQuick
import Unisic.Kit

// An on/off filter beside a SegmentedControl, in its language: sunk into the
// window colour when off, the segment's raised fill when on. A pill of its
// own rather than a segment, because a row of segments reads as "pick one"
// and these toggle independently. The weight stays the same in both states:
// a bolder label would widen the pill under the pointer and reflow the row.
Rectangle {
    id: chip

    property string text
    property string iconName
    property bool checked: false
    property string accessibleDescription: ""
    signal toggled()

    function toggle() { if (chip.enabled) chip.toggled() }

    readonly property color inset: Qt.colorEqual(Theme.primary, Theme.surface) ? Theme.background : Theme.primary

    width: row.width + 24
    height: 36
    radius: 9
    color: checked ? Theme.secondary : mouse.containsMouse ? Theme.surfaceTop : inset
    border.width: 1
    border.color: Theme.alpha(Theme.accent, checked ? 0.45 : 0.10)
    Behavior on color { ColorAnimation { duration: Theme.animFast } }
    Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        UIcon {
            anchors.verticalCenter: parent.verticalCenter
            visible: chip.iconName !== ""
            name: chip.iconName
            size: 14
            color: chip.checked ? Theme.accent : Theme.textSecondary
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: chip.text
            color: chip.checked ? Theme.textPrimary : Theme.textSecondary
            font.pixelSize: Theme.fontS
            font.weight: Font.Medium
            Accessible.ignored: true
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: chip.toggle()
    }
    activeFocusOnTab: enabled
    Keys.onSpacePressed: (e) => UKeys.activate(e, chip.toggle)
    Keys.onReturnPressed: (e) => UKeys.activate(e, chip.toggle)
    Keys.onEnterPressed: (e) => UKeys.activate(e, chip.toggle)
    Accessible.role: Accessible.CheckBox
    Accessible.name: text
    Accessible.description: accessibleDescription
    Accessible.focusable: activeFocusOnTab
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.onToggleAction: chip.toggle()
    Accessible.onPressAction: chip.toggle()
    UFocusRing { inset: -3 }
}
