"""The Italian pack says and draws what the patched PS1 game does, on both USA discs.

The pack is built from the Italian fan patch for the USA discs (import-patch, build --from-patch
--patch-letters). This test doesn't trust that chain: it applies the real patch to a copy of each
disc and reads the patched game itself, then checks the pack against it.

- Text: every dialogue page, menu row and battle line the patch changes is in the pack with the
  same letters and commands in the same order, and every one it leaves alone keeps the English.
  The pack numbers its accented letters its own way (its letter table), so the patched game's
  bytes are turned into the pack's letter by letter before they are compared; the speaker colour
  the patch wraps a name in is left out, as pe_fonts adds it in every language.
- Letters: each of the patch's six accented letters is drawn with the patched game's own picture,
  dialogue width and menu metrics, and the patch leaves the game's other letters as they were
  (the pack draws those the game's way).
- Pictures: each picture piece the pack carries holds the patched disc's bytes at its place.

Known differences, each checked by hand (2026-11-10), are listed in EXPECTED with why.

Needs the patch (PE_TEST_IT_PATCH, itpch_pe_v090.ppf), the pack (PE_TEST_IT_PACK, the zipped
player file) and USA disc 1 (PE_TEST_DISC); PE_TEST_DISC2 adds Disc 2. Without them only the
rule's own tests run and it exits 77. The patched copies (about 750 MB each) go in a temporary
folder and are deleted.

  python -I localization/tests/test_it_matches_patch.py
"""
import json
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
import pelang  # noqa: E402
from extract_text import table_entries  # noqa: E402
from petext import battle, citymap, font, gametext as gt, letters as lt, patchfile, patchimport, pictures, validation  # noqa: E402
from petext.disc import Game  # noqa: E402

# The patch's accented letters, read from its letter pictures by eye (2026-11-10).
ITALIAN = {0x4D: "à", 0x4E: "è", 0x4F: "é", 0x50: "ì", 0x51: "ò", 0x52: "ù"}

EXPECTED = {
    # "Memory Card nello Slot 1/2" + " non è formattata" is 305 px with the patch's letters; the
    # PS1 box (centred in 300 px) runs off the screen. The port puts the notice on the box's next
    # line (menu_layouts.json "memory card notice", the split-lines rule): a line break at the end.
    "sys/071": "a line break at the end",
    "sys/072": "a line break at the end",
    # The Japanese kana grids: the USA game always opens the Latin one, sys/117 (Menu_OpenRenameScreen:
    # g_SaveMetadataWindowIndex is only ever 1, so no page list). The patch changed one cell in each
    # (A2 -> A1, F2 -> F1); the pack keeps them as they were.
    "sys/115": "keeps the English",
    "sys/116": "keeps the English",
}
LINE_BREAK = b"\xf1\xc8\x01\x00"


def pack_letters(data: bytes) -> dict[int, bytes]:
    """code -> letter record, for each coded record of a compiled pack."""
    at, count = struct.unpack_from("<II", data, 80)
    out = {}
    for k in range(count):
        record = data[at + k * lt.RECORD:at + (k + 1) * lt.RECORD]
        if lt.coded(k, record):
            out[record[4]] = record
    return out


def record_pixels(record: bytes) -> list[int]:
    return [v for b in record[8:80] for v in (b & 15, b >> 4)]


def to_pack(raw: bytes, code_of: dict[str, int]) -> bytes:
    """The patched game's bytes with each letter on the pack's code for it; commands as they are."""
    out = bytearray()
    for c in validation.commands(raw):
        if len(c) == 1 and c[0] < 0xF1:
            char = ITALIAN.get(c[0]) if c[0] >= 0x4D else gt.CHARS.get(c[0])
            out.append(code_of.get(char, c[0]))
        else:
            out += c
    return bytes(out)


def compare_text(original, patched, entries: dict[int, bytes], code_of: dict[str, int]) -> tuple[int, dict]:
    """(rows that match, {key: how it differs}) over the dialogue, the menu rows and battle lines."""
    disc = {"SLUS_006.62": "1", "SLUS_006.68": "2"}[original.exe_name]
    same, differ = 0, {}

    def check(key, english, ps1):
        nonlocal same
        got = entries.get(pelang.fnv1a(key))
        if ps1 == english:
            if got is not None:
                differ[key] = "translated where the patch keeps the English"
            return
        if got is None:
            differ[key] = "keeps the English"
            return
        want = to_pack(patchimport.clean_page(ps1, english), code_of)
        got = got[:-1] if got[-1:] == b"\xf9" else got          # the box's end, kept apart on the disc
        if got == want:
            same += 1
        elif got == want + LINE_BREAK:
            differ[key] = "a line break at the end"
        else:
            differ[key] = f"differs: pack {got.hex(' ')} / patched game {want.hex(' ')}"

    catalog = json.loads((HERE / "editor/catalog.json").read_text(encoding="utf-8"))
    for bank in catalog["banks"]:
        number, prefix = bank["locations"][disc], f'dlg/{bank["key"]}/'
        before = pelang._dialogue_pages(original, number)
        after = pelang._dialogue_pages(patched, number, patchimport.patched_pages)
        for page, (raw, _) in before.items():
            key = f"{prefix}{page:03}"
            if page not in after:
                differ[key] = "page missing from the patch"
            else:
                check(key, raw, after[page][0])
    # The city map's bank (petext/citymap.py): the patch changed only its text there, which the
    # pack has as dlg/MAP lines.
    map_a, map_b = citymap.entry_bytes(original), citymap.entry_bytes(patched)
    if not citymap.only_text_changed(map_a, map_b):
        raise AssertionError("the patch changed more than the city map's text in its entry")
    after = patchimport.patched_pages(citymap.english_bank(map_b))
    for page, (raw, _) in gt.pages(citymap.english_bank(map_a))[0].items():
        key = f"dlg/{citymap.BANK_KEY}/{page:03}"
        if page not in after:
            differ[key] = "page missing from the patch"
        else:
            check(key, raw, after[page][0])
    manifest = json.loads((HERE / "data/manifest.json").read_text(encoding="utf-8"))
    before, after = dict(table_entries(original.resident_pack())), dict(table_entries(patched.resident_pack()))
    for key in manifest["entries"]:
        if key.split("/")[0] in ("sys", "item", "desc", "misc") and key in before:
            check(key, before[key], after[key])
    exe_before, exe_after = original.disc.file(original.exe_name), patched.disc.file(patched.exe_name)
    lines, why = battle.patched(exe_before, exe_after)
    if why:
        differ["battle/*"] = why
    english = battle.english(exe_before)
    for key, raw in lines.items():
        check(key, english[key], raw)
    return same, differ


class RuleTest(unittest.TestCase):
    def test_letters_go_on_the_packs_codes_and_commands_stay(self):
        code_of = {"é": 0x53, "ù": 0x51}
        raw = bytes([gt.CODE_OF["P"], 0x4F, 0xFB, 0x07, 0x4F, 0x52, 0xF7])   # a wait's operand is no letter
        self.assertEqual(to_pack(raw, code_of), bytes([gt.CODE_OF["P"], 0x53, 0xFB, 0x07, 0x4F, 0x51, 0xF7]))

    def test_a_record_reads_back_its_picture(self):
        pixels = tuple((i * 7) % 3 for i in range(144))
        record = lt.Letter("à", pixels=pixels, dialogue=8, menu=0x70).record(0x4D)
        data = bytearray(96) + record
        struct.pack_into("<II", data, 80, 96, 1)
        self.assertEqual(pack_letters(bytes(data)), {0x4D: record})
        self.assertEqual(record_pixels(record), list(pixels))
        self.assertEqual((record[5], record[7]), (8, 0x70))


class PatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        env = {k: os.environ.get(k) for k in ("PE_TEST_IT_PATCH", "PE_TEST_IT_PACK", "PE_TEST_DISC")}
        if not all(v and Path(v).is_file() for v in env.values()):
            raise unittest.SkipTest("needs PE_TEST_IT_PATCH, PE_TEST_IT_PACK and PE_TEST_DISC (USA disc 1)")
        cls.patch, cls.pack_path = Path(env["PE_TEST_IT_PATCH"]), Path(env["PE_TEST_IT_PACK"])
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
        out = Path(self.folder.name) / f"{original.exe_name}.bin"
        if not out.exists():
            patchfile.apply_patch(self.patch, original.disc.f.name, out)
        patched = Game(out)
        self.addCleanup(original.disc.f.close)
        self.addCleanup(patched.disc.f.close)
        return original, patched

    def check_disc(self, cue: str):
        original, patched = self.games(cue)
        same, differ = compare_text(original, patched, self.entries, self.code_of)
        print(f"\n{original.exe_name}: {same} rows the same as the patched game; known differences: "
              f"{', '.join(sorted(differ))}")
        self.assertGreater(same, 2400)
        self.assertEqual(differ, EXPECTED)

        # The letters: the six accented ones drawn the patched game's way, the others the game's.
        pal, width, _, px = font.page(patched, font.PAGE0_ENTRY)
        pal_o, width_o, _, px_o = font.page(original, font.PAGE0_ENTRY)
        exe, exe_o = patched.disc.file(patched.exe_name), original.disc.file(original.exe_name)
        metrics, metrics_o = patchimport.menu_metrics(patched), patchimport.menu_metrics(original)
        # the patch reads one dialogue width a letter from WIDTHS_AT + 1 (the four reads changed)
        self.assertEqual([struct.unpack_from("<I", exe, at)[0] for at in patchimport.WIDTH_READS],
                         [0x0006B821, 0x00001821, 0x00001821, 0x00000000])
        for code, char in ITALIAN.items():
            record = self.records.get(self.code_of.get(char))
            self.assertIsNotNone(record, f"{char} isn't in the pack")
            self.assertEqual(record_pixels(record), list(patchimport._layered(pal, font.cell(px, width, code))), char)
            self.assertEqual(record[5], exe[patchimport.WIDTHS_AT + 1 + code], f"{char}: dialogue width")
            self.assertEqual(record[7], metrics[code], f"{char}: menu metrics")
        for code in range(0x4D):
            self.assertEqual(font.cell(px, width, code), font.cell(px_o, width_o, code), f"code {code:02X} redrawn")
            self.assertEqual(exe[patchimport.WIDTHS_AT + 1 + code], gt.ADVANCE[code], f"code {code:02X} width")
            self.assertEqual(metrics[code], metrics_o[code], f"code {code:02X} menu metrics")
            self.assertNotIn(code, {c for c in self.records if c < 0x4D}, f"code {code:02X}: pack redraws a game letter")

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
