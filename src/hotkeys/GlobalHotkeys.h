#pragma once
#include <QObject>
#include <QHash>
#include <QSet>

class QDBusMessage;

// Global shortcuts on Plasma Wayland via the org.kde.KGlobalAccel DBus
// service (no KDE Frameworks link dependency). Falls back to doing nothing
// on desktops without KGlobalAccel — the tray menu still provides access.
class GlobalHotkeys : public QObject
{
    Q_OBJECT
public:
    explicit GlobalHotkeys(QObject *parent = nullptr);

    // Register the action with KGlobalAccel and set its default shortcut. Uses
    // autoloading so KDE's stored key (incl. edits made in the System Settings
    // Shortcuts KCM) stays authoritative — call this on startup for every
    // action. The setShortcut reply is deliberately NOT surfaced: kglobalacceld
    // (observed live) echoes the requested keys on an IsDefault call even when
    // it only filled the default column and the ACTIVE binding stayed "none" —
    // only a real activeKeys() query tells the truth.
    void defineAction(const QString &actionId, const QString &friendlyName,
                      const QString &defaultKeySequence);

    // Push a user-chosen shortcut from Unisic's own settings to KGlobalAccel so
    // the change propagates to the DE. Call this only when the user edits a key
    // (or on startup for a deliberately forced binding). Returns false when the
    // daemon did not take the requested keys — typically because another
    // component already owns them (e.g. stock Plasma binds Ctrl+Esc).
    bool setShortcut(const QString &actionId, const QString &friendlyName,
                     const QString &keySequence);
    // Fire-and-forget async unbind (empty key, SetPresent|NoAutoloading). For
    // callers that never inspect the result — avoids blocking the GUI thread.
    void releaseShortcut(const QString &actionId, const QString &friendlyName);

    void unregisterAll();

    // False when org.kde.kglobalaccel is absent (non-KDE session) — callers
    // must not report a bind "conflict" that is really just no daemon.
    bool available() const { return m_available; }

    // The action's currently ACTIVE keys as reported by the daemon. `ok`
    // separates "unbound" (reply arrived, no keys) from "call failed/timed
    // out" — treating a timeout as unbound once wiped the user's keys via the
    // daemon-authoritative sync.
    QList<int> activeKeys(const QString &actionId, bool *ok = nullptr) const;

    // First non-empty key as a portable QKeySequence string ("Meta+Shift+1"),
    // for daemon-authoritative display in the settings UI.
    static QString portableFromKeys(const QList<int> &keys);
    QString activeKeysPortable(const QString &actionId, bool *ok = nullptr) const
    { return portableFromKeys(activeKeys(actionId, ok)); }

signals:
    void activated(const QString &actionId);
    // A binding for one of OUR actions changed daemon-side (KCM edit, another
    // client) — portableKeys is the new active key ("" = unbound).
    void shortcutChanged(const QString &actionId, const QString &portableKeys);

private slots:
    void onShortcutPressed(const QString &componentUnique, const QString &actionUnique,
                           qlonglong timestamp);
    void onYourShortcutsChanged(const QStringList &actionId, const QList<int> &newKeys);
    void onYourShortcutsListChanged(const QDBusMessage &msg);

private:
    QStringList fullActionId(const QString &actionId, const QString &friendlyName) const;
    QList<int> keysFor(const QString &keySequence) const;
    void ensureSignalConnected();

    static constexpr const char *COMPONENT = "unisic";
    bool m_available = false;
    bool m_signalConnected = false;
    QSet<QString> m_registered; // actions doRegister'ed this process (idempotent skip)
};
