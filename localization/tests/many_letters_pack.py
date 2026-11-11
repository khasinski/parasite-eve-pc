"""A pack whose text needs more letters than the 163 one-byte codes: every letter of
petext/letters.py's catalog, built by `pelang.py build` from a small synthetic strings.json (no game
data needed). localization/tests/test_letters.py checks it in Python; with --check-with, CTest's
mod-lang-many-letters hands it to the game's C reader and letter cache (tests/pe_mod_lang_font_test.c).

  python -I localization/tests/many_letters_pack.py --check-with build/test-mod-lang-font
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pelang  # noqa: E402
from petext import letters  # noqa: E402

LINE = 20                                   # letters a dialogue line


def catalog_text() -> str:
    # letters only: the catalog's signs (dashes, quotes) are fine too, but keep the lines words. Not
    # Arabic joined shapes: the renderer picks those itself, and they never take a code.
    from petext import rtl
    return "".join(ch for ch in letters.CATALOG if ch.isalpha() and not rtl.is_joining(ord(ch)))


# Item names keep one byte a letter, so their letters need codes: the catalog's last two, which by
# Unicode order alone would get none.
ITEM_LETTERS = "".join(sorted(catalog_text(), key=ord)[-2:])


def build(folder: Path, text: str | None = None, items: str = ITEM_LETTERS, extra=()) -> tuple[int, Path]:
    """(exit code, pack): `text` in dialogue lines, `items` as an item name."""
    text = catalog_text() if text is None else text
    chunks = [text[i:i + LINE] for i in range(0, len(text), LINE)]
    strings = [{"key": f"dlg/M001/{k:03}", "text": "Hello there.", "lines_px": [70], "boxes": ["default"],
                "rooms": ["M001"], "unused": False} for k in range(len(chunks))]
    strings.append({"key": "item/054", "text": "Chrysler Key1", "lines_px": [80], "boxes": [], "rooms": [],
                    "unused": False})
    (folder / "strings.json").write_text(json.dumps(strings), encoding="utf-8")
    (folder / "banks.json").write_text(json.dumps(
        [{"key": "M001", "crc32": "1a2b3c4d", "size": 120, "rooms": ["M001"]}]), encoding="utf-8")
    rows = pelang.template_rows(pelang.load_strings(folder / "strings.json"))
    by_key = {f"dlg/M001/{k:03}": chunk for k, chunk in enumerate(chunks)}
    if items:
        by_key["item/054"] = items
    for r in rows:
        r["translation"] = by_key.get(r["key"], "")
    pelang.write_csv(folder / "t.csv", rows)
    out = folder / "many.pelang"
    code = pelang.main(["build", str(folder / "t.csv"), "--strings", str(folder / "strings.json"),
                        "--lang", "xx", "--name", "Many letters", "--allow-overflow", "-o", str(out), *extra])
    return code, out


def main(argv: list[str]) -> int:
    if len(argv) != 2 or argv[0] != "--check-with":
        print(__doc__)
        return 2
    with tempfile.TemporaryDirectory() as tmp:
        code, pack = build(Path(tmp))
        if code:
            print("error: pelang.py build failed")
            return 1
        return subprocess.run([argv[1], "--many-letters", str(pack), str(len(catalog_text()))]).returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
