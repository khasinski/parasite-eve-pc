"""Stretches (petext/stretch.py): the text renderer's runs of Unicode letters in a version 4 pack."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pelang  # noqa: E402
from petext import gametext as gt, stretch  # noqa: E402


class StretchTest(unittest.TestCase):
    def test_runs_become_tokens_and_commands_stay(self):
        raw = gt.encode("Hi there.{br}{red}Aya{/red}: {num} {choice 2}é", extended=True)
        out, table = stretch.convert_entries({"dlg/M001/001": raw})
        text = out["dlg/M001/001"]
        self.assertEqual(table.runs, ["Hi there.", "Aya", ": "])   # the lone spaces stay bytes
        # the line break, colours, number slot and choice are still the box's own bytes
        self.assertIn(b"\xf7\xfb\x04", text)
        self.assertIn(b"\xfb\x05", text)
        self.assertIn(b"\xfb\x08", text)
        self.assertIn(b"\xfb\x09\x02", text)
        # a lone letter stays a byte: a token would be bigger
        self.assertTrue(text.endswith(gt.encode("é", extended=True)))
        back = {ch: raw for raw, ch in stretch.letter_map().items()}
        self.assertEqual(stretch.expand(text, table, back), raw)

    def test_letters_are_the_ones_the_game_resolves(self):
        # a stretch letter on a one-byte cell must be the letter the game's table names for that
        # cell (kPeTextUnicode), or the game draws '?': Cyrillic Ё shares Ë's cell, so it is Ë
        import re
        header = Path(__file__).resolve().parents[2] / "mods" / "pe_lang" / "pe_lang_text_data.h"
        src = header.read_text(encoding="utf-8")
        body = src[src.index("kPeTextUnicode[241] = {"):]
        body = re.sub(r"/\*.*?\*/", "", body[body.index("{") + 1:body.index("};")], flags=re.S)
        table = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", body)]
        self.assertEqual(len(table), 241)
        for raw, letter in stretch.letter_map().items():
            if len(raw) == 1:
                self.assertEqual(table[raw[0]], ord(letter), f"code {raw[0]:02X}")
        _, runs = stretch.convert_entries({"dlg/M001/001": gt.encode("Её ещё", extended=True)})
        self.assertEqual(runs.runs, ["Eë eщë"])

    def test_same_run_kept_once_and_long_runs_split(self):
        line = gt.encode("x" * 200)
        out, table = stretch.convert_entries({"sys/001": line, "sys/002": gt.encode("x" * 200)})
        self.assertEqual(out["sys/001"], out["sys/002"])
        self.assertEqual([len(r) for r in table.runs], [96, 8])
        self.assertEqual(len(out["sys/001"]), 3 * 4)       # 96 + 96 + 8 letters, three tokens

    def test_edited_text_keeps_bytes(self):
        raw = gt.encode("Aya")
        out, table = stretch.convert_entries({"sys/030": raw, "item/001": raw, "sys/116": raw,
                                              "bank/1234": b"M001"})
        self.assertEqual(out, {"sys/030": raw, "item/001": raw, "sys/116": raw, "bank/1234": b"M001"})
        self.assertEqual(table.runs, [])

    def test_a_narrowed_line_carries_its_mark_in_every_stretch(self):
        table, letters = stretch.Stretches(), stretch.letter_map()
        raw = stretch.NARROW + bytes([112]) + gt.encode("Hello there")
        out = table.convert(raw, letters)
        self.assertEqual(len(out), 4)                                   # the mark costs no bytes
        self.assertEqual(table.runs[0], chr(stretch.NARROW_FIRST + 112) + "Hello there")
        back = {ch: bytes([code]) for code, ch in gt.CHARS.items()}
        self.assertEqual(stretch.expand(out, table, back), raw)
        long = stretch.NARROW + bytes([1]) + gt.encode("ab" * 60)       # split: each piece marked
        out = table.convert(long, letters)
        runs = [table.runs[out[i + 2] * 240 + out[i + 3]] for i in range(0, len(out), 4)]
        self.assertTrue(all(r[0] == chr(stretch.NARROW_FIRST + 1) for r in runs))
        self.assertTrue(all(len(r) <= stretch.MAX_LETTERS for r in runs))
        self.assertEqual(stretch.expand(out, table, back), long)

    def test_version_4_pack_layout(self):
        entries = {"sys/001": gt.encode("Hello"), "sys/030": gt.encode("Aya")}
        conv, table = stretch.convert_entries(entries)
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "t.pelang"
            pelang.write_pack(path, "xx", "Test", conv, None, table)
            data = path.read_bytes()
            self.assertEqual(struct.unpack_from("<I", data, 4)[0], 4)
            at, runs, pool, widths = struct.unpack_from("<IIII", data, 88)
            self.assertEqual((runs, pool, widths, at % 4), (1, 5, 0, 0))
            self.assertEqual(struct.unpack_from("<2I", data, at), (0, 5))
            self.assertEqual(struct.unpack_from("<5I", data, at + 8), tuple(map(ord, "Hello")))
            self.assertEqual(len(data), at + 8 + 20)
            _, _, back = pelang.read_pack(path)
            self.assertEqual(back[pelang.fnv1a("sys/001")], entries["sys/001"])
            self.assertEqual(back[pelang.fnv1a("sys/030")], entries["sys/030"])


if __name__ == "__main__":
    unittest.main()
