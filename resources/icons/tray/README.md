# Bundled tray-icon presets

Drop `.svg` or `.png` files here. Each one becomes a selectable tile in
**Settings → Appearance → System tray icon**, shipped inside the app (compiled
into the Qt resource bundle, so they work on every machine without the user
copying anything).

Notes:
- `file(GLOB …)` in `CMakeLists.txt` picks these up at **configure** time — after
  adding/removing a file, re-run `cmake -B build …` (or `cmake --build build`
  after touching CMakeLists) so the resource list refreshes.
- The file's base name is shown as the tile's tooltip, so name them sensibly
  (e.g. `mono-light.svg`, `classic.png`).
- Square icons render best; SVG scales crispest on HiDPI trays.
- The app's own default icon is `../unisic.svg` (the "Default" tile) — no need to
  duplicate it here.
