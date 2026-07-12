import QtQuick
import QtQuick.Window
import QtQuick.Effects
import Unisic
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
    // Height reserved at the top for the custom title bar (0 with system decos).
    readonly property int chromeTop: App.settings.useSystemDecoration ? 0 : 38

    property int currentPage: 0

    // Built-in WINDOW shortcuts (QtQuick Shortcut items below). These are NOT
    // GlobalHotkeys/KGlobalAccel actions: they only fire while the main window
    // has focus, they never register a system-wide grab, and they are
    // deliberately fixed — they do not appear in, nor are editable from, the
    // Settings shortcut UI. Ctrl+/ pops the cheat-sheet listing them. Keep this
    // list and the Shortcut items in sync (single source for both).
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
        Qt.quit()
    }

    // `enabled: !App.shortcutRecording` is REQUIRED: with the default
    // Qt.WindowShortcut context these win Qt's shortcut-override race against a
    // focused UShortcutRecorder, so a user binding e.g. Ctrl+Q as a global
    // hotkey would trigger the window action (quit!) instead of recording it.
    Shortcut { enabled: !App.shortcutRecording; sequences: ["Ctrl+/"]; onActivated: shortcutsHelp.opened ? shortcutsHelp.close() : shortcutsHelp.open() }
    Shortcut { enabled: !App.shortcutRecording; sequences: ["Ctrl+W"]; onActivated: window.hideToTray() }
    Shortcut { enabled: !App.shortcutRecording; sequences: ["Ctrl+Q"]; onActivated: window.quitApp() }
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
                Qt.quit()
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
                Qt.quit()
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

    Rectangle { // content backdrop with subtle vertical falloff
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.background }
            GradientStop { position: 1.0; color: Theme.backgroundDeep }
        }
    }

    Rectangle { // custom title bar (frameless decoration)
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 38
        visible: !App.settings.useSystemDecoration
        z: 20
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.lighter(Theme.primary, 1.12) }
            GradientStop { position: 1.0; color: Theme.primary }
        }

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

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingL
            anchors.verticalCenter: parent.verticalCenter
            text: "Unisic"
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

    Rectangle { // sidebar
        id: sidebar
        width: 224
        y: window.chromeTop
        height: parent.height - window.chromeTop
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.lighter(Theme.primary, 1.12) }
            GradientStop { position: 1.0; color: Theme.primary }
        }
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.shadow
            shadowBlur: 1.0
            shadowHorizontalOffset: 3
            shadowOpacity: 0.5
        }
        z: 2

        Column {
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingXL
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacingM
            anchors.rightMargin: Theme.spacingM
            spacing: 4

            Row {
                spacing: 10
                anchors.horizontalCenter: parent.horizontalCenter
                Image {
                    source: "qrc:/resources/icons/unisic.svg"
                    sourceSize: Qt.size(34, 34)
                    width: 34; height: 34
                    smooth: true
                    anchors.verticalCenter: parent.verticalCenter
                    // Dev builds are gray everywhere (tray, menu, here too).
                    layer.enabled: App.devBuild
                    layer.effect: MultiEffect { saturation: -1.0 }
                }
                Text {
                    text: "Unisic"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontXL
                    font.weight: Font.Bold
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Item { width: 1; height: Theme.spacingXL }

            SidebarItem { iconName: "camera-photo";  label: qsTr("Capture");      active: currentPage === 0; onClicked: currentPage = 0 }
            SidebarItem { iconName: "media-record";  label: qsTr("Record");       active: currentPage === 1; onClicked: currentPage = 1 }
            SidebarItem { iconName: "gif";           label: qsTr("GIF");          active: currentPage === 2; onClicked: currentPage = 2 }
            SidebarItem { iconName: "view-history";  label: qsTr("History");      active: currentPage === 3; onClicked: currentPage = 3 }
            SidebarItem { iconName: "folder-cloud";  label: qsTr("Servers"); active: currentPage === 4; onClicked: currentPage = 4 }
            SidebarItem { iconName: "configure";     label: qsTr("Settings");     active: currentPage === 5; onClicked: currentPage = 5 }
        }

        // Recording pill
        Rectangle {
            visible: App.recording || App.converting
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacingL
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
                    width: 10; height: 10; radius: 5
                    color: Theme.danger
                    anchors.verticalCenter: parent.verticalCenter
                    SequentialAnimation on opacity {
                        // Gate on window visibility too: with the window hidden to
                        // tray during a long recording, an infinite animation keeps
                        // the GUI thread waking ~60x/s for an invisible dot.
                        running: App.recording && window.visible
                        loops: Animation.Infinite
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
                    text: App.converting ? qsTr("Encoding…") : fmtElapsed(App.recordSeconds)
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontM
                    anchors.verticalCenter: parent.verticalCenter
                }
                UIconButton {
                    visible: App.recording && !App.converting
                    iconName: "stop"
                    iconSize: 15
                    width: 30; height: 30
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: App.stopRecording()
                }
            }
        }

        // Version / build footer — hidden while the recording pill occupies the bottom.
        Text {
            visible: !App.recording && !App.converting
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacingM
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            text: "v" + App.appVersion + (App.buildNumber === "dev"
                    ? " · dev"
                    : " · build " + App.buildNumber)
                  + (App.buildDate ? "\n" + App.buildDate : "")
            color: Theme.textTertiary
            font.pixelSize: Theme.fontS
        }
    }

    Item { // content
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: window.chromeTop
        anchors.bottom: parent.bottom

        // Only the visible page is instantiated; leaving a page unloads it so
        // idle RAM tracks a single page instead of all six at once.
        Component { id: capturePage;      CapturePage {} }
        Component { id: recordPage;       RecordPage {} }
        Component { id: gifPage;          GifPage {} }
        Component { id: historyPage;      HistoryPage {} }
        Component { id: destinationsPage; DestinationsPage {} }
        Component { id: settingsPage;     SettingsPage {} }

        Loader { anchors.fill: parent; active: currentPage === 0; visible: active; sourceComponent: capturePage }
        Loader { anchors.fill: parent; active: currentPage === 1; visible: active; sourceComponent: recordPage }
        Loader { anchors.fill: parent; active: currentPage === 2; visible: active; sourceComponent: gifPage }
        Loader { anchors.fill: parent; active: currentPage === 3; visible: active; sourceComponent: historyPage }
        Loader { anchors.fill: parent; active: currentPage === 4; visible: active; sourceComponent: destinationsPage }
        Loader { anchors.fill: parent; active: currentPage === 5; visible: active; sourceComponent: settingsPage }
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

        Text {
            id: toastLabel
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
}
