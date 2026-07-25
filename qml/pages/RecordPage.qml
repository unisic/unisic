import QtQuick
import Unisic
import Unisic.Kit
import "../components"

Item {
    id: page

    // Video (0) or GIF (1). Kept in Settings, not in a page-local property:
    // the page Loader is destroyed when the user navigates away, so a local
    // property would snap back to Video on every return.
    readonly property int mode: App.settings.recordPageMode
    readonly property bool gifMode: mode === 1

    // Nothing modal on this page - the flag exists on every page so Main.qml
    // can ask the active one without a per-page special case.
    readonly property bool modalOpen: false

    // FPS dropdown options (15/30/45/60): snap a stored value to the nearest.
    // Both frame-rate dropdowns (video and GIF) resolve through this - it is
    // the merged page's only definition, so deleting it silently feeds
    // `undefined` to currentIndex and every combo shows the wrong entry.
    readonly property var fpsOpts: [15, 30, 45, 60]
    function nearestFps(v) {
        var best = 0, bd = 1e9
        for (var i = 0; i < fpsOpts.length; ++i) {
            var d = Math.abs(fpsOpts[i] - v)
            if (d < bd) { bd = d; best = i }
        }
        return best
    }
    // Per-application audio picker model - the same one as Settings > Recording
    // > Audio. pw-dump runs off the GUI thread and returns asynchronously.
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

        // spacingM between sections, and title + description pulled into one
        // header block: the description now WRAPS (see below), and on a desktop
        // with no ScreenCast backend that is a three-line paragraph. Measured at
        // 1060x700 (572 px of viewport): video 493, video without a window
        // picker 513, recording unavailable 533, GIF 395. Same rhythm as the
        // Capture page - re-measure both modes before adding a row here.
        Column {
            id: col
            width: parent.width
            spacing: Theme.spacingM

            Column {
                width: parent.width
                spacing: 3

                // Title on the left, mode segment on the right of the SAME row: the
                // 28px chips fit inside the title's line box, so the segment costs
                // no vertical space and the page still fits above the fold.
                Item {
                    width: parent.width
                    height: Math.max(titleText.implicitHeight, modeRow.implicitHeight)

                    Text {
                        id: titleText
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: page.gifMode ? qsTr("GIF recording") : qsTr("Screen recording")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.Bold
                    }
                    // Right-anchored so a longer translated title never pushes the
                    // segment sideways - the chips keep their place in both modes.
                    Row {
                        id: modeRow
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacingS
                        UFilterChip {
                            iconName: "media-record"
                            text: qsTr("Video")
                            checked: !page.gifMode
                            accessibleDescription: qsTr("Record to an MP4 or WebM file")
                            onClicked: App.settings.recordPageMode = 0
                        }
                        UFilterChip {
                            iconName: "gif"
                            text: qsTr("GIF")
                            checked: page.gifMode
                            accessibleDescription: qsTr("Record to an animated GIF")
                            onClicked: App.settings.recordPageMode = 1
                        }
                    }
                }
                Text {
                    // Width + wrap are load-bearing: the long variants (no
                    // ScreenCast backend, no window picker) measure well past the
                    // 758 px viewport, and the Flickable clips - they were silently
                    // cut off mid-sentence instead of wrapping.
                    width: parent.width
                    wrapMode: Text.WordWrap
                    // Two unrelated causes, named apart: a package built without
                    // PipeWire vs. a desktop with no ScreenCast portal backend (a
                    // running pipewire daemon does not imply one - it serves audio).
                    text: App.recordingAvailable
                          ? (page.gifMode
                             ? qsTr("Record a region or a whole screen straight to an optimized .gif.")
                             : (App.capRecordWindowSource
                                ? qsTr("Record the full screen, a region, or a single window to a video file.")
                                : qsTr("Record the full screen or a region to a video file. Recording a single window needs a window picker this desktop does not provide, so that source stays unavailable here.")))
                          : App.capPipeWireBuild
                            ? qsTr("Recording is unavailable: this desktop has no ScreenCast portal backend, so nothing can hand Unisic the screen. A running PipeWire process is not enough - the portal is what asks you for permission and opens the stream. Cinnamon, MATE and XFCE ship no such backend yet.")
                            : qsTr("Recording is unavailable: Unisic was built without PipeWire support.")
                    color: App.recordingAvailable ? Theme.textSecondary : Theme.danger
                    font.pixelSize: Theme.fontM
                }
            }

            // Every button is always present and holds its place - states only
            // enable/disable them, so nothing jumps into (or out of) the row
            // while a recording starts or stops, or when the mode flips. The
            // labels stay put too; only the action behind them and the colour
            // emphasis follow the mode. Flow: wraps at the minimum window width
            // instead of clipping.
            Flow {
                width: parent.width
                spacing: Theme.spacingM
                // The labels deliberately do NOT change with the mode (swapping
                // them would reflow the row), so what the button will actually
                // produce lives in the spoken description instead - that is the
                // one place it can change for free.
                UButton {
                    compact: true; iconName: "monitor"; text: qsTr("Screen")
                    // Colour-only emphasis swap (no size change): video leads
                    // with the whole screen, GIF leads with a region.
                    variant: page.gifMode ? "tonal" : "filled"
                    enabled: App.recordingAvailable && !App.recording && !App.converting
                    accessibleDescription: page.gifMode ? qsTr("Record a whole screen to a GIF")
                                                        : qsTr("Record a whole screen to a video")
                    onClicked: page.gifMode ? App.startGifFullScreen() : App.startVideoScreen()
                }
                UButton {
                    compact: true; iconName: "region"; text: qsTr("Region")
                    variant: page.gifMode ? "filled" : "tonal"
                    enabled: App.recordingAvailable && !App.recording && !App.converting
                    accessibleDescription: page.gifMode ? qsTr("Record a selected region to a GIF")
                                                        : qsTr("Record a selected region to a video")
                    onClicked: page.gifMode ? App.startGifRegion() : App.startVideoRegion()
                }
                UButton {
                    compact: true; iconName: "window"; text: qsTr("Window"); variant: "tonal"
                    // Needs the portal/KWin window picker; the X11 backend has
                    // no window source (it grabs a monitor rect). GIF has no
                    // window source at all, so the button only disables.
                    enabled: App.recordingAvailable && !page.gifMode && App.capRecordWindowSource
                             && !App.recording && !App.converting
                    accessibleDescription: page.gifMode
                                           ? qsTr("Unavailable: GIF has no single-window source")
                                           : qsTr("Record a single window to a video")
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
                    // Instant replay always writes a video, in both modes: this
                    // is its only in-window entry point, so disabling it in GIF
                    // mode would just hide the feature.
                    compact: true; iconName: "media-record"
                    text: App.instantReplayActive ? qsTr("Save replay") : qsTr("Start replay")
                    variant: App.instantReplayActive ? "primary" : "tonal"
                    enabled: App.recordingAvailable && (!App.recording || App.instantReplayActive) && !App.converting
                    accessibleDescription: qsTr("Instant replay always saves a video, never a GIF")
                    onClicked: App.instantReplayActive ? App.saveInstantReplay() : App.startInstantReplay()
                }
            }

            // Two option columns side by side on a wide window, stacked on a
            // narrow one. Each option is its own bordered card (the Settings
            // visual language) on the flat background - no outer box, and the
            // whole page shares one full-width grid. The mode swaps whole
            // column bodies, never single rows, so no control ever appears or
            // disappears inside a row that stays.
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
                        text: page.gifMode ? qsTr("GIF options") : qsTr("Video options")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontL
                        font.weight: Font.Bold
                        bottomPadding: Theme.spacingXS
                    }

                    Column { // video-only options
                        width: parent.width
                        spacing: Theme.spacingS
                        visible: !page.gifMode

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
                    }

                    Column { // GIF-only options
                        width: parent.width
                        spacing: Theme.spacingS
                        visible: page.gifMode

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
                        // Shorter ceiling than video on purpose: a 10-minute GIF
                        // is not a thing anyone wants to hand around.
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
                    }

                    // One row, one setting: the cursor toggle is shared by both
                    // modes (Settings::includeCursor), so it lives outside the
                    // swap instead of being duplicated into each branch.
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
                        text: page.gifMode ? qsTr("Good to know") : qsTr("Audio & replay")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontL
                        font.weight: Font.Bold
                        bottomPadding: Theme.spacingXS
                    }

                    Column { // video-only: audio + replay
                        width: parent.width
                        spacing: Theme.spacingS
                        visible: !page.gifMode

                        // Quick audio toggles - the same settings as
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

                    Rectangle { // GIF-only: tips
                        width: parent.width
                        visible: page.gifMode
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
                                text: qsTr("GIF has no audio track. For a clip with sound, switch to Video mode and record an MP4 or WebM instead.")
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
                            Text {
                                width: parent.width
                                wrapMode: Text.WordWrap
                                text: qsTr("Instant replay always saves a video, never a GIF - its length and audio options live in Video mode.")
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
