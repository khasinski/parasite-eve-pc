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


def bars(points, bright, dark, x: int = 0, y: int = 0, colour: int = WINDOW_COLOUR, mode: int = 0):
    """A window's bars from its corners (Draw_EmitWipeBar): 2 px bars along the edges `bright`, added to
    what is under them, then along the edges `dark`, taken off it; [(pixels, rgb, sign)] in drawing
    order. `mode` picks the colour of g_DrawBlendColorPair (1: the list cursor's bars while it blinks)."""
    corners = [(x + px, y + py) for px, py in points]
    pair = [colour & 0xFFFFFF, _halves(colour)]
    out = []
    for edges, width, sign in ((bright, 2, 1), (dark, -2, -1)):
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


def bevel(x: int, y: int, w: int, h: int, colour: int = WINDOW_COLOUR, mode: int = 0) -> list[tuple[list, tuple, int]]:
    """A window's bars (Draw_AllocColorGradient over a rectangle, g_WipeRectEdgeList): [(pixels, rgb,
    sign)], sign +1 added to what is under, -1 taken off it, in drawing order. `mode` picks the colour
    of g_DrawBlendColorPair (1: the list cursor's bars while it blinks, Draw_AllocColorTriGradient)."""
    return bars(((0, 0), (w, 0), (0, h), (w, h)), ((0, 1), (0, 2)), ((1, 3), (2, 3)), x, y, colour, mode)


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
    if key in OWN_CONTEXT:
        return OWN_CONTEXT[key]
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


# ---- the boxes and choices --------------------------------------------------------------------------
#
# The pop-up boxes (a message with OK, a question with two choices, an item's actions, the memory card's
# progress) are windows the game makes with MenuWidget_CreateSimpleNode(..., 1): Draw_AllocColorGradient
# then fills them with a 32 x 32 tile of the menus' page in the colours at VRAM 448,228 (solid grey)
# under the bars, and they sit in the middle of the screen (0xA0 less half the table's width, 0x78 less
# half its height). Their words are centred as Draw_PrintCenteredTextInWidth does it: the pen moves
# (room - (measured + 4)) >> 1 on. The config menu's windows are the see-through kind, at their own
# places in the window table.

BOX_CLUT = 0x391C                    # the tile's colours (Draw_AllocColorGradient): VRAM 448,228
MESSAGE_WINDOW = 0x28                # Menu_CreateNotificationDialog / Menu_CreateTwoLineDialog (0x3D: its second copy)
CONFIRM_WINDOW, FORMAT_WINDOW = 0x29, 0x2A    # Menu_DrawItemLabel over two choices
PROGRESS_WINDOW = 0x27               # MemCard_DrawProgress
ACTION_WINDOW = 0x02                 # Menu_OpenItemActionSubmenu: three rows (0x03: two, Move and Discard)
CONFIG_WINDOW, SOUND_WINDOW, CURSOR_WINDOW = 0x20, 0x21, 0x23     # main_menu.c
COLOUR_WINDOW, COLOUR_OPTIONS = 0x2E, 0x31
LINE_STEP = 0x0E                     # a box's next line (Draw_OffsetCursor(0, 0xE))
STILL = 0x40                         # a cursor's tile in a list that isn't the one in use (g_DrawColorShaded)
ON_BLOCK, OFF_BLOCK, BLOCKS = 0x83, 0x82, 8
UP_ARROW, DOWN_ARROW = 0x4A, 0x4B
# g_MenuBlendColorPanelPoints (main_menu.c): the colour window's corners, then the edges that get the
# bright bars and the edges that get the dark ones (two boxes side by side).
COLOUR_POINTS = ((0, 0), (100, 0), (184, 0), (0, 40), (100, 40), (184, 40))
COLOUR_BRIGHT, COLOUR_DARK = ((0, 1), (1, 2), (0, 3), (1, 4)), ((3, 4), (4, 5), (1, 4), (2, 5))

# Which place draws a row this file knows better than data/menu_layouts.json (whose limits stay as
# they are): sys/000 "Use" is the first of an item's actions (skills.c copies it into the confirm
# box's label and writes over it before it is drawn), and the rows no layout names.
OWN_CONTEXTS = {"item actions": ("sys/000", "sys/001", "sys/002", "sys/003"),
                "cursor choice": ("sys/049", "sys/050"),
                "progress box": ("sys/064", "sys/069", "sys/070"),
                "confirm label": ("sys/074",),
                "dialog line": ("sys/099",)}
OWN_CONTEXT = {key: where for where, keys in OWN_CONTEXTS.items() for key in keys}


def sys_key(n: int) -> str:
    return f"sys/{n:03d}"


def misc_key(n: int) -> str:
    return f"misc/{n:03d}"


def _end(raw: bytes) -> int:
    return raw.index(0xFF) if 0xFF in raw else len(raw)


def joined(*parts: bytes | None) -> bytes:
    """Text_CopyFFTerminatedBytes then Text_AppendFFTerminatedBytes: the parts one after another."""
    return b"".join(p[:_end(p)] for p in parts if p) + b"\xff"


def _breaks(mf: MenuFont, raw: bytes) -> list[int]:
    """Where a pack's line breaks are in a menu line (Menu_LineBreakOffset's walk, PeText_LineBreakAt)."""
    out, i, end, runs = [], 0, _end(raw), mf.pack.stretches
    while i < end:
        b = raw[i]
        if b == 0xF1:
            if i + 3 < end and raw[i + 1] == 0xC6 and raw[i + 2] < 240 and raw[i + 3] < 240:
                k = raw[i + 2] * 240 + raw[i + 3]
                if k < len(runs) and runs[k] == [LINE_BREAK]:
                    out.append(i)
            i += 4
        else:
            i += 2 if b >= 0xFA else 1
    return out


def split_line(mf: MenuFont, raw: bytes) -> list[bytes]:
    """Menu_SplitDialogLine: the line, and what follows its first line break when something does (a
    break with nothing after it is dropped)."""
    at, end = _breaks(mf, raw), _end(raw)
    if not at:
        return [raw[:end] + b"\xff"]
    if at[0] + 4 >= end:
        return [raw[:at[0]] + b"\xff"]
    return [raw[:at[0]] + b"\xff", raw[at[0] + 4:end] + b"\xff"]


def measure(mf: MenuFont, raw: bytes) -> int:
    """Draw_MeasureTextWidth: every letter of the text, whatever lines a pack's breaks put them on."""
    at, end = [-4] + _breaks(mf, raw), _end(raw)
    return sum(lay_out(mf, raw[a + 4:b] + b"\xff", 0, 0).width for a, b in zip(at, at[1:] + [end]))


def centred(mf: MenuFont, raw: bytes, x: int, y: int, room: int, colour=TEXT_COLOUR) -> Text:
    """Draw_PrintCenteredTextInWidth: the text from the cursor (x, y), moved on by half of what the
    room leaves it (the measured width and 4 more)."""
    return lay_out(mf, raw, x + ((room - (measure(mf, raw) + 4)) >> 1), y, colour)


def draw_box(screen: Screen, menu: MenuDisc, x: int, y: int, w: int, h: int, dimmed: bool = False) -> None:
    """A pop-up box (Draw_AllocColorGradient, textured): the tile over all of it, then its bars."""
    colours = menu.colours(BOX_CLUT)
    tint = DIM_COLOUR if dimmed else TEXT_COLOUR
    for j in range(h):
        for i in range(w):
            screen.put(x + i, y + j, colours[menu.index(i & 31, j & 31)], tint)
    for pixels, rgb, sign in bevel(x, y, w, h):
        screen.blend(pixels, rgb, sign)


def centre_of(menu: MenuDisc, window: int) -> tuple[int, int, int, int]:
    """MenuWidget_CreateSimpleNode: a window as the table has it, or in the middle of the screen
    when the table gives it no x."""
    x, y, w, h = menu.windows[window]
    if x:
        return x, y, w, h
    return 0xA0 - (w >> 1), (0x50 if y else 0x78) - (h >> 1), w, h


def _cell(menu: MenuDisc, grid: int) -> tuple[int, int, int, int, int]:
    gx, gy, columns, _, _, cell_w, row_h, _ = menu.grids[grid]
    return gx, gy, columns, cell_w, row_h


def _finish(screen: Screen, where: str, notes: set, text: Text | None, box: tuple, digits=()) -> Look:
    x, y, w, h = box
    return Look(screen.rgb(), where, sorted(notes | (text.notes if text else set())), list(digits), text,
                top=max(0, y - 43), inked=screen.inked, focus=(y, y + h))


def _said(mf: MenuFont, raw: bytes, key: str, want: str, notes: set) -> bytes | None:
    """The bytes of row `want`: the line being drawn itself, or the pack's (noted when it has none)."""
    if want == key:
        return raw
    found = mf.pack.text(want)
    if found is None:
        notes.add("a row the file doesn't hold is left out")
    return found


# A message with OK. One line (Menu_CreateNotificationDialog: a message, sometimes with a second row
# after it on the same line) or two (Menu_CreateTwoLineDialog); the box is as wide as its widest line
# and 20 more, 120 at least, from x (300 - that) / 2, and a line taller for each line a pack's break
# adds. Each row is shown in the box the game puts it in; the memory card's rows with card 1's.
TWO_LINES = {61: (61, 63), 63: (61, 63), 85: (85, 86), 86: (85, 86),
             66: (71, 66), 67: (71, 67), 75: (71, 75), 71: (71, 66), 72: (72, 66)}
OK_ROW, OK_MESSAGE = 8, 84           # sys/008 "OK" is shown under sys/084 "File loaded"


def message_rows(key: str) -> tuple[int, ...]:
    """The rows of table sys in the row's message box, one a line."""
    n = int(key.split("/")[1])
    return (OK_MESSAGE,) if n == OK_ROW else TWO_LINES.get(n, (n,))


def message_box(mf: MenuFont, raw: bytes, key: str, background: bytes | None, seconds: int = 0,
                pulse: int | None = PULSE, blink: int = 0, dim: bool = True) -> Look:
    """A message box with its OK, as Menu_DrawNotificationText / Menu_DrawTwoLineDialogText draw it."""
    menu, notes = mf.menu, set()
    lines, text = [], None
    for row in message_rows(key):
        said = _said(mf, raw, key, sys_key(row), notes)
        if said is not None:
            lines += [(part, sys_key(row) == key) for part in split_line(mf, said)]
    x, y, w, h = centre_of(menu, MESSAGE_WINDOW)
    width = max([100] + [measure(mf, part) for part, _ in lines])
    extra = LINE_STEP * max(0, len(lines) - 1)
    x, w, h = (300 - width) >> 1, width + 20, h + extra
    screen = Screen(background, dim)
    draw_box(screen, menu, x, y, w, h)
    _, gy, _, cell_w, row_h = _cell(menu, MESSAGE_WINDOW)
    cx, cy = x + w - 0x44, y + gy + extra
    draw_cursor(screen, cx, cy, cell_w, row_h, pulse, blink)
    ok = _said(mf, raw, key, sys_key(OK_ROW), notes)
    if ok is not None:
        laid = centred(mf, ok, cx + 2, cy + 2, cell_w)
        draw_text(screen, mf, laid)
        text = laid if key == sys_key(OK_ROW) else text
    for n, (part, mine) in reversed(list(enumerate(lines))):
        laid = centred(mf, part, x, y + 10 + LINE_STEP * n, w)
        draw_text(screen, mf, laid)
        if mine:
            text = laid
    return _finish(screen, "a message box with its OK", notes, text, (x, y, w, h))


# A question with two choices (Menu_DrawItemLabel over Menu_DrawActionOptionRow's two cells): the label
# 10 px down, centred in the box; the box as wide as the label and 20 more, 140 at least; the choices
# from (width - 128) / 2, 20 px above the bottom, each centred in a 64 px cell. Which label and which
# choices each row is shown with, and where the game first puts the cursor (the second choice wherever
# it sets cursor_x to 1):
#   the label's rows, the choices' first row in sys, the cursor's cell, a second label line, the window
CONFIRMS = {
    "discard": (("item/005", "sys/004"), 5, 1, None, CONFIRM_WINDOW),            # Menu_StepItemDetailPanel
    "use pe": (("item/235", "sys/026"), 5, 0, None, CONFIRM_WINDOW),             # Menu_StepItemDetailPanel2
    "equip": (("sys/113", "item/062"), 5, 0, None, CONFIRM_WINDOW),              # Menu_StepEquipConfirm
    "add slot": (("sys/097",), 5, 0, None, CONFIRM_WINDOW),                      # item_actions.c
    "overwrite": (("sys/068",), 108, 1, None, CONFIRM_WINDOW),                   # save_screens.c
    "format": (("sys/071", "sys/073"), 108, 1, 74, FORMAT_WINDOW),               # Menu_StepItemGrid2
}
CONFIRM_OF = {4: "discard", 5: "discard", 6: "discard", 26: "use pe", 113: "equip", 97: "add slot",
              68: "overwrite", 108: "overwrite", 109: "overwrite", 73: "format", 74: "format"}


def confirm_of(key: str) -> tuple:
    return CONFIRMS[CONFIRM_OF[int(key.split("/")[1])]]


def confirm_box(mf: MenuFont, raw: bytes, key: str, background: bytes | None, seconds: int = 0,
                pulse: int | None = PULSE, blink: int = 0, dim: bool = True, label=None,
                cursor: int | None = None) -> Look:
    """A confirm box: its label (the row, joined with a name where the game joins one) over its two
    choices, the cursor where the game first puts it, or on the choice being translated. `label`,
    `cursor`: another label's rows and the cursor's cell, for a box the game shows."""
    menu, notes = mf.menu, set()
    label_keys, first, start, second, window = confirm_of(key)
    label_keys = label or label_keys
    choices = [sys_key(first), sys_key(first + 1)]
    if cursor is None:
        cursor = choices.index(key) if key in choices else start
    whole = joined(*[_said(mf, raw, key, k, notes) for k in label_keys])
    lines = split_line(mf, whole)
    x, y, w, h = centre_of(menu, window)
    if window == FORMAT_WINDOW:
        width = max([0x78] + [measure(mf, part) for part in lines])
        h = 0x42 + (LINE_STEP if len(lines) > 1 else 0)
    else:
        width, h = max(0x78, measure(mf, whole)), 0x32
    x, w = (300 - width) >> 1, width + 20
    screen = Screen(background, dim)
    draw_box(screen, menu, x, y, w, h)
    _, _, _, cell_w, row_h = _cell(menu, window)
    gx, gy = x + ((w - 0x80) >> 1), y + h - 0x14
    draw_cursor(screen, gx + cell_w * cursor, gy, cell_w, row_h, pulse, blink)
    text = None
    for n in (1, 0):
        said = _said(mf, raw, key, choices[n], notes)
        if said is not None:
            laid = centred(mf, said, gx + 2 + cell_w * n, gy + 2, cell_w)
            draw_text(screen, mf, laid)
            text = laid if key == choices[n] else text
    if second is not None:
        said = _said(mf, raw, key, sys_key(second), notes)
        if said is not None:
            laid = centred(mf, said, x, y + 10 + LINE_STEP * len(lines), w)
            draw_text(screen, mf, laid)
            text = laid if key == sys_key(second) else text
    for n, part in reversed(list(enumerate(lines))):
        laid = centred(mf, part, x, y + 10 + LINE_STEP * n, w)
        draw_text(screen, mf, laid)
        if key in label_keys and n == 0:
            text = laid
    return _finish(screen, "a confirm box: its label and two choices", notes, text, (x, y, w, h))


# An item's actions (Menu_OpenItemActionSubmenu, Menu_ContextPick): Use, Move, Discard for an item that
# is used (Medicine), Reload, Move, Discard for a gun or its bullets; each centred in a 56 px cell.
# Use is dimmed while the item can't be used (Medicine at full HP): shown bright, as with Aya hurt.
ACTIONS = {0: (0, 1, 2), 1: (0, 1, 2), 2: (0, 1, 2), 3: (3, 1, 2)}


def action_box(mf: MenuFont, raw: bytes, key: str, background: bytes | None, seconds: int = 0,
               pulse: int | None = PULSE, blink: int = 0, dim: bool = True, dimmed=(), cursor: int | None = None) -> Look:
    """The box of an item's actions with the cursor on this one. `dimmed`: the rows of sys the game
    draws dimmed just then; `cursor`: the cursor's row when it isn't this one's."""
    menu, notes = mf.menu, set()
    rows = ACTIONS[int(key.split("/")[1])]
    x, y, w, h = centre_of(menu, ACTION_WINDOW)
    gx, gy, _, cell_w, row_h = _cell(menu, ACTION_WINDOW)
    screen = Screen(background, dim)
    draw_box(screen, menu, x, y, w, h)
    at = rows.index(int(key.split("/")[1])) if cursor is None else cursor
    draw_cursor(screen, x + gx, y + gy + row_h * at, cell_w, row_h, pulse, blink)
    text = None
    for n, row in reversed(list(enumerate(rows))):
        said = _said(mf, raw, key, sys_key(row), notes)
        if said is None:
            continue
        laid = centred(mf, said, x + gx + 2, y + gy + 2 + row_h * n, cell_w,
                       DIM_COLOUR if row in dimmed else TEXT_COLOUR)
        draw_text(screen, mf, laid)
        text = laid if sys_key(row) == key else text
    return _finish(screen, "the box of an item's actions", notes, text, (x, y, w, h))


# The memory card's progress box (MemCard_DrawProgress): the word centred 10 px down, and for Saving
# and Loading eight blocks under it, ON for each part done (sprites of the menus' page: pictures).
BLOCKS_ON = 3
NO_BLOCKS = 64                       # sys/064 "Formatting" has none


def progress_box(mf: MenuFont, raw: bytes, key: str, background: bytes | None, seconds: int = 0,
                 dim: bool = True, done: int = BLOCKS_ON) -> Look:
    """The progress box with this word, `done` of its eight blocks ON."""
    menu = mf.menu
    x, y, w, h = centre_of(menu, PROGRESS_WINDOW)
    screen = Screen(background, dim)
    draw_box(screen, menu, x, y, w, h)
    if int(key.split("/")[1]) != NO_BLOCKS:
        for n in reversed(range(BLOCKS)):
            draw_sprite(screen, menu, ON_BLOCK if n < done else OFF_BLOCK, x + 16 + 16 * n, y + 30)
    text = centred(mf, raw, x, y + 10, w)
    draw_text(screen, mf, text)
    return _finish(screen, "the memory card's progress box", set(), text, (x, y, w, h))


# The config menu (main_menu.c): window 0x20 with Sound, Cursor, Window, Position (Draw_SetPrimCallback
# shows four of the grid's six rows: MAX and max are rows the game never shows), each centred in a
# 68 px cell; beside it the window of the row picked: Mono / Stereo (0x21), Default / Memory (0x23), or
# the window colour's (0x2E: R, G, B with their values between arrows, and Completed / Default).
# The help line above shows the hint of the cursor's row (Menu_DrawContextHelpText). While a second
# window is open the list's other rows are dimmed and its cursor stands still.
CONFIG_ROWS = (40, 41, 42, 43)                   # sys rows; 44 and 45 follow in the table
CONFIG_HINT, SOUND_HINT, CURSOR_HINT, CHANNEL_HINT, OPTION_HINT = 49, 58, 61, 63, 66      # misc rows
#   window, its rows, the config list's row, the first hint, the choice in use in a new game
CHOICES = {"sound choice": (SOUND_WINDOW, (46, 47), 0, SOUND_HINT, 1),
           "cursor choice": (CURSOR_WINDOW, (49, 50), 1, CURSOR_HINT, 0)}
CHANNELS, OPTIONS = (53, 54, 55), (51, 52)
CHANNEL_VALUE = (0x40 - 0x20) >> 1               # the default window colour's parts: 16


def _config_list(screen: Screen, mf: MenuFont, raw: bytes, key: str, rows, cursor: int, in_use: bool,
                 pulse, blink, notes: set):
    """Window 0x20 with `rows`, the cursor on row `cursor`; in_use: it is the list the player is in."""
    menu = mf.menu
    wx, wy, ww, wh, x0, y0, cell_w, row_h, _ = grid_cells(menu, CONFIG_WINDOW, CONFIG_WINDOW, len(rows))
    for pixels, rgb, sign in bevel(wx, wy, ww, wh):
        screen.blend(pixels, rgb, sign)
    draw_cursor(screen, x0 - 2, y0 - 2 + row_h * cursor, cell_w, row_h, pulse if in_use else STILL,
                blink if in_use else 0)
    text = None
    for n, row in reversed(list(enumerate(rows))):
        said = _said(mf, raw, key, sys_key(row), notes)
        if said is None:
            continue
        laid = centred(mf, said, x0, y0 + row_h * n, cell_w, TEXT_COLOUR if in_use or n == cursor else DIM_COLOUR)
        draw_text(screen, mf, laid)
        text = laid if sys_key(row) == key else text
    return text, (wx, wy, ww, wh)


def _hint(screen: Screen, mf: MenuFont, row: int, seconds: int, notes: set):
    hint = mf.pack.text(misc_key(row))
    if hint is None:
        notes.add("its hint in the help line is not in the file")
    return draw_help(screen, mf, hint, misc_key(row), seconds)[1]


def config_menu(mf: MenuFont, raw: bytes, key: str, background: bytes | None, seconds: int = 0,
                pulse: int | None = PULSE, blink: int = 0, dim: bool = True) -> Look:
    """The config menu's list with the cursor on this row (a row the game never shows after the four
    it does)."""
    n, notes = int(key.split("/")[1]), set()
    rows = CONFIG_ROWS if n in CONFIG_ROWS else CONFIG_ROWS + (n,)
    cursor = rows.index(n)
    screen = Screen(background, dim)
    if cursor < len(CONFIG_ROWS):
        digits = _hint(screen, mf, CONFIG_HINT + cursor, seconds, notes)
    else:
        digits = draw_help(screen, mf, None, key, seconds)[1]
        notes.add("a row the game never shows (its list ends at Position)")
    text, box = _config_list(screen, mf, raw, key, rows, cursor, True, pulse, blink, notes)
    return _finish(screen, "the config menu", notes, text, (box[0], 0, box[2], box[1] + box[3]), digits)


def config_choice(mf: MenuFont, raw: bytes, key: str, background: bytes | None, seconds: int = 0,
                  pulse: int | None = PULSE, blink: int = 0, dim: bool = True, in_use: int | None = None) -> Look:
    """Mono / Stereo or Default / Memory beside the config menu, the cursor on this row; the blue tile
    on the one in use (Draw_DropShadow: Stereo and Default in a new game, or `in_use`)."""
    menu, notes = mf.menu, set()
    window, rows, picked, hint, default = CHOICES[context_of(key)]
    at = rows.index(int(key.split("/")[1]))
    in_use = default if in_use is None else in_use
    screen = Screen(background, dim)
    digits = _hint(screen, mf, hint + at, seconds, notes)
    _config_list(screen, mf, raw, key, CONFIG_ROWS, picked, False, pulse, blink, notes)
    wx, wy, ww, wh, x0, y0, cell_w, row_h, _ = grid_cells(menu, window, window, len(rows))
    for pixels, rgb, sign in bevel(wx, wy, ww, wh):
        screen.blend(pixels, rgb, sign)
    draw_cursor(screen, x0 - 2, y0 - 2 + row_h * at, cell_w, row_h, pulse, blink)
    text = None
    for n, row in reversed(list(enumerate(rows))):
        if n == in_use:
            c = EQUIPPED_TILE
            screen.blend(tile_add(x0, y0 + row_h * n, cell_w - 4, row_h - 4), (c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF), 1)
        said = _said(mf, raw, key, sys_key(row), notes)
        if said is None:
            continue
        laid = centred(mf, said, x0, y0 + row_h * n, cell_w)
        draw_text(screen, mf, laid)
        text = laid if sys_key(row) == key else text
    return _finish(screen, "the config menu: " + ("Sound" if window == SOUND_WINDOW else "Cursor"), notes, text,
                   (wx, 0, ww, wy + wh), digits)


def colour_window(mf: MenuFont, raw: bytes, key: str, background: bytes | None, seconds: int = 0,
                  pulse: int | None = PULSE, blink: int = 0, dim: bool = True) -> Look:
    """The window colour's window beside the config menu: R, G, B (each in its own colour, its value
    10 px on, an arrow above and under it) and Completed / Default; the cursor on this row."""
    menu, notes = mf.menu, set()
    n = int(key.split("/")[1])
    screen = Screen(background, dim)
    digits = _hint(screen, mf, CHANNEL_HINT + CHANNELS.index(n) if n in CHANNELS else OPTION_HINT + OPTIONS.index(n),
                   seconds, notes)
    _config_list(screen, mf, raw, key, CONFIG_ROWS, 2, False, pulse, blink, notes)
    wx, wy, ww, wh = menu.windows[COLOUR_WINDOW]
    for pixels, rgb, sign in bars(COLOUR_POINTS, COLOUR_BRIGHT, COLOUR_DARK, wx, wy):
        screen.blend(pixels, rgb, sign)
    gx, gy, _, cell_w, row_h = _cell(menu, COLOUR_WINDOW)
    ox, oy, _, option_w, option_h = _cell(menu, COLOUR_OPTIONS)
    if n in CHANNELS:
        draw_cursor(screen, wx + gx + cell_w * CHANNELS.index(n), wy + gy, cell_w, row_h, pulse, blink)
    else:
        draw_cursor(screen, wx + ox, wy + oy + option_h * OPTIONS.index(n), option_w, option_h, pulse, blink)
    text = None
    for k, row in reversed(list(enumerate(OPTIONS))):
        said = _said(mf, raw, key, sys_key(row), notes)
        if said is not None:
            laid = centred(mf, said, wx + ox + 2, wy + oy + 2 + option_h * k + 1, option_w)
            draw_text(screen, mf, laid)
            text = laid if sys_key(row) == key else text
    for k, row in reversed(list(enumerate(CHANNELS))):
        x, y = wx + gx + 2 + cell_w * k, wy + gy + 2
        for d, digit in reversed(list(enumerate(_three(CHANNEL_VALUE)))):
            draw_digit(screen, menu, digit, x + 10 + 5 * d, y + 3, DIGITS_PLAIN)
        said = _said(mf, raw, key, sys_key(row), notes)
        if said is not None:
            laid = lay_out(mf, said, x, y, tuple(0x80 if c == k else 0 for c in range(3)))
            draw_text(screen, mf, laid)
            text = laid if sys_key(row) == key else text
    for k in reversed(range(3)):                 # Menu_DrawBlendColorArrows: dimmed while no direction is held
        draw_sprite(screen, menu, DOWN_ARROW, wx + 0x12 + 0x20 * k, wy + 5 + 0x19, DIM_COLOUR)
    for k in reversed(range(3)):
        draw_sprite(screen, menu, UP_ARROW, wx + 0x12 + 0x20 * k, wy + 5, DIM_COLOUR)
    return _finish(screen, "the config menu: Window", notes, text, (wx, 0, ww, wy + wh), digits)


DRAWN = {"help line": help_line, "item list": item_list, "parasite energy list": pe_list,
         "dialog line": message_box, "OK button": message_box,
         "confirm label": confirm_box, "OK/cancel choice": confirm_box, "yes/no choice": confirm_box,
         "item actions": action_box, "progress box": progress_box,
         "config menu": config_menu, "sound choice": config_choice, "cursor choice": config_choice,
         "colour options": colour_window, "colour channel": colour_window}


def can_draw(key: str) -> bool:
    return context_of(key) in DRAWN


def context_keys(key: str) -> list[str]:
    """The other rows the game look of `key` shows: for an item's name, the other first items' names
    and the item's description (the help line above the list); for a PE's, the other PEs a game
    teaches and the PE's description; for a row of a box or of the config menu, the box's other rows
    (the name a label is joined with, the choices, the hint in the help line)."""
    where = context_of(key)
    k = int(key.split("/")[1]) if where in DRAWN and where != "help line" else 0
    if where == "item list":
        return [item_key(i) for i, _ in START_ITEMS if i != k] + [f"desc/{k:03d}"]
    if where == "parasite energy list":
        return [pe_key(q) for q in LEARNABLE if PE_FIRST + q != k] + [f"desc/{k:03d}"]
    rows: list[str] = []
    if where in ("dialog line", "OK button"):
        rows = [sys_key(n) for n in message_rows(key)] + [sys_key(OK_ROW)]
    elif where in ("confirm label", "OK/cancel choice", "yes/no choice"):
        label, first, _, second, _ = confirm_of(key)
        rows = list(label) + [sys_key(first), sys_key(first + 1)] + ([sys_key(second)] if second is not None else [])
    elif where == "item actions":
        rows = [sys_key(n) for n in ACTIONS[k]]
    elif where == "config menu":
        rows = [sys_key(n) for n in CONFIG_ROWS] + ([misc_key(CONFIG_HINT + CONFIG_ROWS.index(k))] if k in CONFIG_ROWS else [])
    elif where in CHOICES:
        _, choices, _, hint, _ = CHOICES[where]
        rows = [sys_key(n) for n in CONFIG_ROWS + choices] + [misc_key(hint + choices.index(k))]
    elif where in ("colour options", "colour channel"):
        hint = CHANNEL_HINT + CHANNELS.index(k) if k in CHANNELS else OPTION_HINT + OPTIONS.index(k)
        rows = [sys_key(n) for n in CONFIG_ROWS + CHANNELS + OPTIONS] + [misc_key(hint)]
    return [r for r in rows if r != key]


def look(mf: MenuFont, raw: bytes, key: str, background: bytes | None, seconds: int = 0) -> Look | None:
    draw = DRAWN.get(context_of(key))
    return draw(mf, raw, key, background, seconds) if draw else None
