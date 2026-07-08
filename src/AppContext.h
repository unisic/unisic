#pragma once
#include <QObject>
#include <QImage>
#include <QRect>
#include <QPointer>
#include <functional>
#include <qqmlregistration.h>

#include "Settings.h"
#include "upload/UploadManager.h"
#include "history/HistoryStore.h"
#include "record/GifRecorder.h"

class QQmlEngine;
class QMenu;
class QSystemTrayIcon;
class QDBusServiceWatcher;
class QQuickWindow;
class QTimer;
class CaptureManager;
class OverlayController;
class GlobalHotkeys;
class PortalGlobalShortcuts;
class GifRecorder;
class OcrEngine;
class CaptureNotification;

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
    int editorWindowsOpen() const { return m_editorWindows; }
    bool ocrAvailable() const;
    bool hotkeysAvailable() const;
    QString hotkeyBackend() const { return m_hotkeyBackend; }
    QString toastText() const { return m_toast; }

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

private:
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

    void finishCapture(const QImage &img);
    CaptureNotification *showCaptureNotification(const QImage &img, const QString &path,
                                                 const QString &kind);
    void setupTray();
    void defineHotkeys();
    void onRecordingFinished(const QString &path);
    void finishRecordingEntry(const QString &path, const QImage &thumb, const QString &kind);
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
    QTimer *m_trimTimer = nullptr;
    QPointer<QQuickWindow> m_notifWindow; // the live capture popup, if any
    QMenu *m_trayMenu = nullptr; // setContextMenu does not take ownership
    QString m_toast;
    bool m_screenCastPortalPresent = true; // optimistic until the async probe answers
    int m_editorWindows = 0;
    bool m_converting = false;
    bool m_shortcutRecording = false;
    bool m_captureInFlight = false; // re-entry guard for portal captures
};
