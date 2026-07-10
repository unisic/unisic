import QtQuick
import Unisic

// Multi-binding hotkey editor: one removable chip per bound key (left to
// right, primary first) plus a ghost "+ Add shortcut" button that records an
// alternative in place. The value is the comma-joined portable string
// ("Meta+Shift+S, Print") — exactly what the settings store,
// GlobalHotkeys::keysFor parses (each chord becomes an ALTERNATE key of the
// same KGlobalAccel action) and portableFromKeys round-trips. QKeySequence
// carries at most 4 chords in one string, hence the cap.
Item {
    id: root

    property string shortcuts: ""
    readonly property var list: shortcuts.length > 0 ? shortcuts.split(", ") : []
    readonly property int maxBindings: 4

    signal changed(string shortcuts)

    implicitWidth: flow.implicitWidth
    implicitHeight: flow.implicitHeight

    Flow {
        id: flow
        width: parent.width
        spacing: 6

        Repeater {
            model: root.list
            delegate: Rectangle {
                required property string modelData
                required property int index
                width: chipText.implicitWidth + 12 + 24
                height: 32
                radius: Theme.radiusM
                color: Theme.surfaceHi
                border.width: 1
                border.color: Theme.divider
                Text {
                    id: chipText
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontS + 1
                }
                UIconButton {
                    iconName: "close"; iconSize: 10
                    width: 20; height: 20
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    tooltip: qsTr("Remove this binding")
                    onClicked: {
                        const next = root.list.slice()
                        next.splice(index, 1)
                        root.changed(next.join(", "))
                    }
                }
            }
        }

        UShortcutRecorder {
            visible: root.list.length < root.maxBindings
            addStyle: true
            width: recording ? 150 : implicitWidth
            placeholder: root.list.length === 0 ? qsTr("+ Add shortcut")
                                                : qsTr("+ Add alternative")
            shortcut: ""
            onRecorded: (t) => {
                if (t.length === 0 || root.list.indexOf(t) >= 0)
                    return
                root.changed(root.list.concat([t]).join(", "))
            }
        }
    }
}
