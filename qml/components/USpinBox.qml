import QtQuick
import Unisic

Rectangle {
    id: root
    property int value: 0
    property int from: 0
    property int to: 100
    property string suffix: ""
    signal changed(int value)

    implicitWidth: 130
    implicitHeight: 40
    radius: Theme.radiusM
    color: Theme.surfaceHi
    border.width: 1
    border.color: Theme.divider

    function _set(v) {
        v = Math.max(from, Math.min(to, v))
        // Emit only — assigning `value` would break the consumer's binding.
        if (v !== value) root.changed(v)
    }

    UIconButton {
        icon: "−"
        width: 32; height: 32
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root._set(root.value - 1)
    }
    Text {
        anchors.centerIn: parent
        text: root.value + root.suffix
        color: Theme.textPrimary
        font.pixelSize: Theme.fontM
        font.weight: Font.DemiBold
    }
    UIconButton {
        icon: "+"
        width: 32; height: 32
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root._set(root.value + 1)
    }
}
