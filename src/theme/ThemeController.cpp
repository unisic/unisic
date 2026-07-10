#include "ThemeController.h"
#include <QEvent>
#include <QGuiApplication>
#include <QStyleHints>
#include <QPalette>

static ThemeController *s_instance = nullptr;

ThemeController::ThemeController(QObject *parent) : QObject(parent)
{
    m_themeName = m_settings.value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString();
    if (!s_instance)
        s_instance = this;

    // Follow live system scheme/palette changes so the "System" theme updates
    // without a restart. A scheme flip fires BOTH notifications — coalesce into
    // one rev bump per event-loop turn, or every icon re-renders twice.
    if (auto *hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged,
                this, &ThemeController::scheduleSystemBump);
    }
    // Palette changes arrive as QEvent::ApplicationPaletteChange on qApp
    // (QGuiApplication::paletteChanged is deprecated since Qt 6.0).
    qApp->installEventFilter(this);
}

bool ThemeController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange)
        scheduleSystemBump();
    return QObject::eventFilter(watched, event);
}

void ThemeController::scheduleSystemBump()
{
    if (m_bumpQueued)
        return;
    m_bumpQueued = true;
    QMetaObject::invokeMethod(this, [this] {
        m_bumpQueued = false;
        bump();
        emit systemChanged();
    }, Qt::QueuedConnection);
}

ThemeController::~ThemeController()
{
    if (s_instance == this)
        s_instance = nullptr;
}

ThemeController *ThemeController::instance()
{
    return s_instance;
}

void ThemeController::setThemeName(const QString &name)
{
    if (m_themeName == name)
        return;
    m_themeName = name;
    m_settings.setValue(QStringLiteral("ui/theme"), name);
    emit themeNameChanged();
    bump();
}

void ThemeController::bump()
{
    ++m_rev;
    emit revChanged();
}

bool ThemeController::systemDark() const
{
    // Unknown is common on non-KDE/wlroots compositors — fall back to the
    // palette lightness heuristic there instead of always claiming light.
    if (auto *hints = QGuiApplication::styleHints()) {
        const auto scheme = hints->colorScheme();
        if (scheme != Qt::ColorScheme::Unknown)
            return scheme == Qt::ColorScheme::Dark;
    }
    return qApp->palette().color(QPalette::Window).lightness() < 128;
}

QColor ThemeController::sysWindow() const { return qApp->palette().color(QPalette::Active, QPalette::Window); }
QColor ThemeController::sysBase() const { return qApp->palette().color(QPalette::Active, QPalette::Base); }
QColor ThemeController::sysAlternateBase() const { return qApp->palette().color(QPalette::Active, QPalette::AlternateBase); }
QColor ThemeController::sysButton() const { return qApp->palette().color(QPalette::Active, QPalette::Button); }
QColor ThemeController::sysText() const { return qApp->palette().color(QPalette::Active, QPalette::WindowText); }
QColor ThemeController::sysMidText() const
{
    // A muted secondary text colour: blend window text toward window bg.
    const QColor t = sysText();
    const QColor w = sysWindow();
    return QColor((t.red() + w.red()) / 2, (t.green() + w.green()) / 2, (t.blue() + w.blue()) / 2);
}
QColor ThemeController::sysAccent() const { return qApp->palette().color(QPalette::Active, QPalette::Highlight); }
QColor ThemeController::sysAccentText() const { return qApp->palette().color(QPalette::Active, QPalette::HighlightedText); }
