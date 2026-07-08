#include "AppContext.h"
#include "Settings.h"
#include "capture/CaptureManager.h"
#include "capture/KWinScreenShot2.h"
#include "overlay/OverlayController.h"
#include "upload/UploadManager.h"
#include "history/HistoryStore.h"
#include "hotkeys/GlobalHotkeys.h"
#include "hotkeys/PortalGlobalShortcuts.h"
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
#include <QFileDialog>
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
#include <QStandardPaths>
#include <QtConcurrentRun>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusConnectionInterface>
#include <QDebug>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

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
    connect(m_hotkeys, &GlobalHotkeys::activated, this, &AppContext::dispatchHotkey);
    // Live two-way sync: a KCM edit updates the app's stored/displayed key.
    connect(m_hotkeys, &GlobalHotkeys::shortcutChanged, this,
            &AppContext::syncHotkeyFromDaemon);

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
            showToast(tr("Recording failed: %1").arg(e), true);
    });

    // A history file that could not be trashed still gets its entry removed;
    // let the user know the file is still on disk.
    connect(m_history, &HistoryStore::fileTrashFailed, this, [this](const QString &path) {
        showToast(tr("Could not move %1 to trash — the file is still on disk").arg(path), true);
    });

#ifdef HAVE_TESSERACT
    m_ocr = new OcrEngine(this);
#endif
}

AppContext::~AppContext()
{
    // Keep registered shortcuts so they survive restarts (KGlobalAccel autoloads them).
    delete m_trayMenu; // QSystemTrayIcon::setContextMenu doesn't take ownership
}

void AppContext::initialize(QQmlEngine *engine)
{
    m_engine = engine;
    setupTray();
    // Deferred to the first event-loop pass: hotkey registration is a burst of
    // blocking D-Bus round-trips (2 s timeout each) — after the QML engine has
    // loaded the window instead of before it, and late enough that startup
    // toasts (e.g. a Ctrl+Esc conflict) have a UI to appear in.
    QTimer::singleShot(0, this, &AppContext::defineHotkeys);

#ifdef HAVE_PIPEWIRE
    // Async probe: is a ScreenCast portal backend actually present? (-xapp and
    // -lxqt desktops have none.) Optimistic until the reply lands.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << QStringLiteral("org.freedesktop.portal.ScreenCast") << QStringLiteral("version");
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const bool present = !w->isError();
        w->deleteLater();
        if (m_screenCastPortalPresent != present) {
            m_screenCastPortalPresent = present;
            emit recordingAvailableChanged();
            if (!present)
                qWarning() << "No ScreenCast portal backend on this desktop — recording disabled"
                              " (install a backend such as xdg-desktop-portal-wlr/-kde/-gnome)";
        }
    });
#endif
}

void AppContext::dispatchHotkey(const QString &action)
{
    // Emergency stop first, and NOT behind the shortcut-recorder guard:
    // it must fire even while the settings UI is capturing a key press.
    if (action == QLatin1String("stop-recording")) {
        if (recording())
            stopRecording();
        return;
    }
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
}

bool AppContext::recording() const { return m_recorder->recording(); }
bool AppContext::converting() const { return m_converting; }
int AppContext::recordSeconds() const { return m_recorder->elapsedSeconds(); }

bool AppContext::recordingAvailable() const
{
#ifdef HAVE_PIPEWIRE
    // Compile-time PipeWire support AND a runtime ScreenCast portal backend —
    // Cinnamon/MATE/XFCE (-xapp) and LXQt ship none, so the record UI must say
    // so instead of failing with a raw D-Bus error.
    return m_screenCastPortalPresent;
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

bool AppContext::hotkeysAvailable() const
{
    return !m_hotkeyBackend.isEmpty();
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

void AppContext::showToast(const QString &text, bool important)
{
    if (!important && !m_settings->showNotifications())
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

// Appends actionable, DESKTOP-AWARE guidance when the failure looks like the
// classic "unauthorized run / missing backend" situation. The old text sent
// everyone to KDE tools — useless advice on GNOME or sway.
QString AppContext::captureErrorGuidance(const QString &err)
{
    QString text = tr("Capture failed: %1").arg(err);
    if (!(err.contains(QLatin1String("portal"), Qt::CaseInsensitive)
          || err.contains(QLatin1String("NoAuthorized"))
          || err.contains(QLatin1String("denied"), Qt::CaseInsensitive)))
        return text;
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    if (desktop.contains(QLatin1String("KDE"), Qt::CaseInsensitive))
        text += tr(" — install Unisic (sudo cmake --install build) and launch it from the "
                   "application menu so KDE authorizes it, and check that "
                   "xdg-desktop-portal-kde is running.");
    else if (desktop.contains(QLatin1String("GNOME"), Qt::CaseInsensitive))
        text += tr(" — allow screenshots for Unisic in GNOME Settings → Apps, and check that "
                   "xdg-desktop-portal-gnome is running.");
    else if (!err.contains(QLatin1String("grim")))
        // The capture chain's own rescue may already carry grim advice
        // (with per-desktop rationale) — don't tell the user twice.
        text += tr(" — install 'grim' (works on sway/niri/Hyprland-style compositors) or an "
                   "xdg-desktop-portal backend for your desktop.");
    return text;
}

void AppContext::captureFullScreen()
{
    if (m_captureInFlight)
        return; // hammering the hotkey must not stack portal requests
    m_captureInFlight = true;
    withDelay([this] {
        m_capture->captureWorkspace([this](const QImage &img, const QString &err) {
            m_captureInFlight = false;
            if (!err.isEmpty()) {
                if (err != QLatin1String("cancelled"))
                    showToast(captureErrorGuidance(err), true);
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
    if (m_captureInFlight)
        return;
    m_captureInFlight = true;
    withDelay([this] {
        m_capture->captureActiveWindow([this](const QImage &img, const QString &err) {
            m_captureInFlight = false;
            if (!err.isEmpty()) {
                if (err != QLatin1String("cancelled"))
                    showToast(captureErrorGuidance(err), true);
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
    const QString kind = path.endsWith(QLatin1String(".gif")) ? QStringLiteral("gif")
                                                              : QStringLiteral("video");
    if (kind == QLatin1String("video")) {
        // QImage has no mp4/webm plugin — extract a poster frame via ffmpeg,
        // else every video gets a blank thumbnail in history and the popup.
        const QString posterPath = path + QStringLiteral(".poster.png");
        auto *proc = new QProcess(this);
        connect(proc, &QProcess::finished, this, [this, proc, path, kind, posterPath](int, QProcess::ExitStatus) {
            proc->deleteLater();
            QImage thumb(posterPath);
            QFile::remove(posterPath);
            finishRecordingEntry(path, thumb, kind);
        });
        connect(proc, &QProcess::errorOccurred, this, [this, proc, path, kind](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart)
                return;
            proc->deleteLater();
            finishRecordingEntry(path, QImage(), kind);
        });
        proc->start(QStringLiteral("ffmpeg"),
                    {QStringLiteral("-y"), QStringLiteral("-nostats"),
                     QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-i"), path,
                     QStringLiteral("-frames:v"), QStringLiteral("1"), posterPath});
        return;
    }
    QImage thumb(path); // first GIF frame loads fine via Qt's gif plugin
    finishRecordingEntry(path, thumb, kind);
}

void AppContext::finishRecordingEntry(const QString &path, const QImage &thumb, const QString &kind)
{
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
                showToast(tr("Upload failed: %1").arg(err), true);
                if (np) np->setUploading(false);
            }
        });
    }

    scheduleMemoryTrim();
}

// ----------------------------------------------------------- after-capture

// Every enabled action runs immediately and independently the moment the
// capture lands — the editor no longer swallows the pipeline.
void AppContext::finishCapture(const QImage &img)
{
    if (img.isNull())
        return;

    // One name per capture: save and upload must agree (a second-boundary or
    // %rand% template would otherwise produce two different names).
    const QString fileName = makeFileName();
    QString path;
    if (m_settings->autoSave())
        path = saveImageAuto(img, fileName);
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
        m_uploads->uploadData(data, fileName, mime,
            [this, path, img, np](const QString &url, const QString &del, const QString &err) {
                if (!err.isEmpty()) {
                    if (path.isEmpty())
                        m_history->addEntry({}, img, QStringLiteral("image"));
                    showToast(tr("Upload failed: %1").arg(err), true);
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

    scheduleMemoryTrim();
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
    if (!m_engine)
        return;
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
        ++m_editorWindows;
        emit editorWindowsOpenChanged();
        connect(win, &QQuickWindow::visibleChanged, session, [this, session, win](bool v) {
            if (!v) {
                win->deleteLater(); session->deleteLater(); scheduleMemoryTrim();
                --m_editorWindows;
                emit editorWindowsOpenChanged();
            }
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
                showToast(tr("Upload failed: %1").arg(err), true);
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
    // Prefer the screen under the cursor (where the user is working); the
    // primary screen is only the fallback when the cursor position is unknown.
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect g = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
    const QRect avail = screen ? screen->availableGeometry() : g;
    const int cardW = 400, cardH = 150, margin = 16;

    // The fullscreen-transparent-window trick is KWin-only: KWin honors the
    // input mask and composits the transparent remainder invisibly. On
    // wlroots a fullscreen view BLANKS the workspace beneath it after every
    // capture, and on Mutter it animates/joins alt-tab — there a small
    // compositor-placed toplevel is the sane behavior (24px transparent
    // padding keeps the drop shadow).
    //
    // Detect KWin by its D-Bus name, NOT by XDG_CURRENT_DESKTOP: that env var
    // is absent in several legitimate launch contexts (systemd user-service
    // autostart, minimal-env launchers, some AppImage runs), and when it was,
    // this fell to the card-sized branch — which KWin then CENTERS (Wayland
    // ignores client positions), so the popup landed in the middle of the
    // screen. The org.kde.KWin NAME is visible even inside Flatpak (only the
    // restricted ScreenShot2 *interface* is access-gated), so this is reliable
    // where KWinScreenShot2::isAvailable() (Flatpak short-circuit) is not.
    auto *busIface = QDBusConnection::sessionBus().interface();
    const bool fullscreenTrick =
        busIface && busIface->isServiceRegistered(QStringLiteral("org.kde.KWin"));
    const int pad = 24;

    const QString posName = m_settings->capturePopupPosition();
    const bool top = posName.startsWith(QLatin1String("top"));
    const bool left = posName.endsWith(QLatin1String("left"));
    const bool right = posName.endsWith(QLatin1String("right"));
    // Card position in window-local coords (window origin == screen top-left),
    // kept inside the available area so it clears panels.
    const int ax = avail.x() - g.x(), ay = avail.y() - g.y();
    const int px = !fullscreenTrick ? pad
                 : left  ? ax + margin
                 : right ? ax + avail.width() - cardW - margin
                 :         ax + (avail.width() - cardW) / 2;
    const int py = !fullscreenTrick ? pad
                 : top ? ay + margin
                 :       ay + avail.height() - cardH - margin;

    // Only one popup at a time: retire the previous one so its full-resolution
    // image and screen-sized window don't linger (matters when auto-hide is 0).
    if (m_notifWindow)
        m_notifWindow->close();

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
    connect(win, &QQuickWindow::visibleChanged, notif, [this, win, notif, ctx](bool v) {
        if (!v) {
            win->deleteLater(); notif->deleteLater(); ctx->deleteLater();
            scheduleMemoryTrim(); // full image + popup window just went away
        }
    });
    m_notifWindow = win;

    if (screen)
        win->setScreen(screen);
    if (fullscreenTrick) {
        win->setGeometry(g);
        // Create the platform surface up front so the input mask is applied
        // BEFORE the window is mapped — the transparent remainder is
        // click-through from the very first frame.
        win->create();
        win->setMask(QRegion(px, py, cardW, cardH));
        // showFullScreen, not show(): a plain toplevel gets placed by the
        // compositor's policy (KWin offsets it into the work area, e.g. below
        // a top panel), which shifts the whole surface and cuts the card off
        // at the screen edge. Fullscreen is the one state where the surface is
        // pinned exactly to the screen origin — the same trick as the overlay.
        // Focus is still never stolen: WindowDoesNotAcceptFocus is set in QML.
        win->showFullScreen();
    } else {
        // Card-sized toplevel; the compositor decides placement. FIXED size:
        // sway/i3-style tilers float fixed-size windows instead of tiling a
        // half-screen transparent surface into the user's layout. The input
        // mask keeps the 24px shadow ring click-through.
        const QSize cardWin(cardW + 2 * pad, cardH + 2 * pad);
        win->setMinimumSize(cardWin);
        win->setMaximumSize(cardWin);
        win->resize(cardWin);
        win->create();
        win->setMask(QRegion(pad, pad, cardW, cardH));
        win->show();
    }
    return notif;
}

void AppContext::scheduleMemoryTrim()
{
#if defined(__GLIBC__)
    if (!m_trimTimer) {
        m_trimTimer = new QTimer(this);
        m_trimTimer->setSingleShot(true);
        m_trimTimer->setInterval(4000); // debounce bursts of captures
        connect(m_trimTimer, &QTimer::timeout, this, [] { malloc_trim(0); });
    }
    m_trimTimer->start();
#endif
}

QString AppContext::saveImageAuto(const QImage &img, const QString &fileName)
{
    return saveImageTo(img, m_settings->saveDirectory(), fileName);
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

QString AppContext::saveImageTo(const QImage &img, const QString &dir, const QString &fileName)
{
    if (dir.isEmpty() || img.isNull())
        return {};
    QDir().mkpath(dir);
    const QString name = fileName.isEmpty() ? makeFileName() : fileName;
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

// Resolved once — the old `sh -c "command -v wl-copy"` was a blocking
// fork/exec on the GUI thread on the hot path of every capture.
static QString wlCopyPath()
{
    static const QString path = QStandardPaths::findExecutable(QStringLiteral("wl-copy"));
    return path;
}

static void spawnWlCopy(AppContext *app, const QString &wlCopy, const QStringList &args,
                        const QByteArray &payload)
{
    auto *proc = new QProcess(app);
    QObject::connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
    // finished() never fires on FailedToStart — without this the process
    // object (holding the payload in its write buffer) lingers until exit.
    QObject::connect(proc, &QProcess::errorOccurred, proc, &QObject::deleteLater);
    proc->start(wlCopy, args);
    proc->write(payload);
    proc->closeWriteChannel();
}

void AppContext::copyImageToClipboard(const QImage &img)
{
    QGuiApplication::clipboard()->setImage(img);
    // Wayland: clipboard offers can be lost when no window has focus.
    // wl-copy (if present) makes it stick regardless. NOT under XWayland:
    // there Qt owns the X11 CLIPBOARD while wl-copy would set a second,
    // separate Wayland selection — two clipboards fighting.
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return; // includes "wayland-egl"; excludes xcb/XWayland
    const QString wlCopy = wlCopyPath();
    if (wlCopy.isEmpty())
        return;
    // PNG-encoding a 4K capture takes 100+ ms — keep it off the GUI thread.
    // QImage is implicitly shared and the worker only reads its copy.
    QPointer<AppContext> self(this);
    (void)QtConcurrent::run([self, img, wlCopy] {
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        // QProcess must live on the GUI thread.
        QMetaObject::invokeMethod(qApp, [self, png, wlCopy] {
            if (self)
                spawnWlCopy(self, wlCopy, {QStringLiteral("--type"), QStringLiteral("image/png")}, png);
        }, Qt::QueuedConnection);
    });
}

void AppContext::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return;
    const QString wlCopy = wlCopyPath();
    if (!wlCopy.isEmpty()) {
        auto *proc = new QProcess(this);
        connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
        connect(proc, &QProcess::errorOccurred, proc, &QObject::deleteLater);
        proc->start(wlCopy, {});
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
    const QString dir = path.isEmpty() ? m_settings->saveDirectory() : path;
    // On a fresh profile nothing has saved yet, so the default save dir may
    // not exist (defaultSaveDir deliberately has no mkpath side effect).
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

// --------------------------------------------------------- export / import

void AppContext::exportSettingsDialog()
{
    // Native picker: QFileDialog with the platform theme (KDE plasma-integration
    // / the portal on other DEs) is the desktop's own file dialog — the QML
    // FileDialog fell back to the Basic-styled Qt Quick dialog here.
    const QString path = QFileDialog::getSaveFileName(
        nullptr, tr("Export Unisic settings"),
        QDir::homePath() + QStringLiteral("/unisic-settings.json"),
        tr("Unisic settings (*.json)"));
    if (path.isEmpty())
        return; // cancelled
    const QString err = exportSettings(QUrl::fromLocalFile(path));
    showToast(err.isEmpty() ? tr("Settings exported") : err, !err.isEmpty());
}

void AppContext::importSettingsDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        nullptr, tr("Import Unisic settings"), QDir::homePath(),
        tr("Unisic settings (*.json)"));
    if (path.isEmpty())
        return; // cancelled
    const QString err = importSettings(QUrl::fromLocalFile(path));
    showToast(err.isEmpty() ? tr("Settings imported") : err, !err.isEmpty());
}

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

QVector<AppContext::HotkeyAction> AppContext::hotkeyActions() const
{
    return {
        {QStringLiteral("capture-fullscreen"), tr("Capture full screen"), m_settings->hotkeyFullScreen()},
        {QStringLiteral("capture-region"), tr("Capture region"), m_settings->hotkeyRegion()},
        {QStringLiteral("capture-window"), tr("Capture active window"), m_settings->hotkeyWindow()},
        {QStringLiteral("record-gif"), tr("Record GIF (start/stop)"), m_settings->hotkeyGif()},
        {QStringLiteral("record-video"), tr("Record video (start/stop)"), m_settings->hotkeyRecord()},
    };
}

// Daemon-authoritative display: whatever key is ACTUALLY bound is what the
// settings UI must show — the stored string is just the app's last wish.
void AppContext::syncHotkeyFromDaemon(const QString &actionId, const QString &portable)
{
    if (actionId == QLatin1String("capture-fullscreen")) m_settings->setHotkeyFullScreen(portable);
    else if (actionId == QLatin1String("capture-region")) m_settings->setHotkeyRegion(portable);
    else if (actionId == QLatin1String("capture-window")) m_settings->setHotkeyWindow(portable);
    else if (actionId == QLatin1String("record-gif")) m_settings->setHotkeyGif(portable);
    else if (actionId == QLatin1String("record-video")) m_settings->setHotkeyRecord(portable);
    else return;
    // Rare + important: flush so a SIGTERM/logout doesn't resurrect the stale key.
    m_settings->raw()->sync();
}

void AppContext::syncAllHotkeysFromDaemon()
{
    if (!m_hotkeys->available())
        return;
    const auto acts = hotkeyActions();
    for (const HotkeyAction &a : acts) {
        bool ok = false;
        const QString actual = m_hotkeys->activeKeysPortable(a.id, &ok);
        // A failed/timed-out query must NOT be mistaken for "unbound" — that
        // would wipe the stored key (and the sync() below persists the wipe).
        if (ok && actual != a.keys)
            syncHotkeyFromDaemon(a.id, actual);
    }
}

// Called once at startup (deferred past engine load): register each action +
// its default with autoloading so a key edited in KDE's Shortcuts KCM is
// honored, then pick the portal backend when KGlobalAccel isn't the answer.
void AppContext::defineHotkeys()
{
    const QVector<HotkeyAction> acts = hotkeyActions();

    if (m_hotkeys->available()) {
        m_hotkeyBackend = QStringLiteral("kglobalaccel");
        // The registration reply already carries each action's ACTIVE keys —
        // reuse it for both the bootstrap check and the KCM-drift sync
        // instead of 10 extra blocking shortcutKeys round-trips.
        struct RegResult { QList<int> keys; bool ok = false; };
        QHash<QString, RegResult> active;
        for (const HotkeyAction &a : acts) {
            RegResult r;
            m_hotkeys->defineAction(a.id, a.name, a.keys, &r.keys, &r.ok);
            active.insert(a.id, r);
        }

        // Self-heal for fresh installs: our reference kglobalacceld binds both
        // the active and default columns on an IsDefault registration, but
        // daemons in the field have been seen applying it to the default
        // column only — leaving every action registered yet UNBOUND (hotkeys
        // silently dead). Verify once per install; never repeated afterwards,
        // so deliberately unbinding a key in the KCM still sticks.
        if (!m_settings->raw()->value(QStringLiteral("hotkeys/bootstrapped")).toBool()) {
            for (const HotkeyAction &a : acts) {
                const RegResult &r = active.value(a.id);
                if (r.ok && r.keys.isEmpty() && !a.keys.isEmpty()) {
                    qWarning() << "Hotkey" << a.id << "registered but left unbound by the daemon"
                               << "— forcing default" << a.keys;
                    m_hotkeys->setShortcut(a.id, a.name, a.keys);
                }
            }
            m_settings->raw()->setValue(QStringLiteral("hotkeys/bootstrapped"), true);
            // Flush now: a SIGTERM/logout kill skips destructors, and losing
            // the flag would re-force defaults on every launch (breaking
            // deliberate KCM unbinds).
            m_settings->raw()->sync();
        }
        // Fixed emergency stop: ALWAYS Ctrl+Escape, not user-configurable.
        // Pushed with SetPresent|NoAutoloading on every startup, so even a KCM
        // edit is reverted at the next launch. Stock Plasma ships Ctrl+Esc
        // bound to "Show System Activity" — the daemon then refuses the grab,
        // so tell the user instead of failing silently.
        if (!m_hotkeys->setShortcut(QStringLiteral("stop-recording"),
                                    tr("Stop recording (emergency)"),
                                    QStringLiteral("Ctrl+Escape"))) {
            qWarning() << "Ctrl+Escape emergency stop could not be bound (owned by another"
                          " component — on stock Plasma: Show System Activity)";
            showToast(tr("Ctrl+Esc emergency stop unavailable — the key is taken by the system "
                         "(System Settings → Shortcuts to free it)"));
        }
        // The daemon may have autoloaded KCM-edited keys that differ from our
        // stored strings — reflect reality in the UI (from the registration
        // replies; no extra queries). An error/timeout must never be treated
        // as "unbound" — that would wipe (and sync()-persist) the stored key.
        for (const HotkeyAction &a : acts) {
            const RegResult &r = active.value(a.id);
            if (!r.ok)
                continue;
            const QString actual = GlobalHotkeys::portableFromKeys(r.keys);
            if (actual != a.keys)
                syncHotkeyFromDaemon(a.id, actual);
        }
        emit hotkeysAvailableChanged();
        return;
    }

    // Non-KDE: the GlobalShortcuts portal (GNOME 48+, Hyprland, …). The
    // interface can be present yet backed by a broken impl (xdp-gnome's is
    // hardwired to org.gnome.Shell), so the bind response is the real test.
    if (PortalGlobalShortcuts::interfacePresent()) {
        m_portalHotkeys = new PortalGlobalShortcuts(this);
        connect(m_portalHotkeys, &PortalGlobalShortcuts::activated,
                this, &AppContext::dispatchHotkey);
        connect(m_portalHotkeys, &PortalGlobalShortcuts::bindFinished, this,
                [this](bool ok, const QVariantMap &) {
            const QString wanted = ok ? QStringLiteral("portal") : QString();
            if (m_hotkeyBackend != wanted) {
                m_hotkeyBackend = wanted;
                emit hotkeysAvailableChanged();
            }
            if (!ok)
                qWarning() << "GlobalShortcuts portal exists but has no working backend here"
                              " — falling back to compositor-binds guidance";
        });
        // Optimistic until the response lands — avoids flashing the
        // "unavailable" card during the round-trip.
        m_hotkeyBackend = QStringLiteral("portal");
        emit hotkeysAvailableChanged();
        bindPortalHotkeys();
        return;
    }

    m_hotkeyBackend.clear();
    emit hotkeysAvailableChanged();
}

void AppContext::bindPortalHotkeys()
{
    if (!m_portalHotkeys)
        return;
    QVector<PortalGlobalShortcuts::Shortcut> list;
    const auto acts = hotkeyActions();
    for (const HotkeyAction &a : acts)
        list.append({a.id, a.name, PortalGlobalShortcuts::toPortalTrigger(a.keys)});
    list.append({QStringLiteral("stop-recording"), tr("Stop recording (emergency)"),
                 QStringLiteral("CTRL+Escape")});
    m_portalHotkeys->bind(list);
}

// Push ONE action's stored key to the system. KGlobalAccel: setShortcut with
// SetPresent|NoAutoloading, conflict surfaced as a toast + the daemon's actual
// key synced back into the UI. Portal: re-bind the whole set (the portal has
// no per-shortcut rebind; unchanged sets don't re-prompt on KDE/GNOME).
void AppContext::applyHotkey(const QString &actionId)
{
    if (m_portalHotkeys && m_hotkeyBackend == QLatin1String("portal")) {
        bindPortalHotkeys();
        return;
    }
    if (!m_hotkeys->available())
        return;
    const auto acts = hotkeyActions();
    for (const HotkeyAction &a : acts) {
        if (a.id != actionId)
            continue;
        if (!m_hotkeys->setShortcut(a.id, a.name, a.keys)) {
            showToast(tr("Could not bind %1 — the key is taken by another shortcut").arg(a.keys),
                      true);
            // Show what is actually bound instead of the refused wish.
            bool ok = false;
            const QString actual = m_hotkeys->activeKeysPortable(a.id, &ok);
            if (ok)
                syncHotkeyFromDaemon(a.id, actual);
        }
        return;
    }
}

// Bulk push (settings import, the explicit "Apply hotkeys" button): the app's
// stored keys are the user's intent here, so all five are asserted.
void AppContext::applyHotkeys()
{
    if (m_portalHotkeys && m_hotkeyBackend == QLatin1String("portal")) {
        bindPortalHotkeys();
        return;
    }
    if (!m_hotkeys->available())
        return;
    const auto acts = hotkeyActions();
    bool allOk = true;
    for (const HotkeyAction &a : acts)
        allOk &= m_hotkeys->setShortcut(a.id, a.name, a.keys);
    if (!allOk) {
        showToast(tr("Some hotkeys could not be bound (keys taken) — showing the actual state"),
                  true);
        syncAllHotkeysFromDaemon();
    }
}

void AppContext::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        // The StatusNotifier host can appear AFTER us (plasmashell/waybar
        // still starting, GNOME extension loading late) — watch for it and
        // build the tray then. Until it exists, trayAvailable stays false and
        // closing the window really closes it (no vanish-into-nothing trap).
        // ONE watcher for the app's lifetime: retries re-enter this branch and
        // must not stack additional watchers/match rules.
        if (m_trayWatcher)
            return;
        m_trayWatcher = new QDBusServiceWatcher(QStringLiteral("org.kde.StatusNotifierWatcher"),
                                                QDBusConnection::sessionBus(),
                                                QDBusServiceWatcher::WatchForRegistration, this);
        connect(m_trayWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
            // The HOST routinely lags the watcher name (waybar/extension
            // startup) and isSystemTrayAvailable() needs the host — poll a
            // few times instead of giving up after one shot.
            auto *retry = new QTimer(this);
            retry->setInterval(2000);
            auto attempts = std::make_shared<int>(0);
            connect(retry, &QTimer::timeout, this, [this, retry, attempts] {
                if (m_tray || ++*attempts > 15) {
                    retry->deleteLater();
                    return;
                }
                setupTray();
            });
            retry->start();
            if (!m_tray)
                setupTray();
        });
        return;
    }
    if (m_trayWatcher) {
        // deleteLater, not delete: this path is reachable from inside the
        // watcher's own serviceRegistered emission.
        m_trayWatcher->deleteLater();
        m_trayWatcher = nullptr;
    }
    m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/resources/icons/unisic.svg")), this);
    auto *menu = new QMenu;
    m_trayMenu = menu;
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
    emit trayAvailableChanged();
}
