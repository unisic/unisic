# Flatpak / Flathub

Everything needed to build Unisic as a Flatpak and to submit it to Flathub.

- `app.unisic.Unisic.yml` - the manifest. This exact file is what the Flathub
  submission repository contains, at its root.
- `build.sh` - build, install and run the bundle locally, and run the linter
  Flathub reviewers run.

## Why the app behaves differently inside the sandbox

The build is the same code, with every compile-time gate satisfied here just as
it is in the native packages; the paths below detect `FLATPAK_ID` and take the
sandbox-appropriate route. Each is a real limitation of the sandbox, not a
preference:

| Path | Outside | Inside the sandbox |
| --- | --- | --- |
| `KWinScreenShot2::isAvailable` | KWin's fast, silent capture on Plasma | skipped: KWin authorizes the restricted interface by the caller's executable path against the installed `.desktop`, which no sandboxed client can match, so every capture would pay a failed auth round-trip before falling back to the portal |
| `registerHostAppId` / `PortalGlobalShortcuts::registerAppId` | self-assigns the app id so portal permissions are keyed per app | skipped: identity comes from the sandbox metadata |
| `PortalScreenshot::candidateAppIds` | grants the silent-screenshot permission to the desktop-file id, the systemd scope and the anonymous `""` host bucket | grants it to the flatpak id only |
| `execStagedUpdate` | execs a staged newer build | never: the bundle is read-only and running a downloaded binary is exactly what the store forbids |
| `UpdateChecker` | checks GitHub, self-updates or offers `install.sh` | `installKind() == "flatpak"`, `updatesManagedExternally()` true: no check at all, and the Updates pane says Flatpak owns updates (see below - `install.sh` does the updating, from the host) |
| autostart | writes `~/.config/autostart/app.unisic.Unisic.desktop` | `org.freedesktop.portal.Background.RequestBackground` with `autostart: true`: the host-side entry is the portal's to write, and the granted answer is persisted as `ui/portalAutostartGranted` because the portal has no getter |

## What the manifest bundles, and why

Unisic has no optional dependencies: every gate in `CMakeLists.txt` is a hard
requirement and configure stops with the missing package named. A Flatpak is
therefore not a place to leave something out - if a module below is dropped,
the `unisic` module does not configure at all, which is the point. Nothing here
ships with a feature quietly compiled away.

The KDE runtime covers Qt 6, KF6 (including KGuiAddons for the Klipper
clipboard hint), PipeWire, the X11/xcb libraries the X11 session paths need,
and the Qt private headers behind the KWin screencast path. On top of it the
manifest builds:

- **x264 + ffmpeg** - the runtime's own ffmpeg has libvpx and AV1 but **no
  software H.264 encoder at all**, so MP4 recording would only work on a
  machine with a VAAPI-capable GPU, and the GIF pipeline's lossless
  intermediate (`libx264rgb`) would fail outright. Unisic is GPL-3.0-or-later,
  so the GPL build is fine. `org.freedesktop.Platform.ffmpeg-full` is not an
  option here: it stops at branch 24.08 while the KDE 6.11 runtime is 25.08.
- **leptonica + tesseract + `eng`/`pol`/`osd` traineddata** - OCR. The sandbox
  cannot read host langpacks and `/app` is read-only at runtime, so the shipped
  data is the data OCR can use. `osd` is not a language: it is the
  orientation-and-script model behind the script auto-detection that Settings
  turns on by default, and it is the single largest file in the bundle at
  10 MB.
- **zxing-cpp** - QR and barcode payloads inside the OCR path.
- **wl-clipboard** - `wl-copy`, the Wayland clipboard mirror.
- **libssh2 + curl** - the SFTP and SCP upload destinations. The runtime has
  curl, but its libcurl is linked against no SSH backend, so those destinations
  would fail inside the sandbox while the destination editor kept offering
  them. Neither ships a shared library on purpose: libssh2 is linked into
  libcurl, libcurl into the one `/app/bin/curl` that `UploadManager` spawns, and
  nothing is left in `/app/lib` to shadow the runtime's `libcurl.so.4` for
  anything else in the sandbox.
- **layer-shell-qt** - the styled notification card, the capture overlay above
  fullscreen windows, and the pinned preview.
- **libevdev + mtdev + libinput** - the pressed-key badge and the click ripple
  drawn over a recording. `libudev` comes from the runtime, libinput does not,
  and neither do its own dependencies. Bundling is only half of it: the feature
  also needs `--device=input` and `--filesystem=/run/udev:ro` below, or it opens
  no device and reports no permission.

## Permissions

Every hole in `finish-args` is justified in a comment next to it. The short
version: Wayland (plus fallback X11 for Xorg sessions), `--device=dri` for
hardware encoding, network for uploads, PulseAudio for sound cues and recorded
audio, the PipeWire socket for the per-application audio picker, the two XDG
save directories, `--device=input` plus a read-only `/run/udev` for the input
overlays, three bus names (tray, notifications, KDE global shortcuts) and the
permission store. Screenshots, screen recording and file dialogs go through
portals and need nothing static.

Two of them need a word to a reviewer:

- `--talk-name=org.freedesktop.impl.portal.PermissionStore` self-grants the
  Screenshot portal permission once, so the overlay freeze does not raise a
  portal dialog on every capture. Spectacle and Flameshot do the same; a
  screenshot tool that asks per capture is unusable.
- `--device=input` with `--filesystem=/run/udev:ro` is what makes the
  pressed-key badge and the click ripple work. libinput's udev backend reads
  the host udev database for the `ID_INPUT_*` properties that tell a keyboard
  from a touchpad, and flatpak does not expose that database by default, so
  without the second one the first one buys nothing: libinput enumerates zero
  devices and the app reports a permission problem the user cannot fix. Both
  are as narrow as flatpak allows: `--device=input` is `/dev/input` and nothing
  else (not `--device=all`), and the udev hole is the database, read-only.

## How a user gets it, and how it updates

Two ways in, and the same script owns both afterwards:

- `flatpak install flathub app.unisic.Unisic` once the listing is live, or the
  software centre, which is the normal Flatpak route.
- `scripts/install.sh` -> Install or update -> "Install the Flatpak version".
  The entry only appears where the `flatpak` command exists.

Updating has to come from outside the sandbox. An app that could update itself
would need `--talk-name=org.freedesktop.Flatpak`, which is a sandbox escape and
is rejected by Flathub, which is why `updatesManagedExternally()` turns the
Updates pane into an explanation. `install.sh` runs on the host, so it can:

- `flathub_has_app()` asks `flathub.org/api/v2/appstream/app.unisic.Unisic`
  whether the listing exists yet. While it does not, the installer takes the
  `unisic-<version>-x86_64.flatpak` bundle attached to each GitHub release (a
  bundle carries no remote to update from, so a second bundle lands with
  `--reinstall`; `~/.var/app` is untouched by that).
- Once the listing is live the same run moves the install onto the flathub
  remote, and from then on it is a plain `flatpak update` - the user's software
  centre keeps it current like everything else. Nothing has to be re-run by
  hand for that switch to happen.
- The installer's daily auto-update timer covers a `--user` install. A
  `--system` one it refuses on purpose: that needs the password every time, and
  a background timer has nowhere to ask.

The release asset comes from `.github/workflows/flatpak.yml`, which `release.yml`
calls with `local: true` - at that point in the pipeline the `v<version>` tag
the manifest pins does not exist yet, so the checkout is what gets built.

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
   the output is a real failure. The same three are hardcoded as the `ALLOWED`
   set in `.github/workflows/flatpak.yml`, so if a permission change ever adds
   a fourth expected code, it has to be added in both places or every CI run
   fails on it.
4. **Fork <https://github.com/flathub/flathub>**, branch from `new-pr` (not
   `master`), and add exactly two files at the repository root:
   `app.unisic.Unisic.yml` (this manifest, unchanged) and nothing else unless a
   patch is genuinely needed. Open the PR titled `Add app.unisic.Unisic`.
5. **Answer the reviewer.** Expect questions about the permission store, about
   `--device=input` with `/run/udev` (the Permissions section above is the
   answer: the overlays read event devices through libinput, and the udev
   database is what makes a device identifiable at all), about bundling ffmpeg,
   and about the app ID: `app.unisic.Unisic` is backed by
   <https://unisic.app>, which is under the project's control and serves over
   HTTPS, so the reverse-DNS id is legitimate and needs no `io.github.` prefix.
6. **After acceptance** the app moves to `flathub/app.unisic.Unisic`. Updates
   never go through the submission process again: push a manifest bump (new
   tag + commit) to that repository and the buildbot publishes it. Enable 2FA
   on GitHub and accept the invite within a week.

## Keeping it current

- The manifest carries `x-checker-data` for x264, ffmpeg, layer-shell-qt,
  libinput and Unisic itself, so Flathub's external-data-checker opens update
  PRs automatically. The rest are pinned by hand.
- The runtime version is not automatic. When KDE publishes a newer branch, bump
  `runtime-version`, rebuild, and re-run the linter.
- Any new runtime dependency has to be added here as a module with a pinned
  source and checksum: the Flathub build machines have no network. Since every
  gate is a hard build requirement, a dependency that lands in `CMakeLists.txt`
  and not here does not degrade this channel, it breaks the build - which is
  the intended failure mode, and one worth noticing before a release rather
  than after.
