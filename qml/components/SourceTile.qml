import QtQuick
import Unisic.Kit
import "Hotkeys.js" as Hotkeys

// A big source tile of the Capture and Record pages: the drawing (TileArt),
// the title, a subtitle and the hotkey. Hover feedback is colour only
// (surface, border): nothing on the page shifts under the pointer. A tile that
// does not apply right now disables and stays, it never hides.
Rectangle {
    id: tile

    property Component art
    property string title
    property string sub
    property string hotkey
    // False for a source no hotkey action exists for: no "set one" link,
    // since there would be nothing to set.
    property bool bindable: true
    property bool compact: false
    // Lines held open for the subtitle. Two in a narrow compact tile, so the
    // tiles in a row line up; a wide tile whose subtitle fits on one line
    // (the Edit page's two) asks for one.
    property int subLines: compact ? 2 : 1
    // False drops the hotkey line for a page where no tile can have one (the
    // Edit page), instead of an empty 22 px band under every subtitle.
    property bool hotkeyRow: true
    property string accessibleDescription: ""
    signal activated()
    signal openHotkeys()

    // One entry point for pointer, keyboard and assistive tech, so the three
    // can never drift apart.
    function activate() { if (tile.enabled) tile.activated() }

    FontMetrics { id: subMetrics; font.pixelSize: 13 }

    height: tileCol.height + 10 + 14
    radius: 12
    opacity: enabled ? 1.0 : 0.5
    color: cardMouse.containsMouse ? Theme.surfaceTop : Theme.surface
    border.width: 1
    border.color: Theme.alpha(Theme.accent, cardMouse.containsMouse ? 0.32 : 0.10)
    Behavior on color { ColorAnimation { duration: Theme.animFast } }
    Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

    MouseArea {
        id: cardMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: tile.activate()
    }

    // Every label inside is folded into the tile's own Accessible.name below,
    // so each Text opts out and the button announces itself exactly once.
    Column {
        id: tileCol
        x: 10
        y: 10
        width: parent.width - 20
        spacing: tile.compact ? 10 : 14

        Rectangle {
            width: parent.width
            height: tile.compact ? Math.min(132, Math.round(width * 132 / 220)) : 132
            radius: 8
            color: Theme.background
            clip: true
            Loader {
                sourceComponent: tile.art
                readonly property real k: Math.min(parent.width / 220, parent.height / 132, 1)
                scale: k
                transformOrigin: Item.TopLeft
                x: (parent.width - 220 * k) / 2
                y: (parent.height - 132 * k) / 2
            }
        }
        Column {
            width: parent.width
            leftPadding: 4
            spacing: 2
            Text {
                width: parent.width - 8
                elide: Text.ElideRight
                text: tile.title
                color: Theme.textPrimary
                font.pixelSize: 14
                font.weight: Font.DemiBold
                Accessible.ignored: true
            }
            Text {
                width: parent.width - 8
                // Two lines held open in a narrow tile, so the tiles in the
                // row line up.
                // undefined resets to the implicit height; binding implicitHeight
                // itself loops, since an eliding Text re-lays out on height.
                height: tile.subLines > 1 ? Math.ceil(tile.subLines * subMetrics.lineSpacing) : undefined
                wrapMode: tile.subLines > 1 ? Text.Wrap : Text.NoWrap
                maximumLineCount: tile.subLines
                elide: Text.ElideRight
                text: tile.sub
                color: Theme.textSecondary
                font.pixelSize: 13
                Accessible.ignored: true
            }
        }
        // Fixed height whether or not a hotkey is bound, so the tiles in a row
        // always line up.
        Item {
            width: parent.width
            height: 22
            visible: tile.hotkeyRow
            Row {
                x: 4
                spacing: 4
                Repeater {
                    model: Hotkeys.keysOf(tile.hotkey)
                    delegate: Kbd {
                        required property string modelData
                        text: modelData
                    }
                }
            }
            Row {
                x: 4
                width: parent.width - 8
                anchors.verticalCenter: parent.verticalCenter
                visible: tile.bindable && tile.hotkey === ""
                spacing: 4
                // Context kept as CapturePage: these strings moved out of that
                // file together with their translations.
                // The lead-in elides, the link never does: it is the action.
                Text {
                    width: Math.max(0, Math.min(implicitWidth, parent.width - parent.spacing - setLink.implicitWidth))
                    elide: Text.ElideRight
                    text: qsTranslate("CapturePage", "No hotkey yet,")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontS
                    Accessible.ignored: true
                }
                Link {
                    id: setLink
                    text: qsTranslate("CapturePage", "set one")
                    Accessible.description: qsTranslate("CapturePage", "Open Settings at Hotkeys")
                    onActivated: tile.openHotkeys()
                }
            }
        }
    }

    // UKeys keeps the window's Ctrl+1..7 / Ctrl+V bubbling past a focused tile.
    activeFocusOnTab: enabled
    Keys.onSpacePressed: (e) => UKeys.activate(e, tile.activate)
    Keys.onReturnPressed: (e) => UKeys.activate(e, tile.activate)
    Keys.onEnterPressed: (e) => UKeys.activate(e, tile.activate)

    Accessible.role: Accessible.Button
    // The hotkey is part of the identity: it is the only place the shortcut
    // for this mode is shown.
    //: Spoken name of a capture tile: title, subtitle, hotkey.
    Accessible.name: hotkey !== ""
                     ? qsTranslate("CapturePage", "%1, %2, shortcut %3").arg(title).arg(sub).arg(Hotkeys.first(hotkey))
                     : qsTranslate("CapturePage", "%1, %2").arg(title).arg(sub)
    Accessible.description: accessibleDescription
    Accessible.focusable: tile.activeFocusOnTab
    Accessible.onPressAction: tile.activate()
    UFocusRing { inset: -4 }
}
