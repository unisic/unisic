#pragma once
#include <QString>
#include <QStringList>
#include <QList>

// Maps Qt "portable" key strings (QKeySequence::PortableText, e.g.
// "Meta+Shift+S", "Print", "Ctrl+Alt+G") into the forms other desktops want
// when Unisic registers a capture command as a *custom* keyboard shortcut on a
// desktop that offers neither KGlobalAccel nor a working GlobalShortcuts portal
// (COSMIC, Xfce, and — as a fallback — GNOME/Budgie/Cinnamon):
//   - a canonical (modifier set, base key) pair, where the base key is an
//     X-keysym name ("s", "F5", "Print", "Prior", ...) — exactly what COSMIC's
//     RON `key:` field wants and what gtk_accelerator_parse understands;
//   - a GTK accelerator string ("<Super><Shift>s") for the gsettings-family and
//     Xfce backends, both of which speak that vocabulary.
//
// A chord whose key we cannot name returns ok=false, so the caller can drop
// just that chord (and fall back to copy-paste guidance if an action ends up
// with no bindable chord at all). Header-only + pure so tests/ can pin it.
namespace ShortcutKeyMap {

struct Chord {
    QStringList mods;   // canonical order: "Super","Ctrl","Alt","Shift"
    QString key;        // X-keysym base ("s","F5","Print",...); empty = mod-only
    bool ok = false;    // false: unmappable key, skip this chord
};

inline QString mapModifier(const QString &tokenIn)
{
    const QString t = tokenIn.trimmed().toLower();
    if (t == QLatin1String("meta") || t == QLatin1String("super")
        || t == QLatin1String("win")) return QStringLiteral("Super");
    if (t == QLatin1String("ctrl") || t == QLatin1String("control")) return QStringLiteral("Ctrl");
    if (t == QLatin1String("alt")) return QStringLiteral("Alt");
    if (t == QLatin1String("shift")) return QStringLiteral("Shift");
    return QString();
}

// The one place that knows the Qt-portable → X-keysym key vocabulary. Returns
// the empty string for anything we can't confidently name (caller skips it).
inline QString mapKey(const QString &tokenIn)
{
    const QString t = tokenIn.trimmed();
    if (t.isEmpty())
        return QString();

    if (t.size() == 1) {
        const QChar c = t.at(0);
        if (c.isLetter()) return t.toLower();
        if (c.isDigit())  return t;
        switch (c.toLatin1()) {
        case '/':  return QStringLiteral("slash");
        case '\\': return QStringLiteral("backslash");
        case ',':  return QStringLiteral("comma");
        case '.':  return QStringLiteral("period");
        case ';':  return QStringLiteral("semicolon");
        case '\'': return QStringLiteral("apostrophe");
        case '[':  return QStringLiteral("bracketleft");
        case ']':  return QStringLiteral("bracketright");
        case '-':  return QStringLiteral("minus");
        case '=':  return QStringLiteral("equal");
        case '`':  return QStringLiteral("grave");
        default:   return QString();
        }
    }

    // Function keys: F1..F35.
    if ((t.at(0) == QLatin1Char('F') || t.at(0) == QLatin1Char('f')) && t.size() <= 3) {
        bool num = false;
        const int n = t.mid(1).toInt(&num);
        if (num && n >= 1 && n <= 35)
            return QStringLiteral("F") + QString::number(n);
    }

    const QString l = t.toLower();
    if (l == QLatin1String("print"))     return QStringLiteral("Print");
    if (l == QLatin1String("return")
        || l == QLatin1String("enter"))  return QStringLiteral("Return");
    if (l == QLatin1String("space"))     return QStringLiteral("space");
    if (l == QLatin1String("esc")
        || l == QLatin1String("escape"))  return QStringLiteral("Escape");
    if (l == QLatin1String("tab"))       return QStringLiteral("Tab");
    if (l == QLatin1String("backspace")) return QStringLiteral("BackSpace");
    if (l == QLatin1String("del")
        || l == QLatin1String("delete"))  return QStringLiteral("Delete");
    if (l == QLatin1String("ins")
        || l == QLatin1String("insert"))  return QStringLiteral("Insert");
    if (l == QLatin1String("home"))      return QStringLiteral("Home");
    if (l == QLatin1String("end"))       return QStringLiteral("End");
    if (l == QLatin1String("pgup")
        || l == QLatin1String("pageup"))  return QStringLiteral("Prior");
    if (l == QLatin1String("pgdown")
        || l == QLatin1String("pagedown")) return QStringLiteral("Next");
    if (l == QLatin1String("left"))      return QStringLiteral("Left");
    if (l == QLatin1String("right"))     return QStringLiteral("Right");
    if (l == QLatin1String("up"))        return QStringLiteral("Up");
    if (l == QLatin1String("down"))      return QStringLiteral("Down");
    if (l == QLatin1String("menu"))      return QStringLiteral("Menu");
    return QString();
}

// Canonicalize one "Meta+Shift+S" chord. A modifier-only chord (COSMIC allows a
// bare Super) keeps an empty key and stays ok; the gsettings/Xfce serializers
// drop it since those stores cannot bind a modifier alone.
inline Chord parseChord(const QString &chordIn)
{
    Chord c;
    const QString chord = chordIn.trimmed();
    if (chord.isEmpty())
        return c;
    const QStringList tokens = chord.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    QStringList mods;
    QStringList keys;
    for (const QString &tok : tokens) {
        const QString mod = mapModifier(tok);
        if (!mod.isEmpty()) {
            if (!mods.contains(mod))
                mods.append(mod);
        } else {
            keys.append(tok);
        }
    }
    // A modifier that also names the key (bare "Meta") lands in mods, not keys.
    if (keys.size() > 1)
        return c; // more than one non-modifier token: not a single chord we grok
    if (keys.isEmpty()) {
        // Modifier-only: keep it, but only if there IS a modifier.
        if (mods.isEmpty())
            return c;
        c.mods = mods;
        c.ok = true;
        return c;
    }
    const QString key = mapKey(keys.first());
    if (key.isEmpty())
        return c; // unmappable key
    c.mods = mods;
    c.key = key;
    c.ok = true;
    return c;
}

// Every comma-separated alternate from a portable string ("Meta+Shift+S, Print").
// Unmappable chords are returned with ok=false so the caller can count skips.
inline QList<Chord> parseAll(const QString &portable)
{
    QList<Chord> out;
    const QStringList chords = portable.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &ch : chords)
        out.append(parseChord(ch));
    return out;
}

// "<Super><Shift>s" — the accelerator gsettings/xfconf both parse. Empty for an
// unmapped or modifier-only chord (neither store can bind those).
inline QString toGtkAccel(const Chord &c)
{
    if (!c.ok || c.key.isEmpty())
        return QString();
    QString out;
    for (const QString &m : c.mods) {
        if (m == QLatin1String("Super"))      out += QStringLiteral("<Super>");
        else if (m == QLatin1String("Ctrl"))  out += QStringLiteral("<Control>");
        else if (m == QLatin1String("Alt"))   out += QStringLiteral("<Alt>");
        else if (m == QLatin1String("Shift")) out += QStringLiteral("<Shift>");
    }
    out += c.key;
    return out;
}

} // namespace ShortcutKeyMap
