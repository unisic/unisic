import QtQuick
import QtMultimedia
import Unisic

// QtMultimedia video surface for the trim editor. Kept in its own file so the
// `import QtMultimedia` is only ever evaluated when App.capVideoPlayback is true
// and a Loader instantiates it — on a box without qt6-qtmultimedia the trim
// window falls back to the slider-only UI instead of erroring on the import.
Item {
    id: root

    // File to preview. player.source is NOT bound to this directly: when the
    // editor is idle in the background we blank the source to release the whole
    // decode pipeline (~150 MB of demuxer/decoder/frame buffers), then reload.
    property url fileUrl
    property bool suspended: false
    property real _resumePos: 0   // ms, restored after a suspend→resume reload

    readonly property real position: player.position        // ms
    readonly property real duration: player.duration         // ms
    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    readonly property bool ready: player.mediaStatus === MediaPlayer.LoadedMedia
                                  || player.mediaStatus === MediaPlayer.BufferedMedia
                                  || player.mediaStatus === MediaPlayer.EndOfMedia

    function play() { player.play() }
    function pause() { player.pause() }
    function togglePlay() { playing ? player.pause() : player.play() }
    // ms, clamped; pausing on manual seek keeps the frame steady while scrubbing.
    function seek(ms) { player.setPosition(Math.max(0, Math.min(ms, player.duration))) }

    // Tear the decode pipeline down while the editor sits unused in the
    // background; remember where we were so resume() lands on the same frame.
    function suspend() {
        if (suspended)
            return
        _resumePos = player.position
        suspended = true   // → source binding blanks → media unloads
    }
    function resume() {
        if (!suspended)
            return
        suspended = false  // → source reloads; onMediaStatusChanged re-seeks
    }

    MediaPlayer {
        id: player
        source: root.suspended ? "" : root.fileUrl
        videoOutput: vout
        audioOutput: AudioOutput { id: aout }
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia && root._resumePos > 0) {
                setPosition(root._resumePos)
                root._resumePos = 0
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        radius: Theme.radiusM
        clip: true
        VideoOutput {
            id: vout
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectFit
        }
        // Big centered play/pause hit target over the frame.
        MouseArea {
            anchors.fill: parent
            onClicked: root.togglePlay()
        }
    }
}
