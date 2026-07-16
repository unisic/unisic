import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Dialogs
import Unisic
import "components"

Window {
    id: editorWindow
    // Track the image size, but never below the minimum that keeps every
    // toolbar control visible, and never above the work area — oversized
    // images get a fit-scaled (and zoomable/pannable) canvas instead.
    width: Math.min(Math.max(minimumWidth, canvas.imageSize.width / Screen.devicePixelRatio + 96),
                    Screen.width * 0.9)
    height: Math.min(Math.max(minimumHeight,
                              canvas.imageSize.height / Screen.devicePixelRatio
                              + reservedTopBar + reservedBottomBar + 88 + chromeTop),
                     Screen.height * 0.9)
    minimumWidth: 820
    minimumHeight: 480
    visible: true
    title: editorSession.overwriteMode ? qsTr("Unisic Editor (editing saved image)")
                                       : qsTr("Unisic Editor")
    color: Theme.background
    // Same decoration policy as the main window: the stylized frameless title
    // bar unless the user opted into system decorations (Appearance).
    flags: App.settings.useSystemDecoration
           ? Qt.Window
           : (Qt.Window | Qt.FramelessWindowHint)
    readonly property int chromeTop: App.settings.useSystemDecoration ? 0 : 38
    // Constant chrome allowance for the window-size math, sized for the worst
    // case (2 toolbar rows: ToolChip = 40px each + Column spacing + topBar's 18px
    // padding). Reserving the 2-row height means toggling the Shapes sub-bar
    // never resizes the window — canvasFrame (topBar.bottom..bottomBar.top)
    // absorbs the delta instead of the whole Window growing.
    readonly property int reservedTopBar: 2 * 40 + Theme.spacingS + 18   // = 104
    readonly property int reservedBottomBar: 64                          // == bottomBar.height

    Component.onCompleted: {
        editorSession.bindCanvas(canvas)
    }

    // True once the user has taken any export action (save/copy/upload/OCR)
    // — statusText is only ever set by those. Used to skip the
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
    // only becomes an annotation on Enter — commit it before any export so the
    // result is exactly what's rendered (empty text is a safe no-op).
    function commitPendingText() {
        if (editorTextInput.visible) {
            if (editorTextInput.editingExisting)
                canvas.commitTextEdit(editorTextField.text)
            else
                canvas.commitText(editorTextInput.imgX, editorTextInput.imgY, editorTextField.text)
            editorTextInput.visible = false
            editorTextInput.editingExisting = false
            shortcutScope.forceActiveFocus()
        }
    }

    // Editing an existing capture (from history) overwrites the original file, so
    // confirm first; a normal capture saves straight to a fresh file.
    function doSave() {
        commitPendingText()
        if (editorSession.overwriteMode) overwriteConfirm.open()
        else editorSession.save()
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
    // modal window which greyed the whole screen — impossible to judge the
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
            else if (!canvas.ocrMode && e.modifiers === Qt.NoModifier
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
            else return
            e.accepted = true
        }

        // ---------- custom title bar (frameless decoration) ----------
        Rectangle {
            id: editorTitleBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: editorWindow.chromeTop
            visible: !App.settings.useSystemDecoration
            z: 20
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.lighter(Theme.primary, 1.12) }
                GradientStop { position: 1.0; color: Theme.primary }
            }
            // Deferred startSystemMove past a drag threshold — same pattern
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
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingL
                anchors.verticalCenter: parent.verticalCenter
                text: editorWindow.title
                color: Theme.textPrimary
                font.pixelSize: Theme.fontM
                font.weight: Font.DemiBold
            }
            Row {
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

        // ---------- top toolbar ----------
        // Main row: ungrouped tools + one chip per tool group (Shapes) + undo/
        // redo. Sub-bar below: the active group's tools and/or the properties
        // relevant to the active tool (ToolPropsBar) — shown contextually
        // instead of the old always-visible flat properties row. Each row is a
        // Flow so a narrow window wraps instead of overlapping.
        Rectangle {
            id: topBar
            anchors.top: parent.top
            anchors.topMargin: editorWindow.chromeTop + Theme.spacingM
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spacingM
            height: barColumn.implicitHeight + 18
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
                shadowBlur: 0.8; shadowVerticalOffset: 4; shadowOpacity: 0.5
            }

            Column {
                id: barColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacingM
                anchors.rightMargin: Theme.spacingM
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingS

                Flow {
                    width: parent.width
                    spacing: Theme.spacingL

                    Row {
                        spacing: 4

                        Repeater {
                            model: editorWindow.mainRowModel()
                            delegate: ToolChip {
                                iconName: modelData.kind === "group"
                                          ? modelData.group.iconName
                                          : ToolCatalog.toolIconName(modelData.tool, App.settings.editorIconStyle, App.settings.editorToolIcons)
                                // The group glyph has no freedesktop equivalent — always bundled.
                                iconStyle: modelData.kind === "group" ? "custom" : App.settings.editorIconStyle
                                label: modelData.kind === "group"
                                       ? modelData.group.label
                                       : ToolCatalog.labelWithShortcut(modelData.tool)
                                active: modelData.kind === "group"
                                        ? editorWindow.shapesActive
                                        : canvas.tool === modelData.tool.tool
                                onClicked: modelData.kind === "group"
                                           ? editorWindow.toggleShapesGroup()
                                           : canvas.tool = modelData.tool.tool
                            }
                        }

                        Rectangle { width: 1; height: 30; color: Theme.divider; anchors.verticalCenter: parent.verticalCenter }

                        ToolChip { iconName: "edit-undo"; label: qsTr("Undo"); enabled: canvas.canUndo; onClicked: canvas.undo() }
                        ToolChip { iconName: "edit-redo"; label: qsTr("Redo"); enabled: canvas.canRedo; onClicked: canvas.redo() }

                        UButton {
                            visible: canvas.tool === AnnotationCanvas.Crop && canvas.hasSelection
                            compact: true
                            text: qsTr("Apply crop")
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: canvas.applyCrop()
                        }
                    }
                }

                // The sub-bar slot is ALWAYS reserved at one row height: the
                // old visible-toggle collapsed the row, so every tool click
                // shifted the toolbar edge and the whole canvas below it —
                // the content fades in place instead.
                Item {
                    width: parent.width
                    height: Math.max(40, editorSubBar.implicitHeight)

                    Flow {
                    id: editorSubBar
                    width: parent.width
                    spacing: Theme.spacingL
                    // Props follow the active tool, or the SELECTED shape when
                    // the Edit tool is active.
                    readonly property var ctxProps: ToolCatalog.contextProps(canvas.tool, canvas.selectedAnnotTool)
                    readonly property bool active: editorWindow.shapesActive || ctxProps.length > 0
                             || (canvas.tool === AnnotationCanvas.EditShapes && canvas.hasAnnotSelection)
                    opacity: active ? 1 : 0
                    visible: opacity > 0
                    Behavior on opacity { NumberAnimation { duration: Theme.animFast } }

                    Row {
                        spacing: 6

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

                        // Delete affordance for the selected shape — shown for
                        // any tool, since a plain click selects shapes now.
                        ToolChip {
                            visible: canvas.hasAnnotSelection
                            iconName: "edit-delete"
                            label: qsTr("Delete shape")
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: canvas.removeSelectedAnnot()
                        }

                        Rectangle {
                            visible: editorWindow.shapesActive || canvas.hasAnnotSelection
                            width: 1; height: 30; color: Theme.divider
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        ToolPropsBar {
                            canvas: canvas
                            props: editorSubBar.ctxProps
                            recentColors: editorWindow.recentList()
                            anchors.verticalCenter: parent.verticalCenter
                            onStrokePickerRequested: strokePopup.openWith(canvas.strokeColor)
                            onFillPickerRequested: fillPopup.openWith(canvas.shapeFillColor)
                            onTextOutlinePickerRequested: outlinePopup.openWith(canvas.textOutlineColor)
                            onTextBackgroundPickerRequested: textBgPopup.openWith(canvas.textBackgroundColor)
                        }
                    }
                    }
                }
            }
        }

        // ---------- canvas area ----------
        Rectangle {
            id: canvasFrame
            anchors.top: topBar.bottom
            anchors.bottom: bottomBar.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spacingM
            radius: Theme.radiusL
            color: Theme.primary
            border.width: 1
            border.color: Theme.divider
            clip: true

            Flickable {
                id: canvasFlick
                anchors.fill: parent
                anchors.margins: Theme.spacingM
                clip: true
                // Drawing owns the left button — panning happens via the wheel,
                // scrollbars and keyboard, never by dragging the canvas.
                interactive: false
                contentWidth: Math.max(width, canvas.width)
                contentHeight: Math.max(height, canvas.height)
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                // 0 = fit-to-window (never upscales); anything else is an
                // absolute zoom factor set via Ctrl+wheel / Ctrl+± / Ctrl+0.
                property real zoom: 0
                readonly property real fitScale: canvas.imageSize.width > 0
                    ? Math.min(width / canvas.imageSize.width, height / canvas.imageSize.height, 1)
                    : 1
                readonly property real effectiveScale: zoom > 0 ? zoom : fitScale
                // QQuickPaintedItem's texture follows the item size × DPR:
                // cap the zoomed item so the device-pixel texture stays under
                // common GPU limits (blank canvas otherwise) and strokes don't
                // repaint hundreds of MB. Fit is always allowed — a fitted
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

                function clampPan() {
                    contentX = Math.max(0, Math.min(contentWidth - width, contentX))
                    contentY = Math.max(0, Math.min(contentHeight - height, contentY))
                }
                // Zoom anchored to a viewport point: whatever image pixel sits
                // under (ax, ay) stays there across the step. Ctrl+wheel passes
                // the cursor; the keyboard shortcuts default to the center.
                function zoomBy(f, ax, ay) {
                    if (ax === undefined) { ax = width / 2; ay = height / 2 }
                    var cx = canvas.width > 0 ? (contentX + ax - canvas.x) / canvas.width : 0.5
                    var cy = canvas.height > 0 ? (contentY + ay - canvas.y) / canvas.height : 0.5
                    zoom = Math.max(minZoom, Math.min(maxZoom, effectiveScale * f))
                    contentX = canvas.x + cx * canvas.width - ax
                    contentY = canvas.y + cy * canvas.height - ay
                    clampPan()
                }

                WheelHandler {
                    acceptedModifiers: Qt.ControlModifier
                    onWheel: (ev) => canvasFlick.zoomBy(ev.angleDelta.y > 0 ? 1.2 : 1 / 1.2,
                                                        ev.x, ev.y)
                }
                WheelHandler {
                    acceptedModifiers: Qt.NoModifier
                    onWheel: (ev) => {
                        canvasFlick.contentY -= ev.angleDelta.y
                        canvasFlick.contentX -= ev.angleDelta.x
                        canvasFlick.clampPan()
                    }
                }
                WheelHandler {
                    acceptedModifiers: Qt.ShiftModifier
                    onWheel: (ev) => {
                        canvasFlick.contentX -= ev.angleDelta.y
                        canvasFlick.clampPan()
                    }
                }

                // Middle-button drag pans 1:1 (the canvas only claims the left
                // button, so the press falls through to this area). Coordinates
                // are mapped to the viewport — measuring in the moving content
                // space would feed the pan back into itself.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.MiddleButton
                    property real startCX: 0
                    property real startCY: 0
                    property real pressVX: 0
                    property real pressVY: 0
                    onPressed: (m) => {
                        startCX = canvasFlick.contentX
                        startCY = canvasFlick.contentY
                        const p = mapToItem(canvasFlick, m.x, m.y)
                        pressVX = p.x
                        pressVY = p.y
                    }
                    onPositionChanged: (m) => {
                        const p = mapToItem(canvasFlick, m.x, m.y)
                        canvasFlick.contentX = startCX - (p.x - pressVX)
                        canvasFlick.contentY = startCY - (p.y - pressVY)
                        canvasFlick.clampPan()
                    }
                }

                AnnotationCanvas {
                    id: canvas
                    width: imageSize.width * canvasFlick.effectiveScale
                    height: imageSize.height * canvasFlick.effectiveScale
                    onCopyRequested: {
                        editorWindow.commitPendingText()
                        editorSession.copyToClipboard()
                    }
                    x: Math.max(0, (canvasFlick.width - width) / 2)
                    y: Math.max(0, (canvasFlick.height - height) / 2)
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
                    // into the saved default — persist only real user picks.
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
                        editorWindow.commitPendingText()
                        editorTextInput.editingExisting = false
                        editorTextInput.imgX = x
                        editorTextInput.imgY = y
                        editorTextInput.visible = true
                        editorTextField.text = ""
                        editorTextField.forceActiveFocus()
                    }
                    // Double-clicking a placed Text shape (Edit tool) reopens
                    // the editor prefilled; commit replaces the shape's text.
                    onTextEditRequested: (x, y, t) => {
                        editorWindow.commitPendingText()
                        editorTextInput.editingExisting = true
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
                    property bool editingExisting: false
                    visible: false
                    x: canvas.x + imgX * canvas.renderScale
                    y: canvas.y + imgY * canvas.renderScale
                    // Scale with zoom, or the zoomed font gets clipped by a
                    // fixed-size box; grow with the typed lines.
                    width: 360 * Math.max(1, canvas.renderScale)
                    height: Math.max(40 * Math.max(1, canvas.renderScale),
                                     editorTextField.implicitHeight + 16)
                    z: 50
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusS
                        color: Qt.rgba(0, 0, 0, 0.6)
                        border.width: 1
                        border.color: Theme.accent
                    }
                    // TextEdit (multi-line): Enter = new line, Ctrl+Enter (or
                    // any export/click-away via commitPendingText) commits.
                    TextEdit {
                        id: editorTextField
                        anchors.fill: parent
                        anchors.margins: 8
                        color: canvas.strokeColor
                        font.family: canvas.fontFamily === "" ? Qt.application.font.family : canvas.fontFamily
                        font.pixelSize: Math.max(10, canvas.fontSize * canvas.renderScale)
                        font.bold: canvas.fontBold
                        font.italic: canvas.fontItalic
                        font.underline: canvas.fontUnderline
                        // Return focus to the shortcut scope, else Ctrl+Z/S/C/U
                        // and Escape stay dead after using the text tool.
                        Keys.onPressed: (e) => {
                            if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter)
                                    && (e.modifiers & Qt.ControlModifier)) {
                                editorWindow.commitPendingText()
                                e.accepted = true
                            } else if (e.key === Qt.Key_Escape) {
                                editorTextInput.visible = false
                                editorTextInput.editingExisting = false
                                shortcutScope.forceActiveFocus()
                                e.accepted = true
                            }
                        }
                    }
                    Text {
                        visible: editorTextField.text === ""
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 8
                        text: qsTr("Text… (Ctrl+Enter finishes)")
                        color: Theme.textTertiary
                        font.pixelSize: Math.max(10, canvas.fontSize * canvas.renderScale)
                    }
                }
            }

            // OCR mode hint — persistent on-canvas guidance so the user isn't
            // left with a dimmed image and a greyed "Copy selection" button.
            Text {
                visible: canvas.ocrMode && !canvas.ocrBusy && !canvas.hasOcrSelection
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: Theme.spacingM
                z: 60
                text: qsTr("Click a line · double-click a word · drag for letters")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontS
            }
        }

        // ---------- bottom action bar ----------
        Rectangle {
            id: bottomBar
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spacingM
            height: 64
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
                shadowBlur: 0.8; shadowVerticalOffset: -3; shadowOpacity: 0.4
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingL
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: actionRow.left
                anchors.rightMargin: Theme.spacingM
                text: editorSession.statusText !== ""
                      ? editorSession.statusText
                      : canvas.imageSize.width + " × " + canvas.imageSize.height + " px · "
                        + Math.round(canvasFlick.effectiveScale * 100) + "%"
                        + (canvasFlick.zoom > 0 ? "" : qsTr(" (fit)"))
                color: Theme.textSecondary
                font.pixelSize: Theme.fontS + 1
                elide: Text.ElideMiddle
            }

            // Primary exports stay labeled; the occasional OCR actions
            // live behind one "More" menu, and Close is an icon — so the row
            // fits the fixed bar at the minimum window width with no scrolling
            // and never grows the window (it is right-anchored; the status text
            // elides into whatever space is left).
            Row {
                id: actionRow
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingM
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingS

                // --- normal (non-OCR) mode ---
                UButton {
                    visible: !canvas.ocrMode
                    iconName: "edit-copy"; text: qsTr("Copy"); variant: "tonal"
                    onClicked: { editorWindow.commitPendingText(); editorSession.copyToClipboard() }
                }
                UButton {
                    visible: !canvas.ocrMode
                    iconName: "document-save"
                    text: editorSession.overwriteMode ? qsTr("Overwrite") : qsTr("Save")
                    variant: "tonal"; onClicked: editorWindow.doSave()
                }
                UButton {
                    visible: !canvas.ocrMode
                    iconName: "document-send"; text: App.uploads.busy ? qsTr("Uploading…") : qsTr("Upload")
                    variant: "tonal"; enabled: !App.uploads.busy
                    onClicked: { editorWindow.commitPendingText(); editorSession.upload() }
                }
                Rectangle {
                    visible: !canvas.ocrMode
                    width: 1; height: 30; color: Theme.divider
                    anchors.verticalCenter: parent.verticalCenter
                }
                // "More": the occasional text actions, greyed with a reason
                // when the build lacks the dependency (kept discoverable, not hidden).
                UMenuButton {
                    visible: !canvas.ocrMode
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("More")
                    actions: [
                        { label: qsTr("Copy all text"), iconName: "ocr",
                          enabled: App.ocrAvailable, hint: App.ocrAvailable ? "" : qsTr("Needs OCR"),
                          trigger: function () { editorSession.ocrCopyText() } },
                        { label: qsTr("Select text…"), iconName: "select",
                          enabled: App.ocrAvailable, hint: App.ocrAvailable ? "" : qsTr("Needs OCR"),
                          trigger: function () { editorSession.startOcrPick() } }
                    ]
                }
                UIconButton {
                    visible: !canvas.ocrMode
                    iconName: "close"; iconSize: 16; width: 38; height: 38
                    tooltip: qsTr("Close (Esc)")
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: editorWindow.close()
                }

                // --- OCR text-selection mode (swaps in) ---
                UButton {
                    visible: canvas.ocrMode
                    iconName: "edit-copy"
                    text: canvas.ocrBusy ? qsTr("Recognizing…") : qsTr("Copy selection")
                    enabled: canvas.hasOcrSelection
                    onClicked: editorSession.copyOcrSelection()
                }
                UButton {
                    visible: canvas.ocrMode
                    iconName: "draw-highlight"
                    text: qsTr("Highlight selection")
                    enabled: canvas.hasOcrSelection
                    onClicked: editorSession.highlightOcrSelection()
                }
                UButton {
                    visible: canvas.ocrMode
                    // No "blur" icon — redaction paints an OPAQUE black bar (a
                    // blur/mosaic of a password is recoverable); the wrong icon
                    // misrepresented the result.
                    iconName: "edit-delete"
                    text: qsTr("Redact selection")
                    enabled: canvas.hasOcrSelection
                    onClicked: editorSession.redactOcrSelection()
                }
                UButton {
                    visible: canvas.ocrMode
                    text: qsTr("Select all"); variant: "tonal"
                    enabled: !canvas.ocrBusy
                    onClicked: canvas.ocrSelectAll()
                }
                UButton {
                    visible: canvas.ocrMode
                    iconName: "close"; text: qsTr("Done"); variant: "ghost"
                    onClicked: canvas.clearOcrMode()
                }
            }
        }
    }
}
