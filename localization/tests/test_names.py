"""Two-byte name letters (petext/names.py) in the default name and the name entry grids."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pelang  # noqa: E402
from petext import gametext as gt, names, stretch  # noqa: E402


class NamesTest(unittest.TestCase):
    def test_english_letters_stay_bytes_others_get_pairs(self):
        grid = gt.encode("ABé", extended=True)
        default = gt.encode("Ayé", extended=True)
        out, table, errors = names.convert_entries({"sys/115": grid, "sys/030": default})
        self.assertEqual(errors, [])
        self.assertEqual(table.letters, ["é"])                  # kept once, grid first
        self.assertEqual(out["sys/115"], gt.encode("AB") + b"\xfb\x01")
        self.assertEqual(out["sys/030"], gt.encode("Ay") + b"\xfb\x01")

    def test_pairs_skip_fb_00_and_cover_four_pages(self):
        self.assertEqual(names.pair(1), b"\xfb\x01")
        self.assertEqual(names.pair(249), b"\xfb\xf9")
        self.assertEqual(names.pair(250), b"\xfc\x00")
        self.assertEqual(names.pair(999), b"\xfe\xf9")
        with self.assertRaises(ValueError):
            names.pair(1000)

    def test_limits(self):
        long_name = gt.encode("ééééééé", extended=True)             # 7 letters, 14 bytes
        _, _, errors = names.convert_entries({"sys/030": long_name})
        self.assertTrue(errors and "12 bytes" in errors[0])
        _, _, errors = names.convert_entries({"sys/116": gt.encode("A" * 100)})
        self.assertTrue(errors and "99 cells" in errors[0])
        _, _, errors = names.convert_entries({"sys/030": gt.encode("éééé", extended=True) + gt.encode("Ayam")})  # 12
        self.assertEqual(errors, [])

    def test_grid_cells_english_draws_empty_stay_empty(self):
        # sys/117's {g4D} (between ',' and '?') is an empty cell in English; a pack draws its own
        # letter on that code, so the cell is a space, not that letter ('à' as a name letter)
        english = ",{g4D} ?{g0D}"
        source = {"text": english, "english_hash": ""}
        self.assertEqual(pelang.empty_grid_cells("é{g4D}{gEF}{g4C}{gF0}{xF3}"), "é  {g4C}{gF0}{xF3}")
        raw, errors, _ = pelang.check_row("sys/117", "é" + english, source, {})
        self.assertEqual(errors, [])
        self.assertEqual(raw, gt.encode("é,  ?", extended=True) + b"\x0d")
        out, table, errors = names.convert_entries({"sys/117": raw})
        self.assertEqual((out["sys/117"], table.letters, errors), (b"\xfb\x01" + gt.encode(",  ?") + b"\x0d", ["é"], []))
        # other text keeps its tags
        self.assertEqual(pelang.check_row("sys/030", "A{g4D}", {"text": "Aya", "english_hash": ""}, {})[0][1], 0x4D)

    def test_pack_holds_name_letters(self):
        entries, table, _ = names.convert_entries({"sys/030": gt.encode("Ayé", extended=True)})
        entries, runs = stretch.convert_entries(entries)
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "t.pelang"
            pelang.write_pack(path, "xx", "Test", entries, None, runs, table)
            data = path.read_bytes()
            at, count = struct.unpack_from("<II", data, 104)
            self.assertEqual((count, at % 4, len(data)), (1, 0, at + 4))
            self.assertEqual(struct.unpack_from("<I", data, at)[0], ord("é"))


if __name__ == "__main__":
    unittest.main()
