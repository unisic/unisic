import QtQuick
import Unisic
import Unisic.Kit

// Contextual tool-properties strip: renders only the controls relevant to the
// active tool (ToolCatalog `props` - "stroke", "width", "fill", "font").
// Shared by the editor top bar and the overlay pill. The hosts own the color
// pickers (editor: ColorDialog window, overlay: in-scene UColorPopup - a
// dialog window would never get input under the layer-shell keyboard grab),
// so picker requests are surfaced as signals instead of handled here.
//
// ---- why this is a Flow and not a Row ----
// It used to be a plain unbounded Row. A Row lays every child out on one line
// whatever the host's width is, and BOTH hosts draw the bar inside a layered
// item (EditorWindow's topBar and OverlayWindow's toolbar both set
// `layer.enabled: true` for their MultiEffect shadow), so a layer renders the
// subtree into a texture the size of the item and everything past its right
// edge is simply never painted. Measured in an offscreen probe: with the text
// tool in an 880x560 editor the strip was 1057 px against an 832 px slot and
// six controls (text-outline chip and dot, text-background chip and dot, the
// divider and the save-preset chip) sat entirely outside the window, invisible
// and unreachable - that is the "controls stick out beyond the window" report.
// So the strip WRAPS: the host supplies `maxWidth`, the strip never claims
// more than that, and controls that do not fit move to a second line instead
// of falling off the edge. The wrap units are `Cell`s - one logical group of
// controls plus its divider - so a swatch row or a "Fill shapes" chip and its
// colour dot are never split across lines.
Flow {
    id: root

    required property var canvas
    property var props: []
    // Editor-only recent stroke colors (the overlay bar stays compact).
    property var recentColors: []
    // The widest the host can afford to give the strip. 0 (the default) means
    // "unconstrained" - the strip then takes its natural one-line width, which
    // is what a host that has already reserved room for it wants.
    property real maxWidth: 0
    // Whether these controls join the window's Tab chain. TRUE in the editor.
    // FALSE in the capture overlay, where Space and Enter mean "confirm the
    // capture" and the surface holds an exclusive keyboard grab - a focused
    // chip or combo there would swallow the confirm gesture. Every control
    // keeps its Accessible role/name either way, so AT-SPI can still read and
    // Press them without keyboard focus.
    //
    // Not a second spelling of the tab-chain opt-out: everything in the app
    // writes `activeFocusOnTab: false` on the control itself, and a Flow cannot
    // - it is not a focus stop, and setting the attached property on it would
    // just add a phantom one. This forwards that exact property into every
    // control in the strip, which is why it is named for them and not for us.
    property bool controlsFocusable: true
    signal strokePickerRequested()
    signal fillPickerRequested()
    signal textOutlinePickerRequested()
    signal textBackgroundPickerRequested()

    spacing: 6

    function has(p) { return props.indexOf(p) >= 0 }

    // ---- fitting the host ----
    // A Flow needs an explicit width to know where to wrap, and its own
    // implicitWidth is the width of the ALREADY WRAPPED content, so it cannot
    // be used to ask "would this fit on one line?". naturalWidth answers that
    // by adding the cells up itself.
    //
    // These walk `children`, and that IS reactive: QQuickItem declares children
    // as a property with NOTIFY childrenChanged (qquickitem.h), so a cell
    // arriving or leaving re-runs the binding, and every `visible` /
    // `implicitWidth` read below registers as a dependency of its own for a
    // cell that merely changes size. Measured in an offscreen probe against the
    // real component: parenting a 120 px item into the strip at runtime moved
    // the width 307 -> 433, and giving the swatch row two recent colours moved
    // it 433 -> 497 - both without anything re-assigning props.
    readonly property real naturalWidth: {
        var w = 0
        var n = 0
        for (var i = 0; i < children.length; ++i) {
            var c = children[i]
            if (!c || !c.visible)
                continue
            w += c.implicitWidth
            ++n
        }
        return n > 0 ? w + spacing * (n - 1) : 0
    }
    // The width the strip actually PAINTS once it has wrapped into `limit`:
    // Flow's own rule (a cell goes on the current line unless it would cross
    // the edge and it is not alone there), so this is the widest line it ends
    // up laying out. A cell wider than the limit still overflows its line -
    // exactly as the Flow draws it - and this reports that too.
    function wrappedWidth(limit) {
        var widest = 0
        var line = 0
        var n = 0
        for (var i = 0; i < children.length; ++i) {
            var c = children[i]
            if (!c || !c.visible)
                continue
            var w = c.implicitWidth
            if (n > 0 && line + spacing + w > limit) {
                widest = Math.max(widest, line)
                line = w
            } else {
                line = n > 0 ? line + spacing + w : w
            }
            ++n
        }
        return Math.max(widest, line)
    }
    // Every cell is the same height so that controls of different sizes (26 px
    // dots, 28 px combos, 40 px chips, the 44 px labelled combos) line up
    // across a wrapped row - a Flow top-aligns whatever is on one line.
    readonly property real cellHeight: {
        var h = 40
        for (var i = 0; i < children.length; ++i) {
            var c = children[i]
            if (!c || !c.visible)
                continue
            h = Math.max(h, c.implicitHeight)
        }
        return h
    }
    // Never wider than the host allows, never wider than the content needs -
    // and, once it wraps, never wider than what it actually DRAWS. The rect has
    // to match the paint, because a host sizes itself from what the strip
    // reports: the overlay pill's Flow adds up its children's implicitWidth,
    // which for this one is the wrapped content. Measured with a 400 px limit:
    // the arrow tool wrapped into 261 px of content while the item's own rect
    // still claimed the whole 400 - so a pill sized from the 261 it reports had
    // a 139 px wider rect sitting inside it, and "nothing sticks out" was only
    // ever true of the pixels.
    // The 160 floor keeps a host whose arithmetic went negative from
    // collapsing the strip to nothing.
    width: maxWidth > 0 ? wrappedWidth(Math.max(maxWidth, 160)) : naturalWidth

    // ---- one wrap unit ----
    // Controls that belong together (a chip and its colour dot, a group of
    // swatches, a label+combo, a trailing divider) go in one Cell so the wrap
    // can only happen BETWEEN groups.
    //
    // Every Cell must be given an explicit `visible` - a Cell with nothing
    // visible inside it has to drop out of the layout (a zero-width but
    // visible child still costs the Flow a spacing gap), and it CANNOT derive
    // that from its own content. Measured: a positioner reports content size 0
    // for children that are only EFFECTIVELY invisible (an invisible ancestor),
    // so `visible: holder.implicitWidth > 0` latches off the first time it is
    // false and the cell never comes back - the stroke-width Column and every
    // font control stayed dark forever once the strip had been created with an
    // empty `props`.
    component Cell: Item {
        default property alias content: holder.data
        implicitWidth: holder.implicitWidth
        implicitHeight: holder.implicitHeight
        width: implicitWidth
        height: root.cellHeight
        Row {
            id: holder
            spacing: 6
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // ---- a row of swatches is ONE tab stop ----
    // A ColorDot row is a radio group, not thirteen separate destinations. As
    // individual tab stops the text tool's strip cost 31 Tab presses to cross
    // (7 palette swatches + 6 recent colours + 6 style presets + the rest),
    // which made Tab useless for reaching anything after it. The GROUP is the
    // tab stop; Left/Right walk the dots inside it.
    //
    // The dots stay focusable INSIDE the scope (`focus:`, not
    // `activeFocusOnTab`) - that is what keeps every colour reachable from the
    // keyboard. Each one still draws its own UFocusRing from its own
    // activeFocus and still activates through its own _activate(), so pointer,
    // keyboard and AT-SPI keep the single path they always had. A FocusScope
    // (not a plain Item) is what makes `focus: true` on a dot local to the
    // group: in the capture overlay, where the strip is out of the tab chain
    // entirely, the group never takes active focus and so neither does any dot,
    // and the overlay's exclusive keyboard grab is untouched.
    //
    // Arrows only MOVE the ring; Space/Enter picks. Moving must not apply a
    // style preset in passing - that rewrites every canvas style property.
    component DotGroup: FocusScope {
        id: group
        // How many dots the caller put in, and which one Tab lands on.
        property int count: 0
        property int entryIndex: 0
        property int currentIndex: 0
        default property alias content: groupRow.data

        implicitWidth: groupRow.implicitWidth
        implicitHeight: groupRow.implicitHeight
        width: implicitWidth
        height: implicitHeight

        // Entering the row starts on the dot that is in effect right now, not
        // on whatever was last walked past - the same place the eye is.
        onActiveFocusChanged: if (activeFocus) group.currentIndex = group._clamp(group.entryIndex)

        function _clamp(i) { return Math.max(0, Math.min(group.count - 1, i)) }
        function _step(e, delta) {
            if (!UKeys.claim(e))
                return
            group.currentIndex = group._clamp(group.currentIndex + delta)
        }
        Keys.onLeftPressed: (e) => group._step(e, -1)
        Keys.onRightPressed: (e) => group._step(e, 1)

        // The dots carry their own names and Button roles; this only groups them.
        Accessible.role: Accessible.Grouping

        Row { id: groupRow; spacing: 6 }
    }

    // ---- style presets ----
    // Presets only make sense for a tool that HAS a style; Blur/Crop and the
    // like declare no props, and a preset row floating next to nothing would
    // just be noise.
    readonly property bool presetsVisible: props.length > 0
    readonly property int maxPresets: 6
    property var presets: []

    // Every canvas style property a preset captures, keyed by its JSON name.
    // The style is global, so a preset restores all of it regardless of which
    // tool is active - switching tool never rewrites these.
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
            // Colours must be serialized as strings - a QColor stringifies to
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
    Cell {
        visible: root.has("measureMode")
        Row {
            visible: root.has("measureMode")
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            ToolChip {
                activeFocusOnTab: root.controlsFocusable && enabled
                iconName: "measure"; iconStyle: "custom"; label: qsTr("Distance")
                active: root.canvas.measureMode === 0
                anchors.verticalCenter: parent.verticalCenter
                onClicked: root.canvas.measureMode = 0
            }
            ToolChip {
                activeFocusOnTab: root.controlsFocusable && enabled
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
    }

    // ---- highlighter mode (freehand marker / rectangle band / text snap) ----
    Cell {
        visible: root.has("highlightMode")
        Row {
            visible: root.has("highlightMode")
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            ToolChip {
                activeFocusOnTab: root.controlsFocusable && enabled
                iconName: "draw-freehand"; iconStyle: "custom"; label: qsTr("Freehand")
                active: root.canvas.highlightMode === AnnotationCanvas.HlFreehand
                anchors.verticalCenter: parent.verticalCenter
                onClicked: root.canvas.highlightMode = AnnotationCanvas.HlFreehand
            }
            ToolChip {
                activeFocusOnTab: root.controlsFocusable && enabled
                iconName: "draw-rectangle"; iconStyle: "custom"; label: qsTr("Rectangle")
                active: root.canvas.highlightMode === AnnotationCanvas.HlRect
                anchors.verticalCenter: parent.verticalCenter
                onClicked: root.canvas.highlightMode = AnnotationCanvas.HlRect
            }
            ToolChip {
                activeFocusOnTab: root.controlsFocusable && enabled
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
    }

    // ---- stroke color ----
    // The palette swatches and the recent colours are ONE group: they are the
    // same choice offered twice, so Left/Right runs across both without a stop
    // in between.
    Cell {
        visible: root.has("stroke")
        DotGroup {
            id: swatchGroup
            visible: root.has("stroke")
            anchors.verticalCenter: parent.verticalCenter
            activeFocusOnTab: root.controlsFocusable && swatchGroup.count > 0

            readonly property int baseCount: root.has("stroke") ? Theme.swatches.length : 0
            readonly property int recentCount: root.has("stroke") ? root.recentColors.length : 0
            count: swatchGroup.baseCount + swatchGroup.recentCount
            entryIndex: swatchGroup._selected()

            Accessible.name: qsTr("Stroke colour")
            Accessible.description: qsTr("Left and right arrows move between the swatches, Space picks one")

            // Where the ring lands on entry: the swatch holding the colour the
            // canvas is drawing with, or the first one when it came from the picker.
            function _selected() {
                for (var i = 0; i < swatchGroup.baseCount; ++i)
                    if (Qt.colorEqual(root.canvas.strokeColor, Theme.swatches[i]))
                        return i
                for (var j = 0; j < swatchGroup.recentCount; ++j)
                    if (Qt.colorEqual(root.canvas.strokeColor, root.recentColors[j]))
                        return swatchGroup.baseCount + j
                return 0
            }

            Repeater {
                model: root.has("stroke") ? Theme.swatches : []
                delegate: ColorDot {
                    // Not a tab stop of its own - the group is (see DotGroup).
                    activeFocusOnTab: false
                    focus: index === swatchGroup.currentIndex
                    // ...but it does take keyboard focus inside the group, so it
                    // must not tell a screen reader otherwise. ColorDot derives
                    // this from activeFocusOnTab, which is only the right answer
                    // for a control that is its own tab stop.
                    Accessible.focusable: root.controlsFocusable
                    // ColorDot carries no label of its own, so the hosts have to name
                    // every swatch - otherwise a screen reader reads a row of
                    // identical nameless buttons.
                    accessibleName: qsTr("Stroke colour %1").arg(String(modelData))
                    dotColor: modelData
                    active: Qt.colorEqual(root.canvas.strokeColor, modelData)
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: root.canvas.strokeColor = modelData
                }
            }
            Repeater {
                model: root.has("stroke") ? root.recentColors : []
                delegate: ColorDot {
                    activeFocusOnTab: false
                    focus: swatchGroup.baseCount + index === swatchGroup.currentIndex
                    Accessible.focusable: root.controlsFocusable
                    accessibleName: qsTr("Recent stroke colour %1").arg(String(modelData))
                    dotColor: modelData
                    active: Qt.colorEqual(root.canvas.strokeColor, modelData)
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: root.canvas.strokeColor = modelData
                }
            }
        }
        UIconButton {
            activeFocusOnTab: root.controlsFocusable && enabled
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
    }

    // ---- stroke width / arrow head ----
    Cell {
        visible: root.showWidth || root.has("arrowhead")
                 || (root.has("width") && root.has("fill"))
        Column {
            visible: root.showWidth
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                text: qsTr("Stroke")
                color: Theme.textTertiary
                font.pixelSize: 10
                anchors.horizontalCenter: parent.horizontalCenter
                // The combo below carries the spoken name already.
                Accessible.ignored: true
            }
            UValueCombo {
                activeFocusOnTab: root.controlsFocusable && enabled
                accessibleName: qsTr("Stroke width")
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
            activeFocusOnTab: root.controlsFocusable && enabled
            accessibleName: qsTr("Arrow head")
            visible: root.has("arrowhead")
            // 150, not 112: at 112 even English "Double head" elided (84 px of
            // text into a 72 px slot), and every translation was worse. This is
            // a Flow, so the extra 38 px only ever costs a wrap.
            width: 150
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
    }

    // ---- step marker size ----
    Cell {
        visible: root.has("stepSize")
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
                // The combo below carries the spoken name already.
                Accessible.ignored: true
            }
            UValueCombo {
                activeFocusOnTab: root.controlsFocusable && enabled
                accessibleName: qsTr("Step marker size")
                width: 86
                implicitHeight: 28
                values: [16, 18, 20, 22, 26, 32, 40, 48, 56, 64, 80, 96]
                from: 16; to: 128
                suffix: " px"
                value: root.canvas.stepSize
                onChanged: (v) => root.canvas.stepSize = v
            }
        }
    }

    // ---- shape fill ----
    Cell {
        visible: root.has("fill")
        ToolChip {
            activeFocusOnTab: root.controlsFocusable && enabled
            visible: root.has("fill")
            iconName: "fill-color"
            label: qsTr("Fill shapes")
            active: root.canvas.shapeFillEnabled
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.canvas.shapeFillEnabled = !root.canvas.shapeFillEnabled
        }
        ColorDot {
            activeFocusOnTab: root.controlsFocusable && enabled
            accessibleName: qsTr("Shape fill colour %1").arg(String(root.canvas.shapeFillColor))
            visible: root.has("fill")
            dotColor: root.canvas.shapeFillColor
            active: root.canvas.shapeFillEnabled
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.fillPickerRequested()
        }
    }

    // ---- text styling ----
    Cell {
        visible: root.has("font")
        Rectangle {
            visible: root.has("font") && root.has("stroke")
            width: 1; height: 28; color: Theme.divider
            anchors.verticalCenter: parent.verticalCenter
        }
        UComboBox {
            activeFocusOnTab: root.controlsFocusable && enabled
            accessibleName: qsTr("Font")
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
            activeFocusOnTab: root.controlsFocusable && enabled
            accessibleName: qsTr("Font size")
            visible: root.has("font")
            width: 88
            anchors.verticalCenter: parent.verticalCenter
            values: [8, 10, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 56, 64, 72, 96, 144]
            from: 8; to: 144
            suffix: " px"
            value: root.canvas.fontSize
            onChanged: (v) => root.canvas.fontSize = v
        }
    }
    Cell {
        visible: root.has("font")
        ToolChip {
            activeFocusOnTab: root.controlsFocusable && enabled
            visible: root.has("font")
            icon: "B"; label: qsTr("Bold")
            active: root.canvas.fontBold
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.canvas.fontBold = !root.canvas.fontBold
        }
        ToolChip {
            activeFocusOnTab: root.controlsFocusable && enabled
            visible: root.has("font")
            icon: "I"; label: qsTr("Italic")
            active: root.canvas.fontItalic
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.canvas.fontItalic = !root.canvas.fontItalic
        }
        ToolChip {
            activeFocusOnTab: root.controlsFocusable && enabled
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
    }
    Cell {
        visible: root.has("font")
        ToolChip {
            activeFocusOnTab: root.controlsFocusable && enabled
            visible: root.has("font")
            iconName: "text-outline"
            label: qsTr("Text outline")
            active: root.canvas.textOutline
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.canvas.textOutline = !root.canvas.textOutline
        }
        ColorDot {
            activeFocusOnTab: root.controlsFocusable && enabled
            accessibleName: qsTr("Text outline colour %1").arg(String(root.canvas.textOutlineColor))
            visible: root.has("font")
            dotColor: root.canvas.textOutlineColor
            active: root.canvas.textOutline
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.textOutlinePickerRequested()
        }
        ToolChip {
            activeFocusOnTab: root.controlsFocusable && enabled
            visible: root.has("font")
            iconName: "text-background"
            label: qsTr("Text background")
            active: root.canvas.textBackground
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.canvas.textBackground = !root.canvas.textBackground
        }
        ColorDot {
            activeFocusOnTab: root.controlsFocusable && enabled
            accessibleName: qsTr("Text background colour %1").arg(String(root.canvas.textBackgroundColor))
            visible: root.has("font")
            dotColor: root.canvas.textBackgroundColor
            active: root.canvas.textBackground
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.textBackgroundPickerRequested()
        }
    }

    // ---- style presets ----
    // The style is GLOBAL (one set of properties on the canvas, not one per
    // tool), so a preset is just a snapshot of it. Stored as JSON in one
    // settings string; the schema lives here.
    Cell {
        visible: root.presetsVisible
        Rectangle {
            visible: root.presetsVisible
            width: 1; height: 28; color: Theme.divider
            anchors.verticalCenter: parent.verticalCenter
        }
        DotGroup {
            id: presetGroup
            visible: root.presetsVisible && root.presets.length > 0
            anchors.verticalCenter: parent.verticalCenter
            activeFocusOnTab: root.controlsFocusable && presetGroup.count > 0
            count: root.presetsVisible ? root.presets.length : 0
            entryIndex: presetGroup._selected()

            Accessible.name: qsTr("Style presets")
            Accessible.description: qsTr("Left and right arrows move between the presets, Space applies one")

            function _selected() {
                for (var i = 0; i < presetGroup.count; ++i)
                    if (root.presetMatchesCanvas(root.presets[i]))
                        return i
                return 0
            }

            Repeater {
                model: root.presetsVisible ? root.presets : []
                delegate: ColorDot {
                    id: presetDot
                    // Not a tab stop of its own - the group is (see DotGroup).
                    activeFocusOnTab: false
                    focus: presetDot.index === presetGroup.currentIndex
                    Accessible.focusable: root.controlsFocusable
                    accessibleName: qsTr("Style preset %1, %2 px")
                                        .arg(presetDot.index + 1)
                                        .arg(presetDot.modelData.width !== undefined
                                             ? presetDot.modelData.width : 0)
                    accessibleDescription: qsTr("Middle-click to delete this preset")
                    required property var modelData
                    required property int index
                    dotColor: modelData.stroke !== undefined ? modelData.stroke : "#FF4757"
                    // A preset is "active" only when the WHOLE style matches, not just
                    // the colour - two presets can share a colour and differ in width.
                    active: root.presetMatchesCanvas(modelData)
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: root.applyPreset(modelData)
                    UHoverTip {
                        anchor: presetDot
                        show: presetDot.hovered
                        // No names to type: the width is what visibly separates two
                        // presets of the same colour. Middle-click deletes, so the tip
                        // has to say so - nothing else in the bar hints at it.
                        text: qsTr("%1 px - middle-click to delete")
                                  .arg(presetDot.modelData.width !== undefined ? presetDot.modelData.width : 0)
                    }
                    // Deliberately not a hover ✕: the dots are 26 px and sit in a row
                    // the user clicks constantly to APPLY a preset - an adjacent delete
                    // target that size is a misclick waiting to happen. Left clicks are
                    // not accepted here, so they fall through to the dot's own handler.
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.MiddleButton
                        onClicked: root.removePreset(presetDot.index)
                    }
                }
            }
        }
        ToolChip {
            activeFocusOnTab: root.controlsFocusable && enabled
            visible: root.presetsVisible && root.presets.length < root.maxPresets
            iconName: "plus"
            iconStyle: "custom"
            label: qsTr("Save current style as a preset")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.savePreset()
        }
    }
}
