#pragma once
#include <QObject>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <QFile>
#include <QDebug>
#include <QCoreApplication>
#include "ConfigPath.h"
#include <qqmlregistration.h>

#define U_SETTING(type, name, setterName, key, defval)                                 \
    type name() const { return m_s.value(key, defval).value<type>(); }                 \
    void setterName(const type &v) {                                                   \
        if (name() == v) return;                                                       \
        m_s.setValue(key, v);                                                          \
        m_syncTimer.start();                                                            \
        emit name##Changed();                                                          \
    }

class Settings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by AppContext")

    Q_PROPERTY(QString saveDirectory READ saveDirectory WRITE setSaveDirectory NOTIFY saveDirectoryChanged)
    Q_PROPERTY(QString videoSaveDirectory READ videoSaveDirectory WRITE setVideoSaveDirectory NOTIFY videoSaveDirectoryChanged)
    Q_PROPERTY(bool autoSave READ autoSave WRITE setAutoSave NOTIFY autoSaveChanged)
    Q_PROPERTY(bool copyToClipboard READ copyToClipboard WRITE setCopyToClipboard NOTIFY copyToClipboardChanged)
    Q_PROPERTY(bool openEditor READ openEditor WRITE setOpenEditor NOTIFY openEditorChanged)
    Q_PROPERTY(bool uploadAfterCapture READ uploadAfterCapture WRITE setUploadAfterCapture NOTIFY uploadAfterCaptureChanged)
    Q_PROPERTY(bool includeCursor READ includeCursor WRITE setIncludeCursor NOTIFY includeCursorChanged)
    Q_PROPERTY(int captureDelayMs READ captureDelayMs WRITE setCaptureDelayMs NOTIFY captureDelayMsChanged)
    Q_PROPERTY(QString captureSound READ captureSound WRITE setCaptureSound NOTIFY captureSoundChanged)
    Q_PROPERTY(QString recordingSound READ recordingSound WRITE setRecordingSound NOTIFY recordingSoundChanged)
    Q_PROPERTY(int gifFps READ gifFps WRITE setGifFps NOTIFY gifFpsChanged)
    Q_PROPERTY(int gifMaxDurationSec READ gifMaxDurationSec WRITE setGifMaxDurationSec NOTIFY gifMaxDurationSecChanged)
    Q_PROPERTY(int gifQuality READ gifQuality WRITE setGifQuality NOTIFY gifQualityChanged)
    Q_PROPERTY(QString activeDestination READ activeDestination WRITE setActiveDestination NOTIFY activeDestinationChanged)
    Q_PROPERTY(QString hotkeyFullScreen READ hotkeyFullScreen WRITE setHotkeyFullScreen NOTIFY hotkeyFullScreenChanged)
    Q_PROPERTY(QString hotkeyRegion READ hotkeyRegion WRITE setHotkeyRegion NOTIFY hotkeyRegionChanged)
    Q_PROPERTY(QString hotkeyWindow READ hotkeyWindow WRITE setHotkeyWindow NOTIFY hotkeyWindowChanged)
    Q_PROPERTY(QString hotkeyGif READ hotkeyGif WRITE setHotkeyGif NOTIFY hotkeyGifChanged)
    Q_PROPERTY(QString imageFormat READ imageFormat WRITE setImageFormat NOTIFY imageFormatChanged)
    Q_PROPERTY(int imageQuality READ imageQuality WRITE setImageQuality NOTIFY imageQualityChanged)
    Q_PROPERTY(QString filenameTemplate READ filenameTemplate WRITE setFilenameTemplate NOTIFY filenameTemplateChanged)
    Q_PROPERTY(bool showNotifications READ showNotifications WRITE setShowNotifications NOTIFY showNotificationsChanged)
    Q_PROPERTY(bool minimizeToTrayOnClose READ minimizeToTrayOnClose WRITE setMinimizeToTrayOnClose NOTIFY minimizeToTrayOnCloseChanged)
    Q_PROPERTY(bool openAfterSave READ openAfterSave WRITE setOpenAfterSave NOTIFY openAfterSaveChanged)
    Q_PROPERTY(bool afterUploadCopyLink READ afterUploadCopyLink WRITE setAfterUploadCopyLink NOTIFY afterUploadCopyLinkChanged)
    Q_PROPERTY(bool afterUploadOpenInBrowser READ afterUploadOpenInBrowser WRITE setAfterUploadOpenInBrowser NOTIFY afterUploadOpenInBrowserChanged)
    Q_PROPERTY(QString editorStrokeColor READ editorStrokeColor WRITE setEditorStrokeColor NOTIFY editorStrokeColorChanged)
    Q_PROPERTY(int editorStrokeWidth READ editorStrokeWidth WRITE setEditorStrokeWidth NOTIFY editorStrokeWidthChanged)
    Q_PROPERTY(int editorFontSize READ editorFontSize WRITE setEditorFontSize NOTIFY editorFontSizeChanged)
    Q_PROPERTY(QString editorFillColor READ editorFillColor WRITE setEditorFillColor NOTIFY editorFillColorChanged)
    Q_PROPERTY(bool editorFillEnabled READ editorFillEnabled WRITE setEditorFillEnabled NOTIFY editorFillEnabledChanged)
    Q_PROPERTY(QString editorFontFamily READ editorFontFamily WRITE setEditorFontFamily NOTIFY editorFontFamilyChanged)
    Q_PROPERTY(bool editorFontBold READ editorFontBold WRITE setEditorFontBold NOTIFY editorFontBoldChanged)
    Q_PROPERTY(bool editorFontItalic READ editorFontItalic WRITE setEditorFontItalic NOTIFY editorFontItalicChanged)
    Q_PROPERTY(bool editorFontUnderline READ editorFontUnderline WRITE setEditorFontUnderline NOTIFY editorFontUnderlineChanged)
    Q_PROPERTY(bool editorTextOutline READ editorTextOutline WRITE setEditorTextOutline NOTIFY editorTextOutlineChanged)
    Q_PROPERTY(QString editorTextOutlineColor READ editorTextOutlineColor WRITE setEditorTextOutlineColor NOTIFY editorTextOutlineColorChanged)
    Q_PROPERTY(bool editorTextBackground READ editorTextBackground WRITE setEditorTextBackground NOTIFY editorTextBackgroundChanged)
    Q_PROPERTY(QString editorTextBgColor READ editorTextBgColor WRITE setEditorTextBgColor NOTIFY editorTextBgColorChanged)
    Q_PROPERTY(bool editorResetColors READ editorResetColors WRITE setEditorResetColors NOTIFY editorResetColorsChanged)
    Q_PROPERTY(bool editorResetTools READ editorResetTools WRITE setEditorResetTools NOTIFY editorResetToolsChanged)
    Q_PROPERTY(QString recentColors READ recentColors WRITE setRecentColors NOTIFY recentColorsChanged)
    Q_PROPERTY(QString hiddenTools READ hiddenTools WRITE setHiddenTools NOTIFY hiddenToolsChanged)
    Q_PROPERTY(QString overlayToolbarPosition READ overlayToolbarPosition WRITE setOverlayToolbarPosition NOTIFY overlayToolbarPositionChanged)
    Q_PROPERTY(bool selectionGuides READ selectionGuides WRITE setSelectionGuides NOTIFY selectionGuidesChanged)
    Q_PROPERTY(bool smartPick READ smartPick WRITE setSmartPick NOTIFY smartPickChanged)
    Q_PROPERTY(bool captureOnRelease READ captureOnRelease WRITE setCaptureOnRelease NOTIFY captureOnReleaseChanged)
    Q_PROPERTY(QString hotkeyCopyLast READ hotkeyCopyLast WRITE setHotkeyCopyLast NOTIFY hotkeyCopyLastChanged)
    Q_PROPERTY(int videoFps READ videoFps WRITE setVideoFps NOTIFY videoFpsChanged)
    Q_PROPERTY(QString videoFormat READ videoFormat WRITE setVideoFormat NOTIFY videoFormatChanged)
    Q_PROPERTY(int videoQuality READ videoQuality WRITE setVideoQuality NOTIFY videoQualityChanged)
    Q_PROPERTY(int videoMaxDurationSec READ videoMaxDurationSec WRITE setVideoMaxDurationSec NOTIFY videoMaxDurationSecChanged)
    Q_PROPERTY(bool recordSystemAudio READ recordSystemAudio WRITE setRecordSystemAudio NOTIFY recordSystemAudioChanged)
    Q_PROPERTY(bool recordMicrophone READ recordMicrophone WRITE setRecordMicrophone NOTIFY recordMicrophoneChanged)
    Q_PROPERTY(QString hotkeyRecord READ hotkeyRecord WRITE setHotkeyRecord NOTIFY hotkeyRecordChanged)
    Q_PROPERTY(QString hotkeyOcr READ hotkeyOcr WRITE setHotkeyOcr NOTIFY hotkeyOcrChanged)
    Q_PROPERTY(bool showCapturePopup READ showCapturePopup WRITE setShowCapturePopup NOTIFY showCapturePopupChanged)
    Q_PROPERTY(QString capturePopupPosition READ capturePopupPosition WRITE setCapturePopupPosition NOTIFY capturePopupPositionChanged)
    Q_PROPERTY(QString capturePopupStyle READ capturePopupStyle WRITE setCapturePopupStyle NOTIFY capturePopupStyleChanged)
    Q_PROPERTY(int capturePopupDurationSec READ capturePopupDurationSec WRITE setCapturePopupDurationSec NOTIFY capturePopupDurationSecChanged)
    Q_PROPERTY(bool muteOnFullscreen READ muteOnFullscreen WRITE setMuteOnFullscreen NOTIFY muteOnFullscreenChanged)
    Q_PROPERTY(QString ocrLanguages READ ocrLanguages WRITE setOcrLanguages NOTIFY ocrLanguagesChanged)
    Q_PROPERTY(bool useU2Net READ useU2Net WRITE setUseU2Net NOTIFY useU2NetChanged)
    Q_PROPERTY(QString segmentModel READ segmentModel WRITE setSegmentModel NOTIFY segmentModelChanged)
    Q_PROPERTY(QString segmentCustomModel READ segmentCustomModel WRITE setSegmentCustomModel NOTIFY segmentCustomModelChanged)
    Q_PROPERTY(QString editorIconStyle READ editorIconStyle WRITE setEditorIconStyle NOTIFY editorIconStyleChanged)
    Q_PROPERTY(QString editorToolIcons READ editorToolIcons WRITE setEditorToolIcons NOTIFY editorToolIconsChanged)
    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY uiLanguageChanged)
    Q_PROPERTY(bool useSystemDecoration READ useSystemDecoration WRITE setUseSystemDecoration NOTIFY useSystemDecorationChanged)
    Q_PROPERTY(QString trayIconPath READ trayIconPath WRITE setTrayIconPath NOTIFY trayIconPathChanged)
    Q_PROPERTY(bool autoCheckUpdates READ autoCheckUpdates WRITE setAutoCheckUpdates NOTIFY autoCheckUpdatesChanged)
    Q_PROPERTY(bool persistent READ persistent CONSTANT)

public:
    explicit Settings(QObject *parent = nullptr) : QObject(parent)
    {
        // QSettings only guarantees a flush in its destructor — any abnormal
        // exit (crash, SIGKILL, logout, Ctrl+C on a dev build) silently loses
        // every change made since launch. Debounce a sync() after each write
        // so changes hit disk within a second of being made.
        m_syncTimer.setSingleShot(true);
        m_syncTimer.setInterval(800);
        connect(&m_syncTimer, &QTimer::timeout, this, [this] { m_s.sync(); });
        // Belt-and-suspenders flush on every quit path (covers exits that skip
        // the QSettings destructor).
        if (qApp)
            connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { m_s.sync(); });

#ifdef UNISIC_DEV_BUILD
        // Dev build = separate app with its own config. First run: seed from
        // the STABLE app's config so testing starts from familiar settings —
        // EXCEPT hotkeys, which are seeded unbound: the stable component
        // still owns its keys daemon-side, so copied bindings would lose
        // every press and fire the conflict toast on each launch. Assign
        // dev-specific keys in Settings → Hotkeys when needed.
        const QString stableConf = UnisicConfig::stableConfigDir() + QStringLiteral("/unisic.conf");
        if (m_s.allKeys().isEmpty() && QFile::exists(stableConf)) {
            QSettings stable(stableConf, QSettings::IniFormat);
            const QStringList keys = stable.allKeys();
            for (const QString &k : keys)
                m_s.setValue(k, k.startsWith(QLatin1String("hotkeys/")) ? QString()
                                                                        : stable.value(k));
            for (const char *hk : {"fullScreen", "region", "window", "gif", "record", "ocrRegion"})
                m_s.setValue(QStringLiteral("hotkeys/") + QLatin1String(hk), QString());
            m_s.sync();
            if (m_s.status() == QSettings::NoError)
                qInfo() << "Seeded dev settings (hotkeys unbound) from" << stable.fileName();
        }
#else
        // One-time migration from the old ~/.config/Unisic/unisic.conf to the
        // unified ~/.config/unisic/unisic.conf. Key-by-key through QSettings
        // (NOT a raw file copy) so INI grouping/casing stays consistent —
        // a raw copy left keys under a mismatched [General] and dropped the
        // theme. Only when the new file is still empty.
        if (m_s.allKeys().isEmpty() && QFile::exists(UnisicConfig::legacyFilePath())) {
            QSettings legacy(UnisicConfig::legacyFilePath(), QSettings::IniFormat);
            const QStringList keys = legacy.allKeys();
            for (const QString &k : keys)
                m_s.setValue(k, legacy.value(k));
            m_s.sync();
            if (m_s.status() == QSettings::NoError)
                qInfo() << "Migrated" << keys.size() << "settings to" << m_s.fileName();
        }
#endif

        // "Settings reset to defaults on every launch" has two silent causes,
        // both of which QSettings hides unless status() is checked:
        //  1. A CORRUPT file (FormatError) — every read returns the default and
        //     the bad file lingers. Back it up and start clean so the corruption
        //     is not permanent.
        //  2. A non-writable config dir (wrong owner from an old root run,
        //     read-only/exotic mount, ephemeral HOME) — every setValue+sync
        //     silently no-ops, so nothing ever persists. A write+readback probe
        //     is the only way to detect it; surface it so the user is not left
        //     wondering why nothing sticks.
        m_s.sync();
        if (m_s.status() == QSettings::FormatError) {
            const QString f = m_s.fileName();
            QFile::remove(f + QStringLiteral(".corrupt"));
            if (QFile::rename(f, f + QStringLiteral(".corrupt")))
                qWarning() << "Settings file was corrupt — backed up to" << (f + ".corrupt")
                           << "and started fresh";
            m_s.sync(); // re-open the now-absent file cleanly
        }

        // Fold keys from the old "general" group into top-level keys. A group
        // named "general" collides with the INI format's magic "General"
        // section: QSettings WRITES it percent-escaped as [%General], but a
        // fresh process PARSES that back as group "General" (capital G).
        // Reads of "general/..." are case-sensitive, so they missed and every
        // General-tab setting silently reset to its default on each launch —
        // and each session then re-added a lowercase "general" group next to
        // the parsed "General" one, which serialized as a SECOND [%General]
        // section (the duplicated config files). Top-level keys land in the
        // plain [General] section, which round-trips exactly. Qt docs:
        // "Do not use a group called 'General'". Idempotent; also folds keys
        // brought in by the legacy migration above.
        const QStringList oldGeneral = m_s.allKeys();
        bool migratedGeneral = false;
        for (const QString &k : oldGeneral) {
            // "app/" folds too: an earlier dev branch briefly moved these keys to
            // an "app/" group before this top-level-keys fix landed — reconcile
            // those configs back so they don't reset once more.
            if (k.startsWith(QLatin1String("General/"))
                || k.startsWith(QLatin1String("general/"))
                || k.startsWith(QLatin1String("app/"))) {
                // Sorted allKeys yields "General/x" before "general/x", so the
                // lowercase variant (the app's most recent writes) wins.
                m_s.setValue(k.mid(k.indexOf(QLatin1Char('/')) + 1), m_s.value(k));
                m_s.remove(k);
                migratedGeneral = true;
            }
        }
        if (migratedGeneral)
            m_s.sync();
        // Writability probe: round-trip a marker through a fresh reader.
        m_s.setValue(QStringLiteral("_probe"), 1);
        m_s.sync();
        m_writable = (m_s.status() == QSettings::NoError);
        if (m_writable) {
            QSettings check(UnisicConfig::filePath(), QSettings::IniFormat);
            m_writable = check.value(QStringLiteral("_probe")).toInt() == 1;
        }
        m_s.remove(QStringLiteral("_probe"));
        m_s.sync();
        if (!m_writable)
            qWarning() << "Settings are NOT persisting — cannot write" << m_s.fileName()
                       << "(check permissions/ownership of ~/.config/unisic).";
    }

    // False when the config file cannot actually be written back (the UI shows
    // a warning banner so the user is not left re-configuring every launch).
    bool persistent() const { return m_writable; }
    Q_INVOKABLE QString configPath() const { return m_s.fileName(); }

    static QString defaultSaveDir()
    {
        // No mkpath here: U_SETTING evaluates the default on every read, so a
        // side effect would run per QML binding evaluation and resurrect the
        // directory even after the user picked a different one and deleted it.
        // Save paths (saveImageTo, GifRecorder) mkpath right before writing.
        static const QString d =
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/Unisic";
        return d;
    }

    static QString defaultVideoSaveDir()
    {
        // Same no-mkpath rule as defaultSaveDir.
        static const QString d =
            QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Unisic";
        return d;
    }

    U_SETTING(QString, saveDirectory, setSaveDirectory, "saveDirectory", defaultSaveDir())
    // Recordings (GIF/MP4/WebM) land here; screenshots keep saveDirectory.
    U_SETTING(QString, videoSaveDirectory, setVideoSaveDirectory, "videoSaveDirectory", defaultVideoSaveDir())
    U_SETTING(bool, autoSave, setAutoSave, "autoSave", true)
    U_SETTING(bool, copyToClipboard, setCopyToClipboard, "copyToClipboard", true)
    U_SETTING(bool, openEditor, setOpenEditor, "openEditor", true)
    U_SETTING(bool, uploadAfterCapture, setUploadAfterCapture, "uploadAfterCapture", false)
    U_SETTING(bool, includeCursor, setIncludeCursor, "includeCursor", false)
    U_SETTING(int, captureDelayMs, setCaptureDelayMs, "captureDelayMs", 200)
    // Capture sound cue (General tab). Bare key (never a "general"-named
    // group, see the INI [%General] trap). "off" or a bundled id.
    U_SETTING(QString, captureSound, setCaptureSound, "captureSound", QStringLiteral("shutter"))
    // Separate cue for finished recordings/GIFs — a shutter makes no sense
    // after a minutes-long recording, and users may want it off independently.
    U_SETTING(QString, recordingSound, setRecordingSound, "recordingSound", QStringLiteral("ding"))
    U_SETTING(int, gifFps, setGifFps, "gif/fps", 15)
    U_SETTING(int, gifMaxDurationSec, setGifMaxDurationSec, "gif/maxDurationSec", 30)
    U_SETTING(int, gifQuality, setGifQuality, "gif/quality", 2)
    U_SETTING(QString, activeDestination, setActiveDestination, "upload/activeDestination", QStringLiteral("catbox.moe"))
    U_SETTING(QString, hotkeyFullScreen, setHotkeyFullScreen, "hotkeys/fullScreen", QStringLiteral("Meta+Shift+1"))
    U_SETTING(QString, hotkeyRegion, setHotkeyRegion, "hotkeys/region", QStringLiteral("Meta+Shift+2"))
    U_SETTING(QString, hotkeyWindow, setHotkeyWindow, "hotkeys/window", QStringLiteral("Meta+Shift+3"))
    U_SETTING(QString, hotkeyGif, setHotkeyGif, "hotkeys/gif", QStringLiteral("Meta+Shift+G"))
    U_SETTING(QString, imageFormat, setImageFormat, "image/format", QStringLiteral("png"))
    U_SETTING(int, imageQuality, setImageQuality, "image/quality", 90)
    U_SETTING(QString, filenameTemplate, setFilenameTemplate, "image/filenameTemplate", QStringLiteral("Unisic_%date%_%time%"))
    U_SETTING(bool, showNotifications, setShowNotifications, "showNotifications", true)
    U_SETTING(bool, minimizeToTrayOnClose, setMinimizeToTrayOnClose, "minimizeToTrayOnClose", true)
    U_SETTING(bool, openAfterSave, setOpenAfterSave, "openAfterSave", false)
    U_SETTING(bool, afterUploadCopyLink, setAfterUploadCopyLink, "upload/afterUploadCopyLink", true)
    U_SETTING(bool, afterUploadOpenInBrowser, setAfterUploadOpenInBrowser, "upload/afterUploadOpenInBrowser", false)
    U_SETTING(QString, editorStrokeColor, setEditorStrokeColor, "editor/strokeColor", QStringLiteral("#FF4757"))
    U_SETTING(int, editorStrokeWidth, setEditorStrokeWidth, "editor/strokeWidth", 4)
    U_SETTING(int, editorFontSize, setEditorFontSize, "editor/fontSize", 22)
    U_SETTING(QString, editorFillColor, setEditorFillColor, "editor/fillColor", QStringLiteral("#66C8ACD6"))
    U_SETTING(bool, editorFillEnabled, setEditorFillEnabled, "editor/fillEnabled", false)
    // Text-tool styling defaults (empty family = default UI font).
    U_SETTING(QString, editorFontFamily, setEditorFontFamily, "editor/fontFamily", QString())
    U_SETTING(bool, editorFontBold, setEditorFontBold, "editor/fontBold", true)
    U_SETTING(bool, editorFontItalic, setEditorFontItalic, "editor/fontItalic", false)
    U_SETTING(bool, editorFontUnderline, setEditorFontUnderline, "editor/fontUnderline", false)
    U_SETTING(bool, editorTextOutline, setEditorTextOutline, "editor/textOutline", false)
    U_SETTING(QString, editorTextOutlineColor, setEditorTextOutlineColor, "editor/textOutlineColor", QStringLiteral("#000000"))
    U_SETTING(bool, editorTextBackground, setEditorTextBackground, "editor/textBackground", false)
    U_SETTING(QString, editorTextBgColor, setEditorTextBgColor, "editor/textBgColor", QStringLiteral("#B3000000"))
    // When on, colour (resetColors) / non-colour tool-option (resetTools)
    // changes made while annotating apply to that session only: nothing is
    // written back, so every editor/overlay starts from the defaults
    // configured in Settings → Editor again.
    U_SETTING(bool, editorResetColors, setEditorResetColors, "editor/resetColors", false)
    U_SETTING(bool, editorResetTools, setEditorResetTools, "editor/resetTools", false)
    U_SETTING(QString, recentColors, setRecentColors, "editor/recentColors", QString())
    U_SETTING(QString, hiddenTools, setHiddenTools, "editor/hiddenTools", QString())
    U_SETTING(QString, overlayToolbarPosition, setOverlayToolbarPosition, "capture/overlayToolbarPosition", QStringLiteral("follow"))
    // Crosshair guide lines from the cursor to the screen edges while selecting a
    // region (screenshot AND recording overlay). Off by default.
    U_SETTING(bool, selectionGuides, setSelectionGuides, "capture/selectionGuides", false)
    // Region overlay: a plain CLICK selects the detected object (window,
    // panel, image) under the cursor; dragging still draws a manual rect.
    // EXPERIMENTAL (default off): pure-pixel detection cannot recognize every
    // window/element reliably without heavy vision libraries.
    U_SETTING(bool, smartPick, setSmartPick, "capture/smartPick", false)
    // Region screenshot: releasing the selection drag captures immediately
    // (skips the annotate/confirm stage). GIF region picking is unaffected.
    U_SETTING(bool, captureOnRelease, setCaptureOnRelease, "capture/captureOnRelease", false)
    // Replaces the old 2s Ctrl+C grab (it stole ordinary copies right after a
    // capture): a dedicated always-on hotkey that never collides with Ctrl+C.
    U_SETTING(QString, hotkeyCopyLast, setHotkeyCopyLast, "hotkeys/copyLast", QStringLiteral("Meta+Shift+C"))
    U_SETTING(int, videoFps, setVideoFps, "video/fps", 30)
    U_SETTING(QString, videoFormat, setVideoFormat, "video/format", QStringLiteral("mp4"))
    U_SETTING(int, videoQuality, setVideoQuality, "video/quality", 20)
    U_SETTING(int, videoMaxDurationSec, setVideoMaxDurationSec, "video/maxDurationSec", 0)
    // Video recording audio (never GIF). Both OFF by default.
    U_SETTING(bool, recordSystemAudio, setRecordSystemAudio, "audio/recordSystemAudio", false)
    U_SETTING(bool, recordMicrophone, setRecordMicrophone, "audio/recordMicrophone", false)
    U_SETTING(QString, hotkeyRecord, setHotkeyRecord, "hotkeys/record", QStringLiteral("Meta+Shift+R"))
    U_SETTING(QString, hotkeyOcr, setHotkeyOcr, "hotkeys/ocrRegion", QStringLiteral("Meta+Shift+T"))
    U_SETTING(bool, showCapturePopup, setShowCapturePopup, "showCapturePopup", true)
    U_SETTING(QString, capturePopupPosition, setCapturePopupPosition, "capturePopupPosition", QStringLiteral("bottom-right"))
    // "casual" (full card) | "compact" (single slim row)
    U_SETTING(QString, capturePopupStyle, setCapturePopupStyle, "capturePopupStyle", QStringLiteral("casual"))
    U_SETTING(int, capturePopupDurationSec, setCapturePopupDurationSec, "capturePopupDurationSec", 8) // 0 = stay open
    // Skip the capture card while notifications are inhibited (a fullscreen app,
    // Do-Not-Disturb, or screen sharing). OFF by default — the card is feedback
    // for your own deliberate capture, so it should normally show regardless.
    U_SETTING(bool, muteOnFullscreen, setMuteOnFullscreen, "muteOnFullscreen", false)
    U_SETTING(QString, ocrLanguages, setOcrLanguages, "ocr/languages", QStringLiteral("pol+eng"))
    // Use U-2-Net for object cutout / background removal when the model is
    // available (only consulted in builds with onnxruntime). Default on.
    U_SETTING(bool, useU2Net, setUseU2Net, "segment/useU2Net", true)
    // Which saliency model drives cutout/background removal: a catalog id
    // (u2netp/u2net/u2net_human_seg/silueta/isnet-general-use) or "custom".
    U_SETTING(QString, segmentModel, setSegmentModel, "segment/model", QStringLiteral("u2netp"))
    // Absolute path of the user-provided .onnx (only used when model=="custom").
    U_SETTING(QString, segmentCustomModel, setSegmentCustomModel, "segment/customModelPath", QString())
    // Editor/overlay tool icons only (never the main app chrome): "custom" =
    // bundled monochrome glyphs, "system" = freedesktop QIcon::fromTheme.
    U_SETTING(QString, editorIconStyle, setEditorIconStyle, "ui/editorIconStyle", QStringLiteral("custom"))
    // Optional per-tool freedesktop icon-name overrides, JSON {"toolId":"name"}.
    U_SETTING(QString, editorToolIcons, setEditorToolIcons, "ui/editorToolIcons", QString())
    // UI language: "system" follows the OS locale, "en" forces English, "pl"
    // forces Polish. BARE top-level key (never a "general" group — see the
    // constructor's key-folding + AGENTS.md). Applied via QTranslator swap.
    U_SETTING(QString, uiLanguage, setUiLanguage, "uiLanguage", QStringLiteral("system"))
    // Main window chrome: true = system window decoration, false = the app's own
    // custom title bar (frameless).
    U_SETTING(bool, useSystemDecoration, setUseSystemDecoration, "ui/useSystemDecoration", true)
    // Custom system-tray icon (absolute path to a .png/.svg, or a bundled qrc
    // preset). Empty = bundled default. Applied live via QSystemTrayIcon::setIcon.
    U_SETTING(QString, trayIconPath, setTrayIconPath, "ui/trayIconPath", QString())
    // Daily GitHub release check + automatic AppImage self-install
    // (UpdateChecker). Suppressed in dev builds regardless of this value.
    U_SETTING(bool, autoCheckUpdates, setAutoCheckUpdates, "updates/autoCheck", true)

    // Raw access for settings export/import.
    QSettings *raw() { return &m_s; }
    void notifyAll()
    {
        emit saveDirectoryChanged(); emit videoSaveDirectoryChanged(); emit autoSaveChanged(); emit copyToClipboardChanged();
        emit openEditorChanged(); emit uploadAfterCaptureChanged(); emit includeCursorChanged();
        emit captureDelayMsChanged(); emit captureSoundChanged(); emit recordingSoundChanged(); emit gifFpsChanged(); emit gifMaxDurationSecChanged();
        emit gifQualityChanged(); emit activeDestinationChanged(); emit hotkeyFullScreenChanged();
        emit hotkeyRegionChanged(); emit hotkeyWindowChanged(); emit hotkeyGifChanged();
        emit imageFormatChanged(); emit imageQualityChanged(); emit filenameTemplateChanged();
        emit showNotificationsChanged(); emit minimizeToTrayOnCloseChanged(); emit openAfterSaveChanged();
        emit afterUploadCopyLinkChanged(); emit afterUploadOpenInBrowserChanged();
        emit editorStrokeColorChanged(); emit editorStrokeWidthChanged(); emit editorFontSizeChanged();
        emit editorFillColorChanged(); emit editorFillEnabledChanged(); emit recentColorsChanged();
        emit editorFontFamilyChanged(); emit editorFontBoldChanged(); emit editorFontItalicChanged();
        emit editorFontUnderlineChanged(); emit editorTextOutlineChanged(); emit editorTextOutlineColorChanged();
        emit editorTextBackgroundChanged(); emit editorTextBgColorChanged();
        emit editorResetColorsChanged(); emit editorResetToolsChanged();
        emit hiddenToolsChanged(); emit overlayToolbarPositionChanged(); emit selectionGuidesChanged(); emit smartPickChanged();
        emit captureOnReleaseChanged();
        emit hotkeyCopyLastChanged();
        emit videoFpsChanged(); emit videoFormatChanged(); emit videoQualityChanged();
        emit videoMaxDurationSecChanged(); emit hotkeyRecordChanged();
        emit hotkeyOcrChanged();
        emit recordSystemAudioChanged(); emit recordMicrophoneChanged();
        emit showCapturePopupChanged(); emit capturePopupPositionChanged();
        emit capturePopupDurationSecChanged(); emit capturePopupStyleChanged(); emit muteOnFullscreenChanged(); emit ocrLanguagesChanged();
        emit useU2NetChanged(); emit segmentModelChanged(); emit segmentCustomModelChanged();
        emit editorIconStyleChanged(); emit editorToolIconsChanged();
        emit uiLanguageChanged();
        emit useSystemDecorationChanged(); emit trayIconPathChanged();
        emit autoCheckUpdatesChanged();
    }

signals:
    void saveDirectoryChanged();
    void videoSaveDirectoryChanged();
    void autoSaveChanged();
    void copyToClipboardChanged();
    void openEditorChanged();
    void uploadAfterCaptureChanged();
    void includeCursorChanged();
    void captureDelayMsChanged();
    void captureSoundChanged();
    void recordingSoundChanged();
    void gifFpsChanged();
    void gifMaxDurationSecChanged();
    void gifQualityChanged();
    void activeDestinationChanged();
    void hotkeyFullScreenChanged();
    void hotkeyRegionChanged();
    void hotkeyWindowChanged();
    void hotkeyGifChanged();
    void imageFormatChanged();
    void imageQualityChanged();
    void filenameTemplateChanged();
    void showNotificationsChanged();
    void minimizeToTrayOnCloseChanged();
    void openAfterSaveChanged();
    void afterUploadCopyLinkChanged();
    void afterUploadOpenInBrowserChanged();
    void editorStrokeColorChanged();
    void editorStrokeWidthChanged();
    void editorFontSizeChanged();
    void editorFillColorChanged();
    void editorFillEnabledChanged();
    void editorFontFamilyChanged();
    void editorFontBoldChanged();
    void editorFontItalicChanged();
    void editorFontUnderlineChanged();
    void editorTextOutlineChanged();
    void editorTextOutlineColorChanged();
    void editorTextBackgroundChanged();
    void editorTextBgColorChanged();
    void editorResetColorsChanged();
    void editorResetToolsChanged();
    void recentColorsChanged();
    void hiddenToolsChanged();
    void overlayToolbarPositionChanged();
    void selectionGuidesChanged();
    void smartPickChanged();
    void captureOnReleaseChanged();
    void hotkeyCopyLastChanged();
    void videoFpsChanged();
    void videoFormatChanged();
    void videoQualityChanged();
    void videoMaxDurationSecChanged();
    void recordSystemAudioChanged();
    void recordMicrophoneChanged();
    void hotkeyRecordChanged();
    void hotkeyOcrChanged();
    void showCapturePopupChanged();
    void capturePopupPositionChanged();
    void capturePopupStyleChanged();
    void capturePopupDurationSecChanged();
    void muteOnFullscreenChanged();
    void ocrLanguagesChanged();
    void useU2NetChanged();
    void segmentModelChanged();
    void segmentCustomModelChanged();
    void editorIconStyleChanged();
    void editorToolIconsChanged();
    void uiLanguageChanged();
    void useSystemDecorationChanged();
    void trayIconPathChanged();
    void autoCheckUpdatesChanged();

private:
    QSettings m_s{UnisicConfig::filePath(), QSettings::IniFormat};
    QTimer m_syncTimer;
    bool m_writable = true;
};
