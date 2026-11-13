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
from petext import alphabets, citymap, gametext as gt, layout, pictures as picturebin  # noqa: E402
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
        from PIL import ImageDraw
        from translator import artwork as A
        disc = self.discs()[0]
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
