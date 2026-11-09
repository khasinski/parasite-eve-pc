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

    def test_menus_measure_a_letter_with_no_code_as_a_coded_one(self):
        metrics = bytes([7 << 4 | 2] * 512)             # every cell 7 wide from its column 2
        table = table_for("ă")
        gt.PACK_LETTERS.update(table.by_code)
        gt.PACK_EXTRA[letters.extra_token(0)] = letters.CATALOG["ă"]
        try:
            self.assertEqual(menu.width(letters.extra_token(0) + b"\xff", metrics), 8)
            self.assertEqual(menu.width(table.codes["ă"] + b"\xff", metrics), 8)
        finally:
            gt.PACK_EXTRA.clear()


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
        self.assertEqual((out["sys/030"], letters_in_names.letters, errors), (b"\xfb\x42", ["ă"], []))


if __name__ == "__main__":
    unittest.main()


class ManyLetters(unittest.TestCase):
    """More letters than the 163 one-byte codes (tests/many_letters_pack.py)."""

    def setUp(self):
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import many_letters_pack
        self.m = many_letters_pack
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()
        gt.PACK_LETTERS.clear()
        gt.PACK_EXTRA.clear()

    def records(self, data):
        at, n = struct.unpack_from("<II", data, 80)
        return [data[at + k * letters.RECORD:at + (k + 1) * letters.RECORD] for k in range(n)]

    def test_every_catalog_letter_in_one_pack(self):
        code, path = self.m.build(self.dir)
        self.assertEqual(code, 0)
        data = path.read_bytes()
        self.assertEqual(struct.unpack_from("<I", data, 4)[0], 4)
        recs = self.records(data)
        coded = [r for r in recs if r[4]]
        extra = [r for r in recs if not r[4]]
        self.assertEqual(len(recs), len(self.m.catalog_text()))
        self.assertTrue(0 < len(coded) <= 163 and len(extra) == len(recs) - len(coded))
        self.assertEqual(recs[:len(coded)], coded)                       # coded first, by code
        self.assertEqual([r[4] for r in coded], sorted(r[4] for r in coded))
        cps = [struct.unpack_from("<I", r)[0] for r in extra]
        self.assertEqual(cps, sorted(cps))                               # then by Unicode
        # the item name's letters have codes: the item name stays one byte a letter
        coded_letters = {chr(struct.unpack_from("<I", r)[0]) for r in coded}
        self.assertTrue(set(self.m.ITEM_LETTERS) <= coded_letters)
        # no stand-in token reaches the pack, and every letter with no code is in a stretch
        self.assertNotIn(letters.EXTRA_TOKEN, data)
        at, runs, pool = struct.unpack_from("<III", data, 88)
        stretch_letters = set(struct.unpack_from(f"<{pool}I", data, at + (runs + 1) * 4))
        self.assertTrue(set(cps) <= stretch_letters)
        # read back, the text measures as build measured it
        _, _, entries = pelang.read_pack(path)
        self.assertTrue(any(letters.EXTRA_TOKEN in raw for raw in entries.values()))

    def test_most_used_letters_get_the_codes(self):
        text = self.m.catalog_text()
        busy = text[-1]                                  # last by Unicode: would get no code by order
        table = letters.Letters([text, busy * 50], extra=True)
        self.assertIn(busy, {l.char for l in table.by_code.values()})
        self.assertNotIn(busy, {l.char for l in table.extra})
        self.assertEqual(len(table.by_code) + len(table.extra), len(text))

    def test_a_lone_letter_with_no_code_is_a_stretch(self):
        text = self.m.catalog_text()
        table = letters.Letters([text], extra=True)
        lone = table.extra[0].char
        code, path = self.m.build(self.dir, text=text + " " + lone)
        self.assertEqual(code, 0)
        self.assertNotIn(letters.EXTRA_TOKEN, path.read_bytes())

    def test_one_byte_builds_still_stop_at_163(self):
        code, _ = self.m.build(self.dir, extra=["--bytes-only"])
        self.assertEqual(code, 1)

    def test_item_names_past_the_codes_are_refused(self):
        with self.assertRaisesRegex(ValueError, "item names"):
            letters.Letters([self.m.catalog_text()], byte_texts=[self.m.catalog_text()], extra=True)
