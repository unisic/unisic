# Twemoji

The editor's stickers: every fully-qualified emoji of Unicode 17.0 that
Twemoji (https://github.com/jdecked/twemoji), the maintained continuation of
Twitter's emoji set, draws, each with its five skin tones where Unicode has a
single-tone variant. Graphics copyright (c) 2014-2021 Twitter and (c)
2022-present Jason Sofonia & Justine De Caires, licensed under CC BY 4.0
(https://creativecommons.org/licenses/by/4.0/, full text in `LICENSE`,
installed as `share/unisic/LICENSE.twemoji`). The README credit in the project
root is the attribution the Twemoji project asks for.

Every `.svg` is `assets/svg/<name>` of the Twemoji v17.0.3 tag, unmodified. A
file is named by the emoji's code points in lowercase hex, joined with `-`.
Twemoji keeps U+FE0F in most sequences joined by U+200D and drops it
elsewhere (the eye in a speech bubble, `1f441-200d-1f5e8.svg`, drops it too),
so `AnnotationCanvas::stickerArtPath()` tries the name with every code point
first, then without U+FE0F.

`catalog.json` lists the emoji in Unicode's order with their group and skin
tones; `names.<lang>.json` holds, in the same order, each one's name and
search words in the seven UI languages. The groups and order come from
Unicode's `emoji-test.txt` (https://unicode.org/Public/17.0.0/emoji/emoji-test.txt),
the names and search words from CLDR 48.0.0 annotations
(https://github.com/unicode-org/cldr-json, `cldr-annotations-full` and
`cldr-annotations-derived-full`). Both are under the Unicode License v3, full
text in `LICENSE.unicode`, installed as `share/unisic/LICENSE.unicode`.

`update.py` regenerates all of it from those three sources (its docstring has
the command). It replaces every `.svg` and `.json` here, so nothing is edited
by hand; mixed skin-tone pairs and the Component group stay out of the picker.
`AnnotationCanvasTest::stickerCatalogDrawsEveryEmoji` fails on a catalogue
sticker with no art.
