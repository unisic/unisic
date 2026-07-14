#pragma once

#include <QObject>

// KDE's notification server exposes the freedesktop Inhibit/UnInhibit pair.
// Keep it behind an explicit setting and a KDE capability gate: other desktops
// either do not implement the calls or use incompatible private APIs.
class NotificationInhibitor final : public QObject
{
    Q_OBJECT
public:
    explicit NotificationInhibitor(QObject *parent = nullptr);
    ~NotificationInhibitor() override;

    static bool supportedDesktop();
    void acquire();
    void release();
    bool active() const { return m_cookie != 0 || m_pending; }

private:
    void sendRelease();

    uint m_cookie = 0;
    int m_depth = 0;
    bool m_pending = false;
};
