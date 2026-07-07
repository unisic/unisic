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
    visible: true
    title: "Unisic"
    color: Theme.backgroundDeep

    property int currentPage: 0

    onClosing: (close) => {
        if (App.settings.minimizeToTrayOnClose) {
            close.accepted = false
            window.hide()
        }
    }

    Connections {
        target: App
        function onShowMainWindowRequested() {
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

    Rectangle { // sidebar
        id: sidebar
        width: 224
        height: parent.height
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
                        running: App.recording
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.2; duration: 600 }
                        NumberAnimation { to: 1.0; duration: 600 }
                    }
                }
                Text {
                    text: App.converting ? qsTr("Encoding…")
                                         : Qt.formatTime(new Date(0, 0, 0, 0, 0, App.recordSeconds), "mm:ss")
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
    }

    Item { // content
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        CapturePage      { anchors.fill: parent; visible: currentPage === 0 }
        RecordPage       { anchors.fill: parent; visible: currentPage === 1 }
        GifPage          { anchors.fill: parent; visible: currentPage === 2 }
        HistoryPage      { anchors.fill: parent; visible: currentPage === 3 }
        DestinationsPage { anchors.fill: parent; visible: currentPage === 4 }
        SettingsPage     { anchors.fill: parent; visible: currentPage === 5 }
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
    }
}
