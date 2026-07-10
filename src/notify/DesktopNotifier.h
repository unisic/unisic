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
    //
    // Stuck-inhibitor guard: third-party apps sometimes leave a notification
    // inhibitor registered forever (observed in the field: Inhibited == true
    // with no fullscreen window and no DND). Trusting the raw flag then mutes
    // EVERY capture card for the whole session. Only honor the flag once we
    // have seen it actually transition false->true during this run — a real
    // fullscreen/DND event; a stuck-since-startup inhibitor never transitions.
    bool inhibited() const { return m_inhibited && m_inhibitTransitionSeen; }

private slots:
    void onActionInvoked(uint id, const QString &actionKey);
    void onNotificationClosed(uint id, uint reason);
    void onPropertiesChanged(const QString &interfaceName, const QVariantMap &changed,
                             const QStringList &invalidated);

private:
    // Async: builds the Notify call without a blocking Introspect round-trip
    // and registers the id -> object mapping when the reply lands.
    void sendNotify(CaptureNotification *n);
    void retire(CaptureNotification *n);

    AppContext *m_app;
    QHash<uint, QPointer<CaptureNotification>> m_active; // server id -> backing object
    bool m_inhibited = false; // notifications currently suppressed (fullscreen/DND/sharing)
    bool m_inhibitTransitionSeen = false; // saw false->true this run (see inhibited())
};
