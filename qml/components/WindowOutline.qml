import QtQuick
import Unisic.Kit

// A 1 px frame around a frameless window, so its edge stays visible over a
// dark wallpaper or another dark window. System decorations draw their own;
// a maximized or fullscreen window has the screen edge. Paints over the
// content and takes no input.
Rectangle {
    readonly property var _win: Window.window
    anchors.fill: parent
    z: 999
    visible: !App.settings.useSystemDecoration && _win !== null
             && _win.visibility !== Window.Maximized
             && _win.visibility !== Window.FullScreen
    color: "transparent"
    border.width: 1
    border.color: Theme.alpha(Theme.accent, 0.30)
}
