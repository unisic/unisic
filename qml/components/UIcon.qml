import QtQuick
import QtQuick.Window
import Unisic

// Renders a themed icon via the C++ image://icon provider. The tint color and
// the theme revision are part of the URL so the image re-fetches when either
// changes (live re-theming / active-state tinting).
Image {
    id: root
    property string name: ""
    property color color: Theme.textPrimary
    property int size: 18
    // "" = follow the app theme (system theme -> system icons); "custom" forces
    // the bundled glyph; "system" forces QIcon::fromTheme. Used by the editor
    // tool-icon selector; the main app chrome never sets this.
    property string iconStyle: ""

    // Request physical pixels — a logical-size raster is upscaled and blurry
    // on HiDPI screens.
    readonly property int _px: Math.round(size * (Screen.devicePixelRatio || 1))

    function _hex(c) {
        function h(v) { var s = Math.round(v * 255).toString(16); return s.length < 2 ? "0" + s : s }
        return "%23" + h(c.r) + h(c.g) + h(c.b)
    }

    source: name === "" ? ""
            : "image://icon/" + name + "?color=" + _hex(color) + "&sz=" + _px + "&v=" + Theme.rev
              + (iconStyle === "" ? "" : "&src=" + iconStyle)
    sourceSize: Qt.size(_px, _px)
    width: size
    height: size
    // The tint color's alpha is dropped from the URL (opaque #RRGGBB); honor it
    // here so alpha-bearing tokens (textSecondary/textTertiary) dim like their
    // sibling label text.
    opacity: color.a
    fillMode: Image.PreserveAspectFit
    smooth: true
    mipmap: true
    // Safe to cache: the URL embeds color+size+theme revision, so stale hits
    // are impossible; without it every re-evaluation re-hits the provider.
    cache: true
    visible: name !== ""
    // Synchronous: the C++ image://icon provider (Pixmap) uses QIcon::fromTheme
    // and qApp->palette(), which must run on the GUI thread, not the async
    // QQuickPixmapReader thread.
    asynchronous: false
}
