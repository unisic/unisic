#include "WindowRects.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QCoreApplication>
#include <QDebug>

// ------------------------------------------------------------------- KWin

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

void WindowRects::startKWin()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    m_dbusPath = QStringLiteral("/unisic/windowrects_") + token;
    m_pluginName = QStringLiteral("unisic-winrects-") + token;
    if (!bus.registerObject(m_dbusPath, this, QDBusConnection::ExportScriptableSlots)) {
        qWarning() << "WindowRects: cannot register D-Bus receiver";
        finish({});
        return;
    }

    // Script file: KWin loads scripts from disk only.
    const QString file = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                         + QStringLiteral("/unisic-winrects-") + token + QStringLiteral(".js");
    {
        QSaveFile f(file);
        if (!f.open(QIODevice::WriteOnly)) {
            finish({});
            return;
        }
        f.write(kwinScript(bus.baseService(), m_dbusPath).toUtf8());
        f.commit();
    }

    QDBusMessage load = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("loadScript"));
    load << file << m_pluginName;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(load), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, file](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<int> reply = *w;
        if (reply.isError()) {
            qWarning() << "WindowRects: loadScript failed:" << reply.error().message();
            QFile::remove(file);
            finish({});
            return;
        }
        // Plasma 6 exposes the loaded script at /Scripting/Script<id>; run()
        // executes it, and the script reports back via pushWindows.
        QDBusMessage run = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/Scripting/Script%1").arg(reply.value()),
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

// ------------------------------------------------------------------ GNOME

void WindowRects::startGnome()
{
    // org.gnome.Shell.Introspect.GetWindows — the API the GNOME portal itself
    // uses. Stock GNOME restricts it to allowlisted callers unless
    // "unsafe mode" is on, so a denial here is EXPECTED and just falls back.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.gnome.Shell"),
        QStringLiteral("/org/gnome/Shell/Introspect"),
        QStringLiteral("org.gnome.Shell.Introspect"), QStringLiteral("GetWindows"));
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError()) {
            finish({});
            return;
        }
        QVector<QRect> rects;
        const QVariantMap windows = reply.value();
        for (auto it = windows.cbegin(); it != windows.cend(); ++it) {
            const QVariantMap props = qdbus_cast<QVariantMap>(it.value());
            if (props.value(QStringLiteral("is-hidden")).toBool())
                continue;
            const QVariantList fr = qdbus_cast<QVariantList>(
                props.value(QStringLiteral("frame-rect")));
            if (fr.size() == 4) {
                const QRect r(fr[0].toInt(), fr[1].toInt(), fr[2].toInt(), fr[3].toInt());
                if (r.width() >= 8 && r.height() >= 8)
                    rects.append(r);
            }
        }
        finish(rects);
    });
}

// -------------------------------------------------- process-JSON backends

void WindowRects::startProcess(const QString &program, const QStringList &args,
                               std::function<QVector<QRect>(const QByteArray &)> parse)
{
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this,
            [this, proc, parse](int code, QProcess::ExitStatus st) {
        const QByteArray out = proc->readAllStandardOutput();
        proc->deleteLater();
        if (code != 0 || st != QProcess::NormalExit) {
            finish({});
            return;
        }
        finish(parse(out));
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc] {
        proc->deleteLater();
        finish({});
    });
    proc->start(program, args);
}

// sway: walk the layout tree; visible con/floating_con nodes carry their
// frame in "rect" (global logical coordinates).
static QVector<QRect> parseSwayTree(const QByteArray &json)
{
    QVector<QRect> rects;
    std::function<void(const QJsonObject &)> walk = [&](const QJsonObject &node) {
        const QString type = node.value(QStringLiteral("type")).toString();
        if ((type == QLatin1String("con") || type == QLatin1String("floating_con"))
            && node.value(QStringLiteral("visible")).toBool()
            && !node.value(QStringLiteral("name")).isNull()) {
            const QJsonObject r = node.value(QStringLiteral("rect")).toObject();
            const QRect rect(r.value(QStringLiteral("x")).toInt(),
                             r.value(QStringLiteral("y")).toInt(),
                             r.value(QStringLiteral("width")).toInt(),
                             r.value(QStringLiteral("height")).toInt());
            if (rect.width() >= 8 && rect.height() >= 8)
                rects.append(rect);
        }
        for (const auto &key : {QStringLiteral("nodes"), QStringLiteral("floating_nodes")}) {
            const QJsonArray kids = node.value(key).toArray();
            for (const auto &k : kids)
                walk(k.toObject());
        }
    };
    walk(QJsonDocument::fromJson(json).object());
    return rects;
}

// Hyprland: flat client list; rects on other workspaces vanish later when
// OverlayController clips them to each screen.
static QVector<QRect> parseHyprlandClients(const QByteArray &json)
{
    QVector<QRect> rects;
    const QJsonArray arr = QJsonDocument::fromJson(json).array();
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        if (!o.value(QStringLiteral("mapped")).toBool()
            || o.value(QStringLiteral("hidden")).toBool())
            continue;
        const QJsonArray at = o.value(QStringLiteral("at")).toArray();
        const QJsonArray size = o.value(QStringLiteral("size")).toArray();
        if (at.size() == 2 && size.size() == 2) {
            const QRect r(at[0].toInt(), at[1].toInt(), size[0].toInt(), size[1].toInt());
            if (r.width() >= 8 && r.height() >= 8)
                rects.append(r);
        }
    }
    return rects;
}

// ---------------------------------------------------------------- dispatch

void WindowRects::query(QObject *context, Callback cb)
{
    auto *self = new WindowRects();
    QPointer<QObject> guard(context);
    self->m_cb = [cb, guard](const QVector<QRect> &r, const QString &backend) {
        if (guard && cb)
            cb(r, backend);
    };

    // Give up quietly when the backend never answers — smart pick then simply
    // runs without window priors.
    auto *timeout = new QTimer(self);
    timeout->setSingleShot(true);
    timeout->setInterval(500);
    connect(timeout, &QTimer::timeout, self, [self] { self->finish({}); });
    timeout->start();

    auto *iface = QDBusConnection::sessionBus().interface();
    const bool kwin = iface && iface->isServiceRegistered(QStringLiteral("org.kde.KWin"));
    const bool gnome = iface && iface->isServiceRegistered(QStringLiteral("org.gnome.Shell"));
    const QString swaySock = qEnvironmentVariable("SWAYSOCK");
    const bool hypr = !qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE").isEmpty();

    if (kwin) {
        self->m_backend = QStringLiteral("kwin");
        self->startKWin();
    } else if (hypr && !QStandardPaths::findExecutable(QStringLiteral("hyprctl")).isEmpty()) {
        self->m_backend = QStringLiteral("hyprland");
        self->startProcess(QStringLiteral("hyprctl"),
                           {QStringLiteral("-j"), QStringLiteral("clients")},
                           parseHyprlandClients);
    } else if (!swaySock.isEmpty()
               && !QStandardPaths::findExecutable(QStringLiteral("swaymsg")).isEmpty()) {
        self->m_backend = QStringLiteral("sway");
        self->startProcess(QStringLiteral("swaymsg"),
                           {QStringLiteral("-t"), QStringLiteral("get_tree")},
                           parseSwayTree);
    } else if (gnome) {
        self->m_backend = QStringLiteral("gnome");
        self->startGnome();
    } else {
        self->m_backend.clear();
        self->finish({});
    }
}

void WindowRects::finish(const QVector<QRect> &rects)
{
    if (m_done)
        return;
    m_done = true;
    if (!m_pluginName.isEmpty()) {
        // Unload the one-shot KWin script; harmless if load never succeeded.
        QDBusMessage unload = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
            QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("unloadScript"));
        unload << m_pluginName;
        QDBusConnection::sessionBus().asyncCall(unload);
        QDBusConnection::sessionBus().unregisterObject(m_dbusPath);
    }
    if (m_cb)
        m_cb(rects, rects.isEmpty() ? QString() : m_backend);
    deleteLater();
}
