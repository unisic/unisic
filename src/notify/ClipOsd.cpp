#include "ClipOsd.h"
#include "AppContext.h"
#include "LayerShellNotifier.h"
#include "diag/DiagLog.h"
#include "theme/IconImageProvider.h"

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QSocketNotifier>
#include <QDebug>
#include <cstdio>
#include <unistd.h>

namespace {
const QString kQml = QStringLiteral("qrc:/qt/qml/Unisic/qml/ClipOsd.qml");
constexpr int kTop = 32; // px below the top edge, clear of a panel's shadow

// One protocol line; tabs and newlines inside a field would split it.
QByteArray osdLine(const QString &title, const QString &detail, const QString &tone, int ms)
{
    const auto clean = [](QString s) {
        return s.replace(QLatin1Char('\t'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' '));
    };
    return (clean(title) + QLatin1Char('\t') + clean(detail) + QLatin1Char('\t') + tone
            + QLatin1Char('\t') + QString::number(ms) + QLatin1Char('\n')).toUtf8();
}

void flash(QQuickWindow *win, const QString &title, const QString &detail, const QString &tone, int ms)
{
    QMetaObject::invokeMethod(win, "flash", Q_ARG(QVariant, title), Q_ARG(QVariant, detail),
                              Q_ARG(QVariant, tone), Q_ARG(QVariant, ms));
}
} // namespace

ClipOsd::ClipOsd(AppContext *app, bool layerShell, QObject *parent)
    : QObject(parent), m_app(app), m_layerShell(layerShell)
{
}

ClipOsd::~ClipOsd()
{
    if (m_helper) {
        m_helper->closeWriteChannel(); // the helper quits on EOF
        if (!m_helper->waitForFinished(500))
            m_helper->kill();
    }
    delete m_win;
}

void ClipOsd::show(const QString &title, const QString &detail, const QString &tone, int ms)
{
    if (m_layerShell || LayerShellNotifier::placesCardItself()) {
        if (!m_win) {
            QQmlEngine *engine = m_app->qmlEngine();
            if (!engine)
                return;
            QQmlComponent component(engine, QUrl(kQml));
            auto *win = qobject_cast<QQuickWindow *>(component.create());
            if (!win) {
                qWarning() << component.errorString();
                return;
            }
            QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
            if (!screen)
                screen = QGuiApplication::primaryScreen();
            if (screen)
                win->setScreen(screen);
            LayerShellNotifier::place(win, screen, QStringLiteral("top"), kTop);
            connect(win, SIGNAL(done()), win, SLOT(close()));
            connect(win, &QWindow::visibleChanged, win, [win](bool visible) {
                if (!visible)
                    win->deleteLater();
            });
            win->show();
            m_win = win;
        }
        flash(m_win, title, detail, tone, ms);
        return;
    }

    if (m_helper && m_helper->state() == QProcess::Running) {
        // shortcut: a line written while the helper is already fading out is
        // lost; the pill is a hint, and the next event shows again.
        m_helper->write(osdLine(title, detail, tone, ms));
        return;
    }
    const bool x11 = QGuiApplication::platformName() == QLatin1String("xcb");
    if (!x11 && !qEnvironmentVariableIsSet("DISPLAY"))
        return; // no layer-shell and no X server: nowhere to stack above a game
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const QRect g = screen->geometry();
    auto *proc = new QProcess(this);
    proc->setProgram(QCoreApplication::applicationFilePath());
    proc->setArguments({QStringLiteral("--osd-helper"), screen->name(),
                        QString::number(g.x()), QString::number(g.y()),
                        QString::number(g.width()), QString::number(g.height())});
    proc->setProcessChannelMode(QProcess::ForwardedOutputChannel);
    connect(proc, &QProcess::readyReadStandardError, this, [proc] {
        DiagLog::appendChildOutput(QStringLiteral("osd-helper"), proc->readAllStandardError());
    });
    connect(proc, &QProcess::finished, this, [this, proc] {
        if (m_helper == proc)
            m_helper = nullptr;
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        if (m_helper == proc)
            m_helper = nullptr;
        proc->deleteLater();
    });
    proc->start();
    proc->write(osdLine(title, detail, tone, ms)); // buffered until the helper reads
    m_helper = proc;
}

int runOsdHelper(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "xcb"); // override-redirect: the one surface mutter keeps above games
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication app(argc, argv);
#ifdef UNISIC_DEV_BUILD
    app.setApplicationName(QStringLiteral("unisic-dev"));
#else
    app.setApplicationName(QStringLiteral("unisic"));
#endif
    app.setOrganizationName(QStringLiteral("Unisic"));

    const QStringList args = app.arguments();
    const int i = args.indexOf(QStringLiteral("--osd-helper"));
    if (i < 0 || i + 5 >= args.size())
        return 2;
    const QString name = args[i + 1];
    const QRect logical(args[i + 2].toInt(), args[i + 3].toInt(), args[i + 4].toInt(), args[i + 5].toInt());

    QQmlEngine engine;
    engine.addImageProvider(QStringLiteral("icon"), new IconImageProvider(nullptr));
    QQmlComponent component(&engine, QUrl(kQml));
    auto *win = qobject_cast<QQuickWindow *>(component.create());
    if (!win) {
        std::fputs(component.errorString().toUtf8().constData(), stderr);
        return 3;
    }
    QScreen *target = nullptr;
    for (QScreen *s : app.screens())
        if (s->name() == name || s->geometry() == logical) { target = s; break; }
    if (!target)
        target = app.primaryScreen();
    const QRect sg = target ? target->geometry() : logical;
    if (target)
        win->setScreen(target);
    win->setFlags(win->flags() | Qt::BypassWindowManagerHint);
    win->setPosition(sg.x() + (sg.width() - win->width()) / 2, sg.y() + kTop);
    QObject::connect(win, SIGNAL(done()), &app, SLOT(quit()));
    win->show();

    // stdin lines: title \t detail \t tone \t ms. EOF (parent gone) quits.
    static QByteArray inbuf;
    auto *stdinWatch = new QSocketNotifier(0, QSocketNotifier::Read, &app);
    QObject::connect(stdinWatch, &QSocketNotifier::activated, &app, [win, &app] {
        char buf[1024];
        const ssize_t n = ::read(0, buf, sizeof buf);
        if (n <= 0) {
            app.quit();
            return;
        }
        inbuf.append(buf, int(n));
        if (inbuf.size() > 64 * 1024)
            inbuf.clear(); // a runaway writer; the parent only sends short lines
        int nl;
        while ((nl = inbuf.indexOf('\n')) >= 0) {
            const QStringList f = QString::fromUtf8(inbuf.left(nl)).split(QLatin1Char('\t'));
            inbuf.remove(0, nl + 1);
            if (f.size() == 4)
                flash(win, f[0], f[1], f[2], f[3].toInt());
        }
    });
    return app.exec();
}
