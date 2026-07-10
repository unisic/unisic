#include "AppContext.h"
#include "Settings.h"
#include "capture/CaptureManager.h"
#include "capture/KWinScreenShot2.h"
#include "overlay/OverlayController.h"
#include "overlay/ObjectDetector.h"
#include "upload/UploadManager.h"
#include "history/HistoryStore.h"
#include "hotkeys/GlobalHotkeys.h"
#include "hotkeys/ShortcutFormat.h"
#include "hotkeys/PortalGlobalShortcuts.h"
#include "record/GifRecorder.h"
#include "editor/EditorSession.h"
#include "PreviewController.h"
#include "notify/CaptureNotification.h"
#include "notify/DesktopNotifier.h"
#ifdef HAVE_LAYERSHELL
#include "notify/LayerShellNotifier.h"
#include <LayerShellQt/window.h>
#include <QMargins>
#endif
#include "theme/ThemeController.h"
#ifdef HAVE_TESSERACT
#include "ocr/OcrEngine.h"
#endif
#include <QGuiApplication>
#include <QtMath>
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
#include <QIcon>
#include <QSize>
#include <QColor>
#include <QPainter>
#include <QImageReader>
#include <QStyleHints>
#include <QFileSystemWatcher>
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
#include <QTemporaryFile>
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
    m_notifier = new DesktopNotifier(this, this);

    connect(m_hotkeys, &GlobalHotkeys::activated, this, &AppContext::dispatchHotkey);
    // Live two-way sync: a KCM edit updates the app's stored/displayed key.
    connect(m_hotkeys, &GlobalHotkeys::shortcutChanged, this,
            &AppContext::syncHotkeyFromDaemon);

    connect(m_recorder, &GifRecorder::started, this, &AppContext::recordingChanged);
    // Badge/unbadge the tray icon as recording starts and stops.
    connect(this, &AppContext::recordingChanged, this, &AppContext::applyTrayIcon);
    connect(m_recorder, &GifRecorder::started, this, [this] {
        // Region recordings carry a pending rect. Gate on the LIVE recording
        // actually being a Region one: a stale pending rect (set by a region
        // callback whose start() no-op'd because another recording had already
        // begun) must never frame a full-screen/window recording — that frame
        // would be baked into the output.
        if (m_recorder->sourceType() == GifRecorder::Region && !m_pendingRecordRegion.isEmpty())
            showRecordBorder(m_pendingRecordRegion, m_pendingRecordScreen);
    });
    connect(m_recorder, &GifRecorder::elapsedChanged, this, &AppContext::recordSecondsChanged);
    connect(m_recorder, &GifRecorder::converting, this, [this] {
        m_converting = true;
        hideRecordBorder(); // capture is over; the frame must not linger over encoding
        emit recordingChanged();
        showToast(tr("Encoding…"));
    });
    connect(m_recorder, &GifRecorder::finished, this, &AppContext::onRecordingFinished);
    connect(m_recorder, &GifRecorder::failed, this, [this](const QString &e) {
        m_converting = false;
        hideRecordBorder();
        emit recordingChanged();
        if (e != QLatin1String("cancelled"))
            showToast(tr("Recording failed: %1").arg(e), true);
    });

    // A history file that could not be trashed still gets its entry removed;
    // let the user know the file is still on disk.
    connect(m_history, &HistoryStore::fileTrashFailed, this, [this](const QString &path) {
        showToast(tr("Could not move %1 to trash — the file is still on disk").arg(path), true);
    });

    // Live-apply a custom tray icon the moment the setting changes (also covers
    // an import that rewrites trayIconPath).
    connect(m_settings, &Settings::trayIconPathChanged, this, &AppContext::applyTrayIcon);

    // Follow the OS light/dark scheme: recolor the (monochrome) bundled preset
    // in the tray, and let the settings gallery re-render its thumbnails.
    if (auto *hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this] {
            emit trayContrastColorChanged();
            applyTrayIcon();
        });
    }

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
    refreshAutostartIfStale();

#ifdef HAVE_LAYERSHELL
    // Detect layer-shell ONCE — it drives the on-top custom capture card, the
    // record-region border (so it works beyond KWin: wlroots, COSMIC…), and the
    // preview window. Elsewhere (GNOME, X11) these fall back or are unsupported.
    m_layerShellAvailable = QGuiApplication::platformName().startsWith(QLatin1String("wayland"))
                            && LayerShellNotifier::compositorSupportsLayerShell();
    if (m_layerShellAvailable)
        m_layerNotifier = new LayerShellNotifier(this, this);
#endif

    // Drop-in tray-icon folder: create it so it's discoverable and watch it so
    // the settings gallery live-updates when the user adds/removes an icon.
    QDir().mkpath(trayIconsDir());
    m_trayIconsWatcher = new QFileSystemWatcher(this);
    m_trayIconsWatcher->addPath(trayIconsDir());
    connect(m_trayIconsWatcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        emit trayIconPresetsChanged();
        // If the currently-selected custom icon was just deleted, trayIcon()
        // now re-validates to the bundled default — refresh the live tray too.
        applyTrayIcon();
    });
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
    // Quick-copy grace window: time-sensitive and unrelated to captures, so it
    // fires even mid shortcut-recording.
    if (action == QLatin1String("quick-copy")) {
        if (m_quickCopyArmed && !m_quickCopyImage.isNull()) {
            copyImageToClipboard(m_quickCopyImage);
            showToast(tr("Copied to clipboard"));
        }
        disarmQuickCopy();
        return;
    }
    if (m_shortcutRecording)
        return;
    if (action == QLatin1String("capture-fullscreen")) captureFullScreen();
    else if (action == QLatin1String("capture-region")) captureRegion();
    else if (action == QLatin1String("capture-window")) captureWindow();
    else if (action == QLatin1String("ocr-region")) captureRegionOcr();
    else if (action == QLatin1String("record-gif")) {
        if (recording()) stopRecording();
        else startGifRegion();
    } else if (action == QLatin1String("record-video")) {
        if (recording()) stopRecording();
        else startVideoRegion();
    } else if (action == QLatin1String("smoke-test")) {
        if (devBuild()) runSmokeTest();
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

bool AppContext::qrAvailable() const
{
#ifdef HAVE_ZXING
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

QString AppContext::formatShortcut(int key, int modifiers, int nativeScanCode) const
{
    // Header-only helper so the Shift+digit unshift logic is unit-testable
    // (see tests/ShortcutFormatTest.cpp).
    return ShortcutFormat::portable(key, modifiers, nativeScanCode);
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

bool AppContext::nowInhibited() const
{
    return m_notifier && m_notifier->inhibited();
}

void AppContext::captureFullScreen()
{
    // In-flight guard: hammering the hotkey must not stack portal requests.
    // Overlay guard: with the region-selection overlay open, a stray
    // fullscreen/window hotkey would capture the overlay's own dimming and
    // toolbar and push that garbage through the whole after-capture pipeline.
    if (m_captureInFlight || m_overlay->active())
        return;
    const bool inhibited = nowInhibited();
    m_captureInFlight = true;
    withDelay([this, inhibited] {
        // Re-check: with a capture delay configured, the region overlay may
        // have opened between the keypress and this deferred fire.
        if (m_overlay->active()) {
            m_captureInFlight = false;
            return;
        }
        m_capture->captureWorkspace([this, inhibited](const QImage &img, const QString &err) {
            m_captureInFlight = false;
            if (!err.isEmpty()) {
                if (err != QLatin1String("cancelled"))
                    showToast(captureErrorGuidance(err), true);
                return;
            }
            finishCapture(img, inhibited);
        });
    });
}

void AppContext::captureRegion()
{
    const bool inhibited = nowInhibited(); // before the fullscreen overlay opens
    withDelay([this, inhibited] {
        m_overlay->pickAnnotatedImage([this, inhibited](const QImage &img) {
            if (!img.isNull())
                finishCapture(img, inhibited);
        });
    });
}

void AppContext::captureRegionOcr()
{
    withDelay([this] {
        m_overlay->pickAnnotatedImage([this](const QImage &img) {
            if (!img.isNull())
                ocrImage(img);   // recognizes (QR first, then text) + copies
        });
    });
}

void AppContext::captureWindow()
{
    if (m_captureInFlight || m_overlay->active()) // see captureFullScreen
        return;
    const bool inhibited = nowInhibited();
    m_captureInFlight = true;
    withDelay([this, inhibited] {
        if (m_overlay->active()) { // re-check after the capture delay
            m_captureInFlight = false;
            return;
        }
        m_capture->captureActiveWindow([this, inhibited](const QImage &img, const QString &err) {
            m_captureInFlight = false;
            if (!err.isEmpty()) {
                if (err != QLatin1String("cancelled"))
                    showToast(captureErrorGuidance(err), true);
                return;
            }
            finishCapture(img, inhibited);
        });
    });
}

// ---------------------------------------------------------------- recording

void AppContext::startGifRegion()
{
    if (recording()) return;
    m_overlay->pickRegion([this](const QRect &phys, QScreen *screen) {
        if (phys.isEmpty()) return;
        m_pendingRecordRegion = phys;
        m_pendingRecordScreen = screen;
        m_recorder->start(GifRecorder::Gif, GifRecorder::Region, phys, screen);
    });
}

void AppContext::startGifFullScreen()
{
    if (recording()) return;
    m_pendingRecordRegion = QRect();
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
    m_pendingRecordRegion = QRect();
    m_recorder->start(videoOutput(), GifRecorder::Screen);
}

void AppContext::startVideoRegion()
{
    if (recording()) return;
    m_overlay->pickRegion([this](const QRect &phys, QScreen *screen) {
        if (phys.isEmpty()) return;
        m_pendingRecordRegion = phys;
        m_pendingRecordScreen = screen;
        m_recorder->start(videoOutput(), GifRecorder::Region, phys, screen);
    });
}

void AppContext::startVideoWindow()
{
    if (recording()) return;
    m_pendingRecordRegion = QRect();
    m_recorder->start(videoOutput(), GifRecorder::Window);
}

void AppContext::stopRecording()
{
    m_recorder->stop();
}

bool AppContext::capNativeNotification() const
{
    return DesktopNotifier::available();
}

void AppContext::smokeLog(const QString &line)
{
    qInfo().noquote() << "[smoke]" << line;
    m_smokeLog += line + QLatin1Char('\n');
    emit smokeTestChanged();
}

static QImage devTestImage()
{
    QImage img(320, 200, QImage::Format_ARGB32);
    img.fill(QColor(0x2E, 0x23, 0x6C));
    return img;
}

// Synthetic smart-pick check shared by the smoke test and the dev button: a
// window-like light rect on a dark backdrop must be detected, and the point
// inside it must resolve to a candidate (the exact lookup a click does).
static QString smartPickDetectCheck()
{
    QImage img(400, 300, QImage::Format_ARGB32);
    img.fill(QColor(0x17, 0x15, 0x3B));
    {
        QPainter p(&img);
        p.fillRect(QRect(120, 80, 160, 100), QColor(0xEC, 0xEC, 0xF4));
        p.setPen(QPen(QColor(0x43, 0x3D, 0x8B), 2));
        p.drawRect(QRect(120, 80, 160, 100));
    }
    const QVector<QRect> rects = ObjectDetector::detect(img);
    // The whole image is always a candidate now, so "contains the point" is
    // trivially true — assert the drawn rect was found with ACCURATE edges.
    const QRect want(120, 80, 160, 100);
    for (const QRect &r : rects) {
        if (qAbs(r.left() - want.left()) <= 6 && qAbs(r.top() - want.top()) <= 6
            && qAbs(r.right() - want.right()) <= 6 && qAbs(r.bottom() - want.bottom()) <= 6)
            return QStringLiteral("PASS (%1 candidates, rect within ±6 px)").arg(rects.size());
    }
    return QStringLiteral("FAIL (%1 candidates, none matches the drawn rect)").arg(rects.size());
}

void AppContext::devTestSmartPick()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: smart pick detect — %1").arg(smartPickDetectCheck()));
}

void AppContext::devTestNotification()
{
    if (!devBuild())
        return;
    // Full parity with a real capture — including the gates. A dev test that
    // bypassed them "worked" while every real capture card was suppressed,
    // which made the suppression look like a notification bug. Explain instead.
    if (!m_settings->showNotifications()) {
        showToast(tr("Dev: ALL notifications are disabled in Settings "
                     "(General → Show notifications)"), true);
        return;
    }
    if (!m_settings->showCapturePopup()) {
        showToast(tr("Dev: capture notification is DISABLED in Settings "
                     "(General → Show capture notification)"), true);
        return;
    }
    const bool inhibited = nowInhibited();
    if (inhibited && m_settings->muteOnFullscreen())
        showToast(tr("Dev: cards are currently muted (fullscreen / Do Not Disturb "
                     "inhibition is active)"), true);
    showCaptureNotification(devTestImage(), QString(), QStringLiteral("image"), inhibited);
}

void AppContext::devTestEditor()
{
    if (!devBuild())
        return;
    openEditor(devTestImage());
}

void AppContext::devTestHistory()
{
    if (!devBuild())
        return;
    m_history->addEntry(QString(), devTestImage(), QStringLiteral("image"));
    showToast(tr("Dev: added a test history entry"));
}

void AppContext::devTestFavoriteHistory()
{
    if (!devBuild())
        return;
    m_history->addEntry(QString(), devTestImage(), QStringLiteral("image"));
    m_history->setFavorite(0, true);
    showToast(tr("Dev: added a STARRED history entry — try Clear all / delete on it"));
}

void AppContext::devTestEditFromHistory()
{
    if (!devBuild())
        return;
    // Persist a throwaway image, register it in history, then open it in the
    // overwrite editor — the exact path the History "Edit" button drives.
    const QString p = saveImageAuto(devTestImage(), QStringLiteral("devtest-edit.png"));
    if (p.isEmpty()) {
        showToast(tr("Dev: couldn't save the test image"), true);
        return;
    }
    m_history->addEntry(p, devTestImage(), QStringLiteral("image"));
    editFromHistory(p);
}

void AppContext::devTestQuickCopy()
{
    if (!devBuild())
        return;
    if (!m_hotkeys->available()) {
        showToast(tr("Dev: quick-copy needs KGlobalAccel (KDE) — unavailable here"), true);
        return;
    }
    armQuickCopy(devTestImage());
    showToast(m_quickCopyArmed ? tr("Dev: press Ctrl+C within 2s to copy the test image")
                               : tr("Dev: couldn't grab Ctrl+C (key owned elsewhere)"), true);
}

void AppContext::devTestPreview()
{
    if (!devBuild())
        return;
    openPreview(devTestImage());
}

void AppContext::devTestPreviewFromHistory()
{
    if (!devBuild())
        return;
    // Same path the History pin button drives: file on disk -> preview.
    const QString p = saveImageAuto(devTestImage(), QStringLiteral("devtest-preview.png"));
    if (p.isEmpty()) {
        showToast(tr("Dev: couldn't save the test image"), true);
        return;
    }
    m_history->addEntry(p, devTestImage(), QStringLiteral("image"));
    previewFromHistory(p);
}

QString AppContext::settingsRoundTripCheck()
{
    // Export -> parse -> verify every writable Settings property serialized ->
    // import the file back. Importing the just-exported effective config is a
    // no-op for values while exercising the whole read path.
    // QTemporaryFile: unique name + 0600 — the export embeds destination
    // secrets (API keys), which must not sit world-readable in shared /tmp.
    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/unisic-smoketest-XXXXXX.json"));
    if (!tmp.open())
        return QStringLiteral("FAIL (cannot create a temp file)");
    const QString path = tmp.fileName();
    tmp.close(); // exportSettings rewrites it in place; 0600 perms survive
    const QString exportErr = exportSettings(QUrl::fromLocalFile(path));
    if (!exportErr.isEmpty())
        return QStringLiteral("FAIL (export: %1)").arg(exportErr);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("FAIL (cannot re-read %1)").arg(path);
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    const QJsonObject s = root.value(QStringLiteral("settings")).toObject();
    QStringList missing;
    const QMetaObject *mo = m_settings->metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty p = mo->property(i);
        if (p.isWritable() && !s.contains(QString::fromLatin1(p.name())))
            missing << QString::fromLatin1(p.name());
    }
    if (!missing.isEmpty())
        return QStringLiteral("FAIL (not serialized: %1)").arg(missing.join(QLatin1String(", ")));

    const QString importErr = importSettings(QUrl::fromLocalFile(path));
    if (!importErr.isEmpty())
        return QStringLiteral("FAIL (import: %1)").arg(importErr);
    return QStringLiteral("PASS (%1 settings + %2 destinations)")
        .arg(s.size())
        .arg(root.value(QStringLiteral("destinations")).toArray().size());
}

void AppContext::devTestSettingsRoundTrip()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: settings round-trip — %1").arg(settingsRoundTripCheck()));
}

void AppContext::devTestUpload()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: uploading a test image to '%1'…").arg(m_settings->activeDestination()));
    uploadImage(devTestImage(), [this](const QString &url, const QString &err) {
        if (err.isEmpty())
            showToast(tr("Dev: upload OK — %1").arg(url));
        else
            showToast(tr("Dev: upload failed — %1").arg(err), true);
    });
}

QStringList AppContext::hotkeyBindStatus(int *unbound, bool heal)
{
    QStringList lines;
    int bad = 0;
    const auto acts = hotkeyActions();
    for (const HotkeyAction &a : acts) {
        bool ok = false;
        const QString actual = m_hotkeys->activeKeysPortable(a.id, &ok);
        if (!ok) {
            lines << a.id + QStringLiteral(": query failed");
            ++bad;
        } else if (actual.isEmpty() && !a.keys.isEmpty()) {
            ++bad;
            if (heal && m_hotkeys->setShortcut(a.id, a.name, a.keys))
                lines << a.id + QStringLiteral(": was unbound — re-asserted ") + a.keys;
            else
                lines << a.id + QStringLiteral(": UNBOUND (stored ") + a.keys + QLatin1Char(')');
        } else {
            // Bound, but not to what we store = a KCM edit — honor it in the
            // UI (daemon-authoritative display).
            if (actual != a.keys)
                syncHotkeyFromDaemon(a.id, actual);
            lines << a.id + QStringLiteral(": ")
                     + (actual.isEmpty() ? QStringLiteral("(none)") : actual);
        }
    }
    if (unbound)
        *unbound = bad;
    return lines;
}

void AppContext::devTestHotkeyBinds()
{
    if (!devBuild())
        return;
    if (!m_hotkeys->available()) {
        showToast(tr("Dev: KGlobalAccel not available (backend: %1)")
                      .arg(m_hotkeyBackend.isEmpty() ? tr("none") : m_hotkeyBackend));
        return;
    }
    int bad = 0;
    const QStringList lines = hotkeyBindStatus(&bad, true);
    qInfo().noquote() << "[dev] hotkey binds:\n" + lines.join(QLatin1Char('\n'));
    if (bad == 0)
        showToast(tr("Hotkeys: all %1 bound in the daemon").arg(lines.size()));
    else
        showToast(tr("Hotkeys: %1 of %2 were unbound — re-asserted (details in the log)")
                      .arg(bad).arg(lines.size()), true);
}

void AppContext::smokeNext()
{
    if (m_smokeIdx >= m_smokeSteps.size()) {
        m_smokeRunning = false;
        smokeLog(QStringLiteral("=== smoke test done ==="));
        m_smokeSteps.clear();
        emit smokeTestChanged();
        return;
    }
    m_smokeSteps[m_smokeIdx++]();
}

void AppContext::runSmokeTest()
{
    // Dev-only, defense in depth: the F8 dispatch and the QML pane already
    // check, but the invokable itself must not be reachable in a release.
    if (!devBuild() || m_smokeRunning)
        return;
    m_smokeRunning = true;
    m_smokeLog.clear();
    m_smokeIdx = 0;
    m_smokeSteps.clear();
    m_smokeWindows.clear();
    smokeLog(QStringLiteral("=== Unisic smoke test ==="));
    emit smokeTestChanged();

    // 1) capability / availability snapshot (synchronous)
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("build: ") + (devBuild() ? QStringLiteral("dev") : QStringLiteral("release")));
        smokeLog(QStringLiteral("capture backend: ") + (m_capture ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        smokeLog(QStringLiteral("recording: ") + (recordingAvailable() ? QStringLiteral("PASS") : QStringLiteral("SKIP (no PipeWire/portal)")));
        smokeLog(QStringLiteral("notifications: native=%1 custom=%2 -> %3")
                 .arg(capNativeNotification() ? "y" : "n", capCustomNotification() ? "y" : "n",
                      (capNativeNotification() || capCustomNotification()) ? "PASS" : "FAIL"));
        smokeLog(QStringLiteral("record border: ") + (capRecordBorder() ? QStringLiteral("PASS") : QStringLiteral("n/a on this compositor")));
        smokeLog(QStringLiteral("tray: ") + (trayAvailable() ? QStringLiteral("PASS") : QStringLiteral("SKIP (no tray host)")));
        smokeLog(QStringLiteral("hotkeys: %1 (%2)").arg(hotkeysAvailable() ? "PASS" : "SKIP", hotkeyBackend()));
        if (m_hotkeys->available()) {
            // Live daemon check: every action's active binding (heals unbound
            // ones — same repair defineHotkeys runs at startup).
            int bad = 0;
            const QStringList lines = hotkeyBindStatus(&bad, true);
            for (const QString &l : lines)
                smokeLog(QStringLiteral("  bind ") + l);
            smokeLog(QStringLiteral("  hotkey binds: ")
                     + (bad == 0 ? QStringLiteral("PASS")
                                 : QStringLiteral("HEALED %1 (re-run to confirm)").arg(bad)));
        }
        smokeLog(QStringLiteral("OCR: %1, QR: %2").arg(
                 ocrAvailable() ? QStringLiteral("PASS") : QStringLiteral("SKIP (no tesseract)"),
                 qrAvailable() ? QStringLiteral("PASS") : QStringLiteral("SKIP (no zxing-cpp)")));
        smokeNext();
    });

    // 2) real fullscreen capture -> save -> history
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("capture (fullscreen)…"));
        m_capture->captureWorkspace([this](const QImage &img, const QString &err) {
            if (!err.isEmpty())
                smokeLog(QStringLiteral("  capture: FAIL (%1)").arg(err));
            else if (img.isNull())
                smokeLog(QStringLiteral("  capture: FAIL (null image)"));
            else {
                smokeLog(QStringLiteral("  capture: PASS (%1x%2)").arg(img.width()).arg(img.height()));
                const QString p = saveImageAuto(img, QStringLiteral("smoketest.png"));
                if (!p.isEmpty())
                    m_history->addEntry(p, img, QStringLiteral("image"));
                smokeLog(QStringLiteral("  save + history: ") + (p.isEmpty() ? QStringLiteral("FAIL") : QStringLiteral("PASS")));
            }
            smokeNext();
        });
    });

    // 3) post-capture editor open
    m_smokeSteps.append([this] {
        const int before = m_editorWindows;
        QImage t(64, 64, QImage::Format_ARGB32);
        t.fill(QColor(0x2E, 0x23, 0x6C));
        openEditor(t);
        smokeLog(QStringLiteral("editor open: ") + (m_editorWindows > before
                 ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        smokeNext();
    });

    // 3b) edit an existing capture from history (overwrite editor path)
    m_smokeSteps.append([this] {
        const QString p = saveImageAuto(devTestImage(), QStringLiteral("smoketest-edit.png"));
        if (p.isEmpty()) {
            smokeLog(QStringLiteral("edit from history: FAIL (couldn't save source)"));
            smokeNext();
            return;
        }
        m_history->addEntry(p, devTestImage(), QStringLiteral("image"));
        const int before = m_editorWindows;
        editFromHistory(p);
        smokeLog(QStringLiteral("edit from history: ") + (m_editorWindows > before
                 ? QStringLiteral("PASS (overwrite editor)") : QStringLiteral("FAIL")));
        smokeNext();
    });

    // 3c) quick-copy grab (Ctrl+C grace window) — arm, verify, release
    m_smokeSteps.append([this] {
        if (!m_hotkeys->available()) {
            smokeLog(QStringLiteral("quick-copy: SKIP (no KGlobalAccel)"));
            smokeNext();
            return;
        }
        armQuickCopy(devTestImage());
        smokeLog(QStringLiteral("quick-copy grab: ") + (m_quickCopyArmed
                 ? QStringLiteral("PASS") : QStringLiteral("FAIL (Ctrl+C owned elsewhere?)")));
        disarmQuickCopy();
        smokeNext();
    });

    // 3d) floating preview window (pin/opacity/drag)
    m_smokeSteps.append([this] {
        const bool ok = openPreview(devTestImage());
        smokeLog(QStringLiteral("preview window: ") + (ok
                 ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        smokeNext();
    });

    // 3e) history favorite round-trip (star -> role reads back -> unstar)
    m_smokeSteps.append([this] {
        m_history->addEntry(QString(), devTestImage(), QStringLiteral("image"));
        m_history->setFavorite(0, true);
        const bool fav = m_history->data(m_history->index(0), HistoryStore::FavoriteRole).toBool();
        smokeLog(QStringLiteral("history favorite: ") + (fav ? QStringLiteral("PASS")
                                                             : QStringLiteral("FAIL")));
        m_history->setFavorite(0, false);
        smokeNext();
    });

    // 3e2) smart pick: object detection on a synthetic window-like rect —
    // the exact candidate lookup a click in the region overlay performs.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("smart pick detect: ") + smartPickDetectCheck());
        smokeNext();
    });


    // 3f) OCR recognition — a real tesseract run on a rendered known token
    // (digits: language-neutral, works with any installed traineddata).
    m_smokeSteps.append([this] {
#ifdef HAVE_TESSERACT
        QImage t(320, 120, QImage::Format_ARGB32);
        t.fill(Qt::white);
        {
            QPainter p(&t);
            p.setPen(Qt::black);
            QFont f;
            f.setPixelSize(64);
            f.setBold(true);
            p.setFont(f);
            p.drawText(t.rect(), Qt::AlignCenter, QStringLiteral("1234"));
        }
        m_ocr->recognize(t, m_settings->ocrLanguages(), [this](const QString &text, const QString &err) {
            if (!err.isEmpty())
                smokeLog(QStringLiteral("ocr recognize: FAIL (%1)").arg(err));
            else if (text.contains(QLatin1String("1234")))
                smokeLog(QStringLiteral("ocr recognize: PASS"));
            else
                smokeLog(QStringLiteral("ocr recognize: FAIL (got '%1')").arg(text.simplified()));
            smokeNext();
        });
#else
        smokeLog(QStringLiteral("ocr recognize: SKIP (built without tesseract)"));
        smokeNext();
#endif
    });

    // 4) short GIF recording (fullscreen, ~3s, auto-stop)
    m_smokeSteps.append([this] {
        if (!recordingAvailable()) {
            smokeLog(QStringLiteral("recording: SKIP"));
            smokeNext();
            return;
        }
        smokeLog(QStringLiteral("recording (GIF fullscreen, ~3s)…"));
        // `live` dies with THIS smoke recording: without it the 3s auto-stop
        // outlives an early failure (e.g. portal dialog cancelled) and would
        // kill an unrelated recording the user starts right after.
        auto live = std::make_shared<bool>(true);
        auto done = std::make_shared<QMetaObject::Connection>();
        auto fail = std::make_shared<QMetaObject::Connection>();
        *done = connect(m_recorder, &GifRecorder::finished, this, [this, live, done, fail](const QString &f) {
            *live = false;
            disconnect(*done); disconnect(*fail);
            smokeLog(QStringLiteral("  recording: PASS (%1)").arg(f));
            smokeNext();
        });
        *fail = connect(m_recorder, &GifRecorder::failed, this, [this, live, done, fail](const QString &e) {
            *live = false;
            disconnect(*done); disconnect(*fail);
            smokeLog(QStringLiteral("  recording: FAIL (%1)").arg(e));
            smokeNext();
        });
        startGifFullScreen();
        QTimer::singleShot(3000, this, [this, live] { if (*live && recording()) stopRecording(); });
    });

    // 4b) short video recording (MP4 fullscreen, ~3s, auto-stop) — the video
    // pipeline (format selection, convertVideo, poster extraction) is distinct
    // from the GIF path and needs its own pass/fail line.
    m_smokeSteps.append([this] {
        if (!recordingAvailable()) {
            smokeLog(QStringLiteral("video recording: SKIP"));
            smokeNext();
            return;
        }
        smokeLog(QStringLiteral("recording (video fullscreen, ~3s)…"));
        auto live = std::make_shared<bool>(true);
        auto done = std::make_shared<QMetaObject::Connection>();
        auto fail = std::make_shared<QMetaObject::Connection>();
        *done = connect(m_recorder, &GifRecorder::finished, this, [this, live, done, fail](const QString &f) {
            *live = false;
            disconnect(*done); disconnect(*fail);
            smokeLog(QStringLiteral("  video recording: PASS (%1)").arg(f));
            smokeNext();
        });
        *fail = connect(m_recorder, &GifRecorder::failed, this, [this, live, done, fail](const QString &e) {
            *live = false;
            disconnect(*done); disconnect(*fail);
            smokeLog(QStringLiteral("  video recording: FAIL (%1)").arg(e));
            smokeNext();
        });
        startVideoScreen();
        QTimer::singleShot(3000, this, [this, live] { if (*live && recording()) stopRecording(); });
    });

    // 5) capture notification
    m_smokeSteps.append([this] {
        QImage t(48, 48, QImage::Format_ARGB32);
        t.fill(QColor(0xC8, 0xAC, 0xD6));
        auto *n = showCaptureNotification(t, QString(), QStringLiteral("image"), false);
        smokeLog(QStringLiteral("notification: ") + (n ? QStringLiteral("PASS (shown)")
                 : QStringLiteral("SKIP (disabled or no server/layer-shell)")));
        smokeNext();
    });

    // 5b) settings export/import round-trip (metaobject serialization has
    // regression history — the [%General] key folding).
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("settings round-trip: ") + settingsRoundTripCheck());
        smokeNext();
    });

    // 6) upload (needs a real destination + a public target — left manual)
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("upload: SKIP — active destination '%1'; run a real upload manually")
                 .arg(m_settings->activeDestination()));
        smokeNext();
    });

    // 7) cleanup: close every editor/preview window the run opened — F8 must
    // verify and leave the desktop exactly as it found it.
    m_smokeSteps.append([this] {
        int closed = 0;
        for (const QPointer<QQuickWindow> &w : std::as_const(m_smokeWindows)) {
            if (w) {
                w->close();
                ++closed;
            }
        }
        m_smokeWindows.clear();
        smokeLog(QStringLiteral("cleanup: closed %1 test window(s)").arg(closed));
        smokeNext();
    });

    smokeNext();
}

bool AppContext::capRecordBorder() const
{
    if (m_layerShellAvailable)
        return true; // layer-shell overlay: KWin, wlroots, COSMIC…
    // KWin can still host the fullscreen-transparent border without layer-shell.
    auto *bi = QDBusConnection::sessionBus().interface();
    return bi && bi->isServiceRegistered(QStringLiteral("org.kde.KWin"));
}

void AppContext::showRecordBorder(QRect physRegion, QScreen *screen)
{
    hideRecordBorder(); // retire any stale frame first
    if (!m_engine || !screen || physRegion.isEmpty() || !capRecordBorder())
        return; // capRecordBorder(): layer-shell (KWin/wlroots/COSMIC) or KWin trick

    QQmlComponent component(m_engine, QUrl(QStringLiteral("qrc:/qt/qml/Unisic/qml/RecordBorder.qml")));
    if (component.isError()) {
        qWarning() << component.errorString();
        return;
    }

    // physRegion is screen-local physical pixels; the fullscreen window works in
    // logical pixels, so scale down. Snap OUTWARD (floor origin / ceil far edge)
    // so the frame's inner hole always ⊇ the true region, then pad 1px more on
    // every side. The extra pad matters under fractional scaling: beginEncoding()
    // rescales the crop by streamSize/expected and rounds x and w independently,
    // which can inflate the crop's trailing edge by a pixel and collapse a plain
    // floor/ceil margin to sub-pixel. The 1px slack keeps a full-physical-pixel
    // gap so no frame pixel can ever land inside the ffmpeg crop; it only shifts
    // the (already-outside-the-region) frame one logical pixel further out.
    const qreal dpr = screen->devicePixelRatio() > 0 ? screen->devicePixelRatio() : 1.0;
    const int left   = qFloor(physRegion.x() / dpr);
    const int top    = qFloor(physRegion.y() / dpr);
    const int right  = qCeil((physRegion.x() + physRegion.width()) / dpr);
    const int bottom = qCeil((physRegion.y() + physRegion.height()) / dpr);
    const int rx = left - 1;
    const int ry = top - 1;
    const int rw = (right - left) + 2;
    const int rh = (bottom - top) + 2;

    auto *ctx = new QQmlContext(m_engine->rootContext(), this);
    ctx->setContextProperty(QStringLiteral("regionX"), rx);
    ctx->setContextProperty(QStringLiteral("regionY"), ry);
    ctx->setContextProperty(QStringLiteral("regionW"), rw);
    ctx->setContextProperty(QStringLiteral("regionH"), rh);

    QObject *obj = component.create(ctx);
    auto *win = qobject_cast<QQuickWindow *>(obj);
    if (!win) {
        delete obj;
        delete ctx;
        return;
    }
    ctx->setParent(win);
    win->setScreen(screen);

#ifdef HAVE_LAYERSHELL
    if (m_layerShellAvailable) {
        // Fullscreen click-through OVERLAY layer surface — works beyond KWin
        // (wlroots, COSMIC). The QML window is WindowTransparentForInput, so
        // clicks pass through; anchoring all four edges fills the output.
        win->resize(screen->geometry().size());
        if (auto *ls = LayerShellQt::Window::get(win)) {
            using LW = LayerShellQt::Window;
            ls->setLayer(LW::LayerOverlay);
            ls->setScope(QStringLiteral("unisic-record-border"));
            ls->setExclusiveZone(-1); // cover the whole output, ignore panels
            ls->setKeyboardInteractivity(LW::KeyboardInteractivityNone);
            ls->setAnchors(LW::Anchors(LW::AnchorTop | LW::AnchorBottom
                                       | LW::AnchorLeft | LW::AnchorRight));
            ls->setMargins(QMargins(0, 0, 0, 0));
        }
        win->show();
        m_recordBorderWindow = win;
        return;
    }
#endif
    // KWin fullscreen-transparent fallback (no layer-shell build/support).
    // showFullScreen pins the surface to the screen origin (see the popup); the
    // window is input-transparent so it never steals focus or clicks.
    win->setGeometry(screen->geometry());
    win->create();
    win->showFullScreen();
    m_recordBorderWindow = win;
}

void AppContext::hideRecordBorder()
{
    m_pendingRecordRegion = QRect();
    if (m_recordBorderWindow) {
        m_recordBorderWindow->close();
        m_recordBorderWindow->deleteLater();
        m_recordBorderWindow = nullptr;
    }
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
        // Poster extraction should take a fraction of a second. Do not leave a
        // stuck ffmpeg process and its QProcess alive forever if a malformed
        // media file or a broken decoder blocks here.
        QTimer::singleShot(30000, proc, [proc] {
            if (proc->state() != QProcess::NotRunning) {
                qWarning() << "Timed out extracting video poster frame";
                proc->kill();
            }
        });
        return;
    }
    QImage thumb(path); // first GIF frame loads fine via Qt's gif plugin
    finishRecordingEntry(path, thumb, kind);
}

void AppContext::finishRecordingEntry(const QString &path, const QImage &thumb, const QString &kind)
{
    m_history->addEntry(path, thumb, kind);
    showToast(tr("Saved %1").arg(path));

    // Inhibition is sampled NOW, not at recording start: a recording can run
    // for minutes, and muteOnFullscreen must reflect what is on screen when
    // the card would appear (e.g. the recorded app went fullscreen mid-run).
    auto *notif = showCaptureNotification(thumb, path, kind, nowInhibited());
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
void AppContext::finishCapture(const QImage &img, bool inhibited)
{
    if (img.isNull())
        return;

    // One name per capture: save and upload must agree (a second-boundary or
    // %rand% template would otherwise produce two different names).
    const QString fileName = makeFileName();
    QString path;
    if (m_settings->autoSave()) {
        path = saveImageAuto(img, fileName);
        // A failed save must be LOUD: the rest of the pipeline continues (the
        // capture still exists in memory/history), but silently pretending it
        // was persisted loses data on unplugged/read-only/full save targets.
        if (path.isEmpty())
            showToast(tr("Could not save to %1 — check the save folder in Settings")
                          .arg(m_settings->saveDirectory()), true);
    }
    if (m_settings->copyToClipboard())
        copyImageToClipboard(img);

    const bool uploading = m_settings->uploadAfterCapture();
    quint64 historyId = 0;
    if (!uploading || !path.isEmpty())
        historyId = m_history->addEntry(path, img, QStringLiteral("image"));

    auto *notif = showCaptureNotification(img, path, QStringLiteral("image"), inhibited);
    QPointer<CaptureNotification> np(notif);
    if (notif)
        notif->setHistoryId(historyId); // Save/upload address exactly this entry

    if (uploading) {
        if (np) np->setUploading(true);
        // Encode off-thread (100+ ms at 4K), start the upload in the GUI-thread
        // continuation. The callback retains the image ONLY when the history
        // entry can actually need it (nothing saved to disk) — otherwise the
        // 30-60 MB buffer would stay pinned for the whole network transfer.
        encodeImageAsync(img, [this, path, np, fileName,
                               img = path.isEmpty() ? img : QImage()](const QByteArray &data, const QString &mime) {
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
        });
    } else if (!path.isEmpty()) {
        showToast(tr("Saved %1").arg(path));
    }

    if (m_settings->openEditor())
        openEditor(img);

    // When the user hasn't opted into auto-copy, still let them grab it with a
    // reflexive Ctrl+C right after the shot (2s window), then get their key back.
    if (!m_settings->copyToClipboard() && m_settings->quickCopyAfterCapture())
        armQuickCopy(img);

    scheduleMemoryTrim();
}

void AppContext::armQuickCopy(const QImage &img)
{
    // KGlobalAccel-only: it's the one backend that can grab Ctrl+C on demand and
    // release it. The GlobalShortcuts portal binds are fixed at session config.
    if (!m_hotkeys->available() || img.isNull())
        return;
    m_quickCopyImage = img;
    if (!m_quickCopyTimer) {
        m_quickCopyTimer = new QTimer(this);
        m_quickCopyTimer->setSingleShot(true);
        connect(m_quickCopyTimer, &QTimer::timeout, this, &AppContext::disarmQuickCopy);
    }
    // Grab Ctrl+C globally for the grace window. If the daemon refuses (another
    // component owns it) there's simply no quick-copy this time.
    if (!m_hotkeys->setShortcut(QStringLiteral("quick-copy"),
                                tr("Copy last capture"), QStringLiteral("Ctrl+C"))) {
        m_quickCopyImage = QImage();
        return;
    }
    m_quickCopyArmed = true;
    m_quickCopyTimer->start(2000);
}

void AppContext::disarmQuickCopy()
{
    if (m_quickCopyTimer)
        m_quickCopyTimer->stop();
    if (m_quickCopyArmed) {
        // Release the global Ctrl+C grab so normal copy works everywhere again.
        // Async fire-and-forget: the result is never inspected and the blocking
        // variant stalled the GUI thread twice per capture.
        m_hotkeys->releaseShortcut(QStringLiteral("quick-copy"), tr("Copy last capture"));
        m_quickCopyArmed = false;
    }
    m_quickCopyImage = QImage();
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

void AppContext::editFromHistory(const QString &filePath)
{
    QImage img(filePath);
    if (img.isNull()) {
        showToast(tr("Can't open %1 for editing").arg(QFileInfo(filePath).fileName()), true);
        return;
    }
    openEditor(img, filePath);
}

void AppContext::previewFromHistory(const QString &filePath)
{
    QImage img(filePath);
    if (img.isNull()) {
        showToast(tr("Can't open %1 for preview").arg(QFileInfo(filePath).fileName()), true);
        return;
    }
    openPreview(img);
}

void AppContext::openEditor(const QImage &img, const QString &overwritePath)
{
    if (!m_engine)
        return;
    QQmlComponent component(m_engine, QUrl(QStringLiteral("qrc:/qt/qml/Unisic/qml/EditorWindow.qml")));
    if (component.isError()) {
        qWarning() << component.errorString();
        return;
    }
    auto *session = new EditorSession(this, img, overwritePath, this);
    auto *ctx = new QQmlContext(m_engine->rootContext(), session);
    ctx->setContextProperty(QStringLiteral("editorSession"), session);
    QObject *obj = component.create(ctx);
    if (auto *win = qobject_cast<QQuickWindow *>(obj)) {
        ++m_editorWindows;
        emit editorWindowsOpenChanged();
        if (m_smokeRunning)
            m_smokeWindows.append(win); // auto-closed by the smoke test's last step
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

bool AppContext::openPreview(const QImage &img)
{
    if (!m_engine || img.isNull())
        return false;
    // A crash/SIGKILL with a preview open leaves its temp PNG behind — in /tmp
    // that's tmpfs, i.e. RAM until reboot. Sweep stale ones once per process
    // (never per call: another still-open preview owns its own temp file).
    static bool sweptStale = false;
    if (!sweptStale) {
        sweptStale = true;
        QDir tmpDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
        const QStringList stale = tmpDir.entryList({QStringLiteral("unisic-preview-*.png")}, QDir::Files);
        for (const QString &f : stale)
            QFile::remove(tmpDir.filePath(f));
    }
    // Persist a full-res copy the tool window loads by path — keeps that window
    // trivial (no image provider) — and remove it when the window closes. The
    // PNG encode is 100+ ms at 4K, so it runs on a worker; the window is built
    // in the GUI-thread continuation.
    const QString tmp = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                            .filePath(QStringLiteral("unisic-preview-") +
                                      QUuid::createUuid().toString(QUuid::WithoutBraces) +
                                      QStringLiteral(".png"));
    QPointer<AppContext> self(this);
    QPointer<QCoreApplication> application(qApp);
    (void)QtConcurrent::run([self, application, img, tmp] {
        const bool ok = img.save(tmp);
        if (!application) {
            QFile::remove(tmp);
            return;
        }
        QMetaObject::invokeMethod(application.data(), [self, tmp, ok, size = img.size()] {
            if (self)
                self->finishOpenPreview(ok, tmp, size);
            else
                QFile::remove(tmp);
        }, Qt::QueuedConnection);
    });
    return true;
}

void AppContext::finishOpenPreview(bool saved, const QString &tmp, const QSize &imgSize)
{
    if (!saved || !m_engine) {
        QFile::remove(tmp);
        showToast(tr("Couldn't open preview"), true);
        return;
    }
    QQmlComponent component(m_engine, QUrl(QStringLiteral("qrc:/qt/qml/Unisic/qml/PreviewWindow.qml")));
    if (component.isError()) {
        qWarning() << component.errorString();
        QFile::remove(tmp);
        return;
    }
    // Create the controller BEFORE the component so QML resolves `previewCtl`
    // to the real object at bind time (a late setContextProperty wouldn't reach
    // handlers reliably — that left move/close as no-ops).
    auto *ctl = new PreviewController(m_layerShellAvailable, this);
    auto *ctx = new QQmlContext(m_engine->rootContext(), this);
    ctx->setContextProperty(QStringLiteral("previewImagePath"), QUrl::fromLocalFile(tmp).toString());
    ctx->setContextProperty(QStringLiteral("previewImageSize"), imgSize);
    ctx->setContextProperty(QStringLiteral("previewCtl"), ctl);
    QObject *obj = component.create(ctx);
    if (auto *win = qobject_cast<QQuickWindow *>(obj)) {
        ctx->setParent(win);
        ctl->setParent(win);
        ctl->setWindow(win);
        ctl->attach();   // configure layer-shell / flags before the window shows
        connect(win, &QQuickWindow::visibleChanged, win, [win, tmp](bool v) {
            if (!v) {
                QFile::remove(tmp);
                win->deleteLater();
            }
        });
        // Belt-and-braces for exit paths where visibleChanged(false) never
        // fires (deleteLater is idempotent-safe here: remove of a gone file).
        connect(win, &QObject::destroyed, qApp, [tmp] { QFile::remove(tmp); });
        if (m_smokeRunning)
            m_smokeWindows.append(win); // auto-closed by the smoke test's last step
        win->show();
        win->requestActivate();
        return;
    }
    delete obj;
    delete ctl;
    delete ctx; // parented to AppContext — would otherwise outlive every failure
    QFile::remove(tmp);
}

void AppContext::uploadFromNotification(CaptureNotification *n, const QImage &img, const QString &path)
{
    QPointer<CaptureNotification> np(n);
    if (n)
        n->setUploading(true);
    // A recording's card carries the POSTER FRAME as its image — uploading
    // that would ship a still PNG instead of the GIF/video. Upload the media
    // file itself, exactly like finishRecordingEntry's auto-upload does.
    if (n && n->kind() != QLatin1String("image") && !path.isEmpty()) {
        m_uploads->uploadFile(path, [this, path, np](const QString &url, const QString &del, const QString &err) {
            if (!err.isEmpty()) {
                showToast(tr("Upload failed: %1").arg(err), true);
                if (np) np->setUploading(false);
                return;
            }
            m_history->setUrl(path, url, del);
            afterUploadActions(url);
            if (np) np->setUrl(url);
        });
        return;
    }
    const QString fileName = path.isEmpty() ? makeFileName() : QFileInfo(path).fileName();
    // Same off-thread encode + conditional image retention as finishCapture.
    encodeImageAsync(img, [this, path, np, fileName,
                           img = path.isEmpty() ? img : QImage()](const QByteArray &data, const QString &mime) {
        m_uploads->uploadData(data, fileName, mime,
            [this, img, path, np](const QString &url, const QString &del, const QString &err) {
                if (!err.isEmpty()) {
                    showToast(tr("Upload failed: %1").arg(err), true);
                    if (np) np->setUploading(false);
                    return;
                }
                if (!path.isEmpty())
                    m_history->setUrl(path, url, del);
                // Unsaved capture: finishCapture already added a pathless
                // entry for it — attach the URL to exactly that entry (by the
                // card's history id). Fallback add only if it was evicted.
                else if (!np || !m_history->setUrlById(np->historyId(), url, del))
                    m_history->addEntry({}, img, QStringLiteral("image"), url, del);
                afterUploadActions(url);
                if (np) np->setUrl(url);
            });
    });
}

CaptureNotification *AppContext::showCaptureNotification(const QImage &img, const QString &path,
                                                         const QString &kind, bool inhibited)
{
    // The master "Show notifications" switch promises complete silence — it
    // must cover capture cards (layer-shell AND native) exactly like toasts.
    if (!m_settings->showCapturePopup() || !m_settings->showNotifications())
        return nullptr;
    // A real desktop notification (org.freedesktop.Notifications) with an inline
    // thumbnail and Open/Copy/Upload/Delete action buttons. The notification
    // server draws it, so it is always above other windows on every desktop —
    // unlike the old client-drawn fullscreen card, which Wayland would not keep
    // on top (a click elsewhere raised another window over it). The notifier
    // owns the returned object; callers may still poke its upload state.
    auto *notif = new CaptureNotification(this, img, path, kind, nullptr);
#ifdef HAVE_LAYERSHELL
    if (m_layerNotifier) {
        // The layer card draws above everything. Only when the user opted in
        // (muteOnFullscreen) do we honour KDE's inhibition — which conflates a
        // fullscreen app, Do-Not-Disturb, AND stuck third-party inhibitors, so
        // auto-suppressing by default wrongly killed the user's own capture
        // feedback. sampled when THIS capture began (before our own overlay).
        if (inhibited && m_settings->muteOnFullscreen()) {
            notif->deleteLater();
            return nullptr;
        }
        m_layerNotifier->show(notif); // on-top custom card (layer-shell)
        return notif;
    }
#endif
    m_notifier->show(notif);          // native desktop notification
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

void AppContext::encodeImageAsync(const QImage &img,
                                  std::function<void(const QByteArray &, const QString &)> done)
{
    // Snapshot the settings on the GUI thread; the worker must not touch them.
    const QString fmt = m_settings->imageFormat().toLower();
    const int q = qBound(1, m_settings->imageQuality(), 100);
    QPointer<AppContext> self(this);
    QPointer<QCoreApplication> application(qApp);
    (void)QtConcurrent::run([self, application, img, fmt, q, done = std::move(done)] {
        QByteArray out;
        QString mime;
        QBuffer buf(&out);
        buf.open(QIODevice::WriteOnly);
        if ((fmt == QLatin1String("jpg") || fmt == QLatin1String("jpeg")) && img.save(&buf, "JPG", q)) {
            mime = QStringLiteral("image/jpeg");
        } else if (fmt == QLatin1String("webp") && img.save(&buf, "WEBP", q)) {
            mime = QStringLiteral("image/webp");
        } else {
            out.clear();
            buf.seek(0);
            img.save(&buf, "PNG");
            mime = QStringLiteral("image/png");
        }
        if (!application)
            return;
        QMetaObject::invokeMethod(application.data(), [self, out, mime, done] {
            if (self)
                done(out, mime);
        }, Qt::QueuedConnection);
    });
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
    // The deferred wl-copy must not land STALE: two rapid captures can finish
    // encoding out of order, and the user may copy something else during the
    // encode — only the newest copy request may take the Wayland selection
    // (all m_clipboardSeq writers run on the GUI thread; no atomics needed).
    const quint64 seq = ++m_clipboardSeq;
    QPointer<AppContext> self(this);
    QPointer<QCoreApplication> application(qApp);
    (void)QtConcurrent::run([self, application, img, wlCopy, seq] {
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        // QProcess must live on the GUI thread.
        if (!application)
            return;
        QMetaObject::invokeMethod(application.data(), [self, png, wlCopy, seq] {
            if (self && self->m_clipboardSeq == seq)
                spawnWlCopy(self, wlCopy, {QStringLiteral("--type"), QStringLiteral("image/png")}, png);
        }, Qt::QueuedConnection);
    });
}

void AppContext::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return;
    ++m_clipboardSeq; // a newer text copy invalidates any in-flight image mirror
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
    const QString fileName = makeFileName();
    encodeImageAsync(img, [this, img, fileName, done](const QByteArray &data, const QString &mime) {
        m_uploads->uploadData(data, fileName, mime,
            [this, img, done](const QString &url, const QString &del, const QString &err) {
                if (err.isEmpty()) {
                    m_history->addEntry({}, img, QStringLiteral("image"), url, del);
                    afterUploadActions(url);
                }
                if (done)
                    done(url, err);
            });
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
        else if (it.key().contains(QLatin1Char('/'))) {
            QString k = it.key(); // legacy raw keys
            // Old exports kept General-tab keys in a "general" group — that
            // group name breaks INI round-trips (see Settings ctor migration);
            // fold to the top-level key it lives at now.
            if (k.startsWith(QLatin1String("general/")) || k.startsWith(QLatin1String("General/")))
                k = k.mid(k.indexOf(QLatin1Char('/')) + 1);
            m_settings->raw()->setValue(k, it.value().toVariant());
        }
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
        {QStringLiteral("ocr-region"), tr("OCR region (copy text)"), m_settings->hotkeyOcr()},
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
    else if (actionId == QLatin1String("ocr-region")) m_settings->setHotkeyOcr(portable);
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
        for (const HotkeyAction &a : acts)
            m_hotkeys->defineAction(a.id, a.name, a.keys);

        // Leftover from the abandoned one-time-heal scheme (see the verify
        // pass below, which now runs every launch).
        m_settings->raw()->remove(QStringLiteral("hotkeys/bootstrapped"));
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
#ifdef UNISIC_DEV_BUILD
        // Dev-only: F8 runs the smoke test. Fixed key (not user-configurable).
        m_hotkeys->setShortcut(QStringLiteral("smoke-test"),
                               tr("Developer smoke test"), QStringLiteral("F8"));
#endif
        // The quick-copy grab is set with NoAutoloading, so a crash/quit inside
        // the 2s window would leave Ctrl+C bound to us persistently. Clear it on
        // every startup so a stale grab can't hijack the user's Ctrl+C.
        m_hotkeys->releaseShortcut(QStringLiteral("quick-copy"), tr("Copy last capture"));
        // Verify + repair, EVERY launch, with real shortcutKeys queries — the
        // registration replies CANNOT be trusted for this: kglobalacceld
        // (observed live) answers an IsDefault setShortcut with the requested
        // keys even when it stored them into the default column only and the
        // ACTIVE binding stayed "none". That left every hotkey shown as
        // assigned in the UI yet silently dead until the user re-assigned
        // each one by hand. hotkeyBindStatus asserts the stored key on any
        // action the daemon reports unbound, and syncs a KCM-edited key back
        // into the UI. A deliberate KCM unbind made while the app runs still
        // sticks: it arrives via yourShortcutsChanged and empties the stored
        // string, so there is nothing to assert on the next launch.
        int unbound = 0;
        const QStringList report = hotkeyBindStatus(&unbound, true);
        if (unbound > 0)
            qWarning().noquote() << "Hotkey repair:\n" + report.join(QLatin1Char('\n'));
        emit hotkeysAvailableChanged();
        return;
    }

    // Non-KDE: the GlobalShortcuts portal (GNOME 48+, Hyprland, …). The
    // interface can be present yet backed by a broken impl (xdp-gnome's is
    // hardwired to org.gnome.Shell), so the bind response is the real test.
    // Async probe: the blocking one D-Bus-activated the portal and could stall
    // startup by hundreds of ms on a cold session.
    PortalGlobalShortcuts::probeInterface(this, [this](bool present) {
        if (!present) {
            m_hotkeyBackend.clear();
            emit hotkeysAvailableChanged();
            return;
        }
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
    });
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
    m_tray = new QSystemTrayIcon(trayIcon(), this);
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

// Render an image (SVG included) at `size` and flat-recolor it to `color`
// (SourceIn keeps the alpha shape, replaces every colour) — same recipe the
// tool-icon provider uses for monochrome glyphs.
static QPixmap recolorPixmap(const QString &path, const QColor &color, const QSize &size)
{
    QImageReader reader(path);
    reader.setScaledSize(size);
    QImage img = reader.read();
    if (img.isNull())
        return {};
    img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (color.isValid()) {
        QPainter p(&img);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(img.rect(), color);
        p.end();
    }
    return QPixmap::fromImage(img);
}

// Bundled presets live in the read-only qrc tree; treat those as monochrome and
// recolor them. User-dropped files (arbitrary logos) are used as-is.
static bool isBundledTrayIcon(const QString &path)
{
    return path.startsWith(QLatin1String(":/resources/icons/tray/"));
}

bool AppContext::systemIsDark() const
{
    if (auto *h = QGuiApplication::styleHints()) {
        const Qt::ColorScheme s = h->colorScheme();
        if (s == Qt::ColorScheme::Dark) return true;
        if (s == Qt::ColorScheme::Light) return false;
    }
    // Unknown scheme: fall back to the window background's lightness.
    return qApp->palette().color(QPalette::Window).lightness() < 128;
}

QColor AppContext::trayContrastColor() const
{
    // Near-white on dark, near-black on light — strong contrast against whatever
    // panel the tray sits in, without banking on the exact system text colour.
    return systemIsDark() ? QColor(0xEC, 0xEC, 0xEC) : QColor(0x2B, 0x2B, 0x2B);
}

QIcon AppContext::recoloredTrayIcon(const QString &path) const
{
    const QColor c = trayContrastColor();
    QIcon icon;
    // A spread of sizes so the StatusNotifier host picks a crisp one.
    for (int s : {16, 22, 24, 32, 48, 64}) {
        const QPixmap pm = recolorPixmap(path, c, QSize(s, s));
        if (!pm.isNull())
            icon.addPixmap(pm);
    }
    return icon;
}

QIcon AppContext::trayIcon() const
{
    const QString path = m_settings->trayIconPath();
    if (!path.isEmpty()) {
        if (isBundledTrayIcon(path)) {
            const QIcon recolored = recoloredTrayIcon(path);
            if (!recolored.isNull())
                return recolored;
        }
        QIcon custom(path);
        // availableSizes() is EMPTY for scalable SVGs (no discrete sizes) — gate
        // on whether a pixmap actually renders instead, so .svg works too.
        if (!custom.isNull() && !custom.pixmap(QSize(64, 64)).isNull())
            return custom;
    }
    return QIcon(QStringLiteral(":/resources/icons/unisic.svg"));
}

QString AppContext::trayIconThumb(const QString &path, const QColor &color) const
{
    const QPixmap pm = recolorPixmap(path, color.isValid() ? color : trayContrastColor(),
                                     QSize(76, 76));
    if (pm.isNull())
        return {};
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    pm.save(&buf, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

QIcon AppContext::trayIconBadged() const
{
    QPixmap pm = trayIcon().pixmap(QSize(64, 64));
    if (pm.isNull())
        return trayIcon();
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const int d = qRound(pm.width() * 0.44);           // recording dot
    const int m = qRound(pm.width() * 0.04);
    const QRect dot(pm.width() - d - m, pm.height() - d - m, d, d);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x17, 0x15, 0x3B));              // dark ring for contrast
    p.drawEllipse(dot.adjusted(-2, -2, 2, 2));
    p.setBrush(QColor(0xE7, 0x4C, 0x3C));              // recording red
    p.drawEllipse(dot);
    p.end();
    return QIcon(pm);
}

void AppContext::applyTrayIcon()
{
    if (!m_tray)
        return;
    // Badge the tray while ACTIVELY recording (not during encoding) so it's an
    // at-a-glance "recording now" and clears the instant the user stops.
    m_tray->setIcon(recording() && !converting() ? trayIconBadged() : trayIcon());
}

void AppContext::pickTrayIcon()
{
    // Native (DE) picker — same reasoning as the settings export/import dialogs.
    const QString path = QFileDialog::getOpenFileName(
        nullptr, tr("Choose tray icon"), trayIconsDir(),
        tr("Images (*.png *.svg *.svgz *.xpm *.ico *.jpg *.jpeg *.webp)"));
    if (path.isEmpty())
        return; // cancelled
    selectTrayIcon(path);
}

void AppContext::addTrayIcon()
{
    const QString start = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString path = QFileDialog::getOpenFileName(
        nullptr, tr("Add a tray icon"), start.isEmpty() ? QDir::homePath() : start,
        tr("Images (*.png *.svg *.svgz *.xpm *.ico *.jpg *.jpeg *.webp)"));
    if (path.isEmpty())
        return; // cancelled
    QIcon test(path);
    if (test.isNull() || test.pixmap(QSize(64, 64)).isNull()) {
        showToast(tr("Could not load that image as an icon"), true);
        return;
    }

    const QString dir = trayIconsDir();
    QDir().mkpath(dir);
    const QFileInfo src(path);
    // Picked a file that already lives in the folder → just select it, no copy.
    if (src.absolutePath() == QDir(dir).absolutePath()) {
        selectTrayIcon(src.absoluteFilePath());
        return;
    }
    // Copy in under a non-clobbering name (never overwrite an existing preset).
    QString dest = dir + QLatin1Char('/') + src.fileName();
    for (int n = 1; QFile::exists(dest); ++n) {
        dest = dir + QLatin1Char('/') + src.completeBaseName()
             + QStringLiteral("-%1").arg(n)
             + (src.suffix().isEmpty() ? QString() : QLatin1Char('.') + src.suffix());
    }
    if (!QFile::copy(path, dest)) {
        showToast(tr("Could not copy the icon into %1").arg(dir), true);
        return;
    }
    // Selecting it rebuilds the gallery (the preset scan is live) and the folder
    // watcher fires too; both show the new tile, already highlighted.
    selectTrayIcon(dest);
    showToast(tr("Icon added to your tray icons"));
}

void AppContext::selectTrayIcon(const QString &path)
{
    if (path.isEmpty()) {
        m_settings->setTrayIconPath(QString()); // default
        return;
    }
    QIcon test(path);
    // Render check, not availableSizes(): scalable SVGs report zero discrete
    // sizes but render fine — an availableSizes() gate rejects every .svg.
    if (test.isNull() || test.pixmap(QSize(64, 64)).isNull()) {
        showToast(tr("Could not load that image as an icon"), true);
        return;
    }
    m_settings->setTrayIconPath(path); // → trayIconPathChanged → applyTrayIcon()
}

void AppContext::clearTrayIcon()
{
    m_settings->setTrayIconPath(QString()); // → applyTrayIcon() reverts to default
}

QString AppContext::trayIconsDir() const
{
    // A drop-in folder beside the config file (~/.config/unisic/tray-icons):
    // anything the user puts here shows up in the settings icon gallery.
    return QFileInfo(UnisicConfig::filePath()).absolutePath()
           + QStringLiteral("/tray-icons");
}

QStringList AppContext::trayIconPresets() const
{
    QDir d(trayIconsDir());
    if (!d.exists())
        return {};
    static const QStringList filters{
        QStringLiteral("*.png"), QStringLiteral("*.svg"), QStringLiteral("*.svgz"),
        QStringLiteral("*.xpm"), QStringLiteral("*.ico"), QStringLiteral("*.jpg"),
        QStringLiteral("*.jpeg"), QStringLiteral("*.webp")};
    QStringList out;
    const auto files = d.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files)
        out << fi.absoluteFilePath();
    return out;
}

QStringList AppContext::bundledTrayIcons() const
{
    // The Qt resource filesystem is listable via QDir. Paths come back as
    // ":/resources/icons/tray/<name>", which QIcon and QML Image both accept.
    QDir d(QStringLiteral(":/resources/icons/tray"));
    QStringList out;
    const auto files = d.entryList(QDir::Files, QDir::Name);
    for (const QString &f : files)
        out << QStringLiteral(":/resources/icons/tray/") + f;
    return out;
}

QString AppContext::autostartFilePath() const
{
    // XDG autostart: $XDG_CONFIG_HOME/autostart (ConfigLocation == ~/.config).
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
           + QStringLiteral("/autostart/org.unisic.Unisic.desktop");
}

bool AppContext::autostartEnabled() const
{
    return QFile::exists(autostartFilePath());
}

QByteArray AppContext::autostartExecLine() const
{
    // Exec must point at a STABLE path. For an AppImage that is $APPIMAGE (the
    // outer file), not applicationFilePath() (the transient FUSE mount, gone on
    // the next run). --tray-only makes the login launch start hidden in the tray.
    QString execPath = qEnvironmentVariable("APPIMAGE");
    if (execPath.isEmpty())
        execPath = QCoreApplication::applicationFilePath();
    // Desktop Entry spec: backslash and quote need escaping inside a quoted arg;
    // '%' is a field-code introducer and must be doubled.
    execPath.replace(QLatin1Char('\\'), QLatin1String("\\\\"))
            .replace(QLatin1Char('"'), QLatin1String("\\\""))
            .replace(QLatin1Char('%'), QLatin1String("%%"));
    return "Exec=\"" + execPath.toUtf8() + "\" --tray-only\n";
}

bool AppContext::writeAutostartFile()
{
    const QString path = autostartFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write("[Desktop Entry]\n"
            "Type=Application\n"
            "Name=Unisic\n"
            "Comment=Screenshots, annotations, uploads and GIF recording\n"
            + autostartExecLine() +
            "Icon=org.unisic.Unisic\n"
            "Terminal=false\n"
            "Categories=Utility;Graphics;\n"
            "X-GNOME-Autostart-enabled=true\n");
    f.close();
    return true;
}

void AppContext::refreshAutostartIfStale()
{
    // Self-heal a stale Exec (binary rebuilt to a new path / AppImage moved),
    // mirroring ensureDesktopFile() — otherwise the toggle reads "on" while
    // login autostart silently launches nothing.
    const QString path = autostartFilePath();
    if (!QFile::exists(path))
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray data = f.readAll();
    f.close();
    if (!data.contains(autostartExecLine()))
        writeAutostartFile();
}

void AppContext::setAutostartEnabled(bool on)
{
    if (on == autostartEnabled())
        return;
    const QString path = autostartFilePath();
    if (!on) {
        if (!QFile::remove(path) && QFile::exists(path))
            showToast(tr("Could not disable autostart — cannot remove %1").arg(path), true);
        emit autostartEnabledChanged(); // reflect the real (post-remove) state
        return;
    }
    if (!writeAutostartFile()) {
        showToast(tr("Could not enable autostart — cannot write %1").arg(path), true);
        return; // state unchanged; the switch snaps back on the next read
    }
    emit autostartEnabledChanged();
}
