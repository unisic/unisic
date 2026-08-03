#include "AppContext.h"
#include "unisic_build_date.h" // generated into the build dir (cmake/BuildDate.cmake)
#include "Settings.h"
#include "capture/CaptureManager.h"
#include "capture/KWinScreenShot2.h"
#include "capture/PortalRequest.h"
#include "overlay/OverlayController.h"
#include "upload/UploadManager.h"
#include "actions/ExternalActionRunner.h"
#include "history/HistoryStore.h"
#include "history/HistoryFilterModel.h"
#include "hotkeys/GlobalHotkeys.h"
#include "hotkeys/ShortcutFormat.h"
#include "diag/CrashHandler.h"
#include "diag/DiagLog.h"
#include "diag/SmokeSupport.h"

#include <csignal>
#include <QTemporaryFile>
#include "update/UpdateChecker.h"
#include "update/VersionCompare.h"
#include "hotkeys/PortalGlobalShortcuts.h"
#ifdef HAVE_X11_HOTKEYS
#include "hotkeys/X11Hotkeys.h"
#endif
#if defined(HAVE_PIPEWIRE) && defined(HAVE_X11)
#include "record/X11ShmGrabber.h"
#include <QThread>
#endif
#include "record/GifRecorder.h"
#include "record/VideoQuality.h"
#include "media/FfmpegUtil.h"
#include "record/InputPermission.h"
#include "record/CursorOverlayPainter.h"
#include "record/KeystrokeOverlayPainter.h"
#include <linux/input-event-codes.h>
#include "capture/ScreenCastSession.h"
#ifdef HAVE_KWIN_SCREENCAST
#include "capture/KWinScreencasting.h"
#include <QCursor>
#endif
#include "record/RecordBorderController.h"
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
#include "theme/ThemeJson.h"
#include "editor/AnnotationCanvas.h"
#include "ConfigPath.h"
#include "FilenameTemplate.h"
#include "ImageEncode.h"
#include <QSaveFile>
#ifdef HAVE_TESSERACT
#include "ocr/OcrEngine.h"
#endif
#ifdef HAVE_ZXING
#include <ZXing/BitMatrix.h>
#include <ZXing/MultiFormatWriter.h>
#endif
// Clipboard offers (the KDE force-image-copy hint), clipboard reads and drop
// payloads all speak QMimeData, so it is needed with or without KGuiAddons.
#include <QMimeData>
#ifdef HAVE_KGUIADDONS
#include <KSystemClipboard>
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
#include <QQuickItem>
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
#include <QFutureWatcher>
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
#include <QDBusReply>
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
    // Keystroke-badge colors follow the active theme (incl. custom themes).
    // Resolved lazily at key-capture start — m_engine is null here.
    m_recorder->setKeystrokeThemeProvider([this]() -> QPair<QColor, QColor> {
        if (m_engine) {
            if (QObject *theme = m_engine->singletonInstance<QObject *>(
                    QStringLiteral("Unisic"), QStringLiteral("Theme")))
                return {theme->property("keystrokeBg").value<QColor>(),
                        theme->property("keystrokeText").value<QColor>()};
        }
        return {QColor(), QColor()};
    });
    refreshWatermarkImage();
    connect(m_settings, &Settings::watermarkImagePathChanged,
            this, &AppContext::refreshWatermarkImage);
    // The Settings preview has to re-render on every knob the watermark has,
    // including the master switch (it shows the bare mock when off) and the
    // logo path (whose own handler above decodes the file first - connecting
    // after it means the preview never renders a stale logo).
    for (auto sig : { &Settings::watermarkEnabledChanged, &Settings::watermarkTextChanged,
                      &Settings::watermarkOpacityChanged, &Settings::watermarkPositionChanged,
                      &Settings::watermarkTypeChanged, &Settings::watermarkImagePathChanged,
                      &Settings::watermarkPatternChanged, &Settings::watermarkScaleChanged })
        connect(m_settings, sig, this, [this] {
            ++m_watermarkPreviewRev;
            emit watermarkPreviewChanged();
        });

    m_updater = new UpdateChecker(m_settings, this);
    // Tray entry follows availability flips only — stateChanged also fires per
    // download-progress chunk and would rebuild the tray continuously.
    connect(m_updater, &UpdateChecker::availabilityChanged, this, &AppContext::setupTray);
    connect(m_updater, &UpdateChecker::updateFound, this, [this](const QString &v) {
        // Native package install: no silent self-update path, but we CAN offer to
        // run install.sh in a terminal. Ask instead of toasting — QML opens the
        // "Install now?" prompt (once per version, gated by updateFound itself).
        if (m_updater->canInstallViaScript()) {
            emit installerUpdatePromptRequested(v);
            return;
        }
        showToast(m_updater->canSelfUpdate()
                      ? tr("Unisic %1 is available - updating automatically").arg(v)
                      : tr("Unisic %1 is available").arg(v));
    });
    // The install.sh-in-a-terminal path started (or couldn't): tell the user.
    connect(m_updater, &UpdateChecker::installerLaunched, this,
            [this](bool ok, const QString &detail) {
                showToast(ok ? tr("Opened a terminal to install the update.")
                             : tr("Couldn't start the update: %1").arg(detail),
                          !ok);
            });
    connect(m_updater, &UpdateChecker::installed, this, [this](const QString &v) {
        setupTray(); // the entry flips to "Restart to update"
        if (tryUpdateRestart())
            return;
        // Busy right now — tell the user it's ready and keep retrying quietly
        // until the app goes idle (recording over, editors closed, window
        // hidden back into the tray).
        showToast(tr("Unisic %1 installed - it will start on the next launch").arg(v));
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
    connect(m_recorder, &GifRecorder::pausedChanged, this, &AppContext::recordingChanged);
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
        // Our own window is in frame exactly like anything else: a recording
        // started from the Record page otherwise films the page that started it.
        // Here rather than at the trigger because the countdown renders as a
        // toast INSIDE that window; endCaptureIsolation() puts it back, and the
        // converting/failed signals that call it cover every way a recording can
        // end. Never for the instant-replay ring: that rolls for as long as the
        // user leaves it armed, and taking the window away for an open-ended
        // period is not "for the duration of a capture", it is losing the window.
        if (!m_recorder->instantReplayActive())
            hideOwnWindowForCapture();
    });
    connect(m_recorder, &GifRecorder::elapsedChanged, this, &AppContext::recordSecondsChanged);
    connect(m_recorder, &GifRecorder::converting, this, [this] {
        m_converting = true;
        endCaptureIsolation();
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
        endCaptureIsolation();
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
    // record-region border (so it works beyond KWin: wlroots…), and the preview
    // window. Elsewhere (GNOME, X11) these fall back or are unsupported.
    //
    // EXCEPT cosmic-comp. qtwayland's QWaylandWindow::setVisible(false) FIRST
    // destroys the surface's role (resetSurfaceRole → delete mShellSurface, i.e.
    // zwlr_layer_surface_v1.destroy) and only THEN unmaps it with
    // wl_surface.attach(nullptr)+commit — a commit on a now roleless surface.
    // wlroots and mutter tolerate that; cosmic-comp treats it as a protocol
    // violation and SILENTLY drops the socket (no wl_display.error event at all),
    // so Qt aborts with "The Wayland connection broke" the instant ANY layer
    // surface is torn down — which is after every capture card, region overlay and
    // record border (pop-os/cosmic-comp#1590, #2159; same class as the Ghostty
    // hide crash). Every teardown path hits it (close, setVisible(false),
    // deleteLater all route through setVisible(false)), and the order is inside
    // qtwayland — there is no in-app reorder. Until qtwayland unmaps-before-role or
    // cosmic-comp stops disconnecting, treat COSMIC as having no usable layer-shell:
    // the overlay falls back to a fullscreen toplevel (xdg teardown is fine here —
    // every Qt window on COSMIC proves it) and the card/border to the XWayland
    // override-redirect helper (the GNOME path — COSMIC ships XWayland too).
    // UNISIC_FORCE_LAYERSHELL=1 re-enables it once the upstream bug is gone.
    const bool cosmicLayerShellBroken =
        qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("COSMIC"), Qt::CaseInsensitive)
        && qEnvironmentVariable("UNISIC_FORCE_LAYERSHELL") != QLatin1String("1");
    m_layerShellAvailable = !cosmicLayerShellBroken
                            && QGuiApplication::platformName().startsWith(QLatin1String("wayland"))
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
    // Deferred to the first event-loop pass so the asynchronous registration
    // burst starts after the QML engine has loaded the window, and late enough
    // that startup toasts (e.g. a Ctrl+Esc conflict) have a UI to appear in.
    QTimer::singleShot(0, this, &AppContext::defineHotkeys);
    // Singularity rewrites labwc's rc.xml (our custom-shortcut store there) on
    // every login and shortcut edit, silently dropping Unisic's keybinds —
    // watch the store and re-assert them whenever they vanish.
    armDesktopShortcutReassert();

    // Daily release check + AppImage self-install (suppressed on dev builds
    // and when the setting is off — the checker logs why it stays quiet).
    m_updater->startAutoCheck();

    // ffmpeg's encoder list probe can take seconds on a cold filesystem. Run it
    // once off-thread; the watcher delivers its value on this object's thread.
    // The worker captures no QObject/QPointer, so teardown cannot race a weak
    // pointer's control block.
    auto *encoderProbe = new QFutureWatcher<QPair<bool, bool>>(this);
    connect(encoderProbe, &QFutureWatcher<QPair<bool, bool>>::finished, this,
            [this, encoderProbe] {
        const auto available = encoderProbe->result();
        encoderProbe->deleteLater();
        m_vaapiAvailable = available.first;
        m_nvencAvailable = available.second;
        emit recordingCapabilitiesChanged();
    });
    encoderProbe->setFuture(QtConcurrent::run([] {
        return qMakePair(
            FfmpegUtil::hardwareEncoderAvailable(QStringLiteral("vaapi")),
            FfmpegUtil::hardwareEncoderAvailable(QStringLiteral("nvenc")));
    }));

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
                qWarning() << "No ScreenCast portal backend on this desktop - recording disabled"
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
    // On an X11 session the portal is not the only way in: XShm grabs the
    // monitor directly, which is exactly how those portal-less desktops record.
    return m_screenCastPortalPresent || capX11Capture();
#else
    return false;
#endif
}

bool AppContext::capX11Capture() const
{
#if defined(HAVE_PIPEWIRE) && defined(HAVE_X11)
    // The X11 grabber feeds the same sampler/encoder pipeline, which is itself
    // compiled under HAVE_PIPEWIRE - hence both flags, plus an actual X11 session.
    return QGuiApplication::platformName() == QLatin1String("xcb");
#else
    return false;
#endif
}

bool AppContext::capRecordWindowSource() const
{
    // Window recording is resolved by the portal's window picker (or KWin's).
    // The X11 backend records a monitor rect and has no window source, so an
    // X11-only desktop can record screen and region but not a single window.
    return m_screenCastPortalPresent;
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

bool AppContext::ocrHasLanguages() const
{
#ifdef HAVE_TESSERACT
    return !OcrEngine::detectedLanguages().isEmpty();
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

bool AppContext::ffmpegAvailable() const
{
    // Resolved once: the property is read from QML bindings (menu entries that
    // re-evaluate on every open), and a PATH walk per binding pass is a
    // filesystem hit nobody asked for. CONSTANT for the same reason - ffmpeg
    // appearing mid-session is not a case worth a file watcher.
    static const bool found =
        !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
    return found;
}

bool AppContext::perAppAudioAvailable() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("pw-record")).isEmpty()
           && !QStandardPaths::findExecutable(QStringLiteral("pw-dump")).isEmpty();
}

// Pure: runs pw-dump + parses its JSON with no AppContext/GUI state, so both
// query fronts below are safe to call from a worker thread.
static QJsonArray pwDumpNodes()
{
    const QString helper = QStandardPaths::findExecutable(QStringLiteral("pw-dump"));
    if (helper.isEmpty())
        return {};
    QProcess process;
    process.start(helper, {});
    if (!process.waitForFinished(2500)) {
        process.kill();
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(process.readAllStandardOutput());
    return doc.isArray() ? doc.array() : QJsonArray();
}

static QVariantList queryAudioApplicationNodesImpl()
{
    QVariantList result;
    for (const QJsonValue &value : pwDumpNodes()) {
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

// Capture-capable inputs: real mics (Audio/Source) and virtual sources such as
// an EasyEffects processed mic (Audio/Source/Virtual). Monitors never appear -
// PipeWire models them as sink ports, not nodes. The id is node.name, which is
// also the source's pipewire-pulse name, i.e. exactly what ffmpeg's pulse
// input takes - and unlike object.serial it survives a reboot in the setting.
static QVariantList queryAudioInputDevicesImpl()
{
    QVariantList result;
    for (const QJsonValue &value : pwDumpNodes()) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("type")).toString()
            != QLatin1String("PipeWire:Interface:Node"))
            continue;
        const QJsonObject props = object.value(QStringLiteral("info")).toObject()
                                      .value(QStringLiteral("props")).toObject();
        const QString mediaClass = props.value(QStringLiteral("media.class")).toString();
        if (mediaClass != QLatin1String("Audio/Source")
            && mediaClass != QLatin1String("Audio/Source/Virtual"))
            continue;
        const QString id = props.value(QStringLiteral("node.name")).toString();
        if (id.isEmpty())
            continue;
        QString label = props.value(QStringLiteral("node.description")).toString();
        if (label.isEmpty())
            label = props.value(QStringLiteral("node.nick")).toString();
        if (label.isEmpty())
            label = id;
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
    // it synchronously froze the whole UI when the audio dropdown opened.
    auto *watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher] {
        const QVariantList nodes = watcher->result();
        watcher->deleteLater();
        emit audioApplicationNodesReady(nodes);
    });
    watcher->setFuture(QtConcurrent::run(queryAudioApplicationNodesImpl));
}

bool AppContext::audioInputListAvailable() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("pw-dump")).isEmpty();
}

QVariantList AppContext::audioInputDevices() const
{
    return queryAudioInputDevicesImpl();
}

void AppContext::requestAudioInputDevices()
{
    // Same off-thread rule as requestAudioApplicationNodes.
    auto *watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher] {
        const QVariantList devices = watcher->result();
        watcher->deleteLater();
        emit audioInputDevicesReady(devices);
    });
    watcher->setFuture(QtConcurrent::run(queryAudioInputDevicesImpl));
}

QImage qrPreviewImage(const QString &url)
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

QString AppContext::changelogVersion() const
{
#ifdef UNISIC_DEV_BUILD
    // Dev builds run the next release's code, so show the next release's
    // notes: the file is newest-first, take the first `## ` heading.
    QFile f(QStringLiteral(":/resources/CHANGELOG.md"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!f.atEnd()) {
            const QString t = QString::fromUtf8(f.readLine()).trimmed();
            if (t.startsWith(QLatin1String("## ")))
                return t.mid(3).trimmed();
        }
    }
#endif
    return appVersion();
}

QString AppContext::changelog(const QString &lang) const
{
    QFile f(QStringLiteral(":/resources/CHANGELOG.md"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    const QString verHeading = QStringLiteral("## ") + changelogVersion();
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

QString AppContext::effectiveOcrLanguages() const
{
#ifdef HAVE_TESSERACT
    // Auto-detect: recognize with every installed langpack. Fall back to the
    // manual spec when the scan finds nothing (libtesseract may still resolve a
    // baked-in TESSDATA_PREFIX we didn't enumerate), so Init never gets "".
    if (m_settings->ocrAutoLanguage()) {
        const QString detected = OcrEngine::detectedLanguages();
        if (!detected.isEmpty())
            return detected;
    }
#endif
    return m_settings->ocrLanguages();
}

void AppContext::ocrImage(const QImage &img)
{
#ifdef HAVE_TESSERACT
    if (img.isNull()) {
        showToast(tr("Nothing to recognize"));
        return;
    }
    showToast(tr("Recognizing text…"));
    m_ocr->recognize(img, effectiveOcrLanguages(), m_settings->ocrAutoLanguage(), [this](const QString &text, const QString &err) {
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
    m_ocr->recognizeBoxes(img, effectiveOcrLanguages(), m_settings->ocrAutoLanguage(), std::move(cb));
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
                                          QStringLiteral("es"), QStringLiteral("it"),
                                          QStringLiteral("fr"), QStringLiteral("ru"),
                                          QStringLiteral("de")};
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
    const bool filePending = !m_nextCaptureOutputPath.isEmpty();
    m_nextCaptureOutputPath.clear();
    m_nextCaptureOutputFormat.clear();
    m_nextCaptureToStdout = false;
    m_nextCaptureDestination.clear();
    if (stdoutPending && !error.isEmpty())
        emit cliCaptureReady({}, error);
    // Nothing will arrive for a `--output PATH` run that was cancelled - say so,
    // or the process that asked for it waits for a capture that is not coming.
    if (filePending)
        emit cliCaptureFinished(false);
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
        text += tr(". GNOME is blocking silent screenshots for Unisic - run "
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

bool AppContext::capCursorMetadata() const
{
#ifdef HAVE_PIPEWIRE
    return (ScreenCastSession::availableCursorModes()
            & uint(ScreenCastSession::CursorMode::Metadata)) != 0;
#else
    return false;
#endif
}

#ifdef HAVE_KWIN_SCREENCAST
KWinScreencasting *kwinScreencastProbe()
{
    static KWinScreencasting *probe = new KWinScreencasting(qApp);
    return probe;
}
#endif

bool AppContext::capKWinRecord() const
{
#ifdef HAVE_KWIN_SCREENCAST
    return kwinScreencastProbe()->isAvailable();
#else
    return false;
#endif
}

QString AppContext::clickCaptureBlockedReason() const
{
    switch (InputPermission::probe()) {
    case InputPermission::Available:
        return {};
    case InputPermission::NotBuilt:
        return tr("This build has no libinput support, so clicks cannot be detected.");
    case InputPermission::NoPermission:
        break;
    }
    return tr("Reading mouse clicks needs access to input devices. Run “%1”, then log out and back in.")
        .arg(InputPermission::fixHint());
}

QString AppContext::keystrokeCaptureBlockedReason() const
{
    switch (InputPermission::probe()) {
    case InputPermission::Available:
        return {};
    case InputPermission::NotBuilt:
        return tr("This build has no libinput support, so key presses cannot be detected.");
    case InputPermission::NoPermission:
        break;
    }
    return tr("Reading key presses needs access to input devices. Run “%1”, then log out and back in.")
        .arg(InputPermission::fixHint());
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

void AppContext::beginCaptureIsolation()
{
    if (m_settings->doNotDisturbWhileCapturing() && capDoNotDisturb())
        m_dnd->acquire();
}

void AppContext::endCaptureIsolation()
{
    if (m_dnd)
        m_dnd->release();
    restoreOwnWindowAfterCapture();
}

bool AppContext::hideOwnWindowForCapture()
{
    if (!m_settings->hideWindowOnCapture())
        return false;
    if (m_hiddenForCapture)
        return true; // already down for this capture - do not stack restores
    QQuickWindow *win = mainWindow();
    if (!win || !win->isVisible())
        return false; // triggered from a hotkey or the tray: nothing to hide
    win->hide();
    m_hiddenForCapture = win;
    return true;
}

void AppContext::restoreOwnWindowAfterCapture()
{
    if (!m_hiddenForCapture)
        return;
    QQuickWindow *win = m_hiddenForCapture;
    m_hiddenForCapture = nullptr;
    // show(), not showNormal()/requestActivate(): the editor window opens
    // straight after this on the default settings, and stealing focus back from
    // it is exactly the wrong end of the capture to be looking at.
    win->show();
}

void AppContext::withCaptureDelay(std::function<void()> fn)
{
    withDelay([this, fn = std::move(fn)]() mutable {
        if (!hideOwnWindowForCapture()) {
            fn();
            return;
        }
        QTimer::singleShot(kSelfHideSettleMs, this, std::move(fn));
    });
}

QQuickWindow *AppContext::mainWindow() const
{
    // rootObjects() lives on QQmlApplicationEngine (what main() passes in).
    auto *appEngine = qobject_cast<QQmlApplicationEngine *>(m_engine);
    if (!appEngine)
        return nullptr;
    const QList<QObject *> roots = appEngine->rootObjects();
    for (QObject *o : roots)
        if (auto *w = qobject_cast<QQuickWindow *>(o))
            return w;
    return nullptr;
}

void AppContext::captureFullScreen()
{
    // Preference: "full screen" can mean the whole workspace (default) or just
    // the monitor under the cursor. The single-screen path keeps its own guards
    // and inherits the full-screen task preset set by the caller.
    if (m_settings->fullscreenScope() == QLatin1String("screen")) {
        captureScreenUnderCursor();
        return;
    }
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
    beginCaptureIsolation();
    withCaptureDelay([this, inhibited] {
        // Re-check: with a capture delay configured, the region overlay may
        // have opened between the keypress and this deferred fire.
        if (m_overlay->active()) {
            m_captureInFlight = false;
            endCaptureIsolation();
            m_nextCaptureTask = {};
            clearCliCapture(tr("Another capture is already active"));
            return;
        }
        m_capture->captureWorkspace([this, inhibited](const QImage &img, const QString &err) {
            m_captureInFlight = false;
            endCaptureIsolation();
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
    beginCaptureIsolation();
    withCaptureDelay([this, inhibited, initialTool] {
        if (m_overlay->active()) {
            m_captureInFlight = false;
            endCaptureIsolation();
            m_nextCaptureTask = {};
            clearCliCapture(tr("Another capture is already active"));
            return;
        }
        m_overlay->pickAnnotatedImage([this, inhibited](const QImage &img) {
            m_captureInFlight = false;
            endCaptureIsolation();
            if (!img.isNull()) {
                // Persist the rect for re-capture BEFORE finishCapture so a
                // task preset that uploads/deletes still leaves the region.
                const QRect r = m_overlay->lastRegionLogical();
                if (!r.isEmpty())
                    m_settings->setLastCaptureRegion(
                        QStringLiteral("%1|%2,%3,%4,%5")
                            .arg(m_overlay->lastRegionScreen())
                            .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()));
                finishCapture(img, inhibited, m_overlay->takeCopyRequested());
            }
            else
            {
                m_nextCaptureTask = {};
                clearCliCapture(tr("Capture cancelled"));
            }
        }, initialTool == AnnotationCanvas::Measure ? OverlayController::Purpose::Measure
                                                  : OverlayController::Purpose::Shot,
           initialTool);
    });
}

void AppContext::captureScreenUnderCursor()
{
    if (m_captureInFlight || m_overlay->active()) { // see captureFullScreen
        m_nextCaptureTask = {};
        clearCliCapture(tr("Another capture is already active"));
        return;
    }
    const bool inhibited = nowInhibited();
    m_captureInFlight = true;
    beginCaptureIsolation();
    withCaptureDelay([this, inhibited] {
        if (m_overlay->active()) { // re-check after the capture delay
            m_captureInFlight = false;
            endCaptureIsolation();
            m_nextCaptureTask = {};
            clearCliCapture(tr("Another capture is already active"));
            return;
        }
        // Hint only: on KWin the compositor resolves the pointer's screen
        // itself (CaptureActiveScreen); QCursor::pos() is exact on Wayland only
        // while the pointer is over one of our windows (e.g. the tray menu).
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        m_capture->captureActiveScreen(screen, [this, inhibited](const QImage &img, const QString &err) {
            m_captureInFlight = false;
            endCaptureIsolation();
            if (!err.isEmpty() || img.isNull()) {
                m_nextCaptureTask = {};
                clearCliCapture(err.isEmpty() ? tr("Empty capture") : err);
                if (err != QLatin1String("cancelled"))
                    showToast(captureErrorGuidance(err.isEmpty() ? tr("Empty capture") : err), true);
                return;
            }
            finishCapture(img, inhibited);
        });
    });
}

void AppContext::recaptureLastRegion()
{
    if (m_captureInFlight || m_overlay->active()) { // see captureFullScreen
        m_nextCaptureTask = {};
        clearCliCapture(tr("Another capture is already active"));
        return;
    }
    // "<screen>|<x>,<y>,<w>,<h>" in logical px of that screen (see
    // OverlayController::confirmFromWindow).
    const QString stored = m_settings->lastCaptureRegion();
    const int bar = stored.indexOf(QLatin1Char('|'));
    const QStringList parts = stored.mid(bar + 1).split(QLatin1Char(','));
    QRect rect;
    if (bar > 0 && parts.size() == 4) {
        bool okAll = true;
        int v[4];
        for (int i = 0; i < 4; ++i) {
            bool ok = false;
            v[i] = parts[i].toInt(&ok);
            okAll = okAll && ok;
        }
        if (okAll && v[2] > 1 && v[3] > 1)
            rect = QRect(v[0], v[1], v[2], v[3]);
    }
    if (rect.isEmpty()) {
        m_nextCaptureTask = {};
        clearCliCapture(tr("No region to re-capture"));
        showToast(tr("No region to re-capture yet - take a region screenshot first"), true);
        return;
    }
    QScreen *target = nullptr;
    const QString name = stored.left(bar);
    for (QScreen *s : QGuiApplication::screens())
        if (s->name() == name) { target = s; break; }
    if (!target) {
        m_nextCaptureTask = {};
        clearCliCapture(tr("Region's screen is no longer connected"));
        showToast(tr("The screen that region was on is no longer connected"), true);
        return;
    }
    const bool inhibited = nowInhibited();
    m_captureInFlight = true;
    beginCaptureIsolation();
    withCaptureDelay([this, inhibited, target, rect] {
        if (m_overlay->active()) { // re-check after the capture delay
            m_captureInFlight = false;
            endCaptureIsolation();
            m_nextCaptureTask = {};
            clearCliCapture(tr("Another capture is already active"));
            return;
        }
        m_capture->captureScreen(target, [this, inhibited, target, rect](const QImage &img, const QString &err) {
            m_captureInFlight = false;
            endCaptureIsolation();
            if (!err.isEmpty() || img.isNull()) {
                m_nextCaptureTask = {};
                clearCliCapture(err.isEmpty() ? tr("Empty capture") : err);
                if (err != QLatin1String("cancelled"))
                    showToast(captureErrorGuidance(err.isEmpty() ? tr("Empty capture") : err), true);
                return;
            }
            // The stored rect is LOGICAL px; the captured image can be native
            // (KWin) or uniformly scaled (portal crop) — rescale, then crop.
            const double s = double(img.width()) / target->geometry().width();
            const QRectF scaled(rect.x() * s, rect.y() * s, rect.width() * s, rect.height() * s);
            const QRect crop = scaled.toAlignedRect().intersected(img.rect());
            if (crop.width() < 2 || crop.height() < 2) {
                m_nextCaptureTask = {};
                clearCliCapture(tr("Empty capture"));
                showToast(tr("The stored region no longer fits that screen"), true);
                return;
            }
            QImage out = img.copy(crop);
            out.setDevicePixelRatio(img.devicePixelRatio());
            finishCapture(out, inhibited);
        });
    });
}

void AppContext::captureRegionOcr()
{
    if (m_captureInFlight || m_overlay->active())
        return;
    m_captureInFlight = true;
    beginCaptureIsolation();
    withCaptureDelay([this] {
        if (m_overlay->active()) {
            m_captureInFlight = false;
            endCaptureIsolation();
            return;
        }
        m_overlay->pickAnnotatedImage([this](const QImage &img) {
            m_captureInFlight = false;
            endCaptureIsolation();
            if (!img.isNull())
                ocrImage(img);   // recognizes (QR first, then text) + copies
        }, OverlayController::Purpose::Ocr);
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
    beginCaptureIsolation();
    withCaptureDelay([this, inhibited] {
        if (m_overlay->active()) { // re-check after the capture delay
            m_captureInFlight = false;
            endCaptureIsolation();
            m_nextCaptureTask = {};
            clearCliCapture(tr("Another capture is already active"));
            return;
        }
        m_capture->captureActiveWindow([this, inhibited](const QImage &img, const QString &err) {
            m_captureInFlight = false;
            endCaptureIsolation();
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
    }, OverlayController::Purpose::Gif);
}

void AppContext::startGifFullScreen()
{
    if (recording()) return;
    m_pendingRecordRegion = QRect();
    m_pendingRecordScreen = nullptr; // stale region target would misplace the countdown
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
    m_pendingRecordScreen = nullptr;
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
    }, OverlayController::Purpose::Video);
}

void AppContext::startVideoWindow()
{
    if (recording()) return;
    // Reachable from the tray menu and the dev pane too, not just the (disabled)
    // Window button: on an X11-only desktop there is no window picker, so say so
    // instead of opening a portal session that fails with a raw D-Bus error.
    if (!capRecordWindowSource()) {
        showToast(tr("Recording a single window needs a window picker this desktop "
                     "does not provide - record the screen or a region instead."), true);
        return;
    }
    m_pendingRecordRegion = QRect();
    m_pendingRecordScreen = nullptr; // stale target would misplace the countdown
    startRecorderCountdown([this](bool hold) {
        m_recorder->start(videoOutput(), GifRecorder::Window, {}, nullptr, hold);
    });
}

void AppContext::stopRecording()
{
    m_recorder->stop();
}

void AppContext::togglePauseRecording()
{
    m_recorder->togglePause();
    // The GNOME/mutter record-border runs in a separate XWayland helper process
    // that renders its own badge — the reactive QML binding only reaches the KDE
    // in-process border. Push the pause state over the same stdin channel the
    // countdown uses so the helper's badge reads PAUSED and freezes its clock.
    if (m_recordBorderHelper && m_recordBorderHelper->state() != QProcess::NotRunning)
        m_recordBorderHelper->write(m_recorder->paused() ? "p1\n" : "p0\n");
}

void AppContext::startInstantReplay()
{
    if (recording()) return;
    m_pendingRecordRegion = {};
    m_pendingRecordScreen = nullptr;
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
        beginCaptureIsolation();
        begin(false);
        return;
    }
    if (m_recordHoldActive)
        return; // a second trigger while a hold is pending must not stack
    m_recordHoldActive = true;
    beginCaptureIsolation();
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
    } else {
        // Full-screen and window recordings have no region frame, so a small
        // toast was the only cue that anything was happening — easy to miss on a
        // full-screen recording. Show the big countdown centered on the screen
        // instead, on the same layer-shell / XWayland-helper path as the region
        // frame. It is torn down the instant recording begins.
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen) {
            const qreal dpr = screen->devicePixelRatio() > 0 ? screen->devicePixelRatio() : 1.0;
            const QRect full(QPoint(0, 0),
                             QSize(qRound(screen->geometry().width() * dpr),
                                   qRound(screen->geometry().height() * dpr)));
            // Window recordings: scale the disc to the recorded window — the
            // stream is live by countdown time (armed), so its size is known
            // and the disc behaves like a region frame of that size. Full
            // screen keeps the capped whole-surface disc (no ref). The
            // window's POSITION is not knowable on Wayland, so the disc stays
            // centered on the screen; only its size tracks the target.
            QSize cdRef;
            if (m_recorder->sourceType() == GifRecorder::Window) {
                const QSize ss = m_recorder->armedStreamSize();
                if (!ss.isEmpty())
                    cdRef = QSize(qRound(ss.width() / dpr), qRound(ss.height() / dpr));
            }
            showRecordBorder(full, screen, secs, /*countdownOnly=*/true, cdRef);
            inFrame = (m_recordBorderWindow != nullptr || m_recordBorderHelper != nullptr);
        }
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
    if (m_recordBorderCountdownOnly) {
        // Full-screen / window overlay: the surface sits OVER the recorded area,
        // so it must be gone entirely before the first frame, not just blanked.
        // The tail below lets the compositor repaint the bare screen first.
        hideRecordBorder();
    } else if (inFrame) {
        setRecordBorderCountdown(0);
    }
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

// One-liner FFmpeg version banner ("ffmpeg version 6.1 …"), or "" if ffmpeg is
// absent/hangs. Synchronous, but only ever runs behind a user click (the
// diagnostics button / smoke test), and is bounded to 1.5 s.
static QString ffmpegVersionLine()
{
    const QString exe = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (exe.isEmpty())
        return QString();
    QProcess p;
    p.start(exe, {QStringLiteral("-version")});
    if (!p.waitForFinished(1500)) {
        p.kill();
        p.waitForFinished(200);
        return QString();
    }
    return QString::fromLocal8Bit(p.readAllStandardOutput()).section(QLatin1Char('\n'), 0, 0).trimmed();
}

QString AppContext::systemDiagnostics() const
{
    // Deliberately NOT translated: this is a copy-paste technical artifact for
    // an issue report (English keys), same convention as the smoke-test log.
    const auto yn = [](bool b) { return b ? QStringLiteral("yes") : QStringLiteral("no"); };
    const auto tool = [](const QString &n) {
        const QString p = QStandardPaths::findExecutable(n);
        return p.isEmpty() ? QStringLiteral("%1: MISSING").arg(n)
                           : QStringLiteral("%1: %2").arg(n, p);
    };
    QStringList L;
    L << QStringLiteral("Unisic %1 (build %2)").arg(appVersion(), buildNumber());
    if (!buildDate().isEmpty())
        L << QStringLiteral("Build date: %1").arg(buildDate());
    L << QStringLiteral("Qt: %1 (runtime %2)").arg(QStringLiteral(QT_VERSION_STR),
                                                    QString::fromLatin1(qVersion()));
    L << QStringLiteral("App id: %1  ·  config: %2")
             .arg(QCoreApplication::applicationName(), m_settings->configPath());

    L << QString() << QStringLiteral("[Desktop]");
    L << QStringLiteral("XDG_CURRENT_DESKTOP: %1")
             .arg(qEnvironmentVariable("XDG_CURRENT_DESKTOP", QStringLiteral("(unset)")));
    L << QStringLiteral("XDG_SESSION_TYPE: %1")
             .arg(qEnvironmentVariable("XDG_SESSION_TYPE", QStringLiteral("(unset)")));
    L << QStringLiteral("Wayland display: %1")
             .arg(qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") ? QStringLiteral("(none)")
                                                                 : QStringLiteral("yes"));
    auto *bi = QDBusConnection::sessionBus().interface();
    L << QStringLiteral("KWin on bus: %1")
             .arg(yn(bi && bi->isServiceRegistered(QStringLiteral("org.kde.KWin"))));

    L << QString() << QStringLiteral("[Build features]");
#ifdef HAVE_PIPEWIRE
    L << QStringLiteral("PipeWire: yes");
#else
    L << QStringLiteral("PipeWire: no");
#endif
#ifdef HAVE_TESSERACT
    L << QStringLiteral("Tesseract OCR: yes");
#else
    L << QStringLiteral("Tesseract OCR: no");
#endif
#ifdef HAVE_ZXING
    L << QStringLiteral("ZXing (QR/barcode): yes");
#else
    L << QStringLiteral("ZXing (QR/barcode): no");
#endif
#ifdef HAVE_KGUIADDONS
    L << QStringLiteral("KGuiAddons (Klipper history): yes");
#else
    L << QStringLiteral("KGuiAddons (Klipper history): no");
#endif
#ifdef HAVE_TRANSLATIONS
    L << QStringLiteral("Translations baked in: yes");
#else
    L << QStringLiteral("Translations baked in: no");
#endif
    // Every remaining optional flag, listed even when off: a feature compiled
    // out by a missing -dev package in one packaging channel is invisible from
    // the outside, and that is exactly how KWin-native recording shipped
    // disabled everywhere. A pasted diagnostics block now says so outright.
#ifdef HAVE_LAYERSHELL
    L << QStringLiteral("Layer shell (on-top card): yes");
#else
    L << QStringLiteral("Layer shell (on-top card): no");
#endif
#ifdef HAVE_LIBINPUT
    L << QStringLiteral("libinput (click/key overlays): yes");
#else
    L << QStringLiteral("libinput (click/key overlays): no");
#endif
#ifdef HAVE_KWIN_SCREENCAST
    L << QStringLiteral("KWin-native screencast: yes");
#else
    L << QStringLiteral("KWin-native screencast: no");
#endif
#ifdef HAVE_X11
    L << QStringLiteral("X11 (XShm) capture: yes");
#else
    L << QStringLiteral("X11 (XShm) capture: no");
#endif
#ifdef HAVE_X11_HOTKEYS
    L << QStringLiteral("X11 global hotkeys: yes");
#else
    L << QStringLiteral("X11 global hotkeys: no");
#endif

    L << QString() << QStringLiteral("[Capabilities]");
    L << QStringLiteral("Recording (ScreenCast): %1").arg(yn(recordingAvailable()));
    L << QStringLiteral("Native notifications: %1").arg(yn(capNativeNotification()));
    L << QStringLiteral("Custom notification card: %1%2").arg(
             yn(capCustomNotification()),
             capCustomNotification() ? QString()
                                     : QStringLiteral(" (%1)").arg(customNotificationReason()));
    L << QStringLiteral("Record region frame: %1").arg(yn(capRecordBorder()));
    L << QStringLiteral("Screenshot cursor: %1").arg(yn(capScreenshotCursor()));
    L << QStringLiteral("Cursor metadata: %1").arg(yn(capCursorMetadata()));
    L << QStringLiteral("Video playback: %1").arg(yn(capVideoPlayback()));
    L << QStringLiteral("Do not disturb: %1").arg(yn(capDoNotDisturb()));

    L << QString() << QStringLiteral("[External tools]");
    L << tool(QStringLiteral("ffmpeg"));
    const QString ffv = ffmpegVersionLine();
    if (!ffv.isEmpty())
        L << QStringLiteral("  %1").arg(ffv);
    L << tool(QStringLiteral("ffprobe"));
    L << tool(QStringLiteral("wl-copy"));
    L << tool(QStringLiteral("grim"));
    L << tool(QStringLiteral("pw-play"));
#ifdef HAVE_TESSERACT
    const QString langs = OcrEngine::detectedLanguages();
    L << QStringLiteral("Tesseract langpacks: %1")
             .arg(langs.isEmpty() ? QStringLiteral("(none installed)") : langs);
    L << QStringLiteral("OCR script-detect (osd): %1")
             .arg(yn(OcrEngine::scriptDetectionAvailable()));
#endif
    L << QString() << QStringLiteral("[Log]");
    const QString lf = DiagLog::logFilePath();
    L << QStringLiteral("File: %1").arg(lf.isEmpty() ? QStringLiteral("(memory only)") : lf);
    L << QStringLiteral("Buffered lines: %1").arg(DiagLog::bufferedLineCount());
    const DiagLog::PreviousRun &prev = DiagLog::previousRun();
    L << QStringLiteral("Previous run: %1").arg(
        prev.outcome == DiagLog::PreviousRun::Crashed
            ? QStringLiteral("crashed (%1)").arg(prev.signalName)
        : prev.outcome == DiagLog::PreviousRun::Clean  ? QStringLiteral("clean exit")
        : prev.outcome == DiagLog::PreviousRun::Killed ? QStringLiteral("ended without a clean exit (kill/OOM/power)")
                                                       : QStringLiteral("unknown (no earlier log)"));

    return L.join(QLatin1Char('\n'));
}

QString AppContext::diagnosticsWithLog() const
{
    // What the user actually attaches to an issue: the static picture plus what
    // the app was DOING. Kept a separate invokable so the plain Copy
    // diagnostics button stays a small, obviously safe paste.
    QString out = systemDiagnostics();
    const DiagLog::PreviousRun &prev = DiagLog::previousRun();
    if (!prev.report.isEmpty())
        out += QStringLiteral("\n\n[Crash report from the previous run]\n") + prev.report;
    out += QStringLiteral("\n\n[Recent log]\n") + DiagLog::recentLines();
    return out;
}

QString AppContext::logFilePath() const
{
    const QString p = DiagLog::logFilePath();
    return p.isEmpty() ? DiagLog::logDirPath() : p;
}

bool AppContext::hasPreviousCrash() const
{
    return DiagLog::previousRun().outcome == DiagLog::PreviousRun::Crashed;
}

bool AppContext::hasUnseenCrash() const
{
    const QString key = DiagLog::previousRunKey();
    return !key.isEmpty() && m_settings && m_settings->crashNoticeSeen() != key;
}

void AppContext::markCrashNoticeSeen()
{
    if (m_settings)
        m_settings->setCrashNoticeSeen(DiagLog::previousRunKey());
}

void AppContext::showLogInFileManager()
{
    const QString p = DiagLog::logFilePath();
    if (p.isEmpty()) {
        showToast(tr("No log file was opened for this run"), true);
        return;
    }
    showInFileManager(p);
}

QVariantList AppContext::dependencyReport() const
{
    QVariantList out;
    const auto add = [&out](const QString &label, bool ok, bool warn, const QString &detail) {
        out.append(QVariantMap{{QStringLiteral("label"), label},
                               {QStringLiteral("ok"), ok},
                               {QStringLiteral("warn"), warn},
                               {QStringLiteral("detail"), detail}});
    };

    const bool ffmpeg = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
    add(tr("FFmpeg"), ffmpeg, true,
        ffmpeg ? tr("Found - screen recording and GIF export are available.")
               : tr("Missing. Screen recording and GIF export need FFmpeg. Install the \"ffmpeg\" package."));

    const bool wlclip = !QStandardPaths::findExecutable(QStringLiteral("wl-copy")).isEmpty();
    add(tr("wl-clipboard"), wlclip, false,
        wlclip ? tr("Found - copy to clipboard is at its most reliable.")
               : tr("Optional. Install \"wl-clipboard\" for the most reliable copy-to-clipboard on Wayland."));

#ifdef HAVE_TESSERACT
    const bool haveLangs = ocrHasLanguages();
    add(tr("OCR language pack"), haveLangs, true,
        haveLangs ? tr("Found - text recognition (OCR) is ready.")
                  : tr("Missing. OCR is built in but no Tesseract language pack is installed. Install one, e.g. \"tesseract-langpack-eng\"."));
    if (haveLangs) {
        const bool osd = OcrEngine::scriptDetectionAvailable();
        add(tr("OCR auto-language (osd)"), osd, false,
            osd ? tr("Found - OCR detects the script of each capture automatically.")
                : tr("Optional. Install the Tesseract \"osd\" pack so OCR auto-language works across scripts."));
    }
#endif
    return out;
}

bool AppContext::hasDependencyWarnings() const
{
    const QVariantList rep = dependencyReport();
    for (const QVariant &v : rep) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("warn")).toBool() && !m.value(QStringLiteral("ok")).toBool())
            return true;
    }
    return false;
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

const QStringList &bundledSoundIds()
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

QImage devTestImage()
{
    QImage img(320, 200, QImage::Format_ARGB32);
    img.fill(QColor(0x2E, 0x23, 0x6C));
    return img;
}

#ifdef HAVE_KGUIADDONS
// KDE Plasma / KWin: Klipper only copies an image into its HISTORY (the tray
// applet, i.e. paste-it-later) when the clipboard offer carries the
// x-kde-force-image-copy marker MIME. Plain image/png — what QClipboard and
// wl-copy advertise — is pasteable right now but is never recorded, so the
// shot drops out of history the moment anything else is copied (issue #51,
// reported by Augusto-Lescano). The marker is an empty payload wl-copy cannot
// attach, so the offer must be built as a QMimeData. Spectacle/Flameshot do
// exactly this. Ownership passes to KSystemClipboard::setMimeData.
QMimeData *makeForceImageMime(const QImage &img)
{
    auto *mime = new QMimeData;
    mime->setImageData(img);
    mime->setData(QStringLiteral("x-kde-force-image-copy"), QByteArray());
    return mime;
}
#endif

QImage ocrBoxTestImage()
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
    // A window we hid for a capture counts as visible: it is coming back the
    // moment the capture ends, and restarting into an update in that gap would
    // make it never come back at all.
    if (m_hiddenForCapture)
        return true;
    QQuickWindow *win = mainWindow();
    if (!win)
        return true; // can't tell — be conservative, block the restart
    return win->isVisible();
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
    qInfo() << "Idle - restarting into the updated version";
    // Idle implies the window is hidden in the tray: come back the same way.
    m_updater->restartNow(true);
    return true;
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

void AppContext::destinationTestTransport(const QString &guard, int destsBefore,
                                          std::function<void(const QString &)> done)
{
    // The transport half runs curl against a file:// target in a scratch dir,
    // so the check stays offline and costs nobody's upload quota. Testing the
    // user's real destination would put a stray file on their server on every
    // F8 run.
    if (QStandardPaths::findExecutable(QStringLiteral("curl")).isEmpty()) {
        done(QStringLiteral("guard %1, transport SKIP (curl missing)").arg(guard));
        return;
    }
    auto dir = std::make_shared<QTemporaryDir>();
    if (!dir->isValid()) {
        done(QStringLiteral("guard %1, transport FAIL (no scratch dir)").arg(guard));
        return;
    }
    const QString landed = dir->filePath(QStringLiteral("unisic-test.png"));
    QVariantMap dest;
    dest[QStringLiteral("name")] = QStringLiteral("unisic-dev-test-destination");
    dest[QStringLiteral("type")] = QStringLiteral("curl");
    dest[QStringLiteral("requestUrl")] = QUrl::fromLocalFile(dir->path()).toString();
    dest[QStringLiteral("publicUrlBase")] = QStringLiteral("https://example.invalid/unisic-dev");

    auto answered = std::make_shared<bool>(false);
    QPointer<AppContext> self(this);
    // Answered on the check's own channel again (see destinationTestCheck), and
    // the scratch dir rides along in the callback so it outlives a curl that is
    // still writing into it when the timeout below gives up.
    m_uploads->testDestination(dest, [self, answered, dir, landed, guard, destsBefore, done]
                               (bool ok, const QString &url, const QString &err) {
        if (!self || *answered)
            return;
        *answered = true;
        const bool fileOk = QFileInfo(landed).size() > 0;
        const bool urlOk = url.endsWith(QLatin1String("unisic-test.png"));
        const bool cleanOk = self->m_uploads->destinationsJson().size() == destsBefore
                             && self->m_uploads->destination(QStringLiteral("unisic-dev-test-destination")).isEmpty();
        done(QStringLiteral("guard %1, transport %2, no side effects %3")
                 .arg(guard,
                      ok && fileOk && urlOk
                          ? QStringLiteral("PASS (%1 bytes uploaded, link built)")
                                .arg(QFileInfo(landed).size())
                          : QStringLiteral("FAIL (%1)")
                                .arg(err.isEmpty() ? QStringLiteral("no file at the target")
                                                   : err.left(80)),
                      cleanOk ? QStringLiteral("PASS")
                              : QStringLiteral("FAIL (the test saved the destination)")));
    });
    // A wedged curl must not stall the whole smoke run behind it.
    QTimer::singleShot(15000, this, [answered, guard, done] {
        if (*answered)
            return;
        *answered = true;
        done(QStringLiteral("guard %1, transport FAIL (timed out)").arg(guard));
    });
}

AppContext::CheckWindowCollector::CheckWindowCollector(AppContext *c)
    : ctx(c)
{
    ctx->m_collectCheckWindows = true;
    ctx->m_checkWindows.clear();
}

AppContext::CheckWindowCollector::~CheckWindowCollector()
{
    ctx->m_collectCheckWindows = false;
    for (const QPointer<QQuickWindow> &w : std::as_const(ctx->m_checkWindows)) {
        // During a smoke run the same windows are ALSO in m_smokeWindows; the
        // QPointers there simply go null, and the final cleanup step counts
        // whatever is left.
        if (w)
            w->close();
    }
    ctx->m_checkWindows.clear();
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

void AppContext::hotkeyBindStatusAsync(
    bool heal,
    std::function<void(int, const QStringList &, const QStringList &)> done)
{
    struct State {
        QVector<HotkeyAction> actions;
        int index = 0;
        int bad = 0;
        bool heal = false;
        QStringList lines;
        QStringList conflicts;
        std::function<void(int, const QStringList &, const QStringList &)> done;
        std::function<void()> advance;
    };

    auto state = std::make_shared<State>();
    state->actions = hotkeyActions();
    state->heal = heal;
    state->done = std::move(done);
    const std::weak_ptr<State> weak = state;

    state->advance = [this, weak] {
        const auto state = weak.lock();
        if (!state)
            return;
        if (state->index >= state->actions.size()) {
            auto done = std::move(state->done);
            state->advance = {}; // release the recursive closure before callback
            if (done)
                done(state->bad, state->lines, state->conflicts);
            return;
        }

        const HotkeyAction action = state->actions.at(state->index++);
        m_hotkeys->activeKeysAsync(
            action.id, this,
            [this, state, action](bool ok, const QList<int> &raw) {
                const QString actual = GlobalHotkeys::portableFromKeys(raw);

                const auto process = [this, state, action, ok, raw, actual] {
                    if (!ok) {
                        state->lines << action.id + QStringLiteral(": query failed");
                        ++state->bad;
                        state->advance();
                        return;
                    }
                    if (actual.isEmpty() && !action.keys.isEmpty()) {
                        ++state->bad;
                        if (!state->heal) {
                            state->lines << action.id + QStringLiteral(": UNBOUND (stored ")
                                             + action.keys + QLatin1Char(')');
                            state->advance();
                            return;
                        }
                        m_hotkeys->setShortcutAsync(
                            action.id, action.name, action.keys, this,
                            [state, action](bool accepted) {
                                state->lines
                                    << (accepted
                                            ? action.id + QStringLiteral(": was unbound, re-asserted ")
                                                  + action.keys
                                            : action.id + QStringLiteral(": UNBOUND (stored ")
                                                  + action.keys + QLatin1Char(')'));
                                state->advance();
                            });
                        return;
                    }
                    if (state->heal && GlobalHotkeys::sameBinding(actual, action.keys)
                        && GlobalHotkeys::expandShiftDigitVariants(raw) != raw) {
                        m_hotkeys->setShortcutAsync(
                            action.id, action.name, action.keys, this,
                            [state, action, actual](bool) {
                                state->lines << action.id + QStringLiteral(": ") + actual
                                     + QStringLiteral(" (upgraded with Shift+digit variants)");
                                state->advance();
                            });
                        return;
                    }

                    const bool same = GlobalHotkeys::sameBinding(actual, action.keys);
                    if (!same)
                        syncHotkeyFromDaemon(action.id, actual);
                    const QString line = action.id + QStringLiteral(": ")
                        + (actual.isEmpty() ? QStringLiteral("(none)") : actual);
                    if (state->heal && same) {
                        // Refresh the compositor grab exactly as the synchronous
                        // path did, but continue only when the reply has landed.
                        m_hotkeys->setShortcutAsync(
                            action.id, action.name, action.keys, this,
                            [state, line](bool) {
                                state->lines << line;
                                state->advance();
                            });
                        return;
                    }
                    state->lines << line;
                    state->advance();
                };

                if (!ok || raw.isEmpty()) {
                    process();
                    return;
                }
                auto remaining = std::make_shared<int>(raw.size());
                for (int key : raw) {
                    m_hotkeys->keyOwnerAsync(
                        key, this,
                        [state, action, key, remaining, process](const QString &owner) {
                            if (!owner.isEmpty()
                                && !owner.startsWith(GlobalHotkeys::componentPrefix())) {
                                const QString conflict = QKeySequence(key).toString()
                                    + QStringLiteral(" (") + action.name
                                    + QStringLiteral(") → ") + owner;
                                state->conflicts << conflict;
                                state->lines << action.id + QStringLiteral(": CONFLICT ")
                                                 + conflict;
                            }
                            if (--*remaining == 0)
                                process();
                        });
                }
            });
    };
    state->advance();
}

// F8 overwrites the clipboard several times over on purpose: "copy last
// capture" seeds it with a known image, the Klipper history-hint step copies
// another, and the canvas paste check writes text and an image straight through
// QClipboard. Losing whatever the user had copied is a real cost for a check
// they press casually, so the run takes the selection away and gives it back.
//
// A clipboard offer cannot simply be held onto: the QMimeData QClipboard hands
// out belongs to Qt, it dies with the next selection change, and its formats
// are served on demand BY THE SOURCE APPLICATION. Putting it back later
// therefore means pulling every format's bytes NOW - one real transfer each -
// so the snapshot is bounded. Blowing the budget drops the snapshot entirely
// rather than restoring half a clipboard; the log says which happened.
//
// What NO restore can undo is the clipboard HISTORY: on Plasma every copy the
// run makes lands in Klipper (that is what the x-kde-force-image-copy hint is
// for), and nothing can take those entries back out. The smoke log states it.
void AppContext::snapshotClipboardForSmoke()
{
    constexpr qint64 kBudget = 16 * 1024 * 1024;
    // Generous on purpose: an office suite offers a dozen-plus (mostly tiny)
    // flavours of one selection, and that is exactly the clipboard worth
    // protecting. The byte budget, not the count, is what bounds the cost.
    constexpr int kMaxFormats = 24;
    m_smokeClipboard.reset();
    m_smokeClipboardNote.clear();

    auto copy = std::make_unique<QMimeData>();
    const QMimeData *src = QGuiApplication::clipboard()->mimeData(QClipboard::Clipboard);
    if (!src) {
        m_smokeClipboard = std::move(copy); // nothing on it -> restore == clear
        return;
    }
    const QStringList formats = src->formats();
    const bool havePng = formats.contains(QLatin1String("image/png"));
    bool tookImage = false;
    qint64 total = 0;
    int taken = 0;
    for (const QString &f : formats) {
        // Qt's own synthesized types ("application/x-qt-image", the mime-type
        // name marker): they are rebuilt from the real formats on the way back
        // out, and copying them verbatim would put Qt internals on the wire.
        if (f.startsWith(QLatin1String("application/x-qt")))
            continue;
        if (f.startsWith(QLatin1String("image/"))) {
            // One encoding is enough - every consumer converts - and a picture
            // is usually offered in six of them, i.e. six full transfers.
            if (tookImage || (havePng && f != QLatin1String("image/png")))
                continue;
        }
        if (taken >= kMaxFormats) {
            m_smokeClipboardNote = QStringLiteral("it offers %1 formats").arg(formats.size());
            return;
        }
        const QByteArray bytes = src->data(f);
        if (bytes.isEmpty())
            continue;
        total += bytes.size();
        if (total > kBudget) {
            m_smokeClipboardNote = QStringLiteral("more than %1 MB on it").arg(kBudget >> 20);
            return;
        }
        copy->setData(f, bytes);
        ++taken;
        if (f.startsWith(QLatin1String("image/")))
            tookImage = true;
    }
    m_smokeClipboard = std::move(copy);
}

QString AppContext::restoreClipboardAfterSmoke()
{
    // A deferred wl-copy mirror scheduled by the run's copyImageToClipboard
    // must not land after this and take the selection back.
    ++m_clipboardSeq;
    if (!m_smokeClipboard)
        return QStringLiteral("not put back (%1) - it now holds the test image, and the run's "
                              "copies stay in the clipboard history")
            .arg(m_smokeClipboardNote.isEmpty() ? QStringLiteral("could not be read")
                                                : m_smokeClipboardNote);
    QMimeData *data = m_smokeClipboard.release();
    const int formats = data->formats().size();
    if (formats == 0) {
        delete data;
        QGuiApplication::clipboard()->clear(QClipboard::Clipboard);
        return QStringLiteral("emptied again - nothing was on it before the run "
                              "(the run's copies stay in the clipboard history)");
    }
#ifdef HAVE_KGUIADDONS
    if (auto *bus = QDBusConnection::sessionBus().interface();
        bus && bus->isServiceRegistered(QStringLiteral("org.kde.KWin"))) {
        // The same path copyImageToClipboard uses on Plasma: data-control sets
        // the selection without a focused window, and Klipper's history then
        // ends on the user's own entry instead of on the smoke test's image.
        KSystemClipboard::instance()->setMimeData(data, QClipboard::Clipboard);
        return QStringLiteral("%1 format(s) put back - the run's copies stay in Klipper's history")
            .arg(formats);
    }
#endif
    QGuiApplication::clipboard()->setMimeData(data); // ownership passes to Qt
    return QStringLiteral("%1 format(s) put back - the run's copies stay in the clipboard history")
        .arg(formats);
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
                     .arg(fail > 0 ? QStringLiteral(" - FAILURES PRESENT") : QString()));
        m_smokeSteps.clear();
        emit smokeTestChanged();
        return;
    }
    m_smokeSteps[m_smokeIdx++]();
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
    // X11 session: the helper is a plain X11 program, so it works on EVERY X11
    // WM - muffin/Cinnamon, metacity, xfwm, KWin's X11 backend. There is no
    // layer-shell on X11, and an in-process fullscreen transparent toplevel is
    // unredirected (renders black) exactly like on mutter, so the
    // override-redirect helper is the only way to get the stylized card here.
    // Checked BEFORE the KWin test on purpose: KWin-on-X11 has no layer-shell
    // either, so it needs the helper just as much as the others.
    if (QGuiApplication::platformName() == QLatin1String("xcb"))
        return qEnvironmentVariableIsSet("DISPLAY");
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

// Why the styled card is unavailable, for the diagnostics report and the smoke
// log. "no" on its own sends people looking at their desktop, when on KDE the
// answer is almost always the build: layer-shell is the ONLY route there (the
// XWayland helper is deliberately refused while KWin is running), so a binary
// compiled without layer-shell-qt can never show the card, however capable the
// compositor is.
QString AppContext::customNotificationReason() const
{
    if (capCustomNotification())
        return QString();
#ifndef HAVE_LAYERSHELL
    return QStringLiteral("built without layer-shell-qt");
#else
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return QStringLiteral("no layer-shell on X11 and no X display for the helper");
    if (qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("COSMIC"),
                                                             Qt::CaseInsensitive))
        return QStringLiteral("layer-shell disabled on COSMIC (compositor bug)");
    return QStringLiteral("compositor does not offer wlr-layer-shell");
#endif
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
                                          QStringLiteral("es"), QStringLiteral("it"),
                                          QStringLiteral("fr"), QStringLiteral("ru"),
                                          QStringLiteral("de")};
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

    // stdout is the action protocol on both helpers, so the log takes stderr
    // ONLY - including a helper's crash block, which its own signal handler
    // writes there. One merged file keeps the interleaving readable.
    connect(proc, &QProcess::readyReadStandardError, this, [proc] {
        DiagLog::appendChildOutput(QStringLiteral("notif-helper"),
                                   proc->readAllStandardError());
    });

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
        return true; // X11 session: the override-redirect helper always works
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

void AppContext::showRecordBorder(QRect physRegion, QScreen *screen, int countdown,
                                  bool countdownOnly, const QSize &countdownRef)
{
    hideRecordBorder(); // retire any stale frame first (also clears the flag)
    if (!m_engine || !screen || physRegion.isEmpty() || !capRecordBorder())
        return; // capRecordBorder(): layer-shell, KWin trick, X11 or XWayland helper
    m_recordBorderCountdownOnly = countdownOnly;

    // GNOME (Wayland, no layer-shell, no KWin): an in-process toplevel would
    // sink below the next window the user raises — mutter has no keep-above
    // for xdg_toplevel. Spawn the XWayland helper instead: mutter stacks
    // override-redirect X11 windows above every application window, and the
    // empty input shape keeps the frame click-through. The region travels as
    // monitor FRACTIONS because XWayland's coordinate space (logical vs
    // physical layout mode) need not match either of ours.
    // UNISIC_RECORD_BORDER=helper forces this path on any compositor (testing).
    const bool wayland = QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
    // Native X11 session: the SAME helper, and for the same reason. An in-process
    // fullscreen transparent toplevel is unredirected by every mutter-family WM
    // (muffin/Cinnamon, metacity, and KWin's X11 backend honours the same
    // bypass-compositor path), so its transparency is never composited and the
    // frame shows up black - or not at all. An override-redirect window is
    // invisible to the WM, so it is never unredirected, never focused and always
    // stacks on top. The helper is a plain X11 program; on an X11 session it runs
    // natively rather than through XWayland.
    // UNISIC_RECORD_BORDER=inprocess forces the QML path back on for testing.
    const bool x11 = QGuiApplication::platformName() == QLatin1String("xcb")
                     && qEnvironmentVariable("UNISIC_RECORD_BORDER")
                            != QLatin1String("inprocess");
    auto *bi = QDBusConnection::sessionBus().interface();
    const bool kwin = bi && bi->isServiceRegistered(QStringLiteral("org.kde.KWin"));
    const bool forceHelper =
        qEnvironmentVariable("UNISIC_RECORD_BORDER") == QLatin1String("helper");
    if (forceHelper || x11
        || (wayland && !m_layerShellAvailable && !kwin
            && qEnvironmentVariableIsSet("DISPLAY"))) {
        const qreal hdpr = screen->devicePixelRatio() > 0 ? screen->devicePixelRatio() : 1.0;
        const QRect lg = screen->geometry();
        const QSizeF phys(lg.width() * hdpr, lg.height() * hdpr);
        // Overlay colors follow the active theme (incl. custom themes); the
        // fallbacks are the stock tokens. Alpha matters (pill/disc) → HexArgb.
        QColor accent(QStringLiteral("#C8ACD6"));
        QColor badgeBg(0, 0, 0, 200), badgeText(Qt::white), dot(QStringLiteral("#FF4D4D"));
        QColor cdBg(0, 0, 0, 140), cdNumber, frameContrast(0, 0, 0, 140);
        if (QObject *theme = m_engine->singletonInstance<QObject *>(
                QStringLiteral("Unisic"), QStringLiteral("Theme"))) {
            accent = theme->property("accent").value<QColor>();
            badgeBg = theme->property("recBadgeBg").value<QColor>();
            badgeText = theme->property("recBadgeText").value<QColor>();
            dot = theme->property("recDot").value<QColor>();
            cdBg = theme->property("countdownBg").value<QColor>();
            cdNumber = theme->property("countdownNumber").value<QColor>();
            frameContrast = theme->property("recordFrameContrast").value<QColor>();
        }
        if (!cdNumber.isValid())
            cdNumber = accent;
        const auto frac = [](double v) { return QString::number(v, 'f', 8); };
        auto *proc = new QProcess(this);
        proc->setProgram(QCoreApplication::applicationFilePath());
        // Same rule as the notification helper: stdout is the protocol, so
        // only stderr (warnings and its crash block) goes into the log.
        connect(proc, &QProcess::readyReadStandardError, this, [proc] {
            DiagLog::appendChildOutput(QStringLiteral("border-helper"),
                                       proc->readAllStandardError());
        });
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
                            QString::number(countdown),
                            countdownOnly ? QStringLiteral("1") : QStringLiteral("0"),
                            badgeBg.name(QColor::HexArgb),
                            badgeText.name(QColor::HexArgb),
                            dot.name(QColor::HexArgb),
                            cdBg.name(QColor::HexArgb),
                            cdNumber.name(QColor::HexArgb),
                            frameContrast.name(QColor::HexArgb)});
        // The helper's badge carries clickable stop/pause controls; a click there
        // arrives as a "stop"/"pause" line on the helper's stdout. Dispatch it
        // like the in-process border's buttons (the resulting paused state is
        // pushed back over stdin in togglePauseRecording()).
        connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc] {
            const QByteArray out = proc->readAllStandardOutput();
            const QList<QByteArray> lines = out.split('\n');
            for (const QByteArray &line : lines) {
                const QByteArray cmd = line.trimmed();
                if (cmd == "pause")
                    togglePauseRecording();
                else if (cmd == "stop")
                    stopRecording();
            }
        });
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
    // Masks input to the badge so its stop/pause controls are clickable while the
    // rest of the frame stays click-through. Must exist before create() so QML
    // resolves the context property; its window is bound right after.
    auto *borderCtl = new RecordBorderController(this);
    ctx->setContextProperty(QStringLiteral("recordBorderCtl"), borderCtl);

    QObject *obj = component.create(ctx);
    auto *win = qobject_cast<QQuickWindow *>(obj);
    if (!win) {
        delete obj;
        delete ctx;
        delete borderCtl;
        return;
    }
    ctx->setParent(win);
    borderCtl->setParent(win);
    borderCtl->setWindow(win);
    win->setScreen(screen);
    // Pre-recording countdown number (0 = none) — RecordBorder.qml shows it
    // centered in the region and hides the REC badge while it ticks.
    win->setProperty("countdown", countdown);
    // countdownOnly: no frame, no badge, number centered on the whole surface.
    win->setProperty("countdownOnly", countdownOnly);
    // Recorded-target size (logical px) the countdown disc scales to; 0 = whole
    // surface (position is unknowable on Wayland, so only the SIZE tracks it).
    win->setProperty("countdownRefW", countdownRef.width());
    win->setProperty("countdownRefH", countdownRef.height());
    // A countdown-only overlay has NO clickable controls, so make the whole
    // surface input-transparent — otherwise the full-screen layer surface eats
    // every click for the 3 s it is up (the region frame instead masks input to
    // just its badge, which is why it can't use this). Set before show() so the
    // empty input region is committed with the first frame, not a beat later.
    if (countdownOnly)
        win->setFlag(Qt::WindowTransparentForInput, true);

#ifdef HAVE_LAYERSHELL
    if (m_layerShellAvailable) {
        // Fullscreen click-through OVERLAY layer surface — works beyond KWin
        // (wlroots, COSMIC). The QML window is WindowTransparentForInput, so
        // clicks pass through; anchoring all four edges fills the output.
        // setGeometry, NOT resize: layer-shell binds the surface to the wl_output
        // of QWindow::screen() at map time, and Qt re-resolves that screen from
        // the window GEOMETRY (screenForGeometry). A resize-only window still
        // sits at (0,0), which on a multi-monitor layout can overlap the OTHER
        // monitor more — setScreen() gets overridden and the frame maps on the
        // wrong output (region on DP-2 showed its REC frame on HDMI-A-1). The
        // overlay windows never hit this because they setGeometry the same way.
        win->setGeometry(screen->geometry());
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
    m_recordBorderCountdownOnly = false;
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
    if (img.isNull()) {
        // Same reason as the cancel path in clearCliCapture(): a `--output PATH`
        // run has to hear that its capture failed, not sit and wait.
        if (!m_nextCaptureOutputPath.isEmpty()) {
            m_nextCaptureOutputPath.clear();
            m_nextCaptureOutputFormat.clear();
            emit cliCaptureFinished(false);
        }
        return;
    }

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
    const QImage output = stampWatermark(img);

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
    // Register the history entry up front — even when uploading an unsaved
    // capture. The notification card then holds a real entry id, so a manual
    // Save / Show-in-folder from the card while the upload is still in flight
    // links the file to THIS entry instead of stranding it (setFilePathById(0)
    // silently missed). The thumbnail is generated now from `output`, so the
    // full image no longer stays pinned across the whole network transfer.
    quint64 historyId = cliMode ? 0
                                : m_history->addEntry(path, output, QStringLiteral("image"));

    auto *notif = cliMode ? nullptr
                          : showCaptureNotification(output, path, QStringLiteral("image"), inhibited);
    QPointer<CaptureNotification> np(notif);
    if (notif)
        notif->setHistoryId(historyId); // Save/upload address exactly this entry

    if (uploading) {
        if (np) np->setUploading(true);
        // Encode off-thread (100+ ms at 4K), start the upload in the GUI-thread
        // continuation. The full image is released once encoding finishes — the
        // history entry created above already carries its thumbnail, so nothing
        // needs the pixels for the duration of the transfer.
        // The uploaded copy may be a different format than the saved one, and
        // then it needs its own name: `fileName` is the SAVE name, extension
        // included. Same bytes-and-name rule as uploadImage.
        const QString upFmt = uploadImageFormat();
        encodeImageAsync(output, [this, path, np, historyId, fileName, upFmt,
                                  uploadDestination](const QByteArray &data, const QString &mime) {
            // Named from the mime the encoder answered with, not from upFmt:
            // the format asked for is not always the one that came back.
            const QString upName =
                upFmt.isEmpty() ? fileName
                                : FilenameTemplate::withExtension(
                                      fileName, FilenameTemplate::extensionForMime(mime));
            m_uploads->uploadDataTo(uploadDestination, data, upName, mime,
                [this, path, historyId, np](const QString &url, const QString &del, const QString &err) {
                    if (!err.isEmpty()) {
                        // The capture already lives in history (added before the
                        // upload) — a failure just leaves it there without a URL.
                        showToast(tr("Upload failed: %1").arg(err), true);
                        if (np) np->setUploading(false);
                        return;
                    }
                    // Attach the URL to the pre-created entry by id; fall back to a
                    // fresh entry only if it was evicted during a long transfer.
                    if (!m_history->setUrlById(historyId, url, del))
                        m_history->addEntry(path, {}, QStringLiteral("image"), url, del);
                    afterUploadActions(url);
                    if (np) np->setUrl(url);
                });
        }, upFmt);
    } else if (!path.isEmpty() && !cliMode) {
        showToast(tr("Saved %1").arg(path));
    }

    if (editEnabled)
        openEditor(output, {}, historyId);

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

    // `--output PATH` is one-shot: the file was written synchronously above and
    // setNextCaptureOutput() switched off clipboard, history, editor and upload,
    // so there is nothing left to wait for. The stdout variant has its own exit,
    // once the encoded bytes are on the pipe.
    if (cliMode && !cliStdout)
        emit cliCaptureFinished(!path.isEmpty());

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

// The one place the watermark settings are read. The Settings preview renders
// through here too, so a preview that shows a mark the capture would not get
// (or the reverse) is not expressible.
QImage AppContext::stampWatermark(const QImage &source) const
{
    if (!m_settings->watermarkEnabled())
        return source;
    const int opacity = m_settings->watermarkOpacity();
    const QString position = m_settings->watermarkPosition();
    const QString pattern = m_settings->watermarkPattern();
    const int scale = m_settings->watermarkScale();
    // A logo type with no file loaded falls back to the text stamp rather than
    // to nothing: "watermark on" must never silently mean "watermark off".
    if (m_settings->watermarkType() == QLatin1String("image") && !m_watermarkImage.isNull())
        return UnisicImageEffects::watermarkImage(source, m_watermarkImage, opacity,
                                                  position, pattern, scale);
    return UnisicImageEffects::watermarkText(source, m_settings->watermarkText(), opacity,
                                             position, pattern, scale);
}

QString AppContext::watermarkPreviewSource() const
{
    return QStringLiteral("image://watermark/%1").arg(m_watermarkPreviewRev);
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
    // A .gif is two different things wearing one extension, and since captures
    // can be saved as GIF the still one is no longer hypothetical: it belongs
    // in the image editor, the recorded one in the trim window. Only the frame
    // count can tell them apart. imageCount() reads the header, not the frames
    // (measured at 0 ms for both a 4K still and a 20-frame animation), and
    // returns -1 for a file that is not there - which keeps the old answer.
    if (ext == QLatin1String("gif"))
        return QImageReader(path).imageCount() == 1 ? QStringLiteral("image")
                                                    : QStringLiteral("video");
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

    openPath(path);
}

void AppContext::openPath(const QString &path)
{
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
    if (!info.exists()) {
        showToast(tr("Can't find %1").arg(info.fileName()), true);
        return;
    }
    // A folder reaches here from a drop or a paste (never from the file
    // dialog). Answering "cannot edit this file type" would be a non-answer:
    // a folder is not a type, it is the wrong THING.
    if (info.isDir()) {
        showToast(tr("Unisic opens files, not folders"), true);
        return;
    }
    // What the file IS decides, not what the dialog was filtered to (nor what
    // the drag source claimed).
    const QString actual = editableKindFor(path);
    if (actual == QLatin1String("video"))
        openTrimRecording(path);
    else if (actual == QLatin1String("image"))
        editFromHistory(path);
    else
        showToast(tr("Unisic cannot edit this file type"), true);
}

void AppContext::openDroppedUrls(const QVariantList &urls)
{
    QStringList localFiles;
    bool sawRemote = false;
    for (const QVariant &v : urls) {
        const QUrl url = v.toUrl();
        if (url.isEmpty())
            continue;
        if (url.isLocalFile())
            localFiles << url.toLocalFile();
        else
            sawRemote = true;
    }
    if (localFiles.isEmpty()) {
        // A browser image drag offers an http url. Downloading it silently
        // would be a surprise (and a network call nobody asked for), so say no.
        showToast(sawRemote ? tr("Unisic can only open files from this computer")
                            : tr("Nothing to open in that drop"),
                  true);
        return;
    }
    // The first file Unisic can open wins; the count of the rest decides what
    // the note below is allowed to say.
    QString chosen;
    int openable = 0;
    for (const QString &p : std::as_const(localFiles)) {
        if (editableKindFor(p).isEmpty())
            continue;
        ++openable;
        if (chosen.isEmpty())
            chosen = p;
    }
    if (chosen.isEmpty()) {
        // Dragging a folder out of a file manager is the common miss (it has no
        // extension, so the loop above never picks it) and deserves a straight
        // answer instead of "cannot edit this file type".
        bool folder = false;
        for (const QString &p : std::as_const(localFiles))
            if (QFileInfo(p).isDir()) { folder = true; break; }
        showToast(folder ? tr("Unisic opens files, not folders")
                         : tr("Unisic cannot edit this file type"), true);
        return;
    }
    // The note below claims the drop worked, so it must not run ahead of the
    // one thing that can still refuse it: a stale drag whose file is gone gets
    // "Can't find ..." from openPath, and two toasts contradicting each other
    // are worse than one.
    //
    // Plain, NOT important: the drop did what it was asked to and the sentence
    // only says where the rest of the batch went. `important` is reserved for
    // failures (it overrides the user's "don't show notifications" choice), and
    // a note that reads as an error next to an editor that just opened
    // correctly is worse than no note.
    if (localFiles.size() > 1 && QFileInfo::exists(chosen)) {
        // Two different situations, and telling them apart matters: "drop them
        // one at a time" is only true advice when there really is another file
        // that WOULD open. Drop a PNG next to a .txt and the batch is already
        // finished - sending the user back for the .txt would send them after
        // something that can never open.
        showToast(openable > 1
                      ? tr("Opened %1. Drop one file at a time to open the others.")
                            .arg(QFileInfo(chosen).fileName())
                      : tr("Opened %1. Nothing else in that drop is a file Unisic can open.")
                            .arg(QFileInfo(chosen).fileName()));
    }
    openPath(chosen);
}

bool AppContext::openImageData(const QByteArray &data)
{
    if (data.isEmpty())
        return false;
    QImage img;
    if (!img.loadFromData(data))
        return false;
    // The bytes came from somewhere else's scene graph: force DPR 1 so the
    // editor treats them as plain image pixels (AnnotationCanvas' rule).
    img.setDevicePixelRatio(1.0);
    openEditor(img);
    return true;
}

void AppContext::pasteFromClipboard()
{
    // Reading needs no KDE branch: the KSystemClipboard/wl-copy machinery in
    // copyImageToClipboard is about OFFERING data, not taking it.
    pasteMimeData(QGuiApplication::clipboard()->mimeData());
}

void AppContext::pasteMimeData(const QMimeData *mime)
{
    if (!mime) {
        showToast(tr("The clipboard is empty"), true);
        return;
    }
    if (mime->hasImage()) {
        QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            img.setDevicePixelRatio(1.0);
            // No overwrite path on purpose: a pasted image has no file of its
            // own, so Ctrl+S must save a NEW capture instead of writing over
            // whatever file the pixels originally came from.
            openEditor(img);
            return;
        }
    }
    // Same selection rule as a dropped payload (openDroppedUrls): the first
    // file Unisic can actually open wins, so a copied .txt lying next to the
    // PNG does not hijack the paste. If nothing in it is openable, the first
    // local file still goes to openPath, which says why it cannot be opened.
    const QList<QUrl> urls = mime->urls();
    QString firstLocal;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;
        const QString p = url.toLocalFile();
        if (firstLocal.isEmpty())
            firstLocal = p;
        if (!editableKindFor(p).isEmpty()) {
            openPath(p);
            return;
        }
    }
    if (!firstLocal.isEmpty()) {
        openPath(firstLocal);
        return;
    }
    showToast(tr("The clipboard holds no image to paste"), true);
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
                 range + QLatin1Char(',') + FfmpegUtil::gifPaletteGenFilter(quality),
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
                         .arg(range, FfmpegUtil::gifPaletteUseFilter(quality)),
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
                               bool lossless, const QVariantList &audioGains, int mixTrack)
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
    // The trim window sends one gain per audio track; all-1.0 (or none) means
    // the audio is untouched and can stay a stream copy.
    QList<double> gains;
    bool audioEdited = false;
    for (const QVariant &value : audioGains) {
        const double gain = value.toDouble();
        gains << gain;
        if (gain < 0 || !qFuzzyCompare(gain, 1.0))
            audioEdited = true;
    }
    const bool webm = suffix == QLatin1String("webm");
    const auto editArgs = [&] {
        return mixTrack >= 0 && mixTrack < gains.size()
                   ? GifRecorder::audioRemixArgs(webm, gains, mixTrack, tr("Mix"))
                   : GifRecorder::audioEditArgs(webm, gains);
    };
    QStringList args{QStringLiteral("-y"), QStringLiteral("-nostats"),
                     QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-ss"), ss,
                     QStringLiteral("-i"), path,
                     QStringLiteral("-t"), dur};
    if (lossless) {
        // The caller has already snapped `startSeconds` onto a keyframe, so the
        // copy starts exactly there. make_zero rebases the timestamps instead of
        // leaning on a container edit list (which not every player honours).
        // Explicit maps, not `-c copy` alone: ffmpeg's default stream selection
        // keeps ONE audio stream, which silently dropped the stems of a
        // separate-tracks recording. A volume/mute edit re-encodes the audio
        // (a filter cannot run on a copied stream); the video is copied either
        // way, so the cut stays instant.
        args << QStringLiteral("-avoid_negative_ts") << QStringLiteral("make_zero")
             << QStringLiteral("-map") << QStringLiteral("0:v:0")
             << QStringLiteral("-c:v") << QStringLiteral("copy");
        if (audioEdited)
            args << editArgs();
        else
            args << QStringLiteral("-map") << QStringLiteral("0:a?")
                 << QStringLiteral("-c:a") << QStringLiteral("copy");
    } else {
        // Re-encode: with -ss in front of -i ffmpeg seeks to the preceding
        // keyframe and decodes forward, so the output starts on the exact frame.
        const int crf = UnisicVideo::crfFromPercent(m_settings->videoQualityPercent());
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
                   && FfmpegUtil::hardwareEncoderAvailable(QStringLiteral("vaapi"))) {
            args << QStringLiteral("-vaapi_device") << QStringLiteral("/dev/dri/renderD128")
                 << QStringLiteral("-vf") << evenCrop + QStringLiteral(",format=nv12,hwupload")
                 << QStringLiteral("-c:v") << QStringLiteral("h264_vaapi")
                 << QStringLiteral("-qp") << QString::number(qBound(1, crf, 40))
                 << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        } else if (m_settings->videoEncoder() == QLatin1String("nvenc")
                   && FfmpegUtil::hardwareEncoderAvailable(QStringLiteral("nvenc"))) {
            args << QStringLiteral("-vf") << evenCrop
                 << QStringLiteral("-c:v") << QStringLiteral("h264_nvenc")
                 << QStringLiteral("-preset") << QStringLiteral("p4")
                 << QStringLiteral("-cq") << QString::number(crf)
                 << QStringLiteral("-b:v") << QStringLiteral("0")
                 << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
                 << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        } else if (FfmpegUtil::encoderUsable(QStringLiteral("libx264"))) {
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
        // Same audio args the recorder's own conversion uses, so a trim keeps
        // every track of a multi-track recording instead of the first one.
        // No names passed: this file is finished, and ffmpeg's metadata copy
        // carries the ones it already has.
        args << QStringLiteral("-map") << QStringLiteral("0:v:0");
        if (audioEdited)
            args << editArgs();
        else
            args << GifRecorder::finalAudioArgs(webm);
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
    // Clamped here, not in the runner: the self-tests deliberately arm a
    // one-second ceiling, and only the settings-driven path has bounds to obey.
    const int timeoutMs = qBound(ExternalActionRunner::kMinTimeoutSec,
                                 m_settings->externalActionTimeoutSec(),
                                 ExternalActionRunner::kMaxTimeoutSec) * 1000;
    const auto launch = [this, timeoutMs](const QString &input, bool temporary) {
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
        }, timeoutMs);
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
    openEditor(img, filePath, m_history->idForFile(filePath));
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
    // Unless an upload format is set and this file is not already in it - then
    // the SAME rule as a capture-time upload applies, and the bytes on the wire
    // are a re-encode of this file. Only for a still image: a recording, or an
    // animated GIF, has more frames than a re-encode could carry, and
    // editableKindFor already tells those apart by frame count. The file on
    // disk is never touched.
    const QString upFmt = uploadImageFormat();
    if (!upFmt.isEmpty() && !FilenameTemplate::sameFormat(QFileInfo(filePath).suffix(), upFmt)
        && editableKindFor(filePath) == QLatin1String("image")) {
        QImageReader reader(filePath);
        reader.setAutoTransform(true);
        const QImage img = reader.read();
        if (!img.isNull()) {
            encodeImageAsync(img, [this, filePath](const QByteArray &data, const QString &mime) {
                if (data.isEmpty()) {
                    showToast(tr("Could not encode %1 for upload")
                                  .arg(QFileInfo(filePath).fileName()), true);
                    return;
                }
                const QString name = FilenameTemplate::withExtension(
                    QFileInfo(filePath).fileName(), FilenameTemplate::extensionForMime(mime));
                m_uploads->uploadData(data, name, mime,
                    [this, filePath](const QString &url, const QString &del, const QString &err) {
                        if (!err.isEmpty()) {
                            showToast(tr("Upload failed: %1").arg(err), true);
                            return;
                        }
                        // The link belongs to the tile of the file it came
                        // from: the re-encode exists only on the server.
                        m_history->setUrl(filePath, url, del);
                        if (url.isEmpty())
                            showToast(tr("Uploaded"));
                        else
                            afterUploadActions(url);
                    });
            }, upFmt);
            return;
        }
        // Unreadable as an image (a format Qt has no reader for): fall through
        // and send the file as it is rather than refusing the upload outright.
    }
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

void AppContext::openEditor(const QImage &img, const QString &overwritePath, quint64 historyId)
{
    if (!m_engine)
        return;
    QQmlComponent component(m_engine, QUrl(QStringLiteral("qrc:/qt/qml/Unisic/qml/EditorWindow.qml")));
    if (component.isError()) {
        qWarning() << component.errorString();
        return;
    }
    auto *session = new EditorSession(this, img, overwritePath, historyId, this);
    auto *ctx = new QQmlContext(m_engine->rootContext(), session);
    ctx->setContextProperty(QStringLiteral("editorSession"), session);
    QObject *obj = component.create(ctx);
    if (auto *win = qobject_cast<QQuickWindow *>(obj)) {
        ++m_editorWindows;
        emit editorWindowsOpenChanged();
        if (m_smokeRunning)
            m_smokeWindows.append(win); // auto-closed by the smoke test's last step
        if (m_collectCheckWindows)
            m_checkWindows.append(win); // auto-closed by the dev check that opened it
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
    // Parent the watcher to qApp, not this: if AppContext disappears while the
    // encode is finishing, the GUI-thread QPointer branch still removes the
    // private temp file. The shared guard also covers qApp shutting down before
    // the completion event; no QObject pointer crosses into the worker.
    struct PreviewTempCleanup {
        QString path;
        bool handedOff = false;
        ~PreviewTempCleanup() { if (!handedOff) QFile::remove(path); }
    };
    auto cleanup = std::make_shared<PreviewTempCleanup>();
    cleanup->path = tmp;
    auto *watcher = new QFutureWatcher<QPair<bool, QSize>>(qApp);
    QPointer<AppContext> self(this); // created, read and destroyed on the GUI thread
    connect(watcher, &QFutureWatcher<QPair<bool, QSize>>::finished, qApp,
            [watcher, self, cleanup] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (self) {
            cleanup->handedOff = result.first;
            self->finishOpenPreview(result.first, cleanup->path, result.second);
        }
    });
    watcher->setFuture(QtConcurrent::run([img, cleanup] {
        const bool ok = img.save(cleanup->path);
        // The capture can contain a password/bank page/private chat; /tmp is
        // world-listable, so lock the file to the owner (a UUID name is no
        // protection once the directory is listable).
        if (ok)
            QFile::setPermissions(cleanup->path,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        return qMakePair(ok, img.size());
    }));
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
        // Bind the surface to the monitor the user is working on BEFORE the
        // layer-shell configure — without this the fullscreen preview surface
        // lands on whatever output the compositor defaults to (usually the
        // primary), not the one the capture was taken/clicked on. Same rule as
        // LayerShellNotifier: the cursor's screen is the working screen.
        if (QScreen *s = QGuiApplication::screenAt(QCursor::pos()))
            win->setScreen(s);
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
    const QString upFmt = uploadImageFormat();
    const QString baseName = path.isEmpty() ? makeFileName() : QFileInfo(path).fileName();
    // Same off-thread encode + conditional image retention as finishCapture.
    encodeImageAsync(img, [this, path, np, baseName, upFmt,
                           img = path.isEmpty() ? img : QImage()](const QByteArray &data, const QString &mime) {
        const QString fileName =
            upFmt.isEmpty() ? baseName
                            : FilenameTemplate::withExtension(
                                  baseName, FilenameTemplate::extensionForMime(mime));
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
    }, upFmt);
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

QString AppContext::saveImageAuto(const QImage &img, const QString &fileName,
                                  bool allowAutoConvert)
{
    QString dir = m_settings->saveDirectory();
    // Optional per-month subfolders (yyyy-MM) keep a busy screenshots folder
    // tidy. saveImageTo mkpath()s the directory, so no separate mkdir here.
    if (m_settings->dateSubfolders())
        dir += QLatin1Char('/')
             + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM"));
    return saveImageTo(img, dir, fileName, allowAutoConvert);
}

QString AppContext::makeFileName(const QString &formatOverride) const
{
    return FilenameTemplate::expand(m_settings->filenameTemplate(),
                                    m_settings->filenameCounter(),
                                    QDateTime::currentDateTime())
           + QLatin1Char('.')
           + FilenameTemplate::extensionFor(formatOverride.isEmpty()
                                                ? m_settings->imageFormat()
                                                : formatOverride);
}

QString AppContext::convertFileTo(const QString &path, const QString &format)
{
    const QString target = FilenameTemplate::extensionFor(format);
    const QFileInfo fi(path);
    if (!fi.exists()) {
        showToast(tr("Can't find %1").arg(fi.fileName()), true);
        return {};
    }
    if (FilenameTemplate::sameFormat(fi.suffix(), target)) {
        showToast(tr("Already a %1 file").arg(target.toUpper()));
        return {};
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    // An animated source has more frames than any of these formats can hold,
    // and read() would quietly hand back frame one. Say no instead of throwing
    // the rest of the animation away on the user's behalf.
    if (reader.imageCount() > 1) {
        showToast(tr("%1 is animated. Trim it instead").arg(fi.fileName()), true);
        return {};
    }
    const QImage img = reader.read();
    if (img.isNull()) {
        showToast(tr("Can't read %1").arg(fi.fileName()), true);
        return {};
    }
    // Beside the original and under its own name, not the capture template's:
    // a converted file that answers to a different date than the shot it came
    // from is a file nobody can pair back up. saveImageTo picks the encoder off
    // the extension, de-duplicates the name, and turns a GIF with no ffmpeg
    // into a PNG with a toast - all of which is wanted here unchanged. The
    // over-size rule is NOT: this format is the one the user just clicked.
    const QString out = saveImageTo(img, fi.absolutePath(),
                                    fi.completeBaseName() + QLatin1Char('.') + target,
                                    /*allowAutoConvert=*/false);
    if (out.isEmpty()) {
        showToast(tr("Couldn't convert %1").arg(fi.fileName()), true);
        return {};
    }
    // A genuinely new file on disk earns its own tile - unlike an upload, which
    // is the same file gaining a link.
    m_history->addEntry(out, img, QStringLiteral("image"));
    showToast(tr("Saved %1").arg(QFileInfo(out).fileName()));
    return out;
}

QString AppContext::filenamePreview() const
{
    return makeFileName();
}

QVariantMap AppContext::filenameHelp() const
{
    return FilenameTemplate::help();
}

void AppContext::encodeImageAsync(const QImage &img,
                                  std::function<void(const QByteArray &, const QString &)> done,
                                  const QString &formatOverride)
{
    // Snapshot the settings on the GUI thread; the worker must not touch them.
    const QString fmt = formatOverride.isEmpty() ? m_settings->imageFormat().toLower()
                                                  : formatOverride.toLower();
    const int q = qBound(1, m_settings->imageQuality(), 100);
    auto *watcher = new QFutureWatcher<ImageEncode::Result>(this);
    connect(watcher, &QFutureWatcher<ImageEncode::Result>::finished, this,
            [watcher, done = std::move(done)] {
        const ImageEncode::Result encoded = watcher->result();
        watcher->deleteLater();
        done(encoded.bytes, encoded.mime);
    });
    watcher->setFuture(QtConcurrent::run([img, fmt, q] {
        // Same encoder the save path uses, so the uploaded bytes and the file on
        // disk can never be two different pictures. Its GIF branch blocks on
        // ffmpeg, which is exactly why this runs on a pool thread.
        return ImageEncode::encode(img, fmt, q);
    }));
}

QString AppContext::uploadImageFormat() const
{
    const QString fmt = m_settings->uploadFormat().trimmed().toLower();
    // "" and "same" both mean "whatever the save format is" - the combo stores
    // the empty string, the second spelling only guards a hand-edited config.
    if (fmt.isEmpty() || fmt == QLatin1String("same"))
        return {};
    return FilenameTemplate::extensionFor(fmt);
}

QString AppContext::autoConvertIfLarge(const QString &path, const QImage &img)
{
    if (!m_settings->autoConvertLarge() || path.isEmpty())
        return path;
    const QString target = FilenameTemplate::extensionFor(m_settings->autoConvertFormat());
    if (FilenameTemplate::sameFormat(QFileInfo(path).suffix(), target))
        return path;
    // MB as the user means it on a file listing: 1 MB = 1024 KB.
    const qint64 limit = qint64(qMax(1, m_settings->autoConvertOverMb())) * 1024 * 1024;
    const qint64 size = QFileInfo(path).size();
    if (size <= limit)
        return path;

    const ImageEncode::Result enc = ImageEncode::encode(img, target, m_settings->imageQuality());
    // Two ways to end up not converting, and both keep the file that already
    // exists: nothing came back (no ffmpeg for a GIF target, a refused encoder),
    // or the "smaller" format came out bigger, which a lossless-to-PNG target or
    // a screenshot of flat UI colours can absolutely do. A rule whose whole
    // purpose is a smaller file must never be allowed to produce a larger one.
    if (!enc.ok() || enc.format != target || enc.bytes.size() >= size)
        return path;

    const QFileInfo fi(path);
    QString out = fi.absolutePath() + QLatin1Char('/')
                  + FilenameTemplate::withExtension(fi.fileName(), target);
    for (int n = 1; QFile::exists(out); ++n)
        out = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName()
              + QStringLiteral("-%1.").arg(n) + target;
    QFile f(out);
    bool ok = f.open(QIODevice::WriteOnly) && f.write(enc.bytes) == enc.bytes.size();
    ok = f.flush() && ok; // a short write on a full disk must not read as saved
    f.close();
    if (!ok) {
        QFile::remove(out);
        return path; // the original is still there and still correct
    }
    // Only now is the original expendable. Removing it first would risk having
    // neither file if the write above failed.
    QFile::remove(path);
    // formattedDataSize, not a raw byte count: this sentence is the only proof
    // the user gets that the rule earned its keep, and "8,1 MiB instead of
    // 26,4 MiB" says that where "8493021" does not.
    showToast(tr("Over %1 MB, so it was converted to %2 (%3 instead of %4)")
                  .arg(m_settings->autoConvertOverMb())
                  .arg(target.toUpper(),
                       QLocale().formattedDataSize(enc.bytes.size(), 1),
                       QLocale().formattedDataSize(size, 1)));
    return out;
}

bool AppContext::overwriteImageFile(const QImage &img, const QString &path)
{
    if (path.isEmpty() || img.isNull())
        return false;
    const QString ext = QFileInfo(path).suffix();
    const ImageEncode::Result enc = ImageEncode::encode(img, ext, m_settings->imageQuality());
    // The file keeps its name, so a format the encoder had to swap out cannot
    // be written here at all - the bytes would not match the extension. Say so
    // instead: "Save as" is the way to change format, and it makes a new file.
    if (!enc.ok() || enc.format != FilenameTemplate::extensionFor(ext)) {
        showToast(enc.fallbackReason == QLatin1String("gif")
                      ? tr("GIF needs ffmpeg. Use Save as to write another format")
                      : tr("Can't write %1 back as %2. Use Save as")
                            .arg(QFileInfo(path).fileName(), ext.toUpper()),
                  true);
        return false;
    }
    // Written whole and only then swapped in: an interrupted write must not
    // leave the user's original file half-overwritten. QSaveFile is exactly
    // that, and it keeps the original's permissions.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        showToast(tr("Can't write %1").arg(QFileInfo(path).fileName()), true);
        return false;
    }
    f.write(enc.bytes);
    if (!f.commit()) {
        showToast(tr("Can't write %1").arg(QFileInfo(path).fileName()), true);
        return false;
    }
    return true;
}

QString AppContext::saveImageTo(const QImage &img, const QString &dir, const QString &fileName,
                                bool allowAutoConvert)
{
    if (dir.isEmpty() || img.isNull())
        return {};
    QDir().mkpath(dir);
    QString name = fileName.isEmpty() ? makeFileName() : fileName;

    QString fmt = QFileInfo(name).suffix().toLower();
    if (fmt != QLatin1String("png") && fmt != QLatin1String("jpg")
        && fmt != QLatin1String("jpeg") && fmt != QLatin1String("webp")
        && fmt != QLatin1String("gif"))
        fmt = m_settings->imageFormat().toLower();

    // Metadata strip: rebuild from raw pixels so the written file carries no
    // text chunks, description or DPI. Captures normally have NONE (built from
    // raw screen pixels), so skip the full-frame copy unless there is actually
    // something to strip — the editor or a loaded source can add text/DPI. Only
    // ≥24bpp (the capture formats); a rebuild would drop an indexed palette.
    // Before the encode, so what is stripped is what gets encoded.
    QImage stripped;
    if (m_settings->stripMetadata() && img.depth() >= 24
        && (!img.textKeys().isEmpty() || img.dotsPerMeterX() != 0 || img.dotsPerMeterY() != 0))
        stripped = QImage(img.constBits(), img.width(), img.height(),
                          img.bytesPerLine(), img.format()).copy();
    const QImage &toSave = stripped.isNull() ? img : stripped;

    // Encode FIRST, name second, and that order is the whole correctness rule
    // here. Two of the four formats can refuse: JPEG cannot hold the alpha a
    // transparent capture has, and GIF needs an ffmpeg that may not be
    // installed. Both fall back to PNG, so the extension is only known after
    // the encode - and it has to be known before the collision-dedup loop, or
    // two transparent saves in the same second (or a pre-existing same-named
    // .png) both pass the .jpg existence check and silently overwrite one
    // another. Cost: the bytes are a second copy of the picture (~5-10 MB for a
    // 4K PNG next to its 33 MB of pixels), freed as soon as they are written.
    const ImageEncode::Result enc =
        ImageEncode::encode(toSave, fmt, m_settings->imageQuality());
    if (!enc.ok())
        return {};
    if (enc.fallbackReason == QLatin1String("alpha"))
        showToast(tr("Saved as PNG to keep transparency"));
    else if (enc.fallbackReason == QLatin1String("gif"))
        showToast(tr("GIF needs ffmpeg. Saved as PNG"));
    else if (enc.fallbackReason == QLatin1String("encoder"))
        showToast(tr("%1 could not hold this image. Saved as PNG").arg(fmt.toUpper()));
    name = FilenameTemplate::withExtension(name, enc.format);

    QString path = dir + QLatin1Char('/') + name;
    const QFileInfo fi(name);
    for (int n = 1; QFile::exists(path); ++n)
        path = dir + QLatin1Char('/') + fi.completeBaseName()
               + QStringLiteral("-%1.").arg(n) + fi.suffix();

    QFile out(path);
    bool ok = out.open(QIODevice::WriteOnly) && out.write(enc.bytes) == enc.bytes.size();
    ok = out.flush() && ok; // a short write on a full disk must not read as saved
    out.close();
    if (!ok) {
        QFile::remove(path);
        return {};
    }
    // After the write, so the rule sees the real file size rather than an
    // estimate - and before openAfterSave, so what opens is the file that
    // survived.
    if (allowAutoConvert)
        path = autoConvertIfLarge(path, toSave);
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
#ifdef HAVE_KGUIADDONS
    // On Plasma re-assert the image through KSystemClipboard WITH the history
    // hint (above). data-control also sets the selection without a focused
    // window, so this alone is reliable — no wl-copy mirror needed here.
    if (auto *bus = QDBusConnection::sessionBus().interface();
        bus && bus->isServiceRegistered(QStringLiteral("org.kde.KWin"))) {
        KSystemClipboard::instance()->setMimeData(makeForceImageMime(img),
                                                  QClipboard::Clipboard);
        ++m_clipboardSeq; // a stale deferred wl-copy mirror must not clobber this
        return;
    }
#endif
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
    auto *watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this,
            [this, watcher, wlCopy, seq] {
        const QByteArray png = watcher->result();
        watcher->deleteLater();
        // QProcess stays on the GUI thread, and only the newest copy request
        // may take the Wayland selection.
        if (m_clipboardSeq == seq)
            spawnWlCopy(this, wlCopy,
                        {QStringLiteral("--type"), QStringLiteral("image/png")}, png);
    });
    watcher->setFuture(QtConcurrent::run([img] {
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        return png;
    }));
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
// `historyId` is the entry this capture already owns (the editor knows it): the
// URL lands on THAT entry, so an uploaded capture stays one tile instead of
// splitting into "local file" + "uploaded link". 0 (a pasted/dropped image, the
// dev check) still makes a fresh entry, as does an entry evicted mid-transfer.
void AppContext::uploadImage(const QImage &img, UploadDone done, quint64 historyId)
{
    // The upload format is allowed to differ from the save format, so the name
    // is built from IT - a .png name on JPEG bytes is what an "unsupported
    // file type" from the server looks like an hour later.
    const QString upFmt = uploadImageFormat();
    const QString baseName = makeFileName(upFmt);
    encodeImageAsync(img, [this, img, baseName, done, historyId](const QByteArray &data, const QString &mime) {
        // The extension follows the bytes: the encoder can answer with a
        // different format than it was asked for, and the server is told the
        // truth either way.
        const QString fileName =
            FilenameTemplate::withExtension(baseName, FilenameTemplate::extensionForMime(mime));
        m_uploads->uploadData(data, fileName, mime,
            [this, img, done, historyId](const QString &url, const QString &del, const QString &err) {
                if (err.isEmpty()) {
                    if (!m_history->setUrlById(historyId, url, del))
                        m_history->addEntry({}, img, QStringLiteral("image"), url, del);
                    afterUploadActions(url);
                }
                if (done)
                    done(url, err);
            });
    }, upFmt);
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

void AppContext::showInFileManager(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        openDirectory(path.isEmpty() ? QString() : QFileInfo(path).absolutePath());
        return;
    }
    // ShowItems opens the folder with the file selected (Dolphin, Nautilus,
    // Nemo, Thunar all serve it). Async: the service is D-Bus-activatable, so
    // a cold start could stall a blocking call on the GUI thread.
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.FileManager1"),
        QStringLiteral("/org/freedesktop/FileManager1"),
        QStringLiteral("org.freedesktop.FileManager1"),
        QStringLiteral("ShowItems"));
    msg << QStringList{QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)}
        << QString();
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    const QString dir = QFileInfo(path).absolutePath();
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, dir](QDBusPendingCallWatcher *w) {
        if (w->isError())
            openDirectory(dir);
        w->deleteLater();
    });
}

void AppContext::showWelcome()
{
    emit showWelcomeRequested();
}

QSize AppContext::notifCardSize(const QString &style) const
{
    return NotifCard::sizeForStyle(style);
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

void AppContext::exportEntriesToZipDialog(const QVariantList &ids)
{
    // Resolve the selection to on-disk files first, so a save dialog only opens
    // when there is actually something to archive (mirrors the Copy-paths guard).
    QStringList files;
    for (const QVariant &v : ids) {
        const QVariantMap e = m_history->entryById(v.toULongLong());
        const QString fp = e.value(QStringLiteral("filePath")).toString();
        if (!fp.isEmpty() && QFileInfo::exists(fp))
            files << fp;
    }
    if (files.isEmpty()) {
        showToast(tr("None of the selected captures are saved on disk."), true);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        nullptr, tr("Export captures to ZIP"),
        QDir::homePath() + QStringLiteral("/unisic-captures.zip"),
        tr("ZIP archive (*.zip)"));
    if (path.isEmpty())
        return; // cancelled
    showToast(tr("Exporting %1 captures…").arg(files.size()));
    exportFilesToZip(files, path); // default reporting toasts the outcome
}

void AppContext::exportFilesToZip(const QStringList &files, const QString &destPath,
                                  std::function<void(bool, const QString &)> done)
{
    auto report = [this, done](bool ok, const QString &msg) {
        if (done)
            done(ok, msg);
        else
            showToast(msg, !ok);
    };

    // No zip library is linked (see the header note): shell out to Info-ZIP.
    const QString zipBin = QStandardPaths::findExecutable(QStringLiteral("zip"));
    if (zipBin.isEmpty()) {
        report(false, tr("The “zip” program is not installed - install it and try again."));
        return;
    }

    QStringList real;
    for (const QString &f : files)
        if (!f.isEmpty() && QFileInfo::exists(f))
            real << f;
    if (real.isEmpty()) {
        report(false, tr("None of the selected captures are saved on disk."));
        return;
    }

    QString dest = destPath;
    if (!dest.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
        dest += QLatin1String(".zip");
    QFile::remove(dest); // zip *appends* to an existing archive; we want a fresh one

    // Stage uniquely-named symlinks in a temp dir. Duplicate basenames (e.g. two
    // imported files called screenshot.png) would otherwise overwrite each other
    // inside the archive; default zip (no -y) follows each link and stores the
    // real content under the staged name.
    auto *staging = new QTemporaryDir;
    if (!staging->isValid()) {
        const QString e = staging->errorString();
        delete staging;
        report(false, tr("Could not create a temporary folder: %1").arg(e));
        return;
    }
    QStringList members;
    QSet<QString> used;
    for (const QString &f : real) {
        const QFileInfo fi(f);
        const QString base = fi.completeBaseName();
        const QString suf = fi.suffix();
        QString name = fi.fileName();
        for (int n = 2; used.contains(name); ++n)
            name = suf.isEmpty() ? QStringLiteral("%1-%2").arg(base).arg(n)
                                 : QStringLiteral("%1-%2.%3").arg(base).arg(n).arg(suf);
        used.insert(name);
        QFile::link(f, staging->filePath(name));
        members << name;
    }

    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(staging->path());
    const int count = members.size();
    connect(proc, &QProcess::finished, this,
            [this, proc, staging, dest, count, report](int code, QProcess::ExitStatus st) {
                const QString errOut = QString::fromLocal8Bit(proc->readAllStandardError()).trimmed();
                proc->deleteLater();
                delete staging; // removes the staged symlinks
                if (st == QProcess::NormalExit && code == 0 && QFileInfo::exists(dest))
                    report(true, tr("Exported %1 captures to %2")
                                     .arg(count)
                                     .arg(QDir::toNativeSeparators(dest)));
                else
                    report(false, tr("Export failed: %1")
                                      .arg(errOut.isEmpty() ? tr("zip exited with code %1").arg(code)
                                                            : errOut));
            });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, staging, report](QProcess::ProcessError e) {
                if (e != QProcess::FailedToStart)
                    return; // a crash after start still emits finished(), which cleans up
                proc->deleteLater();
                delete staging;
                report(false, tr("Could not run the “zip” program."));
            });
    // "./" prefix so a staged name starting with '-' is a path, not a zip flag
    // (Info-ZIP parses leading-dash argv anywhere as options); -j still stores
    // just the basename, so the archive entries are unchanged.
    QStringList zipArgs{QStringLiteral("-j"), QStringLiteral("-q"), dest};
    for (const QString &m : std::as_const(members))
        zipArgs << (QStringLiteral("./") + m);
    proc->start(zipBin, zipArgs);
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
        // Read-only properties (build facts, and the bounds the UI reads) are
        // not configuration: importing them writes nothing, so exporting them
        // only puts keys in the file that cannot come back out of it.
        if (!p.isWritable())
            continue;
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

// The command a desktop custom shortcut runs: our own binary + `--hotkey <id>`,
// forwarded over the single-instance socket to a running Unisic. Both COSMIC
// (shlex) and the gsettings/xfconf command fields (g_shell_parse_argv) split
// the string, so shell-quote a binary path that carries anything special.
QString AppContext::hotkeyCommand(const QString &actionId) const
{
    QString bin = QCoreApplication::applicationFilePath();
    static const QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9_./:-]"));
    if (bin.contains(unsafe)) {
        bin.replace(QLatin1Char('\''), QLatin1String("'\\''"));
        bin = QLatin1Char('\'') + bin + QLatin1Char('\'');
    }
    return bin + QStringLiteral(" --hotkey ") + actionId;
}

QList<ShortcutBinder::Binding> AppContext::desktopShortcutBindings() const
{
    QList<ShortcutBinder::Binding> out;
    for (const HotkeyAction &a : hotkeyActions()) {
        // OCR without tesseract built in would spawn a no-op — leave it out.
        if (a.id == QLatin1String("ocr-region") && !ocrAvailable())
            continue;
        out.append({a.id, a.name, a.keys, hotkeyCommand(a.id)});
    }
    return out;
}

bool AppContext::desktopShortcutsAuto() const
{
    if (hotkeysAvailable())
        return false;
    return ShortcutBinder::autoInstallable(ShortcutBinder::detect());
}

QString AppContext::desktopShortcutName() const
{
    return ShortcutBinder::desktopName(ShortcutBinder::detect());
}

bool AppContext::installDesktopShortcuts()
{
    const ShortcutBinder::Backend b = ShortcutBinder::detect();
    if (!ShortcutBinder::autoInstallable(b)) {
        showToast(tr("This desktop can't be set up automatically - use the commands below."), true);
        return false;
    }
    const ShortcutBinder::Result r = ShortcutBinder::install(b, desktopShortcutBindings());
    if (!r.ok) {
        showToast(tr("Could not add shortcuts: %1").arg(r.error), true);
        return false;
    }
    m_settings->setDesktopShortcutsInstalled(true);
    QString msg = tr("Added %n shortcut(s) to %1", nullptr, r.written)
                      .arg(ShortcutBinder::desktopName(b));
    if (!r.skipped.isEmpty())
        msg += QLatin1Char(' ')
               + tr("(skipped, no mappable key: %1)").arg(r.skipped.join(QStringLiteral(", ")));
    // A custom shortcut loses to the desktop's own built-in one without a word
    // from either side, so this is the only chance to say why a key that was
    // just "added" does nothing. Important, so it shows even with toasts off.
    const bool clash = !r.taken.isEmpty();
    if (clash)
        msg += QLatin1Char(' ')
               + tr("%1 already uses these keys - change them in Hotkeys: %2")
                     .arg(ShortcutBinder::desktopName(b), r.taken.join(QStringLiteral(", ")));
    showToast(msg, clash);
    return true;
}

void AppContext::removeDesktopShortcuts()
{
    const ShortcutBinder::Backend b = ShortcutBinder::detect();
    const ShortcutBinder::Result r = ShortcutBinder::remove(b);
    if (!r.ok) {
        showToast(tr("Could not remove shortcuts: %1").arg(r.error), true);
    } else {
        m_settings->setDesktopShortcutsInstalled(false);
        showToast(tr("Removed Unisic shortcuts from %1").arg(ShortcutBinder::desktopName(b)));
    }
}

// Singularity regenerates ~/.config/labwc/rc.xml from its own template on login
// and on any shortcut edit — Unisic's injected keybinds do not survive
// (verified against dev.sinty.desktop.Shortcuts.WriteLabwcRcXml). While the
// app runs, a watcher puts them back; the immediate pass below covers the
// common case where the DE rewrote the store before Unisic started. Both are
// gated on the user having pressed "Add shortcuts" at least once (the
// persisted desktopShortcutsInstalled flag), so an explicit Remove sticks.
void AppContext::armDesktopShortcutReassert()
{
    const ShortcutBinder::Backend b = ShortcutBinder::detect();
    const QString path = ShortcutBinder::watchPath(b);
    if (path.isEmpty())
        return;

    const auto reassert = [this, b] {
        if (!m_settings->desktopShortcutsInstalled() || hotkeysAvailable())
            return;
        if (ShortcutBinder::present(b)) {
            m_shortcutReassertMisses = 0; // converged — the store kept our entries
            return;
        }
        // Give-up valve: convergence normally relies on the DE only
        // regenerating the store on login/edit. If a future Singularity starts
        // rewriting rc.xml in REACTION to foreign writes, this loop becomes a
        // 1 Hz file-rewrite war neither side can win — stop after a few
        // consecutive rounds instead of ping-ponging forever.
        constexpr int kReassertMissCap = 5;
        if (++m_shortcutReassertMisses > kReassertMissCap) {
            qWarning() << "Desktop shortcut re-assert: store wiped our entries"
                       << kReassertMissCap << "times in a row - giving up until the next launch";
            if (m_shortcutStoreWatcher) {
                m_shortcutStoreWatcher->deleteLater();
                m_shortcutStoreWatcher = nullptr;
            }
            return;
        }
        const ShortcutBinder::Result r = ShortcutBinder::install(b, desktopShortcutBindings());
        if (r.ok)
            qInfo() << "Re-asserted" << r.written << "desktop shortcuts in"
                    << ShortcutBinder::desktopName(b) << "(the desktop rewrote its store)";
        else
            qWarning() << "Desktop shortcut re-assert failed:" << r.error;
    };

    // The DE replaces the file by rename, which silently drops it from the
    // watcher — re-arm file AND dir on every pass (same rule as the theme
    // watcher). The dir watch also covers the file being briefly absent.
    m_shortcutStoreWatcher = new QFileSystemWatcher(this);
    const auto rearm = [this, path] {
        if (!m_shortcutStoreWatcher)
            return; // the give-up valve dropped the watcher; a queued debounce may still fire once
        if (QFile::exists(path) && !m_shortcutStoreWatcher->files().contains(path))
            m_shortcutStoreWatcher->addPath(path);
        const QString dir = QFileInfo(path).path();
        if (!m_shortcutStoreWatcher->directories().contains(dir))
            m_shortcutStoreWatcher->addPath(dir);
    };
    // Debounced: a regeneration fires several file+dir change signals in a
    // burst, and one pass right after it settles is enough.
    m_shortcutReassertDebounce = new QTimer(this);
    m_shortcutReassertDebounce->setSingleShot(true);
    m_shortcutReassertDebounce->setInterval(1000);
    connect(m_shortcutReassertDebounce, &QTimer::timeout, this, [rearm, reassert] {
        rearm();
        reassert();
    });
    const auto kick = [this](const QString &) { m_shortcutReassertDebounce->start(); };
    connect(m_shortcutStoreWatcher, &QFileSystemWatcher::fileChanged, this, kick);
    connect(m_shortcutStoreWatcher, &QFileSystemWatcher::directoryChanged, this, kick);
    rearm();
    reassert();
}

QString AppContext::desktopShortcutManualText() const
{
    return ShortcutBinder::manualText(ShortcutBinder::detect(), desktopShortcutBindings());
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
    const auto actions = hotkeyActions();
    for (const HotkeyAction &action : actions) {
        m_hotkeys->activeKeysAsync(
            action.id, this,
            [this, action](bool ok, const QList<int> &keys) {
                // A failed/timed-out query must not be mistaken for unbound;
                // that would wipe and persist the stored key.
                const QString actual = GlobalHotkeys::portableFromKeys(keys);
                if (ok && !GlobalHotkeys::sameBinding(actual, action.keys))
                    syncHotkeyFromDaemon(action.id, actual);
            });
    }
}

// Called once at startup (deferred past engine load): register each action +
// its default with autoloading so a key edited in KDE's Shortcuts KCM is
// honored, then pick the portal backend when KGlobalAccel isn't the answer.
void AppContext::defineHotkeys()
{
    // Stored bindings of the hotkeys removed in 0.7.4 — dead keys, drop them.
    m_settings->raw()->remove(QStringLiteral("hotkeys/screen"));
    m_settings->raw()->remove(QStringLiteral("hotkeys/recapture"));

    const QVector<HotkeyAction> acts = hotkeyActions();

#ifdef HAVE_X11_HOTKEYS
    // X11 session: XGrabKey is the reliable global-hotkey path on non-KDE X11
    // (GNOME/Xorg, Xfce), and beats the flaky GlobalShortcuts portal there. On
    // KDE-X11 KGlobalAccel still owns hotkeys (handled by the branch below), but
    // UNISIC_HOTKEY_BACKEND=x11 forces this path for testing; =portal opts out.
    {
        const QString forced = qEnvironmentVariable("UNISIC_HOTKEY_BACKEND");
        const bool wantX11 = X11Hotkeys::isAvailable()
            && (forced == QLatin1String("x11")
                || (forced != QLatin1String("portal") && !m_hotkeys->available()));
        if (wantX11) {
            m_x11hotkeys = new X11Hotkeys(this);
            connect(m_x11hotkeys, &X11Hotkeys::activated, this, &AppContext::dispatchHotkey);
            m_hotkeyBackend = QStringLiteral("x11");
            bindX11Hotkeys();
            emit hotkeysAvailableChanged();
            return;
        }
    }
#endif

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
        m_hotkeys->setShortcutAsync(
            QStringLiteral("stop-recording"), tr("Stop recording (emergency)"),
            QStringLiteral("Ctrl+Escape"), this, [this](bool accepted) {
                if (accepted)
                    return;
                qWarning() << "Ctrl+Escape emergency stop could not be bound (owned by another"
                              " component - on stock Plasma: Show System Activity)";
                showToast(tr("Ctrl+Esc emergency stop unavailable: the key is taken by the system "
                             "(System Settings → Shortcuts to free it)"));
            });
#ifdef UNISIC_DEV_BUILD
        // Dev-only: F8 runs the smoke test. Fixed key (not user-configurable).
        m_hotkeys->setShortcutAsync(QStringLiteral("smoke-test"),
                                    tr("Developer smoke test"), QStringLiteral("F8"), this);
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
        // 0.7.4: screen-under-cursor and re-capture-last-region are no longer
        // hotkeys (the tray menu and CLI still expose both; the full-screen
        // scope preference and the persistent-region preference replace the
        // keys). Release + unregister so an upgraded install keeps no dead
        // grab and no phantom KCM row.
        m_hotkeys->releaseShortcut(QStringLiteral("capture-screen"), tr("Capture screen under cursor"));
        m_hotkeys->unregisterAction(QStringLiteral("capture-screen"));
        m_hotkeys->releaseShortcut(QStringLiteral("recapture-region"), tr("Re-capture last region"));
        m_hotkeys->unregisterAction(QStringLiteral("recapture-region"));
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
        // each one by hand. hotkeyBindStatusAsync asserts the stored key on any
        // action the daemon reports unbound, and syncs a KCM-edited key back
        // into the UI. A deliberate KCM unbind made while the app runs still
        // sticks: it arrives via yourShortcutsChanged and empties the stored
        // string, so there is nothing to assert on the next launch.
        // The backend is usable immediately. Verification continues as an
        // ordered asynchronous state machine, so a slow daemon cannot freeze
        // the already-visible window.
        emit hotkeysAvailableChanged();
        hotkeyBindStatusAsync(
            true, [this](int unbound, const QStringList &report,
                         const QStringList &conflicts) {
                if (unbound > 0)
                    qWarning().noquote()
                        << "Hotkey repair:\n" + report.join(QLatin1Char('\n'));
                // A key another component owns daemon-side never reaches us
                // even though it shows as bound.
                if (!conflicts.isEmpty()) {
                    qWarning().noquote()
                        << "Hotkey conflicts:\n" + conflicts.join(QLatin1Char('\n'));
                    showToast(tr("Hotkey taken by another app: %1. Pick a different key in "
                                 "Settings → Hotkeys, or free it in System Settings → Shortcuts.")
                                  .arg(conflicts.join(QStringLiteral("; "))), true);
                }
            });
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
                              " - falling back to compositor-binds guidance";
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

// Grab the whole set through XGrabKey (X11 session). Re-grabs everything (the
// backend has no per-action rebind); a key another client owns is surfaced as a
// conflict toast, mirroring the KGlobalAccel path.
void AppContext::bindX11Hotkeys()
{
#ifdef HAVE_X11_HOTKEYS
    if (!m_x11hotkeys)
        return;
    QVector<X11Hotkeys::Shortcut> list;
    const auto acts = hotkeyActions();
    for (const HotkeyAction &a : acts)
        list.append({a.id, a.keys});
    // Fixed emergency stop, same key the other backends reserve.
    list.append({QStringLiteral("stop-recording"), QStringLiteral("Ctrl+Escape")});
    const QStringList conflicts = m_x11hotkeys->bind(list);
    if (!conflicts.isEmpty()) {
        qWarning().noquote() << "X11 hotkey conflicts:\n" + conflicts.join(QLatin1Char('\n'));
        showToast(tr("Hotkey taken by another app: %1. Pick a different key in "
                     "Settings → Hotkeys.").arg(conflicts.join(QStringLiteral("; "))), true);
    }
#endif
}

// Push ONE action's stored key to the system. KGlobalAccel: setShortcut with
// SetPresent|NoAutoloading, conflict surfaced as a toast + the daemon's actual
// key synced back into the UI. Portal: re-bind the whole set (the portal has
// no per-shortcut rebind; unchanged sets don't re-prompt on KDE/GNOME).
void AppContext::applyHotkey(const QString &actionId)
{
    if (m_hotkeyBackend == QLatin1String("x11")) {
        bindX11Hotkeys();
        return;
    }
    if (m_portalHotkeys && m_hotkeyBackend == QLatin1String("portal")) {
        bindPortalHotkeys();
        return;
    }
    if (!m_hotkeys->available())
        return;
    const auto acts = hotkeyActions();
    for (const HotkeyAction &action : acts) {
        if (action.id != actionId)
            continue;
        m_hotkeys->setShortcutAsync(
            action.id, action.name, action.keys, this,
            [this, action](bool accepted) {
                if (accepted)
                    return;
                showToast(tr("Could not bind %1; the key is taken by another shortcut")
                              .arg(action.keys), true);
                // Show what is actually bound instead of the refused wish.
                m_hotkeys->activeKeysAsync(
                    action.id, this,
                    [this, action](bool ok, const QList<int> &keys) {
                        if (ok)
                            syncHotkeyFromDaemon(
                                action.id, GlobalHotkeys::portableFromKeys(keys));
                    });
            });
        return;
    }
}

// Bulk push (settings import, the explicit "Apply hotkeys" button): the app's
// stored keys are the user's intent here, so all five are asserted.
void AppContext::applyHotkeys()
{
    if (m_hotkeyBackend == QLatin1String("x11")) {
        bindX11Hotkeys();
        return;
    }
    if (m_portalHotkeys && m_hotkeyBackend == QLatin1String("portal")) {
        bindPortalHotkeys();
        return;
    }
    if (!m_hotkeys->available())
        return;
    const auto acts = hotkeyActions();
    if (acts.isEmpty())
        return;
    auto remaining = std::make_shared<int>(acts.size());
    auto allOk = std::make_shared<bool>(true);
    for (const HotkeyAction &action : acts) {
        m_hotkeys->setShortcutAsync(
            action.id, action.name, action.keys, this,
            [this, remaining, allOk](bool accepted) {
                *allOk &= accepted;
                if (--*remaining != 0 || *allOk)
                    return;
                showToast(tr("Some hotkeys could not be bound (keys taken); showing the actual state"),
                          true);
                syncAllHotkeysFromDaemon();
            });
    }
}

// Defined later in this TU; forward-declared so the tray menu can tint the
// bundled monochrome icons to the menu's own text colour.
static QPixmap recolorPixmap(const QString &path, const QColor &color, const QSize &size);

// A QIcon from a bundled sym/<name>.svg, tinted to the menu text colour so it
// sits right in a native QMenu in both light and dark themes.
static QIcon trayMenuIcon(const QString &name)
{
    const QColor c = qApp->palette().color(QPalette::WindowText);
    QIcon icon;
    for (int s : {16, 22, 24}) {
        const QPixmap pm = recolorPixmap(
            QStringLiteral(":/resources/icons/sym/%1.svg").arg(name), c, QSize(s, s));
        if (!pm.isNull())
            icon.addPixmap(pm);
    }
    return icon;
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
    // The tray menu is the app's quick menu: every capture and recording mode
    // the app has must be reachable here, or the tray is a worse copy of the
    // window. Grouped so the list stays readable at a dozen entries.
    menu->addAction(trayMenuIcon(QStringLiteral("region")), tr("Capture region"), this, &AppContext::captureRegion);
    menu->addAction(trayMenuIcon(QStringLiteral("monitor")), tr("Capture full screen"), this, &AppContext::captureFullScreen);
    menu->addAction(trayMenuIcon(QStringLiteral("monitor")), tr("Capture screen under cursor"), this, &AppContext::captureScreenUnderCursor);
    menu->addAction(trayMenuIcon(QStringLiteral("window")), tr("Capture window"), this, &AppContext::captureWindow);
    QAction *recapture = menu->addAction(trayMenuIcon(QStringLiteral("region")), tr("Re-capture last region"),
                                         this, &AppContext::recaptureLastRegion);
    menu->addAction(trayMenuIcon(QStringLiteral("measure")), tr("Measure"), this, &AppContext::captureMeasure);
    if (ocrAvailable())
        menu->addAction(trayMenuIcon(QStringLiteral("ocr")), tr("Select text…"), this, &AppContext::captureRegionOcr);
    menu->addSeparator();
    menu->addAction(trayMenuIcon(QStringLiteral("media-record")), tr("Record video (region)"), this, &AppContext::startVideoRegion);
    menu->addAction(trayMenuIcon(QStringLiteral("media-record")), tr("Record video (full screen)"), this, &AppContext::startVideoScreen);
    menu->addAction(trayMenuIcon(QStringLiteral("media-record")), tr("Record video (window)"), this, &AppContext::startVideoWindow);
    menu->addAction(trayMenuIcon(QStringLiteral("gif")), tr("Record GIF (region)"), this, &AppContext::startGifRegion);
    menu->addAction(trayMenuIcon(QStringLiteral("gif")), tr("Record GIF (full screen)"), this, &AppContext::startGifFullScreen);
    QAction *replayStart = menu->addAction(trayMenuIcon(QStringLiteral("media-record")), tr("Start instant replay"), this,
                                           &AppContext::startInstantReplay);
    QAction *replaySave = menu->addAction(trayMenuIcon(QStringLiteral("document-save")), tr("Save instant replay"), this,
                                          &AppContext::saveInstantReplay);
    QAction *stopRec = menu->addAction(trayMenuIcon(QStringLiteral("stop")), tr("Stop recording"), this, &AppContext::stopRecording);
    // The menu is built once, so anything state-dependent has to be refreshed
    // when it opens — otherwise it shows whatever was true at startup.
    connect(menu, &QMenu::aboutToShow, this, [this, replayStart, replaySave, stopRec, recapture] {
        replayStart->setVisible(!instantReplayActive());
        replaySave->setVisible(instantReplayActive());
        replayStart->setEnabled(!recording());
        stopRec->setEnabled(recording());
        recapture->setEnabled(!m_settings->lastCaptureRegion().isEmpty());
    });
    menu->addSeparator();
    menu->addAction(trayMenuIcon(QStringLiteral("content-copy")), tr("Copy last capture"), this, &AppContext::copyLastCapture);
    menu->addSeparator();
    if (m_updater && m_updater->restartPending()) {
        // The new version is already swapped in — one click finishes the job.
        menu->addAction(tr("Restart to update to Unisic %1").arg(m_updater->latestVersion()),
                        m_updater, &UpdateChecker::restartNow);
        menu->addSeparator();
    } else if (m_updater && m_updater->updateAvailable()
               && m_updater->canInstallViaScript()) {
        // Native package: one click runs install.sh in a terminal (sudo there).
        menu->addAction(tr("Install update to Unisic %1").arg(m_updater->latestVersion()),
                        m_updater, &UpdateChecker::installViaScript);
        menu->addSeparator();
    } else if (m_updater && m_updater->updateAvailable()) {
        // Persistent counterpart of the one-shot update toast — a tray-dwelling
        // app may never have a window up when the toast fires.
        menu->addAction(tr("Update available - Unisic %1").arg(m_updater->latestVersion()),
                        this, [this] { emit showMainWindowRequested(); });
        menu->addSeparator();
    }
    menu->addAction(trayMenuIcon(QStringLiteral("monitor")), tr("Open Unisic"), this, [this] { emit showMainWindowRequested(); });
    menu->addAction(trayMenuIcon(QStringLiteral("close")), tr("Quit"), qApp, &QCoreApplication::quit);
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

// Sandboxed autostart goes through the Background portal, not through a file:
// $XDG_CONFIG_HOME inside the sandbox is ~/.var/app/<id>/config, which the
// host session never reads, so a .desktop written there would leave the switch
// saying "on" while nothing starts at login. The portal writes the host-side
// entry itself (and, on some backends, asks the user first).
static bool sandboxed()
{
    return qEnvironmentVariableIsSet("FLATPAK_ID");
}

bool AppContext::autostartEnabled() const
{
    // The portal exposes no getter, so the last answer it gave is the state.
    // It is the ANSWER that is stored, never the request: a backend that
    // refuses (or a user who says no) leaves this false and the switch snaps
    // back, which is the honest reading.
    if (sandboxed())
        return m_settings && m_settings->portalAutostartGranted();
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
    // Nothing to keep fresh in a sandbox: the entry lives host-side and is the
    // portal's to write, and the exec path inside /app never moves.
    if (sandboxed())
        return;
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

void AppContext::requestPortalAutostart(bool on)
{
    const QString token = PortalRequest::nextToken();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Background"),
        QStringLiteral("RequestBackground"));
    QVariantMap options{
        {QStringLiteral("handle_token"), token},
        {QStringLiteral("autostart"), on},
        // Without this the portal grants "may run in the background" but writes
        // no autostart entry; with it the entry runs the same tray-only launch
        // the native autostart file does. `unisic` resolves inside the sandbox.
        {QStringLiteral("commandline"), QStringList{QStringLiteral("unisic"),
                                                    QStringLiteral("--tray-only")}},
        {QStringLiteral("reason"), tr("Unisic starts hidden in the tray so its capture "
                                      "shortcuts work right after you log in.")},
    };
    msg << QString() << options; // parent_window: empty (no exported handle on Wayland)

    PortalRequest::send(msg, token, [this, on](uint code, const QVariantMap &results) {
        if (code != 0) {
            if (code != 1) // 1 = the user cancelled; not worth a toast
                showToast(tr("The desktop refused the autostart request"), true);
            emit autostartEnabledChanged(); // snap the switch back to reality
            return;
        }
        const bool granted = results.value(QStringLiteral("autostart"), false).toBool();
        if (m_settings)
            m_settings->setPortalAutostartGranted(granted);
        if (on && !granted)
            showToast(tr("Autostart was not granted"), true);
        emit autostartEnabledChanged();
    }, this, 0); // a portal that asks the user must be allowed to wait for them
}

void AppContext::setAutostartEnabled(bool on)
{
    if (on == autostartEnabled())
        return;
    if (sandboxed()) {
        requestPortalAutostart(on);
        return;
    }
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
