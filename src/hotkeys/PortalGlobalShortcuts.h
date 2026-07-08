#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QVariantMap>
#include <QDBusObjectPath>

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
    // pre-filter and the CreateSession/BindShortcuts response as the truth.
    static bool interfacePresent();

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

    QString m_sessionHandle; // object path string from the CreateSession response
    bool m_signalConnected = false;
    bool m_sessionPending = false; // CreateSession round-trip in flight
    bool m_retriedBind = false;    // one stale-session retry per bind attempt
    QVector<Shortcut> m_queued;    // newest set requested while pending
    QVector<Shortcut> m_lastBound; // for transparent re-bind after a portal restart
};
