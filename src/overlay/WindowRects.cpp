#include "WindowRects.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QCoreApplication>
#include <QDebug>

// The script reports through callDBus straight back at our unique bus name.
// It is defensive on purpose: any thrown error would otherwise fail silently
// and we would only learn about it via the timeout.
static QString kwinScript(const QString &service, const QString &path)
{
    return QStringLiteral(R"JS(
var rects = [];
try {
    var wins = workspace.stackingOrder;
    for (var i = 0; i < wins.length; ++i) {
        var w = wins[i];
        try {
            if (w.minimized) continue;
            if (!(w.normalWindow || w.dialog || w.dock)) continue;
            var cls = ("" + w.resourceClass).toLowerCase();
            if (cls.indexOf("unisic") >= 0) continue;
            if (w.desktops && w.desktops.length
                && w.desktops.indexOf(workspace.currentDesktop) < 0) continue;
            var g = w.frameGeometry;
            rects.push({x: Math.round(g.x), y: Math.round(g.y),
                        w: Math.round(g.width), h: Math.round(g.height)});
        } catch (e) { /* one odd window must not kill the report */ }
    }
} catch (e) { }
callDBus("%1", "%2", "org.unisic.WindowRects", "pushWindows", JSON.stringify(rects));
)JS").arg(service, path);
}

void WindowRects::query(QObject *context, Callback cb)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    auto *iface = bus.interface();
    if (!iface || !iface->isServiceRegistered(QStringLiteral("org.kde.KWin"))) {
        if (cb)
            cb({});
        return;
    }

    auto *self = new WindowRects();
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    self->m_dbusPath = QStringLiteral("/unisic/windowrects_") + token;
    self->m_pluginName = QStringLiteral("unisic-winrects-") + token;
    QPointer<QObject> guard(context);
    self->m_cb = [cb, guard](const QVector<QRect> &r) {
        if (guard && cb)
            cb(r);
    };
    if (!bus.registerObject(self->m_dbusPath, self,
                            QDBusConnection::ExportScriptableSlots)) {
        qWarning() << "WindowRects: cannot register D-Bus receiver";
        self->finish({});
        return;
    }

    // Script file: KWin loads scripts from disk only.
    const QString file = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                         + QStringLiteral("/unisic-winrects-") + token + QStringLiteral(".js");
    {
        QSaveFile f(file);
        if (!f.open(QIODevice::WriteOnly)) {
            self->finish({});
            return;
        }
        f.write(kwinScript(bus.baseService(), self->m_dbusPath).toUtf8());
        f.commit();
    }

    // Give up quietly when the compositor never answers (kwin busy, scripting
    // disabled) — smart pick then simply runs without window priors.
    auto *timeout = new QTimer(self);
    timeout->setSingleShot(true);
    timeout->setInterval(500);
    QObject::connect(timeout, &QTimer::timeout, self, [self, file] {
        QFile::remove(file);
        self->finish({});
    });
    timeout->start();

    QDBusMessage load = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("loadScript"));
    load << file << self->m_pluginName;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(load), self);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, self,
                     [self, file](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<int> reply = *w;
        if (reply.isError()) {
            qWarning() << "WindowRects: loadScript failed:" << reply.error().message();
            QFile::remove(file);
            self->finish({});
            return;
        }
        const int id = reply.value();
        // Plasma 6 exposes the loaded script at /Scripting/Script<id>; run()
        // executes it, and the script reports back via pushWindows.
        QDBusMessage run = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/Scripting/Script%1").arg(id),
            QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run"));
        QDBusConnection::sessionBus().asyncCall(run);
        // The temp file is only needed until run() parsed it; sweep a little
        // later so slow compositors still find it.
        QTimer::singleShot(5000, qApp, [file] { QFile::remove(file); });
    });
}

void WindowRects::pushWindows(const QString &json)
{
    QVector<QRect> rects;
    const QJsonArray arr = QJsonDocument::fromJson(json.toUtf8()).array();
    rects.reserve(arr.size());
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        const QRect r(o.value(QStringLiteral("x")).toInt(),
                      o.value(QStringLiteral("y")).toInt(),
                      o.value(QStringLiteral("w")).toInt(),
                      o.value(QStringLiteral("h")).toInt());
        if (r.width() >= 8 && r.height() >= 8)
            rects.append(r);
    }
    finish(rects);
}

void WindowRects::finish(const QVector<QRect> &rects)
{
    if (m_done)
        return;
    m_done = true;
    // Unload the one-shot script; harmless if load never succeeded.
    QDBusMessage unload = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("unloadScript"));
    unload << m_pluginName;
    QDBusConnection::sessionBus().asyncCall(unload);
    QDBusConnection::sessionBus().unregisterObject(m_dbusPath);
    if (m_cb)
        m_cb(rects);
    deleteLater();
}
