"""Read translation patches without running anything that came with them.

Fan patches for this game come as PPF (versions 1, 2 and 3) or xdelta. This reads both and
writes the patched disc image to a new file; the player's own image is only read.

xdelta files are VCDIFF (RFC 3284) with the xdelta3 additions: an Adler-32 of every window, and
"DJW" compression of a window's sections, the multi-table Huffman coding of xdelta3-djw.h. Every
window's Adler-32 is checked, so a patch made for a different disc stops with an error.
"""
from __future__ import annotations

import struct
import zlib
from pathlib import Path

CHUNK = 8 * 1024 * 1024

VCD_SOURCE, VCD_TARGET, VCD_ADLER = 1, 2, 4
VCD_DATACOMP, VCD_INSTCOMP, VCD_ADDRCOMP = 1, 2, 4
SEC_DJW = 1
NOOP, ADD, RUN, COPY = 0, 1, 2, 3
NEAR, SAME = 4, 3

MAX_CODELEN, TOTAL_CODES = 20, 22
BASIC_CODES, RUN_CODES, EXTRA_CODES = 5, 2, 15
BASIC_ORDER = (4, 5, 6, 7, 8)
EXTRA_ORDER = (9, 10, 3, 11, 2, 12, 13, 1, 14, 15, 16, 17, 18, 19, 20)
FAST_BITS = 12
REVERSED = bytes(int(f"{i:08b}"[::-1], 2) for i in range(256))


def _code_table():
    """The default instruction table of RFC 3284, section 5.6."""
    table = [((RUN, 0, 0), (NOOP, 0, 0))]
    table += [((ADD, size, 0), (NOOP, 0, 0)) for size in range(18)]
    for mode in range(9):
        table.append(((COPY, 0, mode), (NOOP, 0, 0)))
        table += [((COPY, size, mode), (NOOP, 0, 0)) for size in range(4, 19)]
    for mode in range(6):
        table += [((ADD, add, 0), (COPY, copy, mode)) for add in range(1, 5) for copy in range(4, 7)]
    for mode in range(6, 9):
        table += [((ADD, add, 0), (COPY, 4, mode)) for add in range(1, 5)]
    table += [((COPY, 4, mode), (ADD, 1, 0)) for mode in range(9)]
    if len(table) != 256:
        raise AssertionError("VCDIFF code table")
    return table


CODE_TABLE = _code_table()


class _Bits:
    """The DJW bit stream: each byte is read from its low bit up. Reversing the bytes first
    turns that into one plain stream, read from the top."""

    def __init__(self, data: bytes):
        self.data = data.translate(REVERSED)
        self.pos = 0
        self.size = len(data) * 8

    def read(self, count: int) -> int:
        if self.pos + count > self.size:
            raise ValueError("xdelta: compressed section ends early")
        at = self.pos
        self.pos += count
        word = int.from_bytes(self.data[at >> 3:(at >> 3) + 4], "big") << (8 * max(0, 4 - (len(self.data) - (at >> 3))))
        return (word >> (32 - (at & 7) - count)) & ((1 << count) - 1)


class _Prefix:
    """A canonical prefix code from its code lengths, with a table for the short codes."""

    def __init__(self, lengths, abs_max: int):
        counts = [0] * (abs_max + 2)
        for length in lengths:
            counts[length] += 1
        self.lengths = lengths
        self.min = next((i for i in range(1, abs_max + 1) if counts[i]), abs_max + 1)
        self.max = next((i for i in range(abs_max, 0, -1) if counts[i]), 0)
        if not self.max:
            raise ValueError("xdelta: empty prefix code")
        order = sorted((length, symbol) for symbol, length in enumerate(lengths) if length)
        self.symbols = [symbol for _, symbol in order]
        self.first, self.index, code, at = {}, {}, 0, 0
        for length in range(self.min, self.max + 1):
            self.first[length], self.index[length] = code, at
            code = (code + counts[length]) << 1
            at += counts[length]
        self.counts = counts
        self.fast = [None] * (1 << FAST_BITS)
        for length in range(self.min, min(self.max, FAST_BITS) + 1):
            for n in range(counts[length]):
                start = (self.first[length] + n) << (FAST_BITS - length)
                entry = (self.symbols[self.index[length] + n], length)
                for slot in range(start, start + (1 << (FAST_BITS - length))):
                    self.fast[slot] = entry

    def decode(self, bits: _Bits) -> int:
        code = 0
        for length in range(1, self.max + 1):
            code = code << 1 | bits.read(1)
            if length >= self.min and code - self.first[length] < self.counts[length]:
                return self.symbols[self.index[length] + code - self.first[length]]
        raise ValueError("xdelta: invalid compressed code")


def _decode_1_2(bits: _Bits, prefix: _Prefix, mtf: list[int], count: int, skip: int = 0) -> bytearray:
    """Move-to-front with runs in the 1-2 code. `skip`: a zero `skip` places back stays zero."""
    out = bytearray(count)
    n = rep = pending = shift = 0
    while n < count:
        if skip and n >= skip and out[n - skip] == 0:
            n += 1
            continue
        if rep:
            out[n] = mtf[0]
            n += 1
            rep -= 1
            continue
        if pending:
            symbol = mtf.pop(pending)
            mtf.insert(0, symbol)
            out[n] = symbol
            n += 1
            pending = 0
            continue
        pending = prefix.decode(bits)
        if pending <= 1:
            rep = (pending + 1) << shift
            pending = 0
            shift += 1
        else:
            pending -= 1
            shift = 0
    if rep:
        raise ValueError("xdelta: invalid repeat code")
    return out


def djw_decode(data: bytes, size: int) -> bytes:
    """Undo xdelta3's DJW coding of one section of `size` bytes."""
    if size <= 0:
        raise ValueError("xdelta: empty compressed section")
    bits = _Bits(data)
    groups = bits.read(3) + 1
    sector = (bits.read(5) + 1) * 5 if groups > 1 else size
    sectors = 1 + (size - 1) // sector

    extra = bits.read(4) + BASIC_CODES + RUN_CODES
    cl_lengths = [bits.read(4) for _ in range(extra)] + [0] * (TOTAL_CODES - extra)
    cl_prefix = _Prefix(cl_lengths, 15)
    cl_mtf = [0, *BASIC_ORDER, *EXTRA_ORDER]
    lengths = _decode_1_2(bits, cl_prefix, cl_mtf, 256 * groups, 256)
    tables = [_Prefix(list(lengths[g * 256:(g + 1) * 256]), MAX_CODELEN) for g in range(groups)]

    choice = bytes(sectors)
    if groups > 1:
        sel_lengths = [bits.read(3) for _ in range(groups + 1)]
        choice = _decode_1_2(bits, _Prefix(sel_lengths, 7), list(range(groups + 1)), sectors)

    # The loop that matters for speed: a 32-bit look at the stream, one table step per byte made.
    stream, pos, total = bits.data + bytes(8), bits.pos, bits.size
    from_bytes = int.from_bytes
    out, made = bytearray(size), 0
    shift_top = 32 - FAST_BITS
    mask = (1 << FAST_BITS) - 1
    for c in range(sectors):
        table = tables[choice[c]]
        fast = table.fast
        stop = min(size, made + sector)
        while made < stop:
            at = pos >> 3
            entry = fast[(from_bytes(stream[at:at + 4], "big") >> (shift_top - (pos & 7))) & mask]
            if entry is None:
                bits.pos = pos
                out[made] = table.decode(bits)
                pos = bits.pos
            else:
                out[made] = entry[0]
                pos += entry[1]
            made += 1
        if pos > total:
            raise ValueError("xdelta: compressed section ends early")
    return bytes(out)


def _varint(data, at: int) -> tuple[int, int]:
    value = 0
    while True:
        byte = data[at]
        at += 1
        value = value << 7 | byte & 0x7F
        if byte < 0x80:
            return value, at


def _section(data: bytes, compressed: bool) -> bytes:
    if not compressed:
        return data
    size, at = _varint(data, 0)
    return djw_decode(data[at:], size)


def _apply_window(source: bytes, data: bytes, inst: bytes, addr: bytes, size: int) -> bytes:
    target = bytearray(size)
    near, next_near = [0] * NEAR, 0
    same = [0] * (SAME * 256)
    source_size = len(source)
    d = i = a = t = 0
    while i < len(inst):
        code = inst[i]
        i += 1
        for kind, length, mode in CODE_TABLE[code]:
            if kind == NOOP:
                continue
            if not length:
                length, i = _varint(inst, i)
            if kind == ADD:
                target[t:t + length] = data[d:d + length]
                d += length
            elif kind == RUN:
                target[t:t + length] = data[d:d + 1] * length
                d += 1
            else:
                here = source_size + t
                if mode >= 2 + NEAR:
                    at = same[(mode - 2 - NEAR) * 256 + addr[a]]
                    a += 1
                else:
                    at, a = _varint(addr, a)
                    if mode == 1:
                        at = here - at
                    elif mode >= 2:
                        at += near[mode - 2]
                near[next_near] = at
                next_near = (next_near + 1) % NEAR
                same[at % (SAME * 256)] = at
                left, out = length, t
                if at < source_size:
                    piece = min(left, source_size - at)
                    target[out:out + piece] = source[at:at + piece]
                    at, out, left = at + piece, out + piece, left - piece
                if left:
                    at -= source_size
                    if at + left <= out:
                        target[out:out + left] = target[at:at + left]
                    else:           # overlapping copy: a repeating pattern
                        for k in range(left):
                            target[out + k] = target[at + k]
            t += length
    if t != size or len(target) != size:
        raise ValueError("xdelta: a window did not produce its stated size")
    return bytes(target)


def apply_xdelta(patch: bytes, source_path: Path, out_path: Path) -> None:
    if patch[:3] != b"\xd6\xc3\xc4" or patch[3] != 0:
        raise ValueError("not a VCDIFF patch")
    header, at = patch[4], 5
    secondary = None
    if header & 1:
        secondary = patch[at]
        at += 1
    if header & 2:
        raise ValueError("xdelta: custom code tables are not supported")
    if header & 4:
        size, at = _varint(patch, at)
        at += size
    if secondary not in (None, SEC_DJW):
        raise ValueError(f"xdelta: compression {secondary} is not supported")
    with open(source_path, "rb") as src, open(out_path, "wb") as out:
        while at < len(patch):
            indicator = patch[at]
            at += 1
            source_size = source_at = 0
            if indicator & VCD_TARGET:
                raise ValueError("xdelta: target windows are not supported")
            if indicator & VCD_SOURCE:
                source_size, at = _varint(patch, at)
                source_at, at = _varint(patch, at)
            _, at = _varint(patch, at)
            target_size, at = _varint(patch, at)
            delta = patch[at]
            at += 1
            data_size, at = _varint(patch, at)
            inst_size, at = _varint(patch, at)
            addr_size, at = _varint(patch, at)
            adler = None
            if indicator & VCD_ADLER:
                adler = struct.unpack_from(">I", patch, at)[0]
                at += 4
            data = _section(patch[at:at + data_size], bool(delta & VCD_DATACOMP))
            at += data_size
            inst = _section(patch[at:at + inst_size], bool(delta & VCD_INSTCOMP))
            at += inst_size
            addr = _section(patch[at:at + addr_size], bool(delta & VCD_ADDRCOMP))
            at += addr_size
            src.seek(source_at)
            source = src.read(source_size)
            if len(source) != source_size:
                raise ValueError("xdelta: the patch reads past the end of this disc: wrong disc?")
            window = _apply_window(source, data, inst, addr, target_size)
            if adler is not None and zlib.adler32(window) != adler:
                raise ValueError("xdelta: checksum differs: this patch was made for a different disc")
            out.write(window)


def ppf_records(patch: bytes) -> list[tuple[int, bytes]]:
    """(offset, bytes) of a PPF 1, 2 or 3 patch, in file order."""
    if patch[:3] != b"PPF" or patch[5:6] not in (b"\x00", b"\x01", b"\x02"):
        raise ValueError("not a PPF patch")
    version, undo, wide = patch[5], False, False
    if version == 0:
        at = 56
    elif version == 1:
        at = 56 + 4 + 1024
    else:
        block_check, undo, wide = patch[57], bool(patch[58]), True
        at = 60 + (1024 if block_check else 0)
    records = []
    while at < len(patch):
        if patch.startswith(b"@BEGIN_FILE_ID.DIZ", at):
            break
        if wide:
            offset, size = struct.unpack_from("<QB", patch, at)
            at += 9
        else:
            offset, size = struct.unpack_from("<IB", patch, at)
            at += 5
        if at + size > len(patch):
            raise ValueError("PPF record runs past the end of the patch")
        records.append((offset, patch[at:at + size]))
        at += size * (2 if undo else 1)
    return records


def apply_ppf(patch: bytes, source_path: Path, out_path: Path) -> None:
    records = ppf_records(patch)
    size = Path(source_path).stat().st_size
    if any(offset + len(data) > size for offset, data in records):
        raise ValueError("PPF: the patch writes past the end of this disc: wrong disc?")
    with open(source_path, "rb") as src, open(out_path, "wb") as out:
        while block := src.read(CHUNK):
            out.write(block)
        for offset, data in records:
            out.seek(offset)
            out.write(data)


def apply_patch(patch_path, source_path, out_path) -> str:
    """Write the patched image to a new file; returns the patch's format."""
    patch_path, source_path, out_path = Path(patch_path), Path(source_path), Path(out_path)
    if out_path.exists():
        raise ValueError(f"{out_path} already exists")
    if out_path.resolve() in (source_path.resolve(), patch_path.resolve()):
        raise ValueError("the patched image must be a new file")
    patch = patch_path.read_bytes()
    try:
        if patch[:3] == b"PPF":
            apply_ppf(patch, source_path, out_path)
            return "PPF"
        if patch[:3] == b"\xd6\xc3\xc4":
            apply_xdelta(patch, source_path, out_path)
            return "xdelta"
    except BaseException:
        out_path.unlink(missing_ok=True)
        raise
    raise ValueError(f"{patch_path.name}: not a PPF or xdelta patch")
