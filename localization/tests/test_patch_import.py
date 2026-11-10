"""The patch reader and importer, on small hand-made data: no game data or real patch is involved."""
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch as mock_patch

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from petext import font, gametext as gt, latin, patchfile, patchimport, validation  # noqa: E402


def varint(value: int) -> bytes:
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.insert(0, value & 0x7F | 0x80)
        value >>= 7
    return bytes(out)


def window(source: bytes, target: bytes, data: bytes, inst: bytes, addr: bytes, adler=None) -> bytes:
    body = varint(len(target)) + b"\0" + varint(len(data)) + varint(len(inst)) + varint(len(addr))
    body += struct.pack(">I", zlib.adler32(target) if adler is None else adler) + data + inst + addr
    return bytes([patchfile.VCD_SOURCE | patchfile.VCD_ADLER]) + varint(len(source)) + varint(0) + varint(len(body)) + body


def apply(patch: bytes, source: bytes) -> bytes:
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        (root / "patch.xdelta").write_bytes(patch)
        (root / "disc.bin").write_bytes(source)
        patchfile.apply_patch(root / "patch.xdelta", root / "disc.bin", root / "out.bin")
        return (root / "out.bin").read_bytes()


class Pages(unittest.TestCase):
    def test_a_stray_byte_between_pages_loses_nothing(self):
        bank = b"\xfe\x07\x30\xf9\xf7\xfe\x08\x31\xff\xfe\x09\x32\xff" + bytes(4)
        self.assertEqual(sorted(patchimport.patched_pages(bank)), [7, 8, 9])
        self.assertEqual(patchimport.patched_pages(bank)[8], (b"\x31", 0xFF))

    def test_the_end_of_a_bank_stays_the_end(self):
        bank = b"\xfe\x07\x30\xff" + bytes(20) + b"\xfe\x08\x31\xff"
        self.assertEqual(sorted(patchimport.patched_pages(bank)), [7])


HEADER = b"\xd6\xc3\xc4\x00\x00"


class XdeltaTest(unittest.TestCase):
    def test_add_and_copy_from_the_source(self):
        source = b"Hello world"
        # ADD 6 ("Hola, "), then COPY 5 from source address 6 (code 19: COPY, size follows, SELF).
        patch = HEADER + window(source, b"Hola, world", b"Hola, ", bytes([7, 19, 5]), varint(6))
        self.assertEqual(apply(patch, source), b"Hola, world")

    def test_overlapping_copy_repeats_a_pattern(self):
        source = b"xy"
        # ADD 2 "ab", then COPY 6 from the start of the target (address 2): "ababab" more.
        patch = HEADER + window(source, b"abababab", b"ab", bytes([3, 19, 6]), varint(2))
        self.assertEqual(apply(patch, source), b"abababab")

    def test_run(self):
        patch = HEADER + window(b"", b"zzzz", b"z", bytes([0, 4]), b"")
        self.assertEqual(apply(patch, b""), b"zzzz")

    def test_wrong_disc_stops_and_leaves_no_file(self):
        source = b"Hello world"
        patch = HEADER + window(source, b"Hola", b"Hola", bytes([5]), b"", adler=1)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "patch.xdelta").write_bytes(patch)
            (root / "disc.bin").write_bytes(source)
            with self.assertRaisesRegex(ValueError, "different disc"):
                patchfile.apply_patch(root / "patch.xdelta", root / "disc.bin", root / "out.bin")
            self.assertFalse((root / "out.bin").exists())

    def test_output_must_be_a_new_file(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "disc.bin").write_bytes(b"keep")
            (root / "patch.xdelta").write_bytes(HEADER)
            with self.assertRaises(ValueError):
                patchfile.apply_patch(root / "patch.xdelta", root / "disc.bin", root / "disc.bin")
            self.assertEqual((root / "disc.bin").read_bytes(), b"keep")


class Writer:
    """DJW's bit stream: values top bit first, each byte filled from its low bit."""

    def __init__(self):
        self.bits = []

    def put(self, value: int, count: int):
        self.bits += [value >> i & 1 for i in range(count - 1, -1, -1)]

    def bytes(self) -> bytes:
        out = bytearray((len(self.bits) + 7) // 8)
        for i, bit in enumerate(self.bits):
            out[i // 8] |= bit << (i % 8)
        return bytes(out)


def djw_one_table(text: bytes) -> bytes:
    """Encode `text` with one table in which every byte used has an 8-bit code."""
    used = sorted(set(text))
    code_of = {symbol: i for i, symbol in enumerate(used)}
    w = Writer()
    w.put(0, 3)                         # one group
    w.put(patchfile.EXTRA_CODES, 4)     # every code length has a length
    for _ in range(patchfile.TOTAL_CODES):
        w.put(5, 4)                     # ... of 5 bits: symbol N is simply N

    def run(count):
        while count > 0:
            count -= 1
            w.put(count & 1, 5)
            count >>= 1

    mtf = [0, *patchfile.BASIC_ORDER, *patchfile.EXTRA_ORDER]
    previous_zero_run = 0
    for symbol in range(256):
        length = 8 if symbol in code_of else 0
        if length == mtf[0]:
            previous_zero_run += 1
            continue
        run(previous_zero_run)
        previous_zero_run = 0
        at = mtf.index(length)
        mtf.insert(0, mtf.pop(at))
        w.put(at + 1, 5)
    run(previous_zero_run)
    for byte in text:
        w.put(code_of[byte], 8)
    return w.bytes()


class DjwTest(unittest.TestCase):
    def test_one_table(self):
        text = b"el texto de una ventana " * 8
        self.assertEqual(patchfile.djw_decode(djw_one_table(text), len(text)), text)

    def test_a_short_stream_is_an_error(self):
        with self.assertRaises(ValueError):
            patchfile.djw_decode(b"\x00", 400)

    def test_compressed_section_in_a_window(self):
        text = b"abcabcabc"
        data = varint(len(text)) + djw_one_table(text)
        head = bytes([0xD6, 0xC3, 0xC4, 0, 1, patchfile.SEC_DJW])
        inst = bytes([len(text) + 1])
        body = varint(len(text)) + bytes([patchfile.VCD_DATACOMP]) + varint(len(data)) + varint(len(inst)) + varint(0)
        body += struct.pack(">I", zlib.adler32(text)) + data + inst
        patch = head + bytes([patchfile.VCD_ADLER]) + varint(len(body)) + body
        self.assertEqual(apply(patch, b""), text)


class PpfTest(unittest.TestCase):
    def apply(self, patch: bytes) -> bytes:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "p.ppf").write_bytes(patch)
            (root / "d.bin").write_bytes(b"0123456789")
            patchfile.apply_patch(root / "p.ppf", root / "d.bin", root / "o.bin")
            return (root / "o.bin").read_bytes()

    def test_version_1(self):
        head = b"PPF10\x00" + bytes(50)
        self.assertEqual(self.apply(head + struct.pack("<IB", 2, 2) + b"ab"), b"01ab456789")

    def test_version_2(self):
        head = b"PPF20\x01" + bytes(50) + struct.pack("<I", 10) + bytes(1024)
        self.assertEqual(self.apply(head + struct.pack("<IB", 8, 1) + b"z"), b"01234567z9")

    def test_version_3_with_undo_data(self):
        head = b"PPF30\x02" + bytes(50) + bytes([0, 0, 1, 0])
        self.assertEqual(self.apply(head + struct.pack("<QB", 0, 1) + b"Z0"), b"Z123456789")

    def test_a_patch_past_the_end_of_the_disc_stops(self):
        head = b"PPF20\x01" + bytes(50) + struct.pack("<I", 10) + bytes(1024)
        with self.assertRaisesRegex(ValueError, "wrong disc"):
            self.apply(head + struct.pack("<IB", 20, 1) + b"z")


def fake_font(extra: dict[int, list[int]]):
    """A font page like a patch's: the letters of latin.synthetic_cell, plus `extra` cells."""
    width = font.PER_ROW * font.CELL
    px = [0] * (width * 256)
    cells = {code: latin.synthetic_cell(code) for code in gt.CHARS}
    cells.update(extra)
    for code, cell in cells.items():
        x0, y0 = (code % font.PER_ROW) * font.CELL, (code // font.PER_ROW) * font.CELL
        for i, value in enumerate(cell):
            px[(y0 + i // font.CELL) * width + x0 + i % font.CELL] = value
    pal = [(0, 0, 0, 0), (246, 246, 246, 255), (8, 8, 8, 255), (255, 255, 65, 255)] + [(0, 0, 0, 0)] * 12
    return pal, width, 256, px


class LetterMapTest(unittest.TestCase):
    def identify(self, extra):
        with mock_patch.object(font, "page", return_value=fake_font(extra)):
            return patchimport.letter_map(SimpleNamespace())

    def accented(self, char):
        g = next(g for g in latin.GLYPHS if g.char == char)
        return latin.compose(g, list(latin.synthetic_cell(gt.CODE_OF[g.base])))

    def test_accents_and_coloured_copies_need_no_table(self):
        yellow_a = [3 if v == 1 else v for v in latin.synthetic_cell(gt.CODE_OF["a"])]
        accent = self.accented("é")
        yellow_accent = [3 if v == 1 else v for v in accent]
        letters, _, unknown = self.identify({0x4D: accent, 0x5D: yellow_a, 0x5E: yellow_accent})
        self.assertEqual((letters[0x4D], letters[0x5D], letters[0x5E]), ("é", "a", "é"))
        self.assertEqual(unknown, [])

    def test_a_picture_with_no_letter_is_reported(self):
        wrench = [1 if (i % 13) == 0 else 0 for i in range(144)]
        letters, _, unknown = self.identify({0x60: wrench})
        self.assertNotIn(0x60, letters)
        self.assertEqual(unknown, [0x60])


class PageTest(unittest.TestCase):
    letters = {**gt.CHARS, 0x4D: "é"}

    def test_moved_waits_and_names_are_kept(self):
        english = gt.encode("One{wait}two{name}")
        spanish = gt.encode("Uno dos")
        self.assertEqual(patchimport.compare_page(english, spanish, self.letters), ("Uno dos", None))

    def test_a_changed_choice_keeps_the_english(self):
        english = gt.encode("Go?{choice 2}Yes{br}No")
        spanish = gt.encode("Ir?{choice 3}Si{br}No{br}Otro")
        text, why = patchimport.compare_page(english, spanish, self.letters)
        self.assertIsNone(text)
        self.assertIn("choice", why)

    def test_speaker_colour_goes_unless_the_english_has_colour(self):
        tinted = b"\xfb\x04\xfa\x4a\xfb\x05" + gt.encode(" Hola")
        self.assertEqual(patchimport.compare_page(gt.encode("{name}: Hi"), tinted, self.letters)[0], "{name}: Hola")
        with_red = gt.encode("{red}Hi{/red}")
        self.assertEqual(patchimport.clean_page(tinted, with_red), tinted)

    def test_an_unknown_symbol_keeps_the_english(self):
        text, why = patchimport.compare_page(gt.encode("Tool"), bytes([0x98]), self.letters)
        self.assertIsNone(text)
        self.assertIn("98", why)

    def test_a_patch_symbol_command_is_dropped(self):
        self.assertEqual(patchimport.clean_page(b"\xfb\x0a\x10", b""), b"\x10")

    def test_unchanged_text_is_not_a_row(self):
        raw = gt.encode("Same")
        self.assertEqual(patchimport.compare_page(raw, raw, self.letters), (None, None))

    def test_the_build_rule_keeps_choices_only(self):
        source = validation.structure(gt.encode("A{wait}B{choice 2}x{br}y"))
        validation.check_patch(gt.encode("AB{choice 2}x{br}y{wait}"), source)
        with self.assertRaises(ValueError):
            validation.check_patch(gt.encode("AB"), source)


class GridTest(unittest.TestCase):
    letters = {**gt.CHARS, 0x5A: "Ñ", 0x56: "ú"}

    def test_the_patchs_grid_comes_in_with_the_games_symbols_it_kept(self):
        english = gt.encode("ABCDE abcde") + bytes([0x0D]) + gt.encode("&.,") + bytes(84 * [0x0F])
        spanish = gt.encode("ABCD") + bytes([0x5A]) + gt.encode(" abcd") + bytes([0x56, 0x0D]) + gt.encode("&.,") + bytes(85 * [0x0F])
        text, why = patchimport.compare_grid(english, spanish, self.letters)
        self.assertIsNone(why)
        self.assertEqual(text, "ABCDÑ abcdú{g0D}&.," + " " * 84)        # the 100th cell, empty, dropped

    def test_unchanged_or_only_its_empty_cells_added_is_not_a_row(self):
        english = gt.encode("ABC") + bytes(96 * [0x0F])
        self.assertEqual(patchimport.compare_grid(english, english, self.letters), (None, None))
        self.assertEqual(patchimport.compare_grid(english, english + bytes([0x0F]), self.letters), (None, None))

    def test_a_grid_too_big_or_with_a_symbol_of_its_own_keeps_the_english(self):
        english = gt.encode("ABC") + bytes(96 * [0x0F])
        self.assertIn("100 cells", patchimport.compare_grid(english, english + bytes([0x10]), self.letters)[1])
        self.assertIn("0D", patchimport.compare_grid(english, bytes([0x0D]) + english[1:], self.letters)[1])
        self.assertIn("98", patchimport.compare_grid(english, bytes([0x98]) + english[1:], self.letters)[1])


class FontPictureTest(unittest.TestCase):
    def test_is_a_png_the_size_the_font_folder_wants(self):
        with mock_patch.object(font, "page", return_value=fake_font({})):
            png = patchimport.font_picture(SimpleNamespace())
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(struct.unpack(">II", png[16:24]), (252, 48))


def boot_program() -> bytearray:
    """Enough of a USA boot program for the dialogue widths: the table and the code that reads it."""
    exe = bytearray(patchimport.WIDTHS_AT + 0x200)
    exe[patchimport.WIDTHS_AT:patchimport.WIDTHS_AT + 2 * len(gt.ADVANCE)] = bytes(v for w in gt.ADVANCE for v in (0, w))
    for at, (game, _) in patchimport.WIDTH_READS.items():
        struct.pack_into("<I", exe, at, game)
    return exe


def own_widths(exe: bytearray, widths: dict[int, int]) -> bytearray:
    """`exe` reading one width a letter, as the Spanish patch makes it, with these widths."""
    out = bytearray(exe)
    for at, (_, patched) in patchimport.WIDTH_READS.items():
        struct.pack_into("<I", out, at, patched)
    for code, width in widths.items():
        out[patchimport.WIDTHS_AT + 1 + code] = width
    return out


class DialogueWidthsTest(unittest.TestCase):
    def test_the_games_own_table(self):
        exe = boot_program()
        exe[patchimport.WIDTHS_AT + 2 * 0x10 + 1] = 5          # a patch that only narrows A
        widths, how = patchimport.dialogue_widths(boot_program(), exe)
        self.assertEqual(widths[0x10], 5)
        self.assertEqual(widths[0x11], gt.ADVANCE[0x11])
        self.assertIn("the game's table", how)

    def test_one_width_a_letter(self):
        exe = own_widths(boot_program(), {0x00: 6, 0x10: 7, 0x4D: 8, 0x98: 12})
        widths, how = patchimport.dialogue_widths(boot_program(), exe)
        self.assertEqual((widths[0x00], widths[0x10], widths[0x4D], widths[0x98]), (6, 7, 8, 12))
        self.assertEqual(len(widths), patchimport.BYTE_WIDTHS)
        self.assertIn("one width a letter", how)

    def test_one_width_a_letter_written_with_a_nop(self):
        # The Italian patch writes the same moves another way: addu s7,zero,a2 for addu s7,a2,zero,
        # and nop where the Spanish one leaves v0 as it is with addu v0,v0,zero.
        exe = own_widths(boot_program(), {0x10: 7, 0x4D: 6})
        struct.pack_into("<I", exe, 0x284E4, 0x0006B821)
        struct.pack_into("<I", exe, 0x29074, 0)
        widths, how = patchimport.dialogue_widths(boot_program(), exe)
        self.assertEqual((widths[0x10], widths[0x4D]), (7, 6))
        self.assertIn("one width a letter", how)

    def test_an_unknown_reading_or_another_program_reads_nothing(self):
        half = own_widths(boot_program(), {})
        struct.pack_into("<I", half, 0x29074, patchimport.WIDTH_READS[0x29074][0])
        self.assertIsNone(patchimport.dialogue_widths(boot_program(), half)[0])
        other = boot_program()
        other[patchimport.WIDTHS_AT + 1] = 9
        self.assertIsNone(patchimport.dialogue_widths(other, own_widths(other, {}))[0])


class LetterPicturesTest(unittest.TestCase):
    def test_the_patchs_letters_with_their_widths(self):
        bold_a = [1 if 2 <= i % 12 <= 7 and 3 <= i // 12 <= 8 else 2 if i % 12 == 8 and 3 <= i // 12 <= 8 else 0
                  for i in range(144)]
        g = next(g for g in latin.GLYPHS if g.char == "é")
        accent = latin.compose(g, list(latin.synthetic_cell(gt.CODE_OF["e"])))
        yellow_a = [3 if v == 1 else v for v in bold_a]
        original, patched = fake_font({}), fake_font({0x10: bold_a, 0x4D: accent, 0x5D: yellow_a})
        letters = {**gt.CHARS, 0x4D: "é", 0x5D: "A"}
        how = {0x4D: "identified as é", 0x5D: "copy of A"}
        exe = boot_program()
        games = [SimpleNamespace(exe_name="SLUS_006.62", disc=SimpleNamespace(file=lambda name, e=e: e),
                                 resident_pack=lambda: b"")
                 for e in (exe, own_widths(exe, {**{c: gt.ADVANCE[c] for c in range(0x4D)}, 0x10: 6, 0x4D: 7}))]
        metrics_o = bytes([0x70] * 444)
        metrics_p = bytearray(metrics_o)
        metrics_p[0x10], metrics_p[0x4D] = 0x60, 0x50
        with mock_patch.object(font, "page", side_effect=[original, patched]), \
             mock_patch.object(patchimport, "menu_metrics", side_effect=[metrics_o, bytes(metrics_p)]):
            drawn, how_read = patchimport.letter_pictures(*games, letters, how)
        self.assertIn("one width a letter", how_read)
        by = {d["letter"]: d for d in drawn}
        self.assertEqual(sorted(by), ["A", "é"])       # B and the rest are as the game drew them; no copies
        self.assertEqual((by["A"]["code"], by["A"]["dialogue"], by["A"]["menu"]), ("10", 6, 0x60))
        self.assertEqual(by["A"]["picture"][3], "  ######+   ")
        self.assertEqual((by["é"]["dialogue"], by["é"]["menu"]), (7, 0x50))


class MovedWidthsTest(unittest.TestCase):
    """The game's gap-and-width table moved to the resident pack, as the German patch does it."""
    def moved(self, table: bytes) -> tuple[bytearray, bytes]:
        from petext import letterpairs
        exe = boot_program()
        at = 0x800AAD80
        hi, lo = (at + 0x8000) >> 16, at & 0xFFFF
        for read, plus in patchimport.TABLE_READS.items():
            struct.pack_into("<III", exe, read - 8, 0x3C010000 | hi, 0x00370821,
                             (0x24 << 26) | (1 << 21) | (3 << 16) | ((lo + plus) & 0xFFFF))
        pack = bytearray(0x3000)
        pack[at - letterpairs.RESIDENT_AT:at - letterpairs.RESIDENT_AT + len(table)] = table
        return exe, bytes(pack)

    def test_the_table_is_read_where_the_patch_moved_it(self):
        table = bytes(v for w in gt.ADVANCE for v in (0, w)) + bytes([0, 9, 1, 8])   # two new letters
        exe, pack = self.moved(table)
        widths, how = patchimport.dialogue_widths(boot_program(), exe, pack)
        self.assertIn("moved to 0x800AAD80", how)
        self.assertEqual(widths[0x10], gt.ADVANCE[0x10])
        new = len(gt.ADVANCE)                                         # the first code past the game's table
        self.assertEqual((widths[new], widths[new + 1]), (9, 7))     # width less its left gap
        self.assertNotIn(new + 2, widths)                             # past the last letter

    def test_without_the_pack_it_is_not_known(self):
        exe, _ = self.moved(b"")
        self.assertIsNone(patchimport.dialogue_widths(boot_program(), exe)[0])


class TurnedMarkTest(unittest.TestCase):
    """¡ and ¿ are found turned over anywhere in their cell, but a letter half moved off the cell's
    edge doesn't count (the German ü read as ¡)."""
    def cell(self, points) -> tuple[int, ...]:
        return tuple(1 if (i % font.CELL, i // font.CELL) in points else 0 for i in range(font.CELL * font.CELL))

    def test_a_turned_mark_and_a_u_with_dots(self):
        bang = {(x, y) for x in (1, 2) for y in range(2, 8)} | {(1, 9), (2, 9)}
        question = {(x, 2) for x in range(1, 5)} | {(4, y) for y in range(3, 6)} | {(3, 9)}
        bases = {"!": self.cell(bang), "?": self.cell(question)}
        turned = self.cell({(11 - x, 11 - y) for x, y in bang})
        self.assertEqual(patchimport._turned(turned, bases), "¡")
        u = ({(x, y) for x in (0, 1, 4, 5) for y in range(4, 9)} | {(x, 9) for x in range(1, 5)}
             | {(1, 2), (4, 2)})
        self.assertIsNone(patchimport._turned(self.cell(u), bases))


class KanaGridTest(unittest.TestCase):
    def test_a_kana_grid_keeps_the_english(self):
        english, dummy = bytes(range(0x56, 0x5B)), gt.encode("DUMMY")
        self.assertEqual(patchimport.compare_grid(english, dummy, dict(gt.CHARS), "sys/116"),
                         (None, "a Japanese kana grid, which the USA game never opens"))
        self.assertEqual(patchimport.compare_grid(english, english, dict(gt.CHARS), "sys/116"), (None, None))


class BattleInPlaceTest(unittest.TestCase):
    """A patch that writes each battle line over the English one in its slot (the German patch),
    leaving the rest of the old line behind its end."""
    def exe(self) -> bytearray:
        from petext import battle
        exe = bytearray(battle.REGION[1] + 16)
        for key, (at, size, ja) in battle.LINES.items():
            if ja is not None:
                exe[ja:at] = bytes([0x60] * (at - ja - 1)) + b"\xff"        # its Japanese line before it
            exe[at:at + size] = bytes([gt.CODE_OF["E"]] * (size - 1)) + b"\xff"
        return exe

    def test_each_line_is_read_in_its_slot(self):
        from petext import battle
        original = self.exe()
        patched = bytearray(original)
        for k, (at, size, _) in enumerate(battle.LINES.values()):
            patched[at:at + 3] = bytes([gt.CODE_OF["a"] + k % 20, gt.CODE_OF["b"], 0xFF])   # the old tail stays
        lines, why = battle.patched(bytes(original), bytes(patched))
        self.assertIsNone(why)
        self.assertEqual(lines["battle/002"], bytes([gt.CODE_OF["a"] + 2, gt.CODE_OF["b"]]))

    def test_a_slot_still_english_is_not_taken_in_place(self):
        from petext import battle
        original = self.exe()
        patched = bytearray(original)
        for k, (at, size, _) in enumerate(list(battle.LINES.values())[1:]):
            patched[at:at + 3] = bytes([gt.CODE_OF["a"], gt.CODE_OF["b"], 0xFF])
        self.assertEqual(battle.patched(bytes(original), bytes(patched))[0], {})


class TileColourRowsTest(unittest.TestCase):
    def test_only_colour_rows_count_as_pictures(self):
        from petext import pictures
        english = bytes.fromhex("10 10 40 00 e0 01 1c 10 20 10 40 00 e3 81 15 00")
        rows = bytes.fromhex("10 10 40 00 e1 01 1c 10 20 10 40 00 f1 81 15 00")
        script = bytes.fromhex("10 10 40 00 e0 01 1c 11 20 10 40 00 e3 81 15 00")
        self.assertTrue(pictures._palette_rows(english, rows))
        self.assertFalse(pictures._palette_rows(english, script))


if __name__ == "__main__":
    unittest.main()
