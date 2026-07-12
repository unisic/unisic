#pragma once
#include <QObject>
#include <QColor>
#include <QSettings>
#include "../ConfigPath.h"
#include <qqmlregistration.h>

// Owns the selected UI theme (persisted) and bridges the live system palette
// so the QML Theme singleton can build a "System" palette that follows KDE's
// light/dark scheme and accent color. Registered as a module QML singleton;
// the IconImageProvider shares the same object via ThemeController::instance().
class ThemeController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString themeName READ themeName WRITE setThemeName NOTIFY themeNameChanged)
    Q_PROPERTY(int rev READ rev NOTIFY revChanged)

    // Live system palette (valid regardless of selected theme; used by "system").
    Q_PROPERTY(bool systemDark READ systemDark NOTIFY systemChanged)
    Q_PROPERTY(QColor sysWindow READ sysWindow NOTIFY systemChanged)
    Q_PROPERTY(QColor sysBase READ sysBase NOTIFY systemChanged)
    Q_PROPERTY(QColor sysAlternateBase READ sysAlternateBase NOTIFY systemChanged)
    Q_PROPERTY(QColor sysButton READ sysButton NOTIFY systemChanged)
    Q_PROPERTY(QColor sysText READ sysText NOTIFY systemChanged)
    Q_PROPERTY(QColor sysMidText READ sysMidText NOTIFY systemChanged)
    Q_PROPERTY(QColor sysAccent READ sysAccent NOTIFY systemChanged)
    Q_PROPERTY(QColor sysAccentText READ sysAccentText NOTIFY systemChanged)
    Q_PROPERTY(QColor sysTooltipBase READ sysTooltipBase NOTIFY systemChanged)
    Q_PROPERTY(QColor sysTooltipText READ sysTooltipText NOTIFY systemChanged)
    // KDE-only extras read from kdeglobals (roles QPalette does not carry).
    // Alpha-0 when unavailable — the QML side then keeps its own defaults.
    Q_PROPERTY(QColor sysPositive READ sysPositive NOTIFY systemChanged)
    Q_PROPERTY(QColor sysNegative READ sysNegative NOTIFY systemChanged)
    Q_PROPERTY(QColor sysHover READ sysHover NOTIFY systemChanged)

public:
    explicit ThemeController(QObject *parent = nullptr);
    ~ThemeController() override;

    // Singleton factory so the app and the image provider share one instance.
    static ThemeController *instance();

    QString themeName() const { return m_themeName; }
    void setThemeName(const QString &name);
    int rev() const { return m_rev; }

    bool eventFilter(QObject *watched, QEvent *event) override;

    bool systemDark() const;
    QColor sysWindow() const;
    QColor sysBase() const;
    QColor sysAlternateBase() const;
    QColor sysButton() const;
    QColor sysText() const;
    QColor sysMidText() const;
    QColor sysAccent() const;
    QColor sysAccentText() const;
    QColor sysTooltipBase() const;
    QColor sysTooltipText() const;
    QColor sysPositive() const;
    QColor sysNegative() const;
    QColor sysHover() const;

signals:
    void themeNameChanged();
    void systemChanged();
    void revChanged();

private:
    void bump();
    void scheduleSystemBump();

    QString m_themeName;
    int m_rev = 0;
    bool m_bumpQueued = false;
    QSettings m_settings{UnisicConfig::filePath(), QSettings::IniFormat};
};
