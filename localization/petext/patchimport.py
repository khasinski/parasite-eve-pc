"""Turn any fan patch's text into a translation file for a language pack.

The port plays the original game data, so a patch can't be played as it is. This compares the
original disc with the patched one and keeps what the player would read:

  letters   each code past the English font is identified from its art in the patch's own font:
            a copy of a letter (the coloured speaker sets), or an accented letter (the English
            base letter plus the mark that fits best). Nothing about a patch is hard-coded.
  text      every page and menu entry that differs, in the tagged text pelang.py builds from,
            and the battle messages in the boot program (petext/battle.py).

Waits, names and timing may differ from the English: a translator moves them freely, and a
patch that leaves the room scripts alone played that way on the real game. Choices can't change,
because the scripts act on them, and `script_changes` reports any room whose scripts the patch
did edit, since the port can't take those from a pack.

No patch, disc data or translated text goes into the repository.
"""
from __future__ import annotations

import re
import struct
import zlib

from . import font, gametext as gt, latin, validation

RENAME_GRIDS = ("sys/115", "sys/116", "sys/117")
SPEAKER_COLOUR = re.compile(rb"\xfb\x04(\xfa[\x00-\xef]{0,4})\xfb\x05")
BRIGHT = 128


def _inks(pal, cell) -> tuple[int, ...]:
    """The cell's letter pixels, whatever colour its set is, leaving out the dark shadow."""
    bright = [i for i, c in enumerate(pal) if c[3] and max(c[:3]) >= BRIGHT]
    return tuple(1 if v in bright else 0 for v in cell)


def _layered(pal, cell) -> list[int]:
    """The cell as latin.py draws it: 0 empty, 1 stroke, 2 shadow."""
    ink = _inks(pal, cell)
    return [1 if i else 2 if v else 0 for i, v in zip(ink, cell)]


def _is_double_mark(ink) -> bool:
    """A quote cell with two strokes: patches often turn the apostrophe into a closing quote."""
    columns = [any(ink[y * font.CELL + x] for y in range(font.CELL)) for x in range(font.CELL)]
    groups = sum(1 for x, on in enumerate(columns) if on and (x == 0 or not columns[x - 1]))
    return groups >= 2


def _rows(ink) -> list[tuple[int, ...]]:
    return [tuple(ink[y * font.CELL:(y + 1) * font.CELL]) for y in range(font.CELL)]


def _shifted(ink, dx: int, dy: int) -> tuple[int, ...]:
    out = [0] * len(ink)
    for i, on in enumerate(ink):
        x, y = i % font.CELL + dx, i // font.CELL + dy
        if on and 0 <= x < font.CELL and 0 <= y < font.CELL:
            out[y * font.CELL + x] = 1
    return tuple(out)


def _bodies(ink) -> list[tuple[int, tuple[int, ...]]]:
    """A base letter as an accented copy may draw it: as it is, or with rows taken out so a mark
    fits over a capital (a tall mark takes two), moved a pixel or two."""
    rows = _rows(ink)
    used = [y for y, row in enumerate(rows) if any(row)]
    shapes = [(0, tuple(ink))]
    inner = used[1:-1]
    for drop in [(y,) for y in inner] + [(a, b) for a in inner for b in inner if a < b]:
        kept = [row for y, row in enumerate(rows) if y not in drop] + [(0,) * font.CELL] * len(drop)
        shapes.append((len(drop), _shifted(tuple(v for row in kept for v in row), 0, len(drop))))
    return [(n, _shifted(shape, dx, dy)) for n, shape in shapes for dx in (-1, 0, 1) for dy in (-1, 0, 1, 2)]


def _mark_name(points: list[tuple[int, int]], above: bool) -> str | None:
    """Name a mark from its pixels (x, y)."""
    if not points:
        return None
    if not above:
        return "cedilla"
    xs = sorted({x for x, _ in points})
    groups = 1 + sum(1 for a, b in zip(xs, xs[1:]) if b - a > 1)
    if groups == 2:
        return "diaeresis"
    if len(xs) == 1:
        return "dot"
    rows = sorted({y for _, y in points})
    if len(xs) >= 4:
        return "tilde"
    top = [x for x, y in points if y == rows[0]]
    low = [x for x, y in points if y == rows[-1]]
    middle = (xs[0] + xs[-1]) / 2
    if len(top) >= 2 and sum(top) / len(top) == middle and len(low) < len(top):
        return "caron"
    if len(low) >= 2 and sum(low) / len(low) == middle and len(top) < len(low):
        return "circumflex"
    return "acute" if sum(top) / len(top) > sum(low) / len(low) else "grave"


def _identify(ink, bases: dict[str, tuple[int, ...]]) -> str | None:
    """The accented letter a cell draws: the base letter it holds, plus the mark left over."""
    best = None
    for char, base in bases.items():
        for dropped, body in _bodies(base):
            missing = sum(b and not a for a, b in zip(ink, body))
            if missing > 1:
                continue
            spare = [(i % font.CELL, i // font.CELL) for i, (a, b) in enumerate(zip(ink, body)) if a and not b]
            used = [i // font.CELL for i, b in enumerate(body) if b]
            top, bottom = min(used), max(used)
            above = [p for p in spare if p[1] < top]
            below = [p for p in spare if p[1] > bottom]
            stray = len(spare) - len(above) - len(below)
            score = (missing + stray, dropped, -sum(body))
            if best is None or score < best[0]:
                best = (score, char, above, below)
    if best is None or best[0][0] > 2 or (best[2] and best[3]):
        return None
    _, char, above, below = best
    mark = _mark_name(above or below, bool(above))
    if mark is None:
        return None
    base = "i" if char == "ı" else char
    for g in latin.GLYPHS:
        if g.base == base and g.mark == mark and (base != "i" or "dotless" in g.ops):
            return g.char
    return None


def _turned(ink, bases: dict[str, tuple[int, ...]]) -> str | None:
    """Inverted punctuation: the mark turned over, wherever it sits in its cell."""
    flipped = tuple(reversed(ink))
    for char, turned in (("?", "¿"), ("!", "¡")):
        for dx in range(-font.CELL + 1, font.CELL):
            for dy in range(-font.CELL + 1, font.CELL):
                if sum(a != b for a, b in zip(_shifted(flipped, dx, dy), bases[char])) <= 2:
                    return turned
    return None


def letter_map(game) -> tuple[dict[int, str], dict[int, str], list[int]]:
    """(code -> letter, how each code past English was identified, codes with art but no letter)."""
    pal, width, _, px = font.page(game, font.PAGE0_ENTRY)
    cells = {code: font.cell(px, width, code) for code in range(0xF1)}
    inks = {code: _inks(pal, cell) for code, cell in cells.items()}
    letters = dict(gt.CHARS)
    if _is_double_mark(inks[gt.CODE_OF["'"]]):
        letters[gt.CODE_OF["'"]] = '"'
    how: dict[int, str] = {}
    by_shape: dict[tuple[int, ...], str] = {}
    # Letters first: a coloured copy that looks like both O and 0 is much more likely the letter.
    for code, char in sorted(letters.items(), key=lambda item: (not item[1].isalpha(), item[0])):
        by_shape.setdefault(inks[code], char)
    punctuation = {char: inks[gt.CODE_OF[char]] for char in "?!"}
    bases = {char: inks[gt.CODE_OF[char]] for char in sorted({g.base for g in latin.GLYPHS if g.base})
             if char.isalpha() and char != "i"}
    bases["ı"] = tuple(1 if v == 1 else 0 for v in latin.compose(
        next(g for g in latin.GLYPHS if g.char == "ı"), _layered(pal, cells[gt.CODE_OF["i"]])))
    extra = [code for code in range(0x4D, 0xF1) if any(cells[code])]
    unknown: list[int] = []
    # Plain accented letters first, so their coloured copies below find them by shape.
    for code in extra:
        if inks[code] in by_shape:
            continue
        char = _turned(inks[code], punctuation) or _identify(inks[code], bases)
        if char:
            letters[code] = char
            how[code] = f"identified as {char}"
            by_shape.setdefault(inks[code], char)
    for code in extra:
        if code in letters:
            continue
        if inks[code] in by_shape:
            letters[code] = by_shape[inks[code]]
            how[code] = f"copy of {letters[code]}"
        else:
            unknown.append(code)
    return letters, how, unknown


def _unknown_marks(raw: bytes, letters: dict[int, str]) -> list[int]:
    return [c[0] for c in validation.commands(raw) if c[0] < 0xF1 and c[0] not in letters]


def to_text(raw: bytes, letters: dict[int, str]) -> str:
    """The tagged text of a patched page, its letters read through the patch's own map."""
    swapped = {f"{{g{code:02X}}}": char for code, char in letters.items() if code >= 0x4D}
    text = gt.decode(raw)
    for tag, char in swapped.items():
        text = text.replace(tag, char)
    if letters.get(gt.CODE_OF["'"]) == '"':
        text = text.replace("'", '"')
    return text


def patched_pages(bank: bytes, gap: int = 8) -> dict[int, tuple[bytes, int]]:
    """A patched bank's pages. A translator's tools can leave a stray byte between two pages: the
    game finds a page by its id and never notices, but gt.pages stops there."""
    out, at = {}, 0
    while True:
        pages, used = gt.pages(bank[at:])
        if not used:
            return out
        for page, value in pages.items():
            out.setdefault(page, value)
        at += used
        at = bank.find(0xFE, at, at + gap)
        if at < 0:
            return out


def clean_page(raw: bytes, original: bytes) -> bytes:
    """What a pack can't show: symbols the patch drew on its own font page, and colour around a
    speaker's name, which pe_fonts adds itself in every language."""
    raw = re.sub(rb"\xfb[\x0a-\xf0]", b"", raw)      # a symbol from the patch's page (a music note)
    if b"\xfb\x04" in original or b"\xfb\x05" in original:
        return raw
    return SPEAKER_COLOUR.sub(lambda m: m[1], raw)


def choices(raw: bytes) -> list[bytes]:
    return [c for c in validation.commands(raw) if c[:2] == b"\xfb\x09"]


def compare_page(raw: bytes, patched: bytes, letters: dict[int, str]) -> tuple[str | None, str | None]:
    """(translated text, None), or (None, why the page keeps its English)."""
    value = clean_page(patched, raw)
    if value == raw:
        return None, None
    try:
        if choices(value) != choices(raw):
            return None, "the patch changes a choice, which the scripts act on"
        unknown = _unknown_marks(value, letters)
    except ValueError as error:
        return None, f"a command the port doesn't have: {error}"
    if unknown:
        return None, "custom symbol " + " ".join(f"{code:02X}" for code in sorted(set(unknown)))
    return to_text(value, letters), None


GRID_CELLS = 99                         # the name entry grid's cells (petext/names.py)


def compare_grid(raw: bytes, patched: bytes, letters: dict[int, str]) -> tuple[str | None, str | None]:
    """compare_page for a name entry grid (RENAME_GRIDS): one byte a cell, no commands. A game
    symbol the patch kept from the English grid ({g0D}) stays as it is though the patch's letter map
    doesn't name it, and empty cells past the grid's 99 are dropped (the game reads 99)."""
    while len(patched) > GRID_CELLS and patched[-1] == gt.CODE_OF[" "]:
        patched = patched[:-1]
    if patched == raw:
        return None, None
    if len(patched) > GRID_CELLS:
        return None, f"the patch's grid has {len(patched)} cells; the game's has {GRID_CELLS}"
    if any(c >= 0xF1 for c in patched):
        return None, "a command in the name entry grid"
    unknown = sorted({c for c in patched if c not in letters and not (c < 0x4D and c in raw)})
    if unknown:
        return None, "custom symbol " + " ".join(f"{code:02X}" for code in unknown)
    return to_text(patched, letters), None


def script_changes(original, translated, bank_start) -> list[str]:
    """Rooms whose data differs before their first message bank: edited scripts or models."""
    changed = []
    for number in range(1, 451):
        before, after = original.room(number), translated.room(number)
        if before is None or after is None:
            if (before is None) != (after is None):
                changed.append(f"M{number:03d}")
            continue
        cut = bank_start(before.data)
        if before.data[:cut] != after.data[:cut]:
            changed.append(f"M{number:03d}")
    return changed


def picture_changes(original, translated) -> list[str]:
    """Rooms whose textures differ: text drawn into pictures, which a pack can't carry."""
    changed = []
    for number in range(1, 451):
        before, after = original.room(number), translated.room(number)
        if before is not None and after is not None and before.texture != after.texture:
            changed.append(f"M{number:03d}")
    return changed


def font_picture(game) -> bytes:
    """The patch's letters as a picture font (mods/fonts/README.txt): a PNG in the game's order."""
    pal, width, _, px = font.page(game, font.PAGE0_ENTRY)
    rows = font.CELL * 4
    raw = bytearray()
    for y in range(rows):
        raw.append(0)
        for x in range(font.PER_ROW * font.CELL):
            colour = pal[px[y * width + x]]
            raw += bytes(colour) if colour[3] else bytes(4)
    chunk = lambda kind, data: struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    head = struct.pack(">IIBBBBB", font.PER_ROW * font.CELL, rows, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", head) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")


# The dialogue box's widths in the USA boot program (the same place in SLUS_006.62 and SLUS_006.68):
# g_TextboxGlyphSpacing (src/main/text/tables.c), 78 x {left gap, width} from 0x81EA0. A patch with
# letters of its own can make it one width a code from 0x81EA1, with no left gap and room for its new
# letters, by changing the four instructions that read it (file offset: the game's word, the patch's).
WIDTHS_AT = 0x81EA0
WIDTH_READS = {0x284E4: (0x0006B840, 0x00C0B821),   # sll s7,a2,1  -> move s7,a2
               0x28540: (0x902316A0, 0x00001821),   # lbu v1,left  -> move v1,0
               0x28FB8: (0x902316A0, 0x00001821),
               0x29074: (0x00021040, 0x00401021)}   # sll v0,v0,1  -> move v0,v0
BYTE_WIDTHS = 2 * len(gt.ADVANCE) - 1               # codes the one-byte table has room for


def dialogue_widths(original: bytes, translated: bytes) -> tuple[dict[int, int] | None, str]:
    """(code -> how far the patched dialogue box moves after it, how the patch's table was read).
    None when the boot program isn't the USA one, or reads its widths in a way not known here."""
    word = lambda exe, at: struct.unpack_from("<I", exe, at)[0] if at + 4 <= len(exe) else None
    game = bytes(v for w in gt.ADVANCE for v in (0, w))
    if original[WIDTHS_AT:WIDTHS_AT + len(game)] != game or any(word(original, at) != w[0] for at, w in WIDTH_READS.items()):
        return None, "not the USA boot program"
    reads = [word(translated, at) for at in WIDTH_READS]
    table = translated[WIDTHS_AT:WIDTHS_AT + len(game)]
    if reads == [w[0] for w in WIDTH_READS.values()]:
        return {c: table[2 * c + 1] - table[2 * c] for c in range(len(gt.ADVANCE))}, "the game's table (a gap and a width a letter)"
    if reads == [w[1] for w in WIDTH_READS.values()]:
        return {c: table[1 + c] for c in range(BYTE_WIDTHS)}, "one width a letter, the patch's own table"
    return None, "the patch reads its widths in a way not known here"


def menu_metrics(game) -> bytes:
    """The menus' letter metrics (drawn width << 4 | first column) from the resident pack."""
    pack = game.resident_pack()
    at = struct.unpack_from("<i", pack, 0x28)[0]           # ResidentTableDirectory.glyphMetrics
    return pack[at:at + 444]


def letter_pictures(original, translated, letters: dict[int, str], how: dict[int, str]) -> tuple[list[dict], str]:
    """The patch's own look, for pelang.py build --patch-letters: each letter it draws as its picture
    (12 rows of ' ' empty, '#' stroke, '+' shadow), the dialogue box's advance and the menus' metrics
    byte. A game letter the patch left as it was is left out, and so are coloured copies. Returns the
    letters and how the dialogue widths were read."""
    pal_o, width_o, _, px_o = font.page(original, font.PAGE0_ENTRY)
    pal, width, _, px = font.page(translated, font.PAGE0_ENTRY)
    widths, read = dialogue_widths(original.disc.file(original.exe_name), translated.disc.file(translated.exe_name))
    metrics_o, metrics = menu_metrics(original), menu_metrics(translated)
    out, seen = [], set()
    for code in range(0xF1):
        char = letters.get(code)
        if char is None or char in seen or how.get(code, "").startswith("copy of"):
            continue
        if code < 0x4D and gt.CHARS.get(code) != char:      # the apostrophe drawn as a quote
            continue
        seen.add(char)
        cell = _layered(pal, font.cell(px, width, code))
        ink = [v == 1 for v in cell]
        right = max((i % font.CELL for i, on in enumerate(ink) if on), default=-1)
        advance = widths.get(code) if widths else None
        if advance is None:
            advance = gt.ADVANCE[code] if code < 0x4D else right + 2
        if code < 0x4D and (cell == _layered(pal_o, font.cell(px_o, width_o, code)) and advance == gt.ADVANCE[code]
                            and metrics[code] == metrics_o[code]):
            continue
        if not 1 <= advance <= 15 or (metrics[code] & 15) >= font.CELL:
            continue
        rows = ["".join(" #+"[v] for v in cell[y * font.CELL:(y + 1) * font.CELL]) for y in range(font.CELL)]
        out.append({"letter": char, "code": f"{code:02X}", "dialogue": advance, "menu": metrics[code], "picture": rows})
    return out, read


def exe_changes(original, translated) -> int:
    before, after = original.disc.file(original.exe_name), translated.disc.file(translated.exe_name)
    return sum(a != b for a, b in zip(before, after)) + abs(len(before) - len(after))
