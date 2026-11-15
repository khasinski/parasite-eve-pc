"""A fan patch's own letter pictures as a pack's CJK letters (petext/cjk.py patch_pictures), and the
build options a patch's pack uses: --cjk-letters / --cjk-pictures, several CSVs, --adapt, --leave-out.
No font and no game data needed (the small strings.json of test_pelang.py).

  python -I localization/tests/test_cjk_pictures.py
"""
import csv
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pelang  # noqa: E402
from petext import cjk  # noqa: E402
from test_pelang import BANKS, STRINGS  # noqa: E402

TEXTS = {"item/000": "好药",
         "dlg/M001/002": "要放下吗？{br}{choice 2}    是{br}    否",
         "dlg/M001/003": "好。"}
CHARS = "好药要放下吗？是否。"


def picture(n):
    """A 12x12 test picture of its own: strokes on a pattern from n, shadow right of each stroke."""
    rows = []
    for y in range(12):
        row = ""
        for x in range(12):
            stroke = x < 11 and y < 11 and (x * 7 + y * 3 + n) % 5 == 0
            shadow = x > 0 and y < 11 and (x * 7 - 7 + y * 3 + n) % 5 == 0
            row += "#" if stroke else "+" if shadow else " "
        rows.append(row)
    return rows


def values(rows):
    return [{" ": 0, "#": 1, "+": 2}[c] for r in rows for c in r]


class PatchPicturesTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())

    def write(self, names, pictures, split=False):
        """letters.tsv (picture, char, how, uses) and one or two pictures files."""
        letters = self.dir / "letters.tsv"
        with letters.open("w", encoding="utf-8", newline="") as f:
            w = csv.writer(f, delimiter="\t", lineterminator="\n")
            w.writerow(["picture", "char", "how", "score", "nearest", "uses", "note"])
            for pid, ch, how in names:
                w.writerow([pid, ch, how, "", "", pictures[pid][1], ""])
        items = list(pictures.items())
        parts = [items[::2], items[1::2]] if split else [items]
        files = []
        for n, part in enumerate(parts):
            path = self.dir / f"disc{n + 1}-pictures.json"
            path.write_text(json.dumps({"pictures": {pid: {"rows": rows, "uses": uses}
                                                     for pid, (rows, uses) in part}}), encoding="utf-8")
            files.append(path)
        return letters, files

    def test_most_used_picture_space_empty_latin_and_unnamed_left_out(self):
        space = [" " * 12] * 12
        space[1] = "+" + " " * 11                       # a stray shadow dot, as the patch's space has
        pictures = {"a1": (picture(1), 3), "a2": (picture(2), 9), "sp": (space, 100),
                    "q": (picture(3), 4), "lat": (picture(4), 7), "rev": (picture(5), 2)}
        names = [("a1", "好", "read"), ("a2", "好", "japanese"), ("sp", "\\u3000", "blank"),
                 ("q", "？", "slot"), ("lat", "A", "slot"), ("rev", "", "review")]
        letters, files = self.write(names, pictures, split=True)
        got, notes = cjk.patch_pictures(letters, files)
        self.assertEqual(set(got), {"好", "　", "？"})
        self.assertEqual(got["好"], values(picture(2)))        # 9 uses beat 3
        self.assertEqual(got["　"], [0] * 144)            # the space draws nothing
        self.assertEqual(got["？"], values(picture(3)))
        self.assertEqual(len(notes), 1)
        self.assertIn("took a2 (9 uses), not a1 (3)", notes[0])

    def test_bad_pictures_are_refused(self):
        with self.assertRaises(ValueError):
            cjk.picture_pixels(picture(1)[:11])
        with self.assertRaises(ValueError):
            cjk.picture_pixels(["x" * 12] * 12)
        letters, files = self.write([("a1", "好", "read")], {"a1": (picture(1), 1)})
        with letters.open("a", encoding="utf-8") as f:
            f.write("gone\t药\tread\t\t\t1\t\n")
        with self.assertRaisesRegex(ValueError, "gone"):
            cjk.patch_pictures(letters, files)

    def test_atlas_takes_the_pictures_as_they_are_without_a_font(self):
        letters = {ch: values(picture(n)) for n, ch in enumerate("好药")}
        atlas = cjk.Atlas(["好药好"], None, pictures=letters)
        self.assertEqual(atlas.chars, ["好", "药"])
        for k, ch in enumerate(atlas.chars):
            record = atlas.records[k * cjk.RECORD:(k + 1) * cjk.RECORD]
            self.assertEqual(struct.unpack_from("<I", record)[0], ord(ch))
            pixels = letters[ch]
            self.assertEqual(record[4:], bytes(pixels[i] | pixels[i + 1] << 4 for i in range(0, 144, 2)))
        with self.assertRaisesRegex(ValueError, "no letter picture.*吗"):
            cjk.Atlas(["好吗"], None, pictures=letters)


class PatchPackBuildTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.strings = self.dir / "strings.json"
        self.strings.write_text(json.dumps(STRINGS), encoding="utf-8")
        (self.dir / "banks.json").write_text(json.dumps(BANKS), encoding="utf-8")
        self.letters = self.dir / "letters.tsv"
        self.pictures = self.dir / "pictures.json"
        with self.letters.open("w", encoding="utf-8", newline="") as f:
            f.write("picture\tchar\thow\tscore\tnearest\tuses\tnote\n")
            for n, ch in enumerate(CHARS):
                f.write(f"p{n}\t{ch}\tread\t\t\t1\t\n")
        self.pictures.write_text(json.dumps({"pictures": {f"p{n}": {"rows": picture(n), "uses": 1}
                                                          for n in range(len(CHARS))}}), encoding="utf-8")

    def table(self, name, rows):
        path = self.dir / name
        with path.open("w", encoding="utf-8", newline="") as f:
            w = csv.writer(f, lineterminator="\n")
            w.writerow(["key", "translation", "notes"])
            w.writerows([k, t, ""] for k, t in rows.items())
        return path

    def build(self, *args):
        out = self.dir / "zh.pelang"
        if out.exists():
            out.unlink()
        code = pelang.main(["build", *map(str, args), "--strings", str(self.strings), "--lang", "zh-Hans",
                            "--name", "Chinese", "-o", str(out)])
        return code, out

    def letters_args(self):
        return ["--cjk-letters", self.letters, "--cjk-pictures", self.pictures]

    def test_the_pack_holds_the_patchs_pictures(self):
        code, out = self.build(self.table("t.csv", TEXTS), *self.letters_args())
        self.assertEqual(code, 0)
        data = out.read_bytes()
        at, count = struct.unpack_from("<II", data, 64)
        records = {chr(struct.unpack_from("<I", data, at + k * cjk.RECORD)[0]):
                   data[at + k * cjk.RECORD + 4:at + (k + 1) * cjk.RECORD] for k in range(count)}
        self.assertEqual(set(records), set(CHARS))
        for n, ch in enumerate(CHARS):
            pixels = values(picture(n))
            self.assertEqual(records[ch], bytes(pixels[i] | pixels[i + 1] << 4 for i in range(0, 144, 2)), ch)
        _, _, entries = pelang.read_pack(out)
        self.assertIn(b"\xfb\x09\x02", entries[pelang.fnv1a("dlg/M001/002")])

    def test_a_letter_with_no_picture_or_only_half_the_options_fails(self):
        code, out = self.build(self.table("t.csv", {"dlg/M001/003": "好的。"}), *self.letters_args())
        self.assertEqual(code, 1)
        self.assertFalse(out.exists())
        code, _ = self.build(self.table("t.csv", TEXTS), "--cjk-letters", self.letters)
        self.assertEqual(code, 1)

    def test_two_discs_share_rows_only_with_the_same_text(self):
        one = self.table("disc1.csv", TEXTS)
        two = self.table("disc2.csv", {"dlg/M001/003": TEXTS["dlg/M001/003"]})
        code, out = self.build(one, two, *self.letters_args())
        self.assertEqual(code, 0)
        self.assertEqual(len([k for k in pelang.read_pack(out)[2]]), 3 + len(BANKS))
        other = self.table("disc2.csv", {"dlg/M001/003": "是。"})
        code, _ = self.build(one, other, *self.letters_args())
        self.assertEqual(code, 1)

    def test_rooms_and_rows_left_out_and_rows_adapted(self):
        table = self.table("t.csv", TEXTS)
        code, out = self.build(table, *self.letters_args(), "--leave-out", "M001")
        self.assertEqual(code, 0)
        entries = pelang.read_pack(out)[2]
        self.assertIn(pelang.fnv1a("item/000"), entries)
        self.assertNotIn(pelang.fnv1a("dlg/M001/002"), entries)
        self.assertNotIn(pelang.fnv1a("dlg/M001/003"), entries)
        code, out = self.build(table, *self.letters_args(), "--leave-out", "item/000")
        self.assertEqual(code, 0)
        entries = pelang.read_pack(out)[2]
        self.assertNotIn(pelang.fnv1a("item/000"), entries)
        self.assertIn(pelang.fnv1a("dlg/M001/003"), entries)
        # The choice written another way fails; the adapted row replaces it.
        broken = self.table("broken.csv", dict(TEXTS, **{"dlg/M001/002": "要放下吗？{br}    是{br}    否"}))
        self.assertEqual(self.build(broken, *self.letters_args())[0], 1)
        fix = self.table("adapt.csv", {"dlg/M001/002": TEXTS["dlg/M001/002"]})
        code, out = self.build(broken, *self.letters_args(), "--adapt", fix)
        self.assertEqual(code, 0)
        self.assertIn(b"\xfb\x09\x02", pelang.read_pack(out)[2][pelang.fnv1a("dlg/M001/002")])


if __name__ == "__main__":
    unittest.main()
