import QtQuick
import Unisic

// A seamless split pill: two icon halves sharing one rounded background with a
// center divider, so a pair of RELATED actions reads as one compact control.
// In history tiles the LEFT copies the capture itself and the RIGHT copies its
// shared URL — instead of two separate buttons taking twice the room.
Rectangle {
    id: root

    property string leftIcon: ""
    property string rightIcon: ""
    property string leftTip: ""
    property string rightTip: ""
    property bool leftEnabled: true
    property bool rightEnabled: true
    property int cell: 34
    property int iconSize: 16
    signal leftClicked()
    signal rightClicked()

    width: cell * 2 + 1
    height: cell
    radius: Theme.radiusM
    color: Qt.rgba(1, 1, 1, 0.10) // subtle unifying pill on the dark hover scrim

    // LEFT half — copies the capture (image → clipboard image; recording → path).
    Rectangle {
        id: leftHalf
        width: root.cell; height: root.height
        topLeftRadius: root.radius; bottomLeftRadius: root.radius
        opacity: root.leftEnabled ? 1 : 0.35
        color: lm.containsMouse && root.leftEnabled ? Theme.alpha(Theme.accent, 0.28) : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
        UIcon {
            anchors.centerIn: parent
            name: root.leftIcon; size: root.iconSize; color: Theme.textPrimary
        }
        MouseArea {
            id: lm
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: root.leftEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (root.leftEnabled) root.leftClicked()
        }
        UHoverTip { anchor: leftHalf; text: root.leftTip; show: lm.containsMouse }
    }

    Rectangle {
        x: root.cell; width: 1; height: root.height - 12
        anchors.verticalCenter: parent.verticalCenter
        color: Qt.rgba(1, 1, 1, 0.22)
    }

    // RIGHT half — copies the shared URL (disabled until the capture is uploaded).
    Rectangle {
        id: rightHalf
        x: root.cell + 1; width: root.cell; height: root.height
        topRightRadius: root.radius; bottomRightRadius: root.radius
        opacity: root.rightEnabled ? 1 : 0.35
        color: rm.containsMouse && root.rightEnabled ? Theme.alpha(Theme.accent, 0.28) : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
        UIcon {
            anchors.centerIn: parent
            name: root.rightIcon; size: root.iconSize; color: Theme.textPrimary
        }
        MouseArea {
            id: rm
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: root.rightEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (root.rightEnabled) root.rightClicked()
        }
        UHoverTip { anchor: rightHalf; text: root.rightTip; show: rm.containsMouse }
    }
}
