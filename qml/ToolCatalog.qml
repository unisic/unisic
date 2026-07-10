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
        { id: "edit",       tool: 14, iconName: "hand-pointing",      label: qsTr("Select"),       overlay: true,  editor: true,  hideable: true,  props: [] },
        // Superseded in the overlay by "Smart pick" (Settings > Capture):
        // a plain CLICK during region selection picks the detected object.
        { id: "object",     tool: 13, iconName: "object-pick",        label: qsTr("Pick object"),  overlay: false, editor: false, hideable: false, props: [] },
        { id: "pen",        tool: 1,  iconName: "draw-freehand",      label: qsTr("Pen"),          overlay: true,  editor: true,  hideable: true,  props: ["stroke", "width"] },
        { id: "line",       tool: 2,  iconName: "draw-line",          label: qsTr("Line"),         overlay: true,  editor: true,  hideable: true,  group: "shapes", props: ["stroke", "width"] },
        { id: "arrow",      tool: 3,  iconName: "draw-arrow",         label: qsTr("Arrow"),        overlay: true,  editor: true,  hideable: true,  group: "shapes", props: ["stroke", "width"] },
        { id: "rect",       tool: 4,  iconName: "draw-rectangle",     label: qsTr("Rectangle"),    overlay: true,  editor: true,  hideable: true,  group: "shapes", props: ["stroke", "width", "fill"] },
        { id: "ellipse",    tool: 5,  iconName: "draw-ellipse",       label: qsTr("Ellipse"),      overlay: true,  editor: true,  hideable: true,  group: "shapes", props: ["stroke", "width", "fill"] },
        { id: "text",       tool: 6,  iconName: "draw-text",          label: qsTr("Text"),         overlay: true,  editor: true,  hideable: true,  props: ["stroke", "font"] },
        { id: "highlight",  tool: 9,  iconName: "draw-highlight",     label: qsTr("Highlight"),    overlay: true,  editor: true,  hideable: true,  props: ["stroke"] },
        { id: "blur",       tool: 7,  iconName: "blur",               label: qsTr("Blur"),         overlay: true,  editor: true,  hideable: true,  props: [] },
        { id: "pixelate",   tool: 8,  iconName: "pixelate",           label: qsTr("Pixelate"),     overlay: true,  editor: true,  hideable: true,  props: ["width"] },
        { id: "smarterase", tool: 12, iconName: "draw-eraser",        label: qsTr("Smart eraser"), overlay: true,  editor: true,  hideable: true,  props: [] },
        { id: "step",       tool: 10, iconName: "number",             label: qsTr("Step marker"),  overlay: true,  editor: true,  hideable: true,  props: ["stroke"] },
        { id: "crop",       tool: 11, iconName: "transform-crop",     label: qsTr("Crop"),         overlay: false, editor: true,  hideable: true,  props: [] }
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

    // The main toolbar row: visible tools that are NOT behind a group chip.
    function visibleUngrouped(ctx, hiddenCsv) {
        return visibleFor(ctx, hiddenCsv).filter(function (t) { return !t.group })
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
        if (toolEnum === 14 /* EditShapes */)
            return selectedAnnotTool >= 0 ? propsForEnum(selectedAnnotTool) : []
        return propsForEnum(toolEnum)
    }
}
