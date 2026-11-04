"""pelang.py: templates, the pseudo language and the compiler, on a small synthetic strings.json
(no game data needed).

  python -I -m unittest discover -s localization/tests
"""
import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pelang  # noqa: E402
from petext import gametext as gt, latin, layout, validation  # noqa: E402

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

    def build(self, translations: dict) -> tuple[int, Path]:
        out = self.dir / "t.pelang"
        code = pelang.main(["build", str(self.csv_with(translations)), "--strings", str(self.strings),
                            "--lang", "xx", "--name", "Test", "-o", str(out)])
        return code, out

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

    def test_dialogue_keeps_its_page_end(self):
        code, out = self.build({"dlg/M001/003": "Salut."})
        self.assertEqual(code, 0)
        _, _, entries = pelang.read_pack(out)
        self.assertEqual(entries[pelang.fnv1a("dlg/M001/003")], gt.encode("Salut.") + b"\xf9")

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
                            ("tests/pe_lang_font_vectors.h", latin.write_test_vectors)):
            fresh = self.dir / Path(name).name
            write(str(fresh))
            self.assertEqual((repo / name).read_bytes().replace(b"\r\n", b"\n"), fresh.read_bytes(),
                             f"{name} is stale: run `python -I localization/pelang.py latin-header`")


if __name__ == "__main__":
    unittest.main()
