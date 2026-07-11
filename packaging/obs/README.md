# OBS distribution channel

Auto-updating signed repos for **Debian 13, Ubuntu 25.10/26.04, Arch, openSUSE
Tumbleweed + Leap 16.0**, built on the [openSUSE Build Service](https://build.opensuse.org)
project `home:unisic`. Fedora is NOT built here — it stays on COPR
`deandark/Unisic` (Packit, see `.packit.yaml`).

## Data flow

```
push to main with version bump
  → release.yml builds assets, `release` job creates tag v<ver> + GitHub release
  → `obs` job POSTs the runservice trigger token to api.opensuse.org
  → OBS re-runs source services:
      obs_scm       clones main, version = newest v* tag (v stripped)
      extract_file  pulls unisic.spec / PKGBUILD / unisic.dsc / debian.* out of the checkout
      (buildtime)   tar → recompress(gz) → set_version rewrites spec/dsc/changelog/PKGBUILD versions
  → per-target builds → signed repos published on download.opensuse.org
  → users get the update via apt upgrade / zypper up / pacman -Syu
```

**Invariant: the OBS package contains exactly ONE committed file — `_service`.**
Every other file (spec, PKGBUILD, debian.*, dsc) is owned by git and re-extracted
from the release tarball on every trigger. Never edit files in the OBS web UI —
the next service run overwrites them.

## File map

| Git file | Feeds |
|---|---|
| `unisic.spec` (repo root) | openSUSE TW + Leap 16.0 (and COPR Fedora via Packit) |
| `packaging/arch/PKGBUILD` | Arch (and the CI release asset) |
| `packaging/obs/unisic.dsc` + `debian.control/rules/changelog` | Debian 13, Ubuntu (via debtransform) |
| `packaging/obs/_service` | source-of-truth copy of the one file living in OBS |
| `packaging/obs/home_unisic.key` | project signing key (armored), installed as `/usr/share/unisic/obs-signing-key.asc` by CMake |
| `packaging/arch/unisic.install` | pacman post_install note offering the OBS repo |
| `packaging/deb/postinst`+`postrm`, `packaging/rpm/copr-post*.sh` | CI (CPack) packages ONLY: self-register the OBS apt repo / COPR dnf repo so direct downloads update natively; repo-built packages skip them on purpose |

Sync rules: `debian.control` Depends mirrors the CPack DEB block in
`CMakeLists.txt`; `unisic.dsc` Build-Depends mirrors `debian.control`;
both mirror the debian:trixie CI job in `release.yml`. If the OBS project
key is ever rotated, re-download it into `packaging/obs/home_unisic.key`
(`https://build.opensuse.org/projects/home:unisic/signing_keys/download?kind=gpg`)
and update the fingerprint in `packaging/arch/unisic.install`.

## One-time setup

Requires the OBS account `unisic` (project `home:unisic` — already created,
signing key committed as `packaging/obs/home_unisic.key`) and `osc`
configured (`osc whois` to test).

1. Create the project:

   ```sh
   osc meta prj home:unisic -F - <<'XML'
   <project name="home:unisic">
     <title>Unisic</title>
     <description>Auto-updating release repos for Unisic (Debian, Ubuntu, openSUSE, Arch).
   Fedora is served from COPR deandark/Unisic. Rebuilt on each GitHub release via runservice token.</description>
     <person userid="unisic" role="maintainer"/>
     <publish><enable/></publish>
     <repository name="openSUSE_Tumbleweed">
       <path project="openSUSE:Factory" repository="snapshot"/>
       <arch>x86_64</arch>
     </repository>
     <repository name="16.0">
       <path project="openSUSE:Leap:16.0" repository="standard"/>
       <arch>x86_64</arch>
     </repository>
     <repository name="Debian_13">
       <path project="Debian:13" repository="standard"/>
       <arch>x86_64</arch>
     </repository>
     <repository name="xUbuntu_25.10">
       <path project="Ubuntu:25.10" repository="universe"/>
       <arch>x86_64</arch>
     </repository>
     <repository name="xUbuntu_26.04">
       <path project="Ubuntu:26.04" repository="universe"/>
       <arch>x86_64</arch>
     </repository>
     <repository name="Arch" rebuild="local">
       <path project="Arch:Extra" repository="standard"/>
       <arch>x86_64</arch>
     </repository>
   </project>
   XML
   ```

2. Create the package:

   ```sh
   osc meta pkg home:unisic unisic -F - <<'XML'
   <package name="unisic" project="home:unisic">
     <title>Unisic</title>
     <description>Screen capture/annotate/record for Wayland</description>
   </package>
   XML
   ```

3. Commit `_service` (the only OBS-side file):

   ```sh
   osc co home:unisic unisic && cd home:unisic/unisic
   cp <repo>/packaging/obs/_service .
   osc add _service
   osc commit -m "source services: obs_scm + extract_file from github.com/unisic/unisic"
   ```

   While iterating pre-merge, point `revision` at the feature branch (the
   packaging files must exist on a pushed branch for extract_file to find
   them); flip back to `main` before the final commit and mirror any
   `_service` change back into `packaging/obs/_service`.

4. Run + inspect the services:

   ```sh
   osc service remoterun home:unisic unisic
   osc ls -e home:unisic unisic
   # expect: _service:obs_scm:unisic-<ver>.obscpio + .obsinfo,
   #         _service:extract_file:{unisic.spec,PKGBUILD,unisic.dsc,debian.control,debian.rules,debian.changelog}
   ```

5. Watch builds, iterate:

   ```sh
   osc results home:unisic unisic
   osc buildlog home:unisic unisic Debian_13 x86_64
   # local reproduction (much faster iteration):
   osc build openSUSE_Tumbleweed x86_64   # runs in a local chroot
   osc build Debian_13 x86_64             # runs debtransform locally
   osc build Arch x86_64
   ```

   Expected first-build iteration points: openSUSE runtime `Requires:` names
   in `unisic.spec` (`qt6-declarative-imports`, `qt6-svg-imageformat` — verify
   against the build log / `rpm -qp --requires`), Leap 16.0 availability of
   `zxing-cpp`/`layer-shell-qt` devel packages (fallback: `%if` them out —
   the features compile out gracefully).

6. Create the trigger token and GitHub secret:

   ```sh
   osc token --create --operation runservice home:unisic unisic
   gh secret set OBS_TOKEN --repo unisic/unisic   # paste the token string
   ```

   Dry-run from the workstation (same call the `obs` job makes):

   ```sh
   curl -sS -X POST -H "Authorization: Token <SECRET>" \
     "https://api.opensuse.org/trigger/runservice?project=home:unisic&package=unisic"
   ```

7. Note the signing key fingerprint and **expiry**:

   ```sh
   osc signkey home:unisic
   ```

## Maintenance

- **Signing-key expiry (IMPORTANT)**: OBS project keys expire and are NOT
  auto-extended. An expired key hard-breaks `apt update` for every
  Debian/Ubuntu user (`EXPKEYSIG`) and zypper/pacman verification. Before
  expiry run `osc signkey --extend home:unisic`, then retrigger a
  build so the refreshed key propagates into `Release.key`/`repomd.xml.key`.
  Set a calendar reminder when creating the project.
- **Ubuntu 25.10 EOL (July 2026)**: drop the target with
  `osc meta prj -e home:unisic` (delete the repository block); add
  new Ubuntu/Fedora-style targets the same way (verified names come from
  `https://api.opensuse.org/public/distributions`).
- **Manual retrigger**: `osc service remoterun home:unisic unisic`.
- **Publish lag**: download.opensuse.org can lag the build by minutes up to
  ~1h (mirror sync) — "repo still has the old version" right after a release
  is usually just that.
- **Known race**: `obs_scm` checks out `main` HEAD, not the tag. A push
  landing between the release tag and the trigger ships HEAD content under
  the tag's version. Low probability (the trigger fires minutes after the
  tag, on the same commit); self-corrects on the next release.

## Don'ts

- Don't edit package files in the OBS web UI (overwritten by the next run).
- Don't add Fedora targets here — Fedora is COPR's job (`.packit.yaml`).
- Don't rename `packaging/obs/debian.*` files — `extract_file` globs and the
  debtransform `debian.<x>` → `debian/<x>` convention both depend on the names.
