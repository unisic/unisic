import QtQuick
import QtQuick.Window
import QtQuick.Effects
import QtQuick.Dialogs
import Unisic
import "components"

Window {
    id: editorWindow
    width: Math.min(canvas.imageSize.width / Screen.devicePixelRatio + 96, Screen.desktopAvailableWidth * 0.9)
    height: Math.min(canvas.imageSize.height / Screen.devicePixelRatio + 210, Screen.desktopAvailableHeight * 0.9)
    minimumWidth: 820
    minimumHeight: 480
    visible: true
    title: qsTr("Unisic — Editor")
    color: Theme.background

    Component.onCompleted: editorSession.bindCanvas(canvas)

    function recentList() {
        var s = App.settings.recentColors
        return s ? s.split(",").filter(function (x) { return x.length > 0 }) : []
    }
    function addRecent(hex) {
        var list = recentList()
        hex = String(hex)
        list = list.filter(function (x) { return x.toLowerCase() !== hex.toLowerCase() })
        list.unshift(hex)
        if (list.length > 6) list = list.slice(0, 6)
        App.settings.recentColors = list.join(",")
    }

    ColorDialog {
        id: strokeDialog
        title: qsTr("Stroke color")
        selectedColor: canvas.strokeColor
        onAccepted: { canvas.strokeColor = selectedColor; editorWindow.addRecent(selectedColor) }
    }
    ColorDialog {
        id: fillDialog
        title: qsTr("Fill color")
        options: ColorDialog.ShowAlphaChannel
        selectedColor: canvas.shapeFillColor
        onAccepted: { canvas.shapeFillColor = selectedColor; canvas.shapeFillEnabled = true }
    }

    Item {
        id: shortcutScope
        anchors.fill: parent
        focus: true
        Keys.onPressed: (e) => {
            if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_Z) {
                if (e.modifiers & Qt.ShiftModifier) canvas.redo(); else canvas.undo()
            } else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_Y) canvas.redo()
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_S) editorSession.save()
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_C) editorSession.copyToClipboard()
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_U) editorSession.upload()
            else if (e.key === Qt.Key_Escape) editorWindow.close()
            else return
            e.accepted = true
        }

        // ---------- top toolbar ----------
        Rectangle {
            id: topBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spacingM
            height: 58
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

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingM
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                Repeater {
                    model: ToolCatalog.visibleFor("editor", App.settings.hiddenTools)
                    delegate: ToolChip {
                        iconName: ToolCatalog.toolIconName(modelData, App.settings.editorIconStyle, App.settings.editorToolIcons)
                        iconStyle: App.settings.editorIconStyle
                        label: modelData.label
                        active: canvas.tool === modelData.tool
                        onClicked: canvas.tool = modelData.tool
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

            Row {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingM
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                // Fill toggle (applies to rectangle/ellipse)
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
                    onClicked: fillDialog.open()
                }

                Rectangle { width: 1; height: 30; color: Theme.divider; anchors.verticalCenter: parent.verticalCenter }

                // Stroke color presets
                Repeater {
                    model: Theme.swatches
                    delegate: ColorDot {
                        dotColor: modelData
                        active: Qt.colorEqual(canvas.strokeColor, modelData)
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: canvas.strokeColor = modelData
                    }
                }
                // Recent colors
                Repeater {
                    model: editorWindow.recentList()
                    delegate: ColorDot {
                        dotColor: modelData
                        active: Qt.colorEqual(canvas.strokeColor, modelData)
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: canvas.strokeColor = modelData
                    }
                }
                // Custom color picker
                UIconButton {
                    iconName: "color-picker"
                    iconSize: 16
                    width: 30; height: 30
                    tooltip: qsTr("More colors")
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: strokeDialog.open()
                }

                Rectangle { width: 1; height: 30; color: Theme.divider; anchors.verticalCenter: parent.verticalCenter }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        text: qsTr("Stroke %1").arg(canvas.strokeWidth)
                        color: Theme.textTertiary
                        font.pixelSize: 10
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    USlider {
                        width: 100
                        from: 1; to: 16
                        value: canvas.strokeWidth
                        onMoved: (v) => canvas.strokeWidth = Math.round(v)
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

            Item {
                anchors.fill: parent
                anchors.margins: Theme.spacingM

                AnnotationCanvas {
                    id: canvas
                    property real fitScale: imageSize.width > 0
                        ? Math.min(parent.width / imageSize.width, parent.height / imageSize.height, 1)
                        : 1
                    width: imageSize.width * fitScale
                    height: imageSize.height * fitScale
                    anchors.centerIn: parent
                    selectionMode: canvas.tool === AnnotationCanvas.Crop
                    Component.onCompleted: {
                        strokeColor = App.settings.editorStrokeColor
                        strokeWidth = App.settings.editorStrokeWidth
                        fontSize = App.settings.editorFontSize
                        shapeFillColor = App.settings.editorFillColor
                        shapeFillEnabled = App.settings.editorFillEnabled
                    }
                    onStrokeColorChanged: App.settings.editorStrokeColor = String(strokeColor)
                    onStrokeWidthChanged: App.settings.editorStrokeWidth = strokeWidth
                    onFontSizeChanged: App.settings.editorFontSize = fontSize
                    onShapeFillColorChanged: App.settings.editorFillColor = String(shapeFillColor)
                    onShapeFillEnabledChanged: App.settings.editorFillEnabled = shapeFillEnabled
                    onTextRequested: (x, y) => {
                        editorTextInput.imgX = x
                        editorTextInput.imgY = y
                        editorTextInput.visible = true
                        editorTextField.text = ""
                        editorTextField.forceActiveFocus()
                    }
                }

                Item {
                    id: editorTextInput
                    property real imgX: 0
                    property real imgY: 0
                    visible: false
                    x: canvas.x + imgX * canvas.renderScale
                    y: canvas.y + imgY * canvas.renderScale
                    width: 320
                    height: 40
                    z: 50
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusS
                        color: Qt.rgba(0, 0, 0, 0.6)
                        border.width: 1
                        border.color: Theme.accent
                    }
                    TextInput {
                        id: editorTextField
                        anchors.fill: parent
                        anchors.margins: 8
                        color: canvas.strokeColor
                        font.pixelSize: Math.max(10, canvas.fontSize * canvas.renderScale)
                        font.bold: true
                        // Return focus to the shortcut scope, else Ctrl+Z/S/C/U
                        // and Escape stay dead after using the text tool.
                        onAccepted: {
                            canvas.commitText(editorTextInput.imgX, editorTextInput.imgY, text)
                            editorTextInput.visible = false
                            shortcutScope.forceActiveFocus()
                        }
                        Keys.onEscapePressed: {
                            editorTextInput.visible = false
                            shortcutScope.forceActiveFocus()
                        }
                    }
                }
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
                      : canvas.imageSize.width + " × " + canvas.imageSize.height + " px"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontS + 1
                elide: Text.ElideMiddle
            }

            Row {
                id: actionRow
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingM
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingS

                UButton { iconName: "edit-copy"; text: qsTr("Copy");   variant: "tonal"; onClicked: editorSession.copyToClipboard() }
                UButton { iconName: "document-save"; text: qsTr("Save"); variant: "tonal"; onClicked: editorSession.save() }
                UButton {
                    iconName: "document-send"; text: App.uploads.busy ? qsTr("Uploading…") : qsTr("Upload")
                    enabled: !App.uploads.busy
                    onClicked: editorSession.upload()
                }
                UButton {
                    visible: App.ocrAvailable
                    iconName: "ocr"; text: qsTr("Copy text"); variant: "tonal"
                    onClicked: editorSession.ocrCopyText()
                }
                UButton { iconName: "close"; text: qsTr("Close"); variant: "ghost"; onClicked: editorWindow.close() }
            }
        }
    }
}
