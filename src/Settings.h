#pragma once
#include <QObject>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <qqmlregistration.h>

#define U_SETTING(type, name, setterName, key, defval)                                 \
    type name() const { return m_s.value(key, defval).value<type>(); }                 \
    void setterName(const type &v) {                                                   \
        if (name() == v) return;                                                       \
        m_s.setValue(key, v);                                                          \
        emit name##Changed();                                                          \
    }

class Settings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by AppContext")

    Q_PROPERTY(QString saveDirectory READ saveDirectory WRITE setSaveDirectory NOTIFY saveDirectoryChanged)
    Q_PROPERTY(bool autoSave READ autoSave WRITE setAutoSave NOTIFY autoSaveChanged)
    Q_PROPERTY(bool copyToClipboard READ copyToClipboard WRITE setCopyToClipboard NOTIFY copyToClipboardChanged)
    Q_PROPERTY(bool openEditor READ openEditor WRITE setOpenEditor NOTIFY openEditorChanged)
    Q_PROPERTY(bool uploadAfterCapture READ uploadAfterCapture WRITE setUploadAfterCapture NOTIFY uploadAfterCaptureChanged)
    Q_PROPERTY(bool includeCursor READ includeCursor WRITE setIncludeCursor NOTIFY includeCursorChanged)
    Q_PROPERTY(int captureDelayMs READ captureDelayMs WRITE setCaptureDelayMs NOTIFY captureDelayMsChanged)
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
    Q_PROPERTY(QString recentColors READ recentColors WRITE setRecentColors NOTIFY recentColorsChanged)
    Q_PROPERTY(QString hiddenTools READ hiddenTools WRITE setHiddenTools NOTIFY hiddenToolsChanged)
    Q_PROPERTY(QString overlayToolbarPosition READ overlayToolbarPosition WRITE setOverlayToolbarPosition NOTIFY overlayToolbarPositionChanged)
    Q_PROPERTY(int videoFps READ videoFps WRITE setVideoFps NOTIFY videoFpsChanged)
    Q_PROPERTY(QString videoFormat READ videoFormat WRITE setVideoFormat NOTIFY videoFormatChanged)
    Q_PROPERTY(int videoQuality READ videoQuality WRITE setVideoQuality NOTIFY videoQualityChanged)
    Q_PROPERTY(int videoMaxDurationSec READ videoMaxDurationSec WRITE setVideoMaxDurationSec NOTIFY videoMaxDurationSecChanged)
    Q_PROPERTY(QString hotkeyRecord READ hotkeyRecord WRITE setHotkeyRecord NOTIFY hotkeyRecordChanged)
    Q_PROPERTY(bool showCapturePopup READ showCapturePopup WRITE setShowCapturePopup NOTIFY showCapturePopupChanged)
    Q_PROPERTY(QString capturePopupPosition READ capturePopupPosition WRITE setCapturePopupPosition NOTIFY capturePopupPositionChanged)
    Q_PROPERTY(int capturePopupDurationSec READ capturePopupDurationSec WRITE setCapturePopupDurationSec NOTIFY capturePopupDurationSecChanged)
    Q_PROPERTY(QString ocrLanguages READ ocrLanguages WRITE setOcrLanguages NOTIFY ocrLanguagesChanged)
    Q_PROPERTY(QString editorIconStyle READ editorIconStyle WRITE setEditorIconStyle NOTIFY editorIconStyleChanged)
    Q_PROPERTY(QString editorToolIcons READ editorToolIcons WRITE setEditorToolIcons NOTIFY editorToolIconsChanged)
    Q_PROPERTY(bool useSystemDecoration READ useSystemDecoration WRITE setUseSystemDecoration NOTIFY useSystemDecorationChanged)

public:
    explicit Settings(QObject *parent = nullptr) : QObject(parent) {}

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

    U_SETTING(QString, saveDirectory, setSaveDirectory, "general/saveDirectory", defaultSaveDir())
    U_SETTING(bool, autoSave, setAutoSave, "general/autoSave", true)
    U_SETTING(bool, copyToClipboard, setCopyToClipboard, "general/copyToClipboard", true)
    U_SETTING(bool, openEditor, setOpenEditor, "general/openEditor", true)
    U_SETTING(bool, uploadAfterCapture, setUploadAfterCapture, "general/uploadAfterCapture", false)
    U_SETTING(bool, includeCursor, setIncludeCursor, "general/includeCursor", false)
    U_SETTING(int, captureDelayMs, setCaptureDelayMs, "general/captureDelayMs", 200)
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
    U_SETTING(bool, showNotifications, setShowNotifications, "general/showNotifications", true)
    U_SETTING(bool, minimizeToTrayOnClose, setMinimizeToTrayOnClose, "general/minimizeToTrayOnClose", true)
    U_SETTING(bool, openAfterSave, setOpenAfterSave, "general/openAfterSave", false)
    U_SETTING(bool, afterUploadCopyLink, setAfterUploadCopyLink, "upload/afterUploadCopyLink", true)
    U_SETTING(bool, afterUploadOpenInBrowser, setAfterUploadOpenInBrowser, "upload/afterUploadOpenInBrowser", false)
    U_SETTING(QString, editorStrokeColor, setEditorStrokeColor, "editor/strokeColor", QStringLiteral("#FF4757"))
    U_SETTING(int, editorStrokeWidth, setEditorStrokeWidth, "editor/strokeWidth", 4)
    U_SETTING(int, editorFontSize, setEditorFontSize, "editor/fontSize", 22)
    U_SETTING(QString, editorFillColor, setEditorFillColor, "editor/fillColor", QStringLiteral("#66C8ACD6"))
    U_SETTING(bool, editorFillEnabled, setEditorFillEnabled, "editor/fillEnabled", false)
    U_SETTING(QString, recentColors, setRecentColors, "editor/recentColors", QString())
    U_SETTING(QString, hiddenTools, setHiddenTools, "editor/hiddenTools", QString())
    U_SETTING(QString, overlayToolbarPosition, setOverlayToolbarPosition, "capture/overlayToolbarPosition", QStringLiteral("follow"))
    U_SETTING(int, videoFps, setVideoFps, "video/fps", 30)
    U_SETTING(QString, videoFormat, setVideoFormat, "video/format", QStringLiteral("mp4"))
    U_SETTING(int, videoQuality, setVideoQuality, "video/quality", 20)
    U_SETTING(int, videoMaxDurationSec, setVideoMaxDurationSec, "video/maxDurationSec", 0)
    U_SETTING(QString, hotkeyRecord, setHotkeyRecord, "hotkeys/record", QStringLiteral("Meta+Shift+R"))
    U_SETTING(bool, showCapturePopup, setShowCapturePopup, "general/showCapturePopup", true)
    U_SETTING(QString, capturePopupPosition, setCapturePopupPosition, "general/capturePopupPosition", QStringLiteral("bottom-right"))
    U_SETTING(int, capturePopupDurationSec, setCapturePopupDurationSec, "general/capturePopupDurationSec", 8) // 0 = stay open
    U_SETTING(QString, ocrLanguages, setOcrLanguages, "ocr/languages", QStringLiteral("pol+eng"))
    // Editor/overlay tool icons only (never the main app chrome): "custom" =
    // bundled monochrome glyphs, "system" = freedesktop QIcon::fromTheme.
    U_SETTING(QString, editorIconStyle, setEditorIconStyle, "ui/editorIconStyle", QStringLiteral("custom"))
    // Optional per-tool freedesktop icon-name overrides, JSON {"toolId":"name"}.
    U_SETTING(QString, editorToolIcons, setEditorToolIcons, "ui/editorToolIcons", QString())
    // Main window chrome: true = system window decoration, false = the app's own
    // custom title bar (frameless).
    U_SETTING(bool, useSystemDecoration, setUseSystemDecoration, "ui/useSystemDecoration", true)

    // Raw access for settings export/import.
    QSettings *raw() { return &m_s; }
    void notifyAll()
    {
        emit saveDirectoryChanged(); emit autoSaveChanged(); emit copyToClipboardChanged();
        emit openEditorChanged(); emit uploadAfterCaptureChanged(); emit includeCursorChanged();
        emit captureDelayMsChanged(); emit gifFpsChanged(); emit gifMaxDurationSecChanged();
        emit gifQualityChanged(); emit activeDestinationChanged(); emit hotkeyFullScreenChanged();
        emit hotkeyRegionChanged(); emit hotkeyWindowChanged(); emit hotkeyGifChanged();
        emit imageFormatChanged(); emit imageQualityChanged(); emit filenameTemplateChanged();
        emit showNotificationsChanged(); emit minimizeToTrayOnCloseChanged(); emit openAfterSaveChanged();
        emit afterUploadCopyLinkChanged(); emit afterUploadOpenInBrowserChanged();
        emit editorStrokeColorChanged(); emit editorStrokeWidthChanged(); emit editorFontSizeChanged();
        emit editorFillColorChanged(); emit editorFillEnabledChanged(); emit recentColorsChanged();
        emit hiddenToolsChanged(); emit overlayToolbarPositionChanged();
        emit videoFpsChanged(); emit videoFormatChanged(); emit videoQualityChanged();
        emit videoMaxDurationSecChanged(); emit hotkeyRecordChanged();
        emit showCapturePopupChanged(); emit capturePopupPositionChanged();
        emit capturePopupDurationSecChanged(); emit ocrLanguagesChanged();
        emit editorIconStyleChanged(); emit editorToolIconsChanged();
        emit useSystemDecorationChanged();
    }

signals:
    void saveDirectoryChanged();
    void autoSaveChanged();
    void copyToClipboardChanged();
    void openEditorChanged();
    void uploadAfterCaptureChanged();
    void includeCursorChanged();
    void captureDelayMsChanged();
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
    void recentColorsChanged();
    void hiddenToolsChanged();
    void overlayToolbarPositionChanged();
    void videoFpsChanged();
    void videoFormatChanged();
    void videoQualityChanged();
    void videoMaxDurationSecChanged();
    void hotkeyRecordChanged();
    void showCapturePopupChanged();
    void capturePopupPositionChanged();
    void capturePopupDurationSecChanged();
    void ocrLanguagesChanged();
    void editorIconStyleChanged();
    void editorToolIconsChanged();
    void useSystemDecorationChanged();

private:
    QSettings m_s{QStringLiteral("Unisic"), QStringLiteral("unisic")};
};
