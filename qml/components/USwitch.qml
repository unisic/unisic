import QtQuick
import QtQuick.Effects
import Unisic

// iOS-style toggle.
Rectangle {
    id: root
    property bool checked: false
    signal toggled(bool checked)

    width: 50; height: 30
    radius: height / 2
    color: checked ? Theme.accent : Theme.surfaceHi
    border.width: 1
    border.color: checked ? Theme.accent : Theme.divider
    Behavior on color { ColorAnimation { duration: Theme.animMed } }

    Rectangle {
        width: 24; height: 24
        radius: 12
        y: 3
        x: root.checked ? root.width - width - 3 : 3
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.thumbTop }
            GradientStop { position: 1.0; color: Theme.thumbBottom }
        }
        Behavior on x { NumberAnimation { duration: Theme.animMed; easing.type: Easing.OutBack; easing.overshoot: 1.2 } }
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
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        // Emit only — writing `checked` here would destroy the consumer's
        // declarative binding (checked: App.settings.x) on first interaction;
        // the handler updates the source and the binding flows back.
        onClicked: root.toggled(!root.checked)
    }
}
