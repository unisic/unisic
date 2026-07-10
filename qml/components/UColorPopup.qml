import QtQuick
import QtQuick.Controls
import Unisic

// In-scene HSV colour picker. QtQuick.Dialogs' ColorDialog is a separate
// top-level Window — under the region overlay's layer-shell surface (which
// holds an EXCLUSIVE keyboard grab) that window never receives input, so it
// froze the whole capture screen. A Popup renders inside the SAME window
// scene / wl_surface, so it works everywhere the overlay does.
Popup {
    id: root

    // Live value; open() decomposes it into h/s/v/a.
    property color selected: "#ffffff"
    property bool showAlpha: false
    // Quick-pick chips (the theme palette by default).
    property var swatches: Theme.swatches

    // Emitted continuously as the user drags (live preview) and once more on
    // Done — consumers just assign it to their colour property.
    signal picked(color c)
    // The eyedropper button asks the host to start screen colour-picking; the
    // host samples a pixel and calls setColor(c) to resume with it.
    signal requestScreenPick()

    // Set h/s/v/a from an arbitrary colour (used by the hex field and the
    // screen eyedropper result).
    function setColor(c) {
        hue = c.hsvHue >= 0 ? c.hsvHue : hue
        sat = c.hsvSaturation
        val = c.hsvValue
        if (root.showAlpha)
            alpha = c.a
    }

    // HSV state in [0,1]; `col` is the composed result.
    property real hue: 0
    property real sat: 0
    property real val: 1
    property real alpha: 1
    readonly property color col: Qt.hsva(hue, sat, val, alpha)

    function openWith(c) {
        selected = c
        hue = c.hsvHue >= 0 ? c.hsvHue : 0
        sat = c.hsvSaturation
        val = c.hsvValue
        alpha = c.a
        open()
    }

    onColChanged: if (opened) root.picked(col)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    // Escape must close THIS, not cancel the capture session — a focused
    // Popup consumes the key before the overlay root's handler sees it.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: Theme.spacingL

    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }
    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surface
        border.width: 1
        border.color: Theme.divider
    }

    contentItem: Column {
        spacing: Theme.spacingM
        width: 240

        // ---- saturation / value square ----
        Item {
            width: parent.width
            height: 150

            Rectangle { // pure hue base
                anchors.fill: parent
                radius: Theme.radiusS
                color: Qt.hsva(root.hue, 1, 1, 1)
            }
            Rectangle { // white -> transparent (saturation, left to right)
                anchors.fill: parent
                radius: Theme.radiusS
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#ffffffff" }
                    GradientStop { position: 1.0; color: "#00ffffff" }
                }
            }
            Rectangle { // transparent -> black (value, top to bottom)
                anchors.fill: parent
                radius: Theme.radiusS
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#00000000" }
                    GradientStop { position: 1.0; color: "#ff000000" }
                }
            }
            Rectangle { // marker
                width: 14; height: 14; radius: 7
                border.width: 2; border.color: "#ffffff"
                color: "transparent"
                x: root.sat * parent.width - width / 2
                y: (1 - root.val) * parent.height - height / 2
                Rectangle {
                    anchors.fill: parent; anchors.margins: 2; radius: 5
                    border.width: 1; border.color: "#80000000"; color: "transparent"
                }
            }
            MouseArea {
                anchors.fill: parent
                onPressed: (m) => handle(m)
                onPositionChanged: (m) => handle(m)
                function handle(m) {
                    root.sat = Math.max(0, Math.min(1, m.x / width))
                    root.val = Math.max(0, Math.min(1, 1 - m.y / height))
                }
            }
        }

        // ---- hue slider ----
        Item {
            width: parent.width
            height: 18
            Rectangle {
                anchors.fill: parent
                radius: 9
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.000; color: "#ff0000" }
                    GradientStop { position: 0.167; color: "#ffff00" }
                    GradientStop { position: 0.333; color: "#00ff00" }
                    GradientStop { position: 0.500; color: "#00ffff" }
                    GradientStop { position: 0.667; color: "#0000ff" }
                    GradientStop { position: 0.833; color: "#ff00ff" }
                    GradientStop { position: 1.000; color: "#ff0000" }
                }
            }
            Rectangle {
                width: 6; height: parent.height + 4; radius: 3
                y: -2
                x: root.hue * parent.width - width / 2
                color: "transparent"
                border.width: 2; border.color: "#ffffff"
            }
            MouseArea {
                anchors.fill: parent
                onPressed: (m) => root.hue = Math.max(0, Math.min(1, m.x / width))
                onPositionChanged: (m) => root.hue = Math.max(0, Math.min(1, m.x / width))
            }
        }

        // ---- alpha slider (fill colour only) ----
        Item {
            visible: root.showAlpha
            width: parent.width
            height: 18
            Canvas { // checkerboard so transparency reads
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    const s = 6
                    for (let y = 0; y < height; y += s)
                        for (let x = 0; x < width; x += s) {
                            ctx.fillStyle = ((x / s + y / s) % 2 === 0) ? "#cccccc" : "#888888"
                            ctx.fillRect(x, y, s, s)
                        }
                }
            }
            Rectangle {
                anchors.fill: parent
                radius: 9
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.hsva(root.hue, root.sat, root.val, 0) }
                    GradientStop { position: 1.0; color: Qt.hsva(root.hue, root.sat, root.val, 1) }
                }
            }
            Rectangle {
                width: 6; height: parent.height + 4; radius: 3
                y: -2
                x: root.alpha * parent.width - width / 2
                color: "transparent"
                border.width: 2; border.color: "#ffffff"
            }
            MouseArea {
                anchors.fill: parent
                onPressed: (m) => root.alpha = Math.max(0, Math.min(1, m.x / width))
                onPositionChanged: (m) => root.alpha = Math.max(0, Math.min(1, m.x / width))
            }
        }

        // ---- quick swatches ----
        Row {
            spacing: 6
            Repeater {
                model: root.swatches
                delegate: Rectangle {
                    required property var modelData
                    width: 22; height: 22; radius: Theme.radiusS
                    color: modelData
                    border.width: Qt.colorEqual(modelData, root.col) ? 2 : 1
                    border.color: Qt.colorEqual(modelData, root.col) ? Theme.accent : Theme.divider
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            const c = parent.color
                            root.hue = c.hsvHue >= 0 ? c.hsvHue : root.hue
                            root.sat = c.hsvSaturation
                            root.val = c.hsvValue
                            // keep current alpha
                        }
                    }
                }
            }
        }

        // ---- eyedropper + editable hex ----
        Row {
            width: parent.width
            spacing: Theme.spacingS
            UIconButton {
                iconName: "color-picker"; iconSize: 16
                width: 34; height: 34
                anchors.verticalCenter: parent.verticalCenter
                tooltip: qsTr("Pick a colour from the screen")
                onClicked: { root.close(); root.requestScreenPick() }
            }
            Rectangle {
                width: 34; height: 34; radius: Theme.radiusS
                color: root.col
                border.width: 1; border.color: Theme.divider
                anchors.verticalCenter: parent.verticalCenter
            }
            UTextField {
                id: hexField
                width: parent.width - 34 * 2 - Theme.spacingS * 2
                anchors.verticalCenter: parent.verticalCenter
                placeholder: root.showAlpha ? "#AARRGGBB" : "#RRGGBB"
                // Reflect the live colour unless the user is editing the field.
                text: activeFocus ? text
                    : (root.showAlpha
                       ? "#" + root.col.toString().slice(1).toUpperCase()
                       : "#" + root.col.toString().slice(1, 7).toUpperCase())
                function applyHex() {
                    let t = text.trim()
                    if (t.length > 0 && t[0] !== "#") t = "#" + t
                    // #RGB / #RRGGBB / #AARRGGBB only.
                    if (/^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(t)) {
                        const c = Qt.color(t)
                        if (c.a !== undefined) root.setColor(c)
                    }
                }
                onAccepted: applyHex()
                onActiveFocusChanged: if (!activeFocus) applyHex()
            }
        }

        // ---- done ----
        UButton {
            anchors.right: parent.right
            text: qsTr("Done")
            variant: "filled"
            compact: true
            onClicked: { root.picked(root.col); root.close() }
        }
    }
}
