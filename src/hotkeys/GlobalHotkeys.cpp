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
    // KGlobalAccel can only grab keys under KWin. D-Bus-activating it on other
    // desktops — which happens whenever KDE libraries are installed — yields a
    // daemon that registers everything and never fires a single shortcut, so
    // availability must be gated on the session actually being KDE, not on
    // the service being activatable.
    const QStringList desktops =
        qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    const bool kde = desktops.contains(QLatin1String("KDE"), Qt::CaseInsensitive)
                     || desktops.contains(QLatin1String("plasma"), Qt::CaseInsensitive);
    if (!kde) {
        qInfo() << "Not a KDE session — KGlobalAccel skipped (portal/compositor binds instead)";
        return;
    }
    auto *iface = QDBusConnection::sessionBus().interface();
    m_available = iface && (iface->isServiceRegistered(KGA_SERVICE)
                            || iface->startService(KGA_SERVICE).isValid());
    if (!m_available)
        qWarning() << "KGlobalAccel not available — global hotkeys disabled";
}

QString GlobalHotkeys::portableFromKeys(const QList<int> &keys)
{
    // Keep ALL sequences ("Meta+Shift+1; Print"), not just the first: the
    // daemon-authoritative sync feeds this back into setShortcut on the next
    // apply, and a single-key round-trip would silently delete a user's
    // alternate KCM binding. QKeySequence parses/serializes multi-sequence
    // strings natively, and keysFor() iterates every sequence.
    QStringList parts;
    for (int k : keys)
        if (k != 0)
            parts << QKeySequence(QKeyCombination::fromCombined(k))
                         .toString(QKeySequence::PortableText);
    return parts.join(QStringLiteral("; "));
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
    // Live sync: the daemon announces binding changes for our actions (e.g.
    // edits in the System Settings Shortcuts KCM) — keep the app's display
    // truthful instead of showing stale stored strings. The daemon exposes
    // BOTH signal generations with different key encodings; subscribe to both
    // (a duplicate delivery is de-duplicated by the settings setter's guard):
    //   yourShortcutGotChanged  (as ai)    — flat key ints
    //   yourShortcutsChanged    (as a(ai)) — list of packed key sequences
    QDBusConnection::sessionBus().connect(
        KGA_SERVICE, KGA_PATH, KGA_IFACE, QStringLiteral("yourShortcutGotChanged"),
        this, SLOT(onYourShortcutsChanged(QStringList, QList<int>)));
    QDBusConnection::sessionBus().connect(
        KGA_SERVICE, KGA_PATH, KGA_IFACE, QStringLiteral("yourShortcutsChanged"),
        this, SLOT(onYourShortcutsListChanged(QDBusMessage)));
}

void GlobalHotkeys::onYourShortcutsChanged(const QStringList &actionId, const QList<int> &newKeys)
{
    if (actionId.size() >= 2 && actionId.at(0) == QLatin1String(COMPONENT))
        emit shortcutChanged(actionId.at(1), portableFromKeys(newKeys));
}

void GlobalHotkeys::onYourShortcutsListChanged(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (args.size() < 2)
        return;
    const QStringList actionId = args.at(0).toStringList();
    if (actionId.size() < 2 || actionId.at(0) != QLatin1String(COMPONENT))
        return;
    QList<int> flat;
    const QDBusArgument arg = args.at(1).value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
        arg.beginStructure();
        QList<int> seq;
        arg >> seq;
        arg.endStructure();
        for (int k : std::as_const(seq))
            if (k != 0)
                flat.append(k);
    }
    arg.endArray();
    emit shortcutChanged(actionId.at(1), portableFromKeys(flat));
}

void GlobalHotkeys::defineAction(const QString &actionId, const QString &friendlyName,
                                 const QString &defaultKeySequence,
                                 QList<int> *activeKeysOut, bool *ok)
{
    if (ok)
        *ok = false;
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
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "defineAction setShortcut failed for" << actionId << reply.errorMessage();
    } else if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        if (ok)
            *ok = true;
        if (activeKeysOut)
            *activeKeysOut = qdbus_cast<QList<int>>(reply.arguments().first());
    }

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

QList<int> GlobalHotkeys::activeKeys(const QString &actionId, bool *ok) const
{
    if (ok)
        *ok = false;
    QList<int> keys;
    if (!m_available)
        return keys;
    QDBusMessage msg = QDBusMessage::createMethodCall(KGA_SERVICE, KGA_PATH, KGA_IFACE,
                                                      QStringLiteral("shortcutKeys"));
    // shortcutKeys matches on [componentUnique, actionUnique] — friendly names
    // are ignored for the lookup.
    msg << QStringList{QString::fromLatin1(COMPONENT), actionId, QString(), QString()};
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return keys;
    if (ok)
        *ok = true;
    // Reply is a(ai): a list of key sequences, each packed as 4 ints
    // (QKeySequence slots); zeros are empty slots.
    const QDBusArgument arg = reply.arguments().first().value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
        arg.beginStructure();
        QList<int> seq;
        arg >> seq;
        arg.endStructure();
        for (int k : std::as_const(seq))
            if (k != 0)
                keys.append(k);
    }
    arg.endArray();
    return keys;
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
