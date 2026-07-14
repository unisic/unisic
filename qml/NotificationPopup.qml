import QtQuick
import QtQuick.Window
import QtQuick.Effects
import Unisic
import "components"

// Small in-app capture-preview popup. Shown by AppContext after a capture or
// recording; `notif` (CaptureNotification) is injected as a context property.
//
// Five styles (Settings > Appearance > Notification style); C++
// (LayerShellNotifier) sizes the surface to match:
//   casual    400x150  full card: big thumb, title, action row
//   compact   380x96   tighter card: medium thumb, filename, action row
//   small     380x52   one slim row: tiny thumb, filename, inline actions
//   minimal   300x36   pill: filename only — click previews, nothing else
//   thumbnail 240x150  image-first: full-bleed thumb, actions on hover
Window {
    id: popup
    // Card-sized window placed on the wlr-layer-shell OVERLAY layer by C++
    // (LayerShellNotifier) — the layer surface handles stacking (always on top)
    // and focus, so no window-manager flags are needed beyond frameless.
    flags: Qt.FramelessWindowHint
    color: "transparent"
    visible: false   // AppContext sizes, layers, then show()s it

    // 0 = stay open until manually closed.
    readonly property int autoHideSec: App.settings.capturePopupDurationSec
    // Latched ONCE at creation, not live-bound: C++ sized the surface and the
    // input mask for the creation-time style, so an open card switching layout
    // mid-flight would overflow/clip the fixed window. New cards pick up the
    // changed setting because LayerShellNotifier re-reads it per show().
    property string style: "casual"
    // True while a thumbnail is being dragged out. A native drag can take many
    // seconds (aiming at another app), and the pointer leaves the card the moment
    // it starts — without this the 8s auto-hide would fire mid-drag and destroy
    // the drag's origin surface. Pauses auto-hide + the countdown drain.
    property bool dragging: false
    Component.onCompleted: {
        const s = App.settings.capturePopupStyle
        if (["casual", "compact", "small", "minimal", "thumbnail"].indexOf(s) >= 0)
            style = s
    }

    // Auto-dismiss, paused while the pointer is over the card.
    Timer {
        id: dismissTimer
        interval: Math.max(1, popup.autoHideSec) * 1000
        running: popup.autoHideSec > 0 && !hover.hovered && !popup.dragging
        onTriggered: notif.dismiss()
    }

    // The full action set, reused by every style that shows buttons.
    component ActionRow: Row {
        property int btn: 32
        property int icon: 16
        spacing: 1
        UIconButton {
            iconName: "edit"; iconSize: parent.icon; width: parent.btn; height: parent.btn
            tooltip: qsTr("Edit"); visible: notif.kind === "image"
            onClicked: notif.edit()
        }
        UIconButton {
            iconName: "edit-copy"; iconSize: parent.icon; width: parent.btn; height: parent.btn
            tooltip: qsTr("Copy image"); visible: notif.kind === "image"
            onClicked: notif.copyImage()
        }
        UIconButton {
            iconName: "globe"; iconSize: parent.icon; width: parent.btn; height: parent.btn
            tooltip: qsTr("Copy link"); visible: notif.url !== ""
            onClicked: notif.copyUrl()
        }
        UIconButton {
            iconName: "view-preview"; iconSize: parent.icon; width: parent.btn; height: parent.btn
            tooltip: qsTr("Show QR code"); visible: App.qrAvailable && notif.url !== ""
            onClicked: notif.showQr()
        }
        UIconButton {
            iconName: "folder-open"; iconSize: parent.icon; width: parent.btn; height: parent.btn
            tooltip: qsTr("Show in folder")
            onClicked: notif.showInFolder()
        }
        UIconButton {
            iconName: "upload-cloud"; iconSize: parent.icon; width: parent.btn; height: parent.btn
            tooltip: qsTr("Upload"); visible: notif.url === "" && !notif.uploading
            onClicked: notif.upload()
        }
        UIconButton {
            iconName: "ocr"; iconSize: parent.icon; width: parent.btn; height: parent.btn
            tooltip: qsTr("Copy text (OCR)")
            visible: App.ocrAvailable && notif.kind === "image"
            onClicked: notif.ocr()
        }
        UIconButton {
            iconName: "edit-delete"; iconSize: parent.icon; width: parent.btn; height: parent.btn
            tooltip: qsTr("Delete"); visible: notif.filePath !== ""
            onClicked: notif.deleteCapture()
        }
    }

    // Thumbnail interaction, shared by every style that shows one. A plain
    // click opens the floating preview; dragging past the threshold pulls the
    // capture FILE out to another app (file manager, chat, editor) via a native
    // text/uri-list drag — the same 1x1-proxy pattern the History grid uses.
    // Idle until a real gesture starts, so it costs nothing while the card sits.
    component DragThumb: MouseArea {
        anchors.fill: parent
        // Images preview + drag; recordings (with a file) drag only.
        enabled: notif.kind === "image" || notif.filePath !== ""
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        hoverEnabled: true
        drag.target: dragProxy
        drag.threshold: 8
        property string uri: ""
        // Hover cue: the thumbnail is interactive — click = preview, drag = pull
        // the file out. Suppressed in the image-first "thumbnail" style, which
        // shows its own hover action overlay. A menu/tooltip would be clipped by
        // the tiny card surface, so the affordance lives INSIDE the thumbnail.
        Rectangle {
            anchors.fill: parent
            visible: parent.containsMouse && !parent.pressed
                     && notif.kind === "image" && popup.style !== "thumbnail"
            color: Qt.rgba(0, 0, 0, 0.32)
            UIcon {
                anchors.centerIn: parent
                name: "fullscreen"
                size: Math.min(22, Math.round(parent.height * 0.4))
                color: "#FFFFFF"
            }
        }
        // Resolve the payload once, at press: dragUri() may write a temp PNG for
        // an unsaved image, so it must not run from a reactive binding. Pausing
        // auto-hide from press (not just drag start) keeps it simple; a plain
        // click clears it again on release, long before the 8s timer matters.
        onPressed: { uri = notif.dragUri(); popup.dragging = true }
        onReleased: { dragProxy.x = 0; dragProxy.y = 0; popup.dragging = false }
        onCanceled: popup.dragging = false
        onClicked: if (!drag.active && notif.kind === "image") notif.preview()
        Item {
            id: dragProxy
            width: 1; height: 1
            Drag.active: parent.drag.active
            Drag.dragType: Drag.Automatic
            Drag.supportedActions: Qt.CopyAction
            Drag.mimeData: { "text/uri-list": parent.uri }
            Drag.imageSource: notif.thumbSource
        }
    }

    // Filename / URL / upload-state line, shared by the text-bearing styles.
    component StatusText: Text {
        text: notif.uploading ? qsTr("Uploading…")
              : notif.url !== "" ? notif.url
              : notif.fileName !== "" ? notif.fileName
              : qsTr("Not saved")
        color: notif.url !== "" ? Theme.accent : Theme.textSecondary
        font.pixelSize: Theme.fontS
        elide: Text.ElideMiddle
        maximumLineCount: 1
    }

    Rectangle {
        id: card
        x: popupX
        y: popupY
        width: popupW
        height: popupH
        radius: Theme.radiusL
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.lighter(Theme.primary, 1.22) }
            GradientStop { position: 1.0; color: Theme.primary }
        }
        border.width: 1
        border.color: Theme.divider
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true; shadowColor: Theme.shadow
            shadowBlur: 1.0; shadowVerticalOffset: 4; shadowOpacity: 0.6
        }

        // Countdown drain; restarts from full whenever hovering ends.
        // Stepped Timer, NOT a NumberAnimation: a per-frame animation kept the
        // card's render loop awake at ~60 fps for the whole auto-hide window
        // (seconds of full-rate repaints for a slowly shrinking bar). 30 steps
        // over the same duration looks identical at this size.
        property real drain: 1.0
        Timer {
            interval: Math.max(50, dismissTimer.interval / 30)
            repeat: true
            running: popup.autoHideSec > 0 && !hover.hovered && !popup.dragging
            onRunningChanged: if (running) card.drain = 1.0
            onTriggered: card.drain = Math.max(0, card.drain - 1 / 30)
        }

        HoverHandler { id: hover }

        // ================= casual: the full card =================
        Row {
            visible: popup.style === "casual"
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10

            Rectangle {
                width: 122; height: parent.height
                radius: Theme.radiusM
                color: Theme.background
                clip: true
                Image {
                    anchors.fill: parent
                    source: notif.thumbSource
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                Rectangle {
                    visible: notif.kind !== "image"
                    anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 5
                    width: badge.implicitWidth + 12; height: 18; radius: 9
                    color: Theme.accent
                    Text {
                        id: badge
                        anchors.centerIn: parent
                        text: notif.kind.toUpperCase()
                        color: Theme.textOnAccent
                        font.pixelSize: 9; font.bold: true
                    }
                }
                // Click = floating preview; drag = pull the file out (DragThumb).
                DragThumb {}
            }

            Column {
                width: parent.width - 132
                height: parent.height
                spacing: 5

                Text {
                    width: parent.width
                    text: qsTr("Capture ready")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontM
                    font.weight: Font.Bold
                }
                StatusText { width: parent.width }
                ActionRow { btn: 32; icon: 16 }
            }
        }

        // ================= compact: tighter card =================
        Item {
            visible: popup.style === "compact"
            anchors.fill: parent
            anchors.margins: 8

            Rectangle {
                id: compactThumb
                width: 76; height: parent.height
                radius: Theme.radiusM
                color: Theme.background
                clip: true
                Image {
                    anchors.fill: parent
                    source: notif.thumbSource
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                DragThumb {}
            }
            Column {
                anchors.left: compactThumb.right
                anchors.leftMargin: 8
                anchors.right: parent.right
                anchors.rightMargin: 20   // room for the corner close
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                StatusText { width: parent.width }
                ActionRow { btn: 28; icon: 15 }
            }
        }

        // ================= small: one slim row =================
        Item {
            visible: popup.style === "small"
            anchors.fill: parent
            anchors.margins: 8

            Rectangle {
                id: smallThumb
                width: 36; height: 36
                anchors.verticalCenter: parent.verticalCenter
                radius: 6
                color: Theme.background
                clip: true
                Image {
                    anchors.fill: parent
                    source: notif.thumbSource
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize: Qt.size(72, 72)
                }
                DragThumb {}
            }
            UIconButton {
                id: smallClose
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                iconName: "close"; iconSize: 11; width: 22; height: 22
                onClicked: notif.dismiss()
            }
            ActionRow {
                id: smallActions
                btn: 24; icon: 14
                spacing: 0
                anchors.right: smallClose.left
                anchors.rightMargin: 2
                anchors.verticalCenter: parent.verticalCenter
            }
            StatusText {
                anchors.left: smallThumb.right
                anchors.leftMargin: 8
                anchors.right: smallActions.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // ================= minimal: a pill =================
        Item {
            visible: popup.style === "minimal"
            anchors.fill: parent
            anchors.margins: 6

            Rectangle {
                id: minimalDot
                width: 8; height: 8; radius: 4
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 4
                color: notif.uploading ? Theme.danger : Theme.accent
            }
            UIconButton {
                id: minimalClose
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                iconName: "close"; iconSize: 10; width: 20; height: 20
                onClicked: notif.dismiss()
            }
            StatusText {
                anchors.left: minimalDot.right
                anchors.leftMargin: 8
                anchors.right: minimalClose.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
            }
            // The pill itself is the action: click = floating preview.
            MouseArea {
                anchors.left: parent.left
                anchors.right: minimalClose.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                enabled: notif.kind === "image"
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: notif.preview()
            }
        }

        // ================= thumbnail: image-first =================
        Item {
            visible: popup.style === "thumbnail"
            anchors.fill: parent

            Rectangle {
                anchors.fill: parent
                anchors.margins: 1   // keep the card border visible
                radius: Theme.radiusL - 1
                color: Theme.background
                clip: true
                Image {
                    anchors.fill: parent
                    source: notif.thumbSource
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                // Click = preview, drag = pull the file out. Declared before the
                // scrim/actions overlay so those (higher in the stack) keep their
                // own clicks; the drag starts from the rest of the image.
                DragThumb {}
                // Bottom strip: filename over a scrim.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 26
                    color: Qt.rgba(0, 0, 0, 0.55)
                    StatusText {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: Text.AlignVCenter
                        color: "#FFFFFF"
                    }
                }
                // Actions fade in over the image on hover.
                Rectangle {
                    anchors.fill: parent
                    color: Qt.rgba(0, 0, 0, 0.45)
                    visible: hover.hovered
                    ActionRow {
                        anchors.centerIn: parent
                        btn: 30; icon: 16
                    }
                }
            }
        }

        // Corner close for the card-shaped styles (small/minimal have inline
        // ones; thumbnail shows it only while hovered, over the image).
        UIconButton {
            visible: popup.style === "casual" || popup.style === "compact"
                     || (popup.style === "thumbnail" && hover.hovered)
            anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 4
            iconName: "close"; iconSize: 12; width: 24; height: 24
            onClicked: notif.dismiss()
        }

        Rectangle {
            visible: popup.autoHideSec > 0
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.bottomMargin: 3
            height: 2
            radius: 1
            color: Theme.accent
            opacity: 0.5
            width: (card.width - 16) * card.drain
        }
    }
}
