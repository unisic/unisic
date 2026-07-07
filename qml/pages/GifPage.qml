import QtQuick
import Unisic
import "../components"

Item {
    Flickable {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        contentHeight: col.height
        clip: true

        Column {
            id: col
            width: parent.width
            spacing: Theme.spacingL

            Text {
                text: qsTr("GIF recording")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
            }
            Text {
                text: App.recordingAvailable
                      ? qsTr("Record a region or a whole screen straight to an optimized .gif.")
                      : qsTr("Recording is unavailable: Unisic was built without PipeWire support.")
                color: App.recordingAvailable ? Theme.textSecondary : Theme.danger
                font.pixelSize: Theme.fontM
            }

            Row {
                spacing: Theme.spacingM
                UButton {
                    iconName: "region"; text: qsTr("Region → GIF")
                    enabled: App.recordingAvailable && !App.recording && !App.converting
                    onClicked: App.startGifRegion()
                }
                UButton {
                    iconName: "monitor"; text: qsTr("Screen → GIF"); variant: "tonal"
                    enabled: App.recordingAvailable && !App.recording && !App.converting
                    onClicked: App.startGifFullScreen()
                }
                UButton {
                    iconName: "stop"; text: qsTr("Stop"); variant: "danger"
                    enabled: App.recording && !App.converting
                    onClicked: App.stopRecording()
                }
            }

            UCard {
                width: Math.min(parent.width, 694)
                Column {
                    width: parent.width
                    spacing: Theme.spacingL

                    Text {
                        text: qsTr("GIF options")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontL
                        font.weight: Font.DemiBold
                    }

                    Item {
                        width: parent.width; height: 40
                        Text {
                            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Frame rate"); color: Theme.textPrimary; font.pixelSize: Theme.fontM
                        }
                        USpinBox {
                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            from: 1; to: 60; value: App.settings.gifFps; suffix: " fps"
                            onChanged: (v) => App.settings.gifFps = v
                        }
                    }
                    Item {
                        width: parent.width; height: 40
                        Text {
                            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Maximum duration (0 = unlimited)"); color: Theme.textPrimary; font.pixelSize: Theme.fontM
                        }
                        USpinBox {
                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            from: 0; to: 600; value: App.settings.gifMaxDurationSec; suffix: " s"
                            onChanged: (v) => App.settings.gifMaxDurationSec = v
                        }
                    }
                    Item {
                        width: parent.width; height: 40
                        Text {
                            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Quality"); color: Theme.textPrimary; font.pixelSize: Theme.fontM
                        }
                        UComboBox {
                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            width: 190
                            model: [qsTr("Fast / small"), qsTr("Balanced"), qsTr("Best")]
                            currentIndex: App.settings.gifQuality
                            onActivated: (i) => App.settings.gifQuality = i
                        }
                    }
                    Item {
                        width: parent.width; height: 40
                        Text {
                            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Include mouse cursor"); color: Theme.textPrimary; font.pixelSize: Theme.fontM
                        }
                        USwitch {
                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            checked: App.settings.includeCursor
                            onToggled: (c) => App.settings.includeCursor = c
                        }
                    }
                }
            }
        }
    }
}
