#pragma once
#include <QQuickImageProvider>
#include <QHash>

class ThemeController;

// Serves themed icons to QML as image://icon/<name>?color=%23RRGGBB&sz=NN&v=<rev>
//  - "system" theme: QIcon::fromTheme(name) (Breeze / active desktop theme),
//    falling back to the bundled monochrome SVG when the theme lacks the name.
//  - other themes: the bundled :/icons/sym/<name>.svg, recolored to `color`
//    so tool/action glyphs match the current UI text color.
class IconImageProvider : public QQuickImageProvider
{
public:
    explicit IconImageProvider(ThemeController *controller);

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QPixmap tintedBundled(const QString &name, const QColor &color, const QSize &size) const;

    ThemeController *m_controller;
    // Pixmap provider runs on the GUI thread only — no locking needed. The id
    // embeds name+color+sz+v, so keying on it (+ target size) is exact; the
    // cache is cleared whenever the theme revision changes.
    QHash<QString, QPixmap> m_cache;
    int m_cacheRev = -1;
};
