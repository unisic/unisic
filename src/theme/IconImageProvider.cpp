#include "IconImageProvider.h"
#include "ThemeController.h"
#include <QIcon>
#include <QPainter>
#include <QImageReader>
#include <QUrlQuery>
#include <QFile>
#include <QBuffer>

IconImageProvider::IconImageProvider(ThemeController *controller)
    : QQuickImageProvider(QQuickImageProvider::Pixmap), m_controller(controller)
{
}

static QSize resolveSize(const QSize &requested, int hint)
{
    if (requested.isValid() && requested.width() > 0 && requested.height() > 0)
        return requested;
    const int s = hint > 0 ? hint : 24;
    return QSize(s, s);
}

QPixmap IconImageProvider::tintedBundled(const QString &name, const QColor &color, const QSize &size) const
{
    const QString path = QStringLiteral(":/resources/icons/sym/%1.svg").arg(name);
    if (!QFile::exists(path))
        return {};

    QImageReader reader(path);
    reader.setScaledSize(size);
    QImage img = reader.read();
    if (img.isNull())
        return {};
    img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    if (color.isValid()) {
        QPainter p(&img);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(img.rect(), color);
        p.end();
    }
    return QPixmap::fromImage(img);
}

QPixmap IconImageProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    // id looks like "draw-arrow?color=%23ffffff&sz=18&v=3"
    QString name = id;
    QColor color;
    int hint = 0;
    QString srcOverride;
    const int q = id.indexOf(QLatin1Char('?'));
    if (q >= 0) {
        name = id.left(q);
        const QUrlQuery query(id.mid(q + 1));
        const QString c = query.queryItemValue(QStringLiteral("color"), QUrl::FullyDecoded);
        if (!c.isEmpty())
            color = QColor(c);
        hint = query.queryItemValue(QStringLiteral("sz")).toInt();
        srcOverride = query.queryItemValue(QStringLiteral("src"));
    }

    const QSize target = resolveSize(requestedSize, hint);
    ThemeController *tc = m_controller ? m_controller : ThemeController::instance();
    // src=system|custom (from the editor tool-icon selector) overrides the
    // theme-derived choice; otherwise the "system" theme drives it.
    const bool system = srcOverride == QLatin1String("system")
                        || (srcOverride != QLatin1String("custom")
                            && tc && tc->themeName() == QLatin1String("system"));

    if (system) {
        QIcon icon = QIcon::fromTheme(name);
        if (!icon.isNull()) {
            QPixmap pm = icon.pixmap(target);
            if (!pm.isNull()) {
                if (size) *size = pm.size();
                return pm;
            }
        }
        // System theme lacked the name — fall back to the bundled glyph,
        // tinted to the system text color for legibility.
        QPixmap pm = tintedBundled(name, color.isValid() ? color : tc->sysText(), target);
        if (!pm.isNull()) {
            if (size) *size = pm.size();
            return pm;
        }
    }

    QPixmap pm = tintedBundled(name, color, target);
    if (pm.isNull()) {
        // last resort: still try the desktop theme
        QIcon icon = QIcon::fromTheme(name);
        pm = icon.pixmap(target);
    }
    if (size) *size = pm.isNull() ? target : pm.size();
    return pm;
}
