"""Naming the letter pictures of a Chinese fan patch (petext/hanzi.py, name_ja_patch_letters.py).

The letters are drawn here from a Chinese font on the PC (SimSun on Windows, or the font in
PE_TEST_CJK_FONT), so the test needs no disc and no patch: a letter as the font draws it at 12
pixels, moved, squeezed to 11 rows, a sign, a dash, a letter that isn't a candidate; then the
naming rules (blank, page 0 slot, Japanese font, read by hand), decoding a draft, picking the
right reading of a page shared by rooms with different letter pages, and the command end to end.
Skipped (77 when run on its own) without Pillow or such a font.
"""
from __future__ import annotations

import csv
import io
import json
import os
import sys
import tempfile
import unittest
import unittest.mock
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from petext import hanzi, jatext as ja  # noqa: E402

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    Image = None


def find_font() -> Path | None:
    for path in (os.environ.get("PE_TEST_CJK_FONT"), hanzi.FONT_DIR / "simsun.ttc"):
        if path and Path(path).is_file():
            return Path(path)
    return None


FONT = find_font() if Image else None
WHY = "needs Pillow and a Chinese font (SimSun, or PE_TEST_CJK_FONT)"
WORDS = "的一是不了人我在有他这中大来上国个到说们为子和你地出道也时年得就那要下以生会自着去之过家学对可她里后小么心多天而能好都然没日于起还发成事只作当想看文无开手十用主行方又如前所本见经头面公同三已老从动两长知民样现分将外但身些与高意进把法此实回二理美点月明其种声全工己话儿者向情部正名定女问力机给等几很业最间新什打便位因重被走电四第门相次东政海口使教西再平真听世气信北少关并内加化由却代军产入先山五太水万市眼体别处总才场师书比住员九笑性通目华报立马命张活难神数件安表原车白应路期叫死常提感金何更反合放做系计或司利受光王果亲界及今京务制解各任至清物台象记边共风战干接它许八特觉望直服毛林题建南度统色字请交爱让认算论百吃义科怎元社术结六功指思非流每青管夫连远资队跟带花快条院变联言权往展该领传近留红治决周保达办运武半候七必城父强步完革深区即求品士转量空甚众技轻程告江语英基派满式李息写呢识极令黄德收脸钱党倒未持取设始版双历越史商千片容研像找友孩站广改议形委早房音火际则首单据导影失拿网香似斯专石若兵弟谁校读志飞观争究包组造落视济喜离虽坐集编宝谈府拉黑且随格尽剑讲布杀微怕母调局根曾准团段终乐切级克精哪官示冷域读头"
SIGNS = "，。、；：？！…—·「」『』（）【】《》"


def pixels(char: str, size: int = 12, shift=(0, 0), whole: bool = False) -> list[str]:
    """The letter as the font draws it at `size` pixels (strokes '#'), at the top left of the
    cell as the matcher's own table has it, or `whole`: moved up so its last row isn't cut off
    (a patch's letters fill 11 rows), then moved by `shift`."""
    font = ImageFont.truetype(str(FONT), size, index=0)
    big = Image.new("L", (24, 24), 0)
    draw = ImageDraw.Draw(big)
    draw.fontmode = "1"
    draw.text((4, 4), char, font=font, fill=255)
    if whole:
        x0, y0, _, _ = big.getbbox()
        big = big.crop((x0 - shift[0], y0 - shift[1], x0 - shift[0] + 12, y0 - shift[1] + 12))
    else:
        big = big.crop((4 - shift[0], 4 - shift[1], 16 - shift[0], 16 - shift[1]))
    get = big.load()
    return ["".join("#" if get[x, y] > 127 else " " for x in range(12)) for y in range(12)]


@unittest.skipUnless(FONT, WHY)
class Matching(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.faces = hanzi.default_faces(FONT)
        cls.m = hanzi.Matcher(cls.faces, list(dict.fromkeys(WORDS)), list(SIGNS))

    def nearest(self, rows, top=3):
        return [c for c, _ in self.m.rank(rows, top)]

    def test_same_pixels_as_the_font_is_exact(self):
        for ch in "的国说":
            with self.subTest(ch=ch):
                self.assertEqual(self.m.same(pixels(ch)), [ch])
                names = hanzi.name_all({"p": {"rows": pixels(ch)}}, self.m)
                self.assertEqual((names["p"].char, names["p"].how), (ch, "exact"))

    def test_moved_or_smaller_letter_is_still_near(self):
        # Moved within the cell: the same candidates as where it was (each picture is fitted by
        # its ink box). Drawn 11 pixels high like a patch's own 12-dot font: still near. Whether a
        # font's own 12-pixel drawing is near its outline depends on the font (SimSun's 12-pixel
        # 说 is nearer 而, Noto Sans CJK's 国 nearer 同): those are what the reading by hand is for.
        for ch in "国道的":
            with self.subTest(ch=ch):
                self.assertEqual(self.nearest(pixels(ch, whole=True, shift=(1, 0))),
                                 self.nearest(pixels(ch, whole=True)))
                self.assertIn(ch, self.nearest(pixels(ch, size=11, whole=True)))

    def test_one_pixel_off_is_still_near(self):
        rows = [list(r) for r in pixels("道", whole=True)]
        x, y = next((x, y) for y, r in enumerate(rows) for x, c in enumerate(r) if c == "#")
        rows[y][x] = " "
        self.assertIn("道", self.nearest(["".join(r) for r in rows]))

    def test_tiny_picture_is_only_compared_with_signs(self):
        # A dot fitted into its own 2x2 box looks like any letter squeezed that small.
        dot = [" " * 12] * 5 + ["     ##     ", "     ##     "] + [" " * 12] * 5
        near = self.nearest(dot, 6)
        self.assertTrue(near)
        self.assertTrue(all(c in SIGNS for c in near), near)

    def test_dash_is_not_compared_with_square_letters(self):
        dash = [" " * 12] * 5 + [" ########## "] + [" " * 12] * 6
        near = self.nearest(dash, 6)
        self.assertIn(near[0], ("一", "—", "―"))
        self.assertNotIn("国", near)

    def test_fits(self):
        self.assertTrue(hanzi.Matcher.fits((60, 62), (11, 11)))
        self.assertFalse(hanzi.Matcher.fits((60, 62), (11, 1)))
        self.assertTrue(hanzi.Matcher.fits((60, 4), (10, 1)))

    def test_letter_that_is_no_candidate_goes_to_review(self):
        # 龍 (traditional) is not among the candidates: nothing may claim it as exact or close.
        names = hanzi.name_all({"p": {"rows": pixels("龍")}}, self.m)
        self.assertEqual(names["p"].how, "review")

    def test_naming_rules_come_first(self):
        latin = next(cell for cell, c in ja.LATIN.items() if c == "A")
        pictures = {
            "blank": {"rows": [" " * 12] * 12},
            "slot": {"rows": pixels("国"), "slots": [f"p0:{latin}"]},
            "ja": {"rows": pixels("国"), "japanese": "士"},
            "read": {"rows": pixels("国")},
        }
        names = hanzi.name_all(pictures, self.m, {"read": ("说", "by hand")})
        self.assertEqual((names["blank"].char, names["blank"].how), (hanzi.SPACE, "blank"))
        self.assertEqual((names["slot"].char, names["slot"].how), ("A", "slot"))
        self.assertEqual((names["ja"].char, names["ja"].how), ("士", "japanese"))
        self.assertEqual((names["read"].char, names["read"].how), ("说", "read"))


def name(char, how="read"):
    return hanzi.Name(char, how)


class Reading(unittest.TestCase):
    """Decoding drafts and picking a shared page's reading: no font needed."""

    def setUp(self):
        self.names = {"aaaaaaaaaaaa": name("你"), "bbbbbbbbbbbb": name("好"), "cccccccccccc": name("吗"),
                      "dddddddddddd": name("火"), "eeeeeeeeeeee": name("多"),
                      "ffffffffffff": name(hanzi.SPACE, "blank"), "999999999999": name("士", "japanese"),
                      "888888888888": name("ア", "japanese"), "777777777777": name("", "review")}

    @staticmethod
    def draft(ids):
        return "".join(f"{{pic {p * 12}}}" if len(p) == 1 else p for p in ids)

    def test_decode_lists_unnamed_pictures(self):
        chars = hanzi.named(self.names)
        text, missing = hanzi.decode("{name}『{pic aaaaaaaaaaaa}{pic 777777777777}』", chars)
        self.assertEqual(text, "{name}『你{pic 777777777777}』")
        self.assertEqual(missing, ["777777777777"])

    def test_choose_areas(self):
        rows = [
            {"key": "dlg/M001/000", "draft": self.draft("abc")},          # 你好吗: the real text
            {"key": "dlg/M001/001", "draft": self.draft("ab")},
            # Shared page: area 2 reads 你好吗, area 3 the same bytes in another room's letters.
            {"key": "dlg/M002/000", "areas": {"3": self.draft("dec"), "2": self.draft("abc")}},
            # Area 7 shows an empty cell mid-sentence; area 8 a kana; area 9 is right.
            {"key": "dlg/M002/001", "areas": {"7": self.draft("af"), "8": self.draft("a8"), "9": self.draft("ab")}},
            # A Japanese-font letter that is also Chinese (士) belongs.
            {"key": "dlg/M002/002", "areas": {"2": self.draft("a9"), "3": self.draft("a7")}},
            # Readings that tie on everything go the way the rest of the bank reads.
            {"key": "dlg/M002/003", "areas": {"3": self.draft("e"), "2": self.draft("d")}},
        ]
        chosen = hanzi.choose_areas(rows, self.names)
        self.assertEqual(chosen["dlg/M001/000"][0], None)
        self.assertEqual(chosen["dlg/M002/000"][0], "2")
        self.assertEqual(chosen["dlg/M002/001"][0], "9")
        self.assertEqual(chosen["dlg/M002/002"][0], "2")
        self.assertEqual(chosen["dlg/M002/003"][0], "2")


@unittest.skipUnless(FONT, WHY)
class Command(unittest.TestCase):
    def test_pictures_and_drafts_to_csv(self):
        import name_ja_patch_letters as cmd
        pictures = {"pictures": {
            "aaaaaaaaaaaa": {"rows": pixels("说"), "uses": 3},
            "bbbbbbbbbbbb": {"rows": pixels("国"), "uses": 2},
            "cccccccccccc": {"rows": [" " * 12] * 12, "uses": 1},
            "dddddddddddd": {"rows": pixels("龍"), "uses": 1},
            "eeeeeeeeeeee": {"rows": pixels("龍", shift=(1, 0)), "uses": 1},
        }}
        draft = {"disc": 1, "rows": [
            {"key": "dlg/M001/000", "draft": "{pic aaaaaaaaaaaa}{pic cccccccccccc}{pic bbbbbbbbbbbb}{br}{name}"},
            {"key": "dlg/M001/001", "draft": "{pic dddddddddddd}"},
            {"key": "dlg/M001/002", "draft": "{pic eeeeeeeeeeee}"},
        ]}
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / "p.json").write_text(json.dumps(pictures), encoding="utf-8")
            (tmp / "d.json").write_text(json.dumps(draft), encoding="utf-8")
            (tmp / "r.tsv").write_text("picture\tchar\tnote\neeeeeeeeeeee\t道\tread\n", encoding="utf-8")
            with redirect_stdout(io.StringIO()):
                code = cmd.main([str(tmp / "p.json"), "--drafts", str(tmp / "d.json"), "--reviewed",
                                 str(tmp / "r.tsv"), "-o", str(tmp / "out"), "--fonts", str(FONT)])
            self.assertEqual(code, 0)
            with open(tmp / "out" / "disc1.csv", encoding="utf-8", newline="") as f:
                rows = {r["key"]: r["translation"] for r in csv.DictReader(f)}
            self.assertEqual(rows, {"dlg/M001/000": "说　国{br}{name}", "dlg/M001/002": "道"})
            review = (tmp / "out" / "review.txt").read_text(encoding="utf-8")
            self.assertIn("dddddddddddd", review)
            self.assertIn("dlg/M001/001", review)
            self.assertTrue((tmp / "out" / "review.png").exists())
            letters = (tmp / "out" / "letters.tsv").read_text(encoding="utf-8")
            self.assertIn("aaaaaaaaaaaa\t说\texact", letters)
            # The output folder is never overwritten.
            with redirect_stdout(io.StringIO()), unittest.mock.patch("sys.stderr", io.StringIO()):
                self.assertEqual(cmd.main([str(tmp / "p.json"), "--drafts", str(tmp / "d.json"),
                                           "-o", str(tmp / "out"), "--fonts", str(FONT)]), 1)


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=2).result
    if not result.wasSuccessful():
        sys.exit(1)
    sys.exit(77 if not FONT else 0)
