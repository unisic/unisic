# Flatpak / Flathub

Everything needed to build Unisic as a Flatpak and to submit it to Flathub.

- `app.unisic.Unisic.yml` - the manifest. This exact file is what the Flathub
  submission repository contains, at its root.
- `build.sh` - build, install and run the bundle locally, and run the linter
  Flathub reviewers run.

## Why the app behaves differently inside the sandbox

The build is the same code; five paths detect `FLATPAK_ID` and take the
sandbox-appropriate route. Each is a real limitation of the sandbox, not a
preference:

| Path | Outside | Inside the sandbox |
| --- | --- | --- |
| `KWinScreenShot2::isAvailable` | KWin's fast, silent capture on Plasma | skipped: KWin authorizes the restricted interface by the caller's executable path against the installed `.desktop`, which no sandboxed client can match, so every capture would pay a failed auth round-trip before falling back to the portal |
| `registerHostAppId` / `PortalGlobalShortcuts::registerAppId` | self-assigns the app id so portal permissions are keyed per app | skipped: identity comes from the sandbox metadata |
| `PortalScreenshot::candidateAppIds` | grants the silent-screenshot permission to the desktop-file id, the systemd scope and the anonymous `""` host bucket | grants it to the flatpak id only |
| `execStagedUpdate` | execs a staged newer build | never: the bundle is read-only and running a downloaded binary is exactly what the store forbids |
| `UpdateChecker` | checks GitHub, self-updates or offers `install.sh` | `installKind() == "flatpak"`, `updatesManagedExternally()` true: no check at all, and the Updates pane says Flatpak owns updates |
| autostart | writes `~/.config/autostart/app.unisic.Unisic.desktop` | `org.freedesktop.portal.Background.RequestBackground` with `autostart: true`: the host-side entry is the portal's to write, and the granted answer is persisted as `ui/portalAutostartGranted` because the portal has no getter |

## What the manifest bundles, and why

The KDE runtime covers Qt 6, KF6 (including KGuiAddons for the Klipper
clipboard hint) and PipeWire. On top of it the manifest builds:

- **x264 + ffmpeg** - the runtime's own ffmpeg has libvpx and AV1 but **no
  software H.264 encoder at all**, so MP4 recording would only work on a
  machine with a VAAPI-capable GPU, and the GIF pipeline's lossless
  intermediate (`libx264rgb`) would fail outright. Unisic is GPL-3.0-or-later,
  so the GPL build is fine. `org.freedesktop.Platform.ffmpeg-full` is not an
  option here: it stops at branch 24.08 while the KDE 6.11 runtime is 25.08.
- **leptonica + tesseract + `eng`/`pol` traineddata** - OCR. The sandbox cannot
  read host langpacks and `/app` is read-only at runtime, so the shipped
  languages are the languages OCR can offer.
- **zxing-cpp** - QR and barcode payloads inside the OCR path.
- **wl-clipboard** - `wl-copy`, the Wayland clipboard mirror.
- **layer-shell-qt** - the styled notification card, the capture overlay above
  fullscreen windows, and the pinned preview. Without it those fall back to the
  XWayland helper path, which also works.

## Permissions

Every hole in `finish-args` is justified in a comment next to it. The short
version: Wayland (plus fallback X11 for Xorg sessions), `--device=dri` for
hardware encoding, network for uploads, PulseAudio for sound cues and recorded
audio, the PipeWire socket for the per-application audio picker, the two XDG
save directories, three bus names (tray, notifications, KDE global shortcuts)
and the permission store. Screenshots, screen recording and file dialogs go
through portals and need nothing static.

`--talk-name=org.freedesktop.impl.portal.PermissionStore` is the one that needs
a word to a reviewer: it self-grants the Screenshot portal permission once, so
the overlay freeze does not raise a portal dialog on every capture. Spectacle
and Flameshot do the same; a screenshot tool that asks per capture is unusable.

## Build and test locally

Needs `flatpak` and `flatpak-builder`, plus the KDE 6.11 runtime and SDK
(~2.5 GB on first run):

```sh
flatpak install -y flathub org.kde.Platform//6.11 org.kde.Sdk//6.11
```

Then, from the repository root:

```sh
packaging/flatpak/build.sh            # build the manifest as-is (git tag source)
packaging/flatpak/build.sh --local    # build the working tree instead of the tag
packaging/flatpak/build.sh --run      # build, install into the user repo, run it
packaging/flatpak/build.sh --lint     # run the Flathub linter on the manifest
```

`--local` swaps the `unisic` module's git source for a `dir` source, which is
how to test an unreleased change. Never submit a manifest built that way: the
submitted one must point at a tag with its commit.

## Submitting to Flathub

Read <https://docs.flathub.org/docs/for-app-authors/submission> first; this is
the short form of it, with the Unisic-specific answers filled in.

1. **Tag the release.** The manifest pins `tag:` plus the `commit:` that tag
   resolves to. Bump both, and make sure `resources/app.unisic.Unisic.metainfo.xml`
   has a `<release>` entry for that exact version - Flathub shows it, and the
   linter fails without it.
2. **Check the metainfo** (`appstreamcli validate --pedantic
   resources/app.unisic.Unisic.metainfo.xml`). The one pedantic hint about an
   uppercase character in the component ID is expected and accepted.
3. **Build and lint locally**:
   ```sh
   packaging/flatpak/build.sh --run
   packaging/flatpak/build.sh --lint
   ```
   Three linter errors are expected and are not something to fix here.
   `finish-args-portal-impl-permissionstore-talk-name` is the permission-store
   hole below, which a reviewer grants as an exception. The two screenshot ones
   (`appstream-screenshots-not-mirrored-in-ostree`,
   `appstream-external-screenshot-url`) only clear on Flathub's own build
   service, which mirrors the screenshots to dl.flathub.org. Anything else in
   the output is a real failure.
4. **Fork <https://github.com/flathub/flathub>**, branch from `new-pr` (not
   `master`), and add exactly two files at the repository root:
   `app.unisic.Unisic.yml` (this manifest, unchanged) and nothing else unless a
   patch is genuinely needed. Open the PR titled `Add app.unisic.Unisic`.
5. **Answer the reviewer.** Expect questions about the permission store, about
   bundling ffmpeg, and about the app ID: `app.unisic.Unisic` is backed by
   <https://unisic.app>, which is under the project's control and serves over
   HTTPS, so the reverse-DNS id is legitimate and needs no `io.github.` prefix.
6. **After acceptance** the app moves to `flathub/app.unisic.Unisic`. Updates
   never go through the submission process again: push a manifest bump (new
   tag + commit) to that repository and the buildbot publishes it. Enable 2FA
   on GitHub and accept the invite within a week.

## Keeping it current

- The manifest carries `x-checker-data` for ffmpeg, layer-shell-qt and Unisic
  itself, so Flathub's external-data-checker opens update PRs automatically.
- The runtime version is not automatic. When KDE publishes a newer branch, bump
  `runtime-version`, rebuild, and re-run the linter.
- Any new runtime dependency has to be added here as a module with a pinned
  source and checksum: the Flathub build machines have no network.
