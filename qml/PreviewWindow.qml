import QtQuick
import QtQuick.Window
import Unisic
import "components"

// Floating capture preview. With layer-shell the window is a FULLSCREEN
// overlay surface whose input region is masked to the visible card
// (previewCtl.setInputRect — same pattern as the capture popup): the card is
// a plain movable item, so dragging is pure scene-graph and perfectly smooth.
// Without layer-shell it's a normal frameless window sized to the card, moved
// with startSystemMove. The card can be faded with the opacity slider while
// staying fully clickable.
Window {
    id: preview

    // Injected via the per-window context: previewImagePath, previewImageSize,
    // previewCtl (the C++ PreviewController).
    property size imgSize: (typeof previewImageSize !== "undefined" && previewImageSize.width > 0)
                           ? previewImageSize : Qt.size(640, 400)
    readonly property bool layerMode: previewCtl ? previewCtl.layerShell : false

    readonly property int maxW: Screen.desktopAvailableWidth * 0.7
    readonly property int maxH: Screen.desktopAvailableHeight * 0.7
    readonly property real fit: Math.min(1.0, maxW / imgSize.width,
                                         (maxH - 40) / imgSize.height)
    readonly property int cardW: Math.max(280, Math.round(imgSize.width * fit))
    readonly property int cardH: Math.round(imgSize.height * fit) + 40

    // Layer mode: the compositor sizes the surface (fullscreen anchors); these
    // initial values only matter for the non-layer fallback.
    width: cardW
    height: cardH
    color: "transparent"
    title: qsTr("Unisic — Preview")

    // Static flags (no dependencies → evaluated once): PreviewController adds
    // stays-on-top imperatively, so a rebinding here can't clobber it.
    flags: Qt.Window | Qt.FramelessWindowHint

    Item {
        id: root
        anchors.fill: parent

        function updateMask() {
            if (preview.layerMode && previewCtl)
                previewCtl.setInputRect(card.x, card.y, card.width, card.height)
        }

        // The visible preview card. In layer mode it floats inside the
        // fullscreen surface (starts top-right); in fallback mode it IS the
        // window. Fading targets this item, not Window.opacity — qtwayland has
        // no window-opacity protocol, setOpacity is silently ignored there.
        Rectangle {
            id: card
            width: preview.cardW
            height: preview.cardH
            x: preview.layerMode ? Math.max(0, root.width - width - 64) : 0
            y: preview.layerMode ? Math.min(64, Math.max(0, root.height - height)) : 0
            radius: Theme.radiusM
            color: Theme.background
            border.width: 1
            border.color: Theme.divider
            clip: true

            onXChanged: root.updateMask()
            onYChanged: root.updateMask()
            onWidthChanged: root.updateMask()
            onHeightChanged: root.updateMask()
            Component.onCompleted: root.updateMask()

            // Top control bar (doubles as the drag handle). Controls are
            // anchored directly (NOT in a Row: an anchored child turns a Row
            // off entirely — that broke the slider/close side before).
            Rectangle {
                id: bar
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 40
                color: Theme.surface

                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                    property real pressX: 0
                    property real pressY: 0
                    property bool moving: false
                    // Layer mode: drag the card item directly — synchronous
                    // scene-graph movement, no compositor round-trips.
                    drag.target: preview.layerMode ? card : undefined
                    drag.minimumX: 0
                    drag.maximumX: Math.max(0, root.width - card.width)
                    drag.minimumY: 0
                    drag.maximumY: Math.max(0, root.height - card.height)
                    onPressed: (m) => { pressX = m.x; pressY = m.y; moving = false }
                    onPositionChanged: (m) => {
                        if (previewCtl && !preview.layerMode
                            && !moving && (Math.abs(m.x - pressX) > 6 || Math.abs(m.y - pressY) > 6)) {
                            moving = true
                            previewCtl.startMove()
                        }
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
                    value: card.opacity
                    onMoved: (v) => card.opacity = v
                }
            }

            Image {
                anchors.top: bar.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                source: (typeof previewImagePath !== "undefined") ? previewImagePath : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
            }
        }
    }
}
