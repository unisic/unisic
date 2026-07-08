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

// ShareX-style region selection: freezes every screen (one capture per
// monitor), shows a fullscreen frameless window per screen rendering the
// frozen image with an AnnotationCanvas in selection mode, and returns the
// annotated crop. Also used (selection only) to pick a GIF recording region.
class OverlayController : public QObject
{
    Q_OBJECT
public:
    // Result: annotated cropped image (image mode).
    using ImageCallback = std::function<void(const QImage &img)>;
    // Result: region in *physical* pixels on the chosen screen + the screen.
    using RegionCallback = std::function<void(const QRect &physRect, QScreen *screen)>;

    explicit OverlayController(AppContext *app, QObject *parent = nullptr);

    bool active() const { return m_starting || !m_windows.isEmpty(); }

    void pickAnnotatedImage(ImageCallback cb);   // full capture flow
    void pickRegion(RegionCallback cb);          // GIF region flow (no annotation tools)

public slots:
    void confirmFromWindow(QQuickWindow *win);   // Enter / double-click
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
    int m_generation = 0; // invalidates in-flight freeze callbacks
};
