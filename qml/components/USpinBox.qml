import QtQuick
import Unisic

Rectangle {
    id: root
    property int value: 0
    property int from: 0
    property int to: 100
    property string suffix: ""
    // Per-click increment. Ranges like 0-5000 ms are unreachable one unit at
    // a time — consumers set e.g. step: 50.
    property int step: 1
    signal changed(int value)

    implicitWidth: 130
    implicitHeight: 40
    radius: Theme.radiusM
    color: Theme.surfaceHi
    border.width: 1
    border.color: Theme.divider

    function _set(v) {
        v = Math.max(from, Math.min(to, v))
        // Emit only — assigning `value` would break the consumer's binding.
        if (v !== value) root.changed(v)
    }

    // Hold-to-repeat with acceleration: after the initial delay, repeat the
    // step and grow it (up to 10x) so long ranges are traversable by holding.
    Timer {
        id: repeatTimer
        property int dir: 0
        property int boost: 1
        // Once the hold-repeat has stepped, the click emitted on RELEASE must
        // be swallowed or every hold lands one phantom step past the value
        // the user watched. Reset on the next press.
        property bool fired: false
        interval: 350
        repeat: true
        running: minusBtn.pressed || plusBtn.pressed
        onRunningChanged: {
            if (running) {
                dir = plusBtn.pressed ? 1 : -1
                boost = 1
                interval = 350
                fired = false
            }
        }
        onTriggered: {
            interval = 60
            fired = true
            root._set(root.value + dir * root.step * boost)
            if (boost < 10) boost++
        }
    }

    UIconButton {
        id: minusBtn
        icon: "−"
        width: 32; height: 32
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        onClicked: if (!repeatTimer.fired) root._set(root.value - root.step)
    }
    Text {
        anchors.centerIn: parent
        text: root.value + root.suffix
        color: Theme.textPrimary
        font.pixelSize: Theme.fontM
        font.weight: Font.DemiBold
    }
    UIconButton {
        id: plusBtn
        icon: "+"
        width: 32; height: 32
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        onClicked: if (!repeatTimer.fired) root._set(root.value + root.step)
    }
}
