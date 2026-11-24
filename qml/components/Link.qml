import QtQuick
import Unisic.Kit

// A text link: accent, underline-free, keyboard and screen-reader reachable.
Text {
    id: link
    signal activated()
    color: linkMouse.containsMouse ? Qt.lighter(Theme.accent, 1.12) : Theme.accent
    font.pixelSize: Theme.fontS
    font.weight: Font.Medium
    MouseArea {
        id: linkMouse
        anchors.fill: parent
        anchors.margins: -4
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: link.activated()
    }
    activeFocusOnTab: true
    Keys.onSpacePressed: (e) => UKeys.activate(e, link.activated)
    Keys.onReturnPressed: (e) => UKeys.activate(e, link.activated)
    Keys.onEnterPressed: (e) => UKeys.activate(e, link.activated)
    Accessible.role: Accessible.Link
    Accessible.name: text
    Accessible.focusable: true
    Accessible.onPressAction: link.activated()
    UFocusRing { inset: -3 }
}
