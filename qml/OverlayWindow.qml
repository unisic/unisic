import QtQuick
import QtQuick.Window
import QtQuick.Effects
import Unisic
import "components"

Window {
    id: overlayWindow
    flags: Qt.FramelessWindowHint | Qt.Window
    color: "black"
    visible: false

    Item {
        id: root
        anchors.fill: parent
        focus: true

        // Alignment guides only make sense while MANIPULATING the region — the
        // plain selection tool (create / move / resize the rectangle). With a
        // drawing tool active they are just visual noise over the stroke, and
        // over an auto-detected smart-pick object the highlight box already
        // shows the target.
        readonly property bool guidesUseful:
            App.settings.selectionGuides
            && canvas.tool === AnnotationCanvas.None

        // Non-KDE compositors (Mutter, sway) routinely deny requestActivate()
        // issued from a hotkey with no focused window — keyboard (Esc/Enter/
        // nudge) would go nowhere. Pointer presence is a legitimate activation
        // trigger, so claim focus when the cursor enters this screen's overlay.
        HoverHandler {
            onHoveredChanged: {
                // Never steal focus while ANY overlay window's text editor is
                // open — a stray pointer crossing to another monitor would
                // redirect keystrokes and make Escape cancel the whole session.
                if (hovered && !overlayWindow.active && !overlayController.textEditing)
                    overlayWindow.requestActivate()
            }
        }

        // Selection rect in item (screen) coordinates — reactive on the
        // canvas properties so bound positions follow the selection live.
        function selItem() {
            var s = canvas.renderScale
            var r = canvas.selectionRect
            return Qt.rect(r.x * s, r.y * s, r.width * s, r.height * s)
        }
        function toolbarX() {
            var pos = App.settings.overlayToolbarPosition
            var m = 12
            var sel = selItem()
            if (pos === "follow")
                return Math.max(8, Math.min(sel.x + sel.width / 2 - toolbar.width / 2, width - toolbar.width - 8))
            if (pos.indexOf("left") >= 0) return m
            if (pos.indexOf("right") >= 0) return width - toolbar.width - m
            return width / 2 - toolbar.width / 2
        }
        function toolbarY() {
            var pos = App.settings.overlayToolbarPosition
            var m = 12
            var sel = selItem()
            if (pos === "follow") {
                // Below the selection when there's room; otherwise FLIP ABOVE
                // it — the old bottom-clamp slid the toolbar over the region
                // being captured near the screen's bottom edge.
                var below = sel.y + sel.height + m
                if (below + toolbar.height <= height - 8)
                    return below
                var above = sel.y - toolbar.height - m
                if (above >= 8)
                    return above
                // Selection spans (almost) the full height: nowhere outside —
                // overlap its bottom edge as the least harmful spot.
                return height - toolbar.height - 8
            }
            if (pos.indexOf("top") >= 0) return m + 44
            if (pos.indexOf("bottom") >= 0) return height - toolbar.height - m
            return height / 2 - toolbar.height / 2
        }

        // ---- tool grouping (Shapes) — same model as the editor top bar ----
        readonly property var shapesTools: ToolCatalog.groupTools("shapes", "overlay", App.settings.hiddenTools)
        readonly property bool shapesActive: ToolCatalog.groupForEnum(canvas.tool) === "shapes"
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
            const picked = ToolCatalog.toolForShortcut(key, "overlay")
            if (!picked)
                return false
            canvas.tool = picked.tool
            if (picked.group)
                currentShapeId = picked.id
            return true
        }
        function mainRowModel() {
            var out = []
            var seen = {}
            var ts = ToolCatalog.visibleFor("overlay", App.settings.hiddenTools)
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

        // Commit the in-place text box. Shared by TextInput.onAccepted and the
        // Enter branch below, so a confirm works whether keyboard focus landed
        // on the text field or stayed on the overlay root (Wayland activation
        // is unreliable for a hotkey-spawned frameless window).
        function commitTextBox() {
            if (textEditor.editingExisting)
                canvas.commitTextEdit(textField.text)
            else
                canvas.commitText(textEditor.imgX, textEditor.imgY, textField.text)
            textEditor.visible = false
            textEditor.editingExisting = false
            root.forceActiveFocus()
        }
        function closeTextBox() {
            textEditor.visible = false
            textEditor.editingExisting = false
            root.forceActiveFocus()
        }

        Keys.onPressed: (e) => {
            // While the text box is open, Ctrl+Enter CONFIRMS the text (plain
            // Enter types a new line — the field is multi-line now), Escape
            // closes the box (never cancels the session), and every other key
            // is left for the text field to type.
            if (textEditor.visible) {
                if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter)
                        && (e.modifiers & Qt.ControlModifier)) {
                    root.commitTextBox()
                    e.accepted = true
                } else if (e.key === Qt.Key_Escape) {
                    root.closeTextBox()
                    e.accepted = true
                } else {
                    e.accepted = false // let the text field type it
                }
                return
            }
            // Screen colour-pick in progress: Escape backs out of picking and
            // reopens the popup, without cancelling the whole capture.
            if (canvas.colorPicking) {
                if (e.key === Qt.Key_Escape) {
                    canvas.colorPicking = false
                    if (root.pendingColorPopup) {
                        root.pendingColorPopup.open()
                        root.pendingColorPopup = null
                    }
                    e.accepted = true
                }
                return
            }
            // Edit tool with a shape selected: Delete removes it, arrows nudge
            // it, Escape deselects (never cancels the capture on the first press).
            if (canvas.hasAnnotSelection) {
                if (e.key === Qt.Key_Delete || e.key === Qt.Key_Backspace) {
                    canvas.removeSelectedAnnot(); e.accepted = true; return
                }
                if (e.key === Qt.Key_Escape) {
                    canvas.clearAnnotSelection(); e.accepted = true; return
                }
                var st = (e.modifiers & Qt.ShiftModifier) ? 10 : 1
                if (e.key === Qt.Key_Left)  { canvas.nudgeSelectedAnnot(-st, 0); e.accepted = true; return }
                if (e.key === Qt.Key_Right) { canvas.nudgeSelectedAnnot(st, 0);  e.accepted = true; return }
                if (e.key === Qt.Key_Up)    { canvas.nudgeSelectedAnnot(0, -st); e.accepted = true; return }
                if (e.key === Qt.Key_Down)  { canvas.nudgeSelectedAnnot(0, st);  e.accepted = true; return }
            }
            if (e.key === Qt.Key_Escape) {
                overlayController.cancel()
            } else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter
                       || e.key === Qt.Key_Space) {
                // Space confirms too (only reachable here — the text-box branch
                // above returns first, so typing a space still types a space).
                overlayController.confirmFromWindow(overlayWindow)
            } else if (e.key === Qt.Key_C && (e.modifiers & Qt.ControlModifier)) {
                // Spectacle parity: Ctrl+C accepts the selection and copies the
                // result to the clipboard even when auto-copy is off. Screenshot
                // flow only — the GIF region picker keeps its Start button.
                if (annotationToolsEnabled)
                    overlayController.confirmAndCopy(overlayWindow)
            } else if (e.key === Qt.Key_A && (e.modifiers & Qt.ControlModifier)) {
                canvas.selectAll()
            } else if (e.key === Qt.Key_Z && (e.modifiers & Qt.ControlModifier)) {
                if (e.modifiers & Qt.ShiftModifier) canvas.redo()
                else canvas.undo()
            } else if (e.key === Qt.Key_Y && (e.modifiers & Qt.ControlModifier)) {
                canvas.redo()
            } else if (annotationToolsEnabled && canvas.hasSelection
                       && e.modifiers === Qt.NoModifier
                       && root.activateToolShortcut(e.key)) {
                // Only switch to a drawing tool once a region exists — the region
                // path needs tool==None to arm, so a tool press before selecting
                // would trap the user unable to make a selection (toolbar hidden).
            } else if (e.key === Qt.Key_Left)  { canvas.nudgeSelection(e.modifiers & Qt.ShiftModifier ? -10 : -1, 0) }
            else if (e.key === Qt.Key_Right)  { canvas.nudgeSelection(e.modifiers & Qt.ShiftModifier ? 10 : 1, 0) }
            else if (e.key === Qt.Key_Up)     { canvas.nudgeSelection(0, e.modifiers & Qt.ShiftModifier ? -10 : -1) }
            else if (e.key === Qt.Key_Down)   { canvas.nudgeSelection(0, e.modifiers & Qt.ShiftModifier ? 10 : 1) }
            e.accepted = true
        }

        // In-scene picker, NOT QtQuick.Dialogs' ColorDialog: that is a separate
        // top-level window, and under this overlay's layer-shell surface (with
        // an exclusive keyboard grab) it never receives input — it froze the
        // whole capture screen. UColorPopup is a Popup in the same scene.
        // Which popup asked to sample a colour from the screen — reopened with
        // the sampled pixel once the canvas reports it.
        property var pendingColorPopup: null

        UColorPopup {
            id: overlayColorDialog
            onPicked: (c) => canvas.strokeColor = c
            onRequestScreenPick: { root.pendingColorPopup = overlayColorDialog; canvas.colorPicking = true }
        }
        UColorPopup {
            id: overlayFillDialog
            showAlpha: true
            onPicked: (c) => { canvas.shapeFillColor = c; canvas.shapeFillEnabled = true }
            onRequestScreenPick: { root.pendingColorPopup = overlayFillDialog; canvas.colorPicking = true }
        }
        UColorPopup {
            id: overlayOutlineDialog
            onPicked: (c) => { canvas.textOutlineColor = c; canvas.textOutline = true }
            onRequestScreenPick: { root.pendingColorPopup = overlayOutlineDialog; canvas.colorPicking = true }
        }
        UColorPopup {
            id: overlayTextBgDialog
            showAlpha: true
            onPicked: (c) => { canvas.textBackgroundColor = c; canvas.textBackground = true }
            onRequestScreenPick: { root.pendingColorPopup = overlayTextBgDialog; canvas.colorPicking = true }
        }

        AnnotationCanvas {
            id: canvas
            objectName: "overlayCanvas"
            anchors.fill: parent
            selectionMode: true
            tool: AnnotationCanvas.None
            onCopyRequested: {
                if (annotationToolsEnabled)
                    overlayController.confirmAndCopy(overlayWindow)
            }
            // Capture on release: screenshot flow only — the GIF region picker
            // (annotationToolsEnabled false) keeps its explicit Start button.
            confirmOnRelease: App.settings.captureOnRelease && annotationToolsEnabled
            // Selection chrome follows the selected app theme (was fixed purple).
            uiAccent: Theme.accent
            uiScrim: Theme.primary
            // Pixel loupe while picking the region. The zoom is
            // seeded once and written back (Ctrl+scroll edits it live) — a
            // two-way binding would fight the C++ setter.
            pixelLoupe: App.settings.pixelLoupe
            onPixelLoupeZoomChanged: App.settings.pixelLoupeZoom = pixelLoupeZoom
            Component.onCompleted: {
                pixelLoupeZoom = App.settings.pixelLoupeZoom
                strokeColor = App.settings.editorStrokeColor
                strokeWidth = App.settings.editorStrokeWidth
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
            // A selected placed shape (Edit tool) is being restyled — don't
            // overwrite the saved "next shape" defaults. Preferences can pin
            // the saved defaults entirely (editorResetColors/editorResetTools):
            // changes then apply to this overlay session only.
            readonly property bool persistColors: !hasAnnotSelection && !App.settings.editorResetColors
            readonly property bool persistTools: !hasAnnotSelection && !App.settings.editorResetTools
            onStrokeColorChanged: if (!strokeColorIsAuto && persistColors) App.settings.editorStrokeColor = String(strokeColor)
            onStrokeWidthChanged: if (persistTools) App.settings.editorStrokeWidth = strokeWidth
            onFontSizeChanged: if (persistTools) App.settings.editorFontSize = fontSize
            onStepSizeChanged: if (persistTools) App.settings.editorStepSize = stepSize
            onShapeFillColorChanged: if (persistColors) App.settings.editorFillColor = shapeFillColor
            onShapeFillEnabledChanged: if (persistTools) App.settings.editorFillEnabled = shapeFillEnabled
            onFontFamilyChanged: if (persistTools) App.settings.editorFontFamily = fontFamily
            onFontBoldChanged: if (persistTools) App.settings.editorFontBold = fontBold
            onFontItalicChanged: if (persistTools) App.settings.editorFontItalic = fontItalic
            onFontUnderlineChanged: if (persistTools) App.settings.editorFontUnderline = fontUnderline
            onTextOutlineChanged: if (persistTools) App.settings.editorTextOutline = textOutline
            onTextOutlineColorChanged: if (persistColors) App.settings.editorTextOutlineColor = String(textOutlineColor)
            onTextBackgroundChanged: if (persistTools) App.settings.editorTextBackground = textBackground
            onTextBackgroundColorChanged: if (persistColors) App.settings.editorTextBgColor = String(textBackgroundColor)
            onSelectionConfirmed: overlayController.confirmFromWindow(overlayWindow)
            // Screen eyedropper result → reopen the popup that requested it,
            // seeded with the sampled colour.
            onColorPicked: (c) => {
                if (root.pendingColorPopup) {
                    root.pendingColorPopup.openWith(c)
                    root.pendingColorPopup = null
                }
            }
            onTextRequested: (x, y) => {
                textEditor.editingExisting = false
                textEditor.imgX = x
                textEditor.imgY = y
                textEditor.visible = true
                textField.text = ""
                textField.forceActiveFocus()
            }
            onTextEditRequested: (x, y, t) => {
                textEditor.editingExisting = true
                textEditor.imgX = x
                textEditor.imgY = y
                textEditor.visible = true
                textField.text = t
                textField.forceActiveFocus()
                textField.selectAll()
            }

            // Crosshair guides from the cursor to the screen edges. The handler
            // lives INSIDE the canvas: the canvas item receives the pointer, so a
            // sibling handler on root would report hovered only where the canvas
            // isn't (e.g. over the toolbar) — the exact "only shows over toolbar"
            // bug. As a child it tracks the pointer across the whole overlay.
            HoverHandler {
                id: guideHover
                enabled: root.guidesUseful
            }
        }

        // Each guide: a dark underlay + bright accent core, so the line stays
        // legible over both the dimmed backdrop and the bright selected region.
        // Position comes from canvas.hoverPoint (C++), which — unlike a QML
        // HoverHandler — keeps updating while the selection is being dragged.
        Item { // vertical guide
            visible: root.guidesUseful && guideHover.hovered
            x: Math.round(canvas.hoverPoint.x) - 1
            y: 0; width: 3; height: parent.height
            Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.35 }
            Rectangle {
                width: 1; height: parent.height
                anchors.horizontalCenter: parent.horizontalCenter
                color: Theme.accent; opacity: 0.95
            }
        }
        Item { // horizontal guide
            visible: root.guidesUseful && guideHover.hovered
            x: 0; y: Math.round(canvas.hoverPoint.y) - 1
            width: parent.width; height: 3
            Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.35 }
            Rectangle {
                width: parent.width; height: 1
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.accent; opacity: 0.95
            }
        }

        // Dimension readout — follows the selection
        Rectangle {
            visible: canvas.hasSelection
            x: Math.min(root.selItem().x, parent.width - width - 8)
            y: Math.max(4, root.selItem().y - height - 10)
            width: dimText.implicitWidth + 22
            height: 28
            radius: 14
            color: "#000000"
            opacity: 0.8
            Text {
                id: dimText
                anchors.centerIn: parent
                text: Math.round(canvas.selectionRect.width) + " × " + Math.round(canvas.selectionRect.height)
                color: "#FFFFFF"
                font.pixelSize: 12
                font.family: "monospace"
            }
        }

        // Hint bar (before first selection)
        Rectangle {
            visible: !canvas.hasSelection
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 42
            width: hintText.implicitWidth + 40
            height: 40
            radius: 20
            color: Theme.primary
            opacity: 0.92
            border.width: 1
            border.color: Theme.divider
            Text {
                id: hintText
                anchors.centerIn: parent
                text: {
                    const drag = qsTr("Drag to select")
                    return annotationToolsEnabled
                           ? drag + qsTr(" · Ctrl+drag to move · annotate with the toolbar · Space/Enter or double-click to capture · Esc to cancel")
                           : drag + qsTr(" · Ctrl+drag to move · Space/Enter to start · Esc to cancel")
                }
                color: Theme.textPrimary
                font.pixelSize: Theme.fontS + 1
            }
        }

        // Floating toolbar (position configurable; default follows the selection).
        // Main row: tools (with the Shapes group chip) + capture/close. Sub-row:
        // the active group's tools and the active tool's property controls —
        // contextual, so the pill stays compact with the plain selection tool.
        Rectangle {
            id: toolbar
            // Capture-on-release confirms the moment the drag ends — the
            // toolbar would only flash mid-drag, so don't show it at all.
            visible: canvas.hasSelection && !canvas.confirmOnRelease
            x: root.toolbarX()
            y: root.toolbarY()
            width: toolColumn.implicitWidth + 24
            height: toolColumn.implicitHeight + 14
            radius: 27
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.lighter(Theme.primary, 1.25) }
                GradientStop { position: 1.0; color: Theme.primary }
            }
            opacity: 0.97
            border.width: 1
            border.color: Theme.divider
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true; shadowColor: Theme.shadow
                shadowBlur: 1.0; shadowVerticalOffset: 5; shadowOpacity: 0.6
            }

            Column {
                id: toolColumn
                anchors.centerIn: parent
                spacing: 4

                Row {
                    id: toolRow
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 5

                    Row {
                        visible: annotationToolsEnabled
                        spacing: 5

                        Repeater {
                            model: root.mainRowModel()
                            delegate: ToolChip {
                                iconName: modelData.kind === "group"
                                          ? modelData.group.iconName
                                          : ToolCatalog.toolIconName(modelData.tool, App.settings.editorIconStyle, App.settings.editorToolIcons)
                                iconStyle: modelData.kind === "group" ? "custom" : App.settings.editorIconStyle
                                label: modelData.kind === "group"
                                       ? modelData.group.label
                                       : ToolCatalog.labelWithShortcut(modelData.tool)
                                active: modelData.kind === "group"
                                        ? root.shapesActive
                                        : canvas.tool === modelData.tool.tool
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: modelData.kind === "group"
                                           ? root.toggleShapesGroup()
                                           : canvas.tool = modelData.tool.tool
                            }
                        }
                        ToolChip { iconName: "edit-undo"; label: qsTr("Undo"); enabled: canvas.canUndo; anchors.verticalCenter: parent.verticalCenter; onClicked: canvas.undo() }
                        ToolChip { iconName: "edit-redo"; label: qsTr("Redo"); enabled: canvas.canRedo; anchors.verticalCenter: parent.verticalCenter; onClicked: canvas.redo() }

                        Rectangle { width: 1; height: 28; color: Theme.divider; anchors.verticalCenter: parent.verticalCenter }
                    }

                    UButton {
                        compact: true
                        iconName: "checkmark"
                        text: annotationToolsEnabled ? qsTr("Capture") : qsTr("Start")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: overlayController.confirmFromWindow(overlayWindow)
                    }
                    UIconButton {
                        iconName: "close"; iconSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: overlayController.cancel()
                    }
                }

                Row {
                    id: overlaySubBar
                    readonly property var ctxProps: ToolCatalog.contextProps(canvas.tool, canvas.selectedAnnotTool)
                    visible: annotationToolsEnabled
                             && (root.shapesActive || ctxProps.length > 0
                                 || (canvas.tool === AnnotationCanvas.EditShapes && canvas.hasAnnotSelection))
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 5

                    Repeater {
                        model: root.shapesActive ? root.shapesTools : []
                        delegate: ToolChip {
                            iconName: ToolCatalog.toolIconName(modelData, App.settings.editorIconStyle, App.settings.editorToolIcons)
                            iconStyle: App.settings.editorIconStyle
                            label: ToolCatalog.labelWithShortcut(modelData)
                            active: canvas.tool === modelData.tool
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: {
                                canvas.tool = modelData.tool
                                root.currentShapeId = modelData.id
                            }
                        }
                    }
                    ToolChip {
                        visible: canvas.hasAnnotSelection
                        iconName: "edit-delete"
                        label: qsTr("Delete shape")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: canvas.removeSelectedAnnot()
                    }
                    Rectangle {
                        visible: root.shapesActive
                                 || (canvas.tool === AnnotationCanvas.EditShapes && canvas.hasAnnotSelection)
                        width: 1; height: 28; color: Theme.divider
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ToolPropsBar {
                        canvas: canvas
                        props: overlaySubBar.ctxProps
                        anchors.verticalCenter: parent.verticalCenter
                        onStrokePickerRequested: overlayColorDialog.openWith(canvas.strokeColor)
                        onFillPickerRequested: overlayFillDialog.openWith(canvas.shapeFillColor)
                        onTextOutlinePickerRequested: overlayOutlineDialog.openWith(canvas.textOutlineColor)
                        onTextBackgroundPickerRequested: overlayTextBgDialog.openWith(canvas.textBackgroundColor)
                    }
                }
            }
        }

        // In-place text entry for the Text tool. FocusScope so the TextInput
        // reliably owns the keyboard while open (a plain Item left focus
        // ambiguous, which is how Enter reached the root capture handler).
        FocusScope {
            id: textEditor
            property real imgX: 0
            property real imgY: 0
            property bool editingExisting: false
            property alias text: textField.text
            visible: false
            onVisibleChanged: {
                overlayController.textEditing = visible
                if (visible) textField.forceActiveFocus()
            }
            x: Math.min(imgX * canvas.renderScale, root.width - width - 8)
            y: Math.min(imgY * canvas.renderScale, root.height - height - 8)
            width: 360
            height: Math.max(40, textField.implicitHeight + 16)
            z: 300

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusS
                color: Qt.rgba(0, 0, 0, 0.6)
                border.width: 1
                border.color: Theme.accent
            }
            // TextEdit (multi-line): Enter types a new line, Ctrl+Enter commits.
            TextEdit {
                id: textField
                anchors.fill: parent
                anchors.margins: 8
                focus: true
                color: canvas.strokeColor
                font.family: canvas.fontFamily === "" ? Qt.application.font.family : canvas.fontFamily
                font.pixelSize: canvas.fontSize * canvas.renderScale
                font.bold: canvas.fontBold
                font.italic: canvas.fontItalic
                font.underline: canvas.fontUnderline
                // MUST accept Ctrl+Enter/Escape here so they do NOT bubble to
                // the overlay root: commitTextBox() sets textEditor.visible =
                // false, and the root handler — re-checking visibility AFTER
                // that — would then fall through to confirmFromWindow and fire
                // the capture. Accepting stops that propagation. (onAccepted
                // alone did not consume the key event, which was the bug.)
                Keys.onPressed: (e) => {
                    if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter)
                            && (e.modifiers & Qt.ControlModifier)) {
                        root.commitTextBox()
                        e.accepted = true
                    } else if (e.key === Qt.Key_Escape) {
                        root.closeTextBox()
                        e.accepted = true
                    }
                }
            }
            Text {
                visible: textField.text === ""
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 8
                text: qsTr("Text… (Ctrl+Enter finishes)")
                color: Theme.textTertiary
                font.pixelSize: canvas.fontSize * canvas.renderScale
            }
        }
    }
}
