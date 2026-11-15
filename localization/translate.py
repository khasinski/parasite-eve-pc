"""Start the Parasite Eve Translator (localization/translator/): a window for translating the
game from the English pack into a new .pelang.

  python -I localization/translate.py [translation.pelang] [--english "English pack.pelang"]
"""
import os
import sys
from pathlib import Path

# A program packaged without a console (the download) has no stdout or stderr. The pack code
# prints its messages, so give it somewhere harmless to print them.
for _name in ("stdout", "stderr"):
    if getattr(sys, _name) is None:
        setattr(sys, _name, open(os.devnull, "w", encoding="utf-8"))

sys.path.insert(0, str(Path(__file__).resolve().parent))
from translator.app import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
