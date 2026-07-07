#include "AppContext.h"
#include "Settings.h"
#include "capture/CaptureManager.h"
#include "overlay/OverlayController.h"
#include "upload/UploadManager.h"
#include "history/HistoryStore.h"
#include "hotkeys/GlobalHotkeys.h"
#include "record/GifRecorder.h"
#include "editor/EditorSession.h"
#include "notify/CaptureNotification.h"
#include "theme/ThemeController.h"
#ifdef HAVE_TESSERACT
#include "ocr/OcrEngine.h"
#endif
#include <QGuiApplication>
#include <QScreen>
#include <QRegion>
#include <QPointer>
#include <QClipboard>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QKeySequence>
#include <QDesktopServices>
#include <QDateTime>
#include <QDir>
#include <QBuffer>
#include <QTimer>
#include <QProcess>
#include <QPainter>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUuid>
#include <QMetaProperty>
#include <QDebug>

AppContext::AppContext(QObject *parent)
    : QObject(parent)
    , m_settings(new Settings(this))
    , m_capture(new CaptureManager(m_settings, this))
    , m_overlay(new OverlayController(this, this))
    , m_uploads(new UploadManager(m_settings, this))
    , m_history(new HistoryStore(this))
    , m_hotkeys(new GlobalHotkeys(this))
    , m_recorder(new GifRecorder(m_settings, this))
{
    connect(m_hotkeys, &GlobalHotkeys::activated, this, [this](const QString &action) {
        if (m_shortcutRecording)
            return;
        if (action == QLatin1String("capture-fullscreen")) captureFullScreen();
        else if (action == QLatin1String("capture-region")) captureRegion();
        else if (action == QLatin1String("capture-window")) captureWindow();
        else if (action == QLatin1String("record-gif")) {
            if (recording()) stopRecording();
            else startGifRegion();
        } else if (action == QLatin1String("record-video")) {
            if (recording()) stopRecording();
            else startVideoRegion();
        }
    });

    connect(m_recorder, &GifRecorder::started, this, &AppContext::recordingChanged);
    connect(m_recorder, &GifRecorder::elapsedChanged, this, &AppContext::recordSecondsChanged);
    connect(m_recorder, &GifRecorder::converting, this, [this] {
        m_converting = true;
        emit recordingChanged();
        showToast(tr("Encoding…"));
    });
    connect(m_recorder, &GifRecorder::finished, this, &AppContext::onRecordingFinished);
    connect(m_recorder, &GifRecorder::failed, this, [this](const QString &e) {
        m_converting = false;
        emit recordingChanged();
        if (e != QLatin1String("cancelled"))
            showToast(tr("Recording failed: %1").arg(e));
    });

    // A history file that could not be trashed still gets its entry removed;
    // let the user know the file is still on disk.
    connect(m_history, &HistoryStore::fileTrashFailed, this, [this](const QString &path) {
        showToast(tr("Could not move %1 to trash — the file is still on disk").arg(path));
    });

#ifdef HAVE_TESSERACT
    m_ocr = new OcrEngine(this);
#endif
}

AppContext::~AppContext()
{
    // Keep registered shortcuts so they survive restarts (KGlobalAccel autoloads them).
}

void AppContext::initialize(QQmlEngine *engine)
{
    m_engine = engine;
    setupTray();
    defineHotkeys(); // register + autoload; DE-stored keys win, not clobbered
}

bool AppContext::recording() const { return m_recorder->recording(); }
bool AppContext::converting() const { return m_converting; }
int AppContext::recordSeconds() const { return m_recorder->elapsedSeconds(); }

bool AppContext::recordingAvailable() const
{
#ifdef HAVE_PIPEWIRE
    return true;
#else
    return false;
#endif
}

bool AppContext::ocrAvailable() const
{
#ifdef HAVE_TESSERACT
    return true;
#else
    return false;
#endif
}

void AppContext::ocrImage(const QImage &img)
{
#ifdef HAVE_TESSERACT
    if (img.isNull()) {
        showToast(tr("Nothing to recognize"));
        return;
    }
    showToast(tr("Recognizing text…"));
    m_ocr->recognize(img, m_settings->ocrLanguages(), [this](const QString &text, const QString &err) {
        if (!err.isEmpty())
            showToast(err);
        else if (text.isEmpty())
            showToast(tr("No text found"));
        else {
            copyText(text);
            showToast(tr("Text copied"));
        }
    });
#else
    Q_UNUSED(img);
    showToast(tr("OCR is not available in this build"));
#endif
}

void AppContext::ocrFile(const QString &path)
{
    ocrImage(QImage(path));
}

void AppContext::showToast(const QString &text)
{
    if (!m_settings->showNotifications())
        return;
    m_toast = text;
    emit toastChanged();
}

QString AppContext::formatShortcut(int key, int modifiers) const
{
    switch (key) {
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Hyper_L:
    case Qt::Key_Hyper_R:
        return {};
    default:
        break;
    }

    const auto allowed = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    const Qt::KeyboardModifiers mods = Qt::KeyboardModifiers(modifiers) & allowed;
    return QKeySequence(mods.toInt() | key).toString(QKeySequence::PortableText);
}

void AppContext::setShortcutRecording(bool recording)
{
    m_shortcutRecording = recording;
}

void AppContext::withDelay(std::function<void()> fn)
{
    const int delay = qMax(0, m_settings->captureDelayMs());
    QTimer::singleShot(delay, this, std::move(fn));
}

// ------------------------------------------------------------------ capture

// Appends actionable guidance when the failure looks like the classic
// "unauthorized build-tree run / portal missing" situation.
static QString captureErrorText(const QString &err)
{
    QString text = QCoreApplication::translate("AppContext", "Capture failed: %1").arg(err);
    if (err.contains(QLatin1String("portal"), Qt::CaseInsensitive)
        || err.contains(QLatin1String("NoAuthorized"))) {
        text += QCoreApplication::translate(
            "AppContext",
            " — install Unisic (sudo cmake --install build) and launch it from the application "
            "menu so KDE authorizes it, and check that xdg-desktop-portal-kde is running.");
    }
    return text;
}

void AppContext::captureFullScreen()
{
    withDelay([this] {
        m_capture->captureWorkspace([this](const QImage &img, const QString &err) {
            if (!err.isEmpty()) {
                if (err != QLatin1String("cancelled"))
                    showToast(captureErrorText(err));
                return;
            }
            finishCapture(img);
        });
    });
}

void AppContext::captureRegion()
{
    withDelay([this] {
        m_overlay->pickAnnotatedImage([this](const QImage &img) {
            if (!img.isNull())
                finishCapture(img);
        });
    });
}

void AppContext::captureWindow()
{
    withDelay([this] {
        m_capture->captureActiveWindow([this](const QImage &img, const QString &err) {
            if (!err.isEmpty()) {
                if (err != QLatin1String("cancelled"))
                    showToast(captureErrorText(err));
                return;
            }
            finishCapture(img);
        });
    });
}

// ---------------------------------------------------------------- recording

void AppContext::startGifRegion()
{
    if (recording()) return;
    m_overlay->pickRegion([this](const QRect &phys, QScreen *screen) {
        if (phys.isEmpty()) return;
        m_recorder->start(GifRecorder::Gif, GifRecorder::Region, phys, screen);
    });
}

void AppContext::startGifFullScreen()
{
    if (recording()) return;
    m_recorder->start(GifRecorder::Gif, GifRecorder::Screen);
}

GifRecorder::Output AppContext::videoOutput() const
{
    return m_settings->videoFormat().compare(QLatin1String("webm"), Qt::CaseInsensitive) == 0
               ? GifRecorder::WebM : GifRecorder::Mp4;
}

void AppContext::startVideoScreen()
{
    if (recording()) return;
    m_recorder->start(videoOutput(), GifRecorder::Screen);
}

void AppContext::startVideoRegion()
{
    if (recording()) return;
    m_overlay->pickRegion([this](const QRect &phys, QScreen *screen) {
        if (phys.isEmpty()) return;
        m_recorder->start(videoOutput(), GifRecorder::Region, phys, screen);
    });
}

void AppContext::startVideoWindow()
{
    if (recording()) return;
    m_recorder->start(videoOutput(), GifRecorder::Window);
}

void AppContext::stopRecording()
{
    m_recorder->stop();
}

void AppContext::onRecordingFinished(const QString &path)
{
    m_converting = false;
    emit recordingChanged();
    QImage thumb(path); // first GIF frame loads fine via Qt's gif plugin
    const QString kind = path.endsWith(QLatin1String(".gif")) ? QStringLiteral("gif")
                                                              : QStringLiteral("video");
    m_history->addEntry(path, thumb, kind);
    showToast(tr("Saved %1").arg(path));

    auto *notif = showCaptureNotification(thumb, path, kind);
    QPointer<CaptureNotification> np(notif);

    if (m_settings->uploadAfterCapture()) {
        if (np) np->setUploading(true);
        m_uploads->uploadFile(path, [this, path, np](const QString &url, const QString &del, const QString &err) {
            if (err.isEmpty()) {
                m_history->setUrl(path, url, del);
                afterUploadActions(url);
                if (np) np->setUrl(url);
            } else {
                showToast(tr("Upload failed: %1").arg(err));
                if (np) np->setUploading(false);
            }
        });
    }
}

// ----------------------------------------------------------- after-capture

// Every enabled action runs immediately and independently the moment the
// capture lands — the editor no longer swallows the pipeline.
void AppContext::finishCapture(const QImage &img)
{
    if (img.isNull())
        return;

    QString path;
    if (m_settings->autoSave())
        path = saveImageAuto(img);
    if (m_settings->copyToClipboard())
        copyImageToClipboard(img);

    const bool uploading = m_settings->uploadAfterCapture();
    if (!uploading || !path.isEmpty())
        m_history->addEntry(path, img, QStringLiteral("image"));

    auto *notif = showCaptureNotification(img, path, QStringLiteral("image"));
    QPointer<CaptureNotification> np(notif);

    if (uploading) {
        if (np) np->setUploading(true);
        QString mime;
        const QByteArray data = encodeImage(img, &mime);
        m_uploads->uploadData(data, makeFileName(), mime,
            [this, path, img, np](const QString &url, const QString &del, const QString &err) {
                if (!err.isEmpty()) {
                    if (path.isEmpty())
                        m_history->addEntry({}, img, QStringLiteral("image"));
                    showToast(tr("Upload failed: %1").arg(err));
                    if (np) np->setUploading(false);
                    return;
                }
                if (!path.isEmpty())
                    m_history->setUrl(path, url, del);
                else
                    m_history->addEntry({}, img, QStringLiteral("image"), url, del);
                afterUploadActions(url);
                if (np) np->setUrl(url);
            });
    } else if (!path.isEmpty()) {
        showToast(tr("Saved %1").arg(path));
    }

    if (m_settings->openEditor())
        openEditor(img);
}

void AppContext::afterUploadActions(const QString &url)
{
    if (m_settings->afterUploadCopyLink()) {
        copyText(url);
        showToast(tr("Uploaded — link copied"));
    } else {
        showToast(tr("Uploaded: %1").arg(url));
    }
    if (m_settings->afterUploadOpenInBrowser())
        QDesktopServices::openUrl(QUrl(url));
}

void AppContext::openEditor(const QImage &img)
{
    QQmlComponent component(m_engine, QUrl(QStringLiteral("qrc:/qt/qml/Unisic/qml/EditorWindow.qml")));
    if (component.isError()) {
        qWarning() << component.errorString();
        return;
    }
    auto *session = new EditorSession(this, img, this);
    auto *ctx = new QQmlContext(m_engine->rootContext(), session);
    ctx->setContextProperty(QStringLiteral("editorSession"), session);
    QObject *obj = component.create(ctx);
    if (auto *win = qobject_cast<QQuickWindow *>(obj)) {
        connect(win, &QQuickWindow::visibleChanged, session, [session, win](bool v) {
            if (!v) { win->deleteLater(); session->deleteLater(); }
        });
        win->show();
        win->requestActivate();
    } else {
        delete obj;
        session->deleteLater();
    }
}

void AppContext::uploadFromNotification(CaptureNotification *n, const QImage &img, const QString &path)
{
    QPointer<CaptureNotification> np(n);
    if (n)
        n->setUploading(true);
    QString mime;
    const QByteArray data = encodeImage(img, &mime);
    const QString fileName = path.isEmpty() ? makeFileName() : QFileInfo(path).fileName();
    m_uploads->uploadData(data, fileName, mime,
        [this, img, path, np](const QString &url, const QString &del, const QString &err) {
            if (!err.isEmpty()) {
                showToast(tr("Upload failed: %1").arg(err));
                if (np) np->setUploading(false);
                return;
            }
            if (!path.isEmpty())
                m_history->setUrl(path, url, del);
            else
                m_history->addEntry({}, img, QStringLiteral("image"), url, del);
            afterUploadActions(url);
            if (np) np->setUrl(url);
        });
}

CaptureNotification *AppContext::showCaptureNotification(const QImage &img, const QString &path,
                                                         const QString &kind)
{
    if (!m_settings->showCapturePopup() || !m_engine)
        return nullptr;
    QQmlComponent component(m_engine, QUrl(QStringLiteral("qrc:/qt/qml/Unisic/qml/NotificationPopup.qml")));
    if (component.isError()) {
        qWarning() << component.errorString();
        return nullptr;
    }
    // Wayland ignores client-set positions for ordinary toplevels (KWin centers
    // them), so the popup is a transparent window that FILLS the screen — the one
    // geometry the compositor honors (same trick as OverlayController) — with the
    // card drawn at the chosen corner and an input mask making the rest of the
    // surface click-through.
    QScreen *screen = QGuiApplication::primaryScreen();
    const QRect g = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
    const QRect avail = screen ? screen->availableGeometry() : g;
    const int cardW = 400, cardH = 150, margin = 16;

    const QString posName = m_settings->capturePopupPosition();
    const bool top = posName.startsWith(QLatin1String("top"));
    const bool left = posName.endsWith(QLatin1String("left"));
    const bool right = posName.endsWith(QLatin1String("right"));
    // Card position in window-local coords (window origin == screen top-left),
    // kept inside the available area so it clears panels.
    const int ax = avail.x() - g.x(), ay = avail.y() - g.y();
    const int px = left  ? ax + margin
                 : right ? ax + avail.width() - cardW - margin
                 :         ax + (avail.width() - cardW) / 2;
    const int py = top ? ay + margin : ay + avail.height() - cardH - margin;

    auto *notif = new CaptureNotification(this, img, path, kind, this);
    auto *ctx = new QQmlContext(m_engine->rootContext(), notif);
    ctx->setContextProperty(QStringLiteral("notif"), notif);
    ctx->setContextProperty(QStringLiteral("popupX"), px);
    ctx->setContextProperty(QStringLiteral("popupY"), py);
    ctx->setContextProperty(QStringLiteral("popupW"), cardW);
    ctx->setContextProperty(QStringLiteral("popupH"), cardH);
    QObject *obj = component.create(ctx);
    auto *win = qobject_cast<QQuickWindow *>(obj);
    if (!win) {
        delete obj;
        notif->deleteLater();
        ctx->deleteLater();
        return nullptr;
    }
    connect(notif, &CaptureNotification::closeRequested, win, &QQuickWindow::close);
    connect(win, &QQuickWindow::visibleChanged, notif, [win, notif, ctx](bool v) {
        if (!v) { win->deleteLater(); notif->deleteLater(); ctx->deleteLater(); }
    });

    if (screen)
        win->setScreen(screen);
    win->setGeometry(g);
    // Create the platform surface up front so the input mask is applied BEFORE
    // the window is mapped — the transparent remainder is click-through from the
    // very first frame (never a moment where it blocks the whole screen).
    win->create();
    win->setMask(QRegion(px, py, cardW, cardH));
    win->show(); // deliberately no requestActivate() — must not steal focus
    return notif;
}

QString AppContext::saveImageAuto(const QImage &img)
{
    return saveImageTo(img, m_settings->saveDirectory());
}

QString AppContext::makeFileName() const
{
    QString t = m_settings->filenameTemplate().trimmed();
    if (t.isEmpty())
        t = QStringLiteral("Unisic_%date%_%time%");
    const QDateTime now = QDateTime::currentDateTime();
    t.replace(QLatin1String("%date%"), now.toString(QStringLiteral("yyyy-MM-dd")));
    t.replace(QLatin1String("%time%"), now.toString(QStringLiteral("HH-mm-ss")));
    t.replace(QLatin1String("%datetime%"), now.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")));
    t.replace(QLatin1String("%unix%"), QString::number(now.toSecsSinceEpoch()));
    t.replace(QLatin1String("%rand%"),
              QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    static const QRegularExpression illegal(QStringLiteral("[/\\\\:*?\"<>|]"));
    t.replace(illegal, QStringLiteral("_"));

    QString ext = m_settings->imageFormat().toLower();
    if (ext == QLatin1String("jpeg")) ext = QStringLiteral("jpg");
    if (ext != QLatin1String("jpg") && ext != QLatin1String("webp")) ext = QStringLiteral("png");
    return t + QLatin1Char('.') + ext;
}

QString AppContext::filenamePreview() const
{
    return makeFileName();
}

QByteArray AppContext::encodeImage(const QImage &img, QString *mime) const
{
    const QString fmt = m_settings->imageFormat().toLower();
    const int q = qBound(1, m_settings->imageQuality(), 100);
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    if ((fmt == QLatin1String("jpg") || fmt == QLatin1String("jpeg")) && img.save(&buf, "JPG", q)) {
        *mime = QStringLiteral("image/jpeg");
    } else if (fmt == QLatin1String("webp") && img.save(&buf, "WEBP", q)) {
        *mime = QStringLiteral("image/webp");
    } else {
        out.clear();
        buf.seek(0);
        img.save(&buf, "PNG");
        *mime = QStringLiteral("image/png");
    }
    return out;
}

QString AppContext::saveImageTo(const QImage &img, const QString &dir)
{
    if (dir.isEmpty() || img.isNull())
        return {};
    QDir().mkpath(dir);
    const QString name = makeFileName();
    QString path = dir + QLatin1Char('/') + name;
    const QFileInfo fi(name);
    for (int n = 1; QFile::exists(path); ++n)
        path = dir + QLatin1Char('/') + fi.completeBaseName()
               + QStringLiteral("-%1.").arg(n) + fi.suffix();

    const QString fmt = m_settings->imageFormat().toLower();
    bool ok;
    if (fmt == QLatin1String("jpg") || fmt == QLatin1String("jpeg"))
        ok = img.save(path, "JPG", qBound(1, m_settings->imageQuality(), 100));
    else if (fmt == QLatin1String("webp"))
        ok = img.save(path, "WEBP", qBound(1, m_settings->imageQuality(), 100));
    else
        ok = img.save(path, "PNG");
    if (!ok)
        return {};
    if (m_settings->openAfterSave())
        openFile(path);
    return path;
}

void AppContext::copyImageToClipboard(const QImage &img)
{
    QGuiApplication::clipboard()->setImage(img);
    // Wayland: clipboard offers can be lost when no window has focus.
    // wl-copy (if present) makes it stick regardless.
    if (!QProcess::execute(QStringLiteral("sh"),
                           {QStringLiteral("-c"), QStringLiteral("command -v wl-copy >/dev/null")})) {
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        auto *proc = new QProcess(this);
        connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
        proc->start(QStringLiteral("wl-copy"), {QStringLiteral("--type"), QStringLiteral("image/png")});
        proc->write(png);
        proc->closeWriteChannel();
    }
}

void AppContext::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
    if (!QProcess::execute(QStringLiteral("sh"),
                           {QStringLiteral("-c"), QStringLiteral("command -v wl-copy >/dev/null")})) {
        auto *proc = new QProcess(this);
        connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
        proc->start(QStringLiteral("wl-copy"), {});
        proc->write(text.toUtf8());
        proc->closeWriteChannel();
    }
}

// Editor flow: upload the composited image only (saving is a separate action).
void AppContext::uploadImage(const QImage &img, UploadDone done)
{
    QString mime;
    const QByteArray data = encodeImage(img, &mime);
    m_uploads->uploadData(data, makeFileName(), mime,
        [this, img, done](const QString &url, const QString &del, const QString &err) {
            if (err.isEmpty()) {
                m_history->addEntry({}, img, QStringLiteral("image"), url, del);
                afterUploadActions(url);
            }
            if (done)
                done(url, err);
        });
}

void AppContext::openFile(const QString &path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void AppContext::openDirectory(const QString &path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path.isEmpty() ? m_settings->saveDirectory() : path));
}

// --------------------------------------------------------- export / import

QString AppContext::exportSettings(const QUrl &file)
{
    QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path.isEmpty())
        return tr("No file selected");
    if (!path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
        path += QLatin1String(".json");

    // Export the full *effective* configuration (defaults included) so the
    // file reproduces this setup on any machine.
    QJsonObject s;
    const QMetaObject *mo = m_settings->metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty p = mo->property(i);
        s.insert(QString::fromLatin1(p.name()), QJsonValue::fromVariant(p.read(m_settings)));
    }

    // The selected UI theme lives in ThemeController (key ui/theme), not in
    // Settings, so include it explicitly.
    if (auto *tc = ThemeController::instance())
        s.insert(QStringLiteral("themeName"), tc->themeName());

    const QJsonObject root{
        {QStringLiteral("app"), QStringLiteral("unisic")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("settings"), s},
        {QStringLiteral("destinations"), m_uploads->destinationsJson()},
    };
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return tr("Cannot write %1").arg(path);
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    showToast(tr("Settings exported to %1").arg(path));
    return {};
}

QString AppContext::importSettings(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return tr("Cannot read %1").arg(path);
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.value(QStringLiteral("app")).toString() != QLatin1String("unisic"))
        return tr("Not a Unisic settings file");

    const QJsonObject s = root.value(QStringLiteral("settings")).toObject();
    const QMetaObject *mo = m_settings->metaObject();
    for (auto it = s.begin(); it != s.end(); ++it) {
        if (it.key() == QLatin1String("themeName")) {
            if (auto *tc = ThemeController::instance())
                tc->setThemeName(it.value().toString());
            continue;
        }
        const int idx = mo->indexOfProperty(it.key().toLatin1().constData());
        if (idx >= mo->propertyOffset())
            mo->property(idx).write(m_settings, it.value().toVariant());
        else if (it.key().contains(QLatin1Char('/')))
            m_settings->raw()->setValue(it.key(), it.value().toVariant()); // legacy raw keys
    }
    m_settings->raw()->sync();

    if (root.value(QStringLiteral("destinations")).isArray())
        m_uploads->replaceAllDestinations(root.value(QStringLiteral("destinations")).toArray());

    m_settings->notifyAll();
    applyHotkeys();
    showToast(tr("Settings imported"));
    return {};
}

// ---------------------------------------------------------------- shell

// Called once at startup: register each action + its default, with autoloading
// so a key edited in KDE's Shortcuts KCM is honored and not overwritten.
void AppContext::defineHotkeys()
{
    m_hotkeys->defineAction(QStringLiteral("capture-fullscreen"), tr("Capture full screen"),
                            m_settings->hotkeyFullScreen());
    m_hotkeys->defineAction(QStringLiteral("capture-region"), tr("Capture region"),
                            m_settings->hotkeyRegion());
    m_hotkeys->defineAction(QStringLiteral("capture-window"), tr("Capture active window"),
                            m_settings->hotkeyWindow());
    m_hotkeys->defineAction(QStringLiteral("record-gif"), tr("Record GIF (start/stop)"),
                            m_settings->hotkeyGif());
    m_hotkeys->defineAction(QStringLiteral("record-video"), tr("Record video (start/stop)"),
                            m_settings->hotkeyRecord());
}

// Called when the user changes a shortcut in Unisic's own settings (or on
// import): push the chosen keys to KGlobalAccel so the DE reflects the change.
void AppContext::applyHotkeys()
{
    m_hotkeys->setShortcut(QStringLiteral("capture-fullscreen"), tr("Capture full screen"),
                           m_settings->hotkeyFullScreen());
    m_hotkeys->setShortcut(QStringLiteral("capture-region"), tr("Capture region"),
                           m_settings->hotkeyRegion());
    m_hotkeys->setShortcut(QStringLiteral("capture-window"), tr("Capture active window"),
                           m_settings->hotkeyWindow());
    m_hotkeys->setShortcut(QStringLiteral("record-gif"), tr("Record GIF (start/stop)"),
                           m_settings->hotkeyGif());
    m_hotkeys->setShortcut(QStringLiteral("record-video"), tr("Record video (start/stop)"),
                           m_settings->hotkeyRecord());
}

void AppContext::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/resources/icons/unisic.svg")), this);
    auto *menu = new QMenu;
    menu->addAction(tr("Capture region"), this, &AppContext::captureRegion);
    menu->addAction(tr("Capture full screen"), this, &AppContext::captureFullScreen);
    menu->addAction(tr("Capture window"), this, &AppContext::captureWindow);
    menu->addSeparator();
    menu->addAction(tr("Record video (region)"), this, &AppContext::startVideoRegion);
    menu->addAction(tr("Record video (window)"), this, &AppContext::startVideoWindow);
    menu->addAction(tr("Record GIF (region)"), this, &AppContext::startGifRegion);
    menu->addAction(tr("Stop recording"), this, &AppContext::stopRecording);
    menu->addSeparator();
    menu->addAction(tr("Open Unisic"), this, [this] { emit showMainWindowRequested(); });
    menu->addAction(tr("Quit"), qApp, &QCoreApplication::quit);
    m_tray->setContextMenu(menu);
    m_tray->setToolTip(QStringLiteral("Unisic"));
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger)
            emit showMainWindowRequested();
    });
    m_tray->show();
}
