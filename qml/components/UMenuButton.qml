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
    property string tooltip: ""
    // Keeps menus usable in dense action rows without creating a separate,
    // subtly different icon-menu control.
    property bool iconOnly: false
    property var actions: []

    // The popup used to be pinned at 220px with the label growing right and the
    // hint growing left, so long (esp. localized) strings collided and clipped.
    // Measure the widest label+hint row and size the popup to fit (clamped to
    // the overlay); the row layout below also elides the label as a backstop.
    TextMetrics { id: _tmLabel; font.pixelSize: Theme.fontM; font.weight: Font.DemiBold }
    TextMetrics { id: _tmHint;  font.pixelSize: Theme.fontS }
    function measureContentWidth() {
        var w = 220
        for (var i = 0; i < actions.length; ++i) {
            var a = actions[i]
            _tmLabel.text = a.label || ""
            var lw = _tmLabel.width
            var hw = 0
            if (a.hint) { _tmHint.text = a.hint; hw = _tmHint.width + 24 }
            var iconw = (a.iconName !== undefined && a.iconName !== "") ? 27 : 0
            // 10 left pad + icon + label + hint(+gap) + 10 right pad + 12 popup padding
            w = Math.max(w, 10 + iconw + lw + hw + 10 + 12)
        }
        return Math.ceil(w)
    }

    readonly property bool _hovered: mouse.containsMouse && !mouse.pressed
    implicitWidth: iconOnly ? 38 : rowC.implicitWidth + 34
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
            visible: !root.iconOnly && root.text !== ""
            text: root.text; font.pixelSize: Theme.fontM; font.weight: Font.DemiBold
            color: Theme.textPrimary
            anchors.verticalCenter: parent.verticalCenter
        }
        UIcon {
            visible: !root.iconOnly
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

    UHoverTip {
        anchor: root
        text: root.tooltip
        show: mouse.containsMouse && root.tooltip !== ""
    }

    C.Popup {
        id: popup
        parent: C.Overlay.overlay
        // Recomputed on open (onAboutToShow) so newly-bound actions are measured.
        // (Not named contentWidth — that shadows QQuickPopup's final property.)
        property real measuredWidth: 220
        width: {
            var maxW = C.Overlay.overlay ? C.Overlay.overlay.width - 16 : 600
            return Math.min(maxW, Math.max(root.width, measuredWidth))
        }
        height: col.implicitHeight + 12
        z: catcher.z + 1
        padding: 6
        focus: true
        closePolicy: C.Popup.CloseOnEscape

        // The bar is at the window's bottom edge → prefer opening above; flip
        // below only if there is no room above. x is clamped to the overlay.
        onAboutToShow: {
            measuredWidth = root.measureContentWidth()
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
                        // Anchored icon / label / hint: the hint owns the right
                        // edge, the label fills the gap and elides — so a long
                        // label or hint can never overlap or clip the other.
                        UIcon {
                            id: rowIcon
                            visible: modelData.iconName !== undefined && modelData.iconName !== ""
                            name: modelData.iconName || ""; size: 17; color: Theme.textPrimary
                            anchors.left: parent.left; anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            id: rowHint
                            visible: !!modelData.hint
                            anchors.right: parent.right; anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.hint || ""; color: Theme.textTertiary
                            font.pixelSize: Theme.fontS
                        }
                        Text {
                            text: modelData.label; color: Theme.textPrimary
                            font.pixelSize: Theme.fontM
                            elide: Text.ElideRight
                            anchors.left: rowIcon.visible ? rowIcon.right : parent.left
                            anchors.leftMargin: 10
                            anchors.right: rowHint.visible ? rowHint.left : parent.right
                            anchors.rightMargin: rowHint.visible ? 12 : 10
                            anchors.verticalCenter: parent.verticalCenter
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
