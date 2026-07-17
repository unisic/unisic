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

    void pickAnnotatedImage(ImageCallback cb, int initialTool = 0); // full capture flow
    void pickRegion(RegionCallback cb);          // GIF region flow (no annotation tools)

    // One-shot: was this session confirmed with Ctrl+C (confirmAndCopy)?
    // Consumed by the capture callback to force a clipboard copy even when
    // auto-copy is off — Spectacle's CaptureWindow Ctrl+C semantics.
    bool takeCopyRequested()
    {
        const bool r = m_copyRequested;
        m_copyRequested = false;
        return r;
    }

signals:
    void textEditingChanged();

public slots:
    void confirmFromWindow(QQuickWindow *win);   // Enter / double-click
    void confirmAndCopy(QQuickWindow *win);      // Ctrl+C: confirm + force copy
    void cancel();                               // Esc

private:
    void begin(bool annotationTools);
    void createWindows();
    void closeAll();

    AppContext *m_app;
    QVector<QQuickWindow *> m_windows;
    QVector<QScreen *> m_screens;
    QVector<QImage> m_frozen;
    ImageCallback m_imageCb;
    RegionCallback m_regionCb;
    bool m_annotationTools = true;
    bool m_starting = false;
    bool m_textEditing = false;
    bool m_copyRequested = false;
    int m_initialTool = 0; // optional full-screen tool mode (e.g. CLI --measure)
    int m_generation = 0; // invalidates in-flight freeze callbacks
};
