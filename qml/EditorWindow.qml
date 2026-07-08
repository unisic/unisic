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
                    Screen.desktopAvailableWidth * 0.9)
    height: Math.min(Math.max(minimumHeight,
                              canvas.imageSize.height / Screen.devicePixelRatio
                              + topBar.height + bottomBar.height + 88),
                     Screen.desktopAvailableHeight * 0.9)
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
            else if ((e.modifiers & Qt.ControlModifier) && (e.key === Qt.Key_Plus || e.key === Qt.Key_Equal)) canvasFlick.zoomBy(1.2)
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_Minus) canvasFlick.zoomBy(1 / 1.2)
            else if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_0) canvasFlick.zoom = 0
            else if (e.key === Qt.Key_Escape) editorWindow.close()
            else return
            e.accepted = true
        }

        // ---------- top toolbar ----------
        // The two control groups live in a Flow: when the window is too narrow
        // for one row they wrap instead of overlapping, and the bar grows.
        Rectangle {
            id: topBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spacingM
            height: barFlow.implicitHeight + 18
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

            Flow {
                id: barFlow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacingM
                anchors.rightMargin: Theme.spacingM
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingL

                Row {
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
                    spacing: 6
                    height: 40

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
                function zoomBy(f) {
                    // Keep the viewport center stable across the zoom step.
                    var cx = canvas.width > 0 ? (contentX + width / 2 - canvas.x) / canvas.width : 0.5
                    var cy = canvas.height > 0 ? (contentY + height / 2 - canvas.y) / canvas.height : 0.5
                    zoom = Math.max(minZoom, Math.min(maxZoom, effectiveScale * f))
                    contentX = canvas.x + cx * canvas.width - width / 2
                    contentY = canvas.y + cy * canvas.height - height / 2
                    clampPan()
                }

                WheelHandler {
                    acceptedModifiers: Qt.ControlModifier
                    onWheel: (ev) => canvasFlick.zoomBy(ev.angleDelta.y > 0 ? 1.2 : 1 / 1.2)
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
                    x: Math.max(0, (canvasFlick.width - width) / 2)
                    y: Math.max(0, (canvasFlick.height - height) / 2)
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
                    // Scale with zoom, or the zoomed font gets clipped by a
                    // fixed-size box.
                    width: 320 * Math.max(1, canvas.renderScale)
                    height: 40 * Math.max(1, canvas.renderScale)
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
                      : canvas.imageSize.width + " × " + canvas.imageSize.height + " px · "
                        + Math.round(canvasFlick.effectiveScale * 100) + "%"
                        + (canvasFlick.zoom > 0 ? "" : qsTr(" (fit)"))
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
