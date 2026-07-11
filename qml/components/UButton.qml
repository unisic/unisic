import QtQuick
import QtQuick.Effects
import Unisic

// SwiftUI-style button: pill/rounded, springy press scale, gradient fill,
// soft shadow on prominent variants.
Rectangle {
    id: root

    property string text: ""
    property string icon: ""            // legacy emoji/text glyph (fallback)
    property string iconName: ""        // themed icon name (preferred)
    property string variant: "filled"   // filled | tonal | ghost | danger
    property bool compact: false
    signal clicked()

    readonly property color _fg: variant === "filled" ? Theme.textOnAccent
                               : variant === "danger" ? Theme.dangerText
                               : Theme.textPrimary

    readonly property bool _hovered: mouse.containsMouse && !mouse.pressed
    readonly property color _base: variant === "danger" ? Theme.danger
                                  : variant === "tonal" ? Theme.tertiary
                                  : Theme.accent
    readonly property color _bg: variant === "ghost"
                                 ? (_hovered ? Theme.alpha(Theme.accent, 0.14) : "transparent")
                                 : (_hovered ? Qt.lighter(_base, 1.12) : _base)

    implicitWidth: row.implicitWidth + (compact ? 28 : 40)
    implicitHeight: compact ? 34 : 42
    radius: height / 2
    opacity: root.enabled ? 1.0 : 0.4

    color: _bg
    border.width: variant === "ghost" || variant === "tonal" ? 1 : 0
    border.color: Theme.divider

    layer.enabled: variant === "filled" || variant === "danger"
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: root.variant === "filled" ? Theme.alpha(Theme.accent, 0.45) : Theme.shadow
        shadowBlur: 0.7
        shadowVerticalOffset: 3
        shadowOpacity: 0.5
    }

    scale: mouse.pressed && root.enabled ? 0.96 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutBack } }
    Behavior on color { ColorAnimation { duration: Theme.animFast } }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7
        UIcon {
            visible: root.iconName !== ""
            name: root.iconName
            color: root._fg
            size: root.compact ? 16 : 18
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: root.iconName === "" && root.icon !== ""
            text: root.icon
            font.pixelSize: compact ? Theme.fontM : Theme.fontL
            anchors.verticalCenter: parent.verticalCenter
            color: root._fg
        }
        Text {
            visible: root.text !== ""
            text: root.text
            font.pixelSize: compact ? Theme.fontS + 1 : Theme.fontM
            font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
            color: root._fg
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: if (root.enabled) root.clicked()
    }
}
