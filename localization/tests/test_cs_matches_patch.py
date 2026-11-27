"""The Czech pack says and draws what the patched PS1 game does, on both USA discs.

The pack is built from the Czech fan patch for the USA discs (RK-Translations, v1.0, 2010; import-patch
--letters 59=Ů 68=ů 5E=ď 66=ť, build --from-patch --patch-letters). This test doesn't trust that
chain: it applies the real patch to a copy of each disc and reads the patched game itself, then
checks the pack against it.

- Text: every dialogue page, menu row, city map line and battle line the patch changes is in the
  pack with the same letters and commands in the same order, and every one it leaves alone keeps
  the English. The pack numbers its letters its own way (its letter table), so the patched game's
  bytes are turned into the pack's letter by letter before they are compared.
- Letters: the patch redraws many of the game's letters (0x00-0x4C) in its own style and adds 30
  of its own from 0x4D, one letter a code, each with its own dialogue width and menu metrics:
  Á Č Ď É Ě Í Ň Ó Ř Š Ť Ú Ů Ý Ž and the same small. Each letter the patch draws is in the pack
  with the patched game's own picture, width and metrics; no other code past 0x4C has a picture.
- Pictures: each picture piece the pack carries holds the patched disc's bytes at its place.

Needs the patch (PE_TEST_CS_PATCH_DIR, the folder with CD1_pv3.ppf and CD2_pv3.ppf from the
installer), the pack (PE_TEST_CS_PACK, the zipped player file) and USA disc 1 (PE_TEST_DISC);
PE_TEST_DISC2 adds Disc 2. Without them only the rule's own tests run and it exits 77. The
patched copies (about 750 MB each) go in a temporary folder and are deleted.

  python -I localization/tests/test_cs_matches_patch.py
"""
import os
import struct
import sys
import tempfile
import unittest
import zipfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pelang  # noqa: E402
from petext import font, gametext as gt, patchfile, patchimport, pictures  # noqa: E402
from petext.disc import Game  # noqa: E402
import test_it_matches_patch as it  # noqa: E402
from test_it_matches_patch import pack_letters, record_pixels  # noqa: E402

# The patch's letters, read from its letter pictures by eye (2026-11-17): the import names 27 of
# them from their look; 59 and 68 carry a ring (Ů ů), and 5E and 66 are ď and ť with the caron
# drawn as a stroke at the top right.
CZECH = {0x4D: "Á", 0x4E: "Č", 0x4F: "Ď", 0x50: "É", 0x51: "Ě", 0x52: "Í", 0x53: "Ň", 0x54: "Ó",
         0x55: "Ř", 0x56: "Š", 0x57: "Ť", 0x58: "Ú", 0x59: "Ů", 0x5A: "Ý", 0x5B: "Ž",
         0x5C: "á", 0x5D: "č", 0x5E: "ď", 0x5F: "é", 0x60: "ě", 0x61: "í", 0x62: "ň", 0x63: "ó",
         0x64: "ř", 0x65: "š", 0x66: "ť", 0x67: "ú", 0x68: "ů", 0x69: "ý", 0x6A: "ž"}
# Letters the patch draws that no row of its text uses, so the pack leaves them out.
UNUSED = "Ť"
# Symbols the patch redraws that aren't letters, so the pack can't name them and draws them the
# game's way: 0D, the small star in the name entry grid (sys/117, which the patch leaves as it is),
# drawn one row lower by the patch.
SYMBOLS_LEFT = [0x0D]
PATCH_FILES = {"SLUS_006.62": "CD1_pv3.ppf", "SLUS_006.68": "CD2_pv3.ppf"}
# Rows where the pack and the patched game differ, each checked by hand (2026-11-17).
EXPECTED = {
    # "Paměťová karta - slot 1/2" + " není zformátována" is wider than the 300 px the PS1 centres the
    # notice in, so it runs off the box. The port puts the notice on the box's next line
    # (menu_layouts.json "memory card notice", as for Italian): a line break at the end.
    "sys/071": "a line break at the end",
    "sys/072": "a line break at the end",
}


def to_pack(raw: bytes, code_of: dict[str, int]) -> bytes:
    """The patched game's bytes with each letter on the pack's code for it; commands as they are."""
    return it.to_pack(raw, code_of, CZECH)


def compare_text(original, patched, entries, code_of):
    """(rows that match, {key: how it differs}) over the dialogue, city map, menu rows and battle
    lines: test_it_matches_patch.py's walk with the Czech letters."""
    return it.compare_text(original, patched, entries, code_of, CZECH)


class RuleTest(unittest.TestCase):
    def test_letters_go_on_the_packs_codes_and_commands_stay(self):
        code_of = {"ř": 0x70, "Ů": 0x71}
        raw = bytes([gt.CODE_OF["P"], 0x64, 0xFB, 0x07, 0x64, 0x59, 0xF7])   # a wait's operand is no letter
        self.assertEqual(to_pack(raw, code_of), bytes([gt.CODE_OF["P"], 0x70, 0xFB, 0x07, 0x64, 0x71, 0xF7]))

    def test_the_table_is_czech(self):
        self.assertEqual(sorted(CZECH.values()), sorted("ÁČĎÉĚÍŇÓŘŠŤÚŮÝŽáčďéěíňóřšťúůýž"))
        self.assertEqual(sorted(CZECH), list(range(0x4D, 0x6B)))


class PatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        folder, pack, disc = (os.environ.get(k) for k in ("PE_TEST_CS_PATCH_DIR", "PE_TEST_CS_PACK", "PE_TEST_DISC"))
        if not (folder and all((Path(folder) / f).is_file() for f in PATCH_FILES.values())
                and pack and Path(pack).is_file() and disc and Path(disc).is_file()):
            raise unittest.SkipTest("needs PE_TEST_CS_PATCH_DIR, PE_TEST_CS_PACK and PE_TEST_DISC (USA disc 1)")
        cls.patches, cls.pack_path = Path(folder), Path(pack)
        data = pelang.pack_bytes(cls.pack_path)
        cls.records = pack_letters(data)
        cls.code_of = {chr(struct.unpack_from("<I", r)[0]): code for code, r in cls.records.items()}
        _, _, cls.entries = pelang.read_pack(cls.pack_path)
        with zipfile.ZipFile(cls.pack_path) as z:
            cls.pieces = pictures.read(z.read("pictures.bin"))
        cls.folder = tempfile.TemporaryDirectory()

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "folder"):
            cls.folder.cleanup()

    def games(self, cue: str):
        original = Game(cue)
        self.addCleanup(original.disc.f.close)
        out = Path(self.folder.name) / f"{original.exe_name}.bin"
        if not out.exists():
            patchfile.apply_patch(self.patches / PATCH_FILES[original.exe_name], original.disc.f.name, out)
        patched = Game(out)
        self.addCleanup(patched.disc.f.close)
        return original, patched

    def check_disc(self, cue: str):
        original, patched = self.games(cue)
        same, differ = compare_text(original, patched, self.entries, self.code_of)
        print(f"\n{original.exe_name}: {same} rows the same as the patched game; known differences: "
              f"{', '.join(sorted(differ)) or 'none'}")
        for key in sorted(set(differ) | set(EXPECTED)):
            if differ.get(key) != EXPECTED.get(key):
                print(f"  {key}: {differ.get(key)} (expected {EXPECTED.get(key)})")
        self.assertGreater(same, 2500)
        self.assertEqual(differ, EXPECTED)

        # The letters: the patch's 30 drawn the patched game's way.
        pal, width, _, px = font.page(patched, font.PAGE0_ENTRY)
        pal_o, width_o, _, px_o = font.page(original, font.PAGE0_ENTRY)
        exe = patched.disc.file(patched.exe_name)
        metrics, metrics_o = patchimport.menu_metrics(patched), patchimport.menu_metrics(original)
        # the patch reads one dialogue width a letter from WIDTHS_AT + 1 (the four reads changed)
        self.assertEqual([struct.unpack_from("<I", exe, at)[0] for at in patchimport.WIDTH_READS],
                         [0x0006B821, 0x00001821, 0x00001821, 0x00000000])
        # The pack carries only the letters its text uses; the text check above already found every
        # letter of every patched row on the pack's code for it.
        unused = "".join(ch for ch in CZECH.values() if ch not in self.code_of)
        self.assertEqual(unused, UNUSED)
        for code, char in CZECH.items():
            record = self.records.get(self.code_of.get(char))
            if char in UNUSED:
                continue
            self.assertEqual(record_pixels(record), list(patchimport._layered(pal, font.cell(px, width, code))), char)
            self.assertEqual(record[5], exe[patchimport.WIDTHS_AT + 1 + code], f"{char}: dialogue width")
            self.assertEqual(record[7], metrics[code], f"{char}: menu metrics")
        # The game's own letters: the patch redraws some of them in its own style. Each one it
        # changes in any way (picture, dialogue width, menu metrics) the pack draws the patch's way;
        # each one it leaves alone, the pack leaves to the game.
        redrawn, symbols = "", []
        for code in range(0x4D):
            char = gt.CHARS.get(code)
            same = (font.cell(px, width, code) == font.cell(px_o, width_o, code)
                    and exe[patchimport.WIDTHS_AT + 1 + code] == gt.ADVANCE[code] and metrics[code] == metrics_o[code])
            record = self.records.get(self.code_of.get(char)) if char else None
            if same:
                continue
            if char is None:
                symbols.append(code)
                continue
            self.assertIsNotNone(record, f"code {code:02X} ({char!r}): the patch redraws it, the pack doesn't")
            self.assertEqual(record_pixels(record), list(patchimport._layered(pal, font.cell(px, width, code))), repr(char))
            self.assertEqual(record[5], exe[patchimport.WIDTHS_AT + 1 + code], f"{char!r}: dialogue width")
            self.assertEqual(record[7], metrics[code], f"{char!r}: menu metrics")
            redrawn += char
        self.assertEqual(symbols, SYMBOLS_LEFT)
        print(f"{original.exe_name}: the patch redraws {len(redrawn)} of the game's letters, the pack too: {redrawn}")
        for code in range(0x4D, 0xF1):
            self.assertEqual(any(font.cell(px, width, code)), code in CZECH, f"code {code:02X}: a picture no letter names")
        print(f"{original.exe_name}: {len(CZECH) - len(UNUSED)} letters drawn the patched game's way "
              f"(unused by the text: {UNUSED})")

        # The pictures: a piece that lands on this disc holds the patched disc's bytes there.
        landed = 0
        for piece in self.pieces:
            english = original.pe(piece.sector, 1)[piece.offset:piece.offset + len(piece.data)]
            if zlib.crc32(english) != piece.original_crc:
                continue
            landed += 1
            self.assertEqual(piece.data, patched.pe(piece.sector, 1)[piece.offset:piece.offset + len(piece.data)],
                             f"picture piece at sector {piece.sector} + {piece.offset}")
        print(f"{original.exe_name}: {landed} of {len(self.pieces)} picture pieces land, each the patched disc's bytes")
        self.assertEqual(landed, len(self.pieces))

    def test_disc_1(self):
        self.check_disc(os.environ["PE_TEST_DISC"])

    def test_disc_2(self):
        disc2 = os.environ.get("PE_TEST_DISC2")
        if not disc2 or not Path(disc2).is_file():
            self.skipTest("needs PE_TEST_DISC2 (USA disc 2)")
        self.check_disc(disc2)


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=2).result
    if not result.wasSuccessful():
        sys.exit(1)
    sys.exit(77 if result.skipped else 0)
