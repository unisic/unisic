<div align="center">

<img src="resources/icons/unisic.svg" width="160" height="160" alt="Unisic" />

# Unisic

**Most snipping tools stop at a screenshot. Unisic is everything that should happen after.**

Silent capture · Annotate · Smart eraser · Record GIF/MP4/WebM · Upload · Zero telemetry · GPLv3

**[unisic.app](https://unisic.app)** · **[Documentation](https://unisic.app/docs)**

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

Built for **Linux Wayland** on legitimate APIs only (xdg-desktop-portal, KWin ScreenShot2, PipeWire). KDE Plasma gets the fully silent native path; GNOME and wlroots desktops work through portals - see [compositor support](https://unisic.app/docs/compositors). **C++20 · Qt 6 · QML**, fully custom UI.

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

## Install

Grab a package from the **[latest release](https://github.com/unisic/unisic/releases/latest)**. The AppImage updates itself, and the `.deb` / Fedora `.rpm` / Arch `.pkg.tar.zst` register their native repo on first install - from then on updates arrive through your package manager like any other package.

Per-distro repository setup (Fedora COPR, Debian/Ubuntu, openSUSE, Arch, Nix flake) with copy-paste snippets: **[unisic.app → Download](https://unisic.app/#download)** or the [installation docs](https://unisic.app/docs/installation).

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
| <kbd>Ctrl</kbd> + <kbd>Esc</kbd> | Stop recording (fixed emergency stop) |

Every hotkey is editable in Settings → Hotkeys. The same actions work from the command line (`unisic --region | --fullscreen | --window | --gif`), which is also how compositor keybinds should call it - see the docs for the [full CLI](https://unisic.app/docs/configuration#command-line-interface), [file locations](https://unisic.app/docs/configuration#file-locations) and [wlroots setup](https://unisic.app/docs/compositors).

## Build from source

Needs **Qt 6.5+**, CMake and Ninja:

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build && ./build/unisic
```

Per-distro dev packages and the optional features (recording / OCR) are listed in [CONTRIBUTING.md](CONTRIBUTING.md#building).

## Privacy

Unisic collects nothing: no telemetry, no analytics, no account. The only network request it makes on its own is a release check against GitHub - [details](https://unisic.app/docs/introduction#privacy).

## Contributing

Issues and pull requests welcome. Found a bug? [File an issue](https://github.com/unisic/unisic/issues) with your desktop, compositor, GPU and logs. See [CONTRIBUTING.md](CONTRIBUTING.md) for project layout and build instructions. Unisic is developed with agentic AI assistance ([AGENTS.md](AGENTS.md)); every generated change is read line by line and reviewed by the maintainer before it lands.

## License

**GNU GPL v3.** See [LICENSE](LICENSE) and [what that means](https://unisic.app/docs/introduction#license).

## Credits

Built by [@DeBondor](https://github.com/DeBondor) & [@D3anDark](https://github.com/D3anDark). Inspired by [Flameshot](https://flameshot.org/) and [Spectacle](https://apps.kde.org/spectacle/).
