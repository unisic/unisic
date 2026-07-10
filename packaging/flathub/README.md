# Flathub submission

`app.unisic.Unisic.yml` in this directory is the manifest for the
[Flathub submission PR](https://docs.flathub.org/docs/for-app-authors/submission).
It is identical to the CI manifest in the repo root except that it builds from a
**pinned git tag** (Flathub does not accept `type: dir` sources) and lets
flatpak-builder compose the AppStream catalog.

The app ID `app.unisic.Unisic` is backed by the `unisic.app` domain, which is
what Flathub verification checks.

## One-time submission

1. Merge the ID rename (`org.unisic.Unisic` → `app.unisic.Unisic`) to `main`
   and tag a release, e.g. `v0.4.0`. The tag must contain the renamed desktop
   file and metainfo — Flathub validates that they match the app ID.
2. Fill the source pin in this manifest:

   ```sh
   git rev-parse 'v0.4.0^{commit}'   # → paste into `commit:`
   ```

3. Sanity-check locally:

   ```sh
   flatpak install -y flathub org.flatpak.Builder
   flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest app.unisic.Unisic.yml
   flatpak run --command=flatpak-builder-lint org.flatpak.Builder appstream ../../resources/app.unisic.Unisic.metainfo.xml
   flatpak-builder --user --install --force-clean --repo=repo builddir app.unisic.Unisic.yml
   flatpak run --command=flatpak-builder-lint org.flatpak.Builder repo repo
   ```

4. Fork <https://github.com/flathub/flathub>, create a branch **off the
   `new-pr` branch** (not master), copy this `app.unisic.Unisic.yml` into the
   repo root, and open a PR with `new-pr` as the base branch.
5. `flatpak-builder-lint manifest` currently reports exactly two errors, both
   of which need a **linter exception** granted by reviewers (ask for it in
   the PR description — exceptions live in the flatpak-builder-lint repo, not
   in this manifest):
   - `finish-args-kwin-talk-name` (`--talk-name=org.kde.KWin`) — silent KDE
     capture via `org.kde.KWin.ScreenShot2`; auto-falls back to the
     Screenshot portal when unavailable/unauthorized.
   - `finish-args-portal-impl-permissionstore-talk-name`
     (`--talk-name=org.freedesktop.impl.portal.PermissionStore`) — self-grants
     the portal `screenshot` permission so the overlay freeze does not pop a
     dialog on every capture.

   If reviewers refuse an exception, the fallback is to drop the arg: without
   `org.kde.KWin` every capture goes through the portal; without the
   PermissionStore the portal shows its consent dialog per capture until the
   user ticks "always allow".
6. Comment `bot, build` on the PR to trigger a test build; iterate on review.
7. After merge you get collaborator access to `flathub/app.unisic.Unisic` —
   all future updates are PRs against that repo. The `x-checker-data` block
   makes flathubbot open version-bump PRs automatically when a new `v*` tag
   appears.

## After publication

- **Verification (checkmark)**: on <https://flathub.org> open the app's page →
  Developer settings → verify via **website**: it asks to serve a token at
  `https://unisic.app/.well-known/org.flathub.VerifiedApps.txt`.
- **Screenshots**: `resources/app.unisic.Unisic.metainfo.xml` currently points
  at the project banner (`docs/social-preview.png`). Replace with real UI
  screenshots (overlay, editor, recorder) and add `<caption>`s — the Flathub
  quality guidelines gate front-page visibility on them.
- **Runtime bumps**: the runtime is pinned to org.kde.Platform **6.10** — the
  newest version that also has a `flathub-infra/flatpak-github-actions` CI
  image (6.8 is EOL per the linter; a 6.11 image did not exist yet). When
  bumping, change `runtime-version` here and in the root manifest together
  with the CI image tag in `.github/workflows/release.yml`
  (`flatpak-github-actions:kde-6.10`).
