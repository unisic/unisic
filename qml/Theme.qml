pragma Singleton
import QtQuick
import Unisic

// Themeable design tokens. Every consumer keeps using Theme.primary,
// Theme.surface, Theme.textPrimary … unchanged; the values are now computed
// from the palette selected in ThemeController (persisted), and the "system"
// palette follows KDE's live light/dark scheme and accent color.
QtObject {
    id: theme

    readonly property string themeName: ThemeController.themeName
    readonly property int rev: ThemeController.rev

    // Base palette definitions (only a handful of seed colors each; the rest
    // are derived in _expand). A def may override any derived token.
    readonly property var _defs: ({
        "unisic": {
            primary: "#17153B", secondary: "#2E236C", tertiary: "#433D8B", accent: "#C8ACD6",
            bg: "#100E2C", backgroundDeep: "#0B0921",
            surface: "#1E1B4A", surfaceTop: "#252158", surfaceBottom: "#1B1845",
            surfaceHi: "#221F52", surfaceHiTop: "#2C2766",
            text: "#F3F0FA", textOnAccent: "#1B1834", isDark: true
        },
        "dark": {
            primary: "#17171C", secondary: "#212127", tertiary: "#2E2E36", accent: "#C8ACD6",
            bg: "#121216", surface: "#1D1D22", text: "#ECECEF", textOnAccent: "#241C2B", isDark: true
        },
        "light": {
            primary: "#FFFFFF", secondary: "#EEEEF2", tertiary: "#E1E1E8", accent: "#4C6EF5",
            bg: "#F4F5F7", surface: "#FFFFFF", surfaceHi: "#F1F2F5",
            text: "#1B1B1F", textOnAccent: "#FFFFFF", isDark: false
        },
        "catppuccin-mocha": {
            primary: "#1E1E2E", secondary: "#181825", tertiary: "#45475A", accent: "#CBA6F7",
            bg: "#1E1E2E", backgroundDeep: "#11111B",
            surface: "#313244", surfaceHi: "#45475A",
            text: "#CDD6F4", textOnAccent: "#1E1E2E", danger: "#F38BA8", success: "#A6E3A1", isDark: true
        },
        "catppuccin-latte": {
            primary: "#EFF1F5", secondary: "#E6E9EF", tertiary: "#CCD0DA", accent: "#8839EF",
            bg: "#EFF1F5", surface: "#FFFFFF", surfaceHi: "#E6E9EF",
            text: "#4C4F69", textOnAccent: "#FFFFFF", danger: "#D20F39", success: "#40A02B", isDark: false
        },
        "dracula": {
            primary: "#282A36", secondary: "#343746", tertiary: "#44475A", accent: "#BD93F9",
            bg: "#282A36", backgroundDeep: "#1E202A",
            surface: "#343746", surfaceHi: "#44475A",
            text: "#F8F8F2", textOnAccent: "#282A36", danger: "#FF5555", success: "#50FA7B", isDark: true
        },
        "nord": {
            primary: "#2E3440", secondary: "#3B4252", tertiary: "#434C5E", accent: "#88C0D0",
            bg: "#2E3440", backgroundDeep: "#272C36",
            surface: "#3B4252", surfaceHi: "#434C5E",
            text: "#ECEFF4", textOnAccent: "#2E3440", danger: "#BF616A", success: "#A3BE8C", isDark: true
        },
        "gruvbox": {
            primary: "#282828", secondary: "#3C3836", tertiary: "#504945", accent: "#FABD2F",
            bg: "#282828", backgroundDeep: "#1D2021",
            surface: "#3C3836", surfaceHi: "#504945",
            text: "#EBDBB2", textOnAccent: "#282828", danger: "#FB4934", success: "#B8BB26", isDark: true
        }
    })

    function _mixA(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }

    // Public: a translucent tint of a theme color (for hover overlays etc.)
    function alpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }

    function _expand(d) {
        var text = Qt.color(d.text)
        return {
            primary: d.primary,
            secondary: d.secondary,
            tertiary: d.tertiary,
            accent: d.accent,
            background: d.bg,
            backgroundDeep: d.backgroundDeep !== undefined ? d.backgroundDeep : Qt.darker(d.bg, 1.28),
            surface: d.surface,
            surfaceTop: d.surfaceTop !== undefined ? d.surfaceTop : Qt.lighter(d.surface, 1.10),
            surfaceBottom: d.surfaceBottom !== undefined ? d.surfaceBottom : Qt.darker(d.surface, 1.05),
            surfaceHi: d.surfaceHi !== undefined ? d.surfaceHi : Qt.lighter(d.surface, 1.16),
            surfaceHiTop: d.surfaceHiTop !== undefined ? d.surfaceHiTop
                          : Qt.lighter(d.surfaceHi !== undefined ? d.surfaceHi : d.surface, 1.12),
            divider: d.divider !== undefined ? d.divider : _mixA(text, 0.12),
            edgeLight: d.edgeLight !== undefined ? d.edgeLight
                       : (d.isDark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(1, 1, 1, 0.65)),
            shadow: d.shadow !== undefined ? d.shadow : Qt.rgba(0, 0, 0, d.isDark ? 0.45 : 0.16),
            textPrimary: text,
            textSecondary: _mixA(text, 0.62),
            textTertiary: _mixA(text, 0.40),
            textOnAccent: d.textOnAccent,
            success: d.success !== undefined ? d.success : "#7BD88F",
            danger: d.danger !== undefined ? d.danger : "#FF6B81",
            dangerText: d.dangerText !== undefined ? d.dangerText : (d.isDark ? "#2B0E14" : "#FFFFFF"),
            tooltipBg: d.tooltipBg !== undefined ? d.tooltipBg
                       : (d.isDark ? Qt.rgba(0, 0, 0, 0.85) : Qt.rgba(30/255, 27/255, 45/255, 0.92)),
            thumbTop: d.thumbTop !== undefined ? d.thumbTop : "#FFFFFF",
            thumbBottom: d.thumbBottom !== undefined ? d.thumbBottom : "#DDD6EC",
            isDark: d.isDark,
            swatches: d.swatches !== undefined ? d.swatches
                      : ["#FF4757", "#FFD84D", "#2ED573", "#1E90FF", "#C8ACD6", "#FFFFFF", "#17153B"]
        }
    }

    function _system() {
        var dark = ThemeController.systemDark
        var win = ThemeController.sysWindow
        var base = ThemeController.sysBase
        var txt = ThemeController.sysText
        var acc = ThemeController.sysAccent
        var accTxt = ThemeController.sysAccentText
        return _expand({
            primary: win,
            secondary: Qt.darker(win, dark ? 1.12 : 1.03),
            tertiary: acc,
            accent: acc,
            bg: win,
            backgroundDeep: Qt.darker(win, 1.12),
            surface: dark ? Qt.lighter(win, 1.28) : base,
            surfaceHi: dark ? Qt.lighter(win, 1.5) : Qt.darker(base, 1.05),
            text: txt,
            textOnAccent: accTxt,
            divider: _mixA(Qt.color(txt), 0.14),
            isDark: dark
        })
    }

    readonly property var pal: themeName === "system"
                               ? _system()
                               : _expand(_defs[themeName] !== undefined ? _defs[themeName] : _defs["unisic"])

    // --- Public tokens (names unchanged from the original) ---
    readonly property color primary:   pal.primary
    readonly property color secondary: pal.secondary
    readonly property color tertiary:  pal.tertiary
    readonly property color accent:    pal.accent

    readonly property color background:     pal.background
    readonly property color backgroundDeep: pal.backgroundDeep
    readonly property color surface:        pal.surface
    readonly property color surfaceTop:     pal.surfaceTop
    readonly property color surfaceBottom:  pal.surfaceBottom
    readonly property color surfaceHi:      pal.surfaceHi
    readonly property color surfaceHiTop:   pal.surfaceHiTop
    readonly property color divider:        pal.divider
    readonly property color edgeLight:      pal.edgeLight
    readonly property color shadow:         pal.shadow

    readonly property color textPrimary:   pal.textPrimary
    readonly property color textSecondary: pal.textSecondary
    readonly property color textTertiary:  pal.textTertiary
    readonly property color textOnAccent:  pal.textOnAccent

    readonly property color success: pal.success
    readonly property color danger:  pal.danger

    // New tokens (hoisted from previously-hardcoded values)
    readonly property bool  isDark:     pal.isDark
    readonly property color dangerText: pal.dangerText
    readonly property color tooltipBg:  pal.tooltipBg
    readonly property color thumbTop:   pal.thumbTop
    readonly property color thumbBottom: pal.thumbBottom
    readonly property var   swatches:   pal.swatches

    // Geometry — generous SwiftUI-like rounding
    readonly property int radiusS: 8
    readonly property int radiusM: 12
    readonly property int radiusL: 18
    readonly property int radiusXL: 26

    readonly property int spacingS: 6
    readonly property int spacingM: 12
    readonly property int spacingL: 20
    readonly property int spacingXL: 32

    // Type scale
    readonly property int fontS: 12
    readonly property int fontM: 14
    readonly property int fontL: 17
    readonly property int fontXL: 22
    readonly property int fontTitle: 28

    // Motion
    readonly property int animFast: 130
    readonly property int animMed: 220
    readonly property var easeOut: Easing.OutCubic
}
