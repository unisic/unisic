#include "GlobalHotkeys.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusArgument>
#include <QKeySequence>
#include <QCoreApplication>
#include <QDebug>

static const auto KGA_SERVICE = QStringLiteral("org.kde.kglobalaccel");
static const auto KGA_PATH = QStringLiteral("/kglobalaccel");
static const auto KGA_IFACE = QStringLiteral("org.kde.KGlobalAccel");

GlobalHotkeys::GlobalHotkeys(QObject *parent) : QObject(parent)
{
    qDBusRegisterMetaType<QList<int>>();
    auto *iface = QDBusConnection::sessionBus().interface();
    m_available = iface && (iface->isServiceRegistered(KGA_SERVICE)
                            || iface->startService(KGA_SERVICE).isValid());
    if (!m_available)
        qWarning() << "KGlobalAccel not available — global hotkeys disabled";
}

QStringList GlobalHotkeys::fullActionId(const QString &actionId, const QString &friendlyName) const
{
    // KGlobalAccel canonical id: [componentUnique, actionUnique, componentFriendly, actionFriendly]
    return {QString::fromLatin1(COMPONENT), actionId, QStringLiteral("Unisic"), friendlyName};
}

QList<int> GlobalHotkeys::keysFor(const QString &keySequence) const
{
    QList<int> keys;
    const QKeySequence seq(keySequence);
    for (int i = 0; i < seq.count(); ++i)
        keys.append(int(seq[i].toCombined()));
    return keys;
}

void GlobalHotkeys::ensureSignalConnected()
{
    if (m_signalConnected)
        return;
    m_signalConnected = QDBusConnection::sessionBus().connect(
        KGA_SERVICE,
        QStringLiteral("/component/") + QString::fromLatin1(COMPONENT),
        QStringLiteral("org.kde.kglobalaccel.Component"),
        QStringLiteral("globalShortcutPressed"),
        this, SLOT(onShortcutPressed(QString, QString, qlonglong)));
    if (!m_signalConnected)
        qWarning() << "Could not connect to KGlobalAccel component signal";
}

void GlobalHotkeys::defineAction(const QString &actionId, const QString &friendlyName,
                                 const QString &defaultKeySequence)
{
    if (!m_available)
        return;

    const QStringList id = fullActionId(actionId, friendlyName);

    QDBusMessage reg = QDBusMessage::createMethodCall(KGA_SERVICE, KGA_PATH, KGA_IFACE,
                                                      QStringLiteral("doRegister"));
    reg << id;
    QDBusConnection::sessionBus().call(reg, QDBus::Block, 2000);

    // KGlobalAccel SetShortcutFlag: SetPresent=2, NoAutoloading=4, IsDefault=8.
    // We register with IsDefault (8) + autoloading (no NoAutoloading). Verified
    // behavior on Plasma 6 (kglobalacceld) for this exact call:
    //   * fresh install (no stored key): both the active AND default columns are
    //     set to defaultKeySequence, so the hotkey fires immediately;
    //   * a key the user changed in KDE's Shortcuts KCM is autoloaded and is NOT
    //     clobbered on subsequent launches (only the default column is touched).
    // Adding a separate SetPresent(2) call is deliberately avoided: on this
    // daemon it *clears* the binding (persists nothing / wipes a stored key).
    // A user-initiated change from Unisic's own settings is pushed later via
    // setShortcut() (SetPresent), which the daemon does honor.
    const QList<int> defKeys = keysFor(defaultKeySequence);
    QDBusMessage set = QDBusMessage::createMethodCall(KGA_SERVICE, KGA_PATH, KGA_IFACE,
                                                      QStringLiteral("setShortcut"));
    set << id << QVariant::fromValue(defKeys) << uint(0x8);
    QDBusMessage reply = QDBusConnection::sessionBus().call(set, QDBus::Block, 2000);
    if (reply.type() == QDBusMessage::ErrorMessage)
        qWarning() << "defineAction setShortcut failed for" << actionId << reply.errorMessage();

    ensureSignalConnected();
}

bool GlobalHotkeys::setShortcut(const QString &actionId, const QString &friendlyName,
                                const QString &keySequence)
{
    if (!m_available)
        return false;
    const QStringList id = fullActionId(actionId, friendlyName);

    QDBusMessage reg = QDBusMessage::createMethodCall(KGA_SERVICE, KGA_PATH, KGA_IFACE,
                                                      QStringLiteral("doRegister"));
    reg << id;
    QDBusConnection::sessionBus().call(reg, QDBus::Block, 2000);

    // SetShortcutFlag: SetPresent=2, NoAutoloading=4, IsDefault=8.
    // MUST be SetPresent|NoAutoloading (0x6). Verified live against kglobalacceld
    // (Plasma 6): with SetPresent alone (0x2) and autoloading left ON, an action
    // that already has a stored binding IGNORES the new keys — setShortcut returns
    // the OLD key. Only NoAutoloading forces the daemon to take the passed keys and
    // persist them to kglobalshortcutsrc. This was the "hotkeys set from the app UI
    // don't work, only KDE-set ones do" bug: autoloading kept clobbering our value.
    const QList<int> wanted = keysFor(keySequence);
    QDBusMessage set = QDBusMessage::createMethodCall(KGA_SERVICE, KGA_PATH, KGA_IFACE,
                                                      QStringLiteral("setShortcut"));
    set << id << QVariant::fromValue(wanted) << uint(0x2 | 0x4);
    QDBusMessage reply = QDBusConnection::sessionBus().call(set, QDBus::Block, 2000);
    ensureSignalConnected();

    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "setShortcut failed for" << actionId << reply.errorMessage();
        return false;
    }
    // The daemon answers with the keys actually in effect: when another
    // component owns the requested combination it keeps/returns something
    // else — surface that instead of pretending the bind worked.
    if (!reply.arguments().isEmpty()) {
        const QList<int> actual = qdbus_cast<QList<int>>(reply.arguments().first());
        if (actual != wanted) {
            qWarning() << "setShortcut for" << actionId << "requested" << wanted
                       << "but daemon kept" << actual << "(key owned elsewhere?)";
            return false;
        }
    }
    return true;
}

void GlobalHotkeys::unregisterAll()
{
    if (!m_available)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        KGA_SERVICE, QStringLiteral("/component/") + QString::fromLatin1(COMPONENT),
        QStringLiteral("org.kde.kglobalaccel.Component"), QStringLiteral("cleanUp"));
    QDBusConnection::sessionBus().asyncCall(msg);
}

void GlobalHotkeys::onShortcutPressed(const QString &componentUnique, const QString &actionUnique,
                                      qlonglong)
{
    if (componentUnique == QLatin1String(COMPONENT))
        emit activated(actionUnique);
}
