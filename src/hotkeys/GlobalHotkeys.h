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
    // Fully remove a single action from the daemon (and the Shortcuts KCM),
    // unlike releaseShortcut() which only unbinds its keys.
    void unregisterAction(const QString &actionId);

    // False when org.kde.kglobalaccel is absent (non-KDE session) — callers
    // must not report a bind "conflict" that is really just no daemon.
    bool available() const { return m_available; }

    // The action's currently ACTIVE keys as reported by the daemon. `ok`
    // separates "unbound" (reply arrived, no keys) from "call failed/timed
    // out" — treating a timeout as unbound once wiped the user's keys via the
    // daemon-authoritative sync.
    QList<int> activeKeys(const QString &actionId, bool *ok = nullptr) const;

    // Which action the daemon resolves `key` to, as "component/action" (empty
    // when unowned or on error). Two components can BOTH list the same key in
    // their bindings (observed live: a KWin tiling script held Meta+Shift+F
    // while our capture-fullscreen binding still listed it) — only this
    // daemon-side lookup tells who actually receives the press.
    QString keyOwner(int key) const;

    // Shift+digit bindings are pushed with their shifted-symbol variants as
    // alternates (KWin Wayland reports the press with shift consumed — see
    // the impl); portableFromKeys() collapses them back for display.
    static QList<int> expandShiftDigitVariants(const QList<int> &keys);

    // True when two portable strings describe the same binding SET — the
    // daemon reorders alternate keys in its replies ([F9, Meta+F9] comes
    // back as [Meta+F9, F9]), so string comparison over-reports drift.
    static bool sameBinding(const QString &a, const QString &b);

    // First non-empty key as a portable QKeySequence string ("Meta+Shift+1"),
    // for daemon-authoritative display in the settings UI.
    static QString portableFromKeys(const QList<int> &keys);

    // "unisic/" (or "unisic-dev/" in dev builds) — for matching keyOwner()
    // results against our own component.
    static QString componentPrefix()
    { return QString::fromLatin1(COMPONENT) + QLatin1Char('/'); }
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

    // Portable string -> flat combined-int list (each chord of a multi-key
    // string becomes one ALTERNATE key of the action).
    static QList<int> keysFor(const QString &keySequence);

private:
    QStringList fullActionId(const QString &actionId, const QString &friendlyName) const;
    void ensureSignalConnected();

    // Dev builds register a SEPARATE component, so a dev instance never
    // double-dispatches (or steals) the stable app's hotkeys. The dev config
    // is seeded with all hotkeys unbound for the same reason (Settings.h).
    // Underscore, NOT a dash: the daemon exposes each component at
    // /component/<unique> and a dash is invalid in a D-Bus object path — the
    // daemon sanitizes it to '_' while our signal subscription wouldn't,
    // leaving shortcut presses undeliverable.
#ifdef UNISIC_DEV_BUILD
    static constexpr const char *COMPONENT = "unisic_dev";
    static constexpr const char *COMPONENT_FRIENDLY = "Unisic (dev)";
#else
    static constexpr const char *COMPONENT = "unisic";
    static constexpr const char *COMPONENT_FRIENDLY = "Unisic";
#endif
    bool m_available = false;
    bool m_signalConnected = false;
    QSet<QString> m_registered; // actions doRegister'ed this process (idempotent skip)
};
