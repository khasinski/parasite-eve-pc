"""A TIM picture's pixels, out and back in, byte for byte.

A TIM (the PlayStation's picture file) is: u32 0x10, u32 flags (the low two bits: 4-bit, 8-bit,
16-bit or 24-bit colour; bit 3: a palette block follows), the palette block when there is one, then
the picture block. Each block is u32 length (12 + its data), u16 x, y, w, h (a place in the
console's video memory, w counted in 16-bit words) and its data.

Only the picture block's pixels are ever changed here. Everything else, the header, the palette and
any padding at the end of a row, is kept as the disc has it, so a picture read out and written back
unchanged gives the disc's bytes.

Colours: a palette entry is 15-bit (5 bits each of red, green, blue, low bits first, and a top
bit); 0x0000 is see-through where the game draws the picture as a sprite. 24-bit pictures hold
plain red, green, blue bytes, three to a pixel.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

BITS = {0: 4, 1: 8, 2: 16, 3: 24}


def rgb_of(colour: int) -> tuple[int, int, int]:
    """A 15-bit colour as 8-bit red, green, blue (each 5-bit value v as v*8 + v/4, so white is 255
    and every 5-bit value has its own 8-bit one)."""
    r, g, b = colour & 31, (colour >> 5) & 31, (colour >> 10) & 31
    return (r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)


@dataclass(frozen=True)
class Tim:
    data: bytes                      # the whole TIM, as on the disc
    bits: int                        # 4, 8, 16 or 24
    width: int                       # in pixels
    height: int
    row_bytes: int                   # bytes of one row of the picture block
    pixels_at: int                   # where the picture block's data starts in `data`
    palettes: tuple[tuple[int, ...], ...]   # 15-bit colours, one tuple per palette row (4/8-bit)

    @property
    def size(self) -> tuple[int, int]:
        return self.width, self.height


def read(data: bytes) -> Tim:
    """The TIM at the start of `data` (which may run on past its end)."""
    if len(data) < 20:
        raise ValueError("too short for a TIM")
    magic, flags = struct.unpack_from("<II", data, 0)
    if magic != 0x10 or flags & ~0xB or (flags & 3) not in BITS:
        raise ValueError("not a TIM picture")
    bits, at, palettes = BITS[flags & 3], 8, ()
    if flags & 8:
        length, _, _, w, h = struct.unpack_from("<IHHHH", data, at)
        if length != 12 + w * h * 2 or at + length > len(data):
            raise ValueError("the TIM's palette block is damaged")
        colours = struct.unpack_from(f"<{w * h}H", data, at + 12)
        palettes = tuple(colours[r * w:(r + 1) * w] for r in range(h))
        at += length
    elif bits in (4, 8):
        raise ValueError("a 4-bit or 8-bit TIM with no palette")
    length, _, _, w, h = struct.unpack_from("<IHHHH", data, at)
    if length != 12 + w * h * 2 or at + length > len(data) or not w or not h:
        raise ValueError("the TIM's picture block is damaged")
    row = w * 2
    width = {4: w * 4, 8: w * 2, 16: w, 24: row // 3}[bits]
    end = at + length
    return Tim(bytes(data[:end]), bits, width, h, row, at + 12, palettes)


def indices(tim: Tim) -> list[int]:
    """A 4-bit or 8-bit picture's pixels as palette numbers, row by row."""
    out = []
    for y in range(tim.height):
        row = tim.data[tim.pixels_at + y * tim.row_bytes:tim.pixels_at + (y + 1) * tim.row_bytes]
        if tim.bits == 4:
            out.extend(v for b in row for v in (b & 15, b >> 4))
        elif tim.bits == 8:
            out.extend(row)
        else:
            raise ValueError("only 4-bit and 8-bit pictures have palette numbers")
    return out


def rgb(tim: Tim, palette: int = 0) -> bytes:
    """The picture as red, green, blue bytes, row by row (a 4/8-bit one with palette row `palette`)."""
    if tim.bits == 24:
        used = tim.width * 3
        return b"".join(tim.data[tim.pixels_at + y * tim.row_bytes:tim.pixels_at + y * tim.row_bytes + used]
                        for y in range(tim.height))
    if tim.bits == 16:
        words = struct.unpack_from(f"<{tim.width * tim.height}H", tim.data, tim.pixels_at)
        return bytes(c for w in words for c in rgb_of(w))
    colours = [rgb_of(c) for c in tim.palettes[palette]]
    return bytes(c for i in indices(tim) for c in (colours[i] if i < len(colours) else (255, 0, 255)))


def with_rgb(tim: Tim, pixels: bytes) -> bytes:
    """The TIM's bytes with a 24-bit picture's pixels replaced by `pixels` (red, green, blue, row by
    row, the picture's own size). Row padding, header and all else stay."""
    if tim.bits != 24:
        raise ValueError("only a 24-bit picture takes red, green, blue pixels")
    used = tim.width * 3
    if len(pixels) != used * tim.height:
        raise ValueError("the pixels are not the picture's size")
    out = bytearray(tim.data)
    for y in range(tim.height):
        at = tim.pixels_at + y * tim.row_bytes
        out[at:at + used] = pixels[y * used:(y + 1) * used]
    return bytes(out)


def with_indices(tim: Tim, numbers: list[int]) -> bytes:
    """The TIM's bytes with a 4-bit or 8-bit picture's palette numbers replaced. The palette stays."""
    if tim.bits not in (4, 8):
        raise ValueError("only a 4-bit or 8-bit picture takes palette numbers")
    if len(numbers) != tim.width * tim.height or any(not 0 <= n < (16 if tim.bits == 4 else 256) for n in numbers):
        raise ValueError("the palette numbers are not the picture's size, or out of range")
    out = bytearray(tim.data)
    for y in range(tim.height):
        row = numbers[y * tim.width:(y + 1) * tim.width]
        packed = bytes(row[k] | (row[k + 1] << 4) for k in range(0, len(row), 2)) if tim.bits == 4 else bytes(row)
        at = tim.pixels_at + y * tim.row_bytes
        out[at:at + len(packed)] = packed
    return bytes(out)
