"""petext/pictures.py: which patch changes count as pictures, and pictures.bin as the game reads it."""
import struct
import sys
import tempfile
import unittest
import zipfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pelang  # noqa: E402
from petext import pictures  # noqa: E402

SECTOR = 2048


def tim(x, y, w, h, fill=0x11, clut=True) -> bytes:
    """A 4-bit TIM with an optional 16-colour CLUT, its image at VRAM x,y (w words wide)."""
    out = struct.pack("<II", 0x10, 8 if clut else 0)
    if clut:
        out += struct.pack("<IHHHH", 12 + 32, 0, 0x1F0, 16, 1) + bytes(32)
    return out + struct.pack("<IHHHH", 12 + w * h * 2, x, y, w, h) + bytes([fill]) * (w * h * 2)


class FakeGame:
    """Just what pictures.py reads: the PE.IMG sector table, sectors, and the room table."""

    def __init__(self, entries: list[bytes], rooms: list[tuple[bytes, bytes, bytes]]):
        image, self.sectors = bytearray(), []
        for data in entries:
            self.sectors.append(len(image) // SECTOR)
            image += data + bytes(-len(data) % SECTOR)
        self.sectors.append(len(image) // SECTOR)
        self.rooms = []
        for runs in rooms:
            start, counts = len(image) // SECTOR, []
            for run in runs:
                counts.append((len(run) + SECTOR - 1) // SECTOR)
                image += run + bytes(-len(run) % SECTOR)
            self.rooms.append((start, *counts))
        self.image = bytes(image)

    def pe(self, sector, count):
        return self.image[sector * SECTOR:(sector + count) * SECTOR]


def game(changed: bool) -> FakeGame:
    letters = tim(320, 0, 64, 256, 0x22 if changed else 0x11)
    menu = tim(960, 0, 16, 4, 0x33 if changed else 0x11)
    text = b"Hola" if changed else b"Hi!!"
    entries = [b"\0" * 16] * 81
    entries[1] = text + bytes(100)                      # text: not a picture
    entries[9] = bytes(300) + (b"\x55" if changed else b"\x00") * 4   # whole entry: pictures
    entries[72] = bytes(8) + letters + menu              # letters page left out, menu picture taken
    room = (b"scratch" + (b"X" if changed else b"-"), b"texture" + (b"Y" if changed else b"-"),
            b"script" + (b"Z" if changed else b"-"))
    return FakeGame(entries, [room, (b"", b"", b"")])


class PicturesTest(unittest.TestCase):
    def test_only_picture_data_is_taken_and_the_rest_reported(self):
        original, translated = game(False), game(True)
        pieces, report = pictures.diff(original, translated)
        taken = report["pictures_taken"]
        self.assertEqual(set(taken), {"entry 9, field background pictures", "entry 72, picture at VRAM 960,0",
                                      "M001 pictures"})
        self.assertEqual(taken["entry 9, field background pictures"], 4)
        self.assertEqual(taken["M001 pictures"], 2)     # one byte in each of the two picture runs
        left = report["changes_left_out"]
        self.assertIn("entry 72, picture at VRAM 320,0: the letters page", left)
        self.assertIn("entry 1", left)
        self.assertIn("M001 data", left)
        # Each piece holds the patch's bytes and the CRC of the English bytes where it goes.
        for p in pieces:
            at = p.sector * SECTOR + p.offset
            self.assertEqual(zlib.crc32(original.image[at:at + len(p.data)]), p.original_crc)
            self.assertEqual(translated.image[at:at + len(p.data)], p.data)
            self.assertLessEqual(p.offset + len(p.data), SECTOR)

    def test_a_change_across_a_sector_line_is_two_pieces(self):
        a = FakeGame([b""] * 9 + [bytes(4096)] + [b""] * 71, [])
        changed = bytearray(bytes(4096))
        changed[2040:2060] = b"\x01" * 20
        b = FakeGame([b""] * 9 + [bytes(changed)] + [b""] * 71, [])
        pieces, _ = pictures.diff(a, b)
        self.assertEqual([(p.sector - a.sectors[9], p.offset, len(p.data)) for p in pieces], [(0, 2040, 8), (1, 0, 12)])

    def test_file_round_trip_order_and_damage(self):
        pieces = [pictures.Piece(12, 2040, 7, b"Nivel!!!"), pictures.Piece(10, 100, 5, b"SALI"),
                  pictures.Piece(12, 2040, 6, b"Tiempo.."), pictures.Piece(10, 100, 5, b"SALI")]
        data = pictures.write(pieces)
        got = pictures.read(data)
        self.assertEqual([(p.sector, p.offset) for p in got], [(10, 100), (12, 2040), (12, 2040)])  # sorted, duplicate kept once
        self.assertEqual(data[:4], b"PEPX")
        with self.assertRaises(ValueError):
            pictures.write([pictures.Piece(1, 2040, 0, b"123456789")])           # past its sector
        for at, value in ((0, ord("X")), (4, 2), (16 + 6, 0)):
            bad = bytearray(data)
            bad[at] = value
            with self.assertRaises(ValueError):
                pictures.read(bytes(bad))

    def test_bundle_puts_both_discs_pictures_in_one_file(self):
        folder = Path(tempfile.mkdtemp())
        pack = folder / "t.pelang"
        head = pelang.HEADER.pack(pelang.MAGIC, 1, 0, 64, b"xx", b"Test")
        pack.write_bytes(head)
        cd1, cd2 = folder / "cd1.bin", folder / "cd2.bin"
        cd1.write_bytes(pictures.write([pictures.Piece(10, 0, 1, b"A")]))
        cd2.write_bytes(pictures.write([pictures.Piece(10, 0, 2, b"B"), pictures.Piece(10, 0, 1, b"A")]))
        out = folder / "Test.pelang"
        self.assertEqual(pelang.main(["bundle", str(pack), "-o", str(out), "--pictures", str(cd1), "--pictures", str(cd2)]), 0)
        with zipfile.ZipFile(out) as z:
            self.assertEqual(sorted(z.namelist()), ["pictures.bin", "text.bin"])
            self.assertEqual(len(pictures.read(z.read("pictures.bin"))), 2)
        (folder / "bad.bin").write_bytes(b"PEPX")
        self.assertEqual(pelang.main(["bundle", str(pack), "-o", str(folder / "no.pelang"), "--pictures", str(folder / "bad.bin")]), 1)
        self.assertFalse((folder / "no.pelang").exists())


def put_in(game: FakeGame, pieces) -> bytes:
    """The disc as the game shows it with these pieces: each goes in where its English bytes are."""
    image = bytearray(game.image)
    for p in pieces:
        at = p.sector * SECTOR + p.offset
        if zlib.crc32(game.image[at:at + len(p.data)]) != p.original_crc:
            raise AssertionError(f"the piece at sector {p.sector} + {p.offset} does not land")
        image[at:at + len(p.data)] = p.data
    return bytes(image)


class AcrossTest(unittest.TestCase):
    """pictures.across: a Japanese-disc patch's pictures for a pack played on the USA discs."""

    WORD, NEW_WORD = tim(0, 0, 16, 4, 0x11, clut=False), tim(0, 0, 16, 4, 0x77, clut=False)
    JA_NAME, US_NAME, NEW_NAME = (tim(0, 0, 16, 4, v, clut=False) for v in (0x33, 0x44, 0x88))

    @staticmethod
    def disc(pad: int, word: bytes, name: bytes, letters: int, copies: int = 1) -> FakeGame:
        entries = [b"\0" * 16] * 81
        entries[45] = tim(384, 0, 64, 4, letters)                 # an area's letters page (a font entry)
        entries[70] = bytes(pad) + word + name + word * (copies - 1)
        entries[72] = bytes(8) + tim(320, 0, 64, 256, letters)    # the letters page
        entries[76] = bytes([letters]) * 64                       # no TIMs: never looked at
        return FakeGame(entries, [])

    def setUp(self):
        self.ja = self.disc(8, self.WORD, self.JA_NAME, 0x11)
        self.zh = self.disc(8, self.NEW_WORD, self.NEW_NAME, 0x22)
        self.us = self.disc(2100, self.WORD, self.US_NAME, 0x11)   # the same word, in another sector
        self.word_at = self.us.sectors[70] * SECTOR + 2100
        self.name_at = self.word_at + len(self.WORD)

    def test_a_picture_the_other_disc_holds_byte_for_byte_is_taken_where_that_disc_has_it(self):
        pieces, report = pictures.across(self.ja, self.zh, self.us, font_entries=[45])
        self.assertEqual(list(report["pictures_taken"]), ["entry 70, picture 0 at VRAM 0,0"])
        self.assertEqual(list(report["pictures_left_out"]), ["entry 70, picture 1 at VRAM 0,0"])
        shown = put_in(self.us, pieces)
        self.assertEqual(shown[self.word_at:self.word_at + len(self.WORD)], self.NEW_WORD)
        # Everything else is the USA disc's own: its differently drawn picture, both letters pages, entry 76.
        rest = bytearray(shown)
        rest[self.word_at:self.word_at + len(self.WORD)] = self.WORD
        self.assertEqual(bytes(rest), self.us.image)

    def test_a_differently_drawn_picture_goes_in_whole_only_when_named(self):
        pieces, report = pictures.across(self.ja, self.zh, self.us, font_entries=[45], whole=["70/1"])
        self.assertEqual(report["pictures_left_out"], {})
        shown = put_in(self.us, pieces)
        self.assertEqual(shown[self.name_at:self.name_at + len(self.NEW_NAME)], self.NEW_NAME)
        with self.assertRaises(ValueError):                        # not a picture the patch redrew
            pictures.across(self.ja, self.zh, self.us, font_entries=[45], whole=["70/5"])
        wider = self.disc(2100, self.WORD, tim(0, 0, 32, 4, 0x44, clut=False), 0x11)
        with self.assertRaises(ValueError):                        # another size there
            pictures.across(self.ja, self.zh, wider, font_entries=[45], whole=["70/1"])

    def test_every_copy_on_the_other_disc_gets_the_picture(self):
        us = self.disc(2100, self.WORD, self.US_NAME, 0x11, copies=2)
        pieces, _ = pictures.across(self.ja, self.zh, us, font_entries=[45])
        shown = put_in(us, pieces)
        second = self.name_at + len(self.US_NAME)
        self.assertEqual(shown[self.word_at:self.word_at + len(self.WORD)], self.NEW_WORD)
        self.assertEqual(shown[second:second + len(self.WORD)], self.NEW_WORD)

    def test_a_patch_that_redraws_no_picture_gives_none(self):
        letters_only = self.disc(8, self.WORD, self.JA_NAME, 0x22)
        self.assertEqual(pictures.across(self.ja, letters_only, self.us, font_entries=[45]),
                         ([], {"pictures_taken": {}, "pictures_left_out": {}}))


if __name__ == "__main__":
    unittest.main()
