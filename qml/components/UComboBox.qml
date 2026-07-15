import QtQuick
import QtQuick.Controls as C
import Unisic

// Dropdown whose list is a Popup parented to the window Overlay, so it renders
// above every card/Flickable and is never clipped by clip:true.
Rectangle {
    id: root
    property var model: []            // array of strings
    property int currentIndex: 0
    // Long lists (fonts): a filter field pinned at the top of the popup.
    property bool searchable: false
    // Render each entry in its own family (font pickers).
    property bool fontPreview: false
    readonly property string currentText: model && model.length > currentIndex && currentIndex >= 0
                                          ? String(model[currentIndex]) : ""
    signal activated(int index)
    // Live-preview hooks. `highlighted` fires for the entry under the pointer
    // WITHOUT committing it, so a consumer can show what picking it would do;
    // `listOpen` says the user is mid-choice, which is when such a preview is
    // wanted and when a plain hover of the field is not.
    signal highlighted(int index)
    readonly property alias listOpen: popup.opened

    property string _filter: ""
    // Entries carry their SOURCE index so filtering never breaks activation.
    readonly property var _entries: {
        var out = []
        for (var i = 0; model && i < model.length; ++i) {
            var s = String(model[i])
            if (_filter === "" || s.toLowerCase().indexOf(_filter) >= 0)
                out.push({ text: s, idx: i })
        }
        return out
    }

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
            onWheel: (w) => { popup.close(); w.accepted = false }
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
        // Fonts get a wider popup than the compact closed field.
        width: Math.max(root.width, root.searchable ? 240 : 0)
        readonly property int searchH: root.searchable ? 40 : 0
        height: Math.min(list.contentHeight + 12 + searchH, 340)
        z: outsideCatcher.z + 1
        padding: 6
        focus: true
        closePolicy: C.Popup.CloseOnEscape

        // Position under the field in overlay coordinates each time it opens;
        // clamp to the window and flip above the field when out of room below.
        onAboutToShow: {
            root._filter = ""
            searchField.text = ""
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
        onOpened: if (root.searchable) searchField.forceFocus()

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

            UTextField {
                id: searchField
                visible: root.searchable
                width: parent.width
                implicitHeight: 34
                placeholder: qsTr("Search…")
                onEdited: (t) => root._filter = t.toLowerCase()
                // Enter picks the first (best) match.
                onAccepted: {
                    if (root._entries.length > 0) {
                        popup.close()
                        root.activated(root._entries[0].idx)
                    }
                }
            }

            ListView {
                id: list
                width: parent.width
                height: popup.height - 12 - popup.searchH
                clip: true
                model: root._entries
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    width: ListView.view.width
                    height: 34
                    radius: Theme.radiusS
                    color: dMouse.containsMouse ? Theme.tertiary : "transparent"
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.right: parent.right
                        anchors.rightMargin: 30
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.text
                        elide: Text.ElideRight
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontM
                        font.family: root.fontPreview ? modelData.text : Qt.application.font.family
                    }
                    UIcon {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        visible: modelData.idx === root.currentIndex
                        name: "checkmark"
                        size: 15
                        color: Theme.accent
                    }
                    MouseArea {
                        id: dMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onContainsMouseChanged: if (containsMouse) root.highlighted(modelData.idx)
                        // Emit only — writing currentIndex here would destroy the
                        // consumer's binding; the handler updates the source.
                        onClicked: {
                            popup.close()
                            root.activated(modelData.idx)
                        }
                    }
                }
            }
        }
    }
}
