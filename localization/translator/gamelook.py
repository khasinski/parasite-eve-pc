"""The Translator's "As in the game" preview: a dialogue page drawn as the game draws it, letter for
letter and pixel for pixel, over the background picture of the room that shows it.

Everything the game decides is followed here from its own code, so the picture is the game's:
- which letters a page draws, where each goes and how far the pen moves: the dialogue path of
  src/main/draw/text_render.c (ReadDialogue, Stretch, PeText_ResolveLetter, DialogueNative);
- the letters a pack brings and how they are drawn: mods/pe_lang (add_glyphs and install_widths in
  pe_lang.c, pe_lang_font.c, pe_lang_cyrillic.c, on_text_letter, the letter cache's pictures);
- the box: src/main/draw/textbox.c (Menu_DrawTextboxEntries: the default box's first line at
  20,174, 12 px a line; the letters' colour 0x80 grey, or 0xFF,0,0 between FB 04 and FB 05);
- the colours: a 5-bit value v is shown as v * 255 / 31 rounded to the nearest (measured on the
  game's own frames: every background pixel of the first box's room, the letters 247, their
  shadows 8).

From the player's disc (never kept anywhere else): the dialogue font page (PE.IMG entry 10), the
room backgrounds, and the room scripts, which say which rooms open each dialogue page.
Right-to-left packs (Hebrew, Arabic) are drawn as the box draws them (textbox.c Textbox_EmitLine,
PeText_LineFinish): each line read whole, joined (petext/rtl.py), put in drawing order, letters with
no code cut to their strokes where they join (pe_lang_letter_cut), the line ending at x 300.
Not drawn yet: button icons (FB 00-03, page 2), letters of the area font
page (FC, FD and the highest FB codes come from entry 45, the first area's: other areas differ),
styled boxes' backgrounds. The preview says when a page has any of these.
"""
from __future__ import annotations

import hashlib
import json
import struct
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

from . import project as P
from petext import cyrillic, gametext as gt, latin, rtl, tim as timlib
from petext.disc import Game

W, H = 320, 224                  # the game's screen
CELL, COLUMNS = 12, 21
FONT_ENTRY, AREA_FONT_ENTRY = 10, 45
LINE_X, LINE_Y, LINE_HEIGHT = 0x14, 0xAE, 12
RIGHT_EDGE = 320 - 0x14          # a right-to-left line in the default box ends here (Textbox_RightEdge)
GREY, RED = (0x80, 0x80, 0x80), (0xFF, 0, 0)
SPACE, QUESTION = 0x0F, 0x2C
LATIN_FIRST = 0x4D
CYRILLIC_FIRST, CYRILLIC_LAST = 0xBC, 0xEF
RECORD, DICT_RECORD = 80, 76
OP_DOTLESS, OP_ROTATE, OP_TALL = 1, 2, 4
# g_TextboxGlyphSpacing (src/main/text/tables.c): every left gap is 0, the right widths are these.
SPACING = gt.ADVANCE
OPCODES = P.ROOT / "data" / "script_opcodes.json"       # the opcodes room scripts have (no game text)

# The game's own letter table, kPeTextUnicode: what each one-byte code means with no pack table.
DEFAULT_UNICODE = [0] * 0xF1
for _code, _ch in gt.CHARS.items():
    DEFAULT_UNICODE[_code] = ord(_ch)
for _g in latin.GLYPHS:
    DEFAULT_UNICODE[_g.code] = ord(_g.char)
for _g in cyrillic.GLYPHS:
    DEFAULT_UNICODE[_g.code] = ord(_g.char)


def rgb_of(colour: int) -> tuple[int, int, int]:
    return tuple((((colour >> shift) & 31) * 255 + 15) // 31 for shift in (0, 5, 10))


class NotTheDisc(ValueError):
    pass


# ---- from the disc ----------------------------------------------------------------------------------

class DiscLook:
    """What the game look reads from the player's disc: the dialogue font page, the room pictures
    (on demand) and which rooms open each dialogue page (`shown_in`, a few seconds: call it off the
    window's thread). PE.IMG is the same on both discs, so either disc serves every room."""

    def __init__(self, path) -> None:
        self.path = Path(path)
        game = self._game()
        try:
            if game.region != "USA":
                raise NotTheDisc(f"{self.path.name} is a {game.region} disc; the preview needs a USA disc")
            s = game.sectors
            self.font = timlib.read(game.pe(s[FONT_ENTRY], s[FONT_ENTRY + 1] - s[FONT_ENTRY]))
            self.area_font = timlib.read(game.pe(s[AREA_FONT_ENTRY], s[AREA_FONT_ENTRY + 1] - s[AREA_FONT_ENTRY]))
        finally:
            game.disc.f.close()
        if self.font.bits != 4 or self.font.size != (256, 256):
            raise NotTheDisc(f"{self.path.name}: the dialogue font is not the game's")
        self.page = timlib.indices(self.font)
        self.area_page = timlib.indices(self.area_font)
        self._views: dict[int, list[bytes]] = {}
        self._shown: dict[str, list[int]] | None = None

    def _game(self) -> Game:
        try:
            return Game(self.path)
        except (OSError, ValueError, EOFError, KeyError, IndexError) as e:
            raise NotTheDisc(f"{self.path.name} is not a Parasite Eve disc image ({e})") from e

    def shown_in(self) -> dict[str, list[int]]:
        """dialogue key -> the rooms whose scripts open that page (as extract_text.py finds them)."""
        if self._shown is None:
            import extract_text as X
            from petext.script import task_block, walk
            valid = set(json.loads(OPCODES.read_text(encoding="utf-8")))
            shown, banks = defaultdict(set), {}
            game = self._game()
            try:
                for m in range(1, 451):
                    room = game.room(m)
                    if not room:
                        continue
                    payloads = X.bank_slots(room.data)
                    if 1 not in payloads:
                        continue
                    en = room.data[payloads[1]:]
                    pages, used = gt.pages(en)
                    key = banks.setdefault(hashlib.sha1(en[:used]).hexdigest(), f"M{m:03d}")
                    code, _ = walk(task_block(room.data), valid)
                    written = X.constant_writes(code)
                    for _script, _at, page, _box, _mode, reg in X.box_calls(code):
                        for p in [page] if page is not None else sorted(written.get(reg, ())):
                            if p in pages:
                                shown[f"dlg/{key}/{p:03d}"].add(m)
            finally:
                game.disc.f.close()
            self._shown = {k: sorted(v) for k, v in shown.items()}
        return self._shown

    def views(self, room: int) -> list[bytes]:
        """The room's background views as the screen shows them: 320 x 224 RGB each."""
        if room not in self._views:
            game = self._game()
            try:
                self._views[room] = room_views(game, room)
            finally:
                game.disc.f.close()
        return self._views[room]


def _uploads(data: bytes):
    """What Draw_LoadTimAsset sends to video memory for a room's picture bundle (scene_assets.h)."""
    directory = struct.unpack_from("<I", data, 4)[0]
    packed = struct.unpack_from("<I", data, directory + 0x28)[0]
    count, at = packed >> 22, packed & 0x3FFFFF
    for i in range(count):
        _, src, r, src2, r2 = struct.unpack_from("<5I", data, at + i * 20)
        off, h = src & 0xFFFFFF, (src >> 24) or 0x100
        yield off, (r >> 10) & 0x7FF, r >> 21, r & 0x3FF, h
        if src2 & 0xFFFFFF:
            yield off + (src2 & 0xFFFFFF), (r2 >> 10) & 0x7FF, r2 >> 21, r2 & 0x3FF, src2 >> 24


def room_views(game: Game, number: int) -> list[bytes]:
    """A room's 2D background views (include/pe1/background.h, Render_LoadMeshEntry): its picture
    bundles put in video memory, then each view's 16 px tiles from its geometry drawn at their
    places on the screen. [] for a room with none."""
    first, scratch, texture, size = game.rooms[number - 1]
    if not size:
        return []
    vram = bytearray(1024 * 512 * 2)
    for blob in (game.pe(first, scratch), game.pe(first + scratch, texture)):
        try:
            for off, x, y, w, h in _uploads(blob):
                if off + w * h * 2 > len(blob) or x + w > 1024 or y + h > 512:
                    continue
                for row in range(h):
                    to = ((y + row) * 1024 + x) * 2
                    vram[to:to + w * 2] = blob[off + row * w * 2:off + (row + 1) * w * 2]
        except struct.error:
            continue
    data = game.pe(first + scratch + texture, size)
    try:
        directory = struct.unpack_from("<I", data, 4)[0]
        packed = struct.unpack_from("<I", data, directory + 0x1C)[0]
        if not packed >> 22:
            return []
        geom = struct.unpack_from("<I", data, (packed & 0x3FFFFF) + 4)[0] & 0xFFFFFF
        count = struct.unpack_from("<H", data, geom + 6)[0]
        at = geom + struct.unpack_from("<i", data, geom + 0x14)[0]
    except struct.error:
        return []
    groups: dict[int, list] = defaultdict(list)
    for i in range(count):
        e = at + i * 0x38
        if not data[e] & 2:
            continue
        sx, sy = struct.unpack_from("<hh", data, e + 0xC)
        n = struct.unpack_from("<H", data, e + 0x26)[0]
        pos, uv = struct.unpack_from("<ii", data, e + 0x28)
        groups[data[e + 0x24]].append((n, sx, sy, e + pos, e + uv))
    out = []
    for _, entries in sorted(groups.items()):
        screen = bytearray(W * H * 3)
        for n, sx, sy, pos, uv in sorted(entries, key=lambda t: -t[0]):     # the big grid first
            for k in range(n):
                a = struct.unpack_from("<I", data, pos + k * 4)[0]
                w0 = struct.unpack_from("<I", data, uv + k * 8)[0]
                x, y = sx + (a >> 22), sy + ((a >> 12) & 1023)
                u, v = data[uv + k * 8 + 4], data[uv + k * 8 + 3]
                clut = ((w0 & 511) * 1024 + ((w0 >> 5) & 0x3F0)) * 2
                page_x, page_y = (w0 >> 10) & 0x3C0, (w0 >> 7) & 256
                for j in range(16):
                    if not 0 <= y + j < H:
                        continue
                    for i in range(16):
                        if not 0 <= x + i < W:
                            continue
                        uu = u + i
                        index = vram[((page_y + v + j) * 1024 + page_x + uu // 2) * 2 + (uu & 1)]
                        c = vram[clut + index * 2] | vram[clut + index * 2 + 1] << 8
                        if c:
                            p = ((y + j) * W + x + i) * 3
                            screen[p:p + 3] = bytes(rgb_of(c))
        out.append(bytes(screen))
    return out


# ---- from the pack: the font page as the language mod makes it ------------------------------------

def _rd32(b: bytes, at: int) -> int:
    return struct.unpack_from("<I", b, at)[0]


def _cell(page: list[int], code: int) -> list[int]:
    x0, y0 = code % COLUMNS * CELL, code // COLUMNS * CELL
    return [page[(y0 + i // CELL) * 256 + x0 + i % CELL] for i in range(CELL * CELL)]


def _put(page: list[int], code: int, cell: list[int]) -> None:
    x0, y0 = code % COLUMNS * CELL, code // COLUMNS * CELL
    if y0 + CELL > 256:
        return
    for i, v in enumerate(cell):
        page[(y0 + i // CELL) * 256 + x0 + i % CELL] = v


def _ink(s: list[int]):
    pts = [(i % CELL, i // CELL) for i, v in enumerate(s) if v == 1]
    if not pts:
        return None
    return min(p[0] for p in pts), max(p[0] for p in pts), min(p[1] for p in pts), max(p[1] for p in pts)


def letter_cell(r: bytes, base: list[int] | None) -> list[int]:
    """A letter record's picture: pe_lang_letter_compose."""
    if r[6] == 0xFF:
        return [r[8 + i // 2] >> ((i & 1) * 4) & 15 for i in range(CELL * CELL)]
    ops = r[7]
    marks = [(r[8 + k * 19], r[9 + k * 19], r[10 + k * 19],
              [struct.unpack_from("bb", r, 11 + k * 19 + 2 * p) for p in range(8)]) for k in range(3)]
    capital = 0x10 <= r[6] <= 0x29 or bool(ops & OP_TALL)
    s = [1 if v == 1 else 0 for v in (base or [0] * 144)]
    if ops & OP_DOTLESS:
        s = [0] * (3 * CELL) + s[3 * CELL:]
    if ops & OP_ROTATE:
        box = _ink(s)
        turned = [0] * 144
        if box:
            x0, x1, y0, y1 = box
            for i, v in enumerate(s):
                if v:
                    turned[(y0 + y1 - i // CELL) * CELL + (x0 + x1 - i % CELL)] = 1
        s = turned
    if capital and any(count and not fixed for count, fixed, _, _ in marks):
        s = latin._squash(s)
    ink = list(s)
    box = _ink(ink)
    if box:
        x0, x1, _, _ = box
        for count, fixed, anchor_kind, pts in marks:
            if not count:
                continue
            anchor = (x0 + x1) // 2
            if anchor_kind == 1:
                anchor = x0
            elif anchor_kind == 2:
                anchor = x1
            elif anchor_kind == 3:
                best = -1
                for x in range(CELL):
                    n = sum(ink[y * CELL + x] for y in range(CELL))
                    if n > best:
                        best, anchor = n, x
            top = 0 if capital else 1
            for dx, row in pts[:count]:
                x, y = anchor + dx, row if fixed else top + row
                if 0 <= x < CELL and 0 <= y < CELL:
                    s[y * CELL + x] = 1
    return latin._shadow(s)


@dataclass
class Pack:
    """A compiled pack as pe_lang_pack_parse reads it."""
    data: bytes
    version: int
    lang: str
    entries: dict[int, int]                  # hash -> where its text starts
    coded: list[bytes] = field(default_factory=list)
    extra: list[bytes] = field(default_factory=list)
    cjk: list[bytes] = field(default_factory=list)
    widths: bytes = b""
    stretches: list[list[int]] = field(default_factory=list)
    unicode: list[int] = field(default_factory=list)

    @classmethod
    def read(cls, data: bytes) -> "Pack":
        version = _rd32(data, 4)
        header = {4: 112, 3: 88, 2: 80}.get(version, 64)
        count, text = _rd32(data, 8), _rd32(data, 12)
        lang = data[16:32].split(b"\0")[0].decode("ascii", "replace")
        entries = {}
        for i in range(count):
            h, at = struct.unpack_from("<II", data, header + i * 8)
            entries[h] = text + at
        self = cls(data, version, lang, entries)
        if version >= 2:
            text_end, glyphs = _rd32(data, 64), _rd32(data, 68)
            self.cjk = [data[text_end + k * DICT_RECORD:text_end + (k + 1) * DICT_RECORD] for k in range(glyphs)]
        if version >= 3:
            at, n = _rd32(data, 80), _rd32(data, 84)
            for k in range(n):
                r = data[at + k * RECORD:at + (k + 1) * RECORD]
                extra = version == 4 and not r[4] and not (k == 0 and _rd32(r, 0) == ord("0"))
                (self.extra if extra else self.coded).append(r)
        if version == 4:
            at, runs, widths_at = _rd32(data, 88), _rd32(data, 92), _rd32(data, 100)
            starts = struct.unpack_from(f"<{runs + 1}I", data, at)
            pool = at + (runs + 1) * 4
            self.stretches = [list(struct.unpack_from(f"<{starts[k + 1] - starts[k]}I", data, pool + starts[k] * 4))
                              for k in range(runs)]
            if widths_at:
                self.widths = data[widths_at:widths_at + len(self.cjk)]
        self.unicode = list(DEFAULT_UNICODE)
        for r in self.coded:
            self.unicode[r[4]] = _rd32(r, 0)
        return self

    def text(self, key: str) -> bytes | None:
        at = self.entries.get(P.pelang.fnv1a(key))
        return None if at is None else self.data[at:self.data.index(b"\xff", at) + 1]

    def redraws(self, r: bytes) -> bool:
        return r[4] < 0xF1 and r[6] == 0xFF and _rd32(r, 0) == DEFAULT_UNICODE[r[4]]

    def uses_codes(self, first: int, last: int) -> bool:
        """pe_lang_pack_uses_glyphs."""
        for at in self.entries.values():
            p = at
            while self.data[p] != 0xFF:
                c = self.data[p]
                p += 1
                if c < 0xF1:
                    if first <= c <= last:
                        return True
                elif c == 0xF1:
                    p += 3
                elif c == 0xFB:
                    sub = self.data[p]
                    p += 1 + (sub in (7, 9))
                elif c in (0xFC, 0xFD):
                    p += 1
        wanted = {self.unicode[c] for c in range(first, last + 1)}
        return any(cp in wanted for run in self.stretches for cp in run)


class Font:
    """Font page 0 and the box's widths with a pack in use: add_glyphs and install_widths."""

    def __init__(self, disc: DiscLook, pack: Pack) -> None:
        page = list(disc.page)
        low = [r for r in pack.coded if r[4] < LATIN_FIRST]
        for r in low:                                    # the pack's own pictures of the game's letters
            _put(page, r[4], letter_cell(r, _cell(page, r[6]) if r[6] != 0xFF else None))
        right = list(SPACING) + [0] * (0xF1 - len(SPACING))
        for r in pack.coded:
            if r[4] < 78 and pack.redraws(r):
                right[r[4]] = r[5]
        base_right = right[:78]
        for g in latin.GLYPHS:                           # the Latin Extended letters, where cells are empty
            if not any(_cell(page, g.code)):
                _put(page, g.code, latin.compose(g, _cell(page, gt.CODE_OF[g.base]) if g.base else None))
        self.cyrillic = pack.uses_codes(CYRILLIC_FIRST, CYRILLIC_LAST)
        if self.cyrillic:
            for g in cyrillic.GLYPHS:
                if not any(_cell(page, g.code)):
                    _put(page, g.code, cyrillic.compose(g))
        for r in pack.coded[len(low):]:                  # a table's letter takes over its code's cell
            _put(page, r[4], letter_cell(r, _cell(page, r[6]) if r[6] != 0xFF else None))
        left = [0] * 0xF1
        for g in latin.GLYPHS:
            right[g.code] = base_right[gt.CODE_OF[g.base]] if g.base else latin._ink(latin.compose(g, None))[1] + 2
        if self.cyrillic:
            for g in cyrillic.GLYPHS:
                right[g.code] = g.advance
        for r in pack.coded:
            right[r[4]] = r[5] if r[6] == 0xFF else base_right[r[6]] + r[5]
        self.page, self.left, self.right, self.base_right = page, left, right, base_right
        self.original = disc.page
        self.pack = pack
        self.area_page = disc.area_page
        self.clut = disc.font.palettes[0]
        self.area_clut = disc.area_font.palettes[0]


# ---- placing the letters: text_render.c's dialogue path ---------------------------------------------

@dataclass
class Letter:
    x: int                       # from the pen
    pixels: list[int]            # 12 x 12 palette numbers
    w: int
    advance: int
    page: int = 0                # 0: font page 0's palette; 1: the area page's
    note: str = ""
    coded: bool = True           # a letter of the font page (else made in the letter cache: PE_TEXT_NO_CODE)


def _native(font: Font, code: int, kerning: bool) -> Letter | None:
    if code >= 0xF1:
        return None
    kern = 7 if kerning and code == SPACE else 0
    return Letter(-kern - font.left[code], _cell(font.page, code), 12, -kern - font.left[code] + font.right[code])


def _resolve(font: Font, letter: int, kerning: bool) -> Letter:
    """PeText_ResolveLetter with pe_lang's on_text_letter."""
    pack = font.pack
    for code in range(LATIN_FIRST, 0xF1):
        if pack.unicode[code] == letter:
            return _native(font, code, kerning)
    for r in pack.extra:
        if _rd32(r, 0) == letter:
            # made in the letter cache from the disc's own page, before the pack's letters (pe_lang_cjk.c)
            cell = letter_cell(r, _cell(font.original, r[6]) if r[6] != 0xFF else None)
            box = _ink(cell) if r[6] == 0xFF and rtl.is_joining(letter) else None
            if box:
                # a joined shape: cut to its strokes and shadow (pe_lang_letter_cut), the pen one past
                first, width = box[0], min(box[1] - box[0] + 2, CELL - box[0])
                moved = [cell[i + first] if i % CELL + first < CELL else 0 for i in range(CELL * CELL)]
                return Letter(0, moved, width, width + 1, coded=False)
            return Letter(0, cell, 12, r[5] if r[6] == 0xFF else SPACING[r[6]] + r[5], coded=False)
    for k, r in enumerate(pack.cjk):
        if _rd32(r, 0) == letter:
            return Letter(0, [r[4 + i // 2] >> ((i & 1) * 4) & 15 for i in range(144)], 12,
                          pack.widths[k] if k < len(pack.widths) else 12, coded=False)
    code = next((c for c in range(0x4D) if DEFAULT_UNICODE[c] == letter), QUESTION)
    return _native(font, code, kerning)


def _fixed(page_pixels: list[int], cell: int, page: int) -> Letter:
    return Letter(0, _cell(page_pixels, cell), 12, 12, page)


@dataclass
class Placed:
    x: int
    y: int
    letter: Letter
    colour: tuple[int, int, int]


@dataclass
class Look:
    screens: list[list[Placed]]
    notes: list[str]


@dataclass
class _Read:
    """A letter of a right-to-left line as the box read it (PeText_LineRead)."""
    uni: int
    letter: Letter | None        # None: resolved once the line is joined (a stretch's letter)
    colour: tuple[int, int, int]
    kerning: bool = True


def _finish_line(font: Font, line: list[_Read], right: int, y: int) -> list[Placed]:
    """PeText_LineFinish and Textbox_EmitLine: the line joined, put in drawing order (brackets
    turned), each letter's pen moved by its advance, less where a letter with no code joins one on
    its right; the whole line ending at `right`."""
    shapes, keep, nxt = rtl.join_letters([r.uni for r in line])
    kept: list[tuple[int, Letter, bool, _Read]] = []
    for r, shape, k, n in zip(line, shapes, keep, nxt):
        if not k:
            continue
        letter = r.letter if r.letter is not None and shape == r.uni else _resolve(font, shape, r.kerning)
        kept.append((shape, letter, n, r))
    order, turned = rtl.order("".join(chr(s) for s, _, _, _ in kept))
    letters = [l for _, l, _, _ in kept]
    for k, i in enumerate(order):
        if turned[k]:
            letters[i] = _resolve(font, ord(rtl.MIRROR[chr(kept[i][0])]), kept[i][3].kerning)
    out, pen = [], 0
    for k, i in enumerate(order):
        l = letters[i]
        advance = l.advance
        if k + 1 < len(order):
            r = order[k + 1]
            if r + 1 == i and kept[r][2] and not l.coded and not letters[r].coded:
                advance = min(advance, l.x + l.w - 1 - letters[r].x)     # the last column is the shadow
        out.append((pen + l.x, l, kept[i][3].colour))
        pen += advance
    left = right - pen
    return [Placed(left + x, y, l, colour) for x, l, colour in out]


def lay_out(font: Font, raw: bytes, name: bytes = b"", box_x: int = LINE_X, box_y: int = LINE_Y,
            digits: int = 5, numbers: tuple[int, ...] | None = None, right_to_left: bool = False,
            right: int = RIGHT_EDGE) -> Look:
    """A page's letters as Menu_DrawTextboxEntries places them, screen by screen (F8 starts the
    next). `name`: the player's name in the pack's letters (FA). `numbers`: the values the game puts
    in the page's number slots (Render_SetupColorTable), one per FB 08 in order, each drawn in its
    decimal digits 12 px apart; a slot past them ends the page, as the game stops drawing there.
    Without them, every FB 08 draws `digits` sample digits. `right_to_left`: a pack that reads
    right to left (rtl.is_rtl of its language): each line ends at `right` (_finish_line)."""
    screens, here, notes = [], [], set()
    slots = list(numbers) if numbers is not None else None
    x, y, colour = box_x, box_y, GREY
    line: list[_Read] = []
    i = 0

    def add(letter: Letter | None, uni: int | None = None, kerning: bool = True):
        nonlocal x
        if right_to_left:
            if letter is not None or uni is not None:
                line.append(_Read(uni if uni is not None else 0, letter, colour, kerning))
            return
        if letter is None:
            return
        here.append(Placed(x + letter.x, y, letter, colour))
        x += letter.advance

    def end_line():
        if right_to_left and line:
            here.extend(_finish_line(font, line, right, y))
            line.clear()

    while i < len(raw):
        b = raw[i]
        if b == 0xF1 and i + 3 < len(raw):
            kind, hi, lo = raw[i + 1], raw[i + 2], raw[i + 3]
            i += 4
            if kind == 0xC6 and hi * 240 + lo < len(font.pack.stretches):
                for cp in font.pack.stretches[hi * 240 + lo]:
                    if 0x10FE7F <= cp <= 0x10FEFF:
                        continue                         # narrowing and line-break marks: menus only
                    if right_to_left:
                        add(None, cp)                    # resolved once the line is joined
                    else:
                        add(_resolve(font, cp, True))
            elif kind == 0xC5 and hi * 240 + lo < len(font.pack.cjk):
                r = font.pack.cjk[hi * 240 + lo]
                add(Letter(0, [r[4 + k // 2] >> ((k & 1) * 4) & 15 for k in range(144)], 12, 12, coded=False),
                    _rd32(r, 0))
            continue
        if b in (0xF9, 0xFF):
            break
        if b == 0xF7:
            end_line()
            x, y = box_x, y + LINE_HEIGHT
            i += 1
            continue
        if b == 0xF8:
            end_line()
            screens.append(here)
            here, x, y = [], box_x, box_y
            i += 1
            continue
        if b == 0xFA:
            for c in name:
                add(_native(font, c, False), font.pack.unicode[c], False)
            i += 1
            continue
        if b == 0xFB and i + 1 < len(raw):
            n = raw[i + 1]
            i += 2
            if n <= 3:
                notes.add("a button icon (drawn as a blank)")
                if right_to_left:
                    add(Letter(0, [0] * 144, 12, 12), 0x20)
                x += 12
            elif n == 4:
                colour = RED
            elif n == 5:
                colour = GREY
            elif n in (7, 9):
                i += 1
            elif n == 8 and slots is not None:
                if not slots:
                    break
                for d in str(slots.pop(0)):
                    add(_fixed(font.page, int(d), 0))
            elif n == 8:
                for d in range(digits):
                    add(_fixed(font.page, (d + 1) % 10, 0))
            elif n > 9:
                c = n + 0xED
                if c < COLUMNS * 21:
                    add(_fixed(font.page, c, 0))
                else:
                    notes.add("a letter of the area font page (from the first area)")
                    add(_fixed(font.area_page, c - COLUMNS * 21, 1))
            continue
        if b in (0xFC, 0xFD) and i + 1 < len(raw):
            notes.add("a letter of the area font page (from the first area)")
            add(_fixed(font.area_page, raw[i + 1] + (0x34 if b == 0xFC else 0x134), 1))
            i += 2
            continue
        letter = _native(font, b, True)
        i += 1
        if letter is None:
            break
        add(letter, font.pack.unicode[b])
    end_line()
    screens.append(here)
    return Look(screens, sorted(notes))


def draw(font: Font, placed: list[Placed], background: bytes | None = None) -> bytes:
    """The screen, 320 x 224 RGB: the background (black when None) with the letters over it, each a
    12 x 12 sprite in its palette, colour 0 see-through, tinted by the box's colour (x * c / 128). The box queues its letters with AddPrim, so the first
    one queued is drawn last, in front of the next (textbox.c Textbox_EmitLetter)."""
    out = bytearray(background or bytes(W * H * 3))
    for p in reversed(placed):
        clut = font.clut if p.letter.page == 0 else font.area_clut
        for k, index in enumerate(p.letter.pixels):
            px, py = p.x + k % CELL, p.y + k // CELL
            if k % CELL >= p.letter.w or not (0 <= px < W and 0 <= py < H):
                continue
            c = clut[index] if index < len(clut) else 0
            if not c:
                continue
            rgb = rgb_of(c)
            at = (py * W + px) * 3
            out[at:at + 3] = bytes(min(255, v * m // 128) for v, m in zip(rgb, p.colour))
    return bytes(out)


def name_bytes(pack: Pack, name: str) -> bytes:
    """The player's name in the pack's one-byte letters (letters it can't write are left out)."""
    out = bytearray()
    for ch in name:
        code = next((c for c in range(0xF1) if pack.unicode[c] == ord(ch)), None)
        if code is not None:
            out.append(code)
    return bytes(out)


def can_draw(lang: str, menus: bool = False) -> str | None:
    """Why the game look can't draw this language yet, or None."""
    if rtl.is_rtl(lang) and menus:
        return "right-to-left menus are drawn with the computer font for now"
    return None


def can_draw_here() -> bool:
    """Whether this copy of the tool can show the game look (it makes its picture with Pillow)."""
    try:
        import PIL  # noqa: F401
        return True
    except ImportError:
        return False
