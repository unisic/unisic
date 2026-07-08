#pragma once
#include <QObject>
#include <QHash>

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
    // Shortcuts KCM) stays authoritative — call this on startup for every action.
    void defineAction(const QString &actionId, const QString &friendlyName,
                      const QString &defaultKeySequence);

    // Push a user-chosen shortcut from Unisic's own settings to KGlobalAccel so
    // the change propagates to the DE. Call this only when the user edits a key
    // (or on startup for a deliberately forced binding). Returns false when the
    // daemon did not take the requested keys — typically because another
    // component already owns them (e.g. stock Plasma binds Ctrl+Esc).
    bool setShortcut(const QString &actionId, const QString &friendlyName,
                     const QString &keySequence);

    void unregisterAll();

    // False when org.kde.kglobalaccel is absent (non-KDE session) — callers
    // must not report a bind "conflict" that is really just no daemon.
    bool available() const { return m_available; }

    // The action's currently ACTIVE keys as reported by the daemon (empty when
    // unbound or the daemon is unavailable). Lets startup verify that an
    // IsDefault registration actually resulted in a live binding.
    QList<int> activeKeys(const QString &actionId) const;

signals:
    void activated(const QString &actionId);

private slots:
    void onShortcutPressed(const QString &componentUnique, const QString &actionUnique,
                           qlonglong timestamp);

private:
    QStringList fullActionId(const QString &actionId, const QString &friendlyName) const;
    QList<int> keysFor(const QString &keySequence) const;
    void ensureSignalConnected();

    static constexpr const char *COMPONENT = "unisic";
    bool m_available = false;
    bool m_signalConnected = false;
};
