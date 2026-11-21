import QtQuick
import QtQuick.Window
import Unisic
import Unisic.Kit
import "components"

Window {
    id: trimWindow
    width: 1100; height: 720
    minimumWidth: 960; minimumHeight: 600
    visible: true
    title: qsTr("Trim recording")
    color: Theme.primary
    // Same decoration policy as the main and editor windows: the stylized
    // frameless title bar unless the user opted into system decorations.
    flags: App.settings.useSystemDecoration
           ? Qt.Window
           : (Qt.Window | Qt.FramelessWindowHint)
    // The editor's chrome: a 44 px header (kept with system decorations, only
    // its window buttons and drag go), a 320 px settings column on the right.
    readonly property int topH: 44
    readonly property int inspectorWidth: 320
    readonly property color hairline: Theme.alpha(Theme.accent, 0.10)

    // Packages require the QtMultimedia QML module for video preview. Keep the
    // slider fallback so a hand-altered or broken install still opens the file.
    readonly property bool hasPreview: App.capVideoPlayback

    // Whole timeline is expressed in seconds. Duration prefers the player's own
    // (exact, once loaded); until then the ffprobe value passed from C++.
    readonly property real duration: (hasPreview && previewLoader.item
                                      && previewLoader.item.duration > 0)
                                     ? previewLoader.item.duration / 1000 : trimDuration
    readonly property real playhead: (hasPreview && previewLoader.item)
                                     ? previewLoader.item.position / 1000 : 0
    property real trimStart: 0
    property real trimEnd: trimDuration
    // ffprobe's container length can run past the player's own: an out-point
    // beyond the last frame never reaches the loop check, so playback ran off
    // the end of the file instead of looping.
    onDurationChanged: {
        if (duration > 0 && trimEnd > duration)
            trimEnd = duration
        if (viewLen <= 0 || viewLen > duration)
            setView(0, duration)
    }
    // Playback previews the CUT, not the file: it stays inside the selection and
    // (by default) loops it, so hitting play answers "what will I get?".
    property bool loopSelection: true
    // Stream-copy cut: instant and lossless, but it can only start on a
    // keyframe, so the in-point snaps onto one and the timeline shows where.
    property bool lossless: false
    readonly property bool snapping: lossless && plainCut && !trimController.gif
                                     && trimController.keyframeState === TrimController.Ready
    // What the pointer is dragging (0 nothing, 1 in, 2 out, 3 the whole
    // selection) - the preview
    // follows it, so you always see the frame you are placing.
    property int activeHandle: 0
    // One source frame, in seconds (ffprobe avg_frame_rate; 30 fps fallback).
    // ffmpeg's -t and the GIF trim filter treat the out-point as the first
    // EXCLUDED frame, so "the last frame" previews half a frame before it -
    // the frame the saved file really ends on, not the one after it.
    readonly property real frameDur: trimController.frameDuration > 0.0001
                                     ? trimController.frameDuration : 1 / 30
    function endPreviewTime() {
        return Math.max(trimStart, trimEnd - frameDur / 2)
    }

    function fmt(s) {
        if (isNaN(s) || s < 0) s = 0
        const m = Math.floor(s / 60)
        const sec = s - m * 60
        return m + ":" + (sec < 10 ? "0" : "") + sec.toFixed(1)
    }
    function setStart(t) {
        let v = Math.max(0, Math.min(t, trimEnd - 0.1))
        if (snapping)
            v = trimController.snapStart(v)
        trimStart = v
    }
    function setEnd(t) { trimEnd = Math.max(trimStart + 0.1, Math.min(t, duration)) }

    // Cuts inside the selection: split points (seconds, ascending) divide it
    // into parts, and a part can be dropped from the clip. A dropped part is
    // keyed by the split it starts at, or -1 for the first part, so it stays
    // dropped while the in-point moves.
    property var splits: []
    property var dropped: []
    readonly property var parts: {
        const out = []
        let from = trimStart, key = -1
        for (let i = 0; i < splits.length; ++i) {
            const t = splits[i]
            if (t <= trimStart + 0.05 || t >= trimEnd - 0.05)
                continue
            out.push({ start: from, end: t, key: key, kept: dropped.indexOf(key) < 0 })
            from = t
            key = t
        }
        out.push({ start: from, end: trimEnd, key: key, kept: dropped.indexOf(key) < 0 })
        return out
    }
    readonly property var keptParts: parts.filter(function(p) { return p.kept })
    readonly property real keptLength: keptParts.reduce(function(sum, p) { return sum + p.end - p.start }, 0)
    function partAt(t) {
        for (let i = 0; i < parts.length; ++i)
            if (t >= parts[i].start && t < parts[i].end)
                return parts[i]
        return null
    }
    function splitAt(t) {
        if (t <= trimStart + 0.1 || t >= trimEnd - 0.1)
            return
        for (let i = 0; i < splits.length; ++i)
            if (Math.abs(splits[i] - t) < 0.1)
                return
        const next = splits.concat([t]).sort(function(a, b) { return a - b })
        // Splitting a dropped part drops both halves.
        const part = partAt(t)
        if (part && !part.kept)
            dropped = dropped.concat([t])
        splits = next
    }
    function toggleDropAt(t) {
        const part = partAt(Math.max(trimStart, Math.min(t, trimEnd - 0.001)))
        if (!part)
            return
        if (part.kept && keptParts.length === 1)
            return      // the clip cannot become empty
        dropped = part.kept ? dropped.concat([part.key])
                            : dropped.filter(function(k) { return k !== part.key })
    }
    function clearCuts() { splits = []; dropped = [] }
    // Where playback goes from t when t sits in a dropped part: the next kept
    // part's start, or -1 when none follows.
    function nextKeptFrom(t) {
        for (let i = 0; i < keptParts.length; ++i)
            if (keptParts[i].end > t + 0.02)
                return Math.max(t, keptParts[i].start)
        return -1
    }

    // The frame: a crop of the source at a fixed aspect, slid along its free
    // axis (cropPos 0..1), and the output's shorter side (0 = as recorded).
    property string aspect: App.settings.clipAspect
    property real cropPos: 0.5
    property int shortSide: App.settings.clipShortSide
    readonly property int srcW: trimController.videoWidth
    readonly property int srcH: trimController.videoHeight
    readonly property var cropRect: {
        const ratios = { "16:9": 16 / 9, "9:16": 9 / 16, "1:1": 1 }
        const r = ratios[aspect]
        if (!r || srcW <= 0 || srcH <= 0)
            return null
        const w = Math.min(srcW, Math.round(srcH * r)) & ~1
        const h = Math.min(srcH, Math.round(w / r)) & ~1
        if (w >= srcW && h >= srcH)
            return null
        return { x: Math.round((srcW - w) * cropPos) & ~1, y: Math.round((srcH - h) * cropPos) & ~1, w: w, h: h }
    }
    readonly property bool cropHorizontal: cropRect !== null && cropRect.w < srcW
    // A stream copy cannot join parts, crop or scale.
    readonly property bool plainCut: keptParts.length === 1 && cropRect === null
                                     && (shortSide <= 0 || Math.min(srcW, srcH) <= shortSide)
    property string clipName: ""

    function save() {
        if (keptLength < 0.1 || (keptParts.length > 1 && !trimController.audioReady))
            return
        const titles = trimController.audioTracks.map(function(t) { return t.label })
        App.exportClip({
            path: trimSourcePath,
            segments: keptParts.map(function(p) { return [p.start, p.end] }),
            lossless: snapping && plainCut,
            gains: liveGains,
            mixTrack: mixTrackIndex,
            titles: titles,
            crop: cropRect || {},
            width: srcW,
            height: srcH,
            shortSide: shortSide,
            name: clipName.trim()
        })
        close()
    }

    // Timeline zoom: the strip shows [viewStart, viewStart + viewLen]. The
    // wheel zooms around the pointer, Shift+wheel pans, and playback keeps
    // the playhead in view.
    property real viewStart: 0
    property real viewLen: duration
    readonly property real minViewLen: Math.min(duration, 1)
    function setView(start, len) {
        viewLen = Math.max(minViewLen, Math.min(len, duration))
        viewStart = Math.max(0, Math.min(start, duration - viewLen))
    }
    function zoomAround(t, factor) { setView(t - (t - viewStart) / factor, viewLen / factor) }
    onPlayheadChanged: {
        if (viewLen < duration && (playhead < viewStart || playhead > viewStart + viewLen))
            setView(playhead - viewLen * 0.1, viewLen)
    }
    function loopStart() { return keptParts.length > 0 ? keptParts[0].start : trimStart }
    function stepFrame(n) {
        if (previewLoader.item && previewLoader.item.playing)
            previewLoader.item.pause()
        seekTo(playhead + n * frameDur)
    }
    function seekTo(t) {
        if (hasPreview && previewLoader.item)
            previewLoader.item.seek(Math.max(0, Math.min(t, duration)) * 1000)
    }
    // Seeking on every pointer move would out-run the decoder; coalesce to one
    // seek per frame-ish while a handle is being dragged.
    property real pendingSeek: -1
    function scrubTo(t) {
        pendingSeek = t
        if (!scrubTimer.running)
            scrubTimer.restart()
    }
    Timer {
        id: scrubTimer
        interval: 45
        onTriggered: {
            if (trimWindow.pendingSeek >= 0)
                trimWindow.seekTo(trimWindow.pendingSeek)
            trimWindow.pendingSeek = -1
        }
    }
    function playPause() {
        const item = previewLoader.item
        if (!item)
            return
        if (item.playing) {
            item.pause()
            return
        }
        // Play always previews the clip: land inside a kept part first.
        if (playhead < trimStart - 0.05 || playhead > trimEnd - 0.05)
            seekTo(loopStart())
        item.play()
    }

    // Per-track export state, aligned with trimController.audioTracks: volume
    // factor (1 = untouched) and whether the track is kept at all. Sent to
    // trimRecording as one gain per track (-1 = dropped).
    property var trackGains: []
    property var trackMuted: []
    // The recording's own mix track (first track, so players hear everything).
    // Hidden from the list - editing it directly would be pointless, the stems
    // are still in it - and rebuilt from the edited stems on export instead.
    readonly property int mixTrackIndex: {
        const tracks = trimController.audioTracks
        for (let i = 0; i < tracks.length; ++i)
            if (tracks[i].mix)
                return i
        return -1
    }
    function setTrackGain(i, v) {
        const gains = trackGains.slice()
        gains[i] = Math.round(v * 100) / 100
        trackGains = gains
    }
    function setTrackMuted(i, muted) {
        const flags = trackMuted.slice()
        flags[i] = muted
        trackMuted = flags
    }
    // One gain per audio track, -1 = dropped. Drives BOTH the live preview
    // (VideoPreview voices the stems at these gains) and the export.
    readonly property var liveGains: {
        const gains = []
        for (let i = 0; i < trackGains.length; ++i)
            gains.push(trackMuted[i] ? -1 : trackGains[i])
        return gains
    }

    Component.onCompleted: {
        // 48 tiles is a deliberate over-sample: the timeline shows however many
        // fit and never re-renders the strip on resize.
        trimController.buildFilmstrip(48, 72)
        trimController.loadAudioTracks()
    }
    onLosslessChanged: {
        if (lossless) {
            trimController.loadKeyframes()
            setStart(trimStart)     // no-op until the table arrives, then snaps
        }
    }
    Connections {
        target: trimController
        function onKeyframesChanged() {
            if (trimWindow.snapping)
                trimWindow.setStart(trimWindow.trimStart)
        }
        function onAudioTracksChanged() {
            const gains = [], flags = []
            for (let i = 0; i < trimController.audioTracks.length; ++i) {
                gains.push(1)
                flags.push(false)
            }
            trimWindow.trackGains = gains
            trimWindow.trackMuted = flags
        }
    }
    // Keep the preview on the cut: stop (or loop) at the out-point. The seek is
    // deferred: position is a binding onto the player's own, and seeking from
    // inside its change notification re-enters that binding (loop warning).
    Connections {
        target: previewLoader.item
        enabled: trimWindow.hasPreview && previewLoader.item !== null
        // A loaded player sits stopped and draws nothing until it plays:
        // pausing it shows the first frame, also after a suspend reload.
        function onReadyChanged() {
            const item = previewLoader.item
            if (item && item.ready && !item.playing)
                item.pause()
        }
        // Playing on to the file's end stops the player before a position tick
        // reaches the out-point (the last tick lands up to a frame early), so
        // the loop is closed here too. Any other stop is the user's pause.
        property bool wasPlaying: false
        function onPlayingChanged() {
            const item = previewLoader.item
            if (!item)
                return
            if (item.playing) {
                wasPlaying = true
                return
            }
            const atEnd = item.position / 1000 >= Math.min(trimWindow.trimEnd, trimWindow.duration) - 0.25
            if (wasPlaying && atEnd && trimWindow.loopSelection && trimWindow.activeHandle === 0)
                Qt.callLater(function() { trimWindow.seekTo(trimWindow.loopStart()); item.play() })
            wasPlaying = false
        }
        function onPositionChanged() {
            const item = previewLoader.item
            if (!item || !item.playing || trimWindow.activeHandle !== 0)
                return
            const pos = item.position / 1000
            const part = trimWindow.partAt(pos)
            // A dropped part is skipped, as the saved clip will skip it.
            if (part && !part.kept) {
                const next = trimWindow.nextKeptFrom(pos)
                if (next >= 0) {
                    Qt.callLater(function() { trimWindow.seekTo(next + 0.01) })
                    return
                }
            } else if (pos < trimWindow.trimEnd - 0.02) {
                return
            }
            if (trimWindow.loopSelection) {
                Qt.callLater(function() { trimWindow.seekTo(trimWindow.loopStart()) })
            } else {
                item.pause()
                Qt.callLater(function() { trimWindow.seekTo(trimWindow.endPreviewTime()) })
            }
        }
    }

    // Idle background = no live decode pipeline. When the window loses focus we
    // give it a few seconds (so a quick alt-tab back doesn't churn), then drop
    // the video's ~150 MB of decoder/buffers; refocus reloads and re-seeks.
    onActiveChanged: {
        if (!hasPreview)
            return
        if (active) {
            idleRelease.stop()
            if (previewLoader.item)
                previewLoader.item.resume()
        } else {
            if (previewLoader.item)
                previewLoader.item.pause()   // stop audio now; free RAM after debounce
            idleRelease.restart()
        }
    }
    Timer {
        id: idleRelease
        interval: 4000
        onTriggered: if (previewLoader.item) previewLoader.item.suspend()
    }

    component Sep: Item {
        width: 9
        height: 20
        Rectangle {
            anchors.centerIn: parent
            width: 1
            height: parent.height
            color: Theme.alpha(Theme.accent, 0.14)
        }
    }

    // One group of the settings column: a hairline above, the small uppercase
    // label, then its rows.
    component Section: Column {
        property alias title: sectionLabel.text
        default property alias rows: sectionRows.data
        width: parent ? parent.width : 0
        Rectangle { width: parent.width; height: 1; color: trimWindow.hairline }
        Column {
            x: 20
            width: parent.width - 40
            topPadding: 16
            bottomPadding: 18
            spacing: 12
            SectionLabel { id: sectionLabel }
            Column {
                id: sectionRows
                width: parent.width
                spacing: 12
            }
        }
    }

    // A time readout in a settings row: monospace, so the digits do not jitter
    // while a handle is dragged.
    component TimeValue: Text {
        color: Theme.textPrimary
        font.family: "monospace"
        font.pixelSize: 12
        Accessible.ignored: true
    }

    // Keyboard: space play/pause, I/O mark in-out at playhead, arrows scrub,
    // comma/period step one frame, Home/End jump to the cut edges, L loops,
    // S splits at the playhead, Delete drops or restores the part under it,
    // +/- zoom the timeline and 0 fits it, Ctrl+S saves, Ctrl+W closes.
    //
    // This scope WRAPS the whole window content - it must not be a sibling of
    // it. The kit's controls are tab stops and they accept only the keys they
    // own (UButton takes an unmodified Space/Return, USlider its arrows),
    // deliberately letting everything else bubble up to "the window's key
    // scope" - which only works if this item is an ANCESTOR of whatever holds
    // focus. As a sibling it kept the focus (and the keys) only until the first
    // Tab, and the entire vocabulary below went dead.
    Item {
        id: keys
        anchors.fill: parent
        focus: true
        Keys.onPressed: (e) => {
            // Window keys first - they must work without a video preview.
            if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_W) {
                trimWindow.close(); e.accepted = true; return
            }
            if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_S) {
                trimWindow.save(); e.accepted = true; return
            }
            if (!hasPreview || !UKeys.unmodified(e) && e.modifiers !== Qt.ShiftModifier) return
            switch (e.key) {
            case Qt.Key_Space: trimWindow.playPause(); e.accepted = true; break
            case Qt.Key_I: setStart(playhead); e.accepted = true; break
            case Qt.Key_O: setEnd(playhead); e.accepted = true; break
            case Qt.Key_Left: seekTo(playhead - (e.modifiers & Qt.ShiftModifier ? 5 : 1)); e.accepted = true; break
            case Qt.Key_Right: seekTo(playhead + (e.modifiers & Qt.ShiftModifier ? 5 : 1)); e.accepted = true; break
            case Qt.Key_Home: seekTo(trimWindow.trimStart); e.accepted = true; break
            case Qt.Key_End: seekTo(trimWindow.endPreviewTime()); e.accepted = true; break
            case Qt.Key_L: trimWindow.loopSelection = !trimWindow.loopSelection; e.accepted = true; break
            case Qt.Key_S: trimWindow.splitAt(playhead); e.accepted = true; break
            case Qt.Key_Delete:
            case Qt.Key_Backspace: trimWindow.toggleDropAt(playhead); e.accepted = true; break
            case Qt.Key_Comma: trimWindow.stepFrame(-1); e.accepted = true; break
            case Qt.Key_Period: trimWindow.stepFrame(1); e.accepted = true; break
            case Qt.Key_Plus:
            case Qt.Key_Equal: trimWindow.zoomAround(playhead, 2); e.accepted = true; break
            case Qt.Key_Minus: trimWindow.zoomAround(playhead, 0.5); e.accepted = true; break
            case Qt.Key_0: trimWindow.setView(0, trimWindow.duration); e.accepted = true; break
            }
        }
        Component.onCompleted: keys.forceActiveFocus()

        // ---------- header: what is open on the left, save and the window on the right ----------
        Item {
            id: topBar
            width: parent.width
            height: trimWindow.topH
            z: 20

            // Deferred startSystemMove past a drag threshold - same pattern
            // (and reason) as Main.qml's title bar.
            MouseArea {
                anchors.fill: parent
                enabled: !App.settings.useSystemDecoration
                property real pressX: 0
                property real pressY: 0
                property bool moving: false
                onPressed: (m) => { pressX = m.x; pressY = m.y; moving = false }
                onPositionChanged: (m) => {
                    if (!moving && (Math.abs(m.x - pressX) > 6 || Math.abs(m.y - pressY) > 6)) {
                        moving = true
                        trimWindow.startSystemMove()
                    }
                }
                onDoubleClicked: trimWindow.visibility === Window.Maximized
                                 ? trimWindow.showNormal() : trimWindow.showMaximized()
            }
            Rectangle {
                y: parent.height - 1
                width: parent.width; height: 1
                color: trimWindow.hairline
            }

            Row {
                id: headerTitle
                x: 16
                height: parent.height
                spacing: 10
                Image {
                    source: "qrc:/resources/icons/unisic.svg"
                    sourceSize: Qt.size(20, 20)
                    width: 20; height: 20
                    smooth: true
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Trim recording")
                    color: Theme.textPrimary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "/"
                    color: Theme.alpha(Theme.accent, 0.35)
                    font.pixelSize: 13
                    Accessible.ignored: true
                }
                Text {
                    id: headerFile
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, Math.min(Math.ceil(fileMetrics.advanceWidth) + 1,
                                                rightCluster.x - 12 - headerTitle.x - x))
                    elide: Text.ElideMiddle
                    text: trimSourcePath.substring(trimSourcePath.lastIndexOf("/") + 1)
                    color: Theme.textPrimary
                    font.family: "monospace"
                    font.pixelSize: 12
                    TextMetrics { id: fileMetrics; font: headerFile.font; text: headerFile.text }
                    HoverHandler { id: fileHover }
                    UHoverTip { anchor: headerFile; text: trimSourcePath; show: fileHover.hovered }
                }
            }

            Row {
                id: rightCluster
                anchors.right: parent.right
                anchors.rightMargin: App.settings.useSystemDecoration ? 16 : 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                UButton {
                    anchors.verticalCenter: parent.verticalCenter
                    compact: true
                    height: 30
                    cornerRadius: 7
                    iconName: "line-save"
                    text: qsTr("Save trimmed copy")
                    accessibleDescription: qsTr("Ctrl+S")
                    enabled: trimWindow.keptLength >= 0.1
                             && (trimWindow.keptParts.length === 1 || trimController.audioReady)
                    onClicked: trimWindow.save()
                }
                Sep {
                    visible: !App.settings.useSystemDecoration
                    anchors.verticalCenter: parent.verticalCenter
                }
                Row {
                    visible: !App.settings.useSystemDecoration
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    UIconButton {
                        iconName: "minus"; iconSize: 14; width: 34; height: 30
                        tooltip: qsTr("Minimize")
                        onClicked: trimWindow.showMinimized()
                    }
                    UIconButton {
                        iconName: "window"; iconSize: 13; width: 34; height: 30
                        tooltip: qsTr("Maximize")
                        onClicked: trimWindow.visibility === Window.Maximized
                                   ? trimWindow.showNormal() : trimWindow.showMaximized()
                    }
                    UIconButton {
                        iconName: "close"; iconSize: 14; width: 34; height: 30
                        tooltip: qsTr("Close")
                        onClicked: trimWindow.close()
                    }
                }
            }
        }

        // ---------- stage: the preview above, the transport and timeline below ----------
        Rectangle {
            id: stage
            y: topBar.height
            width: inspector.x
            height: parent.height - y
            color: Theme.backgroundDeep

            Rectangle {
                id: previewBox
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: dock.top
                anchors.margins: 20
                radius: Theme.radiusM
                color: Theme.mediaBase
                clip: true

                Loader {
                    id: previewLoader
                    anchors.fill: parent
                    active: hasPreview
                    sourceComponent: previewComp
                }
                Component {
                    id: previewComp
                    VideoPreview {
                        fileUrl: App.fileDragUri(trimSourcePath)
                        trackGains: trimWindow.liveGains
                        mixTrack: trimWindow.mixTrackIndex
                    }
                }

                // The frame being cropped out, on the picture itself: outside it
                // is dimmed, and a drag slides it along its free axis. Mapped
                // through the source size, since the video is fitted with bars.
                Item {
                    id: cropLayer
                    readonly property real fit: (trimWindow.srcW > 0 && trimWindow.srcH > 0)
                                                ? Math.min(previewBox.width / trimWindow.srcW,
                                                           previewBox.height / trimWindow.srcH) : 0
                    width: trimWindow.srcW * fit
                    height: trimWindow.srcH * fit
                    anchors.centerIn: parent
                    visible: trimWindow.cropRect !== null && fit > 0
                    readonly property rect frame: trimWindow.cropRect
                        ? Qt.rect(trimWindow.cropRect.x * fit, trimWindow.cropRect.y * fit,
                                  trimWindow.cropRect.w * fit, trimWindow.cropRect.h * fit)
                        : Qt.rect(0, 0, width, height)
                    readonly property color shade: Theme.alpha(Theme.mediaBase, 0.7)
                    Rectangle { x: 0; y: 0; width: cropLayer.frame.x; height: parent.height; color: cropLayer.shade }
                    Rectangle {
                        x: cropLayer.frame.x + cropLayer.frame.width; y: 0
                        width: parent.width - x; height: parent.height; color: cropLayer.shade
                    }
                    Rectangle {
                        x: cropLayer.frame.x; y: 0
                        width: cropLayer.frame.width; height: cropLayer.frame.y; color: cropLayer.shade
                    }
                    Rectangle {
                        x: cropLayer.frame.x; y: cropLayer.frame.y + cropLayer.frame.height
                        width: cropLayer.frame.width; height: parent.height - y; color: cropLayer.shade
                    }
                    Rectangle {
                        x: cropLayer.frame.x; y: cropLayer.frame.y
                        width: cropLayer.frame.width; height: cropLayer.frame.height
                        color: "transparent"
                        border.width: 2
                        border.color: Theme.accent
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            preventStealing: true
                            property real pressPos: 0
                            property real pressCrop: 0
                            onPressed: (m) => {
                                const p = mapToItem(cropLayer, m.x, m.y)
                                pressPos = trimWindow.cropHorizontal ? p.x : p.y
                                pressCrop = trimWindow.cropPos
                            }
                            onPositionChanged: (m) => {
                                const p = mapToItem(cropLayer, m.x, m.y)
                                const slack = trimWindow.cropHorizontal ? cropLayer.width - cropLayer.frame.width
                                                                        : cropLayer.height - cropLayer.frame.height
                                if (slack <= 0)
                                    return
                                const d = (trimWindow.cropHorizontal ? p.x : p.y) - pressPos
                                trimWindow.cropPos = Math.max(0, Math.min(1, pressCrop + d / slack))
                            }
                            // The inspector's slider is the keyboard way to move it.
                            Accessible.ignored: true
                        }
                    }
                }

                // While a handle is being dragged the frame on screen IS the cut
                // point - say which one, so the two are never confused.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.spacingM
                    visible: trimWindow.activeHandle !== 0
                    width: edgeLabel.implicitWidth + 2 * Theme.spacingM
                    height: edgeLabel.implicitHeight + Theme.spacingS
                    radius: height / 2
                    color: Theme.alpha(Theme.mediaBase, 0.72)
                    Text {
                        id: edgeLabel
                        anchors.centerIn: parent
                        text: trimWindow.activeHandle !== 2
                              ? qsTr("First frame · %1").arg(trimWindow.fmt(trimWindow.trimStart))
                              : qsTr("Last frame · %1").arg(trimWindow.fmt(trimWindow.trimEnd))
                        color: Theme.accent; font.pixelSize: Theme.fontS; font.weight: Font.DemiBold
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: !hasPreview
                    width: parent.width - 2 * Theme.spacingXL
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("No video preview available.")
                    color: Theme.textTertiary; font.pixelSize: Theme.fontM
                }
            }

            Rectangle {
                id: dock
                anchors.bottom: parent.bottom
                width: parent.width
                height: dockColumn.height + 16 + 22
                color: Theme.surface
                Rectangle { width: parent.width; height: 1; color: trimWindow.hairline }

                Column {
                    id: dockColumn
                    x: 20
                    y: 16
                    width: parent.width - 40
                    spacing: 12

                    // Transport: play and loop, where the playhead is, and the two
                    // marks at the playhead. Without a preview there is no
                    // playhead, so the row stays and its controls disable.
                    Item {
                        width: parent.width
                        height: 32
                        Row {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4
                            UIconButton {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 34; height: 32
                                iconName: (previewLoader.item && previewLoader.item.playing) ? "pause" : "play"
                                iconSize: 18
                                enabled: hasPreview
                                tooltip: qsTr("Play the selection (Space)")
                                onClicked: trimWindow.playPause()
                            }
                            UIconButton {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 34; height: 32
                                iconName: "media-repeat"
                                iconSize: 18
                                enabled: hasPreview
                                active: trimWindow.loopSelection
                                tooltip: qsTr("Loop the selection (L)")
                                onClicked: trimWindow.loopSelection = !trimWindow.loopSelection
                            }
                            Item { width: 8; height: 1 }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("%1 / %2").arg(trimWindow.fmt(trimWindow.playhead)).arg(trimWindow.fmt(trimWindow.duration))
                                color: hasPreview ? Theme.textPrimary : Theme.textTertiary
                                font.family: "monospace"
                                font.pixelSize: 12
                            }
                        }
                        Row {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            UIconButton {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 30; height: 30
                                iconName: "line-minus"
                                iconSize: 16
                                enabled: trimWindow.viewLen < trimWindow.duration
                                tooltip: qsTr("Zoom out (-)")
                                onClicked: trimWindow.zoomAround(trimWindow.playhead, 0.5)
                            }
                            UIconButton {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 30; height: 30
                                iconName: "line-plus"
                                iconSize: 16
                                enabled: trimWindow.viewLen > trimWindow.minViewLen
                                tooltip: qsTr("Zoom in on the timeline (+, mouse wheel)")
                                onClicked: trimWindow.zoomAround(trimWindow.playhead, 2)
                            }
                            Sep { anchors.verticalCenter: parent.verticalCenter }
                            UIconButton {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 30; height: 30
                                iconName: "line-cut"
                                iconSize: 16
                                enabled: hasPreview && trimWindow.playhead > trimWindow.trimStart + 0.1
                                         && trimWindow.playhead < trimWindow.trimEnd - 0.1
                                tooltip: qsTr("Split at the playhead (S)")
                                onClicked: trimWindow.splitAt(trimWindow.playhead)
                            }
                            UIconButton {
                                readonly property var part: trimWindow.partAt(trimWindow.playhead)
                                readonly property bool restores: part !== null && !part.kept
                                anchors.verticalCenter: parent.verticalCenter
                                width: 30; height: 30
                                iconName: restores ? "line-undo" : "line-trash"
                                iconSize: 16
                                enabled: hasPreview && trimWindow.parts.length > 1 && part !== null
                                         && (restores || trimWindow.keptParts.length > 1)
                                tooltip: restores ? qsTr("Restore this part (Del)") : qsTr("Remove this part (Del)")
                                onClicked: trimWindow.toggleDropAt(trimWindow.playhead)
                            }
                            Sep { anchors.verticalCenter: parent.verticalCenter }
                            UButton {
                                anchors.verticalCenter: parent.verticalCenter
                                compact: true; height: 30; cornerRadius: 7
                                variant: "outline"
                                enabled: hasPreview
                                text: qsTr("Set start (I)")
                                onClicked: trimWindow.setStart(trimWindow.playhead)
                            }
                            UButton {
                                anchors.verticalCenter: parent.verticalCenter
                                compact: true; height: 30; cornerRadius: 7
                                variant: "outline"
                                enabled: hasPreview
                                text: qsTr("Set end (O)")
                                onClicked: trimWindow.setEnd(trimWindow.playhead)
                            }
                        }
                    }

                    // Timeline: thumbnails, the selection lit and the rest dimmed,
                    // the two handles and the playhead. The handles work without a
                    // preview too, the strip comes from ffmpeg.
                    Item {
                        id: timeline
                        width: parent.width
                        height: 64

                        function timeAt(x) {
                            return track.width > 0
                                   ? Math.max(0, Math.min(trimWindow.viewStart + x / track.width * trimWindow.viewLen,
                                                          trimWindow.duration))
                                   : 0
                        }
                        function xOf(t) {
                            return trimWindow.viewLen > 0
                                   ? track.width * ((t - trimWindow.viewStart) / trimWindow.viewLen) : 0
                        }
                        function inView(x) { return x >= -1 && x <= track.width + 1 }

                        Rectangle {
                            id: track
                            anchors.fill: parent
                            radius: 8
                            color: Theme.surfaceHi
                            clip: true

                            // The strip is a single tiled PNG; this hidden probe reports
                            // its natural size so a cell can slice one tile out of it.
                            Image {
                                id: stripProbe
                                source: trimController.filmstrip
                                visible: false
                                asynchronous: true
                            }
                            readonly property real tileW: (stripProbe.implicitWidth > 0
                                                           && trimController.filmstripTiles > 0)
                                                          ? stripProbe.implicitWidth / trimController.filmstripTiles : 0
                            readonly property real tileH: stripProbe.implicitHeight
                            // Cells keep the tile's aspect, so each one is an undistorted
                            // frame; the count follows the window width, the strip does not.
                            readonly property int cellW: (tileW > 0 && tileH > 0)
                                                         ? Math.max(16, Math.round(tileW * (track.height / tileH))) : 0
                            readonly property bool stripReady: cellW > 0 && stripProbe.status === Image.Ready

                            Repeater {
                                model: track.stripReady ? Math.ceil(track.width / track.cellW) : 0
                                Image {
                                    required property int index
                                    x: index * track.cellW
                                    width: track.cellW
                                    height: track.height
                                    source: trimController.filmstrip
                                    asynchronous: true
                                    smooth: true
                                    // Nearest tile in TIME to this cell's centre, so a
                                    // zoomed timeline repeats the tile it is inside of.
                                    sourceClipRect: {
                                        const tiles = trimController.filmstripTiles
                                        const t = timeline.timeAt((index + 0.5) * track.cellW)
                                        const i = Math.max(0, Math.min(Math.floor(t / Math.max(0.001, trimWindow.duration) * tiles),
                                                                       tiles - 1))
                                        return Qt.rect(Math.round(i * track.tileW), 0,
                                                       Math.round(track.tileW), Math.round(track.tileH))
                                    }
                                }
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: !track.stripReady
                                text: trimController.filmstripState === TrimController.Failed
                                      ? qsTr("No thumbnails for this file")
                                      : qsTr("Loading thumbnails…")
                                color: Theme.textTertiary; font.pixelSize: Theme.fontS
                            }

                            // Everything outside the selection is dimmed: what stays lit
                            // is exactly what the saved file will contain.
                            Rectangle {
                                x: 0; width: Math.max(0, timeline.xOf(trimWindow.trimStart))
                                height: parent.height
                                color: Theme.alpha(Theme.mediaBase, 0.66)
                            }
                            Rectangle {
                                x: timeline.xOf(trimWindow.trimEnd)
                                width: Math.max(0, track.width - x)
                                height: parent.height
                                color: Theme.alpha(Theme.mediaBase, 0.66)
                            }
                            // Dropped parts stay on the strip, dimmed and struck
                            // through, so Delete over one can bring it back.
                            Repeater {
                                model: trimWindow.parts
                                Rectangle {
                                    required property var modelData
                                    visible: !modelData.kept
                                    x: timeline.xOf(modelData.start)
                                    width: Math.max(0, timeline.xOf(modelData.end) - x)
                                    height: parent.height
                                    color: Theme.alpha(Theme.mediaBase, 0.78)
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: parent.width - 8
                                        height: 2
                                        color: Theme.alpha(Theme.accent, 0.45)
                                    }
                                }
                            }
                            Rectangle {
                                x: timeline.xOf(trimWindow.trimStart)
                                width: Math.max(0, timeline.xOf(trimWindow.trimEnd) - x)
                                height: parent.height
                                color: "transparent"
                                border.width: 2
                                border.color: Theme.accent
                            }
                            Repeater {
                                model: trimWindow.parts.slice(1)
                                Rectangle {
                                    required property var modelData
                                    x: timeline.xOf(modelData.start) - 1
                                    width: 2
                                    height: parent.height
                                    color: Theme.accent
                                }
                            }

                            // Where a lossless cut can actually start. The FULL table
                            // stays in trimController for snapping; the visible ticks
                            // are decimated to the timeline's pixel resolution - an
                            // all-intra or short-GOP clip has a keyframe per frame, and
                            // a Rectangle per entry froze the UI on long recordings.
                            readonly property var visibleKeyframes: {
                                if (!trimWindow.snapping || track.width <= 0)
                                    return []
                                const kfs = trimController.keyframes
                                const minPx = 3
                                const out = []
                                let lastX = -minPx
                                for (let i = 0; i < kfs.length; ++i) {
                                    const x = timeline.xOf(kfs[i])
                                    if (x > track.width)
                                        break
                                    if (x >= 0 && x - lastX >= minPx) {
                                        out.push(kfs[i])
                                        lastX = x
                                    }
                                }
                                return out
                            }
                            Repeater {
                                model: track.visibleKeyframes
                                Rectangle {
                                    required property var modelData
                                    x: timeline.xOf(modelData)
                                    y: track.height - 7
                                    width: 1; height: 7
                                    color: Theme.textSecondary
                                    opacity: 0.8
                                }
                            }

                            // Outside the selection a click or drag moves the playhead.
                            // Inside it a drag past a few pixels moves the whole
                            // selection, its length kept; a plain click there still
                            // seeks, on release, once it is clear it was no drag.
                            MouseArea {
                                id: stripMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                property int mode: 0          // 0 scrub, 1 may move, 2 moving
                                property real pressX: 0
                                property real pressStart: 0
                                property real pressLength: 0
                                property var pressSplits: []
                                property var pressDropped: []
                                // Wheel zooms around the pointer; Shift or a
                                // sideways wheel pans.
                                onWheel: (w) => {
                                    const dx = w.angleDelta.x !== 0 ? w.angleDelta.x
                                             : ((w.modifiers & Qt.ShiftModifier) ? w.angleDelta.y : 0)
                                    if (dx !== 0)
                                        trimWindow.setView(trimWindow.viewStart - dx / 120 * trimWindow.viewLen * 0.1,
                                                           trimWindow.viewLen)
                                    else if (w.angleDelta.y !== 0)
                                        trimWindow.zoomAround(timeline.timeAt(w.x), Math.pow(1.25, w.angleDelta.y / 120))
                                }
                                function inSelection(x) {
                                    return x >= timeline.xOf(trimWindow.trimStart)
                                           && x <= timeline.xOf(trimWindow.trimEnd)
                                }
                                cursorShape: mode === 2 ? Qt.ClosedHandCursor
                                             : (containsMouse && inSelection(mouseX) ? Qt.OpenHandCursor
                                                                                     : Qt.ArrowCursor)
                                onPressed: (m) => {
                                    pressX = m.x
                                    pressStart = trimWindow.trimStart
                                    pressLength = trimWindow.trimEnd - trimWindow.trimStart
                                    pressSplits = trimWindow.splits
                                    pressDropped = trimWindow.dropped
                                    mode = inSelection(m.x) ? 1 : 0
                                    if (mode === 0)
                                        trimWindow.seekTo(timeline.timeAt(m.x))
                                }
                                onPositionChanged: (m) => {
                                    if (!pressed)
                                        return
                                    if (mode === 0) {
                                        trimWindow.scrubTo(timeline.timeAt(m.x))
                                        return
                                    }
                                    if (mode === 1) {
                                        if (Math.abs(m.x - pressX) < 5)
                                            return
                                        mode = 2
                                        trimWindow.activeHandle = 3
                                        if (previewLoader.item)
                                            previewLoader.item.pause()
                                    }
                                    const dt = (m.x - pressX) / Math.max(1, track.width) * trimWindow.viewLen
                                    let start = Math.max(0, Math.min(pressStart + dt, trimWindow.duration - pressLength))
                                    if (trimWindow.snapping)
                                        start = trimController.snapStart(start)
                                    trimWindow.trimStart = start
                                    trimWindow.trimEnd = Math.min(start + pressLength, trimWindow.duration)
                                    // The cuts travel with the selection.
                                    const shift = start - pressStart
                                    trimWindow.splits = pressSplits.map(function(t) { return t + shift })
                                    trimWindow.dropped = pressDropped.map(function(k) { return k < 0 ? k : k + shift })
                                    trimWindow.scrubTo(start)
                                }
                                onReleased: (m) => {
                                    if (mode === 2) {
                                        trimWindow.activeHandle = 0
                                        trimWindow.seekTo(trimWindow.trimStart)
                                    } else if (mode === 1) {
                                        trimWindow.seekTo(timeline.timeAt(m.x))
                                    }
                                    mode = 0
                                }
                                onCanceled: {
                                    if (mode === 2)
                                        trimWindow.activeHandle = 0
                                    mode = 0
                                }
                                // The keyboard equivalent lives at window level (the
                                // `keys` Item: arrows scrub, Home/End jump), so this
                                // reports the value and points at those keys instead of
                                // becoming a second focus stop that would swallow them.
                                Accessible.role: Accessible.Slider
                                Accessible.name: qsTr("Playhead")
                                Accessible.description: qsTr("%1 of %2 seconds. Arrow keys scrub, Home and End jump.")
                                                            .arg(trimWindow.playhead.toFixed(1))
                                                            .arg(trimWindow.duration.toFixed(1))
                                Accessible.onIncreaseAction: trimWindow.seekTo(trimWindow.playhead + 1)
                                Accessible.onDecreaseAction: trimWindow.seekTo(trimWindow.playhead - 1)
                            }
                        }

                        // Playhead: a line with a knob above the strip.
                        Item {
                            visible: hasPreview && timeline.inView(x + width / 2)
                            x: timeline.xOf(trimWindow.playhead) - width / 2
                            y: -6
                            width: 10
                            height: track.height + 10
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 2; height: parent.height
                                color: Theme.textPrimary
                            }
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 10; height: 10; radius: 5
                                color: Theme.textPrimary
                            }
                        }

                        // IN / OUT handles: x is a one-way function of the time value;
                        // dragging maps the pointer into track space and pushes the time
                        // back through setStart/setEnd - no binding loop, resize-safe.
                        // While one is dragged the preview seeks to it, so the frame on
                        // screen is the frame the cut will land on.
                        Repeater {
                            model: 2
                            Rectangle {
                                id: handle
                                required property int index
                                readonly property bool isStart: index === 0
                                visible: timeline.inView(x + width / 2)
                                x: timeline.xOf(isStart ? trimWindow.trimStart : trimWindow.trimEnd) - width / 2
                                y: -4
                                width: 12
                                height: track.height + 8
                                radius: 4
                                color: trimWindow.activeHandle === (isStart ? 1 : 2)
                                       ? Qt.lighter(Theme.accent, 1.2) : Theme.accent
                                border.width: 1
                                border.color: Theme.background
                                Rectangle {   // grip
                                    anchors.centerIn: parent
                                    width: 2; height: 16; radius: 1
                                    color: Theme.textOnAccent
                                    opacity: 0.7
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -6      // fat target, thin handle
                                    cursorShape: Qt.SizeHorCursor
                                    preventStealing: true
                                    Accessible.role: Accessible.Slider
                                    Accessible.name: handle.isStart ? qsTr("Trim start") : qsTr("Trim end")
                                    Accessible.description: handle.isStart
                                        ? qsTr("%1 seconds. Press I to move it to the playhead.")
                                              .arg(trimWindow.trimStart.toFixed(1))
                                        : qsTr("%1 seconds. Press O to move it to the playhead.")
                                              .arg(trimWindow.trimEnd.toFixed(1))
                                    Accessible.onIncreaseAction: handle.isStart
                                        ? trimWindow.setStart(trimWindow.trimStart + 1)
                                        : trimWindow.setEnd(trimWindow.trimEnd + 1)
                                    Accessible.onDecreaseAction: handle.isStart
                                        ? trimWindow.setStart(trimWindow.trimStart - 1)
                                        : trimWindow.setEnd(trimWindow.trimEnd - 1)
                                    onPressed: {
                                        trimWindow.activeHandle = handle.isStart ? 1 : 2
                                        if (previewLoader.item)
                                            previewLoader.item.pause()
                                    }
                                    onReleased: {
                                        trimWindow.activeHandle = 0
                                        trimWindow.seekTo(handle.isStart ? trimWindow.trimStart
                                                                         : trimWindow.endPreviewTime())
                                    }
                                    onPositionChanged: (m) => {
                                        const t = timeline.timeAt(mapToItem(track, m.x, 0).x)
                                        if (handle.isStart) {
                                            trimWindow.setStart(t)
                                            trimWindow.scrubTo(trimWindow.trimStart)
                                        } else {
                                            trimWindow.setEnd(t)
                                            trimWindow.scrubTo(trimWindow.endPreviewTime())
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---------- settings column: the selection, how it is cut, its sound ----------
        Rectangle {
            id: inspector
            x: parent.width - width
            y: topBar.height
            width: trimWindow.inspectorWidth
            height: parent.height - y
            color: Theme.surface
            Rectangle { width: 1; height: parent.height; color: trimWindow.hairline }

            Flickable {
                id: inspectorFlick
                anchors.fill: parent
                anchors.leftMargin: 1
                clip: true
                contentHeight: inspectorColumn.height
                boundsBehavior: Flickable.StopAtBounds
                Accessible.role: Accessible.Pane
                Accessible.name: qsTr("Trim options")
                WheelBoost { flickable: inspectorFlick }

                Column {
                    id: inspectorColumn
                    width: inspectorFlick.width

                    // What gets saved, in one line.
                    Item {
                        width: parent.width
                        height: 18 + 36 + 16
                        Rectangle {
                            id: focusTile
                            x: 20; y: 18
                            width: 36; height: 36; radius: 9
                            color: Theme.secondary
                            UIcon {
                                anchors.centerIn: parent
                                name: "line-cut"
                                iconStyle: "custom"
                                size: 18
                                color: Theme.accent
                            }
                        }
                        Column {
                            x: focusTile.x + focusTile.width + 12
                            width: parent.width - x - 20
                            anchors.verticalCenter: focusTile.verticalCenter
                            spacing: 1
                            Text {
                                width: parent.width
                                elide: Text.ElideRight
                                text: qsTr("Selection")
                                color: Theme.textPrimary
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }
                            Text {
                                width: parent.width
                                elide: Text.ElideRight
                                //: Trim window: length of the selection, then of the whole recording.
                                text: qsTr("%1 of %2").arg(trimWindow.fmt(trimWindow.keptLength))
                                                      .arg(trimWindow.fmt(trimWindow.duration))
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontS
                            }
                        }
                    }

                    Section {
                        title: qsTr("Clip")
                        UTextField {
                            width: parent.width
                            text: trimWindow.clipName
                            placeholder: qsTr("Name (optional)")
                            accessibleName: qsTr("Clip name")
                            accessibleDescription: qsTr("Becomes the file name, next to the recording.")
                            onEdited: (t) => trimWindow.clipName = t
                        }
                    }

                    // The frame: an aspect cropped out of the recording (dragged
                    // into place on the preview, or with the slider), and the
                    // output size. Both re-encode.
                    Section {
                        title: qsTr("Frame")
                        InspectorRow {
                            label: qsTr("Aspect")
                            UComboBox {
                                width: 170
                                readonly property var ids: ["original", "16:9", "9:16", "1:1"]
                                model: [qsTr("As recorded"), qsTr("Landscape 16:9"), qsTr("Vertical 9:16"), qsTr("Square 1:1")]
                                enabled: trimWindow.srcW > 0
                                accessibleName: qsTr("Aspect")
                                currentIndex: Math.max(0, ids.indexOf(trimWindow.aspect))
                                onActivated: (i) => trimWindow.aspect = ids[i]
                            }
                        }
                        InspectorRow {
                            label: qsTr("Position")
                            enabled: trimWindow.cropRect !== null
                            USlider {
                                width: 150
                                from: 0; to: 1; stepSize: 0.01
                                value: trimWindow.cropPos
                                accessibleName: trimWindow.cropHorizontal ? qsTr("Frame position, left to right")
                                                                          : qsTr("Frame position, top to bottom")
                                onMoved: (v) => trimWindow.cropPos = v
                            }
                        }
                        InspectorRow {
                            label: qsTr("Size")
                            UComboBox {
                                width: 170
                                readonly property var ids: [0, 1080, 720, 480]
                                model: [qsTr("As recorded"), "1080p", "720p", "480p"]
                                accessibleName: qsTr("Size")
                                currentIndex: Math.max(0, ids.indexOf(trimWindow.shortSide))
                                onActivated: (i) => trimWindow.shortSide = ids[i]
                            }
                        }
                    }

                    Section {
                        title: qsTr("Range")
                        InspectorRow {
                            label: qsTr("Start")
                            TimeValue { text: trimWindow.fmt(trimWindow.trimStart) }
                        }
                        // Without a preview there are no handles to see the frame
                        // at, so the range is set here instead.
                        USlider {
                            visible: !hasPreview
                            accessibleName: qsTr("Start")
                            width: parent.width; from: 0; to: Math.max(0.1, trimWindow.duration - 0.1)
                            value: trimWindow.trimStart
                            // Through setStart(), NOT a raw assignment: with Fast
                            // lossless on, the in-point must snap onto a keyframe here
                            // too, or ffmpeg stream-copies from the previous keyframe
                            // while the UI shows the unsnapped value.
                            onMoved: (v) => trimWindow.setStart(v)
                        }
                        InspectorRow {
                            label: qsTr("End")
                            TimeValue { text: trimWindow.fmt(trimWindow.trimEnd) }
                        }
                        USlider {
                            visible: !hasPreview
                            accessibleName: qsTr("End")
                            width: parent.width; from: 0.1; to: trimWindow.duration
                            value: trimWindow.trimEnd
                            onMoved: (v) => trimWindow.setEnd(v)
                        }
                    }

                    // How the file gets cut. Default (off) re-encodes the selection,
                    // which is the only way the saved frames match the preview
                    // exactly; a stream copy is instant but starts on a keyframe.
                    Section {
                        title: qsTr("Saving")
                        InspectorRow {
                            id: losslessRow
                            label: qsTr("Fast lossless cut")
                            enabled: !trimController.gif && trimWindow.plainCut
                            sub: trimController.gif
                                 ? qsTr("A GIF is always re-rendered, so the cut lands on the exact frame.")
                                 : !trimWindow.plainCut
                                   ? qsTr("Removed parts, a frame or a size are re-encoded, so this is off.")
                                 : !trimWindow.lossless
                                   ? qsTr("Off: the selection is re-encoded and starts on the exact frame.")
                                   : (trimController.keyframeState === TrimController.Busy
                                      ? qsTr("Reading keyframes…")
                                      : (trimController.keyframeState === TrimController.Ready
                                         ? qsTr("On: copies the streams, so the start snaps to a keyframe (ticks).")
                                         : qsTr("No keyframes found - saving will re-encode instead.")))
                            MiniSwitch {
                                checked: trimWindow.lossless && trimWindow.plainCut
                                accessibleName: losslessRow.label
                                accessibleDescription: losslessRow.sub
                                onToggled: (c) => trimWindow.lossless = c
                            }
                        }
                    }

                    // Audio tracks: keep/drop and volume per track, heard live in
                    // the preview. Shown once the probe finds audio; the mix track
                    // is not listed, it is rebuilt from the tracks below.
                    Section {
                        visible: trimController.audioTracks.length > 0
                        title: qsTr("Audio tracks")
                        Text {
                            visible: trimWindow.mixTrackIndex >= 0
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: qsTr("(the mix track is rebuilt from the tracks below)")
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontS
                        }
                        Repeater {
                            model: trimController.audioTracks
                            Column {
                                id: trackRow
                                required property int index
                                required property var modelData
                                visible: !modelData.mix
                                readonly property bool kept: !(trimWindow.trackMuted[index] === true)
                                readonly property real gain: trimWindow.trackGains[index] !== undefined
                                                             ? trimWindow.trackGains[index] : 1
                                width: parent.width
                                spacing: 6
                                InspectorRow {
                                    label: trackRow.modelData.label
                                    MiniSwitch {
                                        checked: trackRow.kept
                                        accessibleName: qsTr("Keep track %1").arg(trackRow.modelData.label)
                                        onToggled: (c) => trimWindow.setTrackMuted(trackRow.index, !c)
                                    }
                                }
                                Item {
                                    width: parent.width
                                    height: gainSlider.height
                                    USlider {
                                        id: gainSlider
                                        anchors.left: parent.left
                                        anchors.right: gainText.left
                                        anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        from: 0; to: 2; stepSize: 0.05
                                        value: trackRow.gain
                                        enabled: trackRow.kept
                                        accessibleName: qsTr("Volume of %1").arg(trackRow.modelData.label)
                                        onMoved: (v) => trimWindow.setTrackGain(trackRow.index, v)
                                    }
                                    Text {
                                        id: gainText
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 40
                                        horizontalAlignment: Text.AlignRight
                                        text: trackRow.kept ? Math.round(trackRow.gain * 100) + "%" : qsTr("off")
                                        color: Theme.textSecondary
                                        font.family: "monospace"
                                        font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    WindowOutline {}
    UWindowResizer { active: !App.settings.useSystemDecoration }
}
