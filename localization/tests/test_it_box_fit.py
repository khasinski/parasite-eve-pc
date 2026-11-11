"""The Italian pack's dialogue stays on screen wherever the English does.

test_es_box_fit.py's check for the pack built from the Italian fan patch for the USA discs
(import-patch, build --from-patch --patch-letters). The patch redraws only its six accented letters
(à è é ì ò ù, each with its own width); the rest are the game's, so most of a page measures as in
English. Needs the USA disc 1 (PE_TEST_DISC) and the pack (PE_TEST_IT_PACK, plain or the zipped
player file); without them it exits 77.

  python -I localization/tests/test_it_box_fit.py
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_es_box_fit as es  # noqa: E402


class ItalianPackTest(es.PackTest):
    PACK_ENV = "PE_TEST_IT_PACK"
    # Off screen only in a box they can't be shown in, checked by hand (2026-11-10): two story lines,
    # each between lines a script ties to the default box, where they fit; too wide only for the
    # 6-line storage menu box.
    NOT_THEIR_BOX = frozenset({
        "dlg/M044/033 in styled 110,128,187,82",     # Wayne: Aspetta un minuto, {name}... (032, 034)
        "dlg/M044/109 in styled 110,128,187,82",     # Ben's line, as in Spanish (108, 110)
    })
    DRAWN_AT_LEAST = 6
    MEASURED_AT_LEAST = 1850        # 1875: the patch leaves about 100 pages as they are ("Daniel: Ben!")


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=2).result
    if not result.wasSuccessful():
        sys.exit(1)
    sys.exit(77 if result.skipped else 0)
