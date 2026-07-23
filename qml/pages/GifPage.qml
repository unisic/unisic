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
                        ? qsTr("Recording is unavailable: this desktop has no ScreenCast portal backend, so nothing can hand Unisic the screen. A running PipeWire process is not enough - the portal is what asks you for permission and opens the stream. Cinnamon, MATE and XFCE ship no such backend yet.")
                        : qsTr("Recording is unavailable: Unisic was built without PipeWire support.")
                color: App.recordingAvailable ? Theme.textSecondary : Theme.danger
                font.pixelSize: Theme.fontM
            }

            // Always-present buttons that only enable/disable — nothing joins
            // or leaves the row while a recording starts or stops.
            Flow {
                width: parent.width
                spacing: Theme.spacingM
                UButton {
                    compact: true; iconName: "region"; text: qsTr("Region → GIF")
                    enabled: App.recordingAvailable && !App.recording && !App.converting
                    onClicked: App.startGifRegion()
                }
                UButton {
                    compact: true; iconName: "monitor"; text: qsTr("Screen → GIF"); variant: "tonal"
                    enabled: App.recordingAvailable && !App.recording && !App.converting
                    onClicked: App.startGifFullScreen()
                }
                UButton {
                    compact: true; iconName: App.recordingPaused ? "play" : "pause"
                    text: App.recordingPaused ? qsTr("Resume") : qsTr("Pause")
                    variant: "tonal"
                    enabled: App.recordingCanPause && !App.converting
                    onClicked: App.togglePauseRecording()
                }
                UButton {
                    compact: true; iconName: "stop"; text: qsTr("Stop"); variant: "danger"
                    enabled: App.recording && !App.converting
                    onClicked: App.stopRecording()
                }
            }

            // Options as per-option cards next to a tips column — one shared
            // full-width grid, same breakpoint as the Record page.
            Flow {
                id: optsFlow
                width: parent.width
                spacing: Theme.spacingL
                readonly property bool twoCol: width >= 720
                readonly property real cardW: twoCol ? (width - Theme.spacingL) / 2 : width

                Column {
                    width: optsFlow.cardW
                    spacing: Theme.spacingS

                    Text {
                        text: qsTr("GIF options")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontL
                        font.weight: Font.Bold
                        bottomPadding: Theme.spacingXS
                    }

                    USettingRow {
                        label: qsTr("Frame rate")
                        UComboBox {
                            width: 130
                            model: ["15 FPS", "30 FPS", "45 FPS", "60 FPS"]
                            readonly property var opts: [15, 30, 45, 60]
                            currentIndex: page.nearestFps(App.settings.gifFps)
                            onActivated: (i) => App.settings.gifFps = opts[i]
                        }
                    }
                    USettingRow {
                        label: qsTr("Maximum duration")
                        UValueCombo {
                            width: 130
                            values: [0, 5, 10, 15, 30, 60, 120, 300, 600]
                            from: 0; to: 600
                            suffix: " s"
                            tooltip: qsTr("0 = unlimited")
                            value: App.settings.gifMaxDurationSec
                            onChanged: (v) => App.settings.gifMaxDurationSec = v
                        }
                    }
                    USettingRow {
                        label: qsTr("Quality")
                        UComboBox {
                            width: 190
                            model: [qsTr("Fast / small"), qsTr("Balanced"), qsTr("Best")]
                            currentIndex: App.settings.gifQuality
                            onActivated: (i) => App.settings.gifQuality = i
                        }
                    }
                    USettingRow {
                        label: qsTr("Include mouse cursor")
                        USwitch {
                            checked: App.settings.includeCursor
                            onToggled: (c) => App.settings.includeCursor = c
                        }
                    }
                }

                Column {
                    width: optsFlow.cardW
                    spacing: Theme.spacingS

                    Text {
                        text: qsTr("Good to know")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontL
                        font.weight: Font.Bold
                        bottomPadding: Theme.spacingXS
                    }

                    Rectangle {
                        width: parent.width
                        implicitHeight: tipsCol.implicitHeight + 2 * Theme.spacingM
                        radius: Theme.radiusM
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.divider
                        Column {
                            id: tipsCol
                            x: Theme.spacingM
                            y: Theme.spacingM
                            width: parent.width - 2 * Theme.spacingM
                            spacing: Theme.spacingS
                            Text {
                                width: parent.width
                                wrapMode: Text.WordWrap
                                text: qsTr("GIF has no audio track. For a clip with sound, record an MP4 or WebM from the Record page instead.")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontS
                            }
                            Text {
                                width: parent.width
                                wrapMode: Text.WordWrap
                                text: qsTr("File size grows quickly with area, frame rate and duration. A small region at 15-30 FPS usually looks great and stays easy to share.")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontS
                            }
                            Text {
                                width: parent.width
                                wrapMode: Text.WordWrap
                                text: qsTr("Every recording is converted in two passes (a color palette first, then the frames), so colors stay crisp - the trade-off is a short encode after you stop.")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontS
                            }
                        }
                    }
                }
            }
        }
    }
}
