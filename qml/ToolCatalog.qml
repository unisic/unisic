pragma Singleton
import QtQuick

// Single source of truth for the annotation tool set. Both the editor and the
// overlay build their toolbars from this, filtered by context and by the
// user's hidden-tools setting. `tool` maps to the AnnotationCanvas.Tool enum.
QtObject {
    readonly property var tools: [
        { id: "select",     tool: 0,  iconName: "select-rectangular", label: qsTr("Select"),       overlay: true,  editor: false, hideable: false },
        // Superseded in the overlay by "Smart pick" (Settings > Capture):
        // a plain CLICK during region selection picks the detected object.
        { id: "object",     tool: 13, iconName: "object-pick",        label: qsTr("Pick object"),  overlay: false, editor: false, hideable: false },
        { id: "pen",        tool: 1,  iconName: "draw-freehand",      label: qsTr("Pen"),          overlay: true,  editor: true,  hideable: true },
        { id: "line",       tool: 2,  iconName: "draw-line",          label: qsTr("Line"),         overlay: true,  editor: true,  hideable: true },
        { id: "arrow",      tool: 3,  iconName: "draw-arrow",         label: qsTr("Arrow"),        overlay: true,  editor: true,  hideable: true },
        { id: "rect",       tool: 4,  iconName: "draw-rectangle",     label: qsTr("Rectangle"),    overlay: true,  editor: true,  hideable: true },
        { id: "ellipse",    tool: 5,  iconName: "draw-ellipse",       label: qsTr("Ellipse"),      overlay: true,  editor: true,  hideable: true },
        { id: "text",       tool: 6,  iconName: "draw-text",          label: qsTr("Text"),         overlay: true,  editor: true,  hideable: true },
        { id: "highlight",  tool: 9,  iconName: "draw-highlight",     label: qsTr("Highlight"),    overlay: true,  editor: true,  hideable: true },
        { id: "blur",       tool: 7,  iconName: "blur",               label: qsTr("Blur"),         overlay: true,  editor: true,  hideable: true },
        { id: "pixelate",   tool: 8,  iconName: "pixelate",           label: qsTr("Pixelate"),     overlay: true,  editor: true,  hideable: true },
        { id: "smarterase", tool: 12, iconName: "draw-eraser",        label: qsTr("Smart eraser"), overlay: true,  editor: true,  hideable: true },
        { id: "step",       tool: 10, iconName: "number",             label: qsTr("Step marker"),  overlay: true,  editor: true,  hideable: true },
        { id: "crop",       tool: 11, iconName: "transform-crop",     label: qsTr("Crop"),         overlay: false, editor: true,  hideable: true }
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
}
