"""Menu text (the resident tables sys/item/desc/misc): how wide the menus draw it, and the limits
of the places that draw it (data/menu_layouts.json).

The menus draw with src/main/draw/text.c, not the dialogue box: each glyph is its width from the
resident pack's glyph metrics (high nibble) plus 1 px, 2 px for digits and the space
(Draw_MeasureTextWidth). There are no line breaks; FA..FF codes pick the next glyph's page.

The metrics are game data, so they come from the disc: extract_text.py writes them to
menu_glyphs.json beside strings.json.
"""
from __future__ import annotations

import fnmatch
import json
from pathlib import Path

from . import gametext as gt
from . import latin, cyrillic

LAYOUTS = Path(__file__).resolve().parent.parent / "data" / "menu_layouts.json"


def latin_metric(code: int, metrics: bytes) -> int | None:
    """The metrics byte pe_lang writes for a Latin Extended code (pe_lang_font_metric)."""
    g = latin.BY_CODE.get(code)
    if not g:
        return None
    if g.base:
        return metrics[gt.CODE_OF[g.base]]
    return min(latin.advance(g, None), 15) << 4


def width(raw: bytes, metrics: bytes) -> int:
    """Draw_MeasureTextWidth, with the Latin Extended codes as pe_lang sets them."""
    w, page = 0, 0
    i = 0
    while i < len(raw):
        c = raw[i]
        if raw[i:i+2] == b'\xf1\xc5':
            w += 12
            page = 0
            i += 4
            continue
        i += 1
        if c == 0xFF:
            break
        if c >= 250:
            page = c - 250
            continue
        glyph, page = c + (page << 8), 0
        if glyph == 0x100:                       # page 1 code 0: a 12 px sprite
            w += 12
            continue
        extra = 2 if glyph < 10 or glyph == 15 else 1
        if glyph >= 256:
            glyph -= 19
        m = latin_metric(glyph, metrics) if latin.FIRST_CODE <= glyph < 0xC0 else None
        if glyph in cyrillic.BY_CODE:
            m = min(cyrillic.BY_CODE[glyph].advance, 15) << 4
        w += ((metrics[glyph % 0x1B9] if m is None else m) >> 4) + extra
    return w


def load_metrics(strings_path) -> bytes | None:
    p = Path(strings_path).with_name("menu_glyphs.json")
    if not p.exists():
        return None
    return bytes.fromhex(json.loads(p.read_text(encoding="utf-8"))["metrics"])


def _expand(pattern: str) -> callable:
    """"item/000-234", "sys/115", "desc/*" -> a key test."""
    table, _, rest = pattern.partition("/")
    if "-" in rest:
        lo, hi = (int(x) for x in rest.split("-"))
        return lambda k: k.startswith(table + "/") and lo <= int(k.split("/")[1]) <= hi
    return lambda k: fnmatch.fnmatchcase(k, pattern)


def load_layouts(path=LAYOUTS) -> dict:
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    for c in data["contexts"]:
        c["_match"] = [_expand(p) for p in c["keys"]]
    for c in data.get("combined", []):
        c["_parts"] = [[_expand(p) for p in alt.split("|")] for alt in c["parts"]]
    return data


def _kind_in(kind, ranges) -> bool:
    """Whether an item kind is in [[lo, hi], ...]; no ranges means every entry, an unknown kind
    (no item record names the key) counts as in."""
    return not ranges or kind is None or any(lo <= kind <= hi for lo, hi in ranges)


def contexts_of(key: str, layouts: dict) -> list[dict]:
    return [c for c in layouts["contexts"] if any(m(key) for m in c["_match"])]


def check(key: str, raw: bytes, source: dict, metrics: bytes, layouts: dict):
    """(errors, warnings, covered) for one menu entry; source is its strings.json row. covered is
    False when no context names the key, so the caller falls back to the table-wide comparison."""
    from .manifest import english_length
    errors, warnings = [], []
    english_len = english_length(source)
    found = contexts_of(key, layouts)
    px = width(raw, metrics)
    for c in found:
        where = f"{c['id']} ({c['source']})"
        if c.get("same_length") and len(raw) != english_len:
            errors.append(f"{len(raw)} bytes; {where} reads exactly {english_len} glyph cells")
        if c.get("glyphs_only") and (b"\xf1\xc5" in raw or any(b >= 0xFA for b in raw)):
            errors.append(f"a page code (FA..FF) in {where}, which takes each byte as one cell's glyph")
        if "max_glyphs" in c and len(raw) > c["max_glyphs"]:
            warnings.append(f"{len(raw)} glyphs; {where} keeps the first {c['max_glyphs']}")
        if "bytes" in c and len(raw) + 1 > c["bytes"]:
            errors.append(f"{len(raw) + 1} bytes with its end; {where} holds {c['bytes']}")
        if "edge" in c and px > c["edge"]:
            errors.append(f"{px} px; {where} reaches the screen edge at {c['edge']} px")
        elif "width" in c and px > c["width"]:
            msg = f"{px} px; {where} fits {c['width']} px"
            (errors if c.get("width_is_hard") else warnings).append(msg + ("" if c.get("confirmed") else "; check it in game"))
        elif "soft_width" in c and px > c["soft_width"] and _kind_in(source.get("item_kind"), c.get("soft_kinds")):
            warnings.append(f"{px} px; {c['soft_note']}")
    return errors, warnings, bool(found)


def check_combined(texts: dict[str, bytes], metrics: bytes, layouts: dict):
    """Pack-wide limits on text the game joins: the widest/longest of each part together.
    texts: every resident key's bytes as the pack would have it (translation or English), or for
    an English entry known only from the source manifest, its (length, menu px)."""
    def n_bytes(v):
        return v[0] if isinstance(v, tuple) else len(v)

    def n_px(v):
        return v[1] if isinstance(v, tuple) else width(v, metrics)

    errors = []
    for c in layouts.get("combined", []):
        worst_px, worst_len, names = 0, 0, []
        for alt in c["_parts"]:
            keys = [k for k in texts if any(m(k) for m in alt)]
            if not keys:
                break
            by_len = max(keys, key=lambda k: n_bytes(texts[k]))
            by_px = max(keys, key=lambda k: n_px(texts[k]))
            worst_len += n_bytes(texts[by_len])
            worst_px += n_px(texts[by_px])
            names.append(by_len if by_len == by_px else f"{by_len}/{by_px}")
        else:
            where = f"{c['id']} ({c['source']})"
            if "bytes" in c and worst_len + 1 > c["bytes"]:
                errors.append(f"{' + '.join(names)}: {worst_len + 1} bytes with the end; {where} holds {c['bytes']}")
            if "edge" in c and worst_px > c["edge"]:
                errors.append(f"{' + '.join(names)}: {worst_px} px; {where} reaches the screen edge at {c['edge']} px")
    return errors
