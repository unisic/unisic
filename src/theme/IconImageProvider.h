#pragma once
#include <QQuickImageProvider>

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
};
