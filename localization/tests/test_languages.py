"""The language list: which alphabets the pack tool can write, and the test packs made from it."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
import pelang  # noqa: E402
from petext import alphabets, cjk, gametext as gt, letters, validation  # noqa: E402

# Everything else in the list waits for its letters (Hebrew reads right to left: petext/rtl.py).
READY = """en id ms sw tl es pt fr de it nl sv nb da fi pl tr cs hu ca sq et sl
           sk ro hr sr-Latn is lt lv vi sr-Cyrl mk kk el
           ru uk be bg zh-Hans zh-Hant ja ko he""".split()


class Languages(unittest.TestCase):
    def test_ready_languages_encode(self):
        for code in READY:
            lang = alphabets.BY_CODE[code]
            self.assertEqual(alphabets.missing(lang), "", code)
            if code not in alphabets.CJK:
                text = alphabets.full_alphabet(lang) + lang.sample
                gt.encode(text, extended=True, cjk_codes=letters.Letters([text]).codes)

    def test_the_rest_are_still_short_of_letters(self):
        for lang in alphabets.LANGUAGES:
            if lang.code not in READY:
                self.assertTrue(alphabets.missing(lang), lang.code)

    def test_capitals(self):
        turkish = alphabets.full_alphabet(alphabets.BY_CODE["tr"])
        self.assertIn("İ", turkish)
        self.assertIn("ı", turkish)
        self.assertNotIn("SS", alphabets.full_alphabet(alphabets.BY_CODE["de"]))

    def test_cjk_letters_stay_in_the_pack_dictionary(self):
        for code in alphabets.CJK:
            lang = alphabets.BY_CODE[code]
            self.assertTrue(all(cjk.supported(ord(c)) for c in lang.letters), code)

    def test_sample_keeps_every_tag(self):
        words = alphabets.sample_words(alphabets.BY_CODE["pl"])
        english = "Aya:{br}Hello {name}, take it.{wait}{red}Key{/red} {choice 2}Yes{br}No"
        text, at = pelang.sample_text(english, words, 0, False, False)
        self.assertEqual(at, 7)
        self.assertNotIn("Hello", text)
        validation.check(gt.encode(text, extended=True), english)

    def test_sample_menu_words_stay_short(self):
        words = alphabets.sample_words(alphabets.BY_CODE["ru"])
        text, _ = pelang.sample_text("Key 12", words, 0, True, False)
        self.assertEqual(len(text), len("Key 12"))
        self.assertEqual(text[3:], " 12")

    def test_sample_shows_the_signs(self):
        self.assertEqual(alphabets.sample_words(alphabets.BY_CODE["el"])[-4:], alphabets.SIGNS)
        self.assertFalse(set(alphabets.SIGNS) & set(alphabets.sample_words(alphabets.BY_CODE["ko"])))

    def test_sample_joins_cjk(self):
        words = alphabets.sample_words(alphabets.BY_CODE["ko"])
        text, _ = pelang.sample_text("Go on home", words, 0, False, True)
        self.assertEqual(len(text), 3)


if __name__ == "__main__":
    unittest.main()
