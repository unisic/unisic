import QtQuick
import QtQuick.Controls as C
import Unisic

// Dropdown whose list is a Popup parented to the window Overlay, so it renders
// above every card/Flickable and is never clipped by clip:true.
Rectangle {
    id: root
    property var model: []            // array of strings
    property int currentIndex: 0
    readonly property string currentText: model && model.length > currentIndex && currentIndex >= 0
                                          ? String(model[currentIndex]) : ""
    signal activated(int index)

    implicitWidth: 220
    implicitHeight: 40
    radius: Theme.radiusM
    color: mouse.containsMouse ? Theme.tertiary : Theme.surfaceHi
    border.width: 1
    border.color: popup.opened ? Theme.accent : Theme.divider
    Behavior on color { ColorAnimation { duration: Theme.animFast } }

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
        }
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: chevron.left
        text: root.currentText
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

    C.Popup {
        id: popup
        parent: C.Overlay.overlay
        width: root.width
        height: Math.min(list.contentHeight + 12, 320)
        z: outsideCatcher.z + 1
        padding: 6
        closePolicy: C.Popup.CloseOnEscape

        // Position under the field in overlay coordinates each time it opens;
        // clamp to the window and flip above the field when out of room below.
        onAboutToShow: {
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

        contentItem: ListView {
            id: list
            clip: true
            implicitHeight: contentHeight
            model: root.model
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
                    text: String(modelData)
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontM
                }
                UIcon {
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    visible: index === root.currentIndex
                    name: "checkmark"
                    size: 15
                    color: Theme.accent
                }
                MouseArea {
                    id: dMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    // Emit only — writing currentIndex here would destroy the
                    // consumer's binding; the handler updates the source.
                    onClicked: {
                        popup.close()
                        root.activated(index)
                    }
                }
            }
        }
    }
}
