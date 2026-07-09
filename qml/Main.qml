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

    // Hide-to-tray only when a tray actually EXISTS — on GNOME without the
    // AppIndicator extension (or bare wlroots) hiding here would make the app
    // unreachable except by launching `unisic` again.
    onClosing: (close) => {
        if (App.settings.minimizeToTrayOnClose && App.trayAvailable) {
            close.accepted = false
            window.hide()
        } else if (!App.trayAvailable) {
            // quitOnLastWindowClosed is false (tray lifetime) — without a tray
            // an accepted close would leave a hidden resident process. Quit —
            // but never kill an in-flight recording/encode or open editors
            // with unsaved annotations.
            if (App.recording || App.converting) {
                close.accepted = false
                App.showToast(qsTr("Recording in progress — stop it before closing"), true)
            } else if (App.editorWindowsOpen > 0) {
                window.hide()
                close.accepted = false
            } else {
                Qt.quit()
            }
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
            SidebarItem { iconName: "folder-cloud";  label: qsTr("Destinations"); active: currentPage === 4; onClicked: currentPage = 4 }
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
