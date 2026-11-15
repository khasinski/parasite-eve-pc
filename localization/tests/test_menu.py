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
from petext import gametext as gt, latin, menu, stretch  # noqa: E402

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

    def build(self, translations: dict, *extra: str) -> int:
        path = self.dir / "t.csv"
        rows = pelang.template_rows(pelang.load_strings(self.strings))
        for r in rows:
            r["translation"] = translations.get(r["key"], "")
        pelang.write_csv(path, rows)
        return pelang.main(["build", str(path), "--strings", str(self.strings), "--lang", "xx",
                            "--name", "Test", "-o", str(self.dir / "t.pelang"), *extra])

    def built(self, key: str) -> bytes | None:
        """`key`'s bytes in the last build's pack (stretches expanded), or None."""
        return pelang.read_pack(self.dir / "t.pelang")[2].get(pelang.fnv1a(key))

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

    def test_narrowing_measures_as_the_renderer_makes_it(self):
        line = gt.encode("W" * 28)                                       # 308 px
        self.assertEqual(menu.width(menu.narrow(line, 240), METRICS), (308 * 240 + 128) >> 8)
        self.assertEqual(menu.narrow(menu.narrow(line, 240), 250), menu.narrow(line, 240))  # never wider
        self.assertEqual(menu.narrow(line, 256), line)
        self.assertEqual(menu.fit(308, 306), 254)                     # 255 rounds to 307
        self.assertEqual(menu.fit(400, 306), 196)
        self.assertIsNone(menu.fit(420, 306))                            # past menu.NARROW_FLOOR
        self.assertEqual(menu.room("desc/000", self.layouts), 306)

    def test_a_patch_menu_line_too_wide_is_drawn_narrower(self):
        self.assertEqual(self.build({"desc/000": "W" * 28}), 1)          # a translator shortens it
        self.assertEqual(self.build({"desc/000": "W" * 28}, "--from-patch"), 0)
        raw = self.built("desc/000")
        self.assertTrue(raw.startswith(stretch.NARROW))
        self.assertEqual(menu.strip_narrow(raw)[0], gt.encode("W" * 28))
        self.assertLessEqual(menu.width(raw, METRICS), 306)

    def test_a_patch_line_too_wide_even_narrowed_keeps_the_games_text(self):
        self.assertEqual(self.build({"desc/000": "W" * 40}, "--from-patch"), 0)   # 440 px: 70%
        self.assertIsNone(self.built("desc/000"))

    def memory_card_rows(self):
        rows = STRINGS + [row("sys/071", "Memory Card in Slot 1"), row("sys/072", "Memory Card in Slot 2"),
                          row("sys/073", " is not formatted"), row("sys/086", "Delete it from the card manager")]
        self.strings.write_text(json.dumps(rows), encoding="utf-8")

    def test_joined_patch_text_is_drawn_narrower_together(self):
        self.memory_card_rows()
        # sys/072 stays English, so the message box can't take sys/073 on a line of its own
        text = {"sys/071": "W" * 20, "sys/073": "W" * 9}                     # 220 + 99 px > 300
        self.assertEqual(self.build(text, "--from-patch"), 0)
        a, b = self.built("sys/071"), self.built("sys/073")
        self.assertIsNotNone(a)
        self.assertIsNotNone(b)
        self.assertLessEqual(menu.width(a, METRICS) + menu.width(b, METRICS), 300)
        self.assertEqual(menu.strip_narrow(a)[0], gt.encode("W" * 20))
        self.assertNotIn(stretch.LINE_BREAK, a)

    def test_joined_patch_text_goes_on_two_lines_in_a_message_box(self):
        self.memory_card_rows()
        text = {"sys/071": "W" * 20, "sys/072": "Memory", "sys/073": "W" * 9}    # 220 + 99 px > 300
        self.assertEqual(self.build(text, "--from-patch"), 0)
        a, b, c = self.built("sys/071"), self.built("sys/072"), self.built("sys/073")
        self.assertEqual(a, gt.encode("W" * 20) + stretch.LINE_BREAK)        # full size, then a new line
        self.assertEqual(b, gt.encode("Memory") + stretch.LINE_BREAK)
        self.assertEqual(c, gt.encode("W" * 9))
        self.assertEqual(menu.width(a, METRICS), 220)                         # the break is no width

    def test_a_patch_message_box_line_too_wide_goes_on_two_lines(self):
        self.memory_card_rows()
        text = "WWWWW WWWWW WWWWW WWWWW WWWWW WW"                               # 330 px on one line
        self.assertEqual(self.build({"sys/086": text}, "--from-patch"), 0)
        raw = self.built("sys/086")
        self.assertEqual(raw, gt.encode("WWWWW WWWWW WWWWW") + stretch.LINE_BREAK + gt.encode("WWWWW WWWWW WW"))
        self.assertFalse(raw.startswith(stretch.NARROW))                       # letters keep their size
        self.assertLessEqual(menu.width(raw, METRICS), 300)

    def test_a_message_box_line_with_no_space_to_split_is_drawn_narrower(self):
        self.memory_card_rows()
        self.assertEqual(self.build({"sys/086": "W" * 28}, "--from-patch"), 0)  # 308 px, one word
        raw = self.built("sys/086")
        self.assertTrue(raw.startswith(stretch.NARROW))
        self.assertNotIn(stretch.LINE_BREAK, raw)

    def test_split_takes_the_space_that_leaves_the_wider_line_narrowest(self):
        line = gt.encode("WW W WWWW WW")                                       # 20 6 40 20, spaces 6
        two = menu.split(line, METRICS, 300)
        self.assertEqual(two, gt.encode("WW W") + stretch.LINE_BREAK + gt.encode("WWWW WW"))
        self.assertEqual(menu.width(two, METRICS), menu.width(gt.encode("WWWW WW"), METRICS))
        self.assertIsNone(menu.split(gt.encode("WWWW WWWW"), METRICS, 30))   # no half fits 30 px
        self.assertIsNone(menu.split(gt.encode("WWWWWW"), METRICS, 300))     # no space at all
        self.assertTrue(menu.splits("sys/086", self.layouts))                 # a message box line
        self.assertFalse(menu.splits("desc/000", self.layouts))               # the help line: one line
        self.assertFalse(menu.splits("sys/073", self.layouts))                # a confirm label too

    def test_template_says_where_menu_text_goes(self):
        rows = {r["key"]: r for r in pelang.template_rows(pelang.load_strings(self.strings))}
        self.assertEqual(rows["desc/000"]["box"], "help line: 294 px")
        self.assertIn("31 bytes", rows["sys/016"]["box"])
        self.assertEqual(rows["item/000"]["widest_px"], self.rows["item/000"]["menu_px"])


if __name__ == "__main__":
    unittest.main()
