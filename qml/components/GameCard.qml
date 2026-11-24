import QtQuick
import QtQuick.Effects
import QtQuick.Window
import Unisic
import Unisic.Kit

// One game in "Recently played": its picture with the name over a shade at
// the bottom. The picture is Steam's art when the detector found it, else the
// newest clip of the game, else the name's first letter. Checked = the Clips
// page is narrowed to that game. Colour-only hover, like every tile.
Item {
    id: card

    property string name
    property string cover        // local path to the game's art, "" for none
    property string fallback     // local path to a clip thumbnail, "" for none
    property bool checked: false
    property string accessibleDescription: ""
    signal activated()

    function activate() { card.activated() }

    // Steam's header art is 460×215.
    implicitHeight: Math.round(width * 215 / 460)

    // A cover Steam has since dropped from its cache fails to load once, and
    // the clip thumbnail takes its place.
    property bool coverFailed: false
    onCoverChanged: coverFailed = false
    readonly property string picture: cover !== "" && !coverFailed ? cover : fallback

    Rectangle {
        id: frame
        anchors.fill: parent
        radius: 10
        clip: true
        // No picture: a tint of the palette behind the name's first letter.
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.alpha(Theme.accent, 0.38) }
            GradientStop { position: 1.0; color: Theme.alpha(Theme.tertiary, 0.55) }
        }

        Text {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -nameText.height / 2
            visible: art.status !== Image.Ready
            text: card.name.charAt(0).toUpperCase()
            color: Theme.alpha(Theme.textPrimary, 0.55)
            font.pixelSize: Math.round(card.height * 0.42)
            font.weight: Font.DemiBold
            Accessible.ignored: true
        }

        Image {
            id: art
            anchors.fill: parent
            source: card.picture !== "" ? App.fileDragUri(card.picture) : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
            sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
            onStatusChanged: if (status === Image.Error && card.picture === card.cover) card.coverFailed = true
            layer.enabled: true
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: mask
            }
        }
        Item {
            id: mask
            anchors.fill: parent
            visible: false
            layer.enabled: true
            Rectangle { anchors.fill: parent; radius: frame.radius }
        }

        // The shade the name sits on: legible over any picture.
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: nameText.height + 14
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 1.0; color: Theme.alpha(Theme.recBadgeBg, 0.88) }
            }
        }
        Text {
            id: nameText
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 8
            text: card.name
            elide: Text.ElideRight
            color: Theme.recBadgeText
            font.pixelSize: Theme.fontS
            font.weight: Font.DemiBold
            Accessible.ignored: true
        }
    }

    // Border over the picture: the checked game in the accent, hover lighter.
    Rectangle {
        anchors.fill: parent
        radius: frame.radius
        color: mouse.containsMouse && !card.checked ? Theme.alpha(Theme.textPrimary, 0.06) : "transparent"
        border.width: card.checked ? 2 : 1
        border.color: card.checked ? Theme.accent
                                   : Theme.alpha(Theme.accent, mouse.containsMouse ? 0.40 : 0.14)
        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: card.activate()
    }

    activeFocusOnTab: true
    Keys.onSpacePressed: (e) => UKeys.activate(e, card.activate)
    Keys.onReturnPressed: (e) => UKeys.activate(e, card.activate)
    Keys.onEnterPressed: (e) => UKeys.activate(e, card.activate)

    Accessible.role: Accessible.CheckBox
    Accessible.name: name
    Accessible.description: accessibleDescription
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.focusable: true
    Accessible.onPressAction: card.activate()
    Accessible.onToggleAction: card.activate()
    UFocusRing { inset: -4 }
}
