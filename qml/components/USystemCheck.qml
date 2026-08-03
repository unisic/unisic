import QtQuick
import QtQuick.Controls
import Unisic
import Unisic.Kit

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
    // Centred, so the WINDOW is its anchor - the containment rule is the one
    // every flyout follows, see UFlyout.qml.
    margins: UFlyout.margin
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(540, parent ? parent.width - 2 * Theme.spacingXL : 540)
    padding: Theme.spacingXL

    onClosed: if (markSeenOnClose) App.settings.systemCheckSeen = true

    Overlay.modal: Rectangle { color: Theme.modalScrim }

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surface
        border.width: 1
        border.color: Theme.divider
    }

    // Scroller + column, exactly like UConfirmDialog/UShortcutsHelp: the row
    // list grows with what the build was compiled with (OCR adds two more) and
    // with how long each install hint runs in the current language, so a report
    // taller than the window has to SCROLL (UFlyout rule 3) instead of pushing
    // its own buttons off screen. Measured before the fix, at the 880x560
    // minimum window with the Polish details: 5 rows fitted with 14px to spare,
    // 6 rows put "Got it" 2px below the window edge, 8 rows put it 109px below,
    // and it was unreachable - the Column had no scroller at all. With room it
    // is inert: contentHeight equals the height and it cannot be flicked.
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
            spacing: Theme.spacingM

            // Accessible only attaches to an Item, so the dialog identity lives on
            // the content column, not on the Popup itself.
            Accessible.role: Accessible.Dialog
            Accessible.name: qsTr("System check")

            Text {
                width: parent.width
                text: qsTr("System check")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontL
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: qsTr("Unisic works out of the box. These optional tools unlock more - install any that are missing.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontM
                wrapMode: Text.WordWrap
            }
            Text {
                id: docsLink
                text: qsTr("How to install these →")
                color: Theme.accent
                font.pixelSize: Theme.fontS
                font.underline: docsLinkMouse.containsMouse

                function _open() { Qt.openUrlExternally("https://unisic.app/docs/dependencies") }

                MouseArea {
                    id: docsLinkMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: docsLink._open()
                }

                activeFocusOnTab: true
                Keys.onSpacePressed: (e) => UKeys.activate(e, docsLink._open)
                Keys.onReturnPressed: (e) => UKeys.activate(e, docsLink._open)
                Keys.onEnterPressed: (e) => UKeys.activate(e, docsLink._open)
                Accessible.role: Accessible.Link
                Accessible.name: qsTr("How to install these")
                Accessible.description: qsTr("Opens the dependency guide in your browser")
                Accessible.focusable: docsLink.activeFocusOnTab
                Accessible.onPressAction: docsLink._open()
                // Standalone text link - see THE INSET RULE in UFocusRing.qml.
                UFocusRing { hostRadius: Theme.radiusS; inset: -3 }
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

                        // The tick/bang glyph is a picture of the state; spell it
                        // out instead so the row reads as one sentence.
                        Accessible.role: Accessible.ListItem
                        Accessible.name: (modelData.ok ? qsTr("Installed")
                                        : modelData.warn ? qsTr("Missing")
                                        : qsTr("Optional"))
                                         + ": " + modelData.label
                        Accessible.description: modelData.detail

                        // All three parts are folded into the row's name and
                        // description above, so they opt out individually here.
                        Text {
                            width: 18
                            text: modelData.ok ? "✓" : (modelData.warn ? "!" : "-")
                            color: modelData.ok ? Theme.success
                                 : (modelData.warn ? Theme.danger : Theme.textTertiary)
                            font.pixelSize: Theme.fontM
                            font.weight: Font.DemiBold
                            Accessible.ignored: true
                        }
                        Column {
                            width: parent.width - 18 - Theme.spacingS
                            spacing: 2
                            Text {
                                text: modelData.label
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontM
                                font.weight: Font.DemiBold
                                Accessible.ignored: true
                            }
                            Text {
                                width: parent.width
                                text: modelData.detail
                                color: Theme.textTertiary
                                font.pixelSize: Theme.fontS
                                wrapMode: Text.WordWrap
                                Accessible.ignored: true
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
}
