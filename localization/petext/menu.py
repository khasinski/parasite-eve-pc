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
        return gt.base_metric(gt.CODE_OF[g.base], metrics)
    return min(latin.advance(g, None), 15) << 4


def _arabic_at(raw: bytes, i: int, page: int):
    """(letter, bytes) when a right-to-left pack's Arabic letter starts at raw[i], else None."""
    if not gt.RTL:
        return None
    from . import rtl
    if raw[i:i + 4] in gt.PACK_EXTRA:
        ch = gt.PACK_EXTRA[raw[i:i + 4]].char
        return (ch, 4) if rtl.is_arabic(ord(ch)) else None
    if not page and raw[i] in gt.PACK_LETTERS and rtl.is_arabic(ord(gt.PACK_LETTERS[raw[i]].char)):
        return gt.PACK_LETTERS[raw[i]].char, 1
    return None


# Narrowing: a line too wide for its room is drawn at (128 + k) / 256 of its width (petext/stretch.py
# NARROW). Below this it would be too cramped to read, and the line keeps the game's text instead.
NARROW_FLOOR = 192              # 75%


def narrowed(px: int, n: int) -> int:
    """How wide a line `px` wide draws at n / 256 (text_render.c Stretch's rounding)."""
    return (px * n + 128) >> 8


def strip_narrow(raw: bytes) -> tuple[bytes, int]:
    """(`raw` without its narrowing mark, n / 256 it is drawn at: 256 when not narrowed)."""
    from .stretch import NARROW
    if raw.startswith(NARROW) and len(raw) >= 4:
        return raw[4:], 128 + raw[3]
    return raw, 256


def narrow(raw: bytes, n: int) -> bytes:
    """`raw` drawn at n / 256 of its width (at most as narrow as it already was)."""
    from .stretch import NARROW
    plain, was = strip_narrow(raw)
    n = min(n, was)
    return plain if n >= 256 else NARROW + bytes([n - 128]) + plain


def width(raw: bytes, metrics: bytes) -> int:
    """Draw_MeasureTextWidth, with the Latin Extended codes as pe_lang sets them. A right-to-left
    pack's Arabic letters measure joined, as the renderer draws them (petext/rtl.py). A line split
    with a line break (split) is as wide as its widest line."""
    from . import letters, rtl
    from .stretch import LINE_BREAK
    if LINE_BREAK in raw:
        return max(width(part, metrics) for part in raw.split(LINE_BREAK))
    raw, n = strip_narrow(raw)
    if n != 256:
        return narrowed(width(raw, metrics), n)
    w, page = 0, 0
    i = 0
    run = []

    def joined():
        out = rtl.width("".join(run), lambda ch: letters.CATALOG[ch].joined_advance(),
                        lambda ch: letters.CATALOG[ch].cut()[1]) if run else 0
        run.clear()
        return out

    while i < len(raw):
        arabic = _arabic_at(raw, i, page)
        if arabic:
            run.append(arabic[0])
            i += arabic[1]
            continue
        w += joined()
        c = raw[i]
        if raw[i:i+4] in gt.PACK_EXTRA:      # a cache cell, cut as a coded letter: drawn width + 1
            m = gt.PACK_EXTRA[raw[i:i+4]].metric(metrics)
            w += (m >> 4) + 1 if m >> 4 and (m & 15) < 12 else 12
            page = 0
            i += 4
            continue
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
        if glyph in gt.PACK_LETTERS:
            m = gt.PACK_LETTERS[glyph].metric(metrics)
        if glyph in gt.PACK_DRAWN:
            m = gt.PACK_DRAWN[glyph].metric(metrics)
        w += ((metrics[glyph % 0x1B9] if m is None else m) >> 4) + extra
    return w + joined()


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
    raw = strip_narrow(raw)[0]
    for c in found:
        where = f"{c['id']} ({c['source']})"
        if c.get("same_length") and len(raw) != english_len:
            errors.append(f"{len(raw)} bytes; {where} reads exactly {english_len} glyph cells")
        if c.get("glyphs_only") and (b"\xf1\xc5" in raw or b"\xf1\xc7" in raw or any(b >= 0xFA for b in raw)):
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


def check_combined(texts: dict[str, bytes], metrics: bytes, layouts: dict, over: set | None = None):
    """Pack-wide limits on text the game joins: the widest/longest of each part together.
    texts: every resident key's bytes as the pack would have it (translation or English), or for
    an English entry known only from the source manifest, its (length, menu px). `over` collects
    the keys of the longest parts of whatever doesn't fit."""
    def n_bytes(v):
        return v[0] if isinstance(v, tuple) else len(strip_narrow(v)[0])

    def n_px(v):
        return v[1] if isinstance(v, tuple) else width(v, metrics)

    from .stretch import LINE_BREAK
    errors = []
    for c in layouts.get("combined", []):
        worst_px, worst_len, names, worst, line_px = 0, 0, [], set(), 0
        for alt in c["_parts"]:
            keys = [k for k in texts if any(m(k) for m in alt)]
            if not keys:
                break
            by_len = max(keys, key=lambda k: n_bytes(texts[k]))
            by_px = max(keys, key=lambda k: n_px(texts[k]))
            worst_len += n_bytes(texts[by_len])
            line_px += n_px(texts[by_px])
            worst_px = max(worst_px, line_px)
            if c.get("line_break") and all(isinstance(texts[k], bytes) and texts[k].endswith(LINE_BREAK) for k in keys):
                line_px = 0                  # each of them ends its line: the next part starts anew
            names.append(by_len if by_len == by_px else f"{by_len}/{by_px}")
            worst |= {by_len, by_px}
        else:
            where = f"{c['id']} ({c['source']})"
            if over is not None and (("bytes" in c and worst_len + 1 > c["bytes"]) or ("edge" in c and worst_px > c["edge"])):
                over |= worst
            if "bytes" in c and worst_len + 1 > c["bytes"]:
                errors.append(f"{' + '.join(names)}: {worst_len + 1} bytes with the end; {where} holds {c['bytes']}")
            if "edge" in c and worst_px > c["edge"]:
                errors.append(f"{' + '.join(names)}: {worst_px} px; {where} reaches the screen edge at {c['edge']} px")
    return errors


def room(key: str, layouts: dict) -> int | None:
    """The fewest pixels any place that draws `key` gives it, or None when none is known."""
    rooms = [c["edge"] if "edge" in c else c["width"] for c in contexts_of(key, layouts)
             if "edge" in c or c.get("width_is_hard")]
    return min(rooms) if rooms else None


def splits(key: str, layouts: dict) -> bool:
    """Whether every place that draws `key` takes a line break as a new line (its "line_break")."""
    found = contexts_of(key, layouts)
    return bool(found) and all(c.get("line_break") for c in found)


def _parts(raw: bytes):
    """Offsets of each letter or command in a menu line (tool bytes)."""
    i = 0
    while i < len(raw):
        yield i
        i += 4 if raw[i] == 0xF1 else 2 if 0xFA <= raw[i] < 0xFF else 1


def split(raw: bytes, metrics: bytes, space: int) -> bytes | None:
    """`raw` on two lines (a line break at the space that leaves the wider line narrowest), each
    within `space` px, or None when no space does that."""
    from .stretch import LINE_BREAK
    raw = strip_narrow(raw)[0]
    if LINE_BREAK in raw:
        return None
    best = None
    for i in _parts(raw):
        if raw[i] != gt.CODE_OF[" "]:
            continue
        w = max(width(raw[:i], metrics), width(raw[i + 1:], metrics))
        if w <= space and (best is None or w < best[0]):
            best = (w, i)
    return None if best is None else raw[:best[1]] + LINE_BREAK + raw[best[1] + 1:]


def split_joined(texts: dict, translated, metrics: bytes, layouts: dict) -> dict[str, bytes]:
    """For joined text past its room whose layout takes line breaks ("line_break"): every
    translated key of each part but the last ends with a line break, so the next part starts a new
    line. {key: new bytes} when that makes it fit, else {}."""
    from .stretch import LINE_BREAK
    out = {}
    for c in layouts.get("combined", []):
        if not c.get("line_break") or not check_combined(texts, metrics, {"combined": [c]}):
            continue
        keys = [k for alt in c["_parts"][:-1] for k in texts if any(m(k) for m in alt)]
        if not keys or any(k not in translated or not isinstance(texts[k], bytes) or
                           texts[k].endswith(LINE_BREAK) or not splits(k, layouts) for k in keys):
            continue
        trial = dict(texts, **{k: texts[k] + LINE_BREAK for k in keys})
        if not check_combined(trial, metrics, {"combined": [c]}):
            out.update({k: trial[k] for k in keys})
    return out


def fit(px: int, space: int) -> int | None:
    """The widest n / 256 (NARROW_FLOOR..255) that draws a `px` line within `space`, or None."""
    for n in range(255, NARROW_FLOOR - 1, -1):
        if narrowed(px, n) <= space:
            return n
    return None
