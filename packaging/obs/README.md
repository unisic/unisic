# OBS distribution channel

Auto-updating signed repos for **Debian 13, Ubuntu 25.10/26.04 and Arch**,
built on the [openSUSE Build Service](https://build.opensuse.org)
project `home:unisic`. rpm distros are NOT built here — Fedora AND openSUSE
(Tumbleweed + Leap 15.6) ship from COPR `deandark/Unisic` (Packit, see
`.packit.yaml`); COPR has no Leap 16 chroot yet, so Leap 16 users take the
Tumbleweed repo or the release rpm.

## Data flow

```
push to main with version bump
  → release.yml builds assets, `release` job creates tag v<ver> + GitHub release
  → `obs` job POSTs the runservice trigger token to api.opensuse.org
  → OBS re-runs source services (ALL server-side — buildtime services can't
    resolve obs-service-* packages in the foreign-distro build roots):
      tar_scm       clones main into unisic-<ver>.tar, version = newest v* tag (v stripped)
      extract_file  pulls unisic.spec / PKGBUILD / unisic.install / unisic.dsc / debian.* out of the tar
      recompress    .tar → .tar.gz (what debtransform expects)
      set_version   rewrites spec/dsc/changelog/PKGBUILD versions to the tag
  → per-target builds → signed repos published on download.opensuse.org
  → users get the update via apt upgrade / pacman -Syu
```

Hard-won details (each cost a broken round — don't regress them):
- `DEBTRANSFORM-TAR` is a LITERAL filename, not a glob — the dsc has no such
  line on purpose; debtransform auto-discovers the single tarball at any version.
- Package builds pass `-DBUILD_TESTING=OFF` (spec + debian.rules) and
  debian.rules has an empty `override_dh_auto_test:`.
- openSUSE (now built on COPR, same `unisic.spec`) needs `pkgconfig(libcurl)`
  (tesseract link interface) and builds with make (no `-G Ninja` — its
  %cmake_build drives make); Fedora keeps Ninja.
- Installed binaries carry no RPATH (CMakeLists sets INSTALL_RPATH "") or
  openSUSE's rpmlint hard-fails the build.

**Invariant: the OBS package contains exactly ONE committed file — `_service`.**
Every other file (spec, PKGBUILD, debian.*, dsc) is owned by git and re-extracted
from the release tarball on every trigger. Never edit files in the OBS web UI —
the next service run overwrites them.

## File map

| Git file | Feeds |
|---|---|
| `unisic.spec` (repo root) | COPR via Packit: Fedora + openSUSE TW/Leap 15.6 (no longer built on OBS) |
| `packaging/arch/PKGBUILD` | Arch (and the CI release asset) |
| `packaging/obs/unisic.dsc` + `debian.control/rules/changelog` | Debian 13, Ubuntu (via debtransform) |
| `packaging/obs/_service` | source-of-truth copy of the one file living in OBS |
| `packaging/obs/home_unisic.key` | project signing key (armored), installed as `/usr/share/unisic/obs-signing-key.asc` by CMake |
| `packaging/arch/unisic.install` | shared by the OBS build AND the CI asset: post_install auto-registers the OBS pacman repo on direct-download installs (skipped when pacman.conf already references the repo — i.e. repo installs), post_remove deletes only its own marker block |
| `packaging/deb/postinst`+`postrm`, `packaging/rpm/copr-post*.sh` | CI (CPack) packages ONLY: self-register the OBS apt repo / COPR repo (dnf on Fedora, zypp on openSUSE) so direct downloads update natively; repo-built packages skip them on purpose |

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
     <description>Auto-updating release repos for Unisic (Debian, Ubuntu, Arch).
   Fedora and openSUSE are served from COPR deandark/Unisic. Rebuilt on each GitHub release via runservice token.</description>
     <person userid="unisic" role="maintainer"/>
     <publish><enable/></publish>
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
   osc build Debian_13 x86_64             # runs debtransform locally
   osc build Arch x86_64
   ```

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
- Don't add rpm targets (Fedora, openSUSE) here — they are COPR's job
  (`.packit.yaml`); the openSUSE repos were removed from the project meta
  when the suse channel moved to COPR.
- Don't rename `packaging/obs/debian.*` files — `extract_file` globs and the
  debtransform `debian.<x>` → `debian/<x>` convention both depend on the names.
