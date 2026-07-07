#include "ThemeController.h"
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
    // without a restart.
    if (auto *hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this] { bump(); emit systemChanged(); });
    }
    connect(qApp, &QGuiApplication::paletteChanged, this, [this] { bump(); emit systemChanged(); });
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
    if (auto *hints = QGuiApplication::styleHints())
        return hints->colorScheme() == Qt::ColorScheme::Dark;
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
