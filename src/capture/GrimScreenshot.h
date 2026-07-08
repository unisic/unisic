#pragma once
#include <QObject>
#include <QImage>
#include <functional>

// wlr-screencopy screenshots via the `grim` CLI — the reliable path on
// wlroots-style compositors (niri, sway, river). Rationale for niri: its
// org.gnome.Shell.Screenshot implementation — which xdg-desktop-portal-gnome's
// Screenshot backend merely proxies — hard-fails with "internal error" on ANY
// multi-monitor setup (`ensure!(outputs.len() == 1)`, niri issue #117), so
// both the portal and the direct D-Bus path die identically there.
// wlr-screencopy is per-output and unaffected. `grim` with no output argument
// composites every output into one image in layout coordinates, which is
// exactly Unisic's "workspace" capture shape.
// No Q_OBJECT: the class declares no signals/slots/properties of its own —
// plain QObject is enough for connect() context lifetimes.
class GrimScreenshot : public QObject
{
public:
    using Callback = std::function<void(const QImage &image, const QString &error)>;
    using QObject::QObject;

    // grim binary present AND a Wayland session. grim opens its own
    // wl_display connection, so it works even when the app itself runs under
    // XWayland (e.g. an AppImage without the Qt wayland plugin).
    static bool isAvailable();

    // Whole layout (all outputs), PNG via stdout, decoded off the GUI thread.
    void captureWorkspace(bool includeCursor, Callback cb);
    // One output by its Wayland name (QScreen::name() when Qt runs natively on
    // Wayland) — native resolution, no mixed-DPI cropping involved.
    void captureOutput(const QString &outputName, bool includeCursor, Callback cb);

private:
    void run(const QStringList &args, Callback cb);
};
