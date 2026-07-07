import QtQuick
import QtQuick.Effects
import Unisic

Item {
    id: root
    property real from: 0
    property real to: 100
    property real value: 0
    property real stepSize: 1
    signal moved(real value)

    implicitWidth: 220
    implicitHeight: 30

    function _ratio() { return to > from ? (value - from) / (to - from) : 0 }
    function _setFromX(x) {
        var r = Math.max(0, Math.min(1, (x - 10) / (width - 20)))
        var v = from + r * (to - from)
        v = Math.round(v / stepSize) * stepSize
        v = Math.max(from, Math.min(to, v))
        if (v !== value) { value = v; moved(v) }
    }

    Rectangle {  // track
        anchors.verticalCenter: parent.verticalCenter
        x: 10; width: parent.width - 20; height: 5
        radius: 2.5
        color: Theme.surfaceHi
        Rectangle {
            width: parent.width * root._ratio()
            height: parent.height
            radius: 2.5
            color: Theme.accent
        }
    }

    Rectangle {  // thumb
        x: 10 + (parent.width - 20) * root._ratio() - width / 2
        anchors.verticalCenter: parent.verticalCenter
        width: 20; height: 20; radius: 10
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.thumbTop }
            GradientStop { position: 1.0; color: Theme.thumbBottom }
        }
        scale: drag.pressed ? 1.15 : 1.0
        Behavior on scale { NumberAnimation { duration: Theme.animFast } }
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.shadow
            shadowBlur: 0.5
            shadowVerticalOffset: 2
            shadowOpacity: 0.6
        }
    }

    MouseArea {
        id: drag
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onPressed: (m) => root._setFromX(m.x)
        onPositionChanged: (m) => { if (pressed) root._setFromX(m.x) }
    }
}
