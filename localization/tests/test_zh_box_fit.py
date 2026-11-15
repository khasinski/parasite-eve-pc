"""The Chinese pack's dialogue stays on screen wherever the English does.

Most pages are tied to their box by the room script that opens them, and pelang.py build already
refuses a page that leaves the screen in such a box. About 150 pages are not: the script works the
page number out (an item box, an elevator's floor), so the build only knows the boxes the bank's
rooms open (boxes_possible) and warns "no script ties this page to a box". This test measures each
of those pages, English and Chinese, in every box it could be shown in: in a box where the English
stays on screen, the Chinese must too. Going past a styled box's rectangle (its shading) but staying
on screen is only listed: the English does that in places, and so did the patch on the Japanese game.

Needs the USA disc 1 (PE_TEST_DISC, every dialogue key is on it) and the pack (PE_TEST_ZH_PACK, plain
or the zipped player file); without them only the rule's own tests run and it exits 77.

  python -I localization/tests/test_zh_box_fit.py
"""
import collections
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
from petext import layout  # noqa: E402


def on_screen(pages, box):
    """True when every screen of `pages` stays on screen in `box`, None when its rectangle is computed."""
    edge = layout.bounds(box, screen=True)
    if edge is None:
        return None
    width, rows = edge
    return all(max(p) <= width and len(p) <= rows for p in pages)


def compare(english, translated, boxes):
    """(boxes where the translation leaves the screen but the English doesn't, boxes where it goes
    past the rectangle but the English doesn't): each a list."""
    lost, past = [], []
    for box in boxes:
        if on_screen(english, box) and on_screen(translated, box) is False:
            lost.append(box)
        elif layout.fits(english, box) and layout.fits(translated, box) is False:
            past.append(box)
    return lost, past


def page_end(raw):
    """A pack page without its F9 end (the box closes there); read_pack stops at FF."""
    return raw[:-1] if raw[-1:] == b"\xf9" else raw


class RuleTest(unittest.TestCase):
    STRIP = "styled 8,180,296,24"          # one line, from x 8
    RIGHT = "styled 150,160,134,22"        # 170 px to the screen edge

    def test_same_shape_passes(self):
        self.assertEqual(compare([[120]], [[120]], ["default", self.STRIP]), ([], []))

    def test_off_screen_where_english_is_on(self):
        self.assertEqual(compare([[100, 100]], [[180, 180]], [self.RIGHT]), ([self.RIGHT], []))

    def test_english_off_screen_too_is_not_the_translations_fault(self):
        # a six-choice menu: six lines in the four-line default box, in English as well
        self.assertEqual(compare([[147] * 6], [[96] * 6], ["default"]), ([], []))

    def test_extra_line_past_the_strip_is_only_listed(self):
        # the patch's leading line break: two lines in a one-line strip, still on screen
        self.assertEqual(compare([[120]], [[0, 144]], [self.STRIP]), ([], [self.STRIP]))

    def test_computed_rectangle_is_skipped(self):
        self.assertEqual(compare([[100]], [[400]], ["styled -1,0,10,10"]), ([], []))

    def test_page_end(self):
        self.assertEqual(page_end(b"\x10\xf9"), b"\x10")
        self.assertEqual(page_end(b"\x10"), b"\x10")


class PackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        disc, pack = os.environ.get("PE_TEST_DISC"), os.environ.get("PE_TEST_ZH_PACK")
        if not disc or not pack or not Path(disc).is_file() or not Path(pack).is_file():
            raise unittest.SkipTest("needs PE_TEST_DISC (USA disc 1) and PE_TEST_ZH_PACK")
        cls.out = Path(tempfile.mkdtemp())
        subprocess.run([sys.executable, "-I", str(HERE / "extract_text.py"), disc, "--out", str(cls.out)],
                       check=True, capture_output=True)
        cls.english = json.loads((cls.out / "strings.json").read_text(encoding="utf-8"))
        _, _, entries = pelang.read_pack(Path(pack))
        cls.pack = {h: page_end(raw) for h, raw in entries.items()}

    def test_pages_stay_on_screen_where_the_english_does(self):
        name = self.pack.get(pelang.fnv1a("sys/030"))
        name_px = layout.name_width(name) if name else layout.NAME_WIDTH
        lost, past, checked = [], collections.defaultdict(list), 0
        for e in self.english:
            key = e["key"]
            raw = self.pack.get(pelang.fnv1a(key))
            boxes = e["boxes"] or e["boxes_possible"]
            if not key.startswith("dlg/") or raw is None or not boxes:
                continue
            checked += 1
            off, over = compare(layout.screens(bytes.fromhex(e["raw"])), layout.screens(raw, name_px), boxes)
            lost += [f"{key} in {box}" for box in off]
            if over:
                past[key.split("/")[1]].append(key)
        print(f"\n{checked} dialogue pages measured; past a box's rectangle (on screen) where the English "
              f"isn't: {sum(map(len, past.values()))} ({', '.join(f'{b} {len(k)}' for b, k in sorted(past.items()))})")
        self.assertGreater(checked, 2000)
        self.assertEqual(lost, [], "off screen in a box where the English stays on screen")


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=2).result
    if not result.wasSuccessful():
        sys.exit(1)
    sys.exit(77 if result.skipped else 0)
