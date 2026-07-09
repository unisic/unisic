import QtQuick
import QtQuick.Window
import QtQuick.Effects
import Unisic
import "components"

// Small in-app capture-preview popup. Shown by AppContext after a capture or
// recording; `notif` (CaptureNotification) is injected as a context property.
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

    // Auto-dismiss, paused while the pointer is over the card.
    Timer {
        id: dismissTimer
        interval: Math.max(1, popup.autoHideSec) * 1000
        running: popup.autoHideSec > 0 && !hover.hovered
        onTriggered: notif.dismiss()
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
        property real drain: 1.0
        NumberAnimation on drain {
            running: popup.autoHideSec > 0 && !hover.hovered
            from: 1.0; to: 0.0
            duration: dismissTimer.interval
        }

        HoverHandler { id: hover }

        Row {
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
                Text {
                    width: parent.width
                    text: notif.uploading ? qsTr("Uploading…")
                          : notif.url !== "" ? notif.url
                          : notif.fileName !== "" ? notif.fileName
                          : qsTr("Not saved")
                    color: notif.url !== "" ? Theme.accent : Theme.textSecondary
                    font.pixelSize: Theme.fontS
                    elide: Text.ElideMiddle
                    maximumLineCount: 1
                }

                Row {
                    spacing: 1
                    UIconButton {
                        iconName: "edit"; iconSize: 16; width: 32; height: 32
                        tooltip: qsTr("Edit"); visible: notif.kind === "image"
                        onClicked: notif.edit()
                    }
                    UIconButton {
                        iconName: "edit-copy"; iconSize: 16; width: 32; height: 32
                        tooltip: qsTr("Copy image"); visible: notif.kind === "image"
                        onClicked: notif.copyImage()
                    }
                    UIconButton {
                        iconName: "globe"; iconSize: 16; width: 32; height: 32
                        tooltip: qsTr("Copy link"); visible: notif.url !== ""
                        onClicked: notif.copyUrl()
                    }
                    UIconButton {
                        iconName: "folder-open"; iconSize: 16; width: 32; height: 32
                        tooltip: qsTr("Show in folder")
                        onClicked: notif.showInFolder()
                    }
                    UIconButton {
                        iconName: "upload-cloud"; iconSize: 16; width: 32; height: 32
                        tooltip: qsTr("Upload"); visible: notif.url === "" && !notif.uploading
                        onClicked: notif.upload()
                    }
                    UIconButton {
                        iconName: "ocr"; iconSize: 16; width: 32; height: 32
                        tooltip: qsTr("Copy text (OCR)")
                        visible: App.ocrAvailable && notif.kind === "image"
                        onClicked: notif.ocr()
                    }
                    UIconButton {
                        iconName: "edit-delete"; iconSize: 16; width: 32; height: 32
                        tooltip: qsTr("Delete"); visible: notif.filePath !== ""
                        onClicked: notif.deleteCapture()
                    }
                }
            }
        }

        UIconButton {
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
