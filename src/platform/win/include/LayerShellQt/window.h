#pragma once
// Windows build only: stands in for LayerShellQt's <LayerShellQt/window.h>,
// which does not exist off Wayland. get() always answers nullptr, and every
// call site is already `if (auto *ls = LayerShellQt::Window::get(win))`, so
// the layer-shell configuration is skipped and the shared code compiles
// unchanged. Only the members the app calls are declared; a new call on
// Linux needs a matching no-op line here.
#include <QFlags>
#include <QMargins>
#include <QString>

class QWindow;

namespace LayerShellQt {

class Window
{
public:
    enum Anchor {
        AnchorNone = 0,
        AnchorTop = 1,
        AnchorBottom = 2,
        AnchorLeft = 4,
        AnchorRight = 8,
    };
    Q_DECLARE_FLAGS(Anchors, Anchor)

    enum Layer {
        LayerBackground = 0,
        LayerBottom = 1,
        LayerTop = 2,
        LayerOverlay = 3,
    };

    enum KeyboardInteractivity {
        KeyboardInteractivityNone = 0,
        KeyboardInteractivityExclusive = 1,
        KeyboardInteractivityOnDemand = 2,
    };

    static Window *get(QWindow *) { return nullptr; }

    void setAnchors(Anchors) {}
    void setExclusiveZone(int) {}
    void setMargins(const QMargins &) {}
    void setKeyboardInteractivity(KeyboardInteractivity) {}
    void setLayer(Layer) {}
    void setScope(const QString &) {}
};

} // namespace LayerShellQt

Q_DECLARE_OPERATORS_FOR_FLAGS(LayerShellQt::Window::Anchors)
