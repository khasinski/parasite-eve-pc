"""The Spanish pack's dialogue stays on screen wherever the English does.

The same check as test_zh_box_fit.py, for the pack built from the Spanish fan patch for the USA
discs (import-patch, build --from-patch --patch-letters). That pack draws most letters the patch's
way, narrower than the game's, so each page is measured with the widths the pack itself carries
(its letter records: what pe_lang lays the text out with), not the game's. About 140 of its pages
are tied to no box by a script, so the build can only warn about them; here each is measured in
every box it could be shown in. Going past a box's rectangle (its shading) but staying on screen is
only listed, as the patched game drew it.

Needs the USA disc 1 (PE_TEST_DISC) and the pack (PE_TEST_ES_PACK, plain or the zipped player
file); without them only the rule's own tests run and it exits 77.

  python -I localization/tests/test_es_box_fit.py
"""
import collections
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pelang  # noqa: E402
from petext import gametext as gt, layout, letters  # noqa: E402
from test_zh_box_fit import compare, page_end  # noqa: E402


def pack_widths(data: bytes) -> dict[int, letters.Letter]:
    """code -> a Letter with the dialogue advance the pack gives it, for every coded letter record
    (a letter drawn the pack's way, or one of its own table's): how the pack's text is measured."""
    at, count = struct.unpack_from("<II", data, 80)
    out = {}
    for k in range(count):
        record = data[at + k * letters.RECORD:at + (k + 1) * letters.RECORD]
        if letters.coded(k, record) and record[5]:
            out[record[4]] = letters.Letter(chr(struct.unpack_from("<I", record)[0]), dialogue=record[5])
    return out


BLANK = (0,) * (letters.CELL * letters.CELL)

# Pages that go off screen only in a box they can't be shown in, checked by hand (2026-11-10). Both
# are drawn as the patch laid them out, with its widths, so as on the patched PS1 game.
NOT_THEIR_BOX = {
    # Ben's line, between two lines a script ties to the default box (108, 110), where it fits;
    # 2 px past the screen only in the 6-line storage menu box.
    "dlg/M044/109 in styled 110,128,187,82",
    # The 6-choice storage menu: fits the 6-line menu box (110,128); too wide only for the one-line
    # strip, which can't hold six choices anyway.
    "dlg/M044/160 in styled 150,160,134,22",
}


class RuleTest(unittest.TestCase):
    def tearDown(self):
        gt.PACK_DRAWN.clear()

    def test_the_packs_widths_measure_its_text(self):
        w, space = gt.CODE_OF["W"], gt.CODE_OF[" "]
        records = (letters.Letter("W", pixels=BLANK, dialogue=6).record(w)
                   + letters.Letter(" ", pixels=BLANK, dialogue=12).record(space))
        data = bytearray(96) + records
        struct.pack_into("<II", data, 80, 96, 2)
        widths = pack_widths(bytes(data))
        self.assertEqual({c: l.advance() for c, l in widths.items()}, {w: 6, space: 12})
        line = gt.encode("WW WW")
        self.assertEqual(max(map(max, layout.screens(line))), 4 * gt.BOX_ADVANCE[w] + gt.BOX_ADVANCE[space])
        gt.PACK_DRAWN.update(widths)
        # the box still pulls a space back as much as the game's (gametext.box_advance)
        self.assertEqual(max(map(max, layout.screens(line))), 4 * 6 + 12 - (gt.ADVANCE[space] - gt.BOX_ADVANCE[space]))

    def test_a_record_without_a_code_is_not_measured(self):
        data = bytearray(96) + letters.Letter("ŋ", pixels=BLANK, dialogue=7).record(0)
        struct.pack_into("<II", data, 80, 96, 1)
        self.assertEqual(pack_widths(bytes(data)), {})


class PackTest(unittest.TestCase):
    """Measures the pack PACK_ENV names. test_it_box_fit.py runs it for the Italian pack."""
    PACK_ENV = "PE_TEST_ES_PACK"
    NOT_THEIR_BOX = NOT_THEIR_BOX
    DRAWN_AT_LEAST = 50             # letters the pack draws the patch's way (Spanish: 91)
    MEASURED_AT_LEAST = 1900        # dialogue pages the pack translates (Spanish: 1975)

    @classmethod
    def setUpClass(cls):
        disc, pack = os.environ.get("PE_TEST_DISC"), os.environ.get(cls.PACK_ENV)
        if not disc or not pack or not Path(disc).is_file() or not Path(pack).is_file():
            raise unittest.SkipTest(f"needs PE_TEST_DISC (USA disc 1) and {cls.PACK_ENV}")
        cls.out = Path(tempfile.mkdtemp())
        subprocess.run([sys.executable, "-I", str(HERE / "extract_text.py"), disc, "--out", str(cls.out)],
                       check=True, capture_output=True)
        cls.english = json.loads((cls.out / "strings.json").read_text(encoding="utf-8"))
        cls.widths = pack_widths(pelang.pack_bytes(Path(pack)))
        _, _, entries = pelang.read_pack(Path(pack))
        cls.pack = {h: page_end(raw) for h, raw in entries.items()}

    def tearDown(self):
        gt.PACK_DRAWN.clear()

    def test_pages_stay_on_screen_where_the_english_does(self):
        self.assertGreaterEqual(len(self.widths), self.DRAWN_AT_LEAST, "the pack draws its letters the patch's way")
        english_pages = {e["key"]: layout.screens(bytes.fromhex(e["raw"])) for e in self.english
                         if e["key"].startswith("dlg/") and (e["boxes"] or e["boxes_possible"])
                         and pelang.fnv1a(e["key"]) in self.pack}
        gt.PACK_DRAWN.update(self.widths)
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
            off, over = compare(english_pages[key], layout.screens(raw, name_px), boxes)
            lost += [f"{key} in {box}" for box in off]
            if over:
                past[key.split("/")[1]].append(key)
        print(f"\n{checked} dialogue pages measured; past a box's rectangle (on screen) where the English "
              f"isn't: {sum(map(len, past.values()))} ({', '.join(f'{b} {len(k)}' for b, k in sorted(past.items()))})")
        self.assertGreaterEqual(checked, self.MEASURED_AT_LEAST)
        self.assertEqual([p for p in lost if p not in self.NOT_THEIR_BOX], [],
                         "off screen in a box where the English stays on screen")


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=2).result
    if not result.wasSuccessful():
        sys.exit(1)
    sys.exit(77 if result.skipped else 0)
