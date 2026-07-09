import QtQuick
import QtQuick.Window
import Unisic
import "components"

// Floating capture preview: a lightweight frameless tool window that stays out
// of the taskbar/alt-tab. It can be pinned always-on-top, faded with an opacity
// slider (still fully clickable), and flipped into a click-through "trace over
// the window" mode. Because click-through eats its own controls, the mode is
// also toggled by a global hotkey (App.settings.hotkeyPreviewPassthrough),
// dispatched from C++ via togglePassthrough().
Window {
    id: preview

    // previewImagePath / previewImageSize are injected via the per-window context.
    property size imgSize: (typeof previewImageSize !== "undefined" && previewImageSize.width > 0)
                           ? previewImageSize : Qt.size(640, 400)
    property bool pinned: true
    property bool passthrough: false

    // C++ (the global hotkey) calls this; keep the name stable.
    function togglePassthrough() {
        passthrough = !passthrough
        // Click-through only makes sense floating above other windows.
        if (passthrough)
            pinned = true
    }

    readonly property int maxW: Screen.desktopAvailableWidth * 0.9
    readonly property int maxH: Screen.desktopAvailableHeight * 0.9
    readonly property real fit: Math.min(1.0, maxW / imgSize.width,
                                         (maxH - 40) / imgSize.height)
    width: Math.max(260, Math.round(imgSize.width * fit))
    height: Math.round(imgSize.height * fit) + bar.height
    color: "transparent"
    title: qsTr("Unisic — Preview")

    // Frameless tool window; optionally always-on-top and/or transparent to input.
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.Tool
           | (pinned ? Qt.WindowStaysOnTopHint : 0)
           | (passthrough ? Qt.WindowTransparentForInput : 0)

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
            // Hidden in click-through mode (it's non-interactive anyway) so the
            // preview is pure image while you trace over the window below.
            visible: !preview.passthrough

            MouseArea {
                anchors.fill: parent
                property real pressX: 0
                property real pressY: 0
                property bool moving: false
                onPressed: (m) => { pressX = m.x; pressY = m.y; moving = false }
                onPositionChanged: (m) => {
                    if (!moving && (Math.abs(m.x - pressX) > 6 || Math.abs(m.y - pressY) > 6)) {
                        moving = true
                        preview.startSystemMove()
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
                    tooltip: preview.pinned ? qsTr("Unpin (allow behind other windows)")
                                            : qsTr("Pin on top")
                    active: preview.pinned
                    width: 30; height: 30; iconSize: 16
                    onClicked: preview.pinned = !preview.pinned
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
                    from: 0.15; to: 1.0; stepSize: 0.01
                    value: preview.opacity
                    onMoved: (v) => preview.opacity = v
                }
                UIconButton {
                    iconName: "close"
                    tooltip: qsTr("Close")
                    width: 30; height: 30; iconSize: 16
                    onClicked: preview.close()
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

        // A faint hint that the window is currently transparent to input.
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
