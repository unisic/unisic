import QtQuick

// Browser-style middle-button autoscroll for any Flickable: hold the wheel
// button and drag — scroll velocity is proportional to the displacement from
// the press point. Declare as the last child of the Flickable itself. It only
// accepts the middle button, so clicks/hover/wheel pass through untouched.
//
// Coordinates are mapped into the Flickable's viewport before use: children
// of a Flickable live in the moving contentItem, and measuring displacement
// there would feed the scroll back into itself (runaway acceleration).
MouseArea {
    id: area
    required property Flickable flickable
    // Scroll velocity in px/s per px of displacement from the press point.
    property real gain: 10

    anchors.fill: parent
    acceptedButtons: Qt.MiddleButton
    // Deliberately no cursorShape: setting it would claim the cursor for this
    // (topmost, full-size) area and override every control underneath.

    property real originX: 0
    property real originY: 0
    onPressed: (m) => {
        const p = area.mapToItem(flickable, m.x, m.y)
        originX = p.x
        originY = p.y
    }

    Timer {
        interval: 16
        repeat: true
        running: area.pressed
        onTriggered: {
            const f = area.flickable
            if (!f)
                return
            const p = area.mapToItem(f, area.mouseX, area.mouseY)
            const dt = interval / 1000
            if (f.contentHeight > f.height) {
                const vy = (p.y - area.originY) * area.gain
                f.contentY = Math.max(0, Math.min(f.contentHeight - f.height,
                                                  f.contentY + vy * dt))
            }
            if (f.contentWidth > f.width) {
                const vx = (p.x - area.originX) * area.gain
                f.contentX = Math.max(0, Math.min(f.contentWidth - f.width,
                                                  f.contentX + vx * dt))
            }
        }
    }
}
