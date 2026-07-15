import QtQuick
import QtQuick.Controls
import Unisic

// The Ctrl+/ cheat-sheet. Lists the built-in WINDOW shortcuts wired in
// Main.qml — these are QtQuick `Shortcut` items, not GlobalHotkeys actions, so
// they never appear in (nor are editable from) the Settings shortcut UI. Keep
// `model` here in sync with the Shortcut items in Main.qml; the two are the
// single source between behaviour (Main.qml) and this display.
Popup {
    id: root

    // [{ keys: ["Ctrl", "/"], label: "…" }, …]. keys is a list of keycaps.
    property var model: []

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(460, parent ? parent.width - 2 * Theme.spacingXL : 460)
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

        Row {
            width: parent.width
            spacing: Theme.spacingS
            UIcon {
                name: "keyboard"
                size: 20
                color: Theme.accent
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: qsTr("Keyboard shortcuts")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontL
                font.weight: Font.DemiBold
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.divider }

        Column {
            width: parent.width
            spacing: Theme.spacingS

            Repeater {
                model: root.model
                delegate: Row {
                    required property var modelData
                    width: parent.width
                    height: 30
                    spacing: Theme.spacingM

                    Text {
                        width: parent.width - keycaps.width - parent.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontM
                        elide: Text.ElideRight
                    }

                    Row {
                        id: keycaps
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4

                        Repeater {
                            model: modelData.keys
                            delegate: Row {
                                id: capRow
                                required property string modelData
                                required property int index
                                spacing: 4
                                Text {
                                    visible: capRow.index > 0
                                    text: "+"
                                    color: Theme.textTertiary
                                    font.pixelSize: Theme.fontS
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Rectangle {
                                    width: Math.max(24, cap.implicitWidth + 14)
                                    height: 24
                                    radius: Theme.radiusS
                                    color: Theme.surfaceHi
                                    border.width: 1
                                    border.color: Theme.divider
                                    Text {
                                        id: cap
                                        anchors.centerIn: parent
                                        text: capRow.modelData
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontS + 1
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Item { width: 1; height: Theme.spacingS }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Press Ctrl+/ or Esc to close")
            color: Theme.textTertiary
            font.pixelSize: Theme.fontS
        }
    }
}
