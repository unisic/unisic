#include "ShortcutBinder.h"
#include "ShortcutKeyMap.h"

#include <QProcess>
#include <QStandardPaths>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QRegularExpression>

namespace {

using ShortcutBinder::Backend;
using ShortcutBinder::Binding;
using ShortcutBinder::Result;

// Every Unisic shortcut spawns a command containing this flag, so COSMIC/Xfce
// (which have no per-entry name field) can still recognize — and cleanly
// remove — their own entries by the command text alone.
const QLatin1String kMarker{" --hotkey "};
// gsettings/Cinnamon entries also carry a self-identifying path/name segment.
const QLatin1String kIdPrefix{"unisic-"};

bool isOurCommand(const QString &cmd)
{
    return cmd.contains(kMarker) && cmd.contains(QLatin1String("unisic"));
}

QString xdgDesktop()
{
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
}

bool haveTool(const QString &name)
{
    return !QStandardPaths::findExecutable(name).isEmpty();
}

// Blocking run — every caller is a user-initiated button, and these tools
// return in well under a second. ok = zero exit code; stdout captured.
bool run(const QString &program, const QStringList &args, QString *out = nullptr)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(program, args);
    if (!p.waitForStarted(3000))
        return false;
    if (!p.waitForFinished(5000)) {
        p.kill();
        return false;
    }
    if (out)
        *out = QString::fromUtf8(p.readAllStandardOutput());
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

// --- GVariant text helpers (we build the values ourselves and pass argv, so no
// shell is involved and nothing needs shell-escaping — only GVariant escaping).
QString gvString(const QString &s)
{
    QString e = s;
    e.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    e.replace(QLatin1Char('\''), QLatin1String("\\'"));
    return QLatin1Char('\'') + e + QLatin1Char('\'');
}

QString gvStringArray(const QStringList &items)
{
    QStringList parts;
    for (const QString &s : items)
        parts << gvString(s);
    return QLatin1Char('[') + parts.join(QLatin1String(", ")) + QLatin1Char(']');
}

// Quoted substrings from a gsettings array print ("['a', 'b']" or "@as []").
QStringList parseGsettingsArray(const QString &text)
{
    QStringList out;
    static const QRegularExpression re(QStringLiteral("'((?:[^'\\\\]|\\\\.)*)'"));
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        QString v = it.next().captured(1);
        v.replace(QLatin1String("\\'"), QLatin1String("'"));
        v.replace(QLatin1String("\\\\"), QLatin1String("\\"));
        out << v;
    }
    return out;
}

// A dconf/gsettings-safe segment for a shortcut's relocatable path.
QString idSegment(const QString &actionId, int chordIndex)
{
    QString seg = kIdPrefix + actionId;
    if (chordIndex >= 0)
        seg += QLatin1Char('-') + QString::number(chordIndex);
    return seg;
}

// ============================================================ COSMIC (RON) ===

QString cosmicCustomPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/cosmic/com.system76.CosmicSettings.Shortcuts/v1/custom");
}

QString ronString(const QString &s)
{
    QString e = s;
    e.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    e.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QLatin1Char('"') + e + QLatin1Char('"');
}

// One compact RON map entry: `(modifiers: [Super, Shift], key: "s"): Spawn("…"),`
QString cosmicEntry(const ShortcutKeyMap::Chord &c, const QString &command)
{
    QString mods = QStringLiteral("modifiers: [")
                   + c.mods.join(QLatin1String(", ")) + QLatin1Char(']');
    QString keyTuple = c.key.isEmpty()
                       ? QStringLiteral("(") + mods + QLatin1Char(')')
                       : QStringLiteral("(") + mods + QStringLiteral(", key: ")
                             + ronString(c.key) + QLatin1Char(')');
    return QStringLiteral("    ") + keyTuple + QStringLiteral(": Spawn(")
           + ronString(command) + QStringLiteral("),");
}

// Strip every Unisic Spawn entry, compact OR the multi-line form COSMIC's own
// settings UI rewrites the file into. A key tuple contains brackets, never
// parens, so `[^()]*` spans it even across newlines.
QString cosmicStripOurs(QString text)
{
    static const QRegularExpression re(
        QStringLiteral(R"(\([^()]*\)\s*:\s*Spawn\(\s*"[^"]*--hotkey[^"]*"\s*\)\s*,?)"));
    text.remove(re);
    // Collapse the blank lines the removals leave behind.
    static const QRegularExpression blanks(QStringLiteral(R"(\n[ \t]*\n([ \t]*\n)+)"));
    text.replace(blanks, QStringLiteral("\n\n"));
    return text;
}

Result cosmicInstall(const QList<Binding> &bindings)
{
    Result r;
    QString text;
    QFile in(cosmicCustomPath());
    if (in.exists() && in.open(QIODevice::ReadOnly))
        text = QString::fromUtf8(in.readAll());
    text = cosmicStripOurs(text);

    QStringList lines;
    for (const Binding &b : bindings) {
        if (b.portable.trimmed().isEmpty())
            continue;
        bool any = false;
        const auto chords = ShortcutKeyMap::parseAll(b.portable);
        for (const ShortcutKeyMap::Chord &c : chords) {
            if (!c.ok) continue;
            lines << cosmicEntry(c, b.command);
            any = true;
            r.written++;
        }
        if (!any)
            r.skipped << b.name;
    }

    QString entries;
    for (const QString &l : lines)
        entries += l + QLatin1Char('\n');

    const int close = text.lastIndexOf(QLatin1Char('}'));
    QString rebuilt;
    if (close < 0) {
        rebuilt = QStringLiteral("{\n") + entries + QStringLiteral("}\n");
    } else {
        QString head = text.left(close);
        int e = head.size();
        while (e > 0 && head.at(e - 1).isSpace()) --e;
        head = head.left(e);
        if (head.isEmpty())
            head = QStringLiteral("{");
        rebuilt = head + QLatin1Char('\n') + entries + QStringLiteral("}\n");
    }

    QDir().mkpath(QFileInfo(cosmicCustomPath()).path());
    QSaveFile out(cosmicCustomPath());
    if (!out.open(QIODevice::WriteOnly) || out.write(rebuilt.toUtf8()) < 0 || !out.commit()) {
        r.ok = false;
        r.error = QStringLiteral("Could not write %1").arg(cosmicCustomPath());
        return r;
    }
    r.ok = true;
    return r;
}

Result cosmicRemove()
{
    Result r;
    QFile in(cosmicCustomPath());
    if (!in.exists()) { r.ok = true; return r; }
    if (!in.open(QIODevice::ReadOnly)) {
        r.error = QStringLiteral("Could not read %1").arg(cosmicCustomPath());
        return r;
    }
    const QString stripped = cosmicStripOurs(QString::fromUtf8(in.readAll()));
    in.close();
    QSaveFile out(cosmicCustomPath());
    if (!out.open(QIODevice::WriteOnly) || out.write(stripped.toUtf8()) < 0 || !out.commit()) {
        r.error = QStringLiteral("Could not write %1").arg(cosmicCustomPath());
        return r;
    }
    r.ok = true;
    return r;
}

// ================================================= gsettings (GNOME/Cinnamon) ===

struct GProfile {
    QString listSchema;
    QString listKey;
    bool    listStoresPaths;   // true: full dbus paths; false: bare names (Cinnamon)
    QString relocSchema;
    QString pathBase;          // "/org/…/custom-keybindings/"
    bool    bindingIsArray;    // Cinnamon binding is `as`, GNOME is `s`
    QString commandKey;        // "command"
};

GProfile gnomeProfile()
{
    return {QStringLiteral("org.gnome.settings-daemon.plugins.media-keys"),
            QStringLiteral("custom-keybindings"), true,
            QStringLiteral("org.gnome.settings-daemon.plugins.media-keys.custom-keybinding"),
            QStringLiteral("/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/"),
            false, QStringLiteral("command")};
}

GProfile cinnamonProfile()
{
    return {QStringLiteral("org.cinnamon.desktop.keybindings"),
            QStringLiteral("custom-list"), false,
            QStringLiteral("org.cinnamon.desktop.keybindings.custom-keybinding"),
            QStringLiteral("/org/cinnamon/desktop/keybindings/custom-keybindings/"),
            true, QStringLiteral("command")};
}

// A list entry (path or name) is ours iff it carries the "unisic-" segment.
bool entryIsOurs(const QString &listItem)
{
    return listItem.contains(kIdPrefix);
}

bool gsettingsSet(const GProfile &p, const QString &path, const QString &key, const QString &gvariant)
{
    return run(QStringLiteral("gsettings"),
               {QStringLiteral("set"), p.relocSchema + QLatin1Char(':') + path, key, gvariant});
}

Result gsettingsInstall(const GProfile &p, const QList<Binding> &bindings)
{
    Result r;
    QString listText;
    if (!run(QStringLiteral("gsettings"),
             {QStringLiteral("get"), p.listSchema, p.listKey}, &listText)) {
        r.error = QStringLiteral("gsettings get %1 %2 failed").arg(p.listSchema, p.listKey);
        return r;
    }
    QStringList list = parseGsettingsArray(listText);
    // Drop (and reset) any of our own leftovers, keep the user's entries.
    QStringList kept;
    for (const QString &item : list) {
        if (entryIsOurs(item)) {
            const QString path = p.listStoresPaths ? item : p.pathBase + item + QLatin1Char('/');
            run(QStringLiteral("gsettings"),
                {QStringLiteral("reset-recursively"), p.relocSchema + QLatin1Char(':') + path});
        } else {
            kept << item;
        }
    }

    QStringList added;
    for (const Binding &b : bindings) {
        if (b.portable.trimmed().isEmpty())
            continue;
        QStringList accels;
        for (const ShortcutKeyMap::Chord &c : ShortcutKeyMap::parseAll(b.portable)) {
            const QString a = ShortcutKeyMap::toGtkAccel(c);
            if (!a.isEmpty())
                accels << a;
        }
        if (accels.isEmpty()) { r.skipped << b.name; continue; }

        if (p.bindingIsArray) {
            // One entry per action, all accelerators in its binding array.
            const QString seg = idSegment(b.actionId, -1);
            const QString path = p.pathBase + seg + QLatin1Char('/');
            bool ok = gsettingsSet(p, path, QStringLiteral("name"),
                                   gvString(QStringLiteral("Unisic: ") + b.name));
            ok = gsettingsSet(p, path, p.commandKey, gvString(b.command)) && ok;
            ok = gsettingsSet(p, path, QStringLiteral("binding"), gvStringArray(accels)) && ok;
            if (ok) {
                added << (p.listStoresPaths ? path : seg);
                r.written++;
            } else {
                r.skipped << b.name;
            }
        } else {
            // GNOME binding is a single string: one entry per accelerator.
            int idx = 0;
            for (const QString &a : accels) {
                const QString seg = idSegment(b.actionId, idx++);
                const QString path = p.pathBase + seg + QLatin1Char('/');
                bool ok = gsettingsSet(p, path, QStringLiteral("name"),
                                       gvString(QStringLiteral("Unisic: ") + b.name));
                ok = gsettingsSet(p, path, p.commandKey, gvString(b.command)) && ok;
                ok = gsettingsSet(p, path, QStringLiteral("binding"), gvString(a)) && ok;
                if (ok) {
                    added << (p.listStoresPaths ? path : seg);
                    r.written++;
                }
            }
        }
    }

    const QStringList finalList = kept + added;
    if (!run(QStringLiteral("gsettings"),
             {QStringLiteral("set"), p.listSchema, p.listKey, gvStringArray(finalList)})) {
        r.error = QStringLiteral("gsettings set %1 %2 failed").arg(p.listSchema, p.listKey);
        return r;
    }
    r.ok = true;
    return r;
}

Result gsettingsRemove(const GProfile &p)
{
    Result r;
    QString listText;
    if (!run(QStringLiteral("gsettings"),
             {QStringLiteral("get"), p.listSchema, p.listKey}, &listText)) {
        r.error = QStringLiteral("gsettings get %1 %2 failed").arg(p.listSchema, p.listKey);
        return r;
    }
    QStringList kept;
    for (const QString &item : parseGsettingsArray(listText)) {
        if (entryIsOurs(item)) {
            const QString path = p.listStoresPaths ? item : p.pathBase + item + QLatin1Char('/');
            run(QStringLiteral("gsettings"),
                {QStringLiteral("reset-recursively"), p.relocSchema + QLatin1Char(':') + path});
        } else {
            kept << item;
        }
    }
    if (!run(QStringLiteral("gsettings"),
             {QStringLiteral("set"), p.listSchema, p.listKey, gvStringArray(kept)})) {
        r.error = QStringLiteral("gsettings set %1 %2 failed").arg(p.listSchema, p.listKey);
        return r;
    }
    r.ok = true;
    return r;
}

// ============================================================== Xfce (xfconf) ===

const QLatin1String kXfceChannel{"xfce4-keyboard-shortcuts"};

Result xfceRemove()
{
    Result r;
    QString listing;
    if (!run(QStringLiteral("xfconf-query"),
             {QStringLiteral("-c"), kXfceChannel, QStringLiteral("-l"), QStringLiteral("-v")},
             &listing)) {
        // No properties yet is not an error.
        r.ok = true;
        return r;
    }
    const QStringList rows = listing.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    static const QRegularExpression sep(QStringLiteral("\\s{2,}"));
    for (const QString &row : rows) {
        const int cut = row.indexOf(sep);
        if (cut < 0) continue;
        const QString prop = row.left(cut);
        const QString value = row.mid(cut).trimmed();
        if (prop.startsWith(QLatin1String("/commands/custom/")) && isOurCommand(value))
            run(QStringLiteral("xfconf-query"),
                {QStringLiteral("-c"), kXfceChannel, QStringLiteral("-p"), prop,
                 QStringLiteral("-r")});
    }
    r.ok = true;
    return r;
}

Result xfceInstall(const QList<Binding> &bindings)
{
    Result r = xfceRemove();
    if (!r.ok)
        return r;
    r.written = 0;
    for (const Binding &b : bindings) {
        if (b.portable.trimmed().isEmpty())
            continue;
        bool any = false;
        for (const ShortcutKeyMap::Chord &c : ShortcutKeyMap::parseAll(b.portable)) {
            const QString accel = ShortcutKeyMap::toGtkAccel(c);
            if (accel.isEmpty()) continue;
            const QString prop = QStringLiteral("/commands/custom/") + accel;
            // -n creates; it fails if the accelerator is already taken (by the
            // user or the system) — skip rather than clobber their binding.
            if (run(QStringLiteral("xfconf-query"),
                    {QStringLiteral("-c"), kXfceChannel, QStringLiteral("-p"), prop,
                     QStringLiteral("-n"), QStringLiteral("-t"), QStringLiteral("string"),
                     QStringLiteral("-s"), b.command})) {
                any = true;
                r.written++;
            }
        }
        if (!any)
            r.skipped << b.name;
    }
    r.ok = true;
    return r;
}

// ===================================================================== manual ===

QString manualWhere(Backend b)
{
    switch (b) {
    case Backend::Cosmic:
        return QStringLiteral("COSMIC: Settings → Keyboard → Custom shortcuts → Add shortcut, one per command.");
    case Backend::Gnome:
        return QStringLiteral("GNOME: Settings → Keyboard → View and Customize Shortcuts → Custom Shortcuts, one per command.");
    case Backend::Cinnamon:
        return QStringLiteral("Cinnamon: System Settings → Keyboard → Shortcuts → Custom Shortcuts → Add custom shortcut, one per command.");
    case Backend::Xfce:
        return QStringLiteral("Xfce: Settings → Keyboard → Application Shortcuts → Add, one per command.");
    default:
        return QStringLiteral("niri (config.kdl):\n\n"
                              "```\nbinds {\n    Mod+Shift+S { spawn \"unisic\" \"--hotkey\" \"capture-region\"; }\n}\n```\n\n"
                              "Hyprland (hyprland.conf): `bind = SUPER SHIFT, S, exec, unisic --hotkey capture-region`\n"
                              "sway (config): `bindsym $mod+Shift+s exec unisic --hotkey capture-region`");
    }
}

} // namespace

// ================================================================= public API ===

namespace ShortcutBinder {

Backend detect()
{
    const QString d = xdgDesktop();
    if (d.contains(QLatin1String("cosmic")))
        return Backend::Cosmic;
    if (d.contains(QLatin1String("cinnamon")))
        return haveTool(QStringLiteral("gsettings")) ? Backend::Cinnamon : Backend::Manual;
    if (d.contains(QLatin1String("xfce")))
        return haveTool(QStringLiteral("xfconf-query")) ? Backend::Xfce : Backend::Manual;
    if (d.contains(QLatin1String("gnome")) || d.contains(QLatin1String("budgie"))
        || d.contains(QLatin1String("unity")) || d.contains(QLatin1String("pop")))
        return haveTool(QStringLiteral("gsettings")) ? Backend::Gnome : Backend::Manual;
    return Backend::Manual;
}

bool autoInstallable(Backend b)
{
    return b == Backend::Cosmic || b == Backend::Gnome
        || b == Backend::Cinnamon || b == Backend::Xfce;
}

QString desktopName(Backend b)
{
    switch (b) {
    case Backend::Cosmic:   return QStringLiteral("COSMIC");
    case Backend::Gnome:    return QStringLiteral("GNOME");
    case Backend::Cinnamon: return QStringLiteral("Cinnamon");
    case Backend::Xfce:     return QStringLiteral("Xfce");
    default:                return QString();
    }
}

Result install(Backend b, const QList<Binding> &bindings)
{
    switch (b) {
    case Backend::Cosmic:   return cosmicInstall(bindings);
    case Backend::Gnome:    return gsettingsInstall(gnomeProfile(), bindings);
    case Backend::Cinnamon: return gsettingsInstall(cinnamonProfile(), bindings);
    case Backend::Xfce:     return xfceInstall(bindings);
    default: { Result r; r.error = QStringLiteral("No auto-install backend"); return r; }
    }
}

Result remove(Backend b)
{
    switch (b) {
    case Backend::Cosmic:   return cosmicRemove();
    case Backend::Gnome:    return gsettingsRemove(gnomeProfile());
    case Backend::Cinnamon: return gsettingsRemove(cinnamonProfile());
    case Backend::Xfce:     return xfceRemove();
    default: { Result r; r.ok = true; return r; }
    }
}

QString manualText(Backend b, const QList<Binding> &bindings)
{
    QString out;
    out += QStringLiteral("```\n");
    for (const Binding &bind : bindings) {
        const QString key = bind.portable.trimmed().isEmpty()
                            ? QStringLiteral("(unset)") : bind.portable;
        out += bind.command + QStringLiteral("    # ") + bind.name
               + QStringLiteral(" — ") + key + QLatin1Char('\n');
    }
    out += QStringLiteral("```\n\n");
    out += manualWhere(b);
    return out;
}

} // namespace ShortcutBinder
