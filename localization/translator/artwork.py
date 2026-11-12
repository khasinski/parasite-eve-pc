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

    @property
    def label(self) -> str:
        return f"{self.where}: {self.name}"


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
            for pic in PICTURES:
                lo, hi = self.span(pic)
                first, last = lo // SECTOR, (hi - 1) // SECTOR
                data = game.pe(first, last - first + 1)
                tim = data[lo - first * SECTOR:hi - first * SECTOR]
                if zlib.crc32(tim) != pic.crc:
                    raise NotTheDisc(f"{self.path.name}: the picture \"{pic.name}\" is not the game's own. "
                                     "Use an unchanged USA disc, not a patched one.")
                self.english[pic.id] = tim
                self.around[pic.id] = (first * SECTOR, data)
        finally:
            game.disc.f.close()

    def span(self, pic: Picture) -> tuple[int, int]:
        """The picture's PE.IMG bytes [start, end)."""
        lo = self.sectors[pic.entry] * SECTOR + pic.at
        return lo, lo + pic.length


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


def shown(disc: Disc, pic: Picture, pieces) -> bytes:
    """The picture's TIM as the game shows it with these pieces."""
    base, data = disc.around[pic.id]
    lo, hi = disc.span(pic)
    laid, _ = _lay(base, data, pieces)
    return laid[lo - base:hi - base]


def redrawn(disc: Disc, pic: Picture, pieces) -> bool:
    return shown(disc, pic, pieces) != disc.english[pic.id]


def put(disc: Disc, pic: Picture, pieces, tim: bytes | None) -> list[pieces_file.Piece]:
    """The pieces with the picture set to `tim` (its whole TIM, same length; None: back to the
    game's). Pieces outside the picture's sectors, and pieces in them that don't land on this disc,
    are kept as they are; the ones that land there are made again from the English bytes."""
    if tim is not None and len(tim) != pic.length:
        raise ValueError("a picture must keep its size")
    base, data = disc.around[pic.id]
    first, last = base // SECTOR, (base + len(data)) // SECTOR - 1
    laid, landed = _lay(base, data, pieces)
    lo, hi = disc.span(pic)
    now = bytearray(laid)
    now[lo - base:hi - base] = disc.english[pic.id] if tim is None else tim
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

def image(pic: Picture, tim_bytes: bytes):
    """The picture as a Pillow image: RGB for a 24-bit one; for a 4/8-bit one a palette image with
    the picture's own colours in its own order, so a paint program that keeps the palette gives
    the same palette numbers back."""
    from PIL import Image
    tim = timlib.read(tim_bytes)
    if tim.bits in (4, 8):
        img = Image.new("P", tim.size)
        colours = [c for colour in tim.palettes[pic.palette] for c in timlib.rgb_of(colour)]
        img.putpalette(colours, rawmode="RGB")
        img.putdata(timlib.indices(tim))
        return img
    return Image.frombytes("RGB", tim.size, timlib.rgb(tim, pic.palette))


def png(pic: Picture, tim_bytes: bytes) -> bytes:
    out = io.BytesIO()
    image(pic, tim_bytes).save(out, "PNG")
    return out.getvalue()


@dataclass
class Loaded:
    tim: bytes                       # the picture's new TIM
    notes: list[str]                 # what was done to fit it, in plain words


def from_png(pic: Picture, english: bytes, data: bytes, current: bytes | None = None) -> Loaded:
    """A PNG (any paint program's: palette, grey, RGB, with or without see-through) as the picture's
    TIM. Raises ValueError, in plain words, for one that can't be used."""
    from PIL import Image, UnidentifiedImageError
    try:
        img = Image.open(io.BytesIO(data))
        img.load()
    except (UnidentifiedImageError, OSError, ValueError, Image.DecompressionBombError) as e:
        raise ValueError(f"that file is not a picture this tool can read ({e})") from e
    tim = timlib.read(english)
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
    before = timlib.indices(timlib.read(current)) if current else timlib.indices(tim)
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
