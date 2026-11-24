import QtQuick
import Unisic.Kit

// A command in a page header, in the look of the Capture page's Open image
// and Paste: card colour, a hairline that warms on hover, a line icon and a
// label. Hover changes colour only.
Rectangle {
    id: btn

    property string text
    property string iconName
    property string accessibleDescription: ""
    // A shortcut shown as key caps after the label, as Capture's Paste shows
    // Ctrl V. Empty draws none.
    property string kbd: ""
    signal activated()

    function activate() { if (btn.enabled) btn.activated() }

    width: row.width + (kbd !== "" ? 14 + 8 : 28)
    height: 36
    radius: 8
    opacity: enabled ? 1 : 0.45
    color: mouse.containsMouse && enabled ? Theme.surfaceTop : Theme.surface
    border.width: 1
    border.color: Theme.alpha(Theme.accent, mouse.containsMouse && enabled ? 0.32 : 0.14)
    Behavior on color { ColorAnimation { duration: Theme.animFast } }

    Row {
        id: row
        x: 14
        anchors.verticalCenter: parent.verticalCenter
        spacing: btn.kbd !== "" ? 10 : 8
        UIcon {
            anchors.verticalCenter: parent.verticalCenter
            visible: btn.iconName !== ""
            name: btn.iconName
            size: 16
            color: Theme.textPrimary
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: btn.text
            color: Theme.textPrimary
            font.pixelSize: 13
            font.weight: Font.Medium
            Accessible.ignored: true
        }
        Kbd {
            anchors.verticalCenter: parent.verticalCenter
            visible: btn.kbd !== ""
            text: btn.kbd
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: btn.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: btn.activate()
    }
    activeFocusOnTab: enabled
    Keys.onSpacePressed: (e) => UKeys.activate(e, btn.activate)
    Keys.onReturnPressed: (e) => UKeys.activate(e, btn.activate)
    Keys.onEnterPressed: (e) => UKeys.activate(e, btn.activate)
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.description: accessibleDescription
    Accessible.focusable: activeFocusOnTab
    Accessible.onPressAction: btn.activate()
    UFocusRing { inset: -4 }
}
