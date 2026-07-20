import QtQuick
import QtQuick.Controls as C
import QtQuick.Effects
import Unisic

// Hover tooltip rendered as a PLAIN visual Item on the window's overlay layer.
// It is deliberately NOT a Popup and carries no input handling: a Popup tooltip
// perturbed the scene's hover delivery, which flip-flopped the anchor's
// `containsMouse` and made the tip blink. A plain overlay item lets every mouse/
// hover event pass straight through to the anchor, so hover stays stable and the
// tip never flickers. Still on the overlay (never clipped by toolbars / the
// custom title bar); flips below the anchor near the window's top edge.
//
// The ONE tooltip in the app: every hover hint goes through here so they share
// the delay, the fade and the look. Qt Quick Controls' own ToolTip is the Basic
// style's grey box and matches nothing else in this UI.
Item {
    id: tip

    required property Item anchor
    property string text: ""
    property bool show: false
    // A tip that appears the instant the pointer crosses a button strobes while
    // you sweep along a toolbar; a short arming delay keeps it calm.
    property int delayMs: 320
    // Long hints wrap instead of running off the window.
    property int maxWidth: 320

    parent: C.Overlay.overlay
    enabled: false // purely visual — never intercepts the anchor's hover

    property bool _armed: false
    Timer {
        id: armTimer
        interval: tip.delayMs
        onTriggered: tip._armed = true
    }
    onShowChanged: {
        if (show) {
            armTimer.restart()
        } else {
            armTimer.stop()
            tip._armed = false
        }
    }

    readonly property bool _wanted: show && _armed && text !== ""
                                    && anchor !== null && parent !== null
    opacity: _wanted ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }

    width: bg.width
    height: bg.height

    // Anchor's top-left in overlay coordinates. `show` is a deliberate binding
    // dependency: mapToItem is NOT reactive to a scrolling ancestor, so an anchor
    // inside a GridView/Flickable (the History grid) would otherwise keep its
    // stale creation-time position and the tip would land far from the button.
    // Re-reading on every show fixes it — the anchor is static during a hover.
    readonly property point _a: (show && anchor && parent)
        ? anchor.mapToItem(parent, 0, 0) : Qt.point(0, 0)
    readonly property bool _above: _a.y > height + 10

    x: {
        if (!anchor || !parent) return 0
        const raw = _a.x + (anchor.width - width) / 2
        return Math.max(4, Math.min(raw, parent.width - width - 4)) // keep on screen
    }
    // Above the anchor, or below it when the top edge is too close.
    y: (!anchor || !parent) ? 0
        : (_above ? _a.y - height - 8 : _a.y + anchor.height + 8)
    // A few pixels of travel on the way in, from the direction it opens.
    transform: Translate {
        y: tip._wanted ? 0 : (tip._above ? 4 : -4)
        Behavior on y { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
    }

    Rectangle {
        id: bg
        implicitWidth: tipLabel.width + 20
        implicitHeight: tipLabel.implicitHeight + 12
        radius: Theme.radiusS
        color: Theme.tooltipBg
        border.width: 1
        border.color: Theme.alpha(Theme.edgeLight, 0.18)
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.shadow
            shadowBlur: 0.7
            shadowVerticalOffset: 2
            shadowOpacity: 0.45
        }

        Text {
            id: tipLabel
            anchors.centerIn: parent
            width: Math.min(implicitWidth, tip.maxWidth)
            text: tip.text
            color: Theme.tooltipText
            font.pixelSize: Theme.fontS
            wrapMode: Text.WordWrap
            horizontalAlignment: lineCount > 1 ? Text.AlignLeft : Text.AlignHCenter
        }
    }
}
