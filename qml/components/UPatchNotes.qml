import QtQuick
import QtQuick.Controls
import Unisic

// "What's new" sheet shown when the version label is clicked. Renders the
// running version's section of the bundled CHANGELOG.md (App.changelog(lang),
// markdown) with an English/Polish toggle. Modelled on UShortcutsHelp: a modal
// Popup on the window Overlay.
Popup {
    id: root

    property string version: ""
    // Default to the UI language, then the user can switch.
    property string lang: Qt.locale().name.slice(0, 2) === "pl" ? "pl" : "en"
    readonly property string notes: App.changelog(lang)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(480, parent ? parent.width - 2 * Theme.spacingXL : 480)
    readonly property real maxBodyHeight: (parent ? parent.height : 600) * 0.6
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

        // Header: icon + title on the left, English/Polish toggle on the right.
        Item {
            width: parent.width
            height: 30

            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingS
                UIcon {
                    name: "star-filled"
                    size: 20
                    color: Theme.accent
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: qsTr("What's new in v%1").arg(root.version)
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontL
                    font.weight: Font.DemiBold
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // English / Polish segmented toggle. Active language filled with the
            // accent; the other is a muted outline — so the current one is clear.
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Repeater {
                    model: [{ code: "en", label: qsTr("EN") },
                            { code: "pl", label: qsTr("PL") }]
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool selected: root.lang === modelData.code
                        width: 40; height: 28; radius: Theme.radiusS
                        color: selected ? Theme.accent : Theme.surfaceHi
                        border.width: 1
                        border.color: selected ? Theme.accent : Theme.divider
                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: parent.selected ? Theme.textOnAccent : Theme.textSecondary
                            font.pixelSize: Theme.fontS
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.lang = modelData.code
                        }
                    }
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.divider }

        Text {
            visible: root.notes === ""
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("No release notes for this version.")
            color: Theme.textTertiary
            font.pixelSize: Theme.fontM
        }

        ScrollView {
            visible: root.notes !== ""
            width: parent.width
            height: Math.min(notesText.implicitHeight, root.maxBodyHeight)
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Text {
                id: notesText
                width: root.width - 2 * root.padding
                text: root.notes
                textFormat: Text.MarkdownText
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: Theme.fontM
                linkColor: Theme.accent
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }

        Item { width: 1; height: Theme.spacingXS }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Press Esc to close")
            color: Theme.textTertiary
            font.pixelSize: Theme.fontS
        }
    }
}
