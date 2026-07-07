import QtQuick
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
                onClicked: App.history.clearAll()
            }
        }

        Text {
            visible: App.history.count === 0
            text: qsTr("Nothing here yet — captures and recordings will appear with thumbnails.")
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

            delegate: Item {
                width: grid.cellWidth
                height: grid.cellHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 8
                    radius: Theme.radiusL
                    color: itemMouse.containsMouse ? Theme.surfaceHi : Theme.surface
                    border.width: 1
                    border.color: Theme.divider
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }

                    Column {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        Rectangle {
                            width: parent.width
                            height: 120
                            radius: Theme.radiusM
                            color: Theme.primary
                            clip: true
                            Image {
                                anchors.fill: parent
                                // encodeURI: '#', '?' or '%' in the data path
                                // would otherwise produce an invalid URL.
                                source: thumbnail !== "" ? "file://" + encodeURI(thumbnail).replace(/[?#]/g, encodeURIComponent) : ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }
                            Rectangle {
                                visible: kind !== "image"
                                anchors.top: parent.top
                                anchors.right: parent.right
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
                        }

                        Text {
                            width: parent.width
                            text: url !== "" ? url
                                 : filePath !== "" ? filePath.split("/").pop() : qsTr("(not saved)")
                            color: url !== "" ? Theme.accent : Theme.textSecondary
                            font.pixelSize: Theme.fontS
                            elide: Text.ElideMiddle
                        }

                        Row {
                            spacing: 4
                            UIconButton {
                                iconName: "edit-copy"; iconSize: 16; tooltip: qsTr("Copy link")
                                width: 30; height: 30
                                enabled: url !== ""
                                onClicked: { App.copyText(url); App.showToast(qsTr("Link copied")) }
                            }
                            UIconButton {
                                iconName: "folder-open"; iconSize: 16; width: 30; height: 30
                                enabled: filePath !== ""
                                onClicked: App.openFile(filePath)
                            }
                            UIconButton {
                                iconName: "ocr"; iconSize: 16; width: 30; height: 30
                                tooltip: qsTr("Copy text (OCR)")
                                visible: App.ocrAvailable && kind === "image" && filePath !== ""
                                onClicked: App.ocrFile(filePath)
                            }
                            UIconButton {
                                iconName: "edit-delete"; iconSize: 16; width: 30; height: 30
                                tooltip: qsTr("Delete (moves file to trash)")
                                onClicked: App.history.remove(index)
                            }
                        }
                    }

                    MouseArea {
                        id: itemMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }
            }
        }
    }
}
