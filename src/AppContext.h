#pragma once
#include <QObject>
#include <QImage>
#include <QColor>
#include <QRect>
#include <QPointer>
#include <QStringList>
#include <functional>
#include <qqmlregistration.h>

#include "Settings.h"
#include "upload/UploadManager.h"
#include "history/HistoryStore.h"
#include "record/GifRecorder.h"

class QQmlEngine;
class QMenu;
class QIcon;
class QSystemTrayIcon;
class QDBusServiceWatcher;
class QFileSystemWatcher;
class QQuickWindow;
class QTimer;
class CaptureManager;
class OverlayController;
class GlobalHotkeys;
class PortalGlobalShortcuts;
class GifRecorder;
class OcrEngine;
class CaptureNotification;
class DesktopNotifier;
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
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool converting READ converting NOTIFY recordingChanged)
    Q_PROPERTY(int recordSeconds READ recordSeconds NOTIFY recordSecondsChanged)
    Q_PROPERTY(bool recordingAvailable READ recordingAvailable NOTIFY recordingAvailableChanged)
    // No StatusNotifier host (GNOME without the AppIndicator extension, bare
    // wlroots): close must actually close, not hide into a tray that isn't there.
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY trayAvailableChanged)
    // True when the capture card is shown on the layer-shell overlay (KWin/wlroots)
    // rather than as a native notification — lets the UI expose card-only options
    // (e.g. corner position, which a native notification server controls itself).
    Q_PROPERTY(bool layerShellActive READ layerShellActive CONSTANT)
    // Developer build (F8 smoke test; capability options stay editable). False in
    // a release build, where unsupported options are shown greyed-out instead.
    Q_PROPERTY(bool devBuild READ devBuild CONSTANT)
    // Compositor capabilities, resolved once at startup. The UI uses them to
    // enable/grey the matching options. capCustomNotification/capRecordBorder ride
    // on layer-shell (KWin/wlroots/COSMIC); capNativeNotification on a running
    // notification server (absent on e.g. a bare Sway).
    Q_PROPERTY(bool capNativeNotification READ capNativeNotification CONSTANT)
    Q_PROPERTY(bool capCustomNotification READ capCustomNotification CONSTANT)
    Q_PROPERTY(bool capRecordBorder READ capRecordBorder CONSTANT)
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
    // A working global-hotkey backend? KGlobalAccel on KDE, the GlobalShortcuts
    // portal elsewhere; false (niri/sway…) switches the Hotkeys settings tab
    // to the compositor-binds explanation instead of dead recorders.
    Q_PROPERTY(bool hotkeysAvailable READ hotkeysAvailable NOTIFY hotkeysAvailableChanged)
    // "kglobalaccel" | "portal" | "" — lets the UI tailor its hints.
    Q_PROPERTY(QString hotkeyBackend READ hotkeyBackend NOTIFY hotkeysAvailableChanged)
    Q_PROPERTY(QString toastText READ toastText NOTIFY toastChanged)
    // Baked in at compile time (CMake): semantic version + CI build number
    // ("dev" for local builds). Shown in the sidebar footer.
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QString buildNumber READ buildNumber CONSTANT)

public:
    using UploadDone = std::function<void(const QString &url, const QString &error)>;

    explicit AppContext(QObject *parent = nullptr);
    ~AppContext() override;

    void initialize(QQmlEngine *engine);

    Settings *settings() const { return m_settings; }
    UploadManager *uploads() const { return m_uploads; }
    HistoryStore *history() const { return m_history; }
    CaptureManager *captureManager() const { return m_capture; }
    QQmlEngine *qmlEngine() const { return m_engine; }

    bool recording() const;
    bool converting() const;
    int recordSeconds() const;
    bool recordingAvailable() const;
    bool trayAvailable() const { return m_tray != nullptr; }
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
    bool capCustomNotification() const { return m_layerShellAvailable; }
    bool capRecordBorder() const;
    // Developer smoke-test: sequentially exercises the main app paths and logs
    // pass/fail/skip. Bound to F8 (dev build only) and the Settings > Developer
    // button. smokeTestLog is the running transcript shown in the debug panel.
    Q_INVOKABLE void runSmokeTest();
    // Per-action manual triggers for the Developer tab (dev build only): each
    // exercises ONE path in isolation. Capture/record reuse the public entry
    // points; these three cover the non-capture paths with a throwaway image.
    Q_INVOKABLE void devTestNotification();
    Q_INVOKABLE void devTestEditor();
    Q_INVOKABLE void devTestHistory();
    QString smokeTestLog() const { return m_smokeLog; }
    bool smokeTestRunning() const { return m_smokeRunning; }
    int editorWindowsOpen() const { return m_editorWindows; }
    bool ocrAvailable() const;
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

    // Capture entry points (also bound to hotkeys and tray).
    Q_INVOKABLE void captureFullScreen();
    Q_INVOKABLE void captureRegion();
    Q_INVOKABLE void captureWindow();
    Q_INVOKABLE void startGifRegion();
    Q_INVOKABLE void startGifFullScreen();
    Q_INVOKABLE void startVideoScreen();
    Q_INVOKABLE void startVideoRegion();
    Q_INVOKABLE void startVideoWindow();
    Q_INVOKABLE void stopRecording();

    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void openFile(const QString &path);
    Q_INVOKABLE void openDirectory(const QString &path);
    // important: shown even when the user disabled notifications (errors
    // must not vanish silently).
    Q_INVOKABLE void showToast(const QString &text, bool important = false);
    Q_INVOKABLE void ocrFile(const QString &path);   // OCR an image file, copy text
    Q_INVOKABLE QString formatShortcut(int key, int modifiers) const;
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
    // Custom tray icon. pickTrayIcon = native file picker; selectTrayIcon =
    // pick a known path (gallery tile; "" reverts to default); clear = default.
    // trayIconsDir = the folder the user drops their own icons into.
    Q_INVOKABLE void pickTrayIcon();
    Q_INVOKABLE void addTrayIcon();   // pick a file, COPY it into trayIconsDir(), select it
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
    void uploadImage(const QImage &img, UploadDone done);
    void openEditor(const QImage &img);
    void ocrImage(const QImage &img);                // OCR + copy recognized text
    // Upload for the capture popup: reuses the existing history entry (by path)
    // instead of adding a new one, and reflects progress back on the popup.
    void uploadFromNotification(CaptureNotification *n, const QImage &img, const QString &path);
    // Desktop-aware "what to do about it" suffix for capture failures.
    QString captureErrorGuidance(const QString &err);

signals:
    void recordingChanged();
    void recordSecondsChanged();
    void toastChanged();
    void showMainWindowRequested();
    void hotkeysAvailableChanged();
    void recordingAvailableChanged();
    void trayAvailableChanged();
    void editorWindowsOpenChanged();
    void smokeTestChanged();
    void autostartEnabledChanged();
    void trayIconPresetsChanged();
    void trayContrastColorChanged();

private:
    bool systemIsDark() const;                        // OS light/dark scheme
    QIcon recoloredTrayIcon(const QString &path) const; // bundled preset -> contrast
    QString autostartFilePath() const;
    QByteArray autostartExecLine() const; // the "Exec=..." line for the .desktop
    bool writeAutostartFile();            // (re)write the autostart .desktop, false on I/O error
    void refreshAutostartIfStale();       // rewrite if the binary/AppImage path moved
    QIcon trayIcon() const;      // custom (Settings) if valid, else bundled default
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

    void finishCapture(const QImage &img, bool inhibited);
    CaptureNotification *showCaptureNotification(const QImage &img, const QString &path,
                                                 const QString &kind, bool inhibited);
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
    // Region recording marker: a transparent, click-through fullscreen window
    // that draws a frame just OUTSIDE the recorded rect (physRegion, physical
    // px on screen) so the user sees what is being captured without the frame
    // landing inside the ffmpeg crop. KWin-only, like the capture popup.
    void showRecordBorder(QRect physRegion, QScreen *screen);
    void hideRecordBorder();
    void withDelay(std::function<void()> fn);
    QString makeFileName() const;                    // template + extension
    QByteArray encodeImage(const QImage &img, QString *mime) const;
    void afterUploadActions(const QString &url);
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
    GlobalHotkeys *m_hotkeys;
    PortalGlobalShortcuts *m_portalHotkeys = nullptr;
    QString m_hotkeyBackend; // "kglobalaccel" | "portal" | ""
    GifRecorder *m_recorder;
    OcrEngine *m_ocr = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QDBusServiceWatcher *m_trayWatcher = nullptr; // at most one, reused across retries
    QFileSystemWatcher *m_trayIconsWatcher = nullptr; // watches trayIconsDir() for drops
    QTimer *m_trimTimer = nullptr;
    DesktopNotifier *m_notifier = nullptr; // native desktop-notification sender
    LayerShellNotifier *m_layerNotifier = nullptr; // set only when layer-shell is usable
    bool m_layerShellAvailable = false;            // compositor exposes zwlr_layer_shell_v1
    QPointer<QQuickWindow> m_recordBorderWindow; // live region-recording frame
    QRect m_pendingRecordRegion;   // physical px; set on a region record, else empty
    QPointer<QScreen> m_pendingRecordScreen;
    QMenu *m_trayMenu = nullptr; // setContextMenu does not take ownership
    QString m_toast;
    bool m_screenCastPortalPresent = true; // optimistic until the async probe answers
    int m_editorWindows = 0;
    bool m_converting = false;
    bool m_shortcutRecording = false;
    bool m_captureInFlight = false; // re-entry guard for portal captures
    // Developer smoke-test runner state (sequential async steps).
    void smokeNext();
    void smokeLog(const QString &line);
    QString m_smokeLog;
    bool m_smokeRunning = false;
    QVector<std::function<void()>> m_smokeSteps;
    int m_smokeIdx = 0;
    bool m_recordInhibited = false; // inhibit state at record start (recordings are exclusive)
};

