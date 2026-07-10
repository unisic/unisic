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
            && canvas.hoverObjectRect.width <= 0

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
            if (pos === "follow")
                return Math.min(sel.y + sel.height + 12, height - toolbar.height - 8)
            if (pos.indexOf("top") >= 0) return m + 44
            if (pos.indexOf("bottom") >= 0) return height - toolbar.height - m
            return height / 2 - toolbar.height / 2
        }

        // Commit the in-place text box. Shared by TextInput.onAccepted and the
        // Enter branch below, so a confirm works whether keyboard focus landed
        // on the text field or stayed on the overlay root (Wayland activation
        // is unreliable for a hotkey-spawned frameless window).
        function commitTextBox() {
            canvas.commitText(textEditor.imgX, textEditor.imgY, textField.text)
            textEditor.visible = false
            root.forceActiveFocus()
        }
        function closeTextBox() {
            textEditor.visible = false
            root.forceActiveFocus()
        }

        Keys.onPressed: (e) => {
            // While the text box is open, Enter CONFIRMS the text (never fires
            // the capture), Escape closes the box (never cancels the session),
            // and every other key is left for the TextInput to type.
            if (textEditor.visible) {
                if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
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
            if (e.key === Qt.Key_Escape) {
                overlayController.cancel()
            } else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                overlayController.confirmFromWindow(overlayWindow)
            } else if (e.key === Qt.Key_A && (e.modifiers & Qt.ControlModifier)) {
                canvas.selectAll()
            } else if (e.key === Qt.Key_Z && (e.modifiers & Qt.ControlModifier)) {
                if (e.modifiers & Qt.ShiftModifier) canvas.redo()
                else canvas.undo()
            } else if (e.key === Qt.Key_Y && (e.modifiers & Qt.ControlModifier)) {
                canvas.redo()
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
        UColorPopup {
            id: overlayColorDialog
            onPicked: (c) => canvas.strokeColor = c
        }
        UColorPopup {
            id: overlayFillDialog
            showAlpha: true
            onPicked: (c) => { canvas.shapeFillColor = c; canvas.shapeFillEnabled = true }
        }

        AnnotationCanvas {
            id: canvas
            objectName: "overlayCanvas"
            anchors.fill: parent
            selectionMode: true
            tool: AnnotationCanvas.None
            // Smart pick: click = select the detected object under the cursor;
            // drag still draws a manual rectangle (Settings > Capture).
            smartPick: App.settings.smartPick
            // Selection chrome follows the selected app theme (was fixed purple).
            uiAccent: Theme.accent
            uiScrim: Theme.primary
            Component.onCompleted: {
                strokeColor = App.settings.editorStrokeColor
                strokeWidth = App.settings.editorStrokeWidth
                fontSize = App.settings.editorFontSize
                shapeFillColor = App.settings.editorFillColor
                shapeFillEnabled = App.settings.editorFillEnabled
            }
            onShapeFillColorChanged: App.settings.editorFillColor = shapeFillColor
            onShapeFillEnabledChanged: App.settings.editorFillEnabled = shapeFillEnabled
            onSelectionConfirmed: overlayController.confirmFromWindow(overlayWindow)
            onTextRequested: (x, y) => {
                textEditor.imgX = x
                textEditor.imgY = y
                textEditor.visible = true
                textField.text = ""
                textField.forceActiveFocus()
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

        // Smart-pick hover badge: highlighted object's size + nesting level
        // (scroll cycles inner element ⇄ container ⇄ whole screen).
        Rectangle {
            visible: !canvas.hasSelection && canvas.hoverObjectRect.width > 0
            readonly property real hx: canvas.hoverObjectRect.x * canvas.renderScale
            readonly property real hy: canvas.hoverObjectRect.y * canvas.renderScale
            readonly property real hw: canvas.hoverObjectRect.width * canvas.renderScale
            x: Math.max(4, Math.min(hx + hw / 2 - width / 2, parent.width - width - 8))
            y: Math.max(4, hy - height - 10)
            width: hoverDimText.implicitWidth + 22
            height: 28
            radius: 14
            color: "#000000"
            opacity: 0.8
            Text {
                id: hoverDimText
                anchors.centerIn: parent
                text: canvas.hoverObjectKind + "  ·  "
                      + Math.round(canvas.hoverObjectRect.width) + " × "
                      + Math.round(canvas.hoverObjectRect.height)
                      + (canvas.hoverDepthCount > 1
                         ? "   " + (canvas.hoverDepth + 1) + "/" + canvas.hoverDepthCount + " ↕"
                         : "")
                color: "#FFFFFF"
                font.pixelSize: 12
                font.family: "monospace"
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

        // Object-pick status (segmentation progress / cutout ready).
        // Sits opposite the toolbar: with a top-anchored toolbar the pill
        // moves to the bottom so the two never overlap.
        Rectangle {
            readonly property bool toolbarOnTop:
                App.settings.overlayToolbarPosition.indexOf("top") >= 0
            visible: canvas.tool === AnnotationCanvas.ObjectPick && canvas.hasSelection
                     && (canvas.segmenting || canvas.hasObjectMask)
            anchors.horizontalCenter: parent.horizontalCenter
            y: toolbarOnTop ? parent.height - height - 42 : 42
            width: segText.implicitWidth + 40
            height: 36
            radius: 18
            color: Theme.primary
            opacity: 0.92
            border.width: 1
            border.color: Theme.divider
            Text {
                id: segText
                anchors.centerIn: parent
                text: canvas.segmenting
                      ? qsTr("Separating object from background…")
                      : qsTr("Background removed. Enter or double-click captures the cutout")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontS + 1
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
                    const drag = App.settings.smartPick
                               ? qsTr("Click an object (scroll = level) or drag to select")
                               : qsTr("Drag to select")
                    return annotationToolsEnabled
                           ? drag + qsTr(" · Ctrl+drag to move · annotate with the toolbar · Enter or double-click to capture · Esc to cancel")
                           : drag + qsTr(" · Ctrl+drag to move · Enter to start · Esc to cancel")
                }
                color: Theme.textPrimary
                font.pixelSize: Theme.fontS + 1
            }
        }

        // Floating toolbar (position configurable; default follows the selection)
        Rectangle {
            id: toolbar
            visible: canvas.hasSelection
            x: root.toolbarX()
            y: root.toolbarY()
            width: toolRow.implicitWidth + 24
            height: 54
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

            Row {
                id: toolRow
                anchors.centerIn: parent
                spacing: 5

                Row {
                    visible: annotationToolsEnabled
                    spacing: 5

                    Repeater {
                        model: ToolCatalog.visibleFor("overlay", App.settings.hiddenTools)
                        delegate: ToolChip {
                            iconName: ToolCatalog.toolIconName(modelData, App.settings.editorIconStyle, App.settings.editorToolIcons)
                            iconStyle: App.settings.editorIconStyle
                            label: modelData.label
                            active: canvas.tool === modelData.tool
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: canvas.tool = modelData.tool
                        }
                    }
                    ToolChip { iconName: "edit-undo"; label: qsTr("Undo"); enabled: canvas.canUndo; anchors.verticalCenter: parent.verticalCenter; onClicked: canvas.undo() }
                    ToolChip { iconName: "edit-redo"; label: qsTr("Redo"); enabled: canvas.canRedo; anchors.verticalCenter: parent.verticalCenter; onClicked: canvas.redo() }

                    Rectangle { width: 1; height: 28; color: Theme.divider; anchors.verticalCenter: parent.verticalCenter }

                    Repeater {
                        model: Theme.swatches
                        delegate: ColorDot {
                            dotColor: modelData
                            active: Qt.colorEqual(canvas.strokeColor, modelData)
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: canvas.strokeColor = modelData
                        }
                    }
                    UIconButton {
                        iconName: "color-picker"; iconSize: 15
                        width: 30; height: 30
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: overlayColorDialog.openWith(canvas.strokeColor)
                    }

                    Rectangle { width: 1; height: 28; color: Theme.divider; anchors.verticalCenter: parent.verticalCenter }

                    ToolChip {
                        iconName: "fill-color"
                        label: qsTr("Fill shapes")
                        active: canvas.shapeFillEnabled
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: canvas.shapeFillEnabled = !canvas.shapeFillEnabled
                    }
                    ColorDot {
                        dotColor: canvas.shapeFillColor
                        active: canvas.shapeFillEnabled
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: overlayFillDialog.openWith(canvas.shapeFillColor)
                    }

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
        }

        // In-place text entry for the Text tool. FocusScope so the TextInput
        // reliably owns the keyboard while open (a plain Item left focus
        // ambiguous, which is how Enter reached the root capture handler).
        FocusScope {
            id: textEditor
            property real imgX: 0
            property real imgY: 0
            property alias text: textField.text
            visible: false
            onVisibleChanged: {
                overlayController.textEditing = visible
                if (visible) textField.forceActiveFocus()
            }
            x: imgX * canvas.renderScale
            y: imgY * canvas.renderScale
            width: 320
            height: 40
            z: 300

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusS
                color: Qt.rgba(0, 0, 0, 0.6)
                border.width: 1
                border.color: Theme.accent
            }
            TextInput {
                id: textField
                anchors.fill: parent
                anchors.margins: 8
                focus: true
                color: canvas.strokeColor
                font.pixelSize: canvas.fontSize * canvas.renderScale
                font.bold: true
                // MUST accept Enter/Escape here so they do NOT bubble to the
                // overlay root: commitTextBox() sets textEditor.visible = false,
                // and the root handler — re-checking visibility AFTER that —
                // would then fall through to confirmFromWindow and fire the
                // capture. Accepting stops that propagation. (onAccepted alone
                // did not consume the key event, which was the bug.)
                Keys.onPressed: (e) => {
                    if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                        root.commitTextBox()
                        e.accepted = true
                    } else if (e.key === Qt.Key_Escape) {
                        root.closeTextBox()
                        e.accepted = true
                    }
                }
            }
        }
    }
}
