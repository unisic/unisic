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
        WheelBoost { flickable: pageFlick }

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
                // Same two causes as RecordPage — see the comment there.
                text: App.recordingAvailable
                      ? qsTr("Record a region or a whole screen straight to an optimized .gif.")
                      : App.capPipeWireBuild
                        ? qsTr("Recording is unavailable: this desktop has no ScreenCast portal backend, so nothing can hand Unisic the screen. A running PipeWire process is not enough — the portal is what asks you for permission and opens the stream. Cinnamon, MATE and XFCE ship no such backend yet.")
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
                    iconName: App.recordingPaused ? "play" : "pause"
                    text: App.recordingPaused ? qsTr("Resume") : qsTr("Pause")
                    variant: "tonal"
                    visible: App.recordingCanPause && !App.converting
                    onClicked: App.togglePauseRecording()
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
                            text: qsTr("Maximum duration"); color: Theme.textPrimary; font.pixelSize: Theme.fontM
                        }
                        UValueCombo {
                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            width: 130
                            values: [0, 5, 10, 15, 30, 60, 120, 300, 600]
                            from: 0; to: 600
                            suffix: " s"
                            tooltip: qsTr("0 = unlimited")
                            value: App.settings.gifMaxDurationSec
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
