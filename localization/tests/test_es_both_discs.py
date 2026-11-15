"""The Spanish pack on both USA discs: its pictures land on them, and Disc 2's dialogue is Disc 1's.

A pack's pictures.bin holds the pictures with words a fan patch redrew: each piece a PE.IMG sector,
a place in it, the CRC-32 of the English bytes there and the patch's bytes. pe_lang puts a piece in
only when the bytes it reads from the disc have that CRC (pe_lang_pictures.c), so a piece whose
CRC matches neither disc never shows. This reads each piece's place on USA disc 1 and disc 2, as
the mod does, and checks that every piece lands on at least one of them; it prints which rooms and
entries the pieces change on each disc.

The text: every dialogue page on Disc 2 is also on Disc 1, with the same English and the same boxes,
so test_es_box_fit.py (which reads Disc 1) covers Disc 2's dialogue too. This checks that.

Needs the pack (PE_TEST_ES_PACK, the zipped player file with pictures.bin), USA disc 1
(PE_TEST_DISC) and disc 2 (PE_TEST_DISC2); without them only the rule's own tests run and it
exits 77.

  python -I localization/tests/test_es_both_discs.py
"""
import collections
import json
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE))
from petext import pictures  # noqa: E402
from petext.disc import Game  # noqa: E402


def lands(piece: pictures.Piece, read) -> bool:
    """Whether pe_lang would put `piece` in: the English bytes at its place have its CRC. `read`
    gives PE.IMG sectors (Game.pe)."""
    english = read(piece.sector, 1)[piece.offset:piece.offset + len(piece.data)]
    return len(english) == len(piece.data) and zlib.crc32(english) == piece.original_crc


def where(game, sector: int) -> str:
    """The room (its picture runs) or PE.IMG entry a sector belongs to."""
    for number, (start, scratch, texture, data) in enumerate(game.rooms, 1):
        if data and start <= sector < start + scratch + texture:
            return f"M{number:03}"
    s = game.sectors
    for entry in range(len(s) - 1):
        if s[entry] <= sector < s[entry + 1]:
            return f"entry {entry}"
    return "past the entries"


class RuleTest(unittest.TestCase):
    def test_a_piece_lands_only_on_its_english_bytes(self):
        image = bytes(range(256)) * 16                     # two sectors
        read = lambda sector, count: image[sector * 2048:(sector + count) * 2048]
        english = image[2048 + 10:2048 + 14]
        good = pictures.Piece(1, 10, zlib.crc32(english), b"Hola")
        self.assertTrue(lands(good, read))
        self.assertFalse(lands(pictures.Piece(1, 11, zlib.crc32(english), b"Hola"), read))   # moved
        self.assertFalse(lands(pictures.Piece(1, 2046, zlib.crc32(english), b"Hola"), read))  # past the sector


class PackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        pack = os.environ.get("PE_TEST_ES_PACK")
        discs = [os.environ.get("PE_TEST_DISC"), os.environ.get("PE_TEST_DISC2")]
        if not pack or not Path(pack).is_file() or not all(d and Path(d).is_file() for d in discs):
            raise unittest.SkipTest("needs PE_TEST_ES_PACK, PE_TEST_DISC (USA disc 1) and PE_TEST_DISC2 (disc 2)")
        with zipfile.ZipFile(pack) as z:
            cls.pieces = pictures.read(z.read("pictures.bin"))
        cls.discs = [Game(d) for d in discs]
        cls.paths = discs

    @classmethod
    def tearDownClass(cls):
        for game in getattr(cls, "discs", []):
            game.disc.f.close()

    def test_every_piece_lands_on_a_disc(self):
        self.assertGreater(len(self.pieces), 0)
        nowhere = []
        for n, game in enumerate(self.discs, 1):
            places = collections.Counter(where(game, p.sector) for p in self.pieces if lands(p, game.pe))
            print(f"\ndisc {n}: {sum(places.values())} of {len(self.pieces)} pieces land, in "
                  f"{len(places)} places ({', '.join(f'{k} {v}' for k, v in sorted(places.items()))})")
        for p in self.pieces:
            if not any(lands(p, game.pe) for game in self.discs):
                nowhere.append(f"sector {p.sector} + {p.offset}, {len(p.data)} bytes")
        self.assertEqual(nowhere, [], "pieces whose English bytes are on neither disc")

    def test_disc_2s_dialogue_is_disc_1s(self):
        pages = []
        for path in self.paths:
            out = Path(tempfile.mkdtemp())
            subprocess.run([sys.executable, "-I", str(HERE / "extract_text.py"), path, "--out", str(out)],
                           check=True, capture_output=True)
            rows = json.loads((out / "strings.json").read_text(encoding="utf-8"))
            pages.append({e["key"]: (e["raw"], e["boxes"], e["boxes_possible"]) for e in rows
                          if e["key"].startswith("dlg/")})
        one, two = pages
        print(f"\ndisc 2: {len(two)} dialogue pages, disc 1: {len(one)}")
        self.assertGreater(len(two), 2000)
        self.assertEqual([k for k in two if two[k] != one.get(k)], [],
                         "Disc 2 pages not on Disc 1 as they are (test_es_box_fit.py doesn't see them)")


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=2).result
    if not result.wasSuccessful():
        sys.exit(1)
    sys.exit(77 if result.skipped else 0)
