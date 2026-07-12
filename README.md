<div align="center">

<img src="resources/icons/unisic.svg" width="160" height="160" alt="Unisic" />

# Unisic

**Most snipping tools stop at a screenshot. Unisic is everything that should happen after.**

Silent capture · Annotate · Object cutout · Record GIF/MP4/WebM · Upload · Zero telemetry · GPLv3

[![Download latest release](https://img.shields.io/badge/Download_Latest_Release-C8ACD6?style=for-the-badge&logo=linux&logoColor=17153B)](https://github.com/unisic/unisic/releases/latest)

<p>
  <img alt="Linux Wayland" src="https://img.shields.io/badge/Linux-Wayland-000?style=for-the-badge&color=2E236C">
  <a href="https://github.com/unisic/unisic/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/unisic/unisic?include_prereleases&style=for-the-badge&label=release&color=C8ACD6"></a>
  <img alt="License" src="https://img.shields.io/badge/license-GPLv3-000?style=for-the-badge&color=433D8B">
</p>
<br />

<img src="docs/screenshots/editor.png" width="99%" alt="Unisic post-capture editor" />
<img src="docs/screenshots/history.png" width="49%" alt="Unisic history page" />
<img src="docs/screenshots/destinations.png" width="49%" alt="Unisic destinations page" />

</div>

## What is Unisic

Most screenshot tools on Linux hand you a rectangle of pixels and walk away. Unisic covers the whole workflow after you press the hotkey: **annotate on the selection overlay before the shot is even taken**, edit afterwards (blur, pixelate, numbered steps, crop, object cutout with background removal), record the same region as a GIF or video, and push the result wherever it belongs — clipboard, disk, or a custom upload destination with the link ready to paste.

Built for **Linux Wayland** on legitimate APIs only (xdg-desktop-portal, KWin ScreenShot2, PipeWire, KGlobalAccel, wlr-screencopy). KDE Plasma gets the fully silent native path; other desktops work through portals. **C++20 · Qt 6 · QML**, fully custom UI.

## Features

- **Capture** — the full screen (all monitors stitched), an interactive region on a frozen per-monitor overlay with live dimensions, or the active window. Silent KWin path on Plasma, portals elsewhere, `grim` on wlroots; configurable delay and optional cursor.
- **Annotate before the shot** — the selection overlay is a canvas: draw arrows, shapes, text, blur and numbered steps on the frozen screen, then press Enter and they're burnt into the crop.
- **Post-capture editor** — opens automatically (optional): 12 tools including highlight, pixelate, smart eraser, numbered steps and crop, with undo/redo and zoom, composited in image-pixel space.
- **Object cutout** — lift the subject out of your selection and drop the background: a dependency-free segmenter by default, with optional U-2-Net (AI) for tricky edges. Exports as transparent PNG/WebP.
- **Record GIF & video** — GIF (two-pass palette) or MP4/WebM through the ScreenCast portal → PipeWire → ffmpeg; region, full screen, or window, with optional system and microphone audio. `Ctrl+Esc` always stops.
- **Extract text & codes** — OCR any region to copy its text (Tesseract), or decode a QR/barcode straight to its payload.
- **Upload anywhere** — custom HTTP destinations, ShareX `.sxcu` import, FTP/SFTP via curl, and built-in hosts (catbox, 0x0.st, Imgur…); the link auto-copies.
- **History** — every capture in a thumbnail grid; deleting moves the file to the trash, and external deletions are picked up automatically.
- **Tray, hotkeys & 9 themes** — a quick-menu tray icon, fully rebindable global hotkeys, and nine palettes — including one that follows your system light/dark scheme and accent color.
- **Languages** — English, Polish, Spanish and Italian; follow your system locale or pick one in Settings.

## Default hotkeys

| Keys | Description |
| --- | --- |
| <kbd>Meta</kbd> + <kbd>Shift</kbd> + <kbd>1</kbd> | Capture the full screen |
| <kbd>Meta</kbd> + <kbd>Shift</kbd> + <kbd>2</kbd> | Capture a region |
| <kbd>Meta</kbd> + <kbd>Shift</kbd> + <kbd>3</kbd> | Capture the active window |
| <kbd>Meta</kbd> + <kbd>Shift</kbd> + <kbd>G</kbd> | Record a GIF (region) |
| <kbd>Meta</kbd> + <kbd>Shift</kbd> + <kbd>R</kbd> | Record video (region) |
| <kbd>Meta</kbd> + <kbd>Shift</kbd> + <kbd>T</kbd> | OCR — copy text out of a region |
| <kbd>Meta</kbd> + <kbd>Shift</kbd> + <kbd>C</kbd> | Copy the last capture again |
| <kbd>Ctrl</kbd> + <kbd>Esc</kbd> | Stop recording (fixed emergency stop) |

Editable in Settings → Hotkeys (applied to the system immediately) or in KDE's Shortcuts KCM.



## Install

Requires a Wayland session with `xdg-desktop-portal`; recording also needs PipeWire and `ffmpeg`.

Install from a repository for automatic updates through your package manager — or grab the self-updating **[AppImage](https://github.com/unisic/unisic/releases/latest)**, or [build from source](#build-from-source).

### Fedora

```sh
sudo dnf copr enable deandark/Unisic
sudo dnf install unisic
```

### Debian / Ubuntu

```sh
REPO=Debian_13   # or xUbuntu_26.04 / xUbuntu_25.10
curl -fsSL "https://download.opensuse.org/repositories/home:/unisic/${REPO}/Release.key" \
  | gpg --dearmor | sudo tee /etc/apt/keyrings/home_unisic.gpg > /dev/null
echo "deb [signed-by=/etc/apt/keyrings/home_unisic.gpg] https://download.opensuse.org/repositories/home:/unisic/${REPO}/ ./" \
  | sudo tee /etc/apt/sources.list.d/home_unisic.list
sudo apt update && sudo apt install unisic
```

### openSUSE

```sh
# Tumbleweed (for Leap 16.0 replace openSUSE_Tumbleweed with 16.0)
sudo zypper addrepo https://download.opensuse.org/repositories/home:unisic/openSUSE_Tumbleweed/home:unisic.repo
sudo zypper refresh
sudo zypper install unisic
```

### Arch

```sh
curl -fsSL 'https://build.opensuse.org/projects/home:unisic/signing_keys/download?kind=gpg' -o /tmp/unisic-obs.key
sudo pacman-key --add /tmp/unisic-obs.key
sudo pacman-key --lsign-key "$(gpg --show-keys --with-colons /tmp/unisic-obs.key | awk -F: '/^fpr/{print $10; exit}')"
printf '\n[home_unisic_Arch]\nServer = https://download.opensuse.org/repositories/home:/unisic/Arch/$arch\n' \
  | sudo tee -a /etc/pacman.conf
sudo pacman -Syu unisic
```

## Build from source

Needs **Qt 6.5+**, CMake and Ninja:

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build && ./build/unisic
```

Per-distro dev packages and the optional features (recording / OCR / AI cutout) are in [CONTRIBUTING.md](CONTRIBUTING.md#building).

## Usage

```sh
unisic --fullscreen | --region | --window | --gif
unisic --export-settings <file> | --import-settings <file>
```

No arguments = background start with tray + main window; a second invocation forwards its command to the running instance (that's how compositor keybinds work). Settings and destinations live in `~/.config/unisic/`, history in `~/.local/share/unisic/`. Filename tokens: `%date%` `%time%` `%datetime%` `%unix%` `%rand%`.

On wlroots compositors (niri, Sway…) install `grim` for capture and bind keys in your compositor config — a running instance picks the command up:

```kdl
binds {
    Mod+Shift+S { spawn "unisic" "--region"; }
    Print { spawn "unisic" "--fullscreen"; }
}
```

## Privacy

Unisic collects nothing. No telemetry, no crash reporting, no analytics, no account. The only network requests it makes are to `api.github.com` and `github.com` to check for a newer release — only the latest version tag is fetched, nothing about you is sent.

## Contributing

Issues and pull requests welcome. Found a bug? [File an issue](https://github.com/unisic/unisic/issues) with your desktop, compositor, GPU and logs. See [CONTRIBUTING.md](CONTRIBUTING.md) for project layout and build instructions.

## License

**GNU GPL v3.** See [LICENSE](LICENSE). You are free to use, study, modify, and redistribute Unisic — including commercially — but any distributed derivative must also be released under GPL v3 with full source. This keeps the project and every fork of it open, forever.

## Credits

Built by [@DeBondor](https://github.com/DeBondor) & [@D3anDark](https://github.com/D3anDark). Inspired by [Flameshot](https://flameshot.org/), [ShareX](https://getsharex.com/), and [Spectacle](https://apps.kde.org/spectacle/).
