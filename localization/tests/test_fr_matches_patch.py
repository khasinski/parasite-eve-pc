"""The French pack says and draws what the patched PS1 game does, on both USA discs.

The pack is built from the French fan patch for the USA discs (import-patch, build --from-patch
--patch-letters). This test doesn't trust that chain: it applies the real patch to a copy of each
disc and reads the patched game itself, then checks the pack against it.

What the patch does, read from the patched game by hand (2026-11-10), and checked here as it is:
- It keeps the game's letters (0x00-0x4C) and adds its own from 0x4D, each with its own width (the
  dialogue box reads one width a code from 0x81EA1, as the Spanish patch has it): accented letters,
  ( ) and the signs № and ….
- To fit more text in the game's space it draws two letters in one code: "li", "es", "té" ... (44
  codes), and "a ", "e ", "r ", "s " (a letter and a space). Each such picture is exactly its two
  letters drawn one after the other at the first one's width, and the code is as wide as the two
  together, so the pack writes the two letters out and draws the same thing. In the menus the
  patch's spaces are 5 px (its letter-and-space codes, and a space code of its own, 0x8F), not the
  game's 8: the pack draws its space 5 px wide in the menus (the dialogue box keeps its own).
- The rooms' message banks change size, so the two words at the start of each room's data that
  point past the bank move with it; no script changes. Its redrawn picture tiles in the title-card
  rooms pick new colour rows; the pack carries those with the pictures.

So the text is compared with the patched game's pages after writing each pair out, and each
letter and pair with the patched game's own picture, width and menu metrics.

Known differences, each checked by hand (2026-11-10), are listed in EXPECTED with why.

Needs the patch (PE_TEST_FR_PATCH_DIR, the folder with PE_CD1.ppf and PE_CD2.ppf), the pack
(PE_TEST_FR_PACK, the zipped player file) and USA disc 1 (PE_TEST_DISC); PE_TEST_DISC2 adds Disc 2.
Without them only the rule's own tests run and it exits 77. The patched copies (about 750 MB each)
go in a temporary folder and are deleted.

  python -I localization/tests/test_fr_matches_patch.py
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
from extract_text import bank_slots, table_entries  # noqa: E402
from petext import battle, citymap, font, gametext as gt, names, patchfile, patchimport, pictures, validation  # noqa: E402
from petext.disc import Game  # noqa: E402
from test_it_matches_patch import pack_letters, record_pixels  # noqa: E402

# The patch's own letters, read from its letter pictures by eye (2026-11-10).
FRENCH = {0x4D: "é", 0x4E: "è", 0x4F: "ê", 0x50: "ë", 0x51: "à", 0x52: "â", 0x53: "ä", 0x54: "î", 0x55: "ï",
          0x56: "ô", 0x57: "ö", 0x58: "ù", 0x59: "û", 0x5A: "ü", 0x5B: "ç", 0x5E: "(", 0x5F: ")", 0x60: "№",
          0x61: "…", 0x66: "Ç"}
# Its codes that draw two letters, read the same way.
PAIRS = {0x62: "a ", 0x63: "e ", 0x64: "r ", 0x65: "s ", 0x67: "li", 0x68: "il", 0x69: "se", 0x6A: "es",
         0x6B: "le", 0x6C: "el", 0x6D: "ti", 0x6E: "it", 0x6F: "ir", 0x70: "ri", 0x71: "is", 0x72: "si",
         0x73: "ia", 0x74: "ai", 0x75: "ie", 0x76: "ei", 0x77: "la", 0x78: "al", 0x79: "te", 0x7A: "et",
         0x7B: "tr", 0x7C: "so", 0x7D: "os", 0x7E: "lo", 0x7F: "ol", 0x80: "fi", 0x81: "if", 0x82: "oi",
         0x83: "st", 0x84: "ts", 0x87: "té", 0x88: "ét", 0x89: "lé", 0x8A: "él", 0x8B: "ci", 0x8C: "ic",
         0x8D: "to", 0x8E: "ot"}
MENU_SPACE = 0x8F                        # the patch's own space, empty, 5 px in the menus
# Pictures the patch draws that no row uses (in the English these codes are kana, which only the
# kana grids sys/115 and 116 show, and the USA game never opens them): æ, œ, "Il", and "?!" (a
# pixel wider than ? and ! together), by their look.
UNUSED = (0x5C, 0x5D, 0x85, 0x86)
# A letter as the computer writes it, where a translator's tool put it instead of the patch's code:
# UTF-8 é, and Latin-1 é. The patched game draws those codes as empty cells.
TYPED = {b"\xc3\xa9": "é", b"\xe9": "é"}
# The four instructions that read the dialogue box's widths, as the patch has them (read by hand;
# the Italian patch's way): addu s7,zero,a2; move v1,zero (twice); nop. One width a code.
WIDTH_READ_WORDS = {0x284E4: 0x0006B821, 0x28540: 0x00001821, 0x28FB8: 0x00001821, 0x29074: 0x00000000}
PATCH_FILES = {"SLUS_006.62": "PE_CD1.ppf", "SLUS_006.68": "PE_CD2.ppf"}
SPACE = gt.CODE_OF[" "]

EXPECTED = {
    # Unused "dummy" rows (no menu shows them): the patch writes "…" as UTF-8 there (E2 80 A6), and
    # 80 is one of its pairs, so they can't be read as text. They keep the English "dummy".
    "misc/035": "keeps the English",
    "misc/036": "keeps the English",
    "misc/037": "keeps the English",
    "misc/038": "keeps the English",
    "misc/039": "keeps the English",
    # "Hé, nous avons..." and "Eve décoiffée", "Affamé ?": the patch has é as the computer writes it
    # (TYPED), which the PS1 draws as an empty cell; the pack has the é the translator meant.
    "dlg/M041/198": "the letter the translator typed",
    "dlg/M347/030": "the letter the translator typed",
    "dlg/M347/031": "the letter the translator typed",
    # A boy's line in Central Park: its third line, "maman que je voulais rentrer à la maison.", runs
    # past the screen's right edge as the patch wrote it. The build wraps only such pages (past the
    # screen edge, as for Spanish); the same words in the same order.
    "dlg/M057/041": "re-wrapped",
}


def letters_of(code: int) -> str | None:
    """What a code of the patched game draws, as text."""
    if code == MENU_SPACE:
        return " "
    return FRENCH.get(code) or PAIRS.get(code) or (gt.CHARS.get(code) if code < 0x4D else None)


def to_pack(raw: bytes, code_of: dict[str, int], typed: bool = False) -> bytes:
    """The patched game's bytes as the pack holds them: each pair written out, each letter on the
    pack's code for it; other commands as they are. `typed`: TYPED read as the letter meant."""
    parts, out, i = list(validation.commands(raw)), bytearray(), 0
    while i < len(parts):
        c = parts[i]
        if typed and len(c) == 1:
            run = c + (parts[i + 1] if i + 1 < len(parts) and len(parts[i + 1]) == 1 else b"")
            hit = next((seq for seq in TYPED if run.startswith(seq)), None)
            if hit:
                out.append(code_of.get(TYPED[hit], gt.CODE_OF.get(TYPED[hit], 0)))
                i += len(hit)
                continue
        if len(c) == 1 and c[0] < 0xF1 and letters_of(c[0]) is not None:
            for char in letters_of(c[0]):
                out.append(code_of[char] if char in code_of else gt.CODE_OF[char])
        else:
            out += c
        i += 1
    return bytes(out)


def words(raw: bytes) -> bytes:
    """The letters and commands of a page without its line breaks, waits and spaces."""
    return bytes(b for c in validation.commands(raw) if c not in (b"\xf7", b"\xf8", bytes([SPACE])) for b in c)


def overlay(cells: list[tuple[list[int], int]]) -> list[int]:
    """Layered cells (0 empty, 1 stroke, 2 shadow) drawn one after another, each from its column, as
    the dialogue box draws letters: a stroke covers the shadow of the letter before it."""
    out = [0] * (font.CELL * font.CELL)
    for cell, dx in cells:
        for i, v in enumerate(cell):
            x, y = i % font.CELL + dx, i // font.CELL
            if v and x < font.CELL and (v == 1 or not out[y * font.CELL + x]):
                out[y * font.CELL + x] = v
    return out


def menu_advance(code: int, metric: int) -> int:
    """How far the menus move on after a code with this metrics byte (src/main/draw/text_render.c
    MenuMeasure): its drawn width, plus 2 px for digits and the space, 1 px for the rest."""
    return (metric >> 4) + (2 if code < 10 or code == SPACE else 1)


def compare_text(original, patched, entries: dict[int, bytes], code_of: dict[str, int], pack: bytes) -> tuple[int, dict]:
    """(rows that match, {key: how it differs}) over the dialogue, the menu rows and battle lines."""
    disc = {"SLUS_006.62": "1", "SLUS_006.68": "2"}[original.exe_name]
    same, differ = 0, {}

    def check(key, english, ps1, menu=False):
        nonlocal same
        got = entries.get(pelang.fnv1a(key))
        if ps1 == english:
            if got is not None:
                differ[key] = "translated where the patch keeps the English"
            return
        if got is None:
            differ[key] = "keeps the English"
            return
        cleaned = patchimport.clean_page(ps1, english)
        want = to_pack(cleaned, code_of)
        got = got[:-1] if got[-1:] == b"\xf9" else got          # the box's end, kept apart on the disc
        if got == want:
            same += 1
        elif got == to_pack(cleaned, code_of, typed=True):
            differ[key] = "the letter the translator typed"
        elif not menu and words(got) == words(want):
            differ[key] = "re-wrapped"
        elif key not in ("sys/115", "sys/116") and set(cleaned) & set(UNUSED):
            differ[key] = "uses a picture this test says no row uses"
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
            if key == "sys/117":
                check_grid(key, before[key], after[key], entries, pack, differ)
                same += key not in differ
            else:
                check(key, before[key], after[key], menu=True)
    exe_before, exe_after = original.disc.file(original.exe_name), patched.disc.file(patched.exe_name)
    lines, why = battle.patched(exe_before, exe_after)
    if why:
        differ["battle/*"] = why
    english = battle.english(exe_before)
    for key, raw in lines.items():
        check(key, english[key], raw)
    return same, differ


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
        out.append(letters_of(raw[i]) or f"{{g{raw[i]:02X}}}")
        i += 1
    while out and out[-1] == " ":
        out.pop()
    return out


def check_grid(key, english, ps1, entries, pack, differ):
    """The name entry grid, cell by cell: the patch's letters, each in one cell."""
    got = entries.get(pelang.fnv1a(key))
    if got is None:
        differ[key] = "keeps the English"
    elif grid_letters(got, pack) != grid_letters(ps1):
        differ[key] = f"grid differs: pack {grid_letters(got, pack)} / patched game {grid_letters(ps1)}"


class RuleTest(unittest.TestCase):
    def test_pairs_are_written_out(self):
        code_of = {"é": 0x60}
        raw = bytes([gt.CODE_OF["P"], 0x6A, 0x79, 0xFB, 0x07, 0x6A, 0x4D, 0x62, MENU_SPACE])
        self.assertEqual(to_pack(raw, code_of), gt.encode("Peste") + bytes([0xFB, 0x07, 0x6A, 0x60]) + gt.encode("a  "))

    def test_a_typed_letter_is_read_only_when_asked(self):
        raw = bytes([gt.CODE_OF["H"], 0xE9, 0xC3, 0xA9])
        self.assertEqual(to_pack(raw, {"é": 0x60}, typed=True), bytes([gt.CODE_OF["H"], 0x60, 0x60]))
        self.assertEqual(to_pack(raw, {"é": 0x60}), raw)

    def test_overlay_draws_the_second_letter_over_the_first_ones_shadow(self):
        a = [0] * 144
        a[0], a[1] = 1, 2
        b = [0] * 144
        b[0] = 1
        self.assertEqual(overlay([(a, 0), (b, 1)])[:2], [1, 1])
        self.assertEqual(overlay([(b, 11), (a, 11)])[11], 1)        # nothing past the cell's edge


class PatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        env = {k: os.environ.get(k) for k in ("PE_TEST_FR_PATCH_DIR", "PE_TEST_FR_PACK", "PE_TEST_DISC")}
        if not (env["PE_TEST_FR_PATCH_DIR"] and all((Path(env["PE_TEST_FR_PATCH_DIR"]) / f).is_file()
                                                     for f in PATCH_FILES.values())
                and all(v and Path(v).is_file() for k, v in env.items() if k != "PE_TEST_FR_PATCH_DIR")):
            raise unittest.SkipTest("needs PE_TEST_FR_PATCH_DIR, PE_TEST_FR_PACK and PE_TEST_DISC (USA disc 1)")
        cls.patches, cls.pack_path = Path(env["PE_TEST_FR_PATCH_DIR"]), Path(env["PE_TEST_FR_PACK"])
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

        # The text.
        same, differ = compare_text(original, patched, self.entries, self.code_of, self.data)
        print(f"\n{original.exe_name}: {same} rows the same as the patched game; known differences: "
              f"{', '.join(sorted(differ))}")
        self.assertGreater(same, 2500)
        for key in sorted(set(differ) | set(EXPECTED)):
            if differ.get(key) != EXPECTED.get(key):
                print(f"  {key}: {differ.get(key)} (expected {EXPECTED.get(key)})")
        self.assertEqual(differ, EXPECTED)

        # The widths: the dialogue box reads one width a code from 0x81EA1 (four reads changed).
        for at, word in WIDTH_READ_WORDS.items():
            self.assertEqual(struct.unpack_from("<I", exe, at)[0], word, f"width read at {at:X}")
        widths = {code: exe[patchimport.WIDTHS_AT + 1 + code] for code in range(patchimport.BYTE_WIDTHS)}

        # The letters: the game's are as they were (the pack draws those the game's way, but for the
        # space in the menus), the patch's own are the pack's, drawn the patched game's way.
        pal, width, _, px = font.page(patched, font.PAGE0_ENTRY)
        pal_o, width_o, _, px_o = font.page(original, font.PAGE0_ENTRY)
        metrics, metrics_o = patchimport.menu_metrics(patched), patchimport.menu_metrics(original)
        cell = lambda code: patchimport._layered(pal, font.cell(px, width, code))
        for code in range(0x4D):
            self.assertEqual(cell(code), patchimport._layered(pal_o, font.cell(px_o, width_o, code)), f"{code:02X}")
            self.assertEqual(widths[code], gt.ADVANCE[code], f"{code:02X}: dialogue width")
            self.assertEqual(metrics[code], metrics_o[code], f"{code:02X}: menu metrics")
        self.assertEqual(sorted(c for c in self.records if c < 0x4D), [SPACE])
        for code, char in FRENCH.items():
            record = self.records.get(self.code_of.get(char))
            self.assertIsNotNone(record, f"{char} isn't in the pack")
            self.assertEqual(record_pixels(record), cell(code), char)
            self.assertEqual(record[5], widths[code], f"{char}: dialogue width")
            self.assertEqual(record[7], metrics[code], f"{char}: menu metrics")
        drawn = {code: (record_pixels(r), r[5], r[7]) for code, r in self.records.items()}
        pack_menu = lambda char: (menu_advance(SPACE, drawn[SPACE][2]) if char == " " else
                                  menu_advance(self.code_of[char], drawn[self.code_of[char]][2]) if char in self.code_of
                                  else menu_advance(gt.CODE_OF[char], metrics_o[gt.CODE_OF[char]]))
        # The pack's space: empty, the game's dialogue width, the patch's menu space.
        self.assertEqual(drawn[SPACE][:2], ([0] * 144, gt.ADVANCE[SPACE]))
        self.assertEqual(pack_menu(" "), menu_advance(MENU_SPACE, metrics[MENU_SPACE]))
        self.assertFalse(any(font.cell(px, width, MENU_SPACE)))
        box_space = gt.BOX_ADVANCE[SPACE]
        for code, pair in PAIRS.items():
            parts = [gt.CODE_OF[ch] if ch in gt.CODE_OF else next(c for c, v in FRENCH.items() if v == ch) for ch in pair]
            dialogue = [box_space if c == SPACE else widths[c] for c in parts]
            self.assertEqual(cell(code), overlay([(cell(parts[0]), 0), (cell(parts[1]), dialogue[0])]), pair)
            self.assertEqual(widths[code], sum(dialogue), f"{pair!r}: dialogue width")
            self.assertEqual(menu_advance(code, metrics[code]), sum(pack_menu(ch) for ch in pair), f"{pair!r}: menu width")
        named = set(FRENCH) | set(PAIRS) | set(UNUSED)
        for code in range(0x4D, 0xF1):
            self.assertEqual(any(font.cell(px, width, code)), code in named, f"code {code:02X}")
        print(f"{original.exe_name}: {len(FRENCH)} letters and {len(PAIRS)} pairs drawn the patched game's way")

        # The pictures: a piece that lands on this disc holds the patched disc's bytes there, and
        # with the pieces put in, the disc differs from the patched one only in its text and the
        # rooms' pointers past their message banks.
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
        self.assertEqual(set(left) - {"text", "letters page", "room header"}, set())
        self.check_room_headers(original, patched)
        self.check_rewrapped(patched)

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
        """(sector, offset) -> what that byte is: text (room message banks and what follows them,
        the boot pack's text, entry 1's disc messages), the letters page, a room's first two words
        (room header), or a room's scripts."""
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
            spans.append((run, run + 8, "room header"))
            spans.append((run + 8, run + cut, f"M{number:03} script"))
            spans.append((run + cut, run + data * 2048, "text"))

        def owner(sector, offset):
            at = sector * 2048 + offset
            return next((what for lo, hi, what in spans if lo <= at < hi), f"sector {sector}")
        return owner

    def check_room_headers(self, original, patched):
        """A room whose first two words differ: both point past its scripts, and move together."""
        moved = 0
        for number in range(1, 451):
            a, b = original.room(number), patched.room(number)
            if a is None or b is None or a.data[:8] == b.data[:8]:
                continue
            cut = min(bank_slots(a.data).values(), default=len(a.data))
            before, after = struct.unpack_from("<II", a.data), struct.unpack_from("<II", b.data)
            self.assertTrue(all(v >= cut for v in before + after), f"M{number:03}: {before} -> {after}")
            self.assertEqual(before[0] - after[0], before[1] - after[1], f"M{number:03}")
            moved += 1
        print(f"{original.exe_name}: {moved} rooms' pointers past their message bank moved with it")

    def check_rewrapped(self, patched):
        """Each re-wrapped page doesn't fit the English box as the patch wrote it, and does in the pack."""
        from petext import layout
        from test_es_box_fit import pack_widths
        gt.PACK_DRAWN.update(pack_widths(pelang.pack_bytes(self.pack_path)))
        self.addCleanup(gt.PACK_DRAWN.clear)
        strings = json.loads((HERE / "data/manifest.json").read_text(encoding="utf-8"))["entries"]
        catalog = {b["key"]: b for b in json.loads((HERE / "editor/catalog.json").read_text(encoding="utf-8"))["banks"]}
        disc = {"SLUS_006.62": "1", "SLUS_006.68": "2"}[patched.exe_name]
        for key in (k for k, why in EXPECTED.items() if why == "re-wrapped"):
            _, room, page = key.split("/")
            raw = pelang._dialogue_pages(patched, catalog[room]["locations"][disc], patchimport.patched_pages)[int(page)][0]
            width, rows = layout.box_limit(strings[key])
            fits = lambda page: all(max(s) <= width and n <= rows
                                    for s, n in zip(layout.screens(page), layout.drawn_rows(page)))
            self.assertFalse(fits(to_pack(raw, self.code_of)), f"{key} fits as the patch wrote it")
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
