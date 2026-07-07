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
                text: qsTr("Screen recording")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
            }
            Text {
                text: App.recordingAvailable
                      ? qsTr("Record the full screen, a region, or a single window to a video file.")
                      : qsTr("Recording is unavailable: Unisic was built without PipeWire support.")
                color: App.recordingAvailable ? Theme.textSecondary : Theme.danger
                font.pixelSize: Theme.fontM
            }

            Row {
                spacing: Theme.spacingM
                UButton {
                    iconName: "monitor"; text: qsTr("Screen")
                    enabled: App.recordingAvailable && !App.recording && !App.converting
                    onClicked: App.startVideoScreen()
                }
                UButton {
                    iconName: "region"; text: qsTr("Region"); variant: "tonal"
                    enabled: App.recordingAvailable && !App.recording && !App.converting
                    onClicked: App.startVideoRegion()
                }
                UButton {
                    iconName: "window"; text: qsTr("Window"); variant: "tonal"
                    enabled: App.recordingAvailable && !App.recording && !App.converting
                    onClicked: App.startVideoWindow()
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
                        text: qsTr("Video options")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontL
                        font.weight: Font.DemiBold
                    }

                    Item {
                        width: parent.width; height: 40
                        Text {
                            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Format"); color: Theme.textPrimary; font.pixelSize: Theme.fontM
                        }
                        UComboBox {
                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            width: 190
                            model: ["mp4", "webm"]
                            currentIndex: App.settings.videoFormat === "webm" ? 1 : 0
                            onActivated: (i) => App.settings.videoFormat = model[i]
                        }
                    }
                    Item {
                        width: parent.width; height: 40
                        Text {
                            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Frame rate"); color: Theme.textPrimary; font.pixelSize: Theme.fontM
                        }
                        USpinBox {
                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            from: 5; to: 60; value: App.settings.videoFps; suffix: " fps"
                            onChanged: (v) => App.settings.videoFps = v
                        }
                    }
                    Item {
                        width: parent.width; height: 44
                        Text {
                            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Quality (CRF %1 — lower is better)").arg(App.settings.videoQuality)
                            color: Theme.textPrimary; font.pixelSize: Theme.fontM
                        }
                        USlider {
                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            width: 200; from: 0; to: 40
                            value: App.settings.videoQuality
                            onMoved: (v) => App.settings.videoQuality = Math.round(v)
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
                            from: 0; to: 3600; value: App.settings.videoMaxDurationSec; suffix: " s"
                            onChanged: (v) => App.settings.videoMaxDurationSec = v
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
