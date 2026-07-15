#pragma once
#include <QObject>
#include <QImage>
#include <QColor>
#include <QRect>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <functional>
#include <atomic>
#include <memory>
#include <qqmlregistration.h>
#include "ocr/OcrWord.h"

#include "Settings.h"
#include "upload/UploadManager.h"
#include "history/HistoryStore.h"
#include "record/GifRecorder.h"
#include "update/UpdateChecker.h"

class QQmlEngine;
class QMenu;
class QIcon;
class QSystemTrayIcon;
class QTranslator;
class QDBusServiceWatcher;
class QFileSystemWatcher;
class QQuickWindow;
class QTimer;
class QProcess;
class CaptureManager;
class OverlayController;
class GlobalHotkeys;
class PortalGlobalShortcuts;
class GifRecorder;
class OcrEngine;
class AnnotationCanvas;
class CaptureNotification;
class DesktopNotifier;
class NotificationInhibitor;
class ExternalActionRunner;
class PreviewController;
class LayerShellNotifier;

// Application facade exposed to QML as the "App" context property.
// Owns every subsystem and implements the after-capture pipeline
// (save / clipboard / editor / upload / history).
class AppContext : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Context property")

    Q_PROPERTY(Settings *settings READ settings CONSTANT)
    Q_PROPERTY(UploadManager *uploads READ uploads CONSTANT)
    Q_PROPERTY(HistoryStore *history READ history CONSTANT)
    Q_PROPERTY(UpdateChecker *updater READ updater CONSTANT)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool converting READ converting NOTIFY recordingChanged)
    Q_PROPERTY(bool instantReplayActive READ instantReplayActive NOTIFY recordingChanged)
    Q_PROPERTY(int recordSeconds READ recordSeconds NOTIFY recordSecondsChanged)
    Q_PROPERTY(bool recordingAvailable READ recordingAvailable NOTIFY recordingAvailableChanged)
    // Why recording is off, so the UI can name the actual missing piece. The two
    // causes are unrelated: capPipeWireBuild is compile-time (the package was
    // built without pipewire-devel), capScreenCastPortal is runtime (the desktop
    // ships no org.freedesktop.impl.portal.ScreenCast backend — Cinnamon, MATE
    // and XFCE run -xapp, LXQt runs -lxqt; neither implements ScreenCast). A
    // running pipewire daemon says nothing about the latter: it serves audio on
    // those desktops with no screen-cast portal in sight.
    Q_PROPERTY(bool capPipeWireBuild READ capPipeWireBuild CONSTANT)
    Q_PROPERTY(bool capScreenCastPortal READ capScreenCastPortal NOTIFY recordingAvailableChanged)
    // No StatusNotifier host (GNOME without the AppIndicator extension, bare
    // wlroots): close must actually close, not hide into a tray that isn't there.
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY trayAvailableChanged)
    // True while a hotkey recorder in Settings is capturing a key combo. The
    // built-in window Shortcuts (Ctrl+W/Q/1-6/… in Main.qml) gate on this
    // (`enabled: !App.shortcutRecording`) so they never steal the combo the
    // user is trying to bind — otherwise Ctrl+Q would quit the app mid-bind.
    Q_PROPERTY(bool shortcutRecording READ shortcutRecording NOTIFY shortcutRecordingChanged)
    // True when the capture card is shown on the layer-shell overlay (KWin/wlroots)
    // rather than as a native notification — lets the UI expose card-only options
    // (e.g. corner position, which a native notification server controls itself).
    Q_PROPERTY(bool layerShellActive READ layerShellActive CONSTANT)
    // Developer build (F8 smoke test; capability options stay editable). False in
    // a release build, where unsupported options are shown greyed-out instead.
    Q_PROPERTY(bool devBuild READ devBuild CONSTANT)
    // Compositor capabilities, resolved once at startup. The UI uses them to
    // enable/grey the matching options. capCustomNotification rides on
    // layer-shell (KWin/wlroots/COSMIC); capRecordBorder additionally covers the
    // KWin fullscreen fallback, plain X11 sessions and the XWayland helper
    // (GNOME); capNativeNotification on a running notification server (absent
    // on e.g. a bare Sway).
    Q_PROPERTY(bool capNativeNotification READ capNativeNotification CONSTANT)
    Q_PROPERTY(bool capCustomNotification READ capCustomNotification CONSTANT)
    Q_PROPERTY(bool capRecordBorder READ capRecordBorder CONSTANT)
    Q_PROPERTY(bool capDoNotDisturb READ capDoNotDisturb CONSTANT)
    Q_PROPERTY(bool capScreenshotCursor READ capScreenshotCursor CONSTANT)
    // QtMultimedia QML module present → the trim editor shows a live video
    // preview; otherwise it degrades to the slider-only range picker.
    Q_PROPERTY(bool capVideoPlayback READ capVideoPlayback CONSTANT)
    Q_PROPERTY(QString smokeTestLog READ smokeTestLog NOTIFY smokeTestChanged)
    Q_PROPERTY(bool smokeTestRunning READ smokeTestRunning NOTIFY smokeTestChanged)
    // Reflects an XDG autostart .desktop in ~/.config/autostart. WRITE creates
    // or removes that file (Exec carries --tray-only so login starts hidden).
    Q_PROPERTY(bool autostartEnabled READ autostartEnabled WRITE setAutostartEnabled NOTIFY autostartEnabledChanged)
    // Absolute paths of user-supplied tray icons dropped into trayIconsDir();
    // the settings gallery lists these as pickable presets (live via a watcher).
    Q_PROPERTY(QStringList trayIconPresets READ trayIconPresets NOTIFY trayIconPresetsChanged)
    // App-shipped tray icons (qrc ":/resources/icons/tray/*"), fixed at build.
    Q_PROPERTY(QStringList bundledTrayIcons READ bundledTrayIcons CONSTANT)
    // Contrast colour for the (monochrome) bundled presets: light on a dark
    // system scheme, dark on a light one. Follows the OS light/dark, live.
    Q_PROPERTY(QColor trayContrastColor READ trayContrastColor NOTIFY trayContrastColorChanged)
    // Open post-capture editors — quit-on-close must not destroy unsaved work.
    Q_PROPERTY(int editorWindowsOpen READ editorWindowsOpen NOTIFY editorWindowsOpenChanged)
    Q_PROPERTY(bool ocrAvailable READ ocrAvailable CONSTANT)
    Q_PROPERTY(bool qrAvailable READ qrAvailable CONSTANT)   // zxing-cpp compiled in
    Q_PROPERTY(bool vaapiAvailable READ vaapiAvailable NOTIFY recordingCapabilitiesChanged)
    Q_PROPERTY(bool nvencAvailable READ nvencAvailable NOTIFY recordingCapabilitiesChanged)
    Q_PROPERTY(bool perAppAudioAvailable READ perAppAudioAvailable CONSTANT)
    // A working global-hotkey backend? KGlobalAccel on KDE, the GlobalShortcuts
    // portal elsewhere; false (niri/sway…) switches the Hotkeys settings tab
    // to the compositor-binds explanation instead of dead recorders.
    Q_PROPERTY(bool hotkeysAvailable READ hotkeysAvailable NOTIFY hotkeysAvailableChanged)
    // "kglobalaccel" | "portal" | "" — lets the UI tailor its hints.
    Q_PROPERTY(QString hotkeyBackend READ hotkeyBackend NOTIFY hotkeysAvailableChanged)
    Q_PROPERTY(QString toastText READ toastText NOTIFY toastChanged)
    // Baked in at compile time (CMake): semantic version + CI build number
    // ("dev" for local builds) + git-commit date of the built state
    // (YYYYMMDD-HHMM, empty when unknown). Shown in the sidebar footer.
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QString buildNumber READ buildNumber CONSTANT)
    Q_PROPERTY(QString buildDate READ buildDate CONSTANT)
    // True until the user opens the release notes for the RUNNING version: drives
    // the blinking "See patch notes" hint pointing at the version label after an
    // update. Cleared by markPatchNotesSeen().
    Q_PROPERTY(bool patchNotesUnseen READ patchNotesUnseen NOTIFY patchNotesUnseenChanged)

public:
    using UploadDone = std::function<void(const QString &url, const QString &error)>;

    explicit AppContext(QObject *parent = nullptr);
    ~AppContext() override;

    void initialize(QQmlEngine *engine);

    Settings *settings() const { return m_settings; }
    UploadManager *uploads() const { return m_uploads; }
    HistoryStore *history() const { return m_history; }
    UpdateChecker *updater() const { return m_updater; }
    CaptureManager *captureManager() const { return m_capture; }
    QQmlEngine *qmlEngine() const { return m_engine; }

    bool recording() const;
    bool converting() const;
    bool instantReplayActive() const { return m_recorder->instantReplayActive(); }
    int recordSeconds() const;
    bool recordingAvailable() const;
    bool capPipeWireBuild() const;
    bool capScreenCastPortal() const;
    bool trayAvailable() const { return m_tray != nullptr; }
    bool shortcutRecording() const { return m_shortcutRecording; }
    bool layerShellActive() const { return m_layerNotifier != nullptr; }
    bool devBuild() const
    {
#ifdef UNISIC_DEV_BUILD
        return true;
#else
        return false;
#endif
    }
    bool capNativeNotification() const;
    bool capCustomNotification() const; // layer-shell OR the GNOME XWayland card
    bool capRecordBorder() const;
    bool capVideoPlayback() const;
    // GNOME/mutter (Wayland, no layer-shell, no KWin) with an X socket: the
    // styled capture card rides the --notification-helper XWayland process.
    bool capNotificationHelper() const;
    bool capDoNotDisturb() const;
    bool capScreenshotCursor() const;
    // True when the compositor exposes wlr-layer-shell — the selection overlay
    // uses it so it can appear ABOVE a fullscreen application.
    bool layerShellAvailable() const { return m_layerShellAvailable; }
    // Developer smoke-test: sequentially exercises the main app paths and logs
    // pass/fail/skip. Bound to F8 (dev build only) and the Settings > Developer
    // button. smokeTestLog is the running transcript shown in the debug panel.
    Q_INVOKABLE void runSmokeTest();
    // Per-action manual triggers for the Developer tab (dev build only): each
    // exercises ONE path in isolation. Capture/record reuse the public entry
    // points; these three cover the non-capture paths with a throwaway image.
    Q_INVOKABLE void devTestNotification();
    // Drives the settings hover preview without a pointer: show, then withdraw
    // after 3 s exactly as leaving the row does.
    Q_INVOKABLE void devTestCardPreview();
    Q_INVOKABLE void devTestEditor();
    Q_INVOKABLE void devTestHistory();
    Q_INVOKABLE void devTestFavoriteHistory();
    Q_INVOKABLE void devTestEditFromHistory();
    Q_INVOKABLE void devTestHistoryDrag();
    Q_INVOKABLE void devTestNotificationDrag();
    Q_INVOKABLE void devTestCopyLast();
    Q_INVOKABLE void devTestRecordBorder();
    Q_INVOKABLE void devTestPreview();
    Q_INVOKABLE void devTestPreviewFromHistory();
    Q_INVOKABLE void devTestHotkeyBinds();
    Q_INVOKABLE void devTestUpload();
    Q_INVOKABLE void devTestSettingsRoundTrip();
    Q_INVOKABLE void devTestCaptureSound();
    Q_INVOKABLE void devTestRecordingSound();
    Q_INVOKABLE void devTestRecordStartSound();
    Q_INVOKABLE void devTestTrashSound();
    Q_INVOKABLE void devTestAltHotkeys();
    Q_INVOKABLE void devTestTextRender();
    Q_INVOKABLE void devTestShapeEdit();
    Q_INVOKABLE void devTestCaptureOnRelease();
    Q_INVOKABLE void devTestOcrBoxes();
    Q_INVOKABLE void devTestOcrHighlight();
    Q_INVOKABLE void devTestClipboardPaste();
    Q_INVOKABLE void devTestCaptureDelay();
    Q_INVOKABLE void devTestCopyAs();
    Q_INVOKABLE void devTestWatermark();
    Q_INVOKABLE void devTestCallout();
    Q_INVOKABLE void devTestShiftSnap();
    Q_INVOKABLE void devTestQrPreview();
    Q_INVOKABLE void devTestDoNotDisturb();
    Q_INVOKABLE void devTestExternalAction();
    Q_INVOKABLE void devTestTaskPreset();
    Q_INVOKABLE void devTestCliOutput();
    Q_INVOKABLE void devTestMeasureTools();
    Q_INVOKABLE void devTestHardwareEncoder();
    Q_INVOKABLE void devTestPerAppAudio();
    Q_INVOKABLE void devTestInstantReplay();
    Q_INVOKABLE void devTestTrimRecording();
    Q_INVOKABLE void devTestCursorCapability();
    Q_INVOKABLE void devTestLanguage();
    Q_INVOKABLE void devTestUpdateCheck();
    Q_INVOKABLE void devTestUpdateAvailable();
    Q_INVOKABLE void devTestAutoRestart();
    Q_INVOKABLE void devTestCountdown();
    Q_INVOKABLE void devTestSaveDialog();
    Q_INVOKABLE void devTestFilename();
    QString smokeTestLog() const { return m_smokeLog; }
    bool smokeTestRunning() const { return m_smokeRunning; }
    int editorWindowsOpen() const { return m_editorWindows; }
    bool ocrAvailable() const;
    bool qrAvailable() const;
    bool vaapiAvailable() const { return m_vaapiAvailable; }
    bool nvencAvailable() const { return m_nvencAvailable; }
    bool perAppAudioAvailable() const;
    Q_INVOKABLE QVariantList audioApplicationNodes() const; // synchronous (dev/smoke only)
    // Async variant for the UI: enumerates off-thread and emits
    // audioApplicationNodesReady so opening the audio dropdown never blocks.
    Q_INVOKABLE void requestAudioApplicationNodes();
    // Install the QTranslators for the current uiLanguage setting (swapping any
    // previously installed ones). Called once before the engine loads and again
    // whenever the language setting changes (live retranslate + tray rebuild).
    Q_INVOKABLE void applyLanguage();
    bool hotkeysAvailable() const;
    QString hotkeyBackend() const { return m_hotkeyBackend; }
    bool autostartEnabled() const;
    void setAutostartEnabled(bool on);
    QStringList trayIconPresets() const;  // image files in trayIconsDir()
    QStringList bundledTrayIcons() const; // qrc-bundled preset icons
    QColor trayContrastColor() const;
    QString toastText() const { return m_toast; }
    QString appVersion() const { return QStringLiteral(UNISIC_VERSION); }
    QString buildNumber() const { return QStringLiteral(UNISIC_BUILD); }
    // In the .cpp: the generated unisic_build_date.h changes on every commit —
    // including it here would recompile every AppContext.h dependent each time.
    QString buildDate() const;
    // Release notes (markdown) for the RUNNING version in `lang` ("en"/"pl"):
    // the `### English`/`### Polski` block of the bundled CHANGELOG.md section
    // whose `##` heading matches appVersion(). Empty when there is no such entry.
    // Shown when the user clicks the version label.
    Q_INVOKABLE QString changelog(const QString &lang) const;
    bool patchNotesUnseen() const { return m_settings->lastSeenVersion() != appVersion(); }
    // Record the running version as seen so the hint stops (idempotent).
    Q_INVOKABLE void markPatchNotesSeen();

    // Capture entry points (also bound to hotkeys and tray).
    // One-shot override used by `unisic --delay SECONDS --region` et al. It is
    // consumed by the next screenshot delay and never writes the saved default.
    void setNextCaptureDelayMs(int delayMs);
    void setNextCaptureOutput(const QString &path, const QString &format, bool toStdout);
    Q_INVOKABLE void captureFullScreen();
    Q_INVOKABLE void captureRegion();
    Q_INVOKABLE void captureMeasure();
    // Region selection -> OCR/QR -> clipboard. No save/history/notification.
    Q_INVOKABLE void captureRegionOcr();
    Q_INVOKABLE void captureWindow();
    Q_INVOKABLE QString pickWatermarkImage();
    // Bring a file you already have into the app: an image opens in the editor,
    // a recording in the trim window. One entry point, routed by what the file
    // actually is — the two windows are the same ones a capture would open.
    // kind: "image" (editor), "video" (trim window), or empty for both. It only
    // preselects the dialog's filter — where the file actually lands is decided
    // by what it IS (editableKindFor), so picking an mp4 under "Images" still
    // opens the trim window instead of failing.
    Q_INVOKABLE void openFileForEditing(const QString &kind = {});
    // "image" | "video" | "" — which window (if any) can take this file. Split
    // out of openFileForEditing so the routing is checkable without a dialog.
    static QString editableKindFor(const QString &path);
    Q_INVOKABLE void openTrimRecording(const QString &path);
    Q_INVOKABLE void trimRecording(const QString &path, qreal startSeconds, qreal endSeconds);
    Q_INVOKABLE void captureWithTask(const QString &mode, const QString &task);
    Q_INVOKABLE void startGifRegion();
    Q_INVOKABLE void startGifFullScreen();
    Q_INVOKABLE void startVideoScreen();
    Q_INVOKABLE void startVideoRegion();
    Q_INVOKABLE void startVideoWindow();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void startInstantReplay();
    Q_INVOKABLE void saveInstantReplay();

    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void showQr(const QString &url);
    // Text representations of a capture. The data URI encode is asynchronous
    // because a full-resolution PNG must never stall the GUI thread.
    void copyImageAs(const QImage &img, const QString &filePath, const QString &url,
                     const QString &format, std::function<void(bool)> done = {});
    Q_INVOKABLE void openFile(const QString &path);
    Q_INVOKABLE void openDirectory(const QString &path);
    // "text/uri-list" payload for dragging a history capture out to another
    // app (file manager, or a video editor like LosslessCut). Fully-encoded so
    // spaces/# in the path survive the drop (external consumers percent-decode).
    Q_INVOKABLE QString fileDragUri(const QString &path) const;
    // important: shown even when the user disabled notifications (errors
    // must not vanish silently).
    Q_INVOKABLE void showToast(const QString &text, bool important = false);
    Q_INVOKABLE void ocrFile(const QString &path);   // OCR an image file, copy text
    Q_INVOKABLE QString formatShortcut(int key, int modifiers, int nativeScanCode = 0) const;
    Q_INVOKABLE void setShortcutRecording(bool recording);
    Q_INVOKABLE void applyHotkeys();
    // Push ONE just-edited action — never re-asserts the app's possibly-stale
    // copies of the others (that used to clobber KCM edits).
    Q_INVOKABLE void applyHotkey(const QString &actionId);
    Q_INVOKABLE QString exportSettings(const QUrl &file);   // "" on success, else error
    Q_INVOKABLE QString importSettings(const QUrl &file);
    // Native (DE) file picker + export/import in one call — the QML FileDialog
    // fell back to an ugly non-native dialog under the forced Basic style.
    Q_INVOKABLE void exportSettingsDialog();
    Q_INVOKABLE void importSettingsDialog();
    Q_INVOKABLE QString filenamePreview() const;
    // Custom tray icon. addTrayIcon = pick a file, COPY it into trayIconsDir;
    // selectTrayIcon = pick a known path (gallery tile; "" reverts to default);
    // clear = default. trayIconsDir = the folder the user drops their icons into.
    Q_INVOKABLE void addTrayIcon();
    Q_INVOKABLE void selectTrayIcon(const QString &path);
    Q_INVOKABLE void clearTrayIcon();
    Q_INVOKABLE QString trayIconsDir() const;
    // Recolored PNG data: URL of a bundled monochrome preset for the gallery
    // thumbnail (so the preview matches the recolored tray icon). color is
    // passed in (App.trayContrastColor) so the binding refreshes on a scheme flip.
    Q_INVOKABLE QString trayIconThumb(const QString &path, const QColor &color) const;

    // Used by EditorSession / CaptureNotification. fileName: reuse a name
    // computed once per capture (save and upload must agree); empty = generate.
    QString saveImageAuto(const QImage &img, const QString &fileName = {});
    QString saveImageTo(const QImage &img, const QString &dir, const QString &fileName = {});
    void copyImageToClipboard(const QImage &img);
    // Play the selected capture-sound cue (General > Capture sound). No-op
    // when "off" or no player (pw-play/paplay/aplay) is present.
    void playCaptureSound();
    // Same, for finished recordings/GIFs (General > Recording sound) —
    // a separate cue with its own setting.
    void playRecordingSound();
    // Cue the instant recording begins (after the countdown), own setting.
    void playRecordStartSound();
    // Fixed trash cue for explicit deletions — always the bundled "trash"
    // sound, deliberately not user-configurable.
    void playTrashSound();
    // Preview the selected capture sound from the settings UI.
    Q_INVOKABLE void previewCaptureSound() { playCaptureSound(); }
    Q_INVOKABLE void previewRecordingSound() { playRecordingSound(); }
    Q_INVOKABLE void previewRecordStartSound() { playRecordStartSound(); }
    // Live preview of the capture card while the user edits its style, corner or
    // edge margin: the REAL card, on the real screen, through the real path (the
    // layer surface or the XWayland helper), with a placeholder image. A drawn
    // mock-up would have to guess what only the compositor knows — where panels
    // and docks push the card to — which is the one thing worth previewing.
    // Re-entrant: each call replaces the card still on screen, so dragging a
    // slider re-renders it live. Never fires the native-notification fallback:
    // an unwanted desktop notification is not a preview.
    // `overrides` (setting name -> value, see NotifCard::settingKeys) renders a
    // card for values the user has NOT saved — pointing at "Top left" in the
    // dropdown must show a top-left card without committing it. Empty = preview
    // exactly what is saved.
    Q_INVOKABLE void previewCapturePopup(const QVariantMap &overrides = {});
    Q_INVOKABLE void hideCapturePopupPreview();
    // "Copy last capture" hotkey: put the most recent screenshot of this
    // session back on the clipboard (toast when none exists yet).
    Q_INVOKABLE void copyLastCapture();
    // Ids for the Settings combo: "off", the bundled cues, then any user
    // files dropped into ~/.config/unisic/sounds (basenames incl. extension).
    Q_INVOKABLE QStringList captureSoundIds() const;
    // Pick an audio file and COPY it into the user sounds dir (the player is
    // only ever handed bundled cues or files from that dir — never an
    // arbitrary config-supplied path). Returns the new id, "" on cancel/fail.
    Q_INVOKABLE QString addCustomSound();
    void uploadImage(const QImage &img, UploadDone done);
    void openEditor(const QImage &img, const QString &overwritePath = {});
    // Floating, pinnable, translucent preview of a capture. Returns false when
    // the window could not be created.
    bool openPreview(const QImage &img);
    // Re-open a saved capture from history; editor save() overwrites it in place.
    Q_INVOKABLE void editFromHistory(const QString &filePath);
    // Open a saved capture from history in the floating pinned preview.
    Q_INVOKABLE void previewFromHistory(const QString &filePath);
    // Copy a saved image file's pixels to the clipboard (history card).
    Q_INVOKABLE void copyImageFromHistory(const QString &filePath);
    Q_INVOKABLE void copyAsFromHistory(const QString &filePath, const QString &url,
                                       const QString &format);
    // Upload a saved capture file (history card) to the active destination and
    // write the resulting URL back onto its history entry.
    Q_INVOKABLE void uploadFromHistory(const QString &filePath);
    void ocrImage(const QImage &img);                // OCR + copy recognized text
    // OCR the image and deliver per-word boxes on the GUI thread (editor's
    // selectable-text overlay). Empty words + a message on failure/no-OCR.
    void ocrBoxes(const QImage &img, std::function<void(const QVector<OcrWord> &, const QString &)> cb);
    // Upload for the capture popup: reuses the existing history entry (by path)
    // instead of adding a new one, and reflects progress back on the popup.
    void uploadFromNotification(CaptureNotification *n, const QImage &img, const QString &path);
    // Desktop-aware "what to do about it" suffix for capture failures.
    QString captureErrorGuidance(const QString &err);

signals:
    void audioApplicationNodesReady(const QVariantList &nodes);
    void recordingChanged();
    void recordSecondsChanged();
    void toastChanged();
    void showMainWindowRequested();
    void showQuickTaskChooserRequested();
    void cliCaptureReady(const QByteArray &data, const QString &error);
    void hotkeysAvailableChanged();
    void recordingAvailableChanged();
    void trayAvailableChanged();
    void shortcutRecordingChanged();
    void editorWindowsOpenChanged();
    void smokeTestChanged();
    void patchNotesUnseenChanged();
    void autostartEnabledChanged();
    void trayIconPresetsChanged();
    void trayContrastColorChanged();
    void recordingCapabilitiesChanged();

private:
    bool systemIsDark() const;                        // OS light/dark scheme
    QIcon recoloredTrayIcon(const QString &path) const; // bundled preset -> contrast
    QString autostartFilePath() const;
    QByteArray autostartExecLine() const; // the "Exec=..." line for the .desktop
    bool writeAutostartFile();            // (re)write the autostart .desktop, false on I/O error
    void refreshAutostartIfStale();       // rewrite if the binary/AppImage path moved
    QIcon trayIcon() const;      // custom (Settings) if valid, else bundled default
    QIcon trayIconBadged() const;// trayIcon() + a red recording dot
    void applyTrayIcon();        // push trayIcon() to the live QSystemTrayIcon
    struct HotkeyAction {
        QString id;
        QString name;
        QString keys;
    };
    QVector<HotkeyAction> hotkeyActions() const;
    void dispatchHotkey(const QString &actionId);
    void bindPortalHotkeys();
    void syncHotkeyFromDaemon(const QString &actionId, const QString &portable);
    void syncAllHotkeysFromDaemon();
    // Query each action's live daemon binding; with heal, re-assert stored
    // keys on actions the daemon reports unbound. Lines for smoke/dev output.
    // `conflicts` (optional) collects keys that are in OUR binding yet resolve
    // to ANOTHER component daemon-side (e.g. a KWin script) — those actions
    // look bound but never fire, and healing cannot win the key back.
    QStringList hotkeyBindStatus(int *unbound, bool heal, QStringList *conflicts = nullptr);
    // Windows (editor/preview) opened while the smoke test runs — the final
    // step closes them so F8 leaves no manual cleanup behind.
    QVector<QPointer<QQuickWindow>> m_smokeWindows;
    // Export -> verify all properties serialized -> import back. Returns a
    // "PASS (...)"/"FAIL (...)" line shared by the smoke test and dev button.
    QString settingsRoundTripCheck();
    // Multi-binding daemon round-trip on a scratch action ("F9, Meta+F9").
    QString altHotkeysCheck();
    // Idle gate for the automatic post-update restart: empty = safe to
    // restart, else a comma-joined list of what blocks it (recording, open
    // editors, visible window…).
    QString autoRestartBlockers() const;
    bool mainWindowVisible() const;
    // Restart into an installed update when idle; false = deferred.
    bool tryUpdateRestart();

    // forceCopy: the overlay was confirmed with Ctrl+C (Spectacle semantics) —
    // copy to the clipboard even when auto-copy is off.
    void finishCapture(const QImage &img, bool inhibited, bool forceCopy = false);
    // Shared player behind playCaptureSound/playRecordingSound: resolves a
    // bundled or user sound id and spawns pw-play/paplay/aplay.
    void playSoundId(const QString &id);
    // Approx playback length (ms) of a bundled/custom cue from its WAV header;
    // -1 if unknown (e.g. an OGG custom). Sizes the pre-record start-cue tail.
    int soundDurationMs(const QString &id) const;
    // Pre-recording start sequence. Calls begin(holdForCommit) IMMEDIATELY so the
    // portal share dialog resolves FIRST; when a countdown or a start sound is
    // set, holdForCommit=true and the recorder waits for commit(). On armed() the
    // countdown/cue runs, then commitRecordingAfterCue() releases encoding so no
    // countdown number and no start sound land in the recording.
    void startRecorderCountdown(std::function<void(bool)> begin);
    // Ticks the visible countdown (region frame number or toast), then commits.
    void runRecordCountdownVisuals(int secs);
    // Clears the number, plays the start cue, then commit()s encoding after a
    // short tail (repaint settled + cue played out, so neither is recorded).
    void commitRecordingAfterCue();
    bool m_recordHoldActive = false; // a hold-for-commit recording is pending
    int m_pendingCountdownSecs = 0;  // visible countdown length for the hold
    CaptureNotification *showCaptureNotification(const QImage &img, const QString &path,
                                                 const QString &kind, bool inhibited,
                                                 const QVariantMap &overrides = {});
    // Spawn the GNOME XWayland card process for one capture. Returns false when
    // it could not be started (caller then falls back to a native notification).
    bool showNotificationHelper(CaptureNotification *n, const QVariantMap &overrides = {});
    // Are notifications inhibited RIGHT NOW (fullscreen app / DND / screen share)?
    // Sampled at each capture/record trigger — BEFORE our own fullscreen selection
    // overlay opens, so the overlay can't self-suppress the resulting card — and
    // threaded per-operation to the card (never a shared member: a screenshot
    // during a recording would otherwise clobber the recording's value).
    // Consulted by the layer-shell card only (the native path is server-suppressed).
    bool nowInhibited() const;
    void setupTray();
    void defineHotkeys();
    void onRecordingFinished(const QString &path);
    void finishRecordingEntry(const QString &path, const QImage &thumb, const QString &kind);
    // Region recording marker: a transparent, click-through fullscreen surface
    // that draws a frame just OUTSIDE the recorded rect (physRegion, physical
    // px on screen) so the user sees what is being captured without the frame
    // landing inside the ffmpeg crop. Hosted on layer-shell (KWin/wlroots/
    // COSMIC), a KWin fullscreen-transparent fallback, or — GNOME — a separate
    // XWayland helper process (see RecordBorderHelper.h).
    void showRecordBorder(QRect physRegion, QScreen *screen, int countdown = 0);
    // Update the live frame's countdown number (in-process window via property,
    // XWayland helper via its stdin). 0 clears it as recording begins.
    void setRecordBorderCountdown(int n);
    void hideRecordBorder();
    void withDelay(std::function<void()> fn);
    // "Quick copy" grace window: when auto-copy-to-clipboard is off, the last
    // capture is held for a couple of seconds and Ctrl+C (grabbed globally via
    // KGlobalAccel for exactly that window) copies it, then the grab is released.
    QString makeFileName() const;                    // template + extension
    // Encode off the GUI thread (a 4K PNG encode is 100+ ms): settings are
    // snapshotted here, the encode runs on a worker, and `done(data, mime)` is
    // delivered back on the GUI thread (dropped if AppContext died meanwhile).
    void encodeImageAsync(const QImage &img,
                          std::function<void(const QByteArray &, const QString &)> done,
                          const QString &formatOverride = {});
    // Continuation of openPreview after the worker-thread PNG save.
    void finishOpenPreview(bool saved, const QString &tmp, const QSize &imgSize);
    void afterUploadActions(const QString &url);
    void beginDoNotDisturb();
    void endDoNotDisturb();
    void captureRegionWithTool(int initialTool);
    void runExternalAction(const QImage &image, const QString &savedPath);
    void refreshWatermarkImage();
    void showTrimWindow(const QString &path, qreal duration);
    struct CaptureTask {
        bool active = false;
        bool save = false;
        bool copy = false;
        bool edit = false;
        bool upload = false;
    };
    static CaptureTask taskFromId(const QString &id);
    void clearCliCapture(const QString &error = {});
    GifRecorder::Output videoOutput() const;
    // Coalesced, debounced malloc_trim(0): after a capture/record/upload cycle
    // frees large QImage/encode buffers, hand the pages back to the OS instead of
    // letting glibc's arena hold them for the app's whole tray lifetime.
    void scheduleMemoryTrim();

    QQmlEngine *m_engine = nullptr;
    Settings *m_settings;
    CaptureManager *m_capture;
    OverlayController *m_overlay;
    UploadManager *m_uploads;
    HistoryStore *m_history;
    UpdateChecker *m_updater = nullptr;
    GlobalHotkeys *m_hotkeys;
    PortalGlobalShortcuts *m_portalHotkeys = nullptr;
    QString m_hotkeyBackend; // "kglobalaccel" | "portal" | ""
    GifRecorder *m_recorder;
    OcrEngine *m_ocr = nullptr;
    QTranslator *m_appTranslator = nullptr; // bundled unisic_<lang>.qm
    QTranslator *m_qtTranslator = nullptr;  // Qt's own strings for the locale
    QSystemTrayIcon *m_tray = nullptr;
    QDBusServiceWatcher *m_trayWatcher = nullptr; // at most one, reused across retries
    QFileSystemWatcher *m_trayIconsWatcher = nullptr; // watches trayIconsDir() for drops
    QTimer *m_trimTimer = nullptr;
    QTimer *m_updateRestartTimer = nullptr; // retries the idle auto-restart
    // Newest screenshot, encoded off-thread (megabytes, not a pinned 4K
    // QImage) — the "Copy last capture" hotkey decodes and copies it.
    QByteArray m_lastCaptureData;
    DesktopNotifier *m_notifier = nullptr; // native desktop-notification sender
    NotificationInhibitor *m_dnd = nullptr;
    ExternalActionRunner *m_actionRunner = nullptr;
    LayerShellNotifier *m_layerNotifier = nullptr; // set only when layer-shell is usable
    bool m_layerShellAvailable = false;            // compositor exposes zwlr_layer_shell_v1
    QPointer<QQuickWindow> m_recordBorderWindow; // live region-recording frame
    // The card currently shown as a settings preview. QPointer: the notifier (or
    // the helper's process exit) owns and destroys it whenever it closes itself.
    QPointer<CaptureNotification> m_previewNotif;
    QProcess *m_recordBorderHelper = nullptr;    // XWayland helper frame (GNOME path)
    QList<QProcess *> m_notifHelpers;            // live GNOME capture-card helpers
    QRect m_pendingRecordRegion;   // physical px; set on a region record, else empty
    QPointer<QScreen> m_pendingRecordScreen;
    QMenu *m_trayMenu = nullptr; // setContextMenu does not take ownership
    QString m_toast;
    bool m_screenCastPortalPresent = true; // optimistic until the async probe answers
    bool m_vaapiAvailable = false;
    bool m_nvencAvailable = false;
    int m_editorWindows = 0;
    bool m_converting = false;
    bool m_shortcutRecording = false;
    bool m_captureInFlight = false; // re-entry guard for portal captures
    int m_nextCaptureDelayMs = -1;  // CLI-only one-shot override; -1 = settings
    CaptureTask m_nextCaptureTask;
    QString m_nextCaptureDestination; // one-shot per-hotkey upload override
    QString m_nextCaptureOutputPath;
    QString m_nextCaptureOutputFormat;
    bool m_nextCaptureToStdout = false;
    // Decoded once when the setting changes, never in the per-capture fan-out.
    // Capped in refreshWatermarkImage() so a logo cannot pin an enormous source.
    QImage m_watermarkImage;
    // Monotonic copy-request id: the deferred wl-copy mirror only fires when
    // its request is still the newest (GUI-thread only, no atomics).
    quint64 m_clipboardSeq = 0;
    // Developer smoke-test runner state (sequential async steps).
    void smokeNext();
    void smokeLog(const QString &line);
    QString m_smokeLog;
    bool m_smokeRunning = false;
    QVector<std::function<void()>> m_smokeSteps;
    int m_smokeIdx = 0;
};
