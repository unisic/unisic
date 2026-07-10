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

// Flatten a monochrome glyph to `color` via SourceIn. Used for the bundled
// SVGs AND for QIcon::fromTheme results: a system icon otherwise keeps the
// SYSTEM scheme's tint, so on a custom app theme (e.g. dark system + the
// unisic purple theme) it rendered near-white on a light surface — barely
// visible. Tinting to the requested app-theme colour keeps every icon,
// whatever its source, consistent with the selected theme.
static QPixmap tintPixmap(QPixmap pm, const QColor &color)
{
    if (pm.isNull() || !color.isValid())
        return pm;
    QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(img.rect(), color);
    p.end();
    return QPixmap::fromImage(img);
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

    const int rev = tc ? tc->rev() : 0;
    if (rev != m_cacheRev) {
        m_cache.clear();
        m_cacheRev = rev;
    }
    const QString cacheKey = id + QLatin1Char('|')
        + QString::number(target.width()) + QLatin1Char('x') + QString::number(target.height());
    if (auto it = m_cache.constFind(cacheKey); it != m_cache.constEnd()) {
        if (size) *size = it->size();
        return *it;
    }

    if (system) {
        QIcon icon = QIcon::fromTheme(name);
        if (!icon.isNull()) {
            // Tint the system glyph to the requested app-theme colour: the raw
            // fromTheme pixmap follows the SYSTEM scheme and clashes with (or
            // vanishes on) a custom app theme. On the actual "system" theme the
            // colour IS the system text colour, so this is a no-op there.
            QPixmap pm = tintPixmap(icon.pixmap(target), color);
            if (!pm.isNull()) {
                if (size) *size = pm.size();
                m_cache.insert(cacheKey, pm);
                return pm;
            }
        }
        // System theme lacked the name — fall back to the bundled glyph,
        // tinted to the system text color for legibility.
        QPixmap pm = tintedBundled(name, color.isValid() ? color : tc->sysText(), target);
        if (!pm.isNull()) {
            if (size) *size = pm.size();
            m_cache.insert(cacheKey, pm);
            return pm;
        }
    }

    QPixmap pm = tintedBundled(name, color, target);
    if (pm.isNull()) {
        // last resort: still try the desktop theme, tinted to the app colour.
        QIcon icon = QIcon::fromTheme(name);
        pm = tintPixmap(icon.pixmap(target), color);
    }
    if (size) *size = pm.isNull() ? target : pm.size();
    m_cache.insert(cacheKey, pm);
    return pm;
}
