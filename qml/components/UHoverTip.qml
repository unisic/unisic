import QtQuick
import QtQuick.Controls as C
import Unisic

// Hover tooltip rendered as a PLAIN visual Item on the window's overlay layer.
// It is deliberately NOT a Popup and carries no input handling: a Popup tooltip
// perturbed the scene's hover delivery, which flip-flopped the anchor's
// `containsMouse` and made the tip blink. A plain overlay item lets every mouse/
// hover event pass straight through to the anchor, so hover stays stable and the
// tip never flickers. Still on the overlay (never clipped by toolbars / the
// custom title bar); flips below the anchor near the window's top edge.
Item {
    id: tip

    required property Item anchor
    property string text: ""
    property bool show: false

    parent: C.Overlay.overlay
    visible: show && text !== "" && anchor !== null && parent !== null
    enabled: false // purely visual — never intercepts the anchor's hover

    width: bg.width
    height: bg.height

    // Anchor's top-left in overlay coordinates. `show` is a deliberate binding
    // dependency: mapToItem is NOT reactive to a scrolling ancestor, so an anchor
    // inside a GridView/Flickable (the History grid) would otherwise keep its
    // stale creation-time position and the tip would land far from the button.
    // Re-reading on every show fixes it — the anchor is static during a hover.
    readonly property point _a: (show && anchor && parent)
        ? anchor.mapToItem(parent, 0, 0) : Qt.point(0, 0)

    x: {
        if (!anchor || !parent) return 0
        const raw = _a.x + (anchor.width - width) / 2
        return Math.max(4, Math.min(raw, parent.width - width - 4)) // keep on screen
    }
    // Above the anchor, or below it when the top edge is too close.
    y: (!anchor || !parent) ? 0
        : (_a.y > height + 10 ? _a.y - height - 8 : _a.y + anchor.height + 8)

    Rectangle {
        id: bg
        implicitWidth: tipLabel.implicitWidth + 16
        implicitHeight: 24
        radius: 12
        color: Theme.tooltipBg
        Text {
            id: tipLabel
            anchors.centerIn: parent
            text: tip.text
            color: Theme.tooltipText
            font.pixelSize: 11
        }
    }
}
