"""The game's text code (the native port's src/main/draw/textbox.c, mods/host/pe_gametext.h):
decode to readable text with tags, encode back to the same bytes, measure a line in pixels.

Every byte is kept: codes without a known meaning become {gNN} (a glyph) or {xNN} (another byte),
so encode(decode(b)) == b for any input. Tags:
  {br}        F7 line break             {wait}      F8 wait for a button, next screen
  {end2}      F9 end (closes the box)   {name}      FA the inserted name
  {red} {/red}  FB 04 / FB 05            {pause}     FB 06
  {num}       FB 08 a number slot        {choice N}  FB 09 N, a choice of N options
  {sym N}     FB 00..03                  {fb XX}     another FB command, {fb07 XX} with its operand
  {fc XX} {fd XX}  a glyph from the extra font pages (the Japanese text uses these)
A page's own end (FF) is not part of its text.
"""
from __future__ import annotations

import re

# Glyph codes the US text uses; the rest stay {gNN}.
CHARS = {i: str(i) for i in range(10)}
CHARS.update({0x0A: "+", 0x0B: "-", 0x0E: "%", 0x0F: " ", 0x2A: "&", 0x2B: "!", 0x2C: "?", 0x2D: '"',
              0x2E: "'", 0x2F: ".", 0x4A: ":", 0x4B: ",", 0x4C: "/"})
CHARS.update({0x10 + i: chr(65 + i) for i in range(26)})
CHARS.update({0x30 + i: chr(97 + i) for i in range(26)})
CODE_OF = {v: k for k, v in CHARS.items()}

# g_TextboxGlyphSpacing (src/main/text/tables.c): the advance of glyph codes 0..77 in a box.
ADVANCE = [7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 6, 6, 8, 11, 14, 9, 8, 7, 8, 7, 7, 8, 9, 6, 7, 9, 8, 11, 9, 7, 8, 7,
           9, 7, 7, 9, 9, 11, 7, 9, 7, 9, 4, 7, 6, 4, 4, 8, 8, 7, 7, 7, 7, 8, 9, 5, 4, 8, 5, 11, 9, 7, 8,
           8, 7, 6, 5, 9, 9, 11, 7, 9, 6, 4, 4, 7, 0]
# What the English box moves its pen: with the English text flag the game sets
# g_TextboxKernGlyph0F, and the box pulls every space (code 0x0F) back 7 px (textbox.c).
BOX_ADVANCE = ADVANCE[:0x0F] + [ADVANCE[0x0F] - 7] + ADVANCE[0x10:]
WIDE = 12          # a glyph from the extra pages, a symbol, a digit of a number
LINE_HEIGHT = 12

FB_TAGS = {4: "{red}", 5: "{/red}", 6: "{pause}", 8: "{num}"}


def decode(b: bytes) -> str:
    out, i = [], 0
    while i < len(b):
        c = b[i]
        if c in CHARS:
            out.append(CHARS[c])
        elif c == 0xF7:
            out.append("{br}")
        elif c == 0xF8:
            out.append("{wait}")
        elif c == 0xF9:
            out.append("{end2}")
        elif c == 0xFA:
            out.append("{name}")
        elif c == 0xFB and i + 1 < len(b):
            sub = b[i + 1]
            if sub in FB_TAGS:
                out.append(FB_TAGS[sub]); i += 1
            elif sub == 9 and i + 2 < len(b):
                out.append(f"{{choice {b[i + 2]}}}"); i += 2
            elif sub == 7 and i + 2 < len(b):
                out.append(f"{{fb07 {b[i + 2]:02X}}}"); i += 2
            elif sub <= 3:
                out.append(f"{{sym {sub}}}"); i += 1
            else:
                out.append(f"{{fb {sub:02X}}}"); i += 1
        elif c in (0xFC, 0xFD) and i + 1 < len(b):
            out.append(f"{{{'fc' if c == 0xFC else 'fd'} {b[i + 1]:02X}}}"); i += 1
        elif c < 0xF1:
            out.append(f"{{g{c:02X}}}")
        else:
            out.append(f"{{x{c:02X}}}")
        i += 1
    return "".join(out)


_TAG = re.compile(r"\{([gx][0-9A-F]{2}|[a-z/]+[0-9]*)(?: ([0-9A-Fa-f]+))?\}")


def encode(text: str, extended: bool = False, cjk_codes=None) -> bytes:
    """`text` in the game's glyph code. With `extended`, letters of the Latin Extended set
    (petext/latin.py: é, ñ, ß, « ...) and Cyrillic (petext/cyrillic.py) are allowed too;
    pe_lang adds their glyphs to the font."""
    extra = _latin_codes() if extended else {}
    out, i = bytearray(), 0
    while i < len(text):
        if text[i] == "{":
            m = _TAG.match(text, i)
            if not m:
                raise ValueError(f"bad tag at {i}: {text[i:i + 12]!r}")
            tag, arg = m.group(1), m.group(2)
            simple = {"br": [0xF7], "wait": [0xF8], "end2": [0xF9], "name": [0xFA], "red": [0xFB, 4],
                      "/red": [0xFB, 5], "pause": [0xFB, 6], "num": [0xFB, 8]}
            if tag in simple:
                out += bytes(simple[tag])
            elif tag == "choice":
                out += bytes([0xFB, 9, int(arg)])
            elif tag == "sym":
                out += bytes([0xFB, int(arg)])
            elif tag == "fb07":
                out += bytes([0xFB, 7, int(arg, 16)])
            elif tag == "fb":
                out += bytes([0xFB, int(arg, 16)])
            elif tag in ("fc", "fd"):
                out += bytes([0xFC if tag == "fc" else 0xFD, int(arg, 16)])
            elif tag[0] in "gx" and len(tag) == 3:
                out.append(int(tag[1:], 16))
            else:
                raise ValueError(f"unknown tag {{{tag}}}")
            i = m.end()
        else:
            ch = text[i]
            if ch in CODE_OF:
                out.append(CODE_OF[ch])
            elif ch in extra:
                out.append(extra[ch])
            elif cjk_codes and ch in cjk_codes:
                out += cjk_codes[ch]
            else:
                raise ValueError(f"no glyph for {ch!r}")
            i += 1
    return bytes(out)


def _latin_codes() -> dict[str, int]:
    from . import latin
    from . import cyrillic
    return {**latin.CODE_OF, **cyrillic.CODE_OF}


def _latin_advance(code: int) -> int | None:
    from . import latin
    from . import cyrillic
    if code in cyrillic.BY_CODE:
        return cyrillic.BY_CODE[code].advance
    g = latin.BY_CODE.get(code)
    if not g:
        return None
    return latin.advance(g, ADVANCE[CODE_OF[g.base]] if g.base else None)


def lines(b: bytes) -> list[int]:
    """The pixel width of each line of a page, as the box draws it (screens split at {wait})."""
    widths, w, i = [], 0, 0
    while i < len(b):
        c = b[i]
        if c in (0xF7, 0xF8):
            widths.append(w); w = 0
        elif c == 0xFA:
            w += ADVANCE[0x10] + ADVANCE[0x48] + ADVANCE[0x30]  # "Aya", the default name (petext/layout.py)
        elif c == 0xFB and i + 1 < len(b):
            sub = b[i + 1]
            if sub <= 3:
                w += WIDE
            elif sub == 8:
                w += WIDE * 5      # TextboxNumber has up to five rendered digits
            i += 1 + (1 if sub in (7, 9) else 0)
        elif c == 0xF1 and b[i:i+2] == b"\xf1\xc5":
            w += WIDE; i += 3
        elif c in (0xFC, 0xFD):
            w += WIDE; i += 1
        elif c < len(BOX_ADVANCE):
            w += BOX_ADVANCE[c]
        elif c < 0xF1:
            w += _latin_advance(c) or WIDE
        i += 1
    widths.append(w)
    return widths


def pages(bank: bytes) -> tuple[dict[int, tuple[bytes, int]], int]:
    """A message bank's pages: FE <id> text FF|F9, FE <id+1> ... until a page end not followed by
    FE. Returns (id -> (page bytes without its end, the end byte), bytes used)."""
    out, i = {}, 0
    if len(bank) < 2 or bank[0] != 0xFE:
        return out, 0
    while i + 1 < len(bank) and bank[i] == 0xFE:
        pid, j = bank[i + 1], i + 2
        while j < len(bank) and bank[j] not in (0xFF, 0xF9):
            step = 1
            if bank[j] in (0xFC, 0xFD):
                step = 2
            elif bank[j] == 0xFB and j + 1 < len(bank):
                step = 3 if bank[j + 1] in (7, 9) else 2
            j += step
        if j >= len(bank):
            break
        out[pid] = (bank[i + 2:j], bank[j])
        i = j + 1
    return out, i
