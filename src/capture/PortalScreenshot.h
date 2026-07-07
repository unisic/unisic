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

private:
    void requestOnce(bool interactive, Callback cb);
};
