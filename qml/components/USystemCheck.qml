import QtQuick
import QtQuick.Controls
import Unisic

// First-run (and on-demand) system check. Lists the optional runtime
// dependencies from App.dependencyReport() — each with a tick or an install
// hint — plus a Copy-diagnostics action for bug reports. Modal Popup parented
// to Overlay.overlay, same shell as UConfirmDialog.
//
// markSeenOnClose flips App.settings.systemCheckSeen so the one-shot first-run
// popup never returns; the Settings "Run system check" button opens its own
// instance with markSeenOnClose:false so a manual peek doesn't touch the latch.
Popup {
    id: root

    property bool markSeenOnClose: true

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(540, parent ? parent.width - 2 * Theme.spacingXL : 540)
    padding: Theme.spacingXL

    onClosed: if (markSeenOnClose) App.settings.systemCheckSeen = true

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
            text: qsTr("System check")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontL
            font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            text: qsTr("Unisic works out of the box. These optional tools unlock more — install any that are missing.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontM
            wrapMode: Text.WordWrap
        }
        Text {
            text: qsTr("How to install these →")
            color: Theme.accent
            font.pixelSize: Theme.fontS
            font.underline: docsLinkMouse.containsMouse
            MouseArea {
                id: docsLinkMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Qt.openUrlExternally("https://unisic.app/docs/dependencies")
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingS

            Repeater {
                // A plain list of {label, ok, warn, detail} maps from C++.
                model: App.dependencyReport()
                delegate: Row {
                    width: parent ? parent.width : 0
                    spacing: Theme.spacingS

                    Text {
                        width: 18
                        text: modelData.ok ? "✓" : (modelData.warn ? "!" : "—")
                        color: modelData.ok ? Theme.success
                             : (modelData.warn ? Theme.danger : Theme.textTertiary)
                        font.pixelSize: Theme.fontM
                        font.weight: Font.DemiBold
                    }
                    Column {
                        width: parent.width - 18 - Theme.spacingS
                        spacing: 2
                        Text {
                            text: modelData.label
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontM
                            font.weight: Font.DemiBold
                        }
                        Text {
                            width: parent.width
                            text: modelData.detail
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontS
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }

        Item { width: 1; height: Theme.spacingS }

        Row {
            anchors.right: parent.right
            spacing: Theme.spacingS
            UButton {
                text: qsTr("Copy diagnostics")
                variant: "ghost"
                compact: true
                onClicked: { App.copyText(App.systemDiagnostics()); App.showToast(qsTr("Diagnostics copied")) }
            }
            UButton {
                text: qsTr("Got it")
                variant: "filled"
                compact: true
                onClicked: root.close()
            }
        }
    }
}
