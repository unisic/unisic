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
                // Two unrelated causes, named apart: a package built without
                // PipeWire vs. a desktop with no ScreenCast portal backend (a
                // running pipewire daemon does not imply one — it serves audio).
                text: App.recordingAvailable
                      ? qsTr("Record the full screen, a region, or a single window to a video file.")
                      : App.capPipeWireBuild
                        ? qsTr("Recording is unavailable: this desktop has no ScreenCast portal backend, so nothing can hand Unisic the screen. A running PipeWire process is not enough - the portal is what asks you for permission and opens the stream. Cinnamon, MATE and XFCE ship no such backend yet.")
                        : qsTr("Recording is unavailable: Unisic was built without PipeWire support.")
                color: App.recordingAvailable ? Theme.textSecondary : Theme.danger
                font.pixelSize: Theme.fontM
            }

            // Every button is always present and holds its place — states only
            // enable/disable them, so nothing jumps into (or out of) the row
            // while a recording starts or stops. Flow: wraps at the minimum
            // window width instead of clipping.
            Flow {
                width: parent.width
                spacing: Theme.spacingM
                UButton {
                    compact: true; iconName: "monitor"; text: qsTr("Screen")
                    enabled: App.recordingAvailable && !App.recording && !App.converting
                    onClicked: App.startVideoScreen()
                }
                UButton {
                    compact: true; iconName: "region"; text: qsTr("Region"); variant: "tonal"
                    enabled: App.recordingAvailable && !App.recording && !App.converting
                    onClicked: App.startVideoRegion()
                }
                UButton {
                    compact: true; iconName: "window"; text: qsTr("Window"); variant: "tonal"
                    enabled: App.recordingAvailable && !App.recording && !App.converting
                    onClicked: App.startVideoWindow()
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
                UButton {
                    compact: true; iconName: "media-record"
                    text: App.instantReplayActive ? qsTr("Save replay") : qsTr("Start replay")
                    variant: App.instantReplayActive ? "primary" : "tonal"
                    enabled: App.recordingAvailable && (!App.recording || App.instantReplayActive) && !App.converting
                    onClicked: App.instantReplayActive ? App.saveInstantReplay() : App.startInstantReplay()
                }
            }

            // Two option columns side by side on a wide window, stacked on a
            // narrow one. Each option is its own bordered card (the Settings
            // visual language) on the flat background — no outer box, and the
            // whole page shares one full-width grid.
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
                        text: qsTr("Video options")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontL
                        font.weight: Font.Bold
                        bottomPadding: Theme.spacingXS
                    }

                    USettingRow {
                        label: qsTr("Format")
                        UComboBox {
                            width: 190
                            model: ["mp4", "webm"]
                            currentIndex: App.settings.videoFormat === "webm" ? 1 : 0
                            onActivated: (i) => App.settings.videoFormat = model[i]
                        }
                    }
                    USettingRow {
                        label: qsTr("Frame rate")
                        UComboBox {
                            width: 130
                            model: ["15 FPS", "30 FPS", "45 FPS", "60 FPS"]
                            readonly property var opts: [15, 30, 45, 60]
                            currentIndex: page.nearestFps(App.settings.videoFps)
                            onActivated: (i) => App.settings.videoFps = opts[i]
                        }
                    }
                    // Label on the head row, slider full-width in the footer:
                    // the long CRF label and a wide slider would collide on one
                    // row in a half-width column.
                    USettingRow {
                        label: qsTr("Quality (CRF %1, lower is better)").arg(App.settings.videoQuality)
                        footer: USlider {
                            width: parent ? parent.width : 0
                            from: 0; to: 40
                            value: App.settings.videoQuality
                            onMoved: (v) => App.settings.videoQuality = Math.round(v)
                        }
                    }
                    USettingRow {
                        label: qsTr("Maximum duration")
                        UValueCombo {
                            width: 130
                            values: [0, 10, 30, 60, 120, 300, 600, 1800, 3600]
                            from: 0; to: 3600
                            suffix: " s"
                            tooltip: qsTr("0 = unlimited")
                            value: App.settings.videoMaxDurationSec
                            onChanged: (v) => App.settings.videoMaxDurationSec = v
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
                        text: qsTr("Audio & replay")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontL
                        font.weight: Font.Bold
                        bottomPadding: Theme.spacingXS
                    }

                    // Quick audio toggles — the same settings as
                    // Settings → Recording → Audio, surfaced where recording starts.
                    USettingRow {
                        label: qsTr("Record system audio")
                        USwitch {
                            checked: App.settings.recordSystemAudio
                            onToggled: (c) => App.settings.recordSystemAudio = c
                        }
                    }
                    USettingRow {
                        label: qsTr("Record microphone")
                        USwitch {
                            checked: App.settings.recordMicrophone
                            onToggled: (c) => App.settings.recordMicrophone = c
                        }
                    }
                    USettingRow {
                        label: qsTr("Application audio only")
                        Row {
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
                    USettingRow {
                        label: qsTr("Replay length")
                        UValueCombo {
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
