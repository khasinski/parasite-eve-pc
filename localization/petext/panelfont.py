"""Widths in the F1 panel's and toasts' real font (IBM Plex Sans), as pe_ui_text_width measures them."""
import re
import struct
from functools import lru_cache
from pathlib import Path

FONT_INC = Path(__file__).resolve().parents[2] / "src/main/boot/fonts/ibm_plex_sans.ttf.inc"
# Surfaces drawn in this font: size, and the room in design units.
# panel-help: footer, PANEL_W 700 less PANEL_PAD 30 each side (pe_mod_panel.c).
# toast: one line in a 4:3 window at the reference height of 820, less its 60 of padding (pe_toast.c).
SURFACES = {"panel-help": (15.0, 640), "toast": (17.0, 1033)}
TOAST_BYTES = 159                       # s_text[160] in pe_toast.c cuts the UTF-8 text here


@lru_cache(maxsize=1)
def _font():
    text = FONT_INC.read_text(encoding="ascii")
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})\b", text[text.index("{"):]))
    tables = {}
    for i in range(struct.unpack_from(">H", data, 4)[0]):
        tag, _, offset, _ = struct.unpack_from(">4sIII", data, 12 + i * 16)
        tables[tag] = offset
    units = struct.unpack_from(">H", data, tables[b"head"] + 18)[0]
    metrics = struct.unpack_from(">H", data, tables[b"hhea"] + 34)[0]
    hmtx, cmap = tables[b"hmtx"], tables[b"cmap"]
    glyphs = {}
    for i in range(struct.unpack_from(">H", data, cmap + 2)[0]):
        at = cmap + struct.unpack_from(">I", data, cmap + 8 + i * 8)[0]
        if struct.unpack_from(">H", data, at)[0] != 4:
            continue
        seg = struct.unpack_from(">H", data, at + 6)[0]
        ends, starts = at + 14, at + 16 + seg
        deltas, ranges = starts + seg, starts + seg * 2
        for s in range(0, seg, 2):
            end, start = struct.unpack_from(">H", data, ends + s)[0], struct.unpack_from(">H", data, starts + s)[0]
            delta, offset = struct.unpack_from(">H", data, deltas + s)[0], struct.unpack_from(">H", data, ranges + s)[0]
            for cp in range(start, end + 1):
                gid = struct.unpack_from(">H", data, ranges + s + offset + (cp - start) * 2)[0] if offset else cp
                if gid or not offset:
                    glyphs[cp] = (gid + delta) & 0xFFFF
        break
    advances = [struct.unpack_from(">H", data, hmtx + 4 * min(g, metrics - 1))[0] for g in range(metrics)]
    return units, glyphs, advances


def width(text: str, em: float) -> float:
    units, glyphs, advances = _font()
    return sum(advances[min(glyphs.get(ord(c), 0), len(advances) - 1)] for c in text) * em / units
