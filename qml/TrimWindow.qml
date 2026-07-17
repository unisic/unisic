import QtQuick
import QtQuick.Window
import Unisic
import "components"

Window {
    id: trimWindow
    width: 940; height: 700
    minimumWidth: 680; minimumHeight: 520
    visible: true
    title: qsTr("Trim recording")
    color: Theme.background
    // Same decoration policy as the main and editor windows: the stylized
    // frameless title bar unless the user opted into system decorations.
    flags: App.settings.useSystemDecoration
           ? Qt.Window
           : (Qt.Window | Qt.FramelessWindowHint)
    readonly property int chromeTop: App.settings.useSystemDecoration ? 0 : 38

    // Video preview is optional: it needs the QtMultimedia QML module
    // (qt6-qtmultimedia). Without it the window falls back to two sliders.
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
    // Playback previews the CUT, not the file: it stays inside the selection and
    // (by default) loops it, so hitting play answers "what will I get?".
    property bool loopSelection: true
    // Stream-copy cut: instant and lossless, but it can only start on a
    // keyframe, so the in-point snaps onto one and the timeline shows where.
    property bool lossless: false
    readonly property bool snapping: lossless && !trimController.gif
                                     && trimController.keyframeState === TrimController.Ready
    // Which handle the pointer is dragging (0 none, 1 in, 2 out) — the preview
    // follows it, so you always see the frame you are placing.
    property int activeHandle: 0
    // One source frame, in seconds (ffprobe avg_frame_rate; 30 fps fallback).
    // ffmpeg's -t and the GIF trim filter treat the out-point as the first
    // EXCLUDED frame, so "the last frame" previews half a frame before it —
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
        // Play always previews the selection: land inside it first.
        if (playhead < trimStart - 0.05 || playhead > trimEnd - 0.05)
            seekTo(trimStart)
        item.play()
    }

    Component.onCompleted: {
        // 48 tiles is a deliberate over-sample: the timeline shows however many
        // fit and never re-renders the strip on resize.
        trimController.buildFilmstrip(48, 72)
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
    }
    // Keep the preview on the cut: stop (or loop) at the out-point. The seek is
    // deferred: position is a binding onto the player's own, and seeking from
    // inside its change notification re-enters that binding (loop warning).
    Connections {
        target: previewLoader.item
        enabled: trimWindow.hasPreview && previewLoader.item !== null
        function onPositionChanged() {
            const item = previewLoader.item
            if (!item || !item.playing || trimWindow.activeHandle !== 0)
                return
            if (item.position / 1000 < trimWindow.trimEnd - 0.02)
                return
            if (trimWindow.loopSelection) {
                Qt.callLater(function() { trimWindow.seekTo(trimWindow.trimStart) })
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

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.background }
            GradientStop { position: 1; color: Theme.backgroundDeep }
        }
    }

    // Keyboard: space play/pause, I/O mark in-out at playhead, arrows scrub.
    Item {
        id: keys
        anchors.fill: parent
        focus: true
        Keys.onPressed: (e) => {
            // Window keys first — they must work without a video preview.
            if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_W) {
                trimWindow.close(); e.accepted = true; return
            }
            if (!hasPreview) return
            switch (e.key) {
            case Qt.Key_Space: trimWindow.playPause(); e.accepted = true; break
            case Qt.Key_I: setStart(playhead); e.accepted = true; break
            case Qt.Key_O: setEnd(playhead); e.accepted = true; break
            case Qt.Key_Left: seekTo(playhead - (e.modifiers & Qt.ShiftModifier ? 5 : 1)); e.accepted = true; break
            case Qt.Key_Right: seekTo(playhead + (e.modifiers & Qt.ShiftModifier ? 5 : 1)); e.accepted = true; break
            case Qt.Key_Home: seekTo(trimWindow.trimStart); e.accepted = true; break
            case Qt.Key_End: seekTo(trimWindow.endPreviewTime()); e.accepted = true; break
            case Qt.Key_L: trimWindow.loopSelection = !trimWindow.loopSelection; e.accepted = true; break
            }
        }
        Component.onCompleted: keys.forceActiveFocus()
    }

    // ---------- custom title bar (frameless decoration) ----------
    Rectangle {
        id: trimTitleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: trimWindow.chromeTop
        visible: !App.settings.useSystemDecoration
        z: 20
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.lighter(Theme.primary, 1.12) }
            GradientStop { position: 1.0; color: Theme.primary }
        }
        // Deferred startSystemMove past a drag threshold — same pattern
        // (and reason) as Main.qml's title bar.
        MouseArea {
            anchors.fill: parent
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
        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingL
            anchors.verticalCenter: parent.verticalCenter
            text: trimWindow.title
            color: Theme.textPrimary
            font.pixelSize: Theme.fontM
            font.weight: Font.DemiBold
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            UIconButton {
                iconName: "minus"; iconSize: 14; width: 30; height: 30
                tooltip: qsTr("Minimize")
                onClicked: trimWindow.showMinimized()
            }
            UIconButton {
                iconName: "window"; iconSize: 13; width: 30; height: 30
                tooltip: qsTr("Maximize")
                onClicked: trimWindow.visibility === Window.Maximized
                           ? trimWindow.showNormal() : trimWindow.showMaximized()
            }
            UIconButton {
                iconName: "close"; iconSize: 14; width: 30; height: 30
                tooltip: qsTr("Close")
                onClicked: trimWindow.close()
            }
        }
    }

    UCard {
        anchors.fill: parent
        anchors.margins: Theme.spacingL
        anchors.topMargin: Theme.spacingL + trimWindow.chromeTop

        // --- Header (top) ---
        Column {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Theme.spacingXS
            Text {
                text: qsTr("Trim recording")
                color: Theme.textPrimary; font.pixelSize: Theme.fontXL; font.weight: Font.Bold
            }
            Text {
                width: parent.width; elide: Text.ElideMiddle; text: trimSourcePath
                color: Theme.textSecondary; font.pixelSize: Theme.fontS
            }
        }

        // --- Controls (bottom) ---
        Column {
            id: controls
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Theme.spacingM

            // Timeline (only meaningful with a live playhead).
            Item {
                id: timeline
                width: parent.width
                height: 82
                visible: hasPreview

                function timeAt(x) {
                    return track.width > 0
                           ? Math.max(0, Math.min(x / track.width * trimWindow.duration, trimWindow.duration))
                           : 0
                }
                function xOf(t) {
                    return trimWindow.duration > 0 ? track.width * (t / trimWindow.duration) : 0
                }

                Rectangle {
                    id: track
                    anchors.fill: parent
                    radius: Theme.radiusS
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
                            // Nearest tile in TIME to this cell's centre.
                            sourceClipRect: {
                                const cells = Math.max(1, Math.ceil(track.width / track.cellW))
                                const tiles = trimController.filmstripTiles
                                const i = Math.max(0, Math.min(Math.floor((index + 0.5) / cells * tiles), tiles - 1))
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
                        color: Qt.rgba(0, 0, 0, 0.66)
                    }
                    Rectangle {
                        x: timeline.xOf(trimWindow.trimEnd)
                        width: Math.max(0, track.width - x)
                        height: parent.height
                        color: Qt.rgba(0, 0, 0, 0.66)
                    }
                    Rectangle {
                        x: timeline.xOf(trimWindow.trimStart)
                        width: Math.max(0, timeline.xOf(trimWindow.trimEnd) - x)
                        height: parent.height
                        color: "transparent"
                        border.width: 2
                        border.color: Theme.accent
                    }

                    // Where a lossless cut can actually start. The FULL table
                    // stays in trimController for snapping; the visible ticks
                    // are decimated to the timeline's pixel resolution — an
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
                            if (x - lastX >= minPx) {
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

                    // Click / drag anywhere on the strip to move the playhead.
                    MouseArea {
                        anchors.fill: parent
                        onPressed: (m) => trimWindow.seekTo(timeline.timeAt(m.x))
                        onPositionChanged: (m) => trimWindow.scrubTo(timeline.timeAt(m.x))
                    }
                }

                // Playhead.
                Rectangle {
                    width: 2; height: track.height + 8
                    y: -4
                    color: Theme.textPrimary
                    x: timeline.xOf(trimWindow.playhead) - width / 2
                }

                // IN / OUT handles: x is a one-way function of the time value;
                // dragging maps the pointer into track space and pushes the time
                // back through setStart/setEnd — no binding loop, resize-safe.
                // While one is dragged the preview seeks to it, so the frame on
                // screen is the frame the cut will land on.
                Repeater {
                    model: 2
                    Rectangle {
                        id: handle
                        required property int index
                        readonly property bool isStart: index === 0
                        x: timeline.xOf(isStart ? trimWindow.trimStart : trimWindow.trimEnd) - width / 2
                        y: -5
                        width: 10
                        height: track.height + 10
                        radius: 3
                        color: trimWindow.activeHandle === (isStart ? 1 : 2)
                               ? Qt.lighter(Theme.accent, 1.2) : Theme.accent
                        border.width: 1
                        border.color: Theme.background
                        Rectangle {   // grip
                            anchors.centerIn: parent
                            width: 2; height: 14; radius: 1
                            color: Theme.background
                            opacity: 0.7
                        }
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6      // fat target, thin handle
                            cursorShape: Qt.SizeHorCursor
                            preventStealing: true
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

            // Time readout: playhead position (left), selection summary (right).
            Item {
                width: parent.width
                height: selLabel.implicitHeight
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    visible: hasPreview
                    text: qsTr("%1 / %2").arg(trimWindow.fmt(trimWindow.playhead)).arg(trimWindow.fmt(trimWindow.duration))
                    color: Theme.textPrimary; font.pixelSize: Theme.fontM
                }
                Text {
                    id: selLabel
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Selection: %1 → %2 (%3)")
                          .arg(trimWindow.fmt(trimWindow.trimStart))
                          .arg(trimWindow.fmt(trimWindow.trimEnd))
                          .arg(trimWindow.fmt(trimWindow.trimEnd - trimWindow.trimStart))
                    color: Theme.accent; font.pixelSize: Theme.fontM
                }
            }

            // Transport + mark buttons (preview mode).
            Row {
                width: parent.width
                spacing: Theme.spacingS
                visible: hasPreview
                UIconButton {
                    iconName: (previewLoader.item && previewLoader.item.playing) ? "pause" : "play"
                    iconSize: 18
                    tooltip: qsTr("Play the selection (Space)")
                    onClicked: trimWindow.playPause()
                }
                UIconButton {
                    iconName: "media-repeat"
                    iconSize: 18
                    active: trimWindow.loopSelection
                    tooltip: qsTr("Loop the selection (L)")
                    onClicked: trimWindow.loopSelection = !trimWindow.loopSelection
                }
                UButton { compact: true; variant: "tonal"; text: qsTr("Set start (I)"); onClicked: trimWindow.setStart(trimWindow.playhead) }
                UButton { compact: true; variant: "tonal"; text: qsTr("Set end (O)"); onClicked: trimWindow.setEnd(trimWindow.playhead) }
            }

            // Fallback: no QtMultimedia → blind sliders + a hint.
            Column {
                width: parent.width
                spacing: Theme.spacingS
                visible: !hasPreview
                Text {
                    width: parent.width; wrapMode: Text.WordWrap
                    text: qsTr("Install qt6-qtmultimedia for a video preview. Adjust the range below.")
                    color: Theme.textTertiary; font.pixelSize: Theme.fontS
                }
                Text { text: qsTr("Start: %1 s").arg(trimWindow.trimStart.toFixed(1)); color: Theme.textPrimary; font.pixelSize: Theme.fontM }
                USlider {
                    width: parent.width; from: 0; to: Math.max(0.1, trimWindow.duration - 0.1)
                    value: trimWindow.trimStart
                    // Through setStart(), NOT a raw assignment: with Fast
                    // lossless on, the in-point must snap onto a keyframe here
                    // too, or ffmpeg stream-copies from the previous keyframe
                    // while the UI shows the unsnapped value.
                    onMoved: (v) => trimWindow.setStart(v)
                }
                Text { text: qsTr("End: %1 s").arg(trimWindow.trimEnd.toFixed(1)); color: Theme.textPrimary; font.pixelSize: Theme.fontM }
                USlider {
                    width: parent.width; from: 0.1; to: trimWindow.duration
                    value: trimWindow.trimEnd
                    onMoved: (v) => trimWindow.setEnd(v)
                }
            }

            // Actions.
            Item {
                width: parent.width
                height: Math.max(saveRow.implicitHeight, cutMode.implicitHeight)

                // How the file gets cut. Default (off) re-encodes the selection,
                // which is the only way the saved frames match the preview
                // exactly; a stream copy is instant but starts on a keyframe.
                Row {
                    id: cutMode
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingS
                    visible: !trimController.gif
                    USwitch {
                        id: losslessSwitch
                        anchors.verticalCenter: parent.verticalCenter
                        checked: trimWindow.lossless
                        onToggled: trimWindow.lossless = checked
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1
                        // Constrained + wrapping: at the 680 px minimum width
                        // (and with longer translations) unbounded text ran
                        // underneath the Cancel/Save row on the right.
                        width: Math.max(120, cutMode.parent.width - saveRow.width
                                             - losslessSwitch.width - cutMode.spacing
                                             - Theme.spacingL)
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: qsTr("Fast lossless cut")
                            color: Theme.textPrimary; font.pixelSize: Theme.fontS
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: !trimWindow.lossless
                                  ? qsTr("Off: the selection is re-encoded and starts on the exact frame.")
                                  : (trimController.keyframeState === TrimController.Busy
                                     ? qsTr("Reading keyframes…")
                                     : (trimController.keyframeState === TrimController.Ready
                                        ? qsTr("On: copies the streams, so the start snaps to a keyframe (ticks).")
                                        : qsTr("No keyframes found — saving will re-encode instead.")))
                            color: Theme.textTertiary; font.pixelSize: Theme.fontS
                        }
                    }
                }
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - saveRow.width - Theme.spacingL
                    visible: trimController.gif
                    wrapMode: Text.WordWrap
                    text: qsTr("A GIF is always re-rendered, so the cut lands on the exact frame.")
                    color: Theme.textTertiary; font.pixelSize: Theme.fontS
                }

                Row {
                    id: saveRow
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingS
                    UButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: trimWindow.close() }
                    UButton {
                        text: qsTr("Save trimmed copy")
                        enabled: trimWindow.trimEnd - trimWindow.trimStart >= 0.1
                        onClicked: {
                            App.trimRecording(trimSourcePath, trimWindow.trimStart, trimWindow.trimEnd,
                                              trimWindow.snapping)
                            trimWindow.close()
                        }
                    }
                }
            }
        }

        // --- Preview fills the space between header and controls ---
        Rectangle {
            anchors.top: header.bottom
            anchors.bottom: controls.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: Theme.spacingM
            anchors.bottomMargin: Theme.spacingM
            radius: Theme.radiusM
            color: hasPreview ? "#000000" : Theme.surface
            clip: true

            Loader {
                id: previewLoader
                anchors.fill: parent
                active: hasPreview
                sourceComponent: previewComp
            }
            Component {
                id: previewComp
                VideoPreview { fileUrl: App.fileDragUri(trimSourcePath) }
            }

            // While a handle is being dragged the frame on screen IS the cut
            // point — say which one, so the two are never confused.
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.spacingM
                visible: trimWindow.activeHandle !== 0
                width: edgeLabel.implicitWidth + 2 * Theme.spacingM
                height: edgeLabel.implicitHeight + Theme.spacingS
                radius: height / 2
                color: Qt.rgba(0, 0, 0, 0.72)
                Text {
                    id: edgeLabel
                    anchors.centerIn: parent
                    text: trimWindow.activeHandle === 1
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
    }
}
