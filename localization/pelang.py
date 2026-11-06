"""Language packs for the pe_lang mod: translator templates, a test language, and the compiler.

  python -I pelang.py template out/text/strings.json -o fr.csv [--merge old-fr.csv]
  python -I pelang.py pseudo   out/text/strings.json -o qps.csv
  python -I pelang.py build    fr.csv --strings out/text/strings.json --lang fr --name "French" -o fr.pelang

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
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
from petext import gametext as gt, latin, validation, layout, menu  # noqa: E402
from petext import modtext, cjk
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


def check_row(key: str, text: str, source: dict, widest_name: dict[str, int], name_px: int = layout.NAME_WIDTH,
              metrics: bytes | None = None, layouts: dict | None = None, wrap: bool = True, atlas=None):
    """(glyph bytes or None, errors, warnings) for one translated row. name_px: how wide {name}
    draws, from the pack's sys/030 (Aya's default name); metrics: the menus' glyph widths
    (menu_glyphs.json), without which menu text is only compared with the English. wrap: a
    dialogue page that overflows its box gets line breaks and screens (petext/wrap.py)."""
    errors, warnings = [], []
    try:
        raw = gt.encode(text, extended=True, cjk_codes=atlas.codes if atlas else None)
    except (ValueError, TypeError, OverflowError) as e:
        msg = "invalid text/tag: " + str(e)
        if "no glyph" in msg:
            msg += " (the font has A-Z, a-z, digits, basic punctuation, Latin Extended, Russian/Ukrainian/Belarusian Cyrillic, and CJK with --cjk-font)"
        return None, [msg], warnings
    try:
        validation.check(raw, srcman.source_structure(source))
    except ValueError as e:
        return None, [str(e)], warnings
    for command in validation.commands(raw):
        if command[0] == 0xF1 and (not atlas or cjk.index(command) >= len(atlas.chars)):
            return None, ["CJK token has no glyph in this pack's dictionary"], []
    if key in ("sys/115", "sys/116", "sys/117") and b"\xf1\xc5" in raw:
        return None, ["CJK cannot be used in the one-byte rename grids"], []
    if source.get("mod_label"):
        if any(c[0] > 0xF1 for c in validation.commands(raw)):
            return None, ["mod labels cannot contain line breaks, waits or commands"], []
        return raw, modtext.check(raw, source, metrics), []
    if key == "sys/030" and b"\xf1\xc5" in raw:
        return None, ["CJK player names cannot be stored safely in the legacy save name field; keep the original name"], []
    if key == "sys/030" and len(raw) > 8:
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
    try:
        atlas = cjk.Atlas([r.get('translation','') for r in rows], getattr(a,'cjk_font',None))
    except (ValueError, OSError, struct.error) as e:
        print(f'error: CJK font: {e}')
        return 1
    name_px = layout.NAME_WIDTH
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
                                     wrap=not a.no_wrap, atlas=atlas)
        src = strings[key]
        same = text == src["text"] if "text" in src else srcman.english_hash(text) == src.get("english_hash")
        if same and any(ch.isalpha() for ch in text):
            warns.append("the translation is the English unchanged (leave it empty to keep the game's text)")
        if a.allow_overflow:
            # check_row returns raw only after control validation succeeds.
            if raw is not None:
                warns += errs
                errs = []
        errors += [f"{key}: {e}" for e in errs]
        warnings += [f"{key}: {w}" for w in warns]
        if raw is not None and not errs:
            entries[key] = raw
    if metrics is not None:
        # Text the game joins (an item's name and a suffix in one buffer): the pack's longest of each.
        resident = {k: bytes.fromhex(r["raw"]) if r.get("raw") else (r["length"], r.get("menu_px", 0))
                    for k, r in strings.items() if not k.startswith("dlg/") and (r.get("raw") or "length" in r)}
        resident.update({k: v for k, v in entries.items() if not k.startswith("dlg/")})
        joined = menu.check_combined(resident, metrics, layouts)
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
    write_pack(a.out, a.lang, a.name, entries, atlas)
    print(f"wrote {a.out}")
    return 0


def write_pack(path: Path, lang: str, name: str, entries: dict[str, bytes], atlas=None) -> None:
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
    version = 2 if records else 1
    header_size = 80 if records else 64
    if records:
        extra = struct.pack('<IIII',header_size+len(table)+len(text),len(atlas.chars),cjk.RECORD,0)
    head = HEADER.pack(MAGIC, version, len(order), header_size + len(table), lang_b, name_b)
    Path(path).write_bytes(head + extra + table + text + records)


def read_pack(path: Path) -> tuple[str, str, dict[int, bytes]]:
    """(lang, name, hash -> glyph bytes) of a pack: the reader the tests check the writer with."""
    data = Path(path).read_bytes()
    magic, version, count, text_at, lang, name = HEADER.unpack_from(data, 0)
    if magic != MAGIC or version not in (1,2):
        raise ValueError("not a version 1/2 .pelang pack")
    out = {}
    for i in range(count):
        h, off = ENTRY.unpack_from(data, (80 if version == 2 else 64) + i * ENTRY.size)
        start = text_at + off
        out[h] = data[start:data.index(b"\xff", start)]
    return lang.rstrip(b"\0").decode(), name.rstrip(b"\0").decode(), out


# The user supplies this original patch; translated content is never bundled.
ITALIAN_PATCH_SHA256 = "1c1f414eb88adf080063c673f094e625aaca1ee40f26c3b628c931c7aae16d5d"
ITALIAN_ACCENTS = {0x4D: "à", 0x4E: "è", 0x4F: "é", 0x50: "ì", 0x51: "ò", 0x52: "ù"}


def _dialogue_pages(game, number):
    room = game.room(number)
    if room is None:
        raise ValueError(f"Missing dialogue room M{number:03}")
    data = room.data
    word = lambda at: struct.unpack_from("<I", data, at)[0]
    table = word(word(4) + 0x20)
    for index in range(table >> 22):
        slot = word((table & 0x3FFFFF) + index * 8 + 4)
        if slot >> 24 == 1:
            return gt.pages(data[slot & 0xFFFFFF:])[0]
    raise ValueError(f"Missing English dialogue bank in M{number:03}")


def _italian_dialogue_rows(original, translated, catalog):
    disc = {"SLUS_006.62": "1", "SLUS_006.68": "2"}.get(original.exe_name)
    if disc is None or translated.exe_name != original.exe_name:
        raise ValueError("Choose an unmodified Parasite Eve USA Disc 1 or Disc 2 image")
    rows, fallback = [], []
    for bank in catalog["banks"]:
        number = bank["locations"][disc]
        before, after = _dialogue_pages(original, number), _dialogue_pages(translated, number)
        prefix = f'dlg/{bank["key"]}/'
        expected = {int(key[len(prefix):]) for key in catalog["entries"] if key.startswith(prefix)}
        if set(before) != expected:
            raise ValueError(f'{bank["key"]}: source page IDs do not match the US catalog')
        if set(after) - expected:
            raise ValueError(f'{bank["key"]}: patch has unexpected dialogue page IDs')
        for page, (raw, _) in sorted(before.items()):
            key = f"{prefix}{page:03}"
            if srcman.english_hash(gt.decode(raw)) != catalog["entries"][key]["english_hash"]:
                raise ValueError(f"{key}: source text does not match the US catalog")
            reason = None
            if page not in after:
                reason = "missing translated page"
            else:
                value = after[page][0]
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
            text = gt.decode(value)
            for code, char in ITALIAN_ACCENTS.items():
                text = text.replace(f"{{g{code:02X}}}", char)
            # Fail rather than exporting untranslated font indices from another font.
            if re.search(r"\{g[0-9A-F]{2}\}", text):
                raise ValueError(f"{key}: unsupported translation glyph")
            rows.append({"key": key, "translation": text,
                         "notes": "Local SadNES cITy v0.90 import; Mickey, _Ombra_, Chop"})
    return rows, fallback


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
        rows, fallback = _italian_dialogue_rows(original, translated, catalog)
        report = {"patch_sha256": ITALIAN_PATCH_SHA256, "translated_dialogue_rows": len(rows),
                  "fallback_original": fallback, "source_images_modified": False,
                  "scope": "Dialogue only. Compile with pelang build and review all layout warnings.",
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
        print(f"Imported {len(rows)} dialogue rows; {len(fallback)} pages retain original text. Review {a.report}.")
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
    b = sub.add_parser("build", help="check a translated CSV and write a .pelang pack")
    b.add_argument("csv", type=Path)
    b.add_argument("--strings", type=Path, required=True,
                   help="strings.json from extract_text.py, or the committed data/manifest.json (no discs needed)")
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
    b.add_argument("--no-wrap", action="store_true",
                   help="don't rewrap dialogue pages that overflow their box (by default they get line breaks at "
                        "spaces and, if still too tall, more screens)")
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
    it = sub.add_parser("import-italian", help="locally import dialogue from the user's SadNES cITy v0.90 PPF")
    it.add_argument("disc", type=Path, help="unmodified USA Disc 1 or Disc 2 CUE/BIN")
    it.add_argument("patch", type=Path, help="itpch_pe_v090.ppf extracted from the author's archive")
    it.add_argument("-o", "--out", type=Path, required=True, help="new translation CSV")
    it.add_argument("--report", type=Path, required=True, help="new JSON report of original-text fallbacks")
    a = ap.parse_args(argv)
    if a.cmd == "interface-header":
        from petext import interfaces
        interfaces.write_header(a.out)
        return 0
    return {"import-italian": cmd_import_italian, "template": cmd_template, "pseudo": cmd_pseudo, "build": cmd_build,
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
    print(f"wrote {a.out}: {len(latin.GLYPHS)} glyphs, codes 0x{latin.FIRST_CODE:02X}-0x{latin.GLYPHS[-1].code:02X}; "
          f"test vectors {a.vectors}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
