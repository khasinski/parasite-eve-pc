"""The French pack's dialogue stays on screen wherever the English does.

test_es_box_fit.py's check for the pack built from the French fan patch for the USA discs
(import-patch, build --from-patch --patch-letters). The patch adds accented letters with their own
widths, and draws many pairs of letters ("li", "es", "e " ...) in one code to fit more text: the
import writes them out, so the pack holds plain letters, as wide together as the patch's pair.
Needs the USA disc 1 (PE_TEST_DISC) and the pack (PE_TEST_FR_PACK, plain or the zipped player
file); without them it exits 77.

  python -I localization/tests/test_fr_box_fit.py
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_es_box_fit as es  # noqa: E402


class FrenchPackTest(es.PackTest):
    PACK_ENV = "PE_TEST_FR_PACK"
    # Checked by hand (2026-11-10).
    NOT_THEIR_BOX = frozenset({
        # The 6-choice storage menu shows in Wayne's 6-line menu box (110,128), where it fits; too
        # wide only for the one-line strip the same room opens, which can't hold six choices anyway
        # (as test_es_box_fit.py).
        "dlg/M044/160 in styled 150,160,134,22",
    })
    AS_ON_THE_PS1 = frozenset()
    DRAWN_AT_LEAST = 20
    MEASURED_AT_LEAST = 1900


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=2).result
    if not result.wasSuccessful():
        sys.exit(1)
    sys.exit(77 if result.skipped else 0)
