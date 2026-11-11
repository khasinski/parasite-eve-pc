"""Name the letter pictures of a Chinese fan patch: which character each picture is.

A patch for the Japanese discs draws its Chinese letters into the Japanese font cells
(petext/japatch.py), so its text holds no character codes, only pictures. This compares each
picture with the characters drawn by fonts on the PC and names it, with how sure it is:

  blank     a picture with no stroke: the full-width space
  slot      page 0's first cells hold the Latin letters, digits and signs (jatext.LATIN); a patch
            redraws them in its own style but keeps them where the game expects them
  japanese  a letter the patch left as the Japanese font drew it (petext/japatch.py names it)
  read      named by hand, from the text around it (the reviewed file)
  exact     the same pixels, stroke for stroke, as one font's own 12-pixel letter
  close     the nearest character by a clear margin (score at most CLOSE, the next one at least
            MARGIN further away), and no other picture nearer to that character
  review    none of the above: the nearest characters are listed for a look by eye

The comparison: the picture's strokes ('#'; the shadow is ignored) and each candidate, both
blurred a little, are compared pixel by pixel; the score is the mean difference per pixel (0 =
the same). A candidate is drawn large from each outline font and fitted into the picture's own
ink box, since a patch's letters differ in size and place from any font's; the fonts' scores are
mixed by weight (SimSun counts most: a patch's 12-dot letters are drawn in its style). Signs
(punctuation and the like) are also compared where a font's 12-pixel bitmap puts them in the
cell, so a comma and an apostrophe stay apart. Two guards keep the fitting honest: a tiny
picture (a dot, a small comma) is compared only in place, since any letter squeezed into a 2x2
box looks like a dot; and a candidate is fitted only into a box of about its own width to
height, so a dash is not compared with a square letter squashed flat.

Nothing here is kept in the repository but the code: fonts are the player's own, and the
pictures and names stay with the patch files.
"""
from __future__ import annotations

import csv
from collections import Counter
import re
from dataclasses import dataclass, field
from pathlib import Path

from . import cjk, jatext as ja

CELL = 12
TILE = 16                      # each candidate's tile in a strip: the cell plus room for the blur
PAD = 2
BIG = 64                       # outline letters are drawn this large, then fitted down
BLUR = 0.8
CLOSE = 22.0                   # a 'close' name's highest score (mean difference, 0..255)
MARGIN = 4.0                   # ... and how much nearer than the next character it must be
TINY = 4                       # a picture's ink this wide and high at most is a sign: compared in place only
SHAPE_SLACK = 1.3              # how far a candidate's width:height may be from the picture's ink box
SPACE = "\u3000"

FONT_DIR = Path("C:/Windows/Fonts")
# (file, how it is drawn, weight): 'bitmap' fonts have their own 12-pixel letters (used for exact
# matches and to place signs), 'outline' ones are drawn large and fitted. Face 0 of each file.
DEFAULT_FONTS = [("simsun.ttc", "bitmap", 0), ("msgothic.ttc", "bitmap", 0),
                 ("simsun.ttc", "outline", 1.0), ("msyh.ttc", "outline", 0.5)]

PIC = re.compile(r"\{pic ([0-9a-f]{12})\}")
INF = float("inf")


def _gb2312(rows) -> list[str]:
    out = []
    for hi in rows:
        for lo in range(0xA1, 0xFF):
            try:
                out.append(bytes([hi, lo]).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    return out


def gb2312_hanzi() -> list[str]:
    """The 6763 characters of GB2312 levels 1 and 2, in GB2312 order (common ones first)."""
    return _gb2312(range(0xB0, 0xF8))


def gb2312_signs() -> list[str]:
    """GB2312's signs a Chinese text uses (rows 1-3: punctuation, numbered signs, full-width
    digits and letters). Kana, Greek, Russian, pinyin and box drawing (rows 4-9) are left out:
    a Chinese patch has no use for them, and squeezed into a box they look like brackets."""
    return [c for c in _gb2312(range(0xA1, 0xA4)) if c != SPACE]


def strokes(rows: list[str]) -> frozenset:
    """The (x, y) of a picture's stroke pixels."""
    return frozenset((x, y) for y, r in enumerate(rows) for x, c in enumerate(r) if c == "#")


def ink_box(pixels) -> tuple[int, int, int, int] | None:
    if not pixels:
        return None
    xs, ys = [x for x, _ in pixels], [y for _, y in pixels]
    return min(xs), min(ys), max(xs), max(ys)


def shape(pixels) -> tuple:
    """The pixels moved to the top left corner: the same letter anywhere in the cell."""
    box = ink_box(pixels)
    return () if box is None else tuple(sorted((x - box[0], y - box[1]) for x, y in pixels))


@dataclass
class Face:
    path: Path
    kind: str                  # 'bitmap' or 'outline'
    weight: float = 1.0        # an outline font's share of the score
    covers: set = field(default_factory=set)

    @property
    def label(self) -> str:
        return f"{self.path.stem}-{self.kind}"


def default_faces(folder: Path = FONT_DIR) -> list[Face]:
    """The fonts this uses on Windows, those that are there; SimSun first. Given one font file
    instead of a folder, that font alone (its own 12-pixel letters and its outlines)."""
    folder = Path(folder)
    if folder.is_file():
        return [Face(folder, "bitmap", 0), Face(folder, "outline", 1.0)]
    return [Face(folder / name, kind, w) for name, kind, w in DEFAULT_FONTS if (folder / name).exists()]


class Matcher:
    """Ranks candidate characters for letter pictures (see the module notes). `signs` are also
    compared in place (12-pixel bitmaps)."""

    def __init__(self, faces: list[Face], chars: list[str], signs: list[str] = ()):
        from PIL import Image, ImageDraw, ImageFont
        self.Image = Image
        self.outlines = [f for f in faces if f.kind == "outline"]
        self.bitmaps = [f for f in faces if f.kind == "bitmap"]
        if not self.outlines:
            raise ValueError("no outline font to compare with")
        self.chars = list(dict.fromkeys([*chars, *signs]))
        self.signs = set(signs)
        wanted = {ord(c) for c in self.chars}
        self.ink: dict[str, list] = {}       # outline face label -> per char: its ink (L image) or None
        self.cells: list[list] = []          # per bitmap face: per sign its 12x12 cell, or None
        self.exact: dict[tuple, list[str]] = {}
        self.cell_pixels: dict[str, set] = {}   # char -> the first bitmap font's cell pixels
        for face in faces:
            face.covers = cjk._coverage(face.path, wanted)
            if face.kind == "outline":
                font = ImageFont.truetype(str(face.path), BIG, index=0)
                inks = []
                for ch in self.chars:
                    im = None
                    if ord(ch) in face.covers:
                        im = Image.new("L", (BIG * 2, BIG * 2), 0)
                        ImageDraw.Draw(im).text((BIG // 4, BIG // 4), ch, font=font, fill=255)
                        box = im.getbbox()
                        im = im.crop(box) if box else None
                    inks.append(im)
                self.ink[face.label] = inks
                continue
            font = ImageFont.truetype(str(face.path), CELL, index=0)
            cells = []
            for ch in self.chars:
                cell = None
                if ord(ch) in face.covers:
                    cell = Image.new("L", (CELL, CELL), 0)
                    draw = ImageDraw.Draw(cell)
                    draw.fontmode = "1"
                    draw.text((0, 0), ch, font=font, fill=255)
                    px = cell.load()
                    pixels = frozenset((x, y) for y in range(CELL) for x in range(CELL) if px[x, y] > 127)
                    if not pixels:
                        cell = None          # the font draws nothing here (a space, or out of the cell)
                    else:
                        key = shape(pixels)
                        if ch not in self.exact.setdefault(key, []):
                            self.exact[key].append(ch)
                        self.cell_pixels.setdefault(ch, pixels)
                cells.append(cell if ch in self.signs else None)
            self.cells.append(cells)
        self._fitted: dict[tuple, list] = {}
        self._placed = None

    # Strips: every candidate side by side, one TILE each, blurred.
    def _strip(self, images, at):
        from PIL import ImageFilter
        strip = self.Image.new("L", (TILE * len(self.chars), TILE), 0)
        for i, im in enumerate(images):
            if im is not None:
                strip.paste(im, (i * TILE + at[0], at[1]))
        return strip.filter(ImageFilter.GaussianBlur(BLUR))

    @staticmethod
    def fits(ink_size: tuple[int, int], box_size: tuple[int, int]) -> bool:
        """Whether a letter whose ink is `ink_size` (w, h) can be drawn into a box of `box_size`
        pixels: its width to height within SHAPE_SLACK of the box's, allowing for the box being
        rounded to whole pixels (half a pixel each way)."""
        (iw, ih), (bw, bh) = ink_size, box_size
        lo = (bw - 1) / (bh + 1) / SHAPE_SLACK
        hi = (bw + 1) / max(bh - 1, 0.5) * SHAPE_SLACK
        return lo <= iw / ih <= hi

    def _fitted_strips(self, box):
        if box not in self._fitted:
            x0, y0, x1, y1 = box
            size = (x1 - x0 + 1, y1 - y0 + 1)
            strips = []
            for face in self.outlines:
                images = []
                for ink in self.ink[face.label]:
                    if ink is None or not self.fits(ink.size, size):
                        images.append(None)
                        continue
                    im = ink.resize(size, self.Image.BOX)
                    top = im.getextrema()[1] or 1
                    images.append(im.point(lambda v, top=top: min(255, v * 384 // top)))
                strips.append((face.weight, self._strip(images, (PAD + x0, PAD + y0)),
                               [im is None for im in images]))
            self._fitted[box] = strips
        return self._fitted[box]

    def _placed_strips(self):
        if self._placed is None:
            self._placed = [(self._strip(cells, (PAD, PAD)), [c is None for c in cells]) for cells in self.cells]
        return self._placed

    def _tiled(self, pixels, dy: int = 0):
        from PIL import ImageFilter
        tile = self.Image.new("L", (TILE, TILE), 0)
        for x, y in pixels:
            tile.putpixel((PAD + x, PAD + y + dy), 255)
        n = len(self.chars)
        strip = self.Image.new("L", (TILE * n, TILE), 0)
        strip.paste(tile, (0, 0))
        width = TILE
        while width < TILE * n:
            strip.paste(strip.crop((0, 0, width, TILE)), (width, 0))
            width *= 2
        return strip.filter(ImageFilter.GaussianBlur(BLUR))

    @staticmethod
    def _diff(picture, strip):
        from PIL import ImageChops
        return ImageChops.difference(picture, strip).convert("F").reduce(TILE).get_flattened_data()

    def scores(self, rows: list[str]) -> list[float]:
        """Each candidate's score for a picture (lower is nearer; inf where no font has it)."""
        pixels = strokes(rows)
        box = ink_box(pixels)
        n = len(self.chars)
        if box is None:
            return [INF] * n
        picture = self._tiled(pixels)
        total, weight = [0.0] * n, [0.0] * n
        tiny = box[2] - box[0] < TINY and box[3] - box[1] < TINY
        for w, strip, missing in [] if tiny else self._fitted_strips(box):
            for i, v in enumerate(self._diff(picture, strip)):
                if not missing[i]:
                    total[i] += w * v
                    weight[i] += w
        best = [t / w if w else INF for t, w in zip(total, weight)]
        # Signs in place: as the patch put them, and one row lower (where a font's 12-pixel
        # letters sit, see same()).
        pictures = [picture] + ([self._tiled(pixels, 1)] if box[3] < CELL - 1 else [])
        for strip, missing in self._placed_strips():
            for pic in pictures:
                for i, v in enumerate(self._diff(pic, strip)):
                    if not missing[i] and v < best[i]:
                        best[i] = v
        return best

    def rank(self, rows: list[str], top: int = 6) -> list[tuple[str, float]]:
        """The `top` nearest characters, nearest first, with their scores."""
        s = self.scores(rows)
        order = sorted(range(len(s)), key=s.__getitem__)[:top]
        return [(self.chars[i], round(s[i], 2)) for i in order if s[i] != INF]

    def same(self, rows: list[str]) -> list[str]:
        """The characters a bitmap font draws with exactly these strokes. Several (a sign drawn
        like another, only lower or higher) narrow down to those in the same place in the cell,
        or one row lower (a font's 12-pixel letters sit a row lower than a patch's)."""
        pixels = strokes(rows)
        found = list(self.exact.get(shape(pixels), []))
        if len(found) > 1:
            lower = frozenset((x, y + 1) for x, y in pixels)
            placed = [c for c in found if self.cell_pixels.get(c) in (pixels, lower)]
            found = placed or found
        return found


@dataclass
class Name:
    char: str
    how: str
    score: float | None = None
    nearest: list = field(default_factory=list)   # [(char, score)] for 'close' and 'review'
    note: str = ""


# Signs a Chinese patch redraws full width in their Latin cells (centred, or low at the left like
# a Chinese comma): Chinese text takes the full-width sign.
FULL_WIDTH = {"!": "！", "?": "？", ",": "，", ":": "："}


def slot_char(slots: list[str]) -> str | None:
    """The Latin letter, digit or sign of a page 0 cell the patch keeps in place, if any."""
    for slot in slots:
        if slot.startswith("p0:") and int(slot[3:]) in ja.LATIN and ja.LATIN[int(slot[3:])] != " ":
            c = ja.LATIN[int(slot[3:])]
            return FULL_WIDTH.get(c, c)
    return None


def read_reviewed(path: Path | None) -> dict[str, tuple[str, str]]:
    """picture id -> (character, note) from a reviewed file (TSV: picture, char, note)."""
    if path is None or not Path(path).exists():
        return {}
    with open(path, encoding="utf-8", newline="") as f:
        return {r["picture"]: (r["char"].replace("\\u3000", SPACE), r.get("note") or "")
                for r in csv.DictReader(f, delimiter="\t") if r.get("char")}


def name_all(pictures: dict[str, dict], matcher: Matcher, reviewed: dict | None = None,
             progress=None) -> dict[str, Name]:
    """Name every picture ({id: {"rows", "uses", "slots", "japanese"?}})."""
    reviewed = reviewed or {}
    names: dict[str, Name] = {}
    ranked = []
    for n, (pid, p) in enumerate(pictures.items()):
        if progress and n % 100 == 0:
            progress(n, len(pictures))
        rows = p["rows"]
        if pid in reviewed:
            names[pid] = Name(reviewed[pid][0], "read", note=reviewed[pid][1])
        elif not strokes(rows):
            names[pid] = Name(SPACE, "blank")
        elif slot_char(p.get("slots", [])):
            names[pid] = Name(slot_char(p["slots"]), "slot")
        elif p.get("japanese"):
            names[pid] = Name(p["japanese"], "japanese")
        else:
            same = matcher.same(rows)
            near = matcher.rank(rows)
            if len(same) == 1:
                names[pid] = Name(same[0], "exact", 0.0, near)
            else:
                ranked.append((pid, near, same))
    # Nearest by a clear margin, most certain first; a character goes to one picture only.
    taken = {n.char: pid for pid, n in names.items() if n.how != "blank"}
    def margin(item):
        near = item[1]
        return (near[1][1] if len(near) > 1 else 99) - (near[0][1] if near else 99)
    for pid, near, same in sorted(ranked, key=margin, reverse=True):
        if not near:
            names[pid] = Name("", "review", None, near, "no font has a near character")
            continue
        best, score = near[0]
        gap = margin((pid, near, same))
        note = f"also exactly: {''.join(same)}" if same else ""
        if best in taken:
            names[pid] = Name(best, "review", score, near, f"nearest is {best}, already given to {taken[best]}")
        elif score <= CLOSE and gap >= MARGIN:
            names[pid] = Name(best, "close", score, near, note)
            taken[best] = pid
        else:
            names[pid] = Name(best, "review", score, near, note)
    return names


TSV_COLUMNS = ["picture", "char", "how", "score", "nearest", "uses", "note"]


def write_letters(path: Path, pictures: dict, names: dict[str, Name]) -> None:
    with open(path, "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(TSV_COLUMNS)
        for pid, p in sorted(pictures.items(), key=lambda kv: -kv[1].get("uses", 0)):
            n = names[pid]
            w.writerow([pid, n.char.replace(SPACE, "\\u3000"), n.how, "" if n.score is None else n.score,
                        " ".join(f"{c}{s}" for c, s in n.nearest[:5]), p.get("uses", 0), n.note])


def named(names: dict[str, Name]) -> dict[str, str]:
    """picture id -> character, for the pictures that count as named (not 'review')."""
    return {pid: n.char for pid, n in names.items() if n.how != "review"}


def decode(draft: str, chars: dict[str, str]) -> tuple[str, list[str]]:
    """A draft row as plain text, and the picture ids it holds that have no name."""
    missing = []

    def swap(m):
        if m.group(1) in chars:
            return chars[m.group(1)]
        missing.append(m.group(1))
        return m.group(0)
    return PIC.sub(swap, draft), missing


def area_readings(row: dict) -> dict[str, str]:
    """{area: draft} for a row: every area's reading when the import wrote them, else its one."""
    return row.get("areas") or {str(row.get("area")): row["draft"]}


_HANZI = frozenset(gb2312_hanzi())
MID_GAP = re.compile(r"\{pic ([0-9a-f]{12})\}(?=\{pic ([0-9a-f]{12})\})")   # two pictures side by side


def letter_pairs(rows: list[dict], names: dict[str, Name]) -> set[tuple[str, str]]:
    """Every pair of named letters side by side in the rows read in one area only: what the
    translation's real text looks like, to tell a shared page's right reading from the others."""
    chars = named(names)
    pairs = set()
    for row in rows:
        if row.get("areas"):
            continue
        ids = PIC.findall(row["draft"])
        pairs.update((chars[a], chars[b]) for a, b in zip(ids, ids[1:]) if a in chars and b in chars)
    return pairs


def reading_quality(draft: str, names: dict[str, Name], pairs: set | None = None) -> tuple[int, float, float]:
    """(letters that don't belong, share of letter pairs not seen elsewhere, mean score) of one
    reading: lower is better. A reading with the wrong area's font shows the Japanese font's own
    kana, pictures with no name, empty cells in the middle of a sentence, or, once every picture
    has a name, letters in an order the rest of the translation never uses (`pairs`,
    letter_pairs). A Japanese-font letter that is also a Chinese one (士, 上) belongs."""
    bad, scores = 0, []
    ids = PIC.findall(draft)
    for pid in ids:
        n = names.get(pid)
        if n is None or n.how == "review" or (n.how == "japanese" and n.char not in _HANZI):
            bad += 1
        if n is not None and n.score is not None:
            scores.append(n.score)
    for a, b in MID_GAP.findall(draft):
        if names.get(a) is not None and names[a].how != "blank" and names.get(b) is not None \
                and names[b].how == "blank":
            bad += 1
    odd = 0.0
    if pairs is not None and len(ids) > 1:
        chars = named(names)
        seen = [(chars.get(a), chars.get(b)) in pairs for a, b in zip(ids, ids[1:])]
        odd = 1 - sum(seen) / len(seen)
    return bad, odd, (sum(scores) / len(scores) if scores else 0.0)


def choose_area(row: dict, names: dict[str, Name], pairs: set | None = None) -> tuple[str, str]:
    """(area, draft) of the reading that is real Chinese (see reading_quality)."""
    readings = area_readings(row)
    area = min(readings, key=lambda a: reading_quality(readings[a], names, pairs))
    return area, readings[area]


def choose_areas(rows: list[dict], names: dict[str, Name]) -> dict[str, tuple[str | None, str]]:
    """{key: (area, draft)} for every row: a row read in one area as it is; a shared page by
    choose_area with the translation's letter pairs, and where readings tie on letters and
    pairs, the area the other pages of its message bank (dlg/MNNN) mostly read in."""
    pairs = letter_pairs(rows, names)
    first, ties, usual = {}, {}, {}
    for row in rows:
        if not row.get("areas"):
            first[row["key"]] = (None, row["draft"])
            continue
        readings = area_readings(row)
        quality = {a: reading_quality(t, names, pairs)[:2] for a, t in readings.items()}
        best = min(quality.values())
        tied = [a for a, q in quality.items() if q == best]
        first[row["key"]] = (tied[0], readings[tied[0]]) if len(tied) == 1 else choose_area(row, names, pairs)
        if len(tied) > 1:
            ties[row["key"]] = (tied, readings)
        else:
            bank = row["key"].rsplit("/", 1)[0]
            usual.setdefault(bank, Counter())[tied[0]] += 1
    for key, (tied, readings) in ties.items():
        counts = usual.get(key.rsplit("/", 1)[0], Counter())
        area = max(tied, key=lambda a: counts[a])
        first[key] = (area, readings[area])
    return first


def review_sheet(path: Path, pictures: dict, names: dict[str, Name], face: Face, scale: int = 5) -> int:
    """A PNG of every 'review' picture beside its nearest characters; returns how many."""
    from PIL import Image, ImageDraw, ImageFont
    todo = [pid for pid, _ in sorted(pictures.items(), key=lambda kv: -kv[1].get("uses", 0))
            if names[pid].how == "review"]
    if not todo:
        return 0
    cols, cw, ch = 4, 360, CELL * scale + 30
    sheet = Image.new("RGB", (cols * cw, ((len(todo) + cols - 1) // cols) * ch), "white")
    draw = ImageDraw.Draw(sheet)
    big = ImageFont.truetype(str(face.path), 36, index=0)
    small = ImageFont.truetype(str(face.path), 12, index=0)
    for k, pid in enumerate(todo):
        x0, y0 = (k % cols) * cw, (k // cols) * ch
        for y, r in enumerate(pictures[pid]["rows"]):
            for x, c in enumerate(r):
                colour = {"#": (0, 0, 0), "+": (200, 200, 200)}.get(c)
                if colour:
                    draw.rectangle((x0 + 4 + x * scale, y0 + 4 + y * scale,
                                    x0 + 3 + (x + 1) * scale, y0 + 3 + (y + 1) * scale), fill=colour)
        draw.text((x0 + 4, y0 + 8 + CELL * scale), f"{k + 1} {pid}", font=small, fill=(0, 0, 160))
        for j, (c, s) in enumerate(names[pid].nearest[:4]):
            draw.text((x0 + 76 + j * 70, y0 + 6), c, font=big, fill=(0, 0, 0))
            draw.text((x0 + 76 + j * 70, y0 + 46), f"{s:.1f}", font=small, fill=(120, 0, 0))
    sheet.save(path)
    return len(todo)
