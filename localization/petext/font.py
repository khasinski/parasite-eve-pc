"""The dialogue font on the disc: 4-bit TIM images in PE.IMG, uploaded to the VRAM slots the
textbox draws from (src/main/text/tables.c, g_FieldFontTextureState):

  page 0  entry 10, VRAM 0x140,0   single-byte codes and FB xx (see glyph_cell)
  page 1  entries 45..66, VRAM 0x180,0   FC xx / FD xx (cells xx + 0x34, xx + 0x134); the game
          has 22 of these and loads one per area

Cells are 12x12, 21 to a row. Nothing read here is written anywhere but out/.
"""
from __future__ import annotations

import struct

PAGE0_ENTRY = 10
PAGE1_ENTRIES = range(45, 67)
CELL, PER_ROW = 12, 21


def tim4(blob: bytes, at: int = 0):
    """(palette as 16 RGBA tuples, width, height, pixel indices row by row) of a 4-bit TIM."""
    magic, flags = struct.unpack_from("<II", blob, at)
    if magic != 0x10 or flags & 7 != 0 or not flags & 8:
        raise ValueError("not a 4-bit TIM with a palette")
    clen = struct.unpack_from("<I", blob, at + 8)[0]
    colours = struct.unpack_from("<16H", blob, at + 20)
    pal = []
    for c in colours:
        r, g, b = (c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31
        pal.append((r, g, b, 0 if c == 0 else 255))
    io = at + 8 + clen
    _, _, _, w, h = struct.unpack_from("<I4H", blob, io)
    raw = blob[io + 12:io + 12 + w * h * 2]
    px = []
    for byte in raw:
        px.append(byte & 15)
        px.append(byte >> 4)
    return pal, w * 4, h, px


def page(game, entry: int):
    """The font image of PE.IMG entry `entry` (its TIM is at the start of the entry)."""
    s = game.sectors
    blob = game.pe(s[entry], s[entry + 1] - s[entry])
    return tim4(blob)


def cell(px: list[int], width: int, index: int) -> tuple[int, ...]:
    """The 12x12 pixel indices of cell `index`."""
    x0, y0 = (index % PER_ROW) * CELL, (index // PER_ROW) * CELL
    return tuple(px[(y0 + y) * width + x0 + x] for y in range(CELL) for x in range(CELL))


PAGE0_CELLS = 21 * PER_ROW       # 441: the rows that fit a 256-pixel page


def glyph_cell(prefix: int | None, code: int) -> tuple[int, int]:
    """(font page, cell) the textbox draws for a glyph (src/main/draw/textbox.c):
    a single byte c is page 0 cell c; FB xx (xx past the FB commands) is cell xx + 0xED,
    running on into page 1 past page 0's last row; FC xx and FD xx are page 1 cells
    xx + 0x34 and xx + 0x134."""
    if prefix is None:
        return 0, code
    if prefix == 0xFB:
        c = code + 0xED
        return (0, c) if c < PAGE0_CELLS else (1, c - PAGE0_CELLS)
    return 1, code + (0x34 if prefix == 0xFC else 0x134)
