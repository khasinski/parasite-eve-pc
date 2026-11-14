"""The English pack (pelang.py english): every row of it is the disc's own bytes, on both discs.

The English pack is the game's own text as a language pack, made from the player's discs: where a
new language starts from, and the check that the pack tools add and lose nothing. This builds it
from the real discs and reads it back as the game does:

  - every row the pack holds is, byte for byte, the row on the disc (a dialogue page that ends in
    F9 keeps that end), and the pack holds nothing else but the banks' names;
  - the rows it leaves to the game are only pages with no text and the four rows the pack tool
    can't write as they are (LEFT_TO_THE_GAME), so nearly all of the game is in it;
  - built from Disc 1 and Disc 2 together it holds every row of either disc, each as that disc has
    it: one pack serves both discs.

text_renderer_game.py (part english-pack) checks it in the game: with it chosen every frame is the
game's own. Needs USA disc 1 (PE_TEST_DISC); PE_TEST_DISC2 adds Disc 2. Without a disc only the
rule's own tests run and it exits 77.

  python -I localization/tests/test_en_pack.py
"""
import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE))
import pelang  # noqa: E402

# Rows with text that the pack leaves to the game: the name entry's three grids (the two picture
# pages, and the letters page whose {g4D} cell a pack empties) and a page that is only controls.
LEFT_TO_THE_GAME = {"dlg/M035/112", "sys/115", "sys/116", "sys/117"}


def extract(discs, out):
    subprocess.run([sys.executable, "-I", str(HERE / "extract_text.py"), *discs, "--out", str(out)],
                   check=True, capture_output=True)
    return out / "strings.json"


def english(strings, out):
    """pelang.py english; returns what it printed."""
    said = io.StringIO()
    with contextlib.redirect_stdout(said), contextlib.redirect_stderr(said):
        code = pelang.main(["english", str(strings), "-o", str(out)])
    if code:
        raise AssertionError(f"pelang.py english failed: {said.getvalue()[-800:]}")
    return said.getvalue()


class RuleTest(unittest.TestCase):
    def test_a_row_is_the_discs_bytes_and_a_page_keeps_its_own_end(self):
        self.assertEqual(pelang.game_bytes("item/001", {"text": "Ab", "raw": "1031"}), bytes.fromhex("1031"))
        self.assertEqual(pelang.game_bytes("dlg/M001/009", {"text": "Ab", "raw": "1031", "end": "FF"}),
                         bytes.fromhex("1031"))
        self.assertEqual(pelang.game_bytes("dlg/M001/009", {"text": "Ab", "raw": "1031", "end": "F9"}),
                         bytes.fromhex("1031f9"))
        # A menu row never takes a page end.
        self.assertEqual(pelang.game_bytes("sys/001", {"text": "Ab", "raw": "1031", "end": "F9"}),
                         bytes.fromhex("1031"))
        # The committed manifest has the text's shape and none of its words.
        self.assertIsNone(pelang.game_bytes("item/001", {"english_hash": "00", "length": 2}))

    def test_the_manifest_is_refused_for_it_has_no_game_text(self):
        with tempfile.TemporaryDirectory() as tmp:
            out, said = Path(tmp) / "en.pelang", io.StringIO()
            with contextlib.redirect_stdout(said), contextlib.redirect_stderr(said):
                code = pelang.main(["english", str(HERE / "data" / "manifest.json"), "-o", str(out)])
            self.assertEqual(code, 1)
            self.assertIn("no game text", said.getvalue())
            self.assertFalse(out.exists())


class DiscTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        discs = [os.environ.get("PE_TEST_DISC"), os.environ.get("PE_TEST_DISC2")]
        if not discs[0] or not Path(discs[0]).is_file():
            raise unittest.SkipTest("needs PE_TEST_DISC (USA disc 1); PE_TEST_DISC2 adds disc 2")
        cls.discs = [d for d in discs if d and Path(d).is_file()]
        cls.tmp = tempfile.TemporaryDirectory()
        tmp = Path(cls.tmp.name)
        # The pack as a translator makes it: from every disc they have, in one go.
        cls.strings = extract(cls.discs, tmp / "text")
        cls.pack = tmp / "en.pelang"
        cls.said = english(cls.strings, cls.pack)
        cls.rows = {r["key"]: r for r in json.loads(cls.strings.read_text(encoding="utf-8")) if not r.get("unused")}
        cls.lang, cls.name, cls.entries = pelang.read_pack(cls.pack)

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "tmp"):
            cls.tmp.cleanup()

    def test_it_is_the_english_pack(self):
        self.assertEqual((self.lang, self.name), ("en", "English pack"))
        print(f"\n{self.said.strip()}")

    def test_every_row_in_the_pack_is_the_discs_own_bytes(self):
        held = {k: self.entries[pelang.fnv1a(k)] for k in self.rows if pelang.fnv1a(k) in self.entries}
        wrong = [k for k, got in held.items() if got != pelang.game_bytes(k, self.rows[k])]
        self.assertEqual(wrong, [], "rows the pack holds differently from the disc")
        self.assertGreater(len(held), 2800)

    def test_the_pack_holds_nothing_but_the_games_rows_and_the_banks_names(self):
        banks = json.loads(self.strings.with_name("banks.json").read_text(encoding="utf-8"))
        known = {pelang.fnv1a(k) for k in self.rows} | {pelang.fnv1a(f"bank/{b['crc32']}") for b in banks}
        self.assertEqual(sorted(set(self.entries) - known), [])
        for b in banks:
            self.assertEqual(self.entries[pelang.fnv1a(f"bank/{b['crc32']}")], b["key"].encode("ascii"))

    def test_only_empty_pages_and_four_known_rows_are_left_to_the_game(self):
        left = {k for k in self.rows if pelang.fnv1a(k) not in self.entries}
        with_text = {k for k in left if self.rows[k]["text"]}
        self.assertEqual(with_text, LEFT_TO_THE_GAME)
        for key in sorted(left):
            self.assertIn(key, self.said, "a row left to the game that the tool did not report")

    def test_one_pack_serves_both_discs(self):
        if len(self.discs) < 2:
            self.skipTest("needs PE_TEST_DISC2 (USA disc 2)")
        tmp = Path(self.tmp.name)
        for n, disc in enumerate(self.discs, 1):
            rows = [r for r in json.loads(extract([disc], tmp / f"disc{n}").read_text(encoding="utf-8"))
                    if not r.get("unused") and r["text"] and r["key"] not in LEFT_TO_THE_GAME]
            wrong = [r["key"] for r in rows
                     if self.entries.get(pelang.fnv1a(r["key"])) != pelang.game_bytes(r["key"], r)]
            print(f"\ndisc {n}: {len(rows)} rows with text, {len(rows) - len(wrong)} in the pack as the disc has them")
            self.assertGreater(len(rows), 2000)
            self.assertEqual(wrong, [], f"rows of disc {n} the pack doesn't hold as that disc has them")


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=2).result
    if not result.wasSuccessful():
        sys.exit(1)
    sys.exit(77 if result.skipped else 0)
