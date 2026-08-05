import QtQuick
import QtQuick.Window
import QtQuick.Effects
import Unisic
import Unisic.Kit
import "components"
import "pages"

Window {
    id: window
    width: 1060
    height: 700
    minimumWidth: 880
    minimumHeight: 560
    // Normally shown at launch; `unisic --tray-only` (autostart) boots hidden
    // into the tray. startHidden is a context property set from C++ (always
    // defined, so this binding never hits an undefined reference).
    visible: !startHidden
    title: "Unisic"
    color: Theme.backgroundDeep

    // System vs custom (frameless) window decoration. Toggling recreates the
    // platform surface; the compositor re-parents the existing scene graph.
    flags: App.settings.useSystemDecoration
           ? Qt.Window
           : (Qt.Window | Qt.FramelessWindowHint)

    // KWin keeps PAINTING the server-side titlebar after the frameless flag is
    // set: the decoration object lives on the xdg_toplevel, and switching modes
    // only takes effect once the surface is re-mapped — until then the KDE
    // titlebar sits on top of our own, and it takes a click outside the window
    // (a deactivate/activate round-trip) to clear it. Re-map it ourselves.
    // Deferred, so the flags binding above has already been applied when the
    // surface goes down; re-shown from the same call so the window keeps its
    // stacking and comes back focused.
    Connections {
        target: App.settings
        function onUseSystemDecorationChanged() {
            if (!window.visible)
                return
            Qt.callLater(function () {
                // Re-mapping resets the size to the implicit one; the compositor
                // owns the position on Wayland either way, so carry the size
                // across by hand.
                const w = window.width
                const h = window.height
                window.visible = false
                window.visible = true
                window.width = w
                window.height = h
                window.requestActivate()
            })
        }
    }
    // Height reserved at the top for the custom title bar (0 with system decos).
    readonly property int chromeTop: App.settings.useSystemDecoration ? 0 : 38

    property int currentPage: 0
    // The one page that is loaded right now. Every other Loader is inactive, so
    // its `item` is null and the chain below picks the single live one.
    readonly property Item activePageItem: pageCapture.item || pageRecord.item
            || pageEdit.item || pageHistory.item || pageServers.item || pageSettings.item

    // True while the first-run flow owns the window. Asks the flow itself, not
    // its Loader: the Loader stays active for another 250 ms after the flow has
    // faded out (teardown timer), and the page behind must come back to life
    // while the backdrop is still opaque, not a tenth of a second later.
    readonly property bool welcomeOpen: welcomeLoader.item !== null && welcomeLoader.item.shown

    // "Something is on top of the window right now." Window-level overlays are
    // listed here; a page adds its own dialogs through the `modalOpen` property
    // every page declares, so this never grows a branch per new dialog.
    // Consumers: the window DropArea and Ctrl+V - dropping (or pasting) an
    // image while a modal is up would open an editor window BEHIND it.
    //
    // `visible`, NOT `opened`: a Popup with an enter transition (UPatchNotes and
    // UUpdatePrompt both fade+scale in) is on screen and modal from the moment
    // open() is called, but only reports `opened` once that transition has
    // FINISHED. Measured: right after patchNotes.open(), opened is false while
    // visible is already true. `visible` covers the whole on-screen life,
    // including the fade-out, which is the side to err on.
    readonly property bool modalOpen:
           window.welcomeOpen || firstRunSystemCheck.visible
        || patchNotes.visible || updatePrompt.visible || shortcutsHelp.visible
        || (activePageItem !== null && activePageItem.modalOpen)

    // "The app is dragging something OUT of itself right now." History tiles
    // start a real system drag (Drag.Automatic + text/uri-list), and the
    // compositor offers that drag straight back to our own surface - so the
    // window DropArea has to stand down for exactly its duration. Same shape as
    // modalOpen: only the page that starts a drag declares the property, and
    // reading an undeclared one yields undefined, which `=== true` reads as
    // "no". Self-clearing too - when the page goes away activePageItem is null.
    readonly property bool pageDragOut:
        activePageItem !== null && activePageItem.dragOutActive === true

    // Built-in WINDOW shortcuts (the QtQuick Shortcut items below). These are
    // NOT GlobalHotkeys/KGlobalAccel actions: they only fire while the main
    // window has focus, they never register a system-wide grab, and they are
    // deliberately fixed — they do not appear in, nor are editable from, the
    // Settings shortcut UI. Ctrl+/ pops the cheat-sheet listing them — keep its
    // model in sync with the Shortcut items below. Keep the Ctrl+1..6 list in
    // sync with the sidebar items and the page Loaders (one source of truth for
    // the page indices).
    function hideToTray() {
        // "Close to tray" (a friend's Ctrl+W). Only hide when a tray icon
        // actually exists, else the window would vanish with no way back —
        // minimize instead on trayless compositors.
        if (App.trayAvailable)
            window.hide()
        else
            window.showMinimized()
    }

    function quitApp() {
        // Never yank an in-flight recording/encode out from under the user.
        if (App.recording || App.converting) {
            App.showToast(qsTr("Recording in progress. Stop it before closing"), true)
            return
        }
        if (App.editorWindowsOpen > 0) {
            App.showToast(qsTr("Close the editor first (unsaved annotations)"), true)
            return
        }
        App.quitApp("Ctrl+Q / quit action")
    }

    // `enabled: !App.shortcutRecording` is REQUIRED: with the default
    // Qt.WindowShortcut context these win Qt's shortcut-override race against a
    // focused UShortcutRecorder, so a user binding e.g. Ctrl+Q as a global
    // hotkey would trigger the window action (quit!) instead of recording it.
    Shortcut { enabled: !App.shortcutRecording; sequences: ["Ctrl+/"]; onActivated: shortcutsHelp.opened ? shortcutsHelp.close() : shortcutsHelp.open() }
    Shortcut { enabled: !App.shortcutRecording; sequences: ["Ctrl+W"]; onActivated: window.hideToTray() }
    Shortcut { enabled: !App.shortcutRecording; sequences: ["Ctrl+Q"]; onActivated: window.quitApp() }
    // Paste an image straight into a new editor document. A focused text field
    // consumes Ctrl+V first (QQuickTextInput accepts the shortcut override for
    // QKeySequence::Paste), so this never steals paste from the Settings search
    // box or a server-sheet field. Off entirely while a modal is up: pasting
    // there must reach the field under the caret, never open an editor window
    // behind the dialog.
    Shortcut { enabled: !App.shortcutRecording && !window.modalOpen; sequences: ["Ctrl+V"]; onActivated: App.pasteFromClipboard() }
    Shortcut { enabled: !App.shortcutRecording; sequences: ["Ctrl+,"]; onActivated: window.currentPage = 5 }
    Shortcut { enabled: !App.shortcutRecording; sequences: ["Ctrl+1"]; onActivated: window.currentPage = 0 }
    Shortcut { enabled: !App.shortcutRecording; sequences: ["Ctrl+2"]; onActivated: window.currentPage = 1 }
    Shortcut { enabled: !App.shortcutRecording; sequences: ["Ctrl+3"]; onActivated: window.currentPage = 2 }
    Shortcut { enabled: !App.shortcutRecording; sequences: ["Ctrl+4"]; onActivated: window.currentPage = 3 }
    Shortcut { enabled: !App.shortcutRecording; sequences: ["Ctrl+5"]; onActivated: window.currentPage = 4 }
    Shortcut { enabled: !App.shortcutRecording; sequences: ["Ctrl+6"]; onActivated: window.currentPage = 5 }

    UShortcutsHelp {
        id: shortcutsHelp
        model: [
            { keys: ["Ctrl", "/"], label: qsTr("Show / hide this list") },
            { keys: ["Ctrl", "W"], label: qsTr("Hide window to tray") },
            { keys: ["Ctrl", "Q"], label: qsTr("Quit Unisic") },
            { keys: ["Ctrl", "V"], label: qsTr("Paste an image into the editor") },
            { keys: ["Ctrl", ","], label: qsTr("Open Settings") },
            { keys: ["Ctrl", "1"], label: qsTr("Jump to a page (Ctrl+1 … Ctrl+6)") },
        ]
    }

    // Hide-to-tray only when a tray actually EXISTS — on GNOME without the
    // AppIndicator extension (or bare wlroots) hiding here would make the app
    // unreachable except by launching `unisic` again.
    onClosing: (close) => {
        if (App.settings.minimizeToTrayOnClose && App.trayAvailable) {
            close.accepted = false
            window.hide()
        } else {
            // Hide-to-tray is off (or there's no tray at all).
            // quitOnLastWindowClosed is false (tray lifetime), so an accepted
            // close would just leave a hidden resident process. Quit — but
            // never kill an in-flight recording/encode or open editors with
            // unsaved annotations.
            if (App.recording || App.converting) {
                close.accepted = false
                App.showToast(qsTr("Recording in progress. Stop it before closing"), true)
            } else if (App.editorWindowsOpen > 0) {
                window.hide()
                close.accepted = false
            } else {
                App.quitApp("main window closed, hide-to-tray off or no tray")
            }
        }
    }

    // When the main window is hidden (closed to no tray) and the only thing
    // keeping the process alive was an open editor, quitting must happen once
    // that last editor closes — otherwise the app lives on invisibly with no
    // tray icon and no window. Mirror onClosing's conditions.
    Connections {
        target: App
        function onEditorWindowsOpenChanged() {
            if (App.editorWindowsOpen === 0 && !window.visible
                    && !(App.settings.minimizeToTrayOnClose && App.trayAvailable)
                    && !App.recording && !App.converting)
                App.quitApp("last editor closed with no window and no tray")
        }
    }

    Connections {
        target: App
        function onShowMainWindowRequested() {
            if (window.visibility === Window.Minimized)
                window.showNormal()
            else
                window.show()
            window.raise()
            window.requestActivate()
        }
    }

    Rectangle { // flat backdrop — the content card floats on this
        anchors.fill: parent
        color: Theme.backgroundDeep
    }

    Item { // custom title bar (frameless decoration) — blends into the backdrop
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 38
        visible: !App.settings.useSystemDecoration
        z: 20

        // Drag anywhere on the bar to move the window (Wayland system move).
        // startSystemMove() is deferred past a small drag threshold: calling it on
        // raw press hands the compositor a move-grab immediately and swallows the
        // release, so onDoubleClicked (maximize) would never fire.
        MouseArea {
            anchors.fill: parent
            property real pressX: 0
            property real pressY: 0
            property bool moving: false
            onPressed: (m) => { pressX = m.x; pressY = m.y; moving = false }
            onPositionChanged: (m) => {
                if (!moving && (Math.abs(m.x - pressX) > 6 || Math.abs(m.y - pressY) > 6)) {
                    moving = true
                    window.startSystemMove()
                }
            }
            onDoubleClicked: window.visibility === Window.Maximized ? window.showNormal()
                                                                    : window.showMaximized()
        }

        Text { // app name, centered in the decoration
            anchors.centerIn: parent
            text: "Unisic"
            color: Theme.textSecondary
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
                onClicked: window.showMinimized()
            }
            UIconButton {
                iconName: "window"; iconSize: 13; width: 30; height: 30
                tooltip: qsTr("Maximize")
                onClicked: window.visibility === Window.Maximized ? window.showNormal()
                                                                  : window.showMaximized()
            }
            UIconButton {
                iconName: "close"; iconSize: 14; width: 30; height: 30
                tooltip: qsTr("Close")
                onClicked: window.close()
            }
        }
    }

    Item { // sidebar — flat on the backdrop, music-player style
        id: sidebar
        width: 224
        y: window.chromeTop
        height: parent.height - window.chromeTop
        z: 2
        // The first-run flow is a hand-rolled full-window overlay, not a Popup,
        // so nothing takes what is behind it out of the tab chain: Tab used to
        // walk from the flow onto the sidebar and the page under the opaque
        // backdrop. `enabled` propagates to children and a disabled subtree is
        // skipped by tab navigation; the flow's own backdrop is opaque, so
        // there is nothing to see change. The title bar stays enabled on
        // purpose - the flow is deliberately closable while it runs.
        enabled: !window.welcomeOpen

        Column {
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingM
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacingM
            anchors.rightMargin: Theme.spacingM
            spacing: 4

            Text { // section header, like the app-name section in music players
                text: "Unisic"
                color: Theme.textTertiary
                font.pixelSize: Theme.fontS
                font.weight: Font.DemiBold
                leftPadding: 11
                bottomPadding: 4
            }

            SidebarItem { iconName: "camera-photo";  label: qsTr("Capture");      active: currentPage === 0; onClicked: currentPage = 0 }
            SidebarItem { iconName: "media-record";  label: qsTr("Record");       active: currentPage === 1; onClicked: currentPage = 1 }
            SidebarItem { iconName: "edit";          label: qsTr("Edit");         active: currentPage === 2; onClicked: currentPage = 2 }

            Item { width: 1; height: Theme.spacingM }

            Text {
                text: qsTr("Library")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontS
                font.weight: Font.DemiBold
                leftPadding: 11
                bottomPadding: 4
            }

            SidebarItem { iconName: "view-history";  label: qsTr("History");      active: currentPage === 3; onClicked: currentPage = 3 }
            SidebarItem { iconName: "folder-cloud";  label: qsTr("Servers"); active: currentPage === 4; onClicked: currentPage = 4 }
        }

        // App card at the bottom (profile-card style): identity + version on
        // the left (click for the release notes), Settings gear on the right.
        Rectangle {
            id: bottomCard
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacingM
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacingM
            anchors.rightMargin: Theme.spacingM
            height: 56
            radius: Theme.radiusM
            color: cardMouse.containsMouse ? Theme.surfaceHi : Theme.surface
            border.width: 1
            border.color: Theme.divider
            Behavior on color { ColorAnimation { duration: Theme.animFast } }

            // One entry point for pointer, keyboard and assistive tech, so the
            // three can never drift apart.
            function openPatchNotes() { patchNotes.open(); App.markPatchNotesSeen() }

            MouseArea {
                id: cardMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: bottomCard.openPatchNotes()
            }

            // UKeys keeps the window's Ctrl+1..6, Ctrl+, and Ctrl+/ bubbling
            // past a focused card.
            activeFocusOnTab: true
            Keys.onSpacePressed: (e) => UKeys.activate(e, bottomCard.openPatchNotes)
            Keys.onReturnPressed: (e) => UKeys.activate(e, bottomCard.openPatchNotes)
            Keys.onEnterPressed: (e) => UKeys.activate(e, bottomCard.openPatchNotes)

            Accessible.role: Accessible.Button
            //: Spoken name of the app card in the sidebar: %1 is the version.
            Accessible.name: qsTr("Unisic %1").arg(App.appVersion)
            Accessible.description: qsTr("Open the release notes")
            Accessible.focusable: bottomCard.activeFocusOnTab
            Accessible.onPressAction: bottomCard.openPatchNotes()
            UFocusRing { inset: 1 }

            UHoverTip {
                anchor: bottomCard
                text: qsTr("What's new")
                      + (App.buildDate ? "\n" + App.buildDate : "")
                show: cardMouse.containsMouse && !gearButton.hovered
            }

            Image {
                id: cardIcon
                source: "qrc:/resources/icons/unisic.svg"
                sourceSize: Qt.size(32, 32)
                width: 32; height: 32
                smooth: true
                anchors.left: parent.left
                anchors.leftMargin: 11
                anchors.verticalCenter: parent.verticalCenter
                // Decorative: the card itself is the named button.
                Accessible.ignored: true
                // Dev builds are gray everywhere (tray, menu, here too).
                layer.enabled: App.devBuild
                layer.effect: MultiEffect { saturation: -1.0 }
            }
            Column {
                anchors.left: cardIcon.right
                anchors.leftMargin: 10
                anchors.right: gearButton.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    width: parent.width
                    text: "Unisic"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontM
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: "v" + App.appVersion + (App.buildNumber === "dev"
                            ? " · dev"
                            : " · build " + App.buildNumber)
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontS
                    elide: Text.ElideRight
                }
            }
            UIconButton {
                id: gearButton
                iconName: "configure"
                iconSize: 16
                width: 32; height: 32
                active: currentPage === 5
                anchors.right: parent.right
                anchors.rightMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                tooltip: qsTr("Settings")
                onClicked: currentPage = 5
            }
        }

        // Recording pill
        Rectangle {
            visible: App.recording || App.converting
            anchors.bottom: bottomCard.top
            anchors.bottomMargin: Theme.spacingM
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 2 * Theme.spacingM
            height: 46
            radius: 23
            color: Theme.secondary
            border.width: 1
            border.color: Theme.divider

            Row {
                anchors.centerIn: parent
                spacing: 8
                Rectangle {
                    id: recDot
                    width: 10; height: 10; radius: 5
                    color: Theme.danger
                    opacity: 1
                    anchors.verticalCenter: parent.verticalCenter
                    SequentialAnimation on opacity {
                        // Gate on window visibility too: with the window hidden to
                        // tray during a long recording, an infinite animation keeps
                        // the GUI thread waking ~60x/s for an invisible dot. Freeze
                        // solid while paused so the pill reads "held", not "live".
                        running: App.recording && !App.recordingPaused && window.visible
                        loops: Animation.Infinite
                        onStopped: recDot.opacity = 1
                        NumberAnimation { to: 0.2; duration: 600 }
                        NumberAnimation { to: 1.0; duration: 600 }
                    }
                }
                Text {
                    // Manual h:mm:ss — Qt.formatTime wraps at 60 minutes.
                    function fmtElapsed(s) {
                        var h = Math.floor(s / 3600)
                        var m = Math.floor((s % 3600) / 60)
                        var sec = s % 60
                        function p(v) { return (v < 10 ? "0" : "") + v }
                        return (h > 0 ? h + ":" + p(m) : p(m)) + ":" + p(sec)
                    }
                    text: App.converting ? qsTr("Encoding…")
                          : (fmtElapsed(App.recordSeconds)
                             + (App.recordingPaused ? " " + qsTr("(paused)") : ""))
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontM
                    anchors.verticalCenter: parent.verticalCenter
                }
                // These two carry no tooltip (the pill is already unmistakable
                // on screen), so their spoken name has to be supplied here -
                // UIconButton otherwise names itself from `tooltip`.
                UIconButton {
                    visible: App.recordingCanPause && !App.converting
                    iconName: App.recordingPaused ? "play" : "pause"
                    iconSize: 15
                    width: 30; height: 30
                    anchors.verticalCenter: parent.verticalCenter
                    accessibleName: App.recordingPaused ? qsTr("Resume recording")
                                                        : qsTr("Pause recording")
                    onClicked: App.togglePauseRecording()
                }
                UIconButton {
                    visible: App.recording && !App.converting
                    iconName: "stop"
                    iconSize: 15
                    width: 30; height: 30
                    anchors.verticalCenter: parent.verticalCenter
                    accessibleName: qsTr("Stop recording")
                    onClicked: App.stopRecording()
                }
            }
        }

        // One-time "new version" nudge: a blinking arrow pointing at the app
        // card after an update, until the release notes are opened. Gated on
        // window.visible so it never animates while the app sits in the tray,
        // and on the welcome flow so it can't burn frames invisibly behind the
        // opaque first-run backdrop.
        Item {
            id: patchHint
            visible: App.patchNotesUnseen && !App.recording && !App.converting
            anchors.bottom: bottomCard.top
            anchors.bottomMargin: Theme.spacingXS
            anchors.horizontalCenter: parent.horizontalCenter
            width: hintCol.width
            height: hintCol.height

            Column {
                id: hintCol
                anchors.centerIn: parent
                spacing: 0
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("See patch notes")
                    color: Theme.accent
                    font.pixelSize: Theme.fontS
                    font.weight: Font.DemiBold
                }
                UIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "chevron-down"
                    size: 16
                    color: Theme.accent
                }
            }

            SequentialAnimation on opacity {
                running: patchHint.visible && window.visible && !welcomeLoader.active
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.25; duration: 650; easing.type: Easing.InOutQuad }
                NumberAnimation { from: 0.25; to: 1.0; duration: 650; easing.type: Easing.InOutQuad }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: bottomCard.openPatchNotes()
            }

            // Named and pressable from assistive tech, but deliberately NOT a
            // tab stop: it is a nudge pointing at the app card, and the card
            // right below it is already a focusable button running the same
            // call. Two stops for one action would just pad the tab order.
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("See patch notes")
            Accessible.description: qsTr("Open the release notes for this version")
            Accessible.onPressAction: bottomCard.openPatchNotes()
        }
    }

    // Release notes for the running version, opened from the app card.
    UPatchNotes {
        id: patchNotes
        // changelogVersion, not appVersion: a dev build shows the next
        // release's in-progress section (release builds: identical values).
        version: App.changelogVersion
    }

    // "Install now?" prompt for native package installs (once per version, only
    // when the app can run install.sh in a terminal). AppContext emits the
    // request in place of the plain update toast for that install kind.
    UUpdatePrompt {
        id: updatePrompt
    }
    Connections {
        target: App
        function onInstallerUpdatePromptRequested(version) {
            updatePrompt.openFor(version)
        }
    }

    // First-run welcome, then the dependency check — never both at once. The
    // welcome always shows on a fresh config (showWelcome latch); the check
    // only when a core optional tool is actually missing, so a fully set-up
    // machine never sees it. Both are skipped on a tray-only boot (no visible
    // window to host a modal); the next normal launch picks them up. The small
    // delay lets the window paint before the modal dims it.
    // Fills the WINDOW (not the screen), below the custom title bar so the
    // window can still be moved and closed while setup runs.
    // Behind a Loader so the 7-step tree (built-in notification preview, theme
    // grid, dozens of tooltip items) only exists while the flow is on screen —
    // it used to stay resident for the whole app lifetime after the first run.
    Loader {
        id: welcomeLoader
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: parent.top
        anchors.topMargin: window.chromeTop
        // UWelcome's own z: 1000 ("above every page") only ranks it among the
        // LOADER's children now — the Loader itself must carry it against the
        // window's content siblings.
        z: 1000
        active: false
        sourceComponent: UWelcome {}
        function openWelcome(markSeen) {
            welcomeUnload.stop()  // a reopen must beat a pending teardown
            active = true
            item.markSeenOnClose = markSeen
            item.open()
        }
    }
    USystemCheck { id: firstRunSystemCheck }
    // Connections, NOT an inline onClosed: a signal handler assigned at the use
    // site REPLACES the one declared inside the component, which would drop
    // UWelcome's own skip-restore + latch handling.
    Connections {
        target: welcomeLoader.item
        // The dependency check queues behind the setup flow instead of stacking
        // on top of it — and only after the real first run (markSeenOnClose),
        // never after a manual peek from Settings.
        function onClosed() {
            if (welcomeLoader.item.markSeenOnClose && !App.settings.systemCheckSeen
                    && App.hasDependencyWarnings())
                firstRunSystemCheck.open()
            welcomeUnload.restart()
        }
    }
    // Tear the tree down only after UWelcome's 140 ms fade-out has finished.
    Timer {
        id: welcomeUnload
        interval: 250
        repeat: false
        onTriggered: welcomeLoader.active = false
    }
    Timer {
        interval: 500
        running: !startHidden
        repeat: false
        onTriggered: {
            if (App.settings.showWelcome) {
                welcomeLoader.openWelcome(true)
            } else if (!App.settings.systemCheckSeen && App.hasDependencyWarnings()) {
                firstRunSystemCheck.open()
            } else if (App.hasUnseenCrash()) {
                // A toast and not a modal, deliberately: the previous run has
                // already ended, there is nothing for the user to decide, and a
                // third first-run dialog stacked behind the other two would be
                // hostile. It names where the report is and latches on the
                // report's own content, so the same crash never says this
                // twice and a different one still does.
                App.showToast(qsTr("Unisic did not shut down properly last time. Settings \u203A General \u203A Activity log has the report."), true)
                App.markCrashNoticeSeen()
            }
        }
    }
    // Re-opened on demand (Settings button, Developer pane): a manual peek must
    // never consume the first-run latch.
    Connections {
        target: App
        function onShowWelcomeRequested() {
            welcomeLoader.openWelcome(false)
        }
    }


    Rectangle { // content — a rounded card floating on the dark backdrop
        id: contentCard
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: window.chromeTop + Theme.spacingM
        anchors.rightMargin: Theme.spacingM
        anchors.bottomMargin: Theme.spacingM
        radius: Theme.radiusL
        color: Theme.background
        border.width: 1
        border.color: Theme.edgeLight
        // Same reason as the sidebar: keep the page under the first-run flow's
        // opaque backdrop out of the tab chain.
        enabled: !window.welcomeOpen

        // Accent glow bleeding down from the card's top edge (the "album art"
        // wash in the reference look). Its own top corners are rounded so it
        // can't poke square pixels past the card's rounded edge.
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 1
            height: 230
            topLeftRadius: contentCard.radius - 1
            topRightRadius: contentCard.radius - 1
            bottomLeftRadius: 0
            bottomRightRadius: 0
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.alpha(Theme.accent, Theme.isDark ? 0.14 : 0.09) }
                GradientStop { position: 1.0; color: Theme.alpha(Theme.accent, 0.0) }
            }
        }

        Item { // square clip: every page pads its content, so the corner
               // regions stay empty and the rounding above shows through
            anchors.fill: parent
            anchors.margins: 1
            clip: true

            // Only the visible page is instantiated; leaving a page unloads it
            // so idle RAM tracks a single page instead of all six at once.
            Component { id: capturePage;      CapturePage {} }
            Component { id: recordPage;       RecordPage {} }
            Component { id: editPage;         EditPage {} }
            Component { id: historyPage;      HistoryPage {} }
            Component { id: destinationsPage; DestinationsPage {} }
            Component { id: settingsPage;     SettingsPage {} }

            Loader { id: pageCapture;  anchors.fill: parent; active: currentPage === 0; visible: active; sourceComponent: capturePage }
            Loader { id: pageRecord;   anchors.fill: parent; active: currentPage === 1; visible: active; sourceComponent: recordPage }
            Loader { id: pageEdit;     anchors.fill: parent; active: currentPage === 2; visible: active; sourceComponent: editPage }
            Loader { id: pageHistory;  anchors.fill: parent; active: currentPage === 3; visible: active; sourceComponent: historyPage }
            Loader { id: pageServers;  anchors.fill: parent; active: currentPage === 4; visible: active; sourceComponent: destinationsPage }
            Loader { id: pageSettings; anchors.fill: parent; active: currentPage === 5; visible: active; sourceComponent: settingsPage }
        }
    }

    // Drag and drop anywhere on the window: an image lands in the editor, a
    // recording in the trim window (App.openDroppedUrls routes through the same
    // openPath() the Edit page's file picker uses).
    //
    // It has to live at the WINDOW level, not inside a page: the pages are
    // behind Loaders that are destroyed when you navigate away, so a page-level
    // drop target would only work while that one page happened to be open.
    // A DropArea handles drag events ONLY - it does not take mouse events, so
    // stacking it over everything leaves the title-bar drag, the sidebar and
    // every page fully clickable.
    DropArea {
        id: dropZone
        anchors.fill: parent
        // Above every page, below the toast (500) and the first-run welcome /
        // system check (1000), which own the window while they are up.
        z: 300
        // Off while a modal owns the window: dropping behind the first-run
        // welcome, the system check, a server edit sheet or a confirmation
        // would open an editor you cannot see. One flag, fed by the window's
        // own overlays plus the active page's (see window.modalOpen).
        //
        // Off as well while Unisic is dragging one of its OWN files out to
        // another application (a History tile): the drag is offered back to
        // this surface, so without the guard the "drop to open" overlay covers
        // the window mid-export and releasing inside it opens an editor / trim
        // window for the file the user was dragging AWAY. The guard keys on the
        // drag's ORIGIN, not on what it carries - a "does this path live in the
        // captures folder?" test would refuse a perfectly legitimate external
        // drop of a capture dragged back in from a file manager, and would miss
        // a drag-out of anything stored elsewhere. Origin cannot be confused:
        // pageDragOut is only ever true between our own proxy's dragStarted and
        // dragFinished.
        enabled: !window.modalOpen && !window.pageDragOut

        // What the overlay is allowed to promise, decided from what the drag
        // OFFERS. FOUR answers, not two: "image" and "video" (the router will
        // open it, and they say WHICH window - openPath() sends an image to the
        // editor and a recording to the trim window, so one "yes" could only
        // ever name one of them and would be lying about the other), "no" (it
        // will be refused) and "maybe" - the honest shrug for a payload the
        // source has not handed over yet. A two-state answer had to guess on
        // the unknowable case, and guessing "yes" also meant promising the
        // editor for every local file, .txt and folders included, which
        // openPath() then refuses.
        property string verdict: "no"

        // The one test for "the router will take this", so the card, the icon
        // and the headline can never disagree about it.
        readonly property bool willOpen: verdict === "image" || verdict === "video"

        readonly property var imageTypes: ["image/png", "image/jpeg", "image/webp",
                                           "image/bmp", "image/tiff", "image/gif"]

        // The file extensions AppContext::editableKindFor actually routes, kept
        // SPLIT the way it splits them (lowercase): the overlay has to name the
        // window the drop will open, and the kind is what decides that in C++
        // too. A DragEvent hands QML the urls and NOTHING else - the routing
        // rule is a static C++ helper QML cannot call, and there is no stat'ing
        // a path from here. So this is a deliberate second copy: keep it in
        // step with editableKindFor. A stale entry only mislabels the overlay
        // for one drag; the drop itself is still decided in C++, which cannot
        // drift from itself.
        readonly property var imageExt: ["png", "jpg", "jpeg", "webp", "bmp", "tif",
                                         "tiff", "avif"]
        // gif sits under video here even though C++ splits it by frame count (a
        // one-frame GIF opens in the image editor). The extension is all a drag
        // gives QML, and the animated one is the likelier drop - so the overlay
        // guesses "video" and the drop itself still lands correctly.
        readonly property var videoExt: ["mp4", "webm", "gif", "mkv", "mov"]

        // Which window the url would open, from the extension of its last path
        // segment - matching QFileInfo::suffix() + toLower() on the C++ side
        // (everything after the LAST dot). "" for everything editableKindFor
        // also refuses. A directory cannot be told from a file by its url alone
        // - but it does not carry one of these extensions either, so a dropped
        // folder lands on "", which is what openPath() answers it ("Unisic
        // opens files, not folders"). Same for a file whose type Unisic has no
        // window for.
        function urlKind(url) {
            var s = String(url).split("#")[0].split("?")[0]
            var seg = s.substring(s.lastIndexOf("/") + 1)
            var dot = seg.lastIndexOf(".")
            if (dot < 0)
                return ""
            var ext = seg.substring(dot + 1).toLowerCase()
            if (dropZone.imageExt.indexOf(ext) >= 0)
                return "image"
            if (dropZone.videoExt.indexOf(ext) >= 0)
                return "video"
            return ""
        }

        function payloadVerdict(ev) {
            // The same order onDropped routes in: a payload that carries file
            // references is decided by its urls, and only one without them
            // falls through to pixels. Testing pixels first would promise the
            // editor for a browser drag offering image bytes AND an https url -
            // the url wins at drop time and is refused.
            if (ev.hasUrls) {
                var u = ev.urls
                // The mime type list always arrives with the pointer; the
                // payload usually does too (XDND lets a target read the
                // selection mid-drag, and wl_data_offer.receive is legal from
                // the moment the offer is announced) - but a source that
                // answers late leaves this empty, which means "not readable
                // yet", never "nothing there". Nothing can be promised about
                // urls nobody has seen, so this is exactly the "maybe" case.
                if (u.length === 0)
                    return "maybe"
                // openDroppedUrls opens the FIRST entry it can and ignores the
                // rest, so the first openable local file settles both answers:
                // that there is something to open, and which window opens it. A
                // mixed batch needs no third answer for that reason - the
                // leader wins, and the toast names the file that opened. Remote
                // urls never count: it will not fetch an https url behind the
                // user's back.
                for (var j = 0; j < u.length; ++j) {
                    if (String(u[j]).indexOf("file:") !== 0)
                        continue
                    var kind = dropZone.urlKind(u[j])
                    if (kind !== "")
                        return kind
                }
                return "no"
            }
            // Pixels need no lookup at all: openImageData takes them as they
            // are, whatever they came from, and always into the editor. Only
            // the types onDropped actually reads, though - a payload offering
            // just image/svg+xml matches a plain "image/" prefix test but the
            // drop handler never touches it.
            var f = ev.formats
            for (var i = 0; i < dropZone.imageTypes.length; ++i)
                if (f.indexOf(dropZone.imageTypes[i]) >= 0)
                    return "image"
            return "no"
        }

        // Accept even a payload we cannot use: refusing it here makes the source
        // cancel the drop, and then onDropped never runs and the rejection is
        // silent. Taking it lets the overlay say why and the toast confirm it.
        onEntered: (drag) => {
            dropZone.verdict = dropZone.payloadVerdict(drag)
            drag.accept(Qt.CopyAction)
        }
        onExited: dropZone.verdict = "no"

        onDropped: (drop) => {
            dropZone.verdict = "no"
            // Read EVERYTHING synchronously: the QMimeData behind this event is
            // destroyed the moment the handler returns.
            if (drop.hasUrls && drop.urls.length > 0) {
                drop.accept(Qt.CopyAction)
                App.openDroppedUrls(drop.urls)
                return
            }
            var f = drop.formats
            for (var i = 0; i < dropZone.imageTypes.length; ++i) {
                var t = dropZone.imageTypes[i]
                if (f.indexOf(t) < 0)
                    continue
                var bytes = drop.getDataAsArrayBuffer(t)
                if (bytes && App.openImageData(bytes)) {
                    drop.accept(Qt.CopyAction)
                    return
                }
            }
            App.showToast(qsTr("Unisic cannot open what you dropped"), true)
        }

        // Drop overlay. This APPEARS, which the "nothing moves or appears under
        // the pointer" rule allows: it is a drag state, not hover feedback, and
        // it carries no MouseArea of its own so it stays input-transparent.
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: window.chromeTop
            visible: dropZone.containsDrag
            color: Theme.alpha(Theme.backgroundDeep, 0.86)

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.spacingXL, 460)
                height: dropCol.height + 2 * Theme.spacingXL
                radius: Theme.radiusXL
                color: Theme.alpha(Theme.accent, dropZone.willOpen ? 0.12 : 0.06)
                border.width: 2
                border.color: dropZone.willOpen ? Theme.accent : Theme.divider

                Column {
                    id: dropCol
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.spacingXL
                    spacing: Theme.spacingM

                    UIcon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: dropZone.verdict === "no" ? "close"
                            : dropZone.verdict === "video" ? "play" : "image"
                        size: 44
                        color: dropZone.willOpen ? Theme.accent : Theme.textTertiary
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        // The headline names the window the drop is actually
                        // going to: promising the editor for a recording, which
                        // openPath() sends to the trim window, is the same
                        // broken promise as promising it for a .txt. "maybe"
                        // gets the neutral card and a sentence that commits to
                        // nothing: the drop still routes, and the toast says
                        // what happened either way.
                        text: dropZone.verdict === "image" ? qsTr("Drop to open in the editor")
                            : dropZone.verdict === "video" ? qsTr("Drop to open in the trim window")
                            : dropZone.verdict === "no" ? qsTr("Unisic cannot open this")
                            : qsTr("Drop to try opening it")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontL
                        font.weight: Font.DemiBold
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Images open in the editor, recordings in the trim window.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontM
                    }
                }
            }
        }
    }

    // Toast
    Rectangle {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: visible ? Theme.spacingL : -height
        width: Math.min(toastLabel.implicitWidth + 44, parent.width - 80)
        height: 46
        radius: 23
        color: Theme.surfaceHi
        border.width: 1
        border.color: Theme.divider
        visible: opacity > 0
        opacity: 0
        z: 500
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.shadow
            shadowBlur: 0.9
            shadowVerticalOffset: 4
            shadowOpacity: 0.6
        }

        Behavior on opacity { NumberAnimation { duration: Theme.animMed } }

        // Announced as a notification rather than read as a stray label: the
        // toast is transient status, never a control.
        //
        // Notification needs no version check, unlike USwitch's role: it has
        // been QAccessible::Notification = 0x86 since long before our floor.
        // Checked in qtbase's own header at the three versions that matter -
        // v6.5.0 (the declared floor), v6.6.0 (openSUSE Leap) and v6.8.3 (the
        // AppImage/tarball pin): src/gui/accessible/qaccessible_base.h ends the
        // "Additional Qt roles" block at Notification in all three. Switch
        // (0x87) is the one that only appears in 6.11. Do not re-litigate.
        Accessible.role: Accessible.Notification
        Accessible.name: toastLabel.text

        Text {
            id: toastLabel
            // The card carries the name; without this the same sentence is
            // announced twice.
            Accessible.ignored: true
            anchors.centerIn: parent
            width: Math.min(implicitWidth, toast.width - 30)
            elide: Text.ElideMiddle
            color: Theme.textPrimary
            font.pixelSize: Theme.fontM
        }

        Timer {
            id: toastTimer
            interval: 4000
            onTriggered: toast.opacity = 0
        }

        Connections {
            target: App
            function onToastChanged() {
                if (App.toastText === "")
                    return
                toastLabel.text = App.toastText
                toast.opacity = 1
                toastTimer.restart()
            }
        }

        // Toasts emitted during startup (hotkey conflicts etc.) fire before
        // this UI exists — pick up the pending one on load.
        Component.onCompleted: {
            if (App.toastText !== "") {
                toastLabel.text = App.toastText
                toast.opacity = 1
                toastTimer.restart()
            }
        }
    }
    UWindowResizer { active: !App.settings.useSystemDecoration }
}
