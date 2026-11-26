"""The Czech pack's dialogue stays on screen wherever the English does.

test_es_box_fit.py's check for the pack built from the Czech fan patch for the USA discs
(RK-Translations; import-patch, build --from-patch --patch-letters). The patch redraws most of the
game's letters and adds Á Č Ď É Ě Í Ň Ó Ř Š Ť Ú Ů Ý Ž and the same small, each with its own width;
the pack draws them all with the patch's pictures and widths. Needs the USA disc 1 (PE_TEST_DISC)
and the pack (PE_TEST_CS_PACK, plain or the zipped player file); without them it exits 77.

  python -I localization/tests/test_cs_box_fit.py
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_es_box_fit as es  # noqa: E402


class CzechPackTest(es.PackTest):
    PACK_ENV = "PE_TEST_CS_PACK"
    # Checked by hand (2026-11-17). The pack draws every letter with the patch's picture and width,
    # and the patch leaves Wayne's rooms' scripts (and so their boxes) as they are, so each page is
    # where the patched PS1 game has it.
    NOT_THEIR_BOX = frozenset({
        # "Nalezen klíč od skladu zbraní." (215 px), between two lines a script ties to the default
        # box (157, 159), where it fits; past the screen only in the 6-line storage menu box (210 px
        # to the edge from x 110; as test_es_box_fit.py's M044/109).
        "dlg/M044/158 in styled 110,128,187,82",
        # The 6-choice storage menu shows in Wayne's 6-line menu box (110,128), where it fits (193 of
        # 210 px); the 7-line and the one-line boxes the same room opens can't be its box (as
        # test_de_box_fit.py).
        "dlg/M044/160 in styled 135,115,162,94",
        "dlg/M044/160 in styled 150,160,134,22",
    })
    AS_ON_THE_PS1 = frozenset()
    DRAWN_AT_LEAST = 90
    MEASURED_AT_LEAST = 1900


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=2).result
    if not result.wasSuccessful():
        sys.exit(1)
    sys.exit(77 if result.skipped else 0)
