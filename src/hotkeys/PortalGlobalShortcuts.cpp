#include "PortalGlobalShortcuts.h"
#include "capture/PortalRequest.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusMetaType>
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
    : QObject(parent)
{
    qDBusRegisterMetaType<PortalShortcutWire>();
    qDBusRegisterMetaType<QList<PortalShortcutWire>>();
}

bool PortalGlobalShortcuts::interfacePresent()
{
    // Deliberately NO isServiceRegistered pre-check: at cold session start the
    // portal may not be on the bus yet, and this very call D-Bus-ACTIVATES it
    // (a registered-name check would report a false "absent" and permanently
    // disable hotkeys for the run). The definitive test stays the
    // CreateSession response.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        PORTAL_SERVICE, PORTAL_PATH,
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << GS_IFACE << QStringLiteral("version");
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 3000);
    return reply.type() == QDBusMessage::ReplyMessage;
}

QString PortalGlobalShortcuts::toPortalTrigger(const QString &portableKeySequence)
{
    const QKeySequence seq(portableKeySequence, QKeySequence::PortableText);
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
    if (key.size() == 1)
        key = key.toLower(); // letter keysyms are lowercase in the spec
    parts << key;
    return parts.join(QLatin1Char('+'));
}

void PortalGlobalShortcuts::bind(const QVector<Shortcut> &shortcuts)
{
    if (m_sessionPending) {
        // CreateSession already in flight (quick repeated Apply): remember the
        // newest set; the response handler binds it. A second CreateSession
        // would leak an unclosed session daemon-side.
        m_queued = shortcuts;
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
            m_signalConnected = QDBusConnection::sessionBus().connect(
                PORTAL_SERVICE, PORTAL_PATH, GS_IFACE, QStringLiteral("Activated"),
                this, SLOT(onActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
            if (!m_signalConnected)
                qWarning() << "Could not subscribe to GlobalShortcuts Activated";
        }
        bindNow(shortcuts);
    }, this);
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

    PortalRequest::send(msg, token, [this](uint code, const QVariantMap &results) {
        if (code != 0) {
            qWarning() << "GlobalShortcuts BindShortcuts failed (code" << code << ")";
            emit bindFinished(false, {});
            return;
        }
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
    }, this);
}

void PortalGlobalShortcuts::onActivated(const QDBusObjectPath &sessionHandle,
                                        const QString &shortcutId, qulonglong,
                                        const QVariantMap &)
{
    if (sessionHandle.path() == m_sessionHandle)
        emit activated(shortcutId);
}
