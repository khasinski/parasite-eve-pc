"""Arabic letters drawn for this game: every joined shape (alone, end, start, middle) of the Arabic
alphabet, a few Persian and Urdu letters, lam-alef and the tatweel, as 12 x 12 strokes in the style
of petext/letters.py (the shadow is generated). The renderer picks the shapes and closes the gap
between letters that join (petext/rtl.py), so:

  - every letter stands on the joining line, row 7; tails and dots below go down to row 11;
  - a shape that joins the letter on its left (start, middle) has a stroke on row 7 in its leftmost
    column, and one that joins the letter on its right (end, middle) in its rightmost column;
  - dots never reach past a shape's strokes, so the strokes decide where letters touch.

Shapes are bodies shared by letters that differ only in their dots (beh, teh, theh, noon and yeh
start and middle alike). They are a starting point for a translator to redraw, not a typeface.
"""
from __future__ import annotations

CELL = 12
JOIN_ROW = 7

# Bodies: (first row, rows top down from column 1), the column dots centre on, and its forms.
# Keys: "<body>.<form>", form one of a (alone), e (end), s (start), m (middle).
BODIES: dict[str, tuple[int, str, int]] = {
    "alef.a": (1, "#/#/#/#/#/#/#", 1),
    "alef.e": (1, "#./#./#./#./#./#./##", 1),
    "alefup.a": (3, ".#/.#/.#/.#/.#", 2),          # alef under a madda or hamza
    "alefup.e": (3, ".#./.#./.#./.#./.##", 2),
    "alefdown.a": (1, ".#/.#/.#/.#/.#/.#/.#", 2),   # alef over a hamza
    "alefdown.e": (1, ".#./.#./.#./.#./.#./.#./.##", 2),
    "beh.a": (5, "#......#/#......#/.######.", 4),
    "beh.e": (5, "#......./#......./.#######", 4),
    "beh.s": (5, "..#/..#/###", 2),
    "beh.m": (5, "..#../..#../#####", 3),
    "noon.a": (5, "#....#/#....#/#....#/.####.", 3),
    "noon.e": (5, "#....../#....../#....##/.####..", 3),
    "yeh.a": (4, "..##../.#..../..###./#....#/.####.", 3),
    "yeh.e": (4, "..##.../.#...../..###../#....##/.####..", 3),
    "jeem.a": (6, "####./..#../.#.../#..../#...#/.###.", 3),
    "jeem.e": (6, "####./..###/.#.../#..../#...#/.###.", 3),
    "jeem.s": (5, "####/...#/####", 2),
    "jeem.m": (5, ".####/....#/#####", 3),
    "dal.a": (4, "..#./...#/...#/####", 2),
    "dal.e": (4, "..#../...#./...#./#####", 2),
    "reh.a": (6, "...#/...#/..#./##..", 3),
    "reh.e": (6, "...#./...##/..#../##...", 3),
    "seen.a": (5, "....#.#.#/#...#.#.#/#...#####/.###.....", 7),
    "seen.e": (5, "....#.#.#./#...#.#.#./#...######/.###......", 7),
    "seen.s": (5, "#.#.#/#.#.#/#####", 3),
    "seen.m": (5, ".#.#.#./.#.#.#./#######", 4),
    "sad.a": (5, ".....###./#...#...#/#...#####/.###.....", 6),
    "sad.e": (5, ".....###../#...#...#./#...######/.###......", 6),
    "sad.s": (5, "..###/.#..#/#####", 3),
    "sad.m": (5, "...###/..#..#/######", 4),
    "tah.a": (2, "#..../#..../#.##./##..#/#...#/#####", 3),
    "tah.e": (2, "#...../#...../#.##../##..#./#...#./######", 3),
    "tah.s": (2, "#..../#..../#.##./##..#/#...#/#####", 3),
    "tah.m": (2, "#...../#...../#.##../##..#./#...#./######", 3),
    "ain.a": (4, ".##/#../#../.###/#.../#.../.###", 2),
    "ain.e": (5, "#.#./.#../.###/#.../#.../.###", 2),
    "ain.s": (5, ".##/#../####", 2),
    "ain.m": (5, ".##./#..#/####", 2),
    "feh.a": (3, "......#./.....#.#/#.....##/#......#/.#######", 7),
    "feh.e": (3, "......#../.....#.#./#.....##./#......#./.########", 7),
    "feh.s": (4, ".##./#..#/.###/####", 2),
    "feh.m": (4, "..##./.#..#/..###/#####", 3),
    "qaf.a": (3, ".....#./....#.#/#....##/#.....#/#.....#/.#####.", 6),
    "qaf.e": (3, ".....#../....#.#./#....##./#.....#./#.....##/.#####..", 6),
    "kaf.a": (1, ".......#/.......#/.......#/.......#/.##....#/#......#/.#######", 4),
    "kaf.e": (1, ".......#./.......#./.......#./.......#./.##....#./#......#./.########", 4),
    "kaf.s": (1, "...##/..#../.#.../#..../.#.../..#../#####", 3),
    "kaf.m": (1, "....##/...#../..#.../.#..../..#.../...#../######", 4),
    "keheh.a": (1, ".......#/.......#/.......#/.......#/.......#/#......#/.#######", 4),
    "keheh.e": (1, ".......#./.......#./.......#./.......#./.......#./#......#./.########", 4),
    "lam.a": (1, ".....#/.....#/.....#/.....#/#....#/#....#/.####.", 3),
    "lam.e": (1, ".....#./.....#./.....#./.....#./#....#./#....#./.######", 3),
    "lam.s": (1, "..#/..#/..#/..#/..#/..#/###", 2),
    "lam.m": (1, "..#../..#../..#../..#../..#../..#../#####", 3),
    "meem.a": (5, "..##./.#..#/.###./#..../#....", 3),
    "meem.e": (5, "..##../.#..#./.#####/#...../#.....", 3),
    "meem.s": (5, ".##./#..#/####", 2),
    "meem.m": (5, "..##../.#..#./######", 3),
    "heh.a": (4, ".##./#..#/#..#/.##.", 2),
    "heh.e": (4, ".##../#..#./#..#./.####", 2),
    "heh.s": (4, ".##./#..#/#.##/####", 2),
    "heh.m": (4, "..#../.#.#./.###./#####", 3),
    "waw.a": (5, "..##/.#.#/..##/...#/..#./##..", 3),
    "waw.e": (5, "..##./.#.#./..###/...#./..#../##...", 3),
    "hamza.a": (4, "..##/.#../..##/##..", 2),
    "lamalef.a": (1, "#....#/#....#/.#...#/.#...#/..#..#/...#.#/....##", 2),
    "lamalef.e": (1, "#....#./#....#./.#...#./.#...#./..#..#./...#.#./....###", 2),
    "lamalefup.a": (2, "#....#/.#...#/.#...#/..#..#/...#.#/....##", 2),
    "lamalefup.e": (2, "#....#./.#...#./.#...#./..#..#./...#.#./....###", 2),
    "tatweel": (7, "####", 2),
}

# Marks: (rows top down, centred on the body's dot column), drawn at a row above or below.
MARKS = {
    "1": ("#",), "2": ("#.#",), "3": (".#.", "#.#"), "3down": ("#.#", ".#."),
    "hamza": ("##", "#."), "madda": ("###",), "bar": ("###",),
}

# letter: body (without the form), then marks as (mark, row) for alone/end and for start/middle.
# Row None: no mark.
LETTERS: dict[int, tuple[str, tuple, tuple]] = {
    0x0621: ("hamza", (), ()),
    0x0622: ("alefup", (("madda", 1),), ()),
    0x0623: ("alefup", (("hamza", 0),), ()),
    0x0624: ("waw", (("hamza", 2),), ()),
    0x0625: ("alefdown", (("hamza", 9),), ()),
    0x0626: ("yeh", (("hamza", 1),), (("hamza", 2),)),
    0x0627: ("alef", (), ()),
    0x0628: ("beh", (("1", 9),), (("1", 9),)),
    0x0629: ("heh", (("2", 2),), ()),
    0x062A: ("beh", (("2", 3),), (("2", 3),)),
    0x062B: ("beh", (("3", 2),), (("3", 2),)),
    0x062C: ("jeem", (("1", 9),), (("1", 9),)),
    0x062D: ("jeem", (), ()),
    0x062E: ("jeem", (("1", 4),), (("1", 3),)),
    0x062F: ("dal", (), ()),
    0x0630: ("dal", (("1", 2),), ()),
    0x0631: ("reh", (), ()),
    0x0632: ("reh", (("1", 4),), ()),
    0x0633: ("seen", (), ()),
    0x0634: ("seen", (("3", 2),), (("3", 2),)),
    0x0635: ("sad", (), ()),
    0x0636: ("sad", (("1", 3),), (("1", 3),)),
    0x0637: ("tah", (), ()),
    0x0638: ("tah", (("1", 2),), (("1", 2),)),
    0x0639: ("ain", (), ()),
    0x063A: ("ain", (("1", 2),), (("1", 3),)),
    0x0641: ("feh", (("1", 1),), (("1", 2),)),
    0x0642: ("qaf", (("2", 1),), (("2", 2),)),
    0x0643: ("kaf", (), ()),
    0x0644: ("lam", (), ()),
    0x0645: ("meem", (), ()),
    0x0646: ("noon", (("1", 3),), (("1", 3),)),
    0x0647: ("heh", (), ()),
    0x0648: ("waw", (), ()),
    0x0649: ("yeh", (), ()),
    0x064A: ("yeh", (("2", 10),), (("2", 9),)),
    0x067E: ("beh", (("3down", 9),), (("3down", 9),)),
    0x0686: ("jeem", (("3", 8),), (("3down", 9),)),
    0x0698: ("reh", (("3", 3),), ()),
    0x06A9: ("keheh", (), ()),
    0x06AF: ("keheh", (("bar", 0),), (("bar", 0),)),
    0x06CC: ("yeh", (), (("2", 9),)),
}

# Persian and Urdu letters that start and continue as another letter's body.
START_BODY = {0x0642: "feh", 0x06A9: "kaf", 0x06AF: "kaf", 0x06CC: "beh", 0x0626: "beh", 0x064A: "beh", 0x0649: "beh",
              0x0646: "beh"}

FORMS = ("a", "e", "s", "m")


def _bitmap(top: int, pattern: str) -> list[list[int]]:
    grid = [[0] * CELL for _ in range(CELL)]
    for y, line in enumerate(pattern.split("/"), top):
        for x, pixel in enumerate(line, 1):
            if pixel == "#":
                grid[y][x] = 1
    return grid


def _pattern(grid: list[list[int]]) -> tuple[int, str]:
    rows = [y for y in range(CELL) if any(grid[y])]
    right = max(x for y in rows for x in range(CELL) if grid[y][x])
    return rows[0], "/".join("".join("#" if grid[y][x] else "." for x in range(1, right + 1))
                             for y in range(rows[0], rows[-1] + 1))


def shape(letter: int, form: str) -> tuple[int, str]:
    """The strokes of `letter` in `form` (a, e, s, m) as (first row, rows)."""
    body, plain, joined = LETTERS[letter]
    if form in "sm":
        body = START_BODY.get(letter, body)
    marks = plain if form in "ae" else joined
    top, rows, centre = BODIES[f"{body}.{form}"]
    grid = _bitmap(top, rows)
    for name, row in marks:
        for dy, line in enumerate(MARKS[name]):
            left = centre - len(line) // 2
            for dx, pixel in enumerate(line):
                if pixel == "#":
                    grid[row + dy][left + dx] = 1
    return _pattern(grid)


def _lam_alef(alef: int, form: str) -> tuple[int, str]:
    body = "lamalefup" if alef in (0x0622, 0x0623) else "lamalef"
    top, rows, _ = BODIES[f"{body}.{form}"]
    grid = _bitmap(top, rows)
    if alef == 0x0622:
        grid[0][1:4] = [1, 1, 1]
    elif alef == 0x0623:
        grid[0][1:3] = [1, 1]
    elif alef == 0x0625:
        grid[9][1:3] = [1, 1]
        grid[10][1] = 1
    return _pattern(grid)


def _art() -> dict[str, tuple[int, str]]:
    from . import rtl
    out: dict[str, tuple[int, str]] = {}
    for letter, (alone, count) in rtl.FORMS.items():
        for k in range(count):
            out[chr(alone + k)] = shape(letter, FORMS[k])
        out[chr(letter)] = out[chr(alone)]            # the letter itself: its alone shape
    for alef, alone in rtl.LAM_ALEF.items():
        out[chr(alone)] = _lam_alef(alef, "a")
        out[chr(alone + 1)] = _lam_alef(alef, "e")
    top, rows, _ = BODIES["tatweel"]
    out[chr(rtl.TATWEEL)] = (top, rows)
    return out


ART = _art()


def joins(ch: str) -> tuple[bool, bool]:
    """Whether a shape joins the letter on its left and on its right."""
    from . import rtl
    c = ord(ch)
    if c == rtl.TATWEEL:
        return True, True
    for letter, (alone, count) in rtl.FORMS.items():
        if alone <= c < alone + count:
            k = c - alone
            return k in (2, 3), k in (1, 3)
    for alone in rtl.LAM_ALEF.values():
        if c in (alone, alone + 1):
            return False, c == alone + 1
    return False, False


def check(ch: str) -> list[str]:
    """What is wrong with a shape: joining strokes not at its edges on row 7."""
    top, rows = ART[ch]
    grid = _bitmap(top, rows)
    cols = [x for x in range(CELL) if any(grid[y][x] for y in range(CELL))]
    left, right = joins(ch)
    problems = []
    if left and not grid[JOIN_ROW][cols[0]]:
        problems.append("joins on the left but its leftmost column has no stroke on row 7")
    if right and not grid[JOIN_ROW][cols[-1]]:
        problems.append("joins on the right but its rightmost column has no stroke on row 7")
    if len(rows.split("/")) + top > CELL or max(len(r) for r in rows.split("/")) > CELL - 2:
        problems.append("outside the cell")
    return problems
