import QtQuick
import QtQuick.Controls as C
import Unisic

// A UButton-shaped action menu: the trigger is a tonal pill; its list is a
// Popup parented to the window Overlay (never clipped by the bar), preferring
// to open ABOVE the trigger since the action bar sits on the window's bottom
// edge. Unlike UComboBox it tracks no selection — each row fires its own
// `trigger` callback. `actions` items: { label, iconName, enabled, hint,
// separatorBefore, trigger:function }.
Rectangle {
    id: root

    property string text: ""
    property string iconName: ""
    property var actions: []

    readonly property bool _hovered: mouse.containsMouse && !mouse.pressed
    implicitWidth: rowC.implicitWidth + 34
    implicitHeight: 42
    radius: height / 2
    color: (popup.opened || _hovered) ? Qt.lighter(Theme.tertiary, 1.12) : Theme.tertiary
    border.width: 1
    border.color: popup.opened ? Theme.accent : Theme.divider
    Behavior on color { ColorAnimation { duration: Theme.animFast } }
    scale: mouse.pressed ? 0.96 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutBack } }

    Row {
        id: rowC
        anchors.centerIn: parent
        spacing: 7
        UIcon {
            visible: root.iconName !== ""
            name: root.iconName; size: 18; color: Theme.textPrimary
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: root.text !== ""
            text: root.text; font.pixelSize: Theme.fontM; font.weight: Font.DemiBold
            color: Theme.textPrimary
            anchors.verticalCenter: parent.verticalCenter
        }
        UIcon {
            name: "chevron-down"; size: 14; color: Theme.textSecondary
            rotation: popup.opened ? 180 : 0
            anchors.verticalCenter: parent.verticalCenter
            Behavior on rotation { NumberAnimation { duration: Theme.animFast } }
        }
    }

    // Click-away catcher (same idiom as UComboBox).
    Item {
        id: catcher
        parent: C.Overlay.overlay
        width: parent ? parent.width : 0
        height: parent ? parent.height : 0
        visible: popup.opened && parent !== null
        z: 999
        MouseArea { anchors.fill: parent; onClicked: popup.close(); onWheel: (w) => { popup.close(); w.accepted = false } }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: popup.opened ? popup.close() : popup.open()
    }

    C.Popup {
        id: popup
        parent: C.Overlay.overlay
        width: Math.max(root.width, 220)
        height: col.implicitHeight + 12
        z: catcher.z + 1
        padding: 6
        focus: true
        closePolicy: C.Popup.CloseOnEscape

        // The bar is at the window's bottom edge → prefer opening above; flip
        // below only if there is no room above. x is clamped to the overlay.
        onAboutToShow: {
            var o = C.Overlay.overlay
            var p = root.mapToItem(o, 0, root.height + 6)
            x = Math.max(0, Math.min(p.x, o.width - width))
            var above = root.mapToItem(o, 0, 0).y - height - 6
            y = above >= 0 ? above : p.y
        }

        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animFast } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animFast } }

        background: Rectangle {
            radius: Theme.radiusM
            color: Theme.surfaceHi
            border.width: 1
            border.color: Theme.divider
        }

        contentItem: Column {
            id: col
            width: popup.availableWidth
            spacing: 2
            Repeater {
                model: root.actions
                delegate: Column {
                    width: col.width
                    spacing: 2
                    Rectangle {
                        visible: modelData.separatorBefore === true
                        x: 6; width: parent.width - 12; height: 1; color: Theme.divider
                    }
                    Rectangle {
                        width: parent.width; height: 36; radius: Theme.radiusS
                        readonly property bool _on: modelData.enabled !== false
                        opacity: _on ? 1 : 0.4
                        color: rMouse.containsMouse && _on ? Theme.tertiary : "transparent"
                        Row {
                            anchors.left: parent.left; anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter; spacing: 10
                            UIcon {
                                visible: modelData.iconName !== undefined && modelData.iconName !== ""
                                name: modelData.iconName || ""; size: 17; color: Theme.textPrimary
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: modelData.label; color: Theme.textPrimary
                                font.pixelSize: Theme.fontM
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Text {
                            visible: !!modelData.hint
                            anchors.right: parent.right; anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.hint || ""; color: Theme.textTertiary
                            font.pixelSize: Theme.fontS
                        }
                        MouseArea {
                            id: rMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: parent._on ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: if (parent._on) { popup.close(); modelData.trigger() }
                        }
                    }
                }
            }
        }
    }
}
