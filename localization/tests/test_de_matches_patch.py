"""The German pack says and draws what the patched PS1 game does, on both USA discs.

The pack is built from the German fan patch for the USA discs (import-patch, build --from-patch
--patch-letters). This test doesn't trust that chain: it applies the real patch to a copy of each
disc and reads the patched game itself, then checks the pack against it.

What the patch does, read from the patched game by hand (2026-11-10), and checked here as it is:
- It redraws every letter (0x00-0x4C) and adds eight (0x4D-0x54: ä ö ü Ä Ö Ü ß and an up arrow),
  each with its own width. The dialogue box's gap-and-width table moves into the resident pack, at
  0x800AAD80 (the four reads now say lui at,0x800B; ... -0x5280(at) / -0x527F(at)).
- It writes two letters with one code: the box's read of its next byte (boot program 0x28370)
  jumps to the patch's routine at 0x800AAE94, which hands the box, for a code 0x90-0xDF, the two
  letters at 0x800AAEFC + 3 * (code - 0x90) (each pair ends with F0). Only the dialogue box reads
  them: its pages and the battle lines; the menus don't.
- It teaches the menu text printer a line break (two calls at 0x8005F2EC and 0x8005F630): the next
  line 12 px lower at the same left edge. The pack carries such a break as its own line break,
  which the port draws the same way (Draw_TextToEnd).
- Wayne's menu box in M048's script is 200 px wide, not 187, and M293's box 272 x 110, not 204 x 82.
  A pack can't change scripts; these are the only script changes (checked below).
- Its redrawn picture tiles in the title-card rooms pick new colour rows; the pack carries those
  with the pictures.

So the text is compared with the patched game's pages after writing each pair out, and each
letter with the patched game's own picture, width and menu metrics.

Known differences, each checked by hand (2026-11-10), are listed in EXPECTED with why.

Needs the patch (PE_TEST_DE_PATCH_DIR, the folder with PE_GER100B_D1.xdelta and _D2.xdelta), the
pack (PE_TEST_DE_PACK, the zipped player file) and USA disc 1 (PE_TEST_DISC); PE_TEST_DISC2 adds
Disc 2. Without them only the rule's own tests run and it exits 77. The patched copies (500-650 MB
each) go in a temporary folder and are deleted.

  python -I localization/tests/test_de_matches_patch.py
"""
import json
import os
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pelang  # noqa: E402
from extract_text import table_entries  # noqa: E402
from petext import battle, citymap, font, gametext as gt, names, patchfile, patchimport, pictures, validation  # noqa: E402
from petext.disc import Game  # noqa: E402
from petext.stretch import LINE_BREAK  # noqa: E402
from test_it_matches_patch import pack_letters, record_pixels  # noqa: E402

# The patch's added letters, read from its letter pictures by eye (2026-11-10).
GERMAN = {0x4D: "ä", 0x4E: "ö", 0x4F: "ü", 0x50: "Ä", 0x51: "Ö", 0x52: "Ü", 0x53: "ß", 0x54: "↑"}
PATCH_FILES = {"SLUS_006.62": "PE_GER100B_D1.xdelta", "SLUS_006.68": "PE_GER100B_D2.xdelta"}

RESIDENT_AT = 0x800A8028                 # where the game loads the resident pack
READ_AT, HOOK = 0x28370, 0x0802AB90      # the box's read of its next byte: j 0x800AAE40
PAIRS_AT, PAIR_CODES = 0x800AAEFC, range(0x90, 0xE0)
WIDTHS = 0x800AAD80                      # the moved gap-and-width table
WIDTH_READ_WORDS = {0x28538: 0x3C01800B, 0x28540: 0x9023AD80, 0x28600: 0x3C01800B, 0x28608: 0x9024AD81,
                    0x28FB0: 0x3C01800B, 0x28FB8: 0x9023AD80, 0x29078: 0x3C01800B, 0x29080: 0x9023AD81}

EXPECTED = {
    # A test page in the debug room M035: its English holds a stray page end (FE) the tools can't
    # read, and the patch empties it. It keeps the English.
    "dlg/M035/112": "keeps the English",
    # "Map jump point / Jump / Forget it", a debug question in the museum's bank: the patch writes
    # its own note there ("30520-110") without the choice, which the scripts act on, so the page
    # can't go in a pack. No room script that the tools can read opens it (not all of the bank's
    # rooms decode to the end); Spanish and Italian translate it.
    "dlg/M191/109": "keeps the English",
    # The Japanese kana grids: the USA game always opens the Latin one, sys/117 (Menu_OpenRenameScreen;
    # g_SaveMetadataWindowIndex is only ever 1). The patch writes "DUMMY" in both.
    "sys/115": "keeps the English",
    "sys/116": "keeps the English",
    # The lift's help in M293 (bank M290): the patch makes that room's box 272 x 110 in its script
    # (from 204 x 82), and its 8 lines of up to 251 px fill it. A pack can't change scripts, so in
    # the English box the build spreads the same words over two screens that fit it.
    "dlg/M290/009": "re-wrapped",
    # "Memory Card in Steckplatz 1/2" + " ist nicht formatiert" is past the screen in one line; the
    # port puts the notice on the box's next line (menu_layouts.json "memory card notice", the
    # split-lines rule): a line break at the end.
    "sys/071": "a line break at the end",
    "sys/072": "a line break at the end",
    # A message box line past the screen at full size: the port puts it on two lines (split-lines).
    "sys/086": "split onto two lines",
    # A save file's location name, 149 px where the panel clips at 138: drawn at 92% width.
    "misc/142": "drawn narrower",
}


def pairs_of(pack: bytes) -> dict[int, bytes]:
    """code -> its two letters, from the patch's table in the resident pack (each ends with F0)."""
    out = {}
    for code in PAIR_CODES:
        at = PAIRS_AT - RESIDENT_AT + 3 * (code - 0x90)
        entry = pack[at:at + 3]
        if entry[2] != 0xF0:
            raise ValueError(f"pair {code:02X} doesn't end with F0: {entry.hex(' ')}")
        out[code] = entry[:2]
    return out


def to_pack(raw: bytes, code_of: dict[str, int], pairs: dict[int, bytes], menu: bool = False) -> bytes:
    """The patched game's bytes as the pack holds them: each pair written out (the dialogue box's
    text only), each letter on the pack's code for it, a menu line break as the pack's own; other
    commands as they are."""
    out = bytearray()
    for c in validation.commands(raw):
        if len(c) == 1 and c[0] < 0xF1:
            for b in (c if menu else pairs.get(c[0], c)):
                char = GERMAN.get(b) if b >= 0x4D else gt.CHARS.get(b)
                out.append(code_of.get(char, b))
        elif menu and c == b"\xf7":
            out += LINE_BREAK
        else:
            out += c
    return bytes(out)


def written_out(raw: bytes, pairs: dict[int, bytes]) -> bytes:
    """A dialogue page in the game's own codes with each pair written out: many pages the patch
    leaves in English are re-written with pairs, and read the same as the English then."""
    return b"".join(pairs.get(c[0], c) if len(c) == 1 else c for c in validation.commands(raw))


def grid_letters(raw: bytes, pack: bytes | None = None) -> list[str]:
    """A name entry grid cell by cell: a pack's two-byte name letters read back with the pack's
    own list (petext/names.py), the patched game's codes through the patch's letters."""
    numbers = {n: ch for ch, n in names.read_numbers(pack).items()} if pack else {}
    out, i = [], 0
    while i < len(raw):
        if numbers and 0xFB <= raw[i] <= 0xFE:
            out.append(numbers.get((raw[i] - 0xFB) * 250 + raw[i + 1], "?"))
            i += 2
            continue
        b = raw[i]
        out.append((GERMAN.get(b) if b >= 0x4D else gt.CHARS.get(b)) or f"{{g{b:02X}}}")
        i += 1
    while out and out[-1] == " ":
        out.pop()
    return out


def words(raw: bytes) -> bytes:
    """The letters and commands of a page without its line breaks, waits and spaces."""
    return bytes(b for c in validation.commands(raw) if c not in (b"\xf7", b"\xf8", bytes([gt.CODE_OF[" "]])) for b in c)


def compare_text(original, patched, entries: dict[int, bytes], code_of: dict[str, int], pack: bytes) -> tuple[int, dict, int]:
    """(rows that match, {key: how it differs}, menu rows with the patch's line break) over the
    dialogue, the menu rows and the battle lines."""
    disc = {"SLUS_006.62": "1", "SLUS_006.68": "2"}[original.exe_name]
    pairs = pairs_of(patched.resident_pack())
    same, differ, breaks, symbols = 0, {}, 0, set()

    def check(key, english, ps1, menu=False):
        nonlocal same, breaks
        got = entries.get(pelang.fnv1a(key))
        if not menu:
            ps1 = written_out(ps1, pairs)
        if key == "sys/117" and got is not None:
            if grid_letters(got, pack) == grid_letters(ps1):
                same += 1
            else:
                differ[key] = f"grid differs: pack {grid_letters(got, pack)} / patched game {grid_letters(ps1)}"
            return
        if ps1 == english:
            if got is not None:
                differ[key] = "translated where the patch keeps the English"
            return
        if got is None:
            differ[key] = "keeps the English"
            return
        want = to_pack(patchimport.clean_page(ps1, english), code_of, {}, menu)
        got = got[:-1] if got[-1:] == b"\xf9" else got          # the box's end, kept apart on the disc
        if key != "sys/117" and {0x0C, 0x0D} & set(want):
            symbols.add(key)
        if got == want:
            same += 1
            breaks += menu and LINE_BREAK in want
        elif got == want + LINE_BREAK:
            differ[key] = "a line break at the end"
        elif menu and LINE_BREAK in got and got.replace(LINE_BREAK, bytes([gt.CODE_OF[" "]])) == want:
            differ[key] = "split onto two lines"
        elif menu and got[:2] == b"\xf1\xc8" and want in got:
            differ[key] = "drawn narrower"
        elif not menu and words(got) == words(want):
            differ[key] = "re-wrapped"
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
            check(key, before[key], after[key], menu=True)
    # The battle lines: the patch writes each over the English one in its slot (petext/battle.py).
    exe_before, exe_after = original.disc.file(original.exe_name), patched.disc.file(patched.exe_name)
    english = battle.english(exe_before)
    for key, (at, size, _) in battle.LINES.items():
        line = exe_after[at:exe_after.index(b"\xff", at, at + size)]
        check(key, english[key], line)
    if symbols:
        differ["symbols"] = f"a redrawn game symbol in {sorted(symbols)}"
    return same, differ, breaks


class RuleTest(unittest.TestCase):
    def test_pairs_are_written_out_in_dialogue_only(self):
        pairs = {0x91: bytes([gt.CODE_OF["e"], gt.CODE_OF["n"]])}
        raw = bytes([gt.CODE_OF["G"], 0x91, 0xFB, 0x07, 0x91, 0x4F])     # a wait's operand is no pair
        self.assertEqual(to_pack(raw, {"ü": 0x60}, pairs), bytes([gt.CODE_OF["G"], gt.CODE_OF["e"], gt.CODE_OF["n"],
                                                                  0xFB, 0x07, 0x91, 0x60]))
        self.assertEqual(to_pack(raw, {"ü": 0x60}, pairs, menu=True)[:2], bytes([gt.CODE_OF["G"], 0x91]))

    def test_a_menu_line_break_is_the_packs(self):
        raw = gt.encode("Ab{br}cd")
        self.assertEqual(to_pack(raw, {}, {}, menu=True), gt.encode("Ab") + LINE_BREAK + gt.encode("cd"))

    def test_a_pair_table_entry_ends_with_f0(self):
        pack = bytearray(0x3200)
        for code in PAIR_CODES:
            at = PAIRS_AT - RESIDENT_AT + 3 * (code - 0x90)
            pack[at:at + 3] = bytes([1, 2, 0xF0])
        self.assertEqual(pairs_of(bytes(pack))[0xDF], bytes([1, 2]))
        pack[PAIRS_AT - RESIDENT_AT + 2] = 0
        with self.assertRaises(ValueError):
            pairs_of(bytes(pack))


class PatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        env = {k: os.environ.get(k) for k in ("PE_TEST_DE_PATCH_DIR", "PE_TEST_DE_PACK", "PE_TEST_DISC")}
        if not (env["PE_TEST_DE_PATCH_DIR"] and all((Path(env["PE_TEST_DE_PATCH_DIR"]) / f).is_file()
                                                     for f in PATCH_FILES.values())
                and all(v and Path(v).is_file() for k, v in env.items() if k != "PE_TEST_DE_PATCH_DIR")):
            raise unittest.SkipTest("needs PE_TEST_DE_PATCH_DIR, PE_TEST_DE_PACK and PE_TEST_DISC (USA disc 1)")
        cls.patches, cls.pack_path = Path(env["PE_TEST_DE_PATCH_DIR"]), Path(env["PE_TEST_DE_PACK"])
        cls.data = data = pelang.pack_bytes(cls.pack_path)
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
        exe, exe_o = patched.disc.file(patched.exe_name), original.disc.file(original.exe_name)
        word = lambda data, at: struct.unpack_from("<I", data, at)[0]
        resident = patched.resident_pack()

        # What the patch does to the boot program, as read by hand.
        self.assertEqual(word(exe, READ_AT), HOOK)
        self.assertEqual({at: word(exe, at) for at in WIDTH_READ_WORDS}, WIDTH_READ_WORDS)

        # The text.
        same, differ, breaks = compare_text(original, patched, self.entries, self.code_of, self.data)
        print(f"\n{original.exe_name}: {same} rows the same as the patched game ({breaks} menu rows with "
              f"the patch's line break); known differences: {', '.join(sorted(differ))}")
        self.assertGreater(same, 2500)
        self.assertGreaterEqual(breaks, 36)
        for key in sorted(set(differ) | set(EXPECTED)):
            if differ.get(key) != EXPECTED.get(key):
                print(f"  {key}: {differ.get(key)} (expected {EXPECTED.get(key)})")
        self.assertEqual(differ, EXPECTED)
        self.check_rewrapped(patched)

        # The letters: every one the patch draws differently is the pack's, drawn the patched game's way.
        pal, width, _, px = font.page(patched, font.PAGE0_ENTRY)
        pal_o, width_o, _, px_o = font.page(original, font.PAGE0_ENTRY)
        metrics, metrics_o = patchimport.menu_metrics(patched), patchimport.menu_metrics(original)
        table = resident[WIDTHS - RESIDENT_AT:WIDTHS - RESIDENT_AT + 2 * 0x55]
        drawn, symbols = 0, []
        for code in range(0x55):
            char = GERMAN.get(code) or gt.CHARS.get(code)
            if char is None:
                # A game symbol, not a letter: the patch redraws "=" (0C) and the star (0D) too. A
                # pack holds letters only, so the port draws the game's own; the German text never
                # uses "=", and the star only as one cell of the name grid (sys/117, as in English).
                symbols.append(code)
                continue
            advance = table[2 * code + 1] - table[2 * code]
            cell = font.cell(px, width, code)
            if (code < 0x4D and cell == font.cell(px_o, width_o, code) and advance == gt.ADVANCE[code]
                    and metrics[code] == metrics_o[code]):
                continue
            record = self.records.get(self.code_of.get(char))
            self.assertIsNotNone(record, f"{char} isn't in the pack")
            self.assertEqual(record_pixels(record), list(patchimport._layered(pal, cell)), char)
            self.assertEqual(record[5], advance, f"{char}: dialogue width")
            self.assertEqual(record[7], metrics[code], f"{char}: menu metrics")
            drawn += 1
        print(f"{original.exe_name}: {drawn} letters drawn the patched game's way")
        self.assertGreaterEqual(drawn, 80)
        self.assertEqual(symbols, [0x0C, 0x0D])
        for code in range(0x55, 0xF1):
            self.assertFalse(any(font.cell(px, width, code)), f"code {code:02X} has a picture")

        # The pictures: a piece that lands on this disc holds the patched disc's bytes there, and
        # with the pieces put in, the disc differs from the patched one only in its text and in the
        # two scripts' boxes.
        by_sector: dict[int, list] = {}
        for piece in self.pieces:
            by_sector.setdefault(piece.sector, []).append(piece)
        landed, left = 0, {}
        owners = self.owners(original)
        total, step = original.pe_img[1] // 2048, 512
        for first in range(0, total, step):
            count = min(step, total - first)
            big_a, big_b = original.pe(first, count), patched.pe(first, count)
            for k in range(count):
                a, b = big_a[k * 2048:(k + 1) * 2048], big_b[k * 2048:(k + 1) * 2048]
                if a != b or first + k in by_sector:
                    landed += self.put_in(first + k, a, b, by_sector, left, owners)
        print(f"{original.exe_name}: {landed} of {len(self.pieces)} picture pieces land, each the patched disc's "
              f"bytes; left: {sorted(left)}")
        self.assertEqual(landed, len(self.pieces))
        self.assertEqual({k: v for k, v in left.items() if k not in ("text", "letters page")},
                         {"M048 script": 1, "M293 script": 3})

    def put_in(self, sector, a, b, by_sector, left, owners) -> int:
        """Put this sector's pieces in the English bytes `a`, check each against the patched `b`,
        and count by owner what still differs. Returns the pieces that landed."""
        landed, put = 0, bytearray(a)
        for piece in by_sector.get(sector, []):
            english = a[piece.offset:piece.offset + len(piece.data)]
            if pictures.zlib.crc32(english) != piece.original_crc:
                continue
            landed += 1
            put[piece.offset:piece.offset + len(piece.data)] = piece.data
            self.assertEqual(piece.data, b[piece.offset:piece.offset + len(piece.data)],
                             f"picture piece at sector {sector} + {piece.offset}")
        for i in range(2048):
            if put[i] != b[i]:
                what = owners(sector, i)
                left[what] = left.get(what, 0) + 1
        return landed

    def owners(self, game):
        """(sector, offset) -> what that byte is: text (room message banks, the boot pack's text,
        entry 1's disc messages), the letters page, or a room's scripts."""
        s, spans = game.sectors, []
        for entry in (1, 2):
            spans.append((s[entry] * 2048, s[entry + 1] * 2048, "text"))
        for entry in (10, 72):                      # the letters page at VRAM 320,0
            spans.append((s[entry] * 2048, s[entry + 1] * 2048, "letters page"))
        # the city map's banks (compare_text checks the rest of its entry is the game's)
        spans.append((s[citymap.ENTRY] * 2048, s[citymap.ENTRY + 1] * 2048, "text"))
        for number, (first, scratch, texture, data) in enumerate(game.rooms, 1):
            if not data:
                continue
            run = (first + scratch + texture) * 2048
            cut = pictures._first_bank(game.room(number).data)
            spans.append((run, run + cut, f"M{number:03} script"))
            spans.append((run + cut, run + data * 2048, "text"))

        def owner(sector, offset):
            at = sector * 2048 + offset
            return next((what for lo, hi, what in spans if lo <= at < hi), f"sector {sector}")
        return owner

    def check_rewrapped(self, patched):
        """Each re-wrapped page doesn't fit the English box as the patch wrote it, and does in the pack."""
        from petext import layout
        from test_es_box_fit import pack_widths
        gt.PACK_DRAWN.update(pack_widths(pelang.pack_bytes(self.pack_path)))
        self.addCleanup(gt.PACK_DRAWN.clear)
        strings = json.loads((HERE / "data/manifest.json").read_text(encoding="utf-8"))["entries"]
        catalog = {b["key"]: b for b in json.loads((HERE / "editor/catalog.json").read_text(encoding="utf-8"))["banks"]}
        disc = {"SLUS_006.62": "1", "SLUS_006.68": "2"}[patched.exe_name]
        pairs = pairs_of(patched.resident_pack())
        for key in (k for k, why in EXPECTED.items() if why == "re-wrapped"):
            _, room, page = key.split("/")
            raw = pelang._dialogue_pages(patched, catalog[room]["locations"][disc], patchimport.patched_pages)[int(page)][0]
            width, rows = layout.box_limit(strings[key])
            fits = lambda page: all(max(s) <= width and n <= rows
                                    for s, n in zip(layout.screens(page), layout.drawn_rows(page)))
            self.assertFalse(fits(to_pack(written_out(raw, pairs), self.code_of, {})), f"{key} fits as the patch wrote it")
            self.assertTrue(fits(self.entries[pelang.fnv1a(key)]), f"{key} doesn't fit in the pack")

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
