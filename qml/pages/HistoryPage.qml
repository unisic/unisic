import QtQuick
import QtQuick.Window
import Unisic
import "../components"

Item {
    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        spacing: Theme.spacingL

        Item {
            width: parent.width
            height: 40
            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("History")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
            }
            UButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Clear all")
                variant: "ghost"
                compact: true
                enabled: App.history.count > 0
                onClicked: clearAllConfirm.open()
            }
            UConfirmDialog {
                id: clearAllConfirm
                title: qsTr("Clear the whole history?")
                text: qsTr("This removes every history entry AND moves the capture files to the trash.\n\nStarred (favorite) captures are kept, both the entry and the file.")
                confirmText: qsTr("Clear all")
                destructive: true
                onAccepted: App.history.clearAll()
            }
        }

        Text {
            visible: App.history.count === 0
            text: qsTr("Nothing here yet. Captures and recordings will appear here with thumbnails.")
            color: Theme.textTertiary
            font.pixelSize: Theme.fontM
        }

        GridView {
            id: grid
            width: parent.width
            height: parent.height - 60 - Theme.spacingL
            clip: true
            cellWidth: 250
            cellHeight: 210
            model: App.history
            boundsBehavior: Flickable.StopAtBounds

            MiddleScroll { flickable: grid }
            WheelBoost { flickable: grid }

            delegate: Item {
                width: grid.cellWidth
                height: grid.cellHeight

                Rectangle {
                    id: card
                    anchors.fill: parent
                    anchors.margins: 8
                    radius: Theme.radiusL
                    color: cardHover.hovered ? Theme.surfaceHi : Theme.surface
                    border.width: 1
                    border.color: cardHover.hovered ? Theme.alpha(Theme.accent, 0.45) : Theme.divider
                    clip: true
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

                    // HoverHandler (not a MouseArea): child button MouseAreas
                    // don't steal its hover, so the action overlay stays up
                    // while the pointer is over a button.
                    HoverHandler { id: cardHover }

                    Column {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        Rectangle {
                            id: thumb
                            width: parent.width
                            height: 128
                            radius: Theme.radiusM
                            color: Theme.primary
                            clip: true
                            Image {
                                anchors.fill: parent
                                source: thumbnail !== "" ? "file://" + encodeURI(thumbnail).replace(/[?#]/g, encodeURIComponent) : ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                                sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                            }
                            // Kind badge (GIF/MP4/…): top-left.
                            Rectangle {
                                visible: kind !== "image"
                                anchors.top: parent.top
                                anchors.left: parent.left
                                anchors.margins: 6
                                width: kindText.implicitWidth + 14
                                height: 20
                                radius: 10
                                color: Theme.accent
                                Text {
                                    id: kindText
                                    anchors.centerIn: parent
                                    text: kind.toUpperCase()
                                    color: Theme.textOnAccent
                                    font.pixelSize: 10
                                    font.weight: Font.Bold
                                }
                            }
                            // Star: top-right, dark scrim disc so it reads on
                            // any screenshot. Above the action overlay (z).
                            Rectangle {
                                z: 2
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.margins: 6
                                width: 26; height: 26; radius: 13
                                color: Qt.rgba(0, 0, 0, 0.5)
                                visible: favorite || cardHover.hovered
                                UIcon {
                                    anchors.centerIn: parent
                                    name: favorite ? "star-filled" : "star"
                                    size: 15
                                    color: favorite ? Theme.accent : "#FFFFFF"
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: App.history.setFavorite(index, !favorite)
                                }
                            }
                            // Action overlay: fades in on hover over the whole
                            // card, so every action fits without cramping the
                            // tile. White icons on a dark scrim.
                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radiusM
                                color: Qt.rgba(0, 0, 0, 0.55)
                                visible: opacity > 0
                                opacity: cardHover.hovered ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: Theme.animFast } }

                                Grid {
                                    anchors.centerIn: parent
                                    columns: 4
                                    rowSpacing: 6
                                    columnSpacing: 6
                                    UIconButton {
                                        iconName: "content-copy"; iconSize: 17; width: 34; height: 34
                                        tooltip: qsTr("Copy image")
                                        visible: kind === "image" && filePath !== ""
                                        onClicked: App.copyImageFromHistory(filePath)
                                    }
                                    UIconButton {
                                        iconName: "globe"; iconSize: 17; width: 34; height: 34
                                        tooltip: qsTr("Copy link")
                                        enabled: url !== ""
                                        onClicked: { App.copyText(url); App.showToast(qsTr("Link copied")) }
                                    }
                                    UIconButton {
                                        iconName: "upload-cloud"; iconSize: 17; width: 34; height: 34
                                        tooltip: qsTr("Upload")
                                        enabled: filePath !== ""
                                        onClicked: App.uploadFromHistory(filePath)
                                    }
                                    UIconButton {
                                        iconName: "folder-open"; iconSize: 17; width: 34; height: 34
                                        tooltip: qsTr("Open file")
                                        enabled: filePath !== ""
                                        onClicked: App.openFile(filePath)
                                    }
                                    UIconButton {
                                        iconName: "edit"; iconSize: 17; width: 34; height: 34
                                        tooltip: qsTr("Edit (overwrites the file on save)")
                                        visible: kind === "image" && filePath !== ""
                                        onClicked: App.editFromHistory(filePath)
                                    }
                                    UIconButton {
                                        iconName: "window-pin"; iconSize: 17; width: 34; height: 34
                                        tooltip: qsTr("Pin as floating preview")
                                        visible: kind === "image" && filePath !== ""
                                        onClicked: App.previewFromHistory(filePath)
                                    }
                                    UIconButton {
                                        iconName: "ocr"; iconSize: 17; width: 34; height: 34
                                        tooltip: qsTr("Copy text (OCR)")
                                        visible: App.ocrAvailable && kind === "image" && filePath !== ""
                                        onClicked: App.ocrFile(filePath)
                                    }
                                    UIconButton {
                                        iconName: "edit-delete"; iconSize: 17; width: 34; height: 34
                                        enabled: !favorite
                                        tooltip: favorite ? qsTr("Starred. Unstar to allow deleting")
                                                          : qsTr("Delete (moves file to trash)")
                                        onClicked: App.history.remove(index)
                                    }
                                }
                            }
                        }

                        Text {
                            width: parent.width
                            text: url !== "" ? url
                                 : filePath !== "" ? filePath.split("/").pop() : qsTr("(not saved)")
                            color: url !== "" ? Theme.accent : Theme.textSecondary
                            font.pixelSize: Theme.fontS
                            elide: Text.ElideMiddle
                        }
                        Text {
                            width: parent.width
                            text: timestamp ? Qt.formatDateTime(timestamp, "yyyy-MM-dd  HH:mm") : ""
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontS - 1
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
