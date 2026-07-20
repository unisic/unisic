import QtQuick
import QtQuick.Effects
import Unisic

// In-window preview of the capture notification: a mock desktop with an EMPTY
// card (placeholder thumbnail, no real capture) parked in the configured
// corner. Deliberately not the real thing - the actual card is a layer-shell /
// XWayland surface owned by a notification host, so previewing it means
// throwing a popup at the corner of the user's screen, away from the setup
// flow they are reading. This keeps the whole decision inside the window.
//
// The card is LIVE, not a picture: the action buttons hover like the real ones
// and clicking one adds or removes it (hiddenNotifActions), the thumbnail shows
// the same hover cue, and the image-first style reveals its actions on hover
// exactly as the shipped card does.
//
// Card size comes from App.notifCardSize() - the same C++ style->size table
// both hosts use - so only the CONTENT here is an approximation of
// NotificationPopup.qml, never the footprint.
Item {
    id: root

    property string style: "casual"
    property string position: "bottom-right"

    readonly property var cardSize: App.notifCardSize(style)
    // The mock desktop is far smaller than a real screen; shrink the card only
    // as much as the frame demands, so it stays legible instead of honest.
    readonly property real cardScale: Math.min(1.0, (width - 2 * inset) / cardSize.width)
    readonly property int inset: 14

    implicitHeight: 196

    // Same table the Notifications page edits, same ids the card renders.
    readonly property var allActions: [
        { id: "edit",   iconName: "edit",         label: qsTr("Edit") },
        { id: "copy",   iconName: "edit-copy",    label: qsTr("Copy image") },
        { id: "link",   iconName: "globe",        label: qsTr("Copy link") },
        { id: "qr",     iconName: "view-preview", label: qsTr("Show QR code") },
        { id: "folder", iconName: "folder-open",  label: qsTr("Show in folder") },
        { id: "upload", iconName: "upload-cloud", label: qsTr("Upload") },
        { id: "ocr",    iconName: "ocr",          label: qsTr("Copy text (OCR)") },
        { id: "trim",   iconName: "cut",          label: qsTr("Trim recording") },
        { id: "delete", iconName: "edit-delete",  label: qsTr("Delete") }
    ]
    readonly property var hiddenIds: App.settings.hiddenNotifActions
                                     ? App.settings.hiddenNotifActions.split(",") : []
    function isHidden(id) { return root.hiddenIds.indexOf(id) >= 0 }
    function actionById(id) {
        for (var i = 0; i < allActions.length; ++i)
            if (allActions[i].id === id)
                return allActions[i]
        return null
    }

    // A ListModel, NOT a plain JS array: reordering has to happen LIVE so the
    // neighbours move out of the way under the hand, and assigning a new array
    // re-creates every delegate - destroying the very button whose DragHandler
    // is mid-drag, which cancels the gesture. ListModel.move() keeps the
    // delegates alive and just re-lays them out.
    ListModel { id: orderModelImpl }
    readonly property alias orderModel: orderModelImpl
    property bool dragging: false

    // Saved order first (unknown/duplicate ids dropped), then anything a newer
    // build introduced - the same normalization the card itself applies.
    function orderedIds() {
        var out = []
        var seen = {}
        var req = App.settings.notificationActionOrder
                  ? App.settings.notificationActionOrder.split(",") : []
        for (var i = 0; i < req.length; ++i) {
            var id = req[i].trim()
            for (var k = 0; k < allActions.length; ++k)
                if (allActions[k].id === id && !seen[id]) { seen[id] = true; out.push(id) }
        }
        for (var j = 0; j < allActions.length; ++j)
            if (!seen[allActions[j].id]) out.push(allActions[j].id)
        return out
    }
    function rebuildOrder() {
        var ids = orderedIds()
        orderModelImpl.clear()
        for (var i = 0; i < ids.length; ++i)
            orderModelImpl.append({ actionId: ids[i] })
    }
    function persistOrder() {
        var ids = []
        for (var i = 0; i < orderModelImpl.count; ++i)
            ids.push(orderModelImpl.get(i).actionId)
        App.settings.notificationActionOrder = ids.join(",")
    }
    Component.onCompleted: rebuildOrder()
    Connections {
        target: App.settings
        // Mid-drag the model IS the truth; rebuilding from the setting we are
        // about to write would yank the delegates out from under the gesture.
        function onNotificationActionOrderChanged() {
            if (!root.dragging)
                root.rebuildOrder()
        }
    }
    function toggleAction(id) {
        var h = root.hiddenIds.slice()
        var i = h.indexOf(id)
        if (i >= 0) h.splice(i, 1)
        else h.push(id)
        App.settings.hiddenNotifActions = h.filter(function (x) { return x !== "" }).join(",")
    }
    // Styles that carry an action row at all (the pill shows only a filename).
    readonly property bool hasActions: style !== "minimal"
    // The image-first style keeps its actions hidden until the pointer is on it.
    property bool thumbHovered: false

    Rectangle {
        id: screen
        anchors.fill: parent
        radius: Theme.radiusM
        color: Theme.backgroundDeep
        border.width: 1
        border.color: Theme.divider
        clip: true

        // A hint of desktop, so the card reads as sitting ON something.
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Theme.radiusM
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(Theme.tertiary.r, Theme.tertiary.g, Theme.tertiary.b, 0.30) }
                GradientStop { position: 1.0; color: Qt.rgba(Theme.secondary.r, Theme.secondary.g, Theme.secondary.b, 0.16) }
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 8
            visible: root.position.indexOf("top") < 0
            text: qsTr("Preview")
            color: Theme.textTertiary
            font.pixelSize: Theme.fontS
        }

        Item {
            id: cardHolder
            width: root.cardSize.width * root.cardScale
            height: root.cardSize.height * root.cardScale
            x: root.position.indexOf("left") >= 0 ? root.inset
             : root.position.indexOf("right") >= 0 ? screen.width - width - root.inset
             : (screen.width - width) / 2
            y: root.position.indexOf("top") >= 0 ? root.inset
                                                 : screen.height - height - root.inset
            Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

            Item {
                id: card
                width: root.cardSize.width
                height: root.cardSize.height
                scale: root.cardScale
                transformOrigin: Item.TopLeft

                // Same recipe as the real card's background.
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusL
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.lighter(Theme.primary, 1.22) }
                        GradientStop { position: 1.0; color: Theme.primary }
                    }
                    border.width: 1
                    border.color: Theme.divider
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        shadowEnabled: true; shadowColor: Theme.shadow
                        shadowBlur: 1.0; shadowVerticalOffset: 4; shadowOpacity: 0.6
                    }
                }

                // Placeholder for the capture that would be there, with the
                // real card's hover cue over it.
                component Thumb: Rectangle {
                    id: thumb
                    property int radiusPx: Theme.radiusM
                    property bool showCue: true
                    radius: radiusPx
                    color: Theme.background
                    clip: true
                    UIcon {
                        anchors.centerIn: parent
                        name: "image"
                        size: Math.max(12, Math.min(26, Math.round(Math.min(thumb.width, thumb.height) * 0.45)))
                        color: Theme.textTertiary
                    }
                    HoverHandler {
                        id: thumbHover
                        cursorShape: Qt.PointingHandCursor
                        onHoveredChanged: root.thumbHovered = hovered
                    }
                    Rectangle {
                        anchors.fill: parent
                        visible: thumb.showCue && thumbHover.hovered
                        color: Qt.rgba(0, 0, 0, 0.32)
                        UIcon {
                            anchors.centerIn: parent
                            name: "fullscreen"
                            size: Math.min(22, Math.round(thumb.height * 0.4))
                            color: "#FFFFFF"
                        }
                    }
                }
                component FileLine: Text {
                    text: "Unisic_2026-07-20_14-32.png"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontS
                    elide: Text.ElideMiddle
                    maximumLineCount: 1
                }

                // One action button. Present ones look like the real card's;
                // removed ones stay in place as a faint outline so they can be
                // put back. Clicking either way flips it.
                component ActionBtn: Rectangle {
                    id: btn
                    property var action: null
                    property int slotIndex: 0
                    property int size: 30
                    property int iconSize: 16
                    // Distance to the next button, so a drag can be turned into
                    // a number of slots moved. gridColumns > 0 means the buttons
                    // wrap (the image-first style), and then a drag also has to
                    // count whole ROWS - otherwise dropping one line down would
                    // read as a single step sideways.
                    property int stride: size + 2
                    property int strideY: size + 2
                    property int gridColumns: 0
                    property bool reorderable: false

                    readonly property bool off: root.isHidden(action ? action.id : "")
                    width: size; height: size
                    radius: Theme.radiusS
                    z: reorderDrag.active ? 5 : 0
                    color: reorderDrag.active ? Theme.alpha(Theme.accent, 0.3)
                         : btnHover.hovered ? Theme.alpha(Theme.accent, off ? 0.14 : 0.22)
                         : off ? "transparent" : Theme.surfaceHi
                    border.width: 1
                    border.color: reorderDrag.active || btnHover.hovered ? Theme.accent
                                : off ? Theme.alpha(Theme.divider, 0.7) : Theme.divider
                    opacity: off && !btnHover.hovered && !reorderDrag.active ? 0.5 : 1.0
                    Behavior on opacity { NumberAnimation { duration: 90 } }
                    // Picked up: lifted above its neighbours while it is held.
                    // The button itself does NOT chase the pointer - it swaps
                    // slot by slot, and the Row animates every swap (see the
                    // move transition below), so the whole gesture is one glide
                    // instead of a jump on release. Chasing the pointer AND
                    // animating the layout fight each other: the compensation
                    // term steps instantly while the base x eases, which reads
                    // as a stutter at every swap.
                    scale: reorderDrag.active ? 1.15 : 1.0
                    Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutBack } }

                    UIcon {
                        anchors.centerIn: parent
                        name: btn.action ? btn.action.iconName : ""
                        size: btn.iconSize
                        color: btn.off ? Theme.textTertiary : Theme.textPrimary
                        opacity: btn.off ? 0.65 : 1.0
                    }
                    HoverHandler {
                        id: btnHover
                        cursorShape: btn.reorderable
                                     ? (reorderDrag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
                                     : Qt.PointingHandCursor
                    }
                    DragHandler {
                        id: reorderDrag
                        enabled: btn.reorderable
                        target: null            // the row/grid does the moving
                        acceptedButtons: Qt.LeftButton
                        yAxis.enabled: btn.gridColumns > 0
                        property int startIndex: -1
                        onActiveChanged: {
                            if (active) {
                                startIndex = btn.slotIndex
                                root.dragging = true
                            } else if (startIndex >= 0) {
                                // The model was already reordered live; write the
                                // result out once, when the hand lets go.
                                root.persistOrder()
                                root.dragging = false
                                startIndex = -1
                            }
                        }
                        onActiveTranslationChanged: {
                            if (!active || startIndex < 0)
                                return
                            let slots = Math.round(activeTranslation.x / Math.max(1, btn.stride))
                            if (btn.gridColumns > 0)
                                slots += btn.gridColumns
                                         * Math.round(activeTranslation.y / Math.max(1, btn.strideY))
                            const target = Math.max(0, Math.min(root.orderModel.count - 1,
                                                                startIndex + slots))
                            if (target !== btn.slotIndex)
                                root.orderModel.move(btn.slotIndex, target, 1)
                        }
                    }
                    // Only fires when the press did NOT turn into a drag.
                    TapHandler { onTapped: if (btn.action) root.toggleAction(btn.action.id) }

                    // Which icon is which. Parented to the window overlay, so it
                    // is never clipped by the preview's mock-screen frame.
                    UHoverTip {
                        anchor: btn
                        text: btn.action ? btn.action.label : ""
                        show: btnHover.hovered && !reorderDrag.active
                    }
                }

                // The real card only draws the actions a given capture can back,
                // so it never runs out of room. The preview has to show all of
                // them at once (that is what you are editing), which does not
                // fit at full button size - so the row sizes its buttons to the
                // width it was given instead of overflowing the card.
                component ActionRow: Row {
                    id: arow
                    property int avail: 200
                    property int maxBtn: 30
                    readonly property int count: Math.max(1, root.orderModel.count)
                    readonly property int btnSize:
                        Math.max(14, Math.min(maxBtn,
                                 Math.floor((avail - (count - 1) * spacing) / count)))
                    spacing: 2
                    // Every neighbour slides out of the way as the held button
                    // passes it, instead of the row snapping to a new order.
                    move: Transition {
                        NumberAnimation { properties: "x"; duration: 150; easing.type: Easing.OutCubic }
                    }
                    Repeater {
                        model: root.orderModel
                        delegate: ActionBtn {
                            required property string actionId
                            required property int index
                            action: root.actionById(actionId)
                            slotIndex: index
                            size: arow.btnSize
                            iconSize: Math.max(9, Math.round(arow.btnSize * 0.55))
                            stride: arow.btnSize + arow.spacing
                            reorderable: true
                        }
                    }
                }

                // ---- casual: the full card ----
                Row {
                    visible: root.style === "casual"
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10
                    Thumb { width: 122; height: parent.height }
                    Column {
                        width: parent.width - 132
                        height: parent.height
                        spacing: 5
                        Text {
                            width: parent.width
                            text: qsTr("Capture ready")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontM
                            font.weight: Font.Bold
                        }
                        FileLine { width: parent.width }
                        ActionRow { avail: parent.width; maxBtn: 28 }
                    }
                }

                // ---- compact ----
                Item {
                    visible: root.style === "compact"
                    anchors.fill: parent
                    anchors.margins: 8
                    Thumb { id: compactThumb; width: 76; height: parent.height }
                    Column {
                        anchors.left: compactThumb.right
                        anchors.leftMargin: 8
                        anchors.right: parent.right
                        anchors.rightMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        FileLine { width: parent.width }
                        ActionRow { avail: parent.width; maxBtn: 26 }
                    }
                }

                // ---- small: one slim row ----
                Item {
                    visible: root.style === "small"
                    anchors.fill: parent
                    anchors.margins: 8
                    Thumb {
                        id: smallThumb
                        width: 36; height: 36
                        radiusPx: 6
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ActionRow {
                        id: smallActions
                        // Shares the slim row with the filename, so it may claim
                        // at most half the width left beside the thumbnail.
                        avail: Math.round((parent.width - smallThumb.width - 28) * 0.55)
                        maxBtn: 22
                        spacing: 1
                        anchors.right: parent.right
                        anchors.rightMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    FileLine {
                        anchors.left: smallThumb.right
                        anchors.leftMargin: 8
                        anchors.right: smallActions.left
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                // ---- minimal: filename pill, no actions at all ----
                Item {
                    visible: root.style === "minimal"
                    anchors.fill: parent
                    anchors.margins: 8
                    FileLine {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                // ---- thumbnail: image first, actions on hover ----
                Item {
                    visible: root.style === "thumbnail"
                    anchors.fill: parent
                    anchors.margins: 6
                    Thumb { anchors.fill: parent; showCue: false }
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusM
                        color: Qt.rgba(0, 0, 0, 0.55)
                        opacity: root.thumbHovered || hoverActions.hovered || root.dragging ? 1 : 0
                        visible: opacity > 0
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                        HoverHandler { id: hoverActions }
                        Grid {
                            id: thumbGrid
                            anchors.centerIn: parent
                            columns: 5
                            spacing: 3
                            move: Transition {
                                NumberAnimation { properties: "x,y"; duration: 150; easing.type: Easing.OutCubic }
                            }
                            Repeater {
                                model: root.orderModel
                                delegate: ActionBtn {
                                    required property string actionId
                                    required property int index
                                    action: root.actionById(actionId)
                                    slotIndex: index
                                    size: 26; iconSize: 14
                                    // Read off the grid itself, so the slot
                                    // maths cannot drift from the layout.
                                    stride: 26 + thumbGrid.spacing
                                    strideY: 26 + thumbGrid.spacing
                                    gridColumns: thumbGrid.columns
                                    reorderable: true
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
