"""pelang.py: templates, the pseudo language and the compiler, on a small synthetic strings.json
(no game data needed).

  python -I -m unittest discover -s localization/tests
"""
import csv
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pelang  # noqa: E402
from petext import gametext as gt, latin, layout, letters, validation  # noqa: E402

STRINGS = [
    {"key": "item/000", "text": "Ammo +6", "lines_px": [44], "boxes": [], "rooms": [], "unused": False},
    {"key": "item/054", "text": "Chrysler Key1", "lines_px": [80], "boxes": [], "rooms": [], "unused": False},
    {"key": "dlg/M001/002", "text": "Drop an item here?{br}{choice 2}    Yes{br}    No",
     "lines_px": [110, 40, 40], "boxes": ["default"], "rooms": ["M001"], "unused": False,
     "japanese": "手持ちのアイテムをここに置きますか?"},
    {"key": "dlg/M001/003", "text": "Bye.", "lines_px": [24], "boxes": ["default"], "rooms": ["M001"],
     "unused": False, "end": "F9"},
    {"key": "misc/100", "text": "filler", "lines_px": [30], "boxes": [], "rooms": [], "unused": True},
]
BANKS = [{"key": "M001", "crc32": "1a2b3c4d", "size": 120, "rooms": ["M001", "M002"]}]


class PelangTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.strings = self.dir / "strings.json"
        self.strings.write_text(json.dumps(STRINGS), encoding="utf-8")
        (self.dir / "banks.json").write_text(json.dumps(BANKS), encoding="utf-8")

    def csv_with(self, translations: dict) -> Path:
        path = self.dir / "t.csv"
        rows = pelang.template_rows(pelang.load_strings(self.strings))
        for r in rows:
            r["translation"] = translations.get(r["key"], "")
        pelang.write_csv(path, rows)
        return path

    def build(self, translations: dict, extra=()) -> tuple[int, Path]:
        out = self.dir / "t.pelang"
        code = pelang.main(["build", str(self.csv_with(translations)), "--strings", str(self.strings),
                            "--lang", "xx", "--name", "Test", "-o", str(out), *extra])
        return code, out

    def test_shipped_french_dialogue_fits_long_names_in_known_boxes(self):
        root = Path(pelang.__file__).resolve().parent
        csv_path = root / "translations/fr.csv"
        manifest = root / "data/manifest.json"
        out = self.dir / "french-preview.pelang"
        self.assertEqual(pelang.main([
            "build", str(csv_path), "--submission", "--strings", str(manifest),
            "--mod-strings", str(root / "data/mod_strings.json"),
            "--lang", "fr", "--name", "French", "--max-messages", "0", "-o", str(out),
        ]), 0)
        _, _, entries = pelang.read_pack(out)
        sources = pelang.srcman.load(manifest)[0]
        name_width = layout.name_width(gt.encode("WWWWWWWW"))
        checked = 0
        for row in pelang.read_csv(csv_path):
            key = row["key"]
            if not key.startswith("dlg/") or not row.get("translation"):
                continue
            raw = entries[pelang.fnv1a(key)]
            if raw.endswith(b"\xf9"):
                raw = raw[:-1]
            if b"\xfa" not in list(validation.commands(raw)):
                continue
            limit = layout.box_limit(sources[key])
            # These unbound pages were also reported by the default-box audit.
            # Testing that box does not establish every possible styled layout.
            if limit is None and key in ("dlg/M044/053", "dlg/M057/023"):
                limit = layout.bounds("default")
            if limit is None:
                continue
            checked += 1
            with self.subTest(key=key):
                for screen in layout.screens(raw, name_width):
                    self.assertLessEqual(max(screen), limit[0])
                    self.assertLessEqual(len(screen), limit[1])
        self.assertGreater(checked, 500)

    def test_the_manifest_builds_the_same_pack_without_any_game_text(self):
        manifest = self.dir / "manifest.json"
        self.assertEqual(pelang.main(["manifest", str(self.strings), "-o", str(manifest)]), 0)
        body = manifest.read_text(encoding="utf-8")
        for r in STRINGS:                                    # no source text in it
            self.assertNotIn(r["text"], body)
        self.assertNotIn("Ammo", body)
        csv_path = self.csv_with({"item/000": "Balles +6", "dlg/M001/003": "Salut."})
        packs = []
        for source in (self.strings, manifest):
            out = self.dir / f"{source.stem}.pelang"
            self.assertEqual(pelang.main(["build", str(csv_path), "--strings", str(source), "--lang", "xx",
                                          "--name", "Test", "-o", str(out)]), 0)
            packs.append(out.read_bytes())
        self.assertEqual(packs[0], packs[1])
        bad = self.csv_with({"dlg/M001/003": "Salut.{wait}"})   # controls are still checked
        self.assertEqual(pelang.main(["build", str(bad), "--strings", str(manifest), "--lang", "xx",
                                      "--name", "Test", "-o", str(self.dir / "bad.pelang")]), 1)

    def test_export_and_submissions_carry_no_source_text(self):
        work = self.csv_with({"item/000": "Balles +6"})
        sub = self.dir / "xx.csv"
        self.assertEqual(pelang.main(["export", str(work), "-o", str(sub)]), 0)
        with open(sub, encoding="utf-8") as f:
            rows = list(csv.DictReader(f))
        self.assertEqual(list(rows[0]), pelang.SUBMISSION_COLUMNS)
        self.assertEqual([(r["key"], r["translation"]) for r in rows], [("item/000", "Balles +6")])
        args = ["--strings", str(self.strings), "--lang", "xx", "--name", "Test", "-o", str(self.dir / "s.pelang"),
                "--submission"]
        self.assertEqual(pelang.main(["build", str(sub)] + args), 0)
        self.assertEqual(pelang.main(["build", str(work)] + args), 1)      # the working file has the English

    def test_template_gives_each_page_its_scene(self):
        rows = {r["key"]: r for r in pelang.template_rows(pelang.load_strings(self.strings))}
        self.assertEqual(rows["dlg/M001/003"]["before"], STRINGS[2]["text"])
        self.assertEqual(rows["dlg/M001/002"]["after"], "Bye.")
        self.assertEqual(pelang.speaker("{name}: Hi"), "Aya")
        self.assertEqual(pelang.speaker("Daniel: Aya!"), "Daniel")
        self.assertEqual(pelang.speaker("Hello there"), "")

    def test_consistency_warnings(self):
        strings = {"item/054": {"text": "Chrysler Key1"},
                   "dlg/M001/003": {"text": "You found the Chrysler Key1."}}
        texts = {"item/054": "Clé Chrysler1", "dlg/M001/003": "Tu as trouvé la clé."}
        self.assertTrue(any("item/054" in w for w in pelang.consistency(texts, strings, {})))
        texts["dlg/M001/003"] = "Tu as trouvé la Clé Chrysler1."
        self.assertEqual(pelang.consistency(texts, strings, {}), [])
        self.assertTrue(pelang.consistency(texts, strings, {"found": "découvert"}))
        out = self.dir / "u.pelang"
        same = self.csv_with({"item/054": "Chrysler Key1"})                 # submitted untranslated
        code = pelang.main(["build", str(same), "--strings", str(self.strings), "--lang", "xx", "--name", "T", "-o", str(out)])
        self.assertEqual(code, 0)

    def test_advances_are_the_games_table(self):
        # gametext.ADVANCE once lost an entry and gave every code from 10 on its neighbour's width.
        import re
        src = (Path(__file__).resolve().parents[2] / "src" / "main" / "text" / "tables.c").read_text(encoding="utf-8")
        body = src[src.index("g_TextboxGlyphSpacing[78] = {"):]
        body = body[:body.index("};")]
        self.assertEqual(gt.ADVANCE, [int(r) for _, r in re.findall(r"\{(\d+),(\d+)\}", body)])

    def test_fnv1a_matches_the_c_reader(self):
        # Reference values of 32-bit FNV-1a; mods/pe_lang/pe_lang_pack.c uses the same function.
        self.assertEqual(pelang.fnv1a(""), 0x811C9DC5)
        self.assertEqual(pelang.fnv1a("a"), 0xE40C292C)
        self.assertEqual(pelang.fnv1a("item/054"), pelang.fnv1a("item/054"))

    def test_template_lists_used_keys_with_both_languages(self):
        path = self.dir / "tpl.csv"
        pelang.main(["template", str(self.strings), "-o", str(path)])
        with open(path, encoding="utf-8") as f:
            rows = list(csv.DictReader(f))
        self.assertEqual([r["key"] for r in rows], ["dlg/M001/002", "dlg/M001/003", "item/000", "item/054"])
        self.assertTrue(rows[0]["japanese"].startswith("手持ち"))
        self.assertEqual(rows[0]["box"], "default")

    def test_template_merge_keeps_translations(self):
        old = self.csv_with({"item/000": "Balles +6"})
        path = self.dir / "new.csv"
        pelang.main(["template", str(self.strings), "-o", str(path), "--merge", str(old)])
        with open(path, encoding="utf-8") as f:
            rows = {r["key"]: r for r in csv.DictReader(f)}
        self.assertEqual(rows["item/000"]["translation"], "Balles +6")

    def test_pack_round_trip(self):
        code, out = self.build({"item/000": "Balles +6", "dlg/M001/002": "Laisser un objet?{br}{choice 2}    Oui{br}    Non"})
        self.assertEqual(code, 0)
        lang, name, entries = pelang.read_pack(out)
        self.assertEqual((lang, name), ("xx", "Test"))
        self.assertEqual(entries[pelang.fnv1a("item/000")], gt.encode("Balles +6"))
        self.assertEqual(entries[pelang.fnv1a("bank/1a2b3c4d")], b"M001")   # the bank names itself
        self.assertEqual(len(entries), 3)

    def test_bundle_zips_the_pack_for_players(self):
        import zipfile
        code, out = self.build({"item/000": "Balles +6"})
        self.assertEqual(code, 0)
        readme = self.dir / "credits.txt"
        readme.write_text("Traduit par quelqu'un.", encoding="utf-8")
        player = self.dir / "French.pelang"
        self.assertEqual(pelang.main(["bundle", str(out), "-o", str(player), "--readme", str(readme)]), 0)
        with zipfile.ZipFile(player) as z:
            self.assertEqual(sorted(z.namelist()), ["readme.txt", "text.bin"])
            self.assertEqual(z.read("text.bin"), out.read_bytes())   # the pack itself, unchanged
        self.assertEqual(pelang.read_pack(player), pelang.read_pack(out))
        first = player.read_bytes()
        self.assertEqual(pelang.main(["bundle", str(out), "-o", str(player), "--readme", str(readme)]), 0)
        self.assertEqual(player.read_bytes(), first)                 # the same pack, the same file
        # A zipped file can be zipped again: the pack inside is what goes in.
        again = self.dir / "again.pelang"
        self.assertEqual(pelang.main(["bundle", str(player), "-o", str(again)]), 0)
        with zipfile.ZipFile(again) as z:
            self.assertEqual(z.namelist(), ["text.bin"])
            self.assertEqual(z.read("text.bin"), out.read_bytes())
        # Not a pack: refused, and nothing written.
        junk = self.dir / "junk.pelang"
        junk.write_bytes(b"not a pack")
        self.assertEqual(pelang.main(["bundle", str(junk), "-o", str(self.dir / "no.pelang")]), 1)
        self.assertFalse((self.dir / "no.pelang").exists())
        with zipfile.ZipFile(junk, "w") as z:
            z.writestr("readme.txt", "no pack here")
        self.assertEqual(pelang.main(["bundle", str(junk), "-o", str(self.dir / "no.pelang")]), 1)

    def test_dialogue_keeps_its_page_end(self):
        code, out = self.build({"dlg/M001/003": "Salut."})
        self.assertEqual(code, 0)
        _, _, entries = pelang.read_pack(out)
        self.assertEqual(entries[pelang.fnv1a("dlg/M001/003")], gt.encode("Salut.") + b"\xf9")

    def test_chinese_font_pack_keeps_choices_and_punctuation(self):
        font = os.getenv("PE_TEST_CJK_FONT")
        if not font:
            self.skipTest("Set PE_TEST_CJK_FONT to test actual Chinese font rasterization")
        translations = {
            "item/000": "弹药",
            "dlg/M001/002": "要丢弃物品吗？{br}{choice 2}    是{br}    否",
            "dlg/M001/003": "你好……再见！",
        }
        code, out = self.build(translations, ["--cjk-font", font])
        self.assertEqual(code, 0)
        _, _, entries = pelang.read_pack(out)
        raw = entries[pelang.fnv1a("dlg/M001/003")]
        self.assertIn(b"\xf1\xc5", raw)
        self.assertTrue(raw.endswith(b"\xf9"))
        choices = entries[pelang.fnv1a("dlg/M001/002")]
        self.assertIn(b"\xfb\x09\x02", choices)
        self.assertEqual(int.from_bytes(out.read_bytes()[4:8], "little"), 4)  # stretches: version 4

    def test_shipped_chinese_preview_fits_eight_character_player_names(self):
        font = os.getenv("PE_TEST_CJK_FONT")
        if not font:
            self.skipTest("Set PE_TEST_CJK_FONT to check the shipped Chinese preview")
        root = Path(pelang.__file__).resolve().parent
        csv_path = root / "pilots/zh-Hans/pilot.csv"
        out = self.dir / "chinese-preview.pelang"
        self.assertEqual(pelang.main([
            "build", str(csv_path), "--submission", "--strings", str(root / "data/manifest.json"),
            "--lang", "zh-Hans", "--name", "Chinese preview", "--cjk-font", font,
            "--max-messages", "0", "-o", str(out),
        ]), 0)
        _, _, entries = pelang.read_pack(out)
        name_width = layout.name_width(gt.encode("WWWWWWWW"))
        self.assertEqual(name_width, 88)
        for row in pelang.read_csv(csv_path):
            if not row["key"].startswith("dlg/") or not row.get("translation"):
                continue
            raw = entries[pelang.fnv1a(row["key"])]
            if raw.endswith(b"\xf9"):
                raw = raw[:-1]
            with self.subTest(key=row["key"]):
                # Default dialogue starts at x=20 and has 296 pixels for text.
                # Font dictionary indices must not hide a long substituted name.
                for screen in layout.screens(raw, name_width):
                    self.assertLessEqual(max(screen), 296)

    def test_build_refuses_missing_glyphs_and_changed_choices(self):
        code, _ = self.build({"item/000": "日本語"})              # CJK: not in the game font yet
        self.assertEqual(code, 1)
        code, _ = self.build({"item/000": "Munitions é"})          # Latin Extended: fine
        self.assertEqual(code, 0)
        code, _ = self.build({"dlg/M001/002": "Laisser un objet?{br}    Oui{br}    Non"})
        self.assertEqual(code, 1)
        path = self.csv_with({})
        with open(path, "a", newline="", encoding="utf-8") as f:
            csv.writer(f).writerow(["dlg/M999/000", "", "", "Bonjour", "", "", "", ""])
        code = pelang.main(["build", str(path), "--strings", str(self.strings), "--lang", "xx",
                            "--name", "Test", "-o", str(self.dir / "u.pelang")])
        self.assertEqual(code, 1)

    def test_build_refuses_a_long_line(self):
        long = "A" + "W" * 50 + "{br}{choice 2}  Y{br}  N"        # one word wider than the box: no wrap helps
        raw, errors, warnings = pelang.check_row("dlg/M001/002", long, STRINGS[2], {"item": 80})
        self.assertTrue(any("dialogue box" in e for e in errors))
        code, out = self.build({"dlg/M001/002": long})
        self.assertEqual(code, 1)
        self.assertFalse(out.exists())

    def test_pseudo_keeps_tags_and_grows(self):
        text = "Drop an item here?{br}{choice 2}    Yes{br}    No"
        p = pelang.pseudo(text)
        self.assertEqual(validation.structure(gt.encode(p, extended=True)), validation.structure(gt.encode(text)))
        self.assertGreater(len(p), len(text))
        self.assertIn("Ÿ", pelang.pseudo("yes"))        # case swapped, then accented
        gt.encode(p, extended=True)    # only glyphs the font has, with the Latin Extended set

    def test_raw_tags_cannot_bypass_control_validation(self):
        for text in ("Hello{xFB}", "Hello{xFF}More", "Hello{end2}More", "Hello{fb 07}",
                     "Hello{sym 9}", "Hello{fb FF}", "Hello{gF1}", "Hello{fc}",
                     "Hello{choice 0}", "Hello{choice 8}", "Hello{choice 999}",
                     "Hello{choice}", "Hello{fb 08}", "Hello{xFA}"):
            with self.subTest(text=text):
                raw, errors, _ = pelang.check_row("dlg/M001/003", text, STRINGS[3], {})
                self.assertIsNone(raw)
                self.assertTrue(errors)

    def test_control_comparison_uses_bytes_not_tag_spellings(self):
        source = {"text": "A{wait}B{num}{name}{pause}{fb07 02}", "boxes": []}
        equivalent = "C{xF8}D{fb 08}{xFA}{fb 06}{fb07 02}"
        self.assertFalse(pelang.check_row("dlg/M001/003", equivalent, source, {})[1])
        for text in ("A{num}{wait}B{name}{pause}{fb07 02}",
                     "A{wait}B{num}{name}{pause}{fb07 03}",
                     "A B{num}{name}{pause}{fb07 02}"):
            self.assertTrue(pelang.check_row("dlg/M001/003", text, source, {})[1])

    def test_height_custom_rectangles_and_unknown_layouts(self):
        def check_row(key, text, source, widest):         # the checks themselves, without the wrapper
            return pelang.check_row(key, text, source, widest, wrap=False)
        source = {"text": "Hello", "boxes": ["default"]}
        self.assertTrue(check_row("dlg/M001/003", "{br}".join(["Hi"] * 30), source, {})[1])
        self.assertFalse(check_row("dlg/M001/003", "{br}".join(["Hi"] * 4), source, {})[1])
        for box in ("styled 20,174,80,24", "20,174,80,24"):
            source["boxes"] = [box]
            self.assertTrue(check_row("dlg/M001/003", "W" * 100, source, {})[1])   # off screen
            self.assertTrue(check_row("dlg/M001/003", "{br}".join(["Hi"] * 6), source, {})[1])
            _, errors, warnings = check_row("dlg/M001/003", "Hi{br}Hi", source, {})
            self.assertFalse(errors)                     # past the rectangle, still on screen
            self.assertIn("past the 1 lines", warnings[0])
            _, errors, warnings = check_row("dlg/M001/003", "W" * 14, source, {})
            self.assertFalse(errors)
            self.assertIn("past the 80 px", warnings[0])
            self.assertEqual(check_row("dlg/M001/003", "Hi", source, {})[1:], ([], []))
        source["boxes"] = ["styled (rectangle computed)"]
        self.assertIn("unknown", check_row("dlg/M001/003", "Hi", source, {})[2][0])
        self.assertEqual(layout.bounds("styled 300,220,80,40"), (20, 1))

    def test_pages_without_a_box_are_checked_against_the_boxes_their_bank_opens(self):
        source = {"text": "Hello", "boxes": [], "boxes_possible": ["default"], "boxes_complete": True}
        self.assertEqual(pelang.check_row("dlg/M001/003", "Hi", source, {})[1:], ([], []))
        warn = pelang.check_row("dlg/M001/003", "W" * 100, source, {})[2]
        self.assertIn("fits none", warn[0])
        source["boxes_possible"] = ["default", "styled 20,20,80,40"]
        warn = pelang.check_row("dlg/M001/003", "W" * 20, source, {})[2]
        self.assertIn("too big for styled 20,20,80,40", warn[0])
        source["boxes_complete"] = False
        self.assertIn("did not decode", pelang.check_row("dlg/M001/003", "Hi", source, {})[2][0])
        source["boxes_possible"] = []
        self.assertIn("unknown", pelang.check_row("dlg/M001/003", "Hi", source, {})[2][0])

    def test_unbound_page_is_not_repaginated_for_an_unrelated_bank_box(self):
        # A bank also has a one-line status box; this page may use its normal
        # dialogue box. Do not turn four ordinary lines into four button waits.
        text = "Wayne: La cle de la reserve...{br} Si tu la retrouves,{br} prends ce qu'il te faut.{br} Tu peux te servir."
        source = {"text": "Wayne: Storage key.", "boxes": [],
                  "boxes_possible": ["default", "styled 150,160,134,22"],
                  "boxes_complete": True}
        raw, errors, warnings = pelang.check_row("dlg/M044/040", text, source, {})
        self.assertFalse(errors)
        self.assertEqual(raw, gt.encode(text, extended=True))
        self.assertNotIn(b"\xf8", raw)
        self.assertTrue(any("no script ties" in w for w in warnings))
        self.assertFalse(any("wrapped" in w for w in warnings))

    def test_unbound_page_can_wrap_when_all_proven_bank_boxes_have_one_size(self):
        text = " ".join(["word"] * 40)
        source = {"text": "Hi", "boxes": [], "boxes_possible": ["default"],
                  "boxes_complete": True}
        raw, errors, warnings = pelang.check_row("dlg/M001/003", text, source, {})
        self.assertFalse(errors)
        self.assertIn(b"\xf7", raw)
        self.assertTrue(any("wrapped" in w for w in warnings))
        # An unknown rectangle or an incomplete scan cannot prove a wrap size.
        for possible, complete in ((["default", "styled (rectangle computed)"], True),
                                   (["default"], False)):
            source.update(boxes_possible=possible, boxes_complete=complete)
            raw, errors, _ = pelang.check_row("dlg/M001/003", text, source, {})
            self.assertFalse(errors)
            self.assertEqual(raw, gt.encode(text, extended=True))

    def test_french_alarm_comment_has_explicit_breaks_for_all_known_bank_boxes(self):
        root = Path(__file__).resolve().parent.parent
        with (root / "translations/fr.csv").open(encoding="utf-8", newline="") as f:
            text = next(r["translation"] for r in csv.DictReader(f)
                        if r["key"] == "dlg/M192/058")
        strings, _, _ = pelang.srcman.load(root / "data/manifest.json")
        source = strings["dlg/M192/058"]
        raw, errors, _ = pelang.check_row("dlg/M192/058", text, source, {})
        self.assertFalse(errors)
        self.assertEqual(raw, gt.encode(text, extended=True))
        self.assertEqual(raw.count(b"\xfa"), 1)
        self.assertEqual(len(layout.screens(raw)), 1)
        for box in source["boxes_possible"]:
            self.assertTrue(layout.fits(layout.screens(raw), box), box)

    def test_overflowing_pages_are_wrapped_at_spaces(self):
        source = {"text": "Bye.", "raw": gt.encode("Hi{br} there").hex(" "), "boxes": ["default"]}
        long = " ".join(["word"] * 30)                                   # one 30-word line
        raw, errors, warnings = pelang.check_row("dlg/M001/003", long, source, {})
        self.assertFalse(errors)
        self.assertIn("wrapped", warnings[0])
        lines = gt.decode(raw).split("{br}")
        self.assertGreater(len(lines), 1)
        self.assertTrue(all(line.startswith(" word") for line in lines[1:]))   # the English indents
        self.assertTrue(all(w <= layout.DEFAULT_WIDTH for w in layout.screens(raw)[0]))
        _, errors, _ = pelang.check_row("dlg/M001/003", long, source, {}, wrap=False)
        self.assertTrue(errors)

    def test_fitting_pages_keep_their_breaks(self):
        source = {"text": "Bye.", "boxes": ["default"]}
        raw, errors, warnings = pelang.check_row("dlg/M001/003", "Hello{br}there", source, {})
        self.assertEqual((gt.decode(raw), errors, warnings), ("Hello{br}there", [], []))

    def test_tall_screens_get_more_screens_but_choice_screens_never_split(self):
        source = {"text": "Bye.", "boxes": ["default"]}
        tall = "{br}".join(["Line"] * 9)
        raw, errors, warnings = pelang.check_row("dlg/M001/003", tall, source, {})
        self.assertFalse(errors)
        self.assertEqual([len(s) for s in layout.screens(raw)], [4, 4, 1])
        self.assertIn("2 screen(s) added", warnings[0])
        choice = {"text": "Q{br}{choice 3}  A{br}  B{br}  C", "boxes": ["default"]}
        text = "Q one{br}Q two{br}{choice 3}  A{br}  B{br}  C"                   # 5 lines with the rows
        raw, errors, _ = pelang.check_row("dlg/M001/003", text, choice, {})
        self.assertTrue(errors)                                           # reported, not split
        self.assertEqual(gt.decode(raw), text)

    def test_waits_reset_height_and_placeholders_have_worst_case_widths(self):
        source = {"text": "Hi{wait}Hi{name}{num}", "boxes": ["default"]}
        text = "{br}".join(["Hi"] * 4) + "{wait}Hi{name}{num}"
        self.assertFalse(pelang.check_row("dlg/M001/003", text, source, {})[1])
        self.assertEqual(layout.screens(gt.encode("{name}{num}")), [[layout.NAME_WIDTH + 60]])
        self.assertEqual(gt.lines(gt.encode("{name}{num}")), [layout.NAME_WIDTH + 60])
        source["boxes"] = ["styled 20,20,80,40"]
        self.assertTrue(pelang.check_row("dlg/M001/003", "Hi{wait}Hi{name}{num}", source, {})[2])

    def test_name_is_measured_with_the_packs_default_name(self):
        source = {"text": "{name}", "boxes": ["default"]}
        self.assertEqual(layout.NAME_WIDTH, layout.name_width(gt.encode("Aya")))
        w = gt.BOX_ADVANCE[gt.CODE_OF["W"]]
        text = "{name}" + "W" * ((layout.DEFAULT_WIDTH - layout.NAME_WIDTH) // w)   # fits with "Aya" only
        self.assertFalse(pelang.check_row("dlg/M001/003", text, source, {}, wrap=False)[1])
        wide = layout.name_width(gt.encode("WWWWWWWW"))
        self.assertTrue(pelang.check_row("dlg/M001/003", text, source, {}, wide, wrap=False)[1])
        self.assertEqual(layout.name_width(gt.encode("WWWWWWWWWW")), wide)   # the game copies 8

    def test_overflow_override_never_bypasses_controls(self):
        path = self.csv_with({"dlg/M001/003": "W" * 100})
        out = self.dir / "overflow.pelang"
        args = ["build", str(path), "--strings", str(self.strings), "--lang", "xx", "--name", "Test",
                "-o", str(out), "--allow-overflow"]
        self.assertEqual(pelang.main(args), 0)
        out.unlink()
        self.csv_with({"dlg/M001/003": "Hello{xFB}"})
        self.assertEqual(pelang.main(args), 1)
        self.assertFalse(out.exists())

    def test_a_patchs_line_keeps_its_breaks_while_it_stays_on_screen(self):
        # The default box's fog is the screen's width: past its 297 px a line is lost only at the
        # screen edge, 300 px from x 20. A patch's line there stays as written, as the patched
        # game drew it; the pack tool's own pages, and lines past the edge, are still wrapped.
        on = "WWWWWWWWW WWWWWWWWW WWWWWWWii"                    # 299 px
        off = on + "i"                                          # 304 px
        self.assertEqual([max(map(max, layout.screens(gt.encode(t)))) for t in (on, off)], [299, 304])
        self.assertEqual(layout.bounds("default", screen=True), (300, layout.DEFAULT_LINES))
        source = STRINGS[3]
        raw, errors, warnings = pelang.check_row("dlg/M001/003", on, source, {}, from_patch=True)
        self.assertEqual((raw, errors), (gt.encode(on), []))
        self.assertTrue(any("past the 297 px of box default" in w for w in warnings), warnings)
        raw, errors, warnings = pelang.check_row("dlg/M001/003", on, source, {})
        self.assertNotEqual(raw, gt.encode(on))
        self.assertIn("wrapped to fit the box", warnings[0])
        raw, errors, warnings = pelang.check_row("dlg/M001/003", off, source, {}, from_patch=True)
        self.assertNotEqual(raw, gt.encode(off))
        self.assertEqual(errors, [])
        self.assertIn("wrapped to fit the box", warnings[0])
        self.assertTrue(all(max(s) <= layout.DEFAULT_WIDTH for s in layout.screens(raw)))

    def pictures_file(self, drawn: dict) -> Path:
        """A letter pictures file as import-patch --patch-letters writes it, from
        letter -> (dialogue advance, menu metrics, the columns its strokes fill)."""
        def rows(columns):
            return ["".join("#" if x in columns and 2 <= y <= 9 else " " for x in range(12)) for y in range(12)]
        path = self.dir / "letters.json"
        path.write_text(json.dumps({"letters": [
            {"letter": ch, "code": "00", "dialogue": d, "menu": m, "picture": rows(c)} for ch, (d, m, c) in drawn.items()
        ]}), encoding="utf-8")
        return path

    def test_a_patchs_letters_lay_its_text_out_with_its_widths(self):
        self.addCleanup(gt.PACK_DRAWN.clear)
        # 30 W are 330 px in the game's letters, past the 297 px box, and 180 px in the patch's
        path = self.csv_with({"dlg/M001/003": "0" + "W" * 30 + "é"})
        out = self.dir / "drawn.pelang"
        args = ["build", str(path), "--strings", str(self.strings), "--lang", "xx", "--name", "Test",
                "-o", str(out), "--no-wrap"]
        self.assertEqual(pelang.main(args), 1)
        pictures = self.pictures_file({"W": (6, 0x50, {1, 2, 3, 4}), "0": (5, 0x40, {1, 2}), "é": (6, 0x50, {2, 3}),
                                       "ñ": (6, 0x50, {1}), " ": (12, 0x60, set())})
        self.assertEqual(pelang.main(args + ["--patch-letters", str(pictures)]), 0)
        data = out.read_bytes()
        at, count = struct.unpack_from("<II", data, 80)
        records = [data[at + k * 80:at + (k + 1) * 80] for k in range(count)]
        codes = [r[4] for r in records]
        self.assertEqual(codes, sorted(codes))
        self.assertTrue(all(letters.coded(k, r) for k, r in enumerate(records)))  # '0' first: code 0
        by = {chr(struct.unpack_from("<I", r)[0]): r for r in records}
        self.assertEqual(set(by), {"0", "W", "é", " "})       # not ñ: a fixed letter the text doesn't use
        self.assertEqual(tuple(by["W"][4:8]), (gt.CODE_OF["W"], 6, letters.NO_BASE, 0x50))
        self.assertEqual(by["W"][8 + 2 * 12:8 + 3 * 12], bytes([0x10, 0x11, 0x01, 0, 0, 0]) * 2)
        self.assertEqual(by["é"][4], latin.CODE_OF["é"])
        raw = pelang.read_pack(out)[2][pelang.fnv1a("dlg/M001/003")]
        self.assertEqual(raw, b"\x00" + bytes([gt.CODE_OF["W"]]) * 30 + bytes([latin.CODE_OF["é"], 0xF9]))
        bad = self.dir / "bad.json"
        bad.write_text(json.dumps({"letters": [{"letter": "W", "dialogue": 6, "menu": 0, "picture": ["#"]}]}))
        self.assertEqual(pelang.main(args + ["--patch-letters", str(bad)]), 1)

    def test_a_patch_symbol_no_catalog_draws_becomes_one_of_the_packs_letters(self):
        self.addCleanup(gt.PACK_DRAWN.clear)
        self.addCleanup(gt.PACK_LETTERS.clear)
        # the wrench a patch draws for "Tool" (import-patch --letters 98=U+1F527): item text keeps
        # one byte a letter, so it takes a code of the pack's table, with the patch's picture and widths
        path = self.csv_with({"item/000": "🔧+6"})
        out = self.dir / "symbol.pelang"
        args = ["build", str(path), "--strings", str(self.strings), "--lang", "xx", "--name", "Test", "-o", str(out)]
        self.assertEqual(pelang.main(args), 1)                      # no picture: no letter
        pictures = self.pictures_file({"🔧": (11, 0xA0, {1, 2, 3, 4, 5, 6, 7, 8})})
        self.assertEqual(pelang.main(args + ["--patch-letters", str(pictures)]), 0)
        data = out.read_bytes()
        at, count = struct.unpack_from("<II", data, 80)
        self.assertEqual(count, 1)
        record = data[at:at + 80]
        self.assertEqual(struct.unpack_from("<I", record)[0], 0x1F527)
        self.assertTrue(0x4D <= record[4] <= 0xEF)
        self.assertEqual(tuple(record[5:8]), (11, letters.NO_BASE, 0xA0))
        raw = pelang.read_pack(out)[2][pelang.fnv1a("item/000")]
        self.assertEqual(raw, bytes([record[4]]) + gt.encode("+6"))

    def test_a_row_can_say_how_its_page_ends(self):
        # dlg/M001/003 closes by itself in the game (F9); a patch made it wait for a button (FF)
        code, out = self.build({"dlg/M001/003": "Adiós.{end FF}", "dlg/M001/002":
                                "¿Dejar?{br}{choice 2}    Sí{br}    No{end F9}"})
        self.assertEqual(code, 0)
        entries = pelang.read_pack(out)[2]
        self.assertFalse(entries[pelang.fnv1a("dlg/M001/003")].endswith(bytes([0xF9])))
        self.assertTrue(entries[pelang.fnv1a("dlg/M001/002")].endswith(bytes([0xF9])))
        code, out = self.build({"dlg/M001/003": "Adiós."})
        self.assertTrue(pelang.read_pack(out)[2][pelang.fnv1a("dlg/M001/003")].endswith(bytes([0xF9])))
        self.assertEqual(pelang.split_page_end("Hola{end ff}"), ("Hola", "FF"))
        self.assertEqual(pelang.split_page_end("Hola{end FF} y"), ("Hola{end FF} y", None))
        self.assertEqual(self.build({"item/000": "Balas +6{end FF}"})[0], 1)

    def test_a_drawn_space_is_still_pulled_back_in_the_box(self):
        self.addCleanup(gt.PACK_DRAWN.clear)
        gt.PACK_DRAWN[0x0F] = letters.Letter(" ", pixels=(0,) * 144, dialogue=12, menu=0x60)
        self.assertEqual((gt.box_advance(0x0F), gt.base_advance(0x0F)), (5, 12))
        self.assertEqual(layout.advance(b"\x0f"), 5)
        self.assertEqual(gt.lines(b"\x0f\x0f"), [10])

    def test_a_patch_line_that_wont_fit_keeps_the_games_text(self):
        path = self.csv_with({"dlg/M001/003": "W" * 100})
        out = self.dir / "patch.pelang"
        args = ["build", str(path), "--strings", str(self.strings), "--lang", "xx", "--name", "Test",
                "-o", str(out), "--no-wrap"]
        self.assertEqual(pelang.main(args), 1)
        self.assertEqual(pelang.main(args + ["--from-patch"]), 0)
        self.assertNotIn(pelang.fnv1a("dlg/M001/003"), pelang.read_pack(out)[2])

    def test_pseudo_leaves_unsupported_source_rows_as_fallback(self):
        rows = STRINGS + [{"key": "sys/116", "text": "Hello{xF3}", "boxes": [], "lines_px": [30]}]
        self.strings.write_text(json.dumps(rows), encoding="utf-8")
        out = self.dir / "pseudo.csv"
        self.assertEqual(pelang.main(["pseudo", str(self.strings), "-o", str(out)]), 0)
        translated = {r["key"]: r for r in pelang.read_csv(out)}
        self.assertEqual(translated["sys/116"]["translation"], "")
        self.assertIn("Unsupported source", translated["sys/116"]["notes"])

    def test_latin_codes_and_encoding(self):
        codes = [g.code for g in latin.GLYPHS]
        self.assertEqual(len(codes), len(set(codes)))
        self.assertTrue(all(latin.FIRST_CODE <= c <= 0xF0 for c in codes))
        self.assertEqual(gt.encode("é", extended=True), bytes([latin.CODE_OF["é"]]))
        with self.assertRaises(ValueError):
            gt.encode("é")              # the plain encoder stays the US font's
        self.assertEqual(gt.lines(gt.encode("é", extended=True)), gt.lines(gt.encode("e")))

    def test_generated_c_files_are_current(self):
        # mods/pe_lang/pe_lang_latin.h and tests/pe_lang_font_vectors.h come from petext/latin.py.
        repo = Path(__file__).resolve().parents[2]
        for name, write in (("mods/pe_lang/pe_lang_latin.h", latin.write_c_header),
                            ("tests/pe_lang_font_vectors.h", latin.write_test_vectors),
                            ("tests/pe_lang_letter_vectors.h", letters.write_test_vectors)):
            fresh = self.dir / Path(name).name
            write(str(fresh))
            self.assertEqual((repo / name).read_bytes().replace(b"\r\n", b"\n"), fresh.read_bytes(),
                             f"{name} is stale: run `python -I localization/pelang.py latin-header`")


class ItalianImportTest(unittest.TestCase):
    def game(self, pages):
        import struct
        from types import SimpleNamespace
        body = b"".join(bytes([0xFE, page]) + raw + b"\xFF" for page, raw in sorted(pages.items()))
        data = bytearray(80 + len(body))
        struct.pack_into("<I", data, 4, 16)
        struct.pack_into("<I", data, 48, (1 << 22) | 64)
        struct.pack_into("<I", data, 68, (1 << 24) | 80)
        data[80:] = body
        return SimpleNamespace(exe_name="SLUS_006.62", room=lambda number: SimpleNamespace(data=bytes(data)))

    def catalog(self, pages):
        from petext.manifest import english_hash
        return {"banks": [{"key": "M001", "locations": {"1": 1, "2": 1}}],
                "entries": {f"dlg/M001/{page:03}": {"english_hash": english_hash(gt.decode(raw))}
                            for page, raw in pages.items()}}

    def test_italian_interpage_newline_preserves_following_pages(self):
        original = {0: gt.encode("First"), 1: gt.encode("Second{wait}")}
        translated = {0: gt.encode("Primo"), 1: gt.encode("Secondo{wait}")}
        game = self.game(translated)
        room = game.room(1)
        split = 80 + 2 + len(translated[0]) + 1
        room.data = room.data[:split] + b"\xf7" + room.data[split:]
        game.room = lambda number: room
        # Original/other profiles keep the strict contiguous-bank parser.
        self.assertEqual(set(pelang._dialogue_pages(game, 1)), {0})
        rows, fallback = pelang._translated_dialogue_rows(self.game(original), game, self.catalog(original))
        self.assertEqual([r["translation"] for r in rows], ["Primo", "Secondo{wait}"])
        self.assertEqual(fallback, [])

    def test_italian_separator_rejects_duplicate_page_ids(self):
        game = self.game({0: gt.encode("Primo")})
        room = game.room(1)
        room.data += b"\xf7\xfe\x00" + gt.encode("Duplicate") + b"\xff"
        game.room = lambda number: room
        with self.assertRaisesRegex(ValueError, "repeats a dialogue page ID"):
            pelang._dialogue_pages(game, 1, italian_separators=True)

    def test_unrecognized_italian_name_variants_remain_control_fallbacks(self):
        for bank, page in [("M035", 45), ("M041", 200), ("M191", 133)]:
            with self.subTest(bank=bank):
                original = {page: gt.encode("Daniel: Hello!")}
                translated = {page: gt.encode("Daniel: Ciao {name}!")}
                catalog = self.catalog(original)
                catalog["banks"][0]["key"] = bank
                catalog["entries"] = {f"dlg/{bank}/{page:03}": next(iter(catalog["entries"].values()))}
                changes = []
                rows, fallback = pelang._translated_dialogue_rows(self.game(original), self.game(translated),
                    catalog, dialogue_adaptations=changes)
                self.assertEqual(rows, [])
                self.assertEqual(changes, [])
                self.assertEqual(fallback, [{"key": f"dlg/{bank}/{page:03}",
                                            "reason": "original control sequence differs"}])

    def test_italian_original_name_command_is_retained(self):
        original = {0: gt.encode("Hello {name}!")}
        translated = {0: gt.encode("Ciao {name}!")}
        rows, fallback = pelang._translated_dialogue_rows(self.game(original), self.game(translated),
                                                         self.catalog(original))
        self.assertEqual(fallback, [])
        self.assertEqual(rows[0]["translation"], "Ciao {name}!")

    def test_unrecognized_italian_wait_remains_a_control_fallback(self):
        original = {14: gt.encode("Continue.")}
        translated = {14: gt.encode("Prima{wait} seconda.")}
        catalog = self.catalog(original)
        catalog["banks"][0]["key"] = "M271"
        catalog["entries"] = {"dlg/M271/014": next(iter(catalog["entries"].values()))}
        changes = []
        rows, fallback = pelang._translated_dialogue_rows(self.game(original), self.game(translated),
            catalog, dialogue_adaptations=changes)
        self.assertEqual(rows, [])
        self.assertEqual(changes, [])
        self.assertEqual(fallback, [{"key": "dlg/M271/014", "reason": "original control sequence differs"}])

    def test_italian_original_wait_command_is_retained(self):
        original = {0: gt.encode("First{wait} second.")}
        translated = {0: gt.encode("Prima{wait} seconda.")}
        rows, fallback = pelang._translated_dialogue_rows(self.game(original), self.game(translated),
                                                         self.catalog(original))
        self.assertEqual(fallback, [])
        self.assertEqual(rows[0]["translation"], "Prima{wait} seconda.")

    def test_accents_unchanged_rows_and_original_control_fallback(self):
        original = {0: gt.encode("Hello"), 1: gt.encode("Same"),
                    2: gt.encode("Missing"), 3: gt.encode("Wait{wait}")}
        translated = {0: bytes(range(0x4D, 0x53)), 1: original[1], 3: gt.encode("Aspetta")}
        rows, fallback = pelang._translated_dialogue_rows(self.game(original), self.game(translated), self.catalog(original))
        self.assertEqual([(r["key"], r["translation"]) for r in rows], [("dlg/M001/000", "àèéìòù")])
        self.assertEqual([r["key"] for r in fallback], ["dlg/M001/002", "dlg/M001/003"])

    def test_rejects_mismatched_source_pages_text_region_and_unknown_glyph(self):
        original = {0: gt.encode("Hello")}; catalog = self.catalog(original)
        for before, after, region in [({}, original, "SLUS_006.62"),
                ({0: gt.encode("Changed")}, original, "SLUS_006.62"),
                (original, {0: b"\x53"}, "SLUS_006.62"),
                (original, {0: gt.encode("Ciao"), 1: b"A"}, "SLUS_006.62"),
                (original, original, "SLPS_012.30")]:
            with self.subTest(before=before, after=after, region=region):
                game = self.game(before); game.exe_name = region
                with self.assertRaises(ValueError):
                    pelang._translated_dialogue_rows(game, self.game(after), catalog)

    def test_unknown_patch_leaves_existing_outputs_untouched(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); source = root / "disc.bin"; source.write_bytes(b"source")
            wrong = root / "wrong.ppf"; wrong.write_bytes(b"not the known patch")
            output = root / "it.csv"; output.write_bytes(b"keep csv")
            report = root / "report.json"; report.write_bytes(b"keep report")
            with patch("petext.disc.Game") as game:
                result = pelang.main(["import-italian", str(source), str(wrong), "-o", str(output), "--report", str(report)])
                game.assert_not_called()
            self.assertEqual(result, 1)
            self.assertEqual(source.read_bytes(), b"source")
            self.assertEqual(output.read_bytes(), b"keep csv")
            self.assertEqual(report.read_bytes(), b"keep report")

    def test_output_cannot_replace_image_patch_or_existing_file(self):
        import hashlib
        from types import SimpleNamespace
        from unittest.mock import Mock, patch
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); source = root / "disc.bin"; source.write_bytes(b"source")
            ppf = root / "translation.ppf"; ppf.write_bytes(bytes(6465866))
            digest = hashlib.sha256(ppf.read_bytes()).hexdigest()
            existing = root / "existing.csv"; existing.write_bytes(b"keep csv")
            for target in [source, ppf, existing]:
                game = SimpleNamespace(disc=SimpleNamespace(f=SimpleNamespace(name=str(source), close=Mock())))
                with patch.object(pelang, "ITALIAN_PATCH_SHA256", digest), patch("petext.disc.Game", return_value=game):
                    result = pelang.main(["import-italian", str(source), str(ppf), "-o", str(target),
                                          "--report", str(root / "report.json")])
                self.assertEqual(result, 1)
                self.assertFalse((root / "report.json").exists())
            self.assertEqual(source.read_bytes(), b"source")
            self.assertEqual(hashlib.sha256(ppf.read_bytes()).hexdigest(), digest)
            self.assertEqual(existing.read_bytes(), b"keep csv")


class GameSourceValidationTest(unittest.TestCase):
    def test_invalid_boot_config_is_a_closed_source_error(self):
        import io
        from types import SimpleNamespace
        from unittest.mock import patch
        from petext.disc import Game
        for config in (b"", b"BOOT = missing", b"BOOT = cdrom:\\UNKNOWN.EXE;1"):
            with self.subTest(config=config):
                image = io.BytesIO(b"original image")
                disc = SimpleNamespace(f=image, root_files=lambda: {"PE.IMG": (1, 1)},
                    file=lambda name: config if name == "SYSTEM.CNF" else b"not a PS-X EXE")
                with patch("petext.disc.Disc", return_value=disc):
                    with self.assertRaises(ValueError):
                        Game("invalid.bin")
                self.assertTrue(image.closed)

    def test_directory_failure_closes_source(self):
        import io
        from types import SimpleNamespace
        from unittest.mock import Mock, patch
        from petext.disc import Game
        image = io.BytesIO(b"original image")
        disc = SimpleNamespace(f=image, root_files=Mock(side_effect=ValueError("not a CD-ROM image")))
        with patch("petext.disc.Disc", return_value=disc):
            with self.assertRaisesRegex(ValueError, "not a CD-ROM image"):
                Game("invalid.bin")
        self.assertTrue(image.closed)


class SpanishImportTest(unittest.TestCase):
    game = ItalianImportTest.game
    catalog = ItalianImportTest.catalog

    def test_chicago_accents_yellow_speakers_and_quotes_reencode(self):
        self.assertEqual(pelang._spanish_text(bytes(range(0x4D, 0x5D)), "test"), "ÁÉÍÓÚáéíóú¿¡ñÑüÜ")
        speaker = bytes([0x64, 0x85, 0x83, 0x78, 0x88, 0x7B, 0x91])
        self.assertEqual(pelang._spanish_text(speaker, "test"), "Hombre:")
        text = pelang._spanish_text(bytes([0x2D, 0x30, 0x2E, 0x53]), "test")
        self.assertEqual(text, '"a"é')
        self.assertEqual(gt.encode(text, extended=True)[-1], latin.CODE_OF["é"])
        with self.assertRaises(ValueError):
            pelang._spanish_text(b"\x98", "test")

    def test_opening_credits_and_duplicate_line_do_not_add_gameplay_commands(self):
        raw = gt.encode("Credits {sym 1}{wait}Hombre: Repeated{wait} What is wrong?")
        original = gt.encode("Man: What is wrong?")
        normalized = pelang._spanish_dialogue(raw, original, "dlg/M001/009")
        self.assertEqual(gt.decode(normalized), "Hombre: What is wrong?")
        validation.check(normalized, validation.structure(original))
        with self.assertRaises(ValueError):
            pelang._spanish_dialogue(b"broken", original, "dlg/M001/009")
        self.assertEqual(pelang._spanish_dialogue(raw, original, "dlg/M001/008"), raw)

    def test_name_colour_normalization_preserves_other_highlighting(self):
        original = gt.encode("{name}: Hello")
        raw = gt.encode("{red}{name}:{/red} Hola")
        self.assertEqual(pelang._spanish_dialogue(raw, original, "test"), gt.encode("{name}: Hola"))
        highlighted = gt.encode("{red}{name}: Hello{/red}")
        self.assertEqual(pelang._spanish_dialogue(raw, highlighted, "test"), raw)
        inline = gt.encode("{red}Hello {name}{/red}")
        self.assertEqual(pelang._spanish_dialogue(inline, original, "test"), inline)

    def test_foreign_pages_unknown_symbols_and_changed_controls_are_reported(self):
        original = {0: gt.encode("Hello"), 1: gt.encode("Wait{wait}"), 2: gt.encode("Symbol")}
        translated = {0: b"\x53", 1: gt.encode("Wait"), 2: b"\x98", 3: gt.encode("Extra")}
        rows, fallback = pelang._translated_dialogue_rows(
            self.game(original), self.game(translated), self.catalog(original), "es")
        self.assertEqual([(r["key"], r["translation"]) for r in rows], [("dlg/M001/000", "é")])
        self.assertEqual({r["key"] for r in fallback}, {"dlg/M001/001", "dlg/M001/002", "dlg/M001/003"})
        self.assertEqual(len({r["reason"] for r in fallback}), 3)

    def test_profile_hash_outputs_and_failed_report_cleanup(self):
        import hashlib
        from types import SimpleNamespace
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); source = root / "original.bin"; translated = root / "translated.bin"
            source.write_bytes(b"source"); translated.write_bytes(b"translated")
            expected = (10, hashlib.sha256(b"translated").hexdigest())
            for mode in ("hash", "existing", "same", "cleanup", "success"):
                with self.subTest(mode=mode):
                    output = root / (mode + ".csv"); report = root / (mode + ".json")
                    if mode == "existing": output.write_bytes(b"keep")
                    if mode == "same": output = source
                    games = []
                    def game(path):
                        obj = SimpleNamespace(exe_name="SLUS_006.62", disc=SimpleNamespace(f=open(path, "rb")))
                        games.append(obj); return obj
                    path_open = Path.open
                    def create(path, *args, **kwargs):
                        if path == report and mode == "cleanup": raise OSError("injected report failure")
                        return path_open(path, *args, **kwargs)
                    profile = (10, "0" * 64) if mode == "hash" else expected
                    with patch("petext.disc.Game", side_effect=game), \
                         patch.object(pelang, "SPANISH_IMAGES", {"SLUS_006.62": profile}), \
                         patch.object(pelang, "_translated_dialogue_rows", return_value=([{
                             "key": "dlg/M001/000", "translation": "Hola", "notes": "test"}], [])), \
                         patch.object(pelang, "_translated_menu_rows", return_value=([], [], [])), \
                         patch.object(Path, "open", create):
                        result = pelang.main(["import-spanish", str(source), str(translated), "-o", str(output),
                                              "--report", str(report)])
                    self.assertEqual(result, 0 if mode == "success" else 1)
                    self.assertTrue(all(g.disc.f.closed for g in games))
                    self.assertEqual(source.read_bytes(), b"source")
                    self.assertEqual(translated.read_bytes(), b"translated")
                    if mode == "success":
                        self.assertEqual(json.loads(report.read_text())["translated_dialogue_rows"], 1)
                    elif mode == "existing": self.assertEqual(output.read_bytes(), b"keep")
                    elif mode != "same": self.assertFalse(output.exists())
                    if mode != "success": self.assertFalse(report.exists())


class ItalianMenuImportTest(unittest.TestCase):
    def run_import(self, before, after, manifest=None, language="it"):
        from types import SimpleNamespace
        from unittest.mock import patch
        from petext.manifest import english_hash
        manifest = manifest if manifest is not None else {"entries": {
            k: {"english_hash": english_hash(gt.decode(v))} for k, v in before.items()}}
        # The existing disc table decoder is independent of these import rules.
        with patch("extract_text.table_entries", side_effect=lambda data: data.items()):
            return pelang._translated_menu_rows(SimpleNamespace(resident_pack=lambda: before),
                                            SimpleNamespace(resident_pack=lambda: after), manifest, language)

    def test_spanish_width_adaptations_do_not_apply_italian_rewrites(self):
        text = {"sys/071": "Memory Card nello Slot 1",
                "sys/078": "No hay ninguna tarjeta de memoria insertada",
                "sys/086": "Borra los datos desde el gestor de tarjetas de memoria"}
        before = {k: gt.encode("Original") for k in text}
        after = {k: gt.encode(v) for k, v in text.items()}
        before["sys/115"] = gt.encode("Rename"); after["sys/115"] = b"\x53"
        before["sys/087"] = gt.encode("Symbol"); after["sys/087"] = b"\x98"
        rows, fallback, fits = self.run_import(before, after, language="es")
        translated = {r["key"]: r["translation"] for r in rows}
        self.assertEqual(translated["sys/071"], text["sys/071"])
        self.assertEqual(translated["sys/078"], "No hay tarjeta de memoria insertada")
        self.assertEqual(translated["sys/086"], "Borra los datos en el gestor de tarjetas")
        self.assertEqual({r["key"] for r in fallback}, {"sys/087", "sys/115"})
        self.assertEqual([r["key"] for r in fits], ["sys/078", "sys/086"])
        self.assertTrue(all(r["reason"] == "reviewed original memory-card field width" for r in fits))

    def test_native_rename_grids_stay_original_and_width_fits_are_reported(self):
        text = {"sys/071": "Memory Card nello Slot 1", "sys/072": "Memory Card nello Slot 2",
                "item/096": "Arma di Maeda", "item/146": "Arma di Maeda"}
        text.update({f"item/{n:03}": f"Evasione CR +{n-43}" for n in range(44,48)})
        before = {k: gt.encode("Original") for k in text}
        after = {k: gt.encode(v) for k,v in text.items()}
        for n in (115,116,117):
            before[f"sys/{n}"] = b"\x56"; after[f"sys/{n}"] = b"\x57"
        rows, fallback, fits = self.run_import(before, after)
        translated = {r["key"]:r["translation"] for r in rows}
        self.assertEqual(translated["sys/071"], "Memory Card Slot 1")
        self.assertEqual(translated["sys/072"], "Memory Card Slot 2")
        self.assertEqual(translated["item/044"], "Evas. CR +1")
        self.assertEqual(translated["item/047"], "Evas. CR +4")
        self.assertEqual(translated["item/096"], "Arma Maeda")
        self.assertEqual(translated["item/146"], "Arma Maeda")
        self.assertEqual(len(fits), 8)
        self.assertEqual([r["key"] for r in fallback], ["sys/115","sys/116","sys/117"])

    def test_changed_controls_and_unindexed_slots_are_not_exported(self):
        before = {"sys/000":gt.encode("Wait{wait}"), "misc/200":gt.encode("Unused")}
        after = {"sys/000":gt.encode("Attendi"), "misc/200":gt.encode("Unused changed")}
        from petext.manifest import english_hash
        manifest = {"entries":{"sys/000":{"english_hash":english_hash(gt.decode(before["sys/000"]))}}}
        rows, fallback, fits = self.run_import(before, after, manifest)
        self.assertEqual(rows, []); self.assertEqual(fits, [])
        self.assertEqual([r["key"] for r in fallback], ["sys/000"])

    def test_wrong_table_indices_source_hash_and_unknown_glyph_fail(self):
        before = {"sys/000":gt.encode("Source")}
        for after, manifest in [({},None),
                ({"sys/000":b"\x53"},None),
                ({"sys/000":gt.encode("Ciao")},{"entries":{"sys/000":{"english_hash":"wrong"}}})]:
            with self.subTest(after=after, manifest=manifest):
                with self.assertRaises(ValueError):self.run_import(before, after, manifest)


class DiscOverlayTest(unittest.TestCase):
    """User-supplied translation patches never modify the source image."""
    def setUp(self):
        import struct
        from petext.disc import RAW_SECTOR, SYNC
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.image = self.root / "płyta test.bin"
        raw = bytearray(RAW_SECTOR * 18)
        for sector in range(18):
            at = sector * RAW_SECTOR
            raw[at:at + 12] = SYNC
            raw[at + 15] = 2
            raw[at + 24:at + 24 + 2048] = bytes((sector + x) % 251 for x in range(2048))
        self.original = bytes(raw)
        self.image.write_bytes(raw)
        self.patch = self.root / "translation.ppf"
        self.header = b"PPF30\x02" + bytes(54)
        self.record = lambda offset, data: struct.pack("<QB", offset, len(data)) + data

    def test_overlay_clips_at_sector_data_boundaries_and_preserves_image(self):
        from petext.disc import Disc, RAW_SECTOR
        cue = self.root / "płyta.cue"
        cue.write_text('FILE "płyta test.bin" BINARY\n  TRACK 01 MODE2/2352\n')
        description = b"Translation credits"
        self.patch.write_bytes(self.header + self.record(22, b"abcde")
            + self.record(24 + 2046, b"WXYZ") + self.record(RAW_SECTOR + 32, b"IT")
            + b"@BEGIN_FILE_ID.DIZ" + description + b"@END_FILE_ID.DIZ"
            + len(description).to_bytes(2, "little"))
        plain, patched = Disc(cue), Disc(cue, patch=self.patch)
        try:
            first = bytearray(plain.sector(0))
            first[:3], first[-2:] = b"cde", b"WX"
            second = bytearray(plain.sector(1)); second[8:10] = b"IT"
            self.assertEqual(patched.read(0, 4096), bytes(first + second))
            self.assertEqual(patched.sector(0), first)  # nonsequential reread
            self.assertEqual(patched.sector(16), plain.sector(16))
            self.assertEqual(patched.f.mode, "rb")
        finally:
            plain.f.close(); patched.f.close()
        self.assertEqual(self.image.read_bytes(), self.original)

    def test_rejects_entire_malformed_overlay_before_reading(self):
        import struct
        from petext.disc import Disc
        cases = [b"", self.header[:59], b"bad" + self.header[3:],
            self.header[:57] + b"\x01" + self.header[58:],
            self.header + b"short", self.header + struct.pack("<QB", 24, 0),
            self.header + struct.pack("<QB", 24, 3) + b"a",
            self.header + self.record(25, b"ab") + self.record(26, b"c"),
            self.header + self.record(30, b"a") + self.record(20, b"b"),
            self.header + self.record(len(self.original), b"x"),
            self.header + b"@BEGIN_FILE_ID.DIZtruncated"]
        for data in cases:
            with self.subTest(data=data[:75]):
                self.patch.write_bytes(data)
                with self.assertRaises(ValueError):
                    Disc(self.image, patch=self.patch)
        self.assertEqual(self.image.read_bytes(), self.original)

    def test_bin_patch_rejects_cooked_iso(self):
        from petext.disc import Disc
        cooked = self.root / "disc.iso"
        cooked.write_bytes(bytes(2048 * 18))
        self.patch.write_bytes(self.header + self.record(24, b"IT"))
        with self.assertRaisesRegex(ValueError, "raw 2352"):
            Disc(cooked, patch=self.patch)


if __name__ == "__main__":
    unittest.main()
