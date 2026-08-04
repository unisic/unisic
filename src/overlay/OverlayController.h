#pragma once
#include <QObject>
#include <QImage>
#include <QRect>
#include <QVector>
#include <functional>

class QQmlEngine;
class QQuickWindow;
class QScreen;
class AppContext;

// Region selection: freezes every screen (one capture per
// monitor), shows a fullscreen frameless window per screen rendering the
// frozen image with an AnnotationCanvas in selection mode, and returns the
// annotated crop. Also used (selection only) to pick a GIF recording region.
class OverlayController : public QObject
{
    Q_OBJECT
    // True while an overlay window's in-place text editor is open — the
    // focus-follows-hover activation must not steal keyboard focus mid-typing
    // (Escape would then cancel the whole session instead of the text box).
    Q_PROPERTY(bool textEditing READ textEditing WRITE setTextEditing NOTIFY textEditingChanged)
    // Whether KWin answered where the active window was when this session
    // opened. Drives the W hint, so it is a property and not a plain getter:
    // the answer arrives asynchronously, usually after the windows are up.
    Q_PROPERTY(bool activeWindowKnown READ activeWindowKnown NOTIFY activeWindowKnownChanged)
public:
    // Result: annotated cropped image (image mode).
    using ImageCallback = std::function<void(const QImage &img)>;
    // Result: region in *physical* pixels on the chosen screen + the screen.
    using RegionCallback = std::function<void(const QRect &physRect, QScreen *screen)>;

    explicit OverlayController(AppContext *app, QObject *parent = nullptr);

    bool active() const { return m_starting || !m_windows.isEmpty(); }
    bool textEditing() const { return m_textEditing; }
    void setTextEditing(bool on)
    {
        if (m_textEditing == on) return;
        m_textEditing = on;
        emit textEditingChanged();
    }

    // What the overlay was opened FOR. The two funnels below each serve several
    // purposes - pickAnnotatedImage covers a screenshot, a measurement and an
    // OCR read, pickRegion covers GIF and video - so the window could not tell
    // them apart and showed the same chrome and the same "Start" button for all
    // of them (issue #98: you cannot see which capture mode is active). Only
    // the caller knows, so the caller says.
    enum class Purpose { Shot, Measure, Ocr, Gif, Video };
    // The QML-side name of a purpose. A plain string and not a Q_ENUM because
    // OverlayController reaches QML as a context property, not as a registered
    // type, so QML could not spell the enumerators anyway - and this value is
    // only ever compared and displayed.
    static QString purposeName(Purpose p);

    void pickAnnotatedImage(ImageCallback cb, Purpose purpose, int initialTool = 0);
    void pickRegion(RegionCallback cb, Purpose purpose); // no annotation tools

    // One-shot: was this session confirmed with Ctrl+C (confirmAndCopy)?
    // Consumed by the capture callback to force a clipboard copy even when
    // auto-copy is off — Spectacle's CaptureWindow Ctrl+C semantics.
    bool takeCopyRequested()
    {
        const bool r = m_copyRequested;
        m_copyRequested = false;
        return r;
    }

    // Last confirmed screenshot selection in LOGICAL pixels of the named
    // screen (re-capture feature). Logical (not physical) so the rect survives
    // the frozen-image-vs-native scaling of both the KWin and portal paths;
    // the re-capture side rescales against the CURRENT screen geometry.
    QRect lastRegionLogical() const { return m_lastRegionLogical; }
    QString lastRegionScreen() const { return m_lastRegionScreen; }

    bool activeWindowKnown() const { return !m_activeWindowLogical.isEmpty(); }

    // Where the active window lands inside one screen's frozen image. Global
    // LOGICAL px in, image px out; empty when the window barely touches this
    // screen. Static so the smoke test can check the arithmetic that decides
    // what the W key selects without opening an overlay.
    static QRectF selectionForScreen(const QRect &globalLogical, const QScreen *screen,
                                     const QSize &imageSize);

signals:
    void textEditingChanged();
    void activeWindowKnownChanged();

public slots:
    void confirmFromWindow(QQuickWindow *win);   // Enter / double-click
    void confirmAndCopy(QQuickWindow *win);      // Ctrl+C: confirm + force copy
    void cancel();                               // Esc
    // W: selects the rectangle of the window that was active when the overlay
    // opened (KWin only). Does nothing when nobody could tell us where it was.
    void selectActiveWindow();

private:
    void begin(bool annotationTools, Purpose purpose);
    void createWindows();
    void closeAll();

    AppContext *m_app;
    QVector<QQuickWindow *> m_windows;
    // Screen each created window belongs to, appended in lockstep with m_windows
    // so confirmFromWindow maps a window back to its screen without assuming
    // m_windows[i] == m_screens[i] (a per-screen component.create() failure would
    // break that positional assumption and rescale/remember the wrong screen).
    QVector<QScreen *> m_windowScreens;
    QVector<QScreen *> m_screens;
    QVector<QImage> m_frozen;
    ImageCallback m_imageCb;
    RegionCallback m_regionCb;
    bool m_annotationTools = true;
    Purpose m_purpose = Purpose::Shot;
    bool m_starting = false;
    bool m_textEditing = false;
    bool m_copyRequested = false;
    int m_initialTool = 0; // optional full-screen tool mode (e.g. CLI --measure)
    int m_generation = 0; // invalidates in-flight freeze callbacks
    QRect m_lastRegionLogical;
    QString m_lastRegionScreen;
    // Active window at the moment this session started, in KWin's global
    // logical px. Asked for in begin(), because by the time the overlay is up
    // the ACTIVE window is the overlay itself.
    QRect m_activeWindowLogical;
};
