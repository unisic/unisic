# AUR packaging

Two recipes, both published from here:

| Package | What it does | Who it is for |
| --- | --- | --- |
| `unisic` | Builds from the tagged GitHub source tarball | Anyone who wants the package rebuilt against the Qt currently in `[extra]` |
| `unisic-bin` | Repacks the `.pkg.tar.zst` the release workflow already built in an `archlinux:base-devel` container | Anyone who does not want to compile a Qt app |

They `conflict` with each other and both `provide` `unisic`, so a helper will
happily swap one for the other.

`packaging/arch/PKGBUILD` is a **third, separate** recipe and is not published
here. It feeds OBS and the GitHub release asset, and its `unisic.install`
scriptlet appends the OBS pacman repo to `/etc/pacman.conf`. Neither AUR
package may do that: enabling a third-party repository is against AUR policy,
and here it would also take the install away from the helper that owns it.

## How updates actually reach an AUR user

`pacman` cannot use the AUR as a repository - the AUR hosts build recipes, not
built packages, so there is no sync database for `pacman -Syu` to read. Only a
helper (`paru`, `yay`, `pikaur`, `trizen`, `aura`, …) updates AUR packages, by
diffing every *foreign* package (`pacman -Qm`, i.e. anything not from a
configured repository) against the AUR by name.

That name match is what ties the channels together:

- Installed through a helper - the helper sees `unisic` / `unisic-bin` and
  updates it. Nothing else is involved.
- Installed by downloading the release `.pkg.tar.zst` - the package is named
  `unisic`, so a helper picks it up as foreign and offers the AUR version. If
  its scriptlet managed to register the OBS repo, the package is no longer
  foreign and plain `pacman -Syu` updates it instead. Either way one channel
  owns it, never both.
- `scripts/install.sh` prefers a helper on Arch when it finds one, so the
  common path lands in the AUR channel by itself.

## The in-app updater must stay out of the way

Both recipes install `/usr/share/unisic/install-channel` containing `aur`.
`UpdateChecker::installKind()` reads it and returns `"aur"`, which makes
`updatesManagedExternally()` true - the Updates pane turns into an explanation
and Unisic never queries GitHub.

Without that marker the install reports `"system"` and the pane offers
*Install now*, which runs `install.sh --self-update native`: that
`pacman -U`s the GitHub `.pkg.tar.zst` **over** the AUR package and appends the
OBS repo to `pacman.conf`, silently moving the install to a different channel.

Releases before the marker existed cannot read it, so `install.sh` also
refuses the native path when it finds the marker or an AUR-installed `unisic`,
and says which helper command to run instead. That guard works on already
published versions because *Install now* fetches `install.sh` fresh from the
repository each time.

## One-time setup

1. Create an account at <https://aur.archlinux.org> (an Arch box is not
   required for any of this).
2. Add your SSH **public** key under *My Account -> SSH Public Key*.
3. Check it took:

   ```sh
   ssh aur@aur.archlinux.org help
   ```

   `Permission denied (publickey)` means the key is not registered yet.

4. Create both repositories by pushing to them - the AUR creates a package
   the first time a valid `.SRCINFO` arrives:

   ```sh
   git clone ssh://aur@aur.archlinux.org/unisic.git
   git clone ssh://aur@aur.archlinux.org/unisic-bin.git
   ```

   A brand-new name clones as an empty repository; that is expected.

## Publishing a release

From this directory:

```sh
./sync.sh --version 0.7.5 --check   # retarget, regenerate .SRCINFO, build both
./sync.sh --version 0.7.5 --push    # ... and push to the AUR
```

`sync.sh` with no `--version` takes the newest GitHub release. It:

- rewrites `pkgver`, the upstream `_pkgrel` (read out of the release asset's
  file name, which moves independently of the AUR `pkgrel`) and the real
  `sha256sums` - never `SKIP`, which is only legitimate for VCS sources;
- fails if the released package declares a dependency the recipes do not list,
  and merely reports the reverse (`libinput`, `hicolor-icon-theme` are namcap
  findings that upstream still under-declares);
- regenerates `.SRCINFO` with `makepkg --printsrcinfo`, in a container when
  the box is not Arch. The AUR web interface reads `.SRCINFO`, not the
  PKGBUILD, so a stale one publishes a package nobody can find;
- with `--check`, builds both packages in `archlinux:base-devel` and runs
  namcap;
- with `--push`, commits `PKGBUILD` + `.SRCINFO` to each AUR repository.

`sync.sh` resets `pkgrel` to 1, because it exists to retarget a version. For an
AUR-only fix at an unchanged `pkgver`, bump `pkgrel` by hand, regenerate
`.SRCINFO`, and push without running `sync.sh` afterwards.

## Publishing from CI

`.github/workflows/aur.yml` does the above by itself after each release, called
by `release.yml`. It runs in an `archlinux:base-devel` container, as an
unprivileged `builder` user with passwordless sudo (makepkg refuses to run as
root, and `makepkg -s` needs sudo to install dependencies - without a sudoers
rule it fails with the unhelpful "Could not resolve all dependencies").

It runs `--check bin`, not a full `--check`: the pipeline's own `arch` job has
already compiled and installed this exact tree, so building the source recipe
again would cost about ten minutes and verify nothing new. The repack has no
such twin, so it is built and namcap'd.

**Retrying a failed push.** Re-running the release pipeline is useless: its
version check sees the tag it already created and skips every job. So `aur.yml`
is `workflow_dispatch`-able on its own, taking the released version as input -
`gh workflow run aur.yml --ref dev -f version=0.8`. It only reads the recipes
out of the checkout; everything it hashes is downloaded from the release.

**Pre-releases are skipped.** A version with a letter in it (`0.8b`, `1.0rc1`)
leaves the AUR on the last stable version - an AUR package with no channel in
its name is understood to be the stable one, and pushing a beta would hand it
to everyone who typed `paru -Syu`.

One-time setup:

1. Generate a key used by nothing else. It is a deploy credential, not your
   login key, so a leak costs you one revocation and no other account:

   ```sh
   ssh-keygen -t ed25519 -f ~/.ssh/aur-ci -C 'unisic ci' -N ''
   ```

2. Add `~/.ssh/aur-ci.pub` as a **second** key on your AUR account (the SSH
   Public Key field takes one key per line - keep your own on its own line).
3. Put the **private** key in the repository's secrets as `AUR_SSH_KEY`,
   base64-encoded onto a single line. The job runs in a container, and the
   runner hands a container step its environment through a `KEY=value` file,
   where a value stops at the first newline: a PEM pasted whole arrives as its
   `-----BEGIN` line and nothing else.

   ```sh
   base64 -w0 < ~/.ssh/aur-ci | gh secret set AUR_SSH_KEY --repo unisic/unisic
   ```

   The AUR account page shows the key as its full `ssh-ed25519 AAAA...` text,
   never as the `SHA256:` fingerprint `ssh-keygen -lf` prints, and browsers do
   not find-in-page inside the textarea it sits in. To check the secret matches
   the account, compare the tail of `cut -d' ' -f2 ~/.ssh/aur-ci.pub` by eye.

The job pins `aur.archlinux.org`'s host key rather than running `ssh-keyscan`,
which would trust whatever answers on the day - the exact thing a known_hosts
file exists to prevent. If the AUR ever rotates it, the job fails closed and
the fingerprint in the workflow has to be updated deliberately.

Automatic publishing does mean a recipe reaches users without anyone having
built the source package for that version. `sync.sh` covers the most likely
way that goes wrong - it refuses to publish when the released package needs a
dependency the recipes do not list - but a source-only build break (a new
CMake option, a dropped Qt module) would still land. Run
`./sync.sh --version <ver> --check` locally when a release touched the build
system.

### Source tarballs and the kit submodule

Up to and including 0.7.5 the tree had no submodules, so `unisic` sourced
GitHub's tag tarball directly. From 0.8 that tarball is **incomplete** -
GitHub's archives never contain submodules, and `CMakeLists.txt` does
`add_subdirectory(external/unisic-kit)`, so a build from it dies at configure
time.

Resolved by publishing the complete tarball the release workflow already builds
(superproject + kit concatenated) as a release asset. `source=()` therefore
points at `releases/download/v<ver>/unisic-<ver>.tar.gz`, not at the tag
archive, and `sync.sh` hashes the same URL. The portable bundle is
`unisic-<ver>-x86_64.tar.gz`, so the two assets never collide.

Two things still have to hold at release time: the kit commit must exist on
`github.com/unisic/unisic-kit` (the org repo tends to lag the local checkout,
and the release workflow archives whatever the submodule points at), and the
`arch` job must have run - the source tarball is produced there, so a release
where that job failed has no tarball for the AUR recipe to fetch.
