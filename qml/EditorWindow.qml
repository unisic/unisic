import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Effects
import Unisic
import Unisic.Kit
import "components"

Window {
    id: editorWindow
    // Track the image size, but never below the minimum that keeps every
    // toolbar control visible, and never above the work area - oversized
    // images get a fit-scaled (and zoomable/pannable) canvas instead.
    width: Math.min(Math.max(minimumWidth, canvas.imageSize.width / Screen.devicePixelRatio + chromeWidth),
                    Screen.width * 0.9)
    height: Math.min(Math.max(minimumHeight, canvas.imageSize.height / Screen.devicePixelRatio + chromeHeight),
                     Screen.height * 0.9)
    minimumWidth: 820
    // Panels: tall enough for one column of tools under the header, short of
    // the screen: on a low one the rail takes a second column instead.
    // Floating: the tools are a row, so any sensible height holds them.
    minimumHeight: Math.min(floatingLayout ? 480 : topBar.rowHeight + toolRail.oneColumn,
                            Math.floor(Screen.height * 0.9))
    // AppContext::openEditor centres and shows the window once it exists. Shown from here,
    // it would map before Component.onCompleted hands the canvas its image, at
    // the 820x480 minimum: the platform centres THAT size, then the window
    // grows to fit the image with its top-left corner pinned, which leaves it
    // hanging off towards the bottom-right corner.
    visible: false
    title: editorSession.overwriteMode ? qsTr("Unisic Editor (editing saved image)")
                                       : qsTr("Unisic Editor")
    color: floatingLayout ? Theme.backgroundDeep : Theme.primary
    // Same decoration policy as the main window: the stylized frameless title
    // bar unless the user opted into system decorations (Appearance).
    flags: App.settings.useSystemDecoration
           ? Qt.Window
           : (Qt.Window | Qt.FramelessWindowHint)
    // A fitted image keeps this far from the stage's edges (stage px).
    readonly property int fitPad: floatingLayout ? Theme.spacingM : Theme.spacingXL
    // The panels rail with one column of chips. A short window wraps the rail
    // into more columns, but the size the window opens at assumes one: bound
    // to the real one, the width would depend on the height that depends on
    // it, and a resize that changes the columns would snap the window back to
    // the image's size.
    readonly property int railChipW: 38
    readonly property int railChipH: 34
    readonly property int railPad: 9
    readonly property int railWidth: railChipW + 2 * railPad

    // Settings > Editor > Layout. "panels": the tools in a rail on the left,
    // the active tool's options in an inspector column on the right.
    // "floating": the image fills the window and the same controls float over
    // it in pills. Every control is the same item in both, only placed
    // differently, so shortcuts, focus order and the smoke test do not care.
    readonly property bool floatingLayout: (layoutOverride !== "" ? layoutOverride
                                                                  : App.settings.editorLayout) === "floating"
    // The smoke test lays an open editor out both ways without touching the
    // saved setting.
    property string layoutOverride: ""
    readonly property int inspectorWidth: 320
    // Floating: a pill holds one row of 44 px chips, keeps pillEdge from the
    // window's edges and pillGap from the next pill.
    readonly property int chipSize: 44
    readonly property int pillPad: 6
    readonly property int pillH: chipSize + 2 * pillPad
    readonly property int pillGap: Theme.spacingM
    readonly property int pillEdge: Theme.spacingL
    readonly property color pillColor: Theme.alpha(Theme.surface, 0.88)
    readonly property color hairline: Theme.alpha(Theme.accent, 0.10)
    // Everything around a fitted image, assuming one row or column of each
    // (see railWidth): in floating, a row of pills above and below plus the
    // tools on a row of their own, as on a narrow window.
    readonly property int chromeWidth: floatingLayout ? 2 * fitPad
                                                      : 2 * fitPad + railWidth + inspectorWidth + 1
    readonly property int chromeHeight: floatingLayout ? 2 * pillEdge + 3 * pillH + 3 * pillGap
                                                       : 2 * fitPad + topBar.rowHeight + statusBar.height
    // The image size, or the last export's or OCR's result (OCR also names
    // the selection gestures here): the status line in panels, the line under
    // the title in floating.
    readonly property string statusLine: editorSession.statusText !== ""
                                         ? editorSession.statusText
                                         : canvas.imageSize.width + " × " + canvas.imageSize.height + " px"
    readonly property string fileTitle: editorSession.fileName !== "" ? editorSession.fileName : qsTr("Untitled")
    // Edits since the last export: the "Edited" mark. `exported` below is
    // the close guard's, and stays set once anything was exported.
    property bool dirty: false

    // What the inspector's header (and the floating options pill) names.
    readonly property var focusInfo: {
        if (canvas.ocrMode)
            return { name: qsTr("Select text"), icon: "line-text-scan",
                     sub: canvas.ocrBusy ? qsTr("Recognizing…") : qsTr("Text recognition") }
        if (canvas.hasAnnotSelection) {
            if (canvas.selectedCount > 1)
                return { name: qsTr("%n shape(s) selected", "", canvas.selectedCount),
                         sub: qsTr("Selected shapes"), icon: "line-square" }
            const st = ToolCatalog.toolByEnum(canvas.selectedAnnotTool)
            return { name: st ? st.label : qsTr("Image"), sub: qsTr("Selected shape"),
                     icon: shapeIcon(canvas.selectedAnnotTool) }
        }
        const t = ToolCatalog.toolByEnum(canvas.tool)
        if (t && canvas.tool !== AnnotationCanvas.None)
            return { name: t.label, sub: qsTr("Active tool"), icon: shapeIcon(canvas.tool) }
        return { name: qsTr("No tool"), sub: qsTr("Pick a tool on the left"), icon: "line-image" }
    }

    // The save formats: the split button's menu in panels, the top of
    // "More" in floating.
    readonly property var saveActions: [
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
    // - a statusText with statusExported is only ever set by those. Used to skip the
    // discard-confirm nag: once exported, closing never nags.
    property bool exported: false
    // Set by the discard confirmation so the second close() pass goes through.
    property bool closeConfirmed: false
    Connections {
        target: editorSession
        function onStatusTextChanged() {
            if (editorSession.statusText !== "" && editorSession.statusExported) {
                editorWindow.exported = true
                editorWindow.dirty = false
            }
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
    // The Sticker tool's quick row: the library's picks, newest first, or its
    // defaults until there is one.
    function stickerList() {
        var s = App.settings.recentStickers
        return s ? s.split(",").filter(function (x) { return x.length > 0 }) : stickerPopup.quickDefaults
    }
    // A sticker already in the row stays where it is: the row never moves
    // under the pointer that is using it.
    function addRecentSticker(sticker) {
        var list = stickerList()
        if (list.indexOf(sticker) >= 0)
            return
        list = [sticker].concat(list).slice(0, stickerPopup.quickDefaults.length)
        App.settings.recentStickers = list.join(",")
    }

    // ---- tools ----
    // The rail (panels) or the bar (floating), in ToolCatalog's order. The
    // bar folds some tools into group chips; each chip stands for the member
    // last used, shows its glyph, and opens its members in the options pill.
    readonly property var toolSequence: ToolCatalog.editorSequence(floatingLayout, App.settings.hiddenTools)
    property var lastInGroup: ({})
    readonly property var activeEntry: {
        const g = ToolCatalog.editorGroupOf(canvas.tool)
        if (!floatingLayout || g === "")
            return null
        for (var i = 0; i < toolSequence.length; ++i)
            if (toolSequence[i].kind === "group" && toolSequence[i].group.id === g)
                return toolSequence[i]
        return null
    }
    function groupPick(entry) {
        const id = lastInGroup[entry.group.id]
        for (var i = 0; i < entry.members.length; ++i)
            if (entry.members[i].id === id)
                return entry.members[i]
        return entry.members[0]
    }
    function toggleGroup(entry) {
        canvas.tool = activeEntry && activeEntry.group.id === entry.group.id
                      ? AnnotationCanvas.None : groupPick(entry).tool
    }
    function activateToolShortcut(key) {
        const picked = ToolCatalog.toolForShortcut(key, "editor")
        if (!picked)
            return false
        canvas.tool = picked.tool
        return true
    }
    Connections {
        target: canvas
        function onToolChanged() {
            const g = ToolCatalog.editorGroupOf(canvas.tool)
            if (g === "")
                return
            var m = Object.assign({}, editorWindow.lastInGroup)
            m[g] = ToolCatalog.toolByEnum(canvas.tool).id
            editorWindow.lastInGroup = m
        }
        function onHistoryChanged() {
            editorWindow.dirty = canvas.canUndo
            Qt.callLater(editorWindow.refreshShapes)
        }
        function onSelectedAnnotChanged() { Qt.callLater(editorWindow.refreshShapes) }
    }

    // The inspector's shape list, top of the stack first; `index` is the
    // canvas's own (bottom first) for selectAnnotIndex.
    property var shapes: []
    function refreshShapes() {
        const list = canvas.annotationList()
        var out = []
        for (var i = list.length - 1; i >= 0; --i) {
            const a = list[i]
            out.push({ index: i, tool: a.tool, color: a.color, selected: a.selected,
                       name: shapeName(a) })
        }
        shapes = out
    }
    function shapeName(a) {
        const t = ToolCatalog.toolByEnum(a.tool)
        if (a.tool === AnnotationCanvas.Step)
            return qsTr("Step %1").arg(a.number)
        if (a.redactKind !== "")
            return qsTr("Redaction") + " · " + redactReview.kindName(a.redactKind)
        const base = t ? t.label : qsTr("Image")
        const text = String(a.text || "").replace(/\s+/g, " ").trim()
        return text !== "" ? base + " · " + text : base
    }
    function shapeIcon(toolEnum) {
        const t = ToolCatalog.toolByEnum(toolEnum)
        return t ? ToolCatalog.toolIconName(t, "custom", "") : "line-image"
    }

    // Auto-redact on the whole image: scans for what SensitiveText finds
    // (recognizing the text first when needed, EditorSession::scanSensitive)
    // and opens the kinds beside the button that ran it; nothing is covered
    // until the user switches a kind on.
    function autoRedact(from) {
        editorWindow.commitPendingText()
        editorSession.scanSensitive()
        redactReview.openFrom(from, editorWindow.floatingLayout)
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
    RedactReview {
        id: redactReview
        objectName: "editorRedactReview"
        canvas: canvas
        status: editorSession.statusText
        busy: canvas.ocrBusy
        onClosed: editorSession.endSensitiveScan()
    }
    StickerPopup {
        id: stickerPopup
        objectName: "editorStickerPopup"
        canvas: canvas
        onPicked: (sticker) => {
            canvas.sticker = sticker
            editorWindow.addRecentSticker(sticker)
        }
    }
    UConfirmDialog {
        id: discardConfirm
        title: qsTr("Discard annotations?")
        text: qsTr("You have unsaved annotations. Close the editor and discard them?")
        confirmText: qsTr("Discard")
        destructive: true
        onAccepted: { editorWindow.closeConfirmed = true; editorWindow.close() }
    }
    // Resize the whole picture. The two sides move together while the aspect
    // lock is on; scaleImage() owns the caps, and a refusal keeps the dialog
    // open with the limits spelled out.
    Popup {
        id: resizeDialog
        property bool keepAspect: true
        property bool refused: false
        property size start: Qt.size(1, 1)

        function openFor() {
            editorWindow.commitPendingText()
            start = canvas.imageSize
            widthField.text = String(start.width)
            heightField.text = String(start.height)
            refused = false
            open()
        }
        function follow(fromWidth) {
            if (!keepAspect) return
            const v = parseInt(fromWidth ? widthField.text : heightField.text)
            if (!(v > 0)) return
            if (fromWidth) heightField.text = String(Math.max(1, Math.round(v * start.height / start.width)))
            else widthField.text = String(Math.max(1, Math.round(v * start.width / start.height)))
        }
        function apply() {
            if (canvas.scaleImage(parseInt(widthField.text), parseInt(heightField.text))) close()
            else refused = true
        }

        parent: Overlay.overlay
        anchors.centerIn: parent
        margins: UFlyout.margin
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: Math.min(380, parent ? parent.width - 2 * Theme.spacingXL : 380)
        padding: Theme.spacingXL
        onOpened: widthField.forceFocus()

        Overlay.modal: Rectangle { color: Theme.modalScrim }
        background: Rectangle {
            radius: Theme.radiusL
            color: Theme.surface
            border.width: 1
            border.color: Theme.divider
        }

        contentItem: Column {
            spacing: Theme.spacingM
            Accessible.role: Accessible.Dialog
            Accessible.name: qsTr("Resize image")

            Text {
                width: parent.width
                text: qsTr("Resize image")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontL
                font.weight: Font.DemiBold
            }
            Row {
                spacing: Theme.spacingS
                UTextField {
                    id: widthField
                    width: 110
                    accessibleName: qsTr("Width in pixels")
                    validator: IntValidator { bottom: 1; top: 16384 }
                    onEdited: resizeDialog.follow(true)
                    onAccepted: resizeDialog.apply()
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "×"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                }
                UTextField {
                    id: heightField
                    width: 110
                    accessibleName: qsTr("Height in pixels")
                    validator: IntValidator { bottom: 1; top: 16384 }
                    onEdited: resizeDialog.follow(false)
                    onAccepted: resizeDialog.apply()
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "px"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                }
            }
            Row {
                spacing: Theme.spacingS
                USwitch {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: resizeDialog.keepAspect
                    accessibleName: aspectLabel.text
                    onToggled: (c) => { resizeDialog.keepAspect = c; if (c) resizeDialog.follow(true) }
                }
                Text {
                    id: aspectLabel
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Keep aspect ratio")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontM
                }
            }
            Text {
                width: parent.width
                visible: resizeDialog.refused
                text: qsTr("Up to 16384 px a side and 64 megapixels in total.")
                color: Theme.danger
                font.pixelSize: Theme.fontM
                wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.spacingS
                UButton {
                    text: qsTr("Cancel")
                    variant: "ghost"
                    compact: true
                    onClicked: resizeDialog.close()
                }
                UButton {
                    text: qsTr("Resize")
                    variant: "filled"
                    compact: true
                    onClicked: resizeDialog.apply()
                }
            }
        }
    }
    Item {
        id: shortcutScope
        objectName: "editorShortcutScope"
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
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_P) {
                editorWindow.commitPendingText(); editorSession.print()
            }
            // Ctrl+C copies the OCR text selection while in OCR mode, else the image.
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_C) {
                if (canvas.ocrMode) editorSession.copyOcrSelection()
                else { editorWindow.commitPendingText(); editorSession.copyToClipboard() }
            }
            else if (!canvas.ocrMode && (e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_V) {
                var pasteAt = canvas.toImage(canvas.width / 2, canvas.height / 2)
                canvas.pasteClipboard(pasteAt.x, pasteAt.y)
            }
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_A) {
                if (canvas.ocrMode) canvas.ocrSelectAll()
                else canvas.selectAllAnnots()
            }
            else if (!canvas.ocrMode && (e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_D)
                canvas.duplicateSelected()
            // Ctrl+] / Ctrl+[ step the selection one place up or down the
            // stack, with Shift all the way. Shift turns the bracket keys into
            // braces on a US layout, so both codes are read.
            else if (!canvas.ocrMode && (e.modifiers & Qt.ControlModifier)
                     && (e.key === Qt.Key_BracketRight || e.key === Qt.Key_BraceRight))
                canvas.arrangeSelected((e.modifiers & Qt.ShiftModifier) ? AnnotationCanvas.BringToFront
                                                                       : AnnotationCanvas.BringForward)
            else if (!canvas.ocrMode && (e.modifiers & Qt.ControlModifier)
                     && (e.key === Qt.Key_BracketLeft || e.key === Qt.Key_BraceLeft))
                canvas.arrangeSelected((e.modifiers & Qt.ShiftModifier) ? AnnotationCanvas.SendToBack
                                                                       : AnnotationCanvas.SendBackward)
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_U) { editorWindow.commitPendingText(); editorSession.upload() }
            else if ((e.modifiers & Qt.ControlModifier) && (e.key === Qt.Key_Plus || e.key === Qt.Key_Equal)) canvasFlick.zoomBy(1.2)
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_Minus) canvasFlick.zoomBy(1 / 1.2)
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_0) canvasFlick.zoom = 0
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_1) canvasFlick.zoomTo(Math.min(1, canvasFlick.maxZoom))
            // Delete / Backspace removes the selected shape(s).
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
            // Arrow keys nudge the selected shape(s) (Shift = ×10).
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

        // ---------- layout ----------
        // Two frames around the same controls (floatingLayout).
        // Panels: a header with the file and the exports, the tools in a rail
        // down the left edge, the inspector (what is picked, its options, the
        // shapes on the image, whole-image actions) down the right edge, the
        // status line and the zoom under the image.
        // Floating: the image fills the window and the same groups float over
        // it in pills: title, tools and exports along the top (the tools on
        // a row of their own when the window is too narrow), whole-image
        // actions, the options and the zoom along the bottom. A fitted image
        // clears every pill in both; only a zoomed one passes under them.

        // A group of controls floating over the image; a plain strip in
        // panels. The shadow is drawn from the background alone, so the
        // controls on it are not rendered through a layer.
        component Pill: Item {
            id: pill
            property bool floating: editorWindow.floatingLayout
            property color panelColor: "transparent"
            Rectangle {
                anchors.fill: parent
                color: pill.floating ? editorWindow.pillColor : pill.panelColor
                radius: pill.floating ? 20 : 0
                border.width: pill.floating ? 1 : 0
                border.color: Theme.alpha(Theme.accent, 0.14)
                layer.enabled: pill.floating && pill.visible
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: Theme.shadow
                    shadowBlur: 1.0
                    blurMax: 40
                    shadowVerticalOffset: 16
                }
            }
        }

        // A thin divider between groups of controls in a row or a column.
        component Sep: Item {
            property bool vertical: true
            property int length: 24
            implicitWidth: vertical ? 9 : length
            implicitHeight: vertical ? length : 13
            width: implicitWidth
            height: implicitHeight
            Rectangle {
                anchors.centerIn: parent
                width: parent.vertical ? 1 : 24
                height: parent.vertical ? Math.min(24, parent.height) : 1
                color: Theme.alpha(Theme.accent, parent.vertical && editorWindow.floatingLayout ? 0.18 : 0.14)
            }
        }

        // A labelled text button: Fit and 1:1 next to the zoom.
        component TextChip: Rectangle {
            id: tc
            property string text: ""
            property string tip: ""
            property bool active: false
            signal clicked()
            function _activate() { tc.clicked() }
            implicitWidth: Math.ceil(tcText.implicitWidth) + 20
            implicitHeight: 26
            radius: 6
            color: active ? Theme.secondary : tcMouse.containsMouse ? Theme.surfaceTop : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.animFast } }
            Text {
                id: tcText
                anchors.centerIn: parent
                text: tc.text
                color: tc.active ? Theme.textPrimary : Theme.textSecondary
                font.pixelSize: Theme.fontS
                font.weight: Font.Medium
                Accessible.ignored: true
            }
            MouseArea {
                id: tcMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: tc._activate()
            }
            UHoverTip { anchor: tc; text: tc.tip; show: tcMouse.containsMouse && tc.tip !== "" }
            activeFocusOnTab: true
            Keys.onSpacePressed: (e) => UKeys.activate(e, tc._activate)
            Keys.onReturnPressed: (e) => UKeys.activate(e, tc._activate)
            Keys.onEnterPressed: (e) => UKeys.activate(e, tc._activate)
            Accessible.role: Accessible.Button
            Accessible.name: tc.tip !== "" ? tc.tip : tc.text
            Accessible.focusable: true
            Accessible.onPressAction: tc._activate()
            UFocusRing {}
        }

        // A whole-image action: a full-width row in the inspector, a 44 px
        // chip in the floating pill (icon only, with a tip, when `compact`).
        component WholeButton: Rectangle {
            id: wb
            property string iconName: ""
            property string text: ""
            property bool pill: false
            property bool compact: false
            signal clicked()
            function _activate() { if (wb.enabled) wb.clicked() }
            readonly property real fullWidth: Math.ceil(wbText.implicitWidth) + (pill ? 18 + 8 + 26 : 16 + 10 + 24)
            implicitWidth: compact ? editorWindow.chipSize : fullWidth
            implicitHeight: pill ? editorWindow.chipSize : 36
            radius: pill ? 14 : 8
            color: wbMouse.containsMouse ? (pill ? Theme.alpha(Theme.accent, 0.18) : Theme.surfaceTop)
                                         : (pill ? "transparent" : Theme.primary)
            Behavior on color { ColorAnimation { duration: Theme.animFast } }
            border.width: pill ? 0 : 1
            border.color: Theme.alpha(Theme.accent, 0.12)
            opacity: enabled ? 1 : 0.4
            Row {
                x: wb.compact ? Math.round((wb.width - wbIcon.width) / 2) : wb.pill ? 12 : 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: wb.pill ? 8 : 10
                UIcon {
                    id: wbIcon
                    name: wb.iconName
                    iconStyle: "custom"
                    size: wb.pill ? 18 : 16
                    color: Theme.accent
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    id: wbText
                    visible: !wb.compact
                    text: wb.text
                    color: Theme.textPrimary
                    font.pixelSize: 13
                    font.weight: wb.pill ? Font.Bold : Font.Medium
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }
            MouseArea {
                id: wbMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: wb._activate()
            }
            UHoverTip { anchor: wb; text: wb.text; show: wb.compact && wbMouse.containsMouse }
            activeFocusOnTab: enabled
            Keys.onSpacePressed: (e) => UKeys.activate(e, wb._activate)
            Keys.onReturnPressed: (e) => UKeys.activate(e, wb._activate)
            Keys.onEnterPressed: (e) => UKeys.activate(e, wb._activate)
            Accessible.role: Accessible.Button
            Accessible.name: wb.text
            Accessible.focusable: true
            Accessible.onPressAction: wb._activate()
            UFocusRing { hostRadius: wb.radius }
        }

        // Window drag (frameless decoration) on the bare parts of the top bar.
        // Every press on it is eaten, also with system decorations, so a click
        // on the padding of a bar never reaches the canvas under it.
        component DragArea: MouseArea {
            acceptedButtons: Qt.AllButtons
            cursorShape: Qt.ArrowCursor
            property real pressX: 0
            property real pressY: 0
            property bool moving: false
            onWheel: (wheel) => wheel.accepted = true
            // Deferred startSystemMove past a drag threshold - same pattern
            // (and reason) as Main.qml's title bar.
            onPressed: (m) => { pressX = m.x; pressY = m.y; moving = false }
            onPositionChanged: (m) => {
                if (App.settings.useSystemDecoration || !(pressedButtons & Qt.LeftButton))
                    return
                if (!moving && (Math.abs(m.x - pressX) > 6 || Math.abs(m.y - pressY) > 6)) {
                    moving = true
                    editorWindow.startSystemMove()
                }
            }
            onDoubleClicked: (m) => {
                if (App.settings.useSystemDecoration || m.button !== Qt.LeftButton)
                    return
                editorWindow.visibility === Window.Maximized
                    ? editorWindow.showNormal() : editorWindow.showMaximized()
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
        //
        // Declared first so Tab starts on the tools.
        Pill {
            id: toolRail
            objectName: "editorToolRail"
            panelColor: Theme.background
            // Floating: one row with the title and the exports when it fits
            // between them, centred on a row of its own under them when not.
            readonly property real naturalW: editorWindow.flowWidth(toolFlow, 100000) + 2 * editorWindow.pillPad
            readonly property real rowLeft: titlePill.x + titlePill.width + editorWindow.pillGap
            readonly property real rowRight: actionsPill.x - editorWindow.pillGap
            readonly property bool inTopRow: floating && rowRight - rowLeft >= naturalW
            x: !floating ? 0
               : inTopRow ? Math.round(rowLeft + (rowRight - rowLeft - width) / 2)
               : Math.round((parent.width - width) / 2)
            y: !floating ? topBar.height
               : inTopRow ? editorWindow.pillEdge
               : editorWindow.pillEdge + editorWindow.pillH + editorWindow.pillGap
            width: floating ? Math.min(naturalW, parent.width - 2 * editorWindow.pillEdge)
                            : Math.max(editorWindow.railWidth, toolFlow.width + 2 * editorWindow.railPad)
            height: floating ? toolFlow.implicitHeight + 2 * editorWindow.pillPad : parent.height - y
            // Over the stage, which fills the window in floating.
            z: 20
            // Panels: the chips give up height (34 down to 28 px) before
            // the tools wrap into a second column.
            readonly property int seps: editorWindow.toolSequence.filter(function (e) { return e.kind === "sep" }).length
            readonly property int chips: editorWindow.toolSequence.length - seps
            readonly property real fixedH: seps * 13 + Math.max(0, editorWindow.toolSequence.length - 1) * toolFlow.spacing + 2 * 8
            readonly property int chipH: Math.max(28, Math.min(editorWindow.railChipH,
                                                               Math.floor((height - fixedH) / Math.max(1, chips))))
            // The rail's height with every tool in one column (minimumHeight).
            readonly property real oneColumn: fixedH + chips * 28
            Accessible.role: Accessible.ToolBar
            Accessible.name: qsTr("Annotation tools")

            DragArea { anchors.fill: parent; visible: toolRail.floating }
            Rectangle {
                visible: !toolRail.floating
                x: parent.width - 1
                width: 1; height: parent.height
                color: Theme.alpha(Theme.accent, 0.08)
            }

            // A window too short for one column of tools gets two, and one too
            // narrow for one row gets two, so no tool is ever scrolled away
            // or clipped.
            Flow {
                id: toolFlow
                flow: toolRail.floating ? Flow.LeftToRight : Flow.TopToBottom
                x: toolRail.floating ? editorWindow.pillPad : editorWindow.railPad
                y: toolRail.floating ? editorWindow.pillPad : 8
                width: toolRail.floating
                       ? Math.min(editorWindow.flowWidth(toolFlow, 100000),
                                  editorWindow.width - 2 * editorWindow.pillEdge - 2 * editorWindow.pillPad)
                       : implicitWidth
                // Never undefined: resetting a size mid-flip left the old
                // column's height on the row (a binding loop on the rail).
                height: toolRail.floating ? implicitHeight : toolRail.height - 2 * 8
                spacing: 2
                // Same gate as the tool letters in shortcutScope.
                enabled: !canvas.ocrMode

                Repeater {
                    id: toolRepeater
                    model: editorWindow.toolSequence
                    delegate: Loader {
                        required property var modelData
                        sourceComponent: modelData.kind === "sep" ? sepEntry
                                       : modelData.kind === "group" ? groupEntry : toolEntry
                        Component {
                            id: sepEntry
                            Sep {
                                vertical: toolRail.floating
                                length: toolRail.floating ? editorWindow.chipSize : editorWindow.railChipW
                            }
                        }
                        Component {
                            id: toolEntry
                            ToolChip {
                                readonly property var t: modelData.tool
                                width: toolRail.floating ? editorWindow.chipSize : editorWindow.railChipW
                                height: toolRail.floating ? editorWindow.chipSize : toolRail.chipH
                                radius: toolRail.floating ? 14 : 8
                                iconSize: toolRail.floating ? 20 : 18
                                iconName: ToolCatalog.toolIconName(t, App.settings.editorIconStyle, App.settings.editorToolIcons)
                                iconStyle: App.settings.editorIconStyle
                                label: t.label
                                shortcut: t.shortcut || ""
                                tipPlacement: toolRail.floating ? "auto" : "right"
                                active: canvas.tool === t.tool
                                // A second click puts the tool down: the
                                // pointer is the hand again.
                                onClicked: canvas.tool = canvas.tool === t.tool ? AnnotationCanvas.None : t.tool
                            }
                        }
                        Component {
                            id: groupEntry
                            ToolChip {
                                readonly property var pick: editorWindow.groupPick(modelData)
                                width: editorWindow.chipSize
                                height: editorWindow.chipSize
                                radius: 14
                                iconSize: 20
                                grouped: true
                                // "More tools" has a glyph of its own; the
                                // others wear the member they would pick.
                                iconName: modelData.group.iconName
                                          ? modelData.group.iconName
                                          : ToolCatalog.toolIconName(pick, App.settings.editorIconStyle, App.settings.editorToolIcons)
                                iconStyle: modelData.group.iconName ? "custom" : App.settings.editorIconStyle
                                label: modelData.group.label
                                shortcut: modelData.group.iconName ? "" : (pick.shortcut || "")
                                // A GROUP chip's label is a bare word that says
                                // nothing about the press opening its members
                                // in the options pill, so it names itself. The
                                // spoken name still STARTS with the label, so
                                // "click Shapes" keeps working.
                                accessibleName: qsTr("%1 tool group").arg(modelData.group.label)
                                accessibleDescription: qsTr("Opens these tools in the options bar: %1. The group itself has no shortcut.")
                                    .arg(modelData.members.map(function (m) { return m.label }).join(", "))
                                active: editorWindow.activeEntry !== null
                                        && editorWindow.activeEntry.group.id === modelData.group.id
                                onClicked: editorWindow.toggleGroup(modelData)
                            }
                        }
                    }
                }
            }
        }

        // Panels: the header (file, state, exports, window buttons).
        // Floating: the title and the exports as pills in the top corners,
        // with the image showing between them.
        Item {
            id: topBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            readonly property int rowHeight: 44
            height: editorWindow.floatingLayout ? 0 : rowHeight
            // Over the stage, which fills the window in floating.
            z: 20

            DragArea { anchors.fill: parent; visible: !editorWindow.floatingLayout }
            Rectangle {
                visible: !editorWindow.floatingLayout
                y: topBar.rowHeight - 1
                width: parent.width; height: 1
                color: editorWindow.hairline
            }

            Row {
                id: headerTitle
                visible: !editorWindow.floatingLayout
                x: 16
                height: topBar.rowHeight
                spacing: 10

                Image {
                    source: "qrc:/resources/icons/unisic.svg"
                    sourceSize: Qt.size(20, 20)
                    width: 20; height: 20
                    smooth: true
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Editor")
                    color: Theme.textPrimary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "/"
                    color: Theme.alpha(Theme.accent, 0.35)
                    font.pixelSize: 13
                    Accessible.ignored: true
                }
                Text {
                    id: headerFile
                    anchors.verticalCenter: parent.verticalCenter
                    // Measured apart from the Text: an elided Text's
                    // implicitWidth re-lays out when its width changes.
                    width: Math.max(0, Math.min(Math.ceil(fileMetrics.advanceWidth) + 1,
                                                rightCluster.x - 12 - headerTitle.x - x
                                                - (editedMark.visible ? editedMark.width + headerTitle.spacing : 0)))
                    elide: Text.ElideMiddle
                    text: editorWindow.fileTitle
                    color: Theme.textPrimary
                    font.family: "monospace"
                    font.pixelSize: 12
                    TextMetrics { id: fileMetrics; font: headerFile.font; text: headerFile.text }
                }
                Row {
                    id: editedMark
                    visible: editorWindow.dirty
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Rectangle {
                        width: 6; height: 6; radius: 3
                        color: Theme.accent
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: qsTr("Edited")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontS
                    }
                }
            }

            // Floating: back to the main window, the file and its state.
            Pill {
                id: titlePill
                visible: floating
                x: editorWindow.pillEdge
                y: editorWindow.pillEdge
                readonly property real textW: Math.min(260, Math.max(titleMetrics.advanceWidth,
                                                                     subMetrics.advanceWidth + (subDot.visible ? 12 : 0)))
                width: Math.max(editorWindow.pillH,
                                Math.min(editorWindow.pillPad + editorWindow.chipSize + 12 + Math.ceil(textW) + 18,
                                         actionsPill.x - editorWindow.pillGap - editorWindow.pillEdge))
                height: editorWindow.pillH

                TextMetrics { id: titleMetrics; font: pillTitle.font; text: pillTitle.text }
                TextMetrics { id: subMetrics; font: pillSub.font; text: pillSub.text }

                DragArea { anchors.fill: parent }

                ToolChip {
                    id: backButton
                    objectName: "editorBack"
                    x: editorWindow.pillPad
                    y: editorWindow.pillPad
                    width: editorWindow.chipSize; height: editorWindow.chipSize
                    radius: 14
                    iconSize: 20
                    iconName: "line-back"
                    iconStyle: "custom"
                    label: qsTr("Back to Unisic")
                    onClicked: {
                        App.showMainWindowRequested()
                        editorWindow.close()
                    }
                }
                Column {
                    x: backButton.x + backButton.width + 12
                    width: titlePill.width - x - 18
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    Text {
                        id: pillTitle
                        width: parent.width
                        elide: Text.ElideMiddle
                        text: editorWindow.fileTitle
                        color: Theme.textPrimary
                        font.pixelSize: 16
                        font.weight: Font.Bold
                    }
                    Row {
                        width: parent.width
                        spacing: 6
                        Rectangle {
                            id: subDot
                            visible: editorWindow.dirty && editorSession.statusText === ""
                            width: 6; height: 6; radius: 3
                            color: Theme.accent
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            id: pillSub
                            width: parent.width - (subDot.visible ? subDot.width + parent.spacing : 0)
                            elide: Text.ElideMiddle
                            text: editorSession.statusText !== "" ? editorSession.statusText
                                  : editorWindow.dirty ? qsTr("Unsaved changes")
                                  : editorWindow.statusLine
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontS
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }

            // What is picked and its options: the props of the active tool
            // (or of the selected shape), a group's members, the crop and OCR
            // actions, the selection's actions (floating). Declared here for
            // the Tab order; it lives in the inspector (panels) or in the
            // middle of the bottom row (floating).
            Pill {
                id: ctxPanel
                objectName: "editorToolOptions"
                parent: floating ? shortcutScope : inspectorBody
                // Props follow the active tool, or the SELECTED shape when
                // the Edit tool is active.
                readonly property var ctxProps: ToolCatalog.contextProps(canvas.tool, canvas.selectedAnnotTool)
                readonly property real minX: wholePill.x + wholePill.width + editorWindow.pillGap
                readonly property real maxX: statusBar.x - editorWindow.pillGap
                readonly property real maxW: floating ? maxX - minX - 2 * editorWindow.pillPad
                                                      : inspectorBody.width
                // Auto-redact where people go to hide text by hand: the options
                // of Blur and Pixelate.
                readonly property bool offersRedact: !canvas.ocrMode && !canvas.hasAnnotSelection
                    && (canvas.tool === AnnotationCanvas.Blur || canvas.tool === AnnotationCanvas.Pixelate)
                readonly property bool shown: canvas.ocrMode || ctxProps.length > 0 || offersRedact
                    || (canvas.tool === AnnotationCanvas.Crop && canvas.hasSelection)
                    || (floating && (editorWindow.activeEntry !== null || canvas.hasAnnotSelection))
                // Floating: centred on the bottom row between the whole-image
                // actions and the zoom, growing up over the image when its
                // lines wrap (canvasFlick.bottomMargin keeps that edge
                // reachable).
                x: floating ? Math.max(minX, Math.min(Math.round((parent.width - width) / 2), maxX - width)) : 0
                y: floating ? parent.height - editorWindow.pillEdge - height : 0
                width: floating ? ctxFlow.width + 2 * editorWindow.pillPad : parent.width
                height: floating ? Math.max(editorWindow.pillH, ctxFlow.height + 2 * editorWindow.pillPad)
                                 : ctxFlow.height
                z: 20
                // Fades in, but hides at once: everything inside is bound to
                // the same state and is already gone.
                opacity: shown ? 1 : 0
                visible: shown
                Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
                Accessible.role: Accessible.ToolBar
                Accessible.name: qsTr("Tool options")

                DragArea { anchors.fill: parent; visible: ctxPanel.floating }

                Flow {
                    id: ctxFlow
                    x: ctxPanel.floating ? editorWindow.pillPad : 0
                    anchors.verticalCenter: parent.verticalCenter
                    // Floating: the widest line it lays out once wrapped
                    // evenly into the room it has, never the whole room.
                    // Panels: the inspector's width, filled line by line.
                    width: ctxPanel.floating
                           ? editorWindow.flowWidth(ctxFlow, editorWindow.flowBalanced(ctxFlow, ctxPanel.maxW))
                           : ctxPanel.maxW
                    spacing: 6

                    Text {
                        visible: ctxPanel.floating
                        height: editorWindow.chipSize
                        leftPadding: 10
                        rightPadding: 4
                        verticalAlignment: Text.AlignVCenter
                        text: editorWindow.focusInfo.name
                        color: Theme.textPrimary
                        font.pixelSize: 13
                        font.weight: Font.Bold
                        Accessible.ignored: true
                    }

                    // A group's members (floating): the group chip on the
                    // bar stands for the one last used.
                    Row {
                        visible: !canvas.ocrMode && editorWindow.activeEntry !== null
                        spacing: 2
                        Repeater {
                            model: editorWindow.activeEntry ? editorWindow.activeEntry.members : []
                            delegate: ToolChip {
                                width: editorWindow.chipSize; height: editorWindow.chipSize
                                radius: 14
                                iconSize: 20
                                iconName: ToolCatalog.toolIconName(modelData, App.settings.editorIconStyle, App.settings.editorToolIcons)
                                iconStyle: App.settings.editorIconStyle
                                label: modelData.label
                                shortcut: modelData.shortcut || ""
                                active: canvas.tool === modelData.tool
                                onClicked: canvas.tool = modelData.tool
                            }
                        }
                        Sep {
                            visible: propsBar.visible
                            length: editorWindow.chipSize
                        }
                    }

                    ToolPropsBar {
                        id: propsBar
                        objectName: "editorPropsBar"
                        visible: !canvas.ocrMode && ctxPanel.ctxProps.length > 0
                        variant: ctxPanel.floating ? "pill" : "inspector"
                        canvas: canvas
                        props: ctxPanel.ctxProps
                        recentColors: editorWindow.recentList()
                        quickStickers: editorWindow.stickerList()
                        // Bounded, the strip wraps - and the Flow drops it onto
                        // its own line first, so it only has to wrap inside
                        // itself when even a whole line is not enough. Then
                        // into even lines, like the Flow around it.
                        maxWidth: ctxPanel.floating ? editorWindow.flowBalanced(propsBar, ctxPanel.maxW) : ctxPanel.maxW
                        groupLimit: ctxPanel.maxW
                        onStrokePickerRequested: strokePopup.openWith(canvas.strokeColor)
                        onFillPickerRequested: fillPopup.openWith(canvas.shapeFillColor)
                        onTextOutlinePickerRequested: outlinePopup.openWith(canvas.textOutlineColor)
                        onTextBackgroundPickerRequested: textBgPopup.openWith(canvas.textBackgroundColor)
                        onStickerLibraryRequested: stickerPopup.openFor(canvas.sticker)
                        onImageFileRequested: {
                            var at = canvas.toImage(canvas.width / 2, canvas.height / 2)
                            editorSession.insertImageFile(at.x, at.y)
                        }
                    }

                    UButton {
                        id: toolRedact
                        objectName: "editorToolRedact"
                        // Kept while its popup is open over the scan's OCR mode.
                        visible: ctxPanel.offersRedact
                                 || (redactReview.visible && redactReview.anchor === toolRedact)
                        compact: true
                        height: ctxPanel.floating ? editorWindow.chipSize : 34
                        cornerRadius: ctxPanel.floating ? 14 : 8
                        iconName: "line-redact"
                        text: qsTr("Auto-redact")
                        onClicked: editorWindow.autoRedact(toolRedact)
                    }

                    UButton {
                        visible: !canvas.ocrMode && canvas.tool === AnnotationCanvas.Crop && canvas.hasSelection
                        compact: true
                        height: ctxPanel.floating ? editorWindow.chipSize : 34
                        cornerRadius: ctxPanel.floating ? 14 : 8
                        iconName: "line-crop"
                        text: qsTr("Apply crop")
                        onClicked: canvas.applyCrop()
                    }

                    // --- OCR text selection ---
                    // How to select is in the status line, which OCR fills
                    // with its result and the gestures.
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

                    // Floating: the selection's actions close the pill.
                    Row {
                        visible: ctxPanel.floating && canvas.hasAnnotSelection && !canvas.ocrMode
                        Sep { length: editorWindow.chipSize }
                        Item {
                            id: floatSelSlot
                            width: selActions.width
                            height: selActions.height
                        }
                    }
                }
            }

            // Actions on the selected shape(s): in the inspector's header
            // (panels) or at the end of the options pill (floating). One set,
            // moved, so shortcuts, focus and the smoke test see one.
            Row {
                id: selActions
                parent: editorWindow.floatingLayout ? floatSelSlot : panelSelSlot
                readonly property bool floating: editorWindow.floatingLayout
                readonly property int size: floating ? editorWindow.chipSize : 34
                spacing: 2
                visible: canvas.hasAnnotSelection && !canvas.ocrMode

                ToolChip {
                    objectName: "editorDuplicateShape"
                    width: selActions.size; height: selActions.size
                    radius: selActions.floating ? 14 : 8
                    iconName: "line-copy"
                    iconStyle: "custom"
                    label: qsTr("Duplicate")
                    shortcut: "Ctrl+D"
                    onClicked: canvas.duplicateSelected()
                }
                UMenuButton {
                    objectName: "editorArrangeShapes"
                    // Implicit, not width: flowWrap measures the implicit
                    // size of what it lays out.
                    implicitWidth: selActions.size; implicitHeight: selActions.size
                    iconOnly: true
                    flat: true
                    iconName: "object-order-front"
                    tooltip: qsTr("Arrange")
                    preferAbove: selActions.floating
                    actions: [
                        { label: qsTr("Bring to front"), hint: "Ctrl+Shift+]",
                          trigger: function () { canvas.arrangeSelected(AnnotationCanvas.BringToFront) } },
                        { label: qsTr("Bring forward"), hint: "Ctrl+]",
                          trigger: function () { canvas.arrangeSelected(AnnotationCanvas.BringForward) } },
                        { label: qsTr("Send backward"), hint: "Ctrl+[",
                          trigger: function () { canvas.arrangeSelected(AnnotationCanvas.SendBackward) } },
                        { label: qsTr("Send to back"), hint: "Ctrl+Shift+[",
                          trigger: function () { canvas.arrangeSelected(AnnotationCanvas.SendToBack) } }
                    ]
                }
                ToolChip {
                    width: selActions.size; height: selActions.size
                    radius: selActions.floating ? 14 : 8
                    iconName: "line-trash"
                    iconStyle: "custom"
                    iconColor: Theme.danger
                    label: canvas.selectedCount > 1 ? qsTr("Delete %n shape(s)", "", canvas.selectedCount)
                                                    : qsTr("Delete shape")
                    shortcut: "Delete"
                    onClicked: canvas.removeSelectedAnnot()
                }
            }

            Pill {
                id: actionsPill
                visible: floating
                x: parent.width - width - editorWindow.pillEdge
                y: editorWindow.pillEdge
                width: rightCluster.width + 2 * editorWindow.pillPad
                height: editorWindow.pillH

                DragArea { anchors.fill: parent }
            }

            // The exports. Primary ones stay labelled in panels; the floating
            // pill keeps Copy and Save as chips and Upload labelled. In OCR
            // mode its own actions sit in the options and these, which act on
            // the whole image, are disabled until "Done" - disabled rather
            // than hidden, so nothing in the bar moves when OCR starts.
            Row {
                id: rightCluster
                objectName: "editorActions"
                parent: editorWindow.floatingLayout ? actionsPill : topBar
                readonly property bool floating: editorWindow.floatingLayout
                readonly property int chipW: floating ? editorWindow.chipSize : 32
                readonly property int chipH: floating ? editorWindow.chipSize : 30
                x: floating ? editorWindow.pillPad : parent.width - width - 6
                y: Math.round(((floating ? editorWindow.pillH : topBar.rowHeight) - height) / 2)
                spacing: floating ? 4 : 6

                ToolChip {
                    anchors.verticalCenter: parent.verticalCenter
                    width: rightCluster.chipW; height: rightCluster.chipH
                    radius: rightCluster.floating ? 14 : 7
                    iconSize: rightCluster.floating ? 19 : 16
                    iconName: "line-undo"
                    iconStyle: "custom"
                    label: qsTr("Undo")
                    shortcut: "Ctrl+Z"
                    enabled: canvas.canUndo
                    onClicked: canvas.undo()
                }
                ToolChip {
                    anchors.verticalCenter: parent.verticalCenter
                    width: rightCluster.chipW; height: rightCluster.chipH
                    radius: rightCluster.floating ? 14 : 7
                    iconSize: rightCluster.floating ? 19 : 16
                    iconName: "line-redo"
                    iconStyle: "custom"
                    label: qsTr("Redo")
                    shortcut: "Ctrl+Shift+Z"
                    enabled: canvas.canRedo
                    onClicked: canvas.redo()
                }
                Sep {
                    anchors.verticalCenter: parent.verticalCenter
                    length: rightCluster.floating ? 24 : 20
                }

                Row {
                    id: actionRow
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: rightCluster.spacing
                    enabled: !canvas.ocrMode
                    Accessible.role: Accessible.ToolBar
                    Accessible.name: qsTr("Editor actions")

                    UButton {
                        visible: !rightCluster.floating
                        anchors.verticalCenter: parent.verticalCenter
                        compact: true
                        height: 30
                        cornerRadius: 7
                        variant: "outline"
                        iconName: "line-copy"; text: qsTr("Copy")
                        onClicked: { editorWindow.commitPendingText(); editorSession.copyToClipboard() }
                    }
                    ToolChip {
                        visible: rightCluster.floating
                        anchors.verticalCenter: parent.verticalCenter
                        width: editorWindow.chipSize; height: editorWindow.chipSize
                        radius: 14
                        iconSize: 19
                        iconName: "line-copy"
                        iconStyle: "custom"
                        label: qsTr("Copy")
                        shortcut: "Ctrl+C"
                        onClicked: { editorWindow.commitPendingText(); editorSession.copyToClipboard() }
                    }
                    USplitMenuButton {
                        visible: !rightCluster.floating
                        anchors.verticalCenter: parent.verticalCenter
                        compact: true
                        outline: true
                        height: 30
                        cornerRadius: 7
                        iconName: "line-save"
                        text: editorSession.overwriteMode ? qsTr("Overwrite") : qsTr("Save")
                        tooltip: editorSession.overwriteMode ? qsTr("Overwrite (Ctrl+S)") : qsTr("Save (Ctrl+S)")
                        dropdownTooltip: qsTr("More save options")
                        onClicked: editorWindow.doSave()
                        actions: editorWindow.saveActions
                    }
                    ToolChip {
                        visible: rightCluster.floating
                        anchors.verticalCenter: parent.verticalCenter
                        width: editorWindow.chipSize; height: editorWindow.chipSize
                        radius: 14
                        iconSize: 19
                        iconName: "line-save"
                        iconStyle: "custom"
                        label: editorSession.overwriteMode ? qsTr("Overwrite") : qsTr("Save")
                        shortcut: "Ctrl+S"
                        onClicked: editorWindow.doSave()
                    }
                    UButton {
                        anchors.verticalCenter: parent.verticalCenter
                        compact: true
                        height: rightCluster.floating ? editorWindow.chipSize : 30
                        cornerRadius: rightCluster.floating ? 14 : 7
                        iconName: rightCluster.floating ? "line-upload" : "line-send"
                        text: App.uploads.busy ? qsTr("Uploading…") : qsTr("Upload")
                        enabled: !App.uploads.busy
                        onClicked: { editorWindow.commitPendingText(); editorSession.upload() }
                    }
                    // "More": the occasional picture actions (and, floating,
                    // the save formats the split button holds in panels).
                    UMenuButton {
                        id: moreMenu
                        anchors.verticalCenter: parent.verticalCenter
                        width: rightCluster.chipW; height: rightCluster.chipH
                        iconOnly: true
                        flat: true
                        iconName: "view-more"
                        tooltip: qsTr("More")
                        preferAbove: false
                        actions: (rightCluster.floating ? editorWindow.saveActions : []).concat([
                            { label: qsTr("Select text…"), iconName: "select",
                              separatorBefore: rightCluster.floating,
                              trigger: function () { editorSession.startOcrPick() } },
                            { label: qsTr("Rotate left"), iconName: "object-rotate-left", separatorBefore: true,
                              trigger: function () { editorWindow.commitPendingText(); canvas.rotateImage(false) } },
                            { label: qsTr("Rotate right"), iconName: "object-rotate-right",
                              trigger: function () { editorWindow.commitPendingText(); canvas.rotateImage(true) } },
                            { label: qsTr("Flip horizontally"), iconName: "object-flip-horizontal",
                              trigger: function () { editorWindow.commitPendingText(); canvas.flipImage(true) } },
                            { label: qsTr("Flip vertically"), iconName: "object-flip-vertical",
                              trigger: function () { editorWindow.commitPendingText(); canvas.flipImage(false) } },
                            { label: qsTr("Resize…"), iconName: "transform-scale",
                              trigger: function () { resizeDialog.openFor() } },
                            { label: qsTr("Print…"), iconName: "document-print", hint: "Ctrl+P",
                              separatorBefore: true,
                              trigger: function () { editorWindow.commitPendingText(); editorSession.print() } }
                        ])
                    }
                }

                Sep {
                    visible: !App.settings.useSystemDecoration
                    anchors.verticalCenter: parent.verticalCenter
                    length: rightCluster.floating ? 24 : 20
                }

                Row {
                    id: titleButtons
                    visible: !App.settings.useSystemDecoration
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    readonly property int w: rightCluster.floating ? 36 : 34
                    readonly property int h: rightCluster.floating ? 36 : 30
                    UIconButton {
                        iconName: "minus"; iconSize: 14; width: titleButtons.w; height: titleButtons.h
                        tooltip: qsTr("Minimize")
                        onClicked: editorWindow.showMinimized()
                    }
                    UIconButton {
                        iconName: "window"; iconSize: 13; width: titleButtons.w; height: titleButtons.h
                        tooltip: qsTr("Maximize")
                        onClicked: editorWindow.visibility === Window.Maximized
                                   ? editorWindow.showNormal() : editorWindow.showMaximized()
                    }
                    UIconButton {
                        iconName: "close"; iconSize: 14; width: titleButtons.w; height: titleButtons.h
                        tooltip: qsTr("Close")
                        onClicked: editorWindow.close()
                    }
                }
            }
        }

        // Floating: the whole-image actions in the bottom-left corner,
        // labelled while the options beside them still fit on one line: 800
        // px holds the arrow's, a selected shape's actions included. Fixed,
        // not the current options' width, or the labels would come and go
        // with every tool switch.
        Pill {
            id: wholePill
            objectName: "editorWholeImage"
            visible: floating
            x: editorWindow.pillEdge
            y: parent.height - editorWindow.pillEdge - height
            width: wholeRow.width + 2 * editorWindow.pillPad
            height: editorWindow.pillH
            z: 20
            readonly property real labelledW: wholeCopy.fullWidth + wholeRedact.fullWidth
                + (wholeBg.visible ? wholeBg.fullWidth + wholeRow.spacing : 0) + wholeRow.spacing
                + 2 * editorWindow.pillPad
            readonly property bool labelled: editorWindow.width - 2 * editorWindow.pillEdge
                - 2 * editorWindow.pillGap - statusBar.width - labelledW >= 800
            Accessible.role: Accessible.ToolBar
            Accessible.name: qsTr("On the whole image")

            DragArea { anchors.fill: parent }

            Row {
                id: wholeRow
                x: editorWindow.pillPad
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                WholeButton {
                    id: wholeCopy
                    pill: true
                    compact: !wholePill.labelled
                    iconName: "line-text-scan"
                    text: qsTr("Copy text")
                    onClicked: editorSession.ocrCopyText()
                }
                WholeButton {
                    id: wholeRedact
                    objectName: "editorWholeRedact"
                    pill: true
                    compact: !wholePill.labelled
                    iconName: "line-redact"
                    text: qsTr("Auto-redact")
                    enabled: !canvas.ocrBusy
                    onClicked: editorWindow.autoRedact(wholeRedact)
                }
                WholeButton {
                    id: wholeBg
                    pill: true
                    compact: !wholePill.labelled
                    visible: App.backgroundRemover.available
                    iconName: "line-remove-bg"
                    text: qsTr("Remove background")
                    enabled: !App.backgroundRemover.busy && !canvas.ocrMode
                    onClicked: { editorWindow.commitPendingText(); editorSession.removeBackground() }
                }
            }
        }

        // Panels: what is picked and its options, the shapes on the image,
        // the whole-image actions.
        Rectangle {
            id: inspector
            objectName: "editorInspector"
            visible: !editorWindow.floatingLayout
            x: parent.width - width
            y: topBar.height
            width: editorWindow.inspectorWidth
            height: parent.height - y
            color: Theme.surface

            Rectangle { width: 1; height: parent.height; color: editorWindow.hairline }

            Flickable {
                id: inspectorFlick
                anchors.fill: parent
                anchors.leftMargin: 1
                clip: true
                contentHeight: inspectorColumn.height
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                Column {
                    id: inspectorColumn
                    width: inspectorFlick.width

                    // What is picked: the selected shape, the active tool,
                    // OCR, or nothing.
                    Item {
                        width: parent.width
                        height: 18 + 36 + 16
                        Rectangle {
                            id: focusTile
                            x: 20; y: 18
                            width: 36; height: 36; radius: 9
                            color: Theme.secondary
                            UIcon {
                                anchors.centerIn: parent
                                name: editorWindow.focusInfo.icon
                                iconStyle: "custom"
                                size: 18
                                color: Theme.accent
                            }
                        }
                        Column {
                            x: focusTile.x + focusTile.width + 12
                            width: panelSelSlot.x - 12 - x
                            anchors.verticalCenter: focusTile.verticalCenter
                            spacing: 1
                            Text {
                                width: parent.width
                                elide: Text.ElideRight
                                text: editorWindow.focusInfo.name
                                color: Theme.textPrimary
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }
                            Text {
                                width: parent.width
                                elide: Text.ElideRight
                                text: editorWindow.focusInfo.sub
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontS
                            }
                        }
                        Item {
                            id: panelSelSlot
                            x: parent.width - 16 - width
                            anchors.verticalCenter: focusTile.verticalCenter
                            width: selActions.visible ? selActions.width : 0
                            height: selActions.height
                        }
                        Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: editorWindow.hairline }
                    }

                    Item {
                        width: parent.width
                        height: propsColumn.height + 2 * 16 + 1
                        Column {
                            id: propsColumn
                            x: 20; y: 16
                            width: parent.width - 2 * 20
                            Item {
                                id: inspectorBody
                                objectName: "editorInspectorBody"
                                visible: ctxPanel.shown
                                width: parent.width
                                height: ctxPanel.height
                            }
                            Text {
                                visible: !ctxPanel.shown
                                width: parent.width
                                wrapMode: Text.WordWrap
                                text: qsTr("Pick a tool to see its options here.")
                                color: Theme.textTertiary
                                font.pixelSize: Theme.fontS
                            }
                        }
                        Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: editorWindow.hairline }
                    }

                    // The shapes on the image, top of the stack first. A
                    // click selects one, Ctrl or Shift adds it.
                    Item {
                        visible: editorWindow.shapes.length > 0
                        width: parent.width
                        height: shapesColumn.height + 14 + 12 + 1
                        Column {
                            id: shapesColumn
                            x: 12; y: 14
                            width: parent.width - 2 * 12
                            spacing: 6
                            Row {
                                x: 8
                                spacing: 8
                                Text {
                                    text: qsTr("Shapes")
                                    color: Theme.textSecondary
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    font.capitalization: Font.AllUppercase
                                    font.letterSpacing: 0.9
                                }
                                Text {
                                    text: editorWindow.shapes.length
                                    color: Theme.textTertiary
                                    font.family: "monospace"
                                    font.pixelSize: 11
                                    Accessible.ignored: true
                                }
                            }
                            Column {
                                objectName: "editorShapeList"
                                width: parent.width
                                spacing: 1
                                Accessible.role: Accessible.List
                                Accessible.name: qsTr("Shapes")
                                Repeater {
                                    model: editorWindow.shapes
                                    delegate: Rectangle {
                                        id: shapeRow
                                        required property var modelData
                                        function _activate(toggle) { canvas.selectAnnotIndex(modelData.index, toggle) }
                                        width: parent.width
                                        height: 34
                                        radius: 7
                                        color: modelData.selected ? Theme.secondary
                                               : shapeMouse.containsMouse ? Theme.surfaceTop : "transparent"
                                        Behavior on color { ColorAnimation { duration: Theme.animFast } }
                                        UIcon {
                                            id: shapeIconItem
                                            x: 8
                                            anchors.verticalCenter: parent.verticalCenter
                                            name: editorWindow.shapeIcon(shapeRow.modelData.tool)
                                            iconStyle: "custom"
                                            size: 16
                                            color: shapeRow.modelData.selected ? Theme.textPrimary : Theme.textSecondary
                                        }
                                        Text {
                                            x: shapeIconItem.x + shapeIconItem.width + 10
                                            width: shapeChip.x - 10 - x
                                            anchors.verticalCenter: parent.verticalCenter
                                            elide: Text.ElideRight
                                            text: shapeRow.modelData.name
                                            color: Theme.textPrimary
                                            font.pixelSize: 13
                                            font.weight: shapeRow.modelData.selected ? Font.DemiBold : Font.Normal
                                        }
                                        Rectangle {
                                            id: shapeChip
                                            x: parent.width - 8 - width
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 10; height: 10; radius: 3
                                            visible: shapeRow.modelData.color.a > 0
                                            color: shapeRow.modelData.color
                                        }
                                        MouseArea {
                                            id: shapeMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: (m) => shapeRow._activate((m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0)
                                        }
                                        activeFocusOnTab: true
                                        Keys.onSpacePressed: (e) => UKeys.activate(e, function () { shapeRow._activate(false) })
                                        Keys.onReturnPressed: (e) => UKeys.activate(e, function () { shapeRow._activate(false) })
                                        Keys.onEnterPressed: (e) => UKeys.activate(e, function () { shapeRow._activate(false) })
                                        Accessible.role: Accessible.ListItem
                                        Accessible.name: modelData.name
                                        Accessible.selectable: true
                                        Accessible.selected: modelData.selected
                                        Accessible.focusable: true
                                        Accessible.onPressAction: shapeRow._activate(false)
                                        UFocusRing { hostRadius: shapeRow.radius }
                                    }
                                }
                            }
                        }
                        Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: editorWindow.hairline }
                    }

                    // Actions on the whole picture, not on a shape.
                    Item {
                        width: parent.width
                        height: wholeColumn.height + 14 + 18
                        Column {
                            id: wholeColumn
                            x: 20; y: 14
                            width: parent.width - 2 * 20
                            spacing: 8
                            Text {
                                text: qsTr("On the whole image")
                                color: Theme.textSecondary
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: 0.9
                            }
                            WholeButton {
                                width: parent.width
                                iconName: "line-text-scan"
                                text: qsTr("Copy text")
                                onClicked: editorSession.ocrCopyText()
                            }
                            WholeButton {
                                id: panelRedact
                                objectName: "editorPanelRedact"
                                width: parent.width
                                iconName: "line-redact"
                                text: qsTr("Auto-redact")
                                enabled: !canvas.ocrBusy
                                onClicked: editorWindow.autoRedact(panelRedact)
                            }
                            WholeButton {
                                width: parent.width
                                visible: App.backgroundRemover.available
                                iconName: "line-remove-bg"
                                text: qsTr("Remove background")
                                enabled: !App.backgroundRemover.busy && !canvas.ocrMode
                                onClicked: { editorWindow.commitPendingText(); editorSession.removeBackground() }
                            }
                        }
                    }
                }
            }
        }

        // ---------- stage ----------
        // Panels: the column between the rail and the inspector, a dot grid
        // under the image. Floating: the whole window, a soft vignette under
        // the image and the pills.
        Rectangle {
            id: stage
            readonly property bool floating: editorWindow.floatingLayout
            x: floating ? 0 : toolRail.width
            y: floating ? 0 : topBar.height
            width: floating ? parent.width : inspector.x - x
            height: floating ? parent.height : statusBar.y - y
            color: Theme.backgroundDeep

            // 20 px tiles, one dot each: one tiny texture, repeated.
            Image {
                anchors.fill: parent
                visible: !stage.floating
                fillMode: Image.Tile
                sourceSize: Qt.size(20, 20)
                source: "data:image/svg+xml;utf8," + encodeURIComponent(
                    "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20'>"
                    + "<circle cx='10' cy='10' r='1' fill='" + Theme.accent + "' fill-opacity='0.10'/></svg>")
                Accessible.ignored: true
            }
            // Drawn small and stretched: a gradient loses nothing to the
            // scaling, and the texture stays 450x300.
            Image {
                visible: stage.floating
                anchors.centerIn: parent
                width: 1800; height: 1200
                sourceSize: Qt.size(450, 300)
                smooth: true
                source: "data:image/svg+xml;utf8," + encodeURIComponent(
                    "<svg xmlns='http://www.w3.org/2000/svg' width='450' height='300'>"
                    + "<defs><radialGradient id='g' cx='0.5' cy='0.5' r='0.5'>"
                    + "<stop offset='0' stop-color='" + Theme.primary + "'/>"
                    + "<stop offset='0.75' stop-color='" + Theme.backgroundDeep + "'/>"
                    + "</radialGradient></defs><rect width='450' height='300' fill='url(#g)'/></svg>")
                Accessible.ignored: true
            }

            Flickable {
                id: canvasFlick
                objectName: "editorCanvasFlick"  // the smoke test drives zoom through it
                anchors.fill: parent
                clip: true
                // Panning is done by hand below (drag with the hand, Space+drag,
                // middle drag, wheel, scrollbars), never by Flickable's own
                // flick: a drawing tool owns the left button.
                interactive: false
                // The clear area between the bars, in viewport coordinates.
                // Floating: below the top two rows of pills and above the
                // bottom one, which depend on the window's width alone, never
                // on the tool, so picking a tool never moves the image.
                readonly property real padSide: editorWindow.fitPad
                readonly property real padTop: stage.floating
                    ? toolRail.y + toolRail.height + editorWindow.pillGap : padSide
                readonly property real padBottom: stage.floating
                    ? editorWindow.pillEdge + editorWindow.pillH + editorWindow.pillGap : padSide
                readonly property real availW: Math.max(1, width - 2 * padSide)
                readonly property real availH: Math.max(1, height - padTop - padBottom)
                // Floating: how far wrapped tool options reach above the
                // bottom row: the image's bottom can be panned that far up,
                // out from under them. A margin, so the options wrapping
                // moves nothing on screen.
                bottomMargin: stage.floating && ctxPanel.visible
                              ? Math.max(0, ctxPanel.height - editorWindow.pillH) : 0
                onBottomMarginChanged: clampPan()
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
                readonly property bool canPan: contentWidth > width + 0.5 || contentHeight > height + 0.5 || bottomMargin > 0.5
                // Held Space turns any tool into the hand (shortcutScope).
                property bool spacePan: false

                function clampPan() {
                    contentX = Math.max(0, Math.min(contentWidth - width, contentX))
                    contentY = Math.max(0, Math.min(contentHeight - height + bottomMargin, contentY))
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

                // The image's drop shadow while it fits: drawn at a quarter
                // of the size and scaled up, so the blurred texture stays
                // small (a blur loses nothing to that).
                Item {
                    visible: canvas.width <= canvasFlick.availW + 0.5 && canvas.height <= canvasFlick.availH + 0.5
                    x: canvas.x
                    y: canvas.y
                    width: canvas.width / 4
                    height: canvas.height / 4
                    scale: 4
                    transformOrigin: Item.TopLeft
                    Rectangle {
                        anchors.fill: parent
                        color: Theme.backgroundDeep
                        layer.enabled: parent.visible
                        layer.effect: MultiEffect {
                            shadowEnabled: true
                            shadowColor: Theme.shadow
                            shadowBlur: 1.0
                            blurMax: 20
                            shadowVerticalOffset: 7.5
                        }
                    }
                }

                // The image's edge, for a screenshot of a dark window on the
                // dark stage. Outside the canvas, so it covers no pixel of it.
                Rectangle {
                    x: canvas.x - 1
                    y: canvas.y - 1
                    width: canvas.width + 2
                    height: canvas.height + 2
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.alpha(Theme.accent, stage.floating ? 0.14 : 0.12)
                }

                AnnotationCanvas {
                    id: canvas
                    objectName: "editorCanvas"
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
                    shapeMenu: true
                    // Commit first: an open re-edit writes into the selected
                    // shape, which the selection below is about to change.
                    // An emptied Text shape is removed by that commit, so the
                    // menu opens only if a shape is still under the pointer.
                    onShapeMenuRequested: (x, y) => {
                        editorWindow.commitPendingText()
                        const img = canvas.toImage(x, y)
                        canvas.selectAnnotAt(img.x, img.y, true)
                        if (!canvas.hasAnnotSelection)
                            return
                        shapeMenuAnchor.x = canvas.x + x
                        shapeMenuAnchor.y = canvas.y + y
                        shapeContextMenu.open()
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

                Item { id: shapeMenuAnchor; width: 1; height: 1 }
                UMenuButton {
                    id: shapeContextMenu
                    objectName: "editorShapeMenu"
                    visible: false
                    anchorItem: shapeMenuAnchor
                    preferAbove: false
                    // The hidden trigger cannot take the keyboard back.
                    onMenuOpenChanged: if (!menuOpen) shortcutScope.forceActiveFocus()
                    actions: [
                        { label: qsTr("Duplicate"), hint: "Ctrl+D",
                          trigger: function () { canvas.duplicateSelected() } },
                        { label: qsTr("Bring to front"), hint: "Ctrl+Shift+]", separatorBefore: true,
                          trigger: function () { canvas.arrangeSelected(AnnotationCanvas.BringToFront) } },
                        { label: qsTr("Bring forward"), hint: "Ctrl+]",
                          trigger: function () { canvas.arrangeSelected(AnnotationCanvas.BringForward) } },
                        { label: qsTr("Send backward"), hint: "Ctrl+[",
                          trigger: function () { canvas.arrangeSelected(AnnotationCanvas.SendBackward) } },
                        { label: qsTr("Send to back"), hint: "Ctrl+Shift+[",
                          trigger: function () { canvas.arrangeSelected(AnnotationCanvas.SendToBack) } },
                        { label: canvas.selectedCount > 1 ? qsTr("Delete %n shape(s)", "", canvas.selectedCount)
                                                         : qsTr("Delete shape"),
                          hint: "Del", separatorBefore: true,
                          trigger: function () { canvas.removeSelectedAnnot() } }
                    ]
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
        }

        // The status line and the zoom under the image (panels); the zoom
        // alone in the bottom-right corner (floating). The zoom keeps the
        // same steps as the wheel / Ctrl+± / Ctrl+0 for anyone who never
        // finds those, and scales the view only: the image, the annotations
        // and the export are untouched. The percentage has a fixed width so
        // "25%" -> "100%" never shifts the + button out from under a pointer
        // clicking it repeatedly.
        Pill {
            id: statusBar
            objectName: "editorStatusBar"
            panelColor: Theme.background
            x: floating ? parent.width - width - editorWindow.pillEdge : toolRail.width
            y: floating ? parent.height - height - editorWindow.pillEdge : parent.height - height
            width: floating ? zoomRow.width + 2 * editorWindow.pillPad : inspector.x - toolRail.width
            height: floating ? editorWindow.pillH : 40
            z: 20

            DragArea { anchors.fill: parent }
            Rectangle {
                visible: !statusBar.floating
                width: parent.width; height: 1
                color: Theme.alpha(Theme.accent, 0.08)
            }

            // Floating shows the state in the title pill instead.
            Text {
                id: statusLabel
                visible: !statusBar.floating
                x: 16
                width: Math.max(0, zoomRow.x - 12 - x)
                anchors.verticalCenter: parent.verticalCenter
                text: editorSession.statusText !== "" ? editorSession.statusText
                      : canvas.imageSize.width + " × " + canvas.imageSize.height + " px · "
                        + qsTr("%n shape(s)", "", editorWindow.shapes.length) + " · "
                        + App.settings.imageFormat.toUpperCase()
                color: Theme.textSecondary
                font.family: "monospace"
                font.pixelSize: 11
                elide: Text.ElideMiddle
            }

            Row {
                id: zoomRow
                readonly property bool floating: statusBar.floating
                readonly property int h: floating ? editorWindow.chipSize : 28
                x: floating ? editorWindow.pillPad : parent.width - width - 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Accessible.role: Accessible.ToolBar
                Accessible.name: qsTr("Zoom")

                UIconButton {
                    id: zoomOut
                    objectName: "editorZoomOut"
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "minus"; iconSize: 14
                    width: zoomRow.floating ? 40 : 30; height: zoomRow.h
                    tooltip: qsTr("Zoom out (Ctrl+-)")
                    enabled: canvasFlick.effectiveScale > canvasFlick.minZoom + 0.001
                    onClicked: canvasFlick.zoomBy(1 / 1.2)
                }
                Rectangle {
                    id: zoomPct
                    objectName: "editorZoomPct"
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(zoomRow.floating ? 58 : 44, Math.ceil(zoomPctMetrics.width) + 12)
                    height: zoomRow.h
                    radius: zoomRow.floating ? 14 : 6
                    color: zoomPctMouse.containsMouse ? (zoomRow.floating ? Theme.alpha(Theme.accent, 0.18) : Theme.surfaceTop)
                                                      : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    function _activate() { canvasFlick.toggleZoom() }
                    TextMetrics { id: zoomPctMetrics; font: zoomPctText.font; text: "400%" }
                    Text {
                        id: zoomPctText
                        anchors.centerIn: parent
                        text: Math.round(canvasFlick.effectiveScale * 100) + "%"
                        color: Theme.textPrimary
                        font.family: zoomRow.floating ? Qt.application.font.family : "monospace"
                        font.pixelSize: zoomRow.floating ? 13 : 12
                        font.weight: zoomRow.floating ? Font.ExtraBold : Font.Normal
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
                    UFocusRing { hostRadius: zoomPct.radius }
                }
                UIconButton {
                    id: zoomIn
                    objectName: "editorZoomIn"
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "plus"; iconSize: 14
                    width: zoomRow.floating ? 40 : 30; height: zoomRow.h
                    tooltip: qsTr("Zoom in (Ctrl++)")
                    enabled: canvasFlick.effectiveScale < canvasFlick.maxZoom - 0.001
                    onClicked: canvasFlick.zoomBy(1.2)
                }
                Sep {
                    visible: !zoomRow.floating
                    anchors.verticalCenter: parent.verticalCenter
                    length: 18
                }
                TextChip {
                    objectName: "editorZoomFit"
                    visible: !zoomRow.floating
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Fit")
                    tip: qsTr("Fit to window (Ctrl+0)")
                    active: canvasFlick.zoom === 0
                    onClicked: { canvasFlick.zoom = 0; canvasFlick.clampPan() }
                }
                TextChip {
                    objectName: "editorZoom100"
                    visible: !zoomRow.floating
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("1:1")
                    tip: qsTr("Actual size (100%)")
                    active: canvasFlick.zoom > 0 && Math.abs(canvasFlick.zoom - 1) < 0.001
                    onClicked: canvasFlick.zoomTo(Math.min(1, canvasFlick.maxZoom))
                }
            }
        }
    }
    UWindowResizer { active: !App.settings.useSystemDecoration }
}
