"""petext/menu.py: menu widths and the limits of data/menu_layouts.json, with synthetic glyph
metrics (no game data needed).

  python -I -m unittest discover -s localization/tests
"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pelang  # noqa: E402
from petext import gametext as gt, latin, menu  # noqa: E402

# Every glyph 6 px wide (0x60); "W" (0x26) 10 px.
METRICS = bytearray([0x60] * 444)
METRICS[gt.CODE_OF["W"]] = 0xA0
METRICS = bytes(METRICS)


def row(key, text, **extra):
    return {"key": key, "text": text, "raw": gt.encode(text).hex(" "), "lines_px": [0], "boxes": [],
            "rooms": [], "unused": False, "menu_px": menu.width(gt.encode(text), METRICS), **extra}


STRINGS = [row("item/000", "Ammo +6", item_kind=10), row("item/001", "Handgun", item_kind=1),
           row("desc/000", "Restores HP"), row("sys/016", " used"), row("sys/017", " got"),
           row("sys/030", "Aya"), row("sys/117", "ABCDE"), row("sys/120", "Other"),
           row("misc/131", "Central Park")]


class MenuTest(unittest.TestCase):
    def setUp(self):
        self.layouts = menu.load_layouts()
        self.dir = Path(tempfile.mkdtemp())
        self.strings = self.dir / "strings.json"
        self.strings.write_text(json.dumps(STRINGS), encoding="utf-8")
        (self.dir / "menu_glyphs.json").write_text(json.dumps({"metrics": METRICS.hex()}), encoding="utf-8")
        (self.dir / "banks.json").write_text("[]", encoding="utf-8")
        self.rows = {r["key"]: r for r in STRINGS}

    def check(self, key, text):
        return menu.check(key, gt.encode(text, extended=True), self.rows[key], METRICS, self.layouts)

    def build(self, translations: dict) -> int:
        path = self.dir / "t.csv"
        rows = pelang.template_rows(pelang.load_strings(self.strings))
        for r in rows:
            r["translation"] = translations.get(r["key"], "")
        pelang.write_csv(path, rows)
        return pelang.main(["build", str(path), "--strings", str(self.strings), "--lang", "xx",
                            "--name", "Test", "-o", str(self.dir / "t.pelang")])

    def test_width_is_draw_measure_text_width(self):
        self.assertEqual(menu.width(gt.encode("AB"), METRICS), 2 * (6 + 1))
        self.assertEqual(menu.width(gt.encode("1 W"), METRICS), (6 + 2) + (6 + 2) + (10 + 1))
        self.assertEqual(menu.width(bytes([0xFB, 0x00]), METRICS), 12)          # page 1 code 0: a sprite
        self.assertEqual(menu.width(gt.encode("AB") + b"\xffAB", METRICS), 14)  # stops at the end

    def test_latin_letters_measure_as_pe_lang_sets_them(self):
        e_acute = gt.encode("é", extended=True)
        self.assertEqual(menu.width(e_acute, METRICS), menu.width(gt.encode("e"), METRICS))
        g = latin.BY_CODE[e_acute[0]]
        self.assertEqual(menu.latin_metric(g.code, METRICS), METRICS[gt.CODE_OF[g.base]])

    def test_help_line_warns_past_the_panel_and_fails_at_the_screen_edge(self):
        self.assertEqual(self.check("desc/000", "Heals"), ([], [], True))
        errors, warnings, _ = self.check("desc/000", "W" * 27)       # 297 px
        self.assertFalse(errors)
        self.assertIn("help line", warnings[0])
        self.assertIn("screen edge", self.check("desc/000", "W" * 28)[0][0])  # 308 px

    def test_count_column_only_for_kinds_that_have_one(self):
        name = "W" * 8 + "A"                                            # 95 px: past 86, within 104
        self.assertTrue(self.check("item/001", name)[1])               # kind 1 draws a count
        self.assertEqual(self.check("item/000", name)[1], [])          # kind 10 doesn't
        self.assertTrue(self.check("item/000", "W" * 10)[1])           # 110 px, past the cell

    def test_clipped_text_past_its_room_is_an_error(self):
        self.assertEqual(self.check("misc/131", "W" * 12)[:2], ([], []))   # 132 px of 138
        self.assertIn("save file location", self.check("misc/131", "W" * 13)[0][0])

    def test_rename_grid_keeps_its_cells(self):
        self.assertEqual(self.check("sys/117", "ÉBCDE")[0], [])
        self.assertIn("glyph cells", self.check("sys/117", "ABCD")[0][0])

    def test_keys_without_a_context_fall_back(self):
        self.assertFalse(self.check("sys/120", "Anything")[2])

    def test_joined_text_must_fit_its_buffer(self):
        self.assertEqual(self.build({"item/000": "Munitions +6"}), 0)
        self.assertNotEqual(self.build({"item/000": "A" * 24, "sys/016": " was used up"}), 0)
        self.assertEqual(self.build({"item/000": "A" * 18, "sys/016": " was used"}), 0)   # 18 + 9 + 1

    def test_template_says_where_menu_text_goes(self):
        rows = {r["key"]: r for r in pelang.template_rows(pelang.load_strings(self.strings))}
        self.assertEqual(rows["desc/000"]["box"], "help line: 294 px")
        self.assertIn("31 bytes", rows["sys/016"]["box"])
        self.assertEqual(rows["item/000"]["widest_px"], self.rows["item/000"]["menu_px"])


if __name__ == "__main__":
    unittest.main()
