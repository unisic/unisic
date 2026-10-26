import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Dialogs
import Unisic
import Unisic.Kit
import "components"

Window {
    id: editorWindow
    // Track the image size, but never below the minimum that keeps every
    // toolbar control visible, and never above the work area - oversized
    // images get a fit-scaled (and zoomable/pannable) canvas instead.
    width: Math.min(Math.max(minimumWidth, canvas.imageSize.width / Screen.devicePixelRatio
                                           + 2 * Theme.spacingM + 2 * fitSide),
                    Screen.width * 0.9)
    height: Math.min(Math.max(minimumHeight,
                              canvas.imageSize.height / Screen.devicePixelRatio
                              + chromeTop + 2 * Theme.spacingM + fitTop + fitBottom),
                     Screen.height * 0.9)
    minimumWidth: 820
    minimumHeight: 480
    // AppContext::openEditor centres and shows the window once it exists. Shown from here,
    // it would map before Component.onCompleted hands the canvas its image, at
    // the 820x480 minimum: the platform centres THAT size, then the window
    // grows to fit the image with its top-left corner pinned, which leaves it
    // hanging off towards the bottom-right corner.
    visible: false
    title: editorSession.overwriteMode ? qsTr("Unisic Editor (editing saved image)")
                                       : qsTr("Unisic Editor")
    color: Theme.backgroundDeep
    // Same decoration policy as the main window: the stylized frameless title
    // bar unless the user opted into system decorations (Appearance).
    flags: App.settings.useSystemDecoration
           ? Qt.Window
           : (Qt.Window | Qt.FramelessWindowHint)
    readonly property int chromeTop: App.settings.useSystemDecoration ? 0 : 38
    // The bars float over the stage; a fitted image keeps clear of them by
    // these margins (stage px). The options bar's slot is reserved whether it
    // shows or not, so picking a tool never moves or rescales the image. A
    // narrow window wraps the options bar onto more lines; those overlap the
    // image's top edge instead of shrinking it, and canvasFlick.topMargin keeps
    // that edge reachable by panning.
    readonly property int pillHeight: 40 + 2 * Theme.spacingS                          // = 52
    readonly property int fitTop: Theme.spacingM + 2 * pillHeight + 2 * Theme.spacingS  // = 128
    readonly property int fitBottom: Theme.spacingM + pillHeight + Theme.spacingS       // = 70
    readonly property int fitSide: Theme.spacingM

    // A Flow wrapped into `limit`, by the Flow's own rule (a child goes on the
    // current line unless it would cross the edge and is not alone there):
    // its widest line and how many lines. A Flow's implicitWidth is already
    // the wrapped width, so it cannot answer this.
    function flowWrap(flow, limit) {
        var widest = 0
        var line = 0
        var n = 0
        var lines = 0
        for (var i = 0; i < flow.children.length; ++i) {
            var c = flow.children[i]
            if (!c || !c.visible)
                continue
            var w = c.implicitWidth > 0 ? c.implicitWidth : c.width
            if (w <= 0)
                continue
            if (n > 0 && line + flow.spacing + w > limit) {
                widest = Math.max(widest, line)
                line = w
                ++lines
            } else {
                line = n > 0 ? line + flow.spacing + w : w
                if (n === 0)
                    lines = 1
            }
            ++n
        }
        return { width: Math.max(widest, line), lines: lines }
    }
    function flowWidth(flow, limit) { return flowWrap(flow, limit).width }
    // The narrowest limit that still wraps into as few lines as `limit` does:
    // the lines come out even, instead of one full line and an orphan (at
    // 880 px the text tool's presets sat alone on line two). The line count
    // only falls as the limit grows, so bisection finds it; about ten passes
    // over a handful of children, on a tool or width change.
    function flowBalanced(flow, limit) {
        var lines = flowWrap(flow, limit).lines
        if (lines <= 1)
            return limit
        var lo = 0
        var hi = limit
        while (hi - lo > 1) {
            var mid = Math.floor((lo + hi) / 2)
            if (flowWrap(flow, mid).lines <= lines)
                hi = mid
            else
                lo = mid
        }
        return hi
    }
    // A held Space must not outlive the window losing the keyboard: the
    // release goes to whichever window has it then.
    onActiveChanged: if (!active) canvasFlick.spacePan = false

    Component.onCompleted: {
        editorSession.bindCanvas(canvas)
    }

    // True once the user has taken any export action (save/copy/upload/OCR)
    // - statusText is only ever set by those. Used to skip the
    // discard-confirm nag: once exported, closing never nags.
    property bool exported: false
    // Set by the discard confirmation so the second close() pass goes through.
    property bool closeConfirmed: false
    Connections {
        target: editorSession
        function onStatusTextChanged() {
            if (editorSession.statusText !== "") editorWindow.exported = true
        }
    }

    // A single Escape / Close must not silently destroy unsaved annotations.
    // Guard the close when there are undoable edits and nothing was exported;
    // the confirm dialog's Discard sets closeConfirmed and re-closes.
    onClosing: (close) => {
        if (canvas.canUndo && !editorWindow.exported && !editorWindow.closeConfirmed) {
            close.accepted = false
            discardConfirm.open()
        }
    }

    // Text still being typed in the floating input is visible on the canvas but
    // only becomes an annotation on Enter - commit it before any export so the
    // result is exactly what's rendered (empty text is a safe no-op).
    function commitPendingText() {
        if (editorTextInput.visible) {
            if (editorTextInput.editingExisting)
                canvas.commitTextEdit(editorTextField.text)
            else
                canvas.commitText(editorTextInput.imgX, editorTextInput.imgY, editorTextField.text)
            editorTextInput.visible = false
            editorTextInput.editingExisting = false
            editorTextInput.inShape = false
            shortcutScope.forceActiveFocus()
        }
    }

    // Same, for the box being replaced by a NEW one. A bubble already holds every
    // keystroke (previewCalloutText), so its box needs no commit - and committing
    // it here would write those words into whatever the canvas selected on the
    // way to opening the new box, i.e. into the other bubble.
    function closePendingText() {
        if (editorTextInput.visible && editorTextInput.inShape) {
            editorTextInput.visible = false
            editorTextInput.editingExisting = false
            editorTextInput.inShape = false
        } else {
            commitPendingText()
        }
    }

    // Editing an existing capture (from history) overwrites the original file, so
    // confirm first; a normal capture saves straight to a fresh file.
    function doSave() {
        commitPendingText()
        if (editorSession.overwriteMode) overwriteConfirm.open()
        else editorSession.save()
    }

    function doSaveAs() {
        commitPendingText()
        editorSession.saveAsDialog()
    }

    MessageDialog {
        id: overwriteConfirm
        title: qsTr("Overwrite file?")
        text: qsTr("This replaces the original saved image with your edited version. This can't be undone.")
        buttons: MessageDialog.Save | MessageDialog.Cancel
        onAccepted: editorSession.save()
    }

    function recentList() {
        var s = App.settings.recentColors
        return s ? s.split(",").filter(function (x) { return x.length > 0 }) : []
    }

    // ---- tool grouping (Shapes) ----
    readonly property var shapesTools: ToolCatalog.groupTools("shapes", "editor", App.settings.hiddenTools)
    readonly property bool shapesActive: ToolCatalog.groupForEnum(canvas.tool) === "shapes"
    // Last shape picked from the sub-bar; the group chip re-selects it.
    property string currentShapeId: "rect"
    function toggleShapesGroup() {
        if (shapesActive) { canvas.tool = AnnotationCanvas.None; return }
        var pick = null
        for (var i = 0; i < shapesTools.length; ++i)
            if (shapesTools[i].id === currentShapeId) pick = shapesTools[i]
        if (!pick && shapesTools.length > 0) pick = shapesTools[0]
        if (pick) canvas.tool = pick.tool
    }
    function activateToolShortcut(key) {
        const picked = ToolCatalog.toolForShortcut(key, "editor")
        if (!picked)
            return false
        canvas.tool = picked.tool
        if (picked.group)
            currentShapeId = picked.id
        return true
    }
    // The visible members of a group, each with its letter - for the group
    // chip's spoken description. ToolCatalog.groups carries NO `shortcut`
    // field: the letters belong to the tools (L/A/M/R/O/D), never to the group,
    // so the chip says which keys its members answer to instead of implying it
    // has one of its own. Follows the hidden-tools setting, like the sub-bar.
    function groupMembers(groupId) {
        const ts = ToolCatalog.groupTools(groupId, "editor", App.settings.hiddenTools)
        var out = []
        for (var i = 0; i < ts.length; ++i)
            out.push(ToolCatalog.labelWithShortcut(ts[i]))
        return out.join(", ")
    }
    // Main-row model: ungrouped tools in catalog order, with each group's chip
    // inserted at its first member's position.
    function mainRowModel() {
        var out = []
        var seen = {}
        var ts = ToolCatalog.visibleFor("editor", App.settings.hiddenTools)
        for (var i = 0; i < ts.length; ++i) {
            var t = ts[i]
            if (t.group) {
                if (!seen[t.group]) {
                    seen[t.group] = true
                    for (var g = 0; g < ToolCatalog.groups.length; ++g)
                        if (ToolCatalog.groups[g].id === t.group)
                            out.push({ kind: "group", group: ToolCatalog.groups[g] })
                }
            } else {
                out.push({ kind: "tool", tool: t })
            }
        }
        return out
    }
    function addRecent(hex) {
        var list = recentList()
        hex = String(hex)
        list = list.filter(function (x) { return x.toLowerCase() !== hex.toLowerCase() })
        list.unshift(hex)
        if (list.length > 6) list = list.slice(0, 6)
        App.settings.recentColors = list.join(",")
    }

    // In-scene pickers, NOT QtQuick.Dialogs' ColorDialog: that was a separate
    // modal window which greyed the whole screen - impossible to judge the
    // colour against the image. These Popups live in the editor scene, dim it
    // only lightly and preview every change live on the canvas. The eyedropper
    // samples from the (frozen) image via the canvas colour-picking mode.
    property var pendingColorPopup: null

    UColorPopup {
        id: strokePopup
        onPicked: (c) => canvas.strokeColor = c
        onClosed: editorWindow.addRecent(String(canvas.strokeColor))
        onRequestScreenPick: { editorWindow.pendingColorPopup = strokePopup; canvas.colorPicking = true }
    }
    UColorPopup {
        id: fillPopup
        showAlpha: true
        onPicked: (c) => { canvas.shapeFillColor = c; canvas.shapeFillEnabled = true }
        onRequestScreenPick: { editorWindow.pendingColorPopup = fillPopup; canvas.colorPicking = true }
    }
    UColorPopup {
        id: outlinePopup
        onPicked: (c) => { canvas.textOutlineColor = c; canvas.textOutline = true }
        onRequestScreenPick: { editorWindow.pendingColorPopup = outlinePopup; canvas.colorPicking = true }
    }
    UColorPopup {
        id: textBgPopup
        showAlpha: true
        onPicked: (c) => { canvas.textBackgroundColor = c; canvas.textBackground = true }
        onRequestScreenPick: { editorWindow.pendingColorPopup = textBgPopup; canvas.colorPicking = true }
    }
    UConfirmDialog {
        id: discardConfirm
        title: qsTr("Discard annotations?")
        text: qsTr("You have unsaved annotations. Close the editor and discard them?")
        confirmText: qsTr("Discard")
        destructive: true
        onAccepted: { editorWindow.closeConfirmed = true; editorWindow.close() }
    }
    Item {
        id: shortcutScope
        anchors.fill: parent
        focus: true
        Keys.onPressed: (e) => {
            if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_Z) {
                if (e.modifiers & Qt.ShiftModifier) canvas.redo(); else canvas.undo()
            } else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_Y) canvas.redo()
            else if ((e.modifiers & Qt.ControlModifier) && (e.modifiers & Qt.ShiftModifier) && e.key === Qt.Key_S) editorWindow.doSaveAs()
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_S) editorWindow.doSave()
            // Ctrl+W closes the editor window (the discard prompt still applies).
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_W) editorWindow.close()
            // Ctrl+C copies the OCR text selection while in OCR mode, else the image.
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_C) {
                if (canvas.ocrMode) editorSession.copyOcrSelection()
                else { editorWindow.commitPendingText(); editorSession.copyToClipboard() }
            }
            else if (!canvas.ocrMode && (e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_V) {
                var pasteAt = canvas.toImage(canvas.width / 2, canvas.height / 2)
                canvas.pasteClipboard(pasteAt.x, pasteAt.y)
            }
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_A && canvas.ocrMode) canvas.ocrSelectAll()
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_U) { editorWindow.commitPendingText(); editorSession.upload() }
            else if ((e.modifiers & Qt.ControlModifier) && (e.key === Qt.Key_Plus || e.key === Qt.Key_Equal)) canvasFlick.zoomBy(1.2)
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_Minus) canvasFlick.zoomBy(1 / 1.2)
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_0) canvasFlick.zoom = 0
            // Delete / Backspace removes the selected shape (Edit tool).
            else if ((e.key === Qt.Key_Delete || e.key === Qt.Key_Backspace) && canvas.hasAnnotSelection)
                canvas.removeSelectedAnnot()
            // Single-key tool switching. Text inputs consume their own keys
            // before this parent scope, so typing an annotation is unaffected.
            // UKeys.unmodified, never a hand-rolled modifier test: it is the
            // one bare-key rule the whole app shares (see UKeys for why).
            else if (!canvas.ocrMode && UKeys.unmodified(e)
                     && editorWindow.activateToolShortcut(e.key)) {}
            // Ctrl+Enter = quick copy-and-close. A BARE Enter must not end the
            // session (it copies to the clipboard and the copy marks the session
            // "exported", which then skips the unsaved-annotations discard prompt).
            else if (!canvas.ocrMode && (e.modifiers & Qt.ControlModifier)
                     && (e.key === Qt.Key_Return || e.key === Qt.Key_Enter)) {
                editorWindow.commitPendingText()
                editorSession.copyToClipboard()
                editorWindow.close()
            }
            // Arrow keys nudge the selected shape (Shift = ×10).
            else if (canvas.hasAnnotSelection && (e.key === Qt.Key_Left || e.key === Qt.Key_Right
                     || e.key === Qt.Key_Up || e.key === Qt.Key_Down)) {
                var step = (e.modifiers & Qt.ShiftModifier) ? 10 : 1
                if (e.key === Qt.Key_Left) canvas.nudgeSelectedAnnot(-step, 0)
                else if (e.key === Qt.Key_Right) canvas.nudgeSelectedAnnot(step, 0)
                else if (e.key === Qt.Key_Up) canvas.nudgeSelectedAnnot(0, -step)
                else canvas.nudgeSelectedAnnot(0, step)
            }
            // Escape cancels the colour eyedropper first, then exits OCR pick
            // mode, then deselects a shape, then cancels a crop selection;
            // only a press with nothing pending closes the editor.
            else if (e.key === Qt.Key_Escape) {
                if (canvas.colorPicking) {
                    canvas.colorPicking = false
                    editorWindow.pendingColorPopup = null
                }
                else if (canvas.ocrMode) canvas.clearOcrMode()
                else if (canvas.hasAnnotSelection) canvas.clearAnnotSelection()
                else if (canvas.hasSelection) canvas.clearSelection()
                else editorWindow.close()
            }
            // Held Space is the hand, whatever the tool (canvasFlick).
            else if (e.key === Qt.Key_Space && UKeys.unmodified(e)) {
                if (!e.isAutoRepeat) canvasFlick.spacePan = true
            }
            else return
            e.accepted = true
        }
        Keys.onReleased: (e) => {
            if (e.key !== Qt.Key_Space || e.isAutoRepeat)
                return
            canvasFlick.spacePan = false
            e.accepted = true
        }

        // ---------- custom title bar (frameless decoration) ----------
        // The main window's bar: it blends into the backdrop with the title
        // centred and dimmed, and only the stage below is a card.
        Item {
            id: editorTitleBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: editorWindow.chromeTop
            visible: !App.settings.useSystemDecoration
            z: 20
            // Deferred startSystemMove past a drag threshold - same pattern
            // (and reason) as Main.qml's title bar.
            MouseArea {
                anchors.fill: parent
                property real pressX: 0
                property real pressY: 0
                property bool moving: false
                onPressed: (m) => { pressX = m.x; pressY = m.y; moving = false }
                onPositionChanged: (m) => {
                    if (!moving && (Math.abs(m.x - pressX) > 6 || Math.abs(m.y - pressY) > 6)) {
                        moving = true
                        editorWindow.startSystemMove()
                    }
                }
                onDoubleClicked: editorWindow.visibility === Window.Maximized
                                 ? editorWindow.showNormal() : editorWindow.showMaximized()
            }
            Text {
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.width - 2 * (titleButtons.width + Theme.spacingL))
                elide: Text.ElideRight
                text: editorWindow.title
                color: Theme.textSecondary
                font.pixelSize: Theme.fontM
                font.weight: Font.DemiBold
            }
            Row {
                id: titleButtons
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                UIconButton {
                    iconName: "minus"; iconSize: 14; width: 30; height: 30
                    tooltip: qsTr("Minimize")
                    onClicked: editorWindow.showMinimized()
                }
                UIconButton {
                    iconName: "window"; iconSize: 13; width: 30; height: 30
                    tooltip: qsTr("Maximize")
                    onClicked: editorWindow.visibility === Window.Maximized
                               ? editorWindow.showNormal() : editorWindow.showMaximized()
                }
                UIconButton {
                    iconName: "close"; iconSize: 14; width: 30; height: 30
                    tooltip: qsTr("Close")
                    onClicked: editorWindow.close()
                }
            }
        }

        // ---------- stage ----------
        // One card holds the image and the bars float over it, the way a photo
        // viewer lays out: tools on top, view and export controls at the
        // bottom. The fitted image sits in the clear area between them
        // (fitTop/fitBottom); zoomed in, it pans underneath them.
        Rectangle {
            id: stage
            anchors.fill: parent
            anchors.topMargin: editorWindow.chromeTop + Theme.spacingM
            anchors.leftMargin: Theme.spacingM
            anchors.rightMargin: Theme.spacingM
            anchors.bottomMargin: Theme.spacingM
            radius: Theme.radiusL
            color: Theme.background
            border.width: 1
            border.color: Theme.edgeLight

            Flickable {
                id: canvasFlick
                objectName: "editorCanvasFlick"  // the smoke test drives zoom through it
                anchors.fill: parent
                // clip is rectangular: the inset keeps a zoomed image out of
                // the card's rounded corners.
                anchors.margins: Theme.spacingS
                clip: true
                // Panning is done by hand below (drag with the hand, Space+drag,
                // middle drag, wheel, scrollbars), never by Flickable's own
                // flick: a drawing tool owns the left button.
                interactive: false
                // The clear area between the bars, in viewport coordinates.
                readonly property real padSide: editorWindow.fitSide - Theme.spacingS
                readonly property real padTop: editorWindow.fitTop - Theme.spacingS
                readonly property real padBottom: editorWindow.fitBottom - Theme.spacingS
                readonly property real availW: Math.max(1, width - 2 * padSide)
                readonly property real availH: Math.max(1, height - padTop - padBottom)
                // How far a wrapped options bar reaches past the one line the
                // fit reserves for it: the image's top can be panned that far
                // down, out from under the bar. A margin, so the bar wrapping
                // moves nothing on screen.
                topMargin: ctxPill.visible
                    ? Math.max(0, ctxPill.y + ctxPill.height + Theme.spacingS - editorWindow.fitTop) : 0
                onTopMarginChanged: clampPan()
                contentWidth: Math.max(width, canvas.width + 2 * padSide)
                contentHeight: Math.max(height, canvas.height + padTop + padBottom)
                onContentWidthChanged: clampPan()
                onContentHeightChanged: clampPan()
                onWidthChanged: clampPan()
                onHeightChanged: clampPan()
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                // 0 = fit-to-window (never upscales); anything else is an
                // absolute zoom factor set by the wheel, a pinch, the zoom bar
                // or Ctrl+-/+. Ctrl+0 and the percentage return to fit.
                property real zoom: 0
                readonly property real fitScale: canvas.imageSize.width > 0
                    ? Math.min(availW / canvas.imageSize.width, availH / canvas.imageSize.height, 1)
                    : 1
                readonly property real effectiveScale: zoom > 0 ? zoom : fitScale
                // QQuickPaintedItem's texture follows the item size × DPR:
                // cap the zoomed item so the device-pixel texture stays under
                // common GPU limits (blank canvas otherwise) and strokes don't
                // repaint hundreds of MB. Fit is always allowed - a fitted
                // item never exceeds the viewport.
                readonly property real texCap: 6000 / Math.max(1, Screen.devicePixelRatio)
                readonly property real maxZoom: canvas.imageSize.width > 0
                    ? Math.max(fitScale, Math.min(4, texCap / canvas.imageSize.width,
                                                  texCap / canvas.imageSize.height))
                    : 4
                // maxZoom shrinks when the image grows (crop -> zoom in ->
                // undo): re-clamp, or the item balloons past the texture cap.
                onMaxZoomChanged: if (zoom > 0 && zoom > maxZoom) { zoom = maxZoom; clampPan() }
                // Lower bound below fitScale would zoom IN on huge images
                // whose fit is already under 0.1.
                readonly property real minZoom: Math.min(0.1, fitScale)
                readonly property bool canPan: contentWidth > width + 0.5 || contentHeight > height + 0.5 || topMargin > 0.5
                // Held Space turns any tool into the hand (shortcutScope).
                property bool spacePan: false

                function clampPan() {
                    contentX = Math.max(0, Math.min(contentWidth - width, contentX))
                    contentY = Math.max(-topMargin, Math.min(contentHeight - height, contentY))
                }
                function panBy(dx, dy) {
                    contentX -= dx
                    contentY -= dy
                    clampPan()
                }
                // Zoom anchored to a viewport point: whatever image pixel sits
                // under (ax, ay) stays there across the step. The wheel, a
                // pinch and a double-click pass the pointer; the keyboard and
                // the zoom bar default to the middle of the clear area.
                function zoomTo(z, ax, ay) {
                    if (ax === undefined) { ax = padSide + availW / 2; ay = padTop + availH / 2 }
                    const fx = canvas.width > 0 ? (contentX + ax - canvas.x) / canvas.width : 0.5
                    const fy = canvas.height > 0 ? (contentY + ay - canvas.y) / canvas.height : 0.5
                    zoom = Math.max(minZoom, Math.min(maxZoom, z))
                    contentX = canvas.x + fx * canvas.width - ax
                    contentY = canvas.y + fy * canvas.height - ay
                    clampPan()
                }
                function zoomBy(f, ax, ay) { zoomTo(effectiveScale * f, ax, ay) }
                // Fit and 100% are the two views worth one click (or one
                // double-click on the image); anything between is what - and
                // + are for.
                function toggleZoom(ax, ay) {
                    if (zoom > 0) { zoom = 0; clampPan() }
                    else zoomTo(Math.min(1, maxZoom), ax, ay)
                }
                // Drag-to-pan, measured in the viewport: measuring in the
                // moving content space would feed the pan back into itself.
                property real panFromX: 0
                property real panFromY: 0
                property point panPress: Qt.point(0, 0)
                function beginPan(area, x, y) {
                    panFromX = contentX
                    panFromY = contentY
                    panPress = area.mapToItem(canvasFlick, x, y)
                }
                function dragPan(area, x, y) {
                    const p = area.mapToItem(canvasFlick, x, y)
                    contentX = panFromX - (p.x - panPress.x)
                    contentY = panFromY - (p.y - panPress.y)
                    clampPan()
                }

                // Wheel and pinch as in a photo viewer: a mouse wheel zooms
                // under the pointer, a touchpad's two-finger scroll pans and
                // its pinch zooms. pixelDelta tells the two apart, not the
                // device type: on Wayland a wheel notch arrives with
                // pixelDelta (0,0) and a touchpad scroll with real pixels
                // (qtbase 6.11, plugins/platforms/wayland), while the type is
                // unreliable - Qt tags some wheels as TouchPad, which is also
                // why every handler here accepts both (WheelHandler takes only
                // PointerDevice.Mouse by default). Under X11 (XI2) a touchpad
                // scroll carries no pixelDelta either, so there it zooms too.
                // The zoom step is scaled by the delta, 1.2x per 120-unit
                // notch, because a high-resolution wheel sends many small
                // events and a fixed step per event would race to the limit.
                // Event coordinates are the handler's parent item's.
                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    acceptedModifiers: Qt.ControlModifier
                    onWheel: (ev) => {
                        if (ev.angleDelta.y === 0)
                            return
                        const p = canvasFlick.mapFromItem(parent, ev.x, ev.y)
                        canvasFlick.zoomBy(Math.pow(1.2, ev.angleDelta.y / 120), p.x, p.y)
                    }
                }
                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    acceptedModifiers: Qt.NoModifier
                    onWheel: (ev) => {
                        const pixels = ev.pixelDelta.x !== 0 || ev.pixelDelta.y !== 0
                        if (!pixels && ev.angleDelta.y !== 0) {
                            const p = canvasFlick.mapFromItem(parent, ev.x, ev.y)
                            canvasFlick.zoomBy(Math.pow(1.2, ev.angleDelta.y / 120), p.x, p.y)
                        } else {
                            // A tilt wheel's sideways notch pans too.
                            canvasFlick.panBy(pixels ? ev.pixelDelta.x : ev.angleDelta.x,
                                              pixels ? ev.pixelDelta.y : 0)
                        }
                    }
                }
                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    acceptedModifiers: Qt.ShiftModifier
                    onWheel: (ev) => canvasFlick.panBy(ev.angleDelta.y, 0)
                }
                PinchHandler {
                    target: null
                    onScaleChanged: (delta) => {
                        const p = canvasFlick.mapFromItem(parent, centroid.position.x, centroid.position.y)
                        canvasFlick.zoomBy(delta, p.x, p.y)
                    }
                }

                // The hand. With no tool picked (how the editor opens, and what
                // a second click on the active tool returns to), or while Space
                // is held, it lies over the image: a drag pans and a
                // double-click switches between fit and 100%. Otherwise it lies
                // under the image and catches only the margin around it, plus
                // the middle button everywhere (the canvas claims only the left
                // and right ones). The right button always reaches the canvas.
                MouseArea {
                    id: panArea
                    anchors.fill: parent
                    readonly property bool hand: canvasFlick.spacePan
                        || (canvas.tool === AnnotationCanvas.None && !canvas.ocrMode && !canvas.colorPicking)
                    z: hand ? 40 : 0
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    cursorShape: pressed ? Qt.ClosedHandCursor
                                         : canvasFlick.canPan ? Qt.OpenHandCursor : Qt.ArrowCursor
                    onPressed: (m) => canvasFlick.beginPan(panArea, m.x, m.y)
                    onPositionChanged: (m) => canvasFlick.dragPan(panArea, m.x, m.y)
                    onDoubleClicked: (m) => {
                        if (m.button !== Qt.LeftButton)
                            return
                        const p = panArea.mapToItem(canvasFlick, m.x, m.y)
                        canvasFlick.toggleZoom(p.x, p.y)
                    }
                }

                AnnotationCanvas {
                    id: canvas
                    width: imageSize.width * canvasFlick.effectiveScale
                    height: imageSize.height * canvasFlick.effectiveScale
                    Accessible.role: Accessible.Canvas
                    Accessible.name: qsTr("Annotation canvas")
                    Accessible.description: qsTr("%1 by %2 pixels")
                                                .arg(canvas.imageSize.width)
                                                .arg(canvas.imageSize.height)
                    onCopyRequested: {
                        editorWindow.commitPendingText()
                        editorSession.copyToClipboard()
                    }
                    x: canvasFlick.padSide + Math.max(0, (canvasFlick.availW - width) / 2)
                    y: canvasFlick.padTop + Math.max(0, (canvasFlick.availH - height) / 2)
                    selectionMode: canvas.tool === AnnotationCanvas.Crop
                    uiAccent: Theme.accent
                    uiScrim: Theme.primary
                    Component.onCompleted: {
                        strokeColor = App.settings.editorStrokeColor
                        strokeWidth = App.settings.editorStrokeWidth
                        highlightMode = App.settings.editorHighlightMode
                        fontSize = App.settings.editorFontSize
                        stepSize = App.settings.editorStepSize
                        shapeFillColor = App.settings.editorFillColor
                        shapeFillEnabled = App.settings.editorFillEnabled
                        fontFamily = App.settings.editorFontFamily
                        fontBold = App.settings.editorFontBold
                        fontItalic = App.settings.editorFontItalic
                        fontUnderline = App.settings.editorFontUnderline
                        textOutline = App.settings.editorTextOutline
                        textOutlineColor = App.settings.editorTextOutlineColor
                        textBackground = App.settings.editorTextBackground
                        textBackgroundColor = App.settings.editorTextBgColor
                    }
                    // The automatic highlighter red<->yellow swap must not leak
                    // into the saved default - persist only real user picks.
                    // While a placed shape is selected (Edit tool), a property
                    // change restyles THAT shape and must NOT overwrite the
                    // saved "next shape" defaults.
                    // Preferences can pin the saved defaults entirely
                    // (editorResetColors / editorResetTools): changes then
                    // apply to this session only.
                    readonly property bool persistColors: !hasAnnotSelection && !App.settings.editorResetColors
                    readonly property bool persistTools: !hasAnnotSelection && !App.settings.editorResetTools
                    onStrokeColorChanged: if (!strokeColorIsAuto && persistColors) App.settings.editorStrokeColor = String(strokeColor)
                    onStrokeWidthChanged: if (persistTools) App.settings.editorStrokeWidth = strokeWidth
                    onHighlightModeChanged: if (persistTools) App.settings.editorHighlightMode = highlightMode
                    onFontSizeChanged: if (persistTools) App.settings.editorFontSize = fontSize
                    onStepSizeChanged: if (persistTools) App.settings.editorStepSize = stepSize
                    onShapeFillColorChanged: if (persistColors) App.settings.editorFillColor = String(shapeFillColor)
                    onShapeFillEnabledChanged: if (persistTools) App.settings.editorFillEnabled = shapeFillEnabled
                    onFontFamilyChanged: if (persistTools) App.settings.editorFontFamily = fontFamily
                    onFontBoldChanged: if (persistTools) App.settings.editorFontBold = fontBold
                    onFontItalicChanged: if (persistTools) App.settings.editorFontItalic = fontItalic
                    onFontUnderlineChanged: if (persistTools) App.settings.editorFontUnderline = fontUnderline
                    onTextOutlineChanged: if (persistTools) App.settings.editorTextOutline = textOutline
                    onTextOutlineColorChanged: if (persistColors) App.settings.editorTextOutlineColor = String(textOutlineColor)
                    onTextBackgroundChanged: if (persistTools) App.settings.editorTextBackground = textBackground
                    onTextBackgroundColorChanged: if (persistColors) App.settings.editorTextBgColor = String(textBackgroundColor)
                    // Eyedropper result → reopen the popup that requested it,
                    // seeded with the sampled pixel.
                    onColorPicked: (c) => {
                        if (editorWindow.pendingColorPopup) {
                            editorWindow.pendingColorPopup.openWith(c)
                            editorWindow.pendingColorPopup = null
                        }
                    }
                    onTextRequested: (x, y) => {
                        // Reposition while a text box is open: keep what was
                        // typed instead of silently discarding it.
                        editorWindow.closePendingText()
                        editorTextInput.editingExisting = false
                        editorTextInput.inShape = false
                        editorTextInput.imgX = x
                        editorTextInput.imgY = y
                        editorTextInput.visible = true
                        editorTextField.text = ""
                        editorTextField.forceActiveFocus()
                    }
                    // A placed Text shape was double-clicked, or a callout was
                    // drawn/double-clicked: the editor reopens prefilled and,
                    // for a callout, exactly over the bubble's writable area
                    // (canvas.textEditRect) so the words are typed where they
                    // will land.
                    onTextEditRequested: (x, y, t) => {
                        editorWindow.closePendingText()
                        editorTextInput.editingExisting = true
                        editorTextInput.inShape = canvas.textEditRect.width > 0
                        editorTextInput.imgX = x
                        editorTextInput.imgY = y
                        editorTextInput.visible = true
                        editorTextField.text = t
                        editorTextField.forceActiveFocus()
                        editorTextField.selectAll()
                    }
                }

                Item {
                    id: editorTextInput
                    property real imgX: 0
                    property real imgY: 0
                    // True for a callout: the box IS the bubble's writable area,
                    // tracking it live as the typing grows the bubble. The glyphs
                    // are then the canvas's own (the annotation is repainted on
                    // every keystroke), so this box contributes only the caret.
                    property bool inShape: false
                    property bool editingExisting: false
                    visible: false
                    x: canvas.x + (inShape ? canvas.textEditRect.x : imgX) * canvas.renderScale
                    y: canvas.y + (inShape ? canvas.textEditRect.y : imgY) * canvas.renderScale
                    // Scale with zoom, or the zoomed font gets clipped by a
                    // fixed-size box; grow with the typed lines.
                    width: inShape ? canvas.textEditRect.width * canvas.renderScale
                                   : 360 * Math.max(1, canvas.renderScale)
                    height: inShape ? canvas.textEditRect.height * canvas.renderScale
                                    : Math.max(40 * Math.max(1, canvas.renderScale),
                                               editorTextField.implicitHeight + 16)
                    z: 50
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusS
                        // Inside a bubble the panel would only dim the text it is
                        // sitting on: the border alone marks the edited area.
                        color: editorTextInput.inShape ? "transparent"
                                                       : Theme.alpha(Theme.mediaBase, 0.6)
                        border.width: 1
                        border.color: Theme.alpha(Theme.accent, editorTextInput.inShape ? 0.5 : 1.0)
                    }
                    // TextEdit (multi-line): Enter = new line, Ctrl+Enter (or
                    // any export/click-away via commitPendingText) commits.
                    TextEdit {
                        id: editorTextField
                        anchors.fill: parent
                        anchors.margins: editorTextInput.inShape ? 0 : 8
                        // Inside a bubble the preview matches the painter:
                        // centred both ways and wrapped. A Text annotation
                        // wraps only on the newlines you type, so it must not.
                        horizontalAlignment: editorTextInput.inShape ? TextEdit.AlignHCenter
                                                                     : TextEdit.AlignLeft
                        verticalAlignment: editorTextInput.inShape ? TextEdit.AlignVCenter
                                                                   : TextEdit.AlignTop
                        wrapMode: editorTextInput.inShape ? TextEdit.Wrap : TextEdit.NoWrap
                        // In a bubble the canvas paints the real glyphs under
                        // this box on every keystroke, so drawing them again
                        // here would only double them, half a pixel off.
                        color: editorTextInput.inShape ? "transparent" : canvas.strokeColor
                        selectionColor: Theme.alpha(Theme.accent, 0.45)
                        selectedTextColor: color
                        // Its height comes from the line, its width from here.
                        cursorDelegate: Rectangle { width: 2; color: Theme.accent }
                        // Grow the bubble as the words are typed, not at commit.
                        onTextChanged: if (editorTextInput.inShape) canvas.previewCalloutText(text)
                        font.family: canvas.fontFamily === "" ? Qt.application.font.family : canvas.fontFamily
                        font.pixelSize: Math.max(10, canvas.fontSize * canvas.renderScale)
                        font.bold: canvas.fontBold
                        font.italic: canvas.fontItalic
                        font.underline: canvas.fontUnderline
                        Accessible.role: Accessible.EditableText
                        Accessible.name: qsTr("Annotation text")
                        Accessible.description: qsTr("Ctrl+Enter finishes, Escape discards")
                        Accessible.editable: true
                        Accessible.multiLine: true
                        // Return focus to the shortcut scope, else Ctrl+Z/S/C/U
                        // and Escape stay dead after using the text tool.
                        Keys.onPressed: (e) => {
                            if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter)
                                    && (e.modifiers & Qt.ControlModifier)) {
                                editorWindow.commitPendingText()
                                e.accepted = true
                            } else if (e.key === Qt.Key_Tab || e.key === Qt.Key_Backtab) {
                                // Tab keys are swallowed here: this box owns the
                                // keyboard until the text is committed
                                // (Ctrl+Enter) or dropped (Escape).
                                //
                                // Measured, with the toolbar chips now tab stops
                                // (offscreen probe, real key events): SHIFT+TAB
                                // left the box and landed on a chip mid-typing,
                                // with the half-typed text still floating over
                                // the canvas and every following letter going to
                                // a chip's tool shortcut instead. Plain TAB did
                                // not move focus - QQuickTextEdit accepts it
                                // itself - it TYPED a literal \t into the
                                // annotation, i.e. an ~80 px invisible gap in
                                // the exported image, from a key no other text
                                // field in this app produces a character with.
                                //
                                // Neither is wanted, and both go away with one
                                // rule that matches the rest of the app: Tab is
                                // a NAVIGATION key everywhere here (UTextField
                                // is a tab stop and Tab moves to the next
                                // field), never a character. This box is a
                                // transient in-canvas input, not one field in a
                                // row of them, so there is nowhere to navigate
                                // to and the navigation is a no-op.
                                e.accepted = true
                            } else if (e.key === Qt.Key_Escape) {
                                // Puts a live-previewed bubble back to the text
                                // it had before this edit, undo entry included.
                                canvas.cancelTextEdit()
                                editorTextInput.visible = false
                                editorTextInput.editingExisting = false
                                editorTextInput.inShape = false
                                shortcutScope.forceActiveFocus()
                                e.accepted = true
                            }
                        }
                    }
                    Text {
                        visible: editorTextField.text === ""
                        anchors.fill: parent
                        anchors.margins: editorTextInput.inShape ? 0 : 8
                        horizontalAlignment: editorTextInput.inShape ? Text.AlignHCenter
                                                                     : Text.AlignLeft
                        verticalAlignment: editorTextInput.inShape ? Text.AlignVCenter
                                                                   : Text.AlignTop
                        elide: Text.ElideRight
                        text: qsTr("Text… (Ctrl+Enter finishes)")
                        color: Theme.textTertiary
                        font.pixelSize: Math.max(10, canvas.fontSize * canvas.renderScale)
                    }
                }
            }

            // ---------- floating bars ----------
            // One step up from the stage, with the window's light edge and no
            // shadow: the stage card is already the layer below them.
            component Pill: Rectangle {
                radius: Theme.radiusXL
                color: Theme.surface
                border.width: 1
                border.color: Theme.edgeLight
                // Presses and wheel turns on the padding stop here instead of
                // drawing on, or zooming, the image under the bar.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.AllButtons
                    cursorShape: Qt.ArrowCursor
                    onWheel: (wheel) => wheel.accepted = true
                }
            }

            // UNLIKE the capture overlay, the chips in here ARE tab stops (the
            // kit default). They can be: this window's key scope ends its
            // if/else chain with a bare `return`, so an unmatched key - Tab
            // included - is left UNaccepted and Qt's focus chain still works;
            // and ToolChip only binds Space/Return/Enter, which auto-accept
            // exactly themselves, so P/L/A/T/... and every Ctrl chord keep
            // bubbling from a focused chip up to shortcutScope.
            //
            // Measured with real key events on a focused chip, in an offscreen
            // probe built from this file's own handler text: P and T switched
            // the tool, Ctrl+Z reached undo and Escape reached the scope, all
            // three with the chip's activation count still 0, while Space,
            // Return and Enter activated the chip and did NOT reach the scope.
            // Tab alone is not covered by that probe (a qmltestrunner window
            // runs no focus chain), so that half rests on the `else return`
            // that closes the chain.
            Pill {
                id: toolPill
                x: Math.round((stage.width - width) / 2)
                y: Theme.spacingM
                width: toolRow.width + 2 * Theme.spacingS
                height: editorWindow.pillHeight
                Accessible.role: Accessible.ToolBar
                Accessible.name: qsTr("Annotation tools")

                Row {
                    id: toolRow
                    anchors.centerIn: parent
                    spacing: 4

                    Row {
                        spacing: 4
                        // Same gate as the tool letters in shortcutScope.
                        enabled: !canvas.ocrMode

                        Repeater {
                            model: editorWindow.mainRowModel()
                            delegate: ToolChip {
                                iconName: modelData.kind === "group"
                                          ? modelData.group.iconName
                                          : ToolCatalog.toolIconName(modelData.tool, App.settings.editorIconStyle, App.settings.editorToolIcons)
                                // The group glyph has no freedesktop equivalent - always bundled.
                                iconStyle: modelData.kind === "group" ? "custom" : App.settings.editorIconStyle
                                label: modelData.kind === "group"
                                       ? modelData.group.label
                                       : ToolCatalog.labelWithShortcut(modelData.tool)
                                // A tool chip's hover-tip label already IS its
                                // spoken name, shortcut letter included, and
                                // ToolChip falls back to it. A GROUP chip's
                                // label is the bare word "Shapes", which says
                                // nothing about it being a group or about the
                                // press opening a sub-bar - so it names itself
                                // here instead. The visible label is untouched,
                                // and the spoken name still STARTS with it, so
                                // "click Shapes" keeps working.
                                accessibleName: modelData.kind === "group"
                                                ? qsTr("%1 tool group").arg(modelData.group.label)
                                                : ""
                                accessibleDescription: modelData.kind === "group"
                                                ? qsTr("Opens these tools in the bar below: %1. The group itself has no shortcut.")
                                                      .arg(editorWindow.groupMembers(modelData.group.id))
                                                : ""
                                active: modelData.kind === "group"
                                        ? editorWindow.shapesActive
                                        : canvas.tool === modelData.tool.tool
                                // A second click puts the tool down, like the
                                // group chip does: the pointer is the hand again.
                                onClicked: modelData.kind === "group"
                                           ? editorWindow.toggleShapesGroup()
                                           : canvas.tool = canvas.tool === modelData.tool.tool
                                                           ? AnnotationCanvas.None : modelData.tool.tool
                            }
                        }
                    }

                    Rectangle { width: 1; height: 30; color: Theme.divider; anchors.verticalCenter: parent.verticalCenter }

                    ToolChip { iconName: "edit-undo"; label: qsTr("Undo"); enabled: canvas.canUndo; onClicked: canvas.undo() }
                    ToolChip { iconName: "edit-redo"; label: qsTr("Redo"); enabled: canvas.canRedo; onClicked: canvas.redo() }
                }
            }

            // The active tool's options, the Shapes group's members, the crop
            // and OCR actions: whatever the moment needs, in one bar under the
            // tools. Its slot is reserved in fitTop whether it shows or not,
            // so picking a tool never moves or rescales the image.
            Pill {
                id: ctxPill
                // Props follow the active tool, or the SELECTED shape when
                // the Edit tool is active.
                readonly property var ctxProps: ToolCatalog.contextProps(canvas.tool, canvas.selectedAnnotTool)
                readonly property real maxW: stage.width - 2 * Theme.spacingM - 2 * Theme.spacingS
                readonly property bool shown: canvas.ocrMode || editorWindow.shapesActive
                    || ctxProps.length > 0 || canvas.hasAnnotSelection
                    || (canvas.tool === AnnotationCanvas.Crop && canvas.hasSelection)
                x: Math.round((stage.width - width) / 2)
                y: toolPill.y + toolPill.height + Theme.spacingS
                width: ctxFlow.width + 2 * Theme.spacingS
                height: Math.max(editorWindow.pillHeight, ctxFlow.height + 2 * Theme.spacingS)
                // Fades in, but hides at once: everything inside is bound to
                // the same state and is already gone, so a fade-out only
                // showed the empty 12 px shell in the middle of the stage.
                opacity: shown ? 1 : 0
                visible: shown
                Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
                Accessible.role: Accessible.ToolBar
                Accessible.name: qsTr("Tool options")

                Flow {
                    id: ctxFlow
                    anchors.centerIn: parent
                    // The widest line it lays out once wrapped evenly into
                    // the stage, never the whole stage: the bar hugs its content.
                    width: editorWindow.flowWidth(ctxFlow, editorWindow.flowBalanced(ctxFlow, ctxPill.maxW))
                    // Same 6 px gap the props strip uses internally,
                    // horizontally and between lines.
                    spacing: 6

                    // The group chips and the delete affordance are one
                    // block, so they wrap as one.
                    Row {
                        id: shapesRow
                        spacing: 6
                        // A zero-width but VISIBLE child still costs the
                        // Flow a spacing gap. Spelled out rather than
                        // derived from `implicitWidth > 0`: a positioner
                        // reports content size 0 while it is itself
                        // invisible, so that form latches off the first
                        // time it is false and never comes back.
                        visible: !canvas.ocrMode && (editorWindow.shapesActive || canvas.hasAnnotSelection)

                        Repeater {
                            model: editorWindow.shapesActive ? editorWindow.shapesTools : []
                            delegate: ToolChip {
                                iconName: ToolCatalog.toolIconName(modelData, App.settings.editorIconStyle, App.settings.editorToolIcons)
                                iconStyle: App.settings.editorIconStyle
                                label: ToolCatalog.labelWithShortcut(modelData)
                                active: canvas.tool === modelData.tool
                                onClicked: {
                                    canvas.tool = modelData.tool
                                    editorWindow.currentShapeId = modelData.id
                                }
                            }
                        }

                        // Delete affordance for the selected shape - shown for
                        // any tool, since a plain click selects shapes now.
                        ToolChip {
                            visible: canvas.hasAnnotSelection
                            iconName: "edit-delete"
                            label: qsTr("Delete shape")
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: canvas.removeSelectedAnnot()
                        }

                        Rectangle {
                            visible: ctxPill.ctxProps.length > 0
                            // Nothing to separate once the props wrap below.
                            opacity: propsBar.y > shapesRow.y ? 0 : 1
                            width: 1; height: 30; color: Theme.divider
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    ToolPropsBar {
                        id: propsBar
                        visible: !canvas.ocrMode && ctxPill.ctxProps.length > 0
                        canvas: canvas
                        props: ctxPill.ctxProps
                        recentColors: editorWindow.recentList()
                        // Bounded, the strip wraps - and the Flow drops it onto
                        // its own line first, so it only has to wrap inside
                        // itself when even a whole line is not enough. Then
                        // into even lines, like the Flow around it.
                        maxWidth: editorWindow.flowBalanced(propsBar, ctxPill.maxW)
                        onStrokePickerRequested: strokePopup.openWith(canvas.strokeColor)
                        onFillPickerRequested: fillPopup.openWith(canvas.shapeFillColor)
                        onTextOutlinePickerRequested: outlinePopup.openWith(canvas.textOutlineColor)
                        onTextBackgroundPickerRequested: textBgPopup.openWith(canvas.textBackgroundColor)
                    }

                    UButton {
                        visible: !canvas.ocrMode && canvas.tool === AnnotationCanvas.Crop && canvas.hasSelection
                        compact: true
                        text: qsTr("Apply crop")
                        onClicked: canvas.applyCrop()
                    }

                    // --- OCR text selection ---
                    UButton {
                        visible: canvas.ocrMode
                        compact: true
                        iconName: "edit-copy"
                        text: canvas.ocrBusy ? qsTr("Recognizing…") : qsTr("Copy selection")
                        enabled: canvas.hasOcrSelection
                        onClicked: editorSession.copyOcrSelection()
                    }
                    UButton {
                        visible: canvas.ocrMode
                        compact: true
                        iconName: "draw-highlight"
                        text: qsTr("Highlight selection")
                        enabled: canvas.hasOcrSelection
                        onClicked: editorSession.highlightOcrSelection()
                    }
                    UButton {
                        visible: canvas.ocrMode
                        compact: true
                        // No "blur" icon - redaction paints an OPAQUE black bar (a
                        // blur/mosaic of a password is recoverable); the wrong icon
                        // misrepresented the result.
                        iconName: "edit-delete"
                        text: qsTr("Redact selection")
                        enabled: canvas.hasOcrSelection
                        onClicked: editorSession.redactOcrSelection()
                    }
                    // Pattern redaction: the point is to black out the few secrets
                    // in a shot, not all of its text (Select all + Redact selection
                    // already does that, and leaves an unreadable image). The
                    // patterns are regexes and are NOT translated - only the labels.
                    UMenuButton {
                        visible: canvas.ocrMode
                        height: 34
                        text: qsTr("Auto-redact")
                        enabled: !canvas.ocrBusy
                        actions: [
                            { label: qsTr("E-mail addresses"), iconName: "edit-delete",
                              trigger: function () {
                                  editorSession.redactTextMatching("[\\w.%+-]+@[\\w.-]+\\.[A-Za-z]{2,}")
                              } },
                            { label: qsTr("IP addresses"), iconName: "edit-delete",
                              trigger: function () {
                                  editorSession.redactTextMatching("\\b\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\b")
                              } },
                            { label: qsTr("Long numbers"), iconName: "edit-delete",
                              trigger: function () {
                                  editorSession.redactTextMatching("\\d[\\d -]{5,}\\d")
                              } }
                        ]
                    }
                    UButton {
                        visible: canvas.ocrMode
                        compact: true
                        text: qsTr("Select all"); variant: "tonal"
                        enabled: !canvas.ocrBusy
                        onClicked: canvas.ocrSelectAll()
                    }
                    UButton {
                        visible: canvas.ocrMode
                        compact: true
                        iconName: "close"; text: qsTr("Done"); variant: "ghost"
                        onClicked: canvas.clearOcrMode()
                    }
                }
            }

            // OCR mode hint - persistent on-canvas guidance so the user isn't
            // left with a dimmed image and a greyed "Copy selection" button.
            Text {
                visible: canvas.ocrMode && !canvas.ocrBusy && !canvas.hasOcrSelection
                anchors.horizontalCenter: parent.horizontalCenter
                y: ctxPill.y + ctxPill.height + Theme.spacingS
                text: qsTr("Click a line · double-click a word · drag for letters")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontS
            }

            // View zoom, the same steps as the wheel / Ctrl+± / Ctrl+0 for
            // anyone who never finds those. It scales the view only: the
            // image, the annotations and the export are untouched. The
            // percentage has a fixed width so "25%" -> "100%" never shifts
            // the + button out from under a pointer clicking it repeatedly.
            Pill {
                id: zoomPill
                x: Theme.spacingM
                y: stage.height - Theme.spacingM - height
                width: zoomRow.width + 2 * Theme.spacingS
                height: editorWindow.pillHeight
                Accessible.role: Accessible.ToolBar
                Accessible.name: qsTr("Zoom")

                Row {
                    id: zoomRow
                    anchors.centerIn: parent
                    spacing: 2
                    UIconButton {
                        id: zoomOut
                        objectName: "editorZoomOut"
                        iconName: "minus"; iconSize: 16; width: 40; height: 40
                        tooltip: qsTr("Zoom out (Ctrl+-)")
                        enabled: canvasFlick.effectiveScale > canvasFlick.minZoom + 0.001
                        onClicked: canvasFlick.zoomBy(1 / 1.2)
                    }
                    Rectangle {
                        id: zoomPct
                        objectName: "editorZoomPct"
                        width: zoomPctMetrics.width + 2 * Theme.spacingS
                        height: 40
                        radius: Theme.radiusM
                        color: zoomPctMouse.containsMouse ? Theme.alpha(Theme.accent, 0.16) : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.animFast } }
                        function _activate() { canvasFlick.toggleZoom() }
                        TextMetrics { id: zoomPctMetrics; font: zoomPctText.font; text: "400%" }
                        Text {
                            id: zoomPctText
                            anchors.centerIn: parent
                            text: Math.round(canvasFlick.effectiveScale * 100) + "%"
                            color: canvasFlick.zoom > 0 ? Theme.textPrimary : Theme.textSecondary
                            font.pixelSize: Theme.fontS + 1
                            Accessible.ignored: true
                        }
                        MouseArea {
                            id: zoomPctMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: zoomPct._activate()
                        }
                        UHoverTip {
                            anchor: zoomPct
                            text: canvasFlick.zoom > 0 ? qsTr("Fit to window (Ctrl+0)") : qsTr("Actual size (100%)")
                            show: zoomPctMouse.containsMouse
                        }
                        activeFocusOnTab: true
                        Keys.onSpacePressed: (e) => UKeys.activate(e, zoomPct._activate)
                        Keys.onReturnPressed: (e) => UKeys.activate(e, zoomPct._activate)
                        Keys.onEnterPressed: (e) => UKeys.activate(e, zoomPct._activate)
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Zoom %1%").arg(Math.round(canvasFlick.effectiveScale * 100))
                        Accessible.description: canvasFlick.zoom > 0 ? qsTr("Fit to window (Ctrl+0)") : qsTr("Actual size (100%)")
                        Accessible.focusable: true
                        Accessible.onPressAction: zoomPct._activate()
                        UFocusRing {}
                    }
                    UIconButton {
                        id: zoomIn
                        objectName: "editorZoomIn"
                        iconName: "plus"; iconSize: 16; width: 40; height: 40
                        tooltip: qsTr("Zoom in (Ctrl++)")
                        enabled: canvasFlick.effectiveScale < canvasFlick.maxZoom - 0.001
                        onClicked: canvasFlick.zoomBy(1.2)
                    }
                    Rectangle {
                        visible: statusLabel.visible
                        width: 1; height: 30; color: Theme.divider
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    // Image size, or the last export's result. It gets what the
                    // bottom edge has left once both bars are placed, and drops
                    // out rather than shrink to a few letters.
                    Text {
                        id: statusLabel
                        readonly property real room: stage.width - 2 * Theme.spacingM
                            - (actionPill.visible ? actionPill.width + Theme.spacingM : 0)
                            - 2 * Theme.spacingS - zoomOut.width - zoomPct.width - zoomIn.width
                            - 1 - 4 * zoomRow.spacing
                        visible: room >= 80
                        width: Math.min(implicitWidth, room)
                        anchors.verticalCenter: parent.verticalCenter
                        leftPadding: Theme.spacingS
                        rightPadding: Theme.spacingS
                        text: editorSession.statusText !== ""
                              ? editorSession.statusText
                              : canvas.imageSize.width + " × " + canvas.imageSize.height + " px"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontS + 1
                        elide: Text.ElideMiddle
                    }
                }
            }

            // Primary exports stay labeled; the occasional OCR actions live
            // behind one "More" menu. Closing is the title bar's (and Esc's).
            // OCR mode brings its own actions up in the options bar, and
            // these, which act on the whole image, step aside until "Done".
            Pill {
                id: actionPill
                visible: !canvas.ocrMode
                x: stage.width - Theme.spacingM - width
                y: stage.height - Theme.spacingM - height
                width: actionRow.width + 2 * Theme.spacingS
                height: editorWindow.pillHeight
                Accessible.role: Accessible.ToolBar
                Accessible.name: qsTr("Editor actions")

                Row {
                    id: actionRow
                    anchors.centerIn: parent
                    spacing: Theme.spacingS

                    UButton {
                        iconName: "edit-copy"; text: qsTr("Copy"); variant: "tonal"
                        onClicked: { editorWindow.commitPendingText(); editorSession.copyToClipboard() }
                    }
                    USplitMenuButton {
                        iconName: "document-save"
                        text: editorSession.overwriteMode ? qsTr("Overwrite") : qsTr("Save")
                        tooltip: editorSession.overwriteMode ? qsTr("Overwrite (Ctrl+S)") : qsTr("Save (Ctrl+S)")
                        dropdownTooltip: qsTr("More save options")
                        onClicked: editorWindow.doSave()
                        actions: [
                            { label: qsTr("Save as…"), iconName: "document-save",
                              hint: "Ctrl+Shift+S",
                              trigger: function () {
                                  editorWindow.doSaveAs()
                              } },
                            { label: qsTr("Save as PNG"), iconName: "document-save",
                              separatorBefore: true,
                              hint: App.settings.imageFormat === "png" ? qsTr("Default") : "",
                              trigger: function () {
                                  editorWindow.commitPendingText()
                                  editorSession.saveAs("png")
                              } },
                            { label: qsTr("Save as JPEG"), iconName: "document-save",
                              hint: (App.settings.imageFormat === "jpg" || App.settings.imageFormat === "jpeg") ? qsTr("Default") : "",
                              trigger: function () {
                                  editorWindow.commitPendingText()
                                  editorSession.saveAs("jpg")
                              } },
                            { label: qsTr("Save as WebP"), iconName: "document-save",
                              hint: App.settings.imageFormat === "webp" ? qsTr("Default") : "",
                              trigger: function () {
                                  editorWindow.commitPendingText()
                                  editorSession.saveAs("webp")
                              } },
                            { label: qsTr("Save as GIF"), iconName: "document-save",
                              enabled: App.ffmpegAvailable,
                              hint: !App.ffmpegAvailable ? qsTr("Needs ffmpeg")
                                    : (App.settings.imageFormat === "gif" ? qsTr("Default") : ""),
                              trigger: function () {
                                  editorWindow.commitPendingText()
                                  editorSession.saveAs("gif")
                              } }
                        ]
                    }
                    UButton {
                        iconName: "document-send"; text: App.uploads.busy ? qsTr("Uploading…") : qsTr("Upload")
                        variant: "tonal"; enabled: !App.uploads.busy
                        onClicked: { editorWindow.commitPendingText(); editorSession.upload() }
                    }
                    Rectangle {
                        width: 1; height: 30; color: Theme.divider
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    // "More": the occasional text actions. OCR is always built in.
                    UMenuButton {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("More")
                        actions: [
                            { label: qsTr("Copy all text"), iconName: "ocr",
                              trigger: function () { editorSession.ocrCopyText() } },
                            { label: qsTr("Select text…"), iconName: "select",
                              trigger: function () { editorSession.startOcrPick() } }
                        ]
                    }
                }
            }
        }
    }
    UWindowResizer { active: !App.settings.useSystemDecoration }
}
