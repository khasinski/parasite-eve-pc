"""Pictures with words in them, taken from a fan patch for a translation file.

A patch changes bytes on the disc. The ones that are picture data go into `pictures.bin` as
pieces: a PE.IMG sector, where in it, the CRC-32 of the English bytes there, and the patch's
bytes. The pe_lang mod puts a piece in as that sector is read from the disc, only when the bytes
it finds there have that CRC, so a piece never lands on another disc version, and Disc 1 and
Disc 2 pieces can share one file (mods/pe_lang/pe_lang_pictures.c).

What counts as picture data (everything else a patch changes is reported, never taken):
- each room's first two runs, both picture bundles (its third holds models, scripts and text);
- the whole of entry 9 (the field's background pictures) and entry 76 (effect pictures);
- every TIM picture found in the other entries, except the letters page at VRAM 320,0, which a
  translation file's letters already replace.
Text (entries 1 and 2 and the rooms' messages) comes through the pack's text instead. Room
scripts, the opening credits (entry 71) and the boot program are not pictures.

pictures.bin, little-endian:
  header  16 bytes: "PEPX", u32 version (1), u32 piece count, u32 offset of the bytes
  pieces  count x {u32 sector, u16 offset in it, u16 length, u32 CRC-32 of the English bytes,
          u32 offset of the new bytes from the bytes start}, sorted by sector then offset
  bytes   each piece's new bytes, in piece order
"""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

SECTOR = 2048
MAGIC, VERSION = b"PEPX", 1
HEADER = struct.Struct("<4sIII")
PIECE = struct.Struct("<IHHII")
WHOLE_ENTRIES = {9: "field background pictures", 76: "effect pictures"}
LETTERS_PAGE = (320, 0, 64, 256)          # the dialogue letters: a translation file's letters replace it
MAX_BYTES = 64 << 20                      # largest pictures.bin the game reads


@dataclass(frozen=True)
class Piece:
    sector: int          # PE.IMG sector
    offset: int          # byte in that sector
    original_crc: int    # CRC-32 of the English bytes there
    data: bytes          # the patch's bytes


def tims(data: bytes):
    """(start, end, image rect) of each well-formed TIM in `data`, found on 4-byte steps."""
    at = 0
    while at + 20 <= len(data):
        magic, flags = struct.unpack_from("<II", data, at)
        if magic == 0x10 and flags in (0, 1, 2, 3, 8, 9):
            p, rect = at + 8, None
            for _ in range(2 if flags & 8 else 1):
                if p + 12 > len(data):
                    rect = None
                    break
                length, x, y, w, h = struct.unpack_from("<IHHHH", data, p)
                if length != 12 + w * h * 2 or not w or not h or x + w > 1024 or y + h > 512 or p + length > len(data):
                    rect = None
                    break
                rect, p = (x, y, w, h), p + length
            if rect:
                yield at, p, rect
                at = p
                continue
        at += 4


def picture_spans(game) -> tuple[list[tuple[int, int, str]], list[tuple[int, int, str]]]:
    """([PE.IMG byte start, end, what] of picture data, [start, end, what] of everything else)."""
    pictures, other = [], []
    s = game.sectors
    for entry in range(len(s) - 1):
        start, end = s[entry] * SECTOR, s[entry + 1] * SECTOR
        if entry in WHOLE_ENTRIES:
            pictures.append((start, end, f"entry {entry}, {WHOLE_ENTRIES[entry]}"))
            continue
        data = game.pe(s[entry], s[entry + 1] - s[entry])
        last = 0
        for lo, hi, rect in tims(data):
            what = f"entry {entry}, picture at VRAM {rect[0]},{rect[1]}"
            if last < lo:
                other.append((start + last, start + lo, f"entry {entry}"))
            if rect == LETTERS_PAGE:
                other.append((start + lo, start + hi, f"{what}: the letters page"))
            else:
                pictures.append((start + lo, start + hi, what))
            last = hi
        if last < len(data):
            other.append((start + last, end, f"entry {entry}"))
    for number, (first, scratch, texture, data) in enumerate(game.rooms, 1):
        if not data:
            continue
        begin = first * SECTOR
        # Both runs are picture bundles: room_load.c sends every TIM in each to VRAM.
        pictures.append((begin, begin + (scratch + texture) * SECTOR, f"M{number:03} pictures"))
        other.append((begin + (scratch + texture) * SECTOR, begin + (scratch + texture + data) * SECTOR, f"M{number:03} data"))
    return pictures, other


def _changed(a: bytes, b: bytes) -> list[tuple[int, int]]:
    """[start, end) runs where `a` and `b` differ."""
    runs, i, n = [], 0, min(len(a), len(b))
    while i < n:
        if a[i] != b[i]:
            j = i + 1
            while j < n and a[j] != b[j]:
                j += 1
            runs.append((i, j))
            i = j
        else:
            i += 1
    return runs


def _pe_bytes(game, lo: int, hi: int) -> bytes:
    first = lo // SECTOR
    data = game.pe(first, (hi + SECTOR - 1) // SECTOR - first)
    return data[lo - first * SECTOR:hi - first * SECTOR]


def diff(original, translated) -> tuple[list[Piece], dict]:
    """The picture pieces a patched disc changes, and what else it changes (by what, in bytes)."""
    pictures, other = picture_spans(original)
    pieces: list[Piece] = []
    taken: dict[str, int] = {}
    for lo, hi, what in pictures:
        a, b = _pe_bytes(original, lo, hi), _pe_bytes(translated, lo, hi)
        if a == b:
            continue
        # One piece per sector the changes touch: first to last changed byte in it.
        by_sector: dict[int, list[int]] = {}
        for x, y in _changed(a, b):
            start = lo + x
            while start < lo + y:
                sector = start // SECTOR
                stop = min(lo + y, (sector + 1) * SECTOR)
                span = by_sector.setdefault(sector, [start, stop])
                span[0], span[1] = min(span[0], start), max(span[1], stop)
                start = stop
        for sector, (start, stop) in sorted(by_sector.items()):
            english = a[start - lo:stop - lo]
            pieces.append(Piece(sector, start - sector * SECTOR, zlib.crc32(english), b[start - lo:stop - lo]))
            taken[what] = taken.get(what, 0) + stop - start
    left: dict[str, int] = {}
    for lo, hi, what in other:
        a, b = _pe_bytes(original, lo, hi), _pe_bytes(translated, lo, hi)
        if a != b:
            left[what] = sum(y - x for x, y in _changed(a, b))
    return pieces, {"pictures_taken": taken, "changes_left_out": left}


def write(pieces: list[Piece]) -> bytes:
    """pictures.bin for these pieces: sorted, the same piece given twice kept once."""
    unique = sorted(set(pieces), key=lambda p: (p.sector, p.offset, p.original_crc, p.data))
    table, blob = bytearray(), bytearray()
    for p in unique:
        if not 0 < len(p.data) <= SECTOR - p.offset:
            raise ValueError(f"a piece of sector {p.sector} runs past its sector")
        table += PIECE.pack(p.sector, p.offset, len(p.data), p.original_crc, len(blob))
        blob += p.data
    out = HEADER.pack(MAGIC, VERSION, len(unique), HEADER.size + len(table)) + bytes(table) + bytes(blob)
    if len(out) > MAX_BYTES:
        raise ValueError("pictures.bin would be larger than the game reads")
    return out


def read(data: bytes) -> list[Piece]:
    """The pieces in a pictures.bin, checked as the game checks them."""
    if len(data) < HEADER.size:
        raise ValueError("pictures.bin is too short")
    magic, version, count, at = HEADER.unpack_from(data, 0)
    if magic != MAGIC or version != VERSION or at != HEADER.size + count * PIECE.size or at > len(data):
        raise ValueError("not a version 1 pictures.bin")
    pieces, last = [], None
    for i in range(count):
        sector, offset, length, crc, start = PIECE.unpack_from(data, HEADER.size + i * PIECE.size)
        if not length or offset + length > SECTOR or at + start + length > len(data) or (last and (sector, offset) < last):
            raise ValueError(f"piece {i} of pictures.bin is damaged")
        last = (sector, offset)
        pieces.append(Piece(sector, offset, crc, data[at + start:at + start + length]))
    return pieces
