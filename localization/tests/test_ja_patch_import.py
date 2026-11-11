"""Reading a patch made for the Japanese discs (petext/japatch.py, import_ja_patch.py).

The quick checks need nothing. The disc check needs the Japanese Disc 1 (PE_TEST_JA_DISC) and
optionally the USA Disc 1 (PE_TEST_DISC): it makes two patches of its own, an old-style PPF and a
PPF3 on top of it (a fan patch and the fix that goes on after it), which change a letter on a
dialogue page, redraw a letter in an area's font, change a menu line, and change one letter
twice. It then checks that the import finds exactly that, on the right USA keys. Exits 77
(skipped) when the Japanese disc isn't given. Nothing from the disc is kept.
"""
from __future__ import annotations

import hashlib
import io
import json
import os
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
import import_ja_patch  # noqa: E402
from extract_text import bank_slots, table_entries  # noqa: E402
from petext import font, japatch, jatext as ja  # noqa: E402
from petext.disc import Game  # noqa: E402

JA_DISC = os.environ.get("PE_TEST_JA_DISC")
US_DISC = os.environ.get("PE_TEST_DISC")


class FakeFonts(japatch.Fonts):
    """Each cell drawn as its own number, so a letter's picture says which cell it came from."""

    def __init__(self):
        super().__init__(None)

    def cell(self, slot, cell):
        if cell > 400:
            return None
        return [(0, 0, 0, 0)] * 16, tuple([1 if slot == "p0" else 2] + [cell & 15, cell >> 4] + [0] * 141)


def pid(slot, cell):
    return japatch.picture_id(FakeFonts().cell(slot, cell)[1])


class PageText(unittest.TestCase):
    def test_letters_become_pictures_and_commands_stay(self):
        raw = bytes([0x10, 0xF7, 0xFC, 0x05, 0xFB, 0x04, 0x11, 0xFB, 0x05, 0xF8])
        text = japatch.page_text(raw, FakeFonts(), 2)
        self.assertEqual(text, f"{{pic {pid('p0', 0x10)}}}{{br}}{{pic {pid('e45', 0x05 + 0x34)}}}"
                               f"{{red}}{{pic {pid('p0', 0x11)}}}{{/red}}{{wait}}")

    def test_an_area_letter_needs_the_area(self):
        self.assertEqual(japatch.page_text(bytes([0xFD, 0x01]), FakeFonts(), None), f"{{?e?:{0x135}}}")

    def test_a_letter_past_its_page_is_marked(self):
        self.assertEqual(japatch.page_text(bytes([0xFD, 0x90]), FakeFonts(), 3), f"{{?e46:{0x90 + 0x134}}}")

    def test_the_pictures_drawn_are_counted(self):
        seen = {}
        japatch.page_text(bytes([0x10, 0x10, 0xFC, 0x00]), FakeFonts(), 2, seen)
        self.assertEqual(seen[pid("p0", 0x10)]["uses"], 2)
        self.assertEqual(seen[pid("e45", 0x34)]["slots"], {"e45:52"})

    def test_the_id_is_the_japanese_tables(self):
        cell = tuple(range(16)) * 9
        self.assertEqual(japatch.picture_id(cell), hashlib.sha1(bytes(cell)).hexdigest()[:12])

    def test_a_page_takes_the_areas_of_the_rooms_that_open_it(self):
        info = {"rooms": {1: 2, 5: 3}, "opens": {4: {3}}}
        self.assertEqual(japatch.page_areas("k", info, 4), [3])
        self.assertEqual(japatch.page_areas("k", info, 9), [2, 3])


def glyph_offsets(raw: bytes):
    """(offset, prefix or None) of each letter on a page, as jatext.tokens reads it."""
    i = 0
    while i < len(raw):
        c = raw[i]
        if c in (0xFC, 0xFD):
            yield i, c
            i += 2
        elif c == 0xFB:
            s = raw[i + 1]
            if s > 9:
                yield i, 0xFB
            i += 3 if s in (7, 9) else 2
        else:
            if c < 0xF7:
                yield i, None
            i += 1


def page_starts(bank: bytes) -> dict[int, int]:
    """page id -> offset of its first byte in the bank (after FE id)."""
    out, i = {}, 0
    while i + 1 < len(bank) and bank[i] == 0xFE:
        out[bank[i + 1]] = i + 2
        j = i + 2
        while bank[j] not in (0xFF, 0xF9):
            j += 2 if bank[j] in (0xFC, 0xFD) else (3 if bank[j] == 0xFB and bank[j + 1] in (7, 9) else (2 if bank[j] == 0xFB else 1))
        i = j + 1
    return out


def ppf1(records) -> bytes:
    return b"PPF10\x00" + b"test".ljust(50) + b"".join(struct.pack("<IB", o, len(d)) + d for o, d in records)


def ppf3(records) -> bytes:
    head = b"PPF30\x02" + b"test".ljust(50) + bytes([0, 0, 0, 0])
    return head + b"".join(struct.pack("<QB", o, len(d)) + d for o, d in records)


@unittest.skipUnless(JA_DISC, "PE_TEST_JA_DISC not set")
class JapaneseDisc(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.dir = Path(cls.tmp.name)
        g = Game(JA_DISC)
        cls.game = g
        disc = g.disc
        raw_at = lambda lba, o: (lba + o // 2048) * disc.sector_size + disc.data_offset + o % 2048
        room_lba = lambda m: g.pe_img[0] + g.rooms[m - 1][0] + g.rooms[m - 1][1] + g.rooms[m - 1][2]
        room = g.room(1)
        bank_at = bank_slots(room.data)[0]
        bank = room.data[bank_at:]
        starts = page_starts(bank)
        pages = japatch.whole_bank(room)
        fonts = japatch.Fonts(g)
        rtype = ja.room_type(room.data)
        cls.area = f"e{rtype + 0x2B}"
        # Page P: a page with an area letter (FC/FD) and a one-byte letter; page Q: another page
        # with a one-byte letter.
        # The area letter redrawn is the one on page P that the most pages draw.
        users = {}
        for p, (raw, _) in pages.items():
            for o, k in glyph_offsets(raw):
                if k in (0xFC, 0xFD):
                    users.setdefault(raw[o:o + 2], set()).add(p)
        a, b = [], []
        for p, (raw, _) in sorted(pages.items()):
            letters = list(glyph_offsets(raw))
            one = [o for o, k in letters if k is None]
            area = sorted((o for o, k in letters if k in (0xFC, 0xFD)), key=lambda o: -len(users[raw[o:o + 2]]))
            if one and area and not a:
                a = [p, one[0], area[0], raw]
                cls.box_pages = {f"dlg/M001/{n:03d}" for n in users[raw[area[0]:area[0] + 2]]}
            elif one and a and not b and p != a[0]:
                b = [p, one[-1], raw]
        cls.p, one_at, area_at, raw_p = a
        cls.q, q_at, raw_q = b
        changes1, changes2 = [], []
        # 1. Page P: the one-byte letter becomes another letter.
        old = raw_p[one_at]
        cls.p_code = next(c for c in range(1, 0xF0) if c != old and fonts.cell("p0", c)[1] != fonts.cell("p0", old)[1])
        changes1.append((raw_at(room_lba(1), bank_at + starts[cls.p] + one_at), bytes([cls.p_code])))
        cls.p_index = [o for o, _ in glyph_offsets(raw_p)].index(one_at)
        # 2. The area letter on page P gets a new picture: a box drawn in its stroke colour.
        _, cell_no = font.glyph_cell(raw_p[area_at], raw_p[area_at + 1])
        entry = rtype + 0x2B
        blob = g.pe(g.sectors[entry], 1)
        clen = struct.unpack_from("<I", blob, 8)[0]
        pix_at = 8 + clen + 12
        pal, width, _, px = font.page(g, entry)
        stroke = max(range(16), key=lambda i: (pal[i][3] > 0, max(pal[i][:3])))
        x0, y0 = (cell_no % font.PER_ROW) * font.CELL, (cell_no // font.PER_ROW) * font.CELL
        cell = []
        for y in range(font.CELL):
            row = [stroke if (y in (1, 10) or x in (1, 10)) and 1 <= x <= 10 and 1 <= y <= 10 else 0 for x in range(font.CELL)]
            cell += row
            line = bytes(row[x] | (row[x + 1] << 4) for x in range(0, font.CELL, 2))
            changes1.append((raw_at(g.pe_img[0] + g.sectors[entry], pix_at + ((y0 + y) * width + x0) // 2), line))
        cls.box = japatch.picture_id(tuple(cell))
        cls.p_area_index = [o for o, _ in glyph_offsets(raw_p)].index(area_at)
        # 3. A menu line: its first one-byte letter changes.
        pack = g.resident_pack()
        cls.sys_key, sys_raw = next((k, r) for k, r in table_entries(pack)
                                    if k.startswith("sys/") and len(r) >= 6 and pack.count(r) == 1 and r[0] < 0xF0)
        sys_code = next(c for c in range(1, 0xF0) if fonts.cell("p0", c)[1] != fonts.cell("p0", sys_raw[0])[1])
        changes1.append((raw_at(g.pe_img[0] + g.sectors[2], pack.find(sys_raw)), bytes([sys_code])))
        # 4. Page Q: the first patch writes one letter, the fix on top another; the fix wins.
        q_old = raw_q[q_at]
        first, cls.q_code = [c for c in range(1, 0xF0) if c != q_old
                             and fonts.cell("p0", c)[1] != fonts.cell("p0", q_old)[1]][:2]
        q_offset = raw_at(room_lba(1), bank_at + starts[cls.q] + q_at)
        changes1.append((q_offset, bytes([first])))
        changes2.append((q_offset, bytes([cls.q_code])))
        cls.q_index = [o for o, _ in glyph_offsets(raw_q)].index(q_at)
        cls.fonts = fonts
        changes1.sort()
        (cls.dir / "fan.ppf").write_bytes(ppf1(changes1))
        (cls.dir / "fix.ppf").write_bytes(ppf3(changes2))
        cls.out = cls.dir / "out"
        cls.out.mkdir()
        args = [JA_DISC, str(cls.dir / "fan.ppf"), str(cls.dir / "fix.ppf"), "-o", str(cls.out / "draft.json"),
                "--pictures", str(cls.out / "pictures.json"), "--report", str(cls.out / "report.json")]
        if US_DISC:
            args += ["--us", US_DISC]
        with redirect_stdout(io.StringIO()) as cls.said:
            cls.code = import_ja_patch.main(args)
        if cls.code == 0:
            cls.rows = {r["key"]: r for r in json.loads((cls.out / "draft.json").read_text(encoding="utf-8"))["rows"]}
            cls.pictures = json.loads((cls.out / "pictures.json").read_text(encoding="utf-8"))["pictures"]
            cls.report = json.loads((cls.out / "report.json").read_text(encoding="utf-8"))

    @classmethod
    def tearDownClass(cls):
        cls.game.disc.f.close()
        cls.tmp.cleanup()

    def letters(self, key):
        import re
        return re.findall(r"\{pic ([0-9a-f]{12})\}|\{\?[^}]*\}", self.rows[key]["draft"])

    def test_runs(self):
        self.assertEqual(self.code, 0, self.said.getvalue())

    def test_only_its_own_files_are_left(self):
        self.assertEqual(sorted(p.name for p in self.out.iterdir()), ["draft.json", "pictures.json", "report.json"])

    def test_the_changed_letter_is_on_the_usa_key(self):
        row = self.rows[f"dlg/M001/{self.p:03d}"]
        self.assertEqual(self.letters(row["key"])[self.p_index], japatch.picture_id(self.fonts.cell("p0", self.p_code)[1]))
        self.assertEqual(row["area"], 2)

    def test_the_redrawn_area_letter_is_the_new_picture(self):
        key = f"dlg/M001/{self.p:03d}"
        self.assertEqual(self.letters(key)[self.p_area_index], self.box)
        rows = self.pictures[self.box]["rows"]
        self.assertEqual(rows[1], " ########## ")
        self.assertEqual(rows[5], " #        # ")
        self.assertNotIn("japanese", self.pictures[self.box])

    def test_every_page_drawing_the_redrawn_letter_is_in_the_draft(self):
        manifest = json.loads((ROOT / "data/manifest.json").read_text(encoding="utf-8"))["entries"]
        drawn = {k for k, r in self.rows.items() if self.box in r["draft"]}
        self.assertGreater(len(drawn), 1)
        self.assertEqual(drawn, {k for k in self.box_pages if k in manifest})
        edited = {f"dlg/M001/{self.p:03d}", f"dlg/M001/{self.q:03d}", self.sys_key}
        for key in set(self.rows) - edited:
            self.assertIn(self.box, self.rows[key]["draft"], key)

    def test_the_fix_goes_on_after_the_patch(self):
        key = f"dlg/M001/{self.q:03d}"
        self.assertEqual(self.letters(key)[self.q_index], japatch.picture_id(self.fonts.cell("p0", self.q_code)[1]))

    def test_the_menu_line(self):
        self.assertIn(self.sys_key, self.rows)
        self.assertEqual(self.report["table_rows"], 1)

    def test_letters_the_patch_left_alone_keep_their_japanese(self):
        same = [p for p in self.pictures.values() if "japanese" in p]
        self.assertTrue(same)
        expect = japatch.japanese_letters()
        p_pic = japatch.picture_id(self.fonts.cell("p0", self.p_code)[1])
        if p_pic in expect:
            self.assertEqual(self.pictures[p_pic]["japanese"], expect[p_pic])

    def test_every_row_has_a_usa_key(self):
        manifest = json.loads((ROOT / "data/manifest.json").read_text(encoding="utf-8"))["entries"]
        self.assertTrue(all(k in manifest for k in self.rows))
        self.assertTrue(all("{?" not in r["draft"] for r in self.rows.values()))

    def test_the_report(self):
        r = self.report
        self.assertEqual(r["font_pages_redrawn"], [self.area])
        self.assertEqual(r["rooms_with_edited_scripts"], [])
        self.assertEqual(r["rooms_with_edited_pictures"], [])
        self.assertEqual(r["boot_program_bytes_changed"], 0)
        self.assertEqual(r["patches"], ["fan.ppf", "fix.ppf"])
        # Only room 1's copy of the shared bank was patched here; a real patch edits them all.
        sharing = japatch.bank_rooms(self.game, [{"key": "M001", "locations": {"1": 1}}], "1", set())["M001"]["rooms"]
        self.assertEqual(r["rooms_whose_bank_copy_differs"], [f"M{n:03d}" for n in sorted(sharing) if n != 1])
        self.assertTrue(r["rooms_whose_bank_copy_differs"])
        if US_DISC:
            self.assertEqual(r["rooms_not_lined_up_with_usa"], ["M195", "M360"])

    def test_a_usa_disc_is_not_taken_as_the_japanese_one(self):
        if not US_DISC:
            self.skipTest("PE_TEST_DISC not set")
        out = self.dir / "wrong"
        out.mkdir()
        with redirect_stderr(io.StringIO()) as said:
            code = import_ja_patch.main([US_DISC, str(self.dir / "fan.ppf"), "-o", str(out / "d.json"),
                                         "--pictures", str(out / "p.json"), "--report", str(out / "r.json")])
        self.assertEqual(code, 1)
        self.assertIn("not a Japanese disc", said.getvalue())
        self.assertEqual(list(out.iterdir()), [])


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=1).result
    if not result.wasSuccessful():
        sys.exit(1)
    sys.exit(0 if JA_DISC else 77)
