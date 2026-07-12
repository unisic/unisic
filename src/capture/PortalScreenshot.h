#pragma once
#include <QObject>
#include <QImage>
#include <functional>

// org.freedesktop.portal.Screenshot backend. Works on any Wayland desktop.
// A non-interactive request needs a stored permission (or a resolvable app
// identity to prompt for one); when it is denied we automatically retry once
// with interactive=true so the desktop's own screenshot dialog appears and
// capture still works on machines where Unisic isn't installed/authorized yet.
class PortalScreenshot : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const QImage &image, const QString &error)>;

    explicit PortalScreenshot(QObject *parent = nullptr) : QObject(parent) {}

    void capture(bool interactive, Callback cb, bool allowInteractiveFallback = true);

    // The app ids the portal may resolve this process to. The permission
    // store is keyed by that id, so the silent-screenshot grant must cover
    // every candidate: the desktop-file id, "" (unresolvable host launches —
    // terminal, AppImage), and the id parsed from our own systemd scope with
    // the same app-<launcher>-<ID>-<rand>.scope rule the portal uses (a
    // menu-launched app whose scope id differs from desktopFileName would
    // otherwise fall through the first two).
    static QStringList candidateAppIds();

    // Write the "screenshot" permission-store grant for every candidate id,
    // then invoke `then` once the store has acknowledged all writes. Runs
    // before EVERY silent request — not just at startup — so a permanent
    // "no" left behind by a denied GNOME access dialog (its Deny is sticky
    // and makes every non-interactive request fail with response code 2)
    // is repaired instead of dooming silent capture forever.
    static void ensureSilentPermission(QObject *context, std::function<void()> then);

private:
    void requestOnce(bool interactive, Callback cb);
    void sendRequest(bool interactive, Callback cb);
};
