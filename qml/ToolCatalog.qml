pragma Singleton
import QtQuick

// Single source of truth for the annotation tool set. Both the editor and the
// overlay build their toolbars from this, filtered by context and by the
// user's hidden-tools setting. `tool` maps to the AnnotationCanvas.Tool enum.
//
// `group` clusters tools behind one toolbar chip (the group's tools + their
// properties live in a sub-bar shown when the chip is pressed). `props` lists
// which property controls apply to a tool — the contextual properties bar
// (ToolPropsBar) renders only those: "stroke" (color), "width" (stroke
// slider), "fill" (fill toggle + color), "font" (text styling).
QtObject {
    readonly property var tools: [
        { id: "select",     tool: 0,  iconName: "select-rectangular", label: qsTr("Select"),       overlay: true,  editor: false, hideable: false, props: [] },
        { id: "edit",       tool: 14, iconName: "hand-pointing",      label: qsTr("Edit shapes"),  shortcut: "V", shortcutKey: Qt.Key_V, overlay: true,  editor: true, hideable: true, props: [] },
        { id: "pen",        tool: 1,  iconName: "draw-freehand",      label: qsTr("Pen"),          shortcut: "P", shortcutKey: Qt.Key_P, overlay: true,  editor: true, hideable: true, props: ["stroke", "width"] },
        { id: "line",       tool: 2,  iconName: "draw-line",          label: qsTr("Line"),         shortcut: "L", shortcutKey: Qt.Key_L, overlay: true,  editor: true, hideable: true, group: "shapes", props: ["stroke", "width"] },
        { id: "arrow",      tool: 3,  iconName: "draw-arrow",         label: qsTr("Arrow"),        shortcut: "A", shortcutKey: Qt.Key_A, overlay: true,  editor: true, hideable: true, group: "shapes", props: ["stroke", "width", "arrowhead"] },
        { id: "measure",    tool: 17, iconName: "measure",            label: qsTr("Measure"),      shortcut: "M", shortcutKey: Qt.Key_M, overlay: true,  editor: true, hideable: true, group: "shapes", props: ["stroke", "width"] },
        { id: "rect",       tool: 4,  iconName: "draw-rectangle",     label: qsTr("Rectangle"),    shortcut: "R", shortcutKey: Qt.Key_R, overlay: true,  editor: true, hideable: true, group: "shapes", props: ["stroke", "width", "fill"] },
        { id: "ellipse",    tool: 5,  iconName: "draw-ellipse",       label: qsTr("Ellipse"),      shortcut: "O", shortcutKey: Qt.Key_O, overlay: true,  editor: true, hideable: true, group: "shapes", props: ["stroke", "width", "fill"] },
        // "callout", not the freedesktop name "dialog-information": that name
        // resolves in the system icon theme, so the tool wore Breeze's info
        // bubble instead of a speech balloon. The bundled SVG only gets used for
        // names no theme claims.
        { id: "callout",    tool: 16, iconName: "callout",            label: qsTr("Callout"),      shortcut: "D", shortcutKey: Qt.Key_D, overlay: true,  editor: true, hideable: true, group: "shapes", props: ["stroke", "width", "fill"] },
        { id: "text",       tool: 6,  iconName: "draw-text",          label: qsTr("Text"),         shortcut: "T", shortcutKey: Qt.Key_T, overlay: true,  editor: true, hideable: true, props: ["stroke", "font"] },
        { id: "highlight",  tool: 9,  iconName: "draw-highlight",     label: qsTr("Highlight"),    shortcut: "H", shortcutKey: Qt.Key_H, overlay: true,  editor: true, hideable: true, props: ["highlightMode", "stroke", "width"] },
        { id: "blur",       tool: 7,  iconName: "blur",               label: qsTr("Blur"),         shortcut: "B", shortcutKey: Qt.Key_B, overlay: true,  editor: true, hideable: true, props: [] },
        { id: "pixelate",   tool: 8,  iconName: "pixelate",           label: qsTr("Pixelate"),     shortcut: "X", shortcutKey: Qt.Key_X, overlay: true,  editor: true, hideable: true, props: ["width"] },
        { id: "smarterase", tool: 12, iconName: "draw-eraser",        label: qsTr("Smart eraser"), shortcut: "E", shortcutKey: Qt.Key_E, overlay: true,  editor: true, hideable: true, props: [] },
        { id: "magnify",    tool: 18, iconName: "magnify",            label: qsTr("Magnifier"),    shortcut: "Z", shortcutKey: Qt.Key_Z, overlay: true,  editor: true, hideable: true, props: ["stroke", "width"] },
        { id: "step",       tool: 10, iconName: "number",             label: qsTr("Step marker"),  shortcut: "N", shortcutKey: Qt.Key_N, overlay: true,  editor: true, hideable: true, props: ["stroke", "stepSize"] },
        { id: "crop",       tool: 11, iconName: "transform-crop",     label: qsTr("Crop"),         shortcut: "C", shortcutKey: Qt.Key_C, overlay: false, editor: true, hideable: true, props: [] }
    ]

    readonly property var groups: [
        { id: "shapes", iconName: "shapes", label: qsTr("Shapes") }
    ]

    // Resolve the icon name for a tool given the editor icon style and the
    // optional per-tool freedesktop-name override map (JSON string). Overrides
    // only apply to the "system" style; "custom" always uses the bundled glyph.
    function toolIconName(t, style, overridesJson) {
        if (style === "system" && overridesJson && overridesJson !== "") {
            try {
                var m = JSON.parse(overridesJson)
                if (m[t.id] && m[t.id] !== "") return m[t.id]
            } catch (e) {}
        }
        return t.iconName
    }

    function isHidden(id, csv) {
        if (!csv || csv === "") return false
        return ("," + csv + ",").indexOf("," + id + ",") >= 0
    }

    // ctx: "overlay" | "editor". Returns the visible tool descriptors.
    function visibleFor(ctx, hiddenCsv) {
        var out = []
        for (var i = 0; i < tools.length; ++i) {
            var t = tools[i]
            if (!t[ctx]) continue
            if (t.hideable && isHidden(t.id, hiddenCsv)) continue
            out.push(t)
        }
        return out
    }

    // Visible members of a group (the group chip hides when this is empty).
    function groupTools(groupId, ctx, hiddenCsv) {
        return visibleFor(ctx, hiddenCsv).filter(function (t) { return t.group === groupId })
    }

    // Descriptor lookup by AnnotationCanvas.Tool enum value (null when none).
    function toolByEnum(toolEnum) {
        for (var i = 0; i < tools.length; ++i)
            if (tools[i].tool === toolEnum) return tools[i]
        return null
    }

    // One shortcut table for both keyboard hosts. Hidden tools remain reachable:
    // hiding is a toolbar-declutter preference, not a feature-disable switch.
    function toolForShortcut(key, ctx) {
        for (var i = 0; i < tools.length; ++i) {
            var t = tools[i]
            if (t[ctx] && t.shortcutKey === key)
                return t
        }
        return null
    }

    // Keep the shortcut discoverable without spending permanent toolbar space:
    // ToolChip renders this label only in its hover tip.
    function labelWithShortcut(t) {
        return t && t.shortcut ? t.label + " (" + t.shortcut + ")"
                               : (t ? t.label : "")
    }

    // Group id of the active tool ("" when ungrouped/none).
    function groupForEnum(toolEnum) {
        var t = toolByEnum(toolEnum)
        return (t && t.group) ? t.group : ""
    }

    // Property controls relevant to the active tool ([] when none).
    function propsForEnum(toolEnum) {
        var t = toolByEnum(toolEnum)
        return (t && t.props) ? t.props : []
    }

    // Props to show in the contextual bar. With the Edit tool the controls
    // follow the SELECTED shape (selectedAnnotTool, -1 = nothing selected).
    function contextProps(toolEnum, selectedAnnotTool) {
        // A selected placed shape wins under ANY tool (click-select works with
        // the drawing tools too, not just Edit shapes) — the props bar must
        // show the properties of what the edits will actually restyle.
        if (selectedAnnotTool >= 0)
            return propsForEnum(selectedAnnotTool)
        if (toolEnum === 14 /* EditShapes */)
            return []
        return propsForEnum(toolEnum)
    }
}
