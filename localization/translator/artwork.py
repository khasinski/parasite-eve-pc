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


def _tutorial(words: str, rest: str, extra: str = "") -> str:
    return ("A page of the Tutorial, as the screen shows it. Redraw " + words + ". The rest of the screen (" + rest +
            ") is the game's and stays as it is, whatever your PNG has there. The words are made of 16×16 squares, and "
            "each square can use only one of the room's sets of 256 colours: for each square you change, the tool picks "
            "the set that fits your drawing best, and each pixel becomes its nearest colour in that set." + extra +
            " Keep it {w}×{h}.")


STRIP = "the words at the bottom"
STRIP_SHOT = "the words at the bottom and, if you like, the game screenshot above them"
CONTROLLER = "the PE picture behind, the shadow, the controller pictures"
BEHIND = "the PE picture behind and the shadow"
SCREENSHOT = (" The screenshot is optional: it shows the game's own menus, with their words in English, and may stay "
              "as it is.")
GAME_OVER = " The Game Over screen above the words is optional and may stay in English."
SHADOWED = (" The shadow of the frame above falls on the top rows of the words: draw them as you see them there, and "
            "the tool takes the shadow back off.")
TUNE_UP = (" The Tune-up screenshot is on two pages, this one and the next: a change loaded from either PNG goes on "
           "both. If both PNGs change it, differently, the tool says so and doesn't load the second until they "
           "are the same.")
MENU = ("The Tutorial menu, as the screen shows it, with no choice picked. Redraw its words. The game keeps six copies "
        "of the menu: this one and five with the green ring on one choice. Yours goes into all six: the tool draws the "
        "game's green ring back on the five, at the same places as the English ones, so keep the choices where they "
        "are. The rest of the screen (the PE picture behind and the shadow) is the game's and stays as it is. The menu "
        "is made of 16×16 squares, and each square can use only one of the room's sets of 256 colours: for each "
        "square you change, the tool picks the set that fits your drawing best, and each pixel becomes its nearest "
        "colour in that set. Keep it {w}×{h}.")


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


@dataclass(frozen=True)
class Screen:
    """A page of the Tutorial as the screen shows it. Each Tutorial room (M353-M366) has one view made of
    several tile lists ("layers": the PE picture behind, a shadow, the words at the bottom, a picture
    panel...) that its script shows a few at a time. A layer is 16x16 8-bit tiles like a card's, on the
    room's colour rows; no texture tile is shared between layers (checked when the disc is read). The
    game draws a screen's layers by their depth, the farthest first (Render_DrawSpriteEntry,
    src/main/render/mesh_list.c: `(u32 at +0 >> 8) & 0xFFF`; same depth: the later list first), colour 0
    see-through, and the shadow layer (its tiles' texture word has bit 23) subtracted from what is
    under it. Checked pixel for pixel against the game's frames of every page of the Tutorial."""
    id: str
    where: str
    name: str
    room: int                # M number
    layers: tuple            # (tile list, x move, y move) for each layer the screen shows
    words: tuple             # the layers with words: the only ones a PNG of the screen changes
    crc: int                 # CRC-32 of its English bytes (`Disc.english`: colour rows, then each layer's tiles)
    tip: str
    copies: tuple = ()       # layers kept with the screen but not drawn: the menu's five copies with a choice ringed
    tints: tuple = ()

    @property
    def label(self) -> str:
        return f"{self.where}: {self.name}"


@dataclass(frozen=True)
class Glyph:
    """One word cut out of a page that holds many: a glyph of the resident pack's glyph table (PE.IMG entry 2,
    `Draw_LookupGlyphDescriptor`: u, v, colour row, width, height), 4-bit, on a page that PE.IMG entry
    `entry` sends to video memory as a raw image (the blob's list under RAW_IMAGES, `Scene_LoadFieldBg`). Its
    bytes: each pixel row's bytes, then its 16 colours. Where they are is read from the disc's glyph table
    and the entry's image list (and refused if they are not as the code below expects).
    Game Over (glyph 0x8B): drawn by Battle_StepPostBattle (src/main/battle/encounter.c) as one quad,
    1:1 at x 100-220, y 100-124 once it has grown and stopped, colour 0 see-through, no blending (none of
    its colours has the see-through bit), each colour times the drawing colour (120 and 116 on alternate
    frames: the two draw buffers) over 128 with the PlayStation's dither, on a black screen. Checked pixel
    for pixel against the game's frames (56 frames, both brightnesses)."""
    id: str
    where: str
    name: str
    entry: int               # PE.IMG entry holding the page
    glyph: int               # the glyph table's entry
    page: tuple              # (VRAM x, y) of the page the game draws it from (its texture page)
    size: tuple              # (width, height) in pixels, as the glyph table has it
    at: tuple                # (x, y) on the screen once it stands still: where the dither starts
    crc: int                 # CRC-32 of its English bytes (`Disc.english`: the pixel rows, then the 16 colours)
    tip: str
    brightness: tuple = (120, 116)    # the drawing colours it is shown with, one frame each
    tints: tuple = ()

    @property
    def label(self) -> str:
        return f"{self.where}: {self.name}"


RAW_IMAGES = 0xABADC06C       # key of a scene blob's raw image list (src/main/scene/room_load.c)
GLYPHS = 0x08                 # the resident pack's directory slot of the glyph table (pe1/resident_tables.h)
# The PlayStation's 4x4 dither, added to an 8-bit colour part before it is cut to 5 bits, by screen pixel.
DITHER = ((-4, 0, -3, 1), (2, -2, 3, -1), (-3, 1, -4, 0), (3, -1, 2, -2))
GAME_OVER_TIP = ("The words shown when Aya falls in a fight and the game is over. 16 colours: each pixel of yours "
                 "becomes the nearest of them, so draw with the colours of the saved PNG. Its first colour, black, is "
                 "see-through: the screen behind the words is black, and Aya, who lies behind them, shows only "
                 "there. The game draws the words a little darker than the PNG, with the PlayStation's fine dot "
                 "pattern (see the preview), in the middle of the screen. Keep the words inside {w}×{h}.")


@dataclass
class Layer:
    entry: int               # the room's tile list
    depth: int
    shadow: bool             # drawn by subtracting from what is under it
    word: bool               # a PNG of the screen changes it
    drawn: bool              # False for the menu's ringed copies
    tiles: list              # each tile's (x, y) on the screen (moved), in the order of the screen's bytes
    at: int                  # its first tile's byte in the screen's bytes


@dataclass
class ScreenLayout:
    layers: list             # Layer, in the order of the screen's bytes
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

# The Tutorial (title screen > Tutorial), rooms M353-M366: 30 screens, each made of its room's layers as the
# game's frames show them (the CTest original-game-tutorial's route, every page). In each room the last layer
# is the PE picture behind and the one before it the shadow; M354/M355 also show the big controller (e6/e7)
# and a close-up of the button. The menu (M353) is moved by the game: the picture behind up 6 rows, the
# shadow and the menu down 31 rows (and left 13 and 3 pixels); its bottom band (e6) is not drawn. Its
# copies e1-e5, each with the green ring on one choice, are the screens of choices 1-5; e0, with no choice
# ringed, is never shown but is the one the translator draws. Same bytes on both discs (the CRCs).
_TUTORIAL_CRC = {
    "tutorial/menu": 0x2505C632,
    "tutorial/controller-1": 0xB79BDD11, "tutorial/controller-2": 0x2BD21FA4, "tutorial/controller-3": 0xE0BB57CC,
    "tutorial/controller-4": 0x1F981358, "tutorial/controller-5": 0xCBAB06AA, "tutorial/controller-6": 0x3190C7A1,
    "tutorial/analog-1": 0x5558EA4F, "tutorial/analog-2": 0x127CC435, "tutorial/analog-3": 0x4B461748,
    "tutorial/analog-4": 0xC6CA2E3B, "tutorial/analog-5": 0x9B6C8ED8, "tutorial/analog-6": 0xCF7FF73C,
    "tutorial/analog-7": 0x13FA4145,
    "tutorial/icons-1": 0xB0CE99E6, "tutorial/icons-2": 0x803091B8, "tutorial/icons-3": 0x618773A8,
    "tutorial/icons-4": 0x1A57C742, "tutorial/icons-5": 0x4251734E, "tutorial/icons-6": 0x0D682FB2,
    "tutorial/icons-7": 0x51EC4288, "tutorial/icons-8": 0xCB2C33AF, "tutorial/icons-9": 0x050DFD15,
    "tutorial/battle-1": 0xAB776775, "tutorial/battle-2": 0x7A90447A, "tutorial/battle-3": 0xA47B1FF6,
    "tutorial/battle-4": 0x6CE8B03E, "tutorial/battle-5": 0x8BDACEDB, "tutorial/battle-6": 0x1DE4491A,
    "tutorial/save": 0x8F12C6D7,
}


def _tutorial_screens() -> tuple:
    out = [Screen("tutorial/menu", "Tutorial", "Menu (How to Use the Controller … Return to Main Title)", 353,
                  ((8, 0, -6), (7, -13, 31), (0, -3, 31)), (0,), _TUTORIAL_CRC.get("tutorial/menu", 0), MENU,
                  copies=(1, 2, 3, 4, 5))]

    def page(pid, name, room, layers, words, tip):
        out.append(Screen(pid, "Tutorial", name, room, tuple((e, 0, 0) for e in layers), words,
                          _TUTORIAL_CRC.get(pid, 0), tip))

    buttons = ("Directional buttons", "START button", "Triangle button", "Cross button", "Square button",
               "Circle button")
    for k, name in enumerate(buttons):              # strip k, the controller (e6), the close-up e(7+k)
        page(f"tutorial/controller-{k + 1}", f"Controller {k + 1}/6 · {name}", 354, (14, 13, 6, 7 + k, k), (k,),
             _tutorial(STRIP, CONTROLLER))
    for k, name in enumerate(("Left stick",) + buttons[1:] + ("Note on the analog mode",)):
        page(f"tutorial/analog-{k + 1}", f"Analog Controller {k + 1}/7 · {name}", 355, (16, 15, 7, 8 + k, k), (k,),
             _tutorial(STRIP, CONTROLLER))
    for k, name in enumerate(("Item Icon", "PE Icon", "Weapon Icon", "Armor Icon")):    # strip k, screenshot e(4+k)
        page(f"tutorial/icons-{k + 1}", f"About Icons {k + 1}/9 · {name}", 361, (9, 8, k, 4 + k), (k, 4 + k),
             _tutorial(STRIP_SHOT, BEHIND, SCREENSHOT))
    # M362: strip k with screenshot e5, e6, e7, e7, e8 (the Tune-up screenshot twice). Its strips are drawn
    # under the shadow (depth 1243), which darkens their top rows.
    for k, (name, shot) in enumerate((("System Icon", 5), ("Sort Icon", 6), ("Tune-up Icon", 7),
                                      ("Added effects", 7), ("BP Icon", 8))):
        page(f"tutorial/icons-{k + 5}", f"About Icons {k + 5}/9 · {name}", 362, (10, 9, k, shot), (k, shot),
             _tutorial(STRIP_SHOT, BEHIND, SCREENSHOT + SHADOWED + (TUNE_UP if shot == 7 else "")))
    for k, name in enumerate(("Gauges", "Attack", "Items", "Parasite Energy", "Escape", "Game Over")):
        page(f"tutorial/battle-{k + 1}", f"Battle Screen {k + 1}/6 · {name}", 363, (13, 12, k, 6 + k), (k, 6 + k),
             _tutorial(STRIP_SHOT if k < 5 else "the words at the bottom and, if you like, the Game Over screen",
                       BEHIND, SCREENSHOT if k < 5 else GAME_OVER))
    # M366: the words (drawn under the shadow, like M362's) and a picture of the save room, which has none.
    page("tutorial/save", "How to Save", 366, (3, 2, 0, 1), (0,),
         _tutorial(STRIP, "the PE picture behind, the shadow and the picture of the save room", SHADOWED))
    return tuple(out)


# Game Over: glyph 0x8B of the system page (PE.IMG entry 9, VRAM 448,0, read at every scene change). The rest of
# that page holds the menus' and the fight's other words, not part of this picture.
GAME_OVER_WORDS = Glyph("game-over", "Game Over", "Game Over", 9, 0x8B, (448, 0), (120, 24), (100, 100), 0x19112F64,
                        GAME_OVER_TIP)

PICTURES = PICTURES + _tutorial_screens() + (GAME_OVER_WORDS,)
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
                if isinstance(pic, (Card, Screen)):
                    room = game.rooms[pic.room - 1]
                    if pic.room not in rooms:
                        rooms[pic.room] = (room[0] * SECTOR, game.pe(room[0], room[1] + room[2] + room[3]))
                    base, data = rooms[pic.room]
                    try:
                        segments, layout = (_card_parts(room, data, pic.view) if isinstance(pic, Card)
                                            else _screen_parts(room, data, pic))
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
                if isinstance(pic, Glyph):
                    first = game.sectors[pic.entry]
                    data = game.pe(first, game.sectors[pic.entry + 1] - first)
                    try:
                        segments = _glyph_parts(pic, data, game.resident_pack())
                    except (ValueError, struct.error) as e:
                        raise NotTheDisc(f"{self.path.name}: the picture \"{pic.name}\" is not the game's own ({e}). "
                                         "Use an unchanged USA disc, not a patched one.") from e
                    english = b"".join(data[a:a + n] for a, n in segments)
                    if zlib.crc32(english) != pic.crc:
                        raise NotTheDisc(f"{self.path.name}: the picture \"{pic.name}\" is not the game's own. "
                                         "Use an unchanged USA disc, not a patched one.")
                    self.english[pic.id], self.around[pic.id] = english, (first * SECTOR, data)
                    self.segments[pic.id] = segments
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


def _raw_images(blob: bytes):
    """(byte of the pixels, VRAM x, y, w, h) of each raw image in a scene blob's list (key RAW_IMAGES), as
    Scene_LoadFieldBg uploads them: {u32 length from its own first byte, RECT, pixels}, until a length of 0."""
    sub = struct.unpack_from("<I", blob, 4)[0]
    table = struct.unpack_from("<I", blob, sub + 8)[0]
    for k in range(table >> 22):
        _, payload, key = struct.unpack_from("<3I", blob, (table & 0x3FFFFF) + 12 * k)
        if key != RAW_IMAGES:
            continue
        at = payload & 0xFFFFFF
        while True:
            length = struct.unpack_from("<I", blob, at)[0]
            if length == 0:
                return
            x, y, w, h = struct.unpack_from("<4H", blob, at + 4)
            if length != 12 + w * h * 2:
                raise ValueError(f"a raw image at byte {at} is not its length")
            yield at + 12, x, y, w, h
            at += length
    raise ValueError("no raw image list")


def _glyph_parts(pic: Glyph, data: bytes, resident: bytes) -> list:
    """A glyph's bytes in its entry (`data`, from the entry's first byte): each pixel row, then its 16 colours,
    found from the disc's own glyph table and the entry's raw image that covers the glyph's page."""
    table = struct.unpack_from("<i", resident, GLYPHS)[0]
    u, v, clut, w, h, mode = struct.unpack_from("<BBHBBB", resident, table + 8 * pic.glyph)
    if (w, h) != pic.size or mode != 0 or u % 2 or w % 2:
        raise ValueError(f"glyph {pic.glyph:#x} is {w}x{h} at {u},{v} (mode {mode})")
    cx, cy = (clut & 63) * 16, clut >> 6

    def byte(x: int, y: int) -> int | None:              # (VRAM word x, y) -> byte in the entry
        for at, rx, ry, rw, rh in _raw_images(data):
            if rx <= x and x < rx + rw and ry <= y < ry + rh:
                return at + ((y - ry) * rw + x - rx) * 2
        return None

    x0, y0 = pic.page[0] + u // 4, pic.page[1] + v
    first, last, colours = byte(x0, y0), byte(pic.page[0] + (u + w - 1) // 4, y0 + h - 1), byte(cx, cy)
    if first is None or last is None or colours is None or byte(cx + 15, cy) != colours + 30:
        raise ValueError(f"glyph {pic.glyph:#x} is not in one raw image of entry {pic.entry}")
    stride = (byte(x0, y0 + 1) or 0) - first
    if stride <= 0 or last != first + (h - 1) * stride + ((u + w - 1) // 4 - u // 4) * 2:
        raise ValueError(f"glyph {pic.glyph:#x}'s rows are not in one raw image")
    first += (u % 4) // 2
    return [(first + r * stride, w // 2) for r in range(h)] + [(colours, 32)]


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


SHADOW_BIT = 1 << 23              # in a tile's texture word: the tile is drawn subtracted (the Tutorial's shadow)


def _room_layers(room: tuple, data: bytes) -> tuple:
    """Every tile list of a room's one view (the Tutorial's rooms): the colour rows' segment, {list: (depth,
    shadow, [(x, y, texture word's byte, its 16 rows' segments)])} and the colour rows the tiles use. Raises
    ValueError if the room is not as expected: a tile not 16x16 8-bit on the room's colour rows, a list
    mixing drawing modes, two tiles of a list at one place, or two tiles (of any list) sharing pixels."""
    _, scratch, texture, _ = room
    rects = list(_uploads(data[:scratch * SECTOR], 0)) + \
        list(_uploads(data[scratch * SECTOR:(scratch + texture) * SECTOR], scratch * SECTOR))

    def vram(x: int, y: int, n: int) -> int:
        found = [off + ((y - ry) * w + x - rx) * 2 for off, rx, ry, w, h in rects
                 if rx <= x and x + n <= rx + w and ry <= y < ry + h]
        if len(found) != 1:
            raise ValueError(f"video memory {x},{y} is in {len(found)} of the room's pictures")
        return found[0]

    cx, cy, cw, ch = COLOUR_ROWS
    colours = (vram(cx, cy, cw), cw * ch * 2)
    if vram(cx, cy + ch - 1, cw) != colours[0] + (ch - 1) * cw * 2:
        raise ValueError("the colour rows are not one picture")
    at = (scratch + texture) * SECTOR
    run = data[at:]
    directory = struct.unpack_from("<I", run, 4)[0]
    packed = struct.unpack_from("<I", run, directory + 0x1C)[0]
    geom = struct.unpack_from("<I", run, (packed & 0x3FFFFF) + 4)[0] & 0xFFFFFF
    count = struct.unpack_from("<H", run, geom + 6)[0]
    table = geom + struct.unpack_from("<i", run, geom + 0x14)[0]
    lists, rows, texels = {}, set(), []
    for i in range(count):
        e = table + i * 0x38
        if not run[e] & 2 or run[e + 0x24] != 0:
            raise ValueError(f"tile list {i} is not one of the room's view")
        n = struct.unpack_from("<H", run, e + 0x26)[0]
        pos, uv = struct.unpack_from("<ii", run, e + 0x28)
        sx, sy = struct.unpack_from("<hh", run, e + 0xC)
        tiles, seen, modes = [], set(), set()
        for k in range(n):
            a = struct.unpack_from("<I", run, e + pos + k * 4)[0]
            w0 = struct.unpack_from("<I", run, e + uv + k * 8)[0]
            x, y = sx + (a >> 22), sy + ((a >> 12) & 1023)
            u, v = run[e + uv + k * 8 + 4], run[e + uv + k * 8 + 3]
            page_x, page_y = (w0 >> 10) & 0x3C0, (w0 >> 7) & 256
            if u % 2 or (x, y) in seen or (w0 >> 5) & 0x3F0 or not cy <= w0 & 511 < cy + ch:
                raise ValueError(f"tile {k} of list {i} is not a 16x16 tile on the room's colour rows")
            seen.add((x, y))
            modes.add(bool(w0 & SHADOW_BIT))
            rows.add(w0 & 511)
            rows_at = [vram(page_x + u // 2, page_y + v + j, TILE // 2) for j in range(TILE)]
            texels.extend(rows_at)
            tiles.append((x, y, at + e + uv + k * 8, rows_at))
        if len(modes) > 1:
            raise ValueError(f"tile list {i} mixes drawing modes")
        lists[i] = ((struct.unpack_from("<I", run, e)[0] >> 8) & 0xFFF, modes == {True}, tiles)
    texels.sort()
    if any(b < a + TILE for a, b in zip(texels, texels[1:])):
        raise ValueError("two of the room's tiles share pixels")
    return colours, lists, tuple(sorted(rows))


def _screen_parts(room: tuple, data: bytes, screen: Screen) -> tuple[list, ScreenLayout]:
    """A Tutorial screen's bytes in its room's sectors: the colour rows, then for each of its layers (and the
    menu's copies) each tile's texture word's low half and its 16 rows of 16 pixels; and its layout."""
    colours, lists, rows = _room_layers(room, data)
    segments, layers, at = [colours], [], colours[1]
    shown = [(e, dx, dy, True) for e, dx, dy in screen.layers]
    word_move = {e: (dx, dy) for e, dx, dy in screen.layers if e in screen.words}
    shown += [(e, *word_move[screen.words[0]], False) for e in screen.copies]
    for e, dx, dy, drawn in shown:
        if e not in lists:
            raise ValueError(f"the room has no tile list {e}")
        depth, shadow, tiles = lists[e]
        layers.append(Layer(e, depth, shadow, e in screen.words, drawn, [(x + dx, y + dy) for x, y, _, _ in tiles], at))
        for _, _, word, rows_at in tiles:
            segments.append((word, 2))
            segments.extend((r, TILE) for r in rows_at)
        at += len(tiles) * (2 + TILE * TILE)
    if sum(1 for layer in layers if layer.shadow and layer.drawn) > 1:
        raise ValueError("a screen with two shadows")
    menu = next((layer for layer in layers if layer.word and layer.drawn), None)
    if any(sorted(menu.tiles) != sorted(layer.tiles) for layer in layers if not layer.drawn):
        raise ValueError("the menu's copies are not laid out as the menu")
    return segments, ScreenLayout(layers, rows)


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
    if isinstance(pic, Glyph):
        return pic.size
    return CARD_SIZE if isinstance(pic, (Card, Screen)) else timlib.read(data).size


def glyph_numbers(pic: Glyph, data: bytes) -> list[int]:
    """A glyph's palette numbers, row by row (4-bit: the low half of a byte is the left pixel)."""
    w, h = pic.size
    return [b >> s & 15 for b in data[:w * h // 2] for s in (0, 4)]


def glyph_colours(pic: Glyph, data: bytes) -> tuple:
    w, h = pic.size
    return struct.unpack_from("<16H", data, w * h // 2)


def _with_numbers(pic: Glyph, data: bytes, numbers: list[int]) -> bytes:
    w, h = pic.size
    return bytes(numbers[k] | numbers[k + 1] << 4 for k in range(0, w * h, 2)) + data[w * h // 2:]


def dithered(colour: int, brightness: int, x: int, y: int) -> tuple[int, int, int]:
    """A colour drawn as a textured quad with a drawing colour (128 = as it is) at screen pixel (x, y): each
    5-bit part as 8 bits times the drawing colour over 128, plus the PlayStation's dither there, cut to 5
    bits, then as the screen shows it."""
    d = DITHER[y & 3][x & 3]
    return screen_rgb(sum((max(0, min(255, (((colour >> s) & 31) << 3) * brightness // 128 + d)) >> 3) << s
                          for s in (0, 5, 10)))


def in_game(pic: Glyph, data: bytes, brightness: int | None = None):
    """The glyph as the game's screen shows it once it stands still: darker by the drawing colour, dithered
    by its place on the screen, over the black screen (colour 0 is see-through)."""
    from PIL import Image
    b = pic.brightness[0] if brightness is None else brightness
    colours, w = glyph_colours(pic, data), pic.size[0]
    img = Image.new("RGB", pic.size)
    img.putdata([dithered(colours[n], b, pic.at[0] + k % w, pic.at[1] + k // w) if colours[n] else (0, 0, 0)
                 for k, n in enumerate(glyph_numbers(pic, data))])
    return img


def previews(pic) -> int:
    """How many copies the Pictures window shows below the picture: its tints, or one as in the game."""
    return len(pic.tints) or (1 if isinstance(pic, Glyph) else 0)


def preview_png(pic, data: bytes) -> bytes:
    if isinstance(pic, Glyph):
        out = io.BytesIO()
        in_game(pic, data).save(out, "PNG")
        return out.getvalue()
    return tinted_png(pic, data)


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


_RGB: dict[int, tuple] = {}


def _rgb(colour: int) -> tuple[int, int, int]:
    colour &= 0x7FFF
    got = _RGB.get(colour)
    if got is None:
        got = _RGB[colour] = screen_rgb(colour)
    return got


def _subtract(under: int, shadow: int) -> int:
    """A shadow pixel drawn subtracted (the GPU's B-F): each 5-bit part, at least 0."""
    return (max(0, (under & 31) - (shadow & 31)) | max(0, (under >> 5 & 31) - (shadow >> 5 & 31)) << 5 |
            max(0, (under >> 10 & 31) - (shadow >> 10 & 31)) << 10)


def _dist(a: tuple, b: tuple) -> int:
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def _screen_colours(data: bytes) -> tuple:
    """The colour rows 480-511, 256 colours each, as one list (row r's colour i at (r - 480) * 256 + i)."""
    return struct.unpack_from(f"<{COLOUR_ROWS[2] * COLOUR_ROWS[3]}H", data, 0)


def _layer_tiles(data: bytes, layer: Layer):
    """Each tile's (x, y on the screen, texture word's low half, its 256 palette numbers, its first byte)."""
    at = layer.at
    for x, y in layer.tiles:
        yield x, y, struct.unpack_from("<H", data, at)[0], data[at + 2:at + 2 + TILE * TILE], at
        at += 2 + TILE * TILE


def _drawing(layout: ScreenLayout, choice: int | None = None) -> list:
    """The layers the screen draws, in the game's order: the deepest first, at one depth the later list
    first. `choice` (the menu, 1-5): the copy with that choice ringed in place of the menu."""
    drawn = [layer for layer in layout.layers if layer.drawn]
    if choice:
        copies = [layer for layer in layout.layers if not layer.drawn]
        drawn = [copies[choice - 1] if layer.word else layer for layer in drawn]
    return sorted(drawn, key=lambda layer: (-layer.depth, -layer.entry))


def _draw(screen: list, colours: tuple, data: bytes, layer: Layer, last=None, pos: int = 0, shade=None) -> None:
    """One layer onto the screen (15-bit colours): colour 0 see-through; a shadow's pixels (with the
    see-through bit, as the GPU blends only those) subtracted, the rest put. `last` gets `pos` where an
    ordinary pixel was put, `shade` the shadow's colour where one was subtracted."""
    w, h = CARD_SIZE
    top = COLOUR_ROWS[1]
    for x, y, word, numbers, _ in _layer_tiles(data, layer):
        base = ((word & 511) - top) * 256
        for j in range(max(0, -y), min(TILE, h - y)):
            row = (y + j) * w + x
            for i in range(max(0, -x), min(TILE, w - x)):
                c = colours[base + numbers[j * TILE + i]]
                if not c:
                    continue
                if layer.shadow and c & 0x8000:
                    screen[row + i] = _subtract(screen[row + i], c)
                    if shade is not None:
                        shade[row + i] = c
                else:
                    screen[row + i] = c
                    if last is not None:
                        last[row + i] = pos


def _compose(data: bytes, layout: ScreenLayout, choice: int | None = None) -> list:
    colours = _screen_colours(data)
    screen = [0] * (CARD_SIZE[0] * CARD_SIZE[1])
    for layer in _drawing(layout, choice):
        _draw(screen, colours, data, layer)
    return screen


def screen_image(data: bytes, layout: ScreenLayout, choice: int | None = None):
    """A Tutorial screen as the game shows it (nothing drawn: black). `choice` (the menu, 1-5): the screen
    with that choice picked, the copy with its green ring in place of the menu."""
    from PIL import Image
    img = Image.new("RGB", CARD_SIZE)
    img.putdata([_rgb(v) for v in _compose(data, layout, choice)])
    return img


class Disagree(ValueError):
    """A picture shown on two screens (the Tune-up screenshot) changed differently in their two PNGs."""

    def __init__(self, other: Screen, message: str) -> None:
        super().__init__(message)
        self.other = other


def shared_with(pic) -> list:
    """(other screen, layer) for each word layer of a Tutorial screen that another screen shows too."""
    if not isinstance(pic, Screen):
        return []
    return [(other, e) for other in PICTURES if isinstance(other, Screen) and other is not pic
            and other.room == pic.room for e in pic.words if e in other.words]


def image(pic, tim_bytes: bytes, layout: CardLayout | None = None):
    """The picture as a Pillow image: RGB for a 24-bit one, a card and a Tutorial screen; for a 4/8-bit one
    a palette image with the picture's own colours in its own order, so a paint program that keeps the
    palette gives the same palette numbers back."""
    from PIL import Image
    if isinstance(pic, Card):
        return _card_image(tim_bytes, layout)
    if isinstance(pic, Screen):
        return screen_image(tim_bytes, layout)
    if isinstance(pic, Glyph):
        img = Image.new("P", pic.size)
        img.putpalette([c for colour in glyph_colours(pic, tim_bytes) for c in timlib.rgb_of(colour)], rawmode="RGB")
        img.putdata(glyph_numbers(pic, tim_bytes))
        return img
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
    if isinstance(pic, Screen):
        return _screen_from_png(pic, img, english, current or english, layout)
    # On the picture as it is now: a translation that changed a picture's colours (the German and Chinese
    # packs did, on the map) keeps them; only the pixels are the PNG's (put() keeps the length).
    now = current or english
    if isinstance(pic, Glyph):
        if img.size != pic.size:
            raise ValueError(f"the picture is {img.width}×{img.height}; it must stay {pic.size[0]}×{pic.size[1]}, "
                             "the size the game draws.")
        palette = [timlib.rgb_of(c) for c in glyph_colours(pic, now)]
        numbers, notes = _palette_numbers(img, palette, glyph_numbers(pic, now))
        return Loaded(_with_numbers(pic, now, numbers), notes)
    tim = timlib.read(now)
    if img.size != tim.size:
        raise ValueError(f"the picture is {img.width}×{img.height}; it must stay {tim.width}×{tim.height}, "
                         "the size the game draws.")
    if tim.bits == 24:
        rgb, notes = _see_through_black(img)
        return Loaded(timlib.with_rgb(tim, (rgb or img.convert("RGB")).tobytes()), notes)
    if tim.bits not in (4, 8):
        raise ValueError("this tool can't load that kind of picture yet")
    palette = [timlib.rgb_of(c) for c in tim.palettes[pic.palette]]
    numbers, notes = _palette_numbers(img, palette, timlib.indices(tim))
    return Loaded(timlib.with_indices(tim, numbers), notes)


def _see_through_black(img) -> tuple:
    """(the PNG as RGB with its see-through parts drawn black, or None when it has none, and a note)."""
    from PIL import Image
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
    return rgb, notes


def _palette_numbers(img, palette: list, before: list) -> tuple[list[int], list[str]]:
    """A PNG as palette numbers of `palette` (its colours as RGB) on a picture whose numbers are `before`, and
    notes: a palette PNG with these colours in this order gives its numbers back as they are; otherwise each
    pixel the number it had when it is still that colour, else the nearest colour's."""
    rgb, notes = _see_through_black(img)
    if rgb is None and img.mode == "P":
        theirs = img.getpalette() or []
        same = all(tuple(theirs[i * 3:i * 3 + 3]) == palette[i] for i in range(len(palette)) if i * 3 + 3 <= len(theirs))
        numbers = list(img.tobytes())        # one byte per pixel in a palette image
        if same and len(theirs) >= len(palette) * 3 and max(numbers) < len(palette):
            return numbers, notes
    flat = (rgb or img.convert("RGB")).tobytes()
    pixels = [tuple(flat[k:k + 3]) for k in range(0, len(flat), 3)]
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
    return numbers, notes


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


class _Rows:
    """The room's colour rows, for choosing a changed square's row and its pixels' numbers."""

    def __init__(self, colours: tuple, rows: tuple) -> None:
        self.colours, self.rows = colours, rows
        top = COLOUR_ROWS[1]
        self.of = {r: colours[(r - top) * 256:(r - top + 1) * 256] for r in rows}
        self.zero = {r: next((i for i, c in enumerate(cols) if c == 0), None) for r, cols in self.of.items()}
        self.index = {}
        for r, cols in self.of.items():
            first: dict[int, int] = {}
            for i, c in enumerate(cols):
                first.setdefault(c, i)
            self.index[r] = first
        self._near: dict = {}
        self._opaque: dict = {}

    def opaque(self, row: int, shadow: int) -> list:
        """(number, colour on the screen) of the row's drawn colours, darkened by `shadow`."""
        key = (row, shadow)
        if key not in self._opaque:
            self._opaque[key] = [(i, _rgb(_subtract(c, shadow) if shadow else c))
                                 for i, c in enumerate(self.of[row]) if c]
        return self._opaque[key]

    def nearest(self, row: int, shadow: int, colour: tuple) -> tuple[int, int]:
        """(distance, number) of the row's drawn colour nearest to `colour` on the screen (darkened by
        `shadow`): the first of the nearest."""
        key = (row, shadow, colour)
        got = self._near.get(key)
        if got is None:
            got = self._near[key] = min((_dist(rgb, colour), i) for i, rgb in self.opaque(row, shadow))
        return got

    def number_of(self, row: int, colour: int) -> int:
        """The number of a 15-bit colour in the row: the same colour if it has it (see-through for
        see-through), else the nearest drawn colour."""
        got = self.index[row].get(colour)
        if got is not None:
            return got
        return self.nearest(row, 0, _rgb(colour))[1]


def _screen_from_png(pic: Screen, img, english: bytes, current: bytes, layout: ScreenLayout) -> Loaded:
    """A Tutorial screen from a PNG of it. Only its word layers change: of each, the pixels the screen
    shows (not covered by a layer drawn later, inside the screen). A square whose pixels on the screen are
    as they are now stays as it is (its bytes too). A changed square takes, of the room's colour rows, the
    one that can draw its pixels on the screen closest overall (its own row on a tie), and each pixel the
    number in that row that shows nearest to the PNG: a drawn colour, darkened where the shadow falls on
    it, or see-through (what is under it). Pixels of a word layer the screen doesn't show keep their
    colour. The menu's ringed copies follow the menu (_ring_copies). A layer another screen shows too (the
    Tune-up screenshot): this PNG's change is taken; a PNG that leaves it as the game has it keeps the
    change loaded from the other screen; both changed, differently: Disagree, nothing loaded."""
    from PIL import Image, ImageChops, ImageStat
    w, h = CARD_SIZE
    if img.size != CARD_SIZE:
        raise ValueError(f"the picture is {img.width}×{img.height}; it must stay {w}×{h}, the size of the screen.")
    rgb, notes = _flatten(img)
    want = rgb.tobytes()
    if want == screen_image(current, layout).tobytes():
        return Loaded(current, notes)
    colours = _screen_colours(current)
    rows = _Rows(colours, layout.rows)
    count = w * h
    screen, last, shade = [0] * count, [-1] * count, [0] * count
    under, place, shadow_pos = {}, {}, -1
    for pos, layer in enumerate(_drawing(layout)):
        if layer.word:
            under[layer.entry], place[layer.entry] = screen[:], pos
        if layer.shadow:
            shadow_pos = pos
        _draw(screen, colours, current, layer, last, pos, shade)
    out = bytearray(current)
    taken = bytearray(count)                       # the pixels a word layer shows: the PNG decides them
    off_by_layer: dict[int, int] = {}
    costs: dict = {}

    def cost_image(row: int):
        """How far each PNG pixel is from the row's nearest drawn colour (Pillow's nearest-colour mapping):
        to choose a fully shown square's row quickly."""
        if row not in costs:
            pal = Image.new("P", (1, 1))
            opaque = [c for _, c in rows.opaque(row, 0)] or [(0, 0, 0)]
            pal.putpalette([v for c in (opaque + opaque[-1:] * 256)[:256] for v in c])
            costs[row] = ImageChops.difference(rgb, rgb.quantize(palette=pal, dither=Image.Dither.NONE).convert("RGB"))
        return costs[row]

    for layer in layout.layers:
        if not (layer.word and layer.drawn):
            continue
        pos, beneath = place[layer.entry], under[layer.entry]
        shaded = shadow_pos > pos
        off = 0

        def shown(c: int, p: int, s: int) -> tuple:
            v = c if c else beneath[p]
            return _rgb(_subtract(v, s) if s else v)

        for x, y, word, numbers, at in _layer_tiles(current, layer):
            own = word & 511
            mine = rows.of[own]
            px, inside = [], 0
            for j in range(max(0, -y), min(TILE, h - y)):
                for i in range(max(0, -x), min(TILE, w - x)):
                    inside += 1
                    p = (y + j) * w + x + i
                    if last[p] <= pos:
                        taken[p] = 1
                        px.append((j * TILE + i, p, shade[p] if shaded else 0, tuple(want[p * 3:p * 3 + 3])))
            if all(shown(mine[numbers[k]], p, s) == q for k, p, s, q in px):
                continue
            if inside == len(px) and all(not s and mine[numbers[k]] for k, p, s, q in px):
                box = (max(0, x), max(0, y), min(w, x + TILE), min(h, y + TILE))
                score = {r: sum(ImageStat.Stat(cost_image(r).crop(box)).sum2) for r in layout.rows}
            else:
                score = {}
                for r in layout.rows:
                    z, total = rows.zero[r], 0
                    for k, p, s, q in px:
                        d = rows.nearest(r, s, q)[0]
                        if z is not None:
                            d = min(d, _dist(shown(0, p, s), q))
                        total += d
                    score[r] = total
            best = min(layout.rows, key=lambda r: (score[r], r != own, r))
            new, z = bytearray(numbers), rows.zero[best]
            seen = {k: (p, s, q) for k, p, s, q in px}
            for k, n in enumerate(numbers):
                if k in seen:
                    p, s, q = seen[k]
                    if best == own and shown(mine[n], p, s) == q:
                        continue
                    d, i = rows.nearest(best, s, q)
                    if z is not None:
                        ds = _dist(shown(0, p, s), q)
                        if ds < d or (ds == d and mine[n] == 0):
                            d, i = ds, z
                    new[k] = i
                    off += d > 0
                elif best != own:
                    new[k] = rows.number_of(best, mine[n])
            struct.pack_into("<H", out, at, (word & ~511 & 0xFFFF) | best)
            out[at + 2:at + 2 + TILE * TILE] = new
        off_by_layer[layer.entry] = off
    if any(not layer.drawn for layer in layout.layers):
        _ring_copies(english, current, out, layout, rows)
    for other, entry in shared_with(pic):
        layer = next(layer for layer in layout.layers if layer.entry == entry and layer.drawn)
        part = slice(layer.at, layer.at + len(layer.tiles) * (2 + TILE * TILE))
        new, now, game = (_layer_values(data, layer) for data in (out, current, english))
        if new == now:
            continue
        if new == game:                    # this PNG left it as the game has it: the other PNG's change stays
            out[part] = current[part]
            off_by_layer[entry] = 0
            notes.append(f'The screenshot is also on "{other.label}": the change loaded there is kept.')
        elif now == game:
            notes.append(f'The screenshot is also on "{other.label}": it changes there too.')
        else:
            raise Disagree(other, f'The screenshot is on two screens, this one and "{other.label}", and this PNG '
                                  'changes it differently from the one loaded there. Make it the same in both PNGs '
                                  '(or change it in one of them only). This PNG was not loaded.')
    changed = recoloured = 0
    for layer in layout.layers:
        if layer.word and layer.drawn:
            for _, _, word, _, at in _layer_tiles(current, layer):
                if out[at:at + 2 + TILE * TILE] != current[at:at + 2 + TILE * TILE]:
                    changed += 1
                    recoloured += struct.unpack_from("<H", out, at)[0] != word
    outside = sum(1 for p in range(count) if not taken[p] and want[p * 3:p * 3 + 3] != bytes(_rgb(screen[p])))
    notes.append(f"{changed} of the screen's squares with words changed" +
                 (f"; {recoloured} took another set of colours." if recoloured else "."))
    off = sum(off_by_layer.values())
    if off:
        notes.append(f"{off:,} pixels were not one of their square's colours: the nearest one is used.")
    if outside:
        notes.append(f"{outside:,} pixels you changed are not part of the words (the picture behind, the shadow and "
                     "the pictures without words stay the game's): they were left as they are.")
    return Loaded(bytes(out), notes)


def _layer_values(data: bytes, layer: Layer) -> list:
    """Every pixel of a layer's tiles as its 15-bit colour (what it draws, whatever its numbers and rows)."""
    colours, top = _screen_colours(data), COLOUR_ROWS[1]
    out = []
    for _, _, word, numbers, _ in _layer_tiles(data, layer):
        base = ((word & 511) - top) * 256
        out.extend(colours[base + n] for n in numbers)
    return out


RING_STEP, RING_SURE = 3, 8       # 5-bit steps: a ring pixel, a pixel surely of the ring


def _steps(a: int, b: int) -> int:
    return max(abs((a >> s & 31) - (b >> s & 31)) for s in (0, 5, 10))


def ring_of(english: bytes, layout: ScreenLayout, copy: Layer) -> dict:
    """The English green ring of one of the menu's copies: {tile place: [its pixels' numbers 0-255]}. The
    copies differ from the plain menu all over by a step or two of colour (each was packed into its colour
    rows on its own), which is not the ring; the ring is where a copy differs by RING_STEP or more, inside
    the box around the pixels that differ by RING_SURE or more (checked on the US discs: that is the ring
    and only the ring, on each copy)."""
    menu = next(layer for layer in layout.layers if layer.word and layer.drawn)
    plain = _layer_values(english, menu)
    ringed = _layer_values(english, copy)
    where = {place: t for t, place in enumerate(menu.tiles)}
    differ = []
    for t, (x, y) in enumerate(copy.tiles):
        t0 = where[(x, y)]
        for k in range(TILE * TILE):
            step = _steps(plain[t0 * 256 + k], ringed[t * 256 + k])
            if step >= RING_STEP:
                differ.append((x + k % TILE, y + k // TILE, step, (x, y), k))
    sure = [(px, py) for px, py, step, _, _ in differ if step >= RING_SURE]
    if not sure:
        return {}
    left, right = min(p[0] for p in sure) - 2, max(p[0] for p in sure) + 2
    high, low = min(p[1] for p in sure) - 2, max(p[1] for p in sure) + 2
    out: dict = {}
    for px, py, _, place, k in differ:
        if left <= px <= right and high <= py <= low:
            out.setdefault(place, []).append(k)
    return out


def _ring_copies(english: bytes, current: bytes, out: bytearray, layout: ScreenLayout, rows: _Rows) -> None:
    """The menu drawn once: each square of the menu that changed goes into the five copies as it now is,
    with the English green ring put back on it (ring_of: those pixels keep the English copy's colour). A
    square of the menu that didn't change leaves the copies' as they are. A square with ring pixels keeps
    the menu's row when that row has the ring's colours, else takes the row that draws the two together
    best."""
    menu = next(layer for layer in layout.layers if layer.word and layer.drawn)
    game = _screen_colours(english)
    top, size = COLOUR_ROWS[1], 2 + TILE * TILE

    def values(data: bytes, cols: tuple, at: int) -> list:
        base = ((struct.unpack_from("<H", data, at)[0] & 511) - top) * 256
        return [cols[base + n] for n in data[at + 2:at + size]]

    rings = {copy.entry: ring_of(english, layout, copy) for copy in layout.layers if not copy.drawn}
    for t, place in enumerate(menu.tiles):
        at0 = menu.at + t * size
        if out[at0:at0 + size] == current[at0:at0 + size]:
            continue
        own, numbers0 = struct.unpack_from("<H", out, at0)[0] & 511, out[at0 + 2:at0 + size]
        now = values(out, rows.colours, at0)
        for copy in (layer for layer in layout.layers if not layer.drawn):
            at = copy.at + copy.tiles.index(place) * size
            word = struct.unpack_from("<H", out, at)[0] & ~511 & 0xFFFF
            mine = values(english, game, at)
            ring = {k: mine[k] for k in rings[copy.entry].get(place, ())}
            if not ring:
                struct.pack_into("<H", out, at, word | own)
                out[at + 2:at + size] = numbers0
                continue
            wanted = [ring.get(k, c) for k, c in enumerate(now)]

            def cost(r: int) -> int:
                total = 0
                for c in wanted:
                    if c not in rows.index[r]:
                        total += 3 * 255 ** 2 if c == 0 else rows.nearest(r, 0, _rgb(c))[0]
                return total

            best = own if all(c in rows.index[own] for c in ring.values()) else \
                min(layout.rows, key=lambda r: (cost(r), r != own, r))
            new = bytearray(numbers0)
            for k, c in enumerate(wanted):
                if best != own or k in ring:
                    new[k] = rows.number_of(best, c)
            struct.pack_into("<H", out, at, word | best)
            out[at + 2:at + size] = new
