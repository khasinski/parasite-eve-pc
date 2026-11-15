"""Name the letter pictures of a Japanese-disc patch import, and write its translation CSVs.

  python localization/name_ja_patch_letters.py PICTURES.json [PICTURES.json ...] \\
      --drafts disc1-draft.json disc2-draft.json --reviewed letters-reviewed.tsv -o OUT_FOLDER

import_ja_patch.py leaves a draft whose letters are pictures ({pic ID}). This names each picture
(petext/hanzi.py: blank, slot, japanese, read from the reviewed file, exact, close, review) and
writes, into OUT_FOLDER (it must not exist yet):

  letters.tsv     every picture: its character, how it was named, the nearest characters
  review.png      the pictures still unnamed, beside their nearest characters, to read by eye
  review.txt      the same pictures, each with a line it is used in
  discN.csv       per draft: key, translation, notes; the format pelang.py builds packs from.
                  A page shared by rooms with different letter pages is read in the room
                  whose letters make Chinese (hanzi.choose_areas). Rows still holding an
                  unnamed picture are left out and listed in review.txt.

Name the review pictures in the reviewed file (TSV: picture, char, note) and run again.
Needs Pillow (run without -I when Pillow is a user install) and the PC's SimSun / Microsoft YaHei fonts. Nothing written here goes into the
repository.
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
from petext import hanzi  # noqa: E402


def load_pictures(paths: list[Path]) -> dict[str, dict]:
    pictures: dict[str, dict] = {}
    for path in paths:
        for pid, p in json.loads(path.read_text(encoding="utf-8"))["pictures"].items():
            if pid in pictures:
                pictures[pid]["uses"] = pictures[pid].get("uses", 0) + p.get("uses", 0)
            else:
                pictures[pid] = dict(p)
    return pictures


def write_csv(path: Path, rows: list[tuple[str, str]], note: str) -> None:
    with open(path, "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["key", "translation", "notes"])
        for key, text in rows:
            w.writerow([key, text, note])


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pictures", type=Path, nargs="+", help="pictures files from import_ja_patch.py")
    ap.add_argument("--drafts", type=Path, nargs="+", required=True, help="draft files from import_ja_patch.py")
    ap.add_argument("--reviewed", type=Path, help="pictures named by hand (TSV: picture, char, note)")
    ap.add_argument("-o", "--out", type=Path, required=True, help="new folder for the results")
    ap.add_argument("--fonts", type=Path, default=hanzi.FONT_DIR, help="folder with simsun.ttc / msyh.ttc")
    a = ap.parse_args(argv)
    if a.out.exists():
        print(f"{a.out} already exists; choose a new folder", file=sys.stderr)
        return 1
    faces = hanzi.default_faces(a.fonts)
    if not any(f.kind == "outline" for f in faces):
        print(f"No SimSun or Microsoft YaHei in {a.fonts}", file=sys.stderr)
        return 1
    pictures = load_pictures(a.pictures)
    drafts = [json.loads(p.read_text(encoding="utf-8")) for p in a.drafts]
    reviewed = hanzi.read_reviewed(a.reviewed)
    print(f"{len(pictures)} pictures, {len(reviewed)} named by hand; comparing the rest with the fonts...")
    matcher = hanzi.Matcher(faces, hanzi.gb2312_hanzi(), hanzi.gb2312_signs())
    names = hanzi.name_all(pictures, matcher, reviewed,
                           progress=lambda n, total: print(f"  {n}/{total}", flush=True))
    chars = hanzi.named(names)

    a.out.mkdir(parents=True)
    hanzi.write_letters(a.out / "letters.tsv", pictures, names)
    todo = hanzi.review_sheet(a.out / "review.png", pictures, names, faces[0])

    left_out: dict[str, list[str]] = {}       # picture -> keys of the rows it keeps out
    for draft, path in zip(drafts, a.drafts):
        rows = draft["rows"]
        chosen = hanzi.choose_areas(rows, names)
        out, seen = [], set()
        for row in rows:
            if row["key"] in seen:
                continue
            seen.add(row["key"])
            text, missing = hanzi.decode(chosen[row["key"]][1], chars)
            if missing:
                for pid in dict.fromkeys(missing):
                    left_out.setdefault(pid, []).append(row["key"])
                continue
            out.append((row["key"], text))
        name = f"disc{draft.get('disc', path.stem)}.csv"
        write_csv(a.out / name, out, "Japanese-disc patch import")
        print(f"{name}: {len(out)} rows, {len(seen) - len(out)} left out")

    with open(a.out / "review.txt", "w", encoding="utf-8") as f:
        f.write("Pictures with no name yet (see review.png), and the rows they keep out of the CSVs.\n")
        f.write("Add each to the reviewed file (picture<TAB>char<TAB>note) and run again.\n\n")
        for pid, p in sorted(pictures.items(), key=lambda kv: -kv[1].get("uses", 0)):
            n = names[pid]
            if n.how != "review":
                continue
            near = " ".join(f"{c}{s}" for c, s in n.nearest[:6])
            f.write(f"{pid}  uses {p.get('uses', 0)}  nearest: {near}\n")
            for key in left_out.get(pid, [])[:5]:
                f.write(f"    {key}\n")
    counts = Counter(n.how for n in names.values())
    print("named: " + ", ".join(f"{how} {counts[how]}" for how in
                                ("blank", "slot", "japanese", "read", "exact", "close", "review")))
    print(f"{todo} pictures to review: {a.out / 'review.png'}, {a.out / 'review.txt'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
