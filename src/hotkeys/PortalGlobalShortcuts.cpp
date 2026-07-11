#include "PortalGlobalShortcuts.h"
#include "capture/PortalRequest.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QGuiApplication>
#include <QKeySequence>
#include <QDebug>

static const auto PORTAL_SERVICE = QStringLiteral("org.freedesktop.portal.Desktop");
static const auto PORTAL_PATH = QStringLiteral("/org/freedesktop/portal/desktop");
static const auto GS_IFACE = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");

// Wire type for BindShortcuts' a(sa{sv}) — no default QDBus marshaller exists.
struct PortalShortcutWire {
    QString id;
    QVariantMap data;
};
Q_DECLARE_METATYPE(PortalShortcutWire)

QDBusArgument &operator<<(QDBusArgument &a, const PortalShortcutWire &s)
{
    a.beginStructure();
    a << s.id << s.data;
    a.endStructure();
    return a;
}

const QDBusArgument &operator>>(const QDBusArgument &a, PortalShortcutWire &s)
{
    a.beginStructure();
    a >> s.id >> s.data;
    a.endStructure();
    return a;
}

PortalGlobalShortcuts::PortalGlobalShortcuts(QObject *parent)
    : QObject(parent),
      // Private connection: the portal pins app identity per D-Bus connection
      // at its first portal call. On GNOME, Qt's xdgdesktopportal platform
      // theme calls the Settings portal (color scheme) during QGuiApplication
      // construction, so the SHARED session bus is already associated with app
      // id "" for terminal/AppImage launches by the time anything here runs —
      // Registry.Register then fails ("Connection already associated") and
      // gnome-control-center's GlobalShortcutsProvider DISCARDS BindShortcuts
      // requests with an empty app id (Response code 2 → hotkey UI collapses
      // to the compositor-binds card). A dedicated connection whose first call
      // is Registry.Register always carries our real desktop id.
      m_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                          QStringLiteral("unisic-globalshortcuts")))
{
    qDBusRegisterMetaType<PortalShortcutWire>();
    qDBusRegisterMetaType<QList<PortalShortcutWire>>();
    if (!m_bus.isConnected()) {
        qWarning() << "Private bus connection failed — global shortcuts on the shared one"
                      " (GNOME may see an empty app id)";
        m_bus = QDBusConnection::sessionBus();
    }
    registerAppId();
    // Session::Closed is NOT emitted when xdg-desktop-portal crashes — watch
    // the service owner too. Sessions are in-memory frontend objects: after a
    // restart the new daemon would never fire Activated for the old handle
    // (hotkeys silently dead while the app believes they work). Re-create and
    // re-bind; persisted grants make it prompt-free on KDE/GNOME.
    auto *w = new QDBusServiceWatcher(PORTAL_SERVICE, m_bus,
                                      QDBusServiceWatcher::WatchForOwnerChange, this);
    // A crash/restart delivers TWO owner changes: (old, "") then ("", new).
    // The first must only invalidate the session (binding into a dying portal
    // is pointless); the actual re-bind has to wait for the second, when the
    // new daemon is on the bus — hence the m_needRebind carry-over.
    connect(w, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &newOwner) {
        if (newOwner.isEmpty()) {
            // Owner lost: session died with the daemon; flag the re-bind for
            // the owner-gained event that follows. A CreateSession still in
            // flight counts too — the in-flight request fails into a dying
            // portal, so schedule the rebind for the new owner or hotkeys stay
            // permanently dead for the rest of the run.
            if (!m_sessionHandle.isEmpty() || m_sessionPending) {
                m_sessionHandle.clear();
                m_needRebind = true;
            }
            return;
        }
        // Owner gained (restart's second event, or an atomic handover). The
        // identity registry is in-memory portal-side: the new daemon has no
        // association for this connection, so register again BEFORE anything
        // else reaches it (even with no session to restore — the FIRST bind
        // may still be ahead of us) or GNOME drops the bind (empty app id).
        registerAppId();
        if (!m_needRebind && m_sessionHandle.isEmpty())
            return; // never had a session — nothing to restore
        m_needRebind = false;
        m_sessionHandle.clear();
        qWarning() << "xdg-desktop-portal restarted — re-binding global shortcuts";
        if (!m_lastBound.isEmpty())
            bind(m_lastBound);
    });
}

void PortalGlobalShortcuts::registerAppId()
{
    if (!qEnvironmentVariable("FLATPAK_ID").isEmpty())
        return; // sandboxed: identity comes from the sandbox metadata
    // Must be the FIRST portal call on m_bus — the portal resolves and pins
    // the sender's identity at its first portal interaction, and Register on
    // an already-pinned connection fails. In-order dispatch (the Register
    // handler is synchronous portal-side) means the calls that follow can be
    // fired without awaiting this reply. Fails harmlessly on xdg-desktop-portal
    // < 1.20 (no Registry) or when our .desktop file is not installed — the
    // portal then falls back to cgroup detection (works for menu launches).
    QDBusMessage msg = QDBusMessage::createMethodCall(
        PORTAL_SERVICE, PORTAL_PATH,
        QStringLiteral("org.freedesktop.host.portal.Registry"),
        QStringLiteral("Register"));
    msg << QGuiApplication::desktopFileName() << QVariantMap{};
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError())
            qInfo() << "GlobalShortcuts app-id registration:" << w->error().message()
                    << "— GNOME may refuse to bind for terminal launches";
    });
}

void PortalGlobalShortcuts::probeInterface(QObject *ctx, std::function<void(bool)> done)
{
    // Async probe (deliberately no isServiceRegistered pre-check: this call
    // D-Bus-ACTIVATES a not-yet-started portal), awaited
    // asynchronously — the blocking form stalled the GUI thread for however
    // long D-Bus took to spin the portal up at cold session start. Generous
    // timeout: at autostart the activation itself can take several seconds,
    // and a timeout here permanently flips the hotkey UI to "unavailable".
    QDBusMessage msg = QDBusMessage::createMethodCall(
        PORTAL_SERVICE, PORTAL_PATH,
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << GS_IFACE << QStringLiteral("version");
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg, 15000), ctx);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, ctx,
                     [done = std::move(done)](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        done(!w->isError());
    });
}

QString PortalGlobalShortcuts::toPortalTrigger(const QString &portableKeySequence)
{
    // Older builds joined alternate shortcuts with "; ", which QKeySequence
    // parses as ONE Qt::Key_unknown chord — normalize to its native ", " so
    // legacy stored strings still yield the first (primary) trigger.
    QString s = portableKeySequence;
    s.replace(QStringLiteral("; "), QStringLiteral(", "));
    const QKeySequence seq(s, QKeySequence::PortableText);
    if (seq.isEmpty())
        return {};
    const QKeyCombination kc = seq[0];

    // freedesktop Shortcuts spec: modifiers CTRL/ALT/SHIFT/LOGO/NUM (upper),
    // key = xkb keysym name without the XKB_KEY_ prefix; letters lowercase.
    QStringList parts;
    const Qt::KeyboardModifiers mods = kc.keyboardModifiers();
    if (mods & Qt::ControlModifier) parts << QStringLiteral("CTRL");
    if (mods & Qt::AltModifier) parts << QStringLiteral("ALT");
    if (mods & Qt::ShiftModifier) parts << QStringLiteral("SHIFT");
    if (mods & Qt::MetaModifier) parts << QStringLiteral("LOGO");
    if (mods & Qt::KeypadModifier) parts << QStringLiteral("NUM");

    QString key = QKeySequence(kc.key()).toString(QKeySequence::PortableText);
    // Qt name -> xkb keysym name for the divergent ones we can hit. The spec
    // parser only reads [A-Za-z0-9_], so punctuation must become its keysym
    // name and "Space" its lowercase form.
    static const QHash<QString, QString> fixups = {
        {QStringLiteral("Esc"), QStringLiteral("Escape")},
        {QStringLiteral("PgUp"), QStringLiteral("Prior")},
        {QStringLiteral("PgDown"), QStringLiteral("Next")},
        {QStringLiteral("Del"), QStringLiteral("Delete")},
        {QStringLiteral("Ins"), QStringLiteral("Insert")},
        {QStringLiteral("Backtab"), QStringLiteral("Tab")},
        {QStringLiteral("Space"), QStringLiteral("space")},
        {QStringLiteral(","), QStringLiteral("comma")},
        {QStringLiteral("."), QStringLiteral("period")},
        {QStringLiteral(";"), QStringLiteral("semicolon")},
        {QStringLiteral("'"), QStringLiteral("apostrophe")},
        {QStringLiteral("/"), QStringLiteral("slash")},
        {QStringLiteral("\\"), QStringLiteral("backslash")},
        {QStringLiteral("["), QStringLiteral("bracketleft")},
        {QStringLiteral("]"), QStringLiteral("bracketright")},
        {QStringLiteral("-"), QStringLiteral("minus")},
        {QStringLiteral("="), QStringLiteral("equal")},
        {QStringLiteral("`"), QStringLiteral("grave")},
    };
    key = fixups.value(key, key);
    if (key.isEmpty())
        return {}; // unparseable key (e.g. Qt::Key_unknown) — no valid trigger
    if (key.size() == 1)
        key = key.toLower(); // letter keysyms are lowercase in the spec
    parts << key;
    return parts.join(QLatin1Char('+'));
}

void PortalGlobalShortcuts::bind(const QVector<Shortcut> &shortcuts)
{
    m_lastBound = shortcuts;
    if (m_sessionPending) {
        // CreateSession already in flight (quick repeated Apply): remember the
        // newest set; the response handler binds it. A second CreateSession
        // would leak an unclosed session daemon-side.
        m_queued = shortcuts;
        return;
    }
    if (m_bindPending) {
        // A BindShortcuts Response is still in flight (the backend may be
        // showing a consent dialog): stash the newest set and issue ONE
        // trailing BindShortcuts when it arrives — same pattern as the pending
        // CreateSession queue. Concurrent BindShortcuts requests race and their
        // Responses can land out of order, leaving stale trigger text.
        m_queued = shortcuts;
        m_bindQueued = true;
        return;
    }
    if (m_sessionHandle.isEmpty())
        createSession(shortcuts);
    else
        bindNow(shortcuts);
}

void PortalGlobalShortcuts::createSession(const QVector<Shortcut> &shortcuts)
{
    const QString token = PortalRequest::nextToken();
    const QString sessionToken = PortalRequest::nextToken();
    m_sessionPending = true;
    m_queued = shortcuts;

    QDBusMessage msg = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH,
                                                      GS_IFACE, QStringLiteral("CreateSession"));
    msg << QVariantMap{
        {QStringLiteral("handle_token"), token},
        {QStringLiteral("session_handle_token"), sessionToken},
    };
    PortalRequest::send(msg, token, [this](uint code, const QVariantMap &results) {
        m_sessionPending = false;
        const QVector<Shortcut> shortcuts = std::move(m_queued);
        m_queued.clear();
        // Classic trap: the session handle is the "session_handle" STRING in
        // the Response results, not the method's return value.
        const QString handle = results.value(QStringLiteral("session_handle")).toString();
        if (code != 0 || handle.isEmpty()) {
            qWarning() << "GlobalShortcuts portal CreateSession failed (code" << code << ")"
                       << "— no working backend on this desktop";
            emit bindFinished(false, {});
            return;
        }
        m_sessionHandle = handle;

        if (!m_signalConnected) {
            m_signalConnected = m_bus.connect(
                PORTAL_SERVICE, PORTAL_PATH, GS_IFACE, QStringLiteral("Activated"),
                this, SLOT(onActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
            if (!m_signalConnected)
                qWarning() << "Could not subscribe to GlobalShortcuts Activated";
        }
        // A portal restart (crash, package upgrade) closes the session — the
        // hotkeys would silently die while the app still believes they work.
        // Re-create + re-bind transparently (persisted grants make it silent).
        m_bus.connect(
            PORTAL_SERVICE, m_sessionHandle,
            QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Closed"),
            this, SLOT(onSessionClosed()));
        bindNow(shortcuts);
    }, this, 0, m_bus);
}

void PortalGlobalShortcuts::onSessionClosed()
{
    qWarning() << "GlobalShortcuts portal session closed (portal restart?) — re-binding";
    m_bus.disconnect(
        PORTAL_SERVICE, m_sessionHandle,
        QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Closed"),
        this, SLOT(onSessionClosed()));
    m_sessionHandle.clear();
    if (!m_lastBound.isEmpty())
        bind(m_lastBound);
}

void PortalGlobalShortcuts::closeSession(const QString &handle)
{
    if (handle.isEmpty())
        return;
    // Drop our Closed subscription for this handle FIRST: neither the Close()
    // below nor a later backend Closed on this now-abandoned session must run
    // onSessionClosed() against a DIFFERENT (current) session handle.
    m_bus.disconnect(
        PORTAL_SERVICE, handle,
        QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Closed"),
        this, SLOT(onSessionClosed()));
    QDBusMessage msg = QDBusMessage::createMethodCall(
        PORTAL_SERVICE, handle,
        QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Close"));
    m_bus.asyncCall(msg);
}

void PortalGlobalShortcuts::bindNow(const QVector<Shortcut> &shortcuts)
{
    QList<PortalShortcutWire> wire;
    wire.reserve(shortcuts.size());
    for (const Shortcut &s : shortcuts) {
        QVariantMap data{{QStringLiteral("description"), s.description}};
        if (!s.preferredTrigger.isEmpty())
            data.insert(QStringLiteral("preferred_trigger"), s.preferredTrigger);
        wire.append({s.id, data});
    }

    const QString token = PortalRequest::nextToken();
    QDBusMessage msg = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH,
                                                      GS_IFACE, QStringLiteral("BindShortcuts"));
    msg << QVariant::fromValue(QDBusObjectPath(m_sessionHandle))
        << QVariant::fromValue(wire)
        << QString() // parent_window: none (may be triggered from the tray)
        << QVariantMap{{QStringLiteral("handle_token"), token}};

    m_bindPending = true;
    PortalRequest::send(msg, token, [this](uint code, const QVariantMap &results) {
        m_bindPending = false;
        if (m_bindQueued) {
            // A newer set arrived while this bind was in flight — its result
            // supersedes this (now stale) one. Re-issue and let the trailing
            // bind emit bindFinished; swallow this stale Response.
            m_bindQueued = false;
            const QVector<Shortcut> queued = std::move(m_queued);
            m_queued.clear();
            bind(queued);
            return;
        }
        if (code == 1) {
            // User cancelled the consent dialog: the backend WORKS, the set
            // just wasn't confirmed — reporting failure here used to flip the
            // whole UI to "no hotkeys on this desktop" with no way back.
            qWarning() << "GlobalShortcuts bind cancelled by the user";
            emit bindFinished(true, {});
            return;
        }
        if (code != 0) {
            qWarning() << "GlobalShortcuts BindShortcuts failed (code" << code << ")";
            // The session may be stale (portal restarted between binds), or
            // this was a re-bind on GNOME — the spec allows ONE BindShortcuts
            // per session and xdg-desktop-portal-gnome enforces it ("Session
            // already has bound shortcuts", code 2). Either way: retry ONCE
            // through a fresh session before reporting failure — a failure
            // report flips the whole hotkey UI off for the run.
            // Close the abandoned session first so neither it nor its Closed
            // match rule leaks daemon-side (a later Closed on it would else
            // disconnect the CURRENT session and trigger a spurious rebind).
            closeSession(m_sessionHandle);
            m_sessionHandle.clear();
            if (!m_retriedBind && !m_lastBound.isEmpty()) {
                m_retriedBind = true;
                bind(m_lastBound);
                return;
            }
            emit bindFinished(false, {});
            return;
        }
        m_retriedBind = false;
        // Response carries a(sa{sv}) with per-id trigger_description strings.
        QVariantMap triggers;
        const QVariant raw = results.value(QStringLiteral("shortcuts"));
        if (raw.canConvert<QDBusArgument>()) {
            const QDBusArgument arg = raw.value<QDBusArgument>();
            arg.beginArray();
            while (!arg.atEnd()) {
                PortalShortcutWire s;
                arg >> s;
                triggers.insert(s.id, s.data.value(QStringLiteral("trigger_description")));
            }
            arg.endArray();
        }
        emit bindFinished(true, triggers);
    }, this, 0, m_bus);
}

void PortalGlobalShortcuts::onActivated(const QDBusObjectPath &sessionHandle,
                                        const QString &shortcutId, qulonglong,
                                        const QVariantMap &)
{
    if (sessionHandle.path() == m_sessionHandle)
        emit activated(shortcutId);
}
