import QtQuick
import QtQuick.Controls
import Unisic

// In-app themed confirmation dialog — replaces QtQuick.Dialogs' MessageDialog,
// which opens a separate Qt-styled window that clashes with the design system.
// Parent it to Overlay.overlay so it centers over the whole window and dims it.
Popup {
    id: root

    property string title: ""
    property string text: ""
    property string confirmText: qsTr("OK")
    property string cancelText: qsTr("Cancel")
    // Destructive actions get the danger colour on the confirm button.
    property bool destructive: false

    signal accepted()

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(440, parent ? parent.width - 2 * Theme.spacingXL : 440)
    padding: Theme.spacingXL

    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.45) }

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surface
        border.width: 1
        border.color: Theme.divider
    }

    contentItem: Column {
        spacing: Theme.spacingM

        Text {
            width: parent.width
            text: root.title
            color: Theme.textPrimary
            font.pixelSize: Theme.fontL
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            text: root.text
            color: Theme.textSecondary
            font.pixelSize: Theme.fontM
            wrapMode: Text.WordWrap
        }
        Item { width: 1; height: Theme.spacingS }
        Row {
            anchors.right: parent.right
            spacing: Theme.spacingS
            UButton {
                text: root.cancelText
                variant: "ghost"
                compact: true
                onClicked: root.close()
            }
            UButton {
                text: root.confirmText
                variant: root.destructive ? "danger" : "filled"
                compact: true
                onClicked: { root.close(); root.accepted() }
            }
        }
    }
}
