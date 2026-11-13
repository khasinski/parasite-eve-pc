"""Pictures with words, for the Translator: out of the translator's own disc as a PNG, edited in any
paint program, and back into the translation's pictures.bin.

Nothing of the game's art is kept anywhere but the translator's disc and their own translation:
the list below holds only where each picture is and the CRC-32 of its English bytes (to know the
disc is the right one), and the pictures shown are read from the disc each time.

A translation's redrawn pictures live in its pictures.bin as pieces (petext/pictures.py): a PE.IMG
sector, a place in it, the CRC-32 of the English bytes there and the new bytes. The game puts a
piece in as the sector is read, only where the bytes it finds have that CRC
(mods/pe_lang/pe_lang_pictures.c), in the file's order, each over what the ones before it left.
`shown` does the same here, so what the tool shows is what the game draws. PE.IMG is the same on
Disc 1 and Disc 2, so one set of pieces serves both.

Loading a picture keeps every other piece of the file as it is (a fan patch's pictures, pieces for
another disc version that don't land on this one); only the pieces in the picture's own sectors
that land on this disc are made again, from the English bytes and the picture as it now is.
"""
from __future__ import annotations

import io
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path

from . import project as P
from petext import disc as discs, pictures as pieces_file, tim as timlib

SECTOR = discs.SECTOR

# Plain words for how the game draws each kind of picture: what a redrawn one must keep.
LIGHTEN = ("The game lays this picture over the title screen keeping the lighter colour of each "
           "pixel, so black shows the screen behind it and any colour is fine. Keep it {w}×{h}.")
SIXTEEN = ("This picture has 16 colours. Each pixel of yours becomes the nearest of them, so draw "
           "with the colours of the saved PNG. Keep it {w}×{h}.")
MAP = ("The city map's place names, one to a row 13 pixels tall. The game draws each name twice at the "
       "same spot: the left copy is the letters, laid over the map so they glow; the right copy is the same "
       "letters on a dark band, which darkens the map behind the name. Redraw both copies, inside 127×12 "
       "pixels per name. 16 colours: draw with the colours of the saved PNG (the place you are at shows the "
       "same pixels in gold). Leave the bar at the bottom as it is. Keep it {w}×{h}.")
CARD = ("A day's title card, as the screen shows it. Redraw the words; leave the rest as it is or change it "
        "too. The card is made of 16×16 squares, and each square can use only one of the card's sets of 256 "
        "colours: for each square you change, the tool picks the set that fits your drawing best, and each "
        "pixel becomes its nearest colour in that set. Keep it {w}×{h}.")
DAY1 = (" Day 1's card may never be shown in the US game: it did not appear on a New Game or when continuing "
        "a Day 1 save, and its English picture still has the Japanese word 共鳴 in it. The German translation "
        "redrew it anyway.")
BATTLE = ("The words, numbers and marks shown in a fight. Draw with the greys of the saved PNG: the game tints "
          "them as it draws them (Miss in red, the damage numbers in white, green, yellow or pink; see the "
          "preview). Each word is cut out by a box of its own, so keep a translated word inside it: "
          "Miss 24×8, NO TARGET 56×7, ENERGY SHOT 64×7, SLOW 28×7, ALL 20×7, RANDOM 40×7, SCAN 40×7, "
          "CONFUSE 48×7, BULLETS and TARGET 36×5, AT, HP and PE 8×4, at the places the English ones have. "
          "The numbers, the I II III tabs and the other marks need no translating. Keep it {w}×{h}.")


@dataclass(frozen=True)
class Picture:
    id: str                  # stays the same for good: scripted runs and tests name pictures by it
    where: str               # the part of the game that shows it
    name: str                # what it says in English
    entry: int               # PE.IMG entry
    at: int                  # its TIM's first byte in the entry
    length: int              # the TIM's bytes
    crc: int                 # CRC-32 of the English TIM (USA discs)
    tip: str
    palette: int = 0         # the palette row it is drawn with (4/8-bit pictures)
    # How the game tints it as it draws it (the drawing colour, 128 = as it is), for the preview:
    # (what is drawn so, (r, g, b)).
    tints: tuple = ()

    @property
    def label(self) -> str:
        return f"{self.where}: {self.name}"


@dataclass(frozen=True)
class Card:
    """A room's 2D background view shown as a whole screen: the day title cards. A view is 300 tiles of
    16x16 8-bit pixels (20 x 15: 320 x 240, of which the screen shows rows 4-227), each with its own
    texture tile (none is shared: checked when the disc is read) and one 256-colour row (VRAM rows
    480-511, uploaded by the room's second picture run). Format: gamelook.room_views."""
    id: str
    where: str
    name: str
    room: int                # M number
    view: int                # the room's view (geometry entry) shown for it
    crc: int                 # CRC-32 of its English bytes (`Disc.english`: colour rows, then each tile)
    tip: str
    tints: tuple = ()

    @property
    def label(self) -> str:
        return f"{self.where}: {self.name}"


CARD_SIZE = (320, 224)            # what the screen shows of a view
CARD_TOP = 4                      # the view's row at the screen's top (the game's frames of Days 2-4 are
                                  # the view's rows 4-227, pixel for pixel)
TILE = 16
COLOUR_ROWS = (0, 480, 256, 32)   # the colour rows' upload (VRAM x, y, w, h) in the room's second run


@dataclass
class CardLayout:
    tiles: list              # each tile's screen (x, y), in the order of Disc.english's tile parts
    rows: tuple              # the colour rows the room's tiles use: the sets a changed tile may take


# The title screen, PE.IMG entry 70 (checked on both USA discs). Its other pictures hold no words to
# translate: TIM 0 the title picture, TIM 2 "© 1998 SQUARE", TIM 7 the menu's green highlight.
PICTURES = (
    Picture("title/press-start", "Title screen", "Press Start Button", 70, 212896, 10388, 0x105C034D, LIGHTEN),
    Picture("title/new-game", "Title screen", "New Game", 70, 231080, 6068, 0x8918E2EC, LIGHTEN),
    Picture("title/continue", "Title screen", "Continue", 70, 237148, 6068, 0x1CB553DF, LIGHTEN),
    Picture("title/tutorial", "Title screen", "Tutorial", 70, 243216, 6068, 0x814A526B, LIGHTEN),
    Picture("title/ex-game", "Title screen", "EX Game", 70, 249284, 6068, 0xE45227B8, LIGHTEN),
    Picture("title/published-by", "Title screen", "Published by Square Electronic Arts L.L.C.", 70, 261420, 8256,
            0xB617A272, SIXTEEN),
    # The city map, PE.IMG entry 72 (read each time the map opens): TIM 1, ten place names, each a 13-row
    # strip, drawn by FxCommon_DrawEffectMarkers (src/overlays/fx_common/effects.c) as two 127x12 quads at
    # one spot: x 0-127 added (palette row 143, or 144 for the current place), x 128-255 subtracted (row
    # 145). Rows 130-142 (a bar) are not drawn by the map. TIM 0 is the letters page, not a picture.
    Picture("map/place-names", "City map", "Place names (Subway, Chinatown, Soho … Museum)", 72, 32840, 18432,
            0xE9B4C181, MAP),
    # The battle words, PE.IMG entry 11 (read at each scene change, Scene_LoadFieldBg): one TIM, 4-bit,
    # 256x32, 8 palette rows. Every word, number and label is drawn with row 0 (the grey steps) and
    # tinted by the drawing colour (src/main/battle/status_hud.c): Miss red, the damage numbers white,
    # green, yellow or pink (Battle_DrawStatusPanel), the rest as it is. Rows 1-5 colour only the marks
    # (the moves left, a value label), not words. The boxes in the tip are the sizes that code draws.
    Picture("battle/words", "Battle", "Words and numbers (Miss, NO TARGET, ENERGY SHOT … SCAN, CONFUSE)", 11, 0,
            4384, 0xBA5C60A2, BATTLE,
            tints=(("words and marks", (128, 128, 128)), ("Miss", (128, 0, 0)), ("damage numbers", (0, 128, 0)),
                   ("damage numbers", (128, 128, 0)), ("damage numbers", (128, 0, 128)))),
    # The day title cards: rooms M351 (Day 1-3) and M352 (Day 4-6, Epilogue), one view each, shown when
    # a saved game is continued (Days 2-6 seen so; Day 1's not seen at all).
    Card("day/1", "Day title card", "Day 1 · RESONANCE", 351, 0, 0x786C4F45, CARD + DAY1),
    Card("day/2", "Day title card", "Day 2 · FUSION", 351, 1, 0x51EB8B9B, CARD),
    Card("day/3", "Day title card", "Day 3 · SELECTION", 351, 2, 0x44BBC4B7, CARD),
    Card("day/4", "Day title card", "Day 4 · CONCEPTION", 352, 0, 0xA04B4049, CARD),
    Card("day/5", "Day title card", "Day 5 · EVOLUTION", 352, 1, 0x14A499D3, CARD),
    Card("day/6", "Day title card", "Day 6 · LIBERATION", 352, 2, 0x401EF5B7, CARD),
    Card("day/epilogue", "Day title card", "Epilogue · SYMBIOSIS", 352, 3, 0xA6953E99, CARD),
)
BY_ID = {p.id: p for p in PICTURES}


class NotTheDisc(ValueError):
    """The file is not a Parasite Eve (USA) disc, or its pictures are not the game's."""


class Disc:
    """The translator's disc: each picture's English bytes, read once and checked."""

    def __init__(self, path) -> None:
        self.path = Path(path)
        try:
            game = discs.Game(self.path)
        except (OSError, ValueError, EOFError, KeyError, IndexError) as e:
            raise NotTheDisc(f"{self.path.name} is not a Parasite Eve disc image ({e})") from e
        try:
            if game.region != "USA":
                raise NotTheDisc(f"{self.path.name} is a {game.region} disc. The pictures are the USA discs' "
                                 "(Disc 1 or Disc 2: either one).")
            self.sectors = list(game.sectors)
            self.english: dict[str, bytes] = {}
            self.around: dict[str, tuple[int, bytes]] = {}     # id -> (first PE.IMG byte, its whole sectors)
            self.segments: dict[str, list] = {}                # id -> [(byte in `around`, length)]: its bytes
            self.layout: dict[str, CardLayout] = {}
            rooms: dict[int, tuple[int, bytes]] = {}
            for pic in PICTURES:
                if isinstance(pic, Card):
                    room = game.rooms[pic.room - 1]
                    if pic.room not in rooms:
                        rooms[pic.room] = (room[0] * SECTOR, game.pe(room[0], room[1] + room[2] + room[3]))
                    base, data = rooms[pic.room]
                    try:
                        segments, layout = _card_parts(room, data, pic.view)
                    except (ValueError, struct.error) as e:
                        raise NotTheDisc(f"{self.path.name}: the picture \"{pic.name}\" is not the game's own ({e}). "
                                         "Use an unchanged USA disc, not a patched one.") from e
                    english = b"".join(data[a:a + n] for a, n in segments)
                    if zlib.crc32(english) != pic.crc:
                        raise NotTheDisc(f"{self.path.name}: the picture \"{pic.name}\" is not the game's own. "
                                         "Use an unchanged USA disc, not a patched one.")
                    self.english[pic.id], self.around[pic.id] = english, (base, data)
                    self.segments[pic.id], self.layout[pic.id] = segments, layout
                    continue
                lo, hi = self.span(pic)
                first, last = lo // SECTOR, (hi - 1) // SECTOR
                data = game.pe(first, last - first + 1)
                tim = data[lo - first * SECTOR:hi - first * SECTOR]
                if zlib.crc32(tim) != pic.crc:
                    raise NotTheDisc(f"{self.path.name}: the picture \"{pic.name}\" is not the game's own. "
                                     "Use an unchanged USA disc, not a patched one.")
                self.english[pic.id] = tim
                self.around[pic.id] = (first * SECTOR, data)
                self.segments[pic.id] = [(lo - first * SECTOR, pic.length)]
        finally:
            game.disc.f.close()

    def span(self, pic: Picture) -> tuple[int, int]:
        """The picture's PE.IMG bytes [start, end)."""
        lo = self.sectors[pic.entry] * SECTOR + pic.at
        return lo, lo + pic.length


def _uploads(blob: bytes, at: int):
    """(byte in the room, VRAM x, y, w, h) of each picture a room's run sends to video memory
    (Draw_LoadTimAsset; as gamelook._uploads)."""
    directory = struct.unpack_from("<I", blob, 4)[0]
    packed = struct.unpack_from("<I", blob, directory + 0x28)[0]
    for i in range(packed >> 22):
        _, src, r, src2, r2 = struct.unpack_from("<5I", blob, (packed & 0x3FFFFF) + i * 20)
        off, h = src & 0xFFFFFF, (src >> 24) or 0x100
        yield at + off, (r >> 10) & 0x7FF, r >> 21, r & 0x3FF, h
        if src2 & 0xFFFFFF:
            yield at + off + (src2 & 0xFFFFFF), (r2 >> 10) & 0x7FF, r2 >> 21, r2 & 0x3FF, src2 >> 24


def _card_parts(room: tuple, data: bytes, view: int) -> tuple[list, CardLayout]:
    """A card's bytes in its room's sectors (`data`, from the room's first byte): the colour rows, then
    for each tile of the view its texture word's low half (the colour row) and its 16 rows of 16
    pixels; and where each tile is on the screen. Raises ValueError if the room is not as expected."""
    _, scratch, texture, _ = room
    rects = list(_uploads(data[:scratch * SECTOR], 0)) + \
        list(_uploads(data[scratch * SECTOR:(scratch + texture) * SECTOR], scratch * SECTOR))

    def vram(x: int, y: int, n: int) -> int:
        """The room's byte holding video memory (x, y) (halfwords), for n halfwords on that row."""
        found = [off + ((y - ry) * w + x - rx) * 2 for off, rx, ry, w, h in rects
                 if rx <= x and x + n <= rx + w and ry <= y < ry + h]
        if len(found) != 1:
            raise ValueError(f"video memory {x},{y} is in {len(found)} of the room's pictures")
        return found[0]

    cx, cy, cw, ch = COLOUR_ROWS
    segments = [(vram(cx, cy, cw), cw * ch * 2)]
    if vram(cx, cy + ch - 1, cw) != segments[0][0] + (ch - 1) * cw * 2:
        raise ValueError("the colour rows are not one picture")
    at = (scratch + texture) * SECTOR
    run = data[at:]
    directory = struct.unpack_from("<I", run, 4)[0]
    packed = struct.unpack_from("<I", run, directory + 0x1C)[0]
    geom = struct.unpack_from("<I", run, (packed & 0x3FFFFF) + 4)[0] & 0xFFFFFF
    count = struct.unpack_from("<H", run, geom + 6)[0]
    table = geom + struct.unpack_from("<i", run, geom + 0x14)[0]
    entries, rows = [], set()
    for i in range(count):
        e = table + i * 0x38
        n = struct.unpack_from("<H", run, e + 0x26)[0]
        pos, uv = struct.unpack_from("<ii", run, e + 0x28)
        rows.update(struct.unpack_from("<I", run, e + uv + k * 8)[0] & 511 for k in range(n))
        if run[e] & 2 and run[e + 0x24] == view:
            entries.append((e, n, pos, uv))
    if len(entries) != 1:
        raise ValueError(f"view {view} has {len(entries)} tile lists")
    if not all(cy <= r < cy + ch for r in rows):
        raise ValueError("a tile's colour row is not among the room's")
    e, n, pos, uv = entries[0]
    sx, sy = struct.unpack_from("<hh", run, e + 0xC)
    tiles, seen = [], set()
    for k in range(n):
        a = struct.unpack_from("<I", run, e + pos + k * 4)[0]
        w0 = struct.unpack_from("<I", run, e + uv + k * 8)[0]
        x, y = sx + (a >> 22), sy + ((a >> 12) & 1023)
        u, v = run[e + uv + k * 8 + 4], run[e + uv + k * 8 + 3]
        page_x, page_y = (w0 >> 10) & 0x3C0, (w0 >> 7) & 256
        if u % 2 or (x, y) in seen or x % TILE or y % TILE or (w0 >> 5) & 0x3F0:
            raise ValueError(f"tile {k} of view {view} is not a 16x16 tile on the grid")
        seen.add((x, y))
        segments.append((at + e + uv + k * 8, 2))
        segments.extend((vram(page_x + u // 2, page_y + v + j, TILE // 2), TILE) for j in range(TILE))
        tiles.append((x, y))
    texels = sorted((a, a + n) for a, n in segments[1:] if n == TILE)
    if any(b > c for (_, b), (c, _) in zip(texels, texels[1:])):
        raise ValueError(f"view {view}'s tiles share pixels")
    if not all((x, y) in seen for x in range(0, CARD_SIZE[0], TILE)
               for y in range(CARD_TOP // TILE * TILE, CARD_TOP + CARD_SIZE[1], TILE)):
        raise ValueError(f"view {view}'s tiles don't cover the screen")
    return segments, CardLayout(tiles, tuple(sorted(rows)))


def _segments(disc, pic) -> list:
    got = getattr(disc, "segments", {}).get(pic.id)
    if got is None:                                  # a test's made-up disc: one TIM
        base, _ = disc.around[pic.id]
        lo, hi = disc.span(pic)
        got = [(lo - base, hi - lo)]
    return got


def _ordered(pieces) -> list[pieces_file.Piece]:
    """In the order pictures.bin keeps them, which is the order the game puts them in."""
    return sorted(set(pieces), key=lambda p: (p.sector, p.offset, p.original_crc, p.data))


def _lay(base: int, data: bytes, pieces) -> tuple[bytes, set]:
    """`data` (PE.IMG from byte `base`, whole sectors) with the pieces put in as the game puts them
    in, and the pieces that went in."""
    out, landed = bytearray(data), set()
    for p in _ordered(pieces):
        at = p.sector * SECTOR + p.offset - base
        if 0 <= at and at + len(p.data) <= len(out) and zlib.crc32(out[at:at + len(p.data)]) == p.original_crc:
            out[at:at + len(p.data)] = p.data
            landed.add(p)
    return bytes(out), landed


def shown(disc: Disc, pic, pieces) -> bytes:
    """The picture's bytes (a TIM; a card's colour rows and tiles) as the game shows them with these pieces."""
    base, data = disc.around[pic.id]
    laid, _ = _lay(base, data, pieces)
    return b"".join(laid[a:a + n] for a, n in _segments(disc, pic))


def redrawn(disc: Disc, pic, pieces) -> bool:
    return shown(disc, pic, pieces) != disc.english[pic.id]


def put(disc: Disc, pic, pieces, tim: bytes | None) -> list[pieces_file.Piece]:
    """The pieces with the picture set to `tim` (its whole TIM, same length; None: back to the
    game's). Pieces outside the picture's sectors, and pieces in them that don't land on this disc,
    are kept as they are; the ones that land there are made again from the English bytes."""
    if tim is not None and len(tim) != len(disc.english[pic.id]):
        raise ValueError("a picture must keep its size")
    base, data = disc.around[pic.id]
    first, last = base // SECTOR, (base + len(data)) // SECTOR - 1
    laid, landed = _lay(base, data, pieces)
    now, new, k = bytearray(laid), disc.english[pic.id] if tim is None else tim, 0
    for a, n in _segments(disc, pic):
        now[a:a + n] = new[k:k + n]
        k += n
    kept = [p for p in _ordered(pieces) if not (first <= p.sector <= last and p in landed)]
    made = pieces_file._sector_pieces(base, data, bytes(now))
    out = _ordered(kept + made)
    # What the game will show from the new pieces must be exactly that: the picture, and every
    # other byte of its sectors as it was.
    if _lay(base, data, out)[0] != bytes(now):
        raise RuntimeError("the picture's pieces would not put the picture in as loaded")
    return out


def read_pieces(parts: dict[str, bytes]) -> list[pieces_file.Piece]:
    data = parts.get(P.PICTURES)
    return pieces_file.read(data) if data else []


def write_pieces(parts: dict[str, bytes], pieces) -> None:
    """Into the translation's parts: pictures.bin, or none when there are no pieces left."""
    if pieces:
        parts[P.PICTURES] = pieces_file.write(list(pieces))
    else:
        parts.pop(P.PICTURES, None)


# ---- PNG out and in (Pillow) ----

def size(pic, data: bytes) -> tuple[int, int]:
    return CARD_SIZE if isinstance(pic, Card) else timlib.read(data).size


def screen_rgb(colour: int) -> tuple[int, int, int]:
    """A 15-bit colour as the game's screen shows it (each 5-bit part v as (v*255 + 15) // 31, as
    gamelook.rgb_of: the frames' colours). Colour 0, see-through, is the black behind it."""
    return tuple((((colour >> shift) & 31) * 255 + 15) // 31 for shift in (0, 5, 10))


def _card_colours(data: bytes, row: int) -> list[int]:
    at = (row - COLOUR_ROWS[1]) * COLOUR_ROWS[2] * 2
    return list(struct.unpack_from(f"<{COLOUR_ROWS[2]}H", data, at))


def _card_tiles(data: bytes, layout: CardLayout):
    """Each tile's (x, y, texture word's low half, its 256 palette numbers)."""
    at = COLOUR_ROWS[2] * COLOUR_ROWS[3] * 2
    for x, y in layout.tiles:
        word = struct.unpack_from("<H", data, at)[0]
        yield x, y, word, data[at + 2:at + 2 + TILE * TILE]
        at += 2 + TILE * TILE


def _on_screen(x: int, y: int) -> tuple | None:
    """The part of a tile at view (x, y) the screen shows: (first row, last row + 1, screen y), or None."""
    top, bottom = max(0, CARD_TOP - y), min(TILE, CARD_TOP + CARD_SIZE[1] - y)
    return (top, bottom, y + top - CARD_TOP) if top < bottom and 0 <= x < CARD_SIZE[0] else None


def _card_image(data: bytes, layout: CardLayout):
    """A card as the screen shows it (see-through pixels black: the screen behind them)."""
    from PIL import Image
    img = Image.new("RGB", CARD_SIZE)
    rows: dict[int, list] = {}
    for x, y, word, numbers in _card_tiles(data, layout):
        seen = _on_screen(x, y)
        if seen is None:
            continue
        top, bottom, sy = seen
        row = word & 511
        if row not in rows:
            rows[row] = [screen_rgb(c) for c in _card_colours(data, row)]
        part = Image.new("RGB", (TILE, bottom - top))
        part.putdata([rows[row][n] for n in numbers[top * TILE:bottom * TILE]])
        img.paste(part, (x, sy))
    return img


def image(pic, tim_bytes: bytes, layout: CardLayout | None = None):
    """The picture as a Pillow image: RGB for a 24-bit one and a card; for a 4/8-bit one a palette image
    with the picture's own colours in its own order, so a paint program that keeps the palette gives
    the same palette numbers back."""
    from PIL import Image
    if isinstance(pic, Card):
        return _card_image(tim_bytes, layout)
    tim = timlib.read(tim_bytes)
    if tim.bits in (4, 8):
        img = Image.new("P", tim.size)
        colours = [c for colour in tim.palettes[pic.palette] for c in timlib.rgb_of(colour)]
        img.putpalette(colours, rawmode="RGB")
        img.putdata(timlib.indices(tim))
        return img
    return Image.frombytes("RGB", tim.size, timlib.rgb(tim, pic.palette))


def png(pic, tim_bytes: bytes, layout: CardLayout | None = None) -> bytes:
    out = io.BytesIO()
    image(pic, tim_bytes, layout).save(out, "PNG")
    return out.getvalue()


def tint_colour(colour: int, tint: tuple[int, int, int]) -> int:
    """A 15-bit colour drawn with a drawing colour (128 = as it is), as the PlayStation's GPU does: each
    5-bit part times the drawing colour's over 128, at most 31. The see-through bit stays."""
    parts = [min(31, ((colour >> shift) & 31) * t >> 7) for shift, t in zip((0, 5, 10), tint)]
    return (colour & 0x8000) | parts[0] | parts[1] << 5 | parts[2] << 10


def tinted(pic: Picture, tim_bytes: bytes):
    """The picture as the game draws it in a fight: one copy for each of its tints, one above the other."""
    from PIL import Image
    tim = timlib.read(tim_bytes)
    numbers = timlib.indices(tim)
    out = Image.new("RGB", (tim.width, tim.height * len(pic.tints)))
    for k, (_, tint) in enumerate(pic.tints):
        colours = [timlib.rgb_of(tint_colour(c, tint)) for c in tim.palettes[pic.palette]]
        copy = Image.new("RGB", tim.size)
        copy.putdata([colours[n] for n in numbers])
        out.paste(copy, (0, tim.height * k))
    return out


def tinted_png(pic: Picture, tim_bytes: bytes) -> bytes:
    out = io.BytesIO()
    tinted(pic, tim_bytes).save(out, "PNG")
    return out.getvalue()


@dataclass
class Loaded:
    tim: bytes                       # the picture's new TIM
    notes: list[str]                 # what was done to fit it, in plain words


def from_png(pic, english: bytes, data: bytes, current: bytes | None = None,
             layout: CardLayout | None = None) -> Loaded:
    """A PNG (any paint program's: palette, grey, RGB, with or without see-through) as the picture's
    TIM, on the picture as it is now (`current`, else the English): its colours stay, its pixels
    become the PNG's. Raises ValueError, in plain words, for one that can't be used."""
    from PIL import Image, UnidentifiedImageError
    try:
        img = Image.open(io.BytesIO(data))
        img.load()
    except (UnidentifiedImageError, OSError, ValueError, Image.DecompressionBombError) as e:
        raise ValueError(f"that file is not a picture this tool can read ({e})") from e
    if isinstance(pic, Card):
        return _card_from_png(img, current or english, layout)
    # On the picture as it is now: a translation that changed a picture's colours (the German and Chinese
    # packs did, on the map) keeps them; only the pixels are the PNG's (put() keeps the length).
    tim = timlib.read(current or english)
    if img.size != tim.size:
        raise ValueError(f"the picture is {img.width}×{img.height}; it must stay {tim.width}×{tim.height}, "
                         "the size the game draws.")
    notes = []
    if img.mode in ("RGBA", "LA", "PA") or (img.mode == "P" and "transparency" in img.info):
        rgba = img.convert("RGBA")
        if any(a < 255 for a in rgba.getchannel("A").tobytes()):
            notes.append("See-through parts were drawn as black.")
        flat = Image.new("RGB", img.size, (0, 0, 0))
        flat.paste(rgba, mask=rgba.getchannel("A"))
        rgb = flat
    else:
        rgb = None
    if tim.bits == 24:
        return Loaded(timlib.with_rgb(tim, (rgb or img.convert("RGB")).tobytes()), notes)
    if tim.bits not in (4, 8):
        raise ValueError("this tool can't load that kind of picture yet")
    palette = [timlib.rgb_of(c) for c in tim.palettes[pic.palette]]
    if rgb is None and img.mode == "P":
        theirs = img.getpalette() or []
        same = all(tuple(theirs[i * 3:i * 3 + 3]) == palette[i] for i in range(len(palette)) if i * 3 + 3 <= len(theirs))
        numbers = list(img.tobytes())        # one byte per pixel in a palette image
        if same and len(theirs) >= len(palette) * 3 and max(numbers) < len(palette):
            return Loaded(timlib.with_indices(tim, numbers), notes)
    flat = (rgb or img.convert("RGB")).tobytes()
    pixels = [tuple(flat[k:k + 3]) for k in range(0, len(flat), 3)]
    before = timlib.indices(tim)
    nearest: dict[tuple, int] = {}
    numbers, off = [], 0
    for k, colour in enumerate(pixels):
        # The palette number the picture had there, when it is this colour (a palette can hold one
        # colour twice): an unchanged pixel keeps its exact bytes.
        if palette[before[k]] == colour:
            numbers.append(before[k])
            continue
        if colour not in nearest:
            nearest[colour] = min(range(len(palette)),
                                  key=lambda i: sum((a - b) ** 2 for a, b in zip(palette[i], colour)))
        n = nearest[colour]
        off += palette[n] != colour
        numbers.append(n)
    if off:
        notes.append(f"{off:,} pixels were not one of the picture's {len(palette)} colours: the nearest one is used.")
    return Loaded(timlib.with_indices(tim, numbers), notes)


def _flatten(img):
    """The PNG as RGB, see-through parts drawn as black (the screen behind a card is black), and a note."""
    from PIL import Image
    if img.mode in ("RGBA", "LA", "PA") or (img.mode == "P" and "transparency" in img.info):
        rgba = img.convert("RGBA")
        flat = Image.new("RGB", img.size, (0, 0, 0))
        flat.paste(rgba, mask=rgba.getchannel("A"))
        see = any(a < 255 for a in rgba.getchannel("A").tobytes())
        return flat, ["See-through parts were drawn as black."] if see else []
    return img.convert("RGB"), []


def _nearest(colours: list, colour: tuple) -> int:
    return min(range(len(colours)), key=lambda i: sum((a - b) ** 2 for a, b in zip(colours[i], colour)))


def _card_from_png(img, current: bytes, layout: CardLayout) -> Loaded:
    """A card from a PNG of the screen. A tile whose pixels on the screen are as they are now stays as it
    is (its bytes too). A changed tile takes, of the room's colour rows, the one whose nearest colours
    are closest to its pixels on the screen overall (its own row on a tie), and each pixel its nearest
    colour in that row (a pixel whose colour is unchanged keeps its number while the row stays; one off
    the screen keeps its colour as near as the row has it). Tiles off the screen stay."""
    from PIL import Image, ImageChops, ImageStat
    if img.size != CARD_SIZE:
        raise ValueError(f"the picture is {img.width}×{img.height}; it must stay {CARD_SIZE[0]}×{CARD_SIZE[1]}, "
                         "the size of the screen.")
    rgb, notes = _flatten(img)
    if rgb.tobytes() == _card_image(current, layout).tobytes():
        return Loaded(current, notes)
    colours = {row: [screen_rgb(c) for c in _card_colours(current, row)] for row in layout.rows}
    # How far each row's nearest colours are from the PNG (Pillow's nearest-colour mapping), to choose a
    # changed tile's row; its pixels' numbers are then found exactly.
    cost = {}
    for row, cols in colours.items():
        pal = Image.new("P", (1, 1))
        pal.putpalette([c for colour in cols for c in colour])
        cost[row] = ImageChops.difference(rgb, rgb.quantize(palette=pal, dither=Image.Dither.NONE).convert("RGB"))
    out = bytearray(current)
    at, changed, recoloured, off = COLOUR_ROWS[2] * COLOUR_ROWS[3] * 2, 0, 0, 0
    nearest: dict = {}

    def near(row: int, colour: tuple) -> int:
        if (row, colour) not in nearest:
            nearest[(row, colour)] = _nearest(colours[row], colour)
        return nearest[(row, colour)]

    for x, y, word, numbers in _card_tiles(current, layout):
        seen = _on_screen(x, y)
        if seen is not None:
            top, bottom, sy = seen
            box = (x, sy, x + TILE, sy + bottom - top)
            flat = rgb.crop(box).tobytes()
            pixels = [tuple(flat[i:i + 3]) for i in range(0, len(flat), 3)]
            row = word & 511
            mine = colours[row]
            if any(mine[n] != q for n, q in zip(numbers[top * TILE:bottom * TILE], pixels)):
                changed += 1
                score = {r: sum(ImageStat.Stat(cost[r].crop(box)).sum2) for r in layout.rows}
                best = min(layout.rows, key=lambda r: (score[r], r != row, r))
                cols, new = colours[best], []
                for i, n in enumerate(numbers):
                    shown = top * TILE <= i < bottom * TILE
                    q = pixels[i - top * TILE] if shown else mine[n]
                    if best == row and mine[n] == q:
                        new.append(n)
                        continue
                    k = near(best, q)
                    off += shown and cols[k] != q
                    new.append(k)
                if best != row:
                    recoloured += 1
                    struct.pack_into("<H", out, at, (word & ~511 & 0xFFFF) | best)
                out[at + 2:at + 2 + TILE * TILE] = bytes(new)
        at += 2 + TILE * TILE
    notes.append(f"{changed} of the card's squares changed" +
                 (f"; {recoloured} took another set of colours." if recoloured else "."))
    if off:
        notes.append(f"{off:,} pixels were not one of their square's colours: the nearest one is used.")
    return Loaded(bytes(out), notes)
