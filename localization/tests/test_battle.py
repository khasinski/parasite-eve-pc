"""Battle messages (petext/battle.py): reading them, lining up a patch's, and the build's rules.
Made-up lines only: no game text or disc data."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
import pelang  # noqa: E402
from petext import battle, gametext as gt, layout, patchimport  # noqa: E402

KEYS = list(battle.LINES)


def line(n: int) -> bytes:
    return gt.encode("{num} {num}" if n == 1 else f"Line {n}")


def japanese(n: int) -> bytes:
    return bytes([0xFB, 0x80 + n, 0x82, 0x7F])


def original() -> bytearray:
    """A boot program with each line where the USA one has it, its Japanese beside it."""
    exe = bytearray(battle.REGION[1] + 64)
    for n, (at, size, ja) in enumerate(battle.LINES.values()):
        exe[at:at + len(line(n)) + 1] = line(n) + b"\xff"
        if ja is not None:
            exe[ja:ja + 5] = japanese(n) + b"\xff"
    return exe


def translated(n: int) -> bytes:
    return gt.encode("{num}/{num}" if n == 1 else f"Linea {n}")


class ReadTest(unittest.TestCase):
    def test_the_english_lines_without_their_end(self):
        lines = battle.english(bytes(original()))
        self.assertEqual(list(lines), KEYS)
        self.assertEqual(lines["battle/002"], line(2))
        self.assertEqual(lines["battle/001"], gt.encode("{num} {num}"))

    def test_a_line_with_no_end_is_not_a_usa_disc(self):
        exe = original()
        at, size, _ = battle.LINES["battle/005"]
        exe[at:at + size] = b"\x10" * size
        with self.assertRaises(ValueError):
            battle.english(bytes(exe))

    def test_keys_follow_the_boot_program(self):
        offsets = [at for at, _, _ in battle.LINES.values()]
        self.assertEqual(offsets, sorted(offsets))
        self.assertTrue(all(battle.REGION[0] <= at and at + size <= battle.REGION[1]
                            for at, size, _ in battle.LINES.values()))
        self.assertEqual(KEYS, [f"battle/{n:03}" for n in range(len(KEYS))])


class PatchTest(unittest.TestCase):
    def test_unchanged_gives_the_english(self):
        exe = bytes(original())
        self.assertEqual(battle.patched(exe, exe), (battle.english(exe), None))

    def test_lines_in_place_beside_the_japanese(self):
        """One way a patch does it: each line starts later in its slot, after 00s the patch's
        widths draw as nothing, and the Japanese stays."""
        exe = original()
        for n, (at, size, _) in enumerate(battle.LINES.values()):
            text = translated(n)
            exe[at:at + size] = bytes(size - len(text) - 1) + text + b"\xff"
        lines, why = battle.patched(bytes(original()), bytes(exe))
        self.assertIsNone(why)
        self.assertEqual(lines, {k: translated(n) for n, k in enumerate(KEYS)})

    def test_lines_moved_and_the_japanese_gone(self):
        """The other: the lines packed from the start of the region, the code pointed at them."""
        exe = original()
        start, end = battle.REGION
        packed = b"".join(b"\x00" * (n % 2) + translated(n) + b"\xff" for n in range(len(KEYS)))
        exe[start:end] = packed + bytes(end - start - len(packed))
        lines, why = battle.patched(bytes(original()), bytes(exe))
        self.assertIsNone(why)
        self.assertEqual(list(lines.values()), [translated(n) for n in range(len(KEYS))])

    def test_a_line_too_many_or_too_few_keeps_them_all_english(self):
        exe = original()
        at, size, _ = battle.LINES["battle/003"]
        exe[at:at + 6] = gt.encode("Ab") + b"\xff" + gt.encode("Cd") + b"\xff"
        self.assertEqual(battle.patched(bytes(original()), bytes(exe))[0], {})
        exe = original()
        start, end = battle.REGION
        exe[start:end] = bytes(end - start)
        lines, why = battle.patched(bytes(original()), bytes(exe))
        self.assertEqual(lines, {})
        self.assertIn("0 lines", why)

    def test_patch_letters_come_through_the_letter_map(self):
        letters = dict(gt.CHARS)
        letters[0x52] = "á"
        raw = gt.encode("L") + b"\x52" + gt.encode("mpara")
        self.assertEqual(patchimport.compare_page(line(4), raw, letters), ("Lámpara", None))


class BuildTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        rows = [{"key": k, "text": gt.decode(line(n)), "raw": line(n).hex(" "), "end": "FF",
                 "lines_px": gt.lines(line(n)), "boxes": [], "rooms": [], "unused": False}
                for n, k in enumerate(KEYS)]
        rows.append({"key": "item/000", "text": "Ammo", "lines_px": [30], "boxes": [], "rooms": [], "unused": False})
        self.strings = self.dir / "strings.json"
        self.strings.write_text(json.dumps(rows), encoding="utf-8")
        (self.dir / "banks.json").write_text("[]", encoding="utf-8")

    def build(self, translations: dict, *extra) -> int:
        path = self.dir / "t.csv"
        rows = pelang.template_rows(pelang.load_strings(self.strings))
        for r in rows:
            r["translation"] = translations.get(r["key"], "")
        pelang.write_csv(path, rows)
        return pelang.main(["build", str(path), "--strings", str(self.strings), "--lang", "xx",
                            "--name", "Test", "-o", str(self.dir / "t.pelang"), *extra])

    def test_template_says_where_it_shows(self):
        rows = {r["key"]: r for r in pelang.template_rows(pelang.load_strings(self.strings))}
        self.assertEqual(rows["battle/002"]["box"], f"battle message: one line, {battle.WIDTH} px")

    def test_a_translation_goes_in_the_pack(self):
        self.assertEqual(self.build({"battle/002": "Línea dos ácida", "battle/001": "Vida {num}/{num}"}), 0)
        _, _, entries = pelang.read_pack(self.dir / "t.pelang")
        self.assertIn(pelang.fnv1a("battle/002"), entries)
        self.assertIn(pelang.fnv1a("battle/001"), entries)

    def test_one_line_inside_the_screen(self):
        errors, _ = layout.check("battle/002", gt.encode("W" * 30), {}, {})
        self.assertTrue(any("fits 300 px" in e for e in errors))
        errors, _ = layout.check("battle/002", gt.encode("Short{br}line"), {}, {})
        self.assertTrue(any("one line" in e for e in errors))
        self.assertEqual(layout.check("battle/002", gt.encode("Short line"), {}, {}), ([], []))
        self.assertEqual(self.build({"battle/003": "W" * 30}), 1)
        self.assertEqual(self.build({"battle/003": "W" * 30}, "--from-patch"), 0)   # back to the game's text
        _, _, entries = pelang.read_pack(self.dir / "t.pelang")
        self.assertNotIn(pelang.fnv1a("battle/003"), entries)

    def test_the_hp_line_keeps_its_numbers_even_from_a_patch(self):
        self.assertEqual(self.build({"battle/001": "Vida {num}"}), 1)
        self.assertEqual(self.build({"battle/001": "Vida {num}"}, "--from-patch"), 1)

    def test_the_repo_manifest_knows_the_keys_without_their_text(self):
        manifest = json.loads((ROOT / "data/manifest.json").read_text(encoding="utf-8"))
        entries = {k: e for k, e in manifest["entries"].items() if k.startswith("battle/")}
        self.assertEqual(sorted(entries), KEYS)
        self.assertEqual(entries["battle/001"]["structure"], ["fb08", "fb08"])
        self.assertTrue(all("text" not in e and "raw" not in e and "menu_px" not in e for e in entries.values()))


if __name__ == "__main__":
    unittest.main()
