import QtQuick
import Unisic

Rectangle {
    id: root
    property alias text: input.text
    property alias placeholder: placeholderText.text
    // Optional leading icon (e.g. "magnify" for search fields); shifts the
    // input right when set, no-op when empty so existing fields keep their
    // layout.
    property string iconName: ""
    property alias readOnly: input.readOnly
    property alias echoMode: input.echoMode
    property alias validator: input.validator
    // The root Rectangle is not a FocusScope, so its own activeFocus stays
    // false; expose the inner TextInput's focus so callers can tell if the
    // user is editing (e.g. UColorPopup's hex-field guard/blur-commit).
    readonly property alias inputActiveFocus: input.activeFocus
    signal edited(string text)
    signal accepted()

    function forceFocus() { input.forceActiveFocus() }

    implicitWidth: 260
    implicitHeight: 40
    radius: Theme.radiusM
    color: Theme.surfaceHi
    border.width: input.activeFocus ? 2 : 1
    border.color: input.activeFocus ? Theme.accent : Theme.divider
    Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

    UIcon {
        visible: root.iconName !== ""
        name: root.iconName
        size: 15
        color: Theme.textTertiary
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
    }

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: root.iconName !== "" ? 34 : 14
        anchors.rightMargin: 14
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.textPrimary
        font.pixelSize: Theme.fontM
        clip: true
        selectionColor: Theme.tertiary
        onTextEdited: root.edited(text)
        onAccepted: root.accepted()
    }
    Text {
        id: placeholderText
        anchors.fill: input
        verticalAlignment: Text.AlignVCenter
        color: Theme.textTertiary
        font.pixelSize: Theme.fontM
        visible: input.text.length === 0 && !input.activeFocus
        elide: Text.ElideRight
    }
}
