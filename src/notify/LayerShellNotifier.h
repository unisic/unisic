#pragma once
#include <QObject>
#include <QVariantMap>

class AppContext;
class CaptureNotification;

// Shows the post-capture card as a wlr-layer-shell OVERLAY surface — reliably
// above other windows on compositors that implement layer-shell (KWin, wlroots)
// while keeping the app's own custom card + action buttons. Only built when
// The capture card on a layer-shell compositor (KWin, wlroots, COSMIC, muffin
// 6.7+): a card-sized overlay surface anchored to the chosen corner, which the
// compositor keeps on top, clear of panels, and — the part that matters — whose
// enter/leave it delivers honestly. AppContext uses the XWayland helper
// (GNOME) or a native desktop notification when this is unavailable.
class LayerShellNotifier : public QObject
{
    Q_OBJECT
public:
    explicit LayerShellNotifier(AppContext *app, QObject *parent = nullptr);

    // Runtime probe: does the current compositor expose zwlr_layer_shell_v1?
    static bool compositorSupportsLayerShell();

    // Build + show the card for `n` on the overlay layer. Takes ownership of `n`.
    // `overrides` lets the settings preview render values the user has not
    // saved (see NotifCard::effectiveSettings); empty for a real capture.
    void show(CaptureNotification *n, const QVariantMap &overrides = {});

private:
    AppContext *m_app;
};
