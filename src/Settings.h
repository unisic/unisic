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
    Q_PROPERTY(bool cursorHighlight READ cursorHighlight WRITE setCursorHighlight NOTIFY cursorHighlightChanged)
    Q_PROPERTY(bool cursorHighlightHalo READ cursorHighlightHalo WRITE setCursorHighlightHalo NOTIFY cursorHighlightHaloChanged)
    Q_PROPERTY(QString cursorHighlightColor READ cursorHighlightColor WRITE setCursorHighlightColor NOTIFY cursorHighlightColorChanged)
    Q_PROPERTY(bool cursorClickRipple READ cursorClickRipple WRITE setCursorClickRipple NOTIFY cursorClickRippleChanged)
    Q_PROPERTY(bool recordKeystrokes READ recordKeystrokes WRITE setRecordKeystrokes NOTIFY recordKeystrokesChanged)
    Q_PROPERTY(QString measureCopyFormat READ measureCopyFormat WRITE setMeasureCopyFormat NOTIFY measureCopyFormatChanged)
    Q_PROPERTY(int captureDelayMs READ captureDelayMs WRITE setCaptureDelayMs NOTIFY captureDelayMsChanged)
    Q_PROPERTY(QString captureSound READ captureSound WRITE setCaptureSound NOTIFY captureSoundChanged)
    Q_PROPERTY(QString recordingSound READ recordingSound WRITE setRecordingSound NOTIFY recordingSoundChanged)
    Q_PROPERTY(QString recordStartSound READ recordStartSound WRITE setRecordStartSound NOTIFY recordStartSoundChanged)
    Q_PROPERTY(int gifFps READ gifFps WRITE setGifFps NOTIFY gifFpsChanged)
    Q_PROPERTY(int gifMaxDurationSec READ gifMaxDurationSec WRITE setGifMaxDurationSec NOTIFY gifMaxDurationSecChanged)
    Q_PROPERTY(int gifQuality READ gifQuality WRITE setGifQuality NOTIFY gifQualityChanged)
    Q_PROPERTY(QString activeDestination READ activeDestination WRITE setActiveDestination NOTIFY activeDestinationChanged)
    Q_PROPERTY(QString hotkeyFullScreen READ hotkeyFullScreen WRITE setHotkeyFullScreen NOTIFY hotkeyFullScreenChanged)
    Q_PROPERTY(QString hotkeyRegion READ hotkeyRegion WRITE setHotkeyRegion NOTIFY hotkeyRegionChanged)
    Q_PROPERTY(QString hotkeyWindow READ hotkeyWindow WRITE setHotkeyWindow NOTIFY hotkeyWindowChanged)
    Q_PROPERTY(QString hotkeyGif READ hotkeyGif WRITE setHotkeyGif NOTIFY hotkeyGifChanged)
    Q_PROPERTY(QString lastCaptureRegion READ lastCaptureRegion WRITE setLastCaptureRegion NOTIFY lastCaptureRegionChanged)
    Q_PROPERTY(bool rememberRegion READ rememberRegion WRITE setRememberRegion NOTIFY rememberRegionChanged)
    Q_PROPERTY(QString fullscreenScope READ fullscreenScope WRITE setFullscreenScope NOTIFY fullscreenScopeChanged)
    Q_PROPERTY(QString fullScreenTask READ fullScreenTask WRITE setFullScreenTask NOTIFY fullScreenTaskChanged)
    Q_PROPERTY(QString regionTask READ regionTask WRITE setRegionTask NOTIFY regionTaskChanged)
    Q_PROPERTY(QString windowTask READ windowTask WRITE setWindowTask NOTIFY windowTaskChanged)
    Q_PROPERTY(QString fullScreenTaskDestination READ fullScreenTaskDestination WRITE setFullScreenTaskDestination NOTIFY fullScreenTaskDestinationChanged)
    Q_PROPERTY(QString regionTaskDestination READ regionTaskDestination WRITE setRegionTaskDestination NOTIFY regionTaskDestinationChanged)
    Q_PROPERTY(QString windowTaskDestination READ windowTaskDestination WRITE setWindowTaskDestination NOTIFY windowTaskDestinationChanged)
    Q_PROPERTY(QString imageFormat READ imageFormat WRITE setImageFormat NOTIFY imageFormatChanged)
    Q_PROPERTY(int imageQuality READ imageQuality WRITE setImageQuality NOTIFY imageQualityChanged)
    Q_PROPERTY(QString filenameTemplate READ filenameTemplate WRITE setFilenameTemplate NOTIFY filenameTemplateChanged)
    Q_PROPERTY(bool watermarkEnabled READ watermarkEnabled WRITE setWatermarkEnabled NOTIFY watermarkEnabledChanged)
    Q_PROPERTY(QString watermarkText READ watermarkText WRITE setWatermarkText NOTIFY watermarkTextChanged)
    Q_PROPERTY(int watermarkOpacity READ watermarkOpacity WRITE setWatermarkOpacity NOTIFY watermarkOpacityChanged)
    Q_PROPERTY(QString watermarkPosition READ watermarkPosition WRITE setWatermarkPosition NOTIFY watermarkPositionChanged)
    Q_PROPERTY(QString watermarkType READ watermarkType WRITE setWatermarkType NOTIFY watermarkTypeChanged)
    Q_PROPERTY(QString watermarkImagePath READ watermarkImagePath WRITE setWatermarkImagePath NOTIFY watermarkImagePathChanged)
    Q_PROPERTY(bool showNotifications READ showNotifications WRITE setShowNotifications NOTIFY showNotificationsChanged)
    // One-shot latch: the first-run system/dependency check pops at most once,
    // then this stays true. The "Run system check" button in Settings reopens it
    // on demand regardless.
    Q_PROPERTY(bool systemCheckSeen READ systemCheckSeen WRITE setSystemCheckSeen NOTIFY systemCheckSeenChanged)
    // Same one-shot latch for the first-run welcome screen (shown before the
    // dependency check, so two modals never stack).
    Q_PROPERTY(bool showWelcome READ showWelcome WRITE setShowWelcome NOTIFY showWelcomeChanged)
    Q_PROPERTY(bool minimizeToTrayOnClose READ minimizeToTrayOnClose WRITE setMinimizeToTrayOnClose NOTIFY minimizeToTrayOnCloseChanged)
    Q_PROPERTY(bool openAfterSave READ openAfterSave WRITE setOpenAfterSave NOTIFY openAfterSaveChanged)
    Q_PROPERTY(bool afterUploadCopyLink READ afterUploadCopyLink WRITE setAfterUploadCopyLink NOTIFY afterUploadCopyLinkChanged)
    Q_PROPERTY(bool afterUploadOpenInBrowser READ afterUploadOpenInBrowser WRITE setAfterUploadOpenInBrowser NOTIFY afterUploadOpenInBrowserChanged)
    Q_PROPERTY(bool doNotDisturbWhileCapturing READ doNotDisturbWhileCapturing WRITE setDoNotDisturbWhileCapturing NOTIFY doNotDisturbWhileCapturingChanged)
    Q_PROPERTY(bool externalActionEnabled READ externalActionEnabled WRITE setExternalActionEnabled NOTIFY externalActionEnabledChanged)
    Q_PROPERTY(QString externalActionCommand READ externalActionCommand WRITE setExternalActionCommand NOTIFY externalActionCommandChanged)
    Q_PROPERTY(QString editorStrokeColor READ editorStrokeColor WRITE setEditorStrokeColor NOTIFY editorStrokeColorChanged)
    Q_PROPERTY(int editorStrokeWidth READ editorStrokeWidth WRITE setEditorStrokeWidth NOTIFY editorStrokeWidthChanged)
    Q_PROPERTY(int editorHighlightMode READ editorHighlightMode WRITE setEditorHighlightMode NOTIFY editorHighlightModeChanged)
    Q_PROPERTY(QString lastSeenVersion READ lastSeenVersion WRITE setLastSeenVersion NOTIFY lastSeenVersionChanged)
    Q_PROPERTY(int editorFontSize READ editorFontSize WRITE setEditorFontSize NOTIFY editorFontSizeChanged)
    Q_PROPERTY(int editorStepSize READ editorStepSize WRITE setEditorStepSize NOTIFY editorStepSizeChanged)
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
    Q_PROPERTY(QString editorStylePresets READ editorStylePresets WRITE setEditorStylePresets NOTIFY editorStylePresetsChanged)
    Q_PROPERTY(QString hiddenTools READ hiddenTools WRITE setHiddenTools NOTIFY hiddenToolsChanged)
    Q_PROPERTY(QString overlayToolbarPosition READ overlayToolbarPosition WRITE setOverlayToolbarPosition NOTIFY overlayToolbarPositionChanged)
    Q_PROPERTY(bool selectionGuides READ selectionGuides WRITE setSelectionGuides NOTIFY selectionGuidesChanged)
    Q_PROPERTY(bool pixelLoupe READ pixelLoupe WRITE setPixelLoupe NOTIFY pixelLoupeChanged)
    Q_PROPERTY(int pixelLoupeZoom READ pixelLoupeZoom WRITE setPixelLoupeZoom NOTIFY pixelLoupeZoomChanged)
    Q_PROPERTY(bool captureOnRelease READ captureOnRelease WRITE setCaptureOnRelease NOTIFY captureOnReleaseChanged)
    Q_PROPERTY(QString hotkeyCopyLast READ hotkeyCopyLast WRITE setHotkeyCopyLast NOTIFY hotkeyCopyLastChanged)
    Q_PROPERTY(int videoFps READ videoFps WRITE setVideoFps NOTIFY videoFpsChanged)
    Q_PROPERTY(QString videoFormat READ videoFormat WRITE setVideoFormat NOTIFY videoFormatChanged)
    Q_PROPERTY(int videoQuality READ videoQuality WRITE setVideoQuality NOTIFY videoQualityChanged)
    Q_PROPERTY(int videoMaxDurationSec READ videoMaxDurationSec WRITE setVideoMaxDurationSec NOTIFY videoMaxDurationSecChanged)
    Q_PROPERTY(bool recordSystemAudio READ recordSystemAudio WRITE setRecordSystemAudio NOTIFY recordSystemAudioChanged)
    Q_PROPERTY(bool recordMicrophone READ recordMicrophone WRITE setRecordMicrophone NOTIFY recordMicrophoneChanged)
    Q_PROPERTY(QString recordAppAudioNode READ recordAppAudioNode WRITE setRecordAppAudioNode NOTIFY recordAppAudioNodeChanged)
    Q_PROPERTY(QString videoEncoder READ videoEncoder WRITE setVideoEncoder NOTIFY videoEncoderChanged)
    Q_PROPERTY(int instantReplaySeconds READ instantReplaySeconds WRITE setInstantReplaySeconds NOTIFY instantReplaySecondsChanged)
    Q_PROPERTY(QString hotkeyInstantReplay READ hotkeyInstantReplay WRITE setHotkeyInstantReplay NOTIFY hotkeyInstantReplayChanged)
    Q_PROPERTY(QString hotkeyRecord READ hotkeyRecord WRITE setHotkeyRecord NOTIFY hotkeyRecordChanged)
    Q_PROPERTY(QString hotkeyOcr READ hotkeyOcr WRITE setHotkeyOcr NOTIFY hotkeyOcrChanged)
    Q_PROPERTY(bool showCapturePopup READ showCapturePopup WRITE setShowCapturePopup NOTIFY showCapturePopupChanged)
    Q_PROPERTY(QString capturePopupPosition READ capturePopupPosition WRITE setCapturePopupPosition NOTIFY capturePopupPositionChanged)
    Q_PROPERTY(QString capturePopupStyle READ capturePopupStyle WRITE setCapturePopupStyle NOTIFY capturePopupStyleChanged)
    Q_PROPERTY(int capturePopupDurationSec READ capturePopupDurationSec WRITE setCapturePopupDurationSec NOTIFY capturePopupDurationSecChanged)
    Q_PROPERTY(int capturePopupMargin READ capturePopupMargin WRITE setCapturePopupMargin NOTIFY capturePopupMarginChanged)
    Q_PROPERTY(QString hiddenNotifActions READ hiddenNotifActions WRITE setHiddenNotifActions NOTIFY hiddenNotifActionsChanged)
    Q_PROPERTY(QString notificationActionOrder READ notificationActionOrder WRITE setNotificationActionOrder NOTIFY notificationActionOrderChanged)
    Q_PROPERTY(bool muteOnFullscreen READ muteOnFullscreen WRITE setMuteOnFullscreen NOTIFY muteOnFullscreenChanged)
    Q_PROPERTY(bool ocrAutoLanguage READ ocrAutoLanguage WRITE setOcrAutoLanguage NOTIFY ocrAutoLanguageChanged)
    Q_PROPERTY(QString ocrLanguages READ ocrLanguages WRITE setOcrLanguages NOTIFY ocrLanguagesChanged)
    Q_PROPERTY(QString editorIconStyle READ editorIconStyle WRITE setEditorIconStyle NOTIFY editorIconStyleChanged)
    Q_PROPERTY(QString editorToolIcons READ editorToolIcons WRITE setEditorToolIcons NOTIFY editorToolIconsChanged)
    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY uiLanguageChanged)
    Q_PROPERTY(bool useSystemDecoration READ useSystemDecoration WRITE setUseSystemDecoration NOTIFY useSystemDecorationChanged)
    Q_PROPERTY(QString trayIconPath READ trayIconPath WRITE setTrayIconPath NOTIFY trayIconPathChanged)
    Q_PROPERTY(bool autoCheckUpdates READ autoCheckUpdates WRITE setAutoCheckUpdates NOTIFY autoCheckUpdatesChanged)
    Q_PROPERTY(QString updateChannel READ updateChannel WRITE setUpdateChannel NOTIFY updateChannelChanged)
    Q_PROPERTY(int recordCountdownSec READ recordCountdownSec WRITE setRecordCountdownSec NOTIFY recordCountdownSecChanged)
    Q_PROPERTY(int soundVolume READ soundVolume WRITE setSoundVolume NOTIFY soundVolumeChanged)
    Q_PROPERTY(bool askWhereToSave READ askWhereToSave WRITE setAskWhereToSave NOTIFY askWhereToSaveChanged)
    Q_PROPERTY(bool stripMetadata READ stripMetadata WRITE setStripMetadata NOTIFY stripMetadataChanged)
    Q_PROPERTY(bool dateSubfolders READ dateSubfolders WRITE setDateSubfolders NOTIFY dateSubfoldersChanged)
    Q_PROPERTY(int filenameCounter READ filenameCounter WRITE setFilenameCounter NOTIFY filenameCounterChanged)
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
            // MUST list EVERY hotkey: the loop above only blanks hotkeys/* keys
            // physically present in stable's file, but a hotkey left at its code
            // default is never written there, so it would ship BOUND on a fresh
            // dev config and collide with the stable KGlobalAccel component.
            for (const char *hk : {"fullScreen", "region", "window", "gif", "record",
                                   "ocrRegion", "copyLast", "instantReplay"})
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
                qWarning() << "Settings file was corrupt - backed up to" << (f + ".corrupt")
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
        // The first-run setup flow is for people who have never used Unisic.
        // Its key is phrased the way a human editing this file by hand would
        // read it: showWelcome=true means "show it". On an UPGRADE the key is
        // absent (the config predates it), and absent means the default, which
        // would open the flow in the face of someone who has been using the app
        // for months. So decide once: any pre-existing key (including ones just
        // brought in by the dev seed or the legacy migration above) means this
        // is not a fresh install.
        //
        // Both outcomes are WRITTEN, so the question is asked exactly once. A
        // fresh install starts writing other keys within the same launch
        // (themesSeeded, a daemon-synced hotkey), so leaving the key absent
        // would make the next launch see a populated config and call the same
        // brand-new user an upgrade - losing the flow to anyone who quits
        // before finishing it.
        if (!m_s.contains(QStringLiteral("showWelcome"))) {
            // 0.7.4 dev builds wrote the inverted "welcomeSeen"; carry it over
            // rather than re-showing setup to someone who already dismissed it.
            if (m_s.contains(QStringLiteral("welcomeSeen"))) {
                m_s.setValue(QStringLiteral("showWelcome"),
                             !m_s.value(QStringLiteral("welcomeSeen")).toBool());
                m_s.remove(QStringLiteral("welcomeSeen"));
            } else {
                m_s.setValue(QStringLiteral("showWelcome"), m_s.allKeys().isEmpty());
            }
            m_s.sync();
        }
        // OCR auto-language defaults ON for fresh installs, but must not override
        // a spec an upgraded user deliberately pinned before this setting existed:
        // if ocr/languages was written (only happens when the user edited it) and
        // ocr/autoLanguage is absent, keep the manual spec by seeding it OFF.
        if (m_s.contains(QStringLiteral("ocr/languages"))
            && !m_s.contains(QStringLiteral("ocr/autoLanguage"))) {
            m_s.setValue(QStringLiteral("ocr/autoLanguage"), false);
            m_s.sync();
        }
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
            qWarning() << "Settings are NOT persisting - cannot write" << m_s.fileName()
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
    // Cursor overlay for recordings. Only has an effect together with
    // includeCursor: it switches the ScreenCast session to the portal's
    // metadata cursor mode, where the compositor stops drawing the pointer and
    // Unisic draws it (plus the halo and click ripples) itself. Off by default
    // — it changes what every recording looks like, and the metadata mode is
    // optional in the portal spec.
    U_SETTING(bool, cursorHighlight, setCursorHighlight, "record/cursorHighlight", false)
    U_SETTING(bool, cursorHighlightHalo, setCursorHighlightHalo, "record/cursorHighlightHalo", true)
    U_SETTING(QString, cursorHighlightColor, setCursorHighlightColor,
              "record/cursorHighlightColor", QStringLiteral("#FFD600"))
    // Click ripples additionally need read access to /dev/input (the `input`
    // group); without it the overlay silently keeps the halo and drops these.
    U_SETTING(bool, cursorClickRipple, setCursorClickRipple, "record/cursorClickRipple", true)
    // Keystroke badge in recordings (screenkey-style). Default OFF: it needs
    // /dev/input access (`input` group) and shows what the user types.
    U_SETTING(bool, recordKeystrokes, setRecordKeystrokes, "record/keystrokes", false)
    // What the ruler (Measure tool) copies on Ctrl+C: "readable" (842 × 317 /
    // 412 px), "plain" (842x317 / 412) or "css" (width: 842px; height: 317px).
    U_SETTING(QString, measureCopyFormat, setMeasureCopyFormat, "capture/measureCopyFormat", QStringLiteral("readable"))
    U_SETTING(int, captureDelayMs, setCaptureDelayMs, "captureDelayMs", 200)
    // Capture sound cue (General tab). Bare key (never a "general"-named
    // group, see the INI [%General] trap). "off" or a bundled id.
    U_SETTING(QString, captureSound, setCaptureSound, "captureSound", QStringLiteral("shutter"))
    // Separate cue for finished recordings/GIFs — a shutter makes no sense
    // after a minutes-long recording, and users may want it off independently.
    U_SETTING(QString, recordingSound, setRecordingSound, "recordingSound", QStringLiteral("ding"))
    // Cue played the moment recording actually begins (after the countdown), as
    // opposed to recordingSound which fires when encoding finishes. "off" or an id.
    U_SETTING(QString, recordStartSound, setRecordStartSound, "recordStartSound", QStringLiteral("beep"))
    U_SETTING(int, gifFps, setGifFps, "gif/fps", 15)
    U_SETTING(int, gifMaxDurationSec, setGifMaxDurationSec, "gif/maxDurationSec", 30)
    U_SETTING(int, gifQuality, setGifQuality, "gif/quality", 2)
    U_SETTING(QString, activeDestination, setActiveDestination, "upload/activeDestination", QStringLiteral("catbox.moe"))
    U_SETTING(QString, hotkeyFullScreen, setHotkeyFullScreen, "hotkeys/fullScreen", QStringLiteral("Meta+Shift+1"))
    U_SETTING(QString, hotkeyRegion, setHotkeyRegion, "hotkeys/region", QStringLiteral("Meta+Shift+2"))
    U_SETTING(QString, hotkeyWindow, setHotkeyWindow, "hotkeys/window", QStringLiteral("Meta+Shift+3"))
    U_SETTING(QString, hotkeyGif, setHotkeyGif, "hotkeys/gif", QStringLiteral("Meta+Shift+G"))
    // Last confirmed region capture ("<screen>|<x>,<y>,<w>,<h>", logical px) —
    // persisted so re-capture survives restarts like ShareX's repeat capture.
    U_SETTING(QString, lastCaptureRegion, setLastCaptureRegion, "capture/lastRegion", QString())
    // Region overlay opens with the last confirmed region already selected
    // (adjust or confirm straight away). Replaces the old re-capture hotkey.
    U_SETTING(bool, rememberRegion, setRememberRegion, "capture/rememberRegion", false)
    // What "full screen" captures: "workspace" = all monitors stitched
    // (default, the old behaviour), "screen" = only the monitor under the
    // cursor. Replaces the old screen-under-cursor hotkey; the tray menu and
    // `--monitor` still hit the single-screen path directly.
    U_SETTING(QString, fullscreenScope, setFullscreenScope, "capture/fullscreenScope", QStringLiteral("workspace"))
    U_SETTING(QString, fullScreenTask, setFullScreenTask, "tasks/fullScreen", QStringLiteral("default"))
    U_SETTING(QString, regionTask, setRegionTask, "tasks/region", QStringLiteral("default"))
    U_SETTING(QString, windowTask, setWindowTask, "tasks/window", QStringLiteral("default"))
    U_SETTING(QString, fullScreenTaskDestination, setFullScreenTaskDestination,
              "tasks/fullScreenDestination", QString())
    U_SETTING(QString, regionTaskDestination, setRegionTaskDestination,
              "tasks/regionDestination", QString())
    U_SETTING(QString, windowTaskDestination, setWindowTaskDestination,
              "tasks/windowDestination", QString())
    U_SETTING(QString, imageFormat, setImageFormat, "image/format", QStringLiteral("png"))
    U_SETTING(int, imageQuality, setImageQuality, "image/quality", 90)
    U_SETTING(QString, filenameTemplate, setFilenameTemplate, "image/filenameTemplate", QStringLiteral("Unisic_%date%_%time%"))
    // A small text stamp applied once to the captured image before the
    // independent save/copy/upload/history/editor fan-out. The settings stay
    // under image/ so export/import picks them up without the INI General trap.
    U_SETTING(bool, watermarkEnabled, setWatermarkEnabled, "image/watermarkEnabled", false)
    U_SETTING(QString, watermarkText, setWatermarkText, "image/watermarkText", QStringLiteral("Unisic"))
    U_SETTING(int, watermarkOpacity, setWatermarkOpacity, "image/watermarkOpacity", 75)
    U_SETTING(QString, watermarkPosition, setWatermarkPosition, "image/watermarkPosition", QStringLiteral("bottom-right"))
    U_SETTING(QString, watermarkType, setWatermarkType, "image/watermarkType", QStringLiteral("text"))
    U_SETTING(QString, watermarkImagePath, setWatermarkImagePath, "image/watermarkImagePath", QString())
    U_SETTING(bool, showNotifications, setShowNotifications, "showNotifications", true)
    U_SETTING(bool, systemCheckSeen, setSystemCheckSeen, "systemCheckSeen", false)
    U_SETTING(bool, showWelcome, setShowWelcome, "showWelcome", true)
    U_SETTING(bool, minimizeToTrayOnClose, setMinimizeToTrayOnClose, "minimizeToTrayOnClose", true)
    U_SETTING(bool, openAfterSave, setOpenAfterSave, "openAfterSave", false)
    U_SETTING(bool, afterUploadCopyLink, setAfterUploadCopyLink, "upload/afterUploadCopyLink", true)
    U_SETTING(bool, afterUploadOpenInBrowser, setAfterUploadOpenInBrowser, "upload/afterUploadOpenInBrowser", false)
    U_SETTING(bool, doNotDisturbWhileCapturing, setDoNotDisturbWhileCapturing,
              "capture/doNotDisturb", false)
    U_SETTING(bool, externalActionEnabled, setExternalActionEnabled,
              "actions/enabled", false)
    U_SETTING(QString, externalActionCommand, setExternalActionCommand,
              "actions/command", QString())
    U_SETTING(QString, editorStrokeColor, setEditorStrokeColor, "editor/strokeColor", QStringLiteral("#FF4757"))
    U_SETTING(int, editorStrokeWidth, setEditorStrokeWidth, "editor/strokeWidth", 4)
    // Highlighter sub-mode: 0 freehand marker, 1 rectangle band, 2 text-snap
    // (default: text-aware, matching the pre-mode behaviour).
    U_SETTING(int, editorHighlightMode, setEditorHighlightMode, "editor/highlightMode", 2)
    // App version whose release notes the user has already seen. When it differs
    // from the running version, Main.qml blinks a "See patch notes" hint at the
    // version label. Empty on a fresh install.
    U_SETTING(QString, lastSeenVersion, setLastSeenVersion, "ui/lastSeenVersion", QString())
    U_SETTING(int, editorFontSize, setEditorFontSize, "editor/fontSize", 22)
    U_SETTING(int, editorStepSize, setEditorStepSize, "editor/stepSize", 22)
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
    // Saved annotation-style presets: a JSON array of objects, each holding the
    // canvas style properties captured when the preset was saved. One opaque
    // string rather than a key per field so the QML side owns the schema and a
    // preset can gain a property without a settings migration.
    U_SETTING(QString, editorStylePresets, setEditorStylePresets, "editor/stylePresets", QString())
    U_SETTING(QString, hiddenTools, setHiddenTools, "editor/hiddenTools", QString())
    U_SETTING(QString, overlayToolbarPosition, setOverlayToolbarPosition, "capture/overlayToolbarPosition", QStringLiteral("follow"))
    // Crosshair guide lines from the cursor to the screen edges while selecting a
    // region (screenshot AND recording overlay). Off by default.
    U_SETTING(bool, selectionGuides, setSelectionGuides, "capture/selectionGuides", false)
    // Pixel loupe on the region overlay: a magnifier by the cursor
    // showing the exact pixel the selection edge will land on. Zoom is the
    // magnification factor (5–16), adjusted live by scrolling; scroll out
    // below 5 to hide the loupe.
    U_SETTING(bool, pixelLoupe, setPixelLoupe, "capture/pixelLoupe", true)
    U_SETTING(int, pixelLoupeZoom, setPixelLoupeZoom, "capture/pixelLoupeZoom", 8)
    // Region overlay: a plain CLICK selects the detected object (window,
    // panel, image) under the cursor; dragging still draws a manual rect.
    // EXPERIMENTAL (default off): pure-pixel detection cannot recognize every
    // window/element reliably without heavy vision libraries.
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
    U_SETTING(QString, recordAppAudioNode, setRecordAppAudioNode,
              "audio/recordAppAudioNode", QString())
    U_SETTING(QString, videoEncoder, setVideoEncoder, "video/encoder", QStringLiteral("auto"))
    U_SETTING(int, instantReplaySeconds, setInstantReplaySeconds,
              "video/instantReplaySeconds", 30)
    U_SETTING(QString, hotkeyInstantReplay, setHotkeyInstantReplay,
              "hotkeys/instantReplay", QStringLiteral("Meta+Shift+I"))
    U_SETTING(QString, hotkeyRecord, setHotkeyRecord, "hotkeys/record", QStringLiteral("Meta+Shift+R"))
    U_SETTING(QString, hotkeyOcr, setHotkeyOcr, "hotkeys/ocrRegion", QStringLiteral("Meta+Shift+T"))
    U_SETTING(bool, showCapturePopup, setShowCapturePopup, "showCapturePopup", true)
    U_SETTING(QString, capturePopupPosition, setCapturePopupPosition, "capturePopupPosition", QStringLiteral("bottom-right"))
    // "casual" (full card) | "compact" (single slim row)
    U_SETTING(QString, capturePopupStyle, setCapturePopupStyle, "capturePopupStyle", QStringLiteral("casual"))
    U_SETTING(int, capturePopupDurationSec, setCapturePopupDurationSec, "capturePopupDurationSec", 8) // 0 = stay open
    // Gap between the card and the screen edge, in logical px. A setting because
    // panel geometry is not knowable on Wayland: layer-shell keeps the card clear
    // of panels by itself (exclusive zones), but the XWayland card path can only
    // read _NET_WORKAREA — and neither covers a dock the user wants extra room
    // for. 8 = the previous hardcoded inset, so the default changes nothing.
    U_SETTING(int, capturePopupMargin, setCapturePopupMargin, "capturePopupMargin", 8)
    // CSV of action ids the capture card must NOT offer ("upload,ocr,delete"),
    // same opt-out shape as hiddenTools. Empty = show everything the capture
    // actually supports; the per-action conditions (a URL exists, OCR is built
    // in, …) still apply on top — this only ever removes buttons.
    U_SETTING(QString, hiddenNotifActions, setHiddenNotifActions, "hiddenNotifActions", QString())
    // Stable action ids in display order. CSV keeps this human-editable beside
    // hiddenNotifActions and lets newer builds append action ids an older config
    // did not know without a schema migration.
    U_SETTING(QString, notificationActionOrder, setNotificationActionOrder,
              "notificationActionOrder",
              QStringLiteral("edit,copy,link,qr,folder,upload,ocr,trim,delete"))
    // Skip the capture card while notifications are inhibited (a fullscreen app,
    // Do-Not-Disturb, or screen sharing). OFF by default — the card is feedback
    // for your own deliberate capture, so it should normally show regardless.
    U_SETTING(bool, muteOnFullscreen, setMuteOnFullscreen, "muteOnFullscreen", false)
    // Auto-detect OCR languages: recognize using every installed Tesseract
    // langpack (osd/equ dropped) instead of the fixed `ocrLanguages` spec. ON by
    // default so OCR works without the user knowing Tesseract language codes;
    // turn it off to pin a specific, faster spec.
    U_SETTING(bool, ocrAutoLanguage, setOcrAutoLanguage, "ocr/autoLanguage", true)
    U_SETTING(QString, ocrLanguages, setOcrLanguages, "ocr/languages", QStringLiteral("pol+eng"))
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
    U_SETTING(bool, useSystemDecoration, setUseSystemDecoration, "ui/useSystemDecoration", false)
    // Custom system-tray icon (absolute path to a .png/.svg, or a bundled qrc
    // preset). Empty = bundled default. Applied live via QSystemTrayIcon::setIcon.
    U_SETTING(QString, trayIconPath, setTrayIconPath, "ui/trayIconPath", QString())
    // Daily GitHub release check + automatic AppImage self-install
    // (UpdateChecker). Suppressed in dev builds regardless of this value.
    U_SETTING(bool, autoCheckUpdates, setAutoCheckUpdates, "updates/autoCheck", true)
    // Release channel for the update check: "stable" = latest full release,
    // "beta" = newest release including pre-releases (GitHub /releases[0]).
    U_SETTING(QString, updateChannel, setUpdateChannel, "updates/channel", QStringLiteral("stable"))
    // Countdown (seconds) shown before a recording actually starts, giving you
    // time to arrange windows. 0 = start immediately. Screenshots are unaffected.
    U_SETTING(int, recordCountdownSec, setRecordCountdownSec, "record/countdownSec", 0)
    // Playback volume (0-100) for the capture/recording sound cues. 100 = the
    // sample's own level; 0 skips playback entirely.
    U_SETTING(int, soundVolume, setSoundVolume, "soundVolume", 100)
    // When saving is enabled, prompt for a destination path per capture instead
    // of writing straight into saveDirectory. Off by default.
    U_SETTING(bool, askWhereToSave, setAskWhereToSave, "askWhereToSave", false)
    // Strip image metadata (text chunks, DPI, description) from saved files —
    // captures carry none by default, so this guarantees a clean PNG/JPEG. On.
    U_SETTING(bool, stripMetadata, setStripMetadata, "image/stripMetadata", true)
    // Organise saved screenshots into per-month subfolders (yyyy-MM) under the
    // save directory. Off by default.
    U_SETTING(bool, dateSubfolders, setDateSubfolders, "image/dateSubfolders", false)
    // Monotonic counter backing the %i% filename token; incremented once per
    // saved capture. Persisted so numbering survives restarts.
    U_SETTING(int, filenameCounter, setFilenameCounter, "image/filenameCounter", 1)

    // Raw access for settings export/import.
    QSettings *raw() { return &m_s; }
    void notifyAll()
    {
        emit saveDirectoryChanged(); emit videoSaveDirectoryChanged(); emit autoSaveChanged(); emit copyToClipboardChanged();
        emit openEditorChanged(); emit uploadAfterCaptureChanged(); emit includeCursorChanged();
        emit cursorHighlightChanged(); emit cursorHighlightHaloChanged();
        emit cursorHighlightColorChanged(); emit cursorClickRippleChanged();
        emit recordKeystrokesChanged();
        emit measureCopyFormatChanged();
        emit captureDelayMsChanged(); emit captureSoundChanged(); emit recordingSoundChanged(); emit recordStartSoundChanged(); emit gifFpsChanged(); emit gifMaxDurationSecChanged();
        emit gifQualityChanged(); emit activeDestinationChanged(); emit hotkeyFullScreenChanged();
        emit hotkeyRegionChanged(); emit hotkeyWindowChanged(); emit hotkeyGifChanged();
        emit lastCaptureRegionChanged(); emit rememberRegionChanged(); emit fullscreenScopeChanged();
        emit fullScreenTaskChanged(); emit regionTaskChanged(); emit windowTaskChanged();
        emit fullScreenTaskDestinationChanged(); emit regionTaskDestinationChanged(); emit windowTaskDestinationChanged();
        emit imageFormatChanged(); emit imageQualityChanged(); emit filenameTemplateChanged();
        emit watermarkEnabledChanged(); emit watermarkTextChanged(); emit watermarkOpacityChanged(); emit watermarkPositionChanged();
        emit watermarkTypeChanged(); emit watermarkImagePathChanged();
        emit showNotificationsChanged(); emit systemCheckSeenChanged(); emit showWelcomeChanged();
        emit minimizeToTrayOnCloseChanged(); emit openAfterSaveChanged();
        emit afterUploadCopyLinkChanged(); emit afterUploadOpenInBrowserChanged();
        emit doNotDisturbWhileCapturingChanged();
        emit externalActionEnabledChanged(); emit externalActionCommandChanged();
        emit editorStrokeColorChanged(); emit editorStrokeWidthChanged(); emit editorFontSizeChanged();
        emit editorHighlightModeChanged(); emit lastSeenVersionChanged();
        emit editorStepSizeChanged();
        emit editorFillColorChanged(); emit editorFillEnabledChanged(); emit recentColorsChanged();
        emit editorStylePresetsChanged();
        emit editorFontFamilyChanged(); emit editorFontBoldChanged(); emit editorFontItalicChanged();
        emit editorFontUnderlineChanged(); emit editorTextOutlineChanged(); emit editorTextOutlineColorChanged();
        emit editorTextBackgroundChanged(); emit editorTextBgColorChanged();
        emit editorResetColorsChanged(); emit editorResetToolsChanged();
        emit hiddenToolsChanged(); emit overlayToolbarPositionChanged(); emit selectionGuidesChanged();
        emit pixelLoupeChanged(); emit pixelLoupeZoomChanged();
        emit captureOnReleaseChanged();
        emit hotkeyCopyLastChanged();
        emit videoFpsChanged(); emit videoFormatChanged(); emit videoQualityChanged();
        emit videoMaxDurationSecChanged(); emit hotkeyRecordChanged();
        emit hotkeyOcrChanged();
        emit recordSystemAudioChanged(); emit recordMicrophoneChanged();
        emit recordAppAudioNodeChanged(); emit videoEncoderChanged();
        emit instantReplaySecondsChanged(); emit hotkeyInstantReplayChanged();
        emit showCapturePopupChanged(); emit capturePopupPositionChanged();
        emit capturePopupDurationSecChanged(); emit capturePopupStyleChanged(); emit capturePopupMarginChanged();
        emit hiddenNotifActionsChanged(); emit notificationActionOrderChanged();
        emit muteOnFullscreenChanged(); emit ocrLanguagesChanged();
        emit editorIconStyleChanged(); emit editorToolIconsChanged();
        emit uiLanguageChanged();
        emit useSystemDecorationChanged(); emit trayIconPathChanged();
        emit autoCheckUpdatesChanged();
        emit updateChannelChanged(); emit recordCountdownSecChanged(); emit soundVolumeChanged();
        emit askWhereToSaveChanged(); emit stripMetadataChanged(); emit dateSubfoldersChanged();
        emit filenameCounterChanged();
    }

signals:
    void saveDirectoryChanged();
    void videoSaveDirectoryChanged();
    void autoSaveChanged();
    void copyToClipboardChanged();
    void openEditorChanged();
    void uploadAfterCaptureChanged();
    void includeCursorChanged();
    void cursorHighlightChanged();
    void cursorHighlightHaloChanged();
    void cursorHighlightColorChanged();
    void cursorClickRippleChanged();
    void recordKeystrokesChanged();
    void measureCopyFormatChanged();
    void captureDelayMsChanged();
    void captureSoundChanged();
    void recordingSoundChanged();
    void recordStartSoundChanged();
    void gifFpsChanged();
    void gifMaxDurationSecChanged();
    void gifQualityChanged();
    void activeDestinationChanged();
    void hotkeyFullScreenChanged();
    void hotkeyRegionChanged();
    void hotkeyWindowChanged();
    void hotkeyGifChanged();
    void lastCaptureRegionChanged();
    void rememberRegionChanged();
    void fullscreenScopeChanged();
    void fullScreenTaskChanged();
    void regionTaskChanged();
    void windowTaskChanged();
    void fullScreenTaskDestinationChanged();
    void regionTaskDestinationChanged();
    void windowTaskDestinationChanged();
    void imageFormatChanged();
    void imageQualityChanged();
    void filenameTemplateChanged();
    void watermarkEnabledChanged();
    void watermarkTextChanged();
    void watermarkOpacityChanged();
    void watermarkPositionChanged();
    void watermarkTypeChanged();
    void watermarkImagePathChanged();
    void showNotificationsChanged();
    void systemCheckSeenChanged();
    void showWelcomeChanged();
    void minimizeToTrayOnCloseChanged();
    void openAfterSaveChanged();
    void afterUploadCopyLinkChanged();
    void afterUploadOpenInBrowserChanged();
    void doNotDisturbWhileCapturingChanged();
    void externalActionEnabledChanged();
    void externalActionCommandChanged();
    void editorStrokeColorChanged();
    void editorStrokeWidthChanged();
    void editorHighlightModeChanged();
    void lastSeenVersionChanged();
    void editorFontSizeChanged();
    void editorStepSizeChanged();
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
    void editorStylePresetsChanged();
    void hiddenToolsChanged();
    void overlayToolbarPositionChanged();
    void selectionGuidesChanged();
    void pixelLoupeChanged();
    void pixelLoupeZoomChanged();
    void captureOnReleaseChanged();
    void hotkeyCopyLastChanged();
    void videoFpsChanged();
    void videoFormatChanged();
    void videoQualityChanged();
    void videoMaxDurationSecChanged();
    void recordSystemAudioChanged();
    void recordMicrophoneChanged();
    void recordAppAudioNodeChanged();
    void videoEncoderChanged();
    void instantReplaySecondsChanged();
    void hotkeyInstantReplayChanged();
    void hotkeyRecordChanged();
    void hotkeyOcrChanged();
    void showCapturePopupChanged();
    void capturePopupPositionChanged();
    void capturePopupStyleChanged();
    void capturePopupDurationSecChanged();
    void capturePopupMarginChanged();
    void hiddenNotifActionsChanged();
    void notificationActionOrderChanged();
    void muteOnFullscreenChanged();
    void ocrAutoLanguageChanged();
    void ocrLanguagesChanged();
    void editorIconStyleChanged();
    void editorToolIconsChanged();
    void uiLanguageChanged();
    void useSystemDecorationChanged();
    void trayIconPathChanged();
    void autoCheckUpdatesChanged();
    void updateChannelChanged();
    void recordCountdownSecChanged();
    void soundVolumeChanged();
    void askWhereToSaveChanged();
    void stripMetadataChanged();
    void dateSubfoldersChanged();
    void filenameCounterChanged();

private:
    QSettings m_s{UnisicConfig::filePath(), QSettings::IniFormat};
    QTimer m_syncTimer;
    bool m_writable = true;
};
