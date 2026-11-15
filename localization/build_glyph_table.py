"""Build the Japanese glyph table: which character each glyph of the Japanese dialogue font draws,
so the Japanese script can be decoded to text.

  python -I build_glyph_table.py JP_DISC1.cue [JP_DISC2.cue] [--out data/ja_glyphs.tsv]

Every distinct glyph bitmap the Japanese dialogue uses is read with Windows' Japanese OCR
(ocr.ps1; needs the Japanese language pack). Glyphs are drawn large in rows of 20 (OCR reads
a row far better than one glyph alone) and read three times, each time in a different order so
every glyph gets other neighbours; the answers are voted. Small kana (ぁ ッ ャ ...) read as their
full-size forms, so a short glyph's answer is turned into its small form; ば and ぱ rows are told
apart by the glyph's own mark (voicing_mark).

The table (TSV) keeps per glyph: its id (hash of its pixels), the character, how many of the three
reads agreed, the fonts and cells it sits in, and a `fixed` column: a character typed there wins
over OCR and survives a rebuild. out/font/review.png shows the glyphs that need an eye (reads that
disagreed or found nothing), numbered as in out/font/review.tsv. The table holds character
identities only, no game text or art.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import random
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

from petext import font, gametext as gt, jatext as ja  # noqa: E402
from petext.disc import Game  # noqa: E402
import extract_text as ex  # noqa: E402

SCALE, PITCH, MARGIN, PER_ROW = 6, 18 * 6, 40, 20
SMALL = dict(zip("あいうえおつやゆよわかけアイウエオツヤユヨワカケ", "ぁぃぅぇぉっゃゅょゎゕゖァィゥェォッャュョヮヵヶ"))
SMALL_HEIGHT = 8                  # small kana are at most this many pixels tall; full kana 10+
# OCR mixes up ば and ぱ: the font draws the handakuten as a solid 2x2 block in the top right,
# the dakuten as two separate dots, so each glyph of these rows is set by its own mark.
HANDAKUTEN = dict(zip("ばびぶべぼバビブベボ", "ぱぴぷぺぽパピプペポ"))
TO_DAKUTEN = {v: k for k, v in HANDAKUTEN.items()}
CONTEXT_MIN, CONTEXT_SHARE = 3, 0.6    # a context read counts with 3+ reads, 60% of them agreeing


def voicing_mark(cell) -> str | None:
    """'handakuten' if the glyph's top right (rows 0-4, columns 6-11) has a solid 2x2 block,
    'dakuten' if it has a row with two dots apart, else None."""
    on = lambda x, y: cell[y * 12 + x] == 1
    for y in range(4):
        for x in range(6, 11):
            if on(x, y) and on(x + 1, y) and on(x, y + 1) and on(x + 1, y + 1):
                return "handakuten"
    for y in range(4):
        row = [x for x in range(6, 12) if on(x, y)]
        if len(row) >= 2 and any(b - a == 2 for a, b in zip(row, row[1:])):
            return "dakuten"
    return None


def rare_kanji(char: str) -> bool:
    """A kanji outside JIS level 1 (Shift-JIS 0x889F..0x9872): the game's text almost never
    uses one, so such a read is usually a variant form OCR preferred."""
    if not (len(char) == 1 and "一" <= char <= "鿿"):
        return False
    try:
        return int.from_bytes(char.encode("shift_jis"), "big") >= 0x989F
    except UnicodeEncodeError:
        return True


def voiced(char: str, cell) -> str:
    """The ba/pa-row form the glyph's own mark says, for an OCR answer in either row."""
    if char in HANDAKUTEN or char in TO_DAKUTEN:
        mark = voicing_mark(cell)
        if mark == "handakuten":
            return HANDAKUTEN.get(char, char)
        if mark == "dakuten":
            return TO_DAKUTEN.get(char, char)
    return char


def collect(discs, lines: set | None = None) -> dict[str, dict]:
    """Every distinct glyph bitmap the Japanese text draws: id -> cell pixels, slots, uses.
    Room dialogue uses page 0 and its area's page 1; the resident tables (item names, system
    messages) only page 0. With `lines`, also adds each line of text as a tuple of glyph ids."""
    glyphs: dict[str, dict] = {}

    def add(pages, slot, cell_no):
        pal, w, h, px = pages[slot]
        cell = font.cell(px, w, cell_no)
        key = hashlib.sha1(bytes(cell)).hexdigest()[:12]
        gl = glyphs.setdefault(key, {"cell": cell, "slots": set(), "uses": 0})
        gl["slots"].add(f"{slot}:{cell_no}")
        gl["uses"] += 1
        return key

    def add_page(pages, slot1, raw):
        line = []
        for kind, a, b in ja.tokens(raw):
            if kind == "glyph":
                pg, c = font.glyph_cell(a, b)
                line.append(add(pages, "p0" if pg == 0 else slot1, c))
            elif a in ("{br}", "{wait}", "{end2}") or a.startswith("{choice"):
                if lines is not None and len(line) > 1:
                    lines.add(tuple(line))
                line = []
        if lines is not None and len(line) > 1:
            lines.add(tuple(line))

    for n, path in enumerate(discs):
        g = Game(path)
        if g.region != "Japan":
            raise SystemExit(f"{path}: not a Japanese disc ({g.exe_name})")
        pages = {"p0": font.page(g, font.PAGE0_ENTRY)}
        if n == 0:
            for key, raw in ex.table_entries(g.resident_pack()):
                if all(font.glyph_cell(p, c)[0] == 0 for p, c in ja.glyph_tokens(raw)):
                    add_page(pages, None, raw)
        for m in range(1, 451):
            r = g.room(m)
            slots = ex.bank_slots(r.data) if r else {}
            if 0 not in slots:
                continue
            e = ja.room_type(r.data) + 0x2B
            if f"e{e}" not in pages:
                pages[f"e{e}"] = font.page(g, e)
            for raw, _ in gt.pages(r.data[slots[0]:])[0].values():
                add_page(pages, f"e{e}", raw)
    return glyphs


def ocr_lines(lines: list[tuple], glyphs: dict, work: Path, per_image=24, scale=4) -> dict[str, Counter]:
    """OCR real lines of text drawn with the game's own glyphs, so the OCR's language model sees
    words, not lone glyphs: glyph id -> Counter of the characters read at its place."""
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    pitch, row_h = 13 * scale, 24 * scale
    longest = max(len(l) for l in lines)
    batches = [lines[i:i + per_image] for i in range(0, len(lines), per_image)]
    for n, batch in enumerate(batches):
        im = Image.new("L", (MARGIN * 2 + pitch * longest, MARGIN * 2 + row_h * len(batch)), 255)
        for j, line in enumerate(batch):
            for i, k in enumerate(line):
                im.paste(glyph_image(glyphs[k]["cell"], scale), (MARGIN + i * pitch, MARGIN + j * row_h))
        im.save(work / f"lines_{n:04d}.png")
    tsv = work.parent / (work.name + ".tsv")
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    str(ROOT / "ocr.ps1"), "-Dir", str(work), "-Out", str(tsv)],
                   check=True, capture_output=True)
    votes: dict[str, Counter] = {}
    for rec in tsv.read_text(encoding="utf-8-sig").splitlines():
        name, x, y, w, h, text = rec.split("\t")
        batch = batches[int(name[6:10])]
        j = round((int(y) + int(h) / 2 - MARGIN - 6 * scale) / row_h)
        if not 0 <= j < len(batch):
            continue
        line = batch[j]
        x, w = int(x), int(w)
        # Only words whose box spans exactly as many glyphs as they have characters: anything
        # else (a merged or split glyph) would shift every vote after it.
        first = round((x - MARGIN) / pitch)
        last = round((x + w - MARGIN - 12 * scale) / pitch)
        if last - first + 1 != len(text) or not 0 <= first <= last < len(line):
            continue
        for c, ch in enumerate(text):
            votes.setdefault(line[first + c], Counter())[ch] += 1
    return votes


def glyph_image(cell, scale=SCALE) -> Image.Image:
    im = Image.new("L", (12, 12), 255)
    im.putdata([0 if v == 1 else 255 for v in cell])
    return im.resize((12 * scale, 12 * scale), Image.NEAREST)


def height(cell) -> int:
    rows = [y for y in range(12) if any(cell[y * 12 + x] == 1 for x in range(12))]
    return rows[-1] - rows[0] + 1 if rows else 0


def ocr_pass(keys: list[str], glyphs: dict, work: Path) -> dict[str, str]:
    """One OCR read of `keys` in rows of PER_ROW, in the given order: key -> character read."""
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    rows = [keys[i:i + PER_ROW] for i in range(0, len(keys), PER_ROW)]
    for n, row in enumerate(rows):
        im = Image.new("L", (MARGIN * 2 + PITCH * PER_ROW, 12 * SCALE + MARGIN * 2), 255)
        for i, k in enumerate(row):
            im.paste(glyph_image(glyphs[k]["cell"]), (MARGIN + i * PITCH, MARGIN))
        im.save(work / f"row_{n:03d}.png")
    tsv = work.parent / (work.name + ".tsv")
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    str(ROOT / "ocr.ps1"), "-Dir", str(work), "-Out", str(tsv)],
                   check=True, capture_output=True)
    got: dict[str, str] = {}
    for line in tsv.read_text(encoding="utf-8-sig").splitlines():
        name, x, y, w, h, text = line.split("\t")
        row = rows[int(name[4:7])]
        x, w = int(x), int(w)
        for j, ch in enumerate(text):
            centre = x + w * (j + 0.5) / len(text)
            i = round((centre - MARGIN - 6 * SCALE) / PITCH)
            if 0 <= i < len(row) and row[i] not in got:
                got[row[i]] = ch
    return got


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("discs", nargs="+")
    ap.add_argument("--out", default=str(ROOT / "data" / "ja_glyphs.tsv"))
    a = ap.parse_args()
    out = Path(a.out)
    work = ROOT / "out" / "font"

    lines: set = set()
    glyphs = collect(a.discs, lines)
    keys = sorted(glyphs, key=lambda k: -glyphs[k]["uses"])
    blank = [k for k in keys if not any(v == 1 for v in glyphs[k]["cell"])]
    ink = [k for k in keys if k not in blank]
    print(f"{len(glyphs)} glyphs ({len(blank)} blank), {len(lines)} distinct lines of text")

    orders = [ink, list(reversed(ink)), random.Random(1).sample(ink, len(ink))]
    reads = [ocr_pass(o, glyphs, work / f"ocr{n}") for n, o in enumerate(orders)]
    context = ocr_lines(sorted(lines), glyphs, work / "lines")
    print(f"context reads for {len(context)} glyphs")

    old = {}
    if out.exists():
        with open(out, encoding="utf-8") as f:
            old = {r["glyph"]: r for r in csv.DictReader(f, delimiter="\t")}
    def tidy(char, cell):
        if char in SMALL and height(cell) <= SMALL_HEIGHT:
            char = SMALL[char]
        return voiced(char, cell)

    rows = []
    for k in keys:
        gl = glyphs[k]
        ctx_char, ctx_note = "", ""
        if k in blank:
            char, agree = " ", 3
        else:
            votes = Counter(r[k] for r in reads if k in r)
            char, agree = votes.most_common(1)[0] if votes else ("", 0)
            char = tidy(char, gl["cell"])
            # Context: look-alike pairs count as one (the decoder picks the form per place).
            ctx = Counter()
            for ch, v in context.get(k, Counter()).items():
                ctx[ja.PAIR.get(ch, (ch,))[0]] += v
            total = sum(ctx.values())
            if total:
                top, v = ctx.most_common(1)[0]
                ctx_note = f"{v}/{total}"
                if total >= CONTEXT_MIN and v / total >= CONTEXT_SHARE:
                    ctx_char = tidy(top, gl["cell"])
        fixed = old.get(k, {}).get("fixed", "")
        fixed = voiced(fixed, gl["cell"]) if fixed else ""
        # Context only settles Japanese glyphs: Latin letters and digits read reliably alone and
        # their neighbours in a line are often other scripts.
        if ctx_char and (char.isascii() or ctx_char.isascii()):
            ctx_char = ""
        if ctx_char and ja.PAIR.get(ctx_char, (ctx_char,))[0] != ja.PAIR.get(char, (char,))[0]:
            char = ctx_char
        rows.append({"glyph": k, "char": char, "agree": agree, "context": ctx_char, "context_votes": ctx_note,
                     "fixed": fixed, "uses": gl["uses"], "slots": " ".join(sorted(gl["slots"]))})

    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]), delimiter="\t")
        w.writeheader()
        w.writerows(rows)

    # Review sheet: glyphs with neither a fix nor a confident context read whose row reads
    # disagreed, fixes that a confident context read contradicts, and unfixed glyphs read as a
    # rare kanji (OCR likes variant forms: 逹 for 達, 釛 for 動, 髙 for 高).
    canon = lambda c: ja.PAIR.get(c, (c,))[0]
    review = [r for r in rows if (not r["fixed"] and not r["context"] and r["agree"] < 3)
              or (r["fixed"] and r["context"] and canon(r["fixed"]) != canon(r["context"]))
              or (not r["fixed"] and rare_kanji(r["char"]))]
    cols = 10
    sheet = Image.new("RGB", (cols * 96, max(1, (len(review) + cols - 1) // cols) * 64), (255, 255, 255))
    d = ImageDraw.Draw(sheet)
    face = ImageFont.truetype(r"C:\Windows\Fonts\msgothic.ttc", 22, index=0)
    for i, r in enumerate(review):
        x, y = (i % cols) * 96, (i // cols) * 64
        sheet.paste(glyph_image(glyphs[r["glyph"]]["cell"], 3).convert("RGB"), (x + 4, y + 20))
        d.text((x + 4, y + 2), str(i), fill=(160, 0, 0))
        d.text((x + 44, y + 26), r["fixed"] or r["char"] or "?", font=face, fill=(0, 0, 170))
        if r["context"]:
            d.text((x + 70, y + 26), r["context"], font=face, fill=(0, 130, 0))
    sheet.save(work / "review.png")
    with open(work / "review.tsv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["n", "glyph", "ocr", "agree", "fixed", "context", "context_votes", "uses"])
        for i, r in enumerate(review):
            w.writerow([i, r["glyph"], r["char"], r["agree"], r["fixed"], r["context"], r["context_votes"], r["uses"]])
    print(f"written {out}: {len(rows)} glyphs; " + ", ".join(
        f"{n} of 3 agree: {sum(1 for r in rows if r['agree'] == n)}" for n in (3, 2, 1, 0))
        + f"; {len(review)} to review in {work / 'review.png'}")


if __name__ == "__main__":
    main()
