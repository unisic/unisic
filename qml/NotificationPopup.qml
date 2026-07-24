import QtQuick
import QtQuick.Window
import QtQuick.Effects
import Unisic
import Unisic.Kit
import "components"

// Small in-app capture-preview popup. Shown by AppContext after a capture or
// recording; `notif` (CaptureNotification) is injected as a context property.
//
// Five styles (Settings > Appearance > Notification style); C++
// (LayerShellNotifier) sizes the surface to match:
//   casual    400x150  full card: big thumb, title, action row
//   compact   380x96   tighter card: medium thumb, filename, action row
//   small     380x52   one slim row: tiny thumb, filename, inline actions
//   minimal   300x36   pill: filename only — click previews, nothing else
//   thumbnail 240x150  image-first: full-bleed thumb, actions on hover
Window {
    id: popup
    // Card-sized window placed on the wlr-layer-shell OVERLAY layer by C++
    // (LayerShellNotifier) — the layer surface handles stacking (always on top)
    // and focus, so no window-manager flags are needed beyond frameless.
    flags: Qt.FramelessWindowHint
    color: "transparent"
    visible: false   // AppContext sizes, layers, then show()s it

    // Card values come from the HOST as context properties (popupStyle,
    // popupAutoHideSec, popupHiddenActions), never from App.settings directly:
    // the settings preview renders a card with overridden values that were
    // deliberately not saved, and both hosts feed these from one snapshot
    // (NotifCard::effectiveSettings).
    // 0 = stay open until manually closed.
    readonly property int autoHideSec: popupAutoHideSec
    // Latched ONCE at creation, not live-bound: C++ sized the surface and the
    // input mask for this style, so an open card switching layout mid-flight
    // would overflow/clip the fixed window. New cards pick up the changed
    // setting because the host re-reads it per show().
    property string style: "casual"
    // True while a thumbnail is being dragged out. A native drag can take many
    // seconds (aiming at another app), and the pointer leaves the card the moment
    // it starts — without this the 8s auto-hide would fire mid-drag and destroy
    // the drag's origin surface. Pauses auto-hide + the countdown drain.
    property bool dragging: false
    // A tiny exit grace avoids pause/resume flicker when the pointer only grazes
    // the card edge. It is state, rather than a continuously-running timer, so
    // an indefinitely hovered card still costs nothing while paused.
    property bool hoverGraceActive: false
    readonly property real autoHideDurationMs: Math.max(0, autoHideSec) * 1000
    property real autoHideRemainingMs: autoHideDurationMs
    property real autoHideStartedAtMs: 0
    readonly property var allActionIds: ["edit", "copy", "link", "qr", "folder",
                                         "upload", "ocr", "trim", "delete"]
    property var orderedActions: allActionIds
    readonly property bool autoHidePaused: autoHideSec > 0
                                           && (hover.hovered || hoverGraceActive || dragging)

    // Filter unknown/duplicate ids from imported or hand-edited settings, then
    // append actions introduced by a newer build. An old config can therefore
    // customize order without making future buttons disappear forever.
    function normalizedActionOrder(csv) {
        const requested = csv ? csv.split(",") : []
        const known = {}
        const out = []
        for (let i = 0; i < allActionIds.length; ++i)
            known[allActionIds[i]] = true
        for (let j = 0; j < requested.length; ++j) {
            const id = requested[j]
            if (known[id] && out.indexOf(id) < 0)
                out.push(id)
        }
        for (let k = 0; k < allActionIds.length; ++k)
            if (out.indexOf(allActionIds[k]) < 0)
                out.push(allActionIds[k])
        return out
    }

    function currentAutoHideRemaining() {
        if (!dismissTimer.running)
            return autoHideRemainingMs
        return Math.max(0, autoHideRemainingMs - (Date.now() - autoHideStartedAtMs))
    }

    function syncAutoHide() {
        const shouldRun = autoHideSec > 0 && !hover.hovered
                          && !hoverGraceActive && !dragging
        if (shouldRun === dismissTimer.running)
            return

        if (shouldRun) {
            if (autoHideRemainingMs <= 0) {
                notif.dismiss()
                return
            }
            autoHideStartedAtMs = Date.now()
            dismissTimer.interval = Math.max(1, autoHideRemainingMs)
            dismissTimer.start()
        } else {
            autoHideRemainingMs = currentAutoHideRemaining()
            dismissTimer.stop()
            card.drain = autoHideDurationMs > 0
                         ? autoHideRemainingMs / autoHideDurationMs : 0
        }
    }

    onDraggingChanged: syncAutoHide()
    Component.onCompleted: {
        if (["casual", "compact", "small", "minimal", "thumbnail"].indexOf(popupStyle) >= 0)
            style = popupStyle
        const csv = popupHiddenActions
        hiddenActions = csv ? csv.split(",").filter(function (x) { return x.length > 0 }) : []
        orderedActions = normalizedActionOrder(popupActionOrder)
        autoHideRemainingMs = autoHideDurationMs
        syncAutoHide()
    }

    // A one-shot timer whose remaining interval is saved on hover/drag and used
    // on resume. Binding `running` directly to hover restarts a QML Timer from
    // its full interval, which made every hover grant the card a fresh lifetime.
    Timer {
        id: dismissTimer
        interval: 1
        onTriggered: {
            popup.autoHideRemainingMs = 0
            card.drain = 0
            notif.dismiss()
        }
    }

    Timer {
        id: hoverExitTimer
        interval: 140
        onTriggered: {
            popup.hoverGraceActive = false
            popup.syncAutoHide()
        }
    }

    // Short, local confirmation for clipboard actions. It intentionally needs
    // no parent/helper acknowledgement: both copy methods are synchronous and
    // only expose a button when their payload exists.
    property string confirmedAction: ""
    function confirmAction(action) {
        confirmedAction = action
        actionConfirmTimer.restart()
    }
    Timer {
        id: actionConfirmTimer
        interval: 900
        onTriggered: popup.confirmedAction = ""
    }

    // Action ids the user switched off in Settings > Notifications. Latched at
    // creation like `style`, and from the same host snapshot.
    property var hiddenActions: []
    function actionShown(id) { return popup.hiddenActions.indexOf(id) < 0 }

    function actionIcon(id) {
        if (id === "edit") return "edit"
        if (id === "copy") return confirmedAction === "copy-image" ? "checkmark" : "edit-copy"
        if (id === "link") return confirmedAction === "copy-link" ? "checkmark" : "globe"
        if (id === "qr") return "view-preview"
        if (id === "folder") return "folder-open"
        if (id === "upload") return "upload-cloud"
        if (id === "ocr") return "ocr"
        if (id === "trim") return "cut"
        if (id === "delete") return "edit-delete"
        return ""
    }
    function actionTooltip(id) {
        if (id === "edit") return qsTr("Edit")
        if (id === "copy") return qsTr("Copy image")
        if (id === "link") return qsTr("Copy link")
        if (id === "qr") return qsTr("Show QR code")
        if (id === "folder") return qsTr("Show in folder")
        if (id === "upload") return qsTr("Upload")
        if (id === "ocr") return qsTr("Copy text (OCR)")
        if (id === "trim") return qsTr("Trim recording")
        if (id === "delete") return qsTr("Delete")
        return ""
    }
    function actionAvailable(id) {
        if (!actionShown(id)) return false
        if (id === "edit" || id === "copy") return notif.kind === "image"
        if (id === "link") return notif.url !== ""
        if (id === "qr") return App.qrAvailable && notif.url !== ""
        if (id === "folder") return true
        if (id === "upload") return notif.url === "" && !notif.uploading
        if (id === "ocr") return App.ocrAvailable && notif.kind === "image"
        if (id === "trim") return notif.filePath !== ""
                                  && (notif.kind === "video" || notif.kind === "gif")
        if (id === "delete") return notif.filePath !== ""
        return false
    }
    function triggerAction(id) {
        if (id === "edit") notif.edit()
        else if (id === "copy") { notif.copyImage(); confirmAction("copy-image") }
        else if (id === "link") { notif.copyUrl(); confirmAction("copy-link") }
        else if (id === "qr") notif.showQr()
        else if (id === "folder") notif.showInFolder()
        else if (id === "upload") notif.upload()
        else if (id === "ocr") notif.ocr()
        else if (id === "trim") notif.trim()
        else if (id === "delete") notif.deleteCapture()
    }

    // The full action set, reused by every style that shows buttons. Every
    // button ANDs actionShown() over its own condition — the user's opt-out only
    // ever subtracts, it can never force a button the capture can't back.
    component ActionRow: Row {
        property int btn: 32
        property int icon: 16
        spacing: 1
        Repeater {
            model: popup.orderedActions
            delegate: UIconButton {
                required property string modelData
                iconName: popup.actionIcon(modelData)
                iconSize: parent.icon
                width: parent.btn
                height: parent.btn
                tooltip: popup.actionTooltip(modelData)
                visible: popup.actionAvailable(modelData)
                onClicked: popup.triggerAction(modelData)
            }
        }
    }

    // Thumbnail interaction, shared by every style that shows one. A plain
    // click opens the floating preview; dragging past the threshold pulls the
    // capture FILE out to another app (file manager, chat, editor) via a native
    // text/uri-list drag — the same 1x1-proxy pattern the History grid uses.
    // Idle until a real gesture starts, so it costs nothing while the card sits.
    component DragThumb: MouseArea {
        anchors.fill: parent
        // Images preview + drag; recordings (with a file) drag only.
        enabled: notif.kind === "image" || notif.filePath !== ""
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        hoverEnabled: true
        drag.target: dragProxy
        drag.threshold: 8
        property string uri: ""
        // Hover cue: the thumbnail is interactive — click = preview, drag = pull
        // the file out. Suppressed in the image-first "thumbnail" style, which
        // shows its own hover action overlay. A menu/tooltip would be clipped by
        // the tiny card surface, so the affordance lives INSIDE the thumbnail.
        Rectangle {
            anchors.fill: parent
            visible: parent.containsMouse && !parent.pressed
                     && notif.kind === "image" && popup.style !== "thumbnail"
            color: Qt.rgba(0, 0, 0, 0.32)
            UIcon {
                anchors.centerIn: parent
                name: "fullscreen"
                size: Math.min(22, Math.round(parent.height * 0.4))
                color: "#FFFFFF"
            }
        }
        // Resolve the payload once, at press: dragUri() may write a temp PNG for
        // an unsaved image, so it must not run from a reactive binding. Pausing
        // auto-hide from press (not just drag start) keeps it simple; a plain
        // click clears it again on release, long before the 8s timer matters.
        onPressed: { uri = notif.dragUri(); popup.dragging = true }
        onReleased: { dragProxy.x = 0; dragProxy.y = 0; popup.dragging = false }
        onCanceled: popup.dragging = false
        onClicked: if (!drag.active && notif.kind === "image") notif.preview()
        Item {
            id: dragProxy
            width: 1; height: 1
            Drag.active: parent.drag.active
            Drag.dragType: Drag.Automatic
            Drag.supportedActions: Qt.CopyAction
            Drag.mimeData: { "text/uri-list": parent.uri }
            Drag.imageSource: notif.thumbSource
        }
    }

    // Filename / URL / upload-state line, shared by the text-bearing styles.
    component StatusText: Text {
        text: notif.uploading ? qsTr("Uploading…")
              : notif.url !== "" ? notif.url
              : notif.fileName !== "" ? notif.fileName
              : qsTr("Not saved")
        color: notif.url !== "" ? Theme.accent : Theme.textSecondary
        font.pixelSize: Theme.fontS
        elide: Text.ElideMiddle
        maximumLineCount: 1
    }

    Item {
        id: card
        x: popupX
        y: popupY
        width: popupW
        height: popupH

        // Keep the static background/shadow in its own layer. Layering `card`
        // itself also cached every child, so a button hover invalidated and
        // re-composited the full card + blur on every animation frame. That is
        // especially visible through the GNOME XWayland helper.
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

        // Countdown drain; pauses and resumes with the one-shot dismiss timer.
        // Stepped Timer, NOT a NumberAnimation: a per-frame animation kept the
        // card's render loop awake at ~60 fps for the whole auto-hide window
        // (seconds of full-rate repaints for a slowly shrinking bar). Up to 30
        // wall-clock-derived steps look identical at this size and cannot drift.
        property real drain: 1.0
        Timer {
            interval: Math.max(50, popup.autoHideDurationMs / 30)
            repeat: true
            running: dismissTimer.running
            onTriggered: card.drain = popup.autoHideDurationMs > 0
                         ? popup.currentAutoHideRemaining() / popup.autoHideDurationMs : 0
        }

        HoverHandler {
            id: hover
            onHoveredChanged: {
                if (hovered) {
                    hoverExitTimer.stop()
                    popup.hoverGraceActive = false
                    popup.syncAutoHide()
                } else if (popup.autoHideSec > 0) {
                    popup.hoverGraceActive = true
                    hoverExitTimer.restart()
                }
            }
        }

        // ================= casual: the full card =================
        Row {
            visible: popup.style === "casual"
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10

            Rectangle {
                width: 122; height: parent.height
                radius: Theme.radiusM
                color: Theme.background
                clip: true
                Image {
                    anchors.fill: parent
                    source: notif.thumbSource
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                Rectangle {
                    visible: notif.kind !== "image"
                    anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 5
                    width: badge.implicitWidth + 12; height: 18; radius: 9
                    color: Theme.accent
                    Text {
                        id: badge
                        anchors.centerIn: parent
                        text: notif.kind.toUpperCase()
                        color: Theme.textOnAccent
                        font.pixelSize: 9; font.bold: true
                    }
                }
                // Click = floating preview; drag = pull the file out (DragThumb).
                DragThumb {}
            }

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
                StatusText { width: parent.width }
                ActionRow { btn: 32; icon: 16 }
            }
        }

        // ================= compact: tighter card =================
        Item {
            visible: popup.style === "compact"
            anchors.fill: parent
            anchors.margins: 8

            Rectangle {
                id: compactThumb
                width: 76; height: parent.height
                radius: Theme.radiusM
                color: Theme.background
                clip: true
                Image {
                    anchors.fill: parent
                    source: notif.thumbSource
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                DragThumb {}
            }
            Column {
                anchors.left: compactThumb.right
                anchors.leftMargin: 8
                anchors.right: parent.right
                anchors.rightMargin: 20   // room for the corner close
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                StatusText { width: parent.width }
                ActionRow { btn: 28; icon: 15 }
            }
        }

        // ================= small: one slim row =================
        Item {
            visible: popup.style === "small"
            anchors.fill: parent
            anchors.margins: 8

            Rectangle {
                id: smallThumb
                width: 36; height: 36
                anchors.verticalCenter: parent.verticalCenter
                radius: 6
                color: Theme.background
                clip: true
                Image {
                    anchors.fill: parent
                    source: notif.thumbSource
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize: Qt.size(72, 72)
                }
                DragThumb {}
            }
            UIconButton {
                id: smallClose
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                iconName: "close"; iconSize: 11; width: 22; height: 22
                onClicked: notif.dismiss()
            }
            ActionRow {
                id: smallActions
                btn: 24; icon: 14
                spacing: 0
                anchors.right: smallClose.left
                anchors.rightMargin: 2
                anchors.verticalCenter: parent.verticalCenter
            }
            StatusText {
                anchors.left: smallThumb.right
                anchors.leftMargin: 8
                anchors.right: smallActions.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // ================= minimal: a pill =================
        Item {
            visible: popup.style === "minimal"
            anchors.fill: parent
            anchors.margins: 6

            Rectangle {
                id: minimalDot
                width: 8; height: 8; radius: 4
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 4
                color: notif.uploading ? Theme.danger : Theme.accent
            }
            UIconButton {
                id: minimalClose
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                iconName: "close"; iconSize: 10; width: 20; height: 20
                onClicked: notif.dismiss()
            }
            StatusText {
                anchors.left: minimalDot.right
                anchors.leftMargin: 8
                anchors.right: minimalClose.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
            }
            // The pill itself is the action: click = floating preview.
            MouseArea {
                anchors.left: parent.left
                anchors.right: minimalClose.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                enabled: notif.kind === "image"
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: notif.preview()
            }
        }

        // ================= thumbnail: image-first =================
        Item {
            visible: popup.style === "thumbnail"
            anchors.fill: parent

            Rectangle {
                anchors.fill: parent
                anchors.margins: 1   // keep the card border visible
                radius: Theme.radiusL - 1
                color: Theme.background
                clip: true
                Image {
                    anchors.fill: parent
                    source: notif.thumbSource
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                // Click = preview, drag = pull the file out. Declared before the
                // scrim/actions overlay so those (higher in the stack) keep their
                // own clicks; the drag starts from the rest of the image.
                DragThumb {}
                // Bottom strip: filename over a scrim.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 26
                    color: Qt.rgba(0, 0, 0, 0.55)
                    StatusText {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: Text.AlignVCenter
                        color: "#FFFFFF"
                    }
                }
                // Actions fade in over the image on hover.
                Rectangle {
                    anchors.fill: parent
                    color: Qt.rgba(0, 0, 0, 0.45)
                    visible: hover.hovered
                    ActionRow {
                        anchors.centerIn: parent
                        btn: 30; icon: 16
                    }
                }
            }
        }

        // Corner close for the card-shaped styles (small/minimal have inline
        // ones; thumbnail shows it only while hovered, over the image).
        UIconButton {
            visible: popup.style === "casual" || popup.style === "compact"
                     || (popup.style === "thumbnail" && hover.hovered)
            anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 4
            iconName: "close"; iconSize: 12; width: 24; height: 24
            onClicked: notif.dismiss()
        }

        Rectangle {
            visible: popup.autoHideSec > 0
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.bottomMargin: 3
            height: 2
            radius: 1
            color: Theme.accent
            // The brighter frozen bar is the pause cue; no animation is needed,
            // and its width remains the exact saved countdown position.
            opacity: popup.autoHidePaused ? 0.9 : 0.5
            width: (card.width - 16) * card.drain
        }
    }
}
