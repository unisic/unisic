#pragma once
#include <QString>
#include <QStringList>
#include <QList>

// Registers Unisic's capture actions as *custom keyboard shortcuts* in the
// desktop's own shortcut store, for desktops that offer neither KGlobalAccel
// nor a working org.freedesktop.portal.GlobalShortcuts backend (so the app
// cannot grab keys itself). Every shortcut spawns `unisic --hotkey <action>`,
// which a running instance forwards over the single-instance socket exactly
// like an in-app hotkey press.
//
// Backends drive each desktop's real config through its own tools/format, so
// the bindings show up (and stay editable) in that desktop's settings UI:
//   - COSMIC : rewrite ~/.config/cosmic/...Shortcuts/v1/custom (RON), Spawn(...)
//   - GNOME / Budgie / Cinnamon : gsettings custom-keybinding relocatable schema
//   - Xfce   : xfconf-query, /commands/custom/<accel>
//   - Singularity : inject <keybind> entries into ~/.config/labwc/rc.xml (the
//     DE is labwc-based, has no GlobalShortcuts portal and no custom-command
//     store — its dev.sinty.desktop.Shortcuts service only remaps its own fixed
//     actions), then `labwc --reconfigure`. The DE's ShortcutManager REGENERATES
//     rc.xml wholesale from a baked-in template on login and on any shortcut
//     edit (verified: WriteLabwcRcXml wipes foreign keybinds), so AppContext
//     re-asserts these entries on startup and via a file watcher.
// Everything else (niri/Hyprland/sway/MATE/unknown) is copy-paste only via
// manualText(). All writes are idempotent: re-installing replaces Unisic's own
// entries and never touches the user's other custom shortcuts.
namespace ShortcutBinder {

enum class Backend { None, Cosmic, Gnome, Cinnamon, Xfce, Singularity, Manual };

struct Binding {
    QString actionId;  // e.g. "capture-region" — also the spawned --hotkey arg
    QString name;      // human label, shown as the custom-shortcut name
    QString portable;  // Qt portable keys, comma-separated alternates ("" = none)
    QString command;   // full command line to spawn
};

struct Result {
    bool ok = false;
    QString error;
    QStringList skipped; // action names with a key we couldn't map to this store
    int written = 0;     // shortcut entries actually written
};

// Which store can we drive here? Detected from XDG_CURRENT_DESKTOP plus the
// presence of the writer tool. Only consulted when there is no KGlobalAccel and
// no working portal, so KDE never reaches this.
Backend detect();
bool autoInstallable(Backend b);   // Cosmic/Gnome/Cinnamon/Xfce
QString desktopName(Backend b);    // "COSMIC" | "GNOME" | "Cinnamon" | "Xfce" | ""

// Replace Unisic's own entries with these (skipping unmappable chords). remove()
// strips them, leaving the user's other custom shortcuts intact.
Result install(Backend b, const QList<Binding> &bindings);
Result remove(Backend b);

// Copy-paste guidance for `b` (used as the fallback, and as the whole card on a
// Manual desktop): the exact commands plus where that desktop wants them.
QString manualText(Backend b, const QList<Binding> &bindings);

// Re-assert support, only for backends whose store the DESKTOP itself rewrites
// (Singularity regenerates rc.xml on login/edit, dropping our entries).
// watchPath: the store file AppContext should watch; empty = store is stable,
// no re-assert needed. present: cheap "are our entries in the store right now".
QString watchPath(Backend b);
bool present(Backend b);

} // namespace ShortcutBinder
