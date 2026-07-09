import QtQuick
import QtQuick.Window
import Unisic
import "components"

// Floating capture preview: a frameless window kept above other windows by
// PreviewController (layer-shell where available — a plain window's stays-on-top
// hint is ignored on Wayland). It can be faded with the opacity slider (still
// fully clickable), and flipped into a click-through "trace over the window"
// mode. Because click-through eats its own controls, the mode is also toggled by
// a global hotkey (App.settings.hotkeyPreviewPassthrough) via previewCtl.
Window {
    id: preview

    // Injected via the per-window context: previewImagePath, previewImageSize,
    // previewCtl (the C++ PreviewController).
    property size imgSize: (typeof previewImageSize !== "undefined" && previewImageSize.width > 0)
                           ? previewImageSize : Qt.size(640, 400)
    readonly property bool passthrough: previewCtl ? previewCtl.passthrough : false

    readonly property int maxW: Screen.desktopAvailableWidth * 0.9
    readonly property int maxH: Screen.desktopAvailableHeight * 0.9
    readonly property real fit: Math.min(1.0, maxW / imgSize.width,
                                         (maxH - 40) / imgSize.height)
    width: Math.max(280, Math.round(imgSize.width * fit))
    height: Math.round(imgSize.height * fit) + bar.height
    color: "transparent"
    title: qsTr("Unisic — Preview")

    // Static flags (no dependencies → evaluated once): PreviewController adds
    // stays-on-top / transparent-for-input imperatively, so a rebinding here
    // can't clobber them.
    flags: Qt.Window | Qt.FramelessWindowHint

    // Kept for the C++ passthrough hotkey (invokeMethod by name).
    function togglePassthrough() { if (previewCtl) previewCtl.togglePassthrough() }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusM
        color: Theme.background
        border.width: preview.passthrough ? 2 : 1
        border.color: preview.passthrough ? Theme.accent : Theme.divider
        clip: true

        // Top control bar (doubles as the drag handle).
        Rectangle {
            id: bar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 40
            color: Theme.surface
            // Hidden in click-through mode (non-interactive anyway) so the
            // preview is a clean image while you trace over the window below.
            visible: !preview.passthrough

            MouseArea {
                anchors.fill: parent
                property real pressX: 0
                property real pressY: 0
                property bool moving: false
                property real lastX: 0
                property real lastY: 0
                onPressed: (m) => { pressX = m.x; pressY = m.y; lastX = m.x; lastY = m.y; moving = false }
                onPositionChanged: (m) => {
                    if (!previewCtl)
                        return
                    if (previewCtl.layerShell) {
                        // Layer surfaces can't be system-moved; nudge via margins.
                        previewCtl.moveBy(Math.round(m.x - lastX), Math.round(m.y - lastY))
                    } else if (!moving && (Math.abs(m.x - pressX) > 6 || Math.abs(m.y - pressY) > 6)) {
                        moving = true
                        previewCtl.startMove()
                    }
                }
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                UIconButton {
                    iconName: "window-pin"
                    tooltip: (previewCtl && previewCtl.pinned) ? qsTr("Unpin (drop below fullscreen)")
                                                               : qsTr("Pin on top")
                    active: previewCtl ? previewCtl.pinned : true
                    width: 30; height: 30; iconSize: 16
                    onClicked: if (previewCtl) previewCtl.pinned = !previewCtl.pinned
                }
                UIconButton {
                    iconName: "view-transparent"
                    tooltip: qsTr("Click-through (trace over the window) — %1 to toggle back")
                             .arg(App.settings.hotkeyPreviewPassthrough)
                    active: preview.passthrough
                    width: 30; height: 30; iconSize: 16
                    onClicked: preview.togglePassthrough()
                }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                USlider {
                    id: opacitySlider
                    width: 110
                    anchors.verticalCenter: parent.verticalCenter
                    from: 0.2; to: 1.0; stepSize: 0.01
                    value: preview.opacity
                    onMoved: (v) => preview.opacity = v
                }
                UIconButton {
                    iconName: "close"
                    tooltip: qsTr("Close")
                    width: 30; height: 30; iconSize: 16
                    onClicked: if (previewCtl) previewCtl.closeWindow(); else preview.close()
                }
            }
        }

        Image {
            anchors.top: bar.visible ? bar.bottom : parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            source: (typeof previewImagePath !== "undefined") ? previewImagePath : ""
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            smooth: true
        }

        // A faint hint that the window is currently transparent to input, plus
        // the hotkey to bring it back.
        Rectangle {
            visible: preview.passthrough
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 8
            width: hintText.implicitWidth + 16
            height: 22
            radius: 11
            color: Theme.accent
            opacity: 0.85
            Text {
                id: hintText
                anchors.centerIn: parent
                text: qsTr("click-through · %1").arg(App.settings.hotkeyPreviewPassthrough)
                color: Theme.textOnAccent
                font.pixelSize: 10; font.bold: true
            }
        }
    }
}
