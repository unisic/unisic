import QtQuick
import Unisic
import Unisic.Kit

// The guided tour: the window dims, a hole is cut over one real control, and
// Uni sits next to it with a speech bubble saying what it is and what you can
// do with it. The control inside the hole stays LIVE - it is the only thing
// still clickable - so a step that says "these three tiles take the shot" can
// be answered by taking one.
//
// How the hole finds its control, and why it is not a registry: every
// highlightable item carries objectName: "tour.<id>" and nothing else.
// AppContext::tourTargetRect() resolves that with findChild and maps it to
// window coordinates. A control that moves, is renamed or is deleted simply
// stops being found, and an unfound target degrades to "dim everything, no
// hole" rather than to a wrong hole somewhere else.
//
// The earlier objection to a spotlight was real but narrower than it looked:
// it applies to controls in OverlayWindow and EditorWindow, which do not exist
// while the main window is up, and to a page whose Loader has been destroyed.
// The tour owns navigation, so it only ever highlights a control on the page it
// has just switched to. Those two windows are covered by the closing step,
// which describes their keys instead of pointing at them.
//
// Geometry is re-read rather than bound: mapToScene is not a bindable
// expression, so a binding would go stale on a resize. Re-read on step change,
// on any window resize, and on a short poll after navigation, because the
// page's Loader needs a frame or two before its children have a size.
Item {
    id: root

    // A manual peek from Settings must never consume the one-shot latch, the
    // same rule UWelcome and USystemCheck follow.
    property bool markSeenOnClose: true
    property int step: 0
    readonly property int stepCount: 7
    property bool showing: false
    // Window coordinates are offset from this item by the custom title bar.
    property real sceneOffsetY: 0

    // The host owns navigation and the shortcut sheet; the tour only asks.
    signal pageRequested(int page)
    signal shortcutSheetRequested()
    signal finished()

    readonly property var letteredTools: {
        var out = []
        var all = ToolCatalog.visibleFor("editor", "")
        for (var i = 0; i < all.length; ++i)
            if (all[i].shortcut)
                out.push(all[i])
        return out
    }

    function openTour(markSeen) {
        markSeenOnClose = markSeen
        step = 0
        showing = true
        applyStep()
    }
    function applyStep() {
        var d = stepData(step)
        if (d.page >= 0)
            root.pageRequested(d.page)
        hole = Qt.rect(0, 0, 0, 0)
        settleTries = 0
        settle.restart()
    }
    function next() {
        if (step + 1 < stepCount) { step++; applyStep() } else { closeTour() }
    }
    function back() {
        if (step > 0) { step--; applyStep() }
    }
    function closeTour() {
        settle.stop()
        showing = false
        if (markSeenOnClose)
            App.settings.tourSeen = true
        root.finished()
    }

    // The hole, in THIS item's coordinates. Empty means "no control to point
    // at": everything dims and Uni speaks from the middle.
    property rect hole: Qt.rect(0, 0, 0, 0)
    readonly property bool hasHole: hole.width > 0 && hole.height > 0
    readonly property int holePad: 8

    function readHole() {
        var id = stepData(step).target
        if (id === "") {
            hole = Qt.rect(0, 0, 0, 0)
            return true
        }
        var r = App.tourTargetRect(id)
        if (r.width <= 0 || r.height <= 0) {
            hole = Qt.rect(0, 0, 0, 0)
            return false
        }
        hole = Qt.rect(r.x - holePad,
                       r.y - root.sceneOffsetY - holePad,
                       r.width + 2 * holePad,
                       r.height + 2 * holePad)
        return true
    }

    property int settleTries: 0
    // The page behind a Loader is not laid out on the frame the tour asks for
    // it, so poll briefly instead of reading once and giving up. Stops as soon
    // as the rect is real, and gives up quietly after two seconds rather than
    // polling for the life of the step.
    Timer {
        id: settle
        interval: 80
        repeat: true
        running: false
        onTriggered: {
            if (root.readHole() || ++root.settleTries > 25)
                settle.stop()
        }
    }
    onWidthChanged: if (showing) readHole()
    onHeightChanged: if (showing) readHole()

    anchors.fill: parent
    visible: showing
    z: 900

    Shortcut {
        enabled: root.showing && !App.shortcutRecording
        sequences: ["Escape"]
        onActivated: root.closeTour()
    }

    // page = which page to switch to (-1 = leave it), target = objectName
    // suffix of the control to cut the hole over ("" = no hole).
    function stepData(i) {
        if (i === 0) {
            return {
                page: 0, target: "capture.tiles",
                title: qsTr("Capture: this page takes the shot"),
                intro: qsTr("The three tiles at the top are the ways to take one. Everything under them decides what happens to it afterwards."),
                rows: [
                    [qsTr("The three tiles"), qsTr("The whole screen, a region you drag, or a window you click")],
                    [qsTr("Delay"), qsTr("Waits before the shot, so you can open a menu first")],
                    [qsTr("Repeat last region"), qsTr("Retakes the exact area of your last region shot")],
                    [qsTr("Upload server"), qsTr("Which destination the Upload switch below sends to")],
                    [qsTr("The switches"), qsTr("Copy, save, upload and open the editor - each one fires on its own")]
                ],
                action: qsTr("Take a region shot now"),
                note: qsTr("Every one of these also has a global shortcut, so you rarely come back to this page.")
            }
        }
        if (i === 1) {
            return {
                page: 1, target: "record.mode",
                title: qsTr("Record: video and GIF on one page"),
                intro: qsTr("The switch at the top picks what comes out. The three buttons under it stay where they are and only change what they will produce."),
                rows: [
                    [qsTr("Video or GIF"), qsTr("The same recording, encoded differently. GIF has no window source, so that button greys out")],
                    [qsTr("Screen, Region, Window"), qsTr("What gets recorded. A region is cropped by the compositor, not afterwards")],
                    [qsTr("Instant replay"), qsTr("Keeps the last seconds in memory the whole time, so you can save something after it has happened")],
                    [qsTr("Cursor and keystrokes"), qsTr("Optional overlays drawn into the recording as it is made")]
                ],
                action: "",
                note: qsTr("A recording can be trimmed afterwards without re-encoding it, from History.")
            }
        }
        if (i === 2) {
            return {
                page: 2, target: "edit.tiles",
                title: qsTr("Edit: the editor takes files you already have"),
                intro: qsTr("It opens by itself after a capture, and this page is how you get an existing file into it."),
                rows: [
                    [qsTr("Open a file"), qsTr("An image goes to the editor, a recording goes to the trim window")],
                    [qsTr("Drop it on the window"), qsTr("Anywhere in Unisic. The banner names which of the two it will open")],
                    ["Ctrl+V", qsTr("Opens whatever image is on the clipboard as a new capture")],
                    [qsTr("Ctrl+S in the editor"), qsTr("Writes over the file you opened. A pasted image has no file, so it is saved as a new one")]
                ],
                action: "",
                note: qsTr("A dragged web address is refused with a message rather than downloaded quietly.")
            }
        }
        if (i === 3) {
            return {
                page: 3, target: "history.search",
                title: qsTr("History: everything you have captured"),
                intro: qsTr("Kept with a thumbnail, searchable, and the fastest way back to a file you took an hour ago."),
                rows: [
                    [qsTr("Search and filters"), qsTr("By name, by kind, favourites only, uploaded only")],
                    [qsTr("Arrow keys"), qsTr("Move between captures. Space selects, Enter opens, Delete removes")],
                    [qsTr("Drag a thumbnail"), qsTr("Drops the file straight into a chat window or another editor")],
                    [qsTr("Shift and click"), qsTr("Selects a range, for deleting or favouriting many at once")]
                ],
                action: "",
                note: qsTr("Instant replays are filtered separately from ordinary recordings, even though both are .mp4 files.")
            }
        }
        if (i === 4) {
            return {
                page: 4, target: "servers.add",
                title: qsTr("Servers: where Upload sends things"),
                intro: qsTr("Any custom HTTP, FTP or SFTP endpoint works, and the link it answers with is copied for you."),
                rows: [
                    [qsTr("Add custom server"), qsTr("Your own endpoint, with the response parsed for the link")],
                    [qsTr("Import .sxcu"), qsTr("Takes ShareX destination files as they are")],
                    [qsTr("Test upload"), qsTr("Pushes a tiny generated image through the server you are editing, before you save it")],
                    [qsTr("Imgur"), qsTr("Paste your own Client-ID. Unisic ships without one so nobody shares a daily limit")]
                ],
                action: "",
                note: qsTr("A test against an FTP server really does leave the test image behind, and the sheet says so first.")
            }
        }
        if (i === 5) {
            return {
                page: 5, target: "settings.search",
                title: qsTr("Settings: the gear at the bottom, or Ctrl+6"),
                intro: qsTr("Everything the pages do not carry, on the same bordered-card grid. The search box at the top jumps straight to a row."),
                rows: [
                    [qsTr("Hotkeys"), qsTr("Global shortcuts that work while Unisic sits in the tray")],
                    [qsTr("Appearance"), qsTr("Nine palettes, and a folder you can drop your own theme file into")],
                    [qsTr("After capture"), qsTr("Filenames, formats, the sound cue and where files are saved")],
                    [qsTr("Diagnostics"), qsTr("Copies a report, with the activity log, for a bug report. Nothing is sent anywhere")]
                ],
                action: "",
                note: qsTr("Every row has a \"?\" that explains what the option actually changes.")
            }
        }
        return {
            page: -1, target: "sidebar",
            title: qsTr("The keys worth knowing"),
            intro: qsTr("Two vocabularies. The tool letters are the same on the selection overlay and in the editor, so the tool you want is one key away in both."),
            rows: [
                [qsTr("Space or Enter"), qsTr("On the overlay: take the shot")],
                [qsTr("Escape"), qsTr("On the overlay: cancel without capturing")],
                ["Ctrl+C", qsTr("On the overlay: confirm and copy, even with auto-copy off")],
                [qsTr("Ctrl+scroll"), qsTr("On the overlay: zoom the pixel loupe while you aim")],
                ["Ctrl+1 ... Ctrl+6", qsTr("Jump between the pages of this window")]
            ],
            action: qsTr("Show the shortcut sheet"),
            note: qsTr("That is the tour. It stays in Settings, General, if you want it again.")
        }
    }

    readonly property var stepInfo: stepData(step)

    // ---- the dim, as four rectangles around the hole ----
    // Four plain rectangles rather than a shader or an OpacityMask: it works on
    // every backend, needs no extra render pass, and leaves the control inside
    // the hole untouched instead of drawing a masked copy of it. Each one eats
    // clicks, so the ONLY live thing in the window is what the hole exposes.
    component Dim: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.62)
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true }
    }

    Dim { // above the hole, or the whole window when there is none
        x: 0; y: 0
        width: parent.width
        height: root.hasHole ? Math.max(0, root.hole.y) : parent.height
    }
    Dim { // below
        visible: root.hasHole
        x: 0
        y: root.hole.y + root.hole.height
        width: parent.width
        height: Math.max(0, parent.height - y)
    }
    Dim { // left
        visible: root.hasHole
        x: 0
        y: root.hole.y
        width: Math.max(0, root.hole.x)
        height: root.hole.height
    }
    Dim { // right
        visible: root.hasHole
        x: root.hole.x + root.hole.width
        y: root.hole.y
        width: Math.max(0, parent.width - x)
        height: root.hole.height
    }

    // The ring around the live control. Not a MouseArea and not filled - the
    // control underneath has to stay clickable, which is the whole point.
    Rectangle {
        visible: root.hasHole
        x: root.hole.x; y: root.hole.y
        width: root.hole.width; height: root.hole.height
        radius: Theme.radiusM
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        Behavior on x { NumberAnimation { duration: Theme.animFast } }
        Behavior on y { NumberAnimation { duration: Theme.animFast } }
        Behavior on width { NumberAnimation { duration: Theme.animFast } }
        Behavior on height { NumberAnimation { duration: Theme.animFast } }

        // A slow breath, so the eye finds the ring. Nothing the pointer could
        // be over moves: the ring sits outside the control, and only its
        // opacity changes.
        SequentialAnimation on opacity {
            running: root.showing
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 0.45; duration: 1100; easing.type: Easing.InOutQuad }
            NumberAnimation { from: 0.45; to: 1.0; duration: 1100; easing.type: Easing.InOutQuad }
        }
    }

    // ---- Uni and her speech bubble ----
    // Placed on the side of the hole with more room, so she never covers the
    // control she is pointing at. Horizontally centred on the hole and clamped
    // to the window.
    readonly property bool speakBelow: !hasHole || (hole.y + hole.height / 2) < height / 2
    // Uni and the bubble are ONE clamped group, laid out side by side. Placing
    // her relative to the bubble AFTER the bubble was clamped is what let her
    // run off the window edge: every clamp fixed the bubble and she was
    // computed from it afterwards. Now the clamp is applied to both together.
    readonly property real uniW: uniShown ? Math.round(uniH * 0.66) : 0   // the art is ~564x855
    readonly property real uniH: 190
    // She is dropped, not squeezed, when the window is too small to hold her
    // beside a readable bubble.
    readonly property bool uniShown: width >= 620 && height >= 360
    readonly property real bubbleW: Math.min(460, width - 2 * Theme.spacingL - uniW)
    readonly property real groupW: bubbleW + uniW

    Item {
        id: group
        width: root.groupW
        height: bubble.height
        x: {
            var want = root.hasHole ? root.hole.x + root.hole.width / 2 - width / 2
                                    : (root.width - width) / 2
            return Math.max(Theme.spacingL,
                            Math.min(root.width - width - Theme.spacingL, want))
        }
        y: {
            if (!root.hasHole)
                return Math.max(Theme.spacingL, (root.height - height) / 2)
            var want = root.speakBelow ? root.hole.y + root.hole.height + Theme.spacingM
                                       : root.hole.y - height - Theme.spacingM
            return Math.max(Theme.spacingL,
                            Math.min(root.height - height - Theme.spacingL, want))
        }
        Behavior on x { NumberAnimation { duration: Theme.animFast } }
        Behavior on y { NumberAnimation { duration: Theme.animFast } }

        // Uni, inside the group so she is clamped with it and can never leave
        // the window. Bottom-aligned with the bubble, the way she sits on a
        // window frame in the app's own artwork.
        Image {
            id: uni
            source: "qrc:/docs/uni.png"
            visible: root.uniShown
            width: root.uniW
            height: root.uniH
            // sourceSize keeps the decode small instead of holding the
            // full-size art in memory for a 190 px draw.
            sourceSize.height: root.uniH
            fillMode: Image.PreserveAspectFit
            smooth: true
            z: 4
            x: 0
            y: bubble.height - height + 8
            Accessible.ignored: true
        }

        // The tail: a rotated square poking out of the bubble toward the hole,
        // clamped so it never rides off the rounded corners.
        Rectangle {
            visible: root.hasHole
            width: 14; height: 14
            rotation: 45
            color: Theme.surface
            border.width: 1
            border.color: Theme.accent
            z: 2
            x: Math.max(root.uniW + Theme.radiusL,
                        Math.min(group.width - Theme.radiusL - width,
                                 root.hole.x + root.hole.width / 2 - group.x - width / 2))
            y: root.speakBelow ? -height / 2 : bubble.height - height / 2
        }

        Rectangle {
            id: bubble
            x: root.uniW
            width: root.bubbleW
            y: 0
            height: Math.min(bodyFlick.contentHeight + 2 * Theme.spacingL,
                             root.height - 2 * Theme.spacingL)
            radius: Theme.radiusL
            color: Theme.surface
            border.width: 1
            border.color: Theme.accent
            z: 3

            // Keeps a click on the bubble from reaching the live control on the
            // rare step where the two overlap.
            MouseArea { anchors.fill: parent }

            Flickable {
                id: bodyFlick
                anchors.fill: parent
                anchors.margins: Theme.spacingL
                contentWidth: width
                contentHeight: bodyCol.implicitHeight
                clip: true
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: bodyCol
                    width: bodyFlick.width
                    spacing: Theme.spacingS

                    // Accessible attaches to an Item, and it carries the step
                    // position because a screen reader user has neither the
                    // dots nor the spotlight to look at.
                    Accessible.role: Accessible.Dialog
                    Accessible.name: qsTr("Tour, step %1 of %2: %3")
                                        .arg(root.step + 1).arg(root.stepCount).arg(root.stepInfo.title)

                    Text {
                        width: parent.width
                        text: root.stepInfo.title
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontL
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true
                    }
                    Text {
                        width: parent.width
                        text: root.stepInfo.intro
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontS
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true
                    }

                    Column {
                        width: parent.width
                        spacing: Theme.spacingXS
                        Repeater {
                            model: root.stepInfo.rows
                            delegate: Column {
                                width: bodyCol.width
                                spacing: 1
                                Text {
                                    width: parent.width
                                    text: modelData[0]
                                    color: Theme.accent
                                    font.pixelSize: Theme.fontS
                                    font.weight: Font.DemiBold
                                    wrapMode: Text.WordWrap
                                    Accessible.ignored: true
                                }
                                Text {
                                    width: parent.width
                                    text: modelData[1]
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontS
                                    wrapMode: Text.WordWrap
                                    Accessible.ignored: true
                                }
                            }
                        }
                    }

                    // The tool letters, only on the closing step and only from
                    // the catalog - hiding tools in Settings shortens this list.
                    Flow {
                        width: parent.width
                        spacing: Theme.spacingXS
                        visible: root.step === root.stepCount - 1
                        Repeater {
                            model: root.letteredTools
                            delegate: Rectangle {
                                width: letterText.implicitWidth + Theme.spacingM
                                height: letterText.implicitHeight + Theme.spacingXS
                                radius: Theme.radiusS
                                color: Theme.alpha(Theme.accent, 0.12)
                                Text {
                                    id: letterText
                                    anchors.centerIn: parent
                                    text: modelData.shortcut + "  " + modelData.label
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontS
                                    Accessible.ignored: true
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        text: root.stepInfo.note
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true
                    }

                    // The step's own action, where there honestly is one. The
                    // highlighted control is live too, so this is a second way
                    // to do the thing, not the only one.
                    UButton {
                        visible: root.stepInfo.action !== ""
                        text: root.stepInfo.action
                        variant: "tonal"
                        compact: true
                        onClicked: {
                            if (root.step === 0)
                                App.captureRegion()
                            else
                                root.shortcutSheetRequested()
                        }
                    }

                    Item { width: 1; height: 1 }

                    Item {
                        width: parent.width
                        height: Math.max(dots.height, navRow.height)

                        // Position, drawn. Never interactive: a dot small
                        // enough to fit is too small to be a target, and the
                        // buttons already move in both directions.
                        Row {
                            id: dots
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spacingXS
                            Accessible.ignored: true
                            Repeater {
                                model: root.stepCount
                                delegate: Rectangle {
                                    width: 6; height: 6; radius: 3
                                    color: index === root.step ? Theme.accent
                                                               : Theme.alpha(Theme.textPrimary, 0.25)
                                }
                            }
                        }

                        Row {
                            id: navRow
                            anchors.right: parent.right
                            spacing: Theme.spacingS
                            UButton {
                                text: qsTr("Close")
                                variant: "ghost"
                                compact: true
                                accessibleDescription: qsTr("Close the tour. It stays available in Settings.")
                                onClicked: root.closeTour()
                            }
                            UButton {
                                text: qsTr("Back")
                                variant: "ghost"
                                compact: true
                                enabled: root.step > 0
                                onClicked: root.back()
                            }
                            UButton {
                                text: root.step + 1 < root.stepCount ? qsTr("Next") : qsTr("Done")
                                variant: "filled"
                                compact: true
                                onClicked: root.next()
                            }
                        }
                    }
                }
            }
        }
    }

}
