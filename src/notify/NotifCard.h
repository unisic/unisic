#pragma once
#include <QJsonObject>
#include <QQmlPropertyMap>
#include <QSize>
#include <QVariantMap>
#include <QString>
#include <QStringList>
#include "../Settings.h"

// The one description of the capture card, shared by BOTH hosts:
//   LayerShellNotifier — in-process layer surface (KWin, wlroots, COSMIC, and
//                        muffin 6.7+); chosen whenever zwlr_layer_shell_v1 is
//                        advertised.
//   NotificationHelper — a second process on xcb, spawned ONLY when that probe
//                        comes back empty (GNOME/mutter). It exists because one
//                        process cannot host two QPA platforms: the app is
//                        Wayland, and an override-redirect X11 window is the
//                        only surface mutter keeps above everything.
// Both hosts render the same NotificationPopup.qml. What used to be copied
// between them — the style->size table and the settings the QML reads — lives
// here instead, because a divergence is not a compile error: it silently clips
// the card on one desktop only.
//
// Settings travel IN-BAND (encodeConfig -> argv -> decode). The helper must not
// read the config file itself: Settings debounces its sync() by 800 ms, so a
// card spawned right after a change (the settings hover preview does exactly
// that) would render the previous value.
namespace NotifCard {

// Transparent gutter around the card inside the surface, so the drop shadow is
// not clipped. Both hosts size the window to card + 2*kPad. Layer-shell masks
// input to the card; the GNOME helper deliberately keeps the gutter interactive
// as an XWayland hover-exit moat (see NotificationHelper.cpp).
inline constexpr int kPad = 16;

inline QString normalizeStyle(const QString &style)
{
    static const QStringList known = {QStringLiteral("casual"), QStringLiteral("compact"),
                                      QStringLiteral("small"), QStringLiteral("minimal"),
                                      QStringLiteral("thumbnail")};
    return known.contains(style) ? style : QStringLiteral("casual");
}

// Style -> surface size. MUST match NotificationPopup.qml's layout table: the
// QML lays out for the style, C++ sizes the window, and nothing reconciles them
// at runtime.
inline QSize sizeForStyle(const QString &style)
{
    const QString s = normalizeStyle(style);
    if (s == QLatin1String("compact"))   return {380, 96};
    if (s == QLatin1String("small"))     return {380, 52};
    if (s == QLatin1String("minimal"))   return {300, 36};
    if (s == QLatin1String("thumbnail")) return {240, 150};
    return {400, 150};                                      // casual
}

// The settings NotificationPopup.qml (and the two hosts' placement maths) read.
// A new card setting is added HERE and nowhere else: the property is read off
// Settings' metaobject and lands in the helper's App.settings under the same
// name, so the QML sees one surface whichever host drew it.
inline const QStringList &settingKeys()
{
    static const QStringList keys = {
        QStringLiteral("capturePopupStyle"),
        QStringLiteral("capturePopupPosition"),
        QStringLiteral("capturePopupDurationSec"),
        QStringLiteral("capturePopupMargin"),
        QStringLiteral("hiddenNotifActions"),
        QStringLiteral("notificationActionOrder"),
    };
    return keys;
}

// The values a card is actually built from: the saved settings, with `overrides`
// (if any) laid on top. Overrides exist for the settings preview — pointing at
// "Top left" in the dropdown must show a top-left card WITHOUT saving top-left,
// so the card cannot simply read Settings.
inline QVariantMap effectiveSettings(const Settings *s, const QVariantMap &overrides = {})
{
    QVariantMap out;
    for (const QString &k : settingKeys())
        out.insert(k, overrides.contains(k) ? overrides.value(k)
                                            : s->property(k.toUtf8().constData()));
    return out;
}

inline QJsonObject encodeConfig(const Settings *s, bool qrAvailable, bool ocrAvailable,
                                const QVariantMap &overrides = {})
{
    QJsonObject settings;
    const QVariantMap eff = effectiveSettings(s, overrides);
    for (auto it = eff.begin(); it != eff.end(); ++it)
        settings.insert(it.key(), QJsonValue::fromVariant(it.value()));
    return QJsonObject{{QStringLiteral("settings"), settings},
                       {QStringLiteral("qrAvailable"), qrAvailable},
                       {QStringLiteral("ocrAvailable"), ocrAvailable}};
}

// Rebuilds the QML-visible `App.settings` in the helper. A property map, not a
// hand-written stub class: the stub had to mirror every property by hand, and a
// forgotten one is invisible in C++ — the QML just reads undefined and the
// setting appears to do nothing on GNOME only.
inline QQmlPropertyMap *makeSettingsMap(const QJsonObject &settings, QObject *parent)
{
    auto *map = new QQmlPropertyMap(parent);
    for (auto it = settings.begin(); it != settings.end(); ++it)
        map->insert(it.key(), it.value().toVariant());
    // Keys the sender's Settings did not carry (an older config, a key added
    // later) would read as undefined in QML. Insert the defaults so the QML
    // always sees the same shape.
    for (const QString &k : settingKeys())
        if (!map->contains(k))
            map->insert(k, QVariant());
    return map;
}

} // namespace NotifCard
