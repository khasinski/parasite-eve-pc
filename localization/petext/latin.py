"""The Latin Extended font set: accented letters and the signs European languages need, made
from the game's own letters at run time (an "e" plus an acute is "é").

The US dialogue font (PE.IMG entry 10, see petext/font.py) uses cells 0-76; the rest of its page
is empty. pe_lang writes these glyphs into free cells from code 0x4D on, when the font is uploaded
(PE_HOOK_TIM_UPLOAD), composing each from its base letter in the game's own font and a mark drawn
here. Only the marks and a few whole signs (ß, æ, «, €) are authored here; no game art is copied
into the repo or into a pack.

This module is the one definition: pelang.py encodes with it, preview_latin.py draws it from a
disc, and `python -I pelang.py latin-header` writes mods/pe_lang/pe_lang_latin.h, the table the mod
composes from (tests check the two agree).

Glyph pixels: 0 empty, 1 stroke, 2 shadow; the game draws a shadow one pixel right and below each
stroke pixel. Capitals stand on rows 1-8, lowercase on rows 3-8 (shadow one row lower).
"""
from __future__ import annotations

from dataclasses import dataclass, field

FIRST_CODE = 0x4D
CELL = 12

# Marks: stroke pixels as (dx, row). dx is from the base letter's ink centre (or its left stem,
# or its right edge, per ANCHOR). Marks above use rows 0-1 (placed at row 1 over lowercase, row 0
# over capitals), marks below the baseline use rows 9-10.
MARKS: dict[str, list[tuple[int, int]]] = {
    "acute": [(1, 0), (0, 1)],
    "grave": [(-1, 0), (0, 1)],
    "circumflex": [(0, 0), (-1, 1), (1, 1)],
    "caron": [(-1, 0), (1, 0), (0, 1)],
    "diaeresis": [(-1, 1), (1, 1)],
    "tilde": [(-1, 0), (0, 0), (2, 0), (-2, 1), (0, 1), (1, 1)],
    "ring": [(0, 0), (1, 0), (0, 1), (1, 1)],
    "dot": [(0, 1)],
    "double_acute": [(0, 0), (2, 0), (-1, 1), (1, 1)],
    "breve": [(-2, 0), (1, 0), (-1, 1), (0, 1)],
    "cedilla": [(0, 9), (-1, 10)],
    "ogonek": [(0, 9), (1, 10)],
    "stroke": [(-2, 6), (-1, 6), (1, 5), (2, 4)],
    "apostrophe": [(1, 1), (1, 2)],
}
BELOW = {"cedilla", "ogonek"}
FIXED_ROWS = BELOW | {"stroke", "apostrophe"}      # rows are absolute, not above the letter
# Where dx counts from: the ink centre unless listed; "stem" is the column with the most ink.
ANCHOR = {"stroke": "stem", "ogonek": "right", "apostrophe": "right"}
ANCHORS = ["centre", "left", "right", "stem"]

# Whole signs drawn here ('#' = stroke; the shadow is added). Rows 0-11, columns 0-11.
ART: dict[str, list[str]] = {
    "ß": ["", " ##", "#  #", "#  #", "# #", "#  #", "#   #", "#   #", "# ##"],
    "æ": ["", "", "", " ## ##", "   #  #", " ######", "#  #", "#  #  #", " ## ##"],
    "Æ": ["", "  #####", " # #", " # #", "#  ####", "####", "#  #", "#  #", "#  ####"],
    "œ": ["", "", "", " ## ##", "#  #  #", "#  ####", "#  #", "#  #  #", " ## ##"],
    "Œ": ["", " ######", "#  #", "#  #", "#  ####", "#  #", "#  #", "#  #", " ######"],
    "«": ["", "", "", "", " # #", "# #", " # #"],
    "»": ["", "", "", "", "# #", " # #", "# #"],
    "€": ["", "", "  ###", " #", "####", " #", "####", " #", "  ###"],
    "°": ["", " #", "# #", " #"],
}


@dataclass
class Glyph:
    char: str
    code: int
    base: str = ""                  # the game letter it is made from ("" for ART)
    mark: str = ""                  # a MARKS name, "slash", or ""
    ops: tuple[str, ...] = field(default_factory=tuple)   # "dotless", "rotate"


def _glyphs() -> list[Glyph]:
    spec: list[tuple[str, str, str, tuple[str, ...]]] = []

    def letters(marked: str, bases: str, mark: str, ops: tuple[str, ...] = ()):
        for c, b in zip(marked, bases):
            spec.append((c, b, mark, ops))

    for case in (str.lower, str.upper):
        letters(case("àèìòù"), case("aeiou"), "grave")
        letters(case("áéíóúý"), case("aeiouy"), "acute")
        letters(case("âêîôû"), case("aeiou"), "circumflex")
        letters(case("äëïöü"), case("aeiou"), "diaeresis")
        letters(case("ãõñ"), case("aon"), "tilde")
        letters(case("åů"), case("au"), "ring")
        letters(case("çş"), case("cs"), "cedilla")
        letters(case("ąę"), case("ae"), "ogonek")
        letters(case("ćńśź"), case("cnsz"), "acute")
        letters(case("ż"), case("z"), "dot")
        letters(case("čěňřšž"), case("cenrsz"), "caron")
        letters(case("ğ"), case("g"), "breve")
        letters(case("őű"), case("ou"), "double_acute")
        letters(case("ł"), case("l"), "stroke")
        letters(case("ø"), case("o"), "slash")
    # Lowercase i takes its marks without its dot; ÿ, ď, ť and İ have their own shapes.
    spec = [(c, b, m, ("dotless",) if b == "i" else o) for c, b, m, o in spec]
    spec += [("ÿ", "y", "diaeresis", ()), ("Ÿ", "Y", "diaeresis", ()),
             ("ď", "d", "apostrophe", ()), ("ť", "t", "apostrophe", ()),
             ("Ď", "D", "caron", ()), ("Ť", "T", "caron", ()),
             ("ı", "i", "", ("dotless",)), ("İ", "I", "dot", ()),
             ("¿", "?", "", ("rotate",)), ("¡", "!", "", ("rotate",))]
    out = [Glyph(c, FIRST_CODE + i, b, m, o) for i, (c, b, m, o) in enumerate(spec)]
    out += [Glyph(c, FIRST_CODE + len(out) + i) for i, c in enumerate(ART)]
    return out


GLYPHS = _glyphs()
CODE_OF = {g.char: g.code for g in GLYPHS}
BY_CODE = {g.code: g for g in GLYPHS}
assert len({g.char for g in GLYPHS}) == len(GLYPHS)
assert GLYPHS[-1].code <= 0xF0, "the Latin set must stay in single-byte codes 0x4D-0xF0"


def _ink(cell: list[int]) -> tuple[int, int, int, int] | None:
    pts = [(i % CELL, i // CELL) for i, v in enumerate(cell) if v == 1]
    if not pts:
        return None
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return min(xs), max(xs), min(ys), max(ys)


def _stroke_only(cell: list[int]) -> list[int]:
    return [1 if v == 1 else 0 for v in cell]


def _shadow(strokes: list[int]) -> list[int]:
    """The game's look: each stroke pixel casts a shadow one pixel right and below."""
    out = list(strokes)
    for i, v in enumerate(strokes):
        x, y = i % CELL, i // CELL
        if v == 1 and x + 1 < CELL and y + 1 < CELL and out[(y + 1) * CELL + x + 1] == 0:
            out[(y + 1) * CELL + x + 1] = 2
    return out


def _squash(strokes: list[int]) -> list[int]:
    """Make room above a capital: drop one middle row that repeats the row under it (a straight
    stem), moving the rows above it down, so the baseline stays where it is."""
    rows = [strokes[y * CELL:(y + 1) * CELL] for y in range(CELL)]
    drop = next((y for y in (5, 4, 6, 3, 7, 2) if rows[y] == rows[y + 1]), 5)
    rows = [[0] * CELL] + rows[:drop] + rows[drop + 1:]
    return [v for r in rows for v in r]


def compose(g: Glyph, base: list[int] | None) -> list[int]:
    """The glyph's 144 pixels, from its base letter's cell in the game font (None for ART)."""
    if not g.base:
        strokes = [0] * (CELL * CELL)
        for y, row in enumerate(ART[g.char]):
            for x, ch in enumerate(row):
                if ch == "#":
                    strokes[y * CELL + x + 1] = 1
        return _shadow(strokes)
    strokes = _stroke_only(base)
    if "dotless" in g.ops:
        strokes = [0 if i < 3 * CELL else v for i, v in enumerate(strokes)]
    if "rotate" in g.ops:
        x0, x1, y0, y1 = _ink(strokes)
        turned = [0] * (CELL * CELL)
        for i, v in enumerate(strokes):
            if v:
                x, y = i % CELL, i // CELL
                turned[(y0 + y1 - y) * CELL + (x0 + x1 - x)] = 1
        strokes = turned
    capital = g.base.isupper()
    if g.mark and g.mark not in BELOW and capital and g.mark not in ("stroke", "slash"):
        strokes = _squash(strokes)
    box = _ink(strokes)
    if g.mark == "slash" and box:
        x0, x1, y0, y1 = box
        span = max(1, y1 - y0)
        for k in range(y1 - y0 + 1):
            x = x0 + (2 * k * (x1 - x0) + span) // (2 * span)      # rounded half up, as in C
            strokes[(y1 - k) * CELL + x] = 1
    elif g.mark and box:
        x0, x1, y0, y1 = box
        kind = ANCHOR.get(g.mark, "centre")
        if kind == "stem":
            counts = [sum(strokes[y * CELL + x] for y in range(CELL)) for x in range(CELL)]
            anchor = counts.index(max(counts))
        else:
            anchor = {"left": x0, "right": x1}.get(kind, (x0 + x1) // 2)
        top = 0 if capital else 1
        for dx, row in MARKS[g.mark]:
            y = row if g.mark in FIXED_ROWS else top + row
            x = anchor + dx
            if 0 <= x < CELL and 0 <= y < CELL:
                strokes[y * CELL + x] = 1
    return _shadow(strokes)


def advance(g: Glyph, base_advance: int | None) -> int:
    """The dialogue box's advance: the base letter's, or for drawn signs their ink width + 2
    (as the game's own letters measure)."""
    if g.base:
        return base_advance or 7
    _, x1, _, _ = _ink(compose(g, None))
    return x1 + 2


def synthetic_cell(code: int) -> list[int]:
    """A stand-in game letter for tests (no game art): a stem, a bar and a right side, capitals on
    rows 1-8 and the rest on rows 3-8, with a dot over code 0x38 (i), all with the game's shadow."""
    strokes = [0] * (CELL * CELL)
    top = 1 if 0x10 <= code <= 0x29 else 3
    stem, right = 1 + code % 3, 5 + code % 2
    for y in range(top, 9):
        strokes[y * CELL + stem] = 1
        if (y + code) % 2:
            strokes[y * CELL + right] = 1
    for x in range(stem, right + 1):
        strokes[((top + 8) // 2) * CELL + x] = 1
    if code == 0x38:
        strokes[1 * CELL + stem] = 1
    return _shadow(strokes)


def write_test_vectors(path: str) -> None:
    """tests/pe_lang_font_vectors.h: every glyph composed from synthetic_cell bases, which the C
    composer must reproduce pixel for pixel."""
    from . import gametext as gt
    lines = ["/* Generated by `python -I localization/pelang.py latin-header` from localization/petext/latin.py:",
             " * each Latin Extended glyph composed from the synthetic test letters (latin.synthetic_cell),",
             " * 144 pixels as digits (0 empty, 1 stroke, 2 shadow); don't edit. */",
             "static const char *const kLatinVectors[] = {"]
    for g in GLYPHS:
        cell = compose(g, synthetic_cell(gt.CODE_OF[g.base]) if g.base else None)
        lines.append(f'    "{"".join(map(str, cell))}", /* U+{ord(g.char):04X} */')
    lines += ["};", ""]
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


def write_c_header(path: str) -> None:
    """mods/pe_lang/pe_lang_latin.h: the same set as data the mod composes from."""
    marks = sorted(MARKS) + ["slash"]
    mark_id = {m: i + 1 for i, m in enumerate(marks)}
    lines = ["/* Generated by `python -I localization/pelang.py latin-header` from localization/petext/latin.py;",
             " * don't edit. */",
             "#ifndef PE_LANG_LATIN_H", "#define PE_LANG_LATIN_H", "",
             "#define PE_LATIN_FIRST_CODE 0x%02X" % FIRST_CODE,
             "#define PE_LATIN_COUNT %d" % len(GLYPHS),
             "#define PE_LATIN_OP_DOTLESS 1", "#define PE_LATIN_OP_ROTATE 2", "",
             "enum { PE_MARK_NONE, %s };" % ", ".join(f"PE_MARK_{m.upper()}" for m in marks), "",
             "typedef struct PeLatinPoint { signed char dx, row; } PeLatinPoint;",
             "typedef struct PeLatinMark { unsigned char count, fixed_rows, anchor; PeLatinPoint pts[8]; } PeLatinMark;",
             "/* anchor: dx counts from 0 the ink centre, 1 its left edge, 2 its right edge, 3 the column",
             " * with the most ink. fixed_rows: rows are absolute (else above the letter: from row 0 over",
             " * a capital, row 1 over lowercase). */",
             "static const PeLatinMark kPeLatinMarks[] = {", "    {0, 0, 0, {{0, 0}}},"]
    for m in marks:
        pts = MARKS.get(m, [])
        anchor = ANCHORS.index(ANCHOR.get(m, "centre"))
        body = ", ".join(f"{{{dx}, {row}}}" for dx, row in pts) or "{0, 0}"
        lines.append(f"    {{{len(pts)}, {int(m in FIXED_ROWS)}, {anchor}, {{{body}}}}}, /* {m} */")
    lines += ["};", "",
              "/* base: the US glyph code it is made from (0xFF: drawn, art row bits in kPeLatinArt). */",
              "typedef struct PeLatinGlyph { unsigned char code, base, mark, ops; } PeLatinGlyph;",
              "static const PeLatinGlyph kPeLatinGlyphs[PE_LATIN_COUNT] = {"]
    from . import gametext as gt
    art_index = {c: i for i, c in enumerate(ART)}
    for g in GLYPHS:
        ops = (1 if "dotless" in g.ops else 0) | (2 if "rotate" in g.ops else 0)
        base = f"0x{gt.CODE_OF[g.base]:02X}" if g.base else "0xFF"
        mark = f"PE_MARK_{g.mark.upper()}" if g.mark else ("PE_MARK_NONE" if g.base else str(art_index[g.char]))
        lines.append(f"    {{0x{g.code:02X}, {base}, {mark}, {ops}}}, /* U+{ord(g.char):04X} */")
    lines += ["};", "", "/* Drawn signs: 12 rows of stroke bits (bit x = column x + 1), in kPeLatinGlyphs order. */",
              "static const unsigned short kPeLatinArt[%d][12] = {" % len(ART)]
    for c, rows in ART.items():
        bits = [sum(1 << x for x, ch in enumerate(r) if ch == "#") for r in rows] + [0] * (12 - len(rows))
        lines.append("    {" + ", ".join(f"0x{b:03X}" for b in bits) + f"}}, /* U+{ord(c):04X} */")
    lines += ["};", "", "#endif", ""]
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


