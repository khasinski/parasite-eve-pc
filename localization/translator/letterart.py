"""Letters the game can't draw yet, saved out as a PNG, drawn in any paint program and loaded back.

Some languages have letters nobody has drawn for the game (Greek, Hebrew, Arabic, a few Cyrillic and
Icelandic letters: petext/letters.py PLACEHOLDER_LETTERS); the game shows a question mark for each.
A translator saves the letters their language still needs as one PNG, paints them, and loads it
back. A loaded letter goes into the translation as a picture of its own, the way a fan patch's
letters come in (pelang.py build --patch-letters): a record of the pack's letter table with its 144
pixels, which pe_lang draws as it is (mods/pe_lang/pe_lang_font.c, pe_lang_letter_compose).

The PNG, at real size (one dot of the game is one pixel):
  - a few lines saying what to do, and with a disc chosen a strip of the game's own letters as a
    style guide (read from the translator's disc; never kept anywhere else);
  - one row of squares after another. Each letter has a 12 x 12 square (black, framed in grey)
    with the letter beside it, large, in a computer font. Faint lines in the square mark the
    capital top (row 1), the small-letter top (row 3) and the bottom line (row 8), where the
    game's own letters sit.
  - Arabic: one row per letter, its shapes side by side (alone, end, middle, start), each with a
    short orange mark outside the square, on the bottom line, on the side(s) it joins on. The
    game cuts a joined shape to its painted dots and sets the next one touching it
    (pe_lang_letter_cut, PeText_LineFinish), so a joining stroke must reach that side.
Reading it back, a dot is painted where it is bright (any colour, its brightest part at least 128,
not see-through); everything else (black, the faint lines) is empty. The tool adds the shadow (one
dot right and below each painted dot, as every letter of the game has: latin._shadow), and the
letter's width follows its rightmost painted dot (+2, as for every letter drawn this way). A square
left empty means the letter isn't drawn (the game keeps its question mark).

The PNG says which letters it holds (a text chunk), so one saved before more letters were needed
still loads; a paint program that drops the chunk is fine while the letters are the same.
"""
from __future__ import annotations

import io
import json
import sys
import unicodedata
from dataclasses import dataclass

from petext import alphabets, latin, letters as lettertable, rtl

CELL = 12
CAPITAL_TOP, SMALL_TOP, BOTTOM = 1, 3, 8             # rows of the game's letters (its A, x, the bottom line)
GUIDE_ROWS = (CAPITAL_TOP, SMALL_TOP, BOTTOM)
PER_ROW = 8                                          # squares in a row (an Arabic row is one letter's shapes)
MARGIN, LINE = 8, 13                                 # the picture's edge; a line of the notes
LABEL = 26                                           # the box of the large computer-font letter
GAP = 4                                              # between that box and the square's frame
ITEM_W, ROW_H = 58, 32
STRIP_H = 30                                         # "The game's own letters:" and the letters
CHUNK = "pe-translator-letters"                      # the PNG text chunk naming its letters

BACK = (44, 44, 44)                                  # the picture
SQUARE = (0, 0, 0)                                   # inside a square: empty
FRAME = (120, 120, 120)
GUIDE = (36, 52, 96)                                 # the faint lines: dark enough to read as empty
JOIN = (255, 150, 0)                                 # the side a shape joins on (outside the square)
INK = (255, 255, 255)                                # a painted dot, as the PNG is saved
TEXT = (230, 230, 230)
PAINTED = 128                                        # a dot is painted when its brightest part is this or more

STYLE_SAMPLE = "AaBbEeHhKkOoPpXxgy0?"                # the game's own letters shown as a style guide


@dataclass(frozen=True)
class Shape:
    """One square of the PNG: a letter, or one joined shape of an Arabic letter."""
    char: str
    joins: str = ""                  # "r", "l" or "rl": the side(s) it joins on (Arabic)
    what: str = ""                   # "alone", "end", "middle", "start" for an Arabic shape


def needs_drawing(ch: str) -> bool:
    """Whether the game shows `ch` as a question mark until someone draws it."""
    letter = lettertable.CATALOG.get(ch)
    return letter is not None and letter.art == lettertable.PLACEHOLDER


ARABIC_SHAPES = (("alone", ""), ("end", "r"), ("start", "l"), ("middle", "rl"))   # +0..+3 from the alone form


def _arabic_row(letter: int) -> list[Shape]:
    """A letter's shapes as the renderer picks them (rtl.join): alone, end, middle, start."""
    alone, n = rtl.FORMS[letter]
    shapes = [Shape(chr(alone + k), joins, what) for k, (what, joins) in enumerate(ARABIC_SHAPES[:n])]
    order = {"alone": 0, "end": 1, "middle": 2, "start": 3}
    return sorted(shapes, key=lambda s: order[s.what])


def wanted(lang: str, texts=()) -> list[list[Shape]]:
    """The rows of squares a translation into `lang` needs: every letter of the language the game
    can't draw yet (petext/alphabets.py), then any other such letter the translation's `texts` use.
    Arabic letters come as their shapes, a row each; lam-alef (one letter for lam then alef) and
    the tatweel get rows of their own when the language or the text has them."""
    language = alphabets.BY_CODE.get(lang)
    letters = alphabets.full_alphabet(language) if language else ""
    used = "".join(ch for text in texts for ch in text)
    order = list(dict.fromkeys(letters + "".join(sorted(set(used) - set(letters), key=ord))))
    plain: list[Shape] = []
    rows: list[list[Shape]] = []
    alefs = []
    for ch in order:
        c = ord(ch)
        if c in rtl.FORMS:
            row = [s for s in _arabic_row(c) if needs_drawing(s.char)]
            if row:
                rows.append(row)
            if c in rtl.LAM_ALEF:
                alefs.append(c)
        elif c == rtl.TATWEEL:
            if needs_drawing(ch):
                rows.append([Shape(ch, "rl", "joins both sides")])
        elif needs_drawing(ch):
            plain.append(Shape(ch))
    if alefs and chr(rtl.LAM) in order:
        lam_alef = [s for a in alefs for s in (Shape(chr(rtl.LAM_ALEF[a]), "", "alone"),
                                               Shape(chr(rtl.LAM_ALEF[a] + 1), "r", "end"))
                    if needs_drawing(s.char)]
        for k in range(0, len(lam_alef), PER_ROW):
            rows.append(lam_alef[k:k + PER_ROW])
    plain_rows = [plain[k:k + PER_ROW] for k in range(0, len(plain), PER_ROW)]
    return plain_rows + rows


def every_shape(rows: list[list[Shape]]) -> list[Shape]:
    return [s for row in rows for s in row]


# ---- a letter as the game gets it ------------------------------------------------------------------

def strokes_of(rows: tuple[str, ...]) -> list[int]:
    """12 rows of '.'/'#' (or a picture's ' #+') as 144 numbers, 1 where painted."""
    return [1 if v == "#" else 0 for row in rows for v in row]


def rows_of(strokes: list[int]) -> tuple[str, ...]:
    return tuple("".join("#" if strokes[y * CELL + x] else "." for x in range(CELL)) for y in range(CELL))


def picture(rows: tuple[str, ...]) -> tuple[str, ...]:
    """The letter as the pack holds it: its painted dots and the shadow the tool adds, as 12 rows
    of ' ' (empty), '#' (dot) and '+' (shadow)."""
    shadowed = latin._shadow(strokes_of(rows))
    return tuple("".join(" #+"[shadowed[y * CELL + x]] for x in range(CELL)) for y in range(CELL))


def advance(rows: tuple[str, ...]) -> int:
    """How far the pen moves after the letter in the dialogue box: its rightmost dot + 2 (the
    shadow and a gap), as for every letter drawn this way (Letter.advance)."""
    xs = [x for row in rows for x, v in enumerate(row) if v == "#"]
    return max(xs) + 2 if xs else 0


def as_rules(drawn: dict[str, tuple[str, ...]]) -> tuple:
    """Drawn letters as Rules.letters entries: (letter, picture rows, dialogue advance, menu metrics
    0 = from the advance, as pe_lang_letter_metric works it out)."""
    return tuple((ch, picture(rows), advance(rows), 0) for ch, rows in sorted(drawn.items()) if advance(rows))


def from_picture(rows: tuple[str, ...]) -> tuple[str, ...]:
    """A pack's picture (' #+') back to its painted dots, as the PNG shows them."""
    return tuple(row.replace("+", ".").replace(" ", ".") for row in rows)


# ---- the PNG ---------------------------------------------------------------------------------------

def _top(strip: bool, arabic: bool) -> int:
    return MARGIN + LINE * (4 if arabic else 3) + 6 + (STRIP_H if strip else 0)


def size(rows: list[list[Shape]], strip: bool) -> tuple[int, int]:
    arabic = any(s.joins for s in every_shape(rows))
    return MARGIN * 2 + PER_ROW * ITEM_W, _top(strip, arabic) + max(1, len(rows)) * ROW_H + MARGIN


def squares(rows: list[list[Shape]], strip: bool) -> dict[Shape, tuple[int, int]]:
    """Where each shape's 12 x 12 square starts in the PNG."""
    arabic = any(s.joins for s in every_shape(rows))
    top, out = _top(strip, arabic), {}
    for r, row in enumerate(rows):
        for k, shape in enumerate(row):
            x = MARGIN + k * ITEM_W + LABEL + GAP + 1
            y = top + r * ROW_H + (ROW_H - CELL) // 2
            out[shape] = (x, y)
    return out


FONT_FILES = {
    "win32": ["C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/tahoma.ttf",
              "C:/Windows/Fonts/times.ttf"],
    "darwin": ["/System/Library/Fonts/Supplemental/Arial Unicode.ttf", "/Library/Fonts/Arial Unicode.ttf",
               "/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/Supplemental/Tahoma.ttf",
               "/System/Library/Fonts/Supplemental/Times New Roman.ttf", "/System/Library/Fonts/GeezaPro.ttc",
               "/System/Library/Fonts/SFArabic.ttf", "/System/Library/Fonts/SFHebrew.ttf"],
    "linux": ["/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf",
              "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
              "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"],
}


def _label_fonts(px: int) -> list:
    """Computer fonts that may hold the letters (the large ones beside the squares)."""
    from PIL import ImageFont
    out = []
    for path in FONT_FILES.get(sys.platform, FONT_FILES["linux"]):
        try:
            out.append(ImageFont.truetype(path, px))
        except OSError:
            continue
    return out


def _has(font, ch: str) -> bool:
    """Whether the font draws `ch` itself rather than its 'no such letter' box."""
    try:
        return font.getmask(ch).getbbox() is not None and bytes(font.getmask(ch)) != bytes(font.getmask("\uFFFF"))
    except Exception:                                # a font that can't take the letter at all
        return False


def _notes_font():
    from PIL import ImageFont
    return ImageFont.load_default()


def png(rows: list[list[Shape]], language: str, drawn: dict[str, tuple[str, ...]],
        style: list[tuple[list[int], tuple]] | None = None) -> bytes:
    """The letters PNG: `drawn` (letter -> 12 rows) shows in its squares, the others are empty.
    `style`: the game's own letters (144 palette numbers, the font's colours) from the disc."""
    from PIL import Image, ImageDraw, PngImagePlugin
    strip = bool(style)
    arabic = any(s.joins for s in every_shape(rows))
    width, height = size(rows, strip)
    img = Image.new("RGB", (width, height), BACK)
    d = ImageDraw.Draw(img)
    small = _notes_font()
    notes = [f"{language} letters for Parasite Eve. Paint each letter white, dot by dot, in its square:",
             "12 x 12 dots at real size (zoom in to paint). The tool adds the dark shadow itself.",
             "Faint lines: capital top, small-letter top, bottom line. An empty square stays a '?'."]
    if arabic:
        notes.append("Orange marks: the side a shape joins on. Its stroke must reach that side on the bottom line.")
    for k, line in enumerate(notes):
        d.text((MARGIN, MARGIN + k * LINE), line, fill=TEXT, font=small)
    top = MARGIN + LINE * len(notes) + 6
    if strip:
        d.text((MARGIN, top + 2), "The game's own letters:", fill=TEXT, font=small)
        x = MARGIN + 140
        for pixels, colours in style:
            for i, v in enumerate(pixels):
                if v and v < len(colours) and colours[v]:
                    img.putpixel((x + i % CELL, top + 2 + i // CELL), colours[v])
            x += CELL
    fonts = _label_fonts(20)
    at = squares(rows, strip)
    for shape, (x, y) in at.items():
        d.rectangle((x - 1, y - 1, x + CELL, y + CELL), outline=FRAME, fill=SQUARE)
        for row in GUIDE_ROWS:
            d.line((x, y + row, x + CELL - 1, y + row), fill=GUIDE)
        for side in shape.joins:
            edge = x + CELL if side == "r" else x - 1
            out = 1 if side == "r" else -1
            for k in range(3):
                img.putpixel((edge + out * k, y + BOTTOM), JOIN)
        rows_drawn = drawn.get(shape.char)
        if rows_drawn:
            for yy, line in enumerate(rows_drawn):
                for xx, v in enumerate(line):
                    if v == "#":
                        img.putpixel((x + xx, y + yy), INK)
        box = (x - 1 - GAP - LABEL, y + CELL // 2 - LABEL // 2)
        font = next((f for f in fonts if _has(f, shape.char)), None)
        if font is not None:
            d.text((box[0] + LABEL // 2, box[1] + LABEL // 2), shape.char, fill=TEXT, font=font, anchor="mm")
        else:
            d.text((box[0], box[1] + 8), f"{ord(shape.char):04X}", fill=TEXT, font=small)
    info = PngImagePlugin.PngInfo()
    info.add_itxt(CHUNK, json.dumps({"rows": [[[s.char, s.joins, s.what] for s in row] for row in rows],
                                     "strip": strip}, ensure_ascii=False))
    out = io.BytesIO()
    img.save(out, "PNG", pnginfo=info, optimize=False)
    return out.getvalue()


@dataclass
class Loaded:
    drawn: dict[str, tuple[str, ...] | None]   # letter -> its painted dots, None for an empty square
    notes: list[str]


def _name(shape: Shape) -> str:
    return f"{shape.char} ({shape.what} shape)" if shape.what in ("alone", "end", "middle", "start") else shape.char


def from_png(data: bytes, rows: list[list[Shape]]) -> Loaded:
    """The letters painted in a letters PNG. `rows`: what the translation needs now (wanted); the
    PNG's own list (its text chunk) is used when it has one. Raises ValueError, in plain words,
    for a picture that isn't a letters PNG of this layout."""
    from PIL import Image
    try:
        img = Image.open(io.BytesIO(data))
        img.load()
    except Exception as e:
        raise ValueError(f"this file is not a picture the tool can read ({e})") from e
    saved = img.info.get(CHUNK)
    layouts = []
    if saved:
        try:
            told = json.loads(saved)
            layouts.append(([[Shape(c, j, w) for c, j, w in row] for row in told["rows"]], bool(told["strip"])))
        except (ValueError, KeyError, TypeError):
            pass
    layouts += [(rows, True), (rows, False)]
    for shapes, strip in layouts:
        if img.size == size(shapes, strip):
            break
    else:
        want = " or ".join(dict.fromkeys(f"{w} x {h}" for w, h in (size(rows, True), size(rows, False))))
        raise ValueError(f"this PNG is {img.size[0]} x {img.size[1]} dots, and the letters PNG of this translation "
                         f"is {want}: save a new one with \"Save as PNG…\" and paint in that")
    rgba = img.convert("RGBA")
    needed = {s.char for s in every_shape(rows)}
    out: dict[str, tuple[str, ...] | None] = {}
    notes: list[str] = []
    ignored = []
    for shape, (x, y) in squares(shapes, strip).items():
        strokes = []
        for i in range(CELL * CELL):
            r, g, b, a = rgba.getpixel((x + i % CELL, y + i // CELL))
            strokes.append(1 if a >= 128 and max(r, g, b) >= PAINTED else 0)
        if shape.char not in needed:
            if any(strokes):
                ignored.append(shape.char)
            continue
        if not any(strokes):
            out[shape.char] = None
            continue
        drawn = rows_of(strokes)
        out[shape.char] = drawn
        if any(strokes[y * CELL + CELL - 1] for y in range(CELL)) or any(strokes[(CELL - 1) * CELL:]):
            notes.append(f"{_name(shape)}: dots in the square's last column or row: their shadow would fall "
                         "outside the letter and is left off there")
        xs = [i % CELL for i, v in enumerate(strokes) if v]
        for side in shape.joins:
            column = max(xs) if side == "r" else min(xs)
            if not strokes[BOTTOM * CELL + column]:
                where = "right" if side == "r" else "left"
                notes.append(f"{_name(shape)} doesn't reach its {where} side on the bottom line, so it won't touch "
                             f"the letter on its {where}")
    if ignored:
        notes.append(f"letters this translation doesn't need were left out: {' '.join(ignored)}")
    return Loaded(out, notes)


def style_letters(page: list[int], colours) -> list[tuple[list[int], tuple]]:
    """The game's own letters for the style strip: (144 palette numbers, RGB colours) from the
    dialogue font page read off the translator's disc (gamelook.DiscLook.page / font palette)."""
    from petext import gametext as gt
    from . import gamelook as GL
    rgb = tuple(GL.rgb_of(c) if c else None for c in colours)
    return [(GL._cell(page, gt.CODE_OF[ch]), rgb) for ch in STYLE_SAMPLE]


def describe(shape: Shape) -> str:
    """The letter's name for the window's list."""
    try:
        name = unicodedata.name(shape.char).title()
    except ValueError:
        name = f"U+{ord(shape.char):04X}"
    return name
