"""Letter pairs (petext/letterpairs.py): a patch's dialogue box reading two letters for one code.

The routine here is the test's own, written the way such a patch works: the box's read of its next
byte jumps to it; for a code 0x90 and up it keeps where the text goes on, points the box at the
code's two letters (ended by F0), and at F0 goes back. No disc or patch is needed.
"""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from petext import gametext as gt, letterpairs as lp  # noqa: E402

BASE = 0x80010000
R = {"zero": 0, "at": 1, "v0": 2, "v1": 3, "a0": 4, "s3": 19}


def i_type(op, rs, rt, imm):
    return (op << 26) | (R[rs] << 21) | (R[rt] << 16) | (imm & 0xFFFF)


def addiu(rt, rs, imm): return i_type(0x09, rs, rt, imm)
def sltiu(rt, rs, imm): return i_type(0x0B, rs, rt, imm)
def lui(rt, imm): return i_type(0x0F, "zero", rt, imm)
def lbu(rt, off, rs): return i_type(0x24, rs, rt, off)
def lw(rt, off, rs): return i_type(0x23, rs, rt, off)
def sw(rt, off, rs): return i_type(0x2B, rs, rt, off)
def addu(rd, rs, rt): return (R[rs] << 21) | (R[rt] << 16) | (R[rd] << 11) | 0x21
def sll(rd, rt, sa): return (R[rt] << 16) | (R[rd] << 11) | (sa << 6)
def j(addr): return (2 << 26) | ((addr >> 2) & 0x3FFFFFF)
def branch(op, rs, rt, pc, to): return i_type(op, rs, rt, (to - pc - 4) >> 2)


NOP = 0
ROUTINE = lp.RESIDENT_AT + 0x100          # the routine, its saved pointer and table in the resident pack
SAVED, TABLE = lp.RESIDENT_AT + 0x0F0, lp.RESIDENT_AT + 0x200
BACK = BASE + lp.READ_AT - 0x800 + 8      # where the box goes on


def build_exe_and_pack(pairs: dict[int, bytes]):
    """A boot program with the hook at READ_AT and a resident pack with the routine and table."""
    exe = bytearray(0x800 + lp.READ_AT + 0x100)
    struct.pack_into("<I", exe, 0x18, BASE)
    struct.pack_into("<II", exe, lp.READ_AT, *lp.READ_WORDS)
    original = bytes(exe)
    struct.pack_into("<II", exe, lp.READ_AT, j(ROUTINE), lbu("v1", 0, "s3"))
    pack = bytearray(0x400)
    t_hi, t_lo = (TABLE + 0x8000) >> 16, TABLE & 0xFFFF     # lo is added signed
    s_hi, s_lo = (SAVED + 0x8000) >> 16, SAVED & 0xFFFF
    code = []

    def here():
        return ROUTINE + 4 * len(code)

    def emit(*words):
        code.extend(words)

    # top: v1 = *s3 (also in the hook's delay slot)
    top = here()
    emit(lbu("v1", 0, "s3"), addiu("v0", "zero", 0xF0))
    beq_at = here()
    emit(0, NOP)                                    # beq v0,v1,restore (patched below)
    emit(sltiu("v0", "v1", 0x90))
    bne_at = here()
    emit(0, NOP)                                    # bne v0,zero,out (patched below)
    emit(sltiu("v0", "v1", 0xF0))
    beq_out = here()
    emit(0, NOP)                                    # beq v0,zero,out: commands are no pairs
    emit(lui("a0", s_hi), addiu("v0", "s3", 1), sw("v0", s_lo, "a0"))        # saved = s3 + 1
    emit(addiu("v1", "v1", -0x90), sll("a0", "v1", 1), addu("v1", "a0", "v1"))  # v1 = 3 * index
    emit(lui("a0", t_hi), addiu("a0", "a0", t_lo), addu("s3", "a0", "v1"))      # s3 = the pair
    out = here()
    emit(lbu("v1", 0, "s3"), j(BACK), addiu("v0", "zero", 0xFA))
    restore = here()
    emit(lui("a0", s_hi), lw("s3", s_lo, "a0"), j(top), NOP)
    code[(beq_at - ROUTINE) // 4] = branch(4, "v0", "v1", beq_at, restore)
    code[(bne_at - ROUTINE) // 4] = branch(5, "v0", "zero", bne_at, out)
    code[(beq_out - ROUTINE) // 4] = branch(4, "v0", "zero", beq_out, out)
    for k, w in enumerate(code):
        struct.pack_into("<I", pack, ROUTINE - lp.RESIDENT_AT + 4 * k, w)
    for c in range(0x90, 0xF0):                     # the table's other entries: two spaces
        at = TABLE - lp.RESIDENT_AT + 3 * (c - 0x90)
        pack[at:at + 3] = pairs.get(c, SPACES) + b"\xf0"
    return original, bytes(exe), bytes(pack)


EN = bytes([gt.CODE_OF["e"], gt.CODE_OF["n"]])
CH = bytes([gt.CODE_OF["c"], gt.CODE_OF["h"]])
SPACES = bytes([gt.CODE_OF[" "]] * 2)


class PairsTest(unittest.TestCase):
    def test_the_patchs_routine_gives_each_codes_letters(self):
        original, exe, pack = build_exe_and_pack({0x90: EN, 0x91: CH})
        found, how = lp.pairs(original, exe, pack)
        self.assertEqual(found[0x90], EN)
        self.assertEqual(found[0x91], CH)
        self.assertEqual(found[0xEF], SPACES)
        self.assertEqual(set(found), set(range(0x90, 0xF0)))     # each code below 0x90 is itself
        self.assertIn("letter pairs", how)

    def test_the_game_reads_one_letter_a_code(self):
        original, _, pack = build_exe_and_pack({})
        self.assertEqual(lp.pairs(original, original, pack), ({}, "one letter a code, as the game"))

    def test_a_routine_this_reader_doesnt_model_reads_nothing(self):
        original, exe, pack = build_exe_and_pack({0x90: EN})
        bad = bytearray(pack)
        struct.pack_into("<I", bad, ROUTINE - lp.RESIDENT_AT, 0x0C000000)       # jal: a call out
        found, how = lp.pairs(original, exe, bytes(bad))
        self.assertEqual(found, {})
        self.assertIn("not known here", how)

    def test_another_boot_program_reads_nothing(self):
        _, exe, pack = build_exe_and_pack({0x90: EN})
        self.assertEqual(lp.pairs(exe, exe, pack), ({}, "not the USA boot program"))

    def test_expand_writes_pairs_out_but_not_operands(self):
        raw = bytes([gt.CODE_OF["G"], 0x90, 0xFB, 0x07, 0x90, 0x91, 0xF7])
        self.assertEqual(lp.expand(raw, {0x90: EN, 0x91: CH}),
                         bytes([gt.CODE_OF["G"]]) + EN + bytes([0xFB, 0x07, 0x90]) + CH + b"\xf7")


if __name__ == "__main__":
    unittest.main()
