import QtQuick
import Unisic
import "../components"

Item {
    id: page
    // FPS dropdown options (15/30/45/60): snap a stored value to the nearest.
    readonly property var fpsOpts: [15, 30, 45, 60]
    function nearestFps(v) {
        var best = 0, bd = 1e9
        for (var i = 0; i < fpsOpts.length; ++i) {
            var d = Math.abs(fpsOpts[i] - v)
            if (d < bd) { bd = d; best = i }
        }
        return best
    }
    Flickable {
        id: pageFlick
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        contentHeight: col.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        MiddleScroll { flickable: pageFlick }

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
                        UComboBox {
                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            width: 130
                            model: ["15 FPS", "30 FPS", "45 FPS", "60 FPS"]
                            readonly property var opts: [15, 30, 45, 60]
                            currentIndex: page.nearestFps(App.settings.gifFps)
                            onActivated: (i) => App.settings.gifFps = opts[i]
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
                            from: 0; to: 600; step: 5; value: App.settings.gifMaxDurationSec; suffix: " s"
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
