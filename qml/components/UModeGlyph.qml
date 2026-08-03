// The band delegates below read `glyph`, an id from outside their own
// component, which is exactly what Bound makes legal.
pragma ComponentBehavior: Bound

import QtQuick
import Unisic.Kit

// One big translucent capture-mode mark in the middle of the screen, with a
// hole cut out of it. The mark names which capture is running without a strip
// of chrome across the desktop; the hole is the selection, so the part of the
// mark the user is framing over is simply not drawn while the rest keeps
// naming the mode. Hiding the whole mark instead made it vanish as soon as a
// selection touched one corner of it (user-reported).
//
// Drawn as the four bands AROUND the hole, each clipping its own copy of the
// mark: one item cannot have a hole in it, and the mask effects that could do
// it in one pass need a GPU path the software renderer (and the offscreen
// smoke run) does not have.
Item {
    id: glyph

    // freedesktop icon name, as UIcon takes it.
    property string name: ""
    property color tint: "white"
    // What to cut out, in this item's coordinates. An empty rect cuts nothing,
    // which is the state the overlay opens in.
    property rect hole: Qt.rect(0, 0, 0, 0)

    readonly property int box: Math.round(Math.min(width, height) * 0.26)
    readonly property int glyphX: Math.round((width - glyph.box) / 2)
    readonly property int glyphY: Math.round((height - glyph.box) / 2)

    Repeater {
        model: {
            const h = glyph.hole
            if (h.width <= 0 || h.height <= 0)
                return [Qt.rect(0, 0, glyph.width, glyph.height)]
            return [Qt.rect(0, 0, glyph.width, h.y),
                    Qt.rect(0, h.y + h.height, glyph.width, glyph.height - h.y - h.height),
                    Qt.rect(0, h.y, h.x, h.height),
                    Qt.rect(h.x + h.width, h.y, glyph.width - h.x - h.width, h.height)]
        }
        Item {
            id: band
            required property rect modelData
            x: band.modelData.x
            y: band.modelData.y
            // A hole that runs past an edge leaves a negative band; clamped, it
            // is an empty item that draws nothing.
            width: Math.max(0, band.modelData.width)
            height: Math.max(0, band.modelData.height)
            clip: true
            UIcon {
                name: glyph.name
                size: glyph.box
                color: glyph.tint
                x: glyph.glyphX - band.x
                y: glyph.glyphY - band.y
            }
        }
    }
}
