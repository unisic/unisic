#pragma once
#include <QObject>

class AppContext;
class CaptureNotification;

// Shows the post-capture card as a wlr-layer-shell OVERLAY surface — reliably
// above other windows on compositors that implement layer-shell (KWin, wlroots)
// while keeping the app's own custom card + action buttons. Only built when
// LayerShellQt is available (HAVE_LAYERSHELL); AppContext uses the native
// desktop notification instead when compositorSupportsLayerShell() is false
// (e.g. GNOME/Mutter, which don't implement the protocol).
class LayerShellNotifier : public QObject
{
    Q_OBJECT
public:
    explicit LayerShellNotifier(AppContext *app, QObject *parent = nullptr);

    // Runtime probe: does the current compositor expose zwlr_layer_shell_v1?
    static bool compositorSupportsLayerShell();

    // Build + show the card for `n` on the overlay layer. Takes ownership of `n`.
    void show(CaptureNotification *n);

private:
    AppContext *m_app;
};
