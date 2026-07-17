import QtQuick
import Unisic

// Contextual tool-properties strip: renders only the controls relevant to the
// active tool (ToolCatalog `props` — "stroke", "width", "fill", "font").
// Shared by the editor top bar and the overlay pill. The hosts own the color
// pickers (editor: ColorDialog window, overlay: in-scene UColorPopup — a
// dialog window would never get input under the layer-shell keyboard grab),
// so picker requests are surfaced as signals instead of handled here.
Row {
    id: root

    required property var canvas
    property var props: []
    // Editor-only recent stroke colors (the overlay bar stays compact).
    property var recentColors: []
    signal strokePickerRequested()
    signal fillPickerRequested()
    signal textOutlinePickerRequested()
    signal textBackgroundPickerRequested()

    spacing: 6

    function has(p) { return props.indexOf(p) >= 0 }

    // ---- style presets ----
    // Presets only make sense for a tool that HAS a style; Blur/Crop and the
    // like declare no props, and a preset row floating next to nothing would
    // just be noise.
    readonly property bool presetsVisible: props.length > 0
    readonly property int maxPresets: 6
    property var presets: []

    // Every canvas style property a preset captures, keyed by its JSON name.
    // The style is global, so a preset restores all of it regardless of which
    // tool is active — switching tool never rewrites these.
    readonly property var _presetKeys: ({
        "stroke": "strokeColor", "width": "strokeWidth",
        "fill": "shapeFillColor", "fillOn": "shapeFillEnabled",
        "arrowHead": "arrowHeadStyle", "hlMode": "highlightMode",
        "stepSize": "stepSize", "fontSize": "fontSize",
        "fontFamily": "fontFamily", "fontBold": "fontBold",
        "fontItalic": "fontItalic", "fontUnderline": "fontUnderline",
        "textOutline": "textOutline", "textOutlineColor": "textOutlineColor",
        "textBg": "textBackground", "textBgColor": "textBackgroundColor"
    })

    Component.onCompleted: _loadPresets()

    Connections {
        target: App.settings
        // Both bars (and a second editor window) write the same setting: reload
        // so a preset saved in one shows up in the other without a restart.
        function onEditorStylePresetsChanged() { root._loadPresets() }
    }

    function _loadPresets() {
        var raw = App.settings.editorStylePresets
        if (!raw) { presets = []; return }
        try {
            var parsed = JSON.parse(raw)
            // Hand-edited or older configs must not take the bar down with them.
            presets = Array.isArray(parsed) ? parsed.slice(0, maxPresets) : []
        } catch (e) {
            console.warn("ToolPropsBar: ignoring unreadable style presets:", e)
            presets = []
        }
    }

    function _storePresets(list) {
        App.settings.editorStylePresets = JSON.stringify(list)
        // The Connections handler reloads; assign too so the row updates even
        // if the value happens to compare equal (no NOTIFY then).
        presets = list
    }

    function savePreset() {
        var p = {}
        for (var jsonKey in _presetKeys) {
            var v = canvas[_presetKeys[jsonKey]]
            // Colours must be serialized as strings — a QColor stringifies to
            // "#aarrggbb" via JSON only by accident of its toString.
            p[jsonKey] = (v !== undefined && v !== null && v.toString && typeof v === "object")
                         ? v.toString() : v
        }
        var list = presets.slice()
        list.push(p)
        _storePresets(list.slice(0, maxPresets))
    }

    function applyPreset(p) {
        for (var jsonKey in _presetKeys) {
            if (p[jsonKey] === undefined)
                continue // preset saved before this property existed
            canvas[_presetKeys[jsonKey]] = p[jsonKey]
        }
    }

    function removePreset(i) {
        if (i < 0 || i >= presets.length)
            return
        var list = presets.slice()
        list.splice(i, 1)
        _storePresets(list)
    }

    function presetMatchesCanvas(p) {
        for (var jsonKey in _presetKeys) {
            if (p[jsonKey] === undefined)
                continue
            var v = canvas[_presetKeys[jsonKey]]
            var isColor = (v !== undefined && v !== null && typeof v === "object" && v.toString)
            if (isColor ? !Qt.colorEqual(v, p[jsonKey]) : v !== p[jsonKey])
                return false
        }
        return true
    }

    // Stroke width is only meaningful for the freehand highlighter mode; the
    // rectangle/text bands ignore it, so hide the control there.
    readonly property bool showWidth:
        has("width") && (!has("highlightMode")
                         || canvas.highlightMode === AnnotationCanvas.HlFreehand)

    // ---- measure mode (distance line / size box) ----
    Row {
        visible: root.has("measureMode")
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        ToolChip {
            iconName: "measure"; iconStyle: "custom"; label: qsTr("Distance")
            active: root.canvas.measureMode === 0
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.canvas.measureMode = 0
        }
        ToolChip {
            iconName: "draw-rectangle"; iconStyle: "custom"; label: qsTr("Size")
            active: root.canvas.measureMode === 1
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.canvas.measureMode = 1
        }
    }
    Rectangle {
        visible: root.has("measureMode")
        width: 1; height: 28; color: Theme.divider
        anchors.verticalCenter: parent.verticalCenter
    }

    // ---- highlighter mode (freehand marker / rectangle band / text snap) ----
    Row {
        visible: root.has("highlightMode")
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        ToolChip {
            iconName: "draw-freehand"; iconStyle: "custom"; label: qsTr("Freehand")
            active: root.canvas.highlightMode === AnnotationCanvas.HlFreehand
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.canvas.highlightMode = AnnotationCanvas.HlFreehand
        }
        ToolChip {
            iconName: "draw-rectangle"; iconStyle: "custom"; label: qsTr("Rectangle")
            active: root.canvas.highlightMode === AnnotationCanvas.HlRect
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.canvas.highlightMode = AnnotationCanvas.HlRect
        }
        ToolChip {
            iconName: "draw-text"; iconStyle: "custom"; label: qsTr("Text")
            active: root.canvas.highlightMode === AnnotationCanvas.HlText
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.canvas.highlightMode = AnnotationCanvas.HlText
        }
    }
    Rectangle {
        visible: root.has("highlightMode")
        width: 1; height: 28; color: Theme.divider
        anchors.verticalCenter: parent.verticalCenter
    }

    // ---- stroke color ----
    Repeater {
        model: root.has("stroke") ? Theme.swatches : []
        delegate: ColorDot {
            dotColor: modelData
            active: Qt.colorEqual(root.canvas.strokeColor, modelData)
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.canvas.strokeColor = modelData
        }
    }
    Repeater {
        model: root.has("stroke") ? root.recentColors : []
        delegate: ColorDot {
            dotColor: modelData
            active: Qt.colorEqual(root.canvas.strokeColor, modelData)
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.canvas.strokeColor = modelData
        }
    }
    UIconButton {
        visible: root.has("stroke")
        iconName: "color-picker"
        iconSize: 16
        width: 30; height: 30
        tooltip: qsTr("More colors")
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.strokePickerRequested()
    }

    Rectangle {
        visible: root.has("stroke") && (root.showWidth || root.has("fill"))
        width: 1; height: 28; color: Theme.divider
        anchors.verticalCenter: parent.verticalCenter
    }

    // ---- stroke width ----
    Column {
        visible: root.showWidth
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text {
            text: qsTr("Stroke")
            color: Theme.textTertiary
            font.pixelSize: 10
            anchors.horizontalCenter: parent.horizontalCenter
        }
        UValueCombo {
            width: 86
            implicitHeight: 28
            values: [1, 2, 3, 4, 6, 8, 10, 12, 16, 20, 24, 32, 48, 64]
            from: 1; to: 64
            suffix: " px"
            value: root.canvas.strokeWidth
            onChanged: (v) => root.canvas.strokeWidth = v
        }
    }

    UComboBox {
        visible: root.has("arrowhead")
        width: 112
        anchors.verticalCenter: parent.verticalCenter
        model: [qsTr("Filled head"), qsTr("Open head"), qsTr("Double head")]
        currentIndex: root.canvas.arrowHeadStyle
        onActivated: (i) => root.canvas.arrowHeadStyle = i
    }

    Rectangle {
        visible: root.has("width") && root.has("fill")
        width: 1; height: 28; color: Theme.divider
        anchors.verticalCenter: parent.verticalCenter
    }

    // ---- step marker size ----
    Rectangle {
        visible: root.has("stepSize") && root.has("stroke")
        width: 1; height: 28; color: Theme.divider
        anchors.verticalCenter: parent.verticalCenter
    }
    Column {
        visible: root.has("stepSize")
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text {
            text: qsTr("Size")
            color: Theme.textTertiary
            font.pixelSize: 10
            anchors.horizontalCenter: parent.horizontalCenter
        }
        UValueCombo {
            width: 86
            implicitHeight: 28
            values: [16, 18, 20, 22, 26, 32, 40, 48, 56, 64, 80, 96]
            from: 16; to: 128
            suffix: " px"
            value: root.canvas.stepSize
            onChanged: (v) => root.canvas.stepSize = v
        }
    }

    // ---- shape fill ----
    ToolChip {
        visible: root.has("fill")
        iconName: "fill-color"
        label: qsTr("Fill shapes")
        active: root.canvas.shapeFillEnabled
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.canvas.shapeFillEnabled = !root.canvas.shapeFillEnabled
    }
    ColorDot {
        visible: root.has("fill")
        dotColor: root.canvas.shapeFillColor
        active: root.canvas.shapeFillEnabled
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.fillPickerRequested()
    }

    // ---- text styling ----
    Rectangle {
        visible: root.has("font") && root.has("stroke")
        width: 1; height: 28; color: Theme.divider
        anchors.verticalCenter: parent.verticalCenter
    }
    UComboBox {
        visible: root.has("font")
        width: 150
        anchors.verticalCenter: parent.verticalCenter
        searchable: true
        fontPreview: true
        property var fams: [qsTr("Default font")].concat(Qt.fontFamilies())
        model: fams
        currentIndex: {
            if (root.canvas.fontFamily === "") return 0
            var i = fams.indexOf(root.canvas.fontFamily)
            return i < 0 ? 0 : i
        }
        onActivated: (i) => root.canvas.fontFamily = (i === 0 ? "" : fams[i])
    }
    UValueCombo {
        visible: root.has("font")
        width: 88
        anchors.verticalCenter: parent.verticalCenter
        values: [8, 10, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 56, 64, 72, 96, 144]
        from: 8; to: 144
        suffix: " px"
        value: root.canvas.fontSize
        onChanged: (v) => root.canvas.fontSize = v
    }
    ToolChip {
        visible: root.has("font")
        icon: "B"; label: qsTr("Bold")
        active: root.canvas.fontBold
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.canvas.fontBold = !root.canvas.fontBold
    }
    ToolChip {
        visible: root.has("font")
        icon: "I"; label: qsTr("Italic")
        active: root.canvas.fontItalic
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.canvas.fontItalic = !root.canvas.fontItalic
    }
    ToolChip {
        visible: root.has("font")
        icon: "U"; label: qsTr("Underline")
        active: root.canvas.fontUnderline
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.canvas.fontUnderline = !root.canvas.fontUnderline
    }
    Rectangle {
        visible: root.has("font")
        width: 1; height: 28; color: Theme.divider
        anchors.verticalCenter: parent.verticalCenter
    }
    ToolChip {
        visible: root.has("font")
        iconName: "text-outline"
        label: qsTr("Text outline")
        active: root.canvas.textOutline
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.canvas.textOutline = !root.canvas.textOutline
    }
    ColorDot {
        visible: root.has("font")
        dotColor: root.canvas.textOutlineColor
        active: root.canvas.textOutline
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.textOutlinePickerRequested()
    }
    ToolChip {
        visible: root.has("font")
        iconName: "text-background"
        label: qsTr("Text background")
        active: root.canvas.textBackground
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.canvas.textBackground = !root.canvas.textBackground
    }
    ColorDot {
        visible: root.has("font")
        dotColor: root.canvas.textBackgroundColor
        active: root.canvas.textBackground
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.textBackgroundPickerRequested()
    }

    // ---- style presets ----
    // The style is GLOBAL (one set of properties on the canvas, not one per
    // tool), so a preset is just a snapshot of it. Stored as JSON in one
    // settings string; the schema lives here.
    Rectangle {
        visible: root.presetsVisible
        width: 1; height: 28; color: Theme.divider
        anchors.verticalCenter: parent.verticalCenter
    }
    Repeater {
        model: root.presetsVisible ? root.presets : []
        delegate: ColorDot {
            id: presetDot
            required property var modelData
            required property int index
            dotColor: modelData.stroke !== undefined ? modelData.stroke : "#FF4757"
            // A preset is "active" only when the WHOLE style matches, not just
            // the colour — two presets can share a colour and differ in width.
            active: root.presetMatchesCanvas(modelData)
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.applyPreset(modelData)
            UHoverTip {
                anchor: presetDot
                show: presetDot.hovered
                // No names to type: the width is what visibly separates two
                // presets of the same colour. Middle-click deletes, so the tip
                // has to say so — nothing else in the bar hints at it.
                text: qsTr("%1 px — middle-click to delete")
                          .arg(presetDot.modelData.width !== undefined ? presetDot.modelData.width : 0)
            }
            // Deliberately not a hover ✕: the dots are 26 px and sit in a row
            // the user clicks constantly to APPLY a preset — an adjacent delete
            // target that size is a misclick waiting to happen. Left clicks are
            // not accepted here, so they fall through to the dot's own handler.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.MiddleButton
                onClicked: root.removePreset(presetDot.index)
            }
        }
    }
    ToolChip {
        visible: root.presetsVisible && root.presets.length < root.maxPresets
        iconName: "plus"
        iconStyle: "custom"
        label: qsTr("Save current style as a preset")
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.savePreset()
    }
}
