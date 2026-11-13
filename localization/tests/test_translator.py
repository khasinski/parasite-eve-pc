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
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE))
import pelang  # noqa: E402
from petext import alphabets, layout  # noqa: E402
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

    def test_the_english_pack_reads_back_as_the_discs_own_text_every_row(self):
        self.assertEqual(self.wrong, 0)
        self.assertGreater(len(self.english), 2800)
        differ = [k for k, text in self.english.items() if text != self.strings[k]["text"]]
        self.assertEqual(differ, [])
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


def player_packs() -> list[Path]:
    folder = os.environ.get("PE_TEST_PLAYER_PACKS")
    return sorted(Path(folder).glob("*.pelang")) if folder and Path(folder).is_dir() else []


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
        self.assertEqual(list(img.crop((0, 0, 8, 3)).getdata()), list(A.image(pic, tim).convert("RGB").getdata()))
        numbers = T.indices(T.read(tim))
        for k, n in enumerate(numbers):
            x, y = k % 8, k // 8
            r = T.rgb_of(grey[n])[0]
            self.assertEqual(img.getpixel((x, y + 3)), (r, 0, 0))
            self.assertEqual(img.getpixel((x, y + 6)), (0, r, 0))

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


class PictureDiscTest(unittest.TestCase):
    """The pictures with words (title screen, city map, battle words) from the real discs (PE_TEST_DISC,
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
                    shown = A.shown(disc, pic, pieces)
                    loaded = A.from_png(pic, disc.english[pic.id], A.png(pic, shown), shown)
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
                self.assertEqual(A.from_png(pic, english, A.png(pic, english)).tim, english)
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
                self.assertEqual(zip_parts(copy), zip_parts(path))      # text.bin, pictures, readme
                again = P.Translation.open(copy, GAME)
                self.assertEqual((again.from_work, again.from_patch, again.locked), (True, True, {}))
                self.assertEqual(again.texts, t.texts)
                self.assertEqual(again.rules.patch_rows, frozenset(held))
                twice = self.dir / ("again " + path.name)
                again.save(twice)
                self.assertEqual(zip_parts(twice), zip_parts(path))
                self.assertEqual(path.read_bytes(), before)
                print(f"\n{path.name}: {t.name} ({t.lang}), {len(held)} rows, {len(t.own_letters)} letter "
                      f"pictures of its own, {len(t.own_cjk)} CJK pictures; saves back byte for byte")

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
                self.assertEqual(differ, {key})
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
                   new: tuple[str, str] = ("fr", "Français")):
        """The scripted window: a new translation (`new`), or the file `opened` opened as it is."""
        saved, report = opened or self.dir / f"Parasite Eve - {new[1]}.pelang", self.dir / "report.json"
        start = [] if opened else ["--new", *new]
        args = [str(saved), *start, "--settings", str(self.dir / "settings.json"), "--report", str(report), *extra]
        cmd = [sys.executable, "-I"] + (["-c", AUDITED_RUN, str(audit), str(START)] if audit else [str(START)])
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
             "map/place-names": (4, 256, 143), "battle/words": (4, 256, 32)}

    def __init__(self, path):
        from translator import artwork as A
        self.path = Path(path)
        self.english, self.around = {}, {}
        for pic in A.PICTURES:
            bits, w, h = self.SIZES[pic.id]
            tim = made_up_tim(bits, w, h, palette=list(range(0, 16 * 2048, 2048)) if bits == 4 else None)
            self.english[pic.id] = tim
            self.around[pic.id] = (0, tim + bytes(-len(tim) % 2048))

    def span(self, pic):
        return 0, len(self.english[pic.id])


class PictureFitTest(unittest.TestCase):
    """The Pictures window from source with a made-up disc (GitHub runs it): the city map's names,
    the largest picture, fit it with the buttons in sight, at 2x in a 900-high window and still in
    sight in a window for a 768-high screen; resizing the window changes the zoom. The battle words
    show their copies in the fight's colours too, and still fit."""
    setUp, tearDown = WindowTest.setUp, WindowTest.tearDown

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
            seen = {}
            for height in (900, 668, 900):
                pw.win.geometry(f"1060x{height}")
                for pic in __import__("translator.artwork").artwork.PICTURES:
                    pw.list.selection_set(pic.id)
                    for _ in range(3):
                        root.update()
                    w, h = AllPicturesDisc.SIZES[pic.id][1:]
                    bottom = pw.message.winfo_rooty() - pw.win.winfo_rooty()
                    right = pw.your_view.winfo_rootx() - pw.win.winfo_rootx() + pw.your_view.winfo_width()
                    details = dict(picture=pic.id, height=pw.win.winfo_height(), zoom=pw.zoom, bottom=bottom,
                                   right=right, width=pw.win.winfo_width())
                    self.assertEqual(pw.your_view.winfo_height() >= h * pw.zoom, True, details)
                    self.assertLessEqual(bottom, pw.win.winfo_height(), details)    # the buttons in sight
                    self.assertLessEqual(right, pw.win.winfo_width(), details)
                    self.assertEqual(bool(pw.fight_view.winfo_ismapped()), bool(pic.tints), details)
                    if pic.tints:                  # yours in each of the game's tints, one above the other
                        self.assertGreaterEqual(pw.fight_view.winfo_height(), h * len(pic.tints) * pw.zoom, details)
                        self.assertIn("red (Miss)", pw.fight_label.cget("text"))
                    seen[(height, pic.id)] = pw.zoom
            self.assertEqual(seen[(900, "map/place-names")], 2, seen)
            self.assertEqual(seen[(668, "map/place-names")], 1, seen)
            self.assertEqual(seen[(900, "title/new-game")], 3, seen)
            self.assertEqual(seen[(900, "battle/words")], 2, seen)
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
