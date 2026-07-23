import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import Unisic
import Unisic.Kit

// First-run setup — a full-window flow, NOT a modal dialog: a greeting, a few
// steps that each ask for one decision, and a send-off. It takes over the
// window (below the title bar, so the window stays closable) instead of
// floating over it, so the first launch is one thing at a time.
//
// Every control writes LIVE, exactly like the Settings pages (and a recorded
// hotkey reaches the shortcut daemon immediately, which no local "cancel"
// could honestly undo). Skipping therefore does not roll anything back — it
// just leaves the flow. Since it starts on the shipped defaults, skipping
// without touching anything is what leaves you on the defaults.
//
// markSeenOnClose clears App.settings.showWelcome so the one-shot first-run
// flow never returns; the Settings button opens it with markSeenOnClose:false
// so a manual peek doesn't touch the latch.
Item {
    id: root

    property bool markSeenOnClose: true

    // Popup-compatible API — Main.qml drives this like the modal it replaced.
    signal closed()
    function open() { step = 0; shown = true }
    function close() {
        if (!shown)
            return
        shown = false
        if (markSeenOnClose)
            App.settings.showWelcome = false
        closed()
    }

    property bool shown: false
    property int step: 0
    readonly property int stepCount: 7

    // Core built-ins (hardcoded in Theme.qml) + every theme in <config>/themes —
    // the user's own AND the decorative ones seeded there as real JSON files
    // (Catppuccin, Dracula, Nord, Gruvbox). Same list the Interface page shows,
    // and it refreshes as theme files come and go.
    readonly property var themeIds: {
        var a = ["system", "unisic", "dark", "light"]
        var c = ThemeController.customThemes
        for (var i = 0; i < c.length; ++i) a.push(c[i].id)
        return a
    }
    readonly property var themeNames: {
        var a = [qsTr("System Theme"), "Unisic", qsTr("Dark"), qsTr("Light")]
        var c = ThemeController.customThemes
        for (var i = 0; i < c.length; ++i) a.push(c[i].name)
        return a
    }

    // One decision per row: label, one line of consequence, a switch.
    component ToggleCard: Rectangle {
        id: card
        property string label: ""
        property string detail: ""
        property bool value: false
        signal toggled(bool v)

        width: parent ? parent.width : 0
        height: cardCol.implicitHeight + 2 * Theme.spacingM
        radius: Theme.radiusM
        color: Theme.surface
        border.width: 1
        border.color: Theme.divider

        Row {
            x: Theme.spacingM
            y: Theme.spacingM
            width: parent.width - 2 * Theme.spacingM
            spacing: Theme.spacingM

            Column {
                id: cardCol
                width: parent.width - 60 - Theme.spacingM
                spacing: 2
                Text {
                    width: parent.width
                    text: card.label
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontM
                    wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    text: card.detail
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontS
                    wrapMode: Text.WordWrap
                    elide: Text.ElideMiddle
                }
            }
            USwitch {
                anchors.verticalCenter: parent.verticalCenter
                checked: card.value
                onToggled: (c) => card.toggled(c)
            }
        }
    }

    function shortcutsFor(actionId) {
        switch (actionId) {
        case "capture-fullscreen": return App.settings.hotkeyFullScreen
        case "capture-region": return App.settings.hotkeyRegion
        case "capture-window": return App.settings.hotkeyWindow
        case "record-video": return App.settings.hotkeyRecord
        }
        return ""
    }
    function setShortcut(actionId, keys) {
        switch (actionId) {
        case "capture-fullscreen": App.settings.hotkeyFullScreen = keys; break
        case "capture-region": App.settings.hotkeyRegion = keys; break
        case "capture-window": App.settings.hotkeyWindow = keys; break
        case "record-video": App.settings.hotkeyRecord = keys; break
        default: return
        }
        App.applyHotkey(actionId)
    }
    function next() {
        if (step < stepCount - 1) { step = step + 1; return }
        close()
    }
    function back() { if (step > 0) step = step - 1 }

    visible: shown
    opacity: shown ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
    // Above every page, below the window's own title bar (anchored by the parent).
    z: 1000
    focus: shown

    Keys.onPressed: (e) => {
        // A hotkey recorder on the shortcuts step swallows the keys it wants,
        // but its cancel (Escape) and any key it declines would otherwise
        // bubble up here and navigate — or close — the whole flow mid-record.
        if (App.shortcutRecording)
            return
        if (e.key === Qt.Key_Escape) { root.close(); e.accepted = true }
        else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) { root.next(); e.accepted = true }
        else if (e.key === Qt.Key_Left) { root.back(); e.accepted = true }
        else if (e.key === Qt.Key_Right) { root.next(); e.accepted = true }
    }
    onShownChanged: if (shown) forceActiveFocus()

    // Opaque backdrop: this IS the window while it runs, and it swallows every
    // click so nothing behind can be operated by accident.
    Rectangle {
        anchors.fill: parent
        color: Theme.background
        MouseArea { anchors.fill: parent; hoverEnabled: true }
    }

    // ---- skip, top right ----
    Text {
        id: skipLink
        visible: root.step < root.stepCount - 1
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.spacingXL
        text: qsTr("Skip setup")
        color: skipMouse.containsMouse ? Theme.textSecondary : Theme.textTertiary
        font.pixelSize: Theme.fontS
        font.underline: skipMouse.containsMouse
        MouseArea {
            id: skipMouse
            anchors.fill: parent
            anchors.margins: -6
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.close()
        }
    }

    // ---- the steps ----
    Item {
        id: stage
        anchors.top: parent.top
        anchors.bottom: bottomBar.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: Theme.spacingXL
        anchors.bottomMargin: Theme.spacingM

        // Every step is centred in the same column so the flow doesn't jump
        // around as the content changes.
        component Step: Item {
            property int index: 0
            anchors.fill: parent
            visible: opacity > 0
            opacity: root.step === index ? 1 : 0
            enabled: root.step === index
            Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        }

        // 0 — greeting
        Step {
            index: 0
            Column {
                anchors.centerIn: parent
                width: Math.min(520, parent.width - 2 * Theme.spacingXL)
                spacing: Theme.spacingM

                Image {
                    source: "qrc:/resources/icons/unisic.svg"
                    sourceSize: Qt.size(112, 112)
                    width: 112; height: 112
                    smooth: true
                    anchors.horizontalCenter: parent.horizontalCenter
                    layer.enabled: App.devBuild
                    layer.effect: MultiEffect { saturation: -1.0 }
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Welcome to Unisic")
                    color: Theme.textPrimary
                    font.pixelSize: 30
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Screenshots and screen recording, built for Wayland.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontL
                    wrapMode: Text.WordWrap
                }
                Item { width: 1; height: Theme.spacingS }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Unisic already works with the settings it ships with. The next few steps just let you make it yours - skip them and the defaults stay, or change everything later in Settings.")
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontM
                    wrapMode: Text.WordWrap
                }
            }
        }

        // 1 — look and language
        Step {
            index: 1
            Column {
                anchors.centerIn: parent
                width: Math.min(520, parent.width - 2 * Theme.spacingXL)
                spacing: Theme.spacingM

                Text {
                    width: parent.width
                    text: qsTr("Make it yours")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontXL
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    text: qsTr("Both apply as you pick them, so you can see the result right away.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                    wrapMode: Text.WordWrap
                }
                Item { width: 1; height: Theme.spacingS }

                Column {
                    width: parent.width
                    spacing: 6
                    Text {
                        text: qsTr("Theme")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontM
                    }
                    UComboBox {
                        width: parent.width
                        model: root.themeNames
                        currentIndex: Math.max(0, root.themeIds.indexOf(ThemeController.themeName))
                        onActivated: (i) => ThemeController.themeName = root.themeIds[i]
                    }
                    Text {
                        width: parent.width
                        text: qsTr("Write your own as a small .json file - Settings › Interface opens the folder.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                        wrapMode: Text.WordWrap
                    }
                }

                Column {
                    width: parent.width
                    spacing: 6
                    Text {
                        text: qsTr("Language")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontM
                    }
                    UComboBox {
                        width: parent.width
                        readonly property var ids: ["system", "en", "pl", "es", "it", "fr"]
                        // Native names on purpose — every user recognises their
                        // own language regardless of the current UI.
                        model: [qsTr("System"), "English", "Polski", "Español", "Italiano", "Français"]
                        currentIndex: Math.max(0, ids.indexOf(App.settings.uiLanguage))
                        onActivated: (i) => App.settings.uiLanguage = ids[i]
                    }
                }

                ToggleCard {
                    label: qsTr("Use the system window decoration")
                    detail: qsTr("On: your desktop draws the title bar and buttons. Off: Unisic draws its own, matching the theme.")
                    value: App.settings.useSystemDecoration
                    onToggled: (v) => App.settings.useSystemDecoration = v
                }
            }
        }

        // 2 — the after-capture pipeline
        Step {
            index: 2
            Column {
                anchors.centerIn: parent
                width: Math.min(520, parent.width - 2 * Theme.spacingXL)
                spacing: Theme.spacingM

                Text {
                    width: parent.width
                    text: qsTr("What happens after a capture")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontXL
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    text: qsTr("Each of these runs on its own, so you can have several at once.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                    wrapMode: Text.WordWrap
                }
                Item { width: 1; height: Theme.spacingS }

                ToggleCard {
                    label: qsTr("Save it to a file")
                    detail: App.settings.saveDirectory
                    value: App.settings.autoSave
                    onToggled: (v) => App.settings.autoSave = v
                }
                ToggleCard {
                    label: qsTr("Copy it to the clipboard")
                    detail: qsTr("Ready to paste straight into a chat or document.")
                    value: App.settings.copyToClipboard
                    onToggled: (v) => App.settings.copyToClipboard = v
                }
                ToggleCard {
                    label: qsTr("Open the editor")
                    detail: qsTr("Annotate with arrows, text, blur and more before you share it.")
                    value: App.settings.openEditor
                    onToggled: (v) => App.settings.openEditor = v
                }
            }
        }

        // 3 — the handful of behaviours that surprise people if left unsaid
        Step {
            index: 3
            Column {
                anchors.centerIn: parent
                width: Math.min(520, parent.width - 2 * Theme.spacingXL)
                spacing: Theme.spacingM

                Text {
                    width: parent.width
                    text: qsTr("How Unisic behaves")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontXL
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    text: qsTr("The few habits worth deciding on now.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                    wrapMode: Text.WordWrap
                }
                Item { width: 1; height: Theme.spacingS }

                ToggleCard {
                    label: qsTr("Ask where to save every capture")
                    detail: qsTr("Off: files go straight to your captures folder with a generated name.")
                    value: App.settings.askWhereToSave
                    onToggled: (v) => App.settings.askWhereToSave = v
                }
                ToggleCard {
                    label: qsTr("Closing the window keeps Unisic in the tray")
                    detail: qsTr("Off: closing the window quits, and the hotkeys stop working.")
                    value: App.settings.minimizeToTrayOnClose
                    onToggled: (v) => App.settings.minimizeToTrayOnClose = v
                }
                ToggleCard {
                    label: qsTr("Start Unisic at login")
                    detail: qsTr("Starts hidden in the tray, so the capture hotkeys work right after you log in.")
                    value: App.autostartEnabled
                    onToggled: (v) => App.autostartEnabled = v
                }
            }
        }

        // 4 — the capture card, previewed in place
        Step {
            index: 4
            Column {
                anchors.centerIn: parent
                width: Math.min(520, parent.width - 2 * Theme.spacingXL)
                spacing: Theme.spacingM

                Text {
                    width: parent.width
                    text: qsTr("The capture card")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontXL
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    text: qsTr("What pops up after each capture. The preview below is empty - nothing is being captured.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                    wrapMode: Text.WordWrap
                }

                UNotifPreview {
                    id: notifPreview
                    width: parent.width
                    opacity: App.settings.showCapturePopup ? 1.0 : 0.35
                    Behavior on opacity { NumberAnimation { duration: 140 } }
                    style: App.settings.capturePopupStyle
                    position: App.settings.capturePopupPosition
                }
                Text {
                    width: parent.width
                    text: notifPreview.hasActions
                          ? qsTr("Hover an action to see what it does. Click one to remove it, click the faded one to put it back, or hold and drag to reorder. A button still only shows up when the capture can back it.")
                          : qsTr("This style is just the filename, so it carries no action buttons.")
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontS
                    wrapMode: Text.WordWrap
                }

                ToggleCard {
                    label: qsTr("Show the card after each capture")
                    detail: qsTr("Its thumbnail drags straight into another app, and it carries actions like save, upload and edit.")
                    value: App.settings.showCapturePopup
                    onToggled: (v) => App.settings.showCapturePopup = v
                }

                Row {
                    width: parent.width
                    spacing: Theme.spacingM
                    enabled: App.settings.showCapturePopup
                    opacity: enabled ? 1.0 : 0.45

                    Column {
                        width: (parent.width - Theme.spacingM) / 2
                        spacing: 6
                        Text {
                            text: qsTr("Style")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontM
                        }
                        UComboBox {
                            width: parent.width
                            readonly property var ids: ["casual", "compact", "small", "minimal", "thumbnail"]
                            model: [qsTr("Casual"), qsTr("Compact"), qsTr("Small"), qsTr("Minimal"), qsTr("Thumbnail")]
                            currentIndex: Math.max(0, ids.indexOf(App.settings.capturePopupStyle))
                            onActivated: (i) => App.settings.capturePopupStyle = ids[i]
                        }
                    }
                    Column {
                        width: (parent.width - Theme.spacingM) / 2
                        spacing: 6
                        Text {
                            text: qsTr("Corner")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontM
                            // The system notification server places its own
                            // popups; only a card Unisic draws obeys this.
                            opacity: App.capCustomNotification ? 1.0 : 0.5
                        }
                        UComboBox {
                            width: parent.width
                            enabled: App.capCustomNotification
                            readonly property var ids: ["top-left", "top-center", "top-right",
                                                        "bottom-left", "bottom-center", "bottom-right"]
                            model: [qsTr("Top left"), qsTr("Top center"), qsTr("Top right"),
                                    qsTr("Bottom left"), qsTr("Bottom center"), qsTr("Bottom right")]
                            currentIndex: Math.max(0, ids.indexOf(App.settings.capturePopupPosition))
                            onActivated: (i) => App.settings.capturePopupPosition = ids[i]
                        }
                    }
                }
                Text {
                    visible: !App.capCustomNotification
                    width: parent.width
                    text: qsTr("This desktop places notifications itself, so the corner is up to it.")
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontS
                    wrapMode: Text.WordWrap
                }
            }
        }

        // 5 — the keys that are already bound
        Step {
            index: 5
            Column {
                anchors.centerIn: parent
                width: Math.min(520, parent.width - 2 * Theme.spacingXL)
                spacing: Theme.spacingM

                Text {
                    width: parent.width
                    text: qsTr("Your shortcuts")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontXL
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    text: App.hotkeysAvailable
                          ? qsTr("These work anywhere, without the Unisic window open.")
                          : qsTr("This desktop does not let Unisic register global shortcuts.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                    wrapMode: Text.WordWrap
                }
                Item { width: 1; height: Theme.spacingS }

                // Editable in place — the same multi-binding editor the Hotkeys
                // page uses, so a key can be set (or cleared) without hunting
                // through Settings first.
                Repeater {
                    model: App.hotkeysAvailable ? [
                        { id: "capture-fullscreen", label: qsTr("Capture the full screen") },
                        { id: "capture-region", label: qsTr("Capture a region") },
                        { id: "capture-window", label: qsTr("Capture a window") },
                        { id: "record-video", label: qsTr("Record video (start and stop)") }
                    ] : []
                    delegate: Column {
                        width: parent.width
                        spacing: 4
                        Text {
                            width: parent.width
                            text: modelData.label
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontM
                            elide: Text.ElideRight
                        }
                        UShortcutList {
                            width: parent.width
                            shortcuts: root.shortcutsFor(modelData.id)
                            onChanged: (t) => root.setShortcut(modelData.id, t)
                            formatKey: (key, mods, scan) => App.formatShortcut(key, mods, scan)
                            onCaptureStateChanged: (active) => App.setShortcutRecording(active)
                        }
                    }
                }

                Text {
                    width: parent.width
                    text: App.hotkeysAvailable
                          ? qsTr("Add alternatives with “+ Add shortcut”, or remove one with its ×. Everything else lives in Settings › Hotkeys.")
                          : qsTr("Bind the capture keys in your desktop's own keyboard settings instead. The tray icon and the Capture page work regardless.")
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontS
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    visible: !App.recordingAvailable
                    width: parent.width
                    height: recNote.implicitHeight + 2 * Theme.spacingM
                    radius: Theme.radiusM
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.divider
                    Text {
                        id: recNote
                        x: Theme.spacingM
                        y: Theme.spacingM
                        width: parent.width - 2 * Theme.spacingM
                        text: qsTr("Screen recording is unavailable on this desktop - it needs the ScreenCast portal. Screenshots are unaffected.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontS
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // 6 — send-off
        Step {
            index: 6
            Column {
                anchors.centerIn: parent
                width: Math.min(520, parent.width - 2 * Theme.spacingXL)
                spacing: Theme.spacingM

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("You're all set")
                    color: Theme.textPrimary
                    font.pixelSize: 30
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: App.hotkeysAvailable && App.settings.hotkeyRegion.length > 0
                          ? qsTr("Press %1 to grab your first region - or use the tray icon and the Capture page.")
                                .arg(App.settings.hotkeyRegion)
                          : qsTr("Take your first capture from the tray icon or the Capture page.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                    wrapMode: Text.WordWrap
                }
                Item { width: 1; height: Theme.spacingS }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Enjoy Unisic.")
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontM
                }
            }
        }
    }

    // ---- progress + navigation ----
    Column {
        id: bottomBar
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Theme.spacingXL
        spacing: Theme.spacingM

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8
            Repeater {
                model: root.stepCount
                delegate: Rectangle {
                    width: root.step === index ? 20 : 8
                    height: 8
                    radius: 4
                    color: root.step === index ? Theme.accent : Theme.divider
                    anchors.verticalCenter: parent.verticalCenter
                    Behavior on width { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                }
            }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.spacingS
            UButton {
                visible: root.step > 0
                text: qsTr("Back")
                variant: "ghost"
                compact: true
                onClicked: root.back()
            }
            UButton {
                text: root.step === root.stepCount - 1 ? qsTr("Start using Unisic") : qsTr("Next")
                variant: "filled"
                compact: true
                onClicked: root.next()
            }
        }
    }
}
