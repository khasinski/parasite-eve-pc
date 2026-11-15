"""The city map's PE.IMG entry (76): its text and its pictures.

The map reads the whole entry whenever it opens (FxCommon_LoadEffectSectors,
src/overlays/fx_common/camera.c). It starts with a table of four u32 part offsets from the entry's
start (Draw_GetTimTableEntry): parts 0 and 1 are the Japanese and the English message banks, and
the map hands part 1 to the dialogue box (Render_SetupFogLayer, effect_init.c) for the place title
shown on arrival and its narration; parts 2 and 3 are the map's effect data and pictures.

The banks are text, so a translation carries them as dlg/MAP rows (extract_text.py), which the
pe_lang mod finds by the English bank's CRC like any room's bank. Fan patches rewrote the banks in
place (moving the parts after them when a bank changed size), so a patch that changed only the text
leaves parts 2 and 3 byte for byte the game's.
"""
from __future__ import annotations

import re
import struct

ENTRY = 76
BANK_KEY = "MAP"
PARTS = 4
# Where the entry is on the USA discs (PE.IMG sectors, the same on Disc 1 and Disc 2; checked by
# localization/tests): how a translation file's pictures.bin is seen to still carry the map's text
# as bytes, the way packs made before the dlg/MAP rows did.
USA_SECTORS = (1975, 2016)

# The map's lines are centred by spaces before each line, and a place title sits on the box's
# third line, as the English has them. The box's pen starts at x 20 (style 3, textbox.c). The English
# placed its lines by eye; measured over its lines, the titles are centred on the screen's middle
# (x 160, 140 px on: 10 of its 14 title lines to the space) and the narration a little left of it
# (133 px on: 8 of its 9 lines). A line typed as words only (no line starting with a space, a title
# not starting with an empty line) is placed by place(); a line with its own spaces is kept as it is.
CENTRE = {"title": 140, "narration": 133}
TITLE_LINES = 3
_PAGE = re.compile(rf"dlg/{BANK_KEY}/(\d{{3}})")


def placed(key: str) -> str | None:
    """'title' (pages 1-10: the place shown on arrival), 'narration' (11-14), else None."""
    m = _PAGE.fullmatch(key)
    page = int(m[1]) if m else 0
    return "title" if 1 <= page <= 10 else "narration" if 11 <= page <= 14 else None


def _lines(cmds: list[bytes]) -> list[list[bytes]]:
    out, line = [], []
    for c in cmds:
        if c[0] == 0xF7:
            out.append(line)
            line = []
        else:
            line.append(c)
    out.append(line)
    return out


def words_only(key: str, cmds: list[bytes], space: bytes) -> bool:
    """Whether a page (its commands, validation.commands) is typed as words only: no line starts
    with a space, a title doesn't start with an empty line, and nothing opens a new screen."""
    if any(c[0] == 0xF8 for c in cmds):
        return False
    lines = _lines(cmds)
    if placed(key) == "title" and not lines[0]:
        return False
    return not any(line and line[0] == space for line in lines)


def place(key: str, cmds: list[bytes], space: bytes, measure) -> bytes:
    """A words-only page laid out as the English: each line with the spaces that put its middle
    nearest the screen's, and a title moved down to the box's third line. `measure(commands)` is
    how wide the box draws commands, `space` the space's code."""
    lines = _lines(cmds)
    step, centre = measure([space]), CENTRE[placed(key)]
    out = []
    for line in lines:
        drawn = measure(line)
        n = max(0, (2 * centre - drawn + step) // (2 * step)) if drawn and step else 0
        out.append(space * n + b"".join(line))
    if placed(key) == "title":
        out = [b""] * max(0, TITLE_LINES - len(out)) + out
    return b"\xf7".join(out)


def shown_words(key: str, text: str) -> str:
    """A map line's tagged text as a translator types it: the spaces before each line and a title's
    empty lines above it taken off ("{br}{br}    Soho" -> "Soho")."""
    if not placed(key):
        return text
    lines = [line.lstrip(" ") for line in text.split("{br}")]
    if placed(key) == "title":
        while len(lines) > 1 and not lines[0]:
            lines.pop(0)
    return "{br}".join(lines)


def entry_bytes(game) -> bytes:
    s = game.sectors
    return game.pe(s[ENTRY], s[ENTRY + 1] - s[ENTRY])


def parts(entry: bytes) -> tuple[int, ...]:
    """The four part offsets, checked to run forward inside the entry."""
    offsets = struct.unpack_from(f"<{PARTS}I", entry, 0)
    if offsets[0] != PARTS * 4 or list(offsets) != sorted(offsets) or offsets[-1] >= len(entry):
        raise ValueError(f"PE.IMG entry {ENTRY} is not the city map's table: {offsets}")
    return offsets


def english_bank(entry: bytes) -> bytes:
    """The English message bank and everything after it (gametext.pages reads up to its end)."""
    return entry[parts(entry)[1]:]


def pictures_part(entry: bytes) -> bytes:
    """Parts 2 and 3: what the map draws besides its text. A patch's entry may be shorter or
    longer than the game's where its banks changed size; what runs past the last part's own data
    is padding to the sector, left out."""
    return entry[parts(entry)[2]:]


def only_text_changed(original: bytes, patched: bytes) -> bool:
    """Whether a patched entry differs from the game's in its table and banks only: parts 2 and 3
    the same bytes, wherever the patch moved them (a patch's padding at the end aside)."""
    a, b = pictures_part(original), pictures_part(patched)
    n = min(len(a), len(b))
    return a[:n] == b[:n] and not any(a[n:]) and not any(b[n:])
