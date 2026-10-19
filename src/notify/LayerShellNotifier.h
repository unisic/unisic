#pragma once
#include <QObject>
#include <QVariantMap>

class AppContext;
class CaptureNotification;
class QQuickWindow;
class QScreen;

// The capture card on a layer-shell compositor (KWin, wlroots, COSMIC, muffin
// 6.7+): a card-sized overlay surface anchored to the chosen corner, which the
// compositor keeps on top, clear of panels, and - the part that matters - whose
// enter/leave it delivers honestly. On Windows the same card is a topmost
// window that places itself (CardSurface_win.cpp). AppContext uses the
// XWayland helper (GNOME) or a native desktop notification when neither is
// available.
class LayerShellNotifier : public QObject
{
    Q_OBJECT
public:
    explicit LayerShellNotifier(AppContext *app, QObject *parent = nullptr);

    // Runtime probe: does the current compositor expose zwlr_layer_shell_v1?
    static bool compositorSupportsLayerShell();
    // True where the card needs no layer-shell because the window system keeps
    // a topmost window on top and lets it place itself: Windows only.
    static bool placesCardItself();

    // Build + show the card for `n` on the overlay layer. Takes ownership of `n`.
    // `overrides` lets the settings preview render values the user has not
    // saved (see NotifCard::effectiveSettings); empty for a real capture.
    void show(CaptureNotification *n, const QVariantMap &overrides = {});

private:
    // Before the first show(): keep `win` on top and put it in the corner that
    // `position` names ("top-left", "bottom-right"...), `edge` px in from the
    // screen edge. CardSurface.cpp / CardSurface_win.cpp.
    static void place(QQuickWindow *win, QScreen *screen, const QString &position, int edge);

    AppContext *m_app;
};
