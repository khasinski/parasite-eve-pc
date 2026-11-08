"""Two-byte name letters (include/pe1/text_render.h, mods/pe_lang/pe_lang_pack.c).

Names the game saves (the player's name, its default sys/030, and what the name entry grids
sys/115-117 offer) can hold any letter: one the game's English set has stays one byte, any other
becomes a two-byte name letter, a page byte FB-FE then a byte, as the Japanese game stored kana and
kanji. The game counts it as one letter, so a name still has up to 8. The pack lists each name
letter's Unicode letter once; the language mod draws it. A save keeps the name in these letters,
and after a load the mod finds the pack that knows them (the one in use first), so a new version of
a pack whose name letters move to other numbers changes the names saved with the old one.

Letter n (from 1) is FB + n // 250, then n % 250: up to 999 a pack. FB 00 stays unused, because the
menus draw that pair as a picture. Grid letters come first, in grid order.
"""
from __future__ import annotations

from . import gametext as gt, stretch

GRIDS = ("sys/115", "sys/116", "sys/117")
DEFAULT_NAME = "sys/030"
KEYS = GRIDS + (DEFAULT_NAME,)
MAX_LETTERS = 999
NAME_LETTERS = 8                     # g_SaveMetadataWindowLength
NAME_BYTES = 12                      # Aya's name window: 13 bytes, its end byte included
GRID_CELLS = 99                      # the English grids' cells


def pair(n: int) -> bytes:
    if not 1 <= n <= MAX_LETTERS:
        raise ValueError(f"more than {MAX_LETTERS} different name letters")
    return bytes([0xFB + n // 250, n % 250])


class NameLetters:
    def __init__(self) -> None:
        self.letters: list[str] = []
        self._ids: dict[str, int] = {}

    def code(self, ch: str) -> bytes:
        if ch in gt.CODE_OF:
            return bytes([gt.CODE_OF[ch]])
        if ch not in self._ids:
            self._ids[ch] = len(self.letters) + 1
            self.letters.append(ch)
        return pair(self._ids[ch])

    def section(self) -> bytes:
        return b"".join(ord(c).to_bytes(4, "little") for c in self.letters)


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


def convert_entries(entries: dict[str, bytes], atlas=None) -> tuple[dict[str, bytes], NameLetters, list[str]]:
    """(entries with their name keys in name letters, the name letters, errors)."""
    names, letters, errors = NameLetters(), stretch.letter_map(atlas), []
    out = dict(entries)
    for key in KEYS:
        if key not in entries:
            continue
        parts = letters_of(entries[key], letters)
        raw = b"".join(p if isinstance(p, bytes) else names.code(p) for p in parts)
        if key in GRIDS and len(parts) > GRID_CELLS:
            errors.append(f"{key}: {len(parts)} letters; a name entry grid has {GRID_CELLS} cells")
        if key == DEFAULT_NAME and (len(parts) > NAME_LETTERS or len(raw) > NAME_BYTES):
            errors.append(f"{key}: the default name must be at most {NAME_LETTERS} letters and "
                          f"{NAME_BYTES} bytes (two for each letter outside English)")
        out[key] = raw
    return out, names, errors
