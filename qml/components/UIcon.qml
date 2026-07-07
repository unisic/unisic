import QtQuick
import Unisic

// Renders a themed icon via the C++ image://icon provider. The tint color and
// the theme revision are part of the URL so the image re-fetches when either
// changes (live re-theming / active-state tinting).
Image {
    id: root
    property string name: ""
    property color color: Theme.textPrimary
    property int size: 18

    function _hex(c) {
        function h(v) { var s = Math.round(v * 255).toString(16); return s.length < 2 ? "0" + s : s }
        return "%23" + h(c.r) + h(c.g) + h(c.b)
    }

    source: name === "" ? ""
            : "image://icon/" + name + "?color=" + _hex(color) + "&sz=" + size + "&v=" + Theme.rev
    sourceSize: Qt.size(size, size)
    width: size
    height: size
    // The tint color's alpha is dropped from the URL (opaque #RRGGBB); honor it
    // here so alpha-bearing tokens (textSecondary/textTertiary) dim like their
    // sibling label text.
    opacity: color.a
    fillMode: Image.PreserveAspectFit
    smooth: true
    mipmap: true
    cache: false
    visible: name !== ""
    // Synchronous: the C++ image://icon provider (Pixmap) uses QIcon::fromTheme
    // and qApp->palette(), which must run on the GUI thread, not the async
    // QQuickPixmapReader thread.
    asynchronous: false
}
