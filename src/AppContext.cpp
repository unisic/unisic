#include "AppContext.h"
#include "unisic_build_date.h" // generated into the build dir (cmake/BuildDate.cmake)
#include "Settings.h"
#include "capture/CaptureManager.h"
#include "capture/KWinScreenShot2.h"
#include "overlay/OverlayController.h"
#include "upload/UploadManager.h"
#include "actions/ExternalActionRunner.h"
#include "history/HistoryStore.h"
#include "history/HistoryFilterModel.h"
#include "hotkeys/GlobalHotkeys.h"
#include "hotkeys/ShortcutFormat.h"
#include "update/UpdateChecker.h"
#include "update/VersionCompare.h"
#include "hotkeys/PortalGlobalShortcuts.h"
#include "record/GifRecorder.h"
#include "record/TrimController.h"
#include "editor/EditorSession.h"
#include "editor/ImageEffects.h"
#include "PreviewController.h"
#include "notify/NotifCard.h"
#include "notify/CaptureNotification.h"
#include "notify/DesktopNotifier.h"
#include "notify/NotificationInhibitor.h"
#ifdef HAVE_LAYERSHELL
#include "notify/LayerShellNotifier.h"
#include <LayerShellQt/window.h>
#include <QMargins>
#endif
#include "theme/ThemeController.h"
#include "editor/AnnotationCanvas.h"
#include "ConfigPath.h"
#ifdef HAVE_TESSERACT
#include "ocr/OcrEngine.h"
#endif
#ifdef HAVE_ZXING
#include <ZXing/BitMatrix.h>
#include <ZXing/MultiFormatWriter.h>
#endif
#include <QGuiApplication>
#include <QtMath>
#include <QScreen>
#include <QRegion>
#include <QPointer>
#include <QClipboard>
#include <QQmlEngine>
#include <QQmlApplicationEngine>
#include <QTranslator>
#include <QLibraryInfo>
#include <QLocale>
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
#include <QElapsedTimer>
#include <QDir>
#include <QBuffer>
#include <QTimer>
#include <QProcess>
#include <QPainter>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUuid>
#include <QMetaProperty>
#include <QStandardPaths>
#include <QMouseEvent>
#include <QtConcurrentRun>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusConnectionInterface>
#include <QDebug>
#include <memory>
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
    m_dnd = new NotificationInhibitor(this);
    m_actionRunner = new ExternalActionRunner(this);
    refreshWatermarkImage();
    connect(m_settings, &Settings::watermarkImagePathChanged,
            this, &AppContext::refreshWatermarkImage);

    m_updater = new UpdateChecker(m_settings, this);
    // Tray entry follows availability flips only — stateChanged also fires per
    // download-progress chunk and would rebuild the tray continuously.
    connect(m_updater, &UpdateChecker::availabilityChanged, this, &AppContext::setupTray);
    connect(m_updater, &UpdateChecker::updateFound, this, [this](const QString &v) {
        showToast(m_updater->canSelfUpdate()
                      ? tr("Unisic %1 is available — updating automatically").arg(v)
                      : tr("Unisic %1 is available").arg(v));
    });
    connect(m_updater, &UpdateChecker::installed, this, [this](const QString &v) {
        setupTray(); // the entry flips to "Restart to update"
        if (tryUpdateRestart())
            return;
        // Busy right now — tell the user it's ready and keep retrying quietly
        // until the app goes idle (recording over, editors closed, window
        // hidden back into the tray).
        showToast(tr("Unisic %1 installed — it will start on the next launch").arg(v));
        if (!m_updateRestartTimer) {
            m_updateRestartTimer = new QTimer(this);
            m_updateRestartTimer->setInterval(60 * 1000);
            connect(m_updateRestartTimer, &QTimer::timeout, this, [this] {
                if (tryUpdateRestart())
                    m_updateRestartTimer->stop();
            });
        }
        m_updateRestartTimer->start();
    });

    connect(m_hotkeys, &GlobalHotkeys::activated, this, &AppContext::dispatchHotkey);
    // Live two-way sync: a KCM edit updates the app's stored/displayed key.
    connect(m_hotkeys, &GlobalHotkeys::shortcutChanged, this,
            &AppContext::syncHotkeyFromDaemon);

    connect(m_recorder, &GifRecorder::started, this, &AppContext::recordingChanged);
    // Portal approved + stream live, but encoding is HELD: run the countdown /
    // start cue, then commit. The start sound is played here (before commit), not
    // on started(), so it is never captured in the recording.
    connect(m_recorder, &GifRecorder::armed, this, [this] {
        if (!m_recordHoldActive)
            return;
        const int secs = m_pendingCountdownSecs;
        m_pendingCountdownSecs = 0;
        if (secs > 0)
            runRecordCountdownVisuals(secs); // ends in commitRecordingAfterCue()
        else
            commitRecordingAfterCue();       // start-sound pre-roll only
    });
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
        endDoNotDisturb();
        hideRecordBorder(); // capture is over; the frame must not linger over encoding
        emit recordingChanged();
        showToast(tr("Encoding…"));
    });
    connect(m_recorder, &GifRecorder::finished, this, &AppContext::onRecordingFinished);
    connect(m_recorder, &GifRecorder::replayExportFailed, this, [this](const QString &error) {
        showToast(tr("Instant replay failed: %1").arg(error), true);
    });
    connect(m_recorder, &GifRecorder::failed, this, [this](const QString &e) {
        m_converting = false;
        endDoNotDisturb();
        // A failure can land before arming (portal denied): clear the pending
        // hold so a later recording can't inherit it.
        m_pendingCountdownSecs = 0;
        m_recordHoldActive = false;
        hideRecordBorder();
        emit recordingChanged();
        if (e != QLatin1String("cancelled"))
            showToast(tr("Recording failed: %1").arg(e), true);
    });

    // A history file that could not be trashed still gets its entry removed;
    // let the user know the file is still on disk.
    connect(m_history, &HistoryStore::fileTrashFailed, this, [this](const QString &path) {
        showToast(tr("Could not move %1 to trash; the file is still on disk").arg(path), true);
    });

    // Fixed (non-configurable) trash cue on every explicit history deletion.
    connect(m_history, &HistoryStore::entryTrashed, this, &AppContext::playTrashSound);

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
    // Re-apply the UI language live when the setting changes (retranslate QML +
    // rebuild the tray menu). The initial install happens in main() before the
    // engine loads.
    connect(m_settings, &Settings::uiLanguageChanged, this, &AppContext::applyLanguage);

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

    // Daily release check + AppImage self-install (suppressed on dev builds
    // and when the setting is off — the checker logs why it stays quiet).
    m_updater->startAutoCheck();

    // ffmpeg's encoder list probe can take seconds on a cold filesystem. Run it
    // once off-thread; the recording UI updates when the result arrives.
    QPointer<AppContext> self(this);
    (void)QtConcurrent::run([self] {
        const bool vaapi = GifRecorder::hardwareEncoderAvailable(QStringLiteral("vaapi"));
        const bool nvenc = GifRecorder::hardwareEncoderAvailable(QStringLiteral("nvenc"));
        // Post to the always-alive application object and test the QPointer on
        // the GUI thread. Reading `self` HERE (worker thread) would race with
        // AppContext's destruction on the main thread — QPointer is not
        // thread-safe against concurrent clearing of its control block.
        QMetaObject::invokeMethod(qApp, [self, vaapi, nvenc] {
            if (!self) return;
            self->m_vaapiAvailable = vaapi;
            self->m_nvencAvailable = nvenc;
            emit self->recordingCapabilitiesChanged();
        }, Qt::QueuedConnection);
    });

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
    if (action == QLatin1String("copy-last")) { copyLastCapture(); return; }
    if (action == QLatin1String("capture-fullscreen")) {
        // Bail BEFORE writing the one-shot task/destination when a capture is
        // already in flight: otherwise this second hotkey overwrites the shared
        // members, then captureX's in-flight guard clears them — wiping the
        // IN-FLIGHT capture's task preset + upload destination.
        if (m_captureInFlight || m_overlay->active()) return;
        m_nextCaptureTask = taskFromId(m_settings->fullScreenTask());
        m_nextCaptureDestination = m_settings->fullScreenTaskDestination();
        captureFullScreen();
    } else if (action == QLatin1String("capture-region")) {
        if (m_captureInFlight || m_overlay->active()) return;
        m_nextCaptureTask = taskFromId(m_settings->regionTask());
        m_nextCaptureDestination = m_settings->regionTaskDestination();
        captureRegion();
    } else if (action == QLatin1String("capture-window")) {
        if (m_captureInFlight || m_overlay->active()) return;
        m_nextCaptureTask = taskFromId(m_settings->windowTask());
        m_nextCaptureDestination = m_settings->windowTaskDestination();
        captureWindow();
    }
    else if (action == QLatin1String("ocr-region")) captureRegionOcr();
    else if (action == QLatin1String("record-gif")) {
        if (recording()) stopRecording();
        else startGifRegion();
    } else if (action == QLatin1String("record-video")) {
        if (recording()) stopRecording();
        else startVideoRegion();
    } else if (action == QLatin1String("instant-replay")) {
        if (instantReplayActive()) saveInstantReplay();
        else if (!recording()) startInstantReplay();
    } else if (action == QLatin1String("smoke-test")) {
        if (devBuild()) runSmokeTest();
    }
}

AppContext::CaptureTask AppContext::taskFromId(const QString &id)
{
    if (id == QLatin1String("copy"))
        return {true, false, true, false, false};
    if (id == QLatin1String("edit"))
        return {true, false, false, true, false};
    if (id == QLatin1String("save"))
        return {true, true, false, false, false};
    if (id == QLatin1String("upload"))
        return {true, false, false, false, true};
    if (id == QLatin1String("copy-save"))
        return {true, true, true, false, false};
    if (id == QLatin1String("copy-edit"))
        return {true, false, true, true, false};
    if (id == QLatin1String("copy-upload"))
        return {true, false, true, false, true};
    if (id == QLatin1String("save-upload"))
        return {true, true, false, false, true};
    if (id == QLatin1String("copy-save-upload"))
        return {true, true, true, false, true};
    if (id == QLatin1String("all"))
        return {true, true, true, true, true};
    return {};
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

bool AppContext::capPipeWireBuild() const
{
#ifdef HAVE_PIPEWIRE
    return true;
#else
    return false;
#endif
}

bool AppContext::capScreenCastPortal() const
{
    // Probed on the session bus at startup, independent of HAVE_PIPEWIRE: a
    // dev-tab "—" here means the desktop has no ScreenCast portal backend even
    // when the build does have PipeWire.
    return m_screenCastPortalPresent;
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

bool AppContext::perAppAudioAvailable() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("pw-record")).isEmpty()
           && !QStandardPaths::findExecutable(QStringLiteral("pw-dump")).isEmpty();
}

// Pure: runs pw-dump + parses its JSON with no AppContext/GUI state, so it is
// safe to call from a worker thread (see requestAudioApplicationNodes).
static QVariantList queryAudioApplicationNodesImpl()
{
    QVariantList result;
    const QString helper = QStandardPaths::findExecutable(QStringLiteral("pw-dump"));
    if (helper.isEmpty())
        return result;
    QProcess process;
    process.start(helper, {});
    if (!process.waitForFinished(2500)) {
        process.kill();
        return result;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(process.readAllStandardOutput());
    if (!doc.isArray())
        return result;
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("type")).toString()
            != QLatin1String("PipeWire:Interface:Node"))
            continue;
        const QJsonObject props = object.value(QStringLiteral("info")).toObject()
                                      .value(QStringLiteral("props")).toObject();
        if (props.value(QStringLiteral("media.class")).toString()
            != QLatin1String("Stream/Output/Audio"))
            continue;
        const QString id = props.value(QStringLiteral("object.serial")).toVariant().toString();
        if (id.isEmpty())
            continue;
        QString label = props.value(QStringLiteral("application.name")).toString();
        if (label.isEmpty())
            label = props.value(QStringLiteral("node.description")).toString();
        if (label.isEmpty())
            label = props.value(QStringLiteral("node.name")).toString();
        result.append(QVariantMap{{QStringLiteral("id"), id},
                                  {QStringLiteral("label"), label}});
    }
    return result;
}

QVariantList AppContext::audioApplicationNodes() const
{
    return queryAudioApplicationNodesImpl();
}

void AppContext::requestAudioApplicationNodes()
{
    // pw-dump can stall for up to 2.5s on a cold/heavy PipeWire graph; running
    // it synchronously froze the whole UI when the audio dropdown opened. Do it
    // off the GUI thread and deliver via a queued signal.
    QPointer<AppContext> self(this);
    (void)QtConcurrent::run([self] {
        const QVariantList nodes = queryAudioApplicationNodesImpl();
        QMetaObject::invokeMethod(qApp, [self, nodes] {
            if (self) emit self->audioApplicationNodesReady(nodes);
        }, Qt::QueuedConnection);
    });
}

// zxing-cpp is already linked for QR decoding. Encoding a small preview is
// synchronous and bounded (360² pixels); unlike screenshot PNG work it never
// needs a worker or retains a full capture-sized buffer.
static QImage qrPreviewImage(const QString &url)
{
#ifdef HAVE_ZXING
    const QByteArray utf8 = url.toUtf8();
    if (utf8.isEmpty() || utf8.size() > 2048)
        return {};
    try {
        ZXing::MultiFormatWriter writer(ZXing::BarcodeFormat::QRCode);
        writer.setMargin(2).setEccLevel(2);
        const ZXing::BitMatrix bits = writer.encode(utf8.toStdString(), 360, 360);
        if (bits.width() <= 0 || bits.height() <= 0)
            return {};
        QImage image(bits.width(), bits.height(), QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < bits.height(); ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < bits.width(); ++x)
                row[x] = bits.get(x, y) ? qRgba(0, 0, 0, 255) : qRgba(255, 255, 255, 255);
        }
        return image;
    } catch (const std::exception &) {
        return {};
    }
#else
    Q_UNUSED(url)
    return {};
#endif
}

QString AppContext::buildDate() const
{
    return QStringLiteral(UNISIC_BUILD_DATE);
}

QString AppContext::changelog(const QString &lang) const
{
    QFile f(QStringLiteral(":/resources/CHANGELOG.md"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    const QString verHeading = QStringLiteral("## ") + appVersion();
    const QString langHeading = QStringLiteral("### ")
        + (lang == QLatin1String("pl") ? QStringLiteral("Polski") : QStringLiteral("English"));
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    QStringList body;
    bool inVersion = false;
    bool inLang = false;
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        // `### ` language sub-heading: switch language capture within the version.
        if (t.startsWith(QLatin1String("### "))) {
            if (inVersion)
                inLang = (t == langHeading);
            continue;
        }
        // `## ` version heading: enter our version; any later `## ` ends it.
        if (t.startsWith(QLatin1String("## "))) {
            if (!inVersion && t == verHeading) {
                inVersion = true;
                inLang = false;
                continue;
            }
            if (inVersion)
                break;
            continue;
        }
        if (inVersion && inLang)
            body.append(line);
    }
    return body.join(QLatin1Char('\n')).trimmed();
}

void AppContext::markPatchNotesSeen()
{
    if (m_settings->lastSeenVersion() == appVersion())
        return;
    m_settings->setLastSeenVersion(appVersion());
    emit patchNotesUnseenChanged();
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

void AppContext::ocrBoxes(const QImage &img,
                          std::function<void(const QVector<OcrWord> &, const QString &)> cb)
{
#ifdef HAVE_TESSERACT
    if (img.isNull()) {
        cb({}, tr("Nothing to recognize"));
        return;
    }
    m_ocr->recognizeBoxes(img, m_settings->ocrLanguages(), std::move(cb));
#else
    Q_UNUSED(img);
    cb({}, tr("OCR is not available in this build"));
#endif
}

// ---------------------------------------------------------------- language

void AppContext::applyLanguage()
{
    const QString pref = m_settings->uiLanguage();
    // Every language (incl. English) has its own .qm, so English text is
    // editable in i18n/unisic_en.ts without touching source strings.
    static const QStringList supported = {QStringLiteral("en"), QStringLiteral("pl"),
                                          QStringLiteral("es"), QStringLiteral("it")};
    QString code;
    if (supported.contains(pref)) {
        code = pref;
    } else { // "system"
        const QString sys = QLocale::system().name().left(2); // e.g. "pl_PL" → "pl"
        code = supported.contains(sys) ? sys : QStringLiteral("en");
    }

    if (m_appTranslator) {
        qApp->removeTranslator(m_appTranslator);
        delete m_appTranslator;
        m_appTranslator = nullptr;
    }
    if (m_qtTranslator) {
        qApp->removeTranslator(m_qtTranslator);
        delete m_qtTranslator;
        m_qtTranslator = nullptr;
    }
#ifdef HAVE_TRANSLATIONS
    if (!code.isEmpty()) {
        auto *appTr = new QTranslator(this);
        if (appTr->load(QStringLiteral(":/i18n/unisic_%1.qm").arg(code))) {
            qApp->installTranslator(appTr);
            m_appTranslator = appTr;
        } else {
            delete appTr;
        }
        // Qt's own dialog strings for the locale (from the system Qt install).
        auto *qtTr = new QTranslator(this);
        if (qtTr->load(QLocale(code), QStringLiteral("qtbase"), QStringLiteral("_"),
                       QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
            qApp->installTranslator(qtTr);
            m_qtTranslator = qtTr;
        } else {
            delete qtTr;
        }
    }
#endif
    // Live refresh (no-op before the engine has loaded): re-evaluate every qsTr
    // binding, and rebuild the C++-constructed tray menu.
    if (m_engine)
        m_engine->retranslate();
    if (m_tray)
        setupTray();
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
    if (m_shortcutRecording == recording)
        return;
    m_shortcutRecording = recording;
    emit shortcutRecordingChanged();
}

void AppContext::setNextCaptureDelayMs(int delayMs)
{
    // Keep the one-shot CLI input bounded even when it came from the local
    // socket. A forgotten multi-hour timer is worse than rejecting a typo.
    m_nextCaptureDelayMs = qBound(0, delayMs, 60 * 1000);
}

void AppContext::setNextCaptureOutput(const QString &path, const QString &format,
                                      bool toStdout)
{
    m_nextCaptureOutputPath = path;
    m_nextCaptureOutputFormat = format.toLower();
    m_nextCaptureToStdout = toStdout;
    // CLI output is a task of its own: do not also mutate clipboard, history,
    // editor or upload state from the resident process's personal defaults.
    m_nextCaptureTask = {true, !path.isEmpty(), false, false, false};
    m_nextCaptureDestination.clear();
}

void AppContext::clearCliCapture(const QString &error)
{
    const bool stdoutPending = m_nextCaptureToStdout;
    m_nextCaptureOutputPath.clear();
    m_nextCaptureOutputFormat.clear();
    m_nextCaptureToStdout = false;
    m_nextCaptureDestination.clear();
    if (stdoutPending && !error.isEmpty())
        emit cliCaptureReady({}, error);
}

void AppContext::withDelay(std::function<void()> fn)
{
    const int delay = m_nextCaptureDelayMs >= 0
                      ? m_nextCaptureDelayMs : qMax(0, m_settings->captureDelayMs());
    m_nextCaptureDelayMs = -1; // a CLI override applies to exactly one capture
    if (delay < 1000) {
        QTimer::singleShot(delay, this, std::move(fn));
        return;
    }

    // A handful of once-per-second updates make the configured 3/5/10 s
    // timer tangible without a hot repaint loop or a capture-sized allocation.
    // This is intentionally a toast: a Wayland capture may be fullscreen,
    // window-only, or an overlay selected later, so no compositor surface can
    // be shown safely before every backend has started.
    const auto remaining = std::make_shared<int>((delay + 999) / 1000);
    showToast(tr("Capture in %1…").arg(*remaining));
    auto *timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, [this, timer, remaining, fn = std::move(fn)]() mutable {
        if (--(*remaining) > 0) {
            showToast(tr("Capture in %1…").arg(*remaining));
            return;
        }
        timer->stop();
        timer->deleteLater();
        fn();
    });
    timer->start();
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
        text += tr(". Install Unisic (sudo cmake --install build) and launch it from the "
                   "application menu so KDE authorizes it, and check that "
                   "xdg-desktop-portal-kde is running.");
    else if (desktop.contains(QLatin1String("GNOME"), Qt::CaseInsensitive))
        // "code 2" with no dialog = the permission store holds a sticky "no"
        // (a once-denied GNOME access dialog). GNOME Settings does not list
        // host apps, so name the actual repair command.
        text += tr(". GNOME is blocking silent screenshots for Unisic — run "
                   "\"flatpak permission-reset screenshot\" and retry, and check that "
                   "xdg-desktop-portal-gnome is running.");
    else if (!err.contains(QLatin1String("grim")))
        // The capture chain's own rescue may already carry grim advice
        // (with per-desktop rationale) — don't tell the user twice.
        text += tr(". Install 'grim' (works on sway/niri/Hyprland-style compositors) or an "
                   "xdg-desktop-portal backend for your desktop.");
    return text;
}

bool AppContext::nowInhibited() const
{
    return m_notifier && m_notifier->inhibited();
}

bool AppContext::capDoNotDisturb() const
{
    return NotificationInhibitor::supportedDesktop();
}

bool AppContext::capScreenshotCursor() const
{
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    if (desktop.contains(QLatin1String("KDE"), Qt::CaseInsensitive))
        return true;
    if (!QStandardPaths::findExecutable(QStringLiteral("grim")).isEmpty()
        && !desktop.contains(QLatin1String("GNOME"), Qt::CaseInsensitive))
        return true;
    return false;
}

void AppContext::beginDoNotDisturb()
{
    if (m_settings->doNotDisturbWhileCapturing() && capDoNotDisturb())
        m_dnd->acquire();
}

void AppContext::endDoNotDisturb()
{
    if (m_dnd)
        m_dnd->release();
}

void AppContext::captureFullScreen()
{
    // In-flight guard: hammering the hotkey must not stack portal requests.
    // Overlay guard: with the region-selection overlay open, a stray
    // fullscreen/window hotkey would capture the overlay's own dimming and
    // toolbar and push that garbage through the whole after-capture pipeline.
    if (m_captureInFlight || m_overlay->active()) {
        m_nextCaptureTask = {};
        clearCliCapture(tr("Another capture is already active"));
        return;
    }
    const bool inhibited = nowInhibited();
    m_captureInFlight = true;
    beginDoNotDisturb();
    withDelay([this, inhibited] {
        // Re-check: with a capture delay configured, the region overlay may
        // have opened between the keypress and this deferred fire.
        if (m_overlay->active()) {
            m_captureInFlight = false;
            endDoNotDisturb();
            m_nextCaptureTask = {};
            clearCliCapture(tr("Another capture is already active"));
            return;
        }
        m_capture->captureWorkspace([this, inhibited](const QImage &img, const QString &err) {
            m_captureInFlight = false;
            endDoNotDisturb();
            if (!err.isEmpty()) {
                m_nextCaptureTask = {};
                clearCliCapture(err);
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
    captureRegionWithTool(AnnotationCanvas::None);
}

void AppContext::captureMeasure()
{
    captureRegionWithTool(AnnotationCanvas::Measure);
}

void AppContext::captureRegionWithTool(int initialTool)
{
    if (m_captureInFlight || m_overlay->active()) {
        m_nextCaptureTask = {};
        clearCliCapture(tr("Another capture is already active"));
        return;
    }
    const bool inhibited = nowInhibited(); // before the fullscreen overlay opens
    m_captureInFlight = true;
    beginDoNotDisturb();
    withDelay([this, inhibited, initialTool] {
        if (m_overlay->active()) {
            m_captureInFlight = false;
            endDoNotDisturb();
            m_nextCaptureTask = {};
            clearCliCapture(tr("Another capture is already active"));
            return;
        }
        m_overlay->pickAnnotatedImage([this, inhibited](const QImage &img) {
            m_captureInFlight = false;
            endDoNotDisturb();
            if (!img.isNull())
                finishCapture(img, inhibited, m_overlay->takeCopyRequested());
            else
            {
                m_nextCaptureTask = {};
                clearCliCapture(tr("Capture cancelled"));
            }
        }, initialTool);
    });
}

void AppContext::captureRegionOcr()
{
    if (m_captureInFlight || m_overlay->active())
        return;
    m_captureInFlight = true;
    beginDoNotDisturb();
    withDelay([this] {
        if (m_overlay->active()) {
            m_captureInFlight = false;
            endDoNotDisturb();
            return;
        }
        m_overlay->pickAnnotatedImage([this](const QImage &img) {
            m_captureInFlight = false;
            endDoNotDisturb();
            if (!img.isNull())
                ocrImage(img);   // recognizes (QR first, then text) + copies
        });
    });
}

void AppContext::captureWindow()
{
    if (m_captureInFlight || m_overlay->active()) { // see captureFullScreen
        m_nextCaptureTask = {};
        clearCliCapture(tr("Another capture is already active"));
        return;
    }
    const bool inhibited = nowInhibited();
    m_captureInFlight = true;
    beginDoNotDisturb();
    withDelay([this, inhibited] {
        if (m_overlay->active()) { // re-check after the capture delay
            m_captureInFlight = false;
            endDoNotDisturb();
            m_nextCaptureTask = {};
            clearCliCapture(tr("Another capture is already active"));
            return;
        }
        m_capture->captureActiveWindow([this, inhibited](const QImage &img, const QString &err) {
            m_captureInFlight = false;
            endDoNotDisturb();
            if (!err.isEmpty()) {
                m_nextCaptureTask = {};
                clearCliCapture(err);
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
        startRecorderCountdown([this, phys, screen](bool hold) {
            m_recorder->start(GifRecorder::Gif, GifRecorder::Region, phys, screen, hold);
        });
    });
}

void AppContext::startGifFullScreen()
{
    if (recording()) return;
    m_pendingRecordRegion = QRect();
    startRecorderCountdown([this](bool hold) {
        m_recorder->start(GifRecorder::Gif, GifRecorder::Screen, {}, nullptr, hold);
    });
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
    startRecorderCountdown([this](bool hold) {
        m_recorder->start(videoOutput(), GifRecorder::Screen, {}, nullptr, hold);
    });
}

void AppContext::startVideoRegion()
{
    if (recording()) return;
    m_overlay->pickRegion([this](const QRect &phys, QScreen *screen) {
        if (phys.isEmpty()) return;
        m_pendingRecordRegion = phys;
        m_pendingRecordScreen = screen;
        startRecorderCountdown([this, phys, screen](bool hold) {
            m_recorder->start(videoOutput(), GifRecorder::Region, phys, screen, hold);
        });
    });
}

void AppContext::startVideoWindow()
{
    if (recording()) return;
    m_pendingRecordRegion = QRect();
    startRecorderCountdown([this](bool hold) {
        m_recorder->start(videoOutput(), GifRecorder::Window, {}, nullptr, hold);
    });
}

void AppContext::stopRecording()
{
    m_recorder->stop();
}

void AppContext::startInstantReplay()
{
    if (recording()) return;
    m_pendingRecordRegion = {};
    startRecorderCountdown([this](bool hold) {
        m_recorder->start(GifRecorder::Replay, GifRecorder::Screen, {}, nullptr, hold);
    });
}

void AppContext::saveInstantReplay()
{
    if (!instantReplayActive()) {
        showToast(tr("Start instant replay first"), true);
        return;
    }
    m_recorder->saveInstantReplay();
    showToast(tr("Saving instant replay…"));
}

void AppContext::startRecorderCountdown(std::function<void(bool)> begin)
{
    const int secs = qBound(0, m_settings->recordCountdownSec(), 10);
    const bool hasStartCue = m_settings->recordStartSound() != QLatin1String("off")
                             && m_settings->soundVolume() > 0;
    // No countdown AND no start cue: nothing to sequence — record immediately.
    if (secs <= 0 && !hasStartCue) {
        beginDoNotDisturb();
        begin(false);
        return;
    }
    if (m_recordHoldActive)
        return; // a second trigger while a hold is pending must not stack
    m_recordHoldActive = true;
    beginDoNotDisturb();
    m_pendingCountdownSecs = secs;
    // Portal negotiates FIRST (its share dialog). The recorder holds encoding
    // until commit(); armed() drives the countdown/cue below, then commits.
    begin(true);
}

void AppContext::runRecordCountdownVisuals(int secs)
{
    // Region recordings: show the frame with the number ticking INSIDE it.
    // showRecordBorder() copies the region by value before hideRecordBorder()
    // clears m_pendingRecordRegion, so the later started-signal reshow is
    // skipped (pending rect now empty). Other sources — and the GNOME helper
    // frame without a region — use toasts.
    bool inFrame = false;
    if (!m_pendingRecordRegion.isEmpty() && m_pendingRecordScreen) {
        showRecordBorder(m_pendingRecordRegion, m_pendingRecordScreen, secs);
        // Both the in-process frame (KDE/wlroots) and the XWayland helper frame
        // (GNOME) render the number — the helper is fed over stdin.
        inFrame = (m_recordBorderWindow != nullptr || m_recordBorderHelper != nullptr);
    }
    if (!inFrame)
        showToast(tr("Recording in %1…").arg(secs));

    auto remaining = std::make_shared<int>(secs);
    auto *timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, [this, timer, remaining, inFrame]() {
        if (--(*remaining) > 0) {
            if (inFrame)
                setRecordBorderCountdown(*remaining);
            else
                showToast(tr("Recording in %1…").arg(*remaining));
            return;
        }
        timer->stop();
        timer->deleteLater();
        commitRecordingAfterCue();
    });
    timer->start();
}

void AppContext::commitRecordingAfterCue()
{
    m_recordHoldActive = false;
    // Clear the countdown number FIRST so the compositor repaints without it —
    // otherwise the "1" leaks into the recording's first frames.
    const bool inFrame = (m_recordBorderWindow != nullptr || m_recordBorderHelper != nullptr);
    if (inFrame)
        setRecordBorderCountdown(0);
    // Play the start cue NOW, before encoding — it plays out through the speakers
    // and so is never captured in a system-audio recording.
    const bool hasStartCue = m_settings->recordStartSound() != QLatin1String("off")
                             && m_settings->soundVolume() > 0;
    if (hasStartCue)
        playRecordStartSound();
    // Tail before encoding actually starts: always enough for the cleared frame
    // to repaint, and — when a start cue plays — long enough for it to finish so
    // it isn't captured. Sized to the cue's own length (clamped) when known.
    int tail;
    if (hasStartCue) {
        const int dur = soundDurationMs(m_settings->recordStartSound());
        tail = qBound(150, dur > 0 ? dur + 70 : 550, 900);
    } else {
        tail = inFrame ? 150 : 0;
    }
    if (tail <= 0) {
        m_recorder->commit();
        return;
    }
    QTimer::singleShot(tail, this, [this]() { m_recorder->commit(); });
}

void AppContext::setRecordBorderCountdown(int n)
{
    if (m_recordBorderWindow)
        m_recordBorderWindow->setProperty("countdown", n);
    else if (m_recordBorderHelper
             && m_recordBorderHelper->state() != QProcess::NotRunning)
        m_recordBorderHelper->write(QByteArray("c") + QByteArray::number(n) + "\n");
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

// Detect the player once (PipeWire → Pulse → ALSA); shelling out keeps
// QtMultimedia off the dependency list.
static QString soundPlayer()
{
    static const QString player = [] {
        for (const QString &p : {QStringLiteral("pw-play"), QStringLiteral("paplay"),
                                 QStringLiteral("aplay")}) {
            const QString found = QStandardPaths::findExecutable(p);
            if (!found.isEmpty())
                return found;
        }
        return QString();
    }();
    return player;
}

static const QStringList &bundledSoundIds()
{
    static const QStringList ids{QStringLiteral("shutter"), QStringLiteral("click"),
                                 QStringLiteral("beep"), QStringLiteral("ding"),
                                 QStringLiteral("pop"), QStringLiteral("chime"),
                                 QStringLiteral("blip"), QStringLiteral("snap"),
                                 QStringLiteral("knock")};
    return ids;
}

// aplay decodes WAV only; OGG needs pw-play/paplay.
static bool soundPlayerTakesOgg()
{
    return QFileInfo(soundPlayer()).fileName() != QLatin1String("aplay");
}

static QImage devTestImage()
{
    QImage img(320, 200, QImage::Format_ARGB32);
    img.fill(QColor(0x2E, 0x23, 0x6C));
    return img;
}

// Text-annotation render check: a multi-line, italic, underlined, outlined
// and backgrounded text composited onto a known base must actually change a
// meaningful number of pixels (dev button + smoke step).
static QString textRenderCheck()
{
    AnnotationCanvas c;
    QImage base(320, 160, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    c.setImage(base);
    c.setFontSize(28);
    c.setFontItalic(true);
    c.setFontUnderline(true);
    c.setTextOutline(true);
    c.setTextBackground(true);
    c.commitText(20, 30, QStringLiteral("multi\nline"));
    const QImage out = c.rendered();
    if (out.size() != base.size())
        return QStringLiteral("FAIL (size changed)");
    int diff = 0;
    for (int y = 0; y < out.height(); y += 4)
        for (int x = 0; x < out.width(); x += 4)
            if (out.pixel(x, y) != base.pixel(x, y))
                ++diff;
    return diff > 20 ? QStringLiteral("PASS (%1 sampled pixels changed)").arg(diff)
                     : QStringLiteral("FAIL (render left the base blank)");
}

// Clipboard paste must create a real text annotation and retain a pasted image
// for the composited export. Shared by the developer button and F8 smoke test.
static QString clipboardPasteCheck()
{
    AnnotationCanvas canvas;
    QImage base(200, 120, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    canvas.setImage(base);
    auto *clipboard = QGuiApplication::clipboard();
    clipboard->setText(QStringLiteral("Unisic paste"));
    if (!canvas.pasteClipboard(30, 30) || canvas.annotCount() != 1)
        return QStringLiteral("FAIL (text was not pasted)");
    QImage stamp(24, 16, QImage::Format_ARGB32_Premultiplied);
    stamp.fill(Qt::black);
    clipboard->setImage(stamp);
    if (!canvas.pasteClipboard(100, 60) || canvas.annotCount() != 2)
        return QStringLiteral("FAIL (image was not pasted)");
    if (canvas.rendered() == base)
        return QStringLiteral("FAIL (paste missing from export)");
    return QStringLiteral("PASS (text + image annotation)");
}

// Watermarking is deliberately export-only: it must change the one image that
// reaches every after-capture action, while leaving its dimensions intact.
static QString watermarkCheck()
{
    QImage base(240, 140, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::black);
    const QImage stamped = UnisicImageEffects::watermarkText(
        base, QStringLiteral("Unisic"), 75, QStringLiteral("bottom-right"));
    QImage logo(80, 40, QImage::Format_ARGB32_Premultiplied);
    logo.fill(Qt::transparent);
    QPainter painter(&logo);
    painter.fillRect(QRect(5, 5, 70, 30), Qt::white);
    painter.end();
    const QImage logoStamped = UnisicImageEffects::watermarkImage(
        base, logo, 75, QStringLiteral("top-left"));
    return stamped.size() == base.size() && stamped != base
               && logoStamped.size() == base.size() && logoStamped != base
               ? QStringLiteral("PASS (text + logo in image pixels)")
               : QStringLiteral("FAIL (stamp missing or resized image)");
}

static QString calloutCheck()
{
    class InputCanvas final : public AnnotationCanvas {
    public:
        using AnnotationCanvas::mouseMoveEvent;
        using AnnotationCanvas::mousePressEvent;
        using AnnotationCanvas::mouseReleaseEvent;
    };
    InputCanvas canvas;
    canvas.setWidth(220);
    canvas.setHeight(160);
    QImage base(220, 160, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    canvas.setImage(base);
    canvas.setTool(AnnotationCanvas::Callout);
    canvas.setStrokeColor(Qt::black);
    canvas.setShapeFillColor(Qt::black);
    canvas.setShapeFillEnabled(true);
    const QPointF from(30, 20), to(180, 100);
    QMouseEvent press(QEvent::MouseButtonPress, from, from, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, to, to, Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, to, to, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    canvas.mousePressEvent(&press);
    canvas.mouseMoveEvent(&move);
    canvas.mouseReleaseEvent(&release);
    const QImage out = canvas.rendered();
    return canvas.annotCount() == 1 && out.pixelColor(38, 110).lightness() < 80
               ? QStringLiteral("PASS (bubble + tail)")
               : QStringLiteral("FAIL (bubble did not render)");
}

static QString shiftSnapCheck()
{
    class InputCanvas final : public AnnotationCanvas {
    public:
        using AnnotationCanvas::mouseMoveEvent;
        using AnnotationCanvas::mousePressEvent;
        using AnnotationCanvas::mouseReleaseEvent;
    };
    InputCanvas canvas;
    QImage base(100, 100, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    canvas.setImage(base);
    canvas.setTool(AnnotationCanvas::Line);
    canvas.setStrokeColor(Qt::black);
    canvas.setStrokeWidth(4);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, QPointF(50, 25), QPointF(50, 25),
                     Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(50, 25), QPointF(50, 25),
                        Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    canvas.mousePressEvent(&press);
    canvas.mouseMoveEvent(&move);
    canvas.mouseReleaseEvent(&release);
    return canvas.rendered().pixelColor(44, 10).lightness() < 80
               ? QStringLiteral("PASS (45° + grid)")
               : QStringLiteral("FAIL (line not constrained)");
}

static QString externalActionCheck()
{
    QString program, output, error;
    QStringList args;
    const QString input = QDir::temp().filePath(QStringLiteral("Unisic input.png"));
    const bool ok = ExternalActionRunner::expandCommand(
        QStringLiteral("true --source $input --dest $output"), input,
        &program, &args, &output, &error);
    if (!ok)
        return QStandardPaths::findExecutable(QStringLiteral("true")).isEmpty()
                   ? QStringLiteral("SKIP (no true helper)")
                   : QStringLiteral("FAIL (%1)").arg(error);
    return args.contains(input) && args.contains(output)
               && output.endsWith(QLatin1String("-action.png"))
               ? QStringLiteral("PASS (direct argv + tokens)")
               : QStringLiteral("FAIL (token expansion)");
}

static QString measureToolsCheck()
{
    class InputCanvas final : public AnnotationCanvas {
    public:
        using AnnotationCanvas::mouseMoveEvent;
        using AnnotationCanvas::mousePressEvent;
        using AnnotationCanvas::mouseReleaseEvent;
    } canvas;
    QImage base(260, 160, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    canvas.setImage(base);
    canvas.setStrokeColor(Qt::black);
    const auto draw = [&canvas](AnnotationCanvas::Tool tool, QPointF a, QPointF b) {
        canvas.setTool(tool);
        QMouseEvent press(QEvent::MouseButtonPress, a, a, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove, b, b, Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, b, b, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        canvas.mousePressEvent(&press);
        canvas.mouseMoveEvent(&move);
        canvas.mouseReleaseEvent(&release);
    };
    draw(AnnotationCanvas::Measure, {20, 40}, {220, 40});
    return canvas.annotCount() == 1 && canvas.rendered() != base
               ? QStringLiteral("PASS (ruler)")
               : QStringLiteral("FAIL (annotation missing)");
}

void AppContext::devTestTextRender()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: text render: %1").arg(textRenderCheck()));
}

void AppContext::devTestClipboardPaste()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: clipboard paste: %1").arg(clipboardPasteCheck()));
}

void AppContext::devTestCaptureDelay()
{
    if (!devBuild())
        return;
    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    setNextCaptureDelayMs(1100);
    withDelay([this, elapsed] {
        showToast(tr("Dev: capture delay: %1")
                  .arg(elapsed->elapsed() >= 1000 ? QStringLiteral("PASS")
                                                   : QStringLiteral("FAIL")));
    });
}

void AppContext::devTestCopyAs()
{
    if (!devBuild())
        return;
    const QString path = QDir::temp().filePath(QStringLiteral("unisic-copy-as-test.png"));
    const QString url = QStringLiteral("https://example.invalid/capture.png");
    copyImageAs({}, path, {}, QStringLiteral("path"));
    const bool pathOk = QGuiApplication::clipboard()->text() == path;
    copyImageAs({}, path, url, QStringLiteral("markdown"));
    const bool markdownOk = QGuiApplication::clipboard()->text() == QStringLiteral("![](%1)").arg(url);
    copyImageAs({}, path, url, QStringLiteral("html"));
    const bool htmlOk = QGuiApplication::clipboard()->text()
                        == QStringLiteral("<img src=\"%1\" alt=\"\">").arg(url);
    showToast(tr("Dev: copy as: %1")
              .arg(pathOk && markdownOk && htmlOk
                       ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
}

void AppContext::devTestWatermark()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: watermark: %1").arg(watermarkCheck()));
}

void AppContext::devTestCallout()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: callout: %1").arg(calloutCheck()));
}

void AppContext::devTestShiftSnap()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: Shift snap: %1").arg(shiftSnapCheck()));
}

void AppContext::devTestQrPreview()
{
    if (!devBuild())
        return;
    showQr(QStringLiteral("https://example.invalid/unisic-qr-test"));
}

void AppContext::devTestDoNotDisturb()
{
    if (!devBuild())
        return;
    if (!capDoNotDisturb()) {
        showToast(tr("Dev: do not disturb: unsupported on this desktop"), true);
        return;
    }
    m_dnd->acquire();
    const bool active = m_dnd->active();
    QTimer::singleShot(800, this, [this, active] {
        m_dnd->release();
        showToast(tr("Dev: do not disturb: %1")
                      .arg(active ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
    });
}

void AppContext::devTestExternalAction()
{
    if (devBuild())
        showToast(tr("Dev: external action: %1").arg(externalActionCheck()));
}

void AppContext::devTestTaskPreset()
{
    if (!devBuild())
        return;
    const CaptureTask copy = taskFromId(QStringLiteral("copy"));
    const CaptureTask all = taskFromId(QStringLiteral("all"));
    const CaptureTask normal = taskFromId(QStringLiteral("default"));
    showToast(tr("Dev: task preset: %1")
                  .arg(copy.active && copy.copy && !copy.save && !copy.edit
                               && !copy.upload && !normal.active
                               && all.active && all.copy && all.save && all.edit && all.upload
                           ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
}

void AppContext::devTestCliOutput()
{
    if (!devBuild())
        return;
    QTemporaryDir dir;
    const QString path = dir.isValid()
                             ? saveImageTo(devTestImage(), dir.path(),
                                           QStringLiteral("cli-output.png"))
                             : QString();
    QImageReader reader(path);
    showToast(tr("Dev: CLI output: %1")
                  .arg(!path.isEmpty() && reader.format().toLower() == "png"
                           ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
}


void AppContext::devTestMeasureTools()
{
    if (devBuild())
        showToast(tr("Dev: measure tools: %1").arg(measureToolsCheck()));
}

void AppContext::devTestHardwareEncoder()
{
    if (!devBuild()) return;
    showToast(tr("Dev: hardware encoder: %1")
                  .arg(m_vaapiAvailable || m_nvencAvailable
                           ? QStringLiteral("PASS") : QStringLiteral("SKIP")));
}

void AppContext::devTestPerAppAudio()
{
    if (!devBuild()) return;
    const QVariantList nodes = audioApplicationNodes();
    showToast(tr("Dev: per-app audio: %1")
                  .arg(!perAppAudioAvailable() ? QStringLiteral("SKIP")
                                               : QStringLiteral("PASS (%1 nodes)").arg(nodes.size())));
}

void AppContext::devTestInstantReplay()
{
    if (!devBuild()) return;
    if (!recordingAvailable()) {
        showToast(tr("Dev: instant replay: recording unavailable"), true);
        return;
    }
    if (instantReplayActive())
        saveInstantReplay();
    else if (!recording())
        startInstantReplay();
}

// Encoder for the trim self-test fixtures: the checks exercise trimming, not a
// specific codec, and a mandatory libx264 failed fixture creation outright on
// GPL-less ffmpeg builds. Same fallback order as the recorder — x264, OpenH264,
// then the always-built-in mpeg4 (stream copy and packet keyframe flags are
// codec-agnostic, so every trim path still gets exercised).
static QStringList trimFixtureEncoderArgs()
{
    if (GifRecorder::encoderUsable(QStringLiteral("libx264")))
        return {QStringLiteral("-c:v"), QStringLiteral("libx264"),
                QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p")};
    if (GifRecorder::encoderUsable(QStringLiteral("libopenh264")))
        return {QStringLiteral("-c:v"), QStringLiteral("libopenh264"),
                QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p")};
    return {QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
            QStringLiteral("-q:v"), QStringLiteral("5"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p")};
}

void AppContext::devTestTrimRecording()
{
    if (!devBuild()) return;
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        showToast(tr("Dev: trim recording: ffmpeg unavailable"), true);
        return;
    }
    const QString path = QDir::temp().filePath(QStringLiteral("unisic-trim-dev.mp4"));
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, path](int code, QProcess::ExitStatus) {
        process->deleteLater();
        if (code == 0)
            openTrimRecording(path);
        else
            showToast(tr("Dev: trim recording: FAIL"), true);
    });
    // Long enough, moving, and with a keyframe every second: the window has a
    // filmstrip whose tiles differ and real keyframe ticks to snap onto.
    QStringList args{QStringLiteral("-y"), QStringLiteral("-nostats"),
                     QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-f"), QStringLiteral("lavfi"),
                     QStringLiteral("-i"), QStringLiteral("testsrc=size=320x180:rate=30:duration=8"),
                     QStringLiteral("-g"), QStringLiteral("30")};
    args << trimFixtureEncoderArgs() << path;
    process->start(ffmpeg, args);
}

void AppContext::devTestTrimCut()
{
    if (!devBuild()) return;
    trimCutCheck([this](const QString &result) {
        showToast(tr("Dev: trim cut: %1").arg(result), result.contains(QLatin1String("FAIL")));
    });
}

void AppContext::trimCutCheck(std::function<void(const QString &)> done)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (ffmpeg.isEmpty() || ffprobe.isEmpty()) {
        done(QStringLiteral("SKIP (ffmpeg/ffprobe missing)"));
        return;
    }
    // A clip with a keyframe every 15 frames: the copy path needs somewhere to
    // snap to, and the moving pattern makes the filmstrip tiles differ.
    const QString source = QDir::temp().filePath(QStringLiteral("unisic-trimcheck.mp4"));
    QFile::remove(source);
    QStringList fixtureArgs{QStringLiteral("-y"), QStringLiteral("-nostats"),
                            QStringLiteral("-loglevel"), QStringLiteral("error"),
                            QStringLiteral("-f"), QStringLiteral("lavfi"),
                            QStringLiteral("-i"),
                            QStringLiteral("testsrc=size=160x90:rate=30:duration=3"),
                            QStringLiteral("-g"), QStringLiteral("15")};
    fixtureArgs << trimFixtureEncoderArgs() << source;
    QProcess::execute(ffmpeg, fixtureArgs);
    if (!QFileInfo::exists(source)) {
        done(QStringLiteral("FAIL (test clip)"));
        return;
    }
    const QString exact = QDir::temp().filePath(QStringLiteral("unisic-trimcheck-trimmed.mp4"));
    const QString copied = QDir::temp().filePath(QStringLiteral("unisic-trimcheck-copy-trimmed.mp4"));
    const QString copySource = QDir::temp().filePath(QStringLiteral("unisic-trimcheck-copy.mp4"));
    QFile::remove(exact);
    QFile::remove(copied);
    QFile::copy(source, copySource);

    // Same window the trim editor builds, on the same file.
    auto *probe = new TrimController(source, 3.0, 1.0 / 30, this);
    probe->buildFilmstrip(8, 48);
    probe->loadKeyframes();

    trimRecording(source, 0.5, 1.5, false);       // exact: re-encode
    trimRecording(copySource, 0.0, 1.0, true);    // lossless: stream copy

    QTimer::singleShot(3000, this, [this, probe, ffprobe, source, copySource,
                                    exact, copied, done] {
        const auto durationOf = [&ffprobe](const QString &path) -> qreal {
            QProcess p;
            p.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
                              QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                              QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"), path});
            if (!p.waitForFinished(2000))
                return -1;
            return QString::fromLatin1(p.readAllStandardOutput().trimmed()).toDouble();
        };
        const qreal exactDuration = durationOf(exact);
        const qreal copyDuration = durationOf(copied);
        const bool exactOk = qAbs(exactDuration - 1.0) < 0.15;
        const bool copyOk = qAbs(copyDuration - 1.0) < 0.25;   // ends on a whole packet
        const bool stripOk = probe->filmstripState() == TrimController::Ready;
        const bool keyframesOk = probe->keyframes().size() >= 2;
        // Snapping may only move the in-point backwards, never past the ask.
        const bool snapOk = keyframesOk && probe->snapStart(1.2) <= 1.2 + 0.001;
        probe->deleteLater();
        // The cuts stay: they went through the real path, so history now points
        // at them. Only the generated sources are scratch. A stale pair from an
        // earlier run is what the removals at the top of this check clear.
        QFile::remove(source);
        QFile::remove(copySource);
        done(QStringLiteral("exact %1 (%2s), lossless %3 (%4s), filmstrip %5, keyframes %6")
                 .arg(exactOk ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                 .arg(exactDuration, 0, 'f', 2)
                 .arg(copyOk ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                 .arg(copyDuration, 0, 'f', 2)
                 .arg(stripOk ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                      keyframesOk && snapOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
    });
}

void AppContext::devTestCursorCapability()
{
    if (devBuild())
        showToast(tr("Dev: screenshot cursor: %1")
                      .arg(capScreenshotCursor() ? QStringLiteral("PASS")
                                                 : QStringLiteral("SKIP")));
}

// EditShapes round-trip: place a text shape, select it, restyle + move it,
// assert the item changed and one undo restores it.
static QString shapeEditCheck()
{
    AnnotationCanvas c;
    QImage base(200, 120, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    c.setImage(base);
    c.commitText(40, 40, QStringLiteral("hi"));
    if (c.annotCount() != 1)
        return QStringLiteral("FAIL (text not placed)");
    c.setTool(AnnotationCanvas::EditShapes);
    c.selectAnnotAt(45, 45);
    if (!c.hasAnnotSelection())
        return QStringLiteral("FAIL (hit-test missed the text)");
    const QImage before = c.rendered();
    c.setStrokeColor(QColor(Qt::blue));
    c.nudgeSelectedAnnot(15, 0);
    const QImage after = c.rendered();
    if (before == after)
        return QStringLiteral("FAIL (restyle/move did not change the render)");
    c.undo(); // undoes the nudge...
    c.undo(); // ...and the color change (coalesced separately)
    if (c.annotCount() != 1)
        return QStringLiteral("FAIL (undo lost the shape)");
    return QStringLiteral("PASS (select, restyle, move, undo)");
}

void AppContext::devTestShapeEdit()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: shape edit: %1").arg(shapeEditCheck()));
}

// Capture-on-release: a synthetic selection drag must confirm exactly once on
// release with the toggle on, and never with it off (dev button + smoke step).
static QString captureOnReleaseCheck()
{
    struct Probe final : AnnotationCanvas {
        using AnnotationCanvas::mousePressEvent;
        using AnnotationCanvas::mouseMoveEvent;
        using AnnotationCanvas::mouseReleaseEvent;
    } c;
    c.setWidth(100);
    c.setHeight(100);
    QImage base(100, 100, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    c.setImage(base);
    c.setSelectionMode(true);
    c.setConfirmOnRelease(true);
    int confirms = 0;
    QObject::connect(&c, &AnnotationCanvas::selectionConfirmed, &c, [&confirms] { ++confirms; });
    const auto drag = [&c](QPointF from, QPointF to) {
        QMouseEvent p(QEvent::MouseButtonPress, from, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        c.mousePressEvent(&p);
        QMouseEvent m(QEvent::MouseMove, to, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        c.mouseMoveEvent(&m);
        QMouseEvent r(QEvent::MouseButtonRelease, to, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        c.mouseReleaseEvent(&r);
    };
    drag({10, 10}, {80, 60});
    if (confirms != 1)
        return QStringLiteral("FAIL (drag release confirmed %1x, expected once)").arg(confirms);
    c.setConfirmOnRelease(false);
    drag({85, 80}, {95, 95}); // outside the first selection: a fresh drag
    if (confirms != 1)
        return QStringLiteral("FAIL (confirmed with the toggle off)");
    return QStringLiteral("PASS (release confirms once; off = no confirm)");
}

void AppContext::devTestCaptureOnRelease()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: capture on release: %1").arg(captureOnReleaseCheck()));
}

// Magnifier round-trip: a synthetic drag over a marked source region must place
// a loupe that shows those pixels enlarged (2x, centred on the source). The
// probe point 6 px off-centre is inside the MAGNIFIED marker but outside the
// source marker, so it distinguishes a real 2x loupe from a 1:1 copy.
static QString magnifyCheck()
{
    struct Probe final : AnnotationCanvas {
        using AnnotationCanvas::mousePressEvent;
        using AnnotationCanvas::mouseMoveEvent;
        using AnnotationCanvas::mouseReleaseEvent;
    } c;
    c.setWidth(200);
    c.setHeight(200);
    QImage base(200, 200, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    {
        QPainter p(&base);
        p.fillRect(QRect(66, 66, 8, 8), QColor(220, 30, 40)); // marker at the source centre
    }
    c.setImage(base);
    c.setTool(AnnotationCanvas::Magnify);
    const auto drag = [&c](QPointF from, QPointF to) {
        QMouseEvent p(QEvent::MouseButtonPress, from, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        c.mousePressEvent(&p);
        QMouseEvent m(QEvent::MouseMove, to, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        c.mouseMoveEvent(&m);
        QMouseEvent r(QEvent::MouseButtonRelease, to, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        c.mouseReleaseEvent(&r);
    };
    drag({50, 50}, {90, 90});   // source = 40x40 centred on (70,70)
    if (c.annotCount() != 1)
        return QStringLiteral("FAIL (loupe not placed)");
    const QImage out = c.rendered();
    const QColor centre = out.pixelColor(70, 70);
    if (centre.red() < 150 || centre.green() > 120)
        return QStringLiteral("FAIL (loupe centre is not the magnified marker)");
    const QColor offCentre = out.pixelColor(76, 70);
    if (offCentre.red() < 150 || offCentre.green() > 120)
        return QStringLiteral("FAIL (loupe does not magnify — 2x expected)");
    return QStringLiteral("PASS (loupe placed, centred, 2x)");
}

void AppContext::devTestMagnify()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: magnifier: %1").arg(magnifyCheck()));
}

// Pixel loupe (region overlay): the panel must appear once the pointer hovers
// in selection mode, flip away from the item edges so it never covers the
// pixels being aimed at, react to Ctrl+scroll within its 4–16 clamp, and stay
// out of the exported render (dev button + smoke step).
static QString pixelLoupeCheck()
{
    struct Probe final : AnnotationCanvas {
        using AnnotationCanvas::hoverMoveEvent;
        using AnnotationCanvas::wheelEvent;
    } c;
    c.setWidth(500);
    c.setHeight(400);
    QImage base(500, 400, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    c.setImage(base);
    c.setSelectionMode(true);
    c.setPixelLoupe(true);
    c.setPixelLoupeZoom(8);
    if (!c.pixelLoupeRect().isEmpty())
        return QStringLiteral("FAIL (loupe visible before any hover)");
    const auto hover = [&c](QPointF at) {
        QHoverEvent e(QEvent::HoverMove, at, at, at);
        c.hoverMoveEvent(&e);
    };
    hover({50, 50});
    const QRectF nearOrigin = c.pixelLoupeRect();
    if (nearOrigin.isEmpty() || nearOrigin.left() <= 50 || nearOrigin.top() <= 50)
        return QStringLiteral("FAIL (loupe not offset below-right of the cursor)");
    hover({490, 390});
    const QRectF nearCorner = c.pixelLoupeRect();
    if (nearCorner.isEmpty() || nearCorner.right() >= 490 || nearCorner.bottom() >= 390)
        return QStringLiteral("FAIL (loupe does not flip away from the item edge)");
    const auto wheel = [&c](int delta) {
        QWheelEvent e(c.hoverPoint(), c.hoverPoint(), QPoint(), QPoint(0, delta),
                      Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        c.wheelEvent(&e);
    };
    wheel(120);
    if (c.pixelLoupeZoom() != 10)
        return QStringLiteral("FAIL (Ctrl+scroll did not raise the zoom)");
    for (int i = 0; i < 10; ++i)
        wheel(-120);
    if (c.pixelLoupeZoom() != 4)
        return QStringLiteral("FAIL (zoom did not clamp at 4)");
    if (c.rendered() != base)
        return QStringLiteral("FAIL (loupe leaked into the exported render)");
    return QStringLiteral("PASS (follows hover, edge flip, Ctrl+scroll 4–16, not exported)");
}

void AppContext::devTestPixelLoupe()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: pixel loupe: %1").arg(pixelLoupeCheck()));
}

// Renders two words and asserts recognizeBoxes returns ≥2 boxes with sane
// geometry — the selectable-text overlay's data source.
static QImage ocrBoxTestImage()
{
    QImage t(400, 120, QImage::Format_ARGB32);
    t.fill(Qt::white);
    QPainter p(&t);
    p.setPen(Qt::black);
    QFont f;
    f.setPixelSize(56);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(20, 20, 170, 80), Qt::AlignCenter, QStringLiteral("1234"));
    p.drawText(QRect(210, 20, 170, 80), Qt::AlignCenter, QStringLiteral("5678"));
    return t;
}

// Applies both OCR text-selection actions to real Tesseract glyph geometry.
// The canvas API itself owns the per-line batching; this harness verifies the
// action leaves exportable annotations and still has exactly one undo step.
static QString ocrHighlightCheck(const QVector<OcrWord> &words)
{
    if (words.isEmpty())
        return QStringLiteral("FAIL (no glyphs)");
    AnnotationCanvas canvas;
    const QImage base = ocrBoxTestImage();
    canvas.setImage(base);
    canvas.setOcrMode(true);
    canvas.setOcrWords(words);
    canvas.ocrSelectAll();
    if (!canvas.highlightOcrSelection() || canvas.annotCount() == 0)
        return QStringLiteral("FAIL (highlight not added)");
    const int highlighted = canvas.annotCount();
    canvas.undo();
    if (canvas.annotCount() != 0)
        return QStringLiteral("FAIL (highlight batch needs more than one undo)");
    canvas.redo();
    if (canvas.annotCount() != highlighted)
        return QStringLiteral("FAIL (highlight redo)");
    // highlightOcrSelection deliberately leaves OCR mode (it turns the transient
    // text selection into permanent marks), which clears the words + selection.
    // A second OCR action is a fresh pick, so re-enter and re-select before
    // redacting — exactly what the UI does for a new selection.
    canvas.setOcrMode(true);
    canvas.setOcrWords(words);
    canvas.ocrSelectAll();
    if (!canvas.redactOcrSelection(false) || canvas.annotCount() <= highlighted)
        return QStringLiteral("FAIL (redaction not added)");
    if (canvas.rendered() == base)
        return QStringLiteral("FAIL (annotations missing from export)");
    return QStringLiteral("PASS (%1 highlight bars + redaction, one undo)").arg(highlighted);
}

// Loads every bundled non-English .qm and checks a known string translates.
static QString languageCheck()
{
#ifdef HAVE_TRANSLATIONS
    const QStringList codes = {QStringLiteral("pl"), QStringLiteral("es"), QStringLiteral("it")};
    QStringList parts;
    for (const QString &c : codes) {
        QTranslator tr;
        if (!tr.load(QStringLiteral(":/i18n/unisic_%1.qm").arg(c)))
            return QStringLiteral("FAIL (unisic_%1.qm not loadable)").arg(c);
        const QString q = tr.translate("AppContext", "Quit");
        if (q.isEmpty() || q == QLatin1String("Quit"))
            return QStringLiteral("FAIL ('Quit' not translated in %1)").arg(c);
        parts << QStringLiteral("%1: '%2'").arg(c, q);
    }
    return QStringLiteral("PASS (Quit → %1)").arg(parts.join(QStringLiteral(", ")));
#else
    return QStringLiteral("SKIP (built without Qt LinguistTools)");
#endif
}

void AppContext::devTestLanguage()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: language: %1").arg(languageCheck()));
}

void AppContext::devTestUpdateCheck()
{
    if (!devBuild())
        return;
    // manual=true: visible errors, no toast/once-per-version bookkeeping.
    m_updater->check(true, [this](const UpdateChecker::Result &r) {
        showToast(tr("Dev: update check: %1")
                      .arg(r.ok ? QStringLiteral("PASS (latest %1 — %2)")
                                      .arg(r.latestVersion.isEmpty() ? QStringLiteral("none")
                                                                     : r.latestVersion,
                                           r.updateAvailable ? QStringLiteral("update available")
                                                             : QStringLiteral("up to date"))
                                : QStringLiteral("FAIL (%1)").arg(r.error)),
                  !r.ok);
    });
}

void AppContext::devTestUpdateAvailable()
{
    if (!devBuild())
        return;
    // Fake "update available" state: exercises the toast, the tray entry and
    // the Settings → General card without a newer release existing. "Update
    // now" then fails gracefully (no asset) unless UNISIC_UPDATE_FEED_URL
    // points at a fake feed.
    m_updater->simulateAvailable(QStringLiteral("99.0"));
}

void AppContext::devTestAutoRestart()
{
    if (!devBuild())
        return;
    const QString b = autoRestartBlockers();
    showToast(b.isEmpty() ? tr("Dev: auto-restart gate: idle — an installed update would restart now")
                          : tr("Dev: auto-restart gate: deferred (%1)").arg(b));
}

QString AppContext::autoRestartBlockers() const
{
    QStringList b;
    if (recording() || m_converting)
        b << tr("recording");
    if (m_captureInFlight)
        b << tr("capture in progress");
    if (m_overlay && m_overlay->active())
        b << tr("selection overlay open");
    if (m_editorWindows > 0)
        b << tr("editor windows open");
    if (mainWindowVisible())
        b << tr("main window visible");
    return b.join(QStringLiteral(", "));
}

bool AppContext::mainWindowVisible() const
{
    // rootObjects() lives on QQmlApplicationEngine (what main() passes in).
    auto *appEngine = qobject_cast<QQmlApplicationEngine *>(m_engine);
    if (!appEngine)
        return true; // can't tell — be conservative, block the restart
    const QList<QObject *> roots = appEngine->rootObjects();
    for (QObject *o : roots)
        if (auto *w = qobject_cast<QQuickWindow *>(o))
            return w->isVisible();
    return true;
}

bool AppContext::tryUpdateRestart()
{
    if (!m_updater->restartPending())
        return true; // nothing pending — also ends the retry timer
    const QString blockers = autoRestartBlockers();
    if (!blockers.isEmpty()) {
        qInfo() << "Update restart deferred:" << blockers;
        return false;
    }
    qInfo() << "Idle — restarting into the updated version";
    // Idle implies the window is hidden in the tray: come back the same way.
    m_updater->restartNow(true);
    return true;
}

void AppContext::devTestOcrBoxes()
{
    if (!devBuild())
        return;
#ifdef HAVE_TESSERACT
    ocrBoxes(ocrBoxTestImage(), [this](const QVector<OcrWord> &words, const QString &err) {
        if (!err.isEmpty())
            showToast(tr("Dev: OCR boxes: FAIL (%1)").arg(err), true);
        else
            showToast(tr("Dev: OCR boxes: %1 (%2 glyphs)")
                          .arg(words.size() >= 4 ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                          .arg(words.size()));
    });
#else
    showToast(tr("Dev: OCR boxes: SKIP (built without tesseract)"));
#endif
}

void AppContext::devTestOcrHighlight()
{
    if (!devBuild())
        return;
#ifdef HAVE_TESSERACT
    ocrBoxes(ocrBoxTestImage(), [this](const QVector<OcrWord> &words, const QString &err) {
        showToast(!err.isEmpty() ? tr("Dev: OCR highlight + redact: FAIL (%1)").arg(err)
                                 : tr("Dev: OCR highlight + redact: %1").arg(ocrHighlightCheck(words)),
                  !err.isEmpty());
    });
#else
    showToast(tr("Dev: OCR highlight + redact: SKIP (built without tesseract)"));
#endif
}

void AppContext::devTestCaptureSound()
{
    if (!devBuild())
        return;
    playCaptureSound();
    showToast(tr("Dev: played capture sound '%1'").arg(m_settings->captureSound()));
}

void AppContext::devTestRecordingSound()
{
    if (!devBuild())
        return;
    playRecordingSound();
    showToast(tr("Dev: played recording sound '%1'").arg(m_settings->recordingSound()));
}

void AppContext::devTestRecordStartSound()
{
    if (!devBuild())
        return;
    playRecordStartSound();
    showToast(tr("Dev: played record-start sound '%1'").arg(m_settings->recordStartSound()));
}

void AppContext::devTestTrashSound()
{
    if (!devBuild())
        return;
    playTrashSound();
    showToast(tr("Dev: played the fixed trash sound"));
}

void AppContext::devTestCountdown()
{
    if (!devBuild())
        return;
    if (m_settings->recordCountdownSec() <= 0) {
        showToast(tr("Dev: countdown is 0s (off) — set it in Recording settings"));
        return;
    }
    // Exercise the real in-frame countdown: set a centered test region (same as
    // the record-border test) so startRecorderCountdown pops the frame with the
    // number ticking inside it, then tears it down at the end.
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen && capRecordBorder()) {
        const qreal dpr = screen->devicePixelRatio() > 0 ? screen->devicePixelRatio() : 1.0;
        const int pw = qRound(screen->geometry().width() * dpr);
        const int ph = qRound(screen->geometry().height() * dpr);
        m_pendingRecordRegion = QRect(pw * 3 / 10, ph * 3 / 10, pw * 2 / 5, ph * 2 / 5);
        m_pendingRecordScreen = screen;
    }
    // Dev: no real recorder/portal, so armed() never fires — drive the countdown
    // visuals directly (commit() no-ops with no recording), then tear the demo
    // frame down a moment after the countdown + start-cue tail.
    const int secs = qBound(1, m_settings->recordCountdownSec(), 10);
    m_recordHoldActive = true; // commitRecordingAfterCue clears it
    runRecordCountdownVisuals(secs);
    QTimer::singleShot(secs * 1000 + 1200, this, [this]() {
        if (!recording())
            hideRecordBorder();
        showToast(tr("Dev: countdown finished — recording would start now"));
    });
}

void AppContext::devTestSaveDialog()
{
    if (!devBuild())
        return;
    QImage img(320, 200, QImage::Format_ARGB32);
    img.fill(QColor(0x2E, 0x23, 0x6C));
    const QString chosen = QFileDialog::getSaveFileName(
        nullptr, tr("Save capture (dev test)"),
        m_settings->saveDirectory() + QLatin1Char('/') + makeFileName(),
        tr("Images (*.png *.jpg *.jpeg *.webp)"));
    if (chosen.isEmpty()) {
        showToast(tr("Dev: save dialog cancelled"));
        return;
    }
    const QFileInfo fi(chosen);
    const QString path = saveImageTo(img, fi.absolutePath(), fi.fileName());
    showToast(path.isEmpty() ? tr("Dev: save FAILED")
                             : tr("Dev: saved to %1").arg(path),
              path.isEmpty());
}

void AppContext::devTestFilename()
{
    if (!devBuild())
        return;
    QString dir = m_settings->saveDirectory();
    if (m_settings->dateSubfolders())
        dir += QLatin1Char('/')
             + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM"));
    showToast(tr("Dev: next file = %1/%2 (counter=%3, subfolders=%4, stripMeta=%5)")
                  .arg(dir, makeFileName())
                  .arg(m_settings->filenameCounter())
                  .arg(m_settings->dateSubfolders() ? tr("on") : tr("off"),
                       m_settings->stripMetadata() ? tr("on") : tr("off")));
}

void AppContext::previewCapturePopup(const QVariantMap &overrides)
{
    // Only the stylized card is previewable. With it off, showCaptureNotification
    // would fall through to a native desktop notification — hovering a settings
    // row must never post one of those to the user's notification history.
    if (!m_settings->showCapturePopup() || !m_settings->showNotifications())
        return;
    hideCapturePopupPreview();
    // inhibited=false: the user asked for this card by pointing at the setting;
    // muteOnFullscreen is about unattended capture feedback, not this.
    m_previewNotif = showCaptureNotification(devTestImage(), QString(),
                                             QStringLiteral("image"), false, overrides);
}

void AppContext::hideCapturePopupPreview()
{
    if (m_previewNotif)
        m_previewNotif->dismiss();
    m_previewNotif.clear();
}

void AppContext::devTestCardPreview()
{
    if (!devBuild())
        return;
    if (!m_settings->showCapturePopup() || !m_settings->showNotifications()) {
        showToast(tr("Dev: card preview needs the stylized card enabled "
                     "(Preferences → Show notifications / capture card)"), true);
        return;
    }
    previewCapturePopup();
    if (!m_previewNotif) {
        showToast(tr("Dev: card preview FAILED (no card was created)"), true);
        return;
    }
    showToast(tr("Dev: card preview — withdrawing in 3 s"));
    QTimer::singleShot(3000, this, [this] { hideCapturePopupPreview(); });
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
                     "(Preferences → Show notifications)"), true);
        return;
    }
    if (!m_settings->showCapturePopup())
        showToast(tr("Dev: stylized cards are off — falling back to a native "
                     "desktop notification"));
    const bool inhibited = nowInhibited();
    if (inhibited && m_settings->muteOnFullscreen())
        showToast(tr("Dev: cards are currently muted (fullscreen / Do Not Disturb "
                     "inhibition is active)"), true);
    showCaptureNotification(devTestImage(), QString(), QStringLiteral("image"), inhibited);
}

void AppContext::devTestNotificationOrder()
{
    if (!devBuild())
        return;
    if (!m_settings->showCapturePopup() || !m_settings->showNotifications()) {
        devTestCardPreview();
        return;
    }

    // The preview override exercises the exact settings snapshot and host path
    // a real card uses, but never dirties the user's persisted order.
    previewCapturePopup({
        {QStringLiteral("notificationActionOrder"),
         QStringLiteral("folder,upload,copy,edit,link,qr,ocr,trim,delete")},
        {QStringLiteral("hiddenNotifActions"), QString()},
        {QStringLiteral("capturePopupDurationSec"), 6},
    });
    QTimer::singleShot(6000, this, [this] { hideCapturePopupPreview(); });
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
    showToast(tr("Dev: added a STARRED history entry; try Clear all / delete on it"));
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

void AppContext::devTestHistoryDrag()
{
    if (!devBuild())
        return;
    // The drag payload is built entirely by fileDragUri(); the QML drag gesture
    // itself can't be driven headlessly, so assert the uri-list string a drop
    // target would receive (spaces percent-encoded, empty path → empty).
    const QString uri = fileDragUri(QStringLiteral("/tmp/unisic drag test.png"));
    const bool ok = uri == QStringLiteral("file:///tmp/unisic%20drag%20test.png")
                    && fileDragUri(QString()).isEmpty();
    showToast(tr("Dev: history drag payload: %1")
                  .arg(ok ? QStringLiteral("PASS")
                          : QStringLiteral("FAIL (%1)").arg(uri)));
}

void AppContext::devTestNotificationDrag()
{
    if (!devBuild())
        return;
    // What the notification thumbnail hands a drop target: a saved capture drags
    // its real file (spaces percent-encoded); an unsaved one materializes a
    // private temp PNG on demand. The QML drag gesture can't run headlessly, so
    // assert only the payload. Stack notifications clean up their files on scope
    // exit (thumb + any temp drag file), so this leaves nothing behind.
    CaptureNotification saved(this, devTestImage(),
                              QStringLiteral("/tmp/unisic drag test.png"),
                              QStringLiteral("image"));
    const bool savedOk =
        saved.dragUri() == QStringLiteral("file:///tmp/unisic%20drag%20test.png");

    CaptureNotification unsaved(this, devTestImage(), QString(), QStringLiteral("image"));
    const QUrl du(unsaved.dragUri());
    const bool unsavedOk = du.isLocalFile() && QFile::exists(du.toLocalFile());

    const bool ok = savedOk && unsavedOk;
    showToast(tr("Dev: notification drag payload: %1")
                  .arg(ok ? QStringLiteral("PASS")
                          : QStringLiteral("FAIL (saved=%1 unsaved=%2)")
                                .arg(savedOk).arg(unsavedOk)),
              !ok);
}

void AppContext::devTestCopyLast()
{
    if (!devBuild())
        return;
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    devTestImage().save(&buf, "PNG");
    m_lastCaptureData = png;
    copyLastCapture();
    const bool ok = !QGuiApplication::clipboard()->image().isNull();
    showToast(tr("Dev: copy last capture: %1")
                  .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL (clipboard empty)")), !ok);
}

void AppContext::devTestPreview()
{
    if (!devBuild())
        return;
    openPreview(devTestImage());
}

void AppContext::devTestRecordBorder()
{
    if (!devBuild())
        return;
    if (!capRecordBorder()) {
        showToast(tr("Dev: record border: unsupported on this compositor"), true);
        return;
    }
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    // Centered region ≈ 40% of the primary screen, in physical pixels — the
    // same unit a real region recording hands to showRecordBorder().
    const qreal dpr = screen->devicePixelRatio() > 0 ? screen->devicePixelRatio() : 1.0;
    const int pw = qRound(screen->geometry().width() * dpr);
    const int ph = qRound(screen->geometry().height() * dpr);
    showRecordBorder(QRect(pw * 3 / 10, ph * 3 / 10, pw * 2 / 5, ph * 2 / 5), screen);
    const bool up = m_recordBorderWindow || m_recordBorderHelper;
    showToast(up ? tr("Dev: record border shown for 4 s")
                 : tr("Dev: record border FAILED to show"), !up);
    QTimer::singleShot(4000, this, [this] {
        if (!recording()) // a real region recording may own the frame by now
            hideRecordBorder();
    });
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

QString AppContext::toolShortcutsCheck() const
{
    if (!m_engine)
        return QStringLiteral("FAIL (no QML engine)");

    // Load the real singleton instead of duplicating the table in C++. The
    // expected ids below are a test oracle; both window key handlers resolve
    // through ToolCatalog.toolForShortcut(), so a missing/duplicate mapping is
    // caught before either UI is opened.
    QQmlComponent probeComponent(m_engine);
    probeComponent.setData(QByteArray(R"qml(
import QtQuick
import Unisic
QtObject {
    readonly property var expected: [
        { key: Qt.Key_V, id: "edit" },
        { key: Qt.Key_P, id: "pen" },
        { key: Qt.Key_L, id: "line" },
        { key: Qt.Key_A, id: "arrow" },
        { key: Qt.Key_M, id: "measure" },
        { key: Qt.Key_R, id: "rect" },
        { key: Qt.Key_O, id: "ellipse" },
        { key: Qt.Key_D, id: "callout" },
        { key: Qt.Key_T, id: "text" },
        { key: Qt.Key_H, id: "highlight" },
        { key: Qt.Key_B, id: "blur" },
        { key: Qt.Key_X, id: "pixelate" },
        { key: Qt.Key_E, id: "smarterase" },
        { key: Qt.Key_N, id: "step" },
        { key: Qt.Key_C, id: "crop" }
    ]
    readonly property bool valid: check()
    function check() {
        for (let i = 0; i < expected.length; ++i) {
            const editorTool = ToolCatalog.toolForShortcut(expected[i].key, "editor")
            if (!editorTool || editorTool.id !== expected[i].id)
                return false
            const overlayTool = ToolCatalog.toolForShortcut(expected[i].key, "overlay")
            if (expected[i].id === "crop") {
                if (overlayTool)
                    return false
            } else if (!overlayTool || overlayTool.id !== expected[i].id) {
                return false
            }
        }
        return true
    }
}
)qml"),
                          // A qrc base url keeps the compile synchronous: a custom
                          // scheme makes the implicit-import qmldir lookup go through
                          // the network loader, so the component never leaves Loading
                          // and create() returns null with an empty errorString.
                          QUrl(QStringLiteral("qrc:/ToolShortcutProbe.qml")));
    if (probeComponent.status() != QQmlComponent::Ready)
        return QStringLiteral("FAIL (probe %1: %2)")
            .arg(probeComponent.status() == QQmlComponent::Loading ? QStringLiteral("still loading")
                                                                   : QStringLiteral("not ready"),
                 probeComponent.errorString().simplified());
    std::unique_ptr<QObject> probe(probeComponent.create());
    if (!probe)
        return QStringLiteral("FAIL (probe create: %1)").arg(probeComponent.errorString().simplified());
    return probe->property("valid").toBool()
        ? QStringLiteral("PASS (15 editor, 14 overlay mappings)")
        : QStringLiteral("FAIL (catalog mapping mismatch)");
}

QString AppContext::historyFilterCheck()
{
    HistoryFilterModel f;
    f.setSourceModel(m_history);

    // Three scratch entries, one per filter dimension. Pathless (never saved),
    // so nothing reaches the trash when they are removed again below.
    const quint64 idImage = m_history->addEntry({}, devTestImage(), QStringLiteral("image"));
    const quint64 idRec = m_history->addEntry({}, devTestImage(), QStringLiteral("gif"));
    // A saved instant replay: same media kind as a recording, own category.
    const quint64 idReplay = m_history->addEntry({}, devTestImage(), QStringLiteral("video"), {}, {},
                                                 QStringLiteral("replay"));
    const quint64 idUploaded = m_history->addEntry({}, devTestImage(), QStringLiteral("image"),
                                                   QStringLiteral("https://example.invalid/smoke-xyzzy.png"));
    auto visible = [&f] {
        QSet<quint64> s;
        const QVariantList ids = f.entryIds();
        for (const QVariant &v : ids)
            s.insert(v.toULongLong());
        return s;
    };
    const QSet<quint64> seeded{idImage, idRec, idUploaded, idReplay};
    QStringList fails;
    auto expect = [&](const QString &what, const QSet<quint64> &want) {
        // Only the seeded ids are asserted: the user's real history is in the
        // same model and legitimately matches the same filters.
        if ((visible() & seeded) != want)
            fails << what;
    };

    f.setKindFilter(QStringLiteral("gif"));
    expect(QStringLiteral("kind=gif"), {idRec});
    f.setKindFilter(QStringLiteral("replay"));
    expect(QStringLiteral("kind=replay"), {idReplay});
    f.setKindFilter(QStringLiteral("image"));
    expect(QStringLiteral("kind=image"), {idImage, idUploaded});
    f.setKindFilter({});

    f.setUploadedOnly(true);
    expect(QStringLiteral("uploadedOnly"), {idUploaded});
    f.setUploadedOnly(false);

    m_history->setFavoriteByIds({QVariant(idImage)}, true);
    f.setFavoritesOnly(true);
    expect(QStringLiteral("favoritesOnly"), {idImage});
    f.setFavoritesOnly(false);
    m_history->setFavoriteByIds({QVariant(idImage)}, false);

    f.setSearchText(QStringLiteral("xyzzy"));   // matches the upload URL only
    expect(QStringLiteral("search"), {idUploaded});
    f.setSearchText({});
    expect(QStringLiteral("no filter"), seeded);

    // Batch delete, the History page's selection action.
    m_history->removeByIds({QVariant(idImage), QVariant(idRec), QVariant(idUploaded),
                            QVariant(idReplay)});
    if (!(visible() & seeded).isEmpty())
        fails << QStringLiteral("removeByIds");
    if (!m_history->entryById(idImage).isEmpty())
        fails << QStringLiteral("entryById after delete");

    return fails.isEmpty() ? QStringLiteral("PASS (kind, replay, uploaded, starred, search, batch delete)")
                           : QStringLiteral("FAIL (%1)").arg(fails.join(QStringLiteral(", ")));
}

QString AppContext::imgurSetupCheck()
{
    // 1) No stored destination may still carry the placeholder Client-ID that
    // shipped up to 0.7 — ensureBuiltins() repairs those on load.
    const QJsonArray dests = m_uploads->destinationsJson();
    for (const QJsonValue &v : dests) {
        const QString auth = v.toObject().value(QStringLiteral("headers")).toObject()
                              .value(QStringLiteral("Authorization")).toString();
        if (auth.contains(QStringLiteral("REPLACE_WITH_YOUR_IMGUR_CLIENT_ID")))
            return QStringLiteral("FAIL (placeholder Client-ID survives in '%1')")
                .arg(v.toObject().value(QStringLiteral("name")).toString());
    }

    // 2) The guard: an Imgur destination with no Client-ID must fail before any
    // request goes out, naming the fix. Scratch destination, removed below —
    // the user's own Imgur destination may legitimately have an ID by now.
    const QString scratch = QStringLiteral("unisic-smoke-imgur");
    m_uploads->saveDestination(QVariantMap{
        {QStringLiteral("name"), scratch},
        {QStringLiteral("type"), QStringLiteral("http")},
        {QStringLiteral("requestUrl"), QStringLiteral("https://api.imgur.com/3/image")},
        {QStringLiteral("fileFormName"), QStringLiteral("image")},
        {QStringLiteral("urlPath"), QStringLiteral("$json:data.link$")},
    });
    QString error;
    bool called = false;
    m_uploads->uploadDataTo(scratch, QByteArray("not a real upload"),
                            QStringLiteral("smoke.png"), QStringLiteral("image/png"),
                            [&](const QString &, const QString &, const QString &err) {
        called = true;
        error = err;
    });
    m_uploads->removeDestination(scratch);
    if (!called)
        return QStringLiteral("FAIL (no Client-ID: upload was attempted, not refused)");
    if (!error.contains(QStringLiteral("Client-ID")))
        return QStringLiteral("FAIL (unhelpful error: %1)").arg(error.left(80));
    return QStringLiteral("PASS (placeholder purged, missing Client-ID refused early)");
}

void AppContext::devTestHistoryFilter()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: history search + filters: %1").arg(historyFilterCheck()));
}

void AppContext::devTestImgurSetup()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: Imgur Client-ID guard: %1").arg(imgurSetupCheck()));
}

void AppContext::devTestSettingsRoundTrip()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: settings round-trip: %1").arg(settingsRoundTripCheck()));
}

void AppContext::devTestUpload()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: uploading a test image to '%1'…").arg(m_settings->activeDestination()));
    uploadImage(devTestImage(), [this](const QString &url, const QString &err) {
        if (err.isEmpty())
            showToast(tr("Dev: upload OK: %1").arg(url));
        else
            showToast(tr("Dev: upload failed: %1").arg(err), true);
    });
}

QString AppContext::altHotkeysCheck()
{
    // Round-trip a MULTI-binding through the real daemon on a scratch action:
    // push "F9, Meta+F9", read the active keys back, expect BOTH to be live
    // and the portable form to collapse to the same string; then release.
    // Exercises keysFor (multi-chord parse), the daemon's alternate-key list
    // and portableFromKeys — the plumbing the alternative-hotkeys UI rides on.
    if (!m_hotkeys->available())
        return QStringLiteral("SKIP (no KGlobalAccel)");
    const QString id = QStringLiteral("alt-hotkey-test");
    const QString name = tr("Alternate hotkey test");
    const QString wanted = QStringLiteral("F9, Meta+F9");
    QString result;
    if (!m_hotkeys->setShortcut(id, name, wanted)) {
        result = QStringLiteral("FAIL (daemon refused the multi-binding — keys taken?)");
    } else {
        bool ok = false;
        const QString actual = m_hotkeys->activeKeysPortable(id, &ok);
        if (!ok)
            result = QStringLiteral("FAIL (readback query failed)");
        else if (!GlobalHotkeys::sameBinding(actual, wanted))
            result = QStringLiteral("FAIL (round-trip returned '%1')").arg(actual);
        else
            result = QStringLiteral("PASS (both alternates live)");
    }
    m_hotkeys->releaseShortcut(id, name);
    // Fully remove the scratch action — unbinding alone (NoAutoloading) leaves a
    // phantom "Alternate hotkey test" row in the Shortcuts KCM forever.
    m_hotkeys->unregisterAction(id);
    return result;
}

void AppContext::devTestAltHotkeys()
{
    if (!devBuild())
        return;
    showToast(tr("Dev: alternate hotkeys — %1").arg(altHotkeysCheck()));
}

QStringList AppContext::hotkeyBindStatus(int *unbound, bool heal, QStringList *conflicts)
{
    QStringList lines;
    int bad = 0;
    const auto acts = hotkeyActions();
    for (const HotkeyAction &a : acts) {
        bool ok = false;
        const QList<int> raw = m_hotkeys->activeKeys(a.id, &ok);
        const QString actual = GlobalHotkeys::portableFromKeys(raw);
        // Cross-component conflict: the daemon keeps the key in OUR binding
        // list while resolving the actual press to another component (a KWin
        // script, another app) — the action looks bound but never fires.
        if (conflicts && ok) {
            for (int k : raw) {
                const QString owner = m_hotkeys->keyOwner(k);
                if (!owner.isEmpty() && !owner.startsWith(GlobalHotkeys::componentPrefix())) {
                    const QString line = QKeySequence(k).toString() + QStringLiteral(" (")
                                         + a.name + QStringLiteral(") → ") + owner;
                    conflicts->append(line);
                    lines << a.id + QStringLiteral(": CONFLICT ") + line;
                }
            }
        }
        if (!ok) {
            lines << a.id + QStringLiteral(": query failed");
            ++bad;
        } else if (actual.isEmpty() && !a.keys.isEmpty()) {
            ++bad;
            if (heal && m_hotkeys->setShortcut(a.id, a.name, a.keys))
                lines << a.id + QStringLiteral(": was unbound, re-asserted ") + a.keys;
            else
                lines << a.id + QStringLiteral(": UNBOUND (stored ") + a.keys + QLatin1Char(')');
        } else if (heal && GlobalHotkeys::sameBinding(actual, a.keys)
                   && GlobalHotkeys::expandShiftDigitVariants(raw) != raw) {
            // Bound to the right key, but WITHOUT the shifted-symbol variant
            // alternates a Shift+digit binding needs on KWin/Wayland (older
            // builds bound only the digit form, which the compositor's
            // consumed-shift lookup never matches) — re-push to upgrade.
            m_hotkeys->setShortcut(a.id, a.name, a.keys);
            lines << a.id + QStringLiteral(": ") + actual
                     + QStringLiteral(" (upgraded with Shift+digit variants)");
        } else {
            // Bound, but not to what we store = a KCM edit — honor it in the
            // UI (daemon-authoritative display). Set-compare: the daemon
            // reorders alternates, and a mere reorder is not an edit.
            if (!GlobalHotkeys::sameBinding(actual, a.keys)) {
                syncHotkeyFromDaemon(a.id, actual);
            } else if (heal) {
                // Grab refresh: the binding can survive daemon-side while the
                // compositor's key grab is gone (observed live: shortcutKeys
                // reported the keys and invokeShortcut fired, yet physical
                // presses did nothing until the user re-assigned every key by
                // hand). Re-pushing the same keys is exactly what that manual
                // re-assign does — do it on every launch so the grab can
                // never stay stale.
                m_hotkeys->setShortcut(a.id, a.name, a.keys);
            }
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
    QStringList conflicts;
    const QStringList lines = hotkeyBindStatus(&bad, true, &conflicts);
    qInfo().noquote() << "[dev] hotkey binds:\n" + lines.join(QLatin1Char('\n'));
    if (!conflicts.isEmpty())
        showToast(tr("Hotkey taken by another app: %1. Pick a different key in "
                     "Settings → Hotkeys, or free it in System Settings → Shortcuts.")
                      .arg(conflicts.join(QStringLiteral("; "))), true);
    else if (bad == 0)
        showToast(tr("Hotkeys: all %1 bound in the daemon").arg(lines.size()));
    else
        showToast(tr("Hotkeys: %1 of %2 were unbound and have been re-asserted (details in the log)")
                      .arg(bad).arg(lines.size()), true);
}

void AppContext::smokeNext()
{
    if (m_smokeIdx >= m_smokeSteps.size()) {
        m_smokeRunning = false;
        // Tally the result tokens across the whole run (some lines carry more
        // than one, e.g. "OCR: PASS, QR: SKIP") so the last line answers the
        // only question that matters at a glance: did anything fail?
        const int pass = m_smokeLog.count(QStringLiteral("PASS"));
        const int fail = m_smokeLog.count(QStringLiteral("FAIL"));
        const int skip = m_smokeLog.count(QStringLiteral("SKIP"));
        smokeLog(QStringLiteral("=== smoke test done: %1 PASS, %2 FAIL, %3 SKIP%4 ===")
                     .arg(pass).arg(fail).arg(skip)
                     .arg(fail > 0 ? QStringLiteral(" — FAILURES PRESENT") : QString()));
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
        smokeLog(QStringLiteral("identity: app=%1 desktop=%2 hotkeys=%3 config=%4")
                     .arg(QCoreApplication::applicationName(),
                          QGuiApplication::desktopFileName(),
                          GlobalHotkeys::componentPrefix().chopped(1),
                          m_settings->configPath()));
        smokeLog(QStringLiteral("capture backend: ") + (m_capture ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        smokeLog(QStringLiteral("screenshot cursor: ")
                 + (capScreenshotCursor() ? QStringLiteral("PASS")
                                          : QStringLiteral("SKIP (portal screenshot has no cursor mode)")));
        smokeLog(QStringLiteral("recording: ")
                 + (recordingAvailable() ? QStringLiteral("PASS")
                    : capPipeWireBuild() ? QStringLiteral("SKIP (no ScreenCast portal backend on this desktop)")
                                         : QStringLiteral("SKIP (built without PipeWire)")));
        smokeLog(QStringLiteral("notifications: native=%1 custom=%2 -> %3")
                 .arg(capNativeNotification() ? "y" : "n", capCustomNotification() ? "y" : "n",
                      (capNativeNotification() || capCustomNotification()) ? "PASS" : "FAIL"));
        // Settings hover preview: the same show/withdraw pair the pointer drives.
        // Only the creation is asserted — withdrawal tears the card down through
        // deleteLater / the helper's exit, so nothing observable has happened yet
        // by the next line. The dev button ("Card preview (3 s)") is where the
        // withdrawal gets checked, by eye.
        if (!m_settings->showNotifications() || !m_settings->showCapturePopup()) {
            smokeLog(QStringLiteral("card preview: SKIP (stylized card disabled)"));
            smokeLog(QStringLiteral("notification action order: SKIP (stylized card disabled)"));
        } else {
            const QString testOrder =
                QStringLiteral("folder,upload,copy,edit,link,qr,ocr,trim,delete");
            const QVariantMap orderOverrides{
                {QStringLiteral("notificationActionOrder"), testOrder},
                {QStringLiteral("hiddenNotifActions"), QString()},
            };
            previewCapturePopup(orderOverrides);
            const bool shown = !m_previewNotif.isNull();
            const bool orderForwarded =
                NotifCard::effectiveSettings(m_settings, orderOverrides)
                    .value(QStringLiteral("notificationActionOrder")).toString() == testOrder;
            hideCapturePopupPreview();
            // "Open a file": the dialog cannot run headless, so assert the routing
        // table it feeds — the part that decides which window a file lands in.
        {
            const bool ok = editableKindFor(QStringLiteral("/tmp/a.PNG")) == QLatin1String("image")
                            && editableKindFor(QStringLiteral("/tmp/b.mp4")) == QLatin1String("video")
                            && editableKindFor(QStringLiteral("/tmp/c.gif")) == QLatin1String("video")
                            && editableKindFor(QStringLiteral("/tmp/d.txt")).isEmpty();
            smokeLog(QStringLiteral("open own file: ")
                     + (ok ? QStringLiteral("PASS (image -> editor, recording -> trim, other -> refused)")
                           : QStringLiteral("FAIL (wrong routing)")));
        }
        smokeLog(QStringLiteral("card preview: ")
                     + (shown ? QStringLiteral("PASS") : QStringLiteral("FAIL (no card created)")));
        smokeLog(QStringLiteral("notification action order: ")
                     + (shown && orderForwarded
                            ? QStringLiteral("PASS (override reached card host)")
                            : QStringLiteral("FAIL")));
        }
        smokeLog(QStringLiteral("tray: ") + (trayAvailable() ? QStringLiteral("PASS") : QStringLiteral("SKIP (no tray host)")));
        smokeLog(QStringLiteral("hotkeys: %1 (%2)").arg(hotkeysAvailable() ? "PASS" : "SKIP", hotkeyBackend()));
        if (m_hotkeys->available()) {
            // Live daemon check: every action's active binding (heals unbound
            // ones — same repair defineHotkeys runs at startup).
            int bad = 0;
            QStringList conflicts;
            const QStringList lines = hotkeyBindStatus(&bad, true, &conflicts);
            for (const QString &l : lines)
                smokeLog(QStringLiteral("  bind ") + l);
            smokeLog(QStringLiteral("  hotkey binds: ")
                     + (!conflicts.isEmpty()
                            ? QStringLiteral("CONFLICT %1 (key owned by another component)")
                                  .arg(conflicts.size())
                            : bad == 0 ? QStringLiteral("PASS")
                                       : QStringLiteral("HEALED %1 (re-run to confirm)").arg(bad)));
        }
        smokeLog(QStringLiteral("OCR: %1, QR: %2").arg(
                 ocrAvailable() ? QStringLiteral("PASS") : QStringLiteral("SKIP (no tesseract)"),
                 qrAvailable() ? QStringLiteral("PASS") : QStringLiteral("SKIP (no zxing-cpp)")));
        smokeLog(QStringLiteral("tool letter shortcuts: ") + toolShortcutsCheck());
        smokeLog(QStringLiteral("history drag payload: ")
                 + (fileDragUri(QStringLiteral("/tmp/a b.png"))
                            == QStringLiteral("file:///tmp/a%20b.png")
                        ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        smokeLog(QStringLiteral("history search + filters: ") + historyFilterCheck());
        smokeLog(QStringLiteral("Imgur Client-ID guard: ") + imgurSetupCheck());
        {
            // Notification thumbnail drag: an unsaved image must materialize a
            // real temp file for the drop target (the new dragUri() branch).
            CaptureNotification nd(this, devTestImage(), QString(), QStringLiteral("image"));
            const QUrl du(nd.dragUri());
            smokeLog(QStringLiteral("notification drag payload: ")
                     + (du.isLocalFile() && QFile::exists(du.toLocalFile())
                            ? QStringLiteral("PASS (temp payload for unsaved capture)")
                            : QStringLiteral("FAIL")));
        }
        smokeNext();
    });

    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("hardware encoder: %1 (VAAPI=%2 NVENC=%3)")
                     .arg((m_vaapiAvailable || m_nvencAvailable) ? "PASS" : "SKIP")
                     .arg(m_vaapiAvailable ? "y" : "n", m_nvencAvailable ? "y" : "n"));
        if (!perAppAudioAvailable())
            smokeLog(QStringLiteral("per-app audio: SKIP (pw-dump/pw-record missing)"));
        else
            smokeLog(QStringLiteral("per-app audio: PASS (%1 active nodes)")
                         .arg(audioApplicationNodes().size()));
        const int segments = GifRecorder::replaySegmentCount(m_settings->instantReplaySeconds());
        smokeLog(QStringLiteral("instant replay ring: ")
                 + (segments >= 3 && segments <= 302
                        ? QStringLiteral("PASS (%1 bounded segments)").arg(segments)
                        : QStringLiteral("FAIL")));
        const bool trimTools = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()
                               && !QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty();
        smokeLog(QStringLiteral("trim recording: %1, preview: %2").arg(
                 trimTools ? QStringLiteral("PASS (helpers found)")
                           : QStringLiteral("SKIP (ffmpeg/ffprobe missing)"),
                 capVideoPlayback() ? QStringLiteral("PASS (QtMultimedia)")
                                    : QStringLiteral("SKIP (no qt6-qtmultimedia)")));
        smokeNext();
    });

    // 1a2) trim cut: the saved file must hold the selection the window showed —
    // both ways of cutting — and the timeline must get its filmstrip/keyframes.
    m_smokeSteps.append([this] {
        trimCutCheck([this](const QString &result) {
            smokeLog(QStringLiteral("trim cut: ") + result);
            smokeNext();
        });
    });

    // 1b) record border: flash the region frame on whichever host this
    // compositor uses (layer-shell / KWin fullscreen fallback / X11 /
    // XWayland helper) and take it down again.
    m_smokeSteps.append([this] {
        if (!capRecordBorder()) {
            smokeLog(QStringLiteral("record border: SKIP (no layer-shell/KWin/XWayland)"));
            smokeNext();
            return;
        }
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen) {
            smokeLog(QStringLiteral("record border: FAIL (no screen)"));
            smokeNext();
            return;
        }
        const qreal dpr = screen->devicePixelRatio() > 0 ? screen->devicePixelRatio() : 1.0;
        const int pw = qRound(screen->geometry().width() * dpr);
        const int ph = qRound(screen->geometry().height() * dpr);
        showRecordBorder(QRect(pw * 3 / 10, ph * 3 / 10, pw * 2 / 5, ph * 2 / 5), screen);
        const bool up = m_recordBorderWindow || m_recordBorderHelper;
        const bool helper = m_recordBorderHelper != nullptr;
        QTimer::singleShot(1200, this, [this, up, helper] {
            if (!recording())
                hideRecordBorder();
            smokeLog(QStringLiteral("record border (%1): %2")
                         .arg(helper ? QStringLiteral("xwayland helper")
                              : m_layerShellAvailable ? QStringLiteral("layer-shell")
                                                      : QStringLiteral("fullscreen window"),
                              up ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
            smokeNext();
        });
    });

    // 1c) KDE notification inhibition is an async D-Bus capability. Exercise
    // the real acquire/release pair, then continue without keeping DND active.
    m_smokeSteps.append([this] {
        if (!capDoNotDisturb()) {
            smokeLog(QStringLiteral("do not disturb: SKIP (not KDE)"));
            smokeNext();
            return;
        }
        m_dnd->acquire();
        QTimer::singleShot(500, this, [this] {
            const bool active = m_dnd->active();
            m_dnd->release();
            smokeLog(QStringLiteral("do not disturb: ")
                     + (active ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
            smokeNext();
        });
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

    // 3c) copy last capture — seed a known image, invoke, clipboard must fill.
    m_smokeSteps.append([this] {
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        devTestImage().save(&buf, "PNG");
        m_lastCaptureData = png;
        copyLastCapture();
        smokeLog(QStringLiteral("copy last capture: ")
                 + (QGuiApplication::clipboard()->image().isNull()
                        ? QStringLiteral("FAIL (clipboard empty)")
                        : QStringLiteral("PASS")));
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


    // 3e3) alternate hotkeys: multi-binding round-trip on a scratch action.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("alternate hotkeys: ") + altHotkeysCheck());
        smokeNext();
    });

    // 3e3b) text annotations: multi-line + styling must render into the composite.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("text render: ") + textRenderCheck());
        smokeNext();
    });

    // 3e3d) Ctrl+V text/image annotations — retained in the exported composite.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("clipboard paste: ") + clipboardPasteCheck());
        smokeNext();
    });

    // 3e3e) capture delay uses the real one-shot timer without opening a
    // portal dialog. The lower bound leaves scheduling jitter room while still
    // proving that a CLI-style override was not executed immediately.
    m_smokeSteps.append([this] {
        auto elapsed = std::make_shared<QElapsedTimer>();
        elapsed->start();
        setNextCaptureDelayMs(1100);
        withDelay([this, elapsed] {
            smokeLog(QStringLiteral("capture delay: ")
                     + (elapsed->elapsed() >= 1000 ? QStringLiteral("PASS")
                                                    : QStringLiteral("FAIL (fired early)")));
            smokeNext();
        });
    });

    // 3e3ga) Text watermark is a one-shot image-pixel export pass; it must
    // retain dimensions so every independent after-capture consumer agrees.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("watermark: ") + watermarkCheck());
        smokeNext();
    });

    // 3e3gb) Callout stays an ordinary vector annotation: no extra canvas
    // buffer and its tail must survive the image-space composite.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("callout: ") + calloutCheck());
        smokeNext();
    });

    // 3e3h) Shift snaps geometry to a grid and constrains line angles/ratios.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("shift snap: ") + shiftSnapCheck());
        smokeNext();
    });

    // 3e3i) QR generation reuses the optional zxing-cpp already present for
    // decoding. Keep the smoke path offline and bounded to a tiny matrix.
    m_smokeSteps.append([this] {
        if (!qrAvailable()) {
            smokeLog(QStringLiteral("QR preview: SKIP (no zxing-cpp)"));
        } else {
            const QImage qr = qrPreviewImage(QStringLiteral("https://example.invalid/unisic-smoke"));
            smokeLog(QStringLiteral("QR preview: ")
                     + (!qr.isNull() && qr.size() == QSize(360, 360)
                            ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        }
        smokeNext();
    });

    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("external action: ") + externalActionCheck());
        const CaptureTask task = taskFromId(QStringLiteral("all"));
        smokeLog(QStringLiteral("task preset: ")
                 + (task.active && task.upload && task.copy && task.save && task.edit
                        ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        QTemporaryDir dir;
        const QString cliPath = dir.isValid()
                                    ? saveImageTo(devTestImage(), dir.path(),
                                                  QStringLiteral("cli-smoke.png"))
                                    : QString();
        smokeLog(QStringLiteral("CLI output format: ")
                 + (!cliPath.isEmpty() && QImageReader(cliPath).format().toLower() == "png"
                        ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        smokeLog(QStringLiteral("measure: ") + measureToolsCheck());
        smokeNext();
    });

    // 3e3c) editable shapes: select / restyle / move / undo round-trip.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("shape edit: ") + shapeEditCheck());
        smokeNext();
    });

    // 3e3d) magnifier: a synthetic drag places a 2x loupe centred on the source.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("magnifier: ") + magnifyCheck());
        smokeNext();
    });

    // 3e3e) pixel loupe: hover placement, edge flip, Ctrl+scroll zoom clamp.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("pixel loupe: ") + pixelLoupeCheck());
        smokeNext();
    });

    // 3e4) capture sound: a player must exist and the WAV must extract.
    m_smokeSteps.append([this] {
        const bool player = !QStandardPaths::findExecutable(QStringLiteral("pw-play")).isEmpty()
                            || !QStandardPaths::findExecutable(QStringLiteral("paplay")).isEmpty()
                            || !QStandardPaths::findExecutable(QStringLiteral("aplay")).isEmpty();
        if (!player) {
            smokeLog(QStringLiteral("capture sound: SKIP (no pw-play/paplay/aplay)"));
        } else {
            const QString id = m_settings->captureSound();
            const QString source = (id == QLatin1String("off") || bundledSoundIds().contains(id))
                                       ? QStringLiteral("bundled")
                                       : QStringLiteral("custom");
            playCaptureSound();
            smokeLog(QStringLiteral("capture sound: PASS (played '%1', %2)").arg(id, source));
        }
        smokeNext();
    });

    // 3e5) recording sound: same player requirement, separate setting/cue.
    m_smokeSteps.append([this] {
        const bool player = !QStandardPaths::findExecutable(QStringLiteral("pw-play")).isEmpty()
                            || !QStandardPaths::findExecutable(QStringLiteral("paplay")).isEmpty()
                            || !QStandardPaths::findExecutable(QStringLiteral("aplay")).isEmpty();
        if (!player) {
            smokeLog(QStringLiteral("recording sound: SKIP (no pw-play/paplay/aplay)"));
        } else {
            const QString id = m_settings->recordingSound();
            const QString source = (id == QLatin1String("off") || bundledSoundIds().contains(id))
                                       ? QStringLiteral("bundled")
                                       : QStringLiteral("custom");
            playRecordingSound();
            smokeLog(QStringLiteral("recording sound: PASS (played '%1', %2)").arg(id, source));
        }
        smokeNext();
    });

    // 3e5b) trash sound: fixed cue — the qrc WAV must extract and play.
    m_smokeSteps.append([this] {
        const bool player = !QStandardPaths::findExecutable(QStringLiteral("pw-play")).isEmpty()
                            || !QStandardPaths::findExecutable(QStringLiteral("paplay")).isEmpty()
                            || !QStandardPaths::findExecutable(QStringLiteral("aplay")).isEmpty();
        if (!player) {
            smokeLog(QStringLiteral("trash sound: SKIP (no pw-play/paplay/aplay)"));
        } else {
            playTrashSound();
            smokeLog(QStringLiteral("trash sound: PASS (played fixed 'trash' cue)"));
        }
        smokeNext();
    });

    // 3e6) capture-on-release: synthetic drag confirms once; toggle off = never.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("capture on release: ") + captureOnReleaseCheck());
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

    // 3f1a) UI translations: the bundled .qm loads and a known string translates.
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("language: ") + languageCheck());
        smokeNext();
    });

    // 3f2) OCR word boxes — the selectable-text overlay's data source.
    m_smokeSteps.append([this] {
#ifdef HAVE_TESSERACT
        ocrBoxes(ocrBoxTestImage(), [this](const QVector<OcrWord> &words, const QString &err) {
            if (!err.isEmpty())
                smokeLog(QStringLiteral("ocr boxes: FAIL (%1)").arg(err));
            else if (words.size() >= 4)
                smokeLog(QStringLiteral("ocr boxes: PASS (%1 glyphs)").arg(words.size()));
            else
                smokeLog(QStringLiteral("ocr boxes: FAIL (%1 glyphs)").arg(words.size()));
            smokeNext();
        });
#else
        smokeLog(QStringLiteral("ocr boxes: SKIP (built without tesseract)"));
        smokeNext();
#endif
    });

    // 3f3) OCR selected text → permanent highlight/redaction annotations.
    m_smokeSteps.append([this] {
#ifdef HAVE_TESSERACT
        ocrBoxes(ocrBoxTestImage(), [this](const QVector<OcrWord> &words, const QString &err) {
            smokeLog(!err.isEmpty() ? QStringLiteral("ocr highlight + redact: FAIL (%1)").arg(err)
                                     : QStringLiteral("ocr highlight + redact: ") + ocrHighlightCheck(words));
            smokeNext();
        });
#else
        smokeLog(QStringLiteral("ocr highlight + redact: SKIP (built without tesseract)"));
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
        auto begun = std::make_shared<QMetaObject::Connection>();
        *done = connect(m_recorder, &GifRecorder::finished, this, [this, live, done, fail, begun](const QString &f) {
            *live = false;
            disconnect(*done); disconnect(*fail); disconnect(*begun);
            smokeLog(QStringLiteral("  recording: PASS (%1)").arg(f));
            smokeNext();
        });
        *fail = connect(m_recorder, &GifRecorder::failed, this, [this, live, done, fail, begun](const QString &e) {
            *live = false;
            disconnect(*done); disconnect(*fail); disconnect(*begun);
            smokeLog(QStringLiteral("  recording: FAIL (%1)").arg(e));
            smokeNext();
        });
        // Start the 3s clock only once actually RECORDING: a cold-start ScreenCast
        // portal negotiation (the session's first recording) can exceed 3s, and
        // stopping while still Starting cancels it ("cancelled") instead of
        // testing it. started() marks the Starting→Recording edge.
        *begun = connect(m_recorder, &GifRecorder::started, this, [this, live] {
            QTimer::singleShot(3000, this, [this, live] { if (*live && recording()) stopRecording(); });
        });
        startGifFullScreen();
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
        auto begun = std::make_shared<QMetaObject::Connection>();
        *done = connect(m_recorder, &GifRecorder::finished, this, [this, live, done, fail, begun](const QString &f) {
            *live = false;
            disconnect(*done); disconnect(*fail); disconnect(*begun);
            smokeLog(QStringLiteral("  video recording: PASS (%1)").arg(f));
            smokeNext();
        });
        *fail = connect(m_recorder, &GifRecorder::failed, this, [this, live, done, fail, begun](const QString &e) {
            *live = false;
            disconnect(*done); disconnect(*fail); disconnect(*begun);
            smokeLog(QStringLiteral("  video recording: FAIL (%1)").arg(e));
            smokeNext();
        });
        // Same as the GIF step: begin the 3s clock only once RECORDING, so a slow
        // cold-start negotiation doesn't get stopped mid-Starting.
        *begun = connect(m_recorder, &GifRecorder::started, this, [this, live] {
            QTimer::singleShot(3000, this, [this, live] { if (*live && recording()) stopRecording(); });
        });
        startVideoScreen();
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

    // 5c) update check: comparator semantics (synchronous, always runs), then
    // a live feed query — manual mode so the run never toasts or burns the
    // once-per-version notification.
    m_smokeSteps.append([this] {
        const bool cmpOk = UpdateVersion::isNewer(QStringLiteral("0.5.2"), QStringLiteral("0.5.1"))
                        && !UpdateVersion::isNewer(QStringLiteral("v0.5.1"), QStringLiteral("0.5.1"))
                        && UpdateVersion::isNewer(QStringLiteral("0.5.1"), QStringLiteral("0.5.1b"))
                        && !UpdateVersion::isNewer(QStringLiteral("0.5.0"), QStringLiteral("0.5.1"));
        smokeLog(QStringLiteral("version compare: ")
                 + (cmpOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        const QString gate = autoRestartBlockers();
        smokeLog(QStringLiteral("auto-restart gate: ")
                 + (gate.isEmpty() ? QStringLiteral("idle")
                                   : QStringLiteral("deferred (%1)").arg(gate)));
        m_updater->check(true, [this](const UpdateChecker::Result &r) {
            // Offline is a SKIP, not a FAIL — dev machines must keep a green run.
            smokeLog(QStringLiteral("update check: ")
                     + (r.ok ? QStringLiteral("PASS (latest %1 — %2)")
                                   .arg(r.latestVersion.isEmpty() ? QStringLiteral("none")
                                                                  : r.latestVersion,
                                        r.updateAvailable ? QStringLiteral("update available")
                                                          : QStringLiteral("up to date"))
                             : QStringLiteral("SKIP (network: %1)").arg(r.error)));
            smokeNext();
        });
    });

    // 6) upload (needs a real destination + a public target — left manual)
    m_smokeSteps.append([this] {
        smokeLog(QStringLiteral("upload: SKIP (active destination '%1'); run a real upload manually")
                 .arg(m_settings->activeDestination()));
        smokeNext();
    });

    // Essentials: filename tokens, save routing, countdown, volume, channel.
    m_smokeSteps.append([this] {
        const QString name = makeFileName();
        smokeLog(QStringLiteral("filename: %1 (counter=%2, dateSubfolders=%3, stripMeta=%4) — %5")
                     .arg(name)
                     .arg(m_settings->filenameCounter())
                     .arg(m_settings->dateSubfolders() ? QStringLiteral("on") : QStringLiteral("off"),
                          m_settings->stripMetadata() ? QStringLiteral("on") : QStringLiteral("off"),
                          name.isEmpty() ? QStringLiteral("FAIL") : QStringLiteral("PASS")));
        smokeLog(QStringLiteral("record countdown: %1s; start sound: %2; sound volume: %3 %; ask-where-to-save: %4")
                     .arg(m_settings->recordCountdownSec())
                     .arg(m_settings->recordStartSound())
                     .arg(m_settings->soundVolume())
                     .arg(m_settings->askWhereToSave() ? QStringLiteral("on") : QStringLiteral("off")));
        smokeLog(QStringLiteral("update channel: %1; autostart: %2")
                     .arg(m_settings->updateChannel(),
                          autostartEnabled() ? QStringLiteral("enabled") : QStringLiteral("disabled")));
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

bool AppContext::capNotificationHelper() const
{
    // The GNOME-shaped gap only: Wayland, no layer-shell, no KWin, but an X
    // socket exists so mutter can host the XWayland override-redirect card.
    // (UNISIC_NOTIFY_HELPER=1 forces it on any compositor for testing.)
    if (qEnvironmentVariable("UNISIC_NOTIFY_HELPER") == QLatin1String("1")
        && qEnvironmentVariableIsSet("DISPLAY"))
        return true;
    if (m_layerShellAvailable)
        return false;
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return false;
    auto *bi = QDBusConnection::sessionBus().interface();
    if (bi && bi->isServiceRegistered(QStringLiteral("org.kde.KWin")))
        return false;
    return qEnvironmentVariableIsSet("DISPLAY");
}

bool AppContext::capCustomNotification() const
{
    return m_layerShellAvailable || capNotificationHelper();
}

bool AppContext::showNotificationHelper(CaptureNotification *n, const QVariantMap &overrides)
{
    if (!n)
        return false;
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return false;
    const QRect lg = screen->geometry();

    // Resolve the UI language exactly as applyLanguage() does, so the card's
    // qsTr strings render in the same language as the rest of the app.
    const QString pref = m_settings->uiLanguage();
    static const QStringList supported = {QStringLiteral("en"), QStringLiteral("pl"),
                                          QStringLiteral("es"), QStringLiteral("it")};
    QString lang = pref;
    if (!supported.contains(pref)) {
        const QString sys = QLocale::system().name().left(2);
        lang = supported.contains(sys) ? sys : QStringLiteral("en");
    }

    auto *proc = new QProcess(this);
    proc->setProgram(QCoreApplication::applicationFilePath());
    // The helper hosts the real NotificationPopup.qml; it reads the thumbnail
    // CaptureNotification already wrote to the cache (owned + removed by `n`).
    // Everything that shapes the card travels as ONE blob, read off Settings'
    // metaobject by NotifCard — the same values the layer-shell host reads
    // straight from Settings. Adding a card setting means adding it to
    // NotifCard::settingKeys(); this call site does not change.
    const QString config = QString::fromUtf8(
        QJsonDocument(NotifCard::encodeConfig(m_settings, qrAvailable(), ocrAvailable(), overrides))
            .toJson(QJsonDocument::Compact));
    proc->setArguments({QStringLiteral("--notification-helper"),
                        screen->name(),
                        QString::number(lg.x()), QString::number(lg.y()),
                        QString::number(lg.width()), QString::number(lg.height()),
                        config,
                        lang,
                        n->kind(),
                        n->uploading() ? QStringLiteral("1") : QStringLiteral("0"),
                        n->url(),
                        n->thumbFilePath(),
                        n->filePath()});

    // Route the card's action tokens (stdout) onto the real CaptureNotification.
    connect(proc, &QProcess::readyReadStandardOutput, n, [proc, n] {
        const QList<QByteArray> lines = proc->readAllStandardOutput().split('\n');
        for (const QByteArray &raw : lines) {
            const QString tok = QString::fromUtf8(raw).trimmed();
            if (tok.isEmpty())
                continue;
            if (tok == QLatin1String("edit"))              n->edit();
            else if (tok == QLatin1String("trim"))         n->trim();
            else if (tok == QLatin1String("preview"))      n->preview();
            else if (tok == QLatin1String("copy-image"))   n->copyImage();
            else if (tok.startsWith(QLatin1String("copy-as:"))) n->copyAs(tok.mid(8));
            else if (tok == QLatin1String("copy-url"))     n->copyUrl();
            else if (tok == QLatin1String("qr"))           n->showQr();
            else if (tok == QLatin1String("folder"))       n->showInFolder();
            else if (tok == QLatin1String("upload"))       n->upload();
            else if (tok == QLatin1String("ocr"))          n->ocr();
            else if (tok == QLatin1String("delete"))       n->deleteCapture();
            // "dismiss": the card is already closing itself; proc-finished cleans up.
        }
    });
    // Push url/upload-state changes back so the card's buttons update live. Bound
    // to `proc` as context: the connection is dropped when the helper is gone, so
    // a late upload-completion never writes to a dead pipe.
    connect(n, &CaptureNotification::stateChanged, proc, [proc, n] {
        if (proc->state() != QProcess::Running)
            return;
        const QString msg = QStringLiteral("state:%1|%2|%3\n")
                                .arg(n->uploading() ? QStringLiteral("1") : QStringLiteral("0"),
                                     n->url(), n->filePath());
        proc->write(msg.toUtf8());
    });
    // edit()/delete()/dismiss() on `n` emit this — tell the card to close.
    connect(n, &CaptureNotification::closeRequested, proc, [proc] {
        if (proc->state() == QProcess::Running)
            proc->write("close\n");
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, n](int, QProcess::ExitStatus) {
        m_notifHelpers.removeAll(proc);
        n->deleteLater();
        proc->deleteLater();
    });
    // stdin stays an open pipe so the helper quits (EOF) if THIS process dies.
    proc->start();
    if (!proc->waitForStarted(1500)) {
        proc->deleteLater();
        return false;
    }
    m_notifHelpers.append(proc);
    return true;
}

bool AppContext::capRecordBorder() const
{
    if (m_layerShellAvailable)
        return true; // layer-shell overlay: KWin, wlroots, COSMIC…
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return true; // X11 session: StaysOnTop + input-transparent work natively
    // KWin can still host the fullscreen-transparent border without layer-shell.
    auto *bi = QDBusConnection::sessionBus().interface();
    if (bi && bi->isServiceRegistered(QStringLiteral("org.kde.KWin")))
        return true;
    // GNOME and friends: the XWayland override-redirect helper only needs an X
    // socket (mutter spawns XWayland on demand when the helper connects).
    return qEnvironmentVariableIsSet("DISPLAY");
}

bool AppContext::capVideoPlayback() const
{
    // The trim editor imports QtMultimedia purely from QML (no C++ link), so the
    // capability is just "is the module's plugin installed in the QML import
    // path" — qt6-qtmultimedia ships the runtime plugin even without its -devel.
    static const bool ok = QFileInfo::exists(
        QLibraryInfo::path(QLibraryInfo::QmlImportsPath)
        + QStringLiteral("/QtMultimedia/qmldir"));
    return ok;
}

void AppContext::showRecordBorder(QRect physRegion, QScreen *screen, int countdown)
{
    hideRecordBorder(); // retire any stale frame first
    if (!m_engine || !screen || physRegion.isEmpty() || !capRecordBorder())
        return; // capRecordBorder(): layer-shell, KWin trick, X11 or XWayland helper

    // GNOME (Wayland, no layer-shell, no KWin): an in-process toplevel would
    // sink below the next window the user raises — mutter has no keep-above
    // for xdg_toplevel. Spawn the XWayland helper instead: mutter stacks
    // override-redirect X11 windows above every application window, and the
    // empty input shape keeps the frame click-through. The region travels as
    // monitor FRACTIONS because XWayland's coordinate space (logical vs
    // physical layout mode) need not match either of ours.
    // UNISIC_RECORD_BORDER=helper forces this path on any compositor (testing).
    const bool wayland = QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
    auto *bi = QDBusConnection::sessionBus().interface();
    const bool kwin = bi && bi->isServiceRegistered(QStringLiteral("org.kde.KWin"));
    const bool forceHelper =
        qEnvironmentVariable("UNISIC_RECORD_BORDER") == QLatin1String("helper");
    if (forceHelper
        || (wayland && !m_layerShellAvailable && !kwin
            && qEnvironmentVariableIsSet("DISPLAY"))) {
        const qreal hdpr = screen->devicePixelRatio() > 0 ? screen->devicePixelRatio() : 1.0;
        const QRect lg = screen->geometry();
        const QSizeF phys(lg.width() * hdpr, lg.height() * hdpr);
        // Frame color follows the active theme; the fallback is the palette accent.
        QColor accent(QStringLiteral("#C8ACD6"));
        if (QObject *theme = m_engine->singletonInstance<QObject *>(
                QStringLiteral("Unisic"), QStringLiteral("Theme")))
            accent = theme->property("accent").value<QColor>();
        const auto frac = [](double v) { return QString::number(v, 'f', 8); };
        auto *proc = new QProcess(this);
        proc->setProgram(QCoreApplication::applicationFilePath());
        proc->setArguments({QStringLiteral("--record-border-helper"),
                            screen->name(),
                            QString::number(lg.x()), QString::number(lg.y()),
                            QString::number(lg.width()), QString::number(lg.height()),
                            QString::number(qRound(phys.width())),
                            QString::number(qRound(phys.height())),
                            frac(physRegion.x() / phys.width()),
                            frac(physRegion.y() / phys.height()),
                            frac(physRegion.width() / phys.width()),
                            frac(physRegion.height() / phys.height()),
                            accent.name(QColor::HexRgb),
                            QString::number(countdown)});
        // stdin stays an open pipe on purpose: if THIS process dies without
        // reaching hideRecordBorder(), the helper sees EOF and quits — no
        // orphaned frame can outlive the recording.
        proc->start();
        m_recordBorderHelper = proc;
        return;
    }

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
    // Pre-recording countdown number (0 = none) — RecordBorder.qml shows it
    // centered in the region and hides the REC badge while it ticks.
    win->setProperty("countdown", countdown);

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
    if (m_recordBorderHelper) {
        QProcess *p = m_recordBorderHelper;
        m_recordBorderHelper = nullptr;
        connect(p, &QProcess::finished, p, &QObject::deleteLater);
        // Clean shutdown is the stdin EOF (the helper's only lifeline); the
        // delayed kill only reaps a wedged helper without blocking the GUI.
        p->closeWriteChannel();
        if (p->state() == QProcess::NotRunning)
            p->deleteLater();
        else
            QTimer::singleShot(1000, p, [p] {
                if (p->state() != QProcess::NotRunning)
                    p->kill();
            });
    }
    if (m_recordBorderWindow) {
        m_recordBorderWindow->close();
        m_recordBorderWindow->deleteLater();
        m_recordBorderWindow = nullptr;
    }
}

void AppContext::onRecordingFinished(const QString &path, bool fromInstantReplay)
{
    m_converting = false;
    emit recordingChanged();
    if (path.isEmpty())
        return; // stopping the rolling replay ring creates no output by itself
    const QString kind = path.endsWith(QLatin1String(".gif")) ? QStringLiteral("gif")
                                                              : QStringLiteral("video");
    if (kind == QLatin1String("video")) {
        // QImage has no mp4/webm plugin — extract a poster frame via ffmpeg,
        // else every video gets a blank thumbnail in history and the popup.
        const QString posterPath = path + QStringLiteral(".poster.png");
        auto *proc = new QProcess(this);
        const auto completed = std::make_shared<bool>(false);
        connect(proc, &QProcess::finished, this,
                [this, proc, path, kind, posterPath, completed, fromInstantReplay](int, QProcess::ExitStatus) {
            if (*completed)
                return;
            *completed = true;
            proc->deleteLater();
            QImage thumb(posterPath);
            QFile::remove(posterPath);
            finishRecordingEntry(path, thumb, kind, fromInstantReplay);
        });
        connect(proc, &QProcess::errorOccurred, this,
                [this, proc, path, kind, posterPath, completed, fromInstantReplay](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart || *completed)
                return;
            *completed = true;
            proc->deleteLater();
            QFile::remove(posterPath);
            finishRecordingEntry(path, QImage(), kind, fromInstantReplay);
        });
        // Extract the poster already downscaled to thumbnail size: the only
        // consumer is a history/popup thumbnail (≤480 px), so a full-res 4K PNG
        // would just cost a ~30 MB GUI-thread decode and a second re-scale in
        // makeThumb. 960 px stays comfortably above every thumbnail target.
        proc->start(QStringLiteral("ffmpeg"),
                    {QStringLiteral("-y"), QStringLiteral("-nostats"),
                     QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-i"), path,
                     QStringLiteral("-frames:v"), QStringLiteral("1"),
                     QStringLiteral("-vf"), QStringLiteral("scale='min(960,iw)':-2"),
                     posterPath});
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
    // First GIF frame, scaled DURING decode (Qt's gif handler honors
    // setScaledSize) — a full 8 MP LZW decode of a fullscreen GIF on the GUI
    // thread just to make a thumbnail is wasted work.
    QImageReader reader(path);
    const QSize orig = reader.size();
    if (orig.isValid() && (orig.width() > 960 || orig.height() > 960))
        reader.setScaledSize(orig.scaled(960, 960, Qt::KeepAspectRatio));
    QImage thumb = reader.read();
    if (thumb.isNull())
        thumb = QImage(path); // fall back to a full decode if the scaled read failed
    finishRecordingEntry(path, thumb, kind);
}

void AppContext::finishRecordingEntry(const QString &path, const QImage &thumb, const QString &kind,
                                      bool fromInstantReplay)
{
    // Audible cue that the (possibly long) encode is done and the file exists —
    // the screenshot pipeline plays its own cue in finishCapture.
    playRecordingSound();

    m_history->addEntry(path, thumb, kind, {}, {},
                        fromInstantReplay ? QStringLiteral("replay") : QString());
    showToast(tr("Saved %1").arg(path));
    runExternalAction(thumb, path);

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
void AppContext::finishCapture(const QImage &img, bool inhibited, bool forceCopy)
{
    if (img.isNull())
        return;

    const CaptureTask task = m_nextCaptureTask;
    const QString uploadDestination = m_nextCaptureDestination;
    m_nextCaptureTask = {};
    m_nextCaptureDestination.clear();
    const QString cliOutputPath = m_nextCaptureOutputPath;
    const QString cliFormat = m_nextCaptureOutputFormat;
    const bool cliStdout = m_nextCaptureToStdout;
    const bool cliMode = cliStdout || !cliOutputPath.isEmpty();
    m_nextCaptureOutputPath.clear();
    m_nextCaptureOutputFormat.clear();
    m_nextCaptureToStdout = false;
    const bool saveEnabled = !cliOutputPath.isEmpty()
                             || (task.active ? task.save : m_settings->autoSave());
    const bool copyEnabled = task.active ? task.copy : m_settings->copyToClipboard();
    const bool editEnabled = task.active ? task.edit : m_settings->openEditor();
    const bool uploadEnabled = task.active ? task.upload : m_settings->uploadAfterCapture();

    // Watermark once before the normal independent fan-out. When disabled this
    // remains an implicitly shared QImage (no extra full-frame allocation);
    // when enabled the helper makes one writable output frame, rather than a
    // save/copy/upload-specific copy for each branch.
    QImage output = img;
    if (m_settings->watermarkEnabled()) {
        if (m_settings->watermarkType() == QLatin1String("image")
            && !m_watermarkImage.isNull()) {
            output = UnisicImageEffects::watermarkImage(
                img, m_watermarkImage, m_settings->watermarkOpacity(),
                m_settings->watermarkPosition());
        } else {
            output = UnisicImageEffects::watermarkText(
                img, m_settings->watermarkText(), m_settings->watermarkOpacity(),
                m_settings->watermarkPosition());
        }
    }

    // Audible cue: a fullscreen capture is otherwise invisible (no overlay
    // flash), so play the shutter/selected sound the moment it lands.
    if (!cliMode)
        playCaptureSound();

    // One name per capture: save and upload must agree (a second-boundary or
    // %rand% template would otherwise produce two different names).
    const QString fileName = makeFileName();
    QString path;
    if (saveEnabled) {
        if (!cliOutputPath.isEmpty()) {
            const QFileInfo target(cliOutputPath);
            path = saveImageTo(output, target.absolutePath(), target.fileName());
            if (path.isEmpty())
                showToast(tr("Could not save to %1").arg(cliOutputPath), true);
        } else if (m_settings->askWhereToSave()) {
            // Prompt for a destination per capture instead of writing straight
            // into the save folder. A cancelled dialog skips the save silently
            // (no error toast) — the capture still lives in memory/history.
            QString startDir = m_settings->saveDirectory();
            if (m_settings->dateSubfolders())
                startDir += QLatin1Char('/')
                          + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM"));
            QDir().mkpath(startDir);
            const QString chosen = QFileDialog::getSaveFileName(
                nullptr, tr("Save capture"),
                startDir + QLatin1Char('/') + fileName,
                tr("Images (*.png *.jpg *.jpeg *.webp)"));
            if (!chosen.isEmpty()) {
                const QFileInfo fi(chosen);
                path = saveImageTo(output, fi.absolutePath(), fi.fileName());
                if (path.isEmpty())
                    showToast(tr("Could not save to %1").arg(chosen), true);
            }
        } else {
            path = saveImageAuto(output, fileName);
            // A failed save must be LOUD: the rest of the pipeline continues (the
            // capture still exists in memory/history), but silently pretending it
            // was persisted loses data on unplugged/read-only/full save targets.
            if (path.isEmpty())
                showToast(tr("Could not save to %1. Check the save folder in Settings")
                              .arg(m_settings->saveDirectory()), true);
        }
    }
    if (copyEnabled || forceCopy)
        copyImageToClipboard(output);

    // Independent after-capture action. A saved file is passed directly;
    // otherwise runExternalAction creates one bounded, short-lived scratch PNG.
    if (!cliMode)
        runExternalAction(output, path);

    const bool uploading = uploadEnabled;
    quint64 historyId = 0;
    if (!cliMode && (!uploading || !path.isEmpty()))
        historyId = m_history->addEntry(path, output, QStringLiteral("image"));

    auto *notif = cliMode ? nullptr
                          : showCaptureNotification(output, path, QStringLiteral("image"), inhibited);
    QPointer<CaptureNotification> np(notif);
    if (notif)
        notif->setHistoryId(historyId); // Save/upload address exactly this entry

    if (uploading) {
        if (np) np->setUploading(true);
        // Encode off-thread (100+ ms at 4K), start the upload in the GUI-thread
        // continuation. The callback retains the image ONLY when the history
        // entry can actually need it (nothing saved to disk) — otherwise the
        // 30-60 MB buffer would stay pinned for the whole network transfer.
        encodeImageAsync(output, [this, path, np, fileName, uploadDestination,
                                  img = path.isEmpty() ? output : QImage()](const QByteArray &data, const QString &mime) {
            m_uploads->uploadDataTo(uploadDestination, data, fileName, mime,
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
    } else if (!path.isEmpty() && !cliMode) {
        showToast(tr("Saved %1").arg(path));
    }

    if (editEnabled)
        openEditor(output, {});

    // Keep the newest screenshot for the "Copy last capture" hotkey — encoded
    // off-thread so the retained buffer is megabytes, not a pinned 4K QImage.
    if (cliStdout) {
        encodeImageAsync(output, [this](const QByteArray &data, const QString &) {
            emit cliCaptureReady(data, data.isEmpty() ? tr("Could not encode the capture")
                                                       : QString());
        }, cliFormat);
    } else if (!cliMode) {
        encodeImageAsync(output, [this](const QByteArray &data, const QString &) {
            m_lastCaptureData = data;
        });
    }

    // Advance the %i% counter once per capture (only when the template uses it),
    // so the next filename gets the next number.
    if (!cliMode && m_settings->filenameTemplate().contains(QLatin1String("%i%")))
        m_settings->setFilenameCounter(m_settings->filenameCounter() + 1);

    scheduleMemoryTrim();
}

void AppContext::copyLastCapture()
{
    const QImage img = QImage::fromData(m_lastCaptureData);
    if (img.isNull()) {
        showToast(tr("No capture to copy yet"), true);
        return;
    }
    copyImageToClipboard(img);
    showToast(tr("Copied to clipboard"));
}

void AppContext::afterUploadActions(const QString &url)
{
    const auto finish = [this](const QString &finalUrl) {
        if (m_settings->afterUploadCopyLink()) {
            copyText(finalUrl);
            showToast(tr("Uploaded, link copied"));
        } else {
            showToast(tr("Uploaded: %1").arg(finalUrl));
        }
        if (!m_settings->afterUploadOpenInBrowser())
            return;
        // The URL is extracted from the upload server's response (attacker-
        // controllable on a compromised/hostile destination). Only auto-open
        // web links — never file://, smb://, or a custom scheme wired to a
        // local handler.
        const QUrl u(finalUrl);
        const QString scheme = u.scheme().toLower();
        if (scheme == QLatin1String("http") || scheme == QLatin1String("https"))
            QDesktopServices::openUrl(u);
    };

    finish(url);
}

QString AppContext::pickWatermarkImage()
{
    const QString start = m_settings->watermarkImagePath().isEmpty()
                              ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                              : QFileInfo(m_settings->watermarkImagePath()).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        nullptr, tr("Choose watermark image"), start.isEmpty() ? QDir::homePath() : start,
        tr("Images (*.png *.svg *.svgz *.jpg *.jpeg *.webp)"));
    if (path.isEmpty())
        return {};

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (!sourceSize.isValid() || sourceSize.width() > 16384 || sourceSize.height() > 16384) {
        showToast(tr("The watermark image is invalid or too large"), true);
        return {};
    }
    reader.setScaledSize(sourceSize.scaled(QSize(1024, 1024), Qt::KeepAspectRatio));
    if (reader.read().isNull()) {
        showToast(tr("Could not load the watermark image"), true);
        return {};
    }
    m_settings->setWatermarkImagePath(path);
    return path;
}

void AppContext::refreshWatermarkImage()
{
    m_watermarkImage = {};
    const QString path = m_settings->watermarkImagePath();
    if (path.isEmpty())
        return;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (!sourceSize.isValid() || sourceSize.width() > 16384 || sourceSize.height() > 16384)
        return;
    // Cap the decoded size at 1024 to avoid pinning a huge source, but NEVER
    // upscale a small logo (KeepAspectRatio would blow a 120px icon up to 1024
    // and then watermarkImage scales it back down — a double resample that
    // fringes the alpha edges).
    if (sourceSize.width() > 1024 || sourceSize.height() > 1024)
        reader.setScaledSize(sourceSize.scaled(QSize(1024, 1024), Qt::KeepAspectRatio));
    m_watermarkImage = reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
}


QString AppContext::editableKindFor(const QString &path)
{
    static const QStringList imageExt = {QStringLiteral("png"), QStringLiteral("jpg"),
                                         QStringLiteral("jpeg"), QStringLiteral("webp"),
                                         QStringLiteral("bmp"), QStringLiteral("tif"),
                                         QStringLiteral("tiff"), QStringLiteral("avif")};
    static const QStringList videoExt = {QStringLiteral("mp4"), QStringLiteral("webm"),
                                         QStringLiteral("gif"), QStringLiteral("mkv"),
                                         QStringLiteral("mov")};
    const QString ext = QFileInfo(path).suffix().toLower();
    if (imageExt.contains(ext))
        return QStringLiteral("image");
    if (videoExt.contains(ext))
        return QStringLiteral("video");
    return {};
}

void AppContext::openFileForEditing(const QString &kind)
{
    // One dialog for both kinds: picking by extension beats making the user
    // choose "image or video?" before they have chosen the file. An image opens
    // in the very editor a screenshot opens; a recording in the trim window the
    // history's Trim button opens.
    const QString start = m_settings->saveDirectory().isEmpty()
                              ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                              : m_settings->saveDirectory();
    const QString images = tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff *.avif)");
    const QString videos = tr("Recordings (*.mp4 *.webm *.gif *.mkv *.mov)");
    const QString both = tr("Images and recordings (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff "
                            "*.avif *.mp4 *.webm *.gif *.mkv *.mov)");
    const QString anyFile = tr("All files (*)");
    QString filter;
    QString title;
    if (kind == QLatin1String("image")) {
        filter = images + QStringLiteral(";;") + videos + QStringLiteral(";;") + anyFile;
        title = tr("Open an image to edit");
    } else if (kind == QLatin1String("video")) {
        filter = videos + QStringLiteral(";;") + images + QStringLiteral(";;") + anyFile;
        title = tr("Open a recording to trim");
    } else {
        filter = both + QStringLiteral(";;") + images + QStringLiteral(";;") + videos
                 + QStringLiteral(";;") + anyFile;
        title = tr("Open image or recording");
    }
    const QString path = QFileDialog::getOpenFileName(
        nullptr, title, start.isEmpty() ? QDir::homePath() : start, filter);
    if (path.isEmpty())
        return; // cancelled

    // What the file IS decides, not what the dialog was filtered to.
    const QString actual = editableKindFor(path);
    if (actual == QLatin1String("video"))
        openTrimRecording(path);
    else if (actual == QLatin1String("image"))
        editFromHistory(path);
    else
        showToast(tr("Unisic cannot edit this file type"), true);
}

void AppContext::openTrimRecording(const QString &path)
{
    if (!QFileInfo::exists(path)) {
        showToast(tr("Recording file not found"), true);
        return;
    }
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (ffprobe.isEmpty()) {
        showToast(tr("Trimming requires ffprobe from the ffmpeg package"), true);
        return;
    }
    auto *probe = new QProcess(this);
    const auto completed = std::make_shared<bool>(false);
    connect(probe, &QProcess::finished, this,
            [this, probe, path, completed](int code, QProcess::ExitStatus status) {
        if (*completed)
            return;
        *completed = true;
        const QByteArray output = probe->readAllStandardOutput();
        probe->deleteLater();
        // Keyed "name=value" lines (default writer, nk left on): the probe asks
        // for the duration AND the frame rate, and line order across sections
        // is not a contract worth leaning on.
        qreal duration = -1;
        qreal frameDur = 0;
        const QList<QByteArray> lines = output.split('\n');
        for (const QByteArray &line : lines) {
            const int eq = line.indexOf('=');
            if (eq <= 0)
                continue;
            const QByteArray key = line.left(eq).trimmed();
            const QByteArray value = line.mid(eq + 1).trimmed();
            if (key == "duration") {
                bool ok = false;
                const qreal d = value.toDouble(&ok);
                if (ok)
                    duration = d;
            } else if (key == "avg_frame_rate") {
                // "30/1", "30000/1001"; "0/0" for unknown → stays 0.
                const int slash = value.indexOf('/');
                bool okNum = false, okDen = false;
                const qreal num = value.left(slash).toDouble(&okNum);
                const qreal den = value.mid(slash + 1).toDouble(&okDen);
                if (slash > 0 && okNum && okDen && num > 0 && den > 0)
                    frameDur = den / num;
            }
        }
        if (code != 0 || status != QProcess::NormalExit || duration <= 0) {
            showToast(tr("Could not read the recording duration"), true);
            return;
        }
        showTrimWindow(path, duration, frameDur);
    });
    connect(probe, &QProcess::errorOccurred, this,
            [this, probe, completed](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || *completed)
            return;
        *completed = true;
        probe->deleteLater();
        showToast(tr("Trimming requires ffprobe from the ffmpeg package"), true);
    });
    probe->start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                           QStringLiteral("-show_entries"),
                           QStringLiteral("stream=avg_frame_rate:format=duration"),
                           QStringLiteral("-of"), QStringLiteral("default=nw=1"), path});
}

void AppContext::showTrimWindow(const QString &path, qreal duration, qreal frameDuration)
{
    if (!m_engine)
        return;
    QQmlComponent component(m_engine, QUrl(QStringLiteral("qrc:/qt/qml/Unisic/qml/TrimWindow.qml")));
    if (component.isError()) {
        qWarning() << component.errorString();
        return;
    }
    auto *ctx = new QQmlContext(m_engine->rootContext(), this);
    auto *trim = new TrimController(path, duration, frameDuration);
    ctx->setContextProperty(QStringLiteral("trimSourcePath"), path);
    ctx->setContextProperty(QStringLiteral("trimDuration"), duration);
    ctx->setContextProperty(QStringLiteral("trimController"), trim);
    QObject *object = component.create(ctx);
    if (auto *window = qobject_cast<QQuickWindow *>(object)) {
        ctx->setParent(window);
        trim->setParent(window);   // strip file dies with the window
        connect(window, &QQuickWindow::visibleChanged, window, [window](bool visible) {
            if (!visible) window->deleteLater();
        });
        if (m_smokeRunning)
            m_smokeWindows.append(window);
        window->show();
        window->requestActivate();
    } else {
        delete object;
        delete trim;
        delete ctx;
    }
}

void AppContext::runTrimStep(const QStringList &args,
                             std::function<void(bool, const QString &)> done)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        done(false, tr("Trimming requires ffmpeg"));
        return;
    }
    auto *process = new QProcess(this);
    const auto completed = std::make_shared<bool>(false);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this,
            [process, completed, done](int code, QProcess::ExitStatus status) {
        if (*completed)
            return;
        *completed = true;
        const QString diagnostic = QString::fromUtf8(process->readAll()).trimmed();
        process->deleteLater();
        done(code == 0 && status == QProcess::NormalExit, diagnostic);
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, completed, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || *completed)
            return;
        *completed = true;
        process->deleteLater();
        done(false, tr("Trimming requires ffmpeg"));
    });
    process->start(ffmpeg, args);
}

void AppContext::trimGif(const QString &path, const QString &output, qreal start, qreal end)
{
    // A GIF cannot be seeked (input -ss silently yields the whole file) and
    // cannot be stream-copied mid-file, so the range is selected inside the
    // filter graph and the selection is re-rendered through the same two-pass
    // palettegen/paletteuse the recorder uses. setpts rebases the selection to
    // zero; without it the output keeps a `start` seconds of empty lead-in.
    const int quality = m_settings->gifQuality();
    const QString range = QStringLiteral("trim=start=%1:end=%2,setpts=PTS-STARTPTS")
                              .arg(QString::number(start, 'f', 3),
                                   QString::number(end, 'f', 3));
    // The palette is scratch: it lives in the cache dir (NOT next to the
    // recording, where an exit mid-trim would leave a stray
    // "*-trimmed.gif.palette.png" forever), and stale ones from an earlier
    // crashed/quit run are swept here. The age gate keeps the sweep away from
    // a palette a concurrent trim is still using.
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cacheDir);
    QDir cache(cacheDir);
    const QFileInfoList stale = cache.entryInfoList({QStringLiteral("trim-palette-*")},
                                                    QDir::Files);
    const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-3600);
    for (const QFileInfo &fi : stale)
        if (fi.lastModified() < cutoff)
            QFile::remove(fi.absoluteFilePath());
    static quint32 paletteSerial = 0;
    const QString palette = cache.filePath(QStringLiteral("trim-palette-%1-%2.png")
                                               .arg(QCoreApplication::applicationPid())
                                               .arg(++paletteSerial));
    runTrimStep({QStringLiteral("-y"), QStringLiteral("-nostats"),
                 QStringLiteral("-loglevel"), QStringLiteral("error"),
                 QStringLiteral("-i"), path,
                 QStringLiteral("-vf"),
                 range + QLatin1Char(',') + GifRecorder::gifPaletteGenFilter(quality),
                 palette},
                [this, path, output, palette, range, quality](bool ok, const QString &diagnostic) {
        if (!ok) {
            QFile::remove(palette);
            showToast(tr("Trim failed: %1").arg(diagnostic), true);
            return;
        }
        runTrimStep({QStringLiteral("-y"), QStringLiteral("-nostats"),
                     QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-i"), path,
                     QStringLiteral("-i"), palette,
                     QStringLiteral("-lavfi"),
                     QStringLiteral("[0:v]%1[x];[x][1:v]%2")
                         .arg(range, GifRecorder::gifPaletteUseFilter(quality)),
                     output},
                    [this, output, palette](bool ok2, const QString &diagnostic2) {
            QFile::remove(palette);
            if (!ok2) {
                QFile::remove(output);
                showToast(tr("Trim failed: %1").arg(diagnostic2), true);
                return;
            }
            onRecordingFinished(output);
        });
    });
}

void AppContext::trimRecording(const QString &path, qreal startSeconds, qreal endSeconds,
                               bool lossless)
{
    if (!QFileInfo::exists(path) || startSeconds < 0 || endSeconds <= startSeconds) {
        showToast(tr("Invalid trim range"), true);
        return;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
        showToast(tr("Trimming requires ffmpeg"), true);
        return;
    }
    const QFileInfo source(path);
    QString output = source.absolutePath() + QLatin1Char('/') + source.completeBaseName()
                     + QStringLiteral("-trimmed.") + source.suffix();
    for (int i = 1; QFileInfo::exists(output); ++i)
        output = source.absolutePath() + QLatin1Char('/') + source.completeBaseName()
                 + QStringLiteral("-trimmed-%1.").arg(i) + source.suffix();

    showToast(tr("Trimming recording…"));
    const QString suffix = source.suffix().toLower();
    if (suffix == QLatin1String("gif")) {
        trimGif(path, output, startSeconds, endSeconds);
        return;
    }

    const QString ss = QString::number(startSeconds, 'f', 3);
    const QString dur = QString::number(endSeconds - startSeconds, 'f', 3);
    QStringList args{QStringLiteral("-y"), QStringLiteral("-nostats"),
                     QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-ss"), ss,
                     QStringLiteral("-i"), path,
                     QStringLiteral("-t"), dur};
    if (lossless) {
        // The caller has already snapped `startSeconds` onto a keyframe, so the
        // copy starts exactly there. make_zero rebases the timestamps instead of
        // leaning on a container edit list (which not every player honours).
        args << QStringLiteral("-c") << QStringLiteral("copy")
             << QStringLiteral("-avoid_negative_ts") << QStringLiteral("make_zero");
    } else {
        // Re-encode: with -ss in front of -i ffmpeg seeks to the preceding
        // keyframe and decodes forward, so the output starts on the exact frame.
        const int crf = qBound(0, m_settings->videoQuality(), 51);
        // yuv420p needs even dimensions (same rule the recorder enforces on its
        // crop): an imported MP4/MOV/MKV can be odd-sized, and libx264 & friends
        // abort with "width not divisible by 2". Trim at most one edge pixel.
        const QString evenCrop = QStringLiteral("crop=trunc(iw/2)*2:trunc(ih/2)*2");
        if (suffix == QLatin1String("webm")) {
            args << QStringLiteral("-vf") << evenCrop
                 << QStringLiteral("-c:v") << QStringLiteral("libvpx-vp9")
                 << QStringLiteral("-crf") << QString::number(crf)
                 << QStringLiteral("-b:v") << QStringLiteral("0")
                 << QStringLiteral("-deadline") << QStringLiteral("good")
                 << QStringLiteral("-cpu-used") << QStringLiteral("4")
                 << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
                 << QStringLiteral("-row-mt") << QStringLiteral("1");
        } else if (m_settings->videoEncoder() == QLatin1String("vaapi")
                   && GifRecorder::hardwareEncoderAvailable(QStringLiteral("vaapi"))) {
            args << QStringLiteral("-vaapi_device") << QStringLiteral("/dev/dri/renderD128")
                 << QStringLiteral("-vf") << evenCrop + QStringLiteral(",format=nv12,hwupload")
                 << QStringLiteral("-c:v") << QStringLiteral("h264_vaapi")
                 << QStringLiteral("-qp") << QString::number(qBound(1, crf, 40))
                 << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        } else if (m_settings->videoEncoder() == QLatin1String("nvenc")
                   && GifRecorder::hardwareEncoderAvailable(QStringLiteral("nvenc"))) {
            args << QStringLiteral("-vf") << evenCrop
                 << QStringLiteral("-c:v") << QStringLiteral("h264_nvenc")
                 << QStringLiteral("-preset") << QStringLiteral("p4")
                 << QStringLiteral("-cq") << QString::number(crf)
                 << QStringLiteral("-b:v") << QStringLiteral("0")
                 << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
                 << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        } else if (GifRecorder::encoderUsable(QStringLiteral("libx264"))) {
            args << QStringLiteral("-vf") << evenCrop
                 << QStringLiteral("-c:v") << QStringLiteral("libx264")
                 << QStringLiteral("-preset") << QStringLiteral("veryfast")
                 << QStringLiteral("-crf") << QString::number(crf)
                 << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
                 << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        } else {
            args << QStringLiteral("-vf") << evenCrop
                 << QStringLiteral("-c:v") << QStringLiteral("libopenh264")
                 << QStringLiteral("-b:v") << QStringLiteral("%1M").arg(qBound(2, (51 - crf) / 3, 16))
                 << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
                 << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        }
        // '?' keeps the audio map a no-op on a recording made without sound.
        args << QStringLiteral("-map") << QStringLiteral("0:v:0")
             << QStringLiteral("-map") << QStringLiteral("0:a:0?");
        if (suffix == QLatin1String("webm"))
            args << QStringLiteral("-c:a") << QStringLiteral("libopus")
                 << QStringLiteral("-b:a") << QStringLiteral("128k");
        else
            args << QStringLiteral("-c:a") << QStringLiteral("aac")
                 << QStringLiteral("-b:a") << QStringLiteral("192k");
    }
    args << output;
    runTrimStep(args, [this, output](bool ok, const QString &diagnostic) {
        if (!ok) {
            QFile::remove(output);
            showToast(tr("Trim failed: %1").arg(diagnostic), true);
            return;
        }
        onRecordingFinished(output);
    });
}

void AppContext::runExternalAction(const QImage &image, const QString &savedPath)
{
    if (!m_settings->externalActionEnabled()
        || m_settings->externalActionCommand().trimmed().isEmpty())
        return;
    const auto launch = [this](const QString &input, bool temporary) {
        m_actionRunner->run(m_settings->externalActionCommand(), input, temporary,
            [this](const QString &output, const QString &error) {
            if (!error.isEmpty()) {
                showToast(tr("External action failed: %1").arg(error), true);
                return;
            }
            if (!output.isEmpty()) {
                const QImage preview(output);
                m_history->addEntry(output, preview, QStringLiteral("image"));
                showToast(tr("External action created %1").arg(output));
            } else {
                showToast(tr("External action finished"));
            }
        });
    };
    if (!savedPath.isEmpty()) {
        launch(savedPath, false);
        return;
    }
    // Unsaved captures need a process-readable input. Encode off the GUI
    // thread, then remove the scratch file when the child exits.
    encodeImageAsync(image, [this, launch](const QByteArray &data, const QString &) {
        auto *tmp = new QTemporaryFile(
            QDir::tempPath() + QStringLiteral("/unisic-action-XXXXXX.png"), this);
        tmp->setAutoRemove(false);
        if (!tmp->open() || tmp->write(data) != data.size()) {
            const QString failedPath = tmp->fileName();
            tmp->deleteLater();
            if (!failedPath.isEmpty()) QFile::remove(failedPath);
            showToast(tr("Could not prepare the external action input"), true);
            return;
        }
        const QString path = tmp->fileName();
        tmp->close();
        tmp->deleteLater();
        launch(path, true);
    });
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

void AppContext::playCaptureSound()
{
    playSoundId(m_settings->captureSound());
}

void AppContext::playRecordingSound()
{
    playSoundId(m_settings->recordingSound());
}

void AppContext::playRecordStartSound()
{
    playSoundId(m_settings->recordStartSound());
}

void AppContext::playTrashSound()
{
    // Deliberately fixed: "trash" is bundled but NOT in captureSoundIds(), so
    // it never shows up in the sound combos and can't be reassigned.
    playSoundId(QStringLiteral("trash"));
}

void AppContext::playSoundId(const QString &id)
{
    if (id.isEmpty() || id == QLatin1String("off"))
        return;
    const QString player = soundPlayer();
    qInfo().noquote() << "[cue] id=" << id
                      << " player=" << (player.isEmpty() ? QStringLiteral("<NONE>") : player);
    if (player.isEmpty())
        return;

    QString file;
    // "trash" is a fixed internal cue: bundled in qrc, deliberately absent
    // from bundledSoundIds() so the settings combos never offer it.
    if (bundledSoundIds().contains(id) || id == QLatin1String("trash")) {
        // A player takes a filesystem path, not a qrc URL — extract the WAV to
        // the cache and reuse it. Size mismatch = the bundled cue changed in an
        // app update; re-extract, or the stale cached copy would play forever.
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                            + QStringLiteral("/sounds");
        QDir().mkpath(dir);
        file = dir + QLatin1Char('/') + id + QStringLiteral(".wav");
        QFile res(QStringLiteral(":/resources/sounds/%1.wav").arg(id));
        if (!QFile::exists(file) || QFileInfo(file).size() != res.size()) {
            QFile::remove(file);
            res.copy(file);
        }
    } else {
        // User cue from ~/.config/unisic/sounds. Only a bare file name is
        // accepted — a hand-edited config must not smuggle an arbitrary path
        // to the player.
        if (QFileInfo(id).fileName() != id)
            return;
        file = UnisicConfig::soundsDir() + QLatin1Char('/') + id;
    }
    if (!QFile::exists(file)) {
        qWarning().noquote() << "[cue] file MISSING:" << file;
        return;
    }

    const int vol = qBound(0, m_settings->soundVolume(), 100);
    qInfo().noquote() << "[cue] file=" << file
                      << " size=" << QFileInfo(file).size() << " vol=" << vol;
    if (vol == 0)
        return; // muted

    const QString base = QFileInfo(player).fileName();
    QStringList args{file};
    // Classify the cue as a short event/notification sound rather than the
    // player default ("Music"). WirePlumber then mixes it as a notification and
    // does NOT apply the Music-role stream-ducking that some setups (EasyEffects
    // chains, a Discord screen-share capture) failed to release — which left the
    // captured audio dead silent after a shutter cue.
    if (base == QLatin1String("pw-play"))
        args << QStringLiteral("--media-role") << QStringLiteral("Notification");
    else if (base == QLatin1String("paplay"))
        args << QStringLiteral("--property=media.role=event");
    // Per-player volume flags (only when not at 100% — the sample's own level).
    // pw-play: --volume takes a linear 0.0..1.0; paplay: 0..65536 (65536=100%);
    // aplay has no volume flag, so it always plays at the sample level.
    if (vol != 100) {
        if (base == QLatin1String("pw-play"))
            args << QStringLiteral("--volume") << QString::number(vol / 100.0, 'f', 2);
        else if (base == QLatin1String("paplay"))
            args << QStringLiteral("--volume") << QString::number(qRound(vol / 100.0 * 65536.0));
    }

    // TEMP DIAG: run the player verbose and capture its own output, plus the
    // audio-session env the QProcess inherits, so we can see whether pw-play
    // actually reaches "streaming" from inside the app or connects nowhere.
    QStringList dbgArgs = args;
    if (base == QLatin1String("pw-play"))
        dbgArgs.prepend(QStringLiteral("-v"));
    qInfo().noquote() << "[cue] exec:" << player << dbgArgs.join(QLatin1Char(' '));
    qInfo().noquote() << "[cue] env XDG_RUNTIME_DIR=" << QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR"))
                      << " WAYLAND_DISPLAY=" << QString::fromLocal8Bit(qgetenv("WAYLAND_DISPLAY"))
                      << " PULSE_SERVER=" << QString::fromLocal8Bit(qgetenv("PULSE_SERVER"))
                      << " PIPEWIRE_REMOTE=" << QString::fromLocal8Bit(qgetenv("PIPEWIRE_REMOTE"));
    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::errorOccurred, this, [id](QProcess::ProcessError e) {
        qWarning().noquote() << "[cue] FAILED start id=" << id << " error=" << int(e);
    });
    connect(proc, &QProcess::finished, this, [proc, id](int code, QProcess::ExitStatus st) {
        qInfo().noquote() << "[cue] finished id=" << id << " exit=" << code << " status=" << int(st)
                          << "\n[cue-out] " << QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
    });
    connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
    connect(proc, &QProcess::errorOccurred, proc, &QObject::deleteLater);
    proc->start(player, dbgArgs);
}

int AppContext::soundDurationMs(const QString &id) const
{
    if (id.isEmpty() || id == QLatin1String("off"))
        return 0;
    QString path;
    if (bundledSoundIds().contains(id) || id == QLatin1String("trash"))
        path = QStringLiteral(":/resources/sounds/%1.wav").arg(id);
    else
        path = UnisicConfig::soundsDir() + QLatin1Char('/') + id;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    const QByteArray d = f.read(44);
    const qint64 total = f.size();
    f.close();
    // PCM WAV only: RIFF/WAVE header with a 32-bit byteRate at offset 28.
    if (d.size() < 44 || !d.startsWith("RIFF") || d.mid(8, 4) != "WAVE")
        return -1;
    const auto u32 = [&d](int o) {
        return quint32(quint8(d[o])) | (quint32(quint8(d[o + 1])) << 8)
             | (quint32(quint8(d[o + 2])) << 16) | (quint32(quint8(d[o + 3])) << 24);
    };
    const quint32 byteRate = u32(28);
    if (byteRate == 0 || total <= 44)
        return -1;
    return int((total - 44) * 1000 / byteRate);
}

QStringList AppContext::captureSoundIds() const
{
    QStringList ids{QStringLiteral("off")};
    ids += bundledSoundIds();
    QStringList exts{QStringLiteral("*.wav")};
    if (soundPlayerTakesOgg())
        exts << QStringLiteral("*.ogg") << QStringLiteral("*.oga");
    const QDir dir(UnisicConfig::soundsDir());
    ids += dir.entryList(exts, QDir::Files | QDir::Readable, QDir::Name);
    return ids;
}

QString AppContext::addCustomSound()
{
    const QString filter = soundPlayerTakesOgg() ? tr("Sounds (*.wav *.ogg *.oga)")
                                                 : tr("Sounds (*.wav)");
    const QString path = QFileDialog::getOpenFileName(
        nullptr, tr("Add capture sound"), QDir::homePath(), filter);
    if (path.isEmpty())
        return {}; // cancelled
    const QFileInfo src(path);
    static const QStringList okExt{QStringLiteral("wav"), QStringLiteral("ogg"),
                                   QStringLiteral("oga")};
    if (!okExt.contains(src.suffix().toLower())) {
        showToast(tr("Unsupported sound format (use WAV or OGG)"), true);
        return {};
    }
    const QString destDir = UnisicConfig::soundsDir();
    QString dest = destDir + QLatin1Char('/') + src.fileName();
    for (int i = 1; QFile::exists(dest); ++i)
        dest = destDir + QLatin1Char('/') + src.completeBaseName()
               + QStringLiteral("-%1.").arg(i) + src.suffix();
    if (!QFile::copy(path, dest)) {
        showToast(tr("Could not copy the sound file"), true);
        return {};
    }
    const QString id = QFileInfo(dest).fileName();
    showToast(tr("Added capture sound \"%1\"").arg(id));
    return id;
}

void AppContext::copyImageFromHistory(const QString &filePath)
{
    QImage img(filePath);
    if (img.isNull()) {
        showToast(tr("Can't open %1 to copy").arg(QFileInfo(filePath).fileName()), true);
        return;
    }
    copyImageToClipboard(img);
    showToast(tr("Image copied"));
}

void AppContext::copyAsFromHistory(const QString &filePath, const QString &url,
                                   const QString &format)
{
    if (filePath.isEmpty()) {
        showToast(tr("Save the capture first to copy its file path"), true);
        return;
    }
    copyImageAs({}, filePath, url, format);
}

void AppContext::uploadFromHistory(const QString &filePath)
{
    if (filePath.isEmpty())
        return;
    // Upload the FILE itself (works for images, GIFs and videos alike) and
    // attach the resulting URL to this entry so the card's link/copy-link
    // light up. afterUploadActions honours the copy-link / open-in-browser
    // settings, matching a capture-time upload.
    showToast(tr("Uploading %1…").arg(QFileInfo(filePath).fileName()));
    m_uploads->uploadFile(filePath, [this, filePath](const QString &url, const QString &del,
                                                     const QString &err) {
        if (!err.isEmpty()) {
            showToast(tr("Upload failed: %1").arg(err), true);
            return;
        }
        m_history->setUrl(filePath, url, del);
        if (url.isEmpty())
            showToast(tr("Uploaded")); // FTP/SFTP destination with no public URL
        else
            afterUploadActions(url);   // shows its own toast (+ copy-link/open)
    });
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
    // Namespace the temp files per app flavor (unisic / unisic-dev): the sweep
    // must only reap THIS flavor's leftovers, or a dev instance would delete the
    // PNG backing a stable instance's currently-open preview (both run side by
    // side by design).
    const QString previewPrefix = QCoreApplication::applicationName()
                                  + QStringLiteral("-preview-");
    static bool sweptStale = false;
    if (!sweptStale) {
        sweptStale = true;
        QDir tmpDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
        const QStringList stale = tmpDir.entryList({previewPrefix + QStringLiteral("*.png")}, QDir::Files);
        for (const QString &f : stale)
            QFile::remove(tmpDir.filePath(f));
    }
    // Persist a full-res copy the tool window loads by path — keeps that window
    // trivial (no image provider) — and remove it when the window closes. The
    // PNG encode is 100+ ms at 4K, so it runs on a worker; the window is built
    // in the GUI-thread continuation.
    const QString tmp = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                            .filePath(previewPrefix +
                                      QUuid::createUuid().toString(QUuid::WithoutBraces) +
                                      QStringLiteral(".png"));
    QPointer<AppContext> self(this);
    QPointer<QCoreApplication> application(qApp);
    (void)QtConcurrent::run([self, application, img, tmp] {
        const bool ok = img.save(tmp);
        // The capture can contain a password/bank page/private chat; /tmp is
        // world-listable, so lock the file to the owner (a UUID name is no
        // protection once the directory is listable).
        if (ok)
            QFile::setPermissions(tmp, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
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
                                                         const QString &kind, bool inhibited,
                                                         const QVariantMap &overrides)
{
    // The master "Show notifications" switch promises complete silence — it
    // must cover capture cards (layer-shell AND native) exactly like toasts.
    // showCapturePopup only selects the STYLE: on = the stylized layer-shell
    // card (when the compositor supports it), off/unsupported = a native
    // desktop notification. It is no longer a second silence switch.
    if (!m_settings->showNotifications())
        return nullptr;
    // A real desktop notification (org.freedesktop.Notifications) with an inline
    // thumbnail and Open/Copy/Upload/Delete action buttons. The notification
    // server draws it, so it is always above other windows on every desktop —
    // unlike the old client-drawn fullscreen card, which Wayland would not keep
    // on top (a click elsewhere raised another window over it). The notifier
    // owns the returned object; callers may still poke its upload state.
    auto *notif = new CaptureNotification(this, img, path, kind, nullptr);
    // Clear the pointer BEFORE emitting closeRequested: both hosts close
    // asynchronously, and a rapid third capture must address the new card, not
    // send another close command to the one already retiring.
    const auto retireActivePopup = [this] {
        QPointer<CaptureNotification> previous = m_activePopupNotif;
        m_activePopupNotif.clear();
        if (previous)
            previous->dismiss();
    };
#ifdef HAVE_LAYERSHELL
    if (m_layerNotifier && m_settings->showCapturePopup()) {
        // The layer card draws above everything. Only when the user opted in
        // (muteOnFullscreen) do we honour KDE's inhibition — which conflates a
        // fullscreen app, Do-Not-Disturb, AND stuck third-party inhibitors, so
        // auto-suppressing by default wrongly killed the user's own capture
        // feedback. Sampled when THIS capture began (before our own overlay).
        if (inhibited && m_settings->muteOnFullscreen()) {
            notif->deleteLater();
            return nullptr;
        }
        retireActivePopup();
        m_activePopupNotif = notif;
        m_layerNotifier->show(notif, overrides); // on-top custom card (layer-shell)
        return notif;
    }
#endif
    // GNOME/mutter (no layer-shell): the SAME styled NotificationPopup.qml rides
    // an XWayland override-redirect helper — the Steam-style toast, and the only
    // surface mutter keeps above everything. A plain Wayland window cannot be
    // placed by its own client, and the fullscreen-and-mask trick that works for
    // the capture overlay renders BLACK here: mutter unredirects fullscreen
    // surfaces, so their transparency is never composited.
    // showNotificationHelper owns `notif` for the card's lifetime.
    if (capNotificationHelper() && m_settings->showCapturePopup()) {
        if (inhibited && m_settings->muteOnFullscreen()) {
            notif->deleteLater();
            return nullptr;
        }
        retireActivePopup();
        if (showNotificationHelper(notif, overrides)) {
            m_activePopupNotif = notif;
            return notif;
        }
        // Could not spawn the helper → fall through to the native notification.
    }
    // A setting change or helper failure may route the newest capture through
    // the native server. Do not leave an older stylized card beside it.
    retireActivePopup();
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
    QString dir = m_settings->saveDirectory();
    // Optional per-month subfolders (yyyy-MM) keep a busy screenshots folder
    // tidy. saveImageTo mkpath()s the directory, so no separate mkdir here.
    if (m_settings->dateSubfolders())
        dir += QLatin1Char('/')
             + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM"));
    return saveImageTo(img, dir, fileName);
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
    // %i% = a monotonic counter (filenameCounter), read-only here — the actual
    // increment happens once per saved capture in finishCapture, so the preview
    // and the real save agree on the same number.
    t.replace(QLatin1String("%i%"), QString::number(m_settings->filenameCounter()));
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

static bool imageHasTransparency(const QImage &img);

void AppContext::encodeImageAsync(const QImage &img,
                                  std::function<void(const QByteArray &, const QString &)> done,
                                  const QString &formatOverride)
{
    // Snapshot the settings on the GUI thread; the worker must not touch them.
    const QString fmt = formatOverride.isEmpty() ? m_settings->imageFormat().toLower()
                                                  : formatOverride.toLower();
    const int q = qBound(1, m_settings->imageQuality(), 100);
    QPointer<AppContext> self(this);
    QPointer<QCoreApplication> application(qApp);
    (void)QtConcurrent::run([self, application, img, fmt, q, done = std::move(done)] {
        QByteArray out;
        QString mime;
        QBuffer buf(&out);
        buf.open(QIODevice::WriteOnly);
        // JPEG has no alpha: a source image carrying transparency would flatten
        // to solid black. Mirror saveImageTo's auto-switch and encode PNG
        // instead, so the uploaded bytes match the saved file. WEBP keeps
        // alpha, so it needs no such guard.
        const bool wantJpg = (fmt == QLatin1String("jpg") || fmt == QLatin1String("jpeg"));
        if (wantJpg && !imageHasTransparency(img) && img.save(&buf, "JPG", q)) {
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

// True when the image actually contains transparent pixels (not merely an
// alpha-capable format). Sampled — a save-time scan of every pixel of a 4K
// frame is wasteful and transparency comes in contiguous regions. pixelColor()
// reads alpha correctly for any format (incl. ARGB32_Premultiplied), so no
// full-frame convertToFormat() copy is needed — the sampling stays ~free.
static bool imageHasTransparency(const QImage &img)
{
    if (!img.hasAlphaChannel())
        return false;
    const int stepY = qMax(1, img.height() / 256);
    const int stepX = qMax(1, img.width() / 256);
    for (int y = 0; y < img.height(); y += stepY)
        for (int x = 0; x < img.width(); x += stepX)
            if (img.pixelColor(x, y).alpha() < 255)
                return true;
    return false;
}

QString AppContext::saveImageTo(const QImage &img, const QString &dir, const QString &fileName)
{
    if (dir.isEmpty() || img.isNull())
        return {};
    QDir().mkpath(dir);
    QString name = fileName.isEmpty() ? makeFileName() : fileName;

    QString fmt = QFileInfo(name).suffix().toLower();
    if (fmt != QLatin1String("png") && fmt != QLatin1String("jpg")
        && fmt != QLatin1String("jpeg") && fmt != QLatin1String("webp"))
        fmt = m_settings->imageFormat().toLower();
    // JPEG can't hold an alpha channel — a source image carrying transparency
    // would flatten to black. Auto-switch such saves to PNG
    // (and fix the extension) BEFORE the collision-dedup loop below, so the
    // switched name is what the loop de-duplicates against — otherwise two
    // transparent saves in the same second (or a pre-existing same-named .png)
    // both pass the .jpg existence check and silently overwrite one another.
    if ((fmt == QLatin1String("jpg") || fmt == QLatin1String("jpeg"))
        && imageHasTransparency(img)) {
        fmt = QStringLiteral("png");
        if (name.endsWith(QLatin1String(".jpg"), Qt::CaseInsensitive)
            || name.endsWith(QLatin1String(".jpeg"), Qt::CaseInsensitive))
            name = name.left(name.lastIndexOf(QLatin1Char('.'))) + QStringLiteral(".png");
        showToast(tr("Saved as PNG to keep transparency"));
    }

    QString path = dir + QLatin1Char('/') + name;
    const QFileInfo fi(name);
    for (int n = 1; QFile::exists(path); ++n)
        path = dir + QLatin1Char('/') + fi.completeBaseName()
               + QStringLiteral("-%1.").arg(n) + fi.suffix();

    // Metadata strip: rebuild from raw pixels so the written file carries no
    // text chunks, description or DPI. Captures normally have NONE (built from
    // raw screen pixels), so skip the full-frame copy unless there is actually
    // something to strip — the editor or a loaded source can add text/DPI. Only
    // ≥24bpp (the capture formats); a rebuild would drop an indexed palette.
    QImage stripped;
    if (m_settings->stripMetadata() && img.depth() >= 24
        && (!img.textKeys().isEmpty() || img.dotsPerMeterX() != 0 || img.dotsPerMeterY() != 0))
        stripped = QImage(img.constBits(), img.width(), img.height(),
                          img.bytesPerLine(), img.format()).copy();
    const QImage &toSave = stripped.isNull() ? img : stripped;

    bool ok;
    if (fmt == QLatin1String("jpg") || fmt == QLatin1String("jpeg"))
        ok = toSave.save(path, "JPG", qBound(1, m_settings->imageQuality(), 100));
    else if (fmt == QLatin1String("webp"))
        ok = toSave.save(path, "WEBP", qBound(1, m_settings->imageQuality(), 100));
    else
        ok = toSave.save(path, "PNG");
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

void AppContext::showQr(const QString &url)
{
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.scheme().isEmpty()) {
        showToast(tr("No valid link to turn into a QR code"), true);
        return;
    }
    const QImage qr = qrPreviewImage(url);
    if (qr.isNull()) {
        showToast(qrAvailable() ? tr("Could not create QR code")
                                : tr("QR codes need zxing-cpp"), true);
        return;
    }
    if (openPreview(qr))
        showToast(tr("QR code preview"));
}

void AppContext::copyImageAs(const QImage &img, const QString &filePath, const QString &url,
                             const QString &format, std::function<void(bool)> done)
{
    const auto copied = [this, done](const QString &text) {
        copyText(text);
        showToast(tr("Copied to clipboard"));
        if (done)
            done(true);
    };
    const auto failed = [this, done](const QString &reason) {
        showToast(reason, true);
        if (done)
            done(false);
    };

    if (format == QLatin1String("path")) {
        if (filePath.isEmpty()) {
            failed(tr("Save the capture first to copy its file path"));
            return;
        }
        copied(filePath);
        return;
    }

    if (format == QLatin1String("markdown") || format == QLatin1String("html")) {
        // Prefer the public upload URL. A local file URI keeps the action useful
        // before upload too, while correctly escaping spaces and non-ASCII paths.
        const QString target = !url.isEmpty()
                               ? url : QUrl::fromLocalFile(filePath).toString(QUrl::FullyEncoded);
        if (target.isEmpty()) {
            failed(tr("Save or upload the capture first to copy it as a link"));
            return;
        }
        if (format == QLatin1String("markdown"))
            copied(QStringLiteral("![](%1)").arg(target));
        else
            copied(QStringLiteral("<img src=\"%1\" alt=\"\">").arg(target.toHtmlEscaped()));
        return;
    }

    failed(tr("Unknown copy format"));
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

QString AppContext::fileDragUri(const QString &path) const
{
    if (path.isEmpty())
        return QString();
    // FullyEncoded: a bare "file:///a b.png" is rejected/truncated by many
    // drop targets — spaces must arrive as %20 in the uri-list.
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
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
    // The export embeds destination secrets (SFTP passwords, API keys) — lock it
    // to the owner before any bytes land (the CLI --export-settings path can
    // target a world-readable /tmp).
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    // Verify the write really landed: a silent partial write (disk full, quota,
    // an unplugged USB/fuse target) must not be reported as a successful backup.
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (f.write(json) != json.size() || !f.flush() || f.error() != QFileDevice::NoError) {
        f.close();
        QFile::remove(path); // don't leave a truncated secrets file behind
        return tr("Cannot write %1").arg(path);
    }
    f.close();
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
        {QStringLiteral("copy-last"), tr("Copy last capture"), m_settings->hotkeyCopyLast()},
        {QStringLiteral("instant-replay"), tr("Start/save instant replay"), m_settings->hotkeyInstantReplay()},
    };
}

// Daemon-authoritative display: whatever key is ACTUALLY bound is what the
// settings UI must show — the stored string is just the app's last wish.
void AppContext::syncHotkeyFromDaemon(const QString &actionId, const QString &portable)
{
    // The daemon REORDERS alternate keys in its replies ("F9, Meta+F9" comes
    // back as "Meta+F9, F9"). A plain string compare in the setter would read
    // that as a KCM edit and persist it (with an immediate disk sync), flipping
    // the user's chip order right after they typed it. Compare set-wise and drop
    // pure reorders — only a genuine binding change should be stored.
    QString stored;
    for (const HotkeyAction &a : hotkeyActions())
        if (a.id == actionId) { stored = a.keys; break; }
    if (GlobalHotkeys::sameBinding(stored, portable))
        return;

    if (actionId == QLatin1String("capture-fullscreen")) m_settings->setHotkeyFullScreen(portable);
    else if (actionId == QLatin1String("capture-region")) m_settings->setHotkeyRegion(portable);
    else if (actionId == QLatin1String("capture-window")) m_settings->setHotkeyWindow(portable);
    else if (actionId == QLatin1String("record-gif")) m_settings->setHotkeyGif(portable);
    else if (actionId == QLatin1String("record-video")) m_settings->setHotkeyRecord(portable);
    else if (actionId == QLatin1String("ocr-region")) m_settings->setHotkeyOcr(portable);
    else if (actionId == QLatin1String("copy-last")) m_settings->setHotkeyCopyLast(portable);
    else if (actionId == QLatin1String("instant-replay")) m_settings->setHotkeyInstantReplay(portable);
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
        if (ok && !GlobalHotkeys::sameBinding(actual, a.keys))
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
            showToast(tr("Ctrl+Esc emergency stop unavailable: the key is taken by the system "
                         "(System Settings → Shortcuts to free it)"));
        }
#ifdef UNISIC_DEV_BUILD
        // Dev-only: F8 runs the smoke test. Fixed key (not user-configurable).
        m_hotkeys->setShortcut(QStringLiteral("smoke-test"),
                               tr("Developer smoke test"), QStringLiteral("F8"));
#endif
        // Upgrade path: older versions grabbed Ctrl+C for a 2s "quick-copy"
        // window (NoAutoloading), and a crash inside it left the grab bound
        // persistently. Release AND unregister the legacy action so a stale
        // grab can't hijack Ctrl+C and no phantom row lingers in the KCM.
        m_hotkeys->releaseShortcut(QStringLiteral("quick-copy"), tr("Copy last capture"));
        m_hotkeys->unregisterAction(QStringLiteral("quick-copy"));
        // Same for the quick-task chooser, dropped in 0.7.1: the tray menu
        // already offers every mode it did. Without this an upgraded install
        // keeps its Meta+Shift+Space grab (dead — nothing listens) and a
        // phantom KCM row for an action that no longer exists.
        m_hotkeys->releaseShortcut(QStringLiteral("quick-task"), tr("Open quick task chooser"));
        m_hotkeys->unregisterAction(QStringLiteral("quick-task"));
        // Purge any zombie component an OLDER binary registered under the
        // DESKTOP-file name (app.unisic.UnisicDev / app.unisic.Unisic) instead
        // of the fixed unique name. Such a duplicate still claims a key grab
        // (e.g. dev Meta+Shift+Q for capture-region) and routes presses to a
        // component this process never listens on, so the hotkey looked dead.
        // No-op when no such component exists (the normal case, incl. stable).
        m_hotkeys->cleanUpComponent(QGuiApplication::desktopFileName());
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
        QStringList conflicts;
        const QStringList report = hotkeyBindStatus(&unbound, true, &conflicts);
        if (unbound > 0)
            qWarning().noquote() << "Hotkey repair:\n" + report.join(QLatin1Char('\n'));
        // A key another component owns daemon-side never reaches us even
        // though it shows as bound — silent-dead without this toast (observed
        // live: a KWin tiling script holding Meta+Shift+F).
        if (!conflicts.isEmpty()) {
            qWarning().noquote() << "Hotkey conflicts:\n" + conflicts.join(QLatin1Char('\n'));
            showToast(tr("Hotkey taken by another app: %1. Pick a different key in "
                         "Settings → Hotkeys, or free it in System Settings → Shortcuts.")
                          .arg(conflicts.join(QStringLiteral("; "))), true);
        }
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
                [this](bool ok, const QVariantMap &triggers) {
            const QString wanted = ok ? QStringLiteral("portal") : QString();
            if (m_hotkeyBackend != wanted) {
                m_hotkeyBackend = wanted;
                emit hotkeysAvailableChanged();
            }
            if (ok)
                qInfo() << "GlobalShortcuts bound via portal," << triggers.size()
                        << "trigger descriptions";
            else
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
            showToast(tr("Could not bind %1; the key is taken by another shortcut").arg(a.keys),
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
        showToast(tr("Some hotkeys could not be bound (keys taken); showing the actual state"),
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
    // Rebuilt live (language switch, tray-icon change): drop the previous
    // icon and menu first, or every rebuild stacks ANOTHER StatusNotifierItem
    // next to the old one in the tray.
    delete m_tray;
    m_tray = nullptr;
    delete m_trayMenu;
    m_trayMenu = nullptr;
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
    if (m_updater && m_updater->restartPending()) {
        // The new version is already swapped in — one click finishes the job.
        menu->addAction(tr("Restart to update to Unisic %1").arg(m_updater->latestVersion()),
                        m_updater, &UpdateChecker::restartNow);
        menu->addSeparator();
    } else if (m_updater && m_updater->updateAvailable()) {
        // Persistent counterpart of the one-shot update toast — a tray-dwelling
        // app may never have a window up when the toast fires.
        menu->addAction(tr("Update available — Unisic %1").arg(m_updater->latestVersion()),
                        this, [this] { emit showMainWindowRequested(); });
        menu->addSeparator();
    }
    menu->addAction(tr("Open Unisic"), this, [this] { emit showMainWindowRequested(); });
    menu->addAction(tr("Quit"), qApp, &QCoreApplication::quit);
    m_tray->setContextMenu(menu);
    m_tray->setToolTip(QGuiApplication::applicationDisplayName());
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
    QIcon icon(QStringLiteral(":/resources/icons/unisic.svg"));
    const QString path = m_settings->trayIconPath();
    if (!path.isEmpty()) {
        QIcon chosen;
        if (isBundledTrayIcon(path))
            chosen = recoloredTrayIcon(path);
        if (chosen.isNull()) {
            QIcon custom(path);
            // availableSizes() is EMPTY for scalable SVGs (no discrete sizes) —
            // gate on whether a pixmap actually renders instead, so .svg works.
            if (!custom.isNull() && !custom.pixmap(QSize(64, 64)).isNull())
                chosen = custom;
        }
        if (!chosen.isNull())
            icon = chosen;
    }
#ifdef UNISIC_DEV_BUILD
    // GRAY tray icon = dev build — tells it apart from the stable app's when
    // both run side by side. Desaturate per-pixel: Format_Grayscale8 would
    // drop the alpha channel (see the project's Qt gotchas).
    QPixmap pm = icon.pixmap(QSize(64, 64));
    if (!pm.isNull()) {
        QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < img.height(); ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const int g = qGray(line[x]);
                line[x] = qRgba(g, g, g, qAlpha(line[x]));
            }
        }
        icon = QIcon(QPixmap::fromImage(img));
    }
#endif
    return icon;
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
    // Keyed on the desktop id, so the dev build (app.unisic.UnisicDev) keeps
    // its own autostart entry and never overwrites the stable one.
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
           + QStringLiteral("/autostart/") + QGuiApplication::desktopFileName()
           + QStringLiteral(".desktop");
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
            "Name=" + QGuiApplication::applicationDisplayName().toUtf8() + "\n"
            "Comment=Screenshots, annotations, uploads and GIF recording\n"
            + autostartExecLine() +
            "Icon=" + QGuiApplication::desktopFileName().toUtf8() + "\n"
            "Terminal=false\n"
            "Categories=Utility;Graphics;\n"
            "X-GNOME-Autostart-enabled=true\n");
    f.close();
    return true;
}

void AppContext::refreshAutostartIfStale()
{
    // Pre-rename installs wrote org.unisic.Unisic.desktop; migrate it or the
    // old entry keeps autostarting alongside (and ignores the toggle).
    const QString legacy = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                           + QStringLiteral("/autostart/org.unisic.Unisic.desktop");
    if (QFile::remove(legacy) && !QFile::exists(autostartFilePath()))
        writeAutostartFile();

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
            showToast(tr("Could not disable autostart: cannot remove %1").arg(path), true);
        emit autostartEnabledChanged(); // reflect the real (post-remove) state
        return;
    }
    if (!writeAutostartFile()) {
        showToast(tr("Could not enable autostart: cannot write %1").arg(path), true);
        return; // state unchanged; the switch snaps back on the next read
    }
    emit autostartEnabledChanged();
}
