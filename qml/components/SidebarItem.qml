import QtQuick
import Unisic

Rectangle {
    id: root
    property string icon: ""
    property string iconName: ""
    property string label: ""
    property bool active: false
    signal clicked()

    width: parent ? parent.width : 200
    height: 38
    radius: Theme.radiusS
    color: active ? Theme.tertiary
         : mouse.containsMouse ? Theme.alpha(Theme.tertiary, 0.45)
         : "transparent"
    Behavior on color { ColorAnimation { duration: Theme.animFast } }

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 11
        spacing: 10
        UIcon {
            visible: root.iconName !== ""
            name: root.iconName
            color: root.active ? Theme.textPrimary : Theme.textSecondary
            size: 17
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: root.iconName === "" && root.icon !== ""
            text: root.icon; font.pixelSize: 16
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.label
            color: root.active ? Theme.textPrimary : Theme.textSecondary
            font.pixelSize: Theme.fontM
            font.weight: root.active ? Font.DemiBold : Font.Normal
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
