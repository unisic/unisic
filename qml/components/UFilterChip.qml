import QtQuick
import Unisic

// Pill toggle used by the History page's filter bar. Radio-style (one of a set
// writes the same property) or standalone toggle — the caller owns the state,
// the chip only reports clicks.
Rectangle {
    id: chip

    property string text: ""
    property string iconName: ""
    property bool checked: false
    signal clicked()

    height: 28
    width: chipRow.implicitWidth + 24
    radius: height / 2
    color: checked ? Theme.accent : (chipHover.hovered ? Theme.surfaceHi : Theme.surface)
    border.width: 1
    border.color: checked ? Theme.accent : Theme.divider
    Behavior on color { ColorAnimation { duration: Theme.animFast } }
    Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

    HoverHandler { id: chipHover }

    Row {
        id: chipRow
        anchors.centerIn: parent
        spacing: 5
        UIcon {
            visible: chip.iconName !== ""
            anchors.verticalCenter: parent.verticalCenter
            name: chip.iconName
            size: 13
            color: chip.checked ? Theme.textOnAccent : Theme.textSecondary
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: chip.text
            color: chip.checked ? Theme.textOnAccent : Theme.textSecondary
            font.pixelSize: Theme.fontS
            font.weight: Font.DemiBold
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: chip.clicked()
    }
}
