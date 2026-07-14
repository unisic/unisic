import QtQuick
import QtQuick.Window
import Unisic
import "components"

Window {
    id: trimWindow
    width: 860; height: 620
    minimumWidth: 640; minimumHeight: 460
    visible: true
    title: qsTr("Trim recording")
    color: Theme.background

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

    function fmt(s) {
        if (isNaN(s) || s < 0) s = 0
        const m = Math.floor(s / 60)
        const sec = s - m * 60
        return m + ":" + (sec < 10 ? "0" : "") + sec.toFixed(1)
    }
    function setStart(t) { trimStart = Math.max(0, Math.min(t, trimEnd - 0.1)) }
    function setEnd(t) { trimEnd = Math.max(trimStart + 0.1, Math.min(t, duration)) }
    function seekTo(t) {
        if (hasPreview && previewLoader.item)
            previewLoader.item.seek(Math.max(0, Math.min(t, duration)) * 1000)
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
            if (!hasPreview) return
            switch (e.key) {
            case Qt.Key_Space: previewLoader.item.togglePlay(); e.accepted = true; break
            case Qt.Key_I: setStart(playhead); e.accepted = true; break
            case Qt.Key_O: setEnd(playhead); e.accepted = true; break
            case Qt.Key_Left: seekTo(playhead - (e.modifiers & Qt.ShiftModifier ? 5 : 1)); e.accepted = true; break
            case Qt.Key_Right: seekTo(playhead + (e.modifiers & Qt.ShiftModifier ? 5 : 1)); e.accepted = true; break
            case Qt.Key_Home: seekTo(0); e.accepted = true; break
            case Qt.Key_End: seekTo(duration); e.accepted = true; break
            }
        }
        Component.onCompleted: keys.forceActiveFocus()
    }

    UCard {
        anchors.fill: parent
        anchors.margins: Theme.spacingL

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
                width: parent.width
                height: 46
                visible: hasPreview
                Rectangle {
                    id: track
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 8; radius: 4
                    color: Theme.surfaceHi
                    // Selected keep-range.
                    Rectangle {
                        x: trimWindow.duration > 0 ? track.width * (trimWindow.trimStart / trimWindow.duration) : 0
                        width: trimWindow.duration > 0
                               ? track.width * ((trimWindow.trimEnd - trimWindow.trimStart) / trimWindow.duration) : 0
                        height: parent.height; radius: parent.radius
                        color: Theme.accent
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: (m) => trimWindow.seekTo(m.x / track.width * trimWindow.duration)
                    }
                }
                // Playhead.
                Rectangle {
                    width: 2; height: 34; radius: 1; color: Theme.textPrimary
                    anchors.verticalCenter: parent.verticalCenter
                    x: track.x + (trimWindow.duration > 0
                                  ? track.width * (trimWindow.playhead / trimWindow.duration) : 0) - width / 2
                }
                // IN / OUT handles: x is a one-way function of the time value;
                // dragging maps the pointer into track space and pushes the time
                // back through setStart/setEnd — no binding loop, resize-safe.
                Rectangle {
                    id: inHandle
                    width: 12; height: 40; radius: 6
                    color: Theme.accent; border.width: 2; border.color: Theme.background
                    anchors.verticalCenter: parent.verticalCenter
                    x: track.x + (trimWindow.duration > 0
                                  ? track.width * (trimWindow.trimStart / trimWindow.duration) : 0) - width / 2
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.SizeHorCursor
                        onPositionChanged: (m) => trimWindow.setStart(
                            mapToItem(track, m.x, 0).x / track.width * trimWindow.duration)
                    }
                }
                Rectangle {
                    id: outHandle
                    width: 12; height: 40; radius: 6
                    color: Theme.accent; border.width: 2; border.color: Theme.background
                    anchors.verticalCenter: parent.verticalCenter
                    x: track.x + (trimWindow.duration > 0
                                  ? track.width * (trimWindow.trimEnd / trimWindow.duration) : 0) - width / 2
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.SizeHorCursor
                        onPositionChanged: (m) => trimWindow.setEnd(
                            mapToItem(track, m.x, 0).x / track.width * trimWindow.duration)
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
                    onClicked: if (previewLoader.item) previewLoader.item.togglePlay()
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
                    onMoved: (v) => trimWindow.trimStart = Math.min(v, trimWindow.trimEnd - 0.1)
                }
                Text { text: qsTr("End: %1 s").arg(trimWindow.trimEnd.toFixed(1)); color: Theme.textPrimary; font.pixelSize: Theme.fontM }
                USlider {
                    width: parent.width; from: 0.1; to: trimWindow.duration
                    value: trimWindow.trimEnd
                    onMoved: (v) => trimWindow.trimEnd = Math.max(v, trimWindow.trimStart + 0.1)
                }
            }

            // Actions.
            Row {
                anchors.right: parent.right
                spacing: Theme.spacingS
                UButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: trimWindow.close() }
                UButton {
                    text: qsTr("Save trimmed copy")
                    enabled: trimWindow.trimEnd - trimWindow.trimStart >= 0.1
                    onClicked: {
                        App.trimRecording(trimSourcePath, trimWindow.trimStart, trimWindow.trimEnd)
                        trimWindow.close()
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
