"""Japanese text on the Japanese discs, decoded to Unicode with the glyph table
(data/ja_glyphs.tsv, built by build_glyph_table.py).

A glyph's character depends on which font image it is drawn from: page 0 is shared, page 1 is
the area's own (PE.IMG entry roomType + 0x2B, see petext/font.py), so decoding a room's text
needs its room type. Commands decode to the same tags as petext/gametext.py ({br}, {wait},
{name}, {choice N}, {red} ...); a glyph missing from the table becomes {?slot:cell}.

Look-alike pairs (エ/工, ニ/二, ー/一, カ/力, ベ/べ ...) can't be told apart by their pixels at
12 px; each occurrence is decided by its neighbours (katakana beside katakana, the long-vowel mark
after kana).
"""
from __future__ import annotations

import csv
import struct
from pathlib import Path

from . import font, gametext as gt

TABLE = Path(__file__).resolve().parent.parent / "data" / "ja_glyphs.tsv"
FB_COMMANDS = set(range(10))      # FB 00..09 are box commands; FB 0A.. draw glyphs
FB_TAGS = {4: "{red}", 5: "{/red}", 6: "{pause}", 8: "{num}"}

# (kanji or hiragana form, katakana form): the katakana form wins next to katakana.
LOOKALIKES = [("工", "エ"), ("二", "ニ"), ("力", "カ"), ("口", "ロ"), ("夕", "タ"), ("卜", "ト"),
              ("八", "ハ"), ("べ", "ベ"), ("ぺ", "ペ"), ("へ", "ヘ"), ("一", "ー")]
PAIR = {c: p for p in LOOKALIKES for c in p}


def is_katakana(ch: str) -> bool:
    return "ァ" <= ch <= "ヺ" or ch == "ー"


def is_kana(ch: str) -> bool:
    return "ぁ" <= ch <= "ゖ" or is_katakana(ch)


def room_type(data: bytes) -> int:
    return data[struct.unpack_from("<I", data, 4)[0] + 3]      # SceneRoomDirectory.roomType


def glyph_tokens(raw: bytes):
    """(prefix or None, code) for every glyph a page draws; commands are skipped."""
    for kind, a, b in tokens(raw):
        if kind == "glyph":
            yield a, b


def tokens(raw: bytes):
    """("glyph", prefix or None, code) or ("tag", text, None) for each element of a page."""
    i = 0
    while i < len(raw):
        c = raw[i]
        if c in (0xFC, 0xFD) and i + 1 < len(raw):
            yield "glyph", c, raw[i + 1]; i += 2; continue
        if c == 0xFB and i + 1 < len(raw):
            s = raw[i + 1]
            if s == 9 and i + 2 < len(raw):
                yield "tag", f"{{choice {raw[i + 2]}}}", None; i += 3; continue
            if s == 7 and i + 2 < len(raw):
                yield "tag", f"{{fb07 {raw[i + 2]:02X}}}", None; i += 3; continue
            if s in FB_TAGS:
                yield "tag", FB_TAGS[s], None
            elif s <= 3:
                yield "tag", f"{{sym {s}}}", None
            elif s in FB_COMMANDS:
                yield "tag", f"{{fb {s:02X}}}", None
            else:
                yield "glyph", 0xFB, s
            i += 2; continue
        if c == 0xF7:
            yield "tag", "{br}", None
        elif c == 0xF8:
            yield "tag", "{wait}", None
        elif c == 0xF9:
            yield "tag", "{end2}", None
        elif c == 0xFA:
            yield "tag", "{name}", None
        elif c < 0xF7:            # the Japanese text uses codes up to F6 as glyphs (F5 is ン)
            yield "glyph", None, c
        else:
            yield "tag", f"{{x{c:02X}}}", None
        i += 1


# Page 0's first rows hold the same Latin letters, digits and signs, in the same cells, as the US
# font, so they are known exactly (OCR mixes up 0/o, 1/l/I).
LATIN = dict(gt.CHARS)
LATIN.update({0x0C: "=", 0x0D: "*"})


class GlyphTable:
    def __init__(self, path=TABLE):
        self.chars: dict[str, str] = {}
        with open(path, encoding="utf-8") as f:
            for r in csv.DictReader(f, delimiter="\t"):
                ch = r["fixed"] or r["char"]
                for slot in r["slots"].split():
                    self.chars[slot] = ch
        for cell, ch in LATIN.items():
            self.chars[f"p0:{cell}"] = ch

    def decode(self, raw: bytes, rtype: int | None) -> str:
        """A page's text. rtype is the room type whose page-1 font the page is drawn with (None
        for text that only uses page 0, such as the resident tables)."""
        parts = []
        for kind, a, b in tokens(raw):
            if kind == "tag":
                parts.append(a)
                continue
            pg, cell = font.glyph_cell(a, b)
            slot = f"p0:{cell}" if pg == 0 else (f"e{rtype + 0x2B}:{cell}" if rtype is not None else f"e?:{cell}")
            parts.append(self.chars.get(slot) or f"{{?{slot}}}")
        return settle(parts)


def settle(parts: list[str]) -> str:
    """Pick each look-alike's form from its neighbours (tags are skipped over)."""
    chars = [(i, p) for i, p in enumerate(parts) if len(p) == 1]
    for n, (i, ch) in enumerate(chars):
        if ch not in PAIR:
            continue
        plain, kata = PAIR[ch]
        prev = chars[n - 1][1] if n and chars[n - 1][0] == i - 1 else ""
        nxt = chars[n + 1][1] if n + 1 < len(chars) and chars[n + 1][0] == i + 1 else ""
        if kata == "ー":
            # the long-vowel mark follows a kana; the kanji 一 doesn't need to
            parts[i] = "ー" if prev and is_kana(prev) else "一"
        elif (prev and is_katakana(prev)) or (nxt and is_katakana(nxt)):
            parts[i] = kata
        else:
            parts[i] = plain
    return "".join(parts)
