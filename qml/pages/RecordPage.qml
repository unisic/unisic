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
    // Per-application audio picker model — the same one as Settings → Recording
    // → Audio. pw-dump runs off the GUI thread and returns asynchronously.
    property var appAudioNodes: []
    function refreshAppAudioNodes() { App.requestAudioApplicationNodes() }
    Connections {
        target: App
        function onAudioApplicationNodesReady(nodes) { page.appAudioNodes = nodes }
    }
    // Load once so a previously-saved node shows correctly instead of "Off".
    Component.onCompleted: if (App.perAppAudioAvailable) page.refreshAppAudioNodes()
    readonly property var appAudioIds: [""].concat(appAudioNodes.map(function(n) { return n.id }))
    readonly property var appAudioLabels: [qsTr("Off")].concat(appAudioNodes.map(function(n) { return n.label }))
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
                UButton {
                    iconName: "media-record"
                    text: App.instantReplayActive ? qsTr("Save replay") : qsTr("Start replay")
                    variant: App.instantReplayActive ? "primary" : "tonal"
                    enabled: App.recordingAvailable && (!App.recording || App.instantReplayActive) && !App.converting
                    onClicked: App.instantReplayActive ? App.saveInstantReplay() : App.startInstantReplay()
                }
            }

            // Two side-by-side option cards on a wide window; they wrap to a
            // single stacked column when the viewport is too narrow to hold
            // both (the parent Flickable then scrolls as a last resort). This
            // uses the horizontal room so neither card is tall enough to force
            // a scroll at the default window size.
            Flow {
                id: optsFlow
                width: parent.width
                spacing: Theme.spacingL
                readonly property bool twoCol: width >= 720
                readonly property real cardW: twoCol ? (width - Theme.spacingL) / 2
                                                     : Math.min(width, 694)

                UCard {
                    width: optsFlow.cardW
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
                            UComboBox {
                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                width: 130
                                model: ["15 FPS", "30 FPS", "45 FPS", "60 FPS"]
                                readonly property var opts: [15, 30, 45, 60]
                                currentIndex: page.nearestFps(App.settings.videoFps)
                                onActivated: (i) => App.settings.videoFps = opts[i]
                            }
                        }
                        // Label above, slider full-width below: in a half-width
                        // card the long CRF label and a 200px slider would collide
                        // on one row.
                        Column {
                            width: parent.width
                            spacing: Theme.spacingS
                            Text {
                                text: qsTr("Quality (CRF %1, lower is better)").arg(App.settings.videoQuality)
                                color: Theme.textPrimary; font.pixelSize: Theme.fontM
                            }
                            USlider {
                                width: parent.width; from: 0; to: 40
                                value: App.settings.videoQuality
                                onMoved: (v) => App.settings.videoQuality = Math.round(v)
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
                                values: [0, 10, 30, 60, 120, 300, 600, 1800, 3600]
                                from: 0; to: 3600
                                suffix: " s"
                                tooltip: qsTr("0 = unlimited")
                                value: App.settings.videoMaxDurationSec
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

                UCard {
                    width: optsFlow.cardW
                    Column {
                        width: parent.width
                        spacing: Theme.spacingL

                        Text {
                            text: qsTr("Audio & replay")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontL
                            font.weight: Font.DemiBold
                        }

                        // Quick audio toggles — the same settings as
                        // Settings → Recording → Audio, surfaced where recording starts.
                        Item {
                            width: parent.width; height: 40
                            Text {
                                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Record system audio"); color: Theme.textPrimary; font.pixelSize: Theme.fontM
                            }
                            USwitch {
                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                checked: App.settings.recordSystemAudio
                                onToggled: (c) => App.settings.recordSystemAudio = c
                            }
                        }
                        Item {
                            width: parent.width; height: 40
                            Text {
                                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Record microphone"); color: Theme.textPrimary; font.pixelSize: Theme.fontM
                            }
                            USwitch {
                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                checked: App.settings.recordMicrophone
                                onToggled: (c) => App.settings.recordMicrophone = c
                            }
                        }
                        Item {
                            width: parent.width; height: 40
                            Text {
                                anchors.left: parent.left
                                anchors.right: appAudioRow.left
                                anchors.rightMargin: Theme.spacingM
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Application audio only"); color: Theme.textPrimary
                                font.pixelSize: Theme.fontM; elide: Text.ElideRight
                            }
                            Row {
                                id: appAudioRow
                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.spacingS
                                UComboBox {
                                    width: 140
                                    enabled: App.perAppAudioAvailable
                                    model: page.appAudioLabels
                                    currentIndex: Math.max(0, page.appAudioIds.indexOf(App.settings.recordAppAudioNode))
                                    onActivated: (i) => App.settings.recordAppAudioNode = page.appAudioIds[i]
                                }
                                UButton { compact: true; variant: "tonal"; text: qsTr("Refresh"); enabled: App.perAppAudioAvailable; onClicked: page.refreshAppAudioNodes() }
                            }
                        }
                        Item {
                            width: parent.width; height: 40
                            Text {
                                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Replay length"); color: Theme.textPrimary; font.pixelSize: Theme.fontM
                            }
                            UValueCombo {
                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                width: 130; values: [10, 15, 30, 60, 120, 300, 600]
                                from: 10; to: 600; suffix: " s"
                                value: App.settings.instantReplaySeconds
                                onChanged: (v) => App.settings.instantReplaySeconds = v
                            }
                        }
                    }
                }
            }
        }
    }
}
