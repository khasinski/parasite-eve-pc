"""The Translator's "As in the game" preview for the lines the menus draw: the menu's own window, drawn
as the game draws it, over the room picture darkened as the menus darken the scene, with the line in
the menus' letters at its place.

Followed from the game's own code, so the picture is the game's:
- the letters: src/main/draw/text_render.c (ReadMenu, MenuNative, Stretch, Squeeze, PackCell) and
  src/main/draw/text.c (Draw_EmitLetter: a quad from font page 0 at VRAM 320,0 in the colours at
  VRAM 448,34, Draw_TextToEnd's line breaks); a pack's letters and their menu metrics as
  mods/pe_lang sets them (install_widths, on_text_letter);
- the windows: src/main/menu/widget.c (MenuWidget_UpdateAndDraw), src/main/draw/wipe.c
  (Draw_AllocColorGradient, Draw_EmitWipeBar, Draw_AllocColorRect: 2 px bars, the top and left ones
  added to what is under them, the bottom and right ones taken off it, in the window colour, the
  side bars at half); where each window is and how big: the boot program's own window tables
  (MenuWidget_LookupSimpleDescriptor, read from the disc as src/main/asset/game_state.c does);
- the scene behind: the menus halve every colour of the scene (each 5-bit value v becomes v >> 1:
  measured on the game's own frames);
- the help line (Menu_DrawContextHelpText, save_screens.c): window 0x13, the line at +6,+5, the
  play-time clock at the window's right.

From the player's disc (never kept anywhere else): the font page (PE.IMG entry 10), the menus'
page (entry 9: the clock, the colours), the resident pack (glyph table, menu metrics) and the boot
program's window tables.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field
from fractions import Fraction

from . import gamelook as GL
from . import project as P
from petext import gametext as gt, latin, cyrillic
from petext.disc import Game

W, H = GL.W, GL.H
MENU_PAGE_ENTRY = 9                  # the menus' page: raw image at VRAM 448,0, 64 x 254 halfwords
MENU_COLOURS_ROW = 34                # Draw_EmitLetter's clut 0x89C = VRAM 448,34
GLYPHS, METRICS = 0x08, 0x28         # the resident pack's directory: glyph table, menu metrics
SIMPLE_TABLE, SIMPLE_COUNT = 0x80092478, 0x41      # g_MenuWidgetSimpleDescriptors in the boot program
GRID_TABLE = 0x80092888                             # g_MenuWidgetGridDescriptors, as many
ITEMS = 0x0C                         # the resident pack's directory: item records (0x20 bytes) up to 0x10
WINDOW_COLOUR = 0x404040             # the default window colour (Draw_BlendColor(0x404040), game_data.c)
TEXT_COLOUR, HINT_COLOUR = (0x80, 0x80, 0x80), (0x40, 0x80, 0x40)
DIM_COLOUR = (0x40, 0x40, 0x40)      # g_DrawColorShaded: dimmed letters (g_DrawTextDimmed) and sprites (g_DrawColorSelect)
SPRITE_PAGE = 0x100                  # a menu byte after FB: a sprite of the letter set (MenuNative)
NARROW_FIRST, NARROW_LAST, LINE_BREAK = 0x10FE80, 0x10FEFF, 0x10FE7F
MENU_ROOM = 10                       # the room drawn behind the menus (the first dialogue's, M010)


class NotTheDisc(GL.NotTheDisc):
    pass


# ---- from the disc ----------------------------------------------------------------------------------

class MenuDisc:
    """What the menus' look reads from the player's disc, once: the menus' page (entry 9) as video
    memory has it, the glyph table and menu metrics of the resident pack, and the window table."""

    def __init__(self, path) -> None:
        try:
            game = Game(path)
        except (OSError, ValueError, EOFError, KeyError, IndexError) as e:
            raise NotTheDisc(f"{path} is not a Parasite Eve disc image ({e})") from e
        try:
            if game.region != "USA":
                raise NotTheDisc("the preview needs a USA disc")
            s = game.sectors
            entry = game.pe(s[MENU_PAGE_ENTRY], s[MENU_PAGE_ENTRY + 1] - s[MENU_PAGE_ENTRY])
            resident = game.resident_pack()
            name = next((n for n in game.disc.root_files() if n.startswith("SLUS")), None)
            exe = game.disc.file(name) if name else b""
        finally:
            game.disc.f.close()
        from .artwork import _raw_images
        images = [r for r in _raw_images(entry) if r[1:] == (448, 0, 64, 254)]
        if len(images) != 1:
            raise NotTheDisc("the menus' page is not the game's")
        at = images[0][0]
        self.page = entry[at:at + 64 * 254 * 2]              # VRAM 448,0 .. 511,253, 128 bytes a row
        glyphs = struct.unpack_from("<i", resident, GLYPHS)[0]
        self.glyphs = [struct.unpack_from("<BBHBBB", resident, glyphs + 8 * k) for k in range(0x100)]
        metrics = struct.unpack_from("<i", resident, METRICS)[0]
        self.metrics = bytes(resident[metrics:metrics + 0x1B9])
        if len(exe) < 0x800:
            raise NotTheDisc("no boot program on the disc")
        base = struct.unpack_from("<I", exe, 24)[0]
        self.windows = [struct.unpack_from("<4i", exe, 0x800 + SIMPLE_TABLE - base + 16 * k)
                        for k in range(SIMPLE_COUNT)]
        # g_MenuWidgetGridDescriptors: x, y, gridWidth, visibleRows, yLimit, cell width, row height, flags
        self.grids = [struct.unpack_from("<8i", exe, 0x800 + GRID_TABLE - base + 32 * k)
                      for k in range(SIMPLE_COUNT)]
        items, end = struct.unpack_from("<ii", resident, ITEMS)
        self.items = [bytes(resident[at:at + 32]) for at in range(items, end - 31, 32)]

    def word(self, x: int, y: int) -> int:
        """Video memory halfword (x, y) of the menus' page."""
        return struct.unpack_from("<H", self.page, ((y * 64) + x - 448) * 2)[0]

    def colours(self, clut: int) -> list[int]:
        x, y = (clut & 63) * 16, clut >> 6
        return [self.word(x + i, y) for i in range(16)]

    def index(self, u: int, v: int) -> int:
        """A 4-bit pixel of the page at texture (u, v) (tpage 448,0)."""
        b = self.page[v * 128 + u // 2]
        return b >> 4 if u & 1 else b & 15


# ---- the letters ------------------------------------------------------------------------------------

@dataclass
class MenuLetter:
    x: int                       # from the pen
    y: int
    pixels: list[int]            # w x 12 palette numbers, row by row
    w: int                       # columns of the picture
    drawn: int                   # columns on the screen (PE_TEXT_DRAWN_W: fewer when squeezed)
    advance: int
    width: int                   # what the menus' measuring counts (Draw_MeasureTextWidth)
    colours: str = "menu"        # "menu": the menus' colours; "font": the dialogue font's (letter cache)


class MenuFont:
    """Font page 0 and the menu metrics with a pack in use (gamelook.Font plus install_widths' metrics)."""

    def __init__(self, font: GL.Font, menu: MenuDisc) -> None:
        self.font, self.menu, self.pack = font, menu, font.pack
        metrics = bytearray(menu.metrics)
        base = bytes(metrics)

        def letter_metric(r: bytes, base_metrics: bytes) -> int:      # pe_lang_letter_metric
            if r[6] == 0xFF:
                return r[7] if r[7] else (r[5] << 4) & 0xFF
            width = (base_metrics[r[6]] >> 4) + r[5]
            return (min(width, 15) << 4) | (base_metrics[r[6]] & 15)

        coded = self.pack.coded
        for r in coded:
            if r[4] < GL.LATIN_FIRST:
                metrics[r[4]] = letter_metric(r, metrics)
        for g in latin.GLYPHS:                                         # pe_lang_font_metric
            if g.base:
                metrics[g.code] = metrics[gt.CODE_OF[g.base]]
            else:
                metrics[g.code] = min(latin._ink(latin.compose(g, None))[1] + 2, 15) << 4
        if font.cyrillic:
            for g in cyrillic.GLYPHS:
                metrics[g.code] = (g.advance << 4) & 0xFF
        for r in coded:
            metrics[r[4]] = letter_metric(r, metrics)
        self.metrics = bytes(metrics)
        self.letter_metric = letter_metric
        self.colours = menu.colours((MENU_COLOURS_ROW << 6) | (448 >> 4))

    def native(self, glyph: int) -> MenuLetter:
        """MenuNative: a letter cut from the font page at its first column, its width wide."""
        if glyph >= 0x101:
            glyph -= 0x13
        glyph %= 0x1B9
        m = self.metrics[glyph]
        width, first = m >> 4, m & 15
        spacing = (1 if glyph < 10 or glyph == 15 else 0) + 1
        u, v = glyph % 21 * 12 + first, glyph // 21 * 12
        page = self.font.page
        pixels = [page[(v + j) * 256 + u + i] if v + j < 256 and u + i < 256 else 0
                  for j in range(12) for i in range(width)]
        return MenuLetter(spacing >> 1, 1, pixels, width, width, width + spacing, width + spacing)

    def resolve(self, letter: int) -> MenuLetter:
        """PeText_ResolveLetter for a menu, with pe_lang's on_text_letter."""
        pack = self.pack
        for code in range(GL.LATIN_FIRST, 0xF1):
            if pack.unicode[code] == letter:
                return self.native(code)
        for r in pack.extra:
            if GL._rd32(r, 0) == letter:
                cell = GL.letter_cell(r, GL._cell(self.font.original, r[6]) if r[6] != 0xFF else None)
                metric = self.letter_metric(r, self.menu.metrics)
                if (metric & 15) >= 12 or not metric >> 4:
                    metric = 0
                first, width = (metric & 15, min(metric >> 4, 12 - (metric & 15))) if metric else (0, 12)
                advance = (metric >> 4) + 1 if metric else 12
                return self._cell(cell, first, width, advance)
        for k, r in enumerate(pack.cjk):
            if GL._rd32(r, 0) == letter:
                cell = [r[4 + i // 2] >> ((i & 1) * 4) & 15 for i in range(144)]
                return self._cell(cell, 0, 12, pack.widths[k] if k < len(pack.widths) else 12)
        code = next((c for c in range(GL.LATIN_FIRST) if GL.DEFAULT_UNICODE[c] == letter), GL.QUESTION)
        return self.native(code)

    @staticmethod
    def _cell(cell: list[int], first: int, width: int, advance: int) -> MenuLetter:
        """PackCell: a letter of the letter cache (the dialogue font's colours), one pixel down."""
        pixels = [cell[j * 12 + first + i] for j in range(12) for i in range(width)]
        return MenuLetter(0, 1, pixels, width, width, advance, advance, "font")


def _take(cap: list[int], cut: list[int], need: int) -> int:
    while need > 0:
        m = sum(1 for c in cap if c > 0)
        if not m:
            break
        take = min(need, m)
        j = 0
        for i in range(len(cap)):
            if cap[i] <= 0:
                continue
            if ((j + 1) * take) // m > (j * take) // m:
                cap[i] -= 1
                cut[i] += 1
            j += 1
        need -= take
    return need


def _squeeze(letters: list[MenuLetter], space: list[bool], pen: int, narrow: int) -> int:
    """Squeeze (text_render.c): a narrowed line's room taken from its blanks first."""
    n = len(letters)
    need = pen - ((pen * narrow + 128) >> 8)
    start, at = [], 0
    for l in letters:
        start.append(at)
        at += l.advance
    cut, art = [0] * n, [0] * n
    cap = [l.advance - 4 if space[i] and l.advance > 4 else 0 for i, l in enumerate(letters)]
    need = _take(cap, cut, need)
    cap = []
    for i, l in enumerate(letters):
        gap = l.advance - (l.x - start[i]) - l.w
        cap.append(gap if not space[i] and gap > 0 else 0)
    need = _take(cap, cut, need)
    cap = [l.advance - cut[i] - 2 if space[i] and l.advance - cut[i] > 2 else 0 for i, l in enumerate(letters)]
    need = _take(cap, cut, need)
    cap = [l.w - 1 if not space[i] and l.w > 1 else 0 for i, l in enumerate(letters)]
    _take(cap, art, need)
    at = 0
    for i, l in enumerate(letters):
        l.x = at + (l.x - start[i])
        if art[i]:
            l.drawn = l.w - art[i]
        at += l.advance - cut[i] - art[i]
    return at


@dataclass
class Placed:
    x: int
    y: int
    letter: MenuLetter
    colour: tuple


@dataclass
class Text:
    placed: list[Placed] = field(default_factory=list)
    width: int = 0               # as the menus measure it (first line)
    notes: set = field(default_factory=set)


def lay_out(mf: MenuFont, raw: bytes, x: int, y: int, colour=TEXT_COLOUR) -> Text:
    """Draw_PrintRawText: the menu letters of `raw` (to FF) from the cursor (x, y); a line break
    starts the next line 12 px lower at x."""
    out, pen, line_x, line_y, mode, i = Text(), x, x, y, 0, 0
    first_line = True
    pack = mf.pack
    while i < len(raw) and raw[i] != 0xFF:
        b = raw[i]
        if b == 0xF1 and i + 3 < len(raw) and raw[i + 1] == 0xC6 and raw[i + 2] < 240 and raw[i + 3] < 240:
            run = pack.stretches[raw[i + 2] * 240 + raw[i + 3]] if raw[i + 2] * 240 + raw[i + 3] < len(pack.stretches) else None
            i += 4
            mode = 0
            if run is None:
                continue
            if run == [LINE_BREAK]:
                line_y += 12
                pen = line_x
                first_line = False
                continue
            letters, space, narrow, at = [], [], 256, 0
            for cp in run:
                if NARROW_FIRST <= cp <= NARROW_LAST:
                    narrow = cp - NARROW_FIRST + 128
                    continue
                if cp == LINE_BREAK:
                    continue
                l = mf.resolve(cp)
                space.append(cp == 0x20)
                l.x += at
                at += l.advance
                letters.append(l)
            if narrow != 256:
                out.notes.add("narrowed letters (drawn here column by column, a little differently)")
                at = _squeeze(letters, space, at, narrow)
            for l in letters:
                out.placed.append(Placed(pen + l.x, line_y + l.y, l, colour))
            pen += at
            if first_line:
                out.width += at
            continue
        if b == 0xF1 and i + 3 < len(raw) and raw[i + 1] == 0xC5:          # a dictionary letter
            k = raw[i + 2] * 240 + raw[i + 3]
            i += 4
            mode = 0
            if k < len(pack.cjk):
                r = pack.cjk[k]
                cell = [r[4 + j // 2] >> ((j & 1) * 4) & 15 for j in range(144)]
                l = MenuFont._cell(cell, 0, 12, pack.widths[k] if k < len(pack.widths) else 12)
                out.placed.append(Placed(pen + l.x, line_y + l.y, l, colour))
                pen += l.advance
                out.width += l.width if first_line else 0
            continue
        i += 1
        if b >= 0xFA:
            mode = b - 0xFA
            continue
        glyph = b + (mode << 8)
        mode = 0
        if glyph == SPRITE_PAGE:              # MenuNative's sprite: 12 px on; measured as glyph 0xED
            out.notes.add("a picture in the line (drawn as a blank)")
            pen += 12
            if first_line:
                out.width += (mf.metrics[SPRITE_PAGE - 0x13] >> 4) + 1
            continue
        l = mf.native(glyph)
        out.placed.append(Placed(pen + l.x, line_y + l.y, l, colour))
        pen += l.advance
        if first_line:
            out.width += l.width
    return out


# ---- the windows ------------------------------------------------------------------------------------

def _inside(p, x: int, y: int) -> bool:
    """Whether the GPU fills pixel (x, y) for triangle p. Matched against the game's own frames: the
    sample point is (x + 1/2, y); a point exactly on an edge counts only on a bottom or right edge."""
    (x0, y0), (x1, y1), (x2, y2) = p
    if (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0) < 0:
        (x1, y1), (x2, y2) = (x2, y2), (x1, y1)
    pts = ((x0, y0), (x1, y1), (x2, y2))
    px, py = Fraction(2 * x + 1, 2), y
    for k in range(3):
        (ax, ay), (bx, by) = pts[k], pts[(k + 1) % 3]
        e = (bx - ax) * (py - ay) - (by - ay) * (px - ax)
        if e < 0:
            return False
        if e == 0 and ((ay == by and bx < ax) or by < ay):
            return False
    return True


def _triangle(p) -> list[tuple[int, int]]:
    (x0, y0), (x1, y1), (x2, y2) = p
    if (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0) == 0:
        return []
    return [(x, y) for y in range(min(y0, y1, y2), max(y0, y1, y2) + 1)
            for x in range(min(x0, x1, x2) - 1, max(x0, x1, x2) + 1) if _inside(p, x, y)]


def bevel(x: int, y: int, w: int, h: int, colour: int = WINDOW_COLOUR, mode: int = 0) -> list[tuple[list, tuple, int]]:
    """A window's bars (Draw_AllocColorGradient over a rectangle, g_WipeRectEdgeList): [(pixels, rgb,
    sign)], sign +1 added to what is under, -1 taken off it, in drawing order. `mode` picks the colour
    of g_DrawBlendColorPair (1: the list cursor's bars while it blinks, Draw_AllocColorTriGradient)."""
    corners = [(x, y), (x + w, y), (x, y + h), (x + w, y + h)]
    pair = [colour & 0xFFFFFF, _halves(colour)]
    out = []
    for edges, width, sign in (([(0, 1), (0, 2)], 2, 1), ([(1, 3), (2, 3)], -2, -1)):
        for i, j in edges:                                   # Draw_AllocColorRect
            a, b = list(corners[i]), list(corners[j])
            fa, fb, mag = list(a), list(b), abs(width)
            c = pair[mode]
            if a[1] == b[1]:
                fa[0] += mag
                fb[0] -= mag
                fa[1] += width
                fb[1] += width
            else:
                fa[0] += width
                fb[0] += width
                fa[1] += mag
                fb[1] -= mag
                c = (c >> 1) & 0x7F7F7F
            pixels = set(_triangle((tuple(a), tuple(b), tuple(fa)))) | set(_triangle((tuple(fb), tuple(fa), tuple(b))))
            out.append((sorted(pixels), (c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF), sign))
    return out


def _halves(colour: int) -> int:
    """Draw_BlendColor's second colour (unused by rectangles; kept for the windows with points)."""
    r, g, b = colour & 0xFF, (colour >> 8) & 0xFF, (colour >> 16) & 0xFF
    return min((b + g) >> 1, 255) | min((b + r) >> 1, 255) << 8 | min((g + r) >> 1, 255) << 16


# ---- the screen -------------------------------------------------------------------------------------

def _v5(c8: int) -> int:
    """A screen colour back to its 5-bit value (gamelook.rgb_of's inverse)."""
    return (c8 * 31 + 127) // 255


class Screen:
    """A 320 x 224 screen in 5-bit colour values, drawn into as the GPU does."""

    def __init__(self, background: bytes | None, dim: bool = True) -> None:
        self.v = bytearray(W * H * 3)
        self.inked: set[tuple[int, int]] = set()     # pixels a texel was put on (nothing shows through)
        if background:
            for k in range(W * H * 3):
                v = _v5(background[k])
                self.v[k] = v >> 1 if dim else v

    def blend(self, pixels, rgb: tuple, sign: int) -> None:
        add = [c >> 3 for c in rgb]
        for x, y in pixels:
            if 0 <= x < W and 0 <= y < H:
                at = (y * W + x) * 3
                for k in range(3):
                    self.v[at + k] = max(0, min(31, self.v[at + k] + sign * add[k]))

    def put(self, x: int, y: int, colour: int, tint: tuple = (0x80, 0x80, 0x80)) -> None:
        """A texel (a 15-bit colour, 0 = see-through) modulated by the quad's colour (x * c >> 7)."""
        if not colour or not (0 <= x < W and 0 <= y < H):
            return
        self.inked.add((x, y))
        at = (y * W + x) * 3
        for k, shift in enumerate((0, 5, 10)):
            self.v[at + k] = min(31, (((colour >> shift) & 31) * tint[k]) >> 7)

    def rgb(self) -> bytes:
        return bytes((v * 255 + 15) // 31 for v in self.v)


def draw_text(screen: Screen, mf: MenuFont, text: Text) -> None:
    dialogue = mf.font.clut
    for p in text.placed:
        l = p.letter
        colours = mf.colours if l.colours == "menu" else dialogue
        for j in range(12):
            for i in range(l.drawn):
                src = i if l.drawn == l.w else (i * l.w) // l.drawn
                index = l.pixels[j * l.w + src]
                screen.put(p.x + i, p.y + j, colours[index] if index < len(colours) else 0, p.colour)


def draw_sprite(screen: Screen, menu: MenuDisc, glyph: int, x: int, y: int, tint=TEXT_COLOUR) -> None:
    """Draw_AllocSprite: a glyph of the glyph table, 1:1 (4-bit glyphs on the menus' page)."""
    u, v, clut, w, h, mode = menu.glyphs[glyph]
    colours = menu.colours(clut)
    for j in range(h):
        for i in range(w):
            screen.put(x + i, y + j, colours[menu.index(u + i, v + j)], tint)


DIGITS_SMALL = (0x3A1C, 0xCC)        # Draw_SetFontVariant(1): the clock's digits (clut, first u)
DIGITS_PLAIN = (0x395D, 0x84)        # Draw_SetFontVariant(0)


def draw_digit(screen: Screen, menu: MenuDisc, digit: int, x: int, y: int, font=DIGITS_SMALL,
               tint=TEXT_COLOUR) -> None:
    """Draw_EmitDigitSprite (PeText_MenuDigit): a 5 x 7 digit at v 0xA4, or for -1 the blank at 88,164."""
    colours = menu.colours(font[0])
    u, v = (font[1] + (digit % 10) * 5, 0xA4) if digit >= 0 else (88, 164)
    for j in range(7):
        for i in range(5):
            screen.put(x + i, y + j, colours[menu.index(u + i, v + j)], tint)


def tile_add(x: int, y: int, w: int, h: int) -> list[tuple[int, int]]:
    """The pixels of a flat tile (Draw_AllocColorTri: added to what is under it, tpage 0x20)."""
    return [(i, j) for j in range(y, y + h) for i in range(x, x + w)]


def draw_clock(screen: Screen, menu: MenuDisc, x: int, y: int, seconds: int = 0) -> list[tuple[int, int, int, int]]:
    """Menu_DrawContextHelpText's clock from (x, y) (the window's corner + its width - 0x27, +1):
    Draw_PrintTimeValue(seconds, 1), which leaves the pen 31 px on, then the clock's frame 0x21 px
    back from there. The frame is put in the list last, so the GPU draws it first, under the digits.
    Returns the digits' boxes."""
    value = min(seconds, 359999)
    draw_sprite(screen, menu, 0x8C, x + 31 - 0x21, y)
    boxes, pen = [], x
    for kind, n in (("d", value // 36000), ("d", value // 3600), (":", 0), ("d", (value // 600) % 6),
                    ("d", value // 60), (":", 0), ("d", (value // 10) % 6), ("d", value)):
        if kind == ":":
            draw_sprite(screen, menu, 0x9B, pen, y)
            pen += 3
        else:
            draw_digit(screen, menu, n, pen, y)
            boxes.append((pen, y, 5, 7))
            pen += 5
    return boxes


# ---- the places the menus draw a line ---------------------------------------------------------------

HELP_WINDOW = 0x13
HINT_KEYS = ("sys/014", "sys/015")           # drawn in Draw_SetColor(0x408040)


@dataclass
class Look:
    rgb: bytes
    where: str
    notes: list[str]
    digits: list = field(default_factory=list)   # the clock's digit boxes (the time differs from the game's)
    text: Text | None = None
    top: int = 0                                 # the first row worth showing
    inked: set = field(default_factory=set)      # pixels drawn over (nothing of the scene shows)
    focus: tuple | None = None                   # rows (first, past the last) the preview should keep in view


def context_of(key: str) -> str | None:
    """Which place of the menus draws this line, from data/menu_layouts.json, as far as this preview
    draws it."""
    import fnmatch
    for c in _layouts()["contexts"]:
        if any(fnmatch.fnmatchcase(key, k) or _in_range(key, k) for k in c["keys"]):
            return c["id"]
    return None


_LAYOUTS: dict | None = None


def _layouts() -> dict:
    global _LAYOUTS
    if _LAYOUTS is None:
        from petext import menu as M
        _LAYOUTS = M.load_layouts()
    return _LAYOUTS


def _in_range(key: str, pattern: str) -> bool:
    """'misc/000-088' style ranges in menu_layouts.json."""
    if "-" not in pattern.rsplit("/", 1)[-1]:
        return False
    table, span = pattern.rsplit("/", 1)
    first, last = span.split("-")
    if not key.startswith(table + "/"):
        return False
    try:
        n = int(key.rsplit("/", 1)[1])
    except ValueError:
        return False
    return int(first) <= n <= int(last)


def draw_help(screen: Screen, mf: MenuFont, raw: bytes | None, key: str, seconds: int = 0):
    """Window 0x13 with its clock and the line (none when raw is None): (the line, the clock's digit boxes)."""
    menu = mf.menu
    x, y, w, h = menu.windows[HELP_WINDOW]
    for pixels, rgb, sign in bevel(x, y, w, h):
        screen.blend(pixels, rgb, sign)
    digits = draw_clock(screen, menu, x + w - 0x27, y + 1, seconds)
    text = None
    if raw is not None:
        text = lay_out(mf, raw, x + 6, y + 5, HINT_COLOUR if key in HINT_KEYS else TEXT_COLOUR)
        draw_text(screen, mf, text)
    return text, digits


def help_line(mf: MenuFont, raw: bytes, key: str, background: bytes | None, seconds: int = 0) -> Look:
    """The help line at the top of the menus (descriptions, hints), as Menu_DrawContextHelpText draws it."""
    screen = Screen(background)
    text, digits = draw_help(screen, mf, raw, key, seconds)
    return Look(screen.rgb(), "the help line at the top of the menus", sorted(text.notes), digits, text)


# The item list (Menu_DrawSelectableEquipSlotList, inventory.c): window 0x01 holding grid 0x01, two
# columns of 124 x 16 cells; each cell as Menu_DrawSlotRow (active_list.c) draws it: the item's icon,
# its name 18 px on, and for guns and crates a count box 86 px after the name starts (a sprite, then
# the rounds 5 px lower in three digits). An equipped item's cell gets a blue tile behind it
# (Draw_DropShadow, 0x7F2000 added), a gun with every round loaded a mark over its icon (sprite 0x69).
# The cursor's cell gets the window's bars and a tile added whose grey pulses (Draw_AllocColorTriGradient).
ITEM_WINDOW = ITEM_GRID = 0x01
ITEM_SLOTS = 10                      # Aya's slots at the start (Inv_GetAyaSlotLimit): the window shows 5 rows
# Aya's items at the start of the game, slot by slot, as the game's own item screen shows them on the
# first visit to the menus (a new game, in front of Carnegie Hall): item record, equipped.
START_ITEMS = ((67, True), (149, True), (62, False), (25, False), (5, False))
START_ROUNDS = {25: 6}               # rounds that are not the item record's: the Ammo Crate holds 6
CURSOR_SLOT = 4                      # where the item being translated goes when it isn't one of them
EQUIPPED_TILE = 0x7F2000
FULL_MARK, COUNT_X, NAME_X = 0x69, 0x12 + 0x56, 0x12
PULSE = 0x20                         # the cursor tile's grey at the middle of its pulse (0..0x40)
# On this screen the game draws the icons, count boxes and digits in the dimmed colour (g_DrawColorSelect
# is set: measured on its frames, every sprite of the list at half), and a name dimmed unless the item
# can be used now (Menu_DrawSelectableEquipSlotRow, Inv_RebuildSelectableMask): flag bit 0, and not a
# Medicine (item ids 6-10) while HP is full, as it is at the start.


def usable(rec: bytes) -> bool:
    """Whether the start list draws this item's name undimmed (Inv_RebuildSelectableMask, outside
    battle; Aya has a weapon and armour, so the consumables that need one keep their bit)."""
    return bool(rec[5] & 1) and not 6 <= rec[4] <= 10


def item_key(k: int) -> str:
    return f"item/{k:03d}"


def count_sprite(kind: int) -> int | None:
    """The count box's sprite for an item kind (Menu_DrawSlotRow), None when it shows no count: guns
    (kinds 1-7; the code's early return for kind 4 can never be taken) and kinds 0x13 on."""
    if 0 < kind < 8:
        return kind + 0x1A if kind > 4 else 0x1F
    return None if kind < 0x13 else kind + 0xC


def grid_cells(menu: MenuDisc, window: int, grid: int, count: int):
    """(window x, y, w, h, the first cell's x, y, cell width, row height, columns) for a list of
    `count` cells: Draw_SetPrimCallback shortens the window to the rows the list fills (up to the
    grid's own rows); MenuWidget_DrawListRow draws each cell 2 px in from its corner."""
    x, y, w, h = menu.windows[window]
    gx, gy, columns, rows, _, cell_w, row_h, _ = menu.grids[grid]
    shown = min(rows, (count + columns - 1) // columns)
    h += row_h * (shown - rows)
    return x, y, w, h, x + gx + 2, y + gy + 2, cell_w, row_h, columns


def item_cells(menu: MenuDisc):
    """grid_cells for the start list (Aya's 10 slots)."""
    return grid_cells(menu, ITEM_WINDOW, ITEM_GRID, ITEM_SLOTS)


def draw_cursor(screen: Screen, x: int, y: int, w: int, h: int, pulse: int | None = PULSE, blink: int = 0) -> None:
    """A list's cursor on the cell at (x, y): the tile added whose grey pulses, then the window's bars
    (Draw_AllocColorTriGradient; `blink` 1 while the bars blink). The list puts it in after the cells,
    so the GPU draws it first, under them."""
    if pulse is not None:
        screen.blend(tile_add(x, y, w, h), (pulse, pulse, pulse), 1)
    for pixels, rgb, sign in bevel(x, y, w, h, mode=blink):
        screen.blend(pixels, rgb, sign)


def item_list(mf: MenuFont, raw: bytes, key: str, background: bytes | None, seconds: int = 0,
              pulse: int | None = PULSE, blink: int = 0, dim: bool = True) -> Look:
    """The item list with Aya's first items and this item in the cursor's cell; the help line above it
    with the item's description. Names and the description are the pack's (the translator's words, or
    the English the Translator puts beside them); a name the pack doesn't hold is left out."""
    menu, pack = mf.menu, mf.pack
    k = int(key.split("/")[1])
    slots = list(START_ITEMS)
    cursor = next((s for s, (i, _) in enumerate(slots) if i == k), None)
    if cursor is None:
        slots[CURSOR_SLOT] = (k, False)
        cursor = CURSOR_SLOT
    screen = Screen(background, dim)
    notes = set()
    description = pack.text(f"desc/{k:03d}")
    _, digits = draw_help(screen, mf, description, f"desc/{k:03d}", seconds)
    if description is None:
        notes.add("its description is not in the file")
    wx, wy, ww, wh, x0, y0, cell_w, row_h, columns = item_cells(menu)
    for pixels, rgb, sign in bevel(wx, wy, ww, wh):
        screen.blend(pixels, rgb, sign)
    # Drawn as the GPU takes the list: the last thing the game puts in is drawn first. The cursor goes in
    # after the cells (MenuWidget_DrawList), each cell's blue tile after the cell.
    draw_cursor(screen, x0 - 2 + cell_w * (cursor % columns), y0 - 2 + row_h * (cursor // columns),
                cell_w, row_h, pulse, blink)
    text = None
    for s in reversed(range(len(slots))):
        i, equipped = slots[s]
        rec = menu.items[i]
        x, y = x0 + cell_w * (s % columns), y0 + row_h * (s // columns)
        if equipped:
            c = EQUIPPED_TILE
            screen.blend(tile_add(x, y, cell_w - 4, row_h - 4), (c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF), 1)
        kind, rounds = rec[6], START_ROUNDS.get(i, struct.unpack_from("<H", rec, 0x0A)[0])
        sprite = count_sprite(kind)
        if sprite is not None:
            pen = x + COUNT_X
            for n, digit in reversed(list(enumerate(_three(rounds)))):
                draw_digit(screen, menu, digit, pen + 5 * n, y + 5, DIGITS_PLAIN, DIM_COLOUR)
            draw_sprite(screen, menu, sprite, pen, y, DIM_COLOUR)
        name = raw if i == k else pack.text(item_key(i))
        if name is not None:
            laid = lay_out(mf, name, x + NAME_X, y, TEXT_COLOUR if usable(rec) else DIM_COLOUR)
            draw_text(screen, mf, laid)
            if i == k:
                text = laid
                notes |= laid.notes
        elif i != k:
            notes.add("a name the file doesn't hold is left out")
        draw_sprite(screen, menu, rec[0] - 1, x, y, DIM_COLOUR)
        if 0 < kind < 8:
            capacity = rec[9] + struct.unpack_from("<h", rec, 0x12)[0]
            if rounds == capacity if capacity < 1000 else rounds == 999:
                draw_sprite(screen, menu, FULL_MARK, x, y, DIM_COLOUR)
    where = "the item list: Aya's first items, this one under the cursor"
    return Look(screen.rgb(), where, sorted(notes), digits, text, top=0, inked=screen.inked)


def _three(value: int) -> list[int]:
    """Draw_PrintNumberWidth3Unk's digits: three places, blanks (-1) before the first that isn't 0."""
    out, place = [], 100
    for n in range(3):
        q = value // place
        out.append(-1 if n < 2 and q == 0 else q % 10)
        place //= 10
    return out


# The Parasite Energy list (Menu_CreateSkillActionScreen, skills.c): window 0x08 holding grid 0x08, two
# columns of 100 x 16 cells, the PEs Aya has learned in their own order (Item_PackParasiteSpells: bit n
# of her PE flags is PE n, its name table 8's entry n + 0xEB, item record n + 235). Each cell is only the
# name, from the cell's start (Menu_DrawModNameItem), dimmed when the PE can't be used now
# (Menu_GetItemSlotAvailability -> Menu_CheckItemAffordable). The cursor as the item list's. The PE bar
# (Draw_AllocColorQuad) is in the HP box, outside the window: not drawn (the user's choice).
PE_WINDOW = PE_GRID = 0x08
PE_FIRST = 235                       # item/235 = PE 0 (Heal 1) .. item/254 = PE 19 (Liberate)
PE_COUNT = 20
# The PEs a game teaches: 0-12 (Heal 1 .. Preraise), 18 Full Recover, 19 Liberate. 13-17 (Mito14-18)
# are names no game ever gives Aya; one of them is shown in its own place only while it is translated.
LEARNABLE = tuple(range(13)) + (18, 19)
HEALS = (0, 1, 2, 18)                # Menu_CheckItemAffordable: not while HP is full


def pe_key(p: int) -> str:
    return f"item/{PE_FIRST + p:03d}"


def pe_usable(rec: bytes, p: int, hurt: bool = True) -> bool:
    """Whether the field menu draws this PE's name undimmed (Menu_CheckItemAffordable outside battle,
    with PE enough for it): the record's flag bit 0 (Heal 1-3 and Full Recover), and for the heals,
    Aya hurt."""
    return bool(rec[5] & 1) and (hurt or p not in HEALS)


def pe_list(mf: MenuFont, raw: bytes, key: str, background: bytes | None, seconds: int = 0,
            pulse: int | None = PULSE, blink: int = 0, dim: bool = True, learned=LEARNABLE,
            hurt: bool = True) -> Look:
    """The PE list with every PE a game teaches (or `learned`) and this one under the cursor, as the
    field menu draws it with Aya hurt (or not: `hurt`); the help line above it with the PE's
    description. Names are the pack's; a name the pack doesn't hold is left out."""
    menu, pack = mf.menu, mf.pack
    p = int(key.split("/")[1]) - PE_FIRST
    shown = sorted(set(learned) | {p})
    cursor = shown.index(p)
    screen = Screen(background, dim)
    notes = set()
    description = pack.text(f"desc/{PE_FIRST + p:03d}")
    _, digits = draw_help(screen, mf, description, f"desc/{PE_FIRST + p:03d}", seconds)
    if description is None:
        notes.add("its description is not in the file")
    wx, wy, ww, wh, x0, y0, cell_w, row_h, columns = grid_cells(menu, PE_WINDOW, PE_GRID, len(shown))
    for pixels, rgb, sign in bevel(wx, wy, ww, wh):
        screen.blend(pixels, rgb, sign)
    cx, cy = x0 - 2 + cell_w * (cursor % columns), y0 - 2 + row_h * (cursor // columns)
    draw_cursor(screen, cx, cy, cell_w, row_h, pulse, blink)
    text = None
    for s in reversed(range(len(shown))):
        q = shown[s]
        name = raw if q == p else pack.text(pe_key(q))
        if name is None:
            notes.add("a name the file doesn't hold is left out")
            continue
        colour = TEXT_COLOUR if pe_usable(menu.items[PE_FIRST + q], q, hurt) else DIM_COLOUR
        laid = lay_out(mf, name, x0 + cell_w * (s % columns), y0 + row_h * (s // columns), colour)
        draw_text(screen, mf, laid)
        if q == p:
            text = laid
            notes |= laid.notes
    where = "the PE list: every PE a game teaches, this one under the cursor"
    return Look(screen.rgb(), where, sorted(notes), digits, text, top=0, inked=screen.inked,
                focus=(cy, cy + row_h))


DRAWN = {"help line": help_line, "item list": item_list, "parasite energy list": pe_list}


def can_draw(key: str) -> bool:
    return context_of(key) in DRAWN


def context_keys(key: str) -> list[str]:
    """The other rows the game look of `key` shows: for an item's name, the other first items' names
    and the item's description (the help line above the list); for a PE's, the other PEs a game
    teaches and the PE's description."""
    where = context_of(key)
    k = int(key.split("/")[1]) if where in ("item list", "parasite energy list") else 0
    if where == "item list":
        return [item_key(i) for i, _ in START_ITEMS if i != k] + [f"desc/{k:03d}"]
    if where == "parasite energy list":
        return [pe_key(q) for q in LEARNABLE if PE_FIRST + q != k] + [f"desc/{k:03d}"]
    return []


def look(mf: MenuFont, raw: bytes, key: str, background: bytes | None, seconds: int = 0) -> Look | None:
    draw = DRAWN.get(context_of(key))
    return draw(mf, raw, key, background, seconds) if draw else None
