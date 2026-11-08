"""A pack's own letter table: which codes letters get, what the pack holds, and how they measure."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
import pelang  # noqa: E402
from petext import alphabets, cjk, gametext as gt, latin, layout, letters, menu  # noqa: E402


def table_for(*texts, **kw):
    return letters.Letters(texts, **kw)


class Table(unittest.TestCase):
    def tearDown(self):
        gt.PACK_LETTERS.clear()

    def test_letters_are_one_byte_each(self):
        table = table_for("Ξεσκεπάζω")
        raw = gt.encode("Ξεσκεπάζω", extended=True, cjk_codes=table.codes)
        self.assertEqual(len(raw), 9)
        self.assertTrue(all(letters.FIRST_CODE <= b <= letters.LAST_CODE for b in raw))

    def test_no_table_for_text_with_fixed_letters(self):
        self.assertFalse(table_for("Señor, ¿qué?", "Съешь"))

    def test_a_code_the_text_uses_stays_its_letter(self):
        table = table_for("éàñ ă Я")
        taken = {latin.CODE_OF[c] for c in "éàñ"} | {gt._latin_codes()["Я"]}
        self.assertFalse(taken & set(table.by_code))

    def test_a_glyph_tag_keeps_its_code(self):
        self.assertNotIn(0xEF, table_for("{gEF}ă").by_code)

    def test_every_language_fits(self):
        for lang in alphabets.LANGUAGES:
            if lang.code in alphabets.CJK or lang.code in alphabets.NEEDS_LAYOUT:
                continue
            with self.subTest(lang.code):
                text = alphabets.full_alphabet(lang) + lang.sample
                table = table_for(text)
                self.assertLessEqual(len(table.by_code), letters.LAST_CODE - letters.FIRST_CODE + 1)
                gt.encode(text, extended=True, cjk_codes=table.codes)

    def test_too_many_letters_are_an_error(self):
        everything = "".join(letters.CATALOG)
        with self.assertRaisesRegex(ValueError, "letters past the fixed codes"):
            table_for(everything)

    def test_signs_stay_in_the_dictionary_for_cjk(self):
        self.assertIn("…", table_for("…").codes)
        self.assertNotIn("…", table_for("…", dictionary=True).codes)
        self.assertIn("ă", table_for("ă", dictionary=True).codes)

    def test_dictionary_leaves_the_tables_letters_alone(self):
        table = table_for("…")
        self.assertEqual(cjk.Atlas(["…"], None, skip=table.codes).chars, [])


class Records(unittest.TestCase):
    def test_a_letter_on_the_games_own_carries_no_pixels(self):
        record = letters.CATALOG["ă"].record(0x80)
        self.assertEqual(len(record), letters.RECORD)
        self.assertEqual(struct.unpack_from("<I", record)[0], ord("ă"))
        self.assertEqual(record[4:7], bytes([0x80, 0, gt.CODE_OF["a"]]))
        self.assertEqual(record[8], len(letters.MARKS["breve"].points))

    def test_a_drawn_letter_carries_its_pixels(self):
        record = letters.CATALOG["α"].record(0x80)
        self.assertEqual(record[6], letters.NO_BASE)
        self.assertEqual(record[5], letters.CATALOG["α"].advance())
        self.assertTrue(any(record[8:]))
        self.assertTrue(all(b & 15 <= 2 and b >> 4 <= 2 for b in record[8:]))

    def test_marks_fit_a_record(self):
        for letter in letters.CATALOG.values():
            self.assertLessEqual(len(letter.marks), letters.MAX_MARKS, letter.char)
            for name in letter.marks:
                self.assertLessEqual(len(letters.MARKS[name].points), 8, name)

    def test_every_letter_has_ink(self):
        for letter in letters.CATALOG.values():
            base = latin.synthetic_cell(gt.CODE_OF[letter.base]) if letter.base else None
            self.assertIn(1, letter.compose(base), letter.char)

    def test_every_drawn_letter_stays_in_its_cell(self):
        for ch, (top, pattern) in letters.ART.items():
            lines = pattern.split("/")
            self.assertLessEqual(top + len(lines), 11, ch)
            self.assertLessEqual(max(map(len, lines)) + 1, 11, ch)


class Measure(unittest.TestCase):
    def tearDown(self):
        gt.PACK_LETTERS.clear()

    def test_a_code_measures_as_the_packs_letter(self):
        table = table_for("—")
        gt.PACK_LETTERS.update(table.by_code)
        code = table.codes["—"][0]
        self.assertEqual(gt._latin_advance(code), letters.CATALOG["—"].advance())
        self.assertEqual(layout.advance(bytes([code])), letters.CATALOG["—"].advance())
        self.assertEqual(gt.lines(bytes([code])), [letters.CATALOG["—"].advance()])

    def test_a_horn_adds_to_the_base_letter(self):
        self.assertEqual(letters.CATALOG["ư"].advance(), gt.ADVANCE[gt.CODE_OF["u"]] + 1)
        metrics = bytes([0x61] * 78)
        self.assertEqual(letters.CATALOG["ư"].metric(metrics), 0x71)

    def test_menus_measure_the_packs_letter(self):
        metrics = bytes([7 << 4] * 512)
        table = table_for("ш ă")
        gt.PACK_LETTERS.update(table.by_code)
        self.assertEqual(menu.width(table.codes["ă"] + b"\xff", metrics), 8)


class Packs(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()
        gt.PACK_LETTERS.clear()

    def pack(self, *texts):
        atlas = cjk.Atlas(texts, None, skip=(table := table_for(*texts)).codes)
        atlas.letters = table
        atlas.codes.update(table.codes)
        out = self.dir / "t.pelang"
        pelang.write_pack(out, "el", "Greek", {"sys/001": gt.encode(texts[0], extended=True, cjk_codes=atlas.codes)}, atlas)
        return out, table

    def test_table_makes_version_3(self):
        path, table = self.pack("Ωμέγα")
        data = path.read_bytes()
        magic, version, count, text_at = struct.unpack_from("<4sIII", data)
        self.assertEqual((version, count), (3, 1))
        dictionary, glyphs, record, reserved, at, n = struct.unpack_from("<6I", data, 64)
        self.assertEqual((glyphs, record, reserved, n), (0, 76, 0, len(table.by_code)))
        self.assertEqual(at, dictionary)
        self.assertEqual(len(data) - at, n * letters.RECORD)
        codes = [data[at + k * letters.RECORD + 4] for k in range(n)]
        self.assertEqual(codes, sorted(codes))
        self.assertEqual(pelang.read_pack(path)[2].popitem()[1], gt.encode("Ωμέγα", extended=True, cjk_codes=table.codes))

    def test_a_pack_with_no_table_is_unchanged(self):
        path, _ = self.pack("Hello")
        self.assertEqual(struct.unpack_from("<I", path.read_bytes(), 4)[0], 1)

    def test_table_letters_stay_out_of_what_gets_saved(self):
        atlas = cjk.Atlas([], None)
        atlas.letters = table_for("ă")
        atlas.codes.update(atlas.letters.codes)
        source = {"text": "Aya", "english_hash": ""}
        # one-byte packs (--bytes-only) still refuse it: another pack reads that code as another letter
        raw, errors, _ = pelang.check_row("sys/030", "ă", source, {}, atlas=atlas, name_letters=False)
        self.assertIsNone(raw)
        self.assertIn("saved player name", errors[0])
        # with name letters it is allowed, and the saved name gets a two-byte name letter instead
        from petext import names
        raw, errors, _ = pelang.check_row("sys/030", "ă", source, {}, atlas=atlas)
        self.assertEqual(errors, [])
        out, letters_in_names, errors = names.convert_entries({"sys/030": raw}, atlas)
        self.assertEqual((out["sys/030"], letters_in_names.letters, errors), (b"\xfb\x01", ["ă"], []))


if __name__ == "__main__":
    unittest.main()
