import QtQuick
import QtQuick.Window
import Unisic
import Unisic.Kit

// Persistent visual frame and interactive controls for scrolling screenshot capture.
// Transparent and click-through inside the captured region so the user can scroll
// with mouse wheel, trackpad, drag scrollbars, or keyboard.
Window {
    id: overlayWindow
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    function updateMask() {
        if (typeof scrollCtl === "undefined" || !scrollCtl)
            return

        const bx = badge.visible ? badge.x : 0
        const by = badge.visible ? badge.y : 0
        const bw = badge.visible ? badge.width : 0
        const bh = badge.visible ? badge.height : 0

        const px = previewCard.visible ? previewCard.x : 0
        const py = previewCard.visible ? previewCard.y : 0
        const pw = previewCard.visible ? previewCard.width : 0
        const ph = previewCard.visible ? previewCard.height : 0

        scrollCtl.setInputRects(bx, by, bw, bh, px, py, pw, ph)
    }

    onWidthChanged: updateMask()
    onHeightChanged: updateMask()

    // 3px border drawn strictly OUTSIDE the captured region, with contrast line
    readonly property int bw: 3
    readonly property color contrast: Theme.recordFrameContrast

    Item {
        anchors.fill: parent

        // Top border
        Rectangle {
            x: scrollCtl.regionX - overlayWindow.bw
            y: scrollCtl.regionY - overlayWindow.bw
            width: scrollCtl.regionW + 2 * overlayWindow.bw
            height: overlayWindow.bw
            color: Theme.accent
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: overlayWindow.contrast }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: overlayWindow.contrast }
        }
        // Bottom border
        Rectangle {
            x: scrollCtl.regionX - overlayWindow.bw
            y: scrollCtl.regionY + scrollCtl.regionH
            width: scrollCtl.regionW + 2 * overlayWindow.bw
            height: overlayWindow.bw
            color: Theme.accent
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: overlayWindow.contrast }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: overlayWindow.contrast }
        }
        // Left border
        Rectangle {
            x: scrollCtl.regionX - overlayWindow.bw
            y: scrollCtl.regionY
            width: overlayWindow.bw
            height: scrollCtl.regionH
            color: Theme.accent
            Rectangle { anchors.left: parent.left; height: parent.height; width: 1; color: overlayWindow.contrast }
            Rectangle { anchors.right: parent.right; height: parent.height; width: 1; color: overlayWindow.contrast }
        }
        // Right border
        Rectangle {
            x: scrollCtl.regionX + scrollCtl.regionW
            y: scrollCtl.regionY
            width: overlayWindow.bw
            height: scrollCtl.regionH
            color: Theme.accent
            Rectangle { anchors.left: parent.left; height: parent.height; width: 1; color: overlayWindow.contrast }
            Rectangle { anchors.right: parent.right; height: parent.height; width: 1; color: overlayWindow.contrast }
        }
    }

    // Floating Control Badge
    Rectangle {
        id: badge
        readonly property bool roomAbove: scrollCtl.regionY - height - 10 >= 0
        x: Math.max(10, Math.min(overlayWindow.width - width - 10, scrollCtl.regionX))
        y: roomAbove ? (scrollCtl.regionY - height - 10) : (scrollCtl.regionY + scrollCtl.regionH + 10)
        width: badgeRow.width + 24
        height: 38
        radius: 19
        color: Theme.primary
        border.width: 1
        border.color: Theme.border

        onXChanged: overlayWindow.updateMask()
        onYChanged: overlayWindow.updateMask()
        onWidthChanged: overlayWindow.updateMask()
        onHeightChanged: overlayWindow.updateMask()
        onVisibleChanged: overlayWindow.updateMask()
        Component.onCompleted: overlayWindow.updateMask()

        Row {
            id: badgeRow
            anchors.centerIn: parent
            spacing: 8

            UIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron-down"
                color: Theme.accent
                size: 16
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: scrollCtl.frameCount <= 1
                      ? qsTr("Scroll down to capture...")
                      : qsTr("%1 × %2 px (%3)")
                        .arg(scrollCtl.stitchedWidth)
                        .arg(scrollCtl.stitchedHeight)
                        .arg(scrollCtl.frameCount)
                color: Theme.textPrimary
                font.pixelSize: Theme.fontM
                font.weight: Font.DemiBold
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 1; height: 18
                color: Theme.divider
            }

            UButton {
                anchors.verticalCenter: parent.verticalCenter
                compact: true
                iconName: "checkmark"
                text: qsTr("Finish")
                onClicked: scrollCtl.finish()
            }

            UIconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "close"
                accessibleName: qsTr("Cancel")
                onClicked: scrollCtl.cancel()
            }
        }
    }

    // Floating Live Preview Card
    Rectangle {
        id: previewCard
        readonly property bool roomRight: scrollCtl.regionX + scrollCtl.regionW + width + 16 <= overlayWindow.width
        readonly property bool roomLeft: scrollCtl.regionX - width - 16 >= 0
        visible: scrollCtl.frameCount > 1

        width: 120
        height: Math.min(300, Math.max(90, scrollCtl.stitchedHeight > 0
                                       ? (scrollCtl.stitchedHeight * 100 / Math.max(1, scrollCtl.stitchedWidth))
                                       : 100))
        x: roomRight ? (scrollCtl.regionX + scrollCtl.regionW + 12)
                     : (roomLeft ? (scrollCtl.regionX - width - 12) : (overlayWindow.width - width - 12))
        y: Math.max(10, Math.min(overlayWindow.height - height - 10, scrollCtl.regionY))

        radius: 8
        color: Theme.primary
        border.width: 1
        border.color: Theme.accent

        onXChanged: overlayWindow.updateMask()
        onYChanged: overlayWindow.updateMask()
        onWidthChanged: overlayWindow.updateMask()
        onHeightChanged: overlayWindow.updateMask()
        onVisibleChanged: overlayWindow.updateMask()

        Column {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4

            Item {
                width: parent.width
                height: parent.height - 20
                clip: true

                Image {
                    id: previewImg
                    anchors.fill: parent
                    source: "image://scrollpreview/" + scrollCtl.previewRevision
                    fillMode: Image.PreserveAspectFit
                    cache: false
                }
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("%1 px").arg(scrollCtl.stitchedHeight)
                color: Theme.textSecondary
                font.pixelSize: 10
                font.bold: true
            }
        }
    }

    Keys.onReturnPressed: (e) => { e.accepted = true; scrollCtl.finish(); }
    Keys.onEnterPressed: (e) => { e.accepted = true; scrollCtl.finish(); }
    Keys.onSpacePressed: (e) => { e.accepted = true; scrollCtl.finish(); }
    Keys.onEscapePressed: (e) => { e.accepted = true; scrollCtl.cancel(); }
}
