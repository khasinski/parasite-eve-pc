"""Letter pairs: a patch that writes two letters with one code (dual-tile encoding).

A patch can fit a longer script in the game's space by giving codes with no letter picture a pair of
letters, and teaching the dialogue box to read them: it puts a jump where the box reads its next
byte, and a short routine of its own that hands the box the pair's letters one at a time. Nothing
about a patch is hard-coded here: the patched game's own routine is run, instruction by instruction,
on each code, and what the box would read back is the code's letters.

The routine may sit in the boot program or in the resident data pack, which the game loads at
RESIDENT_AT; both are mapped the way the game has them in memory.
"""
from __future__ import annotations

import struct

# The dialogue box's read of its next byte in the USA boot programs (the same place in SLUS_006.62
# and SLUS_006.68), src/main/text: lbu v1,0(s3), then addiu v0,zero,0xFA; the box goes on at +8.
READ_AT = 0x28370
READ_WORDS = (0x92630000, 0x240200FA)
RESIDENT_AT = 0x800A8028                 # g_ResidentDataPack (include/pe1/resident_tables.h)
TEXT = 0x7FF00000                        # where the run puts the code it asks about: no game data
END = 0xFF
STEPS = 2000
S3, V1 = 19, 3


class Unknown(Exception):
    """The routine does something this reader doesn't model."""


class Memory:
    """The boot program and the resident pack as the game has them, plus the text being read."""

    def __init__(self, exe: bytes, pack: bytes):
        self.exe, self.pack = exe, pack
        self.base = struct.unpack_from("<I", exe, 0x18)[0]
        self.written: dict[int, int] = {}
        self.text = b""

    def byte(self, addr: int) -> int:
        addr &= 0xFFFFFFFF
        if addr in self.written:
            return self.written[addr]
        if TEXT <= addr < TEXT + len(self.text):
            return self.text[addr - TEXT]
        if RESIDENT_AT <= addr < RESIDENT_AT + len(self.pack):
            return self.pack[addr - RESIDENT_AT]
        at = addr - self.base + 0x800
        if 0x800 <= at < len(self.exe):
            return self.exe[at]
        if self.base <= addr < self.base + 0x200000:
            return 0                     # memory the boot program doesn't fill
        raise Unknown(f"reads 0x{addr:08X}")

    def word(self, addr: int) -> int:
        return int.from_bytes(bytes(self.byte(addr + i) for i in range(4)), "little")

    def store(self, addr: int, value: int, size: int) -> None:
        for i in range(size):
            self.written[(addr + i) & 0xFFFFFFFF] = (value >> (8 * i)) & 0xFF


def _signed(value: int, bits: int) -> int:
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def _step(mem: Memory, regs: list[int], pc: int) -> int | None:
    """Run the word at pc; returns where a jump or branch goes (after its delay slot), or None."""
    w = mem.word(pc)
    op, rs, rt, rd, sa, fn = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 63
    imm, simm = w & 0xFFFF, _signed(w & 0xFFFF, 16)
    a, b = regs[rs], regs[rt]
    out, target = None, None
    if op == 0:
        if fn == 0x00:
            out, reg = (b << sa), rd
        elif fn == 0x02:
            out, reg = (b >> sa), rd
        elif fn == 0x03:
            out, reg = (_signed(b, 32) >> sa), rd
        elif fn == 0x08:
            target = a
        elif fn in (0x20, 0x21):
            out, reg = a + b, rd
        elif fn in (0x22, 0x23):
            out, reg = a - b, rd
        elif fn == 0x24:
            out, reg = a & b, rd
        elif fn == 0x25:
            out, reg = a | b, rd
        elif fn == 0x26:
            out, reg = a ^ b, rd
        elif fn == 0x2A:
            out, reg = int(_signed(a, 32) < _signed(b, 32)), rd
        elif fn == 0x2B:
            out, reg = int(a < b), rd
        else:
            raise Unknown(f"instruction {w:08X}")
    elif op == 2:
        target = (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
    elif op in (4, 5):
        if (a == b) == (op == 4):
            target = pc + 4 + simm * 4
    elif op == 6:
        if _signed(a, 32) <= 0:
            target = pc + 4 + simm * 4
    elif op == 7:
        if _signed(a, 32) > 0:
            target = pc + 4 + simm * 4
    elif op == 1 and rt in (0, 1):
        if (_signed(a, 32) < 0) == (rt == 0):
            target = pc + 4 + simm * 4
    elif op in (0x08, 0x09):
        out, reg = a + simm, rt
    elif op == 0x0A:
        out, reg = int(_signed(a, 32) < simm), rt
    elif op == 0x0B:
        out, reg = int(a < (simm & 0xFFFFFFFF)), rt
    elif op == 0x0C:
        out, reg = a & imm, rt
    elif op == 0x0D:
        out, reg = a | imm, rt
    elif op == 0x0E:
        out, reg = a ^ imm, rt
    elif op == 0x0F:
        out, reg = imm << 16, rt
    elif op in (0x20, 0x24):
        value = mem.byte(a + simm)
        out, reg = (_signed(value, 8) if op == 0x20 else value), rt
    elif op in (0x21, 0x25):
        value = mem.byte(a + simm) | mem.byte(a + simm + 1) << 8
        out, reg = (_signed(value, 16) if op == 0x21 else value), rt
    elif op == 0x23:
        out, reg = mem.word(a + simm), rt
    elif op in (0x28, 0x29, 0x2B):
        mem.store(a + simm, b, {0x28: 1, 0x29: 2, 0x2B: 4}[op])
    else:
        raise Unknown(f"instruction {w:08X}")       # jal and the rest: the box's read doesn't call out
    if out is not None and reg:
        regs[reg] = out & 0xFFFFFFFF
    return target


def _read(mem: Memory, regs: list[int], start: int) -> int:
    """Run from the box's read until the box goes on; the byte it then has (v1)."""
    pc, back, steps = start, start + 8, 0
    while pc != back:
        steps += 1
        if steps > STEPS:
            raise Unknown("the routine doesn't come back")
        target = _step(mem, regs, pc)
        if target is not None:
            if _step(mem, regs, pc + 4) is not None:
                raise Unknown("a jump in a jump's delay slot")
            pc = target
        else:
            pc += 4
    return regs[V1] & 0xFF


def letters_of(mem: Memory, start: int, code: int) -> bytes:
    """What the box reads for one code: the code itself, or the letters the patch gives it."""
    mem.written.clear()
    mem.text = bytes((code, END))
    regs = [0] * 32
    regs[S3] = TEXT
    out = bytearray()
    while True:
        value = _read(mem, regs, start)
        if value == END and regs[S3] == TEXT + 1:
            return bytes(out)
        if value >= 0xF0 or len(out) >= 8:
            raise Unknown(f"code {code:02X} reads back {bytes(out + bytes((value,))).hex(' ')}")
        out.append(value)
        regs[S3] = (regs[S3] + 1) & 0xFFFFFFFF     # the box moves past a letter


def pairs(original_exe: bytes, exe: bytes, pack: bytes) -> tuple[dict[int, bytes], str]:
    """(code -> the letters the box reads for it, for each code that isn't simply itself; how the
    patch's box was read)."""
    word = lambda data, at: struct.unpack_from("<I", data, at)[0]
    if tuple(word(original_exe, READ_AT + 4 * i) for i in range(2)) != READ_WORDS:
        return {}, "not the USA boot program"
    if tuple(word(exe, READ_AT + 4 * i) for i in range(2)) == READ_WORDS:
        return {}, "one letter a code, as the game"
    mem = Memory(exe, pack)
    start = mem.base + READ_AT - 0x800
    found = {}
    try:
        for code in range(0xF0):
            letters = letters_of(mem, start, code)
            if letters != bytes((code,)):
                found[code] = letters
    except Unknown as error:
        return {}, f"the patch reads its letters in a way not known here ({error})"
    return found, f"{len(found)} codes stand for letter pairs"


def expand(raw: bytes, found: dict[int, bytes]) -> bytes:
    """A page with each pair code written out as its letters; command operands stay as they are."""
    if not found:
        return raw
    from . import validation
    out = bytearray()
    for command in validation.commands(raw):
        out += found.get(command[0], command) if len(command) == 1 else command
    return bytes(out)
