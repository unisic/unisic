#!/usr/bin/env python3
"""Rebuild the sticker set from a Twemoji release, emoji-test.txt and CLDR.

    python3 -I update.py <twemoji>/assets/svg emoji-test.txt <cldr-dir> <this dir>

<cldr-dir> holds, per UI language, <lang>.json and <lang>.d.json: cldr-json's
cldr-annotations-full/annotations/<lang>/annotations.json and
cldr-annotations-derived-full/annotationsDerived/<lang>/annotations.json.
Every SVG here is replaced by the ones catalog.json names, copied unmodified;
names.<lang>.json holds each emoji's name and search words, in catalog order.
"""

import json
import os
import shutil
import sys

LANGS = ["en", "pl", "es", "it", "fr", "ru", "de"]
TONES = [0x1F3FB, 0x1F3FC, 0x1F3FD, 0x1F3FE, 0x1F3FF]


def file_name(svg_dir, cps):
    # Twemoji keeps U+FE0F in most U+200D sequences and drops it elsewhere;
    # AnnotationCanvas's stickerArtPath() tries the same two names.
    for keep in (True, False):
        name = "-".join("%x" % c for c in cps if keep or c != 0xFE0F) + ".svg"
        if os.path.exists(os.path.join(svg_dir, name)):
            return name
    return None


def main(svg_dir, test_txt, cldr_dir, out_dir):
    groups, entries, by_key = [], [], {}
    group = None
    with open(test_txt, encoding="utf-8") as f:
        for line in f:
            if line.startswith("# group: "):
                group = line[9:].strip()
                continue
            if not line.strip() or line.startswith("#") or group == "Component":
                continue
            points, rest = line.split(";", 1)
            if not rest.strip().startswith("fully-qualified"):
                continue
            cps = [int(p, 16) for p in points.split()]
            name = rest.split("#", 1)[1].strip().split(" ", 2)[2]
            if not file_name(svg_dir, cps):
                print("no art:", name, file=sys.stderr)
                continue
            tones = sorted(set(cps) & set(TONES))
            key = tuple(c for c in cps if c != 0xFE0F and c not in TONES)
            if not tones:
                if group not in groups:
                    groups.append(group)
                e = {"cps": cps, "g": groups.index(group), "name": name, "tones": {}}
                entries.append(e)
                by_key[key] = e
            elif len(tones) == 1 and key in by_key:
                # One tone throughout; mixed pairs stay out of the picker.
                by_key[key]["tones"][TONES.index(tones[0])] = cps

    def text(cps):
        return "".join(chr(c) for c in cps)

    names, keywords = {}, {}
    for lang in LANGS:
        ann = {}
        for part in (lang + ".json", lang + ".d.json"):
            with open(os.path.join(cldr_dir, part), encoding="utf-8") as f:
                data = json.load(f)
            ann.update(next(iter(data.values()))["annotations"])
        n, k = [], []
        for e in entries:
            s = text(e["cps"])
            a = ann.get(s) or ann.get(s.replace("\ufe0f", "")) or {}
            n.append((a.get("tts") or [""])[0] or (e["name"] if lang == "en" else ""))
            k.append(" ".join(a.get("default", [])))
        names[lang], keywords[lang] = n, k

    catalog = {
        "groups": groups,
        "emoji": [[text(e["cps"]), e["g"]]
                  + ([[text(e["tones"][i]) for i in range(5)]] if len(e["tones"]) == 5 else [])
                  for e in entries],
    }
    for f in os.listdir(out_dir):
        if f.endswith(".svg") or f.endswith(".json"):
            os.remove(os.path.join(out_dir, f))
    for e in entries:
        for cps in [e["cps"]] + (list(e["tones"].values()) if len(e["tones"]) == 5 else []):
            name = file_name(svg_dir, cps)
            shutil.copyfile(os.path.join(svg_dir, name), os.path.join(out_dir, name))

    def write(name, data):
        with open(os.path.join(out_dir, name), "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, separators=(",", ":"))
    write("catalog.json", catalog)
    for lang in LANGS:
        write("names.%s.json" % lang, {"names": names[lang], "keywords": keywords[lang]})
    print(len(entries), "emoji,", sum(len(e["tones"]) == 5 for e in entries), "with tones,",
          len([f for f in os.listdir(out_dir) if f.endswith(".svg")]), "files")


if __name__ == "__main__":
    main(*sys.argv[1:5])
