import QtQuick

// Browser-style middle-button autoscroll for any Flickable: hold the wheel
// button and drag — scroll velocity is proportional to the displacement from
// the press point. Declare as a child of the Flickable itself. It only
// accepts the middle button, so clicks/hover/wheel pass through untouched,
// and z:-1 keeps it UNDER the content so text fields still receive
// middle-click primary-selection paste.
//
// Positions are captured in SCENE coordinates at event time and reused by the
// timer. Children of a Flickable live in the moving contentItem: reading
// mouseX/mouseY (content coords) from the timer — or re-mapping them through
// the current transform — feeds the scroll back into the measurement (the
// pointer "moves" as the content slides underneath), which either runs away
// or decays the scroll to a halt. Scene coords only change on real pointer
// motion, which is exactly what positionChanged delivers.
MouseArea {
    id: area
    required property Flickable flickable
    // Scroll velocity in px/s per px of displacement from the press point.
    property real gain: 10

    anchors.fill: parent
    z: -1
    acceptedButtons: Qt.MiddleButton
    // Deliberately no cursorShape: setting it would claim the cursor for this
    // (full-size) area and override every control's cursor underneath.

    property real originX: 0
    property real originY: 0
    property real curX: 0
    property real curY: 0
    onPressed: (m) => {
        const p = area.mapToItem(null, m.x, m.y)
        originX = p.x
        originY = p.y
        curX = p.x
        curY = p.y
    }
    onPositionChanged: (m) => {
        const p = area.mapToItem(null, m.x, m.y)
        curX = p.x
        curY = p.y
    }

    Timer {
        interval: 16
        repeat: true
        running: area.pressed
        onTriggered: {
            const f = area.flickable
            if (!f)
                return
            const dt = interval / 1000
            if (f.contentHeight > f.height) {
                const vy = (area.curY - area.originY) * area.gain
                f.contentY = Math.max(0, Math.min(f.contentHeight - f.height,
                                                  f.contentY + vy * dt))
            }
            if (f.contentWidth > f.width) {
                const vx = (area.curX - area.originX) * area.gain
                f.contentX = Math.max(0, Math.min(f.contentWidth - f.width,
                                                  f.contentX + vx * dt))
            }
        }
    }
}
