"""Draw the letters of petext/letters.py as pe_lang will, from the game font on the player's disc.

  python preview_letters.py US_DISC1.cue [-o out/letters.png]

Writes a picture (git-ignored under out/): every letter enlarged, by group, with a line of each
language set with the dialogue box's spacing. For choosing letter shapes.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

from petext import alphabets, cyrillic, font, gametext as gt, latin, letters  # noqa: E402
from petext.disc import Game  # noqa: E402

COLOURS = {0: (24, 26, 40), 1: (246, 246, 246), 2: (70, 70, 80)}
COLUMNS, CELL_SCALE, LINE_SCALE = 22, 4, 3
PITCH = 12 * CELL_SCALE + 10

GROUPS = [
    ("Slovak, Romanian, Croatian, Icelandic, Lithuanian, Latvian", ("sk", "ro", "hr", "is", "lt", "lv")),
    ("Vietnamese", ("vi",)),
    ("Greek", ("el",)),
    ("Serbian, Macedonian, Kazakh", ("sr-Cyrl", "mk", "kk")),
]
SIGNS = "( ) ; … — – „ “ ” ‘ ’ ‚ ·".split()


def game_cells(disc: str) -> dict[int, list[int]]:
    _, width, _, pixels = font.page(Game(disc), font.PAGE0_ENTRY)
    return {code: list(font.cell(pixels, width, code)) for code in range(77)}


def paste(img: Image.Image, cell: list[int], x: int, y: int, scale: int) -> None:
    for i, v in enumerate(cell):
        if v:
            cx, cy = x + (i % 12) * scale, y + (i // 12) * scale
            img.paste(COLOURS[v], (cx, cy, cx + scale, cy + scale))


def drawn(letter: letters.Letter, game: dict[int, list[int]]) -> list[int]:
    return letter.compose(game[gt.CODE_OF[letter.base]] if letter.base else None)


def fixed_cell(ch: str, game: dict[int, list[int]]) -> list[int]:
    """A letter with a fixed code, as pe_lang composes it."""
    code = gt.CODE_OF.get(ch)
    if code is not None:
        return game[code]
    code = gt._latin_codes()[ch]
    if code < 77:
        return game[code]
    if code in cyrillic.BY_CODE:
        return cyrillic.compose(cyrillic.BY_CODE[code])
    glyph = latin.BY_CODE[code]
    return latin.compose(glyph, game[gt.CODE_OF[glyph.base]] if glyph.base else None)


def set_line(img: Image.Image, text: str, x: int, y: int, game: dict[int, list[int]], limit: int) -> None:
    """A line as the dialogue box sets it, ending where it would leave the picture."""
    for ch in text:
        if ch == " ":
            x += gt.BOX_ADVANCE[0x0F] * LINE_SCALE
            continue
        if ch in letters.CATALOG:
            cell, advance = drawn(letters.CATALOG[ch], game), letters.CATALOG[ch].advance()
        else:
            cell = fixed_cell(ch, game)
            code = gt.CODE_OF[ch] if ch in gt.CODE_OF else gt._latin_codes()[ch]
            advance = gt.BOX_ADVANCE[code] if code < 77 else gt._latin_advance(code) or gt.WIDE
        if x + advance * LINE_SCALE > limit:
            return
        paste(img, cell, x, y, LINE_SCALE)
        x += advance * LINE_SCALE


def group_letters(codes) -> list[str]:
    seen: dict[str, None] = {}
    for code in codes:
        for ch in alphabets.full_alphabet(alphabets.BY_CODE[code]):
            if ch in letters.CATALOG:
                seen[ch] = None
    return list(seen)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("disc")
    ap.add_argument("-o", "--out", default=str(ROOT / "out" / "letters.png"))
    a = ap.parse_args()
    game = game_cells(a.disc)
    label = ImageFont.truetype(r"C:\Windows\Fonts\arial.ttf", 14)
    title = ImageFont.truetype(r"C:\Windows\Fonts\arialbd.ttf", 18)
    width = COLUMNS * PITCH + 40

    sections = [(name, group_letters(codes), codes) for name, codes in GROUPS] + [("Signs for every language", SIGNS, ())]
    height = 20
    for _, chars, codes in sections:
        height += 40 + ((len(chars) + COLUMNS - 1) // COLUMNS) * (PITCH + 22) + len(codes) * (13 * LINE_SCALE) + 16
    img = Image.new("RGB", (width, height), COLOURS[0])
    d = ImageDraw.Draw(img)
    y = 20
    for name, chars, codes in sections:
        d.text((20, y), name, fill=(120, 200, 255), font=title)
        y += 34
        for i, ch in enumerate(chars):
            x = 20 + (i % COLUMNS) * PITCH
            row = y + (i // COLUMNS) * (PITCH + 22)
            paste(img, drawn(letters.CATALOG[ch], game), x, row, CELL_SCALE)
            d.text((x + 8, row + 12 * CELL_SCALE + 2), ch, fill=(240, 200, 90), font=label)
        y += ((len(chars) + COLUMNS - 1) // COLUMNS) * (PITCH + 22) + 6
        for code in codes:
            set_line(img, alphabets.BY_CODE[code].sample, 20, y, game, width - 20)
            y += 13 * LINE_SCALE
        y += 10
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    img.save(a.out)
    print(f"wrote {a.out}: {sum(len(chars) for _, chars, _ in sections)} letters")
    return 0


if __name__ == "__main__":
    sys.exit(main())
