"""Two-byte name letters (include/pe1/text_render.h, mods/pe_lang/pe_lang_pack.c).

Names the game saves (the player's name, its default sys/030, and what the name entry grids
sys/115-117 offer) can hold any letter: one the game's English set has stays one byte, any other
becomes a two-byte name letter, a page byte FB-FE then a byte, as the Japanese game stored kana and
kanji. The game counts it as one letter, so a name still has up to 8. The pack lists each name
letter's Unicode letter; the language mod draws it. A save keeps the name in these letters, and
after a load the mod finds the pack that knows them (the one in use first).

Letter n is FB + n // 250, then n % 250: 1-999. The numbers mean the same letter everywhere, so a
saved name reads the same with every pack and every version of one:
- a letter in the shared list (petext/name_catalog.py, append only) has its place there as its
  number, in every pack;
- any other letter (Chinese characters, kanji) gets one of the pack's own numbers, 999 down to
  OWN_FLOOR. Building a new version with the old pack given keeps the old numbers, so names saved
  with the old version still read the same.
A pack lists the letters it uses, with 0 for the numbers it doesn't. Never used: 0 (the menus draw
FB 00 as a picture) and RESERVED, the codes the game's own menu text draws (FD 04, FE 04).
"""
from __future__ import annotations

from . import gametext as gt, stretch
from .name_catalog import CATALOG

GRIDS = ("sys/115", "sys/116", "sys/117")
DEFAULT_NAME = "sys/030"
KEYS = GRIDS + (DEFAULT_NAME,)
MAX_LETTERS = 999
NAME_LETTERS = 8                     # g_SaveMetadataWindowLength
NAME_BYTES = 12                      # Aya's name window: 13 bytes, its end byte included
GRID_CELLS = 99                      # the English grids' cells
RESERVED = frozenset((504, 754))     # FD 04 and FE 04: the game's own menu text draws them
OWN_FLOOR = 700                      # the pack's own numbers: 999 down to here; the shared list stays below


def _catalog_numbers() -> dict[str, int]:
    out, n = {}, 0
    for ch in CATALOG:
        n += 1
        while n in RESERVED:
            n += 1
        out[ch] = n
    if n >= OWN_FLOOR:
        raise AssertionError("the shared name letter list reaches the packs' own numbers")
    return out


SHARED = _catalog_numbers()
OWN_NUMBERS = [n for n in range(MAX_LETTERS, OWN_FLOOR - 1, -1) if n not in RESERVED]


def pair(n: int) -> bytes:
    if not 1 <= n <= MAX_LETTERS:
        raise ValueError(f"more than {MAX_LETTERS} different name letters")
    return bytes([0xFB + n // 250, n % 250])


class NameLetters:
    """The name letters a pack uses. `keep`: letter -> number from an older version of the pack,
    kept for letters outside the shared list (pelang.py build --keep-names-from)."""

    def __init__(self, keep: dict[str, int] | None = None) -> None:
        self.letters: list[str] = []
        self.numbers: dict[str, int] = {}
        self._keep = {ch: n for ch, n in (keep or {}).items()
                      if ch not in SHARED and ch not in gt.CODE_OF and n in OWN_NUMBERS}
        self._free = [n for n in OWN_NUMBERS if n not in set(self._keep.values())]
        self.kept = 0

    def code(self, ch: str) -> bytes:
        if ch in gt.CODE_OF:
            return bytes([gt.CODE_OF[ch]])
        if ch not in self.numbers:
            if ch in SHARED:
                n = SHARED[ch]
            elif ch in self._keep:
                n = self._keep[ch]
                self.kept += 1
            elif self._free:
                n = self._free.pop(0)
            else:
                raise ValueError(f"more than {len(OWN_NUMBERS)} name letters outside the shared list")
            self.numbers[ch] = n
            self.letters.append(ch)
        return pair(self.numbers[ch])

    def section(self) -> bytes:
        """u32 Unicode per number from 1 to the highest used, 0 for numbers this pack doesn't use."""
        table = [0] * max(self.numbers.values(), default=0)
        for ch, n in self.numbers.items():
            table[n - 1] = ord(ch)
        return b"".join(cp.to_bytes(4, "little") for cp in table)


def read_numbers(pack: bytes) -> dict[str, int]:
    """letter -> number of a compiled pack's name letters (empty for a pack without them)."""
    import struct
    if len(pack) < 112 or pack[:4] != b"PELG" or struct.unpack_from("<I", pack, 4)[0] < 4:
        return {}
    at, count = struct.unpack_from("<II", pack, 104)
    if at + count * 4 > len(pack):
        return {}
    out = {}
    for n in range(1, count + 1):
        cp = struct.unpack_from("<I", pack, at + (n - 1) * 4)[0]
        if cp:
            out[chr(cp)] = n
    return out


def letters_of(raw: bytes, letters: dict[bytes, str]) -> list:
    """Each letter of `raw` as a Unicode letter, or as its own bytes when it has none."""
    out, i = [], 0
    while i < len(raw):
        if raw[i] == 0xF1 and raw[i:i + 4] in letters:
            out.append(letters[raw[i:i + 4]]); i += 4
        elif raw[i:i + 1] in letters:
            out.append(letters[raw[i:i + 1]]); i += 1
        else:
            out.append(raw[i:i + 1]); i += 1
    return out


def convert_entries(entries: dict[str, bytes], atlas=None, keep: dict[str, int] | None = None
                    ) -> tuple[dict[str, bytes], NameLetters, list[str]]:
    """(entries with their name keys in name letters, the name letters, errors). `keep`: an older
    version's numbers (read_numbers) for letters outside the shared list."""
    names, letters, errors = NameLetters(keep), stretch.letter_map(atlas), []
    out = dict(entries)
    for key in KEYS:
        if key not in entries:
            continue
        parts = letters_of(entries[key], letters)
        try:
            raw = b"".join(p if isinstance(p, bytes) else names.code(p) for p in parts)
        except ValueError as e:
            errors.append(f"{key}: {e}")
            continue
        if key in GRIDS and len(parts) > GRID_CELLS:
            errors.append(f"{key}: {len(parts)} letters; a name entry grid has {GRID_CELLS} cells")
        if key == DEFAULT_NAME and (len(parts) > NAME_LETTERS or len(raw) > NAME_BYTES):
            errors.append(f"{key}: the default name must be at most {NAME_LETTERS} letters and "
                          f"{NAME_BYTES} bytes (two for each letter outside English)")
        out[key] = raw
    return out, names, errors
