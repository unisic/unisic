import QtQuick
import Unisic.Kit

// A row of equal segments sunk into the window colour, one of them on: the
// Capture page's delay presets, the Record page's Video/GIF and its frame
// rates. `currentIndex` -1 lights none (a value set elsewhere that no segment
// names). The segments only report the pick; the page owns the setting.
//
// capturePageCheck() (F8) reads the segments as the children of this item's
// first child and their `pressed`: keep that shape.
Rectangle {
    id: root

    property var labels: []
    // Optional, one per segment: what picking it does, for a screen reader.
    property var descriptions: []
    property int currentIndex: -1
    property string accessibleName: ""
    signal picked(int index)

    // A theme whose window colour equals its card colour (the built-in light
    // one) would lose the well, so that one takes the background instead.
    readonly property color inset: Qt.colorEqual(Theme.primary, Theme.surface) ? Theme.background : Theme.primary

    height: 28 + 6 + 2
    radius: 9
    color: inset
    border.width: 1
    border.color: Theme.alpha(Theme.accent, 0.10)
    Accessible.role: Accessible.Grouping
    Accessible.name: accessibleName

    Row {
        x: 4; y: 4
        spacing: 2
        Repeater {
            model: root.labels
            delegate: Rectangle {
                id: seg
                required property int index
                required property string modelData
                readonly property bool pressed: index === root.currentIndex
                function activate() { root.picked(index) }
                width: (root.width - 8 - (root.labels.length - 1) * 2) / root.labels.length
                height: 28
                radius: 6
                color: pressed ? Theme.secondary
                     : segMouse.containsMouse ? Theme.surfaceTop : "transparent"
                Behavior on color { ColorAnimation { duration: Theme.animFast } }
                Text {
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, parent.width - 8)
                    elide: Text.ElideRight
                    text: seg.modelData
                    color: seg.pressed ? Theme.textPrimary : Theme.textSecondary
                    font.pixelSize: Theme.fontS
                    font.weight: seg.pressed ? Font.DemiBold : Font.Normal
                    Accessible.ignored: true
                }
                MouseArea {
                    id: segMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: seg.activate()
                }
                activeFocusOnTab: root.enabled
                Keys.onSpacePressed: (e) => UKeys.activate(e, seg.activate)
                Keys.onReturnPressed: (e) => UKeys.activate(e, seg.activate)
                Keys.onEnterPressed: (e) => UKeys.activate(e, seg.activate)
                Accessible.role: Accessible.Button
                Accessible.name: modelData
                Accessible.description: root.descriptions[index] || ""
                Accessible.checkable: true
                Accessible.checked: pressed
                Accessible.focusable: activeFocusOnTab
                Accessible.onPressAction: seg.activate()
                UFocusRing { inset: -1 }
            }
        }
    }
}
