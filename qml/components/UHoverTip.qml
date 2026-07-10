import QtQuick
import QtQuick.Controls
import Unisic

// Hover tooltip as a Popup: it renders on the window's overlay layer, so it
// is never clipped by toolbars or the custom title bar, and it flips BELOW
// its anchor when the window's top edge is too close — with native
// decorations nothing can draw outside the window, so an above-the-anchor
// tip on the top toolbar used to vanish under the decoration.
Popup {
    id: tip

    required property Item anchor
    property string text: ""
    property bool show: false

    visible: show && text !== ""
    parent: anchor
    x: (anchor.width - width) / 2
    y: anchor.mapToItem(null, 0, 0).y > height + 10 ? -(height + 8)
                                                    : anchor.height + 8
    padding: 0
    margins: 4 // keep inside the window when the anchor sits at an edge
    closePolicy: Popup.NoAutoClose

    background: null
    contentItem: Rectangle {
        implicitWidth: tipLabel.implicitWidth + 16
        implicitHeight: 24
        radius: 12
        color: Theme.tooltipBg
        Text {
            id: tipLabel
            anchors.centerIn: parent
            text: tip.text
            color: Theme.isDark ? "#FFFFFF" : Theme.textOnAccent
            font.pixelSize: 11
        }
    }
}
