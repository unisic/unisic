import QtQuick
import QtQuick.Window
import QtQuick.Effects
import Unisic
import Unisic.Kit
import "components"

Window {
    id: overlayWindow
    flags: Qt.FramelessWindowHint | Qt.Window
    color: Theme.mediaBase
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

        // Which capture mode opened this overlay (issue #98). All five freeze
        // the screen and draw the same chrome, so a mis-fired hotkey used to
        // announce itself only once the shot was already taken - or, worse, once
        // the recording had started. The WORD is the answer; the colour only
        // reinforces it, because colour alone says nothing on a greyscale screen
        // or to a colour-blind user.
        readonly property string modeLabel: {
            switch (overlayPurpose) {
            case "measure": return qsTr("Measure")
            case "ocr":     return qsTr("Read text")
            case "gif":     return qsTr("Record GIF")
            case "video":   return qsTr("Record video")
            }
            return qsTr("Screenshot")
        }
        readonly property string modeIcon: {
            switch (overlayPurpose) {
            case "measure": return "measure"
            case "ocr":     return "ocr"
            case "gif":     return "gif"
            case "video":   return "media-record"
            }
            return "region"
        }
        readonly property color modeColor: Theme.modeColor(overlayPurpose)
        // What confirming actually does, on the button AND in the hint: "Start"
        // on a screen that could equally be a GIF, a video or a text read
        // answers nothing. Measuring still exports an image, so it captures.
        readonly property string confirmLabel: {
            switch (overlayPurpose) {
            case "ocr":   return qsTr("Read text")
            case "gif":   return qsTr("Record GIF")
            case "video": return qsTr("Record video")
            }
            return qsTr("Capture")
        }
        // Preference: the badge and the coloured frame are the part that can be
        // turned off. The button, the hint and the spoken name are not - a label
        // that names the wrong action is a bug, not a matter of taste.
        readonly property bool showMode: App.settings.overlayModeBadge
        // "badge" (named pill at the top) or "icon" (one big translucent glyph
        // in the middle). The coloured frame is drawn for both - it is the
        // mode's colour, not one of the two ways of naming it.
        readonly property string modeStyle: App.settings.overlayModeStyle

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
        // How wide the pill's CONTENT may get: the screen less an 8 px margin
        // on each side and the pill's own 12 px padding on each side. Both
        // rows inside the pill wrap against this, so the pill can never be
        // wider than the screen it floats over - it used to come out 894 px
        // wide from the 16 tool chips alone, which is off the edge of anything
        // narrower than about 910 logical px.
        readonly property real maxBarWidth: Math.max(200, width - 40)

        // Every fixed toolbar position has to end up inside the screen too.
        // Only the "follow" branch used to clamp; left/right/centre computed a
        // raw coordinate that goes negative as soon as the pill is wider than
        // the screen, and the follow branch's own last-resort fallback can push
        // the pill off the top of a short screen.
        function clampX(v) { return Math.max(8, Math.min(v, width - toolbar.width - 8)) }
        function clampY(v) { return Math.max(8, Math.min(v, height - toolbar.height - 8)) }
        function toolbarX() {
            var pos = App.settings.overlayToolbarPosition
            var m = 12
            var sel = selItem()
            if (pos === "follow")
                return clampX(sel.x + sel.width / 2 - toolbar.width / 2)
            if (pos.indexOf("left") >= 0) return clampX(m)
            if (pos.indexOf("right") >= 0) return clampX(width - toolbar.width - m)
            return clampX(width / 2 - toolbar.width / 2)
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
                return clampY(height - toolbar.height - 8)
            }
            if (pos.indexOf("top") >= 0) return clampY(m + 44)
            if (pos.indexOf("bottom") >= 0) return clampY(height - toolbar.height - m)
            return clampY(height / 2 - toolbar.height / 2)
        }

        // Sum of a positioner's visible children plus its gaps: what the row
        // WOULD measure on a single line. A Flow's own implicitWidth is the
        // width of the already-wrapped content, so it cannot answer that.
        function naturalRowWidth(item) {
            var w = 0
            var n = 0
            for (var i = 0; i < item.children.length; ++i) {
                var c = item.children[i]
                if (!c || !c.visible)
                    continue
                var cw = c.implicitWidth > 0 ? c.implicitWidth : c.width
                // A Repeater is itself a zero-sized child of the positioner and
                // is skipped by the layout; counting it would add a phantom gap.
                if (cw <= 0)
                    continue
                w += cw
                ++n
            }
            return n > 0 ? w + item.spacing * (n - 1) : 0
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
        // Members of a group, spoken by the group chip so assistive tech can
        // say what pressing it opens. Same text as the editor's, resolved for
        // the "overlay" context, which hides a different set of tools.
        function groupMembers(groupId) {
            const ts = ToolCatalog.groupTools(groupId, "overlay", App.settings.hiddenTools)
            var out = []
            for (var i = 0; i < ts.length; ++i)
                out.push(ToolCatalog.labelWithShortcut(ts[i]))
            return out.join(", ")
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
            textEditor.inShape = false
            root.forceActiveFocus()
        }
        function closeTextBox() {
            // Puts a live-previewed bubble back to the text it had before this
            // edit, undo entry included.
            canvas.cancelTextEdit()
            textEditor.visible = false
            textEditor.editingExisting = false
            textEditor.inShape = false
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
                if (canvas.tool === AnnotationCanvas.Measure) {
                    // Ruler: Ctrl+C copies the measured DIMENSIONS (not the image)
                    // and leaves the overlay up so you can keep measuring.
                    var mt = canvas.measuresText(App.settings.measureCopyFormat)
                    if (mt !== "") { App.copyText(mt); App.showToast(qsTr("Measurements copied")) }
                    else App.showToast(qsTr("Nothing measured yet - Ctrl+drag to measure"))
                } else if (annotationToolsEnabled) {
                    // Spectacle parity: Ctrl+C accepts the selection and copies the
                    // result to the clipboard even when auto-copy is off. Screenshot
                    // flow only — the GIF region picker keeps its Start button.
                    overlayController.confirmAndCopy(overlayWindow)
                }
            } else if (e.key === Qt.Key_Tab && canvas.tool === AnnotationCanvas.Measure) {
                // Toggle the ruler between distance line and size box.
                canvas.measureMode = canvas.measureMode === 1 ? 0 : 1
            } else if (e.key === Qt.Key_A && (e.modifiers & Qt.ControlModifier)) {
                canvas.selectAll()
            } else if (e.key === Qt.Key_Z && (e.modifiers & Qt.ControlModifier)) {
                if (e.modifiers & Qt.ShiftModifier) canvas.redo()
                else canvas.undo()
            } else if (e.key === Qt.Key_Y && (e.modifiers & Qt.ControlModifier)) {
                canvas.redo()
            } else if (e.key === Qt.Key_W && UKeys.unmodified(e)
                       && overlayController.activeWindowKnown) {
                // Selects the window that was active when the overlay opened.
                // Above the tool letters on purpose: W is not one of them, but
                // this must work BEFORE a selection exists, which is exactly
                // what the tool branch below refuses to do.
                overlayController.selectActiveWindow()
            } else if (annotationToolsEnabled && canvas.hasSelection
                       && UKeys.unmodified(e)
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
            // The frozen screen plus the live selection: a graphic with no text
            // of its own, so assistive tech gets a name and the key vocabulary
            // rather than an anonymous unlabelled surface.
            Accessible.role: Accessible.Canvas
            Accessible.name: qsTr("Capture region, %1").arg(root.modeLabel)
            Accessible.description: qsTr("Drag to select. Space or Enter captures, Escape cancels, arrow keys nudge the selection.")
            selectionMode: true
            tool: AnnotationCanvas.None
            onCopyRequested: {
                if (canvas.tool === AnnotationCanvas.Measure) {
                    var mt = canvas.measuresText(App.settings.measureCopyFormat)
                    if (mt !== "") { App.copyText(mt); App.showToast(qsTr("Measurements copied")) }
                } else if (annotationToolsEnabled) {
                    overlayController.confirmAndCopy(overlayWindow)
                }
            }
            // Capture on release: screenshot flow only — the GIF region picker
            // (annotationToolsEnabled false) keeps its explicit Start button.
            confirmOnRelease: App.settings.captureOnRelease && annotationToolsEnabled
            // Bare click = full-screen capture: screenshot flow only too — a
            // stray click must not start a recording.
            clickSelectsAll: annotationToolsEnabled
            // Selection chrome follows the selected app theme (was fixed purple),
            // and takes the mode colour when the mode badge is on, so the
            // handles the user is dragging carry the same answer as the frame.
            uiAccent: root.showMode ? root.modeColor : Theme.accent
            uiScrim: Theme.primary
            // Pixel loupe while picking the region. The zoom is
            // seeded once and written back (scroll edits it live) — a
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
                textEditor.inShape = false
                textEditor.imgX = x
                textEditor.imgY = y
                textEditor.visible = true
                textField.text = ""
                textField.forceActiveFocus()
            }
            // A non-empty canvas.textEditRect means the string belongs INSIDE a
            // shape (a callout): the box then tracks the bubble's writable area
            // instead of floating at its corner, so typing is what you get.
            onTextEditRequested: (x, y, t) => {
                textEditor.editingExisting = true
                textEditor.inShape = canvas.textEditRect.width > 0
                textEditor.imgX = x
                textEditor.imgY = y
                textEditor.visible = true
                textField.text = t
                textField.forceActiveFocus()
                textField.selectAll()
            }

            // This is cursor feedback rather than an action control: gate it to
            // the canvas on the monitor that actually owns the pointer, or every
            // other monitor would retain a stale crosshair. The C++ hover point
            // below keeps tracking during drags, when hover events pause.
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
            Rectangle { anchors.fill: parent; color: Theme.alpha(Theme.mediaBase, 0.35) }
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
            Rectangle { anchors.fill: parent; color: Theme.alpha(Theme.mediaBase, 0.35) }
            Rectangle {
                width: parent.width; height: 1
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.accent; opacity: 0.95
            }
        }

        // Dimension readout - follows the selection. It sits on top of whatever
        // the user happens to have on screen, so it borrows the recording
        // badge's ink instead of a page token: same job, same guarantee that it
        // stays readable over arbitrary pixels. The badge colour carries its own
        // alpha, so the numbers themselves are not dimmed with it.
        Rectangle {
            visible: canvas.hasSelection
            x: Math.min(root.selItem().x, parent.width - width - 8)
            y: Math.max(4, root.selItem().y - height - 10)
            width: dimText.implicitWidth + 22
            height: 28
            radius: 14
            color: Theme.recBadgeBg
            Text {
                id: dimText
                anchors.centerIn: parent
                text: Math.round(canvas.selectionRect.width) + " × " + Math.round(canvas.selectionRect.height)
                color: Theme.recBadgeText
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
                    if (canvas.tool === AnnotationCanvas.Measure)
                        return qsTr("Drag to measure · Tab: distance/size · Ctrl+C copies the sizes · Esc to close")
                    const drag = qsTr("Drag to select")
                    // Only offered where KWin answered where the active window
                    // is; nowhere else can an app learn that.
                    const win = overlayController.activeWindowKnown
                              ? qsTr(" · W selects the active window") : ""
                    // The verb has to match the mode: the same sentence used to
                    // promise a capture whether it took a screenshot, read text
                    // or started recording.
                    if (overlayPurpose === "ocr")
                        return drag + win + qsTr(" · Ctrl+drag to move · Space/Enter reads the text · Esc to cancel")
                    if (overlayPurpose === "gif")
                        return drag + win + qsTr(" · Ctrl+drag to move · Space/Enter starts the GIF · Esc to cancel")
                    if (overlayPurpose === "video")
                        return drag + win + qsTr(" · Ctrl+drag to move · Space/Enter starts the video · Esc to cancel")
                    return annotationToolsEnabled
                           ? drag + win + qsTr(" · click for the whole screen · Ctrl+drag to move · annotate with the toolbar · Space/Enter or double-click to capture · Esc to cancel")
                           : drag + win + qsTr(" · Ctrl+drag to move · Space/Enter to start · Esc to cancel")
                }
                color: Theme.textPrimary
                font.pixelSize: Theme.fontS + 1
            }
        }

        // Mode frame: the screen edge in the mode colour. Peripheral vision
        // answers "which capture is this" wherever the pointer happens to be,
        // and it survives the first drag, which the hint bar above does not.
        // Built like RecordBorder's region frame - a contrast hairline on each
        // side of the coloured stroke - so a light mode colour stays visible
        // over a light desktop and a dark one over a dark desktop.
        Rectangle {
            visible: root.showMode
            anchors.fill: parent
            color: "transparent"
            border.width: 1
            border.color: Theme.recordFrameContrast
        }
        Rectangle {
            visible: root.showMode
            anchors.fill: parent
            anchors.margins: 1
            color: "transparent"
            border.width: 3
            border.color: root.modeColor
        }
        Rectangle {
            visible: root.showMode
            anchors.fill: parent
            anchors.margins: 4
            color: "transparent"
            border.width: 1
            border.color: Theme.recordFrameContrast
        }

        // Mode badge. It borrows the recording badge's ink for the same reason
        // the dimension readout does: it sits over arbitrary frozen pixels.
        // Never a tab stop and never spoken - the canvas already carries the
        // mode in its accessible name, and the overlay's keyboard grab means a
        // focusable chip here would swallow the Space that finishes the shot.
        Rectangle {
            visible: root.showMode && root.modeStyle !== "icon"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 8
            width: modeRow.implicitWidth + 24
            height: 28
            radius: 14
            color: Theme.recBadgeBg
            border.width: 1
            border.color: root.modeColor
            Accessible.ignored: true
            Row {
                id: modeRow
                anchors.centerIn: parent
                spacing: 6
                UIcon {
                    name: root.modeIcon
                    size: 13
                    color: root.modeColor
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: root.modeLabel
                    color: Theme.recBadgeText
                    font.pixelSize: 12
                    font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        // Mode glyph: the second way of naming the mode. One big translucent
        // mark in the middle of the screen instead of a pill at the top - it
        // reads at a glance without a strip of chrome across the desktop. The
        // selection is cut out of it, so the part of it you are framing over is
        // not drawn while the rest keeps naming the capture. (Nothing here is
        // captured either way - the image comes from the frozen screen, not
        // from this window.)
        UModeGlyph {
            anchors.fill: parent
            name: root.modeIcon
            tint: root.modeColor
            hole: root.selItem()
            opacity: 0.35
            visible: root.showMode && root.modeStyle === "icon"
            Accessible.ignored: true
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
            Accessible.role: Accessible.ToolBar
            Accessible.name: qsTr("Capture tools")
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

                    // A Flow, not a Row: on a small screen sixteen 40 px chips
                    // plus Capture and Cancel measure 870 px of content and the
                    // pill simply ran off the edge (the toolbar is layered for
                    // its shadow, so the part outside was not even painted).
                    // Bounded by what the pill can afford, the chips wrap onto a
                    // second line instead. `Capture` and `Cancel` stay outside
                    // the wrap so they never move away from the pill's end.
                    Flow {
                        id: toolChipFlow
                        visible: annotationToolsEnabled
                        spacing: 5
                        width: Math.max(45, Math.min(root.naturalRowWidth(toolChipFlow),
                                                     root.maxBarWidth - captureButton.width
                                                     - cancelButton.width - 2 * toolRow.spacing))

                        Repeater {
                            model: annotationToolsEnabled ? root.mainRowModel() : []
                            delegate: ToolChip {
                                // NOTHING in this toolbar is a tab stop. On the
                                // capture surface Space and Enter mean "confirm
                                // the capture" (the hint bar says so) and the
                                // window holds an exclusive keyboard grab, so a
                                // focused chip would swallow the one gesture
                                // that finishes the shot. Every tool already
                                // has a single-letter shortcut here, and
                                // AT-SPI's Press action works without focus, so
                                // dropping out of the Tab chain costs nothing.
                                activeFocusOnTab: false
                                iconName: modelData.kind === "group"
                                          ? modelData.group.iconName
                                          : ToolCatalog.toolIconName(modelData.tool, App.settings.editorIconStyle, App.settings.editorToolIcons)
                                iconStyle: modelData.kind === "group" ? "custom" : App.settings.editorIconStyle
                                label: modelData.kind === "group"
                                       ? modelData.group.label
                                       : ToolCatalog.labelWithShortcut(modelData.tool)
                                // Same reason as the editor's copy of this
                                // chip: a tool's hover-tip label already is its
                                // spoken name, but a GROUP chip's label is the
                                // bare word "Shapes", which says nothing about
                                // being a group or about opening a sub-bar.
                                accessibleName: modelData.kind === "group"
                                                ? qsTr("%1 tool group").arg(modelData.group.label)
                                                : ""
                                accessibleDescription: modelData.kind === "group"
                                                ? qsTr("Opens these tools in the bar below: %1. The group itself has no shortcut.")
                                                      .arg(root.groupMembers(modelData.group.id))
                                                : ""
                                active: modelData.kind === "group"
                                        ? root.shapesActive
                                        : canvas.tool === modelData.tool.tool
                                onClicked: modelData.kind === "group"
                                           ? root.toggleShapesGroup()
                                           : canvas.tool = modelData.tool.tool
                            }
                        }
                        ToolChip { activeFocusOnTab: false; iconName: "edit-undo"; label: qsTr("Undo"); enabled: canvas.canUndo; onClicked: canvas.undo() }
                        ToolChip { activeFocusOnTab: false; iconName: "edit-redo"; label: qsTr("Redo"); enabled: canvas.canRedo; onClicked: canvas.redo() }

                        // Carried in a chip-tall Item: a Flow positions its
                        // children itself, so a 28 px divider cannot centre
                        // itself against the row with an anchor.
                        Item {
                            width: 1; height: 40
                            Rectangle { anchors.centerIn: parent; width: 1; height: 28; color: Theme.divider }
                        }
                    }

                    UButton {
                        id: captureButton
                        // Same rule as the chips above: named, pressable from
                        // AT, but never a tab stop on this surface.
                        activeFocusOnTab: false
                        compact: true
                        iconName: "checkmark"
                        text: root.confirmLabel
                        accessibleDescription: qsTr("Space or Enter also confirms")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: overlayController.confirmFromWindow(overlayWindow)
                    }
                    UIconButton {
                        id: cancelButton
                        activeFocusOnTab: false
                        iconName: "close"; iconSize: 15
                        // No tooltip on this one (it sits on a frozen screen
                        // where a floating tip would read as part of the shot),
                        // so the spoken name has to be supplied by hand.
                        accessibleName: qsTr("Cancel")
                        accessibleDescription: qsTr("Escape also cancels")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: overlayController.cancel()
                    }
                }

                // Also a Flow: the shape chips and the props strip wrap onto a
                // second line when the pill cannot be made wide enough for both.
                Flow {
                    id: overlaySubBar
                    readonly property var ctxProps: ToolCatalog.contextProps(canvas.tool, canvas.selectedAnnotTool)
                    visible: annotationToolsEnabled
                             && (root.shapesActive || ctxProps.length > 0
                                 || (canvas.tool === AnnotationCanvas.EditShapes && canvas.hasAnnotSelection))
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 5
                    width: Math.min(root.naturalRowWidth(overlaySubBar), root.maxBarWidth)

                    // The group chips and the delete affordance wrap as one
                    // block. `visible` is spelled out rather than derived from
                    // the content: a positioner reports content size 0 while it
                    // is itself invisible, so an `implicitWidth > 0` form
                    // latches off and never comes back.
                    Row {
                        id: overlaySubLead
                        spacing: 5
                        visible: root.shapesActive || canvas.hasAnnotSelection

                        Repeater {
                            model: root.shapesActive ? root.shapesTools : []
                            delegate: ToolChip {
                                activeFocusOnTab: false
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
                            activeFocusOnTab: false
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
                    }
                    ToolPropsBar {
                        canvas: canvas
                        props: overlaySubBar.ctxProps
                        // Keeps the whole strip out of the Tab chain, for the
                        // same reason as the chips above. A Flow cannot carry
                        // the opt-out itself, so it forwards it (see the
                        // property).
                        controlsFocusable: false
                        // Never wider than the pill can be on this screen; the
                        // strip wraps inside itself when even a whole line of
                        // the pill is not enough for it.
                        maxWidth: root.maxBarWidth
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
            // True for a callout: the box IS the bubble's writable area, tracking
            // it live as the typing grows the bubble. The glyphs are then the
            // canvas's own (the annotation is repainted on every keystroke), so
            // this box contributes only the caret.
            property bool inShape: false
            property bool editingExisting: false
            property alias text: textField.text
            visible: false
            onVisibleChanged: {
                overlayController.textEditing = visible
                if (visible) textField.forceActiveFocus()
            }
            // A bubble sits inside the image by construction, so its box is
            // placed on it, never nudged back inside the overlay.
            x: inShape ? canvas.textEditRect.x * canvas.renderScale
                       : Math.min(imgX * canvas.renderScale, root.width - width - 8)
            y: inShape ? canvas.textEditRect.y * canvas.renderScale
                       : Math.min(imgY * canvas.renderScale, root.height - height - 8)
            width: inShape ? canvas.textEditRect.width * canvas.renderScale : 360
            height: inShape ? canvas.textEditRect.height * canvas.renderScale
                            : Math.max(40, textField.implicitHeight + 16)
            z: 300

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusS
                // Inside a bubble the panel would only dim the text it is sitting
                // on: the border alone marks the edited area.
                color: textEditor.inShape ? "transparent" : Theme.alpha(Theme.mediaBase, 0.6)
                border.width: 1
                border.color: Theme.alpha(Theme.accent, textEditor.inShape ? 0.5 : 1.0)
            }
            // TextEdit (multi-line): Enter types a new line, Ctrl+Enter commits.
            TextEdit {
                id: textField
                anchors.fill: parent
                anchors.margins: textEditor.inShape ? 0 : 8
                // Inside a bubble the preview matches the painter: centred both
                // ways and wrapped. A Text annotation breaks only where you
                // type a newline, so it must not wrap.
                horizontalAlignment: textEditor.inShape ? TextEdit.AlignHCenter
                                                        : TextEdit.AlignLeft
                verticalAlignment: textEditor.inShape ? TextEdit.AlignVCenter
                                                      : TextEdit.AlignTop
                wrapMode: textEditor.inShape ? TextEdit.Wrap : TextEdit.NoWrap
                focus: true
                // In a bubble the canvas paints the real glyphs under this box on
                // every keystroke, so drawing them again here would only double
                // them, half a pixel off.
                color: textEditor.inShape ? "transparent" : canvas.strokeColor
                selectionColor: Theme.alpha(Theme.accent, 0.45)
                selectedTextColor: color
                // Its height comes from the line, its width from here.
                cursorDelegate: Rectangle { width: 2; color: Theme.accent }
                // Grow the bubble as the words are typed, not at commit.
                onTextChanged: if (textEditor.inShape) canvas.previewCalloutText(text)
                font.family: canvas.fontFamily === "" ? Qt.application.font.family : canvas.fontFamily
                font.pixelSize: canvas.fontSize * canvas.renderScale
                font.bold: canvas.fontBold
                font.italic: canvas.fontItalic
                font.underline: canvas.fontUnderline
                Accessible.role: Accessible.EditableText
                Accessible.name: qsTr("Annotation text")
                Accessible.description: qsTr("Ctrl+Enter finishes, Escape discards")
                Accessible.editable: true
                Accessible.multiLine: true
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
                anchors.fill: parent
                anchors.margins: textEditor.inShape ? 0 : 8
                horizontalAlignment: textEditor.inShape ? Text.AlignHCenter : Text.AlignLeft
                verticalAlignment: textEditor.inShape ? Text.AlignVCenter : Text.AlignTop
                elide: Text.ElideRight
                text: qsTr("Text… (Ctrl+Enter finishes)")
                color: Theme.textTertiary
                font.pixelSize: canvas.fontSize * canvas.renderScale
            }
        }
    }
}
