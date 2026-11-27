"""The Chinese pack's pictures with words on the USA discs, against the patched Japanese game itself.

The Chinese fan patch is for the Japanese discs, which keep their pictures at other places than the
USA discs do. The title menu's words and the map's place names are English pictures on both
releases, most of them byte for byte the same, so the pack carries the patch's Chinese ones for the
USA discs (import_ja_patch.py --disc-pictures; petext/pictures.py, across).

This doesn't take the import tool's word for it. It lays the real patch over the Japanese disc
(read only: nothing is written), finds every TIM picture the patch redrew in the numbered PE.IMG
entries, and on each USA disc, with the pack's pieces put in as pe_lang puts them in (a piece goes
in only where the disc's bytes have its CRC), checks that:
- the patch redrew exactly the pictures listed here (CARRIED and LEFT_OUT), so a patch that
  redraws another one doesn't go unnoticed;
- every picture in CARRIED is byte for byte the patched Japanese game's picture;
- the picture in LEFT_OUT (the title picture: the USA disc draws it differently, so the patch's
  line under the logo has no place on it) is the USA disc's own;
- every piece of the pack lands on the disc, inside one of the CARRIED pictures: nothing else on
  the disc changes.

Needs the pack (PE_TEST_ZH_PACK, the zipped player file with pictures.bin), the patch
(PE_TEST_ZH_PATCH_DIR: the folder with its PPFs, named SLPS_012.30 and SLPS_012.31), Japanese
disc 1 (PE_TEST_JA_DISC) and USA disc 1 (PE_TEST_DISC); PE_TEST_JA_DISC2 and PE_TEST_DISC2 add
the second discs. Without them only the rule's own test runs and it exits 77.

  python -I localization/tests/test_zh_pictures.py
"""
import os
import sys
import unittest
import zipfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE))
from petext import font, patchfile, pictures  # noqa: E402
from petext.disc import Game, SECTOR  # noqa: E402

# (PE.IMG entry, which TIM of it from 0): the same on the Japanese and the USA discs.
CARRIED = {(70, 1): "Press Start Button", (70, 3): "New Game", (70, 4): "Continue", (70, 5): "Tutorial",
           (70, 6): "EX Game", (72, 1): "the map's place names"}
LEFT_OUT = {(70, 0): "the title picture (the patch's line under the logo)"}
FONT_ENTRIES = {font.PAGE0_ENTRY, *font.PAGE1_ENTRIES}
NOT_TIMS = {9, 76}                 # field backgrounds; effect data (on the Japanese discs it starts with a message bank)


def is_letters(entry: int, rect) -> bool:
    """A letters page: the pack's letters replace those, they are not pictures with words."""
    return rect == pictures.LETTERS_PAGE or (entry in FONT_ENTRIES and rect[:2] in pictures.LETTERS_AT)


def put_in(data: bytes, first_sector: int, pieces) -> tuple[bytes, list]:
    """`data` (PE.IMG from `first_sector`) as the game shows it with the pack, and the pieces that
    went in: each only where the bytes there have its CRC (mods/pe_lang/pe_lang_pictures.c)."""
    shown, landed = bytearray(data), []
    for p in pieces:
        at = (p.sector - first_sector) * SECTOR + p.offset
        if 0 <= at and at + len(p.data) <= len(data) and zlib.crc32(data[at:at + len(p.data)]) == p.original_crc:
            shown[at:at + len(p.data)] = p.data
            landed.append(p)
    return bytes(shown), landed


class RuleTest(unittest.TestCase):
    def test_a_piece_goes_in_only_where_its_english_bytes_are(self):
        data = bytes(range(256)) * 16
        english = data[SECTOR + 10:SECTOR + 14]
        good = pictures.Piece(5, 10, zlib.crc32(english), b"\xe6\x96\xb0!")
        shown, landed = put_in(data, 4, [good, pictures.Piece(5, 11, zlib.crc32(english), b"oops")])
        self.assertEqual(landed, [good])
        self.assertEqual(shown[SECTOR + 10:SECTOR + 14], b"\xe6\x96\xb0!")
        self.assertEqual(shown[:SECTOR + 10] + english + shown[SECTOR + 14:], data)
        self.assertTrue(is_letters(72, (320, 0, 64, 256)) and is_letters(45, (384, 0, 64, 240)))
        self.assertFalse(is_letters(72, (960, 0, 64, 143)) or is_letters(70, (0, 0, 126, 24)))


class PackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        env = os.environ.get
        pack, patches = env("PE_TEST_ZH_PACK"), env("PE_TEST_ZH_PATCH_DIR")
        cls.pairs = {1: (env("PE_TEST_JA_DISC"), env("PE_TEST_DISC"), "SLPS_012.30"),
                     2: (env("PE_TEST_JA_DISC2"), env("PE_TEST_DISC2"), "SLPS_012.31")}
        ja, us, name = cls.pairs[1]
        if not (pack and Path(pack).is_file() and patches and (Path(patches) / name).is_file()
                and ja and Path(ja).is_file() and us and Path(us).is_file()):
            raise unittest.SkipTest("needs PE_TEST_ZH_PACK, PE_TEST_ZH_PATCH_DIR, PE_TEST_JA_DISC and PE_TEST_DISC")
        cls.patches = Path(patches)
        with zipfile.ZipFile(pack) as z:
            if "pictures.bin" not in z.namelist():
                raise AssertionError(f"{pack} has no pictures.bin: not the player file with the patch's pictures")
            cls.pieces = pictures.read(z.read("pictures.bin"))

    def check_disc(self, number: int):
        ja_path, us_path, patch = self.pairs[number]
        if not (ja_path and Path(ja_path).is_file() and us_path and Path(us_path).is_file()
                and (self.patches / patch).is_file()):
            self.skipTest(f"needs Japanese and USA disc {number} (PE_TEST_JA_DISC2, PE_TEST_DISC2) and {patch}")
        ja, zh, us = Game(ja_path), Game(ja_path), Game(us_path)
        try:
            self.assertEqual((ja.region, us.region), ("Japan", "USA"))
            # The patch laid over the Japanese disc as it is read; the image itself is only read.
            records = sorted(patchfile.ppf_records((self.patches / patch).read_bytes()))
            zh.disc._overlay, zh.disc._overlay_starts = records, [offset for offset, _ in records]
            redrawn, inside, landed_all = {}, [], []
            for entry in range(len(ja.sectors) - 1):
                if entry in NOT_TIMS:
                    continue
                count = ja.sectors[entry + 1] - ja.sectors[entry]
                a, b = ja.pe(ja.sectors[entry], count), zh.pe(ja.sectors[entry], count)
                first = us.sectors[entry]
                u = us.pe(first, us.sectors[entry + 1] - first)
                shown, landed = put_in(u, first, self.pieces)
                landed_all += landed
                if a == b:
                    self.assertEqual(landed, [], f"pieces land in entry {entry}, which the patch doesn't touch")
                    continue
                theirs = list(pictures.tims(u))
                for n, (lo, hi, rect) in enumerate(pictures.tims(a)):
                    if a[lo:hi] == b[lo:hi] or is_letters(entry, rect):
                        continue
                    redrawn[(entry, n)] = rect
                    self.assertLess(n, len(theirs), f"USA disc {number} has no picture {entry}/{n}")
                    lo2, hi2, rect2 = theirs[n]
                    self.assertEqual((rect2, hi2 - lo2), (rect, hi - lo), f"picture {entry}/{n} is another size on the USA disc")
                    if (entry, n) in CARRIED:
                        self.assertEqual(shown[lo2:hi2], b[lo:hi],
                                         f"{CARRIED[entry, n]} ({entry}/{n}) with the pack is not the patched game's picture")
                        self.assertNotEqual(u[lo2:hi2], b[lo:hi])
                        inside.append((first * SECTOR + lo2, first * SECTOR + hi2))
                    else:
                        self.assertEqual(shown[lo2:hi2], u[lo2:hi2], f"picture {entry}/{n} changes on the USA disc")
            self.assertEqual(set(redrawn), set(CARRIED) | set(LEFT_OUT),
                             "the pictures the patch redraws (letters pages aside) are not the ones this test knows")
            self.assertEqual(len(landed_all), len(self.pieces), f"only {len(landed_all)} of {len(self.pieces)} pieces "
                                                                f"land on USA disc {number}")
            for p in self.pieces:
                at = p.sector * SECTOR + p.offset
                self.assertTrue(any(lo <= at and at + len(p.data) <= hi for lo, hi in inside),
                                f"the piece at sector {p.sector} + {p.offset} is outside the carried pictures")
            print(f"\ndisc {number}: all {len(self.pieces)} pieces land; {len(CARRIED)} pictures are the patched game's "
                  f"({', '.join(CARRIED.values())}); left as the USA disc has it: {', '.join(LEFT_OUT.values())}")
        finally:
            for game in (ja, zh, us):
                game.disc.f.close()

    def test_disc_1_pictures_are_the_patched_games(self):
        self.check_disc(1)

    def test_disc_2_pictures_are_the_patched_games(self):
        self.check_disc(2)


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=2).result
    if not result.wasSuccessful():
        sys.exit(1)
    sys.exit(77 if result.skipped else 0)
