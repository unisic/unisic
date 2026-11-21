import QtQuick
import Unisic
import Unisic.Kit
import "../components"

// Atelier layout, the Capture page's: what to record on the left, how it is
// recorded in an inspector column on the right. The inspector
// (components/Inspector.qml) is one of the sanctioned exceptions to "one
// full-width grid per page" (docs/dev/ui-kit.md), with the Capture and Edit
// pages': it holds settings, not content, and keeps them in view while the
// tiles change.
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

    // Same rule as the Capture page, for three tiles instead of four.
    readonly property bool compact: height < 760 || width - aside.width - 2 * 40 < 3 * 200 + 2 * 14
    // An automatic replay ring gives way to a recording, so it does not lock the tiles.
    readonly property bool idle: App.recordingAvailable && (!App.recording || App.autoReplayActive) && !App.converting

    signal openPage(int index)
    signal openSettings(int tab, string query)

    // FPS segments (15/30/45/60): snap a stored value to the nearest.
    // Both frame-rate controls (video and GIF) resolve through this - it is
    // the merged page's only definition, so deleting it silently feeds
    // `undefined` to currentIndex and every control shows the wrong entry.
    readonly property var fpsOpts: [15, 30, 45, 60]
    readonly property var fpsLabels: fpsOpts.map((v) => v + " FPS")
    function nearestFps(v) {
        var best = 0, bd = 1e9
        for (var i = 0; i < fpsOpts.length; ++i) {
            var d = Math.abs(fpsOpts[i] - v)
            if (d < bd) { bd = d; best = i }
        }
        return best
    }

    // shortcut: one kind per mode, so Video mode lists recordings and not the
    // instant-replay clips (HistoryFilterModel matches a single category);
    // widen the filter if clips should show here too.
    HistoryFilterModel {
        id: recentModel
        sourceModel: App.history
        kindFilter: page.gifMode ? "gif" : "video"
    }

    // ------------------------------------------------------------------ main --

    Flickable {
        id: pageFlick
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: aside.left
        contentHeight: col.height + 2 * col.y
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        MiddleScroll { flickable: pageFlick }
        WheelBoost { flickable: pageFlick }

        Column {
            id: col
            x: 40
            y: page.compact ? 20 : 28
            // Clamped for the Recent GridView, as on the Capture page.
            width: Math.max(0, pageFlick.width - 2 * 40)
            spacing: page.compact ? 18 : 30

            Item {
                width: parent.width
                height: Math.max(titleCol.height, modeSeg.height)

                Column {
                    id: titleCol
                    anchors.left: parent.left
                    anchors.right: modeSeg.left
                    anchors.rightMargin: 24
                    spacing: 6
                    PageTitle {
                        width: parent.width
                        text: page.gifMode ? qsTr("GIF recording") : qsTr("Screen recording")
                    }
                    Text {
                        // Width + wrap are load-bearing: the long variants (no
                        // ScreenCast backend, no window picker) run to several
                        // lines and were cut off mid-sentence without them.
                        width: parent.width
                        wrapMode: Text.WordWrap
                        // PipeWire is a hard build requirement, so there is only one
                        // cause left to name: a desktop with no ScreenCast portal
                        // backend (a running pipewire daemon does not imply one - it
                        // serves audio).
                        // The line names the tiles that START a recording. Read
                        // as a description, the source row looked like tabs and
                        // the Video segment like the start button (#134).
                        text: App.recordingAvailable
                              ? (page.gifMode
                                 ? qsTr("Press Screen or Region to start recording an optimized .gif.")
                                 : (App.capRecordWindowSource
                                    ? qsTr("Press Screen, Region or Window to start recording to a video file.")
                                    : qsTr("Press Screen or Region to start recording to a video file. Recording a single window needs a window picker this desktop does not provide, so that source stays unavailable here.")))
                              : qsTr("Recording is unavailable: this desktop has no ScreenCast portal backend, so nothing can hand Unisic the screen. A running PipeWire process is not enough - the portal is what asks you for permission and opens the stream. Cinnamon, MATE and XFCE ship no such backend yet.")
                        color: App.recordingAvailable ? Theme.textSecondary : Theme.danger
                        font.pixelSize: 13
                    }
                }

                // Right-anchored so a longer translated title never pushes the
                // segment sideways. A segment, not a record-dot chip: the dot is
                // the "start" sign, and on the mode switch it read as the button
                // that begins recording - pressing the already-selected Video
                // chip did nothing, so recording "failed silently" (#134).
                SegmentedControl {
                    id: modeSeg
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 2
                    width: 180
                    accessibleName: qsTr("Recording mode")
                    labels: [qsTr("Video"), qsTr("GIF")]
                    descriptions: [qsTr("Record to an MP4 or WebM file"), qsTr("Record to an animated GIF")]
                    currentIndex: page.mode
                    onPicked: (i) => App.settings.recordPageMode = i
                }
            }

            Column {
                width: parent.width
                spacing: 12

                SectionLabel { text: qsTr("Source") }

                // Every tile is always present and holds its place - states only
                // enable/disable them, so nothing jumps into (or out of) the row
                // while a recording starts or stops, or when the mode flips. The
                // titles stay put too; only the action behind them and the line
                // under the title follow the mode.
                Flow {
                    id: sourceFlow
                    objectName: "recordSourceFlow"
                    width: parent.width
                    spacing: 14
                    readonly property int minCard: page.compact ? 130 : 200
                    readonly property int perRow: width >= 3 * minCard + 2 * spacing ? 3 : 1
                    readonly property real cardW: Math.floor((width - (perRow - 1) * spacing) / perRow)

                    SourceTile {
                        width: sourceFlow.cardW
                        compact: page.compact
                        art: fullRecArt
                        title: qsTr("Screen")
                        sub: page.gifMode ? qsTr("Record a whole screen to a GIF")
                                          : qsTr("Record a whole screen to a video")
                        hotkey: page.gifMode ? App.settings.hotkeyGifScreen : App.settings.hotkeyRecordScreen
                        enabled: page.idle
                        onOpenHotkeys: page.openSettings(-1, page.gifMode ? qsTranslate("SettingsPage", "GIF of the full screen")
                                                                          : qsTranslate("SettingsPage", "Video of the full screen"))
                        onActivated: page.gifMode ? App.startGifFullScreen() : App.startVideoScreen()
                    }
                    SourceTile {
                        width: sourceFlow.cardW
                        compact: page.compact
                        art: regionRecArt
                        title: qsTr("Region")
                        sub: page.gifMode ? qsTr("Record a selected region to a GIF")
                                          : qsTr("Record a selected region to a video")
                        // The record-gif / record-video actions start a region.
                        hotkey: page.gifMode ? App.settings.hotkeyGif : App.settings.hotkeyRecord
                        enabled: page.idle
                        onOpenHotkeys: page.openSettings(8, "")
                        onActivated: page.gifMode ? App.startGifRegion() : App.startVideoRegion()
                    }
                    SourceTile {
                        width: sourceFlow.cardW
                        compact: page.compact
                        art: windowRecArt
                        title: qsTr("Window")
                        // Needs the portal/KWin window picker; the X11 backend has
                        // no window source (it grabs a monitor rect). GIF has no
                        // window source at all, so the tile only disables, and
                        // the line under the title says why.
                        sub: page.gifMode ? qsTr("Unavailable: GIF has no single-window source")
                             : !App.capRecordWindowSource ? qsTr("Unavailable: this desktop has no window picker")
                             : qsTr("Record a single window to a video")
                        hotkey: page.gifMode ? "" : App.settings.hotkeyRecordWindow
                        bindable: !page.gifMode && App.capRecordWindowSource
                        enabled: page.idle && !page.gifMode && App.capRecordWindowSource
                        onOpenHotkeys: page.openSettings(-1, qsTranslate("SettingsPage", "Video of a window"))
                        onActivated: App.startVideoWindow()
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 12

                SectionLabel { text: qsTr("While recording") }

                Grid {
                    id: controlGrid
                    width: parent.width
                    columns: 3
                    columnSpacing: 14
                    rowSpacing: 10
                    readonly property real cellW: Math.floor((width - 2 * columnSpacing) / 3)

                    ActionTile {
                        width: controlGrid.cellW
                        compact: page.compact
                        iconName: App.recordingPaused ? "play" : "pause"
                        title: App.recordingPaused ? qsTr("Resume") : qsTr("Pause")
                        sub: App.recordingPaused ? qsTr("Paused")
                             : App.recording ? qsTr("Recording") : qsTr("Not recording")
                        enabled: App.recordingCanPause && !App.converting
                        hotkey: App.settings.hotkeyPauseRecording
                        onActivated: App.togglePauseRecording()
                    }
                    ActionTile {
                        width: controlGrid.cellW
                        compact: page.compact
                        iconName: "stop"
                        iconColor: Theme.danger
                        title: qsTr("Stop")
                        sub: qsTr("Save the file")
                        enabled: App.recording && !App.converting
                        onActivated: App.stopRecording()
                    }
                    ActionTile {
                        // Instant replay always writes a video, in both modes: this
                        // is its only in-window entry point, so disabling it in GIF
                        // mode would just hide the feature.
                        width: controlGrid.cellW
                        compact: page.compact
                        iconName: "media-record"
                        iconColor: App.instantReplayActive ? Theme.recDot : Theme.accent
                        title: App.instantReplayActive ? qsTr("Save replay") : qsTr("Start replay")
                        //: Instant replay tile; %1 is the replay length in seconds.
                        sub: qsTr("Last %1 s, always a video").arg(App.settings.instantReplaySeconds)
                        hotkey: App.settings.hotkeyInstantReplay
                        accessibleDescription: qsTr("Instant replay always saves a video, never a GIF")
                        enabled: App.recordingAvailable && (!App.recording || App.instantReplayActive) && !App.converting
                        onActivated: App.instantReplayActive ? App.saveInstantReplay() : App.startInstantReplay()
                    }
                }
            }

            RecentStrip {
                id: recentSection
                width: parent.width
                model: recentModel
                gridName: "recordRecentGrid"
                available: pageFlick.height - 2 * col.y - recentSection.y
                listName: qsTr("Recent recordings")
                emptyText: page.gifMode ? qsTr("Your GIFs will show up here.")
                                        : qsTr("Your recordings will show up here.")
                onOpenHistory: page.openPage(3)
            }
        }
    }

    // ------------------------------------------------------------- inspector --

    Inspector {
        id: aside
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        flickName: "recordAside"
        accessibleName: qsTr("Recording options")

        // The mode swaps whole section bodies, never single rows, so no
        // control appears or disappears inside a group that stays.
        Column {
            x: 20
            width: parent.width - 40
            topPadding: page.compact ? 14 : 22
            bottomPadding: page.compact ? 12 : 18
            spacing: page.compact ? 10 : 14

            SectionLabel {
                text: page.gifMode ? qsTr("GIF options") : qsTr("Video options")
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            Column { // video-only options
                width: parent.width
                spacing: page.compact ? 10 : 14
                visible: !page.gifMode

                // Inline, unlike the frame rate: two segments fit beside the
                // label, and the row it saves keeps Video mode's column above
                // the fold at 1060x700.
                InspectorRow {
                    id: formatRow
                    label: qsTr("Format")
                    SegmentedControl {
                        readonly property var formats: ["mp4", "webm"]
                        width: 140
                        accessibleName: formatRow.label
                        labels: ["MP4", "WebM"]
                        currentIndex: App.settings.videoFormat === "webm" ? 1 : 0
                        onPicked: (i) => App.settings.videoFormat = formats[i]
                    }
                }
                Column {
                    width: parent.width
                    spacing: 8
                    Text {
                        id: videoFpsLabel
                        text: qsTr("Frame rate")
                        color: Theme.textPrimary
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        Accessible.ignored: true
                    }
                    SegmentedControl {
                        width: parent.width
                        accessibleName: videoFpsLabel.text
                        labels: page.fpsLabels
                        currentIndex: page.nearestFps(App.settings.videoFps)
                        onPicked: (i) => App.settings.videoFps = page.fpsOpts[i]
                    }
                }
                // Percent, not the raw CRF: higher reads as better here,
                // which is the way round everyone expects, and the
                // recorder derives the CRF from it.
                InspectorRow {
                    id: qualityRow
                    label: qsTr("Quality")
                    sub: qsTr("Higher is sharper and bigger. 50% is the balanced default.")
                    compact: page.compact
                    UValueCombo {
                        width: 104
                        values: [25, 40, 50, 60, 75, 90, 100]
                        from: 0; to: 100
                        suffix: "%"
                        accessibleName: qualityRow.label
                        accessibleDescription: qualityRow.sub
                        value: App.settings.videoQualityPercent
                        onChanged: (v) => App.settings.videoQualityPercent = v
                    }
                }
                InspectorRow {
                    id: videoMaxRow
                    label: qsTr("Maximum duration")
                    UValueCombo {
                        width: 104
                        values: [0, 10, 30, 60, 120, 300, 600, 1800, 3600]
                        from: 0; to: 3600
                        suffix: " s"
                        tooltip: qsTr("0 = unlimited")
                        accessibleName: videoMaxRow.label
                        value: App.settings.videoMaxDurationSec
                        onChanged: (v) => App.settings.videoMaxDurationSec = v
                    }
                }
            }

            Column { // GIF-only options
                width: parent.width
                spacing: page.compact ? 10 : 14
                visible: page.gifMode

                Column {
                    width: parent.width
                    spacing: 8
                    Text {
                        id: gifFpsLabel
                        text: qsTr("Frame rate")
                        color: Theme.textPrimary
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        Accessible.ignored: true
                    }
                    SegmentedControl {
                        width: parent.width
                        accessibleName: gifFpsLabel.text
                        labels: page.fpsLabels
                        currentIndex: page.nearestFps(App.settings.gifFps)
                        onPicked: (i) => App.settings.gifFps = page.fpsOpts[i]
                    }
                }
                // A list, not segments: a third of the column cut "Fast /
                // small" and "Balanced" short in every language but English.
                InspectorRow {
                    id: gifQualityRow
                    label: qsTr("Quality")
                    UComboBox {
                        width: 150
                        accessibleName: gifQualityRow.label
                        model: [qsTr("Fast / small"), qsTr("Balanced"), qsTr("Best")]
                        currentIndex: App.settings.gifQuality
                        onActivated: (i) => App.settings.gifQuality = i
                    }
                }
                // Shorter ceiling than video on purpose: a 10-minute GIF
                // is not a thing anyone wants to hand around.
                InspectorRow {
                    id: gifMaxRow
                    label: qsTr("Maximum duration")
                    UValueCombo {
                        width: 104
                        values: [0, 5, 10, 15, 30, 60, 120, 300, 600]
                        from: 0; to: 600
                        suffix: " s"
                        tooltip: qsTr("0 = unlimited")
                        accessibleName: gifMaxRow.label
                        value: App.settings.gifMaxDurationSec
                        onChanged: (v) => App.settings.gifMaxDurationSec = v
                    }
                }
            }

            // One row, one setting: the cursor toggle is shared by both
            // modes (Settings::includeCursor), so it lives outside the
            // swap instead of being duplicated into each branch.
            InspectorRow {
                id: cursorRow
                label: qsTr("Include mouse cursor")
                MiniSwitch {
                    checked: App.settings.includeCursor
                    accessibleName: cursorRow.label
                    onToggled: (c) => App.settings.includeCursor = c
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.alpha(Theme.accent, 0.10) }

        Column {
            x: 20
            width: parent.width - 40
            topPadding: page.compact ? 12 : 18
            bottomPadding: page.compact ? 12 : 18
            spacing: page.compact ? 10 : 14

            SectionLabel { text: page.gifMode ? qsTr("Good to know") : qsTr("Audio & replay") }

            Column { // video-only: audio + replay
                width: parent.width
                spacing: page.compact ? 10 : 14
                visible: !page.gifMode

                // Quick audio toggles - the same settings as
                // Settings → Recording → Audio, surfaced where recording starts.
                InspectorRow {
                    id: systemAudioRow
                    label: qsTr("Record system audio")
                    MiniSwitch {
                        checked: App.settings.recordSystemAudio
                        accessibleName: systemAudioRow.label
                        onToggled: (c) => App.settings.recordSystemAudio = c
                    }
                }
                InspectorRow {
                    id: micRow
                    label: qsTr("Record microphone")
                    MiniSwitch {
                        checked: App.settings.recordMicrophone
                        accessibleName: micRow.label
                        onToggled: (c) => App.settings.recordMicrophone = c
                    }
                }
                Column {
                    width: parent.width
                    spacing: 8
                    Text {
                        id: appAudioLabel
                        text: qsTr("Application audio only")
                        color: Theme.textPrimary
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        Accessible.ignored: true
                    }
                    // Polls pw-dump only while visible, so GIF mode (this
                    // column hidden) stops it. The picker names no group of
                    // its own; the caption above becomes that name.
                    UAppAudioPicker {
                        Accessible.role: Accessible.Grouping
                        Accessible.name: appAudioLabel.text
                    }
                }
                InspectorRow {
                    id: tracksRow
                    label: qsTr("Separate audio tracks")
                    sub: qsTr("Keeps every source on its own named track for editing, behind a first track with the full mix. Needs two sources or more, otherwise the recording has one track anyway.")
                    compact: page.compact
                    MiniSwitch {
                        checked: App.settings.separateAudioTracks
                        accessibleName: tracksRow.label
                        accessibleDescription: tracksRow.sub
                        onToggled: (c) => App.settings.separateAudioTracks = c
                    }
                }
                InspectorRow {
                    id: replayRow
                    label: qsTr("Replay length")
                    UValueCombo {
                        width: 104; values: [10, 15, 30, 60, 120, 300, 600, 1800, 3600]
                        from: 10; to: 3600; suffix: " s"
                        accessibleName: replayRow.label
                        value: App.settings.instantReplaySeconds
                        onChanged: (v) => App.settings.instantReplaySeconds = v
                    }
                }
            }

            Column { // GIF-only: tips
                width: parent.width
                spacing: Theme.spacingS
                visible: page.gifMode
                Repeater {
                    model: [
                        qsTr("GIF has no audio track. For a clip with sound, switch to Video mode and record an MP4 or WebM instead."),
                        qsTr("File size grows quickly with area, frame rate and duration. A small region at 15-30 FPS usually looks great and stays easy to share."),
                        qsTr("Every recording is converted in two passes (a color palette first, then the frames), so colors stay crisp - the trade-off is a short encode after you stop."),
                        qsTr("Instant replay always saves a video, never a GIF - its length and audio options live in Video mode.")
                    ]
                    delegate: Text {
                        required property string modelData
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: modelData
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontS
                    }
                }
            }
        }
    }

    Component { id: fullRecArt;   TileArt.FullScreenArt { rec: true } }
    Component { id: regionRecArt; TileArt.RegionArt { rec: true } }
    Component { id: windowRecArt; TileArt.WindowArt { rec: true } }
}
