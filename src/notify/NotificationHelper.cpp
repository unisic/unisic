#include "NotificationHelper.h"
#include "NotifCard.h"
#include <QJsonDocument>
#include "theme/IconImageProvider.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegion>
#include <QScreen>
#include <QSocketNotifier>
#include <QTimer>
#include <QDBusConnection>
#include <QStyleHints>
#include <QSurfaceFormat>
#include <QTranslator>
#include <QUrl>
#include <cstdio>
#include <unistd.h>

namespace {

// Stub `App` exposed to NotificationPopup.qml. The real AppContext cannot exist
// in this xcb-only helper process, so the QML gets an object with the same shape
// for the handful of members the card touches. `settings` is a QQmlPropertyMap
// built from the parent's config (NotifCard::makeSettingsMap) rather than a
// hand-mirrored class: the QML reads the real property names, and a card setting
// added later needs no change here at all.
class AppStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *settings READ settings CONSTANT)
    Q_PROPERTY(bool qrAvailable READ qrAvailable CONSTANT)
    Q_PROPERTY(bool ocrAvailable READ ocrAvailable CONSTANT)
public:
    using QObject::QObject;
    QQmlPropertyMap *m_settings = nullptr;
    bool m_qr = false;
    bool m_ocr = false;
    QObject *settings() const { return m_settings; }
    bool qrAvailable() const { return m_qr; }
    bool ocrAvailable() const { return m_ocr; }
};

// Stub `notif` (mirrors CaptureNotification's QML surface). Every action button
// prints a one-word token on stdout; the parent maps it back onto the real
// CaptureNotification. State the buttons key off (url/uploading/filePath) is
// pushed from the parent over stdin as "state:<uploading>|<url>|<filePath>".
class NotifBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString thumbSource READ thumbSource CONSTANT)
    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(QString filePath READ filePath NOTIFY stateChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY stateChanged)
    Q_PROPERTY(QString url READ url NOTIFY stateChanged)
    Q_PROPERTY(bool uploading READ uploading NOTIFY stateChanged)
public:
    using QObject::QObject;
    QString m_thumbSource;
    QString m_kind;
    QString m_filePath;
    QString m_url;
    bool m_uploading = false;

    QString thumbSource() const { return m_thumbSource; }
    QString kind() const { return m_kind; }
    QString filePath() const { return m_filePath; }
    QString fileName() const { return m_filePath.isEmpty() ? QString() : QFileInfo(m_filePath).fileName(); }
    QString url() const { return m_url; }
    bool uploading() const { return m_uploading; }

    void setState(bool uploading, const QString &url, const QString &filePath)
    {
        m_uploading = uploading;
        m_url = url;
        m_filePath = filePath;
        emit stateChanged();
    }

    // Mirrors CaptureNotification::dragUri, minus the temp-file branch: this
    // xcb helper never holds the full image (only the file path came over the
    // command line), so an unsaved capture simply isn't draggable here.
    Q_INVOKABLE QString dragUri() const
    {
        return m_filePath.isEmpty() ? QString()
                                    : QUrl::fromLocalFile(m_filePath).toString(QUrl::FullyEncoded);
    }

    Q_INVOKABLE void edit() { emit action(QStringLiteral("edit")); }
    Q_INVOKABLE void preview() { emit action(QStringLiteral("preview")); }
    Q_INVOKABLE void copyImage() { emit action(QStringLiteral("copy-image")); }
    Q_INVOKABLE void copyAs(const QString &format) { emit action(QStringLiteral("copy-as:") + format); }
    Q_INVOKABLE void copyUrl() { emit action(QStringLiteral("copy-url")); }
    Q_INVOKABLE void showQr() { emit action(QStringLiteral("qr")); }
    Q_INVOKABLE void showInFolder() { emit action(QStringLiteral("folder")); }
    Q_INVOKABLE void upload() { emit action(QStringLiteral("upload")); }
    Q_INVOKABLE void ocr() { emit action(QStringLiteral("ocr")); }
    Q_INVOKABLE void deleteCapture() { emit action(QStringLiteral("delete")); }
    Q_INVOKABLE void trim() { emit action(QStringLiteral("trim")); }
    // dismiss closes the card locally (auto-hide / close button) AND tells the
    // parent, so it can retire its CaptureNotification.
    Q_INVOKABLE void dismiss()
    {
        emit action(QStringLiteral("dismiss"));
        emit closeRequested();
    }

signals:
    void stateChanged();
    void closeRequested();
    void action(const QString &token);
};

void emitToken(const QString &token)
{
    const QByteArray b = token.toUtf8();
    ::fwrite(b.constData(), 1, size_t(b.size()), stdout);
    ::fputc('\n', stdout);
    ::fflush(stdout);
}

} // namespace

int runNotificationHelper(int argc, char *argv[])
{
    // xcb BEFORE QGuiApplication: an XWayland override-redirect window is the only
    // surface mutter keeps above every application window (one process can't host
    // two QPA platforms, so this is a child process, not the Wayland main one).
    qputenv("QT_QPA_PLATFORM", "xcb");
    // Same custom look as the main window (UButton/UIcon assume the Basic style).
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication app(argc, argv);
    // A dev build is a SEPARATE app; match its name so QSettings/ThemeController
    // read the same persisted theme (ui/theme) as the parent.
#ifdef UNISIC_DEV_BUILD
    app.setApplicationName(QStringLiteral("unisic-dev"));
#else
    app.setApplicationName(QStringLiteral("unisic"));
#endif
    app.setOrganizationName(QStringLiteral("Unisic"));

    // --notification-helper <name> <lx> <ly> <lw> <lh> <configJson> <lang>
    //   <kind> <uploading> <url> <thumbPath> [filePath]
    // The helper is this same binary, so the argument list never version-skews
    // against the parent — positional is fine. Everything the card's look is
    // driven by rides in <configJson> (NotifCard::encodeConfig), so adding a
    // card setting never touches this list again — which is what the old
    // one-arg-per-setting shape cost: every addition shifted every index below
    // it, silently, at runtime.
    const QStringList args = app.arguments();
    const int i = args.indexOf(QStringLiteral("--notification-helper"));
    if (i < 0 || i + 12 >= args.size())
        return 2;
    const QString name = args[i + 1];
    const QRect logical(args[i + 2].toInt(), args[i + 3].toInt(),
                        args[i + 4].toInt(), args[i + 5].toInt());
    const QJsonObject config =
        QJsonDocument::fromJson(args[i + 6].toUtf8()).object();
    const QJsonObject settingsJson = config.value(QStringLiteral("settings")).toObject();
    const QString corner = settingsJson.value(QStringLiteral("capturePopupPosition"))
                               .toString(QStringLiteral("bottom-right"));
    const int edgeArg = settingsJson.value(QStringLiteral("capturePopupMargin")).toInt(8);
    const QString style =
        NotifCard::normalizeStyle(settingsJson.value(QStringLiteral("capturePopupStyle")).toString());
    const int durationSec = settingsJson.value(QStringLiteral("capturePopupDurationSec")).toInt(8);
    const bool qrAvail = config.value(QStringLiteral("qrAvailable")).toBool();
    const bool ocrAvail = config.value(QStringLiteral("ocrAvailable")).toBool();
    const QString lang = args[i + 7];
    const QString kind = args[i + 8];
    const bool uploading = args[i + 9] == QLatin1String("1");
    const QString url = args[i + 10];
    const QString thumbPath = args[i + 11];
    const QString filePath = (i + 12 < args.size()) ? args[i + 12] : QString();

    // UI language: the popup's qsTr strings resolve against the parent's language.
    QTranslator appTr;
    if (appTr.load(QStringLiteral(":/i18n/unisic_%1.qm").arg(lang)))
        app.installTranslator(&appTr);
    QTranslator qtTr;
    if (qtTr.load(QLocale(lang), QStringLiteral("qtbase"), QStringLiteral("_"),
                  QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTr);

    // System-theme icons resolve against Breeze, exactly as the main process.
    const bool dark = app.styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    QIcon::setFallbackThemeName(dark ? QStringLiteral("breeze-dark") : QStringLiteral("breeze"));

    // Same table as the layer-shell host — NotifCard is the only copy.
    const QSize card = NotifCard::sizeForStyle(style);
    const int cardW = card.width(), cardH = card.height();
    const int pad = NotifCard::kPad;
    const int edge = qMax(0, edgeArg);

    auto *appStub = new AppStub(&app);
    appStub->m_settings = NotifCard::makeSettingsMap(settingsJson, appStub);
    appStub->m_qr = qrAvail;
    appStub->m_ocr = ocrAvail;

    auto *bridge = new NotifBridge(&app);
    bridge->m_kind = kind;
    bridge->m_filePath = filePath;
    bridge->m_url = url;
    bridge->m_uploading = uploading;
    if (!thumbPath.isEmpty())
        bridge->m_thumbSource = QUrl::fromLocalFile(thumbPath).toString();
    QObject::connect(bridge, &NotifBridge::action, &app, [](const QString &t) { emitToken(t); });

    QQmlEngine engine;
    engine.addImageProvider(QStringLiteral("icon"), new IconImageProvider(nullptr));
    engine.rootContext()->setContextProperty(QStringLiteral("App"), appStub);
    engine.rootContext()->setContextProperty(QStringLiteral("notif"), bridge);
    engine.rootContext()->setContextProperty(QStringLiteral("popupX"), pad);
    engine.rootContext()->setContextProperty(QStringLiteral("popupY"), pad);
    engine.rootContext()->setContextProperty(QStringLiteral("popupW"), cardW);
    engine.rootContext()->setContextProperty(QStringLiteral("popupH"), cardH);
    // Same three the layer-shell host sets, from the same snapshot — the card
    // QML reads these, never App.settings.
    engine.rootContext()->setContextProperty(QStringLiteral("popupStyle"), style);
    engine.rootContext()->setContextProperty(QStringLiteral("popupAutoHideSec"), qMax(0, durationSec));
    engine.rootContext()->setContextProperty(
        QStringLiteral("popupHiddenActions"),
        settingsJson.value(QStringLiteral("hiddenNotifActions")).toString());

    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Unisic/qml/NotificationPopup.qml")));
    if (component.isError()) {
        std::fputs(component.errorString().toUtf8().constData(), stderr);
        return 3;
    }
    QObject *obj = component.create();
    auto *win = qobject_cast<QQuickWindow *>(obj);
    if (!win) {
        delete obj;
        return 3;
    }

    // Match the Wayland screen to an X screen (see RecordBorderHelper): name →
    // exact logical geometry → sole screen → primary.
    const QList<QScreen *> screens = app.screens();
    QScreen *target = nullptr;
    for (QScreen *s : screens)
        if (s->name() == name) { target = s; break; }
    if (!target)
        for (QScreen *s : screens)
            if (s->geometry() == logical) { target = s; break; }
    if (!target && screens.size() == 1)
        target = screens.first();
    if (!target)
        target = app.primaryScreen();
    if (target)
        win->setScreen(target);

    // Override-redirect, always-on-top, no focus stealing — but NOT
    // input-transparent (the card's buttons must receive clicks).
    win->setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::BypassWindowManagerHint
                  | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    win->setColor(Qt::transparent);
    QSurfaceFormat fmt = win->format();
    fmt.setAlphaBufferSize(8); // ARGB visual — rounded corners / shadow need it
    win->setFormat(fmt);

    const int winW = cardW + 2 * pad, winH = cardH + 2 * pad;
    // availableGeometry, not geometry: this helper runs on xcb, where Qt derives
    // it from _NET_WORKAREA — so panels and docks that reserve space are already
    // subtracted and the card lands beside them instead of under them. The
    // layer-shell path gets the same clearance from exclusive zones. Nothing to
    // guard: with no strut (or no reader for it) the two rects are identical, so
    // the worst case is exactly the old full-screen behaviour. Intersected with
    // the screen because _NET_WORKAREA is one rect for the whole X screen, not
    // one per monitor — on a multi-head layout Qt's per-screen clamp can still
    // hand back a rect that is empty or off this monitor.
    QRect sg = target ? target->availableGeometry() : QRect(0, 0, 1920, 1080);
    if (target && (sg.isEmpty() || !target->geometry().intersects(sg)))
        sg = target->geometry();
    // Anchor the WINDOW at `edge`; the card sits `pad` inside, so the visible
    // card lands edge+pad from the screen — the same inset as the layer-shell card.
    int x = sg.x() + edge, y = sg.y() + edge;
    if (corner.contains(QLatin1String("right")))
        x = sg.x() + sg.width() - winW - edge;
    else if (corner.contains(QLatin1String("center")))
        x = sg.x() + (sg.width() - winW) / 2;
    if (corner.startsWith(QLatin1String("bottom")))
        y = sg.y() + sg.height() - winH - edge;
    win->setGeometry(QRect(x, y, winW, winH));
    win->show();
    // Only the card region takes pointer input; the transparent shadow pad around
    // it stays click-through so it can't eat clicks on windows in that corner.
    win->setMask(QRegion(pad, pad, cardW, cardH));



    // Close the card when the QML asks (auto-hide, close button, or a parent
    // "close" command routed through dismiss()).
    QObject::connect(bridge, &NotifBridge::closeRequested, &app, &QGuiApplication::quit);

    // Parent -> helper commands, newline-delimited:
    //   state:<uploading>|<url>|<filePath>   — refresh the action buttons
    //   close                                — the parent retired this card
    // stdin EOF (parent died) also quits, so a card never outlives its parent.
    static QByteArray inbuf;
    auto *stdinWatch = new QSocketNotifier(0, QSocketNotifier::Read, &app);
    QObject::connect(stdinWatch, &QSocketNotifier::activated, &app, [bridge, &app] {
        char buf[512];
        const ssize_t n = ::read(0, buf, sizeof buf);
        if (n <= 0) { app.quit(); return; }
        inbuf.append(buf, int(n));
        int nl;
        while ((nl = inbuf.indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(inbuf.left(nl));
            inbuf.remove(0, nl + 1);
            if (line == QLatin1String("close")) {
                app.quit();
                return;
            }
            if (line.startsWith(QLatin1String("state:"))) {
                const QStringList parts = line.mid(6).split(QLatin1Char('|'));
                if (parts.size() >= 3)
                    // filePath may itself contain '|' — rejoin the tail.
                    bridge->setState(parts[0] == QLatin1String("1"), parts[1],
                                     QStringList(parts.mid(2)).join(QLatin1Char('|')));
            }
        }
    });

    return app.exec();
}

#include "NotificationHelper.moc"
