import QtQuick
import Unisic

Rectangle {
    id: root
    property alias text: input.text
    property alias placeholder: placeholderText.text
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
    function selectAll() { input.selectAll() }

    implicitWidth: 260
    implicitHeight: 40
    radius: Theme.radiusM
    color: Theme.surfaceHi
    border.width: input.activeFocus ? 2 : 1
    border.color: input.activeFocus ? Theme.accent : Theme.divider
    Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 14
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
