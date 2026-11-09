"""Language packs for the pe_lang mod: translator templates, a test language, and the compiler.

  python -I pelang.py template out/text/strings.json -o fr.csv [--merge old-fr.csv]
  python -I pelang.py pseudo   out/text/strings.json -o qps.csv
  python -I pelang.py build    fr.csv --strings out/text/strings.json --lang fr --name "French" -o fr.pelang
  python -I pelang.py bundle   fr.pelang -o "French.pelang" [--readme credits.txt]

`strings.json` comes from extract_text.py, run on the translator's own discs (add --ja for the
Japanese column). A template row is: key, the English and Japanese text (for reference: they come
from the translator's discs and are never put in a pack), the translation, and layout notes.
Text uses extract_text's tags ({br}, {wait}, {name}, {choice 2}, {red} ...; see
petext/gametext.py). An empty translation keeps the game's own text.

`build` checks every translated row and writes a .pelang pack holding only the translations,
already in the game's glyph code. Errors stop the build: an unknown key, a character the font
has no glyph for, malformed commands, changed control sequences, overflow in a known dialogue
box, or menu text past a buffer or the screen edge (data/menu_layouts.json). Line breaks can move;
waits and other commands retain their count, order and operands. Unknown layouts and unconfirmed
menu widths are warnings for in-game review. --allow-overflow is an explicit layout override for
testing (it never bypasses control validation).

Pack format (little-endian), read by mods/pe_lang/pe_lang_pack.c:
  header  64 bytes: "PELG", u32 version (1), u32 entry count, u32 offset of the text,
          char lang[16] and char name[32] (UTF-8, NUL-padded)
  entries count x {u32 key hash, u32 text offset from the text start}, sorted by hash
          (FNV-1a 32 of the key's UTF-8, e.g. "item/054"; the build refuses a collision)
  text    each entry's glyph bytes followed by FF (a dialogue page that ends in F9 keeps it
          before the FF)
Besides the translations, a pack holds one "bank/<crc32>" entry per message bank: the bank's key
("M001", ASCII), so pe_lang can tell which bank a room has loaded (the CRC32 of the bytes its
pages span, as banks.json lists them) and find that bank's dlg/ keys.

Translation files: what a player downloads and opens with F1 > Language > Add a translation is a
.pelang too, either the pack above or a zip holding it as `text.bin` (written by `bundle`, read by
mods/pe_lang/pe_lang_bundle.c). readme.txt is kept for the player to read, and parts the game
doesn't know are left alone.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import struct
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
from petext import gametext as gt, latin, validation, layout, menu  # noqa: E402
from petext import modtext, cjk, letters as lettertable, stretch, names as namelib
from petext import manifest as srcman  # noqa: E402
from petext import wrap as wrapping  # noqa: E402

MAGIC, VERSION = b"PELG", 1
HEADER = struct.Struct("<4sIII16s32s")
ENTRY = struct.Struct("<II")
# Context columns go last so older CSVs keep their columns where they were.
COLUMNS = ["key", "english", "japanese", "translation", "box", "widest_px", "rooms", "notes",
           "speaker", "before", "after"]
# What a contributor submits (localization/translations/<lang>.csv): no source text in it.
SUBMISSION_COLUMNS = ["key", "translation", "notes"]
SOURCE_COLUMNS = ("english", "japanese", "before", "after")
SPEAKER = re.compile(r"^\s*(\{name\}|[A-Z][A-Za-z.' ]{1,24}):")


def fnv1a(key: str) -> int:
    h = 0x811C9DC5
    for b in key.encode("utf-8"):
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def load_strings(path: Path) -> dict[str, dict]:
    rows = json.loads(Path(path).read_text(encoding="utf-8"))
    return {r["key"]: r for r in rows if not r.get("unused")}


def menu_limits(key: str, layouts: dict) -> str:
    """The template's "box" for a menu entry: where it is drawn and how much room there is."""
    out = []
    for c in menu.contexts_of(key, layouts):
        room = [f"{c[k]} px" for k in ("width",) if k in c] + [f"{c['bytes'] - 1} bytes" for _ in [0] if "bytes" in c]
        if c.get("same_length"):
            room.append("same length as the English")
        if "max_glyphs" in c:
            room.append(f"{c['max_glyphs']} letters")
        out.append(f"{c['id']}: " + ", ".join(room))
    return "; ".join(out)


def speaker(text: str) -> str:
    """Who says a dialogue page, from its "Name:" prefix ({name} is Aya)."""
    m = SPEAKER.match(text)
    return "" if not m else ("Aya" if m.group(1) == "{name}" else m.group(1).strip())


def template_rows(strings: dict[str, dict]) -> list[dict]:
    """The translator CSV. It holds the game's English and Japanese for reference, so it stays on
    the translator's machine; `export` writes the file that is submitted."""
    out, layouts = [], menu.load_layouts()
    keys = sorted(strings)
    bank_of = lambda k: k.rsplit("/", 1)[0] if k.startswith("dlg/") else None
    for n, key in enumerate(keys):
        r = strings[key]
        dialogue = key.startswith("dlg/")
        if dialogue:
            box = "; ".join(r.get("boxes") or [])
        elif r.get("mod_label"):
            box = f"mod label: {r['max_glyphs']} glyphs, {r['width']} px, single line"
        else:
            box = menu_limits(key, layouts)
        # The pages before and after in the same bank: a page's scene, for whoever translates it.
        near = lambda m: strings[keys[m]]["text"][:160] if 0 <= m < len(keys) and bank_of(keys[m]) == bank_of(key) else ""
        out.append({"key": key, "english": r["text"], "japanese": r.get("japanese") or "", "translation": "",
                    "box": box,
                    "widest_px": r["menu_px"] if "menu_px" in r else max(r.get("lines_px") or [0]),
                    "rooms": " ".join((r.get("rooms") or [])[:6]), "notes": "",
                    "speaker": speaker(r["text"]) if dialogue else "",
                    "before": near(n - 1) if dialogue else "", "after": near(n + 1) if dialogue else ""})
    return out


def cmd_export(a) -> int:
    """The submission: key, translation and notes of the translated rows, no source text."""
    rows = [r for r in read_csv(a.csv) if r.get("translation")]
    with open(a.out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=SUBMISSION_COLUMNS, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    print(f"wrote {len(rows)} translated rows to {a.out} (key, translation, notes: no source text)")
    return 0


def cmd_manifest(a) -> int:
    strings = load_strings(a.strings)
    banks = json.loads(a.strings.with_name("banks.json").read_text(encoding="utf-8"))
    data = srcman.build(strings, banks, menu.load_metrics(a.strings))
    a.out.write_text(json.dumps(data, ensure_ascii=False, separators=(",", ":"), sort_keys=True), encoding="utf-8")
    print(f"wrote {a.out}: {len(data['entries'])} keys, {len(banks)} banks, no game text")
    return 0


def consistency(entries_text: dict[str, str], strings: dict[str, dict], glossary: dict[str, str]) -> list[str]:
    """Warnings for a translation that names things differently than the rest of the pack does:
    a dialogue page that names an item (by its English name) but not as the item's own translation,
    and glossary terms (english,translation) not translated as the glossary says. Case-insensitive;
    languages that inflect names will see some of these on purpose."""
    out = []
    mentions = {k: r["mentions"] for k, r in strings.items() if r.get("mentions")}
    if not mentions and any("text" in r for r in strings.values()):
        mentions = srcman.item_mentions(strings)
    for key, text in entries_text.items():
        low = text.lower()
        for item in mentions.get(key, ()):
            name = entries_text.get(item)
            if name and name.lower() not in low:
                out.append(f"{key}: names {item}, translated \"{name}\", but that name isn't in this line")
        english = strings.get(key, {}).get("text", "")
        for term, tr in glossary.items():
            if re.search(r"(?<![A-Za-z])" + re.escape(term) + r"(?![A-Za-z])", english, re.I) and tr.lower() not in low:
                out.append(f"{key}: glossary: \"{term}\" should be \"{tr}\"")
    return out


def read_csv(path: Path) -> list[dict]:
    if Path(path).is_dir():
        return [row for module in sorted(Path(path).rglob('*.csv')) for row in read_csv(module)]
    with open(path, newline="", encoding="utf-8-sig") as f:
        return list(csv.DictReader(f))


def write_csv(path: Path, rows: list[dict]) -> None:
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)


def cmd_template(a) -> int:
    rows = template_rows(modtext.load(getattr(a, "mod_strings", None), load_strings(a.strings)))
    if a.merge:
        old = {r["key"]: r for r in read_csv(a.merge)}
        kept = 0
        for r in rows:
            prev = old.get(r["key"])
            if prev and prev.get("translation"):
                r["translation"], r["notes"] = prev["translation"], prev.get("notes", "")
                if prev.get("english") and prev["english"] != r["english"]:
                    r["notes"] = ("English changed; check. " + r["notes"]).strip()
                kept += 1
        print(f"kept {kept} translations from {a.merge}")
    write_csv(a.out, rows)
    print(f"wrote {len(rows)} rows to {a.out}")
    return 0


PSEUDO_ACCENTS = str.maketrans("aeiouyncszAEIOUYNCSZ", "áéîõüÿñçšżÀÉÎÕÜŸÑÇŠŻ")


def pseudo(text: str) -> str:
    """A visibly translated, about 30% longer version of `text`: letters swap case and take accents
    (from the Latin Extended set, so the composed glyphs show up), and the text is marked and padded
    with '!' and '+'. Tags stay as they are."""
    parts = re.split(r"(\{[^}]*\})", text)
    plain = sum(len(p) for p in parts if not p.startswith("{"))
    body = "".join(p if p.startswith("{") else p.swapcase().translate(PSEUDO_ACCENTS) for p in parts)
    return f"!{body}{'+' * max(1, plain * 3 // 10)}!" if plain else body


def sample_text(text: str, words: list[str], at: int, short: bool, joined: bool) -> tuple[str, int]:
    """`text` with each English word swapped for the next of `words`; tags stay where they are.
    `short`: a menu entry, so a word takes no more letters than the English did. `joined`: Chinese,
    Japanese and Korean, which go without spaces."""
    parts = re.split(r"(\{[^}]*\})", text)

    def swap(m: re.Match) -> str:
        nonlocal at
        word = words[at % len(words)]
        at += 1
        return word[:len(m.group())] if short else word
    for i in range(0, len(parts), 2):
        parts[i] = re.sub(r"[A-Za-z][A-Za-z']*", swap, parts[i])
        if joined:
            parts[i] = parts[i].replace(" ", "")
    return "".join(parts), at


def cmd_sample(a) -> int:
    """A test language: the game's own tags and page breaks with no game text left, and the words of
    one or more languages in place of the English. For checking letters in the game, not for reading."""
    from petext import alphabets
    picked = [alphabets.BY_CODE[code] for code in a.languages]
    words = [word for lang in picked for word in alphabets.sample_words(lang)]
    joined = any(lang.code in alphabets.CJK for lang in picked)
    rows, at, kept = [], 0, 0
    for key, r in sorted(load_strings(a.strings).items()):
        if key in ("sys/030", "sys/115", "sys/116", "sys/117") or not re.search(r"[A-Za-z]", r["text"]):
            continue
        try:
            validation.check(gt.encode(r["text"], extended=True), r["text"])
        except ValueError:
            kept += 1
            continue
        text, at = sample_text(r["text"], words, at, not key.startswith("dlg/"), joined)
        rows.append({"key": key, "translation": text, "notes": ""})
    with a.out.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=SUBMISSION_COLUMNS)
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {len(rows)} rows to {a.out}; {kept} stay in English: their controls are unusual")
    return 0


def cmd_languages(a) -> int:
    """Which languages the pack tool can write today, from the list in petext/alphabets.py."""
    from petext import alphabets, letters
    for lang in alphabets.LANGUAGES:
        gone = alphabets.missing(lang)
        state = "ready" if not gone else f"missing {len(gone)} letters: {gone[:40]}"
        unseen = "".join(c for c in dict.fromkeys(alphabets.full_alphabet(lang))
                         if c in letters.CATALOG and letters.CATALOG[c].art == letters.PLACEHOLDER)
        if unseen and not gone:
            state = f"ready, but {len(unseen)} letters show ? until drawn: {unseen[:40]}"
        if lang.code in alphabets.NEEDS_LAYOUT:
            state = "needs new text layout"
        print(f"{lang.code:8} {lang.name:24} {state}")
    return 0


def cmd_pseudo(a) -> int:
    rows = template_rows(modtext.load(getattr(a, "mod_strings", None), load_strings(a.strings)))
    skipped = 0
    for r in rows:
        try:
            validation.check(gt.encode(r["english"], extended=True), r["english"])
        except ValueError:
            # A few extracted source rows contain unsupported control bytes.
            # Keep fallback instead of making an unsafe pack or bypassing checks.
            r["notes"] = "Unsupported source controls: kept as game text."
            skipped += 1
            continue
        r["translation"] = pseudo(r["english"])
    if skipped:
        print(f"kept {skipped} rows with unsupported source controls as game text")
    write_csv(a.out, rows)
    print(f"wrote {len(rows)} pseudo-translated rows to {a.out}")
    return 0


def empty_grid_cells(text: str) -> str:
    """A rename grid's {gXX} cells on codes 4D-EF as spaces. English draws them empty, but a pack
    puts its own letters on those codes, which a pick would save (pe_lang_grid_blank does the same
    to the grid the game shows)."""
    return re.sub(r"\{g([0-9A-Fa-f]{2})\}", lambda m: " " if 0x4D <= int(m[1], 16) < 0xF0 else m[0], text)


def check_row(key: str, text: str, source: dict, widest_name: dict[str, int], name_px: int = layout.NAME_WIDTH,
              metrics: bytes | None = None, layouts: dict | None = None, wrap: bool = True, atlas=None,
              from_patch: bool = False, name_letters: bool = True):
    """(glyph bytes or None, errors, warnings) for one translated row. name_px: how wide {name}
    draws, from the pack's sys/030 (Aya's default name); metrics: the menus' glyph widths
    (menu_glyphs.json), without which menu text is only compared with the English. wrap: a
    dialogue page that overflows its box gets line breaks and screens (petext/wrap.py)."""
    errors, warnings = [], []
    if key in namelib.GRIDS:
        text = empty_grid_cells(text)
    try:
        raw = gt.encode(text, extended=True, cjk_codes=atlas.codes if atlas else None)
    except (ValueError, TypeError, OverflowError) as e:
        msg = "invalid text/tag: " + str(e)
        if "no glyph" in msg:
            msg += " (the font has A-Z, a-z, digits, basic punctuation, Latin Extended, Russian/Ukrainian/Belarusian Cyrillic, the letters of petext/letters.py, and CJK with --cjk-font)"
        return None, [msg], warnings
    try:
        (validation.check_patch if from_patch else validation.check)(raw, srcman.source_structure(source))
    except ValueError as e:
        return None, [str(e)], warnings
    for command in validation.commands(raw):
        if command[:2] == lettertable.EXTRA_TOKEN:
            if command not in gt.PACK_EXTRA:
                return None, ["a letter with no code that this pack doesn't hold"], []
            if stretch.stays_bytes(key) and not (name_letters and key in namelib.KEYS):
                return None, [f"{gt.PACK_EXTRA[command].char!r} has no one-byte code in this pack, and "
                              f"{key} keeps one byte per letter"], []
            continue
        if command[0] == 0xF1 and (not atlas or cjk.index(command) >= len(atlas.chars)):
            return None, ["CJK token has no glyph in this pack's dictionary"], []
    # with name letters (petext/names.py) any letter can go in a name; without, only one-byte ones
    if not name_letters and key in ("sys/115", "sys/116", "sys/117") and b"\xf1\xc5" in raw:
        return None, ["CJK cannot be used in the one-byte rename grids"], []
    table = set(atlas.letters.by_code) if atlas and atlas.letters else set()
    if not name_letters and key in ("sys/115", "sys/116", "sys/117") and table & set(raw):
        return None, ["a letter from the pack's own table cannot go in a rename grid: what the player picks is saved"], []
    if source.get("mod_label"):
        if any(c[0] > 0xF1 for c in validation.commands(raw)):
            return None, ["mod labels cannot contain line breaks, waits or commands"], []
        return raw, modtext.check(raw, source, metrics), []
    if not name_letters and key == "sys/030" and b"\xf1\xc5" in raw:
        return None, ["CJK player names cannot be stored safely in the legacy save name field; keep the original name"], []
    if not name_letters and key == "sys/030" and table & set(raw):
        return None, ["a letter from the pack's own table cannot go in the saved player name; keep the original name"], []
    if not name_letters and key == "sys/030" and len(raw) > 8:
        return None, ["default player name exceeds the save format's 8-byte field"], []
    note = None
    limit = layout.box_limit(source) if wrap and key.startswith("dlg/") else None
    if limit and any(max(s) > limit[0] or len(s) > limit[1] for s in layout.screens(raw, name_px)):
        english = srcman.english_shape(source)
        wrapped, added = wrapping.wrap(raw, limit[0], limit[1], english, name_px, atlas.characters if atlas else None)
        if wrapped != raw:
            raw = wrapped
            note = "wrapped to fit the box" + (f", {added} screen(s) added" if added else "") + "; check the breaks"
    layout_errors, warnings = layout.check(key, raw, source, widest_name, name_px, metrics, layouts)
    errors.extend(layout_errors)
    if note:
        warnings.insert(0, note)
    return raw, errors, warnings


def narrow_to_fit(key, raw, errs, source, metrics, layouts, stretches=True):
    """(`raw` narrowed to fit its menu room, n / 256), or None when only width is wrong and no
    narrowing down to menu.NARROW_FLOOR makes it fit (or the line can't be narrowed at all)."""
    if (metrics is None or not stretches or stretch.stays_bytes(key) or key.startswith(("dlg/", "battle/"))
            or not all(" px; " in e for e in errs)):
        return None
    space = menu.room(key, layouts)
    n = menu.fit(menu.width(raw, metrics), space) if space is not None else None
    if n is None:
        return None
    raw = menu.narrow(raw, n)
    return (raw, n) if not menu.check(key, raw, source, metrics, layouts)[0] else None


def cmd_build(a) -> int:
    from_manifest = srcman.is_manifest(a.strings)
    if from_manifest:                       # no discs: the committed source manifest
        base, banks, metrics = srcman.load(a.strings)
    else:
        base, metrics = load_strings(a.strings), menu.load_metrics(a.strings)
        banks = json.loads(Path(a.banks or a.strings.with_name("banks.json")).read_text(encoding="utf-8"))
    strings = modtext.load(getattr(a, "mod_strings", None), base)
    layouts = menu.load_layouts()
    if metrics is None:
        print(f"note: no menu_glyphs.json beside {a.strings} (rerun extract_text.py): "
              "menu text is compared with the English only", file=sys.stderr)
    widest_name: dict[str, int] = {}
    for key, r in strings.items():
        if not key.startswith("dlg/"):
            t = key.split("/")[0]
            px = r["menu_px"] if metrics is not None and "menu_px" in r else max(r.get("lines_px") or [0])
            widest_name[t] = max(widest_name.get(t, 0), px)
    entries, errors, warnings = {}, [], []
    seen = set()
    rows = read_csv(a.csv)
    if a.submission:
        # A submitted file holds translations only: the game's text stays on the translator's machine.
        leaked = sorted({c for r in rows for c in SOURCE_COLUMNS if (r.get(c) or "").strip()})
        if leaked:
            errors.append(f"{a.csv}: has source text in its {', '.join(leaked)} column(s); submit the file "
                          "`pelang.py export` writes (key, translation, notes)")
    gt.PACK_LETTERS.clear()
    gt.PACK_EXTRA.clear()
    gt.PACK_DRAWN.clear()
    from petext import rtl
    gt.RTL = rtl.is_rtl(a.lang)             # lines end at the box's right side
    texts = [r.get('translation','') for r in rows]
    pictures = {}
    if getattr(a, "patch_letters", None):
        try:
            pictures = lettertable.load_pictures(a.patch_letters)
        except (OSError, ValueError, KeyError, TypeError) as e:
            print(f"error: --patch-letters: {e}")
            return 1
    try:
        byte_texts = [r.get('translation','') for r in rows
                      if stretch.stays_bytes(r.get('key','')) and (a.bytes_only or not r.get('key','') in namelib.KEYS)]
        table = lettertable.Letters(texts, dictionary=bool(getattr(a,'cjk_font',None)),
                                    byte_texts=byte_texts, extra=not a.bytes_only,
                                    shapes=rtl.shapes_for(texts) if gt.RTL else (),
                                    own=lettertable.picture_letters(pictures))
        atlas = cjk.Atlas(texts, getattr(a,'cjk_font',None), skip=table.codes)
    except (ValueError, OSError, struct.error) as e:
        print(f'error: CJK font: {e}')
        return 1
    if pictures:
        drawn = table.draw(pictures, texts)
        print(f"{drawn} letters drawn with the patch's pictures and widths")
    atlas.codes.update(table.codes)
    atlas.letters = table
    gt.PACK_LETTERS.update(table.by_code)
    gt.PACK_EXTRA.update(table.by_token)
    gt.PACK_DRAWN.update(table.drawn)
    name_px = layout.name_width(gt.encode("Aya"))
    for r in rows:
        if r.get("key") == "sys/030" and r.get("translation"):
            try:
                name_px = layout.name_width(gt.encode(r["translation"], extended=True, cjk_codes=atlas.codes))
            except (ValueError, TypeError, OverflowError):
                pass                                  # reported with the row below
    for r in rows:
        key, text = r.get("key", ""), r.get("translation", "")
        if not text:
            continue
        if key in seen:
            errors.append(f"{key}: duplicate translated key")
            continue
        seen.add(key)
        if key not in strings:
            errors.append(f"{key}: no such key in {a.strings}")
            continue
        raw, errs, warns = check_row(key, text, strings[key], widest_name, name_px, metrics, layouts,
                                     wrap=not a.no_wrap, atlas=atlas, from_patch=a.from_patch,
                                     name_letters=not a.bytes_only)
        src = strings[key]
        same = text == src["text"] if "text" in src else srcman.english_hash(text) == src.get("english_hash")
        if same and any(ch.isalpha() for ch in text):
            warns.append("the translation is the English unchanged (leave it empty to keep the game's text)")
        if a.allow_overflow:
            # check_row returns raw only after control validation succeeds.
            if raw is not None:
                warns += errs
                errs = []
        elif a.from_patch and raw is not None and errs:
            # A patch was sized for its own letters. A menu line too wide for these is drawn a
            # little narrower to fit (menu.NARROW_FLOOR at most); anything else keeps the game's
            # text, which shouldn't cost the player the whole language.
            fitted = narrow_to_fit(key, raw, errs, src, metrics, layouts, not a.bytes_only)
            if fitted:
                raw, n = fitted
                warns.append(f"drawn at {n * 100 // 256}% width to fit: {errs[0]}")
                errs = []
            else:
                warns += [f"keeps the game's text: {e}" for e in errs]
                errs, raw = [], None
        errors += [f"{key}: {e}" for e in errs]
        warnings += [f"{key}: {w}" for w in warns]
        if raw is not None and not errs:
            entries[key] = raw
    if metrics is not None:
        # Text the game joins (an item's name and a suffix in one buffer): the pack's longest of each.
        resident = {k: bytes.fromhex(r["raw"]) if r.get("raw") else (r["length"], r.get("menu_px", 0))
                    for k, r in strings.items() if not k.startswith("dlg/") and (r.get("raw") or "length" in r)}
        resident.update({k: v for k, v in entries.items() if not k.startswith("dlg/")})
        over: set[str] = set()
        joined = menu.check_combined(resident, metrics, layouts, over)
        while a.from_patch and joined and over & entries.keys():
            # As with a single line: the longest parts are drawn narrower, or past that go back
            # to the game's text, until it fits.
            parts = sorted(over & entries.keys())
            if not a.bytes_only and not any(stretch.stays_bytes(k) for k in parts):
                for n in range(255, menu.NARROW_FLOOR - 1, -1):
                    trial = dict(resident, **{k: menu.narrow(entries[k], n) for k in parts})
                    still: set[str] = set()
                    menu.check_combined(trial, metrics, layouts, still)
                    if not still & set(parts):
                        warnings += [f"joined text drawn at {n * 100 // 256}% width to fit: {e}" for e in joined]
                        for k in parts:
                            entries[k] = resident[k] = trial[k]
                        break
                else:
                    n = None
                if n is not None:
                    over.clear()
                    joined = menu.check_combined(resident, metrics, layouts, over)
                    continue
            warnings += [f"joined text keeps the game's text: {e}" for e in joined]
            for key in over & entries.keys():
                del entries[key]
                resident[key] = bytes.fromhex(strings[key]["raw"]) if strings[key].get("raw") else (
                    strings[key]["length"], strings[key].get("menu_px", 0))
            over.clear()
            joined = menu.check_combined(resident, metrics, layouts, over)
        (warnings if a.allow_overflow else errors).extend(f"joined text: {e}" for e in joined)
    glossary = {}
    if a.glossary:
        glossary = {r["english"]: r["translation"] for r in read_csv(a.glossary) if r.get("english") and r.get("translation")}
    texts = {r["key"]: r["translation"] for r in rows if r.get("translation") and r.get("key") in entries}
    warnings += [f"consistency: {w}" for w in consistency(texts, strings, glossary)]
    # Dialogue pages keep the page end the game had (F9 instead of FF closes some boxes), and the
    # pack says which bank is which: "bank/<crc32>" holds that bank's key ("M001") in ASCII.
    for key, raw in list(entries.items()):
        end = strings[key].get("end", "FF")
        if key.startswith("dlg/") and end != "FF":
            entries[key] = raw + bytes([int(end, 16)])
    for b in banks:
        entries[f"bank/{b['crc32']}"] = b["key"].encode("ascii")
    hashes: dict[int, str] = {}
    for key in entries:
        h = fnv1a(key)
        if h in hashes:
            errors.append(f"{key}: key hash collides with {hashes[h]}")
        hashes[h] = key
    for w in warnings[: a.max_messages]:
        print(f"warning: {w}")
    for e in errors[: a.max_messages]:
        print(f"error: {e}")
    translated = sum(1 for k in entries if not k.startswith("bank/"))
    print(f"{translated} of {len(strings)} keys translated, {len(banks)} banks; "
          f"{len(warnings)} warning(s), {len(errors)} error(s)")
    if errors:
        return 1
    stretches = name_letters = None
    if not a.bytes_only:
        try:
            keep, keep_from = names_to_keep(a.keep_names_from, a.out)
        except OSError as e:
            print(f"error: --keep-names-from {a.keep_names_from}: {e}")
            return 1
        entries, name_letters, name_errors = namelib.convert_entries(entries, atlas, keep)
        if name_letters.kept:
            print(f"kept the numbers of {name_letters.kept} name letter(s) outside the shared list "
                  f"from {keep_from}, so names saved with it read the same")
        if name_errors:
            for e in name_errors[: a.max_messages]:
                print(f"error: {e}")
            return 1
        entries, stretches = stretch.convert_entries(entries, atlas)
    write_pack(a.out, a.lang, a.name, entries, atlas, stretches, name_letters)
    print(f"wrote {a.out}")
    return 0


def names_to_keep(given: Path | None, out: Path) -> tuple[dict[str, int], Path | None]:
    """The name letter numbers a build keeps (petext/names.py), and where from: the pack given, else
    the pack the build writes over, if it reads as one; plain or a zipped .pelang (bundle). Raises
    OSError when the given one can't be read."""
    source = given or (out if out.is_file() else None)
    if not source:
        return {}, None
    try:
        return namelib.read_numbers(pack_bytes(source)), source
    except (OSError, ValueError, zipfile.BadZipFile) as e:
        if given:
            raise OSError(str(e)) from e
        return {}, None


HEADER_SIZES = {1: 64, 2: 80, 3: 88, 4: 112}


def write_pack(path: Path, lang: str, name: str, entries: dict[str, bytes], atlas=None,
               stretches=None, name_letters=None) -> None:
    """Version 1 (text), 2 (+ the CJK dictionary), 3 (+ the letter table) or, with stretches,
    4 (+ the text renderer's stretches and the name letters; mods/pe_lang/pe_lang_pack.c)."""
    lang_b, name_b = lang.encode("utf-8"), name.encode("utf-8")
    if not lang_b or not name_b or b"\0" in lang_b + name_b or len(lang_b) > 15 or len(name_b) > 31:
        raise SystemExit("--lang and --name must be nonempty, without NUL; at most 15 and 31 bytes")
    order = sorted(entries, key=fnv1a)
    text, table = bytearray(), bytearray()
    for key in order:
        table += ENTRY.pack(fnv1a(key), len(text))
        text += entries[key] + b"\xff"
    extra = b''
    records = bytes(atlas.records) if atlas else b''
    letters = atlas.letters.records() if atlas and atlas.letters else b''
    version = 4 if stretches is not None else 3 if letters else 2 if records else 1
    header_size = HEADER_SIZES[version]
    tail = b''
    if version > 1:
        dictionary = header_size+len(table)+len(text)
        extra = struct.pack('<IIII',dictionary,len(atlas.chars) if records else 0,cjk.RECORD,0)
    if version > 2:
        extra += struct.pack('<II',dictionary+len(records),len(letters)//lettertable.RECORD)
    if version > 3:
        end = dictionary + len(records) + len(letters)
        pad = -end % 4
        section, pool = stretches.section()
        tail = bytes(pad) + section
        extra += struct.pack('<IIII', end + pad, len(stretches.runs), pool, 0)
        names = name_letters.section() if name_letters else b''
        extra += struct.pack('<II', end + pad + len(section), len(names) // 4)
        tail += names
    head = HEADER.pack(MAGIC, version, len(order), header_size + len(table), lang_b, name_b)
    Path(path).write_bytes(head + extra + table + text + records + letters + tail)


BUNDLE_TEXT = "text.bin"          # the pack inside a zipped .pelang (pe_lang_bundle.h)
BUNDLE_MAX = 256 << 20             # largest .pelang the game opens
PACK_MAX = 64 << 20                # largest pack the game reads


def pack_bytes(path: Path) -> bytes:
    """The compiled pack in a .pelang file: the file itself, or its zip's text.bin."""
    data = Path(path).read_bytes()
    if not data.startswith(b"PK\x03\x04"):
        return data
    with zipfile.ZipFile(Path(path)) as z:
        try:
            info = z.getinfo(BUNDLE_TEXT)
        except KeyError:
            raise ValueError(f"the zip has no {BUNDLE_TEXT}") from None
        if info.file_size > PACK_MAX:
            raise ValueError(f"{BUNDLE_TEXT} is larger than the game reads")
        return z.read(info)


def read_pack(path: Path) -> tuple[str, str, dict[int, bytes]]:
    """(lang, name, hash -> glyph bytes) of a pack, plain or zipped: the reader the tests check the
    writer with. A version 4 pack's stretches come back as the one-byte (or F1 C5) letters they
    stand for, so the text compares with what build measured."""
    data = pack_bytes(path)
    magic, version, count, text_at, lang, name = HEADER.unpack_from(data, 0)
    if magic != MAGIC or version not in HEADER_SIZES:
        raise ValueError("not a version 1-4 .pelang pack")
    out = {}
    for i in range(count):
        h, off = ENTRY.unpack_from(data, HEADER_SIZES[version] + i * ENTRY.size)
        start = text_at + off
        out[h] = data[start:data.index(b"\xff", start)]
    if version == 4:
        out = {h: _expand_stretches(data, raw) for h, raw in out.items()}
    return lang.rstrip(b"\0").decode(), name.rstrip(b"\0").decode(), out


def _expand_stretches(data: bytes, raw: bytes) -> bytes:
    dictionary, glyphs = struct.unpack_from("<II", data, 64)
    letters_at, letters = struct.unpack_from("<II", data, 80)
    at, runs = struct.unpack_from("<II", data, 88)
    back = {ch: bytes([code]) for code, ch in gt.CHARS.items()}
    back.update({ch: bytes([code]) for ch, code in gt._latin_codes().items()})
    extra = 0
    for k in range(letters):
        r = data[letters_at + k * lettertable.RECORD:]
        if lettertable.coded(k, r):
            back[chr(struct.unpack_from("<I", r)[0])] = bytes([r[4]])
        else:                                      # no code: as build wrote it before the stretches
            back[chr(struct.unpack_from("<I", r)[0])] = lettertable.extra_token(extra)
            extra += 1
    for k in range(glyphs):
        back[chr(struct.unpack_from("<I", data, dictionary + k * cjk.RECORD)[0])] = cjk.token(k)
    starts = struct.unpack_from(f"<{runs + 1}I", data, at)
    pool = at + (runs + 1) * 4
    table = stretch.Stretches()
    for k in range(runs):
        table.runs.append("".join(chr(c) for c in struct.unpack_from(
            f"<{starts[k + 1] - starts[k]}I", data, pool + starts[k] * 4)))
    return stretch.expand(raw, table, back)


# The user supplies this original patch; translated content is never bundled.
ITALIAN_PATCH_SHA256 = "1c1f414eb88adf080063c673f094e625aaca1ee40f26c3b628c931c7aae16d5d"
ITALIAN_ACCENTS = {0x4D: "à", 0x4E: "è", 0x4F: "é", 0x50: "ì", 0x51: "ò", 0x52: "ù"}


def _dialogue_pages(game, number, read=None, *, italian_separators=False):
    room = game.room(number)
    if room is None:
        raise ValueError(f"Missing dialogue room M{number:03}")
    data = room.data
    word = lambda at: struct.unpack_from("<I", data, at)[0]
    table = word(word(4) + 0x20)
    for index in range(table >> 22):
        slot = word((table & 0x3FFFFF) + index * 8 + 4)
        if slot >> 24 == 1:
            bank = data[slot & 0xFFFFFF:]
            if read is not None:
                return read(bank)
            pages, used = gt.pages(bank)
            # The verified Italian v0.90 M026 bank has an extra newline after
            # a page terminator. It is outside the page, not a script command.
            while italian_separators and bank[used:used + 2] == b"\xf7\xfe":
                following, count = gt.pages(bank[used + 1:])
                if not count:
                    break
                if pages.keys() & following.keys():
                    raise ValueError("Italian bank repeats a dialogue page ID")
                pages.update(following)
                used += count + 1
            return pages
    raise ValueError(f"Missing English dialogue bank in M{number:03}")


def _translated_dialogue_rows(original, translated, catalog, language="it", *, dialogue_adaptations=None):
    disc = {"SLUS_006.62": "1", "SLUS_006.68": "2"}.get(original.exe_name)
    if disc is None or translated.exe_name != original.exe_name:
        raise ValueError("Choose an unmodified Parasite Eve USA Disc 1 or Disc 2 image")
    rows, fallback = [], []
    for bank in catalog["banks"]:
        number = bank["locations"][disc]
        before = _dialogue_pages(original, number)
        after = _dialogue_pages(translated, number, italian_separators=language == "it")
        prefix = f'dlg/{bank["key"]}/'
        expected = {int(key[len(prefix):]) for key in catalog["entries"] if key.startswith(prefix)}
        if set(before) != expected:
            raise ValueError(f'{bank["key"]}: source page IDs do not match the US catalog')
        extra = set(after) - expected
        if extra and language != "es":
            raise ValueError(f'{bank["key"]}: patch has unexpected dialogue page IDs')
        for page in sorted(extra):
            fallback.append({"key": f"{prefix}{page:03}", "reason": "patch-only page; not imported"})
        for page, (raw, _) in sorted(before.items()):
            key = f"{prefix}{page:03}"
            if srcman.english_hash(gt.decode(raw)) != catalog["entries"][key]["english_hash"]:
                raise ValueError(f"{key}: source text does not match the US catalog")
            reason = None
            if page not in after:
                reason = "missing translated page"
            else:
                value = after[page][0]
                if language == "es":
                    value = _spanish_dialogue(value, raw, key)
                elif language == "it":
                    value = _italian_dialogue(value, key)
                if value == raw:
                    continue
                try:
                    if validation.structure(raw) != validation.structure(value):
                        reason = "original control sequence differs"
                except ValueError:
                    reason = "unsupported original or translated control sequence"
            if reason:
                fallback.append({"key": key, "reason": reason})
                continue
            try:
                text = (_spanish_text if language == "es" else _italian_text)(value, key)
            except ValueError:
                if language != "es":
                    raise
                fallback.append({"key": key, "reason": "unsupported custom translation glyph"})
                continue
            rows.append({"key": key, "translation": text,
                         "notes": "Local Green_goblin Chicago (02-02-2025) import; original speaker colours"
                         if language == "es" else "Local SadNES cITy v0.90 import; Mickey, _Ombra_, Chop"})
            if language == "it" and value != after[page][0] and dialogue_adaptations is not None:
                dialogue_adaptations.append({"key": key,
                    "reason": "Join translation-added pause; retain original control sequence"
                    if key == "dlg/M271/014" else
                    "Omit translation-added name reference; retain original control sequence"})
    return rows, fallback


def _italian_dialogue(raw, key):
    # Only exact reviewed v0.90 additions are omitted. Names, waits and choices
    # must still pass the original strict validator after this adaptation.
    profiles = {
        "dlg/M035/045": "63e4c1b9f648bd1c3826599f2f6246e829e78072ff50f19c054159671fa4dde0",
        "dlg/M041/200": "6f36cda284e7e3ac913197b5ba5e89f529c9cd5195b4c706b1388a5298e54bf2",
        "dlg/M191/133": "e9045dedabdc10f1f78215c673d4f2c5b8d72dcec829fb1833e68cb3683fb202",
        "dlg/M271/014": "f4fbba92653ed987b253f7f103384fef778acb17a3565c2373547db9e62ff3f6",
    }
    expected = profiles.get(key)
    if expected and expected == hashlib.sha256(raw).hexdigest():
        if key == "dlg/M271/014":
            # The following word already starts with a space. Preserve every
            # word and let the existing wrapper fit the original single page.
            return raw.replace(b"\xf8", b"", 1)
        address = ", {name}" if key == "dlg/M035/045" else " {name}"
        return raw.replace(gt.encode(address), b"", 1)
    return raw


def _italian_text(raw, key):
    text = gt.decode(raw)
    for code, char in ITALIAN_ACCENTS.items():
        text = text.replace(f"{{g{code:02X}}}", char)
    if re.search(r"\{g[0-9A-F]{2}\}", text):
        raise ValueError(f"{key}: unsupported translation glyph")
    return text


SPANISH_IMAGES = {
    "SLUS_006.62": (495531120, "8dcf72a9d53320ae2422a356c59db1961721d94ea1baa41e15d37c30c9bb6062"),
    "SLUS_006.68": (646877616, "d2840fe2ea715b006c518d3f7eb5bdae1698226f9be45ebc0c28d4fda5ed8bdb"),
}
SPANISH_GLYPHS = dict(enumerate("ÁÉÍÓÚáéíóú¿¡ñÑüÜ", 0x4D))
SPANISH_GLYPHS.update(enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 0x5D))
SPANISH_GLYPHS.update(enumerate("abcdefghijklmnopqrstuvwxyz", 0x77))
SPANISH_GLYPHS.update(enumerate(":áéíóúñ", 0x91))


def _spanish_text(raw, key):
    # Chicago replaces the apostrophe cell with a closing double quote and
    # uses separate yellow speaker glyphs. The port keeps its original font.
    text = gt.decode(raw).replace("'", '"')
    for code, char in SPANISH_GLYPHS.items():
        text = text.replace(f"{{g{code:02X}}}", char)
    if re.search(r"\{g[0-9A-F]{2}\}", text):
        raise ValueError(f"{key}: unsupported translation glyph")
    return text


def _spanish_dialogue(raw, original, key):
    if key == "dlg/M001/009":
        # This exact patch adds credits and repeats page 8 before page 9.
        # Retain the speaker prefix and page 9's complete actual dialogue.
        parts = raw.split(b"\xf8", 2)
        if len(parts) != 3:
            raise ValueError("Spanish opening page does not match the Chicago profile")
        raw = parts[1].split(b"\x0f", 1)[0] + b"\x0f" + parts[2].lstrip(b"\x0f")
    if b"\xfb\x04" not in original and b"\xfb\x05" not in original:
        raw = re.sub(b"\xfb\x04(\xfa[\x4a\x0f]*)\xfb\x05", lambda m: m[1], raw)
    return raw


def _translated_menu_rows(original, translated, manifest, language="it"):
    from extract_text import table_entries
    before = dict(table_entries(original.resident_pack()))
    after = dict(table_entries(translated.resident_pack()))
    expected = {key for key in manifest["entries"] if key.split("/")[0] in ("sys", "item", "desc", "misc")}
    if before.keys() != after.keys() or not expected <= before.keys():
        raise ValueError("Resident text table indices do not match the US catalog")
    rows, fallback, adaptations = [], [], []
    for key in sorted(expected):
        raw, value = before[key], after[key]
        if srcman.english_hash(gt.decode(raw)) != manifest["entries"][key]["english_hash"]:
            raise ValueError(f"{key}: resident source text does not match the US catalog")
        if value == raw:
            continue
        if key in ("sys/115", "sys/116", "sys/117"):
            fallback.append({"key": key, "reason": "native rename glyph grid; not a translated label"})
            continue
        try:
            same_controls = validation.structure(raw) == validation.structure(value)
        except ValueError:
            same_controls = False
        if not same_controls:
            fallback.append({"key": key, "reason": "original resident control sequence differs"})
            continue
        try:
            text = original_text = (_spanish_text if language == "es" else _italian_text)(value, key)
        except ValueError:
            if language != "es":
                raise
            fallback.append({"key": key, "reason": "unsupported custom translation glyph"})
            continue
        if language == "es":
            text = {"sys/078": "No hay tarjeta de memoria insertada",
                    "sys/086": "Borra los datos en el gestor de tarjetas"}.get(key, text)
        elif key in ("sys/071", "sys/072"):
            text = text.replace("nello ", "", 1)
        elif key in ("item/044", "item/045", "item/046", "item/047"):
            text = text.replace("Evasione", "Evas.", 1)
        elif key in ("item/096", "item/146"):
            text = text.replace("Arma di ", "Arma ", 1)
        if text != original_text:
            adaptations.append({"key": key, "before": original_text, "after": text,
                                "reason": "reviewed original memory-card field width" if language == "es"
                                else "reviewed original menu width / ammo-count boundary"})
        rows.append({"key": key, "translation": text,
                     "notes": "Local Green_goblin Chicago (02-02-2025) menu import"
                     if language == "es" else "Local SadNES cITy v0.90 menu import; Mickey, _Ombra_, Chop"})
    return rows, fallback, adaptations


def cmd_import_italian(a):
    from petext.disc import Game
    original = translated = None
    created = []
    try:
        # Fixed profile: reject a different patch before opening the disc.
        if a.patch.stat().st_size != 6465866 or hashlib.sha256(a.patch.read_bytes()).hexdigest() != ITALIAN_PATCH_SHA256:
            raise ValueError("Expected the original SadNES cITy Italian v0.90 PPF patch")
        original = Game(a.disc)
        translated = Game(a.disc, patch=a.patch)
        protected = {a.disc.resolve(), a.patch.resolve(), Path(original.disc.f.name).resolve()}
        if a.out.resolve() in protected or a.report.resolve() in protected or a.out.resolve() == a.report.resolve():
            raise ValueError("CSV and report must be separate from the disc and patch")
        if a.out.exists() or a.report.exists():
            raise ValueError("CSV or report already exists; choose new output paths")
        catalog = json.loads((ROOT / "editor/catalog.json").read_text(encoding="utf-8"))
        dialogue_adaptations = []
        rows, fallback = _translated_dialogue_rows(original, translated, catalog,
                                                   dialogue_adaptations=dialogue_adaptations)
        dialogue_count = len(rows)
        manifest = json.loads((ROOT / "data/manifest.json").read_text(encoding="utf-8"))
        menu_rows, menu_fallback, adaptations = _translated_menu_rows(original, translated, manifest)
        rows.extend(menu_rows); fallback.extend(menu_fallback)
        report = {"patch_sha256": ITALIAN_PATCH_SHA256, "translated_dialogue_rows": dialogue_count,
                  "translated_menu_rows": len(menu_rows), "layout_adaptations": adaptations,
                  "dialogue_control_adaptations": dialogue_adaptations,
                  "fallback_original": fallback, "source_images_modified": False,
                  "scope": "Dialogue and resident labels. Native rename grids remain original. Review all compiler layout warnings.",
                  "credits": "SadNES cITy: Mickey, _Ombra_, Chop",
                  "source": "https://www.sadnescity.it/traduzioni/pe/pe.php"}
        with a.out.open("x", encoding="utf-8", newline="") as output:
            created.append(a.out)
            writer = csv.DictWriter(output, fieldnames=SUBMISSION_COLUMNS)
            writer.writeheader(); writer.writerows(rows)
        with a.report.open("x", encoding="utf-8") as output:
            created.append(a.report)
            json.dump(report, output, ensure_ascii=False, indent=2)
            output.write("\n")
        print(f"Imported {len(rows)} text rows; {len(fallback)} entries retain original text. Review {a.report}.")
        return 0
    except (OSError, ValueError, EOFError, KeyError, struct.error) as error:
        for path in created:
            path.unlink(missing_ok=True)
        print(f"Italian import failed: {error}", file=sys.stderr)
        return 1
    finally:
        for game in (original, translated):
            if game is not None:
                game.disc.f.close()


def cmd_import_spanish(a):
    from petext.disc import Game
    original = translated = None
    created = []
    try:
        original = Game(a.disc)
        translated = Game(a.translated_disc)
        profile = SPANISH_IMAGES.get(original.exe_name)
        if profile is None or translated.exe_name != original.exe_name:
            raise ValueError("Choose matching original and Chicago-translated USA discs")
        image = translated.disc.f
        if image.seek(0, 2) != profile[0]:
            raise ValueError("Translated image size does not match Chicago (02-02-2025)")
        image.seek(0)
        digest = hashlib.sha256()
        while block := image.read(8 * 1024 * 1024):
            digest.update(block)
        if digest.hexdigest() != profile[1]:
            raise ValueError("Translated image hash does not match Chicago (02-02-2025)")
        protected = {a.disc.resolve(), a.translated_disc.resolve(),
                     Path(original.disc.f.name).resolve(), Path(image.name).resolve()}
        if a.out.resolve() in protected or a.report.resolve() in protected or a.out.resolve() == a.report.resolve():
            raise ValueError("CSV and report must be separate from both images")
        if a.out.exists() or a.report.exists():
            raise ValueError("CSV or report already exists; choose new output paths")
        catalog = json.loads((ROOT / "editor/catalog.json").read_text(encoding="utf-8"))
        rows, fallback = _translated_dialogue_rows(original, translated, catalog, "es")
        dialogue_count = len(rows)
        manifest = json.loads((ROOT / "data/manifest.json").read_text(encoding="utf-8"))
        menu_rows, menu_fallback, adaptations = _translated_menu_rows(original, translated, manifest, "es")
        rows.extend(menu_rows); fallback.extend(menu_fallback)
        report = {"translated_image_sha256": profile[1], "translated_dialogue_rows": dialogue_count,
                  "translated_menu_rows": len(menu_rows), "layout_adaptations": adaptations,
                  "fallback_original": fallback, "source_images_modified": False,
                  "scope": "Partial local text import. Review fallbacks and all compiler layout warnings. "
                           "Original font and speaker colours; patch-only credits and repeated opening line omitted. "
                           "Native rename grids and unsupported custom symbols stay original.",
                  "credits": "Green_goblin, TheMarkusBoy; ROM hacking: CUE, Ombra, Chop, Phoenix",
                  "source": "https://parasiteevetranslationproject.blogspot.com/2025/01/nuevos-parches-disponibles-con-sorpresa.html"}
        with a.out.open("x", encoding="utf-8", newline="") as output:
            created.append(a.out)
            writer = csv.DictWriter(output, fieldnames=SUBMISSION_COLUMNS)
            writer.writeheader(); writer.writerows(rows)
        with a.report.open("x", encoding="utf-8") as output:
            created.append(a.report)
            json.dump(report, output, ensure_ascii=False, indent=2)
            output.write("\n")
        print(f"Imported {len(rows)} text rows; {len(fallback)} entries not imported. Review {a.report}.")
        return 0
    except (OSError, ValueError, EOFError, KeyError, struct.error) as error:
        for path in created:
            path.unlink(missing_ok=True)
        print(f"Spanish import failed: {error}", file=sys.stderr)
        return 1
    finally:
        for game in (original, translated):
            if game is not None:
                game.disc.f.close()


def cmd_import_patch(a):
    """Any fan patch: see petext/patchimport.py."""
    import tempfile
    from extract_text import bank_slots, table_entries
    from petext import patchfile, patchimport
    from petext.disc import Game
    if a.out.exists() or a.report.exists() or (a.font and a.font.exists()):
        print("Output files already exist; choose new paths", file=sys.stderr)
        return 1
    overrides = {}
    for item in a.letters:
        code, _, char = item.partition("=")
        overrides[int(code, 16)] = char
    games = []
    try:
        with tempfile.TemporaryDirectory(dir=a.out.parent.resolve()) as folder:
            original = Game(a.disc)
            games.append(original)
            image = a.patch
            if a.patch.suffix.lower() not in (".bin", ".iso", ".img", ".cue"):
                image = Path(folder) / "patched.bin"
                print(f"{patchfile.apply_patch(a.patch, original.disc.f.name, image)} patch applied to a temporary copy")
            translated = Game(image)
            games.append(translated)
            disc = {"SLUS_006.62": "1", "SLUS_006.68": "2"}.get(original.exe_name)
            if disc is None or translated.exe_name != original.exe_name:
                raise ValueError("Choose an unmodified USA disc and its own patch")
            letters, how, unknown = patchimport.letter_map(translated)
            letters.update({k: v for k, v in overrides.items() if v})
            unknown = [code for code in unknown if code not in overrides]
            catalog = json.loads((ROOT / "editor/catalog.json").read_text(encoding="utf-8"))
            rows, left, end_changed = [], [], 0
            for bank in catalog["banks"]:
                number, prefix = bank["locations"][disc], f'dlg/{bank["key"]}/'
                before, after = _dialogue_pages(original, number), _dialogue_pages(translated, number, patchimport.patched_pages)
                left += [{"key": f"{prefix}{page:03}", "reason": "page only in the patch"} for page in sorted(set(after) - set(before))]
                for page, (raw, end) in sorted(before.items()):
                    key = f"{prefix}{page:03}"
                    if page not in after:
                        left.append({"key": key, "reason": "page missing from the patch"})
                        continue
                    end_changed += after[page][1] != end
                    text, why = patchimport.compare_page(raw, after[page][0], letters)
                    if why:
                        left.append({"key": key, "reason": why})
                    elif text is not None:
                        rows.append({"key": key, "translation": text, "notes": "patch import"})
            manifest = json.loads((ROOT / "data/manifest.json").read_text(encoding="utf-8"))
            before, after = dict(table_entries(original.resident_pack())), dict(table_entries(translated.resident_pack()))
            for key in sorted(k for k in manifest["entries"] if k.split("/")[0] in ("sys", "item", "desc", "misc")):
                if key in patchimport.RENAME_GRIDS:
                    continue
                text, why = patchimport.compare_page(before[key], after[key], letters)
                if why:
                    left.append({"key": key, "reason": why})
                elif text is not None:
                    rows.append({"key": key, "translation": text, "notes": "patch import"})
            report = {"translated_rows": len(rows), "kept_english": left, "letters_found": {f"{k:02X}": v for k, v in how.items()},
                      "symbols_without_a_letter": [f"{code:02X}" for code in unknown],
                      "pages_with_a_different_end": end_changed,
                      "rooms_with_edited_scripts": patchimport.script_changes(original, translated, lambda d: min(bank_slots(d).values(), default=len(d))),
                      "rooms_with_edited_pictures": patchimport.picture_changes(original, translated),
                      "boot_program_bytes_changed": patchimport.exe_changes(original, translated)}
            with a.out.open("x", encoding="utf-8", newline="") as output:
                writer = csv.DictWriter(output, fieldnames=SUBMISSION_COLUMNS)
                writer.writeheader()
                writer.writerows(rows)
            with a.report.open("x", encoding="utf-8") as output:
                json.dump(report, output, ensure_ascii=False, indent=2)
                output.write("\n")
            if a.font:
                a.font.write_bytes(patchimport.font_picture(translated))
            print(f"{len(rows)} rows imported; {len(left)} keep the English; symbols without a letter: "
                  f"{report['symbols_without_a_letter'] or 'none'}; rooms with edited scripts: "
                  f"{len(report['rooms_with_edited_scripts'])}. Review {a.report}.")
            for game in games:
                game.disc.f.close()
            games.clear()
        return 0
    except (OSError, ValueError, EOFError, KeyError, struct.error) as error:
        print(f"Patch import failed: {error}", file=sys.stderr)
        return 1
    finally:
        for game in games:
            game.disc.f.close()


def cmd_bundle(a) -> int:
    """A translation file for players: the pack zipped as text.bin, with an optional readme."""
    try:
        pack = pack_bytes(a.pack)
        lang, name, entries = read_pack(a.pack)
    except (OSError, ValueError, struct.error, zipfile.BadZipFile) as err:
        print(f"error: {a.pack} is not a .pelang pack: {err}", file=sys.stderr)
        return 1
    if len(pack) > PACK_MAX:
        print(f"error: {a.pack} is larger than the game reads", file=sys.stderr)
        return 1
    readme = a.readme.read_bytes() if a.readme else None
    a.out.parent.mkdir(parents=True, exist_ok=True)
    tmp = a.out.with_name(a.out.name + ".tmp")
    # Fixed dates, so the same pack always makes the same file.
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(zipfile.ZipInfo(BUNDLE_TEXT, (1980, 1, 1, 0, 0, 0)), pack, zipfile.ZIP_DEFLATED)
        if readme is not None:
            z.writestr(zipfile.ZipInfo("readme.txt", (1980, 1, 1, 0, 0, 0)), readme, zipfile.ZIP_DEFLATED)
    if tmp.stat().st_size > BUNDLE_MAX:
        tmp.unlink()
        print(f"error: {a.out} would be larger than the game opens", file=sys.stderr)
        return 1
    tmp.replace(a.out)
    print(f"{a.out}: {name} ({lang}), {len(entries)} entries, {a.out.stat().st_size} bytes")
    return 0


def main(argv=None) -> int:
    # Messages quote the translator's text; a console that can't show a character gets an escape
    # (Б) instead of stopping the build.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="backslashreplace")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    t = sub.add_parser("template", help="a translator CSV from strings.json")
    t.add_argument("strings", type=Path)
    t.add_argument("-o", "--out", type=Path, required=True)
    t.add_argument("--merge", type=Path, help="carry the translations of an older CSV over, by key")
    p = sub.add_parser("pseudo", help="a test language: every entry, marked and ~30%% longer")
    p.add_argument("strings", type=Path)
    p.add_argument("-o", "--out", type=Path, required=True)
    sa = sub.add_parser("sample", help="a test language: the game's tags, and the words of the languages you name")
    sa.add_argument("strings", type=Path, help="strings.json from extract_text.py")
    sa.add_argument("languages", nargs="+", help="language codes from petext/alphabets.py: `pelang.py languages` lists them")
    sa.add_argument("-o", "--out", type=Path, required=True)
    sub.add_parser("languages", help="which languages the pack tool can write today")
    b = sub.add_parser("build", help="check a translated CSV and write a .pelang pack")
    b.add_argument("csv", type=Path)
    b.add_argument("--strings", type=Path, required=True,
                   help="strings.json from extract_text.py, or the committed data/manifest.json (no discs needed)")
    b.add_argument("--from-patch", action="store_true",
                   help="the CSV came from import-patch: waits, names and timing may differ from the English")
    b.add_argument("--submission", action="store_true",
                   help="the file is a submission: refuse source text (english/japanese/context columns)")
    b.add_argument("--glossary", type=Path, help="CSV of english,translation terms to keep consistent "
                                                 "(checked where the English is known: with strings.json)")
    b.add_argument("--banks", type=Path, help="banks.json from extract_text.py (default: beside strings.json)")
    b.add_argument("--lang", required=True, help="language code, e.g. fr, pt-BR, en-x-jp, qps")
    b.add_argument("--name", required=True, help="the name the Language row shows, e.g. French")
    b.add_argument("-o", "--out", type=Path, required=True)
    b.add_argument("--cjk-font", type=Path, action="append", help="external TTF/OTF/TTC face 0; repeat for fallback fonts")
    b.add_argument("--max-messages", type=int, default=50)
    b.add_argument("--allow-overflow", action="store_true",
                   help="emit known layout overflows as warnings for deliberate in-game testing; controls stay strict")
    b.add_argument("--bytes-only", action="store_true",
                   help="write each letter as its own byte (a version 1-3 pack) instead of the text "
                        "renderer's stretches")
    b.add_argument("--keep-names-from", type=Path,
                   help="the previous version of this pack (a .pelang: from build, or the translation file): name letters outside the shared list "
                        "keep its numbers, so names in players' saves read the same (default: the file "
                        "-o writes over, when there is one)")
    b.add_argument("--patch-letters", type=Path,
                   help="a letter pictures file from import-patch --patch-letters: the pack draws its "
                        "letters as the patch does, with the patch's widths, and lays its text out with them")
    b.add_argument("--no-wrap", action="store_true",
                   help="don't rewrap dialogue pages that overflow their box (by default they get line breaks at "
                        "spaces and, if still too tall, more screens)")
    bu = sub.add_parser("bundle", help="a translation file for players: the pack in a .pelang zip")
    bu.add_argument("pack", type=Path, help="a .pelang pack from build")
    bu.add_argument("-o", "--out", type=Path, required=True, help="the .pelang file to write")
    bu.add_argument("--readme", type=Path, help="a text file shown to players: credits, version, notes")
    e = sub.add_parser("export", help="the file to submit: key, translation, notes (no source text)")
    e.add_argument("csv", type=Path)
    e.add_argument("-o", "--out", type=Path, required=True)
    m = sub.add_parser("manifest", help="write the source manifest (no game text) from strings.json")
    m.add_argument("strings", type=Path)
    m.add_argument("-o", "--out", type=Path, required=True)
    h = sub.add_parser("latin-header", help="write the Latin Extended table the pe_lang mod composes "
                                            "from, and the test vectors its composer is checked with")
    h.add_argument("out", type=Path, nargs="?", default=ROOT.parent / "mods" / "pe_lang" / "pe_lang_latin.h")
    h.add_argument("--vectors", type=Path, default=ROOT.parent / "tests" / "pe_lang_font_vectors.h")
    for command in (t, p, b):
        command.add_argument("--mod-strings", type=Path, help="optional mod-owned key and layout manifest")
    c = sub.add_parser("cyrillic-header", help="regenerate authored Cyrillic glyph data")
    c.add_argument("out", type=Path, nargs="?", default=ROOT.parent / "mods/pe_lang/pe_lang_cyrillic_data.h")
    u = sub.add_parser("interface-header", help="regenerate the mod interface Unicode bridge")
    u.add_argument("out", type=Path, nargs="?", default=ROOT.parent / "mods/pe_lang/pe_lang_text_data.h")
    it = sub.add_parser("import-italian", help="locally import text from the user's SadNES cITy v0.90 PPF")
    it.add_argument("disc", type=Path, help="unmodified USA Disc 1 or Disc 2 CUE/BIN")
    it.add_argument("patch", type=Path, help="itpch_pe_v090.ppf extracted from the author's archive")
    it.add_argument("-o", "--out", type=Path, required=True, help="new translation CSV")
    it.add_argument("--report", type=Path, required=True, help="new JSON report of original-text fallbacks")
    es = sub.add_parser("import-spanish", help="locally import text from the Chicago (02-02-2025) translation")
    es.add_argument("disc", type=Path, help="unmodified USA Disc 1 or Disc 2 CUE/BIN")
    es.add_argument("translated_disc", type=Path, help="separate copy patched with the author's Chicago XDELTA")
    es.add_argument("-o", "--out", type=Path, required=True, help="new translation CSV")
    es.add_argument("--report", type=Path, required=True, help="new JSON report of original-text fallbacks")
    ip = sub.add_parser("import-patch", help="turn any fan patch (PPF, xdelta or a patched image) into a translation file")
    ip.add_argument("disc", type=Path, help="unmodified USA Disc 1 or Disc 2 CUE/BIN")
    ip.add_argument("patch", type=Path, help="the patch, or a patched copy of the disc")
    ip.add_argument("-o", "--out", type=Path, required=True, help="new translation CSV")
    ip.add_argument("--report", type=Path, required=True, help="new JSON report: what kept the English and why")
    ip.add_argument("--font", type=Path, help="also write the patch's letters as a picture font (.png)")
    ip.add_argument("--letters", nargs="*", default=[], metavar="HEX=CHAR",
                    help="name a code the import couldn't identify, e.g. 5A=Ñ")
    a = ap.parse_args(argv)
    if a.cmd == "interface-header":
        from petext import interfaces
        interfaces.write_header(a.out)
        return 0
    return {"import-italian": cmd_import_italian, "import-spanish": cmd_import_spanish, "import-patch": cmd_import_patch,
            "template": cmd_template, "pseudo": cmd_pseudo, "sample": cmd_sample, "languages": cmd_languages,
            "build": cmd_build, "bundle": cmd_bundle,
            "export": cmd_export, "manifest": cmd_manifest,
            "latin-header": cmd_latin_header, "cyrillic-header": cmd_cyrillic_header}[a.cmd](a)


def cmd_cyrillic_header(a) -> int:
    from petext import cyrillic
    cyrillic.write_c_header(a.out)
    print(f"wrote {a.out}: {len(cyrillic.GLYPHS)} authored Cyrillic glyphs")
    return 0


def cmd_latin_header(a) -> int:
    latin.write_c_header(str(a.out))
    latin.write_test_vectors(str(a.vectors))
    lettertable.write_test_vectors(str(a.vectors.with_name("pe_lang_letter_vectors.h")))
    print(f"wrote {a.out}: {len(latin.GLYPHS)} glyphs, codes 0x{latin.FIRST_CODE:02X}-0x{latin.GLYPHS[-1].code:02X}; "
          f"test vectors {a.vectors}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
