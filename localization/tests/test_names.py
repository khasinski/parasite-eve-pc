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
