"""Letters past the fixed codes, in a table of each pack's own.

The single-byte codes 0x4D-0xEF are all spoken for (petext/latin.py, petext/cyrillic.py), but no one
language uses them all. A pack can give the codes its text doesn't use to letters it needs, and those
letters stay one byte each, which the game's fixed text fields depend on: 31 bytes for an item
message, 63 for a dialog line. Every byte code means the same as before in a pack with no table, and
Chinese, Japanese and Korean keep the dictionary of petext/cjk.py.

The meaning of a code now belongs to the pack, so nothing that outlives the pack may hold one: the
default player name and the rename grids stay on the fixed codes, and a rename default with a table
letter stays the game's (pe_lang's on_text_lookup).

A letter comes one of three ways:
  base   the game's own letter, untouched (a Greek capital Alpha is the game's A)
  marks  the game's own letter plus marks, put together by pe_lang when the font is uploaded, since
         no game art may go in a pack. A pack carries the recipe, marks and all, so a new mark needs
         no new build of the game
  art    drawn here, like cyrillic.py. Only these strokes are authored; the shadow is generated
Letters no one has drawn well yet (Greek, Hebrew, Arabic and a few others) are placeholders: a
question mark, packed as art, until a translation brings their real pictures (PLACEHOLDER below). See "Letter tables" in docs/LOCALIZATION.md for the record format.
"""
from __future__ import annotations

import struct
import unicodedata
from dataclasses import dataclass, field

from . import cjk, cyrillic, gametext as gt, latin

FIRST_CODE, LAST_CODE = 0x4D, 0xEF
CELL = 12
RECORD = 80
MAX_MARKS = 3
MARK_BYTES = 19                 # PeLatinMark: count, fixed rows, anchor, eight (dx, row)
NO_BASE = 0xFF
OP_DOTLESS, OP_ROTATE, OP_TALL = 1, 2, 4      # OP_TALL: a mark above an ascender, as on a capital
ANCHORS = {name: k for k, name in enumerate(latin.ANCHORS)}


@dataclass(frozen=True)
class Mark:
    points: tuple[tuple[int, int], ...]
    anchor: str = "centre"
    fixed: bool = False         # rows are absolute, else counted from above the letter


MARKS: dict[str, Mark] = {name: Mark(tuple(points), latin.ANCHOR.get(name, "centre"), name in latin.FIXED_ROWS)
                          for name, points in latin.MARKS.items()}
MARKS.update({
    "macron": Mark(((-1, 1), (0, 1), (1, 1), (2, 1))),
    "hook": Mark(((0, 0), (1, 0), (1, 1))),
    "horn": Mark(((1, 2), (2, 1)), "right"),
    "tonos": Mark(((0, 0), (-1, 1)), "left"),
    "acute_high": Mark(((0, -1), (0, 0))),      # between the dots of a dialytika
    "comma_below": Mark(((0, 10), (-1, 11)), fixed=True),
    "dot_below": Mark(((0, 10),), fixed=True),
    "dot_below_side": Mark(((2, 10),), fixed=True),
    "stem_apostrophe": Mark(((2, 1), (2, 2)), "stem", True),
    "bar_ascender": Mark(((-1, 2), (0, 2), (1, 2)), "stem", True),
    "bar_capital": Mark(((-1, 5), (1, 5), (2, 5)), "stem", True),
    "bar_small": Mark(((-1, 6), (1, 6), (2, 6)), "stem", True),
    "bar_across": Mark(((-2, 5), (-1, 5), (0, 5), (1, 5), (2, 5)), fixed=True),
    "bar_across_small": Mark(((-2, 6), (-1, 6), (0, 6), (1, 6), (2, 6)), fixed=True),
    "bar_y": Mark(((-2, 6), (-1, 6), (0, 6), (1, 6), (2, 6)), fixed=True),
    "tail": Mark(((0, 9), (0, 10)), "right", True),
    "dots_above_comma": Mark(((0, 3), (1, 3), (0, 4), (1, 4)), fixed=True),
    # Two marks side by side: a capital has two rows above it, so they can't stack.
    "circumflex_left": Mark(((-1, 0), (-2, 1), (0, 1))),
    "breve_left": Mark(((-3, 0), (0, 0), (-2, 1), (-1, 1))),
    "grave_right": Mark(((2, 0), (3, 1))),
    "acute_right": Mark(((3, 0), (2, 1))),
    "hook_right": Mark(((2, 0), (3, 0), (3, 1))),
    "tilde_right": Mark(((2, 1), (3, 0), (4, 1))),
})

# Authored strokes: (first row, rows top down), each pattern from column 1. # is a stroke. Capitals
# stand on rows 1-8, lowercase on rows 3-8, tails go down to row 10.
ART: dict[str, tuple[int, str]] = {
    # Signs every language needs.
    "(": (1, "..#/.#./#../#../#../#../#../#../.#./..#"),
    ")": (1, "#../.#./..#/..#/..#/..#/..#/..#/.#./#.."),
    "…": (8, "#..#..#"),
    "—": (5, "##########"),
    "–": (5, "#####"),
    "„": (8, ".#.#/.#.#/#.#."),
    "“": (1, ".#.#/#.#./#.#."),
    "”": (1, ".#.#/.#.#/#.#."),
    "‘": (1, ".#/#./#."),
    "’": (1, ".#/.#/#."),
    "‚": (8, ".#/.#/#."),
    "·": (5, "#"),
}


# Letters with no picture yet. Each draws as a question mark until a translation brings real ones
# (a fan patch's own letter art, or a translator's drawing): letters drawn here from nothing came out
# hard to read, and a reader is better served by a plain question mark than by a wrong-looking
# letter. Everything else already works on them (the pack, name letters, right-to-left order,
# Arabic joining), so a real picture is the only thing left to add: put it in ART and take the
# letter out of here.
PLACEHOLDER = (1, ".###./#...#/....#/...#./..#../..#../...../..#..")
PLACEHOLDER_LETTERS = "".join((
    "ðþÞ",                                                      # Icelandic
    "ΓΔΘΛΞΠΣΦΨΩαβγδεζηθκλμξπρςστφχψωΏάέήώ",                     # Greek
    "ЂђЋћЉљЊњЏџӘүұ",                                             # Serbian, Macedonian, Kazakh
    "אבגדהוזחטיךכלםמןנסעףפץצקרשת",                               # Hebrew
))


def _arabic_letters() -> str:
    """Every Arabic shape the renderer picks (alone, end, start, middle; lam-alef; the tatweel)
    and each base letter, all placeholders for now."""
    from . import rtl
    out = []
    for letter in rtl.FORMS:
        out.append(chr(letter))
        out += [chr(form) for form in rtl.forms_of(letter)]
    out.append(chr(rtl.TATWEEL))
    return "".join(dict.fromkeys(out))

def _strokes(top: int, pattern: str) -> list[int]:
    lines = pattern.split("/")
    assert top + len(lines) <= CELL and max(map(len, lines)) <= CELL - 2, pattern
    strokes = [0] * (CELL * CELL)
    for y, line in enumerate(lines, top):
        for x, pixel in enumerate(line, 1):
            if pixel == "#":
                strokes[y * CELL + x] = 1
    return strokes


def _drawn_strokes(ch: str) -> list[int]:
    """The strokes of a letter drawn in the repo: here, or in cyrillic.py."""
    if ch in ART:
        return _strokes(*ART[ch])
    return latin._stroke_only(cyrillic.compose(cyrillic.BY_CODE[cyrillic.CODE_OF[ch]]))


def _rotate(strokes: list[int]) -> list[int]:
    x0, x1, y0, y1 = latin._ink(strokes)
    turned = [0] * (CELL * CELL)
    for i, v in enumerate(strokes):
        if v:
            turned[(y0 + y1 - i // CELL) * CELL + (x0 + x1 - i % CELL)] = 1
    return turned


@dataclass(frozen=True)
class Letter:
    char: str
    base: str = ""                  # the game letter it starts from
    drawn: str = ""                 # or the drawn letter it starts from
    marks: tuple[str, ...] = ()
    ops: int = 0
    extra: int = 0                  # marks wider than the base letter: added to its advance
    art: tuple[int, str] | None = field(default=None)
    # A picture of the pack's own, shadow and all (a fan patch's look, drawn()): its 144 pixels and
    # the widths it brings, the dialogue box's advance and the menus' metrics byte.
    pixels: tuple[int, ...] | None = field(default=None)
    dialogue: int = 0
    menu: int = 0

    @property
    def from_game(self) -> bool:
        return bool(self.base)

    def compose(self, base_cell: list[int] | None = None) -> list[int]:
        """The letter's 144 pixels (0 empty, 1 stroke, 2 shadow). `base_cell` is the game's letter
        for one that starts from it; pe_lang_letter_compose does the same on the player's disc."""
        if self.pixels:
            return list(self.pixels)
        if self.art:
            return latin._shadow(_strokes(*self.art))
        strokes = latin._stroke_only(base_cell) if self.base else _drawn_strokes(self.drawn)
        if self.ops & OP_DOTLESS:
            strokes = [0 if i < 3 * CELL else v for i, v in enumerate(strokes)]
        if self.ops & OP_ROTATE:
            strokes = _rotate(strokes)
        capital = (self.base or self.drawn).isupper() or bool(self.ops & OP_TALL)
        if capital and any(not MARKS[m].fixed for m in self.marks):
            strokes = latin._squash(strokes)
        ink = latin._ink(strokes)
        if ink and self.marks:
            out = list(strokes)
            x0, x1, _, _ = ink
            for name in self.marks:
                mark = MARKS[name]
                if mark.anchor == "stem":
                    counts = [sum(strokes[y * CELL + x] for y in range(CELL)) for x in range(CELL)]
                    anchor = counts.index(max(counts))
                else:
                    anchor = {"left": x0, "right": x1}.get(mark.anchor, (x0 + x1) // 2)
                top = 0 if capital else 1
                for dx, row in mark.points:
                    x, y = anchor + dx, row if mark.fixed else top + row
                    if 0 <= x < CELL and 0 <= y < CELL:
                        out[y * CELL + x] = 1
            strokes = out
        return latin._shadow(strokes)

    def advance(self) -> int:
        """The dialogue box's advance, as the mod works it out."""
        if self.dialogue:
            return self.dialogue
        if self.base:
            return gt.base_advance(gt.CODE_OF[self.base]) + self.extra
        return latin._ink(self.compose())[1] + 2

    def cut(self) -> tuple[int, int]:
        """(first stroke column, width with the shadow): how pe_lang cuts a joining letter
        (pe_lang_letter_cut), in dialogue and menus alike."""
        x0, x1, _, _ = latin._ink(self.compose())
        return x0, min(x1 - x0 + 2, CELL - x0)

    def joined_advance(self) -> int:
        """How far a joining letter moves the pen (one pixel past its cut); the renderer moves it
        two less where it joins the letter on its left (petext/rtl.py width)."""
        return self.cut()[1] + 1

    def metric(self, metrics: bytes) -> int:
        """The menus' metrics byte: drawn width << 4 | first column."""
        if self.pixels and self.menu:
            return self.menu
        if self.base:
            value = gt.base_metric(gt.CODE_OF[self.base], metrics)
            return min((value >> 4) + self.extra, 15) << 4 | value & 15
        return min(self.advance(), 15) << 4

    def record(self, code: int) -> bytes:
        """What goes in the pack: u32 character, u8 code, advance, base code, ops, 72 bytes."""
        head = struct.pack("<I", ord(self.char)) + bytes([code])
        if self.base:
            body = bytearray(72)
            for k, name in enumerate(self.marks):
                mark = MARKS[name]
                packed = bytes([len(mark.points), mark.fixed, ANCHORS[mark.anchor]])
                packed += b"".join(struct.pack("bb", dx, row) for dx, row in mark.points)
                body[k * MARK_BYTES:(k + 1) * MARK_BYTES] = packed.ljust(MARK_BYTES, b"\0")
            return head + bytes([self.extra, gt.CODE_OF[self.base], self.ops]) + bytes(body)
        pixels = self.compose()
        packed = bytes(pixels[i] | pixels[i + 1] << 4 for i in range(0, 144, 2))
        # a picture of the pack's own keeps its menu metrics in the ops byte (0: from the advance)
        return head + bytes([self.advance(), NO_BASE, self.menu if self.pixels else 0]) + packed


def _catalog() -> dict[str, Letter]:
    out: dict[str, Letter] = {}

    def add(ch: str, base: str = "", *marks: str, ops: int = 0, extra: int = 0, drawn: str = ""):
        assert ch not in out, ch
        assert len(marks) <= MAX_MARKS
        out[ch] = Letter(ch, base, drawn, marks, ops, extra)

    def both(lower: str, base: str, *marks: str, ops: int = 0):
        add(lower, base, *marks, ops=ops)
        add(lower.upper(), base.upper(), *marks)

    for ch, art in ART.items():
        out[ch] = Letter(ch, art=art)
    for ch in PLACEHOLDER_LETTERS + _arabic_letters():
        out[ch] = Letter(ch, art=PLACEHOLDER)
    out["Ð"] = Letter("Ð", "D", "", ("bar_capital",))

    # Latin: Slovak, Romanian, Croatian, Lithuanian, Latvian.
    add("ĺ", "l", "acute", ops=OP_TALL)
    add("Ĺ", "L", "acute")
    add("ľ", "l", "stem_apostrophe")
    add("Ľ", "L", "stem_apostrophe")
    both("ŕ", "r", "acute")
    both("ă", "a", "breve")
    both("ș", "s", "comma_below")
    both("ț", "t", "comma_below")
    both("ţ", "t", "cedilla")
    add("đ", "d", "bar_ascender")
    add("Đ", "D", "bar_capital")
    both("ė", "e", "dot")
    both("į", "i", "ogonek")
    both("ų", "u", "ogonek")
    both("ā", "a", "macron")
    both("ē", "e", "macron")
    both("ī", "i", "macron", ops=OP_DOTLESS)
    both("ū", "u", "macron")
    add("ģ", "g", "acute")
    add("Ģ", "G", "comma_below")
    both("ķ", "k", "comma_below")
    both("ļ", "l", "comma_below")
    both("ņ", "n", "comma_below")

    # Vietnamese: a vowel, what makes it a vowel of its own, and one of five tones.
    tones = {"grave": ("̀", "grave_right"), "acute": ("́", "acute_right"),
             "hook": ("̉", "hook_right"), "tilde": ("̃", "tilde_right")}

    def vietnamese(ch: str, base: str, *marks: str, ops: int = 0, extra: int = 0):
        if ch not in out and ch not in latin.CODE_OF:
            add(ch, base, *marks, ops=ops, extra=extra)

    for case in (str.lower, str.upper):
        for vowel in "aeiouy":
            base, dotless = case(vowel), OP_DOTLESS if case(vowel) == "i" else 0
            for tone, (combining, _) in tones.items():
                vietnamese(unicodedata.normalize("NFC", base + combining), base, tone, ops=dotless)
            below = "dot_below_side" if vowel == "y" else "dot_below"
            vietnamese(unicodedata.normalize("NFC", base + "̣"), base, below)
        for vowel, combining, plain, left in (("a", "̂", "circumflex", "circumflex_left"),
                                              ("e", "̂", "circumflex", "circumflex_left"),
                                              ("o", "̂", "circumflex", "circumflex_left"),
                                              ("a", "̆", "breve", "breve_left")):
            base = case(vowel)
            for tone, (mark, right) in tones.items():
                vietnamese(unicodedata.normalize("NFC", base + combining + mark), base, left, right)
            vietnamese(unicodedata.normalize("NFC", base + combining + "̣"), base, plain, "dot_below")
        for vowel in "ou":
            base = case(vowel)
            vietnamese(unicodedata.normalize("NFC", base + "̛"), base, "horn", extra=1)
            for tone, (combining, _) in tones.items():
                vietnamese(unicodedata.normalize("NFC", base + "̛" + combining), base, "horn", tone, extra=1)
            vietnamese(unicodedata.normalize("NFC", base + "̛̣"), base, "horn", "dot_below", extra=1)

    # Greek: capitals the game has a look-alike for stay real letters, so a font draws its own.
    for ch, base in zip("ΑΒΕΖΗΙΚΜΝΟΡΤΥΧ", "ABEZHIKMNOPTYX"):
        add(ch, base)
    for ch, base in (("ο", "o"), ("ν", "v"), ("υ", "u")):
        add(ch, base)
    add("ι", "i", ops=OP_DOTLESS)
    for ch, base in zip("ΆΈΉΊΌΎ", "AEHIOY"):
        add(ch, base, "tonos")
    add("Ϊ", "I", "diaeresis")
    add("Ϋ", "Y", "diaeresis")
    add("ό", "o", "acute")
    add("ύ", "u", "acute")
    add("ί", "i", "acute", ops=OP_DOTLESS)
    add("ϊ", "i", "diaeresis", ops=OP_DOTLESS)
    add("ϋ", "u", "diaeresis")
    add("ΐ", "i", "diaeresis", "acute_high", ops=OP_DOTLESS)
    add("ΰ", "u", "diaeresis", "acute_high")

    # Serbian, Macedonian, Kazakh.
    for ch, base in (("Ј", "J"), ("ј", "j"), ("Ѕ", "S"), ("ѕ", "s"), ("Ү", "Y"), ("Һ", "H"), ("һ", "h")):
        add(ch, base)
    add("Ѓ", "", "acute", drawn="Г")
    add("ѓ", "", "acute", drawn="г")
    add("Ќ", "K", "acute")
    add("ќ", "", "acute", drawn="к")
    add("ә", "e", ops=OP_ROTATE)
    add("Ғ", "", "bar_capital", drawn="Г")
    add("ғ", "", "bar_small", drawn="г")
    add("Қ", "K", "tail")
    add("қ", "", "tail", drawn="к")
    add("Ң", "H", "tail")
    add("ң", "", "tail", drawn="н")
    add("Ө", "O", "bar_across")
    add("ө", "o", "bar_across_small")
    add("Ұ", "Y", "bar_y")

    add(";", ",", "dots_above_comma")
    return out


CATALOG = _catalog()
assert not set(CATALOG) & (set(gt.CODE_OF) | set(gt._latin_codes())), "a letter with a fixed code"


def write_test_vectors(path: str) -> None:
    """tests/pe_lang_letter_vectors.h: every letter's record and what it comes to on the synthetic
    test letters, which the C composer must reproduce pixel for pixel."""
    lines = ["/* Generated by `python -I localization/pelang.py latin-header` from localization/petext/letters.py:",
             " * each letter's pack record in hex, then its 144 pixels on the synthetic test letters; don't edit. */",
             "static const char *const kLetterVectors[][2] = {"]
    for letter in CATALOG.values():
        base = latin.synthetic_cell(gt.CODE_OF[letter.base]) if letter.base else None
        pixels = "".join(map(str, letter.compose(base)))
        lines.append(f'    {{"{letter.record(FIRST_CODE).hex()}", "{pixels}"}}, /* U+{ord(letter.char):04X} */')
    lines += ["};", ""]
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


EXTRA_TOKEN = b"\xf1\xc7"     # an extra letter's form until it goes in a stretch; never in a pack
MAX_EXTRA = 240 * 240


def extra_token(index: int) -> bytes:
    return EXTRA_TOKEN + bytes(divmod(index, 240))


class Letters:
    """One pack's table: the catalog's letters its text uses, on codes its text doesn't.

    The 163 codes go to the letters used most, and first to those in text that keeps one byte per
    letter (`byte_texts`: item names and the like, petext/stretch.py). With `extra`, the letters
    left over have no code: they are kept after the coded ones (code 0, rising Unicode) and reach
    the screen only in stretches and name letters, which pe_lang draws into the letter cache. Until
    the stretches are made they are written as F1 C7 hi lo (extra_token), which no pack holds.

    With `dictionary`, signs the dictionary can hold (curly quotes, dashes) stay there, as the
    full-width signs Chinese, Japanese and Korean text expects.

    `shapes` are letters the pack holds although its text doesn't use them: the joined shapes of
    its Arabic letters, which the renderer picks as it draws (petext/rtl.py). Those, and joined
    shapes the text does use, never take a code: pe_lang cuts them so joined letters touch, which it
    does for letters with no code."""

    def __init__(self, texts, dictionary: bool = False, byte_texts=(), extra: bool = False, shapes=(), own=None):
        # `own`: letters the pack brings pictures of that no catalog draws (a fan patch's symbol)
        known = {**CATALOG, **{ch: letter for ch, letter in (own or {}).items()
                               if ch not in CATALOG and ch not in gt.CODE_OF and ch not in gt._latin_codes()}}
        fixed, used, taken = gt._latin_codes(), {}, set()
        in_bytes = set()
        for text in byte_texts:
            in_bytes.update(ch for ch in gt._TAG.sub("", text) if ch in known)
        for text in texts:
            for m in gt._TAG.finditer(text):
                if m.group(1)[0] == "g" and len(m.group(1)) == 3:
                    taken.add(int(m.group(1)[1:], 16))
            for ch in gt._TAG.sub("", text):
                if ch in gt.CODE_OF:
                    continue
                if ch in fixed:
                    taken.add(fixed[ch])
                elif ch in known and not (dictionary and cjk.supported(ord(ch))):
                    used[ch] = used.get(ch, 0) + 1
        from .rtl import is_joining
        joined = {ch for ch in shapes if ch in known} | {ch for ch in used if is_joining(ord(ch))}
        for ch in joined:
            used.pop(ch, None)
        free = [code for code in range(LAST_CODE, FIRST_CODE - 1, -1) if code not in taken]
        # byte-only text first, then the most used, then by letter so a build is repeatable
        order = sorted(used, key=lambda ch: (ch not in in_bytes, -used[ch], ord(ch)))
        if len(order) > len(free):
            short = [ch for ch in order if ch in in_bytes][len(free):]
            if not extra or short:
                where = " used in item names and other one-byte text" if extra else ""
                raise ValueError(f"{len(short) if extra else len(order)} letters{where} past the fixed "
                                 f"codes need a table, and only {len(free)} codes are free in this pack")
        coded, rest = order[:len(free)], sorted(set(order[len(free):]) | joined, key=ord)
        if len(rest) > MAX_EXTRA:
            raise ValueError(f"more than {MAX_EXTRA} letters without a code")
        self.by_code = {code: known[ch] for code, ch in zip(free, sorted(coded, key=ord))}
        self.codes = {letter.char: bytes([code]) for code, letter in self.by_code.items()}
        self.extra = [known[ch] for ch in rest]
        self.extra_codes = {letter.char: extra_token(k) for k, letter in enumerate(self.extra)}
        self.codes.update(self.extra_codes)
        self.by_token = {extra_token(k): letter for k, letter in enumerate(self.extra)}
        self.drawn: dict[int, Letter] = {}

    def draw(self, pictures: dict[str, dict], texts) -> int:
        """Give letters the pack's own pictures and widths (pelang.py build --patch-letters: a fan
        patch's look, from import-patch). A game letter (codes 00-4C) always takes its picture; a
        fixed Latin Extended or Cyrillic letter when the text uses it; both keep their code and what
        it means, so saves, name grids and other packs read them as before, and they go in `drawn`.
        A letter of the pack's own table takes the picture in its record. Returns how many."""
        fixed = gt._latin_codes()
        used = {ch for text in texts for ch in gt._TAG.sub("", text)}
        count = 0
        for ch, p in pictures.items():
            letter = Letter(ch, pixels=tuple(p["pixels"]), dialogue=p["dialogue"], menu=p["menu"])
            if ch in gt.CODE_OF:
                self.drawn[gt.CODE_OF[ch]] = letter
            elif ch in fixed and ch in used:
                self.drawn[fixed[ch]] = letter
            elif ch in self.codes and len(self.codes[ch]) == 1:
                self.by_code[self.codes[ch][0]] = letter
            elif ch in self.extra_codes:
                k = self.extra.index(next(x for x in self.extra if x.char == ch))
                self.extra[k] = letter
                self.by_token[extra_token(k)] = letter
            else:
                continue
            count += 1
        return count

    def __bool__(self) -> bool:
        return bool(self.by_code or self.extra or self.drawn)

    def records(self) -> bytes:
        coded = {**self.by_code, **self.drawn}
        return (b"".join(coded[code].record(code) for code in sorted(coded)) +
                b"".join(letter.record(0) for letter in self.extra))


def coded(index: int, record: bytes) -> bool:
    """Whether record `index` of a table has a code. Code 0 means none, except that the first record
    may redraw the game's '0', whose code is 0 (pe_lang_pack.c reads it the same way)."""
    return bool(record[4]) or (index == 0 and struct.unpack_from("<I", record)[0] == ord("0"))


def picture_letters(pictures: dict[str, dict]) -> dict[str, Letter]:
    """load_pictures' letters as Letters (for Letters(own=...): a patch's symbol no catalog draws)."""
    return {ch: Letter(ch, pixels=tuple(p["pixels"]), dialogue=p["dialogue"], menu=p["menu"])
            for ch, p in pictures.items()}


def load_pictures(path) -> dict[str, dict]:
    """A letter pictures file (import-patch --patch-letters): letter -> its 144 pixels, the dialogue
    box's advance and the menus' metrics byte. Rows of 12: ' ' empty, '#' stroke, '+' shadow."""
    import json
    data = json.loads(open(path, encoding="utf-8").read())
    out = {}
    for entry in data["letters"]:
        ch, rows = entry["letter"], entry["picture"]
        if len(ch) != 1 or len(rows) != CELL or any(len(r) != CELL or set(r) - set(" #+") for r in rows):
            raise ValueError(f"{path}: {ch!r}: a picture is {CELL} rows of {CELL} of ' ', '#' and '+'")
        if not 1 <= entry["dialogue"] <= 15 or not 0 <= entry["menu"] <= 255 or (entry["menu"] & 15) >= CELL:
            raise ValueError(f"{path}: {ch!r}: a width out of range")
        out[ch] = {"pixels": [" #+".index(v) for r in rows for v in r],
                   "dialogue": entry["dialogue"], "menu": entry["menu"]}
    return out
