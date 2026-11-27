"""The German pack's dialogue stays on screen wherever the English does.

test_es_box_fit.py's check for the pack built from the German fan patch for the USA discs
(import-patch, build --from-patch --patch-letters). The patch redraws every letter, with its own
widths, and writes two letters with one code in its dialogue (the import writes them out, so the
pack holds plain letters). Needs the USA disc 1 (PE_TEST_DISC) and the pack (PE_TEST_DE_PACK, plain
or the zipped player file); without them it exits 77.

  python -I localization/tests/test_de_box_fit.py
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_es_box_fit as es  # noqa: E402


class GermanPackTest(es.PackTest):
    PACK_ENV = "PE_TEST_DE_PACK"
    # Checked by hand (2026-11-10).
    NOT_THEIR_BOX = frozenset({
        # The 6-choice storage menu shows in Wayne's 6-line menu box (110,128); the 7-line and the
        # one-line boxes the same room opens can't be its box (as test_es_box_fit.py).
        "dlg/M044/160 in styled 135,115,162,94",
        "dlg/M044/160 in styled 150,160,134,22",
    })
    AS_ON_THE_PS1 = frozenset({
        # Its last choice, "Modifikationsgegenstand nehmen", is 220 px from x 110: 10 px past the
        # screen. The patch widens the box to 200 px (M048's script; a pack can't change scripts),
        # but no box fits it on screen, so the patched PS1 game draws it 10 px past the edge as well,
        # at the same place with the same letters and widths.
        "dlg/M044/160 in styled 110,128,187,82",
    })
    DRAWN_AT_LEAST = 80
    MEASURED_AT_LEAST = 1900


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=2).result
    if not result.wasSuccessful():
        sys.exit(1)
    sys.exit(77 if result.skipped else 0)
