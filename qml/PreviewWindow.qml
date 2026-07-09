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
    height: Math.round(imgSize.height * fit) + 40
    color: "transparent"
    title: qsTr("Unisic — Preview")

    // Static flags (no dependencies → evaluated once): PreviewController adds
    // stays-on-top / transparent-for-input imperatively, so a rebinding here
    // can't clobber them.
    flags: Qt.Window | Qt.FramelessWindowHint

    // Kept for the C++ passthrough hotkey (invokeMethod by name).
    function togglePassthrough() { if (previewCtl) previewCtl.togglePassthrough() }

    // Fading is done on the CONTENT, not Window.opacity — qtwayland has no
    // window-opacity protocol, setOpacity is silently ignored there.
    Rectangle {
        id: content
        anchors.fill: parent
        radius: Theme.radiusM
        color: Theme.background
        border.width: preview.passthrough ? 2 : 1
        border.color: preview.passthrough ? Theme.accent : Theme.divider
        clip: true

        // Top control bar (doubles as the drag handle). Controls are anchored
        // directly (NOT in a Row: an anchored child turns a Row off entirely —
        // that broke the slider/close side before).
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
                id: dragArea
                anchors.fill: parent
                property real pressX: 0
                property real pressY: 0
                property bool moving: false
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                onPressed: (m) => { pressX = m.x; pressY = m.y; moving = false }
                onPositionChanged: (m) => {
                    if (previewCtl && !previewCtl.layerShell
                        && !moving && (Math.abs(m.x - pressX) > 6 || Math.abs(m.y - pressY) > 6)) {
                        moving = true
                        previewCtl.startMove()
                    }
                }
                // Layer surface: it can't be system-moved and repositioning it
                // per-move-event diverges (the compositor lags the events), so
                // the window stays put while dragging and the full delta is
                // applied once on release. Item coordinates are exact for that
                // very reason — the window never moves mid-drag.
                onReleased: (m) => {
                    if (previewCtl && previewCtl.layerShell && !moving)
                        previewCtl.moveBy(Math.round(m.x - pressX), Math.round(m.y - pressY))
                }
            }

            UIconButton {
                id: pinBtn
                iconName: "window-pin"
                tooltip: (previewCtl && previewCtl.pinned) ? qsTr("Unpin (drop below fullscreen)")
                                                           : qsTr("Pin on top")
                active: previewCtl ? previewCtl.pinned : true
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 30; height: 30; iconSize: 16
                onClicked: if (previewCtl) previewCtl.pinned = !previewCtl.pinned
            }
            UIconButton {
                id: passBtn
                iconName: "view-transparent"
                tooltip: qsTr("Click-through (trace over the window) — %1 to toggle back")
                         .arg(App.settings.hotkeyPreviewPassthrough)
                active: preview.passthrough
                anchors.left: pinBtn.right
                anchors.leftMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                width: 30; height: 30; iconSize: 16
                onClicked: preview.togglePassthrough()
            }

            UIconButton {
                id: closeBtn
                iconName: "close"
                tooltip: qsTr("Close")
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 30; height: 30; iconSize: 16
                onClicked: preview.close()
            }
            USlider {
                id: opacitySlider
                width: 110; height: 30
                anchors.right: closeBtn.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                from: 0.2; to: 1.0; stepSize: 0.01
                value: content.opacity
                onMoved: (v) => content.opacity = v
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
