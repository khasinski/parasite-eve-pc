"""Two-byte name letters (petext/names.py) in the default name and the name entry grids."""
import hashlib
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pelang  # noqa: E402
from petext import gametext as gt, names, stretch  # noqa: E402
from petext.name_catalog import CATALOG  # noqa: E402

E = b"\xfb\x29"                       # é: letter 41 of the shared list, in every pack


class NamesTest(unittest.TestCase):
    def test_english_letters_stay_bytes_others_get_pairs(self):
        grid = gt.encode("ABé", extended=True)
        default = gt.encode("Ayé", extended=True)
        out, table, errors = names.convert_entries({"sys/115": grid, "sys/030": default})
        self.assertEqual(errors, [])
        self.assertEqual(table.letters, ["é"])                  # kept once, grid first
        self.assertEqual(out["sys/115"], gt.encode("AB") + E)
        self.assertEqual(out["sys/030"], gt.encode("Ay") + E)

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
        self.assertEqual((out["sys/117"], table.letters, errors), (E + gt.encode(",  ?") + b"\x0d", ["é"], []))
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
            self.assertEqual((count, at % 4, len(data)), (41, 0, at + 41 * 4))
            table = struct.unpack_from(f"<{count}I", data, at)
            self.assertEqual((table[40], set(table[:40])), (ord("é"), {0}))   # numbers it doesn't use: 0
            self.assertEqual(names.read_numbers(data), {"é": 41})

    def test_shared_list_is_append_only(self):
        # Changing what is here renumbers the letters in players' saved names. Only add letters at
        # the end of name_catalog.py, then pin the new length and hash here.
        pinned = 666
        self.assertGreaterEqual(len(CATALOG), pinned)
        self.assertEqual(hashlib.sha256(CATALOG[:pinned].encode()).hexdigest(),
                         "9decee1bf64b96b7f8d6ac02b66f0dedc8def7812c58f2f799aed69ba489f50a")
        self.assertEqual(len(set(CATALOG)), len(CATALOG))
        self.assertFalse(set(CATALOG) & set(gt.CODE_OF))       # the game's letters stay one byte
        self.assertEqual((names.SHARED["é"], names.SHARED["ă"], names.SHARED["Ά"], names.SHARED["ж"]),
                         (41, 66, 307, 430))

    def test_same_letter_same_number_in_every_pack(self):
        # a Greek and a Russian pack: their letters never share a number, and a letter both have
        # (é) has the same one, so a name saved with one reads the same, or not at all, with the other
        greek, russian = names.NameLetters(), names.NameLetters()
        greek_codes = [greek.code(c) for c in "ΑΒΓé"]
        russian_codes = [russian.code(c) for c in "ЖЗИé"]
        self.assertEqual(greek_codes[-1], russian_codes[-1])
        self.assertFalse(set(greek_codes[:-1]) & set(russian_codes[:-1]))
        # grid order doesn't matter: a new version that moves letters keeps their numbers
        moved = names.NameLetters()
        self.assertEqual([moved.code(c) for c in "éΓΒΑ"], greek_codes[::-1])

    def test_numbers_skip_the_games_own_codes(self):
        self.assertNotIn(504, names.SHARED.values())
        self.assertNotIn(754, names.OWN_NUMBERS)
        self.assertEqual((names.SHARED["ぎ"], names.SHARED["く"]), (503, 505))   # either side of FD 04
        self.assertLess(max(names.SHARED.values()), names.OWN_FLOOR)

    def test_own_letters_count_down_and_a_new_version_keeps_them(self):
        first = names.NameLetters()
        self.assertEqual([first.code(c) for c in "中文字"], [names.pair(999), names.pair(998), names.pair(997)])
        self.assertEqual(first.code("é"), E)
        # the next version drops 文 and adds 名 first: 中 and 字 keep their numbers, and 名 doesn't
        # take 文's old one (998), or a name saved with 文 would read as 名
        second = names.NameLetters(first.numbers)
        self.assertEqual([second.code(c) for c in "名中字"], [names.pair(996), names.pair(999), names.pair(997)])
        self.assertEqual(second.kept, 2)
        # a kept number for a shared letter, or one out of range, is ignored
        third = names.NameLetters({"é": 999, "中": 5, "字": 754})
        self.assertEqual([third.code(c) for c in "é中字"], [E, names.pair(999), names.pair(998)])

    def test_too_many_own_letters(self):
        many = "".join(chr(0x4E00 + k) for k in range(len(names.OWN_NUMBERS) + 1))
        letters = names.NameLetters()
        for c in many[:-1]:
            letters.code(c)
        with self.assertRaises(ValueError):
            letters.code(many[-1])

    def test_build_keeps_the_numbers_of_the_pack_it_writes_over(self):
        with tempfile.TemporaryDirectory() as d:
            old, new, other = Path(d) / "old.pelang", Path(d) / "new.pelang", Path(d) / "other.pelang"
            letters = names.NameLetters()
            letters.code("中"); letters.code("字")
            out, runs = stretch.convert_entries({"sys/030": b""})
            pelang.write_pack(old, "xx", "Test", out, None, runs, letters)
            self.assertEqual(pelang.names_to_keep(old, new), ({"中": 999, "字": 998}, old))   # given
            self.assertEqual(pelang.names_to_keep(None, new), ({}, None))                    # nothing there
            self.assertEqual(pelang.names_to_keep(None, old), ({"中": 999, "字": 998}, old))   # written over
            other.write_bytes(b"not a pack")
            self.assertEqual(pelang.names_to_keep(None, other), ({}, other))
            with self.assertRaises(OSError):
                pelang.names_to_keep(Path(d) / "missing.pelang", new)


if __name__ == "__main__":
    unittest.main()
