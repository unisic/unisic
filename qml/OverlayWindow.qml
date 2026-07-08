import QtQuick
import QtQuick.Window
import QtQuick.Effects
import QtQuick.Dialogs
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

        Keys.onPressed: (e) => {
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

        ColorDialog {
            id: overlayColorDialog
            title: qsTr("Stroke color")
            selectedColor: canvas.strokeColor
            onAccepted: canvas.strokeColor = selectedColor
        }

        AnnotationCanvas {
            id: canvas
            objectName: "overlayCanvas"
            anchors.fill: parent
            selectionMode: true
            tool: AnnotationCanvas.None
            Component.onCompleted: {
                strokeColor = App.settings.editorStrokeColor
                strokeWidth = App.settings.editorStrokeWidth
                fontSize = App.settings.editorFontSize
            }
            onSelectionConfirmed: overlayController.confirmFromWindow(overlayWindow)
            onTextRequested: (x, y) => {
                textEditor.imgX = x
                textEditor.imgY = y
                textEditor.visible = true
                textField.text = ""
                textField.forceActiveFocus()
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
                text: annotationToolsEnabled
                      ? qsTr("Drag to select · annotate with the toolbar · Enter or double-click to capture · Esc to cancel")
                      : qsTr("Drag to select the recording region · Enter to start · Esc to cancel")
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
                        onClicked: overlayColorDialog.open()
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

        // In-place text entry for the Text tool
        Item {
            id: textEditor
            property real imgX: 0
            property real imgY: 0
            visible: false
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
                color: canvas.strokeColor
                font.pixelSize: canvas.fontSize * canvas.renderScale
                font.bold: true
                // Return focus to root — the plain Item is no focus scope, so
                // without this Escape/Enter/undo keys go dead after text entry
                // and the overlay can no longer be cancelled from the keyboard.
                onAccepted: {
                    canvas.commitText(textEditor.imgX, textEditor.imgY, text)
                    textEditor.visible = false
                    root.forceActiveFocus()
                }
                Keys.onEscapePressed: {
                    textEditor.visible = false
                    root.forceActiveFocus()
                }
            }
        }
    }
}
