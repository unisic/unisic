import QtQuick
import Unisic
import Unisic.Kit

// A seven-step guided tour that DRIVES the window instead of describing it:
// each step navigates to the page it is talking about and the card sits in the
// corner, so the thing being named is on screen while you read about it.
//
// That is why it is NOT a modal Popup like USystemCheck. A modal dims and
// covers exactly the page the step is pointing at, which turns a tour into a
// wall of text. This is a floating card over a live window: the sidebar still
// works, the page underneath is real, and a step can offer to actually DO the
// thing it just described.
//
// A spotlight cut-out is still not possible and should not be retried: the
// tool letters belong to OverlayWindow and EditorWindow, which do not exist
// while the main window is up; every page lives behind a Loader destroyed on
// navigation, so a highlighted Item vanishes as the tour advances; and a hole
// plus a follower tooltip opening under the pointer is what the layout rules
// forbid. Naming the control and putting its page on screen gets most of the
// value with none of that.
//
// The tool letters in the last step are read LIVE from ToolCatalog - a second
// hardcoded list would be wrong the first time a shortcut moved.
//
// To remove the feature completely: delete this file, its line in
// CMakeLists.txt, the tourSeen setting, the tourLoader block and its
// Connections handler in Main.qml, the send-off button in UWelcome.qml, the
// Settings row, showTour(), devTestTour(), tourCardCheck() and its smoke step.
Item {
    id: root

    // A manual peek from Settings must never consume the one-shot latch, the
    // same rule UWelcome and USystemCheck follow.
    property bool markSeenOnClose: true
    property int step: 0
    readonly property int stepCount: 7
    property bool showing: false

    // The host owns navigation and the shortcut sheet; the tour only asks.
    signal pageRequested(int page)
    signal shortcutSheetRequested()
    signal finished()

    // Live from the single source of the annotation tools AND their letters.
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
        var p = stepData(step).page
        if (p >= 0)
            root.pageRequested(p)
    }
    function next() {
        if (step + 1 < stepCount) {
            step++
            applyStep()
        } else {
            closeTour()
        }
    }
    function back() {
        if (step > 0) {
            step--
            applyStep()
        }
    }
    function closeTour() {
        showing = false
        if (markSeenOnClose)
            App.settings.tourSeen = true
        root.finished()
    }

    anchors.fill: parent
    visible: showing
    // Transparent and without a MouseArea of its own, so everything outside the
    // card stays clickable: the page below is the point.
    z: 900

    // Escape closes, as everywhere else. A Shortcut rather than a Keys handler
    // because the card deliberately does not hold focus.
    Shortcut {
        enabled: root.showing && !App.shortcutRecording
        sequences: ["Escape"]
        onActivated: root.closeTour()
    }

    // Each step is { page, title, intro, rows: [[what, does]], action, note }.
    // A function, not a property, so every qsTr() re-runs on a language change
    // while the tour is open. page = -1 means "leave the window where it is".
    function stepData(i) {
        if (i === 0) {
            return {
                page: 0,
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
                page: 1,
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
                page: 2,
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
                page: 3,
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
                page: 4,
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
                page: 5,
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
            page: -1,
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

    // NOT named "data": that is Item's default property, the one every
    // child is appended to, and shadowing it silently breaks the card.
    readonly property var stepInfo: stepData(step)

    Rectangle {
        id: card
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingL
        width: Math.min(430, parent.width - 2 * Theme.spacingL)
        height: Math.min(bodyCol.implicitHeight + 2 * Theme.spacingL,
                         parent.height - 2 * Theme.spacingL)
        radius: Theme.radiusL
        color: Theme.surface
        border.width: 1
        border.color: Theme.accent

        // The card floats over a live page, so it needs to read as lifted, not
        // as part of it. Colour and a border only - no drop shadow item, which
        // would be another scene-graph layer for nothing.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: parent.radius + 1
            color: "transparent"
            border.width: 1
            border.color: Theme.alpha(Theme.accent, 0.25)
            z: -1
        }

        // Swallows clicks so a press on the card never reaches the page below.
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
                spacing: Theme.spacingM

                // Accessible attaches to an Item, and it carries the step
                // position because a screen reader user has no dots to look at.
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
                    spacing: Theme.spacingS
                    Repeater {
                        model: root.stepInfo.rows
                        delegate: Column {
                            width: bodyCol.width
                            spacing: 1
                            Text {
                                text: modelData[0]
                                color: Theme.accent
                                font.pixelSize: Theme.fontS
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                                width: parent.width
                                Accessible.ignored: true
                            }
                            Text {
                                text: modelData[1]
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontS
                                wrapMode: Text.WordWrap
                                width: parent.width
                                Accessible.ignored: true
                            }
                        }
                    }
                }

                // The tool letters, only on the last step and only from the
                // catalog. A Flow, because the number of lettered tools is not
                // fixed - hiding tools in Settings shortens this list.
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

                // The step's own action, where there honestly is one: a tour
                // that says "you can do X" and then lets you do X beats a tour
                // that only says it.
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

                Item { width: 1; height: Theme.spacingXS }

                Item {
                    width: parent.width
                    height: Math.max(dots.height, navRow.height)

                    // Position, drawn. Never interactive: a dot small enough to
                    // fit is too small to be a target, and the buttons already
                    // move in both directions.
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
