"""Read a fan patch made for the Japanese discs, for a USA-disc language pack (petext/japatch.py).

  python -I localization/import_ja_patch.py "Parasite Eve (Japan) (Disc 1).cue" \\
      --us "Parasite Eve (USA) (Disc 1).cue" PATCH [PATCH ...] \\
      -o draft.json --pictures pictures.json --report report.json

Each PATCH (PPF 1, 2 or 3, or xdelta) is applied in the order given, the way a player stacks a fix
on top of the first release; or give one patched copy of the disc (.bin or .cue) instead. The
patches go onto temporary copies next to the output; the disc image is only read, and no
patcher program is run. The USA disc of the same number is used to check that the Japanese room
scripts open the same pages as the USA ones; leave it out to skip that check.

The draft's letters are pictures ({pic ID}); naming them comes next. Nothing written here goes
into the repository.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
from petext import japatch, patchfile  # noqa: E402
from petext.disc import Game  # noqa: E402
from petext.script import opcodes  # noqa: E402

IMAGES = (".bin", ".iso", ".img", ".cue")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("disc", type=Path, help="unmodified Japanese Disc 1 or Disc 2 (CUE or BIN)")
    ap.add_argument("patches", type=Path, nargs="+", help="the patches, in the order they go on, or a patched copy")
    ap.add_argument("--us", type=Path, help="the USA disc of the same number, for the line-up check")
    ap.add_argument("-o", "--out", type=Path, required=True, help="new draft (JSON)")
    ap.add_argument("--pictures", type=Path, required=True, help="new letter pictures file (JSON)")
    ap.add_argument("--report", type=Path, required=True, help="new report (JSON)")
    ap.add_argument("--src", type=Path, default=ROOT.parent, help="the port checkout, for the opcode table")
    a = ap.parse_args(argv)
    if any(p.exists() for p in (a.out, a.pictures, a.report)):
        print("Output files already exist; choose new paths", file=sys.stderr)
        return 1
    images = [p for p in a.patches if p.suffix.lower() in IMAGES]
    if images and len(a.patches) > 1:
        print("Give either patches or one patched copy of the disc, not both", file=sys.stderr)
        return 1
    games = []
    try:
        with tempfile.TemporaryDirectory(dir=a.out.parent.resolve()) as folder:
            original = Game(a.disc)
            games.append(original)
            if original.region != "Japan":
                raise ValueError(f"{a.disc} is not a Japanese disc ({original.exe_name})")
            image = images[0] if images else original.disc.f.name
            for n, patch in enumerate([] if images else a.patches):
                step = Path(folder) / f"patched-{n}.bin"
                print(f"{patch.name}: {patchfile.apply_patch(patch, image, step)} patch applied to a temporary copy")
                if n:
                    Path(image).unlink()
                image = step
            patched = Game(image)
            games.append(patched)
            us = Game(a.us) if a.us else None
            if us is not None:
                games.append(us)
            rows, pictures, report = japatch.read(original, patched, us, opcodes(a.src))
            report["patches"] = [p.name for p in a.patches]
            note = ("Draft from a patch for the Japanese discs: each row's letters are pictures ({pic ID}, "
                    "see the pictures file). The patch's text and art: it stays out of the repository.")
            with a.out.open("x", encoding="utf-8") as f:
                json.dump({"note": note, "disc": report["disc"], "rows": rows}, f, ensure_ascii=False, indent=1)
                f.write("\n")
            with a.pictures.open("x", encoding="utf-8") as f:
                json.dump({"note": "12 rows: ' ' empty, '#' stroke, '+' shadow. The patch's art.", "pictures": pictures},
                          f, ensure_ascii=False, indent=1)
                f.write("\n")
            with a.report.open("x", encoding="utf-8") as f:
                json.dump(report, f, ensure_ascii=False, indent=2)
                f.write("\n")
            for game in games:
                game.disc.f.close()
            games.clear()
        print(f"{report['draft_rows']} rows ({report['dialogue_rows']} dialogue, {report['table_rows']} table); "
              f"{len(report['kept_english'])} keep the English; {report['pictures']} letter pictures "
              f"({report['pictures_still_japanese']} still Japanese ones); font pages redrawn: "
              f"{len(report['font_pages_redrawn'])}; rooms with edited scripts: {len(report['rooms_with_edited_scripts'])}. "
              f"Review {a.report}.")
        return 0
    except (OSError, ValueError, EOFError, KeyError, struct.error) as error:
        print(f"Japanese patch import failed: {error}", file=sys.stderr)
        return 1
    finally:
        for game in games:
            game.disc.f.close()


if __name__ == "__main__":
    sys.exit(main())
