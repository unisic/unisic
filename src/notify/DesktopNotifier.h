#pragma once
#include <QObject>
#include <QHash>
#include <QPointer>

class AppContext;
class CaptureNotification;

// Posts the post-capture "ready" message as a real freedesktop desktop
// notification (org.freedesktop.Notifications) with an inline thumbnail and
// Open / Copy / Upload / Delete action buttons. The compositor's notification
// server draws it — always above other windows, on every desktop — which the
// old custom fullscreen popup could not guarantee on Wayland (no keep-above).
// ActionInvoked signals are routed back to the owning CaptureNotification.
class DesktopNotifier : public QObject
{
    Q_OBJECT
public:
    explicit DesktopNotifier(AppContext *app, QObject *parent = nullptr);

    // Is a notification server present on the session bus?
    static bool available();

    // Show a notification for `n`. Takes ownership: `n` is deleted once the
    // notification closes and no upload it started is still in flight.
    void show(CaptureNotification *n);

    // Live-tracked notification-inhibition state (KDE flips this true while an
    // application is fullscreen, during Do-Not-Disturb, and while sharing the
    // screen). The layer-shell card consults this to stay off a fullscreen app;
    // the native path doesn't need it — the server suppresses on its own.
    bool inhibited() const { return m_inhibited; }

private slots:
    void onActionInvoked(uint id, const QString &actionKey);
    void onNotificationClosed(uint id, uint reason);
    void onPropertiesChanged(const QString &interfaceName, const QVariantMap &changed,
                             const QStringList &invalidated);

private:
    uint sendNotify(CaptureNotification *n);
    void retire(CaptureNotification *n);

    AppContext *m_app;
    QHash<uint, QPointer<CaptureNotification>> m_active; // server id -> backing object
    bool m_inhibited = false; // notifications currently suppressed (fullscreen/DND/sharing)
};
