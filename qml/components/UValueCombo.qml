import QtQuick
import QtQuick.Controls as C
import Unisic

// Numeric dropdown: a preset list plus a free-entry "Custom" field at the
// bottom. A typed value applies immediately but is NEVER added to the preset
// list. Same Overlay-parented popup pattern as UComboBox so it escapes
// Flickable clipping. Optional `tooltip` shows on hover (used for hints like
// "0 = keep open" that used to bloat the row label).
Rectangle {
    id: root
    property var values: []           // preset numbers, ascending
    property int value: 0
    property int from: 0
    property int to: 100
    property string suffix: ""
    property string tooltip: ""
    signal changed(int value)
    // Same live-preview hooks as UComboBox: the entry under the pointer, and
    // whether the list is open (i.e. the user is choosing).
    signal highlighted(int value)
    readonly property alias listOpen: popup.opened

    implicitWidth: 130
    implicitHeight: 40
    radius: Theme.radiusM
    color: mouse.containsMouse ? Theme.tertiary : Theme.surfaceHi
    border.width: 1
    border.color: popup.opened ? Theme.accent : Theme.divider
    Behavior on color { ColorAnimation { duration: Theme.animFast } }

    function _apply(v) {
        v = Math.max(from, Math.min(to, Math.round(v)))
        // Emit only — assigning `value` would break the consumer's binding.
        if (v !== value) root.changed(v)
    }

    Item {
        id: outsideCatcher
        parent: C.Overlay.overlay
        width: parent ? parent.width : 0
        height: parent ? parent.height : 0
        visible: popup.opened && parent !== null
        z: 999

        MouseArea {
            anchors.fill: parent
            onClicked: popup.close()
            onWheel: (w) => { popup.close(); w.accepted = false }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: chevron.left
        text: root.value + root.suffix
        color: Theme.textPrimary
        font.pixelSize: Theme.fontM
        elide: Text.ElideRight
    }
    UIcon {
        id: chevron
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        name: "chevron-down"
        size: 16
        color: Theme.textSecondary
        rotation: popup.opened ? 180 : 0
        Behavior on rotation { NumberAnimation { duration: Theme.animFast } }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: popup.opened ? popup.close() : popup.open()
    }

    UHoverTip {
        anchor: root
        text: root.tooltip
        show: mouse.containsMouse && !popup.opened
    }

    C.Popup {
        id: popup
        parent: C.Overlay.overlay
        width: Math.max(root.width, 130)
        height: Math.min(list.contentHeight + 12 + 46, 340)
        z: outsideCatcher.z + 1
        padding: 6
        focus: true
        closePolicy: C.Popup.CloseOnEscape

        // Position under the field in overlay coordinates each time it opens;
        // clamp to the window and flip above the field when out of room below.
        onAboutToShow: {
            customField.text = ""
            var overlay = C.Overlay.overlay
            var p = root.mapToItem(overlay, 0, root.height + 6)
            x = Math.max(0, Math.min(p.x, overlay.width - width))
            if (p.y + height > overlay.height) {
                var above = root.mapToItem(overlay, 0, 0).y - height - 6
                y = above >= 0 ? above : Math.max(0, overlay.height - height)
            } else {
                y = p.y
            }
        }

        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animFast } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animFast } }

        background: Rectangle {
            radius: Theme.radiusM
            color: Theme.surfaceHi
            border.width: 1
            border.color: Theme.divider
        }

        contentItem: Column {
            spacing: 6

            ListView {
                id: list
                width: parent.width
                height: popup.height - 12 - 46
                clip: true
                model: root.values
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    width: ListView.view.width
                    height: 34
                    radius: Theme.radiusS
                    color: dMouse.containsMouse ? Theme.tertiary : "transparent"
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData + root.suffix
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontM
                    }
                    UIcon {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        visible: Number(modelData) === root.value
                        name: "checkmark"
                        size: 15
                        color: Theme.accent
                    }
                    MouseArea {
                        id: dMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onContainsMouseChanged: if (containsMouse) root.highlighted(Number(modelData))
                        onClicked: {
                            popup.close()
                            root._apply(Number(modelData))
                        }
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: Theme.divider }

            UTextField {
                id: customField
                width: parent.width
                implicitHeight: 34
                placeholder: qsTr("Custom…")
                validator: IntValidator { bottom: root.from; top: root.to }
                onAccepted: {
                    var v = parseInt(text)
                    if (!isNaN(v)) {
                        popup.close()
                        root._apply(v)
                    }
                }
            }
        }
    }
}
