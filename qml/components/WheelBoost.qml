import QtQuick

// Faster, deterministic wheel scrolling for a Flickable: each mouse-wheel
// notch moves a fixed pixel step (Flickable's built-in wheel physics needed
// far too many notches on long pages), while touchpads keep their precise
// 1:1 pixelDelta feel. Declare as a child of the Flickable, like
// MiddleScroll. Wheel-only (NoButton) and z:-1 — clicks, hover and the
// middle-click autoscroll pass through untouched.
MouseArea {
    id: area
    required property Flickable flickable
    // Pixels scrolled per mouse-wheel notch.
    property real stepPx: 220

    anchors.fill: parent
    z: -1
    acceptedButtons: Qt.NoButton

    onWheel: (w) => {
        const f = area.flickable
        if (!f) {
            w.accepted = false
            return
        }
        const dy = w.pixelDelta.y !== 0 ? w.pixelDelta.y
                                        : (w.angleDelta.y / 120) * area.stepPx
        const dx = w.pixelDelta.x !== 0 ? w.pixelDelta.x
                                        : (w.angleDelta.x / 120) * area.stepPx
        if (f.contentHeight > f.height)
            f.contentY = Math.max(0, Math.min(f.contentHeight - f.height, f.contentY - dy))
        if (f.contentWidth > f.width)
            f.contentX = Math.max(0, Math.min(f.contentWidth - f.width, f.contentX - dx))
        w.accepted = true
    }
}
