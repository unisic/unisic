import QtQuick
import QtQuick.Controls
import Unisic
import Unisic.Kit

// "Update available - install now?" prompt for NATIVE package installs, which
// can't self-update in place but CAN be updated by running install.sh in a
// spawned terminal (App.updater.installViaScript). Shown once per version when
// an automatic check discovers an update and App.updater.canInstallViaScript is
// true. Modal Popup on the window Overlay, modelled on UPatchNotes.
Popup {
    id: root

    property string version: ""

    function openFor(v) {
        root.version = v
        root.open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    // Centred, so the WINDOW is its anchor - the containment rule is the one
    // every flyout follows, see UFlyout.qml.
    margins: UFlyout.margin
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(440, parent ? parent.width - 2 * Theme.spacingXL : 440)
    padding: Theme.spacingXL

    Overlay.modal: Rectangle { color: Theme.modalScrim }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animFast; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.animMed; easing.type: Easing.OutBack }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animFast; easing.type: Easing.InCubic }
    }

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surface
        border.width: 1
        border.color: Theme.divider
    }

    // Scroller + column, the same shell as UConfirmDialog: short as this prompt
    // is, its two sentences are translated and the window can be 560px tall, so
    // it scrolls rather than pushing "Install now" off the bottom edge
    // (UFlyout rule 3). With room it is inert - contentHeight equals the height
    // and it cannot be flicked.
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

        MiddleScroll { flickable: bodyFlick }
        WheelBoost { flickable: bodyFlick }

        Column {
            id: bodyCol
            width: bodyFlick.width
            spacing: Theme.spacingL

            // Accessible only attaches to an Item, so the dialog identity lives on
            // the content column, not on the Popup itself.
            Accessible.role: Accessible.Dialog
            Accessible.name: qsTr("Update available")

            // Header: icon tile + title + version pill.
            Row {
                width: parent.width
                spacing: Theme.spacingM

                Rectangle {
                    width: 36; height: 36; radius: Theme.radiusM
                    color: Theme.alpha(Theme.accent, 0.16)
                    anchors.verticalCenter: parent.verticalCenter
                    UIcon {
                        anchors.centerIn: parent
                        name: "star-filled"
                        size: 20
                        color: Theme.accent
                    }
                }
                Text {
                    text: qsTr("Update available")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontL
                    font.weight: Font.DemiBold
                    anchors.verticalCenter: parent.verticalCenter
                }
                Rectangle {
                    height: 22
                    width: versionText.implicitWidth + 16
                    radius: 11
                    color: Theme.alpha(Theme.accent, 0.14)
                    border.width: 1
                    border.color: Theme.alpha(Theme.accent, 0.35)
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        id: versionText
                        anchors.centerIn: parent
                        text: qsTr("v%1").arg(root.version)
                        color: Theme.accent
                        font.pixelSize: Theme.fontS
                        font.weight: Font.DemiBold
                    }
                }
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Unisic %1 is available. Install it now?").arg(root.version)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontM
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("A terminal window will open and ask for your login password to update the installed package.")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontS
            }

            // Footer: Later (dismiss) + Install now.
            Row {
                anchors.right: parent.right
                spacing: Theme.spacingM

                UButton {
                    text: qsTr("Later")
                    variant: "tonal"
                    compact: true
                    onClicked: root.close()
                }
                UButton {
                    text: qsTr("Install now")
                    variant: "filled"
                    compact: true
                    onClicked: {
                        App.updater.installViaScript()
                        root.close()
                    }
                }
            }
        }
    }
}
