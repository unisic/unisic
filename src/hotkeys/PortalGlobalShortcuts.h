#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QVariantMap>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <functional>

// Global hotkeys via org.freedesktop.portal.GlobalShortcuts — the portable
// path on desktops without KGlobalAccel (GNOME 48+, Hyprland; KDE also
// implements it but KGlobalAccel is preferred there: silent, richer).
//
// Flow per app run (session handles are NOT stable across restarts):
//   CreateSession -> BindShortcuts(all actions, preferred triggers) ->
//   listen for Activated on the session.
// The *bindings* persist backend-side keyed by app id, and re-binding an
// unchanged set does not re-prompt on KDE (>= Plasma 6.4) or GNOME — the
// consent dialog appears once. Hyprland registers the ids and the user binds
// keys in hyprland.conf ("global" dispatcher); preferred_trigger is a
// suggestion everywhere, so the app only ever learns the human-readable
// trigger_description back, never raw keys.
class PortalGlobalShortcuts : public QObject
{
    Q_OBJECT
public:
    struct Shortcut {
        QString id;               // stable action id, e.g. "capture-region"
        QString description;      // user-visible purpose (consent dialog)
        QString preferredTrigger; // spec format, e.g. "CTRL+SHIFT+s" (may be empty)
    };

    explicit PortalGlobalShortcuts(QObject *parent = nullptr);

    // The frontend only exposes the interface when a backend module claims
    // it — but a claimed interface can still be broken (xdp-gnome's backend
    // is hardwired to org.gnome.Shell and fails on niri), so treat this as a
    // Async variant: the Get may D-Bus-ACTIVATE the portal at cold session
    // start (multi-hundred-ms), so the blocking form stalls the GUI thread at
    // startup. `done(present)` is delivered on ctx's thread; dropped if ctx
    // is destroyed first.
    static void probeInterface(QObject *ctx, std::function<void(bool)> done);

    // Convert a QKeySequence portable string ("Ctrl+Shift+S") to the
    // freedesktop Shortcuts spec trigger ("CTRL+SHIFT+s"). Empty on no-parse.
    static QString toPortalTrigger(const QString &portableKeySequence);

    // Create the session (if needed) and bind the full set. Safe to call again
    // with a changed set — the backend may show its consent dialog once.
    void bind(const QVector<Shortcut> &shortcuts);

signals:
    void activated(const QString &shortcutId);
    // Bind result: ok=false means no working backend (fall back to the
    // compositor-binds hint); descriptions are the backend's human-readable
    // per-id trigger texts when given.
    void bindFinished(bool ok, const QVariantMap &triggerDescriptions);

private slots:
    void onActivated(const QDBusObjectPath &sessionHandle, const QString &shortcutId,
                     qulonglong timestamp, const QVariantMap &options);
    void onSessionClosed();

private:
    void createSession(const QVector<Shortcut> &shortcuts);
    void bindNow(const QVector<Shortcut> &shortcuts);
    // Disconnect our Closed subscription for `handle` and Close() it so an
    // abandoned session (and its D-Bus match rule) does not leak daemon-side.
    void closeSession(const QString &handle);
    // Registry.Register our desktop id as the FIRST call on m_bus (see the
    // ctor comment for why identity needs a private connection).
    void registerAppId();

    // PRIVATE bus connection for everything this class does. The portal keys
    // app identity per D-Bus connection, resolved and pinned at the FIRST
    // portal call; on GNOME Qt's xdgdesktopportal platform theme reads the
    // Settings portal during QGuiApplication construction, pinning the shared
    // session bus to app id "" for terminal/AppImage launches — and GNOME's
    // shortcuts provider DISCARDS BindShortcuts from an empty app id. A fresh
    // connection whose first call is Registry.Register carries a real id.
    QDBusConnection m_bus;
    QString m_sessionHandle; // object path string from the CreateSession response
    bool m_signalConnected = false;
    bool m_sessionPending = false; // CreateSession round-trip in flight
    bool m_bindPending = false;    // BindShortcuts Response round-trip in flight
    bool m_bindQueued = false;     // a newer set arrived during an in-flight bind
    bool m_retriedBind = false;    // one stale-session retry per bind attempt
    bool m_needRebind = false;     // portal owner lost — re-bind on owner gain
    QVector<Shortcut> m_queued;    // newest set requested while pending
    QVector<Shortcut> m_lastBound; // for transparent re-bind after a portal restart
};
