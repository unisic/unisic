import QtQuick
import Unisic.Kit

// The source tiles' 220x132 drawings, shared by the Capture and Record pages
// and reached as TileArt.FullScreenArt and so on. Plain rectangles, no
// QtQuick.Shapes, which Debian splits into its own package. Coordinates
// follow the design's SVG; a stroked shape is grown by half its stroke, since
// SVG centres a stroke on the outline and a Rectangle border sits inside it.
//
// `rec` turns a drawing into its recording version: the badge the recorder
// shows sits above the recorded area, as the real one does (RecordBorder.qml),
// and whatever is not recorded steps back. Never animated: the page can stay
// open all day, and a blinking dot would keep the scene redrawing.
Item {
    component Box: Rectangle {
        property real sx
        property real sy
        property real sw
        property real sh
        x: sx; y: sy; width: sw; height: sh
    }
    component Framed: Rectangle {
        property real sx
        property real sy
        property real sw
        property real sh
        property real stroke: 1.5
        property real rx: 0
        x: sx - stroke / 2; y: sy - stroke / 2
        width: sw + stroke; height: sh + stroke
        radius: rx > 0 ? rx + stroke / 2 : 0
        border.width: stroke
    }
    // The REC badge in miniature, in the recorder's own colours. A sample
    // time, not a live one.
    component RecBadge: Rectangle {
        property real sx
        property real sy
        x: sx; y: sy
        width: recRow.width + 12
        height: 16
        radius: 8
        color: Theme.recBadgeBg
        Row {
            id: recRow
            anchors.centerIn: parent
            spacing: 4
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 6; height: 6; radius: 3
                color: Theme.recDot
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "00:12"
                color: Theme.recBadgeText
                font.pixelSize: 9
                font.weight: Font.DemiBold
                font.family: "monospace"
            }
        }
    }

    component FullScreenArt: Item {
        property bool rec: false
        width: 220; height: 132
        Framed { sx: 20; sy: 28; sw: 104; sh: 66; rx: 4; color: Theme.alpha(Theme.accent, 0.16); border.color: Theme.accent }
        Box { sx: 30; sy: 38; sw: 40; sh: 5; radius: 2.5; color: Theme.alpha(Theme.accent, 0.5) }
        Box { sx: 30; sy: 50; sw: 84; sh: 34; radius: 3; color: Theme.tertiary }
        // A recording takes one screen, so the second monitor dims.
        Item {
            width: 220; height: 132
            opacity: parent.rec ? 0.4 : 1
            Framed { sx: 134; sy: 38; sw: 68; sh: 48; rx: 4; color: Theme.alpha(Theme.accent, 0.16); border.color: Theme.accent }
            Box { sx: 142; sy: 46; sw: 52; sh: 30; radius: 3; color: Theme.tertiary }
            Box { sx: 166.75; sy: 84.75; sw: 2.5; sh: 22.5; radius: 1.25; color: Theme.tertiary }
            Box { sx: 154.75; sy: 104.75; sw: 26.5; sh: 2.5; radius: 1.25; color: Theme.tertiary }
        }
        // The stands: round-capped 2.5 px strokes.
        Box { sx: 70.75; sy: 92.75; sw: 2.5; sh: 14.5; radius: 1.25; color: Theme.tertiary }
        Box { sx: 56.75; sy: 104.75; sw: 30.5; sh: 2.5; radius: 1.25; color: Theme.tertiary }
        RecBadge { visible: parent.rec; sx: 20; sy: 7 }
    }

    component RegionArt: Item {
        id: regionArt
        property bool rec: false
        width: 220; height: 132
        // 5 on, 4 off around the selection, starting at its top-left corner
        // and running clockwise, cut at the corners like the SVG dash array.
        readonly property var dashes: {
            var out = []
            var edges = [[58, 30, 1, 0, 108], [166, 30, 0, 1, 64], [166, 94, -1, 0, 108], [58, 94, 0, -1, 64]]
            var s0 = 0
            for (var e = 0; e < edges.length; ++e) {
                var ed = edges[e]
                for (var k = Math.floor(s0 / 9); k * 9 < s0 + ed[4]; ++k) {
                    var a = Math.max(k * 9, s0) - s0
                    var b = Math.min(k * 9 + 5, s0 + ed[4]) - s0
                    if (b <= a)
                        continue
                    var x1 = ed[0] + ed[2] * a, y1 = ed[1] + ed[3] * a
                    var x2 = ed[0] + ed[2] * b, y2 = ed[1] + ed[3] * b
                    out.push(ed[3] === 0
                             ? { x: Math.min(x1, x2), y: y1 - 0.75, w: Math.abs(x2 - x1), h: 1.5 }
                             : { x: x1 - 0.75, y: Math.min(y1, y2), w: 1.5, h: Math.abs(y2 - y1) })
                }
                s0 += ed[4]
            }
            return out
        }
        Box { sx: 14; sy: 14; sw: 96; sh: 64; radius: 4; color: Theme.secondary }
        Box { sx: 96; sy: 40; sw: 110; sh: 80; radius: 4; color: Theme.secondary }
        Box { sx: 108; sy: 54; sw: 60; sh: 5; radius: 2.5; color: Theme.tertiary }
        Box { sx: 108; sy: 66; sw: 86; sh: 40; radius: 3; color: Theme.tertiary }
        // The dimmed screen around the selection, as four bands. They run
        // 2000 px past the drawing: a tile wider (or narrower) than 220x132
        // centres it, and a dim that stopped at 220 drew a box edge across
        // the frame. SourceTile's clip trims the excess.
        Box { sx: -2000; sy: -2000; sw: 4220; sh: 2030; color: Theme.alpha(Theme.backgroundDeep, 0.62) }
        Box { sx: -2000; sy: 94; sw: 4220; sh: 2038; color: Theme.alpha(Theme.backgroundDeep, 0.62) }
        Box { sx: -2000; sy: 30; sw: 2058; sh: 64; color: Theme.alpha(Theme.backgroundDeep, 0.62) }
        Box { sx: 166; sy: 30; sw: 2054; sh: 64; color: Theme.alpha(Theme.backgroundDeep, 0.62) }
        Repeater {
            model: regionArt.dashes
            delegate: Box {
                required property var modelData
                sx: modelData.x; sy: modelData.y; sw: modelData.w; sh: modelData.h
                color: Theme.accent
            }
        }
        Repeater {
            model: [[55, 27], [163, 27], [55, 91], [163, 91]]
            delegate: Framed {
                required property var modelData
                sx: modelData[0]; sy: modelData[1]; sw: 6; sh: 6
                color: Theme.textPrimary
                border.color: Theme.accent
            }
        }
        Box {
            visible: !regionArt.rec
            sx: 84; sy: 102; sw: 56; sh: 16; radius: 4
            color: Theme.accent
            Text {
                anchors.centerIn: parent
                text: "1280×720"
                color: Theme.textOnAccent
                font.pixelSize: 9
                font.weight: Font.DemiBold
                font.family: "monospace"
            }
        }
        RecBadge { visible: regionArt.rec; sx: 58; sy: 7 }
    }

    component WindowArt: Item {
        property bool rec: false
        width: 220; height: 132
        Box { sx: 18; sy: 16; sw: 100; sh: 70; radius: 5; color: Theme.secondary; opacity: parent.rec ? 0.3 : 0.55 }
        Box { sx: 128; sy: 62; sw: 76; sh: 56; radius: 5; color: Theme.secondary; opacity: parent.rec ? 0.3 : 0.55 }
        Framed { sx: 58; sy: 34; sw: 110; sh: 78; rx: 6; stroke: 2; color: Theme.secondary; border.color: Theme.accent }
        Box { sx: 58; sy: 49.5; sw: 110; sh: 1; color: Theme.alpha(Theme.accent, 0.4) }
        Box { sx: 65.5; sy: 39.5; sw: 5; sh: 5; radius: 2.5; color: Theme.accent }
        Box { sx: 74.5; sy: 39.5; sw: 5; sh: 5; radius: 2.5; color: Theme.alpha(Theme.accent, 0.5) }
        Box { sx: 68; sy: 60; sw: 54; sh: 5; radius: 2.5; color: Theme.tertiary }
        Box { sx: 68; sy: 72; sw: 90; sh: 30; radius: 3; color: Theme.tertiary }
        RecBadge { visible: parent.rec; sx: 58; sy: 12 }
    }

    component ScrollArt: Item {
        width: 220; height: 132
        Box { sx: 70; sy: -10; sw: 80; sh: 160; radius: 4; color: Theme.secondary }
        Repeater {
            model: [[80, 4, 44, 5, 2.5], [80, 16, 60, 22, 3], [80, 46, 52, 5, 2.5],
                    [80, 58, 60, 22, 3], [80, 88, 40, 5, 2.5], [80, 100, 60, 22, 3]]
            delegate: Box {
                required property var modelData
                sx: modelData[0]; sy: modelData[1]; sw: modelData[2]; sh: modelData[3]
                radius: modelData[4]
                color: Theme.tertiary
            }
        }
        Framed { sx: 64; sy: 40; sw: 92; sh: 50; rx: 3; color: Theme.alpha(Theme.accent, 0.14); border.color: Theme.accent }
        // The scroll arrow: a shaft and a chevron, round-capped 1.75 px strokes.
        Box { sx: 175.125; sy: 35.125; sw: 1.75; sh: 59.75; radius: 0.875; color: Theme.accent }
        Repeater {
            model: [[173, 53.13], [179, -53.13]]
            delegate: Box {
                required property var modelData
                sx: modelData[0] - 5.875; sy: 90 - 0.875; sw: 11.75; sh: 1.75
                radius: 0.875
                rotation: modelData[1]
                color: Theme.accent
            }
        }
    }

    // A round-capped straight stroke from (x1, y1) to (x2, y2): the SVG line
    // without QtQuick.Shapes. A cap reaches half the stroke past each end.
    component Stroke: Rectangle {
        property real x1
        property real y1
        property real x2
        property real y2
        property real w: 2
        readonly property real len: Math.sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1))
        x: (x1 + x2) / 2 - (len + w) / 2
        y: (y1 + y2) / 2 - w / 2
        width: len + w; height: w
        radius: w / 2
        rotation: Math.atan2(y2 - y1, x2 - x1) * 180 / Math.PI
    }

    // The Edit page's "Edit an image": a screenshot with the editor's marks
    // on it, a pixelated patch, a box with its numbered step, an arrow.
    component EditImageArt: Item {
        width: 220; height: 132
        Box { sx: 28; sy: 14; sw: 164; sh: 104; radius: 5; color: Theme.secondary }
        Box { sx: 40; sy: 26; sw: 52; sh: 5; radius: 2.5; color: Theme.tertiary }
        Box { sx: 40; sy: 38; sw: 76; sh: 44; radius: 3; color: Theme.tertiary }
        Box { sx: 124; sy: 38; sw: 56; sh: 5; radius: 2.5; color: Theme.tertiary }
        Box { sx: 124; sy: 50; sw: 44; sh: 5; radius: 2.5; color: Theme.tertiary }
        Box { sx: 40; sy: 96; sw: 64; sh: 5; radius: 2.5; color: Theme.tertiary }
        Repeater {
            model: 24
            delegate: Box {
                required property int index
                sx: 124 + (index % 8) * 7; sy: 64 + Math.floor(index / 8) * 7; sw: 7; sh: 7
                color: Theme.alpha(Theme.accent, [0.18, 0.34, 0.5][(index * 7 + Math.floor(index / 8)) % 3])
            }
        }
        Framed { sx: 36; sy: 34; sw: 84; sh: 52; rx: 3; stroke: 2; color: "transparent"; border.color: Theme.accent }
        Box {
            sx: 111; sy: 25; sw: 18; sh: 18; radius: 9
            color: Theme.accent
            Text {
                anchors.centerIn: parent
                text: "1"
                color: Theme.textOnAccent
                font.pixelSize: 10
                font.weight: Font.Bold
                font.family: "monospace"
            }
        }
        Stroke { x1: 172; y1: 109; x2: 124; y2: 89; color: Theme.accent }
        Stroke { x1: 124; y1: 89; x2: 130.5; y2: 97.9; color: Theme.accent }
        Stroke { x1: 124; y1: 89; x2: 134.8; y2: 87.05; color: Theme.accent }
    }

    // The Edit page's "Trim a video": a filmstrip with the kept range framed
    // and the rest stepping back, over a timeline with its two trim handles.
    component TrimVideoArt: Item {
        width: 220; height: 132
        Box { sx: 14; sy: 18; sw: 192; sh: 62; radius: 4; color: Theme.secondary }
        Repeater {
            model: 12
            delegate: Item {
                required property int index
                Box { sx: 19 + index * 16; sy: 22; sw: 8; sh: 4; radius: 1; color: Theme.alpha(Theme.backgroundDeep, 0.5) }
                Box { sx: 19 + index * 16; sy: 72; sw: 8; sh: 4; radius: 1; color: Theme.alpha(Theme.backgroundDeep, 0.5) }
            }
        }
        Repeater {
            model: 5
            delegate: Box {
                required property int index
                sx: 18 + index * 38; sy: 30; sw: 32; sh: 38; radius: 2
                color: Theme.tertiary
            }
        }
        // Outside the kept range. A rounded outer end and a square inner one,
        // so the dim meets the frame without a notch.
        Box { sx: 14; sy: 18; sw: 39; sh: 62; radius: 4; color: Theme.alpha(Theme.backgroundDeep, 0.62) }
        Box { sx: 167; sy: 18; sw: 39; sh: 62; radius: 4; color: Theme.alpha(Theme.backgroundDeep, 0.62) }
        Framed { sx: 53; sy: 18; sw: 114; sh: 62; rx: 3; stroke: 2; color: "transparent"; border.color: Theme.accent }
        Box { sx: 14; sy: 96; sw: 192; sh: 4; radius: 2; color: Theme.tertiary }
        Box { sx: 53; sy: 96; sw: 114; sh: 4; color: Theme.accent }
        Repeater {
            model: [49, 163]
            delegate: Box {
                required property var modelData
                sx: modelData; sy: 88; sw: 8; sh: 20; radius: 3
                color: Theme.accent
                Box { sx: 2.75; sy: 6; sw: 2.5; sh: 8; radius: 1.25; color: Theme.alpha(Theme.textOnAccent, 0.6) }
            }
        }
        Box {
            sx: 86; sy: 110; sw: 48; sh: 16; radius: 4
            color: Theme.accent
            Text {
                anchors.centerIn: parent
                text: "00:07"
                color: Theme.textOnAccent
                font.pixelSize: 9
                font.weight: Font.DemiBold
                font.family: "monospace"
            }
        }
    }
}
