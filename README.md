<div align="center">

<img src="resources/icons/unisic.svg" width="160" height="160" alt="Unisic" />

# Unisic

**Most snipping tools stop at a screenshot. Unisic is everything that should happen after.**

Silent capture · Annotate · Smart eraser · Record GIF/MP4/WebM · Upload · Zero telemetry · GPLv3

[![Download latest release](https://img.shields.io/badge/Download_Latest_Release-C8ACD6?style=for-the-badge&logo=linux&logoColor=17153B)](https://github.com/unisic/unisic/releases/latest)

<p>
  <img alt="Linux Wayland" src="https://img.shields.io/badge/Linux-Wayland-000?style=for-the-badge&color=433D8B">
  <a href="https://github.com/unisic/unisic/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/unisic/unisic?include_prereleases&style=for-the-badge&label=release&color=433D8B"></a>
  <a href="https://github.com/unisic/unisic/releases"><img alt="Downloads" src="https://img.shields.io/github/downloads/unisic/unisic/total?style=for-the-badge&color=433D8B"></a>
  <img alt="License" src="https://img.shields.io/badge/license-GPLv3-000?style=for-the-badge&color=433D8B">
</p>
[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/deandark)
<br />

<img src="docs/screenshots/editor.png" width="99%" alt="Unisic post-capture editor" />
<img src="docs/screenshots/history.png" width="49%" alt="Unisic history page" />
<img src="docs/screenshots/destinations.png" width="49%" alt="Unisic destinations page" />

</div>

## What is Unisic

Most screenshot tools hand you a rectangle of pixels and walk away. Unisic covers the whole workflow after you press the hotkey: draw on the selection **before the shot is even taken**, polish it in the editor (blur, numbered steps, crop, smart eraser), record the same region as a GIF or video, and send the result where it belongs - clipboard, disk, or your own upload destination with the link ready to paste.

Built for **Linux Wayland** on legitimate APIs only (xdg-desktop-portal, KWin ScreenShot2, PipeWire). KDE Plasma gets the fully silent native path; GNOME and wlroots desktops work through portals. **C++20 · Qt 6 · QML**, fully custom UI.

## Install

Any Wayland desktop with `xdg-desktop-portal` works; recording also needs PipeWire and `ffmpeg`.

**The quick way:** grab a package from the **[latest release](https://github.com/unisic/unisic/releases/latest)**. The AppImage updates itself, and the `.deb` / Fedora `.rpm` / Arch `.pkg.tar.zst` register their native repo on first install - from then on updates arrive through `apt upgrade` / `dnf upgrade` / `pacman -Syu` like any other package. openSUSE installs from the repo below (no release rpm - a binary rpm is pinned to the exact Qt it was built against).

Or add the repo yourself:

<details open>
<summary><b>Fedora</b></summary>

```sh
sudo dnf copr enable deandark/Unisic
sudo dnf install unisic
```
</details>

<details>
<summary><b>Debian / Ubuntu</b></summary>

```sh
REPO=Debian_13   # or xUbuntu_26.04 / xUbuntu_25.10
curl -fsSL "https://download.opensuse.org/repositories/home:/unisic/${REPO}/Release.key" \
  | gpg --dearmor | sudo tee /etc/apt/keyrings/home_unisic.gpg > /dev/null
echo "deb [signed-by=/etc/apt/keyrings/home_unisic.gpg] https://download.opensuse.org/repositories/home:/unisic/${REPO}/ ./" \
  | sudo tee /etc/apt/sources.list.d/home_unisic.list
sudo apt update && sudo apt install unisic
```
</details>

<details>
<summary><b>openSUSE</b></summary>

```sh
# Tumbleweed (for Leap 16.0 replace openSUSE_Tumbleweed with 16.0)
sudo zypper addrepo https://download.opensuse.org/repositories/home:unisic/openSUSE_Tumbleweed/home:unisic.repo
sudo zypper refresh
sudo zypper install unisic
```
</details>

<details>
<summary><b>Arch</b></summary>

```sh
curl -fsSL 'https://build.opensuse.org/projects/home:unisic/signing_keys/download?kind=gpg' -o /tmp/unisic-obs.key
sudo pacman-key --add /tmp/unisic-obs.key
sudo pacman-key --lsign-key "$(gpg --show-keys --with-colons /tmp/unisic-obs.key | awk -F: '/^fpr/{print $10; exit}')"
printf '\n[home_unisic_Arch]\nServer = https://download.opensuse.org/repositories/home:/unisic/Arch/$arch\n' \
  | sudo tee -a /etc/pacman.conf
sudo pacman -Syu unisic
```
</details>

Prefer compiling? See [Build from source](#build-from-source).

## First steps

Unisic starts in the background with a tray icon. Press a hotkey and go:

| Keys | Description |
| --- | --- |
| <kbd>Meta</kbd> + <kbd>Shift</kbd> + <kbd>1</kbd> | Capture the full screen |
| <kbd>Meta</kbd> + <kbd>Shift</kbd> + <kbd>2</kbd> | Capture a region |
| <kbd>Meta</kbd> + <kbd>Shift</kbd> + <kbd>3</kbd> | Capture the active window |
| <kbd>Meta</kbd> + <kbd>Shift</kbd> + <kbd>G</kbd> | Record a GIF (region) |
| <kbd>Meta</kbd> + <kbd>Shift</kbd> + <kbd>R</kbd> | Record video (region) |
| <kbd>Meta</kbd> + <kbd>Shift</kbd> + <kbd>T</kbd> | OCR - copy text out of a region |
| <kbd>Meta</kbd> + <kbd>Shift</kbd> + <kbd>C</kbd> | Copy the last capture again |
| <kbd>Ctrl</kbd> + <kbd>Esc</kbd> | Stop recording (fixed emergency stop) |

Every hotkey is editable in Settings → Hotkeys (applied to the system immediately), or on KDE in the Shortcuts KCM.

The same actions work from the command line - a second invocation forwards the command to the running instance, which is exactly how compositor keybinds should call it:

```sh
unisic --fullscreen | --region | --window | --gif
unisic --export-settings <file> | --import-settings <file>
```

On wlroots compositors (niri, Sway…) install `grim` for capture and bind the commands in your compositor config:

```kdl
binds {
    Mod+Shift+S { spawn "unisic" "--region"; }
    Print { spawn "unisic" "--fullscreen"; }
}
```

Settings and upload destinations live in `~/.config/unisic/`, capture history in `~/.local/share/unisic/`. Filename tokens: `%date%` `%time%` `%datetime%` `%unix%` `%rand%`.

## Compositor support

Unisic targets KDE Plasma first and stays portable everywhere else, but how *complete* it feels depends on what the compositor exposes:

- **KDE Plasma / KWin** — the full experience: silent native capture, in-app editable global hotkeys, layer-shell surfaces, tray, and window-state queries.
- **wlroots** (Sway, Hyprland, river, Wayfire, labwc, COSMIC…) — nearly complete: layer-shell surfaces work, capture via `grim` or portals, window state via `wlr-foreign-toplevel-management`; bind the hotkeys in your compositor config.
- **X11 sessions** (e.g. Cinnamon) — screenshots and the editor work through the portal; screen recording is unavailable (no ScreenCast backend).
- **GNOME / Mutter** — **not as fully functional as it could be, by compositor limitation, not by choice.** Mutter exposes no protocol or API for a client to read window state (e.g. whether an application is fullscreen), has no layer-shell (the record-region frame falls back to an XWayland helper), needs the AppIndicator extension for a tray, and routes hotkeys through the portal. Everything core still works; some niceties that other compositors allow simply cannot be implemented on GNOME.

## Features

- **Capture** - full screen (all monitors), an interactive region with live dimensions, or the active window; configurable delay, optional cursor.
- **Annotate before the shot** - the selection overlay is a canvas: arrows, shapes, text, blur and numbered steps on the frozen screen; Enter burns them into the crop.
- **Post-capture editor** - opens automatically if you want it: 12 tools including highlight, pixelate, smart eraser and crop, with undo/redo and zoom.
- **Record GIF & video** - region, full screen or window → GIF, MP4 or WebM, with optional system and microphone audio. <kbd>Ctrl</kbd>+<kbd>Esc</kbd> always stops.
- **Extract text & codes** - OCR any region to copy its text, or point it at a QR/barcode to copy the payload.
- **Upload anywhere** - custom HTTP destinations, ShareX `.sxcu` import, FTP/SFTP, built-in hosts (catbox, Imgur…); the link auto-copies.
- **History** - every capture in a thumbnail grid; deleting moves the file to the trash.
- **Tray, hotkeys & 9 themes** - quick-menu tray icon, fully rebindable global hotkeys, nine palettes including one that follows your system light/dark scheme and accent color.
- **Languages** - English, Polish, Spanish and Italian; follows your system locale or pick one in Settings.

## Build from source

Needs **Qt 6.5+**, CMake and Ninja:

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build && ./build/unisic
```

Per-distro dev packages and the optional features (recording / OCR / AI cutout) are listed in [CONTRIBUTING.md](CONTRIBUTING.md#building).

## Privacy

Unisic collects nothing. No telemetry, no crash reporting, no analytics, no account. The only network requests it makes are to `api.github.com` and `github.com` to check for a newer release - only the latest version tag is fetched, nothing about you is sent.

## Contributing

Issues and pull requests welcome. Found a bug? [File an issue](https://github.com/unisic/unisic/issues) with your desktop, compositor, GPU and logs. See [CONTRIBUTING.md](CONTRIBUTING.md) for project layout and build instructions.

## Development

Unisic is developed with agentic AI assistance (see [`AGENTS.md`](AGENTS.md) for the contributor guide those agents follow). Every generated change is read line by line and reviewed by the maintainer before it lands - the tooling speeds things up, but nothing merges unread, so the codebase stays free of unreviewed machine output and its usual mistakes. Bug reports are still the best safety net: if something slipped through, please [file an issue](https://github.com/unisic/unisic/issues).

## License

**GNU GPL v3.** See [LICENSE](LICENSE). You are free to use, study, modify, and redistribute Unisic - including commercially - but any distributed derivative must also be released under GPL v3 with full source. This keeps the project and every fork of it open, forever.

## Credits

Built by [@DeBondor](https://github.com/DeBondor) & [@D3anDark](https://github.com/D3anDark). Inspired by [Flameshot](https://flameshot.org/) and [Spectacle](https://apps.kde.org/spectacle/).
