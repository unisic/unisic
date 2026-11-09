import QtQuick
import Unisic
import Unisic.Kit

// Contextual tool-properties strip: renders only the controls relevant to the
// active tool (ToolCatalog `props` - "stroke", "width", "fill", "font").
// Shared by the editor (inspector or bottom pill) and the overlay pill. The hosts own the color
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
    // The Sticker tool's row after the mouse pointer (EditorWindow.stickerList).
    property var quickStickers: []
    // The widest the host can afford to give the strip. 0 (the default) means
    // "unconstrained" - the strip then takes its natural one-line width, which
    // is what a host that has already reserved room for it wants.
    property real maxWidth: 0
    // How wide one wrapped DotGroup may run: the host's own bound. Not a
    // `maxWidth` the host balanced from the cells' widths - a group wrapping
    // to that changes the very cells it came from, and the Cell's Row
    // re-polished itself in a loop (the sticker row in a narrow editor).
    property real groupLimit: maxWidth
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
    // How the editor host draws the strip: "bar" (the overlay pill and the
    // old one-line look), "inspector" (the panels layout: labelled sections
    // stacked down the inspector, a width slider, a text arrowhead switch) or
    // "pill" (the floating layout: 24 px swatches, Thin/Medium/Thick and an
    // icon arrowhead switch). Same canvas properties under every look.
    property string variant: "bar"
    readonly property bool inspector: variant === "inspector"
    readonly property bool pill: variant === "pill"
    signal strokePickerRequested()
    signal fillPickerRequested()
    signal textOutlinePickerRequested()
    signal textBackgroundPickerRequested()
    signal imageFileRequested()
    signal stickerLibraryRequested()

    spacing: 6

    function has(p) { return props.indexOf(p) >= 0 }

    // A recent colour that is also a palette swatch is already on offer one
    // dot to the left; shown twice it drew two selection rings for one colour.
    // Filtered here, not when it is recorded: the palette changes with the theme.
    readonly property var shownRecent: has("stroke")
        ? recentColors.filter(function (c) {
              return !Theme.swatches.some(function (s) { return Qt.colorEqual(s, c) })
          })
        : []

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
            w += Math.max(c.implicitWidth, c.width)
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
            // What the Flow lays out: a titled cell is a whole line wide.
            var w = Math.max(c.implicitWidth, c.width)
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
            h = Math.max(h, c.lineHeight !== undefined ? c.lineHeight : c.implicitHeight)
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
        id: cell
        default property alias content: holder.data
        // A labelled section of the inspector: the label above, a value
        // (the width in px) at its right, the controls below, one full line.
        property string title: ""
        property string value: ""
        readonly property real _head: title !== "" ? titleText.height + 10 : 0
        // A titled cell spans the line through `width`, never `implicitWidth`:
        // the floating host balances maxWidth FROM the implicit widths, so an
        // implicitWidth that read maxWidth looped for the one pass after a
        // layout flip in which the title had not yet emptied.
        implicitWidth: holder.implicitWidth
        implicitHeight: holder.implicitHeight + _head + (title !== "" ? 12 : 0)
        // One line of this cell: a wrapped DotGroup counts as one line of its
        // dots, so it makes its own cell taller and not every cell in the strip.
        readonly property real lineHeight: {
            var h = 0
            for (var i = 0; i < holder.children.length; ++i) {
                var c = holder.children[i]
                if (c.visible)
                    h = Math.max(h, c.lineHeight !== undefined ? c.lineHeight : c.height)
            }
            return h
        }
        width: title !== "" ? Math.max(root.maxWidth, implicitWidth) : implicitWidth
        height: title !== "" ? implicitHeight : Math.max(root.cellHeight, implicitHeight)
        Text {
            id: titleText
            visible: cell.title !== ""
            text: cell.title
            color: Theme.textPrimary
            font.pixelSize: Theme.fontM
            font.weight: Font.Medium
            Accessible.ignored: true
        }
        Text {
            visible: cell.title !== "" && cell.value !== ""
            anchors.right: parent.right
            anchors.baseline: titleText.baseline
            text: cell.value
            color: Theme.textSecondary
            font.family: "monospace"
            font.pixelSize: Theme.fontS
            Accessible.ignored: true
        }
        // Where the wrap put this cell, for its Divider.
        readonly property bool lineStart: x === 0
        readonly property bool lineEnd: {
            var after = false
            for (var i = 0; i < root.children.length; ++i) {
                var c = root.children[i]
                if (c === cell)
                    after = true
                else if (after && c.visible)
                    return c.y > cell.y
            }
            return true
        }
        Row {
            id: holder
            spacing: cell.title !== "" ? 8 : 6
            y: cell.title !== "" ? cell._head : (cell.height - height) / 2
        }
    }

    // A row of mutually exclusive buttons in one tray: the inspector's
    // labelled arrowhead switch (`labelled`, stretched to `fill`) and the
    // floating pill's icon-only width and arrowhead switches.
    component Segmented: Rectangle {
        id: seg
        // [{ value, label, iconName, bar }]: `bar` draws a stroke that thick
        // instead of an icon (the Thin/Medium/Thick presets).
        property var options: []
        property var current
        property bool labelled: false
        property real fill: 0
        property string accessibleName: ""
        signal picked(var value)
        readonly property real buttonW: fill > 0 && options.length > 0
            ? (fill - 6 - 2 * (options.length - 1)) / options.length : 38
        implicitWidth: fill > 0 ? fill : segRow.implicitWidth + 6
        implicitHeight: (labelled ? 30 : 32) + 6
        width: implicitWidth
        height: implicitHeight
        radius: labelled ? 9 : Theme.radiusM
        color: labelled ? Theme.primary : Theme.alpha(Theme.backgroundDeep, 0.55)
        border.width: labelled ? 1 : 0
        border.color: Theme.alpha(Theme.accent, 0.10)
        Accessible.role: Accessible.Grouping
        Accessible.name: seg.accessibleName
        Row {
            id: segRow
            x: 3; y: 3
            spacing: 2
            Repeater {
                model: seg.options
                delegate: Rectangle {
                    id: segBtn
                    required property var modelData
                    readonly property bool on: seg.current === modelData.value
                    function _activate() { if (segBtn.enabled) seg.picked(segBtn.modelData.value) }
                    width: seg.buttonW
                    height: seg.labelled ? 30 : 32
                    radius: seg.labelled ? 6 : 9
                    color: on ? (seg.labelled ? Theme.secondary : Theme.tertiary)
                         : segMouse.containsMouse ? Theme.alpha(Theme.accent, 0.10) : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    Row {
                        anchors.centerIn: parent
                        spacing: 6
                        UIcon {
                            visible: (segBtn.modelData.iconName || "") !== ""
                            anchors.verticalCenter: parent.verticalCenter
                            name: segBtn.modelData.iconName || ""
                            iconStyle: "custom"
                            size: 15
                            color: segBtn.on ? Theme.textPrimary : Theme.textSecondary
                        }
                        Rectangle {
                            visible: (segBtn.modelData.bar || 0) > 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: segBtn.modelData.bar || 0
                            radius: height / 2
                            color: segBtn.on ? Theme.textPrimary : Theme.textSecondary
                        }
                        Text {
                            visible: seg.labelled
                            anchors.verticalCenter: parent.verticalCenter
                            text: segBtn.modelData.label
                            color: segBtn.on ? Theme.textPrimary : Theme.textSecondary
                            font.pixelSize: Theme.fontS
                            font.weight: segBtn.on ? Font.DemiBold : Font.Normal
                        }
                    }
                    UHoverTip {
                        anchor: segBtn
                        text: segBtn.modelData.label
                        show: !seg.labelled && segMouse.containsMouse
                    }
                    MouseArea {
                        id: segMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: segBtn._activate()
                    }
                    activeFocusOnTab: root.controlsFocusable && enabled
                    Keys.onSpacePressed: (e) => UKeys.activate(e, segBtn._activate)
                    Keys.onReturnPressed: (e) => UKeys.activate(e, segBtn._activate)
                    Keys.onEnterPressed: (e) => UKeys.activate(e, segBtn._activate)
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: segBtn.modelData.label
                    Accessible.focusable: segBtn.activeFocusOnTab
                    Accessible.checkable: true
                    Accessible.checked: segBtn.on
                    Accessible.onPressAction: segBtn._activate()
                    UFocusRing { hostRadius: segBtn.radius }
                }
            }
        }
    }

    // A divider separates two groups on one line. Where the wrap leaves it at
    // the start or the end of a line it separates nothing (it was the "| ● +"
    // a narrow editor put on a line of its own), so it goes transparent there.
    // Transparent, not invisible: hiding it would narrow the cell, which can
    // move the wrap and bring the divider back.
    component Divider: Rectangle {
        property bool leading: false
        width: 1; height: 28; color: Theme.divider
        anchors.verticalCenter: parent.verticalCenter
        opacity: parent && parent.parent
                 && (leading ? parent.parent.lineStart : parent.parent.lineEnd) ? 0 : 1
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
        // What else the Cell puts on the line beside this group (the trailing
        // Divider and its gap by default). A Cell cannot wrap inside itself,
        // so a group longer than the strip - thirteen stickers, or the
        // swatches plus six recent colours in the editor's inspector - wraps
        // its own dots instead of running past the strip's edge.
        property real reserve: 7
        readonly property real limit: root.groupLimit > 0 ? Math.max(root.groupLimit, 160) - reserve
                                                        : Number.POSITIVE_INFINITY
        // Summed, not groupRow.implicitWidth: that is the width already
        // wrapped, which cannot answer "how wide unwrapped?". By width, not
        // implicitWidth: a ColorDot sets only its width.
        readonly property real naturalWidth: {
            var w = 0
            var n = 0
            for (var i = 0; i < groupRow.children.length; ++i) {
                var c = groupRow.children[i]
                if (!c.visible || c.width <= 0)
                    continue
                w += c.width
                ++n
            }
            return n > 0 ? w + groupRow.spacing * (n - 1) : 0
        }
        // One line of dots, for the Cell's lineHeight.
        readonly property real lineHeight: {
            var h = 0
            for (var i = 0; i < groupRow.children.length; ++i) {
                var c = groupRow.children[i]
                if (c.visible)
                    h = Math.max(h, c.height)
            }
            return h
        }

        implicitWidth: groupRow.width
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

        // Wrapped, even lines (5, 5, 3 stickers, not 6, 6, 1). Off where a
        // narrower line would push a separator to the next line's start.
        property bool balanced: false
        readonly property int spacing: 6
        // [lines, widest line] at width w, by Flow's own rule: a dot goes on
        // the line unless it would cross the edge and it is not alone there.
        function _wrap(w) {
            var lines = 0
            var line = 0
            var widest = 0
            for (var i = 0; i < groupRow.children.length; ++i) {
                var c = groupRow.children[i]
                if (!c.visible || c.width <= 0)
                    continue
                if (lines === 0 || line + groupRow.spacing + c.width > w) {
                    ++lines
                    line = c.width
                } else {
                    line += groupRow.spacing + c.width
                }
                widest = Math.max(widest, line)
            }
            return [lines, widest]
        }
        function _lines(w) { return _wrap(w)[0] }
        // The width the dots actually draw, so the next control in the Cell
        // sits right after them; balanced, the narrowest width that needs no
        // more lines than the limit does.
        readonly property real flowWidth: {
            if (naturalWidth <= limit)
                return naturalWidth
            if (!balanced)
                return _wrap(limit)[1]
            var lines = _lines(limit)
            var lo = 0
            var hi = Math.floor(limit)
            while (hi - lo > 1) {
                var mid = Math.floor((lo + hi) / 2)
                if (_lines(mid) <= lines)
                    hi = mid
                else
                    lo = mid
            }
            return hi
        }

        Flow {
            id: groupRow
            spacing: group.spacing
            width: group.flowWidth
        }
    }

    // ---- style presets ----
    // Presets only make sense for a tool that HAS a style; Blur/Crop and the
    // like declare no props, and a preset row floating next to nothing would
    // just be noise. Spotlight's shape and the sticker pick are choices, not a
    // style a preset captures.
    readonly property bool presetsVisible: props.some(function (p) {
        return p !== "spotlightShape" && p !== "sticker"
    })
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
        Divider {
            visible: root.has("measureMode")
        }
    }

    // ---- spotlight shape (rectangle / ellipse hole in the dim) ----
    Cell {
        visible: root.has("spotlightShape")
        Row {
            visible: root.has("spotlightShape")
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            ToolChip {
                activeFocusOnTab: root.controlsFocusable && enabled
                iconName: "draw-rectangle"; iconStyle: "custom"; label: qsTr("Rectangle")
                active: root.canvas.spotlightShape === 0
                anchors.verticalCenter: parent.verticalCenter
                onClicked: root.canvas.spotlightShape = 0
            }
            ToolChip {
                activeFocusOnTab: root.controlsFocusable && enabled
                iconName: "draw-ellipse"; iconStyle: "custom"; label: qsTr("Ellipse")
                active: root.canvas.spotlightShape === 1
                anchors.verticalCenter: parent.verticalCenter
                onClicked: root.canvas.spotlightShape = 1
            }
        }
        Divider {
            visible: root.has("spotlightShape")
        }
    }

    // ---- sticker: the mouse pointer, the quick row, the library, a file ----
    Cell {
        visible: root.has("sticker")
        DotGroup {
            id: stickerGroup
            visible: root.has("sticker")
            balanced: true
            anchors.verticalCenter: parent.verticalCenter
            activeFocusOnTab: root.controlsFocusable
            readonly property var choices: ["pointer"].concat(root.quickStickers)
            count: choices.length
            entryIndex: Math.max(0, choices.indexOf(root.canvas.sticker))
            Accessible.name: qsTr("Sticker")
            Accessible.description: qsTr("Left and right arrows move between the stickers, Space picks one")

            Repeater {
                model: root.has("sticker") ? stickerGroup.choices : []
                delegate: ToolChip {
                    id: stickerChip
                    objectName: modelData === "pointer" ? "" : "quickSticker"
                    required property string modelData
                    required property int index
                    // Not a tab stop of its own - the group is (see DotGroup).
                    activeFocusOnTab: false
                    focus: index === stickerGroup.currentIndex
                    Accessible.focusable: root.controlsFocusable
                    readonly property string art: modelData === "pointer" ? ""
                                                  : root.canvas.stickerArt(modelData)
                    iconName: modelData === "pointer" ? "cursor-arrow" : ""
                    iconStyle: "custom"
                    // No art: the emoji font draws it, as the canvas would.
                    icon: modelData === "pointer" || art !== "" ? "" : modelData
                    label: modelData === "pointer" ? qsTr("Mouse pointer") : ""
                    accessibleName: modelData === "pointer" ? "" : modelData
                    active: root.canvas.sticker === modelData
                    onClicked: root.canvas.sticker = modelData
                    Image {
                        visible: stickerChip.art !== ""
                        anchors.centerIn: parent
                        width: 24
                        height: 24
                        sourceSize: Qt.size(48, 48)
                        source: stickerChip.art
                    }
                }
            }
        }
        Divider {
            visible: root.has("sticker")
        }
    }
    Cell {
        visible: root.has("sticker")
        Row {
            visible: root.has("sticker")
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            ToolChip {
                activeFocusOnTab: root.controlsFocusable && enabled
                iconName: "view-more"; iconStyle: "custom"; label: qsTr("All stickers\u2026")
                anchors.verticalCenter: parent.verticalCenter
                onClicked: root.stickerLibraryRequested()
            }
            ToolChip {
                activeFocusOnTab: root.controlsFocusable && enabled
                iconName: "image"; iconStyle: "custom"; label: qsTr("Picture from a file\u2026")
                anchors.verticalCenter: parent.verticalCenter
                onClicked: root.imageFileRequested()
            }
        }
        Divider {
            visible: root.has("sticker")
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
        Divider {
            visible: root.has("highlightMode")
        }
    }

    // ---- stroke color ----
    // The palette swatches and the recent colours are ONE group: they are the
    // same choice offered twice, so Left/Right runs across both without a stop
    // in between.
    Cell {
        visible: root.has("stroke")
        title: root.inspector ? qsTr("Color") : ""
        DotGroup {
            id: swatchGroup
            visible: root.has("stroke")
            reserve: root.inspector ? 8 + 26 : 6 + 30 + 7 // the colour picker button, then the Divider
            anchors.verticalCenter: parent.verticalCenter
            activeFocusOnTab: root.controlsFocusable && swatchGroup.count > 0

            readonly property int baseCount: root.has("stroke") ? Theme.swatches.length : 0
            readonly property int recentCount: root.shownRecent.length
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
                    if (Qt.colorEqual(root.canvas.strokeColor, root.shownRecent[j]))
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
                    width: root.pill ? 24 : 26
                    height: width
                    radius: width / 2
                    accessibleName: qsTr("Stroke colour %1").arg(String(modelData))
                    dotColor: modelData
                    active: Qt.colorEqual(root.canvas.strokeColor, modelData)
                    onClicked: root.canvas.strokeColor = modelData
                }
            }
            // Palette | recent: one keyboard group, two sources the eye should
            // tell apart. Shorter than a Divider, which separates whole groups.
            // Dot-tall, so the Flow (which top-aligns) centres the line. Where
            // the group wraps it separates nothing at a line's edge, so it goes
            // transparent there (not invisible: that would move the wrap).
            Item {
                id: recentSeparator
                visible: swatchGroup.recentCount > 0
                width: 1; height: 26
                Rectangle {
                    anchors.centerIn: parent
                    width: 1; height: 16; color: Theme.divider
                    // At the end when the next dot (26 px) would not fit after it.
                    opacity: recentSeparator.x === 0
                             || recentSeparator.x + 1 + swatchGroup.spacing + 26 > swatchGroup.flowWidth ? 0 : 1
                }
            }
            Repeater {
                model: root.shownRecent
                delegate: ColorDot {
                    activeFocusOnTab: false
                    focus: swatchGroup.baseCount + index === swatchGroup.currentIndex
                    Accessible.focusable: root.controlsFocusable
                    width: root.pill ? 24 : 26
                    height: width
                    radius: width / 2
                    accessibleName: qsTr("Recent stroke colour %1").arg(String(modelData))
                    dotColor: modelData
                    active: Qt.colorEqual(root.canvas.strokeColor, modelData)
                    onClicked: root.canvas.strokeColor = modelData
                }
            }
        }
        Rectangle {
            id: customColor
            visible: root.has("stroke") && root.inspector
            width: 26; height: 26
            radius: 13
            color: customMouse.containsMouse ? Theme.alpha(Theme.accent, 0.10) : "transparent"
            function _activate() { root.strokePickerRequested() }
            // A dashed ring has no Rectangle spelling; one SVG circle in the
            // theme's accent, rebuilt only when the theme changes.
            Image {
                anchors.fill: parent
                sourceSize: Qt.size(52, 52)
                source: "data:image/svg+xml;utf8," + encodeURIComponent(
                    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 26 26"><circle cx="13" cy="13" r="12.5" fill="none" stroke="'
                    + Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 1).toString()
                    + '" stroke-opacity="0.5" stroke-dasharray="3 2.5"/></svg>')
            }
            UIcon {
                anchors.centerIn: parent
                name: "line-plus"
                iconStyle: "custom"
                size: 14
                color: Theme.textSecondary
            }
            UHoverTip { anchor: customColor; text: qsTr("More colors"); show: customMouse.containsMouse }
            MouseArea {
                id: customMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: customColor._activate()
            }
            activeFocusOnTab: root.controlsFocusable
            Keys.onSpacePressed: (e) => UKeys.activate(e, customColor._activate)
            Keys.onReturnPressed: (e) => UKeys.activate(e, customColor._activate)
            Keys.onEnterPressed: (e) => UKeys.activate(e, customColor._activate)
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("More colors")
            Accessible.focusable: customColor.activeFocusOnTab
            Accessible.onPressAction: customColor._activate()
            UFocusRing { inset: 0; hostRadius: customColor.radius }
        }
        UIconButton {
            activeFocusOnTab: root.controlsFocusable && enabled
            visible: root.has("stroke") && !root.inspector
            iconName: "color-picker"
            iconSize: 16
            width: 30; height: 30
            tooltip: qsTr("More colors")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.strokePickerRequested()
        }

        Divider {
            visible: root.has("stroke") && !root.inspector && (root.showWidth || root.has("fill"))
        }
    }

    // ---- stroke width / arrow head: the inspector's labelled sections ----
    Cell {
        visible: root.inspector && root.showWidth
        title: qsTr("Width")
        value: qsTr("%1 px").arg(root.canvas.strokeWidth)
        USlider {
            activeFocusOnTab: root.controlsFocusable && enabled
            visible: root.inspector && root.showWidth
            width: Math.max(160, root.maxWidth)
            from: 1
            to: Math.max(24, root.canvas.strokeWidth)
            value: root.canvas.strokeWidth
            accessibleName: qsTr("Stroke width")
            onMoved: (v) => root.canvas.strokeWidth = Math.round(v)
        }
    }
    Cell {
        visible: root.inspector && root.has("arrowhead")
        title: qsTr("Arrowhead")
        Segmented {
            visible: root.inspector && root.has("arrowhead")
            labelled: true
            fill: Math.max(160, root.maxWidth)
            accessibleName: qsTr("Arrow head")
            options: root.arrowOptions
            current: root.canvas.arrowHeadStyle
            onPicked: (v) => root.canvas.arrowHeadStyle = v
        }
    }

    // ---- stroke width / arrow head: the floating pill's presets ----
    readonly property var arrowOptions: [
        { value: 1, label: qsTr("Open"), iconName: "line-arrow" },
        { value: 0, label: qsTr("Filled"), iconName: "line-head-filled" },
        { value: 2, label: qsTr("Both"), iconName: "line-head-both" }
    ]
    Cell {
        visible: root.pill && (root.showWidth || root.has("arrowhead"))
        Segmented {
            visible: root.pill && root.showWidth
            anchors.verticalCenter: parent.verticalCenter
            accessibleName: qsTr("Stroke width")
            options: [
                { value: 2, label: qsTr("Thin"), bar: 2 },
                { value: 4, label: qsTr("Medium"), bar: 4 },
                { value: 8, label: qsTr("Thick"), bar: 7 }
            ]
            current: root.canvas.strokeWidth
            onPicked: (v) => root.canvas.strokeWidth = v
        }
        Segmented {
            visible: root.pill && root.has("arrowhead")
            anchors.verticalCenter: parent.verticalCenter
            accessibleName: qsTr("Arrow head")
            options: root.arrowOptions
            current: root.canvas.arrowHeadStyle
            onPicked: (v) => root.canvas.arrowHeadStyle = v
        }
        Divider {
            visible: root.pill && root.has("width") && root.has("fill")
        }
    }

    // ---- stroke width / arrow head: the overlay bar ----
    Cell {
        visible: root.variant === "bar"
                 && (root.showWidth || root.has("arrowhead") || (root.has("width") && root.has("fill")))
        Column {
            visible: root.variant === "bar" && root.showWidth
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
            visible: root.variant === "bar" && root.has("arrowhead")
            // 150, not 112: at 112 even English "Double head" elided (84 px of
            // text into a 72 px slot), and every translation was worse. This is
            // a Flow, so the extra 38 px only ever costs a wrap.
            width: 150
            anchors.verticalCenter: parent.verticalCenter
            model: [qsTr("Filled head"), qsTr("Open head"), qsTr("Double head")]
            currentIndex: root.canvas.arrowHeadStyle
            onActivated: (i) => root.canvas.arrowHeadStyle = i
        }

        Divider {
            visible: root.variant === "bar" && root.has("width") && root.has("fill")
        }
    }

    // ---- step marker size ----
    Cell {
        visible: root.has("stepSize")
        Divider {
            visible: root.has("stepSize") && root.has("stroke")
            leading: true
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
        Divider {
            visible: root.has("font") && root.has("stroke")
            leading: true
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
        Divider {
            visible: root.has("font")
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
        title: root.inspector ? qsTr("Style presets") : ""
        Divider {
            visible: root.presetsVisible && !root.inspector
            leading: true
        }
        DotGroup {
            id: presetGroup
            visible: root.presetsVisible && root.presets.length > 0
            reserve: 7 + 6 + 40 // the leading Divider, then the save chip
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
                    dotColor: modelData.stroke !== undefined ? modelData.stroke
                                                               : (Theme.swatches.length > 0 ? Theme.swatches[0] : Theme.accent)
                    // A preset is "active" only when the WHOLE style matches, not just
                    // the colour - two presets can share a colour and differ in width.
                    active: root.presetMatchesCanvas(modelData)
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
