"""The Translator's work with no window (translator/project.py): a .pelang in, rows a person edits,
a .pelang out that the game loads.

Three sets of tests:

  rules        no disc and no game text: made-up lines against the committed manifest. A new
               translation saves a file the pack reader reads, and reopens as typed; a line that
               fails the checks is left out of the pack but never lost; the file's other parts
               survive; a pack the compiler wouldn't write back as it is can't be saved over; the
               file is, byte for byte, what `pelang.py build` writes from the same lines; every
               compiler error lands on a line; the preview's widths are the layout check's.
  real English needs PE_TEST_DISC (PE_TEST_DISC2 adds Disc 2): the English pack made from the discs
               reads back as the discs' own text, every row; and a whole translation (every row of
               the game, in a made-up test language) goes through open, save and reopen.
  real packs   needs PE_TEST_PLAYER_PACKS (a folder of .pelang translations): each opens with every
               row it holds, and each either saves back unchanged or refuses to save.

  python -I localization/tests/test_translator.py          (77: something was skipped)
"""
import contextlib
import csv
import io
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
import zipfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE))
import pelang  # noqa: E402
from petext import alphabets, citymap, gametext as gt, layout, menu, pictures as picturebin  # noqa: E402
from translator import project as P  # noqa: E402

GAME = P.Game()


def plain_dialogue(n: int = 12) -> list[str]:
    """Dialogue rows a made-up line fits: no controls to keep, drawn in the usual box."""
    return [k for k in GAME.keys if k.startswith("dlg/") and GAME.rows[k].get("structure") == []
            and GAME.rows[k].get("boxes") == ["default"]][:n]


def name_dialogue() -> str:
    return next(k for k in GAME.keys if k.startswith("dlg/") and GAME.must_keep(k) == ["{name}"]
                and GAME.rows[k].get("boxes") == ["default"])


def command_line_build(texts: dict, lang: str, name: str, out: Path) -> str:
    sheet = out.with_suffix(".csv")
    with open(sheet, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=pelang.SUBMISSION_COLUMNS)
        w.writeheader()
        for key in sorted(texts):
            w.writerow({"key": key, "translation": texts[key], "notes": ""})
    said = io.StringIO()
    with contextlib.redirect_stdout(said), contextlib.redirect_stderr(said):
        code = pelang.main(["build", str(sheet), "--strings", str(P.MANIFEST), "--lang", lang, "--name", name,
                            "-o", str(out), "--max-messages", "100000"])
    if code:
        raise AssertionError(said.getvalue()[-800:])
    return said.getvalue()


class RuleTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def new(self, lang="fr", name="French"):
        return P.Translation(GAME, lang, name)

    def test_the_manifest_has_every_row_and_no_two_share_a_hash(self):
        self.assertEqual(len(GAME.keys), len(GAME.by_hash))
        self.assertEqual(sum(n for _, _, n in GAME.groups()), len(GAME.keys))
        self.assertEqual([g for g, _, _ in GAME.groups()][:5], ["sys", "item", "desc", "misc", "battle"])

    def test_a_new_translation_saves_a_file_the_pack_reader_reads_and_reopens_as_typed(self):
        a, b = plain_dialogue(2)
        t = self.new()
        t.set_text(a, "Une ligne à moi, écrite pour ce test.")
        t.set_text(b, "Voilà{br} deux lignes.")
        t.notes[a] = "to check with Marie"
        path = self.dir / "fr.pelang"
        report = t.save(path)
        self.assertEqual((report.in_pack, report.left_out, report.whole), (2, {}, []))
        # what the game opens: a zip whose text.bin is a pack with both rows and the banks' names
        with zipfile.ZipFile(path) as z:
            self.assertEqual(sorted(z.namelist()), ["text.bin", "translator/work.json"])
        lang, name, entries = pelang.read_pack(path)
        self.assertEqual((lang, name), ("fr", "French"))
        self.assertEqual({h for h in entries if h in GAME.by_hash}, {pelang.fnv1a(a), pelang.fnv1a(b)})
        self.assertEqual(len(entries), 2 + len(GAME.banks))
        again = P.Translation.open(path, GAME)
        self.assertTrue(again.from_work)
        self.assertEqual(again.locked, {})
        self.assertEqual((again.lang, again.name, again.texts, again.notes), ("fr", "French", t.texts, t.notes))
        # and without its work part the pack alone reads back as the same words
        read = P.read_pack_text(path, GAME)
        self.assertEqual(read.texts, t.texts)
        self.assertEqual(read.unknown, 0)

    def test_a_translation_made_here_keeps_the_strict_rules_and_has_no_letters_of_its_own(self):
        """Letters the catalog draws (accents, Cyrillic) are not taken for a pack's own pictures, and
        a pack the strict rules write back as it is stays strict, with or without its work part."""
        a, b = plain_dialogue(2)
        t = self.new("ru", "Russian")
        t.set_text(a, "Ёлка, щука и «объём» — à côté, ça suffit, Œuvre.")
        t.set_text(b, "Привет{br} мир.")
        path = self.dir / "ru.pelang"
        t.save(path)
        again = P.Translation.open(path, GAME)
        self.assertEqual((again.own_letters, again.own_cjk, again.from_patch), ((), (), False))
        with zipfile.ZipFile(path) as z:
            work = json.loads(z.read(P.WORK))
        self.assertEqual((work["version"], "rules" in work), (P.WORK_VERSION, False))
        bare = self.dir / "bare.pelang"
        bare.write_bytes(pelang.pack_bytes(path))
        alone = P.Translation.open(bare, GAME)
        self.assertEqual((alone.from_work, alone.from_patch, alone.locked), (False, False, {}))
        # (read back from the pack alone, Cyrillic letters the game draws with a Latin one's picture
        # come back as that Latin letter: the same bytes, so the same pack)
        alone.save(self.dir / "bare again.pelang")
        self.assertEqual(pelang.pack_bytes(self.dir / "bare again.pelang"), pelang.pack_bytes(path))

    def test_a_file_saved_with_the_first_work_part_still_opens_as_typed(self):
        key, = plain_dialogue(1)
        t = self.new()
        t.set_text(key, "Une ligne.")
        path = self.dir / "fr.pelang"
        t.save(path)
        with zipfile.ZipFile(path) as z:
            parts = {i.filename: z.read(i) for i in z.infolist()}
        work = json.loads(parts[P.WORK])
        work["version"] = 1
        parts[P.WORK] = json.dumps(work).encode("utf-8")
        with zipfile.ZipFile(path, "w") as z:
            for name, data in parts.items():
                z.writestr(name, data)
        again = P.Translation.open(path, GAME)
        self.assertEqual((again.from_work, again.from_patch, again.texts), (True, False, t.texts))

    def test_the_file_is_what_the_command_line_writes_from_the_same_lines(self):
        rows = plain_dialogue(6)
        t = self.new("es", "Spanish")
        for n, key in enumerate(rows):
            t.set_text(key, f"¿Línea número {n}? ¡Sí, señor, una línea larga para que tenga que partirse "
                            f"en varias cuando no cabe en la caja del juego!")
        t.set_text("sys/030", "Ana")
        t.save(self.dir / "es.pelang")
        command_line_build(t.texts, "es", "Spanish", self.dir / "cli.pelang")
        self.assertEqual(pelang.pack_bytes(self.dir / "es.pelang"), (self.dir / "cli.pelang").read_bytes())

    def test_a_line_that_fails_is_left_out_of_the_pack_and_kept_in_the_work(self):
        good, = plain_dialogue(1)
        bad = name_dialogue()
        t = self.new()
        t.set_text(good, "Une bonne ligne.")
        t.set_text(bad, "Cette ligne a perdu le nom.")
        report = t.save(self.dir / "fr.pelang")
        self.assertEqual(report.in_pack, 1)
        self.assertEqual(list(report.left_out), [bad])
        self.assertIn("control commands must match", report.left_out[bad][0])
        entries = pelang.read_pack(self.dir / "fr.pelang")[2]
        self.assertIn(pelang.fnv1a(good), entries)
        self.assertNotIn(pelang.fnv1a(bad), entries)
        again = P.Translation.open(self.dir / "fr.pelang", GAME)
        self.assertEqual(again.text(bad), "Cette ligne a perdu le nom.")
        self.assertEqual(again.locked, {})
        # fixed, it goes in
        again.set_text(bad, "{name}: La voilà.")
        self.assertEqual(again.save().left_out, {})
        self.assertIn(pelang.fnv1a(bad), pelang.read_pack(self.dir / "fr.pelang")[2])

    def test_the_files_other_parts_and_the_version_before_survive_a_save(self):
        key, = plain_dialogue(1)
        t = self.new()
        t.set_text(key, "Un.")
        path = self.dir / "fr.pelang"
        t.save(path)
        parts = {"pictures.bin": bytes(range(256)) * 9, "readme.txt": "Crédits: moi.\n".encode("utf-8"),
                 "extras/later.dat": b"\x00\x01kept"}
        with zipfile.ZipFile(path, "a") as z:
            for name, data in parts.items():
                z.writestr(name, data)
        before = path.read_bytes()
        again = P.Translation.open(path, GAME)
        self.assertEqual(again.parts, parts)
        again.set_text(key, "Deux.")
        again.save()
        with zipfile.ZipFile(path) as z:
            self.assertEqual({n: z.read(n) for n in parts}, parts)
            self.assertEqual(sorted(z.namelist()), sorted([*parts, "text.bin", "translator/work.json"]))
        self.assertEqual((self.dir / "fr.pelang.before-last-save").read_bytes(), before)
        self.assertEqual(P.Translation.open(path, GAME).text(key), "Deux.")

    def test_a_pack_rebuilt_by_other_means_wins_over_a_stale_work_part(self):
        a, b = plain_dialogue(2)
        bad = name_dialogue()
        t = self.new()
        t.set_text(a, "Ancienne ligne.")
        t.set_text(bad, "Sans le nom.")
        path = self.dir / "fr.pelang"
        t.save(path)
        command_line_build({a: "Nouvelle ligne.", b: "Une autre."}, "fr", "French", self.dir / "cli.bin")
        with zipfile.ZipFile(path) as z:
            work = z.read(P.WORK)
        with zipfile.ZipFile(path, "w") as z:
            z.writestr("text.bin", (self.dir / "cli.bin").read_bytes())
            z.writestr(P.WORK, work)
        again = P.Translation.open(path, GAME)
        self.assertFalse(again.from_work)
        self.assertEqual(again.text(a), "Nouvelle ligne.")
        self.assertEqual(again.text(b), "Une autre.")
        self.assertEqual(again.text(bad), "Sans le nom.", "a row only the work part had is kept")

    def test_a_pack_the_compiler_would_not_write_back_cannot_be_saved_over(self):
        # A pack made by other means, holding a line the checks refuse: a battle message too wide.
        key = next(k for k in GAME.keys if k.startswith("battle/") and GAME.rows[k].get("structure") == [])
        sheet = self.dir / "wide.csv"
        sheet.write_text("key,translation,notes\n" + f"{key},{'W' * 60},\n", encoding="utf-8")
        said = io.StringIO()
        with contextlib.redirect_stdout(said), contextlib.redirect_stderr(said):
            code = pelang.main(["build", str(sheet), "--strings", str(P.MANIFEST), "--lang", "xx", "--name", "Wide",
                                "-o", str(self.dir / "wide.pelang"), "--allow-overflow"])
        self.assertEqual(code, 0, said.getvalue())
        before = (self.dir / "wide.pelang").read_bytes()
        t = P.Translation.open(self.dir / "wide.pelang", GAME)
        self.assertEqual(t.text(key), "W" * 60)
        self.assertEqual(list(t.locked), [key])
        with self.assertRaises(P.WouldChange):
            t.save()
        with self.assertRaises(P.WouldChange):
            t.save(self.dir / "copy.pelang")
        self.assertEqual((self.dir / "wide.pelang").read_bytes(), before)
        self.assertFalse((self.dir / "copy.pelang").exists())

    def test_a_file_that_is_not_a_pack_is_refused_plainly(self):
        (self.dir / "x.pelang").write_bytes(b"not a pack at all")
        with self.assertRaises(P.NotAPack):
            P.Translation.open(self.dir / "x.pelang", GAME)
        with zipfile.ZipFile(self.dir / "empty.pelang", "w") as z:
            z.writestr("readme.txt", "no pack in here")
        with self.assertRaises(P.NotAPack):
            P.Translation.open(self.dir / "empty.pelang", GAME)
        with self.assertRaises(P.NotAPack):
            P.Translation.open(self.dir / "missing.pelang", GAME)

    def test_every_language_the_tool_offers_reads_back_as_the_same_pack(self):
        """Each ready language's whole alphabet, typed, saved, read back from the compiled pack
        alone (no work part) and compiled again: the same pack. So opening a pack made by other
        means loses no letter."""
        rows = plain_dialogue(12)
        checked = []
        for choice in P.languages():
            if choice.state == "not yet" or choice.code in alphabets.CJK:
                continue
            letters = alphabets.full_alphabet(alphabets.BY_CODE[choice.code])
            lines = [letters[i:i + 18] for i in range(0, len(letters), 18)]
            self.assertLessEqual(len(lines), len(rows), choice.code)
            t = self.new(choice.code, choice.name)
            for key, line in zip(rows, lines):
                t.set_text(key, line)
            path = self.dir / f"{choice.code}.pelang"
            report = t.save(path)
            self.assertEqual(report.left_out, {}, choice.code)
            read = P.read_pack_text(pelang.pack_bytes(path), GAME)
            self.assertEqual(set(read.texts), set(t.texts), choice.code)
            self.assertFalse([s for s in read.texts.values() if "{g" in s or "{x" in s], choice.code)
            back = P.build(GAME, read.texts, choice.code, choice.name)
            self.assertTrue(back.ok, f"{choice.code}: {back.errors()[:3]}")
            self.assertEqual(back.pack, pelang.pack_bytes(path), choice.code)
            checked.append(choice.code)
        self.assertGreater(len(checked), 35)
        print(f"\n{len(checked)} languages read back as the same pack: {' '.join(checked)}")

    def test_the_default_name_reads_back_with_its_own_letters(self):
        t = self.new("pl", "Polish")
        t.set_text("sys/030", "Łucja")
        path = self.dir / "pl.pelang"
        self.assertEqual(t.save(path).left_out, {})
        self.assertEqual(P.read_pack_text(path, GAME).texts, {"sys/030": "Łucja"})

    def test_a_page_end_that_is_not_the_games_reads_back_as_its_tag(self):
        key = next(k for k in plain_dialogue(40) if GAME.rows[k].get("end") == "FF")
        t = self.new()
        t.set_text(key, "La boîte se ferme seule.{end F9}")
        path = self.dir / "fr.pelang"
        self.assertEqual(t.save(path).left_out, {})
        self.assertEqual(P.read_pack_text(path, GAME).texts[key], "La boîte se ferme seule.{end F9}")

    def test_every_error_the_compiler_prints_lands_on_a_line(self):
        rows = plain_dialogue(3)
        item = next(k for k in GAME.keys if k.startswith("item/") and GAME.rows[k].get("item_kind") is not None)
        texts = {rows[0]: "No such letter: ☃", rows[1]: "{nonsense}", name_dialogue(): "no name",
                 item: "An item name far too long to be joined with anything at all"}
        built = P.build(GAME, texts, "fr", "French")
        self.assertFalse(built.ok)
        self.assertEqual([m.text for m in built.messages if m.level == "error" and m.key is None], [])
        self.assertEqual(built.keys_with("error"), set(texts))
        self.assertTrue(any(e.startswith("joined text") for e in built.errors(item)))
        # The default name is checked last, once the rest builds; a save finds that too, leaves every
        # failing row out, and still writes a pack with the row that passes.
        t = self.new()
        for key, text in {**texts, "sys/030": "Much too long a name", rows[2]: "Une bonne ligne."}.items():
            t.set_text(key, text)
        compiled, left, _ = t.compile()
        self.assertTrue(compiled.ok)
        self.assertEqual(set(left), set(texts) | {"sys/030"})
        self.assertIn("default name must be at most", left["sys/030"][0])
        self.assertEqual({h for h in P.pelang_entries(compiled.pack) if h in GAME.by_hash}, {pelang.fnv1a(rows[2])})
        # an unknown key and a refusal about the whole pack are told too, not dropped
        whole = P.messages_of("error: CJK font: needs a font\nerror: zz/001: no such key\nnote: x\n3 of 9 keys")
        self.assertEqual([(m.key, m.text) for m in whole], [(None, "CJK font: needs a font"),
                                                            (None, "zz/001: no such key")])

    def test_one_line_is_checked_and_drawn_as_the_compiler_lays_it_out(self):
        key, = plain_dialogue(1)
        short = P.check_row(GAME, key, "Une ligne courte.", "fr")
        self.assertEqual((short.errors, short.warnings), ([], []))
        pv = short.preview
        self.assertTrue(pv.dialogue)
        self.assertFalse(pv.wrapped)
        self.assertEqual(len(pv.screens), 1)
        self.assertEqual("".join(p.text for p in pv.screens[0].lines[0]), "Une ligne courte.")
        self.assertEqual((pv.boxes[0].name, pv.boxes[0].width, pv.boxes[0].lines), ("default", 297, 4))
        # the pieces' places add up to the layout check's own width for the line
        raw = P.pelang_entries(P.build(GAME, {key: "Une ligne courte."}, "fr", "French").pack)[pelang.fnv1a(key)]
        raw = P.split_page_end(raw)[0]
        self.assertEqual(pv.screens[0].widths, layout.screens(raw)[0])
        last = pv.screens[0].lines[0][-1]
        self.assertEqual(last.x + last.width, pv.screens[0].widths[0])

        long = P.check_row(GAME, key, "mot " * 120, "fr")
        self.assertEqual(long.errors, [])
        self.assertTrue(any("wrapped to fit the box" in w for w in long.warnings))
        self.assertTrue(long.preview.wrapped)
        self.assertGreater(len(long.preview.screens), 1)
        for screen in long.preview.screens:
            self.assertLessEqual(len(screen.lines), 4)
            self.assertLessEqual(max(screen.widths), 297)

        named = P.check_row(GAME, name_dialogue(), "{name}: Oui.", "fr", default_name="Ana")
        self.assertEqual(named.errors, [])
        first = named.preview.screens[0].lines[0][0]
        self.assertEqual((first.kind, first.text), ("name", "Ana"))
        self.assertEqual(first.width, named.preview.name_px)

        wrong = P.check_row(GAME, name_dialogue(), "pas de nom", "fr")
        self.assertTrue(wrong.errors)
        self.assertIsNone(wrong.preview)

        item = next(k for k in GAME.keys if k.startswith("item/"))
        menu_row = P.check_row(GAME, item, "Clé", "fr")
        self.assertEqual(menu_row.errors, [])
        self.assertFalse(menu_row.preview.dialogue)
        self.assertGreater(menu_row.preview.menu_px, 0)

        self.assertEqual(P.check_row(GAME, key, "", "fr").preview, None)

    def test_a_line_too_wide_is_an_error_and_is_still_drawn(self):
        key = next(k for k in GAME.keys if k.startswith("battle/") and GAME.rows[k].get("structure") == [])
        r = P.check_row(GAME, key, "W" * 60, "fr")
        self.assertTrue(any("battle message fits" in e for e in r.errors), r.errors)
        self.assertIsNotNone(r.preview)
        self.assertGreater(r.preview.screens[0].widths[0], r.preview.boxes[0].width)

    def test_the_languages_offered_are_the_pack_tools_own_list(self):
        choices = {c.code: c for c in P.languages()}
        self.assertEqual(list(choices), [l.code for l in alphabets.LANGUAGES])
        self.assertEqual({c for c, v in choices.items() if v.state == "not yet"}, alphabets.NEEDS_LAYOUT)
        self.assertEqual(choices["fr"].state, "ready")
        self.assertEqual(choices["el"].state, "letters missing")
        self.assertIn("font", choices["ja"].note)


class EnglishTest(unittest.TestCase):
    """With the real discs: the English pack as `pelang.py english` makes it."""

    @classmethod
    def setUpClass(cls):
        discs = [os.environ.get("PE_TEST_DISC"), os.environ.get("PE_TEST_DISC2")]
        if not discs[0] or not Path(discs[0]).is_file():
            raise unittest.SkipTest("needs PE_TEST_DISC (USA disc 1); PE_TEST_DISC2 adds disc 2")
        cls.discs = [d for d in discs if d and Path(d).is_file()]
        cls.tmp = tempfile.TemporaryDirectory()
        cls.dir = Path(cls.tmp.name)
        subprocess.run([sys.executable, "-I", str(HERE / "extract_text.py"), *cls.discs, "--out", str(cls.dir / "text")],
                       check=True, capture_output=True)
        cls.strings = {r["key"]: r for r in json.loads((cls.dir / "text" / "strings.json").read_text(encoding="utf-8"))
                       if not r.get("unused")}
        cls.pack = cls.dir / "en.pelang"
        said = io.StringIO()
        with contextlib.redirect_stdout(said), contextlib.redirect_stderr(said):
            code = pelang.main(["english", str(cls.dir / "text" / "strings.json"), "-o", str(cls.pack)])
        if code:
            raise AssertionError(said.getvalue()[-800:])
        cls.english, cls.wrong = P.read_english(cls.pack, GAME)

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "tmp"):
            cls.tmp.cleanup()

    def test_the_menu_look_reads_the_menus_from_the_discs_and_measures_every_help_line_as_the_game(self):
        """The window table, menu metrics and the menus' letter colours as each disc has them (the same on
        both), and every English help line laid out to the width the manifest has from the discs."""
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("needs Pillow")
        from translator import gamelook as G, menulook as M
        menus = [M.MenuDisc(d) for d in self.discs]
        for md in menus:
            self.assertEqual(md.windows, menus[0].windows)
            self.assertEqual(md.windows[M.HELP_WINDOW], (8, 12, 300, 20))
            self.assertEqual(md.metrics[:0x100], GAME.metrics[:0x100])
            self.assertEqual(md.colours((M.MENU_COLOURS_ROW << 6) | 28)[:4], [0, 0x7FFF, 0x0421, 0x7FFF])
        pack = G.Pack.read(pelang.pack_bytes(self.pack))
        mf = M.MenuFont(G.Font(G.DiscLook(self.discs[0]), pack), menus[0])
        keys = [k for k in GAME.rows if M.context_of(k) == "help line" and pack.text(k) is not None]
        self.assertGreater(len(keys), 350)
        # A button picture in a menu line (FB 00) moves the pen 12 px, but the menus measure it as glyph 0xED
        # plus a pixel (MenuMeasure: 13 px on the USA discs), and so do the compiler (petext/menu.py width)
        # and the manifest. misc/068 is the only such help line.
        sprites = {k: pack.text(k).count(b"\xfb\x00") for k in keys}
        self.assertEqual({k: n for k, n in sprites.items() if n}, {"misc/068": 1})
        self.assertEqual(menu.width(b"\xfb\x00\xff", GAME.metrics), 13)
        wrong = {k: (M.lay_out(mf, pack.text(k), 0, 0).width, GAME.rows[k].get("menu_px"),
                     menu.width(bytes.fromhex(self.strings[k]["raw"]), GAME.metrics)) for k in keys}
        wrong = {k: v for k, v in wrong.items() if not v[0] == v[1] == v[2]}
        self.assertEqual(wrong, {})
        print(f"\n{len(keys)} English help lines laid out by the menu look at the manifest's widths")

    def test_the_item_list_draws_every_item_name_with_aya_s_first_items(self):
        """The item list's window, grid and Aya's first items as the discs have them (the same on both),
        and every English item name drawn in the cursor's cell at the manifest's width, with its icon, its
        count box when it has one, and nothing left out."""
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("needs Pillow")
        from translator import gamelook as G, menulook as M
        menus = [M.MenuDisc(d) for d in self.discs]
        for md in menus:
            self.assertEqual((md.grids, md.items), (menus[0].grids, menus[0].items))
            self.assertEqual(md.windows[M.ITEM_WINDOW], (28, 32, 252, 132))
            self.assertEqual(md.grids[M.ITEM_GRID][:8], (2, 2, 2, 8, 50, 124, 16, 0))
            self.assertEqual(M.item_cells(md), (28, 32, 252, 84, 32, 36, 124, 16, 2))
            self.assertEqual(len(md.items), 255)
            self.assertTrue(all(md.items[i][4] == i + 1 and md.items[i][0] for i in range(235)))
            # M84F, N Vest, Club 1, Ammo Crate, Medicine 1: their icons, kinds and what the list shows of them
            first = [md.items[i] for i, _ in M.START_ITEMS]
            self.assertEqual([r[6] for r in first], [1, 9, 8, 0x13, 10])
            self.assertEqual([M.count_sprite(r[6]) for r in first], [0x1F, None, None, 0x1F, None])
            self.assertEqual([M.usable(r) for r in first], [False] * 5)
        pack = G.Pack.read(pelang.pack_bytes(self.pack))
        mf = M.MenuFont(G.Font(G.DiscLook(self.discs[0]), pack), menus[0])
        keys = [k for k in GAME.rows if M.context_of(k) == "item list" and pack.text(k)]
        self.assertEqual(len(keys), 235)
        wrong, notes = {}, {}
        for k in keys:
            look = M.item_list(mf, pack.text(k), k, None)
            if look.text.width != GAME.rows[k]["menu_px"]:
                wrong[k] = (look.text.width, GAME.rows[k]["menu_px"])
            if look.notes:
                notes[k] = look.notes
        self.assertEqual(wrong, {})
        self.assertEqual(notes, {})
        print(f"\n{len(keys)} English item names drawn in the item list at the manifest's widths")

    def test_the_pe_list_draws_every_pe_name_beside_the_others(self):
        """The PE list's window and grid as the discs have them (the same on both), which PEs the field
        menu can use (Heal 1-3 and Full Recover), and every English PE name drawn under the cursor at the
        manifest's width, inside its 100 px cell, with the other PEs a game teaches and nothing left out."""
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("needs Pillow")
        from translator import gamelook as G, menulook as M
        menus = [M.MenuDisc(d) for d in self.discs]
        for md in menus:
            self.assertEqual(md.windows[M.PE_WINDOW], (28, 32, 204, 132))
            self.assertEqual(md.grids[M.PE_GRID][:8], (2, 2, 2, 8, 8, 100, 16, 0))
            self.assertEqual(M.grid_cells(md, M.PE_WINDOW, M.PE_GRID, len(M.LEARNABLE)),
                             (28, 32, 204, 132, 32, 36, 100, 16, 2))
            pes = md.items[M.PE_FIRST:M.PE_FIRST + M.PE_COUNT]
            self.assertEqual([r[0] for r in pes], [0xFF] * 20)
            self.assertEqual([p for p in range(20) if M.pe_usable(pes[p], p)], [0, 1, 2, 18])
            self.assertEqual([p for p in range(20) if M.pe_usable(pes[p], p, hurt=False)], [])
        pack = G.Pack.read(pelang.pack_bytes(self.pack))
        mf = M.MenuFont(G.Font(G.DiscLook(self.discs[0]), pack), menus[0])
        keys = [k for k in GAME.rows if M.context_of(k) == "parasite energy list" and pack.text(k)]
        self.assertEqual(keys, [M.pe_key(p) for p in range(20)])
        wrong, notes = {}, {}
        for k in keys:
            look = M.pe_list(mf, pack.text(k), k, None)
            x = look.text.placed[0].x - look.text.placed[0].letter.x
            if look.text.width != GAME.rows[k]["menu_px"] or look.text.width > 98:
                wrong[k] = (look.text.width, GAME.rows[k]["menu_px"])
            if look.notes or (x - 32) % 100 or look.focus[1] - look.focus[0] != 16:
                notes[k] = (look.notes, x, look.focus)
        self.assertEqual(wrong, {})
        self.assertEqual(notes, {})
        print(f"\n{len(keys)} English PE names drawn in the PE list at the manifest's widths")

    def test_every_box_row_is_drawn_in_its_box(self):
        """The boxes' and the config menu's windows and grids as both discs have them (BoxMenuDisc's
        numbers), and every English row of the menus' boxes and choices drawn: nothing left out, the line
        at the manifest's width (a confirm label: joined with its name), inside the screen, the box on
        the screen."""
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("needs Pillow")
        from translator import gamelook as G, menulook as M
        menus = [M.MenuDisc(d) for d in self.discs]
        for md in menus:
            for n, w in BoxMenuDisc.WINDOWS.items():
                self.assertEqual((md.windows[n], md.grids[n][:8]), (w, BoxMenuDisc.GRIDS[n]), hex(n))
        pack = G.Pack.read(pelang.pack_bytes(self.pack))
        mf = M.MenuFont(G.Font(G.DiscLook(self.discs[0]), pack), menus[0])
        lists = ("help line", "item list", "parasite energy list")
        keys = [k for k in GAME.rows if M.can_draw(k) and M.context_of(k) not in lists]
        self.assertEqual(len(keys), 52)
        unseen = {"sys/044", "sys/045"}          # MAX, max: the config list ends at Position
        wrong = {}
        for k in keys:
            look = M.look(mf, pack.text(k), k, None)
            ends = [p.x - p.letter.x for p in look.text.placed] + [p.x + p.letter.drawn for p in look.text.placed]
            label = M.context_of(k) == "confirm label" and k in M.confirm_of(k)[0] and len(M.confirm_of(k)[0]) > 1
            # (a label the game joins is shown whole: sys/073's first line is sys/071 and sys/073 together)
            whole = M.joined(*[pack.text(r) for r in M.confirm_of(k)[0]]) if label else None
            width = (M.measure(mf, M.split_line(mf, whole)[0]) if label else GAME.rows[k]["menu_px"])
            top, bottom = look.focus
            found = (look.notes if k not in unseen else [], look.text.width == width,
                     0 <= min(ends) and max(ends) <= 320, 0 <= top < bottom <= 224)
            if found != ([], True, True, True):
                wrong[k] = (found, look.text.width, width)
        self.assertEqual(wrong, {})
        print(f"\n{len(keys)} English rows of the menus' boxes and choices drawn in their boxes")

    def test_every_battle_line_is_drawn_on_the_battle_bar(self):
        """The first fight's room (M005, its third view) the same on both discs, and every English battle
        line drawn on the bar: one line from x 20, as long as the manifest measures it (the HP line with
        9999 in its two number slots, 12 px a digit, where the manifest counts five), inside the screen,
        nothing the preview can't draw."""
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("needs Pillow")
        from petext import battle
        from translator import battlelook as B, gamelook as G
        looks = [G.DiscLook(d) for d in self.discs]
        rooms = [d.views(B.ROOM) for d in looks]
        for views in rooms:
            self.assertEqual(views, rooms[0])
        self.assertEqual(len(rooms[0]), 3)
        pack = G.Pack.read(pelang.pack_bytes(self.pack))
        font = G.Font(looks[0], pack)
        wrong = {}
        for k in battle.LINES:
            for top in (False, True):
                look = B.look(font, pack.text(k), k, rooms[0][B.VIEW], top)
                placed = G.lay_out(font, pack.text(k), b"", battle.X, (B.TOP if top else B.BOTTOM)[1],
                                   numbers=B.NUMBERS.get(k, ())).screens[0]
                last = placed[-1]
                end = last.x - last.letter.x + last.letter.advance - battle.X
                row = GAME.rows[k]
                width = row["lines_px"][0] - 12 * (5 - 4) * len(row["structure"])
                found = (look.notes, placed[0].x - placed[0].letter.x, end == width)
                if found != ([], battle.X, True):
                    wrong[(k, top)] = (found, end, width)
        self.assertEqual(wrong, {})
        print(f"\n{len(battle.LINES)} English battle lines drawn on the battle bar, at the bottom and at the top")

    def test_the_window_shows_a_confirm_label_joined_with_the_item_s_name(self):
        """"As in the game" for "will be discarded": the confirm box with Medicine 1's name in the
        translator's words, and the game's own Cancel beside their OK."""
        if not window_works():
            self.skipTest("no screen for a window here")
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("needs Pillow")
        import time
        import tkinter as tk
        from translator import app as A, gamelook as G
        A.SETTINGS = self.dir / "box-settings.json"
        root = tk.Tk()
        try:
            window = A.App(root, None, str(self.dir / "box.pelang"), select="sys/004", new=("es", "Español"))
            window.load_english(str(self.pack))
            until = time.monotonic() + 60
            while (window.english_busy or not window.english) and time.monotonic() < until:
                root.update()
                time.sleep(0.02)
            self.assertIsNone(window.use_disc(self.discs[0], remember=False))
            window.tr.set_text("item/005", "Medicina 1")
            window.tr.set_text("sys/004", " se tirará")
            window.tr.set_text("sys/005", "Vale")
            window.preview = None
            window.check_now()
            until = time.monotonic() + 60
            while window.preview is None and time.monotonic() < until:
                root.update()
                time.sleep(0.02)
            # (compared as text: a pack's stretches are numbered by what else is in it)
            shown = P.read_pack_text(window.preview.pack, GAME).texts
            self.assertEqual({k: shown.get(k) for k in ("sys/004", "item/005", "sys/005", "sys/006")},
                             {"sys/004": " se tirará", "item/005": "Medicina 1", "sys/005": "Vale",
                              "sys/006": self.english["sys/006"]})
            window.look_mode.set("game")
            self.assertTrue(window.draw_game())
            texts = [window.canvas.itemcget(i, "text") for i in window.canvas.find_all()
                     if window.canvas.type(i) == "text"]
            self.assertTrue(any("a confirm box" in t for t in texts), texts)
        finally:
            root.destroy()

    def test_the_english_pack_reads_back_as_the_discs_own_text_every_row(self):
        self.assertEqual(self.wrong, 0)
        self.assertGreater(len(self.english), 2800)
        # (the city map's lines are shown as words, without the spaces that centre them)
        differ = [k for k, text in self.english.items() if text != citymap.shown_words(k, self.strings[k]["text"])]
        self.assertEqual(differ, [])
        self.assertEqual(len([k for k in self.english if citymap.placed(k)]), 14)
        print(f"\n{len(self.english)} rows of the English pack read back as the discs' text ({len(self.discs)} disc(s))")

    def test_the_window_with_the_english_pack_types_into_the_row_asked_for(self):
        """The English pack loads while the window is up and refills the list: the row being
        worked on must stay (it once jumped to the first row, and the typing landed there)."""
        if not window_works():
            self.skipTest("no screen for a window here")
        for n, key in enumerate(plain_dialogue(4)[1:]):
            saved, report = self.dir / f"window{n}.pelang", self.dir / f"window{n}.json"
            typed = f"Ligne {n}, tapée à l'essai."
            done = subprocess.run([sys.executable, "-I", str(START), str(saved), "--new", "fr", "Français",
                                   "--english", str(self.pack), "--select", key, "--type", typed,
                                   "--settings", str(self.dir / "settings.json"), "--report", str(report)],
                                  capture_output=True, text=True, timeout=300)
            said = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(done.returncode, 0, said)
            self.assertEqual((said["english_rows"], said["english_error"]), (len(self.english), None))
            self.assertEqual((said["row"], said["text_in_row"], said["translated"], said["row_state"]),
                             (key, typed, 1, "ok"))
            self.assertEqual(P.Translation.open(saved, GAME).texts, {key: typed})

    def test_the_window_shows_an_item_name_in_the_item_list_beside_the_other_names(self):
        """"As in the game" for an item's name: the item list, with the translator's words for the
        first items they have translated and the game's English for the others."""
        if not window_works():
            self.skipTest("no screen for a window here")
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("needs Pillow")
        import time
        import tkinter as tk
        from translator import app as A, gamelook as G
        A.SETTINGS = self.dir / "item-list-settings.json"
        root = tk.Tk()
        try:
            window = A.App(root, None, str(self.dir / "item-list.pelang"), select="item/200", new=("es", "Español"))
            window.load_english(str(self.pack))
            until = time.monotonic() + 60
            while (window.english_busy or not window.english) and time.monotonic() < until:
                root.update()
                time.sleep(0.02)
            self.assertIsNone(window.use_disc(self.discs[0], remember=False))
            window.tr.set_text("item/067", "Pistola M84F")
            window.tr.set_text("item/200", "Llave del ensayo")
            window.preview = None
            window.check_now()
            until = time.monotonic() + 60
            while window.preview is None and time.monotonic() < until:
                root.update()
                time.sleep(0.02)
            pack = G.Pack.read(window.preview.pack)
            alone = lambda key, text, lang: G.Pack.read(P.check_row(GAME, key, text, lang, "x").preview.pack).text(key)
            self.assertEqual(pack.text("item/067"), alone("item/067", "Pistola M84F", "es"))
            for key in ("item/149", "item/062", "item/025", "item/005", "desc/200"):
                self.assertEqual(pack.text(key), alone(key, self.english[key], "es"), key)
            window.look_mode.set("game")
            self.assertTrue(window.draw_game())
            texts = [window.canvas.itemcget(i, "text") for i in window.canvas.find_all()
                     if window.canvas.type(i) == "text"]
            self.assertTrue(any("the item list" in t for t in texts), texts)
        finally:
            root.destroy()

    def test_the_window_shows_a_pe_name_in_the_pe_list_beside_the_other_pes(self):
        """"As in the game" for a PE's name: the PE list, with the translator's words for the PEs they
        have translated and the game's English for the others; Liberate, on the last row, kept in view."""
        if not window_works():
            self.skipTest("no screen for a window here")
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("needs Pillow")
        import time
        import tkinter as tk
        from translator import app as A, gamelook as G
        A.SETTINGS = self.dir / "pe-list-settings.json"
        root = tk.Tk()
        try:
            window = A.App(root, None, str(self.dir / "pe-list.pelang"), select="item/254", new=("es", "Español"))
            window.load_english(str(self.pack))
            until = time.monotonic() + 60
            while (window.english_busy or not window.english) and time.monotonic() < until:
                root.update()
                time.sleep(0.02)
            self.assertIsNone(window.use_disc(self.discs[0], remember=False))
            window.tr.set_text("item/235", "Curar 1")
            window.tr.set_text("item/254", "Liberar")
            window.preview = None
            window.check_now()
            until = time.monotonic() + 60
            while window.preview is None and time.monotonic() < until:
                root.update()
                time.sleep(0.02)
            pack = G.Pack.read(window.preview.pack)
            alone = lambda key, text, lang: G.Pack.read(P.check_row(GAME, key, text, lang, "x").preview.pack).text(key)
            self.assertEqual(pack.text("item/235"), alone("item/235", "Curar 1", "es"))
            for key in ("item/236", "item/247", "item/253", "desc/254"):
                self.assertEqual(pack.text(key), alone(key, self.english[key], "es"), key)
            self.assertIsNone(pack.text("item/250"))            # a cut PE is not beside Liberate
            window.look_mode.set("game")
            shown, show = [], window.show_game_picture
            window.show_game_picture = lambda rgb, top, *rest: (shown.append(top), show(rgb, top, *rest))
            self.assertTrue(window.draw_game())
            texts = [window.canvas.itemcget(i, "text") for i in window.canvas.find_all()
                     if window.canvas.type(i) == "text"]
            self.assertTrue(any("the PE list" in t for t in texts), texts)
            # Liberate's cell is rows 146-161: the picture starts low enough to show it, whole
            self.assertEqual(len(shown), 1)
            self.assertLessEqual(shown[0], 146)
            self.assertGreaterEqual(shown[0] + A.VIEW, 162)
        finally:
            root.destroy()

    def test_the_row_being_worked_on_stays_when_the_english_pack_arrives(self):
        """The same, in that order for sure: a row picked and typed in, then the English pack loads."""
        if not window_works():
            self.skipTest("no screen for a window here")
        import time
        import tkinter as tk
        from translator import app as A
        A.SETTINGS = self.dir / "in-process-settings.json"
        root = tk.Tk()
        try:
            key = plain_dialogue(3)[2]
            window = A.App(root, None, str(self.dir / "in-process.pelang"), select=key, new=("fr", "Français"))
            root.update()
            self.assertEqual(window.key, key)
            window.edit.insert("end", "Avant l'anglais.")
            root.update()
            window.load_english(str(self.pack))
            until = time.monotonic() + 60
            while (window.english_busy or not window.english) and time.monotonic() < until:
                root.update()
                time.sleep(0.02)
            root.update()
            self.assertEqual(len(window.english), len(self.english))
            self.assertEqual((window.key, window.rows.selection()), (key, (key,)))
            self.assertEqual(window.edit.get("1.0", "end-1c"), "Avant l'anglais.")
            window.edit.insert("end", " Après.")
            root.update()
            self.assertEqual(window.tr.texts, {key: "Avant l'anglais. Après."})
        finally:
            root.destroy()

    def test_a_pack_that_is_not_the_games_english_is_told_apart(self):
        t = P.Translation(GAME, "fr", "French")
        for key in plain_dialogue(3):
            t.set_text(key, "Pas de l'anglais.")
        t.save(self.dir / "fr.pelang")
        self.assertEqual(P.read_english(self.dir / "fr.pelang", GAME)[1], 3)

    def test_the_whole_game_goes_through_the_tool(self):
        """Every row of the game translated into a made-up test language (pelang.py pseudo's:
        the English with its letters accented and the line about 30% longer), saved, reopened."""
        t = P.Translation(GAME, "qps", "Test language")
        for key, text in self.english.items():
            if GAME.writable(key) and key != "sys/030":
                t.set_text(key, pelang.pseudo(text))
        typed = dict(t.texts)
        self.assertGreater(len(typed), 2800)
        path = self.dir / "qps.pelang"
        report = t.save(path)
        self.assertEqual(report.whole, [])
        self.assertEqual(report.in_pack + len(report.left_out), len(typed))
        entries = pelang.read_pack(path)[2]
        for key in typed:
            self.assertEqual(pelang.fnv1a(key) in entries, key not in report.left_out, key)
        # nothing left out without a reason a person can act on, and nothing typed is lost
        self.assertTrue(all(why and why[0] for why in report.left_out.values()))
        again = P.Translation.open(path, GAME)
        self.assertTrue(again.from_work)
        self.assertEqual(again.texts, typed)
        self.assertEqual(again.locked, {})
        # saved again untouched: the same file
        before = path.read_bytes()
        again.save()
        self.assertEqual(path.read_bytes(), before)
        # and it is the pack the command line writes from the rows that pass
        command_line_build({k: v for k, v in typed.items() if k not in report.left_out}, "qps", "Test language",
                           self.dir / "cli.bin")
        self.assertEqual(pelang.pack_bytes(path), (self.dir / "cli.bin").read_bytes())
        reasons = {}
        for why in report.left_out.values():
            reasons[why[0].split(";")[0][:60]] = reasons.get(why[0].split(";")[0][:60], 0) + 1
        print(f"\nwhole game in a test language: {report.in_pack} rows in the pack, {len(report.left_out)} left "
              f"out for being too long for their room; reopened as typed")

    def test_every_dialogue_line_of_the_game_can_be_drawn(self):
        """The preview of each English dialogue page: its widths are the layout check's."""
        t = P.Translation.open(self.pack, GAME)
        entries = pelang.read_pack(self.pack)[2]
        seen = 0
        for key in sorted(k for k in self.english if k.startswith(("dlg/M001/", "dlg/M026/", "battle/"))):
            r = P.check_row(GAME, key, self.english[key], "en")
            if r.preview is None:
                self.assertTrue(r.errors, key)
                continue
            if not r.preview.wrapped:
                raw = P.split_page_end(entries[pelang.fnv1a(key)])[0] if key.startswith("dlg/") else entries[pelang.fnv1a(key)]
                self.assertEqual([s.widths for s in r.preview.screens], layout.screens(raw), key)
            seen += 1
        self.assertGreater(seen, 200)
        # The game's own pages that run past their box: the strict rules would rewrap five, so the
        # pack keeps the looser rules (as a fan patch's pack does) and writes back as it is.
        self.assertEqual((t.from_patch, t.locked), (True, {}))
        t.from_patch, t.patch_text = False, {}
        strict = t.untouched_changes()
        self.assertEqual(len(strict), 5, "the five English pages that run past their own box")
        self.assertTrue(all(k.startswith("dlg/M044/") for k in strict))

    def test_the_city_maps_lines_typed_as_words_land_where_the_english_has_them(self):
        """The English's own words for the 14 map lines, typed as words only: the compiler's
        centring puts most lines exactly where the English has them, and none more than a space away
        (the English placed them by eye: citymap.CENTRE)."""
        keys = sorted(k for k in self.english if citymap.placed(k))
        out = self.dir / "map-words.bin"
        command_line_build({k: self.english[k] for k in keys}, "en", "Words", out)
        built = pelang.read_pack(out)[2]
        exact = lines = 0
        for key in keys:
            mine, theirs = built[pelang.fnv1a(key)].split(b"\xf7"), bytes.fromhex(self.strings[key]["raw"]).split(b"\xf7")
            self.assertEqual(len(mine), len(theirs), key)
            for a, b in zip(mine, theirs):
                lines += 1
                exact += a == b
                self.assertEqual(a.lstrip(b"\x0f"), b.lstrip(b"\x0f"), key)       # the words are the English's
                spaces = len(a) - len(a.lstrip(b"\x0f")) - (len(b) - len(b.lstrip(b"\x0f")))
                self.assertLessEqual(abs(spaces), 1 if a.lstrip(b"\x0f") else 2, (key, a, b))
        print(f"\nthe English map lines typed as words: {exact} of {lines} lines placed exactly as the English")
        self.assertGreaterEqual(exact, 32)


class CityMapRuleTest(unittest.TestCase):
    """The city map's lines (dlg/MAP, petext/citymap.py), with no disc: typed as words, centred by
    the compiler; a line with its own spacing kept; a file still carrying them as bytes flagged."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    @staticmethod
    def spaces(words: str, kind: str) -> int:
        width = gt.lines(gt.encode(words, extended=True))[0]
        return max(0, (2 * citymap.CENTRE[kind] - width + 7) // 14)

    def test_the_map_lines_and_bank_are_in_the_manifest(self):
        keys = sorted(k for k in GAME.keys if k.startswith("dlg/MAP/"))
        self.assertEqual(keys, [f"dlg/MAP/{p:03d}" for p in range(15)])
        self.assertEqual([citymap.placed(k) for k in keys], [None] + ["title"] * 10 + ["narration"] * 4)
        self.assertIn({"key": "MAP", "crc32": "fc4679c2"}, GAME.banks)
        self.assertTrue(all(GAME.rows[k]["boxes"] == ["default"] for k in keys))

    def test_a_line_typed_as_words_is_centred_and_a_title_goes_to_the_third_line(self):
        t = P.Translation(GAME, "es", "Prueba")
        t.set_text("dlg/MAP/005", "Barrio Soho")
        t.set_text("dlg/MAP/002", "Comisaría{br}número 17")
        t.set_text("dlg/MAP/012", "La gente lo olvidará{br}y seguirá con su vida.{fb07 C8}")
        built, left, _ = t.compile()
        self.assertTrue(built.ok, built.said[-400:])
        self.assertEqual(left, {})
        texts = P.read_pack_text(built.pack, GAME).texts
        pad = lambda w, kind: " " * self.spaces(w, kind) + w
        self.assertEqual(texts["dlg/MAP/005"], "{br}{br}" + pad("Barrio Soho", "title"))
        self.assertEqual(texts["dlg/MAP/002"], "{br}" + pad("Comisaría", "title") + "{br}" + pad("número 17", "title"))
        self.assertEqual(texts["dlg/MAP/012"], pad("La gente lo olvidará", "narration") + "{br}"
                         + pad("y seguirá con su vida.", "narration") + "{fb07 C8}")
        # the preview calls none of that a change to the words
        check = t.check_row("dlg/MAP/005")
        self.assertEqual((check.errors, check.warnings, check.preview.wrapped), ([], [], False))
        # and the command line writes the same pack from the same words
        command_line_build(dict(t.texts), "es", "Prueba", self.dir / "cli.bin")
        self.assertEqual(built.pack, (self.dir / "cli.bin").read_bytes())

    def test_a_line_with_its_own_spacing_is_kept_until_its_words_change(self):
        """A pack whose map line has spacing of its own (a fan patch's): the Translator shows the
        words, saves the line exactly as the pack had it, also after reopening, and centres it only
        once the words change."""
        own = "{br}  Soho{br}  New York"                      # not where the tool would put it
        path = self.dir / "own.pelang"
        command_line_build({"dlg/MAP/005": own}, "es", "Prueba", path)
        t = P.Translation.open(path, GAME)
        self.assertEqual((t.text("dlg/MAP/005"), t.layouts), ("Soho{br}New York", {"dlg/MAP/005": own}))
        self.assertEqual(t.locked, {})
        saved = self.dir / "saved.pelang"
        t.save(saved)
        self.assertEqual(pelang.pack_bytes(saved), pelang.pack_bytes(path))
        again = P.Translation.open(saved, GAME)
        self.assertTrue(again.from_work)
        self.assertEqual((again.text("dlg/MAP/005"), again.layouts["dlg/MAP/005"]), ("Soho{br}New York", own))
        again.save(self.dir / "twice.pelang")
        self.assertEqual(pelang.pack_bytes(self.dir / "twice.pelang"), pelang.pack_bytes(path))
        again.set_text("dlg/MAP/005", "SoHo{br}New York")
        again.save(self.dir / "changed.pelang")
        text = P.read_pack_text(pelang.pack_bytes(self.dir / "changed.pelang"), GAME).texts["dlg/MAP/005"]
        self.assertEqual(text, "{br}" + " " * self.spaces("SoHo", "title") + "SoHo{br}"
                         + " " * self.spaces("New York", "title") + "New York")
        reopened = P.Translation.open(self.dir / "changed.pelang", GAME)
        self.assertEqual((reopened.text("dlg/MAP/005"), reopened.layouts), ("SoHo{br}New York", {}))

    def test_a_file_still_carrying_the_map_as_bytes_is_flagged_on_its_map_lines(self):
        path = self.dir / "pack.bin"
        command_line_build({"dlg/MAP/004": "Parque Central"}, "es", "Prueba", path)
        for sector, flagged in ((citymap.USA_SECTORS[0], True), (citymap.USA_SECTORS[1] - 1, True),
                                (citymap.USA_SECTORS[1], False)):
            with self.subTest(sector=sector):
                bundle = self.dir / f"b{sector}.pelang"
                with zipfile.ZipFile(bundle, "w") as z:
                    z.writestr("text.bin", path.read_bytes())
                    z.writestr("pictures.bin", picturebin.write([picturebin.Piece(sector, 428, 0, b"\xfe\x01")]))
                t = P.Translation.open(bundle, GAME)
                self.assertEqual(t.map_as_bytes, flagged)
                self.assertEqual(P.MAP_AS_BYTES in t.check_row("dlg/MAP/004").warnings, flagged)
                self.assertNotIn(P.MAP_AS_BYTES, t.check_row(plain_dialogue(1)[0], "Hola.").warnings)

    def test_only_a_change_to_the_map_entrys_text_is_left_to_the_text(self):
        """petext/citymap.only_text_changed: the banks and table may change (a patch's bank of another
        size moves what follows); the parts after them may only move."""
        table = lambda parts: b"".join(p.to_bytes(4, "little") for p in parts)
        art = bytes(range(256)) * 4
        english = table((16, 32, 64, 64 + 512)) + b"\x11" * 16 + b"\x22" * 32 + art + b"\0" * 64
        moved = table((16, 32, 48, 48 + 512)) + b"\x33" * 16 + b"\x44" * 16 + art + b"\0" * 80
        self.assertTrue(citymap.only_text_changed(english, moved))
        drawn = bytearray(moved)
        drawn[100] ^= 1
        self.assertFalse(citymap.only_text_changed(english, bytes(drawn)))
        with self.assertRaises(ValueError):
            citymap.parts(table((16, 64, 32, 600)) + art)


class CityMapDiscTest(unittest.TestCase):
    """The city map's entry on the real discs (PE_TEST_DISC, PE_TEST_DISC2)."""

    def test_the_map_entry_and_bank_are_where_the_translator_expects_them_on_each_disc(self):
        from petext.disc import Game
        discs = [d for d in (os.environ.get("PE_TEST_DISC"), os.environ.get("PE_TEST_DISC2")) if d and Path(d).is_file()]
        if not discs:
            self.skipTest("needs PE_TEST_DISC (USA disc 1); PE_TEST_DISC2 adds disc 2")
        bank = next(b for b in GAME.banks if b["key"] == citymap.BANK_KEY)
        for path in discs:
            with self.subTest(disc=path):
                g = Game(path)
                self.addCleanup(g.disc.f.close)
                self.assertEqual((g.sectors[citymap.ENTRY], g.sectors[citymap.ENTRY + 1]), citymap.USA_SECTORS)
                entry = citymap.entry_bytes(g)
                pages, used = gt.pages(citymap.english_bank(entry))
                self.assertEqual(f"{zlib.crc32(citymap.english_bank(entry)[:used]):08x}", bank["crc32"])
                self.assertEqual(sorted(pages), list(range(15)))
                self.assertEqual(citymap.parts(entry)[:2], (16, 428))
                self.assertTrue(citymap.only_text_changed(entry, entry))


def player_packs() -> list[Path]:
    folder = os.environ.get("PE_TEST_PLAYER_PACKS")
    return sorted(Path(folder).glob("*.pelang")) if folder and Path(folder).is_dir() else []


def new_bank_names(entries: dict[int, bytes]) -> set[int]:
    """The bank names (bank/<CRC> -> its key) the manifest has that a pack made before them lacks:
    the build writes every bank's name, so saving such a pack adds those entries and nothing else
    (the city map's bank came after the player packs were first made)."""
    return {pelang.fnv1a(f"bank/{b['crc32']}") for b in GAME.banks} - set(entries)


def assert_same_but_new_bank_names(test, saved: Path, original: Path) -> None:
    """The file's parts as the original's, its text.bin's entries too but for new_bank_names."""
    a, b = zip_parts(saved), zip_parts(original)
    test.assertEqual({k: v for k, v in a.items() if k != "text.bin"}, {k: v for k, v in b.items() if k != "text.bin"})
    was, now = pelang.read_pack(original)[2], pelang.read_pack(saved)[2]
    if not new_bank_names(was):
        test.assertEqual(a["text.bin"], b["text.bin"])
        return
    test.assertEqual({h: v for h, v in now.items() if h in was}, was)
    test.assertEqual(set(now) - set(was), new_bank_names(was))


def zip_parts(path: Path) -> dict[str, bytes]:
    """A .pelang's parts but the tool's work part (a plain pack: just its text)."""
    data = path.read_bytes()
    if not data.startswith(b"PK\x03\x04"):
        return {pelang.BUNDLE_TEXT: data}
    with zipfile.ZipFile(path) as z:
        return {i.filename: z.read(i) for i in z.infolist() if i.filename != P.WORK}


def a_font_for(chars: str) -> str | None:
    """A CJK font file on this computer with all of `chars`, or None (with Pillow, which drawing
    from a font needs)."""
    try:
        import PIL  # noqa: F401
    except ImportError:
        return None
    from petext import cjk
    for path in ("C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simsun.ttc",
                 "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                 "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
                 "/System/Library/Fonts/PingFang.ttc", "/System/Library/Fonts/STHeiti Light.ttc",
                 "/System/Library/Fonts/Hiragino Sans GB.ttc"):
        if Path(path).is_file() and {ord(c) for c in chars} <= cjk._coverage(path, {ord(c) for c in chars}):
            return path
    return None


def the_window_has_pillow() -> bool:
    """Whether the scripted window can load Pillow. run_window starts it with `python -I`, which
    hides the user's own site-packages, so a Pillow installed there (pip install --user) is seen by
    the tests but not by the window."""
    return subprocess.run([sys.executable, "-I", "-c", "import PIL"], capture_output=True).returncode == 0


def one_word_changed(text: str) -> str:
    """The text with one letter doubled in its first word of four letters or more (any script),
    outside its tags ({name}, {wait}...)."""
    pieces = re.split(r"(\{[^}]*\})", text)
    for n, piece in enumerate(pieces):
        if not piece.startswith("{"):
            changed = re.sub(r"(\w)(\w{3,})", lambda m: m.group(1) * 2 + m.group(2), piece, count=1)
            if changed != piece:
                return "".join(pieces[:n] + [changed] + pieces[n + 1:])
    raise ValueError("no word to change")


def png_bytes(img) -> bytes:
    out = io.BytesIO()
    img.save(out, "PNG")
    return out.getvalue()


def made_up_tim(bits: int, width: int, height: int, palette=None, pad: int = 0) -> bytes:
    """A TIM of the test's own: pixels counting up, `pad` extra bytes at each row's end (24-bit)."""
    import struct
    flags = {4: 0, 8: 1, 24: 3}[bits] | (8 if palette else 0)
    out = struct.pack("<II", 0x10, flags)
    if palette:
        out += struct.pack("<IHHHH", 12 + len(palette) * 2, 0, 480, len(palette), 1) + struct.pack(f"<{len(palette)}H", *palette)
    row = {4: width // 2, 8: width, 24: width * 3 + pad}[bits]
    pixels = bytes((y * 7 + x) % 251 for y in range(height) for x in range(row))
    return out + struct.pack("<IHHHH", 12 + len(pixels), 0, 0, row // 2, height) + pixels


def made_up_card(grey_rows: int = 28):
    """A card's bytes and layout of the test's own: 28 colour rows (480-507), the first `grey_rows` of
    them 256 greys from dark (colour 0, see-through, first) and the rest with white and pure red too;
    300 tiles on the 20 x 15 grid, all on row 480, pixel numbers counting up."""
    import struct
    from translator import artwork as A
    rows = bytearray(256 * 32 * 2)
    for r in range(32):
        for i in range(256):
            grey = i * 31 // 255
            colour = grey | grey << 5 | grey << 10
            if r >= grey_rows and i == 254:
                colour = 0x7FFF
            if r >= grey_rows and i == 255:
                colour = 31
            struct.pack_into("<H", rows, (r * 256 + i) * 2, colour)
    tiles, layout = bytearray(), A.CardLayout([], tuple(range(480, 508)))
    for k in range(300):
        x, y = k % 20 * 16, k // 20 * 16
        layout.tiles.append((x, y))
        tiles += struct.pack("<H", 0x8000 | 480) + bytes((k + i) % 200 + 20 for i in range(256))
    return bytes(rows) + bytes(tiles), layout


TUTORIAL_GREEN = 253                # in the made-up colour rows: pure green (the menu's ring), white, red
SHADOW_ROW = 500                   # the made-up shadow's colour row: greys with the see-through bit


def made_up_tutorial_rows() -> bytes:
    """32 colour rows: even rows greys, odd rows greys turning blue (the made-up layers use even rows), row 500 the shadow's dark greys with
    the see-through bit (drawn subtracted), and in rows 508-511 pure green, white and red at 253-255."""
    import struct
    rows = bytearray(256 * 32 * 2)
    for r in range(32):
        for i in range(1, 256):
            g = i * 31 // 255
            colour = g | g << 5 | (g if r % 2 == 0 else min(31, g + 6)) << 10
            if r + 480 == SHADOW_ROW:
                colour = 0x8000 | min(31, i // 8) * 0x421
            if r >= 28 and i >= TUTORIAL_GREEN:
                colour = {TUTORIAL_GREEN: 31 << 5, 254: 0x7FFF, 255: 31}[i]
            struct.pack_into("<H", rows, (r * 256 + i) * 2, colour)
    return bytes(rows)


def _tutorial_roles(pic):
    """(layer, role) for each of a real Tutorial screen's layers and copies: the shapes of the real ones."""
    out = [(pic.layers[0][0], "back"), (pic.layers[1][0], "shadow")]
    for e, _, _ in pic.layers[2:]:
        if pic.copies:
            out.append((e, "menu"))
        elif e in pic.words:
            out.append((e, "strip" if e == pic.words[0] else "panel"))
        elif pic.room in (354, 355):
            out.append((e, "controller" if e == pic.layers[2][0] else "close-up"))
        else:
            out.append((e, "picture"))
    return out + [(e, f"menu {k + 1}") for k, e in enumerate(pic.copies)]


def made_up_tutorial():
    """Every Tutorial screen made up at its real shape, no disc: {id: (base, room bytes, segments, layout)}.
    The screens of a room share one room's bytes, so a layer two screens show (the Tune-up screenshot) is
    one set of bytes, as on the disc. Layers: the picture behind (20x14 tiles, depth 4088), the shadow
    (14x10 at 61,4, depth 1219, subtracted), the words (20x5 at 0,152; depth 1243, under the shadow, in
    M362 and M366), panels and the menu (13x9 at 60,4, their right edge see-through), the controller
    (depth 24) and close-ups (7x5 at 160,40). The menu's copy k has a 2-pixel green ring around line k
    and a few specks a step lighter all over (as the game's copies have)."""
    import struct
    from translator import artwork as A
    shapes = {"back": (0, 0, 20, 14, 4088), "shadow": (61, 4, 14, 10, 1219), "strip": (0, 152, 20, 5, 8),
              "panel": (60, 4, 13, 9, 8), "menu": (60, 4, 13, 9, 8), "controller": (60, 4, 13, 9, 24),
              "close-up": (160, 40, 7, 5, 8), "picture": (60, 4, 13, 9, 8)}
    rows, rooms, out = made_up_tutorial_rows(), {}, {}
    for pic in A.PICTURES:
        if not isinstance(pic, A.Screen):
            continue
        blob, placed = rooms.setdefault(pic.room, (bytearray(rows), {}))
        moves = {e: (dx, dy) for e, dx, dy in pic.layers}
        segments, layers, at = [(0, len(rows))], [], len(rows)
        for e, role in _tutorial_roles(pic):
            kind = "menu" if role.startswith("menu") else role
            x0, y0, cols, lines, depth = shapes[kind]
            if kind == "strip":
                y0 = 151 if pic.room == 366 else 152
                depth = 1243 if pic.room in (362, 366) else 8
            if e not in placed:
                tiles = []
                for t in range(cols * lines):
                    x, y = x0 + t % cols * 16, y0 + t // cols * 16
                    row = SHADOW_ROW if kind == "shadow" else 508 + 2 * (t % 2) if kind == "menu" else 480 + 2 * ((t + e) % 14)
                    like = pic.words[0] if role.startswith("menu ") else e     # a copy is the menu, ringed
                    numbers = bytearray(((like * 37 + pic.room + t * 3 + i) % 220) + 20 for i in range(256))
                    if kind == "shadow":
                        numbers = bytearray((t + i) % 64 + 8 for i in range(256))
                    if kind in ("panel", "menu", "picture", "controller") and t % cols == cols - 1:
                        for i in range(256):
                            if i % 16 >= 12:
                                numbers[i] = 0                      # the panel's see-through right edge
                    if role.startswith("menu "):
                        k = int(role[5:])
                        for i in range(256):
                            px, py = x + i % 16, y + i // 16
                            band = 40 + k * 20
                            if (70 <= px <= 250 and py in (band, band + 1, band + 14, band + 15)) or \
                                    (band <= py <= band + 15 and px in (70, 71, 249, 250)):
                                numbers[i] = TUTORIAL_GREEN
                            elif (px * 7 + py * 3 + k) % 41 == 0 and 0 < numbers[i] < 200:
                                numbers[i] += 8                     # a speck one step lighter: not the ring
                    tiles.append((x, y, len(blob)))
                    blob += struct.pack("<H", row) + numbers
                placed[e] = (depth, kind == "shadow", tiles)
            depth, shadow, tiles = placed[e]
            dx, dy = moves.get(e, moves.get(pic.words[0], (0, 0)))
            layers.append(A.Layer(e, depth, shadow, e in pic.words, not role.startswith("menu "),
                                  [(x + dx, y + dy) for x, y, _ in tiles], at))
            segments += [(off, 258) for _, _, off in tiles]
            at += 258 * len(tiles)
        out[pic.id] = (pic.room, segments, A.ScreenLayout(layers, tuple(range(480, 512))))
    final = {room: bytes(blob + bytes(-len(blob) % 2048)) for room, (blob, _) in rooms.items()}
    return {pid: ((room - 300) * 1000 * 2048, final[room], segments, layout)
            for pid, (room, segments, layout) in out.items()}


class TutorialDisc:
    """artwork.Disc's parts for the made-up Tutorial screens (made_up_tutorial), no disc."""

    def __init__(self, path="made-up.cue"):
        self.path = Path(path)
        self.english, self.around, self.segments, self.layout = {}, {}, {}, {}
        self.add_tutorial()

    def add_tutorial(self):
        for pid, (base, data, segments, layout) in made_up_tutorial().items():
            self.around[pid], self.segments[pid], self.layout[pid] = (base, data), segments, layout
            self.english[pid] = b"".join(data[a:a + n] for a, n in segments)


def tutorial_value(data: bytes, layout, entry: int, x: int, y: int) -> int:
    """The 15-bit colour layer `entry` (drawn or a copy) has at screen (x, y), from the screen's bytes."""
    import struct
    layer = next(lay for lay in layout.layers if lay.entry == entry)
    for t, (tx, ty) in enumerate(layer.tiles):
        if tx <= x < tx + 16 and ty <= y < ty + 16:
            at = layer.at + t * 258
            row = struct.unpack_from("<H", data, at)[0] & 511
            n = data[at + 2 + (y - ty) * 16 + x - tx]
            return struct.unpack_from("<H", data, ((row - 480) * 256 + n) * 2)[0]
    raise KeyError((entry, x, y))


def subtracted(under: int, shadow: int) -> int:
    return sum(max(0, (under >> s & 31) - (shadow >> s & 31)) << s for s in (0, 5, 10))


def made_up_glyph_page(width: int = 120, height: int = 24, rect=(448, 0, 64, 254), before: int = 1):
    """(an entry, a resident pack, where the page's pixels start in the entry) of the test's own, laid out as
    the game's: the entry a scene blob whose raw image list holds `before` small images and then the page
    (VRAM `rect`), pixels counting up; the pack's glyph table with glyph 0x8B at u 0 v 184, `width` x
    `height`, colour row 448,233."""
    import struct
    from translator import artwork as A
    images = b""
    for k in range(before):                                         # 2 words x 1 row elsewhere
        images += struct.pack("<I4H", 16, 900 + 2 * k, 500, 2, 1) + b"\x01\x02\x03\x04"
    page_at = 16 + len(images) + 12
    x, y, w, h = rect
    pixels = bytes((k * 7) % 251 for k in range(w * h * 2))
    images += struct.pack("<I4H", 12 + len(pixels), x, y, w, h) + pixels + b"\0\0\0\0"
    sub = 16 + len(images)
    blob = struct.pack("<II", 0, sub) + bytes(8) + images
    blob += bytes(8) + struct.pack("<I", 1 << 22 | (sub + 12)) + struct.pack("<3I", 0, 16, A.RAW_IMAGES)
    resident = bytearray(0x40 + 8 * 0x90)
    struct.pack_into("<i", resident, A.GLYPHS, 0x40)
    struct.pack_into("<BBHBBBB", resident, 0x40 + 8 * 0x8B, 0, 184, 233 << 6 | 448 >> 4, width, height, 0, 0)
    return blob, bytes(resident), page_at


def made_up_glyph(width: int = 8, height: int = 3, colours=None):
    """A Glyph of the test's own and its bytes: pixels counting through the 16 colours, then the colours."""
    import struct
    from translator import artwork as A
    pic = A.Glyph("test/glyph", "Test", "Glyph", 9, 0x8B, (448, 0), (width, height), (100, 100), 0, A.GAME_OVER_TIP)
    colours = colours or [0] + [k | (31 - k) << 5 | 8 << 10 for k in range(2, 32, 2)]
    numbers = [k % 16 for k in range(width * height)]
    data = bytes(numbers[k] | numbers[k + 1] << 4 for k in range(0, len(numbers), 2)) + struct.pack("<16H", *colours)
    return pic, data


class FakeDisc:
    """artwork.Disc's parts for one made-up picture at PE.IMG byte 3000 of sectors 1-3: no disc."""

    def __init__(self, tim: bytes) -> None:
        from translator import artwork as A
        self.pic = A.Picture("test/one", "Test", "One", 0, 3000 - 2048, len(tim), 0, A.LIGHTEN)
        self.sectors = [1]
        data = bytearray(bytes(range(256)) * 24)          # sectors 1-3
        data[3000 - 2048:3000 - 2048 + len(tim)] = tim
        self.english = {self.pic.id: tim}
        self.around = {self.pic.id: (2048, bytes(data))}

    def span(self, pic):
        return 3000, 3000 + pic.length


class PictureRuleTest(unittest.TestCase):
    """Pictures with no disc: made-up TIMs. A picture read out and written back is the same bytes;
    a PNG out and back in is the same picture; a picture loaded keeps every other piece."""

    def setUp(self):
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("needs Pillow")

    def test_a_picture_read_out_and_written_back_is_the_same_bytes(self):
        from petext import tim as T
        for data in (made_up_tim(24, 10, 4, pad=2), made_up_tim(4, 8, 3, palette=list(range(0, 32 * 16, 32))),
                     made_up_tim(8, 6, 2, palette=list(range(256)))):
            t = T.read(data + b"after")
            self.assertEqual(t.data, data)
            again = T.with_rgb(t, T.rgb(t)) if t.bits == 24 else T.with_indices(t, T.indices(t))
            self.assertEqual(again, data)
        t = T.read(made_up_tim(24, 10, 4, pad=2))
        new = T.with_rgb(t, bytes(len(T.rgb(t))))
        self.assertEqual([new[t.pixels_at + y * t.row_bytes + 30:t.pixels_at + (y + 1) * t.row_bytes] for y in range(4)],
                         [t.data[t.pixels_at + y * t.row_bytes + 30:t.pixels_at + (y + 1) * t.row_bytes] for y in range(4)])
        self.assertEqual(T.rgb_of(0x7FFF), (255, 255, 255))
        self.assertEqual(len({T.rgb_of(v) for v in range(32)}), 32)

    def test_a_png_out_and_back_in_is_the_same_picture(self):
        from PIL import Image
        from translator import artwork as A
        pic = A.Picture("t/p", "T", "P", 0, 0, 0, 0, A.SIXTEEN)
        palette = [0, 0x7FFF, 0x001F, 0x03E0, 0x7C00, 0x7FFF] + [0x4210] * 10      # white twice
        for data in (made_up_tim(24, 10, 4, pad=2), made_up_tim(4, 8, 3, palette=palette)):
            png = A.png(pic, data)
            self.assertEqual(A.from_png(pic, data, png).tim, data)
            rgb = io.BytesIO()
            Image.open(io.BytesIO(png)).convert("RGB").save(rgb, "PNG")
            back = A.from_png(pic, data, rgb.getvalue())
            self.assertEqual((back.tim, back.notes), (data, []))

    def test_a_png_that_does_not_fit_is_told_plainly_or_fitted(self):
        from PIL import Image
        from petext import tim as T
        from translator import artwork as A
        pic = A.Picture("t/p", "T", "P", 0, 0, 0, 0, A.SIXTEEN)
        data = made_up_tim(4, 8, 3, palette=[0, 0x7FFF] + [0x001F] * 14)
        out = io.BytesIO()
        Image.new("RGB", (9, 3)).save(out, "PNG")
        with self.assertRaisesRegex(ValueError, "must stay 8×3"):
            A.from_png(pic, data, out.getvalue())
        with self.assertRaisesRegex(ValueError, "not a picture"):
            A.from_png(pic, data, b"not a png")
        out = io.BytesIO()
        Image.new("RGB", (8, 3), (250, 240, 250)).save(out, "PNG")              # near white, not white
        loaded = A.from_png(pic, data, out.getvalue())
        self.assertEqual(set(T.indices(T.read(loaded.tim))), {1})
        self.assertIn("24 pixels were not one of the picture's 16 colours", " ".join(loaded.notes))
        out = io.BytesIO()
        Image.new("RGBA", (8, 3), (255, 255, 255, 0)).save(out, "PNG")          # all see-through
        loaded = A.from_png(pic, data, out.getvalue())
        self.assertEqual(set(T.indices(T.read(loaded.tim))), {0})
        self.assertIn("See-through parts were drawn as black.", loaded.notes)

    def test_a_picture_whose_colours_a_translation_changed_keeps_them(self):
        """The German and Chinese packs changed the map's colours, not only its pixels: a PNG loaded
        over such a picture changes its pixels and keeps the translation's colours (it put the English
        colours back until 2026-11-13)."""
        from PIL import Image, ImageDraw
        from petext import tim as T
        from translator import artwork as A
        pic = A.Picture("t/p", "T", "P", 0, 0, 0, 0, A.MAP)
        english = made_up_tim(4, 8, 3, palette=[0, 0x7FFF, 0x001F] + [0x4210] * 13)
        theirs = bytearray(english)
        theirs[24:26] = (0x03E0).to_bytes(2, "little")            # palette number 2: blue -> green
        theirs = bytes(theirs)
        self.assertEqual(T.read(theirs).palettes[0][2], 0x03E0)
        self.assertEqual(A.from_png(pic, english, A.png(pic, theirs), theirs).tim, theirs)    # unchanged
        img = Image.open(io.BytesIO(A.png(pic, theirs))).convert("RGB")
        ImageDraw.Draw(img).point((0, 0), fill=T.rgb_of(0x03E0))   # one pixel in their green
        out = io.BytesIO()
        img.save(out, "PNG")
        loaded = A.from_png(pic, english, out.getvalue(), theirs)
        self.assertEqual(T.read(loaded.tim).palettes, T.read(theirs).palettes)
        self.assertEqual(T.indices(T.read(loaded.tim))[0], 2)
        self.assertEqual(loaded.notes, [])

    def test_a_card_out_as_png_and_back_is_the_same_bytes(self):
        """A card's PNG is what the screen shows, the view's rows 4-227; loaded back unchanged it is the
        same bytes, and a pixel changed back to what it was changes nothing either."""
        from translator import artwork as A
        card, layout = made_up_card()
        pic = A.Card("t/c", "T", "C", 1, 0, 0, A.CARD)
        img = A.image(pic, card, layout)
        self.assertEqual(img.size, (320, 224))
        # Tile 0's row 4 (its fifth) is the screen's first row; tile (0, 224)'s row 3 its last.
        self.assertEqual(img.getpixel((0, 0)), A.screen_rgb(card[16384 + 2 + 4 * 16] * 31 // 255 * 0x421))
        last = 280 * 258 + 16384 + 2 + 3 * 16
        self.assertEqual(img.getpixel((0, 223)), A.screen_rgb(card[last] * 31 // 255 * 0x421))
        self.assertEqual(A.from_png(pic, card, A.png(pic, card, layout), None, layout), A.Loaded(card, []))

    def test_a_card_square_painted_in_a_colour_it_lacks_takes_a_row_that_has_it(self):
        """White and red painted into one square whose row has neither: that square takes a row with
        both and its pixels are exactly the PNG's; every other square, the squares off the screen and
        the rows above the screen keep their bytes."""
        import struct
        from PIL import Image, ImageDraw
        from translator import artwork as A
        card, layout = made_up_card(grey_rows=27)               # row 507 alone has white and red
        pic = A.Card("t/c", "T", "C", 1, 0, 0, A.CARD)
        img = A.image(pic, card, layout)
        draw = ImageDraw.Draw(img)
        draw.rectangle((32, 12, 47, 27), fill=(255, 255, 255))   # the square at view (32, 16): screen y 12
        draw.line((32, 20, 47, 20), fill=(255, 0, 0))
        loaded = A.from_png(pic, card, png_bytes(img), None, layout)
        self.assertEqual(A.image(pic, loaded.tim, layout).tobytes(), img.tobytes())
        k = 22                                                    # 20 tiles a row: (32, 16) is tile 22
        at = 16384 + k * 258
        self.assertEqual(struct.unpack_from("<H", loaded.tim, at)[0], 0x8000 | 507)
        self.assertEqual(loaded.tim[:at] + loaded.tim[at + 258:], card[:at] + card[at + 258:])
        self.assertEqual(loaded.notes, ["1 of the card's squares changed; 1 took another set of colours."])

    def test_a_card_square_whose_row_still_fits_keeps_it(self):
        """A grey drawn into a square, a colour its own row has: the square keeps its row (a tie goes to
        its own), and only the pixels drawn change their numbers."""
        import struct
        from PIL import ImageDraw
        from translator import artwork as A
        card, layout = made_up_card()
        pic = A.Card("t/c", "T", "C", 1, 0, 0, A.CARD)
        img = A.image(pic, card, layout)
        ImageDraw.Draw(img).point((100, 100), fill=A.screen_rgb(0x4210))
        loaded = A.from_png(pic, card, png_bytes(img), None, layout)
        self.assertEqual(A.image(pic, loaded.tim, layout).tobytes(), img.tobytes())
        changed = [i for i, (a, b) in enumerate(zip(card, loaded.tim)) if a != b]
        self.assertEqual(len(changed), 1, changed)
        self.assertEqual(struct.unpack_from("<H", loaded.tim, 16384 + (6 * 20 + 6) * 258)[0], 0x8000 | 480)

    def test_a_card_png_of_another_size_is_refused(self):
        from PIL import Image
        from translator import artwork as A
        card, layout = made_up_card()
        pic = A.Card("t/c", "T", "C", 1, 0, 0, A.CARD)
        with self.assertRaisesRegex(ValueError, "320×224"):
            A.from_png(pic, card, png_bytes(Image.new("RGB", (320, 240))), None, layout)

    def test_a_tutorial_screen_is_its_layers_drawn_in_the_games_order(self):
        """Deepest first; the shadow subtracted from what is under it; colour 0 see-through; the words under
        the shadow where they are deeper (M362), over it elsewhere (M361); the menu's layers moved, and a
        choice drawn with its ringed copy."""
        from translator import artwork as A
        disc = TutorialDisc()

        def screen(pid, choice=None):
            img = A.screen_image(disc.english[pid], disc.layout[pid], choice)
            return lambda x, y: img.getpixel((x, y))

        for pid, strip, panel, back, shadow, under_shadow in (("tutorial/icons-5", 0, 5, 10, 9, True),
                                                              ("tutorial/icons-1", 0, 4, 9, 8, False)):
            data, layout = disc.english[pid], disc.layout[pid]
            at = screen(pid)
            s = tutorial_value(data, layout, shadow, 100, 155)
            self.assertTrue(s & 0x8000 and s & 0x7FFF)
            words = tutorial_value(data, layout, strip, 100, 155)
            self.assertEqual(at(100, 155), A.screen_rgb(subtracted(words, s) if under_shadow else words), pid)
            self.assertEqual(at(70, 10), A.screen_rgb(tutorial_value(data, layout, panel, 70, 10)), pid)
            self.assertEqual(tutorial_value(data, layout, panel, 265, 10), 0)            # see-through: under it
            self.assertEqual(at(265, 10), A.screen_rgb(subtracted(tutorial_value(data, layout, back, 265, 10),
                                                                  tutorial_value(data, layout, shadow, 265, 10))))
            self.assertEqual(at(5, 5), A.screen_rgb(tutorial_value(data, layout, back, 5, 5)))
        data, layout = disc.english["tutorial/menu"], disc.layout["tutorial/menu"]
        self.assertEqual(screen("tutorial/menu")(5, 0), A.screen_rgb(tutorial_value(data, layout, 8, 5, 0)))
        self.assertEqual([lay.tiles[0] for lay in layout.layers[:3]], [(0, -6), (61 - 13, 4 + 31), (57, 35)])
        ring = (100 - 3, 40 + 2 * 20 + 31)      # the top of copy 2's ring, moved with the menu
        self.assertEqual(screen("tutorial/menu", 2)(*ring), A.screen_rgb(31 << 5))
        self.assertNotEqual(screen("tutorial/menu")(*ring), A.screen_rgb(31 << 5))
        self.assertNotEqual(screen("tutorial/menu", 3)(*ring), A.screen_rgb(31 << 5))

    def test_a_tutorial_screen_out_as_png_and_back_is_the_same_bytes(self):
        from translator import artwork as A
        disc = TutorialDisc()
        for pid in disc.english:
            pic, layout, english = A.BY_ID[pid], disc.layout[pid], disc.english[pid]
            self.assertEqual(A.size(pic, english), (320, 224))
            self.assertEqual(A.from_png(pic, english, A.png(pic, english, layout), None, layout),
                             A.Loaded(english, []), pid)

    def test_only_the_words_of_a_tutorial_png_are_taken(self):
        """Greys painted over the words, the picture behind and the controller: the words are exactly the
        PNG's, every other layer keeps its bytes, and the pixels left are counted in a note."""
        from PIL import ImageDraw
        from translator import artwork as A
        disc = TutorialDisc()
        pid = "tutorial/controller-1"
        pic, layout, english = A.BY_ID[pid], disc.layout[pid], disc.english[pid]
        img = A.image(pic, english, layout)
        draw = ImageDraw.Draw(img)
        draw.rectangle((10, 160, 300, 175), fill=A.screen_rgb(10 * 0x421))      # the words
        draw.rectangle((0, 0, 9, 9), fill=(255, 0, 255))                        # the picture behind
        draw.rectangle((100, 20, 109, 29), fill=(255, 0, 255))                  # the controller
        loaded = A.from_png(pic, english, png_bytes(img), None, layout)
        got = A.image(pic, loaded.tim, layout)
        for x in range(320):
            for y in range(152, 224):
                self.assertEqual(got.getpixel((x, y)), img.getpixel((x, y)), (x, y))
        strip = next(lay for lay in layout.layers if lay.entry == 0)
        part = slice(strip.at, strip.at + 258 * len(strip.tiles))
        self.assertNotEqual(loaded.tim[part], english[part])
        self.assertEqual(loaded.tim[:part.start] + loaded.tim[part.stop:], english[:part.start] + english[part.stop:])
        self.assertTrue(any(n.startswith("200 pixels you changed are not part of the words") for n in loaded.notes),
                        loaded.notes)

    def test_the_shadow_is_taken_back_off_redrawn_words_under_it(self):
        """In M362 the shadow falls on the words' top rows: a pixel painted there in a darkened colour
        becomes the colour that, darkened, is it; and the screen is the PNG."""
        from translator import artwork as A
        disc = TutorialDisc()
        pid = "tutorial/icons-5"
        pic, layout, english = A.BY_ID[pid], disc.layout[pid], disc.english[pid]
        import struct
        s = tutorial_value(english, layout, 9, 100, 155)
        strip = next(lay for lay in layout.layers if lay.entry == 0)
        t = next(t for t, (x, y) in enumerate(strip.tiles) if x <= 100 < x + 16 and y <= 155 < y + 16)
        row = struct.unpack_from("<H", english, strip.at + t * 258)[0] & 511
        wanted = struct.unpack_from("<H", english, ((row - 480) * 256 + 230) * 2)[0]    # a light colour of its row
        self.assertNotEqual(wanted, tutorial_value(english, layout, 0, 100, 155))
        painted = subtracted(wanted, s)
        img = A.image(pic, english, layout)
        img.putpixel((100, 155), A.screen_rgb(painted))
        loaded = A.from_png(pic, english, png_bytes(img), None, layout)
        got = tutorial_value(loaded.tim, layout, 0, 100, 155)
        self.assertEqual(subtracted(got, s), painted)
        self.assertNotEqual(got & 0x7FFF, painted)                   # the light colour, not the dark one painted
        self.assertEqual(A.image(pic, loaded.tim, layout).tobytes(), img.tobytes())
        self.assertEqual(loaded.notes, ["1 of the screen's squares with words changed."])

    def test_the_menu_drawn_once_goes_into_its_copies_with_the_ring_put_back(self):
        """The menu painted over: each choice's screen is the new menu with the English green ring on it,
        and only the ring: the copies' specks (a step of colour) are not put back. Squares of the menu not
        painted leave the copies as they were."""
        from PIL import ImageDraw
        from translator import artwork as A
        disc = TutorialDisc()
        pic, layout, english = A.BY_ID["tutorial/menu"], disc.layout["tutorial/menu"], disc.english["tutorial/menu"]
        img = A.image(pic, english, layout)
        ImageDraw.Draw(img).rectangle((60, 60, 250, 170), fill=A.screen_rgb(3 * 0x421))
        loaded = A.from_png(pic, english, png_bytes(img), None, layout)
        menu = A.image(pic, loaded.tim, layout)
        self.assertEqual(menu.tobytes(), img.tobytes())
        green = A.screen_rgb(31 << 5)
        for k, copy in enumerate((lay for lay in layout.layers if not lay.drawn), 1):
            ring = {(x + k2 % 16, y + k2 // 16) for (x, y), ks in A.ring_of(english, layout, copy).items() for k2 in ks}
            band = 40 + k * 20 + 31
            drawn = {(x - 3, y) for x in range(70, 251) for y in (band, band + 1, band + 14, band + 15)} | \
                    {(x - 3, y) for y in range(band, band + 16) for x in (70, 71, 249, 250)}
            self.assertEqual(ring, {p for p in drawn if p[1] < 4 + 144 + 31}, k)     # the ring inside the menu
            chosen, before = A.screen_image(loaded.tim, layout, k), A.screen_image(english, layout, k)
            for y in range(224):
                for x in range(320):
                    p = (x, y)
                    if p in ring:
                        self.assertEqual(chosen.getpixel(p), green, (k, p))
                    elif 60 <= x <= 250 and 60 <= y <= 170:          # painted: the new menu, specks and all gone
                        self.assertEqual(chosen.getpixel(p), menu.getpixel(p), (k, p))
                    elif not (57 <= x < 265 and 35 <= y < 179):      # outside the menu: as it was
                        self.assertEqual(chosen.getpixel(p), before.getpixel(p), (k, p))
            plain = next(lay for lay in layout.layers if lay.word and lay.drawn)
            for t, place in enumerate(plain.tiles):
                if loaded.tim[plain.at + t * 258:plain.at + t * 258 + 258] == english[plain.at + t * 258:plain.at + t * 258 + 258]:
                    at = copy.at + copy.tiles.index(place) * 258
                    self.assertEqual(loaded.tim[at:at + 258], english[at:at + 258], (k, place))

    def test_the_tune_up_screenshot_takes_a_change_from_either_png_and_refuses_two_that_differ(self):
        """About Icons 7 and 8 show one screenshot. Changed in 7's PNG, it is changed on 8 too; 8's PNG
        saved before (the screenshot as the game has it) keeps 7's change; 8's PNG changing it otherwise
        is refused (Disagree names 7) and nothing of it is loaded."""
        from PIL import ImageDraw
        from translator import artwork as A
        disc = TutorialDisc()
        seven, eight = A.BY_ID["tutorial/icons-7"], A.BY_ID["tutorial/icons-8"]
        self.assertEqual([(o.id, e) for o, e in A.shared_with(seven)], [("tutorial/icons-8", 7)])
        self.assertEqual([(o.id, e) for o, e in A.shared_with(eight)], [("tutorial/icons-7", 7)])
        saved_eight = A.image(eight, disc.english[eight.id], disc.layout[eight.id])
        img = A.image(seven, disc.english[seven.id], disc.layout[seven.id])
        ImageDraw.Draw(img).rectangle((80, 40, 150, 60), fill=A.screen_rgb(5 * 0x421))
        loaded = A.from_png(seven, disc.english[seven.id], png_bytes(img), None, disc.layout[seven.id])
        self.assertIn('The screenshot is also on "Tutorial: About Icons 8/9 · Added effects": it changes there too.',
                      loaded.notes)
        pieces = A.put(disc, seven, [], loaded.tim)
        now = A.shown(disc, eight, pieces)
        self.assertEqual(A.image(eight, now, disc.layout[eight.id]).crop((80, 40, 151, 61)).tobytes(),
                         img.crop((80, 40, 151, 61)).tobytes())
        # 8's PNG as saved before, its words redrawn: the words are taken, the screenshot stays 7's.
        ImageDraw.Draw(saved_eight).rectangle((20, 180, 60, 190), fill=A.screen_rgb(5 * 0x421))
        kept = A.from_png(eight, disc.english[eight.id], png_bytes(saved_eight), now, disc.layout[eight.id])
        self.assertIn('The screenshot is also on "Tutorial: About Icons 7/9 · Tune-up Icon": the change loaded there '
                      'is kept.', kept.notes)
        shown = A.image(eight, kept.tim, disc.layout[eight.id])
        self.assertEqual(shown.crop((80, 40, 151, 61)).tobytes(), img.crop((80, 40, 151, 61)).tobytes())
        self.assertEqual(shown.crop((20, 180, 61, 191)).tobytes(), saved_eight.crop((20, 180, 61, 191)).tobytes())
        other = A.image(eight, now, disc.layout[eight.id])
        ImageDraw.Draw(other).rectangle((100, 100, 120, 110), fill=A.screen_rgb(28 * 0x421))
        with self.assertRaises(A.Disagree) as said:
            A.from_png(eight, disc.english[eight.id], png_bytes(other), now, disc.layout[eight.id])
        self.assertIs(said.exception.other, seven)
        self.assertIn("This PNG was not loaded", str(said.exception))

    def test_a_tint_is_the_gpus_colour_times_the_drawing_colour(self):
        """The battle words' preview: each 5-bit part times the drawing colour over 128 (128 = as it is),
        at most 31, as the PlayStation draws a tinted sprite; the see-through bit stays."""
        from translator import artwork as A
        white, grey, blue_stp = 0x7FFF, 16 | 16 << 5 | 16 << 10, 0x8000 | 20 << 10
        self.assertEqual(A.tint_colour(white, (128, 128, 128)), white)
        self.assertEqual(A.tint_colour(white, (128, 0, 0)), 31)
        self.assertEqual(A.tint_colour(grey, (128, 128, 0)), 16 | 16 << 5)
        self.assertEqual(A.tint_colour(grey, (64, 255, 128)), 8 | 31 << 5 | 16 << 10)    # half; at most 31
        self.assertEqual(A.tint_colour(blue_stp, (0, 0, 128)), blue_stp)

    def test_a_tinted_picture_is_one_copy_per_tint(self):
        from petext import tim as T
        from translator import artwork as A
        pic = A.Picture("t/p", "T", "P", 0, 0, 0, 0, A.BATTLE,
                        tints=(("a", (128, 128, 128)), ("b", (128, 0, 0)), ("c", (0, 128, 0))))
        grey = [k | k << 5 | k << 10 for k in range(0, 32, 2)]
        tim = made_up_tim(4, 8, 3, palette=grey)
        img = A.tinted(pic, tim)
        self.assertEqual(img.size, (8, 9))
        self.assertEqual(img.crop((0, 0, 8, 3)).tobytes(), A.image(pic, tim).convert("RGB").tobytes())
        numbers = T.indices(T.read(tim))
        for k, n in enumerate(numbers):
            x, y = k % 8, k // 8
            r = T.rgb_of(grey[n])[0]
            self.assertEqual(img.getpixel((x, y + 3)), (r, 0, 0))
            self.assertEqual(img.getpixel((x, y + 6)), (0, r, 0))

    def test_a_glyph_is_found_from_the_glyph_table_and_the_entrys_raw_images(self):
        """Game Over's kind: its rows and colours are where the disc's own glyph table and the entry's raw image
        list put them (another image before the page moves them along); a glyph of another size, or one that is
        not inside one raw image, is refused."""
        from translator import artwork as A
        pic = A.GAME_OVER_WORDS
        for before in (1, 3):
            blob, resident, page = made_up_glyph_page(before=before)
            parts = A._glyph_parts(pic, blob, resident)
            self.assertEqual(parts, [(page + (184 + r) * 128, 60) for r in range(24)] + [(page + 233 * 128, 32)])
        blob, resident, _ = made_up_glyph_page(width=100)
        with self.assertRaisesRegex(ValueError, "is 100x24"):
            A._glyph_parts(pic, blob, resident)
        blob, resident, _ = made_up_glyph_page(rect=(448, 0, 64, 200))          # stops above the colours
        with self.assertRaisesRegex(ValueError, "not in one raw image"):
            A._glyph_parts(pic, blob, resident)
        blob, resident, _ = made_up_glyph_page(rect=(448, 190, 64, 64))         # starts below the first row
        with self.assertRaisesRegex(ValueError, "not in one raw image"):
            A._glyph_parts(pic, blob, resident)

    def test_a_glyph_png_out_and_back_is_the_same_bytes_and_keeps_its_colours(self):
        """A PNG of Game Over has its own 16 colours (palette or RGB, same bytes back); a colour it lacks
        becomes the nearest of the 16 (with a note); the colours, the English ones or a translation's, stay;
        another size is refused."""
        from PIL import Image, ImageDraw
        from petext import tim as T
        from translator import artwork as A
        pic, data = made_up_glyph()
        png = A.png(pic, data)
        self.assertEqual(Image.open(io.BytesIO(png)).mode, "P")
        self.assertEqual(A.from_png(pic, data, png).tim, data)
        back = A.from_png(pic, data, png_bytes(Image.open(io.BytesIO(png)).convert("RGB")))
        self.assertEqual((back.tim, back.notes), (data, []))
        img = Image.open(io.BytesIO(png)).convert("RGB")
        near = tuple(min(255, c + 3) for c in T.rgb_of(A.glyph_colours(pic, data)[5]))
        ImageDraw.Draw(img).point((0, 0), fill=near)                       # number 0 there before
        loaded = A.from_png(pic, data, png_bytes(img))
        self.assertEqual(A.glyph_numbers(pic, loaded.tim)[0], 5)
        self.assertEqual(A.glyph_numbers(pic, loaded.tim)[1:], A.glyph_numbers(pic, data)[1:])
        self.assertEqual(loaded.tim[-32:], data[-32:])
        self.assertIn("1 pixels were not one of the picture's 16 colours", " ".join(loaded.notes))
        theirs = data[:-2] + (0x7FFF).to_bytes(2, "little")                  # a translation's own last colour
        self.assertEqual(A.from_png(pic, data, A.png(pic, theirs), theirs).tim, theirs)
        with self.assertRaisesRegex(ValueError, "must stay 8×3"):
            A.from_png(pic, data, png_bytes(Image.new("RGB", (9, 3))))

    def test_a_glyph_in_the_game_is_darker_and_dithered_by_its_place_on_the_screen(self):
        """Game Over as the screen shows it: each colour part as 8 bits times the drawing colour over 128,
        plus the PlayStation's 4x4 dither at that screen pixel, cut to 5 bits; colour 0 the black screen."""
        from translator import artwork as A
        white = 0x7FFF
        self.assertEqual(A.dithered(white, 128, 1, 0), (255, 255, 255))           # dither 0 there
        self.assertEqual(A.dithered(white, 128, 0, 0), (247, 247, 247))           # 248 - 4 -> 30
        self.assertEqual(A.dithered(white, 128, 2, 1), (255, 255, 255))           # 248 + 3 -> 31
        self.assertEqual(A.dithered(1, 120, 0, 0), (0, 0, 0))                     # 7 - 4 -> 0
        self.assertEqual(A.dithered(16 << 5, 120, 3, 3)[1], A.screen_rgb(14 << 5)[1])   # 120 - 2 -> 14
        self.assertEqual(A.dithered(white, 120, 0, 3), A.dithered(white, 120, 4, 7))     # every 4 pixels
        pic, data = made_up_glyph()
        img = A.in_game(pic, data)
        colours, numbers = A.glyph_colours(pic, data), A.glyph_numbers(pic, data)
        for k, n in enumerate(numbers):
            x, y = k % 8, k // 8
            want = (0, 0, 0) if n == 0 else A.dithered(colours[n], 120, 100 + x, 100 + y)
            self.assertEqual(img.getpixel((x, y)), want)
        self.assertNotEqual(A.in_game(pic, data, 116).tobytes(), img.tobytes())
        self.assertEqual(A.previews(pic), 1)
        self.assertEqual(A.preview_png(pic, data), png_bytes(img))

    def test_a_menu_word_in_the_game_is_its_own_colours_and_at_half(self):
        """A menu word as the menus draw it (a sprite, no dither): each colour exactly, and at half brightness
        each 5-bit part halved; colour 0 the black behind. The preview is the two, one above the other; a PNG
        out and back is the same bytes, as Game Over's."""
        import dataclasses
        from PIL import Image
        from translator import artwork as A
        _, data = made_up_glyph(8, 3)
        pic = dataclasses.replace(A.MENU_WORDS[0], size=(8, 3))
        self.assertTrue(pic.sprite)
        self.assertEqual(pic.brightness, (128, 64))
        colours, numbers = A.glyph_colours(pic, data), A.glyph_numbers(pic, data)
        full, half = A.in_game(pic, data, 128), A.in_game(pic, data, 64)
        for k, n in enumerate(numbers):
            c = colours[n]
            want = [(0, 0, 0), (0, 0, 0)] if n == 0 else [
                tuple(((c >> s & 31) * 255 + 15) // 31 for s in (0, 5, 10)),
                tuple(((c >> s & 31) // 2 * 255 + 15) // 31 for s in (0, 5, 10))]
            self.assertEqual([full.getpixel((k % 8, k // 8)), half.getpixel((k % 8, k // 8))], want, k)
        self.assertEqual(A.in_game(pic, data).tobytes(), full.tobytes())          # its first: as drawn
        self.assertEqual(A.previews(pic), 2)
        both = Image.new("RGB", (8, 6))
        both.paste(full, (0, 0))
        both.paste(half, (0, 3))
        self.assertEqual(A.preview_png(pic, data), png_bytes(both))
        self.assertEqual(A.from_png(pic, data, A.png(pic, data)).tim, data)

    def test_the_menu_words_are_the_pages_words_each_once(self):
        """50 menu words and icons with words, each its own glyph and id, after Game Over in the list, each
        a sprite shown as drawn and at half."""
        from translator import artwork as A
        self.assertEqual(len(A.MENU_WORDS), 50)
        self.assertEqual(len({p.glyph for p in A.MENU_WORDS} | {A.GAME_OVER_WORDS.glyph}), 51)
        self.assertEqual(len({p.id for p in A.PICTURES}), len(A.PICTURES))
        self.assertEqual(A.PICTURES[-51:], (A.GAME_OVER_WORDS,) + A.MENU_WORDS)
        for pic in A.MENU_WORDS:
            self.assertEqual((pic.entry, pic.page, pic.sprite, pic.brightness), (9, (448, 0), True, (128, 64)), pic.id)
            self.assertTrue(pic.id.startswith(("menu/", "menu-icon/")), pic.id)

    def test_a_picture_loaded_keeps_every_other_piece(self):
        import zlib
        from petext import pictures as pf, tim as T
        from translator import artwork as A
        tim = made_up_tim(24, 40, 30)                     # 3,620 bytes: from sector 1 into sector 3
        disc = FakeDisc(tim)
        pic, (base, data) = disc.pic, disc.around[disc.pic.id]
        elsewhere = pf.Piece(9, 0, 1234, b"xy")                                  # another sector
        other_disc = pf.Piece(2, 10, 0xDEADBEEF, b"zz")                           # lands on no byte here
        before_pic = pf.Piece(1, 4, zlib.crc32(data[4:8]), b"keep")              # same sector, before it
        pieces = [elsewhere, other_disc, before_pic]
        t = T.read(tim)
        new = T.with_rgb(t, bytes(len(T.rgb(t))))
        out = A.put(disc, pic, pieces, new)
        self.assertEqual(A.shown(disc, pic, out), new)
        self.assertIn(elsewhere, out)
        self.assertIn(other_disc, out)
        self.assertEqual(A._lay(base, data, out)[0][4:8], b"keep")            # its bytes still go in
        self.assertTrue(all(p.offset + len(p.data) <= 2048 for p in out))
        back = A.put(disc, pic, out, None)
        self.assertFalse(A.redrawn(disc, pic, back))
        self.assertEqual(A._lay(base, data, back)[0], A._lay(base, data, pieces)[0])
        with self.assertRaisesRegex(ValueError, "keep its size"):
            A.put(disc, pic, pieces, new[:-1])
        parts = {}
        A.write_pieces(parts, out)
        self.assertEqual(sorted(A.read_pieces(parts), key=str), sorted(out, key=str))
        A.write_pieces(parts, [])
        self.assertNotIn(P.PICTURES, parts)


class FakeLookDisc:
    """gamelook.DiscLook's font parts with a blank font page: no disc. Widths don't need pictures."""

    def __init__(self):
        from petext import tim as T
        tim = T.read(made_up_tim(4, 8, 1, palette=[0, 0x7BDE, 0x0421] + [0] * 13))
        self.page, self.area_page = [0] * (256 * 256), [0] * (256 * 256)
        for code in range(0x4D):                 # each game letter a stroke, as every letter has ink
            x0, y0 = code % 21 * 12, code // 21 * 12
            for y in range(2, 10):
                self.page[(y0 + y) * 256 + x0 + 3] = 1
            for x in range(3, 8):
                self.page[(y0 + 9) * 256 + x0 + x] = 1
        self.font = self.area_font = tim


class FakeBattleDisc(FakeLookDisc):
    """FakeLookDisc with the first fight's room: three views, each one grey the screen can show (5-bit
    values 8, 12, 16)."""

    def views(self, number: int) -> list[bytes]:
        return [bytes([((8 + 4 * k) * 255 + 15) // 31]) * (320 * 224 * 3) for k in range(3)] if number == 5 else []


class BattleLookRuleTest(unittest.TestCase):
    """The battle lines' game look with no disc: the bar where the game puts it, the line on it in
    the dialogue letters as the compiler measures them, the HP line's numbers."""

    def look(self, key: str, text: str, top: bool = False):
        from translator import battlelook as B, gamelook as G
        result = P.check_row(GAME, key, text, "es", "Español")
        self.assertEqual(result.errors, [], result.errors)
        pack = G.Pack.read(result.preview.pack)
        font = G.Font(FakeBattleDisc(), pack)
        return result, pack, font, B.look(font, pack.text(key), key, FakeBattleDisc().views(5)[2], top)

    def test_the_line_sits_on_the_bar_at_the_bottom_or_the_top(self):
        from translator import battlelook as B, gamelook as G, menulook as M
        for top in (False, True):
            result, pack, font, look = self.look("battle/010", "¡No puedes huir!", top)
            bar_y, line_y = B.TOP if top else B.BOTTOM
            self.assertEqual(look.bar, (bar_y, bar_y + 20))
            self.assertEqual(look.notes, [])
            # the room as it is (not darkened) outside the bar; the bar's bevel over it, the letters on it
            grey = (16 * 255 + 15) // 31
            screen = M.Screen(FakeBattleDisc().views(5)[2], dim=False)
            for pixels, rgb, sign in M.bevel(0, bar_y, 320, 20):
                screen.blend(pixels, rgb, sign)
            placed = G.lay_out(font, pack.text("battle/010"), b"", 20, line_y).screens[0]
            self.assertEqual(look.rgb, G.draw(font, placed, screen.rgb()))
            outside = [y for y in range(224) if not bar_y <= y < bar_y + 20]
            self.assertTrue(all(look.rgb[(y * 320 + x) * 3] == grey for y in outside for x in range(0, 320, 7)))
            self.assertNotEqual(look.rgb[(bar_y * 320 + 100) * 3], grey)
            self.assertEqual({p.y for p in placed}, {line_y})
            last = placed[-1]
            self.assertEqual(last.x - last.letter.x + last.letter.advance - 20, result.preview.screens[0].widths[0])
            self.assertIn("at the top" if top else "at the bottom", look.where)

    def test_the_hp_line_shows_9999_in_both_number_slots(self):
        from translator import battlelook as B, gamelook as G
        result, pack, font, look = self.look("battle/001", "PV {num}/{num}")
        raw = pack.text("battle/001")

        def laid(numbers):
            placed = G.lay_out(font, raw, b"", 20, B.BOTTOM[1], numbers=numbers).screens[0]
            last = placed[-1]
            return placed, last.x - last.letter.x + last.letter.advance - 20

        nines, end = laid(B.NUMBERS["battle/001"])
        ones, one_end = laid((1, 1))
        self.assertEqual(B.NUMBERS["battle/001"], (9999, 9999))
        self.assertEqual((len(nines) - len(ones), end - one_end), (6, 6 * 12))   # three more digits each, 12 px apart
        self.assertEqual(end, result.preview.screens[0].widths[0] - 2 * 12)      # the compiler counts five digits a slot
        nine = G._fixed(font.page, 9, 0)
        self.assertEqual([(p.letter.pixels, p.letter.advance) for p in nines[-4:]], [(nine.pixels, 12)] * 4)
        self.assertIn("HP 9999/9999", look.where)
        self.assertEqual(look.notes, [])

    def test_a_number_slot_past_the_values_ends_the_line(self):
        """As the game does when a page has more FB 08 than values (Menu_DrawTextboxEntries returns)."""
        from translator import gamelook as G
        _, pack, font, _ = self.look("battle/001", "PV {num}/{num}")
        raw = pack.text("battle/001")
        one = G.lay_out(font, raw, b"", 20, 195, numbers=(7,)).screens[0]
        both = G.lay_out(font, raw, b"", 20, 195, numbers=(7, 8)).screens[0]
        self.assertEqual(len(both), len(one) + 1)          # the second number's one digit ("/" is in both)
        self.assertEqual(G.lay_out(font, raw, b"", 20, 195).screens[0][-1].letter.pixels,
                         G._fixed(font.page, 5, 0).pixels)  # no values: the sample digits, as before

    def test_a_line_too_long_for_the_screen_is_said(self):
        from translator import battlelook as B, gamelook as G
        from petext import battle
        result = P.check_row(GAME, "battle/010", "W" * 40, "es", "Español")
        pack = G.Pack.read(result.preview.pack)
        look = B.look(G.Font(FakeBattleDisc(), pack), pack.text("battle/010"), "battle/010",
                      FakeBattleDisc().views(5)[2])
        self.assertTrue(result.errors)                     # the compiler says so too
        self.assertIn("the line runs off the screen", look.notes)
        self.assertTrue(B.can_draw("battle/000") and not B.can_draw("dlg/M001/009") and not B.can_draw("sys/000"))
        self.assertEqual(battle.X, 20)


LETTER_COUNTS = {"is": 3, "sr-Cyrl": 10, "mk": 6, "kk": 3, "el": 36, "he": 27, "ar": 102}


def made_up_strokes(k: int, joins: str = "") -> tuple[str, ...]:
    """A made-up letter of its own for square k (12 rows of '.'/'#'): a box of its own width, a bar
    and a dot; a joined shape's bottom line reaches both sides of its ink."""
    w = 3 + k % 6
    pts = {(1, y) for y in range(2, 9)} | {(w, y) for y in range(2 + k % 3, 9)} | {(x, 8) for x in range(1, w + 1)}
    pts |= {(x, 2 + k % 5) for x in range(1, w + 1)} | {(1 + (k // 6) % w, 1)}
    if joins:
        pts |= {(x, 8) for x in range(0, w + 2)}
    return tuple("".join("#" if (x, y) in pts else "." for x in range(12)) for y in range(12))


def paint(png: bytes, squares: dict, letters: dict, colour=(255, 255, 255)) -> bytes:
    """The letters PNG with `letters` (char -> 12 rows) painted into their squares."""
    from PIL import Image
    img = Image.open(io.BytesIO(png)).convert("RGBA")
    for shape, (x, y) in squares.items():
        for yy, row in enumerate(letters.get(shape.char, ())):
            for xx, v in enumerate(row):
                if v == "#":
                    img.putpixel((x + xx, y + yy), colour + (255,))
    return png_bytes(img)


def shadow_of(rows: tuple[str, ...]) -> list[int]:
    """144 pixels: 1 painted, 2 the shadow one dot right and below (the game's look), worked out here."""
    cell = [1 if v == "#" else 0 for row in rows for v in row]
    out = list(cell)
    for i, v in enumerate(cell):
        x, y = i % 12, i // 12
        if v and x < 11 and y < 11 and not out[i + 13]:
            out[i + 13] = 2
    return out


def unpack(fmt, data, at):
    import struct
    return struct.unpack_from(fmt, data, at)


class LetterArtTest(unittest.TestCase):
    """Letters the game can't draw yet, as a PNG and back (translator/letterart.py), with no disc:
    which letters, the PNG out and in, and the letters in the translation and its pack."""

    def setUp(self):
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("needs Pillow (the letters PNG is made with it)")
        from translator import letterart
        self.L = letterart
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_letters_to_draw_are_the_ones_the_game_shows_as_a_question_mark(self):
        L = self.L
        for lang, count in LETTER_COUNTS.items():
            shapes = L.every_shape(L.wanted(lang))
            self.assertEqual(len(shapes), count, lang)
            self.assertEqual(len({s.char for s in shapes}), count, lang)
            for s in shapes:
                self.assertEqual(P.lettertable.CATALOG[s.char].art, P.lettertable.PLACEHOLDER, (lang, s.char))
            self.assertTrue(all(len(row) <= L.PER_ROW for row in L.wanted(lang)))
        language = alphabets.BY_CODE["el"]
        self.assertEqual({s.char for s in L.every_shape(L.wanted("el"))},
                         {c for c in alphabets.full_alphabet(language) if L.needs_drawing(c)})
        self.assertEqual(L.wanted("fr"), [])
        self.assertEqual(L.wanted("de"), [])
        # a letter of another language that a line uses comes after the language's own
        self.assertEqual([s.char for s in L.every_shape(L.wanted("fr", ["Où est Ωmega?"]))], ["Ω"])
        self.assertEqual(L.every_shape(L.wanted("el", ["ð"]))[-1].char, "ð")
        self.assertEqual(len(L.every_shape(L.wanted("el", ["αβγ ΓΔ"]))), 36)

    def test_an_arabic_row_is_each_letters_shapes_with_the_sides_they_join_on(self):
        L = self.L
        from petext import rtl
        rows = L.wanted("ar")
        beh = next(row for row in rows if row[0].char == "ﺏ")
        self.assertEqual([(s.char, s.joins, s.what) for s in beh],
                         [("ﺏ", "", "alone"), ("ﺐ", "r", "end"), ("ﺒ", "rl", "middle"),
                          ("ﺑ", "l", "start")])
        alef = next(row for row in rows if row[0].char == "ﺍ")
        self.assertEqual([(s.joins, s.what) for s in alef], [("", "alone"), ("r", "end")])
        self.assertIn([("ﻻ", ""), ("ﻼ", "r")], [[(s.char, s.joins) for s in row] for row in rows])
        # Every shape the renderer's joining gives the language's letters, in any word, is in the PNG,
        # and it joins on exactly the sides the PNG marks.
        letters = [c for c in alphabets.full_alphabet(alphabets.BY_CODE["ar"]) if ord(c) in rtl.FORMS]
        words = [a + b + c for a in letters for b in letters for c in letters[:6]] + ["لا", "سلام", "بلا"]
        marked = {s.char: s.joins for s in L.every_shape(rows)}
        for word in words:
            joined, touches = rtl.join(word)
            for k, ch in enumerate(joined):
                self.assertIn(ch, marked, (word, f"{ord(ch):04X}"))
                right, left = k > 0 and touches[k - 1], touches[k]
                self.assertEqual(("r" in marked[ch], "l" in marked[ch]), (right, left), (word, f"{ord(ch):04X}"))

    def test_a_png_out_and_back_gives_the_same_letters(self):
        L = self.L
        for lang in LETTER_COUNTS:
            rows = L.wanted(lang)
            shapes = L.every_shape(rows)
            letters = {s.char: made_up_strokes(k, s.joins) for k, s in enumerate(shapes)}
            for style in (None, [([1] * 144, (None, (250, 250, 250)))] * 3):
                png = L.png(rows, lang, letters, style)
                back = L.from_png(png, rows)
                self.assertEqual((back.drawn, back.notes), (letters, []), lang)
                empty = L.from_png(L.png(rows, lang, {}, style), rows)
                self.assertEqual(empty.drawn, {s.char: None for s in shapes})
                # painted into the empty PNG the tool saved, as a person would
                again = L.from_png(paint(L.png(rows, lang, {}, style), L.squares(rows, bool(style)), letters), rows)
                self.assertEqual(again.drawn, letters)

    def test_a_dot_is_painted_where_it_is_bright_and_nothing_else_counts(self):
        L = self.L
        from PIL import Image
        rows = L.wanted("is")
        squares = L.squares(rows, False)
        one = {s.char: made_up_strokes(k) for k, s in enumerate(L.every_shape(rows))}
        png = L.png(rows, "Icelandic", {})
        for colour in ((255, 255, 255), (255, 0, 0), (200, 200, 0), (0, 0, 130)):
            self.assertEqual(L.from_png(paint(png, squares, one, colour), rows).drawn, one, colour)
        for colour in ((100, 100, 100), (0, 0, 127), L.GUIDE, L.FRAME):
            self.assertEqual(set(L.from_png(paint(png, squares, one, colour), rows).drawn.values()), {None}, colour)
        img = Image.open(io.BytesIO(paint(png, squares, one))).convert("RGBA")
        for x, y in squares.values():                  # see-through dots are empty, however bright
            for i in range(144):
                r, g, b, _ = img.getpixel((x + i % 12, y + i // 12))
                img.putpixel((x + i % 12, y + i // 12), (r, g, b, 0))
        self.assertEqual(set(L.from_png(png_bytes(img), rows).drawn.values()), {None})
        # the faint lines show in an empty square and read as empty
        img = Image.open(io.BytesIO(png)).convert("RGB")
        x, y = next(iter(squares.values()))
        self.assertEqual([img.getpixel((x + 4, y + r)) for r in L.GUIDE_ROWS], [L.GUIDE] * 3)
        self.assertEqual(img.getpixel((x + 4, y + 5)), L.SQUARE)

    def test_a_png_a_paint_program_saved_without_its_note_still_loads(self):
        L = self.L
        from PIL import Image
        rows = L.wanted("el")
        letters = {s.char: made_up_strokes(k) for k, s in enumerate(L.every_shape(rows))}
        png = L.png(rows, "Greek", letters)
        self.assertIn(L.CHUNK, Image.open(io.BytesIO(png)).info)
        bare = png_bytes(Image.open(io.BytesIO(png)).convert("RGB"))
        self.assertNotIn(L.CHUNK, Image.open(io.BytesIO(bare)).info)
        self.assertEqual(L.from_png(bare, rows).drawn, letters)

    def test_a_png_saved_before_more_letters_were_needed_still_loads(self):
        L = self.L
        from PIL import Image
        before = L.wanted("el")
        after = L.wanted("el", ["Γεια ðþÞ אבג"])         # lines now use Icelandic and Hebrew letters too
        self.assertGreater(len(after), len(before))      # a row more
        letters = {s.char: made_up_strokes(k) for k, s in enumerate(L.every_shape(before))}
        png = L.png(before, "Greek", letters)
        self.assertEqual(L.from_png(png, after).drawn, letters)     # by the PNG's own list of letters
        bare = png_bytes(Image.open(io.BytesIO(png)).convert("RGB"))
        # with no note, still while the squares are where they were (new letters come after the others)
        two_more = L.wanted("el", ["Γεια ðþ"])
        self.assertEqual(L.size(two_more, False), L.size(before, False))
        self.assertEqual(L.from_png(bare, two_more).drawn, {**letters, "ð": None, "þ": None})
        with self.assertRaises(ValueError) as said:
            L.from_png(bare, after)
        self.assertIn("Save as PNG", str(said.exception))
        self.assertIn(f"{Image.open(io.BytesIO(bare)).size[0]} x", str(said.exception))
        # and the other way: a PNG with letters the translation no longer needs
        later = L.png(after, "Greek", {**letters, "ð": made_up_strokes(1)})
        loaded = L.from_png(later, before)
        self.assertEqual(loaded.drawn, letters)
        self.assertEqual(loaded.notes, ["letters this translation doesn't need were left out: ð"])

    def test_what_is_not_a_letters_png_is_refused_in_plain_words(self):
        L = self.L
        from PIL import Image
        rows = L.wanted("el")
        with self.assertRaises(ValueError) as said:
            L.from_png(b"not a picture", rows)
        self.assertIn("not a picture", str(said.exception))
        with self.assertRaises(ValueError) as said:
            L.from_png(png_bytes(Image.new("RGB", (64, 64))), rows)
        self.assertIn("64 x 64", str(said.exception))

    def test_a_letter_off_its_squares_edge_or_not_reaching_its_joining_side_is_said(self):
        L = self.L
        rows = L.wanted("ar")
        shapes = L.every_shape(rows)
        middle = next(s for s in shapes if s.what == "middle")
        alone = next(s for s in shapes if s.what == "alone")
        short = tuple("." * 12 if y != 5 else ".####......." for y in range(12))   # nothing on the bottom line
        edge = tuple(("#" * 12) if y == 11 else "." * 12 for y in range(12))
        loaded = L.from_png(L.png(rows, "Arabic", {middle.char: short, alone.char: edge}), rows)
        self.assertEqual(loaded.drawn[middle.char], short)
        said = " | ".join(loaded.notes)
        self.assertIn(f"{middle.char} (middle shape) doesn't reach its right side on the bottom line", said)
        self.assertIn(f"{middle.char} (middle shape) doesn't reach its left side", said)
        self.assertIn(f"{alone.char} (alone shape): dots in the square's last column or row", said)
        self.assertNotIn("alone shape) doesn't reach", said)

    def test_the_pack_holds_a_drawn_letter_as_painted_with_its_shadow(self):
        """Drawn letters go into the pack as pictures of its own, exactly as `pelang.py build
        --patch-letters` writes them; the work part keeps them all, also those no line uses."""
        L = self.L
        tr = P.Translation(GAME, "el", "Ελληνικά")
        key = plain_dialogue(1)[0]
        tr.set_text(key, "Γεια σου, φίλε μου.")
        gamma, psi = made_up_strokes(2), made_up_strokes(7)
        tr.set_letter("Γ", gamma)
        tr.set_letter("Ψ", psi)                       # no line uses it
        path = self.dir / "el.pelang"
        saved = tr.save(path)
        self.assertEqual((saved.left_out, saved.whole), ({}, []))
        with zipfile.ZipFile(path) as z:
            work = json.loads(z.read(P.WORK))
            text = z.read("text.bin")
        self.assertEqual(work["version"], P.WORK_VERSION)
        self.assertEqual(work["letters"], {"Γ": list(gamma), "Ψ": list(psi)})
        records = {}
        at, count = unpack("<II", text, 80)
        for k in range(count):
            r = text[at + 80 * k:at + 80 * (k + 1)]
            records[chr(unpack("<I", r, 0)[0])] = r
        r = records["Γ"]
        pixels = [r[8 + i // 2] >> ((i & 1) * 4) & 15 for i in range(144)]
        self.assertEqual((r[6], r[5], r[7], pixels), (0xFF, max(row.rfind("#") for row in gamma) + 2, 0, shadow_of(gamma)))
        self.assertNotIn("Ψ", records)                # the pack holds only what its lines show
        # byte for byte what the command line writes with the same letters (--patch-letters)
        sheet, letters, out = self.dir / "t.csv", self.dir / "letters.json", self.dir / "cli.bin"
        with open(sheet, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=pelang.SUBMISSION_COLUMNS)
            w.writeheader()
            w.writerow({"key": key, "translation": "Γεια σου, φίλε μου.", "notes": ""})
        letters.write_text(json.dumps({"letters": [
            {"letter": ch, "picture": list(L.picture(rows)), "dialogue": L.advance(rows), "menu": 0}
            for ch, rows in (("Γ", gamma), ("Ψ", psi))]}, ensure_ascii=False), encoding="utf-8")
        done = subprocess.run([sys.executable, "-I", str(HERE / "pelang.py"), "build", str(sheet), "--strings",
                               str(P.MANIFEST), "--lang", "el", "--name", "Ελληνικά", "-o", str(out),
                               "--max-messages", "1000000", "--patch-letters", str(letters)],
                              capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stdout[-600:] + done.stderr[-600:])
        self.assertEqual(out.read_bytes(), text)
        # reopened: the letters are the work part's; a letter given back its '?' leaves pack and file
        again = P.Translation.open(path, GAME)
        self.assertEqual(again.drawn, {"Γ": gamma, "Ψ": psi})
        self.assertEqual(again.own_letters, ())
        self.assertEqual(again.letter("Γ"), gamma)
        self.assertIsNone(again.letter("Δ"))
        again.set_letter("Γ", None)
        again.save()
        third = P.Translation.open(path, GAME)
        self.assertEqual(third.drawn, {"Ψ": psi})
        self.assertNotIn("Γ", {entry[0] for entry in P.letters_in_pack(third.pack)[0]})
        with self.assertRaises(ValueError):
            again.set_letter("Δ", ("#",) * 12)

    def test_a_file_from_before_letters_still_opens(self):
        tr = P.Translation(GAME, "fr", "Français")
        tr.set_text(plain_dialogue(1)[0], "Une ligne.")
        path = self.dir / "fr.pelang"
        tr.save(path)
        with zipfile.ZipFile(path) as z:
            parts = {i.filename: z.read(i) for i in z.infolist()}
        work = json.loads(parts[P.WORK])
        work["version"] = 3
        with zipfile.ZipFile(path, "w") as z:
            for name, data in parts.items():
                z.writestr(name, json.dumps(work) if name == P.WORK else data)
        again = P.Translation.open(path, GAME)
        self.assertTrue(again.from_work)
        self.assertEqual((again.drawn, again.texts), ({}, tr.texts))

    def test_drawn_arabic_shapes_reach_the_pack_for_every_shape_of_a_used_letter(self):
        L = self.L
        from petext import rtl
        tr = P.Translation(GAME, "ar", "عربي")
        tr.set_text(plain_dialogue(1)[0], "بيت لا")
        rows = tr.letters_to_draw()
        letters = {s.char: made_up_strokes(k, s.joins) for k, s in enumerate(L.every_shape(rows))}
        for ch, strokes in letters.items():
            tr.set_letter(ch, strokes)
        tr.save(self.dir / "ar.pelang")
        own = {entry[0]: entry[1] for entry in P.letters_in_pack(pelang.pack_bytes(self.dir / "ar.pelang"))[0]}
        # every shape of the letters used (alone, end, middle, start), drawn as painted; the ones the
        # words take now among them
        wanted = set(letters) & rtl.shapes_for(["بيت لا"])
        self.assertTrue(set(rtl.join("بيت")[0]) | set(rtl.join("لا")[0]) <= wanted)
        self.assertEqual(len(wanted), 4 * 4 + 2 + 2)     # beh, yeh, teh, lam: four shapes each; alef, lam-alef: two
        for ch in wanted:
            self.assertEqual(L.from_picture(own[ch]), letters[ch], f"{ord(ch):04X}")


def rtl_model(pictures: dict, word: str, right: int = 300) -> dict:
    """{(x, row): 1 or 2} of an Arabic word as the game draws it, worked out here (the model the game
    part `rtl` holds to the game's frames): the renderer's shapes in drawing order, each cut to its
    strokes and shadow, one pixel apart or touching where they join, the line ending at `right`, the
    leftmost letter in front."""
    from petext import rtl
    joined, nxt = rtl.join(word)
    order, _ = rtl.order(joined)
    placed, pen = [], 0
    for k, i in enumerate(order):
        cell = pictures[joined[i]]
        xs = [p % 12 for p, v in enumerate(cell) if v == 1]
        first, w = min(xs), min(max(xs) - min(xs) + 2, 12 - min(xs))
        advance = w + 1
        if k + 1 < len(order) and order[k + 1] + 1 == i and nxt[order[k + 1]]:
            advance = w - 1
        placed.append((pen, first, w, cell))
        pen += advance
    pixels = {}
    for at, first, w, cell in reversed(placed):
        for p, v in enumerate(cell):
            if v and first <= p % 12 < first + w:
                pixels[(right - pen + at + p % 12 - first, p // 12)] = v
    return pixels


def fake_font():
    """A Font-like object with FakeLookDisc's palette, for gamelook.draw."""
    from petext import tim as T
    clut = T.read(made_up_tim(4, 8, 1, palette=[0, 0x7BDE, 0x0421] + [0] * 13)).palettes[0]
    return type("FakeFont", (), {"clut": clut, "area_clut": clut})()


class RightToLeftLookTest(unittest.TestCase):
    """The game look of right-to-left lines (gamelook.lay_out right_to_left), with no disc: Arabic as
    the game draws it (the model the game's frames were matched to), Hebrew lines ending at x 300 in
    the mirror order of a left-to-right line."""

    def setUp(self):
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("needs Pillow")
        from translator import gamelook as G, letterart as L
        self.G, self.L = G, L

    def pack_with_letters(self, lang: str, key: str, text: str):
        tr = P.Translation(GAME, lang, lang)
        tr.set_text(key, text)
        letters = {s.char: made_up_strokes(k, s.joins) for k, s in enumerate(self.L.every_shape(tr.letters_to_draw()))}
        for ch, rows in letters.items():
            tr.set_letter(ch, rows)
        checked = tr.check_row(key)
        self.assertEqual(checked.errors, [])
        pack = self.G.Pack.read(checked.preview.pack)
        return pack, self.G.Font(FakeLookDisc(), pack), letters

    def test_an_arabic_word_is_joined_right_to_left_and_ends_at_300(self):
        G = self.G
        key = plain_dialogue(1)[0]
        for word in ("سلام", "بيت", "مستشفى", "لا", "قط"):
            pack, font, letters = self.pack_with_letters("ar", key, word)
            look = G.lay_out(font, pack.text(key), right_to_left=True)
            self.assertEqual((look.notes, len(look.screens)), ([], 1))
            drawn = G.draw(font, look.screens[0])
            got = {(i // 3 % G.W, i // 3 // G.W - G.LINE_Y): drawn[i:i + 3] for i in range(0, len(drawn), 3)
                   if drawn[i:i + 3] != b"\0\0\0"}
            want = rtl_model({ch: shadow_of(rows) for ch, rows in letters.items()}, word)
            self.assertEqual(set(got), set(want), word)
            stroke = bytes(G.rgb_of(font.clut[1]))
            self.assertEqual({p for p, c in got.items() if c == stroke}, {p for p, v in want.items() if v == 1}, word)
            # the rightmost letter's pen moves one past its shadow column to the line's end
            self.assertEqual(max(x for x, _ in want), G.RIGHT_EDGE - 2, word)

    def test_a_hebrew_line_is_the_mirror_order_ending_at_300(self):
        G = self.G
        from petext import rtl
        key = plain_dialogue(1)[0]
        text = "שלום Aya (1997)!"
        pack, font, _ = self.pack_with_letters("he", key, text)
        placed = G.lay_out(font, pack.text(key), right_to_left=True).screens[0]
        xs = [p.x - p.letter.x for p in placed]
        self.assertEqual(xs, sorted(xs))                  # placed left to right
        last = placed[-1]
        self.assertEqual(last.x - last.letter.x + last.letter.advance, G.RIGHT_EDGE)
        # the Hebrew letters (each painted its own way) in the order the renderer draws them
        visual = rtl.visual(text)
        hebrew = [ch for ch in visual if "א" <= ch <= "ת"]
        cells = {tuple(G._resolve(font, ord(ch), True).pixels): ch for ch in hebrew}
        self.assertEqual(len(cells), len(set(hebrew)))
        self.assertEqual([cells[tuple(p.letter.pixels)] for p in placed if tuple(p.letter.pixels) in cells], hebrew)
        self.assertEqual(len(placed), len(visual))         # spaces are letters too (blank ones)
        left = G.lay_out(font, pack.text(key)).screens[0]
        self.assertEqual(left[0].x - left[0].letter.x, G.LINE_X)      # left to right still starts at 20

    def test_the_first_letter_queued_is_drawn_in_front(self):
        """The box queues its letters with AddPrim, so the first is drawn last (Textbox_EmitLetter)."""
        G = self.G
        a, b = G.Letter(0, [1] * 144, 12, 6), G.Letter(0, [2] * 144, 12, 6)
        font = fake_font()
        out = G.draw(font, [G.Placed(0, 0, a, G.GREY), G.Placed(6, 0, b, G.GREY)])
        both = 8 * 3                                      # x 8, y 0: both letters cover it
        self.assertEqual(out[both:both + 3], bytes(G.rgb_of(font.clut[1])))
        self.assertEqual(out[14 * 3:15 * 3], bytes(G.rgb_of(font.clut[2])))


class GameLookRuleTest(unittest.TestCase):
    """The game look's rules with no disc: the room scripts' opcodes are the game's, and letters
    are placed as the compiler measures them."""

    def test_the_opcode_list_is_the_games(self):
        from petext.script import opcodes
        from translator import gamelook as G
        if not (HERE.parent / "src/main/script/flow_opcodes.c").is_file():
            self.skipTest("needs the port's source (src/main/script/flow_opcodes.c)")
        self.assertEqual(set(json.loads(G.OPCODES.read_text(encoding="utf-8"))), opcodes(HERE.parent))

    def test_letters_are_placed_as_the_compiler_measures_the_lines(self):
        from translator import gamelook as G
        key = name_dialogue()
        text = "¡Ñandú, {name}! ¿Qué tal? Ça et lì: œuvre.{br}Žluťoučký kůň úpěl ďábelské ódy."
        result = P.check_row(GAME, key, text, "es", "Español", default_name="Íñigo")
        pv = result.preview
        self.assertEqual(result.errors, [], result.errors)
        pack = G.Pack.read(pv.pack)
        look = G.lay_out(G.Font(FakeLookDisc(), pack), pack.text(key), G.name_bytes(pack, pv.shown_name))
        self.assertEqual(len(look.screens), len(pv.screens))
        for placed, screen in zip(look.screens, pv.screens):
            lines = {}
            for p in placed:
                lines.setdefault(p.y, []).append(p)
            ends = [ps[-1].x - ps[-1].letter.x + ps[-1].letter.advance - G.LINE_X for _, ps in sorted(lines.items())]
            self.assertEqual(ends, [w for w in screen.widths if w], (ends, screen.widths))
        self.assertEqual(look.notes, [])

    def test_the_compiler_measures_a_grave_as_the_game_draws_it(self):
        """"à" is code 0x4D, whose entry in the game's own width table is a 0 the game never draws;
        the language mod puts the letter in with its base letter's width, 8 px (install_widths), and a
        game run with "à" in the first box draws it so (the game look matches that frame pixel for
        pixel; with 0 px, 299 pixels differ). The compiler measured it 0 px until 2026-11-12."""
        from petext import gametext as gt, layout
        self.assertEqual((gt.box_advance(0x4D), gt.base_advance(0x4D), layout.advance(bytes([0x4D])),
                          gt.lines(bytes([0x4D, 0x4D, 0xF7, 0x4D]))), (8, 8, 8, [16, 8]))


class FakeMenuDisc:
    """menulook.MenuDisc with no disc: the manifest's menu metrics, the help window where the boot
    program has it, a blank page and the menus' letter colours."""

    def __init__(self):
        from translator import menulook as M
        self.metrics = GAME.metrics
        self.windows = [(0, 0, 0, 0)] * M.SIMPLE_COUNT
        self.windows[M.HELP_WINDOW] = (8, 12, 300, 20)
        self.page = bytes(64 * 254 * 2)
        self.glyphs = [(0, 0, 0, 0, 0, 0)] * 0x100

    def colours(self, clut):
        return [0, 0x7FFF, 0x0421, 0x7FFF] + [0x0421] * 12

    def index(self, u, v):
        return 0


class BoxMenuDisc(FakeMenuDisc):
    """FakeMenuDisc with the boxes' and the config menu's windows and grids as the boot program has them
    on both USA discs (EnglishTest.test_every_box_row_is_drawn_in_its_box checks them)."""

    WINDOWS = {0x02: (0, 0, 60, 52), 0x03: (0, 0, 60, 36), 0x20: (28, 32, 72, 100), 0x21: (100, 32, 62, 36),
               0x23: (100, 48, 72, 36), 0x27: (0, 0, 160, 50), 0x28: (0, 0, 100, 50), 0x29: (0, 0, 200, 50),
               0x2A: (0, 0, 200, 50), 0x2E: (100, 64, 184, 40), 0x31: (0, 0, 0, 0), 0x3D: (0, 0, 100, 50)}
    GRIDS = {0x02: (2, 2, 1, 3, 3, 56, 16, 16), 0x03: (2, 2, 1, 2, 2, 56, 16, 16), 0x20: (2, 2, 1, 6, 6, 68, 16, 16),
             0x21: (2, 2, 1, 2, 2, 58, 16, 16), 0x23: (2, 2, 1, 2, 2, 68, 16, 0), 0x27: (0,) * 8,
             0x28: (10, 30, 1, 1, 1, 60, 16, 0), 0x29: (0, 0, 2, 1, 1, 64, 16, 0), 0x2A: (0, 0, 2, 1, 1, 64, 16, 0),
             0x2E: (2, 12, 3, 1, 1, 32, 16, 12), 0x31: (102, 2, 1, 2, 2, 80, 18, 0), 0x3D: (10, 30, 1, 1, 1, 60, 16, 0)}

    def __init__(self):
        from translator import menulook as M
        super().__init__()
        self.grids = [(0,) * 8] * M.SIMPLE_COUNT
        for n, w in self.WINDOWS.items():
            self.windows[n] = w
        for n, g in self.GRIDS.items():
            self.grids[n] = g


class MenuLookRuleTest(unittest.TestCase):
    """The menus' look with no disc: which lines it draws, letters placed as the compiler measures
    menu lines, and the window's edges."""

    def test_the_help_lines_are_drawn_in_the_help_line(self):
        from translator import menulook as M
        for key in ("desc/005", "misc/040", "misc/088", "sys/014", "sys/092"):
            self.assertEqual(M.context_of(key), "help line", key)
            self.assertTrue(M.can_draw(key), key)
        for key in ("sys/031", "misc/089", "dlg/M001/009", "battle/000", "sys/016"):
            self.assertFalse(M.can_draw(key), key)
        for key in ("item/000", "item/067", "item/234"):
            self.assertEqual(M.context_of(key), "item list")
            self.assertTrue(M.can_draw(key), key)
        for key in ("item/235", "item/248", "item/254"):
            self.assertEqual(M.context_of(key), "parasite energy list")
            self.assertTrue(M.can_draw(key), key)

    def test_the_pe_list_rules(self):
        """The PE list's rows: PE n is item/235 + n; the PEs a game teaches (0-12, Full Recover, Liberate);
        which names the field menu draws undimmed (flag bit 0, the heals only while Aya is hurt); the rows
        shown beside a PE's name (the other PEs a game teaches, its description)."""
        from translator import menulook as M
        self.assertEqual((M.pe_key(0), M.pe_key(18), M.pe_key(19)), ("item/235", "item/253", "item/254"))
        self.assertEqual(M.LEARNABLE, (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 18, 19))
        record = lambda flags: bytes([0xFF, 0, 0x96, 0, 0, flags]) + bytes(26)
        self.assertEqual([M.pe_usable(record(f), p) for p, f in ((0, 3), (18, 3), (3, 2), (19, 2), (7, 2))],
                         [True, True, False, False, False])
        self.assertEqual([M.pe_usable(record(3), p, hurt=False) for p in (0, 1, 2, 18)], [False] * 4)
        self.assertTrue(M.pe_usable(record(1), 5, hurt=False))
        others = [f"item/{k:03d}" for k in list(range(235, 248)) + [253, 254]]
        self.assertEqual(M.context_keys("item/236"), [k for k in others if k != "item/236"] + ["desc/236"])
        self.assertEqual(M.context_keys("item/250"), others + ["desc/250"])

    def test_the_pe_list_puts_the_pe_under_the_cursor_in_its_own_place(self):
        """pe_list with no disc: every PE a game teaches in two columns of 100 x 16 cells, the one being
        translated in its own place under the cursor (a cut one too), the window as many rows as the
        list fills, the heals bright and the rest dimmed."""
        from translator import gamelook as G, menulook as M
        md = FakeMenuDisc()
        md.windows[M.PE_WINDOW] = (28, 32, 204, 132)
        md.grids = [(0,) * 8] * M.SIMPLE_COUNT
        md.grids[M.PE_GRID] = (2, 2, 2, 8, 8, 100, 16, 0)
        md.items = [bytes(32)] * 255
        for p in range(20):
            md.items[235 + p] = bytes([0xFF, 0, 0x96 + p, 0, 0, 3 if p in (0, 1, 2, 18) else 2]) + bytes(26)
        rows = {f"item/{235 + p:03d}": f"PE {p}" for p in range(20)}
        rows["desc/236"] = "Cura 60 PV"
        result = P.check_row(GAME, "item/236", "Curar 2", "es", "Español", context=rows)
        pack = G.Pack.read(result.preview.pack)
        mf = M.MenuFont(G.Font(FakeLookDisc(), pack), md)
        look = M.pe_list(mf, pack.text("item/236"), "item/236", None)
        self.assertEqual(M.grid_cells(md, M.PE_WINDOW, M.PE_GRID, 15), (28, 32, 204, 132, 32, 36, 100, 16, 2))
        self.assertEqual(M.grid_cells(md, M.PE_WINDOW, M.PE_GRID, 10), (28, 32, 204, 84, 32, 36, 100, 16, 2))
        self.assertEqual(look.focus, (34, 50))                         # Heal 2: the right column's first row
        self.assertEqual(look.text.placed[0].x, 132 + look.text.placed[0].letter.x)
        self.assertEqual({p.y for p in look.text.placed}, {37})
        self.assertEqual(look.text.placed[0].colour, M.TEXT_COLOUR)
        self.assertEqual(look.notes, [])
        cut = M.pe_list(mf, pack.text("item/250"), "item/250", None)
        self.assertEqual(cut.focus, (34 + 16 * 6, 50 + 16 * 6))       # PE 15 is the 14th of 16: row 6, right
        self.assertEqual(cut.text.placed[0].x, 132 + cut.text.placed[0].letter.x)
        self.assertEqual(cut.text.placed[0].colour, M.DIM_COLOUR)
        last = M.pe_list(mf, pack.text("item/254"), "item/254", None)
        self.assertEqual(last.focus, (34 + 16 * 7, 50 + 16 * 7))      # Liberate is the 15th of 15
        self.assertEqual(M.pe_list(mf, pack.text("item/253"), "item/253", None).text.placed[0].colour, M.TEXT_COLOUR)
        self.assertEqual(M.pe_list(mf, pack.text("item/253"), "item/253", None, hurt=False).text.placed[0].colour,
                         M.DIM_COLOUR)
        alone = M.pe_list(mf, pack.text("item/236"), "item/236", None, learned=(1,))
        self.assertEqual(alone.focus, (34, 50))

    def test_the_item_list_rules(self):
        """Menu_DrawSlotRow's count box (guns, kinds 1-7, and kinds 0x13 on), its three places with blanks
        before the first digit, which names are drawn undimmed (Inv_RebuildSelectableMask with full HP),
        and the rows shown beside an item's name (the other first items, its description)."""
        from translator import menulook as M
        self.assertEqual([M.count_sprite(k) for k in range(0, 9)], [None, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x20, 0x21, None])
        self.assertEqual([M.count_sprite(k) for k in (0x0A, 0x12, 0x13, 0x14, 0x15)], [None, None, 0x1F, 0x20, 0x21])
        self.assertEqual([M._three(n) for n in (0, 6, 15, 100, 105, 999)],
                         [[-1, -1, 0], [-1, -1, 6], [-1, 1, 5], [1, 0, 0], [1, 0, 5], [9, 9, 9]])
        record = lambda item_id, flags: bytes([1, 0, 0, 0, item_id, flags]) + bytes(26)
        self.assertEqual([M.usable(record(i, f)) for i, f in ((1, 3), (6, 3), (10, 3), (11, 3), (12, 0), (68, 0))],
                         [True, False, False, True, False, False])
        self.assertEqual(M.context_keys("item/005"), ["item/067", "item/149", "item/062", "item/025", "desc/005"])
        self.assertEqual(M.context_keys("item/200"),
                         ["item/067", "item/149", "item/062", "item/025", "item/005", "desc/200"])
        self.assertEqual(M.context_keys("desc/005"), [])

    def test_an_item_name_is_previewed_beside_the_other_names_but_checked_alone(self):
        """check_row's context goes in the preview's pack only: the row's messages are the same, and the
        pack holds the other names and the description for the item list to draw."""
        from translator import gamelook as G, menulook as M
        context = {"item/067": "Pistola M84F", "item/149": "Chaleco N", "item/062": "Porra 1", "item/025": "",
                   "item/005": "Medicina 1", "desc/200": "Abre la sala de ensayo"}
        alone = P.check_row(GAME, "item/200", "Llave del ensayo", "es", "Español")
        beside = P.check_row(GAME, "item/200", "Llave del ensayo", "es", "Español", context=context)
        self.assertEqual((alone.errors, alone.warnings), (beside.errors, beside.warnings))
        pack = G.Pack.read(beside.preview.pack)
        self.assertEqual(G.Pack.read(alone.preview.pack).text("item/067"), None)
        for key, text in context.items():
            self.assertEqual(pack.text(key) is not None, bool(text), key)
        self.assertEqual(beside.preview.menu_px, alone.preview.menu_px)

    def test_letters_are_placed_as_the_compiler_measures_menu_lines(self):
        """The width the menu look lays a line out to is the compiler's (petext/menu.py width), for the
        game's letters, Latin Extended ones, letters with no code, digits and the space (each a pixel
        wider)."""
        from translator import gamelook as G, menulook as M
        for key, text in (("desc/005", "Cura 45: ¡ñandú, œuvre, ð þ!"), ("misc/040", "Usar un objeto 0123"),
                          ("item/001", "Pistola 9 mm")):
            result = P.check_row(GAME, key, text, "es", "Español")
            pv = result.preview
            self.assertEqual(result.errors, [], (key, result.errors))
            pack = G.Pack.read(pv.pack)
            mf = M.MenuFont(G.Font(FakeLookDisc(), pack), FakeMenuDisc())
            laid = M.lay_out(mf, pack.text(key), 0, 0)
            self.assertEqual(laid.width, pv.menu_px, (key, laid.width, pv.menu_px))
            self.assertTrue(laid.placed)
            ends = max(p.x + p.letter.drawn for p in laid.placed)
            self.assertLessEqual(ends, pv.menu_px + 1, key)

    def test_a_windows_edges_are_two_pixels_with_mitred_corners(self):
        """Draw_AllocColorRect's bars as the game's frames show them (the help window, measured): the top
        bar rows y and y+1, the second a pixel in at each end; the left bar from the second row down; the
        added bars at the window colour (side ones at half), the bottom and right ones taken off."""
        from translator import menulook as M
        bars = M.bevel(8, 12, 300, 20)
        (top, rgb_top, s_top), (left, rgb_left, s_left), (right, _, s_right), (bottom, _, s_bottom) = bars
        self.assertEqual((s_top, s_left, s_right, s_bottom), (1, 1, -1, -1))
        self.assertEqual((rgb_top, rgb_left), ((0x40, 0x40, 0x40), (0x20, 0x20, 0x20)))
        rows = {}
        for x, y in top:
            rows.setdefault(y, []).append(x)
        self.assertEqual({y: (min(xs), max(xs), len(xs)) for y, xs in rows.items()},
                         {12: (8, 307, 300), 13: (9, 306, 298)})
        self.assertEqual({x for x, _ in left}, {8, 9})
        self.assertNotIn((8, 12), left)
        self.assertIn((8, 13), left)
        self.assertTrue(all(30 <= y <= 31 for _, y in bottom) and all(306 <= x <= 307 for x, _ in right))

    def test_the_boxes_rows_and_what_is_shown_beside_them(self):
        """Which box draws each row of the menus' boxes and choices, and the rows each is shown with: the
        name a confirm label is joined with, the choices, a message's second line and its OK, a config
        row's hint in the help line."""
        from translator import menulook as M
        where = {"sys/000": "item actions", "sys/003": "item actions", "sys/004": "confirm label",
                 "sys/005": "OK/cancel choice", "sys/007": "dialog line", "sys/008": "OK button",
                 "sys/041": "config menu", "sys/046": "sound choice", "sys/049": "cursor choice",
                 "sys/051": "colour options", "sys/054": "colour channel", "sys/064": "progress box",
                 "sys/070": "progress box", "sys/074": "confirm label", "sys/099": "dialog line",
                 "sys/108": "yes/no choice"}
        for key, place in where.items():
            self.assertEqual(M.context_of(key), place, key)
            self.assertTrue(M.can_draw(key), key)
        self.assertEqual(M.context_keys("sys/004"), ["item/005", "sys/005", "sys/006"])
        self.assertEqual(M.context_keys("sys/005"), ["item/005", "sys/004", "sys/006"])
        self.assertEqual(M.context_keys("sys/026"), ["item/235", "sys/005", "sys/006"])
        self.assertEqual(M.context_keys("sys/113"), ["item/062", "sys/005", "sys/006"])
        self.assertEqual(M.context_keys("sys/073"), ["sys/071", "sys/108", "sys/109", "sys/074"])
        self.assertEqual(M.context_keys("sys/008"), ["sys/084"])
        self.assertEqual(M.context_keys("sys/084"), ["sys/008"])
        self.assertEqual(M.context_keys("sys/061"), ["sys/063", "sys/008"])
        self.assertEqual(M.context_keys("sys/003"), ["sys/001", "sys/002"])
        self.assertEqual(M.context_keys("sys/041"), ["sys/040", "sys/042", "sys/043", "misc/050"])
        self.assertEqual(M.context_keys("sys/047"), ["sys/040", "sys/041", "sys/042", "sys/043", "sys/046", "misc/059"])
        self.assertEqual(M.context_keys("sys/052"), ["sys/040", "sys/041", "sys/042", "sys/043", "sys/053", "sys/054",
                                                     "sys/055", "sys/051", "misc/067"])
        self.assertEqual(M.context_keys("sys/070"), [])
        self.assertEqual(M.joined(b"ab\xff", None, b"cd\xffzz"), b"abcd\xff")

    def box_look(self, key, text, context):
        """A box drawn with no disc (BoxMenuDisc): the row in Spanish beside `context`."""
        from translator import gamelook as G, menulook as M
        result = P.check_row(GAME, key, text, "es", "Español", context=context)
        self.assertEqual(result.errors, [], (key, result.errors))
        pack = G.Pack.read(result.preview.pack)
        mf = M.MenuFont(G.Font(FakeLookDisc(), pack), BoxMenuDisc())
        return mf, M.look(mf, pack.text(key), key, None)

    @staticmethod
    def left(text):
        return text.placed[0].x - text.placed[0].letter.x

    @staticmethod
    def lit(look, x, y):
        """Whether something was added to the black screen at (x, y) (the cursor's tile)."""
        return look.rgb[(y * 320 + x) * 3] > 0

    def test_a_message_box_is_as_wide_as_its_line_with_its_ok_at_the_right(self):
        from translator import menulook as M
        mf, look = self.box_look("sys/084", "Partida cargada correctamente", {"sys/008": "Vale"})
        width = M.measure(mf, mf.pack.text("sys/084"))
        x, w = (300 - max(100, width)) >> 1, max(100, width) + 20
        self.assertEqual(look.focus, (95, 145))
        self.assertEqual(self.left(look.text), x + ((w - (width + 4)) >> 1))
        self.assertEqual({p.y for p in look.text.placed}, {105 + 1})
        ok = M.lay_out(mf, mf.pack.text("sys/008"), 0, 0).width
        _, ok_look = self.box_look("sys/008", "Vale", {"sys/084": "Partida cargada correctamente"})
        self.assertEqual(self.left(ok_look.text), x + w - 0x44 + 2 + ((60 - (ok + 4)) >> 1))
        self.assertEqual({p.y for p in ok_look.text.placed}, {95 + 30 + 2 + 1})
        self.assertTrue(self.lit(look, x + w - 0x44 + 3, 95 + 30 + 12))           # the cursor on OK
        # a box of two lines (sys/061 over sys/063) is a line taller, OK under the second
        _, two = self.box_look("sys/063", "Sin bloque libre", {"sys/061": "Tarjeta de memoria 1", "sys/008": "Vale"})
        self.assertEqual(two.focus, (95, 95 + 50 + M.LINE_STEP))
        self.assertEqual({p.y for p in two.text.placed}, {105 + M.LINE_STEP + 1})
        self.assertEqual(two.notes, [])
        _, alone = self.box_look("sys/063", "Sin bloque libre", {})
        self.assertIn("a row the file doesn't hold is left out", alone.notes)

    def test_a_confirm_box_joins_the_name_to_its_label_and_puts_the_cursor_where_the_game_does(self):
        from translator import menulook as M
        rows = {"item/005": "Medicina 1", "sys/005": "Sí", "sys/006": "Cancelar"}
        mf, look = self.box_look("sys/004", " se tirará", rows)
        whole = M.joined(mf.pack.text("item/005"), mf.pack.text("sys/004"))
        width = max(0x78, M.measure(mf, whole))
        x, w = (300 - width) >> 1, width + 20
        self.assertEqual(look.focus, (95, 145))
        self.assertEqual(self.left(look.text), x + ((w - (width + 4)) >> 1))
        self.assertEqual(look.text.width, M.measure(mf, whole))
        gx, gy = x + ((w - 0x80) >> 1), 95 + 50 - 0x14
        # Menu_StepItemDetailPanel puts the cursor on Cancel; Heal's "will be used" on OK
        self.assertEqual((self.lit(look, gx + 3, gy + 12), self.lit(look, gx + 64 + 3, gy + 12)), (False, True))
        _, used = self.box_look("sys/026", " se usará", {"item/235": "Curar 1", "sys/005": "Sí", "sys/006": "No"})
        ux = (300 - max(0x78, used.text.width)) >> 1
        ugx = ux + ((max(0x78, used.text.width) + 20 - 0x80) >> 1)
        self.assertEqual((self.lit(used, ugx + 3, gy + 12), self.lit(used, ugx + 64 + 3, gy + 12)), (True, False))
        # a choice being translated gets the cursor, and is the text the preview points at
        _, cancel = self.box_look("sys/006", "Anular", rows)
        self.assertEqual({p.y for p in cancel.text.placed}, {gy + 2 + 1})
        _, ok = self.box_look("sys/005", "Vale", rows)
        self.assertEqual({p.y for p in ok.text.placed}, {gy + 2 + 1})
        self.assertGreater(self.left(cancel.text), self.left(ok.text) + 32)

    def test_an_items_actions_and_the_progress_box_sit_in_the_middle_of_the_screen(self):
        from translator import menulook as M
        mf, reload_ = self.box_look("sys/003", "Recargar", {"sys/001": "Mover", "sys/002": "Tirar"})
        width = M.lay_out(mf, mf.pack.text("sys/003"), 0, 0).width
        self.assertEqual(reload_.focus, (94, 94 + 52))                       # window 2: 60 x 52 in the middle
        self.assertEqual(self.left(reload_.text), 130 + 2 + 2 + ((56 - (width + 4)) >> 1))
        self.assertEqual({p.y for p in reload_.text.placed}, {94 + 2 + 2 + 1})
        _, move = self.box_look("sys/001", "Mover", {"sys/000": "Usar", "sys/002": "Tirar"})
        self.assertEqual({p.y for p in move.text.placed}, {94 + 2 + 2 + 16 + 1})
        self.assertTrue(self.lit(move, 133, 94 + 2 + 16 + 12))               # the cursor on Move
        self.assertFalse(self.lit(move, 133, 94 + 2 + 12))
        mf, saving = self.box_look("sys/069", "Guardando", {})
        width = M.lay_out(mf, mf.pack.text("sys/069"), 0, 0).width
        self.assertEqual(saving.focus, (95, 145))                            # window 0x27: 160 x 50
        self.assertEqual(self.left(saving.text), 80 + ((160 - (width + 4)) >> 1))
        self.assertEqual({p.y for p in saving.text.placed}, {105 + 1})

    def test_the_config_menu_shows_the_rows_hint_and_its_choices(self):
        from translator import menulook as M
        rows = {"sys/040": "Sonido", "sys/042": "Ventana", "sys/043": "Posición", "misc/050": "Elige el cursor"}
        mf, cursor = self.box_look("sys/041", "Cursor", rows)
        self.assertEqual({p.y for p in cursor.text.placed}, {36 + 16 + 1})
        self.assertEqual(cursor.notes, [])
        self.assertTrue(self.lit(cursor, 33, 34 + 16 + 12))                  # the cursor on the second row
        self.assertEqual(cursor.focus, (0, 32 + 4 * 16 + 4))                 # four rows shown, with the help line
        _, unseen = self.box_look("sys/044", "MAX", rows)
        self.assertIn("a row the game never shows (its list ends at Position)", unseen.notes)
        # Stereo in use in a new game: the blue tile on its row; the cursor on Mono
        _, mono = self.box_look("sys/046", "Mono", {"sys/047": "Estéreo"})
        x0, y0 = 100 + 2 + 2, 32 + 2 + 2
        self.assertEqual({p.y for p in mono.text.placed}, {y0 + 1})
        blue = lambda y: mono.rgb[(y * 320 + x0 + 1) * 3: (y * 320 + x0 + 1) * 3 + 3]
        self.assertGreater(blue(y0 + 16 + 1)[2], blue(y0 + 16 + 1)[0])
        self.assertEqual(blue(y0 + 1)[2], blue(y0 + 1)[0])
        # R, G, B each in its own colour
        _, green = self.box_look("sys/054", "V", {"sys/053": "R", "sys/055": "A"})
        self.assertEqual({p.colour for p in green.text.placed}, {(0, 0x80, 0)})
        _, done = self.box_look("sys/051", "Hecho", {"sys/052": "Inicial"})
        self.assertEqual({p.colour for p in done.text.placed}, {M.TEXT_COLOUR})

    def test_the_scene_is_darkened_and_the_bars_drawn_into_it(self):
        from translator import menulook as M
        grey = bytes([132, 132, 132]) * (M.W * M.H)          # 5-bit 16 everywhere
        screen = M.Screen(grey)
        self.assertEqual(screen.v[0], 8)
        for pixels, rgb, sign in M.bevel(8, 12, 300, 20):
            screen.blend(pixels, rgb, sign)
        at = lambda x, y: screen.v[(y * M.W + x) * 3]
        self.assertEqual((at(100, 12), at(8, 20), at(100, 31), at(307, 20), at(100, 20)), (16, 12, 0, 4, 8))


class LettersDiscTest(unittest.TestCase):
    """The letters PNG's style guide from the real discs (PE_TEST_DISC, PE_TEST_DISC2): the game's own
    letters, as the dialogue font page has them, in the font's colours; the same from both discs."""

    def test_the_style_strip_is_the_games_own_letters(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("needs Pillow")
        from translator import gamelook as G, letterart as L
        discs = [d for d in (os.environ.get("PE_TEST_DISC"), os.environ.get("PE_TEST_DISC2")) if d and Path(d).is_file()]
        if not discs:
            self.skipTest("needs PE_TEST_DISC (and PE_TEST_DISC2)")
        rows = L.wanted("el")
        pngs = []
        for disc in discs:
            look = G.DiscLook(disc)
            colours = look.font.palettes[0]
            png = L.png(rows, "Greek", {}, L.style_letters(look.page, colours))
            img = Image.open(io.BytesIO(png)).convert("RGB")
            self.assertEqual(img.size, L.size(rows, True))
            top = L.MARGIN + L.LINE * 3 + 6 + 2
            for k, ch in enumerate(L.STYLE_SAMPLE):
                cell = G._cell(look.page, gt.CODE_OF[ch])
                self.assertTrue(any(v == 1 for v in cell), ch)
                for i, v in enumerate(cell):
                    x, y = L.MARGIN + 140 + k * 12 + i % 12, top + i // 12
                    want = G.rgb_of(colours[v]) if v and colours[v] else L.BACK
                    self.assertEqual(img.getpixel((x, y)), want, (disc, ch, i))
            self.assertEqual({s.char: None for s in L.every_shape(rows)}, L.from_png(png, rows).drawn)
            pngs.append(png)
        self.assertEqual(len(set(pngs)), 1)


class PictureDiscTest(unittest.TestCase):
    """The pictures with words (title screen, city map, battle words, day cards, Tutorial, Game Over) from the real discs (PE_TEST_DISC,
    PE_TEST_DISC2): each one read out as a PNG and loaded back gives the disc's bytes, and is the same
    on both discs. With the player packs (PE_TEST_PLAYER_PACKS) too: each pack's pictures as it shows
    them, out as a PNG and back, change nothing (their own colours included)."""

    def discs(self):
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("needs Pillow")
        from translator import artwork as A
        discs = [d for d in (os.environ.get("PE_TEST_DISC"), os.environ.get("PE_TEST_DISC2")) if d and Path(d).is_file()]
        if not discs:
            self.skipTest("needs PE_TEST_DISC (and PE_TEST_DISC2)")
        return [A.Disc(d) for d in discs]

    def test_the_map_names_are_the_strips_the_map_draws(self):
        """Entry 72's second picture: 4-bit, 256x143, three palette rows (143 normal, 144 the place
        you are at, 145 the band), ten 13-row strips with words in both halves, at the same place on
        both discs."""
        from petext import tim as T
        from translator import artwork as A
        pic = A.BY_ID["map/place-names"]
        for disc in self.discs():
            t = T.read(disc.english[pic.id])
            self.assertEqual((t.bits, t.size, len(t.palettes)), (4, (256, 143), 3))
            self.assertEqual(disc.span(pic)[0] // 2048, disc.sectors[72] + 16)
            numbers = T.indices(t)
            for row in range(10):
                for half in (0, 128):
                    ink = sum(1 for y in range(row * 13, row * 13 + 12) for x in range(half, half + 127)
                              if numbers[y * 256 + x])
                    self.assertGreater(ink, 100, (row, half))

    def test_the_battle_words_are_the_picture_the_fight_draws_from(self):
        """Entry 11: one picture, 4-bit, 256x32, eight palette rows, the first of them the 16 grey steps
        every word is drawn with (status_hud.c tints them), at the entry's first sector on both discs;
        the redrawn words' boxes hold ink in the English picture."""
        from petext import tim as T
        from translator import artwork as A
        pic = A.BY_ID["battle/words"]
        boxes = {"Miss": (80, 0, 24, 8), "NO TARGET": (88, 8, 56, 7), "ENERGY SHOT": (144, 8, 64, 7),
                 "SLOW": (180, 15, 28, 7), "ALL": (212, 16, 20, 7), "RANDOM": (0, 24, 40, 7),
                 "SCAN": (40, 24, 40, 7), "CONFUSE": (80, 24, 48, 7), "BULLETS": (88, 15, 36, 5),
                 "TARGET": (124, 15, 36, 5), "AT": (80, 20, 8, 4), "HP": (88, 20, 8, 4), "PE": (96, 20, 8, 4)}
        for disc in self.discs():
            t = T.read(disc.english[pic.id])
            self.assertEqual((t.bits, t.size, len(t.palettes)), (4, (256, 32), 8))
            self.assertEqual(disc.span(pic)[0], disc.sectors[11] * 2048)
            self.assertEqual([T.rgb_of(c)[0] for c in t.palettes[0]],
                             sorted(T.rgb_of(c)[0] for c in t.palettes[0]))
            self.assertTrue(all(len(set(T.rgb_of(c))) == 1 for c in t.palettes[0]))
            numbers = T.indices(t)
            for word, (x0, y0, w, h) in boxes.items():
                ink = sum(1 for y in range(y0, y0 + h) for x in range(x0, x0 + w) if numbers[y * 256 + x])
                self.assertGreater(ink, w * h // 8, word)

    def test_game_over_is_the_glyph_the_end_of_a_lost_fight_draws(self):
        """Game Over on both discs: glyph 0x8B of the resident pack's table (120x24 at u 0 v 184 of page 448,0,
        colour row 448,233), rows 184-207 of entry 9's raw image at VRAM 448,0 (64x254 words, its pixels from
        the entry's byte 20); no other glyph overlaps it or uses its colours; colour 0 see-through and no colour
        with the see-through bit (so no blending); and the tool's in-game picture is an independent model of
        what the GPU draws at x 100, y 100 with brightness 120 and 116."""
        import struct
        from petext import disc as discs
        from translator import artwork as A
        pic = A.GAME_OVER_WORDS
        for disc in self.discs():
            game = discs.Game(disc.path)
            try:
                resident = game.resident_pack()
                entry = game.pe(game.sectors[9], game.sectors[10] - game.sectors[9])
            finally:
                game.disc.f.close()
            self.assertEqual(struct.unpack_from("<I4H", entry, 8), (12 + 64 * 254 * 2, 448, 0, 64, 254))
            self.assertEqual(disc.segments[pic.id], [(20 + (184 + r) * 128, 60) for r in range(24)] + [(20 + 233 * 128, 32)])
            table = struct.unpack_from("<i", resident, 8)[0]
            end = struct.unpack_from("<i", resident, 12)[0]                       # the next table of the pack
            glyphs = [struct.unpack_from("<BBHBBBB", resident, table + 8 * k) for k in range((end - table) // 8)]
            self.assertEqual(glyphs[0x8B][:6], (0, 184, 233 << 6 | 448 >> 4, 120, 24, 0))
            for k, (u, v, clut, w, h, _, _) in enumerate(glyphs):
                if k != 0x8B and w and h:
                    self.assertFalse(u < 120 and v < 208 and v + h > 184, k)
                    self.assertNotEqual(clut, glyphs[0x8B][2], k)
            english = disc.english[pic.id]
            colours = struct.unpack_from("<16H", entry, 20 + 233 * 128)
            self.assertEqual(A.glyph_colours(pic, english), colours)
            self.assertEqual(colours[0], 0)
            self.assertFalse(any(c & 0x8000 for c in colours))
            numbers = [entry[20 + (184 + y) * 128 + x // 2] >> 4 * (x & 1) & 15 for y in range(24) for x in range(120)]
            self.assertEqual(A.glyph_numbers(pic, english), numbers)
            self.assertGreater(sum(1 for n in numbers if n), 900)
            dither = ((-4, 0, -3, 1), (2, -2, 3, -1), (-3, 1, -4, 0), (3, -1, 2, -2))
            for brightness in (120, 116):
                model = []
                for k, n in enumerate(numbers):
                    x, y = 100 + k % 120, 100 + k // 120
                    parts = [max(0, min(255, (colours[n] >> s & 31) * brightness // 16 + dither[y % 4][x % 4])) >> 3
                             for s in (0, 5, 10)]
                    model.append((0, 0, 0) if n == 0 else tuple((p * 255 + 15) // 31 for p in parts))
                drawn = A.in_game(pic, english, brightness).tobytes()
                self.assertEqual([tuple(drawn[k:k + 3]) for k in range(0, len(drawn), 3)], model, brightness)

    def test_the_menu_words_are_glyphs_of_the_page_each_in_its_own_box(self):
        """Each menu word on both discs: its glyph of the resident pack's table is the size listed, 4-bit, its
        colour row on entry 9's page; its bytes are that box's rows of the page and the colour row (read here
        from the entry on its own); its box overlaps no other glyph's; colour 0 is see-through and no other
        colour shows as black in its PNG (so black is see-through, as the tip says); every one has ink."""
        import struct
        from petext import disc as discs, tim as T
        from translator import artwork as A
        for disc in self.discs():
            game = discs.Game(disc.path)
            try:
                resident = game.resident_pack()
                entry = game.pe(game.sectors[9], game.sectors[10] - game.sectors[9])
            finally:
                game.disc.f.close()
            table = struct.unpack_from("<i", resident, 8)[0]
            end = struct.unpack_from("<i", resident, 12)[0]
            glyphs = [struct.unpack_from("<BBHBBBB", resident, table + 8 * k) for k in range((end - table) // 8)]
            for pic in A.MENU_WORDS:
                with self.subTest(disc=disc.path.name, picture=pic.id):
                    u, v, clut, w, h, mode, _ = glyphs[pic.glyph]
                    self.assertEqual(((w, h), mode), (pic.size, 0))
                    cx, cy = (clut & 63) * 16, clut >> 6
                    self.assertTrue(448 <= cx < 512 and cy < 254)
                    rows = [(20 + (v + r) * 128 + u // 2, w // 2) for r in range(h)]
                    self.assertEqual(disc.segments[pic.id], rows + [(20 + cy * 128 + (cx - 448) * 2, 32)])
                    english = disc.english[pic.id]
                    numbers = [entry[20 + (v + y) * 128 + (u + x) // 2] >> 4 * ((u + x) & 1) & 15
                               for y in range(h) for x in range(w)]
                    self.assertEqual(A.glyph_numbers(pic, english), numbers)
                    colours = struct.unpack_from("<16H", entry, 20 + cy * 128 + (cx - 448) * 2)
                    self.assertEqual(A.glyph_colours(pic, english), colours)
                    self.assertFalse([c for c in colours if c and T.rgb_of(c) == (0, 0, 0)])
                    self.assertGreater(sum(1 for n in numbers if colours[n]), 10)
                    for k, (u2, v2, _, w2, h2, _, _) in enumerate(glyphs):
                        if k != pic.glyph and w2 and h2:
                            self.assertFalse(u < u2 + w2 and u2 < u + w and v < v2 + h2 and v2 < v + h, k)

    def test_a_redrawn_menu_word_keeps_the_rest_of_its_page(self):
        """With each player pack, loading a redrawn menu word changes only that word's bytes of the page, and
        the word is then the one loaded; the French, German and Spanish packs' own redrawn words stay."""
        from translator import artwork as A
        packs = player_packs()
        if not packs:
            self.skipTest("needs PE_TEST_PLAYER_PACKS (a folder of .pelang translations)")
        disc = self.discs()[0]
        for path in packs:
            pieces = A.read_pieces(zip_parts(path))
            for pic in A.MENU_WORDS:
                with self.subTest(pack=path.name, picture=pic.id):
                    base, data = disc.around[pic.id]
                    mine = set(k for a, n in disc.segments[pic.id] for k in range(a, a + n))
                    n = pic.size[0] * pic.size[1] // 2
                    new = bytes(b ^ 0x11 for b in disc.english[pic.id][:n]) + disc.english[pic.id][n:]
                    before = A._lay(base, data, pieces)[0]
                    put = A.put(disc, pic, pieces, new)
                    after = A._lay(base, data, put)[0]
                    self.assertEqual(A.shown(disc, pic, put), new)
                    self.assertEqual([k for k in range(len(data)) if before[k] != after[k] and k not in mine], [])

    def test_a_redrawn_game_over_keeps_the_rest_of_its_page(self):
        """Game Over shares its page with the menus' other words, which the French, German and Spanish packs
        redrew: loading a Game Over over each pack changes only Game Over's bytes of the page."""
        from translator import artwork as A
        packs = player_packs()
        if not packs:
            self.skipTest("needs PE_TEST_PLAYER_PACKS (a folder of .pelang translations)")
        pic, disc = A.GAME_OVER_WORDS, self.discs()[0]
        base, data = disc.around[pic.id]
        mine = set(k for a, n in disc.segments[pic.id] for k in range(a, a + n))
        new = bytes(b ^ 0x11 for b in disc.english[pic.id][:1440]) + disc.english[pic.id][1440:]
        for path in packs:
            with self.subTest(pack=path.name):
                pieces = A.read_pieces(zip_parts(path))
                before = A._lay(base, data, pieces)[0]
                after = A._lay(base, data, A.put(disc, pic, pieces, new))[0]
                self.assertEqual(A.shown(disc, pic, A.put(disc, pic, pieces, new)), new)
                self.assertEqual([k for k in range(len(data)) if before[k] != after[k] and k not in mine], [])

    def test_the_day_cards_are_their_rooms_views_as_the_screen_shows_them(self):
        """Each day card is its room's view (gamelook.room_views, which the game's frames of Days 2-4
        equal) from its row 4 down: 300 tiles, none shared, on the room's 28 colour rows, the same bytes
        on both discs (the CRCs)."""
        from petext import disc as D
        from translator import artwork as A, gamelook as G
        for disc in self.discs():
            game = D.Game(disc.path)
            try:
                for pic in A.PICTURES:
                    if not isinstance(pic, A.Card):
                        continue
                    layout = disc.layout[pic.id]
                    self.assertEqual((len(layout.tiles), layout.rows), (300, tuple(range(480, 508))))
                    view = G.room_views(game, pic.room)[pic.view]           # rows 0-223
                    mine = A.image(pic, disc.english[pic.id], layout).tobytes()
                    self.assertEqual(mine[:220 * 320 * 3], view[4 * 320 * 3:], pic.id)
            finally:
                game.disc.f.close()

    def test_the_tutorial_screens_are_their_rooms_layers_as_the_game_draws_them(self):
        """Each Tutorial screen (and the menu with each choice picked) is, pixel for pixel, the model the
        game's frames of the CTest original-game-tutorial's route were matched with (every page, 0 pixels
        differing; translator-tutorial checks them again): the room's pictures put in video memory, the
        screen's tile lists drawn from there by depth (deepest first, at one depth the later list first),
        colour 0 see-through, the shadow subtracted. Built here from the disc on its own, not from the
        tool's bytes. Both discs: the same bytes (the CRCs)."""
        import struct
        from petext import disc as D
        from translator import artwork as A, gamelook as G
        fives = [[(c >> s & 31) for s in (0, 5, 10)] for c in range(32768)]

        def model(game, pic, choice=None):
            first, scratch, texture, size = game.rooms[pic.room - 1]
            vram = bytearray(1024 * 512 * 2)
            for blob in (game.pe(first, scratch), game.pe(first + scratch, texture)):
                for off, x, y, w, h in G._uploads(blob):
                    if off + w * h * 2 <= len(blob) and x + w <= 1024 and y + h <= 512:
                        for r in range(h):
                            to = ((y + r) * 1024 + x) * 2
                            vram[to:to + w * 2] = blob[off + r * w * 2:off + (r + 1) * w * 2]
            run = game.pe(first + scratch + texture, size)
            directory = struct.unpack_from("<I", run, 4)[0]
            packed = struct.unpack_from("<I", run, directory + 0x1C)[0]
            geom = struct.unpack_from("<I", run, (packed & 0x3FFFFF) + 4)[0] & 0xFFFFFF
            table = geom + struct.unpack_from("<i", run, geom + 0x14)[0]
            shown = [(choice if choice and e == pic.words[0] else e, dx, dy) for e, dx, dy in pic.layers]
            depth = {e: (struct.unpack_from("<I", run, table + e * 0x38)[0] >> 8) & 0xFFF for e, _, _ in shown}
            screen = [(0, 0, 0)] * (320 * 224)
            for e, dx, dy in sorted(shown, key=lambda t: (-depth[t[0]], -t[0])):
                at = table + e * 0x38
                n = struct.unpack_from("<H", run, at + 0x26)[0]
                pos, uv = struct.unpack_from("<ii", run, at + 0x28)
                sx, sy = struct.unpack_from("<hh", run, at + 0xC)
                for k in range(n):
                    a = struct.unpack_from("<I", run, at + pos + k * 4)[0]
                    w0 = struct.unpack_from("<I", run, at + uv + k * 8)[0]
                    x, y = sx + dx + (a >> 22), sy + dy + ((a >> 12) & 1023)
                    u, v = run[at + uv + k * 8 + 4], run[at + uv + k * 8 + 3]
                    clut = ((w0 & 511) * 1024 + ((w0 >> 5) & 0x3F0)) * 2
                    page_x, page_y = (w0 >> 10) & 0x3C0, (w0 >> 7) & 256
                    for j in range(16):
                        for i in range(16):
                            if not (0 <= x + i < 320 and 0 <= y + j < 224):
                                continue
                            index = vram[((page_y + v + j) * 1024 + page_x + (u + i) // 2) * 2 + ((u + i) & 1)]
                            c = vram[clut + index * 2] | vram[clut + index * 2 + 1] << 8
                            if not c:
                                continue
                            p = (y + j) * 320 + x + i
                            if w0 & (1 << 23) and c & 0x8000:             # the shadow: subtracted
                                screen[p] = tuple(max(0, a - b) for a, b in zip(screen[p], fives[c & 0x7FFF]))
                            else:
                                screen[p] = tuple(fives[c & 0x7FFF])
            return bytes(v for five in screen for v in G.rgb_of(five[0] | five[1] << 5 | five[2] << 10))

        for disc in self.discs():
            game = D.Game(disc.path)
            try:
                for pic in A.PICTURES:
                    if not isinstance(pic, A.Screen):
                        continue
                    layout = disc.layout[pic.id]
                    with self.subTest(disc=disc.path.name, screen=pic.id):
                        self.assertEqual(A.image(pic, disc.english[pic.id], layout).tobytes(), model(game, pic))
                        for k, e in enumerate(pic.copies, 1):
                            self.assertEqual(A.screen_image(disc.english[pic.id], layout, k).tobytes(),
                                             model(game, pic, e), k)
                        drawn = [lay for lay in layout.layers if lay.drawn]
                        self.assertEqual([lay.shadow for lay in drawn], [False, True] + [False] * (len(drawn) - 2))
                        self.assertEqual(drawn[0].depth, 4088)
                        words_depth = {lay.depth for lay in drawn if lay.word and lay.entry == pic.words[0]}
                        self.assertEqual(words_depth, {1243} if pic.room in (362, 366) else {8})
            finally:
                game.disc.f.close()

    def test_the_tune_up_screenshot_on_the_disc_is_one_layer_of_two_screens(self):
        """About Icons 7 and 8 (M362) show the same tile list e7: loading 7's PNG with the screenshot
        changed changes 8 the same; 8's PNG as the game has it keeps that; changed otherwise, refused."""
        disc = self.discs()[0]                   # first: it skips without Pillow or a disc
        from PIL import ImageDraw
        from translator import artwork as A
        seven, eight = A.BY_ID["tutorial/icons-7"], A.BY_ID["tutorial/icons-8"]
        self.assertEqual(A.shared_with(seven), [(eight, 7)])
        img = A.image(seven, disc.english[seven.id], disc.layout[seven.id])
        ImageDraw.Draw(img).rectangle((90, 50, 160, 70), fill=(0, 0, 0))
        pieces = A.put(disc, seven, [], A.from_png(seven, disc.english[seven.id], png_bytes(img), None,
                                                   disc.layout[seven.id]).tim)
        now = A.shown(disc, eight, pieces)
        on_seven = A.image(seven, A.shown(disc, seven, pieces), disc.layout[seven.id])
        on_eight = A.image(eight, now, disc.layout[eight.id])
        self.assertEqual(on_eight.crop((60, 4, 268, 148)).tobytes(), on_seven.crop((60, 4, 268, 148)).tobytes())
        self.assertNotEqual(now, disc.english[eight.id])
        game_png = A.png(eight, disc.english[eight.id], disc.layout[eight.id])
        kept = A.from_png(eight, disc.english[eight.id], game_png, now, disc.layout[eight.id])
        self.assertEqual(kept.tim, now)
        other = A.image(eight, now, disc.layout[eight.id])
        ImageDraw.Draw(other).rectangle((100, 100, 140, 120), fill=(255, 255, 255))
        with self.assertRaises(A.Disagree):
            A.from_png(eight, disc.english[eight.id], png_bytes(other), now, disc.layout[eight.id])

    def test_each_player_packs_pictures_out_and_back_change_nothing(self):
        from translator import artwork as A
        packs = player_packs()
        if not packs:
            self.skipTest("needs PE_TEST_PLAYER_PACKS (a folder of .pelang translations)")
        disc = self.discs()[0]
        for path in packs:
            pieces = A.read_pieces(zip_parts(path))
            for pic in A.PICTURES:
                with self.subTest(pack=path.name, picture=pic.id):
                    shown, layout = A.shown(disc, pic, pieces), disc.layout.get(pic.id)
                    loaded = A.from_png(pic, disc.english[pic.id], A.png(pic, shown, layout), shown, layout)
                    self.assertEqual((loaded.tim, loaded.notes), (shown, []))
                    again = A.put(disc, pic, pieces, loaded.tim)
                    for other in A.PICTURES:
                        self.assertEqual(A.shown(disc, other, again), A.shown(disc, other, pieces), other.id)

    def test_every_picture_out_as_png_and_back_is_the_discs_bytes(self):
        from translator import artwork as A
        read = self.discs()
        for pic in A.PICTURES:
            with self.subTest(picture=pic.id):
                english = read[0].english[pic.id]
                self.assertTrue(all(d.english[pic.id] == english for d in read))
                layout = read[0].layout.get(pic.id)
                self.assertEqual(A.from_png(pic, english, A.png(pic, english, layout), None, layout).tim, english)
                self.assertEqual(A.put(read[0], pic, [], english), [])           # the game's: no pieces


class PlayerPackTest(unittest.TestCase):
    """With real translations (PE_TEST_PLAYER_PACKS: a folder of .pelang files): the five player
    packs, each made from a fan patch with its looser rules and its own letter pictures."""

    def setUp(self):
        self.packs = player_packs()
        if not self.packs:
            self.skipTest("needs PE_TEST_PLAYER_PACKS (a folder of .pelang translations)")
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_each_opens_whole_and_saves_back_byte_for_byte(self):
        """Nothing of the fan patch's import is needed: the pack's own rules and letters are enough
        to write it back exactly, its other parts (pictures, readme) too, and again after reopening."""
        for path in self.packs:
            with self.subTest(pack=path.name):
                before = path.read_bytes()
                t = P.Translation.open(path, GAME)
                entries = pelang.read_pack(path)[2]
                held = {GAME.by_hash[h] for h in entries if h in GAME.by_hash}
                self.assertEqual(set(t.texts), held)
                self.assertEqual(t.unknown, 0)
                self.assertTrue(all(t.texts[k] for k in held))
                self.assertEqual(t.locked, {})
                self.assertTrue(t.from_patch)
                self.assertEqual(t.rules.patch_rows, frozenset(held))
                copy = self.dir / path.name
                report = t.save(copy)
                self.assertEqual(report.left_out, {})
                assert_same_but_new_bank_names(self, copy, path)       # text.bin, pictures, readme
                again = P.Translation.open(copy, GAME)
                self.assertEqual((again.from_work, again.from_patch, again.locked), (True, True, {}))
                self.assertEqual(again.texts, t.texts)
                self.assertEqual(again.rules.patch_rows, frozenset(held))
                twice = self.dir / ("again " + path.name)
                again.save(twice)
                assert_same_but_new_bank_names(self, twice, path)
                self.assertEqual(path.read_bytes(), before)
                print(f"\n{path.name}: {t.name} ({t.lang}), {len(held)} rows, {len(t.own_letters)} letter "
                      f"pictures of its own, {len(t.own_cjk)} CJK pictures; saves back byte for byte")

    def test_the_city_maps_lines_are_lines_in_the_packs_own_letters(self):
        """A pack that translates the city map has it as dlg/MAP lines, not as raw bytes in its
        pictures (which drew the German and French patches' letter pairs as other letters), and every
        letter of those lines is one its dialogue uses elsewhere."""
        for path in self.packs:
            with self.subTest(pack=path.name):
                t = P.Translation.open(path, GAME)
                lines = {k: v for k, v in t.texts.items() if citymap.placed(k)}
                self.assertFalse(t.map_as_bytes)
                if not lines:
                    continue
                elsewhere = {ch for k, v in t.texts.items() if k.startswith("dlg/") and not citymap.placed(k)
                             for ch in re.sub(r"\{[^}]*\}", "", v)}
                odd = {ch for v in lines.values() for ch in re.sub(r"\{[^}]*\}", "", v)} - elsewhere
                self.assertEqual(odd, set(), f"letters in its map lines that its dialogue never uses")
                print(f"\n{path.name}: {len(lines)} city map lines, in letters its dialogue uses")

    def test_a_line_changed_in_a_fan_pack_keeps_its_rules_and_is_the_only_change(self):
        """A line the strict rules refuse (its waits differ from the English...) is changed: it
        passes under the pack's rules, and it is the only row of the pack that changes."""
        for path in self.packs:
            with self.subTest(pack=path.name):
                t = P.Translation.open(path, GAME)
                strict = P.build(GAME, t.texts, t.lang, t.name, keep_names_from=t.pack,
                                 rules=P.Rules(letters=t.own_letters, cjk=t.own_cjk))
                refused = sorted(k for k in strict.keys_with("error") if k.startswith("dlg/") and k in t.texts)
                key = (refused or sorted(k for k in t.texts if k.startswith("dlg/")))[0]
                changed = one_word_changed(t.text(key))
                check = t.check_row(key, changed)
                self.assertEqual(check.errors, [], key)
                self.assertNotIn(key, t.rules_for(key, changed).patch_rows)
                t.set_text(key, changed)
                copy = self.dir / path.name
                report = t.save(copy)
                self.assertEqual(report.left_out, {})
                was, now = pelang.read_pack(path)[2], pelang.read_pack(copy)[2]
                differ = {GAME.by_hash.get(h, h) for h in set(was) | set(now) if was.get(h) != now.get(h)}
                self.assertEqual(differ, {key} | new_bank_names(was))
                again = P.Translation.open(copy, GAME)
                self.assertEqual((again.text(key), again.from_patch, again.locked), (changed, True, {}))
                self.assertNotIn(key, again.rules.patch_rows)
                self.assertEqual(len(again.rules.patch_rows), len(t.texts) - 1)
                print(f"\n{path.name}: {len(refused)} dialogue lines the strict rules refuse; {key} changed, "
                      f"the only row that differs")

    def test_a_character_no_picture_has_needs_a_font_and_then_stays_in_the_pack(self):
        path = next((p for p in self.packs if P.Translation.open(p, GAME).needs_font()), None)
        if path is None:
            self.skipTest("no Chinese, Japanese or Korean pack among the player packs")
        t = P.Translation.open(path, GAME)
        have = {ch for ch, _ in t.own_cjk}
        new = next(chr(c) for c in range(0x4E00, 0x9FFF) if chr(c) not in have)
        font = a_font_for(new)
        key = sorted(k for k in t.texts if k.startswith("dlg/"))[0]
        text = t.text(key) + new
        missing = t.check_row(key, text)
        self.assertEqual(len(missing.errors), 1, missing.errors)
        self.assertIn(new, missing.errors[0])
        self.assertIn("Font", missing.errors[0])             # in plain words: the button to use
        self.assertNotIn("--cjk-font", missing.errors[0])
        if font is None:
            self.skipTest(f"no font file with U+{ord(new):04X} on this computer (or no Pillow)")
        t.cjk_fonts = [font]
        self.assertEqual(t.check_row(key, text).errors, [])
        t.set_text(key, text)
        copy = self.dir / path.name
        self.assertEqual(t.save(copy).left_out, {})
        again = P.Translation.open(copy, GAME)             # no font: the pack has its picture now
        self.assertEqual(again.locked, {})
        self.assertIn(new, {ch for ch, _ in again.own_cjk})
        other = sorted(k for k in again.texts if k.startswith("dlg/"))[1]
        self.assertEqual(again.check_row(other, again.text(other) + new).errors, [])


START = HERE / "translate.py"

# Runs the window (translate.py, scripted with --report) and then the work a session does outside
# the scripted path (the language list, reopening the file), with an audit hook that lists every
# file opened. sys.argv[1] is where the list goes; the rest are translate.py's arguments.
AUDITED_RUN = r"""
import os, runpy, sys
opened = set()
def hook(event, args):
    if event == "open" and isinstance(args[0], (str, bytes, os.PathLike)):
        opened.add(os.path.abspath(os.fsdecode(args[0])))
sys.addaudithook(hook)
out, start, args = sys.argv[1], sys.argv[2], sys.argv[3:]
code = 0
try:
    sys.argv = [start] + args
    try:
        runpy.run_path(start, run_name="__main__")
    except SystemExit as e:
        code = e.code or 0
    from translator import project as P
    game = P.Game()
    P.languages()
    P.Translation.open(args[0], game)
finally:
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(sorted(opened)))
sys.exit(code)
"""

# Runs the window (translate.py, scripted with --report) with every check of a line raising, as it
# does when Pillow can't be loaded where the window runs and a line needs letters drawn from a font.
BROKEN_CHECK_RUN = r"""
import os, runpy, sys
start, args = sys.argv[1], sys.argv[2:]
sys.path.insert(0, os.path.dirname(start))
from translator import project as P
def broken(*a, **k):
    raise ModuleNotFoundError("No module named 'PIL' (made up by the test)")
P.check_row = broken
sys.argv = [start] + args
runpy.run_path(start, run_name="__main__")
"""


def window_works() -> bool:
    return subprocess.run([sys.executable, "-I", "-c", "import tkinter; tkinter.Tk().destroy()"],
                          capture_output=True).returncode == 0


class DownloadCheckTest(unittest.TestCase):
    """translator/package.py's search of a built download for game text, on made-up builds with a
    made-up translation planted in them (no disc, no PyInstaller needed)."""

    def setUp(self):
        sys.path.insert(0, str(HERE / "translator"))
        import package
        self.K = package
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.lines = [f"Une ligne inventée numéro {n}, assez longue pour être cherchée." for n in range(3)]
        t = P.Translation(GAME, "fr", "Français")
        for key, line in zip(plain_dialogue(3), self.lines):
            t.set_text(key, line)
        self.pack = self.dir / "fr.pelang"
        t.save(self.pack)
        self.needles = package.Needles()
        self.needles.add_pack(self.pack, GAME)

    def tearDown(self):
        self.tmp.cleanup()

    def build_with(self, files: dict) -> Path:
        built = Path(tempfile.mkdtemp(dir=self.dir))
        (built / "data").mkdir()
        for data in P.DATA_FILES:
            (built / data).write_bytes((HERE / data).read_bytes())
        for name, blob in files.items():
            (built / name).write_bytes(blob)
        return built

    def caught(self, files: dict) -> str:
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(SystemExit) as no:
            self.K.look_inside(self.build_with(files), self.needles)
        return str(no.exception)

    def test_a_clean_build_passes(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.K.look_inside(self.build_with({"tool.py": b"print('hello')"}), self.needles)

    def test_the_download_is_named_with_the_release_version(self):
        """Its name carries the version as the game's and the Save Editor's release files do."""
        cmake = (HERE.parent / "CMakeLists.txt").read_text(encoding="utf-8")
        major, minor = re.search(r"^project\(parasite-eve-pc VERSION (\d+)\.(\d+)\.", cmake, re.M).groups()
        self.assertEqual(self.K.release_version(), f"{major}.{minor}")
        sample = self.dir / "CMakeLists.txt"
        sample.write_text("cmake_minimum_required(VERSION 3.20)\nproject(parasite-eve-pc VERSION 1.4.0 LANGUAGES C)\n",
                          encoding="utf-8")
        self.assertEqual(self.K.release_version(sample), "1.4")
        sample.write_text("project(something-else VERSION 2.0.0)\n", encoding="utf-8")
        with self.assertRaises(SystemExit):
            self.K.release_version(sample)

    def test_a_pack_two_zips_deep_is_found(self):
        inner = io.BytesIO()
        with zipfile.ZipFile(inner, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("deep/fr.pelang", self.pack.read_bytes())
        self.assertIn("lib.zip/deep/fr.pelang/text.bin: a language pack", self.caught({"lib.zip": inner.getvalue()}))

    def test_one_line_as_text_is_found(self):
        blob = b"#" * 5000 + f"s = '{self.lines[1]}'".encode("utf-8") + b"\n"
        self.assertIn("x.py: fr.pelang (text)", self.caught({"x.py": blob}))

    def test_data_files_other_than_the_tools_are_refused(self):
        built = {"data/strings.json": b"[]"}
        self.assertIn("data files inside", self.caught(built))


class WindowTest(unittest.TestCase):
    """The window itself (translator/app.py) started as a program, from source: the scripted run
    the download's build also uses (--new, --select, --type, --report). No disc, no game text."""

    def setUp(self):
        if not window_works():
            self.skipTest("no screen for a window here")
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def run_window(self, *extra, audit: Path | None = None, opened: Path | None = None,
                   new: tuple[str, str] = ("fr", "Français"), broken_check: bool = False):
        """The scripted window: a new translation (`new`), or the file `opened` opened as it is.
        `broken_check`: every check of a line raises (BROKEN_CHECK_RUN)."""
        saved, report = opened or self.dir / f"Parasite Eve - {new[1]}.pelang", self.dir / "report.json"
        start = [] if opened else ["--new", *new]
        args = [str(saved), *start, "--settings", str(self.dir / "settings.json"), "--report", str(report), *extra]
        cmd = [sys.executable, "-I"] + (["-c", AUDITED_RUN, str(audit), str(START)] if audit else
                                        ["-c", BROKEN_CHECK_RUN, str(START)] if broken_check else [str(START)])
        done = subprocess.run(cmd + args, capture_output=True, text=True, timeout=300)
        self.assertTrue(report.is_file(), done.stdout[-800:] + done.stderr[-800:])
        return done.returncode, json.loads(report.read_text(encoding="utf-8")), saved

    def test_typing_in_the_window_saves_the_file_the_tool_writes_from_the_same_line(self):
        key = plain_dialogue(1)[0]
        typed = "Une ligne tapée dans la fenêtre, à l'essai."
        code, report, saved = self.run_window("--select", key, "--type", typed)
        self.assertEqual(code, 0, report)
        self.assertTrue(report["ok"], report)
        self.assertEqual((report["row"], report["text_in_row"], report["row_state"]), (key, typed, "ok"))
        self.assertEqual((report["in_pack"], report["left_out"], report["whole"]), (1, {}, []))
        same = P.Translation(GAME, "fr", "Français")
        same.set_text(key, typed)
        same.save(self.dir / "same.pelang")
        self.assertEqual(saved.read_bytes(), (self.dir / "same.pelang").read_bytes())
        again = P.Translation.open(saved, GAME)
        self.assertEqual((again.from_work, again.texts), (True, {key: typed}))
        # the person's own settings were not used: the run kept to the file it was given
        self.assertEqual(json.loads((self.dir / "settings.json").read_text(encoding="utf-8"))["last"], str(saved))

    def test_a_line_that_fails_is_reported_and_left_out(self):
        key = plain_dialogue(1)[0]
        code, report, saved = self.run_window("--select", key, "--type", "{wait}" * 3 + "x" * 900)
        self.assertEqual(code, 0, report)
        self.assertEqual(report["row_state"], "error")
        self.assertEqual((report["in_pack"], list(report["left_out"])), (0, [key]))

    def test_an_english_pack_that_is_not_there_fails_the_run(self):
        code, report, _ = self.run_window("--english", str(self.dir / "missing.pelang"))
        self.assertEqual(code, 1)
        self.assertFalse(report["ok"])
        self.assertIn("missing.pelang", report["error"])

    def test_a_check_that_fails_ends_the_scripted_run_at_once_with_its_reason(self):
        """A check that raises (Pillow missing where the window runs, say) never marks the row: the
        run says why straight away instead of waiting two minutes and saying "timed out"."""
        key = plain_dialogue(1)[0]
        code, report, saved = self.run_window("--select", key, "--type", "Une ligne.", broken_check=True)
        self.assertEqual(code, 1, report)
        self.assertFalse(report["ok"])
        self.assertIn(f"the check itself failed on {key}", report["error"])
        self.assertIn("ModuleNotFoundError: No module named 'PIL' (made up by the test)", report["error"])
        self.assertLess(report["seconds"], 60)
        self.assertEqual((report["row"], report["text_in_row"], report["row_state"]), (key, "Une ligne.", ""))
        self.assertEqual(P.Translation.open(saved, GAME).texts, {})     # the new file, the line not saved in it

    def layout(self, font_scale: float, screen: int | None = None, widths: tuple = ()) -> dict:
        import tkinter as tk
        import tkinter.font as tkfont
        from translator import app as A
        A.SETTINGS = self.dir / "layout-settings.json"
        root = tk.Tk()
        if screen is not None:               # a screen this wide, whatever this computer's is
            root.winfo_screenwidth = lambda: screen
        try:
            for name in ("TkDefaultFont", "TkTextFont", "TkHeadingFont"):
                font = tkfont.nametofont(name)
                font.configure(size=round(abs(font.cget("size")) * font_scale))
            window = A.App(root, None, str(self.dir / "layout.pelang"), select=plain_dialogue(1)[0],
                           new=("fr", "Français"))
            def settle():                    # what a person sees: after every pass the window makes
                for _ in range(3):
                    root.update()

            settle()
            for width in widths:             # the window made narrower or wider once it is up
                root.minsize(1, 1)
                root.geometry(f"{width}x{root.winfo_height()}")
                settle()
            list_font = tkfont.nametofont("TkDefaultFont")
            rows, picked = window.rows, window.rows.selection()
            cell = rows.bbox(picked[0], "translation") if picked else ""
            return {"window": root.winfo_width(), "left": window.groups.master.winfo_width(),
                    "middle": rows.master.master.winfo_width(), "middle_needs": rows.master.master.winfo_reqwidth(),
                    "right": window.canvas.master.winfo_width(),
                    "preview": window.canvas.winfo_width(), "line": rows.column("line", "width"),
                    "translation": rows.column("translation", "width"),
                    "longest": max(list_font.measure(k.split("/", 1)[1]) for k in GAME.keys),
                    # what shows: the picked row's Translation cell inside the list, the scroll bar
                    "cell": cell, "list_width": rows.winfo_width(),
                    "row_in_sight": bool(picked and rows.bbox(picked[0])),
                    # to tell why when it isn't: the row picked, its place, and what the list shows
                    "picked": picked, "row_index": rows.index(picked[0]) if picked else None,
                    "rows_listed": len(rows.get_children()), "yview": rows.yview(),
                    "top_row": rows.identify_row(40), "viewable": rows.winfo_viewable(),
                    "scroll_bar": window.rows_scroll.winfo_ismapped() and window.rows_scroll.winfo_width() > 4,
                    "scale": window.scale}
        finally:
            root.destroy()

    def test_the_list_of_lines_gets_the_room_whatever_the_font(self):
        """macOS and Linux fonts are wider than Windows'. There the text boxes once pushed the right
        side wider than its preview, squeezing the list, and line names were cut ("M001/00")."""
        for scale in (1.0, 1.3):
            with self.subTest(fonts=f"{scale:.0%}"):
                size = self.layout(scale)
                self.assertLessEqual(size["right"], size["preview"] + 24, size)
                self.assertGreaterEqual(size["line"], size["longest"], size)
                self.assertGreaterEqual(size["middle"], size["window"] - size["left"] - size["right"] - 24, size)
                self.assertGreaterEqual(size["translation"], 150, size)
                self.assert_room_shared(size)

    def test_the_list_of_lines_gets_the_room_on_a_1280_screen(self):
        """GitHub's Linux screen, and many laptops, are 1280 wide: the window opens 1240 wide. The
        preview, not the list, gives way: the picked row's Translation shows, with its scroll bar."""
        for scale in (1.0, 1.3):
            with self.subTest(fonts=f"{scale:.0%}"):
                size = self.layout(scale, screen=1280)
                self.assertLessEqual(size["window"], 1240, size)
                if size["window"] < 1200:        # the real screen is smaller still (GitHub's Apple-chip
                    self.assert_room_shared(size)    # Mac: 1024): the rule at the width it got
                    continue
                self.assertGreaterEqual(size["middle"], size["middle_needs"], size)
                self.assertTrue(size["cell"], size)                      # the picked row is in sight
                x, _, w, _ = size["cell"]
                self.assertGreaterEqual(w, 80, size)
                self.assertLessEqual(x + w, size["list_width"], size)
                self.assertTrue(size["scroll_bar"], size)
                self.assertLessEqual(size["right"], size["preview"] + 24, size)
                self.assertGreaterEqual(size["scale"], 1.0, size)
                self.assertLess(size["scale"], 2, size)                  # it did give way

    def test_the_row_picked_stays_picked_when_the_whole_check_comes_back_first(self):
        """The list is refilled when the check of every line comes back. If that comes before the
        list's select event is handled (it did on macOS), the row picked must stay picked."""
        import tkinter as tk
        from translator import app as A
        A.SETTINGS = self.dir / "order-settings.json"
        key = plain_dialogue(1)[0]
        root = tk.Tk()
        try:
            window = A.App(root, None, str(self.dir / "order.pelang"), select=key, new=("fr", "Français"))
            self.assertEqual(window.rows.selection(), (key,))   # picked; its event not handled yet
            window.fill_rows(keep=True)                          # the whole check's refill, first
            for _ in range(3):
                root.update()
            self.assertEqual((window.rows.selection(), window.key), ((key,), key))
            self.assertTrue(window.rows.bbox(key))
        finally:
            root.destroy()

    def test_the_row_picked_is_worked_on_when_the_select_event_comes_at_once(self):
        """macOS sends the list's select event at once, inside the refill (Windows sends it later):
        the row picked must still be the one the editor works on (GitHub's Macs, d012fa89a)."""
        import tkinter as tk
        from translator import app as A
        A.SETTINGS = self.dir / "at-once-settings.json"
        key = plain_dialogue(1)[0]
        root = tk.Tk()
        try:
            window = A.App(root, None, str(self.dir / "at-once.pelang"), select=key, new=("fr", "Français"))
            real = window.rows.selection_set

            def at_once(*items):                 # as on macOS: the event before selection_set returns
                real(*items)
                window.rows.event_generate("<<TreeviewSelect>>")

            window.rows.selection_set = at_once
            window.fill_rows(keep=True)
            self.assertEqual((window.rows.selection(), window.key), ((key,), key))
        finally:
            root.destroy()

    def assert_room_shared(self, size: dict) -> None:
        """The rule at whatever width the window really got (a screen may hold it to less than
        asked: GitHub's Windows screen is 1024 wide): the list gets the room it asks for and the
        preview the rest, from the game's pixels at their own size up to twice it."""
        self.assertLessEqual(size["right"], size["preview"] + 24, size)       # the side is its preview
        self.assertTrue(size["row_in_sight"], size)                          # the line worked on shows
        if size["middle"] < size["middle_needs"] - 4:      # the list short of room: the preview gave all
            self.assertAlmostEqual(size["scale"], 1.0, places=2, msg=size)
        elif size["scale"] < 1.995:                        # the preview not full size: the list has
            self.assertLessEqual(size["middle"], size["middle_needs"] + 4, size)  # no room to spare

    def test_the_preview_is_full_size_when_there_is_room(self):
        for scale in (0.8, 1.0, 1.3):
            with self.subTest(fonts=f"{scale:.0%}"):
                size = self.layout(scale, screen=1920)
                self.assert_room_shared(size)

    def test_the_preview_follows_the_window_being_made_narrower_and_wider(self):
        """A window can open smaller than asked or be resized; on Linux and macOS the dividers stay
        where the first layout put them unless the tool moves them."""
        first = self.layout(1.0, screen=1920)
        narrower = first["window"] - 250
        narrow = self.layout(1.0, screen=1920, widths=(narrower,))
        again = self.layout(1.0, screen=1920, widths=(narrower, first["window"]))
        for size in (first, narrow, again):
            self.assert_room_shared(size)
        self.assertLessEqual(narrow["window"], narrower + 4, narrow)
        if first["scale"] > 1.0:
            self.assertLess(narrow["scale"], first["scale"], (first, narrow))
        if again["window"] == first["window"]:            # not held back by a small screen
            same = ("left", "middle", "right", "preview", "scale")
            self.assertEqual({k: again[k] for k in same}, {k: first[k] for k in same})

    def test_the_font_kept_in_the_settings_draws_characters_with_no_picture(self):
        key = plain_dialogue(1)[0]
        typed = "一丁七"           # three Chinese characters of the commonest
        font = a_font_for(typed)
        no_font = self.run_window("--select", key, "--type", typed, new=("zh-Hans", "Chinese"))[1]
        self.assertEqual((no_font["row_state"], no_font["in_pack"], list(no_font["left_out"])), ("error", 0, [key]))
        self.assertIn("Font", no_font["left_out"][key][0])
        if font is None:
            self.skipTest("no Chinese font file on this computer (or no Pillow)")
        if not the_window_has_pillow():
            self.skipTest("Pillow is only in this Python's user folder, which the window (python -I) "
                          "doesn't see: run the tests with a Python that has Pillow installed for it")
        (self.dir / "settings.json").write_text(json.dumps({"cjk_fonts": [font]}), encoding="utf-8")
        (self.dir / "Parasite Eve - Chinese.pelang").unlink()
        code, report, saved = self.run_window("--select", key, "--type", typed, new=("zh-Hans", "Chinese"))
        self.assertEqual(code, 0, report)
        self.assertEqual((report["fonts"], report["row_state"], report["in_pack"]), ([font], "ok", 1))
        again = P.Translation.open(saved, GAME)          # the pack has their pictures: no font needed
        self.assertEqual((again.text(key), again.locked), (typed, {}))
        self.assertEqual({ch for ch, _ in again.own_cjk}, set(typed))

    def test_the_tool_reads_no_file_of_the_repo_but_its_code_and_its_data_files(self):
        """What the download must carry (P.DATA_FILES), and that nothing else is read: no
        translation, no pilot, no Japanese table, no editor catalog."""
        audit = self.dir / "opened.txt"
        code, report, _ = self.run_window("--select", plain_dialogue(1)[0], "--type", "Une ligne.", audit=audit)
        self.assertEqual(code, 0, report)
        root = HERE.resolve()
        read = set()
        for line in audit.read_text(encoding="utf-8").splitlines():
            path = Path(line).resolve()
            if path.is_relative_to(root) and path.suffix not in (".py", ".pyc"):
                read.add(path.relative_to(root).as_posix())
        self.assertEqual(read, set(P.DATA_FILES) - set(P.DISC_DATA_FILES))   # no disc in this run


class PictureWindowTest(unittest.TestCase):
    """The Pictures window from source, with no disc (GitHub runs it): it opens, says to choose the
    disc, offers nothing to load, and a file that is not a disc is refused in plain words."""
    setUp, tearDown = WindowTest.setUp, WindowTest.tearDown

    def test_the_pictures_window_with_no_disc_asks_for_one(self):
        import tkinter as tk
        from translator import app as A
        A.SETTINGS = self.dir / "settings.json"
        root = tk.Tk()
        try:
            window = A.App(root, None, str(self.dir / "p.pelang"), new=("fr", "Français"))
            root.update()
            self.assertEqual(str(window.pictures_button.cget("state")), "normal")
            window.open_pictures()
            root.update()
            pw = window.pictures_window
            self.assertIn("Choose your Parasite Eve (USA) disc", pw.disc_label.cget("text"))
            self.assertEqual([str(b.cget("state")) for b in (pw.save_png, pw.load_png, pw.undo)], ["disabled"] * 3)
            self.assertEqual(len(pw.list.get_children()), len(__import__("translator.artwork").artwork.PICTURES))
            (self.dir / "not-a-disc.bin").write_bytes(b"\0" * 100000)
            error = window.use_disc(str(self.dir / "not-a-disc.bin"))
            self.assertIn("is not a Parasite Eve disc image", error)
            self.assertNotIn("disc", json.loads(A.SETTINGS.read_text(encoding="utf-8")) if A.SETTINGS.exists() else {})
            pw.close()
            self.assertIsNone(window.pictures_window)
        finally:
            root.destroy()


class AllPicturesDisc:
    """artwork.Disc's parts with every picture made up at its real size and kind: no disc."""
    SIZES = {"title/press-start": (24, 144, 24), "title/new-game": (24, 84, 24), "title/continue": (24, 84, 24),
             "title/tutorial": (24, 84, 24), "title/ex-game": (24, 84, 24), "title/published-by": (4, 256, 64),
             "map/place-names": (4, 256, 143), "battle/words": (4, 256, 32), "game-over": (4, 120, 24)}

    def __init__(self, path):
        from translator import artwork as A
        self.path = Path(path)
        self.english, self.around, self.layout, self.segments = {}, {}, {}, {}
        for pic in A.PICTURES:
            if isinstance(pic, A.Screen):
                continue
            if isinstance(pic, A.Glyph):
                _, data = made_up_glyph(*pic.size)
                self.english[pic.id], self.around[pic.id] = data, (0, data + bytes(-len(data) % 2048))
                self.segments[pic.id] = [(0, len(data))]
                continue
            if isinstance(pic, A.Card):
                card, self.layout[pic.id] = made_up_card()
                self.english[pic.id], self.around[pic.id] = card, (0, card + bytes(-len(card) % 2048))
                self.segments[pic.id] = [(0, len(card))]
                continue
            bits, w, h = self.SIZES[pic.id]
            tim = made_up_tim(bits, w, h, palette=list(range(0, 16 * 2048, 2048)) if bits == 4 else None)
            self.english[pic.id] = tim
            self.around[pic.id] = (0, tim + bytes(-len(tim) % 2048))
        TutorialDisc.add_tutorial(self)

    def span(self, pic):
        return 0, len(self.english[pic.id])


class PictureFitTest(unittest.TestCase):
    """The Pictures window from source with a made-up disc (GitHub runs it): the city map's names,
    the largest picture, fit it with the buttons in sight, at 2x in a 900-high window and still in
    sight in a window for a 768-high screen; resizing the window changes the zoom. The battle words
    show their copies in the fight's colours too, Game Over itself as the game shows it, and still fit."""
    tearDown = WindowTest.tearDown

    def setUp(self):
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("needs Pillow")                  # the Pictures window draws with it
        WindowTest.setUp(self)

    def test_the_map_fits_the_pictures_window_with_its_buttons_in_sight(self):
        import tkinter as tk
        from translator import app as A
        A.SETTINGS = self.dir / "settings.json"
        root = tk.Tk()
        try:
            window = A.App(root, None, str(self.dir / "p.pelang"), new=("fr", "Français"))
            window.picture_disc = AllPicturesDisc(self.dir / "made-up.cue")
            window.open_pictures()
            pw = window.pictures_window
            from translator import picture_window as PW
            seen, tall = {}, {}
            for height in (900, 668, 900):
                pw.win.geometry(f"1060x{height}")
                for pic in __import__("translator.artwork").artwork.PICTURES:
                    pw.list.selection_set(pic.id)
                    for _ in range(3):
                        root.update()
                    w, h = ((320, 224) if pic.id.startswith(("day/", "tutorial/")) else
                            pic.size if isinstance(pic, PW.A.Glyph) else AllPicturesDisc.SIZES[pic.id][1:])
                    bottom = pw.message.winfo_rooty() - pw.win.winfo_rooty()
                    right = pw.your_view.winfo_rootx() - pw.win.winfo_rootx() + pw.your_view.winfo_width()
                    details = dict(picture=pic.id, height=pw.win.winfo_height(), zoom=pw.zoom, bottom=bottom,
                                   right=right, width=pw.win.winfo_width())
                    self.assertEqual(pw.your_view.winfo_height() >= h * pw.zoom, True, details)
                    self.assertLessEqual(bottom, pw.win.winfo_height(), details)    # the buttons in sight
                    self.assertLessEqual(right, pw.win.winfo_width(), details)
                    self.assertEqual(bool(pw.fight_view.winfo_ismapped()), bool(PW.A.previews(pic)), details)
                    if pic.tints:                  # yours in each of the game's tints, one above the other
                        self.assertGreaterEqual(pw.fight_view.winfo_height(), h * len(pic.tints) * pw.zoom, details)
                        self.assertIn("red (Miss)", pw.fight_label.cget("text"))
                    if pic.id == "game-over":      # yours as the screen shows it
                        self.assertGreaterEqual(pw.fight_view.winfo_height(), h * pw.zoom, details)
                        self.assertIn("as the game shows it", pw.fight_label.cget("text"))
                    if getattr(pic, "sprite", False):   # a menu word: as the menus draw it, and at half
                        self.assertGreaterEqual(pw.fight_view.winfo_height(), 2 * h * pw.zoom, details)
                        self.assertIn("as the menus draw it", pw.fight_label.cget("text"))
                    # The largest zoom that fits the window as it really is: a screen smaller than the
                    # height asked for (GitHub's Windows and Mac runners) gives a smaller window.
                    room_w = pw.win.winfo_width() - PW.ROOM_AROUND[0]
                    room_h = pw.win.winfo_height() - PW.ROOM_AROUND[1]
                    fits = max(1, min(PW.ZOOM, room_w // w, room_h // (pw.copies(pic) * h)))
                    self.assertEqual(pw.zoom, fits, details)
                    seen[(height, pic.id)] = pw.zoom
                    tall[height] = pw.win.winfo_height() >= height - 20
            if tall[900]:                          # the window got the 900 it asked for
                self.assertEqual(seen[(900, "map/place-names")], 2, seen)
                self.assertEqual(seen[(900, "title/new-game")], 3, seen)
                self.assertEqual(seen[(900, "battle/words")], 2, seen)
            self.assertEqual(seen[(668, "map/place-names")], 1, seen)
        finally:
            root.destroy()

    def test_two_pngs_that_change_the_tune_up_screenshot_differently_are_named(self):
        """About Icons 7 and 8 show one screenshot: 8's PNG changing it otherwise than 7's (loaded first)
        is refused with both PNGs named, and the translation keeps 7's change only."""
        import tkinter as tk
        from PIL import ImageDraw
        from translator import app as A, artwork as W
        A.SETTINGS = self.dir / "settings.json"
        root = tk.Tk()
        try:
            window = A.App(root, None, str(self.dir / "p.pelang"), new=("fr", "Français"))
            disc = window.picture_disc = AllPicturesDisc(self.dir / "made-up.cue")
            seven, eight = W.BY_ID["tutorial/icons-7"], W.BY_ID["tutorial/icons-8"]
            pngs = []
            for pic, box in ((seven, (80, 40, 150, 60)), (eight, (100, 100, 120, 110))):
                img = W.image(pic, disc.english[pic.id], disc.layout[pic.id])
                ImageDraw.Draw(img).rectangle(box, fill=W.screen_rgb(5 * 0x421))
                pngs.append(png_bytes(img))
            window.load_picture(seven, pngs[0], "seven.png")
            before = dict(window.tr.parts)
            with self.assertRaises(ValueError) as said:
                window.load_picture(eight, pngs[1], "eight.png")
            self.assertIn('The two PNGs that disagree: eight.png (for this screen) and seven.png (loaded for '
                          '"Tutorial: About Icons 7/9 · Tune-up Icon").', str(said.exception))
            self.assertEqual(window.tr.parts, before)
        finally:
            root.destroy()


class GameLookWindowTest(unittest.TestCase):
    """The preview's switch from source, with no disc (GitHub runs it): "As in the game" with no disc
    draws the computer font's preview, as before."""
    setUp, tearDown = WindowTest.setUp, WindowTest.tearDown

    def test_with_no_disc_the_game_look_falls_back_to_the_computer_font(self):
        import tkinter as tk
        from translator import app as A
        A.SETTINGS = self.dir / "settings.json"
        root = tk.Tk()
        try:
            window = A.App(root, None, str(self.dir / "g.pelang"), select=plain_dialogue(1)[0], new=("fr", "Français"))
            window.tr.set_text(plain_dialogue(1)[0], "Une ligne.")
            window.check_now()
            for _ in range(100):
                root.update()
                if window.preview is not None:
                    break
                __import__("time").sleep(0.05)
            self.assertIsNotNone(window.preview)
            window.look_mode.set("game")
            self.assertFalse(window.draw_game())
            window.draw()
            texts = [window.canvas.itemcget(i, "text") for i in window.canvas.find_all()
                     if window.canvas.type(i) == "text"]
            self.assertTrue(any("Letter shapes are your computer's" in t for t in texts), texts)
        finally:
            root.destroy()

    def test_a_battle_line_is_drawn_on_the_bar_and_a_click_moves_the_bar(self):
        """With a disc's look (a made-up one here), a battle line is drawn on the battle bar at the bottom;
        a click on the picture moves the bar to the top, and the next click back."""
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("needs Pillow")
        import tkinter as tk
        from translator import app as A
        A.SETTINGS = self.dir / "settings.json"
        root = tk.Tk()
        try:
            window = A.App(root, None, str(self.dir / "g.pelang"), select="battle/010", new=("es", "Español"))
            window.look_disc = FakeBattleDisc()
            window.tr.set_text("battle/010", "¡No puedes huir!")
            window.check_now()
            for _ in range(100):
                root.update()
                if window.preview is not None:
                    break
                __import__("time").sleep(0.05)
            self.assertIsNotNone(window.preview)
            window.look_mode.set("game")

            def notes():
                window.draw()
                return " ".join(window.canvas.itemcget(i, "text") for i in window.canvas.find_all()
                                if window.canvas.type(i) == "text")

            self.assertTrue(window.draw_game())
            said = notes()
            self.assertIn("the battle bar at the bottom", said)
            self.assertIn("first fight's room", said)
            window.next_room_picture()
            self.assertIn("the battle bar at the top", notes())
            window.next_room_picture()
            self.assertIn("the battle bar at the bottom", notes())
        finally:
            root.destroy()


class LettersWindowTest(unittest.TestCase):
    """Letters the game can't draw yet, in the window from source (GitHub runs it; no disc, no game
    text): the scripted run's Save as PNG / Load PNG, the Letters window, a right-to-left line in the
    game look."""
    tearDown = WindowTest.tearDown
    run_window = WindowTest.run_window

    def setUp(self):
        WindowTest.setUp(self)
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("needs Pillow")
        if not the_window_has_pillow():
            self.skipTest("the window (python -I) can't load Pillow here")

    def test_the_letters_png_saved_painted_and_loaded_back_goes_into_the_file(self):
        from translator import letterart as L
        empty = self.dir / "Greek letters.png"
        code, report, saved = self.run_window("--save-letters", str(empty), new=("el", "Ελληνικά"))
        self.assertEqual(code, 0, report)
        self.assertEqual(report["letters"], {"saved": str(empty)})
        rows = L.wanted("el")
        png = empty.read_bytes()
        self.assertEqual(L.from_png(png, rows).drawn, {s.char: None for s in L.every_shape(rows)})
        letters = {s.char: made_up_strokes(k) for k, s in enumerate(L.every_shape(rows))}
        painted = self.dir / "painted.png"
        painted.write_bytes(paint(png, L.squares(rows, False), letters))
        key = plain_dialogue(1)[0]
        code, report, saved = self.run_window("--letters", str(painted), "--select", key, "--type", "Γεια σου ΔΘΛ",
                                              opened=saved)
        self.assertEqual(code, 0, report)
        self.assertEqual((report["letters"]["drawn"], report["letters"]["empty"], report["letters"]["notes"]),
                         (sorted(letters), [], []))
        self.assertEqual(report["row_state"], "ok")
        again = P.Translation.open(saved, GAME)
        self.assertEqual((again.drawn, again.texts), (letters, {key: "Γεια σου ΔΘΛ"}))
        own = {entry[0]: entry for entry in P.letters_in_pack(again.pack)[0]}
        self.assertEqual(set(own), set("Γεια σου ΔΘΛ") & set(letters))
        self.assertEqual(own["Δ"][1:3], (L.picture(letters["Δ"]), L.advance(letters["Δ"])))

    def test_a_picture_that_isnt_the_letters_png_fails_the_run_saying_why(self):
        from PIL import Image
        wrong = self.dir / "wrong.png"
        Image.new("RGB", (64, 64)).save(wrong)
        code, report, saved = self.run_window("--letters", str(wrong), new=("he", "עברית"))
        self.assertEqual(code, 1, report)
        self.assertIn("could not load the letters PNG", report["error"])
        self.assertIn("Save as PNG", report["error"])

    def open_app(self, lang: str, name: str, key: str | None = None):
        import tkinter as tk
        from translator import app as A
        A.SETTINGS = self.dir / "settings.json"
        root = tk.Tk()
        return root, A.App(root, None, str(self.dir / f"{lang}.pelang"), select=key, new=(lang, name))

    def test_the_letters_window_lists_the_letters_and_marks_the_drawn_ones(self):
        from translator import letterart as L
        root, app = self.open_app("he", "עברית")
        try:
            app.open_letters()
            win = app.letters_window
            self.assertEqual(len(win.list.get_children()), 27)
            self.assertEqual({win.list.set(i, "state") for i in win.list.get_children()}, {"a ? in the game"})
            self.assertIn("27 letters of Hebrew", win.intro.cget("text"))
            rows = app.tr.letters_to_draw()
            letters = {s.char: made_up_strokes(k) for k, s in enumerate(L.every_shape(rows))}
            loaded = app.load_letters(paint(app.letters_png(), L.squares(rows, False), letters))
            self.assertEqual(sum(1 for v in loaded.drawn.values() if v), 27)
            win.marks()
            win.show()
            self.assertEqual({win.list.set(i, "state") for i in win.list.get_children()}, {"drawn"})
            self.assertTrue(app.dirty)
            self.assertGreater(len(win.canvas.find_all()), 20)     # the picked letter's dots
            win.list.selection_set("0")
            win.clear_letter()
            self.assertEqual(win.list.set("0", "state"), "a ? in the game")
            self.assertNotIn(win.shapes[0].char, app.tr.drawn)
            # the PNG saved now shows the 26 still drawn
            self.assertEqual(sum(1 for v in L.from_png(app.letters_png(), rows).drawn.values() if v), 26)
            win.close()
            self.assertIsNone(app.letters_window)
        finally:
            root.destroy()
        root, app = self.open_app("fr", "Français")
        try:
            app.open_letters()
            self.assertIn("nothing to draw", app.letters_window.intro.cget("text"))
            self.assertEqual(str(app.letters_window.save_png.cget("state")), "disabled")
        finally:
            root.destroy()

    def test_a_right_to_left_line_is_drawn_as_in_the_game(self):
        key = plain_dialogue(1)[0]
        root, app = self.open_app("he", "עברית", key)
        try:
            app.look_disc = FakeLookDisc()
            app.tr.set_text(key, "שלום Aya!")
            app.check_now()
            for _ in range(100):
                root.update()
                if app.preview is not None:
                    break
                __import__("time").sleep(0.05)
            self.assertIsNotNone(app.preview)
            app.look_mode.set("game")
            self.assertTrue(app.draw_game())             # it fell back to the computer font before
        finally:
            root.destroy()


class PlayerPackWindowTest(unittest.TestCase):
    """The window with a real player pack (PE_TEST_PLAYER_PACKS). Apart from WindowTest, which
    GitHub runs and where every test must run: the packs hold game text and never go there."""
    setUp, tearDown, run_window = WindowTest.setUp, WindowTest.tearDown, WindowTest.run_window

    def test_a_fan_patch_pack_opened_in_the_window_saves_a_changed_line_and_nothing_else(self):
        packs = [p for p in player_packs() if "Spanish" in p.name]
        if not packs:
            self.skipTest("needs PE_TEST_PLAYER_PACKS with the Spanish pack")
        copy = self.dir / packs[0].name
        copy.write_bytes(packs[0].read_bytes())
        t = P.Translation.open(copy, GAME)
        strict = P.build(GAME, t.texts, t.lang, t.name, keep_names_from=t.pack,
                         rules=P.Rules(letters=t.own_letters, cjk=t.own_cjk))
        key = sorted(k for k in strict.keys_with("error") if k.startswith("dlg/") and k in t.texts)[0]
        changed = one_word_changed(t.text(key))
        code, report, saved = self.run_window("--select", key, "--type", changed, opened=copy)
        self.assertEqual(code, 0, report)
        self.assertEqual((report["from_patch"], report["locked"], report["row_state"]), (True, 0, "ok"))
        self.assertEqual((report["left_out"], report["whole"]), ({}, []))
        was, now = pelang.read_pack(packs[0])[2], pelang.read_pack(saved)[2]
        self.assertEqual({GAME.by_hash.get(h, h) for h in set(was) | set(now) if was.get(h) != now.get(h)}, {key})
        self.assertEqual(zip_parts(saved).keys(), zip_parts(packs[0]).keys())
        self.assertEqual({k: v for k, v in zip_parts(saved).items() if k != pelang.BUNDLE_TEXT},
                         {k: v for k, v in zip_parts(packs[0]).items() if k != pelang.BUNDLE_TEXT})


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=2).result
    if not result.wasSuccessful():
        sys.exit(1)
    sys.exit(77 if result.skipped else 0)
