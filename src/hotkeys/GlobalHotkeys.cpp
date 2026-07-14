#include "GlobalHotkeys.h"
#include "ShortcutFormat.h"
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
    // availability must be gated on the session actually being KDE.
    //
    // Detect KWin by its D-Bus NAME, not only by XDG_CURRENT_DESKTOP: that env
    // var is absent in legitimate launch contexts (systemd user-service
    // autostart — exactly how a tray app usually starts — minimal-env
    // launchers), and gating on it alone would silently route hotkeys to the
    // portal (or nowhere) on a real KDE session. org.kde.KWin on the bus is
    // the reliable, env-independent signal.
    // Dev aid: UNISIC_HOTKEY_BACKEND=portal forces the non-KDE path so the
    // GlobalShortcuts portal flow can be exercised on a KDE box.
    if (qEnvironmentVariable("UNISIC_HOTKEY_BACKEND") == QLatin1String("portal")) {
        qInfo() << "UNISIC_HOTKEY_BACKEND=portal — KGlobalAccel skipped";
        return;
    }
    auto *iface = QDBusConnection::sessionBus().interface();
    const bool kwinOnBus = iface && iface->isServiceRegistered(QStringLiteral("org.kde.KWin"));
    const QStringList desktops =
        qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    const bool kdeEnv = desktops.contains(QLatin1String("KDE"), Qt::CaseInsensitive)
                        || desktops.contains(QLatin1String("plasma"), Qt::CaseInsensitive);
    if (!kwinOnBus && !kdeEnv) {
        qInfo() << "Not a KDE session — KGlobalAccel skipped (portal/compositor binds instead)";
        return;
    }
    m_available = iface && (iface->isServiceRegistered(KGA_SERVICE)
                            || iface->startService(KGA_SERVICE).isValid());
    if (!m_available)
        qWarning() << "KGlobalAccel not available — global hotkeys disabled";
}

QList<int> GlobalHotkeys::expandShiftDigitVariants(const QList<int> &keys)
{
    // KWin on Wayland reports a pressed Shift+digit with the shift CONSUMED
    // by the symbol: physical Meta+Shift+1 reaches the daemon lookup as
    // Meta+! (and some paths as Meta+Shift+!). A binding stored only as
    // Meta+Shift+1 therefore never fires. Bind all three encodings as
    // alternates of one action — whichever the compositor computes, the
    // daemon finds the action. portableFromKeys() collapses them back so
    // the UI keeps showing just "Meta+Shift+1".
    QList<int> out = keys;
    for (int k : keys) {
        if (!(k & Qt::ShiftModifier))
            continue;
        const int mods = k & int(Qt::KeyboardModifierMask);
        const int sym = ShortcutFormat::shiftedSymbolFor(k & ~int(Qt::KeyboardModifierMask));
        if (!sym)
            continue;
        const int withShift = mods | sym;
        const int noShift = (mods & ~int(Qt::ShiftModifier)) | sym;
        if (!out.contains(withShift))
            out.append(withShift);
        if (!out.contains(noShift))
            out.append(noShift);
    }
    return out;
}

QString GlobalHotkeys::portableFromKeys(const QList<int> &keys)
{
    // Keep ALL sequences ("Meta+Shift+1, Print"), not just the first: the
    // daemon-authoritative sync feeds this back into setShortcut on the next
    // apply, and a single-key round-trip would silently delete a user's
    // alternate KCM binding. The separator MUST be ", " — QKeySequence's only
    // multi-sequence separator; "; " parses as ONE Qt::Key_unknown chord that
    // setShortcut would then push over the user's real bindings.
    //
    // Auto-generated Shift+digit variants (see expandShiftDigitVariants) are
    // collapsed back into their digit form so the UI never shows the
    // "Meta+Shift+1, Meta+Shift+!, Meta+!" internals.
    QStringList parts;
    for (int k : keys) {
        if (k == 0)
            continue;
        const int base = ShortcutFormat::baseForShiftedSymbol(k & ~int(Qt::KeyboardModifierMask));
        if (base) {
            const int digitForm = (k & int(Qt::KeyboardModifierMask))
                                  | int(Qt::ShiftModifier) | base;
            if (keys.contains(digitForm))
                continue; // derived variant of a digit binding also present
        }
        parts << QKeySequence(QKeyCombination::fromCombined(k))
                     .toString(QKeySequence::PortableText);
    }
    return parts.join(QStringLiteral(", "));
}

bool GlobalHotkeys::sameBinding(const QString &a, const QString &b)
{
    if (a == b)
        return true;
    const QList<int> ka = keysFor(a), kb = keysFor(b);
    return QSet<int>(ka.begin(), ka.end()) == QSet<int>(kb.begin(), kb.end());
}

QStringList GlobalHotkeys::fullActionId(const QString &actionId, const QString &friendlyName) const
{
    // KGlobalAccel canonical id: [componentUnique, actionUnique, componentFriendly, actionFriendly]
    return {QString::fromLatin1(COMPONENT), actionId, QString::fromLatin1(COMPONENT_FRIENDLY),
            friendlyName};
}

QList<int> GlobalHotkeys::keysFor(const QString &keySequence)
{
    // Older builds stored multi-sequence strings joined with "; " — normalize
    // to QKeySequence's native ", " so legacy configs keep both bindings.
    QString s = keySequence;
    s.replace(QStringLiteral("; "), QStringLiteral(", "));
    QList<int> keys;
    const QKeySequence seq(s);
    for (int i = 0; i < seq.count(); ++i) {
        // Never forward an unparseable chord: SetPresent|NoAutoloading makes
        // the daemon persist whatever it is handed, so a Qt::Key_unknown here
        // would replace the user's real bindings with a dead key.
        const int k = int(seq[i].toCombined());
        if (k != 0 && k != int(Qt::Key_unknown))
            keys.append(k);
    }
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
                                 const QString &defaultKeySequence)
{
    if (!m_available)
        return;

    const QStringList id = fullActionId(actionId, friendlyName);

    QDBusMessage reg = QDBusMessage::createMethodCall(KGA_SERVICE, KGA_PATH, KGA_IFACE,
                                                      QStringLiteral("doRegister"));
    reg << id;
    QDBusConnection::sessionBus().call(reg, QDBus::Block, 800);
    m_registered.insert(actionId);

    // KGlobalAccel SetShortcutFlag: SetPresent=2, NoAutoloading=4, IsDefault=8.
    // We register with IsDefault (8) + autoloading (no NoAutoloading):
    //   * fresh install (no stored key): the daemon binds defaultKeySequence;
    //   * a key the user changed in KDE's Shortcuts KCM is autoloaded and is NOT
    //     clobbered on subsequent launches (only the default column is touched).
    // Adding a separate SetPresent(2) call is deliberately avoided: on this
    // daemon it *clears* the binding (persists nothing / wipes a stored key).
    // The reply is intentionally ignored: kglobalacceld echoes the requested
    // keys even when it filled only the default column and left the ACTIVE
    // binding "none" (observed live — e.g. after a historical wipe). Callers
    // must verify with activeKeys() and repair via setShortcut() (SetPresent),
    // which the daemon does honor — see AppContext::defineHotkeys.
    const QList<int> defKeys = keysFor(defaultKeySequence);
    QDBusMessage set = QDBusMessage::createMethodCall(KGA_SERVICE, KGA_PATH, KGA_IFACE,
                                                      QStringLiteral("setShortcut"));
    set << id << QVariant::fromValue(defKeys) << uint(0x8);
    QDBusMessage reply = QDBusConnection::sessionBus().call(set, QDBus::Block, 800);
    if (reply.type() == QDBusMessage::ErrorMessage)
        qWarning() << "defineAction setShortcut failed for" << actionId << reply.errorMessage();

    ensureSignalConnected();
}

void GlobalHotkeys::releaseShortcut(const QString &actionId, const QString &friendlyName)
{
    if (!m_available)
        return;
    // Fire-and-forget async unbind (used by the startup legacy quick-copy clear
    // and the alt-hotkey test): the caller never inspects the result, so two
    // blocking round-trips (2 s timeout each) on the GUI thread bought nothing.
    // D-Bus messages on one connection stay ordered, so doRegister lands first.
    const QStringList id = fullActionId(actionId, friendlyName);
    QDBusMessage reg = QDBusMessage::createMethodCall(KGA_SERVICE, KGA_PATH, KGA_IFACE,
                                                      QStringLiteral("doRegister"));
    reg << id;
    QDBusConnection::sessionBus().asyncCall(reg);
    QDBusMessage set = QDBusMessage::createMethodCall(KGA_SERVICE, KGA_PATH, KGA_IFACE,
                                                      QStringLiteral("setShortcut"));
    set << id << QVariant::fromValue(QList<int>()) << uint(0x2 | 0x4);
    QDBusConnection::sessionBus().asyncCall(set);
    ensureSignalConnected();
}

bool GlobalHotkeys::setShortcut(const QString &actionId, const QString &friendlyName,
                                const QString &keySequence)
{
    if (!m_available)
        return false;
    const QStringList id = fullActionId(actionId, friendlyName);

    // doRegister is idempotent and per-process registration only needs to
    // happen once per action — skip the extra blocking round-trip on repeats.
    if (!m_registered.contains(actionId)) {
        QDBusMessage reg = QDBusMessage::createMethodCall(KGA_SERVICE, KGA_PATH, KGA_IFACE,
                                                          QStringLiteral("doRegister"));
        reg << id;
        QDBusConnection::sessionBus().call(reg, QDBus::Block, 800);
        m_registered.insert(actionId);
    }

    // SetShortcutFlag: SetPresent=2, NoAutoloading=4, IsDefault=8.
    // MUST be SetPresent|NoAutoloading (0x6). Verified live against kglobalacceld
    // (Plasma 6): with SetPresent alone (0x2) and autoloading left ON, an action
    // that already has a stored binding IGNORES the new keys — setShortcut returns
    // the OLD key. Only NoAutoloading forces the daemon to take the passed keys and
    // persist them to kglobalshortcutsrc. This was the "hotkeys set from the app UI
    // don't work, only KDE-set ones do" bug: autoloading kept clobbering our value.
    const QList<int> wanted = expandShiftDigitVariants(keysFor(keySequence));
    QDBusMessage set = QDBusMessage::createMethodCall(KGA_SERVICE, KGA_PATH, KGA_IFACE,
                                                      QStringLiteral("setShortcut"));
    set << id << QVariant::fromValue(wanted) << uint(0x2 | 0x4);
    QDBusMessage reply = QDBusConnection::sessionBus().call(set, QDBus::Block, 800);
    ensureSignalConnected();

    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "setShortcut failed for" << actionId << reply.errorMessage();
        return false;
    }
    // The daemon answers with the keys actually in effect: when another
    // component owns the requested combination it keeps/returns something
    // else — surface that instead of pretending the bind worked. Compare the
    // COLLAPSED portable forms: the daemon may legitimately grant only part
    // of the auto-generated variant set.
    if (!reply.arguments().isEmpty()) {
        const QList<int> actual = qdbus_cast<QList<int>>(reply.arguments().first());
        // ORDER-INSENSITIVE: the daemon reorders alternate keys in replies.
        if (QSet<int>(actual.begin(), actual.end())
            != QSet<int>(wanted.begin(), wanted.end())) {
            qWarning() << "setShortcut for" << actionId << "requested" << wanted
                       << "but daemon kept" << actual << "(key owned elsewhere?)";
            return false;
        }
    }
    return true;
}

QString GlobalHotkeys::keyOwner(int key) const
{
    if (!m_available)
        return {};
    QDBusMessage msg = QDBusMessage::createMethodCall(KGA_SERVICE, KGA_PATH, KGA_IFACE,
                                                      QStringLiteral("action"));
    msg << key;
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 800);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    const QStringList id = reply.arguments().first().toStringList();
    if (id.size() < 2 || id.at(0).isEmpty())
        return {};
    return id.at(0) + QLatin1Char('/') + id.at(1);
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
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 800);
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

void GlobalHotkeys::cleanUpComponent(const QString &componentUnique)
{
    if (!m_available || componentUnique.isEmpty())
        return;
    // KGlobalAccel maps a component's unique name to its D-Bus object path by
    // replacing every character outside [A-Za-z0-9_] with '_' (and prefixing an
    // underscore if it starts with a digit). e.g. app.unisic.UnisicDev →
    // /component/app_unisic_UnisicDev. cleanUp on a component that does not exist
    // is a harmless no-op, so this is safe to run every launch on both builds.
    QString path;
    path.reserve(componentUnique.size());
    for (const QChar ch : componentUnique) {
        const char c = ch.toLatin1();
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                        || (c >= '0' && c <= '9') || c == '_';
        path.append(ok ? ch : QLatin1Char('_'));
    }
    if (!path.isEmpty() && path.at(0).isDigit())
        path.prepend(QLatin1Char('_'));
    QDBusMessage msg = QDBusMessage::createMethodCall(
        KGA_SERVICE, QStringLiteral("/component/") + path,
        QStringLiteral("org.kde.kglobalaccel.Component"), QStringLiteral("cleanUp"));
    QDBusConnection::sessionBus().asyncCall(msg);
}

void GlobalHotkeys::unregisterAction(const QString &actionId)
{
    if (!m_available)
        return;
    // Fully drop a single action from the daemon (and thus the Shortcuts KCM).
    // releaseShortcut() only unbinds the keys, which — with NoAutoloading —
    // leaves the action PERSISTED as an unbound row; a scratch/test action must
    // not linger there.
    QDBusMessage msg = QDBusMessage::createMethodCall(KGA_SERVICE, KGA_PATH, KGA_IFACE,
                                                      QStringLiteral("unRegister"));
    msg << QStringList{QString::fromLatin1(COMPONENT), actionId};
    QDBusConnection::sessionBus().asyncCall(msg);
}

void GlobalHotkeys::onShortcutPressed(const QString &componentUnique, const QString &actionUnique,
                                      qlonglong)
{
    if (componentUnique == QLatin1String(COMPONENT))
        emit activated(actionUnique);
}
