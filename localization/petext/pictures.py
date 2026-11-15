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
  translation file's letters already replace;
- in a room whose picture bundles the patch changed, the colour row each 16 px tile of a picture
  draws with, when the patch picked new ones for its redrawn tiles (the German patch): changes in
  the room's third run before its message banks, taken only when every changed byte is such a
  row (a 16-bit field whose low 9 bits are a VRAM row of 448-511, the palettes), so no script
  change can come in with them.
  The run's first two words are left out when they only moved with the message banks
  (moved_header: a patch whose banks are another size, the French one); the pack keeps the game's.
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
LETTERS_AT = {(320, 0), (384, 0)}         # where the font entries' letter pages go in VRAM (petext/font.py)
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


def _first_bank(data: bytes) -> int:
    """Where a room's third run has its first message bank (extract_text.bank_slots), or its end."""
    try:
        d = struct.unpack_from("<I", data, 4)[0]
        ts = struct.unpack_from("<I", data, d + 0x20)[0]
        return min((struct.unpack_from("<I", data, (ts & 0x3FFFFF) + i * 8 + 4)[0] & 0xFFFFFF
                    for i in range(ts >> 22)), default=len(data))
    except struct.error:
        return 0


def _palette_rows(a: bytes, b: bytes) -> bool:
    """Every byte that differs is the low byte of a tile's colour row, 0x1C0-0x1FF (its high byte,
    with the row's top bit and the flags, the same in both)."""
    return all(k + 1 < len(a) and a[k + 1] == b[k + 1] and a[k + 1] & 1 and a[k] >= 0xC0 and b[k] >= 0xC0
               for x, y in _changed(a, b) for k in range(x, y))


def tile_palette_spans(original, translated) -> list[tuple[int, int, str]]:
    """[PE.IMG byte start, end, what] of each room's tile colour rows the patch changed (see the
    module's note), in rooms whose picture bundles it changed."""
    spans = []
    for number, (first, scratch, texture, data) in enumerate(original.rooms, 1):
        if not data:
            continue
        begin = first * SECTOR
        run = (begin + (scratch + texture) * SECTOR, begin + (scratch + texture + data) * SECTOR)
        if _pe_bytes(original, begin, run[0]) == _pe_bytes(translated, begin, run[0]):
            continue
        a, b = _pe_bytes(original, *run), _pe_bytes(translated, *run)
        cut = _first_bank(a)
        head = 8 if moved_header(a, b, cut) else 0
        if a[head:cut] != b[head:cut] and _palette_rows(a[head:cut], b[head:cut]):
            spans.append((run[0] + head, run[0] + cut, f"M{number:03} tile colour rows"))
    return spans


def moved_header(a: bytes, b: bytes, cut: int) -> bool:
    """Whether a room's first two words (`a` the game's third run, `b` the patch's) both point past
    its scripts (`cut`, its first message bank) and moved by the same amount: what follows the
    message banks moved because the patch's banks are another size. No script changes with them."""
    if len(a) < 8 or len(b) < 8 or a[:8] == b[:8]:
        return False
    before, after = struct.unpack_from("<2I", a), struct.unpack_from("<2I", b)
    return min(before + after) >= cut and before[0] - after[0] == before[1] - after[1]


def diff(original, translated) -> tuple[list[Piece], dict]:
    """The picture pieces a patched disc changes, and what else it changes (by what, in bytes)."""
    pictures, other = picture_spans(original)
    rows = tile_palette_spans(original, translated)
    pictures += rows
    split = []                                              # the rest of that run stays "data"
    for lo, hi, what in other:
        for start, end, _ in sorted(r for r in rows if lo <= r[0] < hi):
            if start > lo:
                split.append((lo, start, what))
            lo = end
        split.append((lo, hi, what))
    other = split
    pieces: list[Piece] = []
    taken: dict[str, int] = {}
    for lo, hi, what in pictures:
        a, b = _pe_bytes(original, lo, hi), _pe_bytes(translated, lo, hi)
        if a == b:
            continue
        for piece in _sector_pieces(lo, a, b):
            pieces.append(piece)
            taken[what] = taken.get(what, 0) + len(piece.data)
    left: dict[str, int] = {}
    for lo, hi, what in other:
        a, b = _pe_bytes(original, lo, hi), _pe_bytes(translated, lo, hi)
        if a != b:
            left[what] = sum(y - x for x, y in _changed(a, b))
    return pieces, {"pictures_taken": taken, "changes_left_out": left}


def _sector_pieces(lo: int, english: bytes, new: bytes) -> list[Piece]:
    """One piece per sector the changes touch, first to last changed byte in it. `english` (the bytes
    on the player's disc) and `new` (the patch's) both start at PE.IMG byte `lo`."""
    by_sector: dict[int, list[int]] = {}
    for x, y in _changed(english, new):
        start = lo + x
        while start < lo + y:
            sector = start // SECTOR
            stop = min(lo + y, (sector + 1) * SECTOR)
            span = by_sector.setdefault(sector, [start, stop])
            span[0], span[1] = min(span[0], start), max(span[1], stop)
            start = stop
    return [Piece(sector, start - sector * SECTOR, zlib.crc32(english[start - lo:stop - lo]), new[start - lo:stop - lo])
            for sector, (start, stop) in sorted(by_sector.items())]


def across(original, patched, other, font_entries=(), whole=()) -> tuple[list[Piece], dict]:
    """Picture pieces for another release's discs from a patch made for `original`'s: a fan patch for
    the Japanese discs, for a pack played on the USA discs (`other`).

    The two releases keep their data at different places, so nothing is matched by place. A TIM
    picture the patch redrew in a numbered entry is taken when that entry on the other disc holds
    the same picture, byte for byte (the title menu's words and the map's place names are English on
    the Japanese discs too): every copy of it there gets the patch's bytes.
    A picture the other disc draws differently is left out and reported, unless `whole` names it
    ("ENTRY/N": the entry's Nth TIM, from 0, on both discs): then the patch's picture goes in whole
    over the other disc's, which must be a TIM of the same kind and size at the same VRAM place.
    Not pictures here: the letters pages (LETTERS_PAGE anywhere, and the TIMs at LETTERS_AT in
    `font_entries`: a pack's letters replace them), and the entries that aren't bundles of TIMs
    (WHOLE_ENTRIES; on the Japanese discs entry 76 starts with a message bank). Rooms' pictures are
    not carried across."""
    pieces: list[Piece] = []
    taken: dict[str, int] = {}
    left: dict[str, str] = {}
    asked = set(whole)
    s, p, t = original.sectors, patched.sectors, other.sectors
    for entry in range(min(len(s), len(t)) - 1):
        if entry in WHOLE_ENTRIES:
            continue
        a, b = original.pe(s[entry], s[entry + 1] - s[entry]), patched.pe(p[entry], p[entry + 1] - p[entry])
        if a == b:
            continue
        theirs = None
        for n, (lo, hi, rect) in enumerate(tims(a)):
            if a[lo:hi] == b[lo:hi] or rect == LETTERS_PAGE or (entry in font_entries and rect[:2] in LETTERS_AT):
                continue
            what = f"entry {entry}, picture {n} at VRAM {rect[0]},{rect[1]}"
            if theirs is None:
                theirs = other.pe(t[entry], t[entry + 1] - t[entry])
                found = list(tims(theirs))
            base = t[entry] * SECTOR
            same = [l2 for l2, h2, _ in found if theirs[l2:h2] == a[lo:hi]]
            name = f"{entry}/{n}"
            if not same and name in asked:
                if n >= len(found) or found[n][2] != rect or found[n][1] - found[n][0] != hi - lo \
                        or theirs[found[n][0] + 4:found[n][0] + 8] != a[lo + 4:lo + 8]:
                    raise ValueError(f"picture {name} is not the same kind and size on the other disc")
                same = [found[n][0]]
            asked.discard(name)
            if not same:
                left[what] = "the other disc draws this picture differently"
                continue
            for l2 in same:
                for piece in _sector_pieces(base + l2, theirs[l2:l2 + hi - lo], b[lo:hi]):
                    pieces.append(piece)
                    taken[what] = taken.get(what, 0) + len(piece.data)
    if asked:
        raise ValueError(f"no picture the patch redrew is {', '.join(sorted(asked))}")
    return pieces, {"pictures_taken": taken, "pictures_left_out": left}


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
