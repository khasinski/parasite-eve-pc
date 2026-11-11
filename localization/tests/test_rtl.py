"""petext/rtl.py against the cases the game's renderer is tested with (text-render)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from petext import rtl  # noqa: E402

CASES = Path(__file__).resolve().parents[2] / "tests" / "text_render_rtl_cases.txt"


def cases():
    what = ""
    for line in CASES.read_text(encoding="utf-8").splitlines():
        if line.startswith("#"):
            what = line[1:].strip()
            continue
        if line:
            logical, drawn = line.split("\t")
            yield what, logical, drawn


class Order(unittest.TestCase):
    def test_cases_draw_as_the_game_draws_them(self):
        found = list(cases())
        self.assertGreaterEqual(len(found), 20)
        for what, logical, drawn in found:
            with self.subTest(what):
                self.assertEqual(rtl.visual(logical), drawn)

    def test_language_codes(self):
        for code in ("he", "ar", "fa", "ur", "yi", "ar-EG", "ks-Arab", "he_IL"):
            self.assertTrue(rtl.is_rtl(code), code)
        for code in ("en", "ru", "ku-Latn", "zh-Hans", "az", "el"):
            self.assertFalse(rtl.is_rtl(code), code)


class Join(unittest.TestCase):
    def test_touching_letters(self):
        joined, nxt = rtl.join("ببا دب")
        self.assertEqual(joined, "ﺑﺒﺎ ﺩﺏ")
        self.assertEqual(nxt, [True, True, False, False, False, False])

    def test_every_form_is_a_joining_letter(self):
        for letter in rtl.FORMS:
            for form in rtl.forms_of(letter):
                if rtl.FORMS[letter][1] > 1:
                    self.assertTrue(rtl.is_joining(form), hex(form))

    def test_width_closes_the_gaps_between_joined_letters(self):
        # every letter 6 wide, moving the pen 7: three joined letters lose two gaps of 2
        self.assertEqual(rtl.width("ببب", lambda ch: 7, lambda ch: 6), 7 + 5 + 5)
        self.assertEqual(rtl.width("دد", lambda ch: 7, lambda ch: 6), 14)
        self.assertEqual(rtl.width("של", lambda ch: 7, lambda ch: 6), 14)


class ArabicLetters(unittest.TestCase):
    def test_every_shape_is_a_placeholder_until_a_translation_draws_it(self):
        from petext import letters
        for letter in rtl.FORMS:
            for form in rtl.forms_of(letter):
                self.assertIn(chr(form), letters.CATALOG, hex(form))
                self.assertEqual(letters.CATALOG[chr(form)].art, letters.PLACEHOLDER, hex(form))
            self.assertIn(chr(letter), letters.CATALOG, hex(letter))

    def test_cut_is_strokes_and_shadow(self):
        from petext import letters
        first, width = letters.CATALOG["ﺑ"].cut()            # the question mark, 5 wide from column 1
        self.assertEqual((first, width), (1, 6))
        self.assertEqual(letters.CATALOG["ﺑ"].joined_advance(), 7)


class ArabicPack(unittest.TestCase):
    """A small Arabic pack built by `pelang.py build --lang ar` from synthetic strings."""

    def setUp(self):
        import tempfile
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        from petext import gametext as gt
        self.tmp.cleanup()
        gt.PACK_LETTERS.clear()
        gt.PACK_EXTRA.clear()
        gt.RTL = False

    def build(self, rows: dict, lang: str = "ar"):
        import json
        import pelang
        strings = [{"key": "dlg/M001/000", "text": "Hello there.", "lines_px": [70], "boxes": ["default"],
                    "rooms": ["M001"], "unused": False},
                   {"key": "sys/030", "text": "Aya", "lines_px": [20], "boxes": [], "rooms": [], "unused": False}]
        (self.dir / "strings.json").write_text(json.dumps(strings), encoding="utf-8")
        (self.dir / "banks.json").write_text(json.dumps(
            [{"key": "M001", "crc32": "1a2b3c4d", "size": 120, "rooms": ["M001"]}]), encoding="utf-8")
        table = pelang.template_rows(pelang.load_strings(self.dir / "strings.json"))
        for r in table:
            r["translation"] = rows.get(r["key"], "")
        pelang.write_csv(self.dir / "t.csv", table)
        out = self.dir / "ar.pelang"
        code = pelang.main(["build", str(self.dir / "t.csv"), "--strings", str(self.dir / "strings.json"),
                            "--lang", lang, "--name", "Arabic", "-o", str(out)])
        return code, out

    def test_joined_shapes_go_in_the_pack_with_no_code(self):
        import struct
        from petext import letters
        code, path = self.build({"dlg/M001/000": "\u0633\u0644\u0627\u0645 \u0628\u064a\u062a",
                                 "sys/030": "\u0622\u064a\u0629"})
        self.assertEqual(code, 0)
        data = path.read_bytes()
        self.assertEqual(data[16:18], b"ar")
        at, n = struct.unpack_from("<II", data, 80)
        records = [data[at + k * letters.RECORD:at + (k + 1) * letters.RECORD] for k in range(n)]
        held = {chr(struct.unpack_from("<I", r)[0]): r[4] for r in records}
        for letter in "\u0633\u0644\u0627\u0645\u0628\u064a\u062a\u0622\u0629":
            for form in rtl.forms_of(ord(letter)):
                self.assertEqual(held.get(chr(form)), 0, hex(form))     # every shape, none with a code
        self.assertIn("\ufefb", held)                                   # lam-alef

    def test_lines_measure_joined(self):
        from petext import letters
        code, _ = self.build({"dlg/M001/000": "\u0628\u0628\u0628"})
        self.assertEqual(code, 0)
        widths = [letters.CATALOG[ch].joined_advance() for ch in "\ufe90\ufe92\ufe91"]
        cuts = [letters.CATALOG[ch].cut()[1] for ch in "\ufe90\ufe92\ufe91"]
        # reading order: start, middle, end; the middle and end each join the letter before them
        want = widths[2] + (cuts[1] - 1) + (cuts[0] - 1)
        self.assertEqual(rtl.width("\u0628\u0628\u0628", lambda ch: letters.CATALOG[ch].joined_advance(),
                                   lambda ch: letters.CATALOG[ch].cut()[1]), want)
        self.assertLess(want, 3 * letters.CATALOG["\u0628"].advance())


if __name__ == "__main__":
    unittest.main()
