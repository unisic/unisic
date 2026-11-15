import QtQuick
import Unisic.Kit

// The settings column's compact switch (36x20). USwitch is the 50x30 one the
// Settings rows use; that column is narrower and lists more of them.
Rectangle {
    id: sw
    property bool checked: false
    property string accessibleName: ""
    property string accessibleDescription: ""
    signal toggled(bool checked)
    function toggle() { if (sw.enabled) sw.toggled(!sw.checked) }
    width: 36
    height: 20
    radius: 10
    color: checked ? Theme.accent : Theme.secondary
    border.width: checked ? 0 : 1
    border.color: Theme.alpha(Theme.accent, 0.24)
    Behavior on color { ColorAnimation { duration: Theme.animFast } }
    Rectangle {
        width: sw.checked ? 16 : 14
        height: width
        radius: width / 2
        x: sw.checked ? 18 : 3
        y: (sw.height - height) / 2
        color: sw.checked ? Theme.primary : Theme.textSecondary
        Behavior on x { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
    }
    MouseArea {
        anchors.fill: parent
        anchors.margins: -6
        cursorShape: Qt.PointingHandCursor
        onClicked: sw.toggle()
    }
    activeFocusOnTab: enabled
    Keys.onSpacePressed: (e) => UKeys.activate(e, sw.toggle)
    Keys.onReturnPressed: (e) => UKeys.activate(e, sw.toggle)
    Keys.onEnterPressed: (e) => UKeys.activate(e, sw.toggle)
    // CheckBox, as in USwitch: Accessible.Switch needs Qt 6.11.
    Accessible.role: Accessible.CheckBox
    Accessible.name: accessibleName
    Accessible.description: accessibleDescription
    Accessible.focusable: activeFocusOnTab
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.onToggleAction: sw.toggle()
    Accessible.onPressAction: sw.toggle()
    UFocusRing { inset: -3 }
}
