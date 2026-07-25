import QtQuick
import QtQuick.Controls
import Unisic
import Unisic.Kit

// A six-step tour of the things a new user genuinely cannot discover, offered
// (never forced) after the first-run welcome and permanently available from
// Settings > General.
//
// Deliberately a modal card sequence in the same shell as USystemCheck, and
// NOT a spotlight overlay. A spotlight is not merely awkward here, it is
// impossible: the two steps worth having point at controls in OverlayWindow
// and EditorWindow, which do not exist while the main window is up; every page
// lives behind a Loader that is destroyed on navigation, so a highlighted Item
// vanishes as the tour advances; and a hole plus a follower tooltip opening
// under the pointer is exactly what the layout rules forbid. A passive hint
// strip is worse in a quieter way - CapturePage has 44 px of slack at
// 1060x700, so a strip pushes its option grid below the fold.
//
// The tool letters are read LIVE from ToolCatalog, never copied: a second
// hardcoded list would be wrong the first time a shortcut changed.
//
// To remove the feature completely: delete this file, its line in
// CMakeLists.txt, the tourSeen setting, the tourLoader block and the send-off
// button in UWelcome.qml, and the Settings row that opens it.
Popup {
    id: root

    // A manual peek from Settings must never consume the one-shot latch, the
    // same rule UWelcome and USystemCheck follow.
    property bool markSeenOnClose: true
    property int step: 0
    readonly property int stepCount: 6

    // Live from the single source of the annotation tools AND their letters.
    readonly property var editorTools: ToolCatalog.visibleFor("editor", "")

    function openTour(markSeen) {
        markSeenOnClose = markSeen
        step = 0
        open()
    }
    function next() {
        if (step + 1 < stepCount)
            step++
        else
            close()
    }
    function back() {
        if (step > 0)
            step--
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    margins: UFlyout.margin
    modal: true
    focus: true
    // NOT CloseOnPressOutside: a six-step sequence dismissed by a stray click
    // on step four cannot be resumed, and there is a Skip button that says so.
    closePolicy: Popup.CloseOnEscape
    width: Math.min(600, parent ? parent.width - 2 * Theme.spacingXL : 600)
    padding: Theme.spacingXL

    onClosed: if (markSeenOnClose) App.settings.tourSeen = true

    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.45) }

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surface
        border.width: 1
        border.color: Theme.divider
    }

    // Each step is { title, intro, rows: [[key, what], ...], note }. Written as
    // a function rather than a property so every qsTr() re-runs on a language
    // change while the tour is open.
    function stepData(i) {
        if (i === 0) {
            var rows = []
            for (var t = 0; t < editorTools.length && rows.length < 8; ++t) {
                var tool = editorTools[t]
                if (tool.shortcut)
                    rows.push([tool.shortcut, tool.label])
            }
            return {
                title: qsTr("Every tool has a letter"),
                intro: qsTr("The same letters work on the selection overlay and in the editor, so the tool you reach for is one key away in both. These are the current bindings, read from the toolbar itself."),
                rows: rows,
                note: qsTr("Hover any tool button to see its letter again.")
            }
        }
        if (i === 1) {
            return {
                title: qsTr("The selection overlay has its own keys"),
                intro: qsTr("While you are dragging out a region, the overlay owns the keyboard. It is not the main window, so the usual shortcuts do not apply."),
                rows: [
                    [qsTr("Space or Enter"), qsTr("Take the shot")],
                    [qsTr("Escape"), qsTr("Cancel without capturing")],
                    ["Ctrl+C", qsTr("Confirm and copy, even with auto-copy off")],
                    [qsTr("Ctrl+scroll"), qsTr("Zoom the pixel loupe while you aim")],
                    [qsTr("A letter"), qsTr("Annotate before the shot is taken")]
                ],
                note: qsTr("Annotating on the overlay means the shot is finished the moment you press Space.")
            }
        }
        if (i === 2) {
            return {
                title: qsTr("Bringing a file in"),
                intro: qsTr("Unisic opens what you give it, and what the file IS decides where it goes - not where it came from."),
                rows: [
                    [qsTr("Drop an image"), qsTr("Opens in the editor, and Ctrl+S writes over the original")],
                    [qsTr("Drop a recording"), qsTr("Opens in the trim window")],
                    ["Ctrl+V", qsTr("Opens a copied image as a new capture")],
                    [qsTr("Drop a web link"), qsTr("Refused with a message, never downloaded quietly")]
                ],
                note: qsTr("A pasted image has no file of its own, so Ctrl+S saves it as a new capture instead of overwriting anything.")
            }
        }
        if (i === 3) {
            return {
                title: qsTr("Taking the same shot again"),
                intro: qsTr("Three things exist for the capture you just took, or the one you keep retaking."),
                rows: [
                    [qsTr("Repeat last region"), qsTr("Retakes the exact area of your last region shot, from the Capture page or the tray")],
                    [qsTr("Copy last capture"), qsTr("A global shortcut that puts the newest shot back on the clipboard")],
                    [qsTr("Drag the notification"), qsTr("Drag the thumbnail straight into a chat or an editor")],
                    [qsTr("Click the notification"), qsTr("Opens a floating preview you can pin on top")]
                ],
                note: qsTr("The floating preview can be pinned above other windows and faded, for copying something while you work.")
            }
        }
        if (i === 4) {
            return {
                title: qsTr("Uploading somewhere of your own"),
                intro: qsTr("Servers takes any custom HTTP, FTP or SFTP destination, and imports ShareX .sxcu files as they are."),
                rows: [
                    [qsTr("Test upload"), qsTr("Sends a tiny generated image through the server you are editing, before you save it")],
                    [qsTr("Imgur"), qsTr("Needs your own Client-ID, pasted in the server editor")],
                    [qsTr("After upload"), qsTr("The link can be copied and the page opened for you")]
                ],
                note: qsTr("Unisic ships without an Imgur ID on purpose: one shared ID would put every user on one daily limit.")
            }
        }
        return {
            title: qsTr("Instant replay"),
            intro: qsTr("Recording can keep the last few seconds in memory the whole time, so you can save something after it has already happened."),
            rows: [
                [qsTr("Turn it on"), qsTr("Record page, Instant replay")],
                [qsTr("Save the buffer"), qsTr("A global shortcut writes the last seconds to a file")],
                [qsTr("Find it later"), qsTr("History filters replays separately from recordings")]
            ],
            note: qsTr("That is the tour. Everything in it is in Settings too, and this card lives in Settings > General.")
        }
    }

    readonly property var data: stepData(step)

    contentItem: Flickable {
        id: bodyFlick
        implicitHeight: UFlyout.fitHeight(root.parent, bodyCol.implicitHeight
                                          + root.topPadding + root.bottomPadding)
                        - root.topPadding - root.bottomPadding
        contentWidth: width
        contentHeight: bodyCol.implicitHeight
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: bodyCol
            width: bodyFlick.width
            spacing: Theme.spacingM

            // Accessible attaches to an Item, never to the Popup, so the dialog
            // identity lives here - and it carries the step position, because a
            // screen reader user has no progress dots to look at.
            Accessible.role: Accessible.Dialog
            Accessible.name: qsTr("Tour, step %1 of %2: %3")
                                .arg(root.step + 1).arg(root.stepCount).arg(root.data.title)

            Text {
                width: parent.width
                text: root.data.title
                color: Theme.textPrimary
                font.pixelSize: Theme.fontL
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
                Accessible.ignored: true
            }
            Text {
                width: parent.width
                text: root.data.intro
                color: Theme.textSecondary
                font.pixelSize: Theme.fontM
                wrapMode: Text.WordWrap
                Accessible.ignored: true
            }

            Column {
                width: parent.width
                spacing: Theme.spacingS
                Repeater {
                    model: root.data.rows
                    delegate: Row {
                        width: bodyCol.width
                        spacing: Theme.spacingM
                        // The key column is fixed so the descriptions line up;
                        // it wraps rather than eliding, because a translated
                        // key name ("Ctrl+przewijanie") is longer than the
                        // English one and must stay readable.
                        Rectangle {
                            width: Math.round(bodyCol.width * 0.34)
                            height: Math.max(24, keyText.implicitHeight + Theme.spacingS)
                            radius: Theme.radiusS
                            color: Theme.alpha(Theme.accent, 0.12)
                            Text {
                                id: keyText
                                anchors.centerIn: parent
                                width: parent.width - Theme.spacingM
                                text: modelData[0]
                                color: Theme.accent
                                font.pixelSize: Theme.fontS
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                Accessible.ignored: true
                            }
                        }
                        Text {
                            width: bodyCol.width - Math.round(bodyCol.width * 0.34) - Theme.spacingM
                            text: modelData[1]
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontM
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                    }
                }
            }

            Text {
                width: parent.width
                text: root.data.note
                color: Theme.textTertiary
                font.pixelSize: Theme.fontS
                wrapMode: Text.WordWrap
                Accessible.ignored: true
            }

            Item { width: 1; height: Theme.spacingS }

            Item {
                width: parent.width
                height: Math.max(dots.height, navRow.height)

                // Position, drawn. Never interactive: a dot small enough to fit
                // is too small to be a target, and the two buttons already move
                // in both directions.
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
                        text: qsTr("Skip")
                        variant: "ghost"
                        compact: true
                        accessibleDescription: qsTr("Close the tour. It stays available in Settings.")
                        onClicked: root.close()
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
