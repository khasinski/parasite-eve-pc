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


if __name__ == "__main__":
    unittest.main()
