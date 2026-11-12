# Simplified Chinese (zh_CN) UI language: design and rollout plan

Status: implemented on the `feat/zh-cn` branch; the website follow-up is open.
This file is the record of adding the eighth interface language. `docs/dev/i18n.md` stays the rulebook for everyday string work; this
file covers only this language's decisions, phases and acceptance.

## 1. Goal and non-goals

Goal: Unisic ships a complete Simplified Chinese interface. A user can pick
"简体中文" in Settings > General > Language, or get it automatically on a
`zh_CN` desktop, and every user-facing string, bundled Qt dialog that has a
translation, and the notification card follow that choice with no restart.

Non-goals for this change:

- Traditional Chinese (`zh_TW` / `zh_HK`). The marketing site already has a
  `zh-Hant` landing page, but the app catalog is a separate translation job.
  Those locales keep falling back to English until a `unisic_zh_TW.ts` exists.
- Chinese release notes. `CHANGELOG.md` stays English + Polish; the in-app
  "What's new" shows English to a Chinese user, exactly as it does for German
  and Russian today.
- Shipping a Tesseract `chi_sim` pack. OCR already maps the Han script to an
  installed `chi_sim` / `chi_tra` pack, so Chinese text recognition works as
  soon as the user installs the pack; bundling it is a packaging decision with
  a 40 MB+ size cost.
- Translating the Windows installer wizard. Inno Setup has no official
  Simplified Chinese `.isl`, so the wizard stays in the shipped Inno languages
  while the app itself is Chinese.

## 2. Baseline

Measured on the tree that this change starts from:

| Item | Value |
|---|---|
| Existing catalogs | 7 (`en`, `pl`, `es`, `it`, `fr`, `ru`, `de`) |
| Messages per catalog | 1,943 active, of which 1,760 unique source strings (the older catalogs also carry vanished entries that lupdate drops) |
| English source text | about 76,000 characters, about 12,900 words |
| Long help texts | 60 messages over 200 characters (17,555 characters total) |
| Messages with `%` placeholders | 299 |
| Plural messages | 3 |
| Translation contexts | 48 |
| Unfinished / empty entries | 0 / 0 |
| Catalog size | about 9,867 lines each |

The infrastructure already does the hard part: lupdate/lrelease are mandatory
build dependencies, `.qm` files are baked into the qrc, `applyLanguage()` swaps
QTranslators live, and CI rejects unfinished or empty entries in every
`i18n/unisic_*.ts`. Adding a language is a catalog plus a handful of wiring
points, not an architecture change.

## 3. Design decisions

### 3.1 Language code and file name

- Setting value and catalog name: `zh_CN`, file `i18n/unisic_zh_CN.ts`,
  compiled runtime resource `:/i18n/unisic_zh_CN.qm`.
- Reason: Qt's own Simplified Chinese catalog is `qtbase_zh_CN.qm`, and
  `QLocale("zh_CN")` drives date and month formatting. A two-letter `zh` would
  compile, but would not line up with Qt's translation lookup.
- The picker label is the native name `简体中文`, consistent with the existing
  "every language in its own language" rule. It is placed after `Deutsch`.

### 3.2 System locale detection

The current code takes `QLocale::system().name().left(2)`, which yields `zh`
for every Chinese desktop. That would miss `zh_CN` and silently land on
English. The lookup is consolidated into one helper used by both
`applyLanguage()` and the notification helper subprocess:

```
supported: en pl es it fr ru de zh_CN
system "zh_CN" / "zh_SG" / "zh"  -> zh_CN
system "zh_TW" / "zh_HK" / "zh_MO" -> en (documented fallback)
anything else -> first two letters if supported, else en
```

The notification helper currently carries a second copy of the language list.
Leaving two lists is how one of them gets forgotten, so this change replaces
both with the helper.

### 3.3 Language picker

Two QML files need the new id and label in the same position:

- `qml/pages/SettingsPage.qml` (Settings > General > Language)
- `qml/components/UWelcome.qml` (first-run setup)

`ids` and `model` must stay index-aligned. A wrong pairing would show
"简体中文" while storing a different code, so both arrays are edited in the
same commit and checked visually.

### 3.4 Catalog maintenance

- The skeleton is generated from the source tree, never hand-built, so
  locations and contexts match lupdate's output.
- Every translation is filled; no `type="unfinished"` and no empty
  `<translation>` reaches the tree.
- `%1`, `%n`, `%date%`, `*.png`, `pw-play` / `paplay` / `aplay`, shortcut names
  (`Ctrl+Shift+A`, `F8`, `Esc`), product and protocol names (`Unisic`, `KDE
  Plasma`, `Wayland`, `X11`, `PipeWire`, `Tesseract`, `AppImage`, `Flatpak`)
  stay verbatim.
- Style: concise UI Chinese, direct address with "你", no em dash (the
  repository bans it), Chinese punctuation only where the string is a
  sentence. Labels keep their existing punctuation shape from English.
- Plural messages: Chinese has one form. The three `%n` strings use a single
  `<numerusform>`.

### 3.5 Fonts and input methods

The UI does not bundle a font and does not override `font.family` (the kit's
`Theme.qml` only defines pixel sizes). Chinese therefore renders through
fontconfig fallback. This is correct on a normal Chinese desktop, but a
minimal install or a sandbox without a CJK font shows tofu. Mitigations:

- QA runs with Noto Sans CJK installed and with it removed, so the failure mode
  is known.
- Native packages add a CJK font as a recommendation, not a dependency:
  Debian/Ubuntu `fonts-noto-cjk`, Fedora `google-noto-sans-cjk-fonts`,
  Arch `noto-fonts-cjk`.
- Input methods (fcitx5 / ibus) are verified in the annotation text tool and a
  settings text field. This is a real Wayland dependency, not something a
  compile can prove.

### 3.6 Docs and claims

Adding a language changes several "seven" statements. They are updated in the
same commit:

- `docs/dev/i18n.md`, `AGENTS.md`, `CLAUDE.md`, `CONTRIBUTING.md`,
  `docs/dev/packaging.md`
- CMake status/error text, the CI completeness job message
- `resources/CHANGELOG.md` English and Polish block under the current beta
  heading
- The sibling `unisic-website` repository: nine landing-page dictionaries plus
  `content/docs/configuration.md`. That repository is not in this workspace;
  this change records the exact edits needed and leaves them for that repo's
  own commit.

## 4. Touch points

| File | Change |
|---|---|
| `i18n/unisic_zh_CN.ts` | New, 1,943 translated messages |
| `CMakeLists.txt` | Add the TS file, update the "seven" comments and status line |
| `src/AppContext.cpp` | One language-code helper, used by `applyLanguage()` and `showNotificationHelper()`; add `zh_CN` |
| `src/Settings.h` | Update the settings comment |
| `src/diag/SmokeTests.cpp` | `languageCheck()` covers `zh_CN` |
| `qml/pages/SettingsPage.qml` | Picker id + native name |
| `qml/components/UWelcome.qml` | Picker id + native name |
| `.github/workflows/tests.yml` | Message wording only; the glob already covers the new file |
| Docs and changelog | As listed in 3.6 |
| Packaging | Optional CJK font recommendation |

## 5. Phases

Each phase ends with a concrete exit criterion. No phase starts before the
previous one is green.

### P0: Build environment and baseline

- Fedora 43 container (Qt 6.9+, the project's full mandatory dependency set) with
  the tree bind-mounted.
- Configure and build the unmodified tree once, run `ctest`, so any later
  failure is attributable to this change.
- Exit: `cmake -B build` succeeds, build succeeds, `ctest` passes on the
  baseline commit.

### P1: Skeleton and glossary

- Derive `unisic_zh_CN.ts` from the current English catalog so the message set
  is byte-for-byte the same, then confirm against lupdate output.
- Write the glossary: capture, region, window, overlay, editor, history,
  tray, hotkey, upload, destination, recording, and the tone rules.
- Exit: catalog has 1,914 entries, all `unfinished`, and the glossary is
  attached to the translation work.

### P2: Translation

- Translate in bounded batches with the glossary and neighboring context.
- Automated gate after merging: every id present, no empty value, placeholder
  multiset preserved, no em dash, no accidental English left in non-technical
  strings.
- Spot review of high-traffic surfaces: Settings, Main, Editor, Overlay,
  notification card, History.
- Exit: 0 unfinished, 0 empty, placeholder check clean.

### P3: Wiring

- Code, CMake, smoke test, settings comment, docs and changelog updates from
  section 4.
- Exit: a Chinese locale reaches the Chinese catalog in a fresh profile, and
  the language picker round-trips.

### P4: Build and automated verification

- Full build with lrelease, `ctest`, the CI-equivalent translation check.
- Load the built `.qm` and assert representative strings, including the
  `Quit` string the smoke test uses.
- Exit: build, lrelease, ctest and the `.qm` assertion all pass.

### P5: Visual and IME acceptance

- Chinese UI screenshots of the main pages, Settings > Language, the editor,
  the capture overlay, the notification card and the tray menu.
- Checks at 100%, 125% and 150% scale, with and without a CJK font, and with
  an input method in the annotation text tool.
- Exit: no clipped or overlapping text, no tofu on a normal install, IME
  commits text.

### P6: Delivery

- Commit the change in reviewable pieces, with the changelog entry and the
  website follow-up note.
- Exit: working tree clean, summary lists what was verified and what still
  needs the website repository.

## 6. Acceptance criteria

1. `i18n/unisic_zh_CN.ts` has 1,943 entries, 0 unfinished, 0 empty.
2. `lrelease` emits `unisic_zh_CN.qm`; the qrc contains it.
3. A `zh_CN` system locale boots the app in Chinese; `zh_TW` falls back to
   English as documented.
4. Settings and the first-run wizard show `简体中文` and switching applies
   immediately, without a restart.
5. The F8 language check reports `PASS` for `zh_CN`.
6. Build and `ctest` are green from a clean configure.
7. Chinese screenshots show no clipped labels at 100%, 125% and 150%.
8. No "seven languages" claim remains in the repository docs, CMake text or
   CI messages.

## 7. Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| `zh` vs `zh_CN` mismatch | Whole feature silently falls back to English | One helper, unit-checked by hand with `LANG=zh_CN.UTF-8` and `LANG=zh_TW.UTF-8`; smoke check covers the catalog |
| Two copies of the language list | Notification card stays English | Consolidate into the helper in this change |
| Machine-like or inconsistent translation | Product quality | Glossary, batch context, spot review; long help texts reviewed first |
| Placeholder broken by translation | Crash or wrong text at runtime | Automated multiset check before merge |
| Missing CJK font | Tofu in sandbox or minimal install | Packaging recommendation, QA with and without the font |
| IME not working on Wayland | Chinese users cannot type annotations | Explicit IME check in P5; fall back to documenting the tested input method if a compositor limitation appears |
| Traditional Chinese users get Simplified | Wrong language for `zh_TW` | Explicit fallback to English, separate catalog later |
| Docs/website drift | "seven languages" survives | Same-commit doc sweep, website follow-up recorded |

## 8. Rollback

The change is additive. Removing `i18n/unisic_zh_CN.ts` from the CMake list and
reverting the picker entries restores the previous behavior; no settings
migration is needed because an unknown `uiLanguage` value already falls back
through the helper. Users who picked `zh_CN` get English after a rollback,
which is the same path an unknown value takes today.

## 9. Effort

| Work | Person-days |
|---|---|
| P0 environment and baseline | 0.5 |
| P1 skeleton and glossary | 0.25 |
| P2 translation and review | 2 - 3 |
| P3 wiring | 0.5 |
| P4 build and automated verification | 0.5 |
| P5 visual and IME acceptance | 1 - 1.5 |
| P6 delivery | 0.25 |
| Total | 5 - 6.5 |

Translation is the critical path. Wiring and docs are bounded and can land in
parallel with review once the catalog skeleton exists.

## 10. Progress log

| Phase | Status | Evidence |
|---|---|---|
| P0 | done | Fedora 43 container, Qt 6.10.3, full build and 24/24 ctest |
| P1 | done | 1,943-entry catalog generated from the source tree, glossary in place |
| P2 | done | 1,760 unique strings translated, 56 review findings on the initial pass plus 125 dev-delta strings, automated checks clean |
| P3 | done | code, pickers, CMake, smoke test, docs and changelog wired |
| P4 | done | Qt 6.10.3 build exit 0, 24/24 ctest, .qm load check, CI completeness check green |
| P5 | done with exceptions | screenshots at 100/125/150%, live switching, system locale and zh_TW fallback verified; IME and the notification card are not verifiable without a real compositor |
| P6 | done in this repo | committed on `feat/zh-cn`; website follow-up open |

## 11. Verification record

Environment: Fedora 43 container with the project's full mandatory dependency
set, Qt 6.10.3, CMake 3.31.11, Ninja, 20-way build.

| Check | Result |
|---|---|
| `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` | exit 0, `HAVE_TRANSLATIONS=ON`, every `HAVE_*` gate ON |
| `cmake --build build` | exit 0, `unisic_zh_CN.qm` baked into the qrc |
| `ctest --test-dir build` | 24/24 passed |
| CI completeness rule (unfinished and empty in every `.ts`) | 0 and 0 in all eight catalogs |
| `lupdate -no-obsolete` over `src`, `qml` and the kit | 1,943 current sources, 0 new, 2 obsolete entries dropped |
| `QTranslator` load of `unisic_zh_CN.qm` | `Quit -> 退出`, `Language -> 语言`, `Today -> 今天`, and five more |

The initial catalog was translated against `main`. The feature branch was then
rebased onto `dev` (`82d70b8`), which added 125 messages for the 0.8.10 features
(share sources, print, short links, watched folder, S3, spotlight, stickers,
cut out, background removal). Those 125 were translated after the rebase, so the
catalog on `dev` is complete, not just the `main` subset.
| Settings UI in Chinese at 100%, 125%, 150% | no clipped or overlapping text |
| Live switch Chinese -> English -> Chinese | applies immediately, no restart |
| `uiLanguage=system` with `LANG=zh_CN.UTF-8` | boots in Chinese |
| `uiLanguage=zh_TW` | boots in English, as documented |
| CJK fonts removed | every Chinese glyph becomes tofu, confirming the packaging recommendation |
| Input method (fcitx5/ibus) | not tested: no compositor and no IME in the build container |

The local environment cannot verify two things that the design treats as real
dependencies: Chinese input methods on Wayland, and the notification card
rendered by the helper subprocess on a real compositor. Both need a desktop
run before release; the helper shares the same language resolver and catalog,
so the code path is covered, but the rendering is not.
