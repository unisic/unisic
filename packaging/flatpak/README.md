# Flatpak packaging

Builds Unisic as a Flatpak against `org.kde.Platform` 6.10.

This is **self-distribution**, not a Flathub submission: the bundle is attached
to the GitHub release and users either sideload it or add a Flatpak repo you
host yourself. Nothing here is submitted to or maintained by Flathub.

## Build locally

```sh
flatpak install -y flathub org.kde.Sdk//6.10 org.kde.Platform//6.10
flatpak-builder --force-clean --user --install build-flatpak packaging/flatpak/app.unisic.Unisic.yml
flatpak run app.unisic.Unisic
```

The `unisic` module builds the working tree (`type: dir`), so what you get is
the checkout you ran it from — not a published tag. To build a released version
reproducibly instead, swap that source for the pinned `type: git` block noted in
the manifest.

## Build a distributable bundle

```sh
flatpak-builder --force-clean --repo=repo build-flatpak packaging/flatpak/app.unisic.Unisic.yml
flatpak build-bundle repo unisic.flatpak app.unisic.Unisic
```

Users install it with `flatpak install --user ./unisic.flatpak`. A bundle is a
one-shot sideload: it carries no update channel, so the in-app updater only
notifies and points at `flatpak update` (see `UpdateChecker::installKind()`).

## Host a repo (automatic updates)

To get real updates without Flathub, publish the `repo/` directory over HTTPS —
GitHub Pages works — and hand users a `.flatpakrepo` file:

```ini
[Flatpak Repo]
Title=Unisic
Url=https://<host>/repo/
GPGKey=<base64 of the exported public key>
```

Sign the repo when publishing (`--gpg-sign=<key>`) and re-run
`flatpak build-update-repo repo` after each release. Users add it once with
`flatpak remote-add --user unisic https://<host>/unisic.flatpakrepo`, then
`flatpak update` picks up every later release.

## Sandbox notes

Things that behave differently from a native install, all handled in code:

- **KWin fast path is off.** KWin rejects sandboxed clients on
  `org.kde.KWin.ScreenShot2`, so `KWinScreenShot2::isAvailable()` returns false
  under `FLATPAK_ID` and capture goes through the portal. `org.kde.KWin` is
  still granted — window recording uses `queryWindowInfo`.
- **Portal identity** comes from the sandbox metadata, so the host-app
  `Registry.Register` calls are skipped and the screenshot permission is
  granted for `FLATPAK_ID` only.
- **No self-update.** The sandbox cannot replace its own binary; the updater is
  notify-only here.
- **OCR** uses the bundled tesseract with `eng` + `pol` data (the default
  `ocrLanguages`). Host langpacks are invisible to the sandbox — add more
  languages in the `tessdata` module if you need them.
- **Degraded when absent from the runtime:** `zip` (ZIP export of captures) and
  `pw-dump`/`pw-record` (per-application audio capture). Both are probed with
  `QStandardPaths::findExecutable` and report a clear message when missing.
