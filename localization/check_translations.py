"""Check and build every submitted language from the committed source manifest (no discs), as CI
does: localization/translations/<lang>.csv and the pilots in localization/pilots/.

  python -I localization/check_translations.py [--out out/ci] [lang ...]

Writes out/ci/<lang>.pelang, out/ci/<lang>.txt (the compiler's report) and out/ci/summary.md;
exits 1 if any language has errors. Language names come from translations/languages.json.
"""
from __future__ import annotations

import argparse
import contextlib
import io
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
import pelang  # noqa: E402

MANIFEST = ROOT / "data" / "manifest.json"
MOD_STRINGS = ROOT / "data" / "mod_strings.json"


def submissions() -> dict[str, Path]:
    found = {p.stem: p for p in sorted((ROOT / "pilots").glob("*.csv"))}
    found.update({p.stem: p for p in sorted((ROOT / "translations").glob("*.csv"))})   # a submission wins
    return found


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("langs", nargs="*")
    ap.add_argument("--out", type=Path, default=ROOT.parent / "out" / "ci")
    a = ap.parse_args(argv)
    names_file = ROOT / "translations" / "languages.json"
    names = json.loads(names_file.read_text(encoding="utf-8")) if names_file.exists() else {}
    a.out.mkdir(parents=True, exist_ok=True)
    rows, failed = [], 0
    for lang, path in submissions().items():
        if a.langs and lang not in a.langs:
            continue
        report = io.StringIO()
        args = ["build", str(path), "--strings", str(MANIFEST), "--submission", "--lang", lang,
                "--name", names.get(lang, lang), "-o", str(a.out / f"{lang}.pelang"), "--max-messages", "100000"]
        if MOD_STRINGS.exists():
            args += ["--mod-strings", str(MOD_STRINGS)]
        with contextlib.redirect_stdout(report), contextlib.redirect_stderr(report):
            code = pelang.main(args)
        text = report.getvalue()
        (a.out / f"{lang}.txt").write_text(text, encoding="utf-8")
        summary = next((line for line in text.splitlines() if "keys translated" in line), "no summary")
        failed += code != 0
        rows.append(f"| {lang} | {names.get(lang, lang)} | {'ok' if code == 0 else 'ERRORS'} | {summary} |")
        print(f"{lang}: {'ok' if code == 0 else 'errors'}: {summary}")
    md = ["| lang | name | result | |", "|---|---|---|---|"] + rows
    (a.out / "summary.md").write_text("\n".join(md) + "\n", encoding="utf-8")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
