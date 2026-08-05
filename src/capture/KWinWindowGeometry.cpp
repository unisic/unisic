#include "KWinWindowGeometry.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QTemporaryFile>
#include <QTimer>

namespace {
constexpr int kQueryTimeoutMs = 2000;

QDBusMessage scriptingCall(const QString &path, const QString &iface, const QString &method)
{
    return QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), path, iface, method);
}
}

KWinWindowGeometry::~KWinWindowGeometry()
{
    cleanup();
}

bool KWinWindowGeometry::isAvailable()
{
    // Inside a sandbox the compositor's bus name is not reachable (and the
    // script would run outside our filesystem view anyway).
    if (qEnvironmentVariableIsSet("FLATPAK_ID"))
        return false;
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(QStringLiteral("org.kde.KWin"));
}

void KWinWindowGeometry::queryActiveWindow(Callback cb)
{
    if (!cb)
        return;
    if (m_cb) {
        cb({}, QStringLiteral("a window query is already running"));
        return;
    }
    if (!isAvailable()) {
        cb({}, QStringLiteral("KWin is not on the session bus"));
        return;
    }
    m_cb = std::move(cb);

    const qint64 pid = QCoreApplication::applicationPid();
    m_serviceName = QStringLiteral("app.unisic.kwingeom_%1").arg(pid);
    m_objectPath = QStringLiteral("/kwingeom");
    m_pluginName = QStringLiteral("unisic-active-window-%1").arg(pid);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(m_serviceName)
        || !bus.registerObject(m_objectPath, this, QDBusConnection::ExportAllSlots)) {
        finish({}, QStringLiteral("could not register the reply endpoint on D-Bus"));
        return;
    }

    // The script has to exist as a file KWin can read; it is unloaded and
    // deleted again as soon as the answer (or the timeout) lands.
    m_script = new QTemporaryFile(QDir::tempPath() + QStringLiteral("/unisic-geom-XXXXXX.js"), this);
    if (!m_script->open()) {
        finish({}, QStringLiteral("could not write the KWin helper script"));
        return;
    }
    const QString js = QStringLiteral(
        "const w = workspace.activeWindow || workspace.activeClient;\n"
        "const g = w ? w.frameGeometry : null;\n"
        "callDBus(\"%1\", \"%2\", \"app.unisic.KWinWindowGeometry\", \"report\",\n"
        "         g ? Math.round(g.x) : 0, g ? Math.round(g.y) : 0,\n"
        "         g ? Math.round(g.width) : 0, g ? Math.round(g.height) : 0);\n")
                           .arg(m_serviceName, m_objectPath);
    m_script->write(js.toUtf8());
    m_script->flush();

    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(kQueryTimeoutMs);
    connect(m_timeout, &QTimer::timeout, this,
            [this] { finish({}, QStringLiteral("KWin did not report a window in time")); });
    m_timeout->start();

    QDBusMessage load = scriptingCall(QStringLiteral("/Scripting"),
                                      QStringLiteral("org.kde.kwin.Scripting"),
                                      QStringLiteral("loadScript"));
    load << m_script->fileName() << m_pluginName;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(load, kQueryTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                const QDBusPendingReply<int> reply = *w;
                if (reply.isError()) {
                    finish({}, QStringLiteral("KWin refused the helper script: %1")
                                   .arg(reply.error().message()));
                    return;
                }
                // Plasma 6 object path for a loaded script. The run() call is
                // fire-and-forget: the script's own callDBus is the reply.
                QDBusConnection::sessionBus().asyncCall(
                    scriptingCall(QStringLiteral("/Scripting/Script%1").arg(reply.value()),
                                  QStringLiteral("org.kde.kwin.Script"),
                                  QStringLiteral("run")),
                    kQueryTimeoutMs);
            });
}

void KWinWindowGeometry::report(int x, int y, int w, int h)
{
    if (!m_cb)
        return;
    if (w <= 0 || h <= 0) {
        finish({}, QStringLiteral("no active window"));
        return;
    }
    finish(QRect(x, y, w, h), QString());
}

void KWinWindowGeometry::finish(const QRect &rect, const QString &error)
{
    Callback cb = std::move(m_cb);
    m_cb = nullptr;
    if (m_timeout)
        m_timeout->stop();
    // Deferred: report() runs inside the D-Bus dispatch for the script's own
    // call, and tearing the exported object down underneath it would drop the
    // reply KWin is still waiting for. The guard keeps a query started from
    // inside the callback from being cleaned up before it began.
    QMetaObject::invokeMethod(this, [this] {
        if (!m_cb)
            cleanup();
    }, Qt::QueuedConnection);
    if (cb)
        cb(rect, error);
}

void KWinWindowGeometry::cleanup()
{
    if (m_timeout) {
        m_timeout->stop();
        m_timeout->deleteLater();
        m_timeout = nullptr;
    }
    if (!m_pluginName.isEmpty()) {
        QDBusMessage unload = scriptingCall(QStringLiteral("/Scripting"),
                                            QStringLiteral("org.kde.kwin.Scripting"),
                                            QStringLiteral("unloadScript"));
        unload << m_pluginName;
        QDBusConnection::sessionBus().asyncCall(unload, kQueryTimeoutMs);
        m_pluginName.clear();
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!m_objectPath.isEmpty()) {
        bus.unregisterObject(m_objectPath);
        m_objectPath.clear();
    }
    if (!m_serviceName.isEmpty()) {
        bus.unregisterService(m_serviceName);
        m_serviceName.clear();
    }
    if (m_script) {
        m_script->deleteLater();
        m_script = nullptr;
    }
}
