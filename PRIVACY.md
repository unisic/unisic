# Privacy policy

Unisic has no telemetry, no analytics, no crash reporting and no account.
Screenshots, recordings, settings, the capture history, OCR, QR code reading
and background removal all stay on your computer. The capture counter behind
unisic.app/stats is a local file (`unisic.stats`) that the page reads in your
browser; Unisic never sends it anywhere.

Unisic connects to the network only in the cases below. Apart from the update
check, each one starts from something you do.

## Automatic: update check

- What: one HTTPS request to `api.github.com` asking for the latest Unisic
  release. It carries the HTTP headers every request has, with the user agent
  `Unisic/<version> (screenshot tool)`, so GitHub sees your IP address, the
  time and the Unisic version. Nothing else is sent.
- When: about 10 seconds after Unisic starts, then once every 24 hours.
- Download: where Unisic updates itself (the AppImage, the Windows installer
  and zip, an unpacked tarball outside `/usr`), a newer release is then
  downloaded from `github.com` and checked against the SHA-256 GitHub
  publishes for it. Distribution packages are updated by your package manager
  instead; an update is installed only after you press Install now.
- Default: on. Off in development builds, in the Flatpak (Flathub updates it)
  and in the AUR package (your AUR helper updates it).
- Turn it off: Settings > General > Updates > Automatic updates. Check now
  in the same place makes one check when you press it.
- Third party: GitHub,
  [privacy statement](https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement).

## When you ask: uploads

- What: the capture or recording you upload, its file name, and any keys or
  tokens you entered for that server, sent to the upload server.
- When: only when you press Upload, or after every capture if you turned on
  "Upload and copy the link" (off by default).
- Which server: the one on top of your list on the Servers page (catbox.moe
  by default), or the one you pick from the capture card. If that server
  fails, the Failure Chain switch on the Servers page (on by default) sends
  the file to the next ready server on your list. A server picked by hand is
  never swapped. Turn the switch off to upload only to your chosen server.
- Third parties, each with its own terms: the built-in servers are
  [catbox.moe and litterbox.catbox.moe](https://catbox.moe/legal.php),
  [uguu.se](https://uguu.se/faq.html),
  [tmpfiles.org](https://tmpfiles.org/tos),
  [buzzheavier.com](https://buzzheavier.com/),
  [vgy.me](https://vgy.me/privacy-policy) and
  [Imgur](https://imgur.com/privacy). Servers you add yourself (HTTP, FTP,
  SFTP, S3) receive what you configure them to receive. FTP, SFTP, PUT and S3
  uploads, and buzzheavier.com, go through the `curl` program.

## When you ask: link shortener

- What: the link of an uploaded file, sent to is.gd or TinyURL.
- When: after an upload, only if "Shorten the link" is on (off by default,
  Settings > Capture).
- Third parties: [is.gd](https://is.gd/privacy.php),
  [TinyURL](https://tinyurl.com/app/privacy-policy).

## When you ask: background removal download (Linux)

- What: a download of the ONNX Runtime library and the background removal
  model from a GitHub release page (`github.com/unisic/unisic-models`). Nothing is uploaded; the model runs on
  your computer.
- When: only when you press Download in Settings > Editor.
- Third party: GitHub, see above.

## When you ask: installing an update of a distribution package

- What: Install now (Settings or the tray menu) downloads
  `https://unisic.app/install.sh` (same user agent as the update check) and
  runs it in a terminal. The script downloads the package from GitHub and
  installs it with your package manager.
- When: only when you press Install now.

## Links you open

Buttons such as the release notes, the documentation, the stats page, Ko-fi
and the upload server's account pages open your web browser. From then on the
browser makes the request, not Unisic.

## Installers and packages

The distribution packages register the Unisic repository (openSUSE Build
Service or Fedora COPR) with your package manager, which then checks it when
you update your system. The optional automatic update timer of `install.sh`
is off until you turn it on in its menu.

## Changes

This file lives in the Unisic repository, so every change to it is in the
project's history:
https://github.com/unisic/unisic/commits/HEAD/PRIVACY.md
