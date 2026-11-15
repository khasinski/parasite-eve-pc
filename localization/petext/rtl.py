"""Right-to-left text: the same joining and ordering the game's renderer does (PeText_Join,
PeText_Order in src/main/draw/text_render.c), so the pack tool measures and previews lines as the
game draws them. tests/text_render_rtl_cases.txt holds cases both sides must pass.

A pack reads right to left when its language code says so (pe_lang_code_rtl in
mods/pe_lang/pe_lang_pack.c). Then every line is drawn whole:
  - Arabic letters take their joined shapes, the Unicode presentation forms (alone, end, start,
    middle), and lam then alef is one letter. Joined letters touch: each is cut to its strokes and
    shadow and moves the pen one pixel past them, except where it joins the letter on its left.
  - Hebrew and Arabic run right to left; English words and numbers keep left to right inside.
  - Vowel marks are left out.
"""
from __future__ import annotations

RTL_LANGUAGES = {"he", "iw", "yi", "ar", "fa", "ur", "ps", "sd", "ug", "ckb", "dv"}


def is_rtl(lang: str) -> bool:
    """Whether a pack with this language code reads right to left (pe_lang_code_rtl)."""
    parts = lang.replace("_", "-").split("-")
    for sub in parts[1:]:
        if len(sub) == 4:
            return sub.lower() in ("arab", "hebr", "thaa")
    return parts[0].lower() in RTL_LANGUAGES


# Arabic letter: (alone form, how many forms: 1 none, 2 alone and end, 4 alone, end, start, middle)
FORMS = {
    0x0621: (0xFE80, 1), 0x0622: (0xFE81, 2), 0x0623: (0xFE83, 2), 0x0624: (0xFE85, 2),
    0x0625: (0xFE87, 2), 0x0626: (0xFE89, 4), 0x0627: (0xFE8D, 2), 0x0628: (0xFE8F, 4),
    0x0629: (0xFE93, 2), 0x062A: (0xFE95, 4), 0x062B: (0xFE99, 4), 0x062C: (0xFE9D, 4),
    0x062D: (0xFEA1, 4), 0x062E: (0xFEA5, 4), 0x062F: (0xFEA9, 2), 0x0630: (0xFEAB, 2),
    0x0631: (0xFEAD, 2), 0x0632: (0xFEAF, 2), 0x0633: (0xFEB1, 4), 0x0634: (0xFEB5, 4),
    0x0635: (0xFEB9, 4), 0x0636: (0xFEBD, 4), 0x0637: (0xFEC1, 4), 0x0638: (0xFEC5, 4),
    0x0639: (0xFEC9, 4), 0x063A: (0xFECD, 4), 0x0641: (0xFED1, 4), 0x0642: (0xFED5, 4),
    0x0643: (0xFED9, 4), 0x0644: (0xFEDD, 4), 0x0645: (0xFEE1, 4), 0x0646: (0xFEE5, 4),
    0x0647: (0xFEE9, 4), 0x0648: (0xFEED, 2), 0x0649: (0xFEEF, 2), 0x064A: (0xFEF1, 4),
    0x067E: (0xFB56, 4), 0x0686: (0xFB7A, 4), 0x0698: (0xFB8A, 2), 0x06A9: (0xFB8E, 4),
    0x06AF: (0xFB92, 4), 0x06CC: (0xFBFC, 4),
}
LAM = 0x0644
LAM_ALEF = {0x0622: 0xFEF5, 0x0623: 0xFEF7, 0x0625: 0xFEF9, 0x0627: 0xFEFB}
TATWEEL = 0x0640


def is_mark(c: int) -> bool:
    """A vowel mark or other combining mark: left out of the line."""
    return (0x0300 <= c <= 0x036F or 0x0591 <= c <= 0x05BD or c in (0x05BF, 0x05C1, 0x05C2, 0x05C4, 0x05C5, 0x05C7)
            or 0x0610 <= c <= 0x061A or 0x064B <= c <= 0x065F or c == 0x0670 or 0x06D6 <= c <= 0x06DC
            or 0x06DF <= c <= 0x06E4 or c in (0x06E7, 0x06E8) or 0x06EA <= c <= 0x06ED)


def is_joining(c: int) -> bool:
    """Drawn joined, so cut to its strokes (PE_LANG_JOINING)."""
    return 0xFB50 <= c <= 0xFDFF or 0xFE70 <= c <= 0xFEFC or c == TATWEEL


def join_type(c: int) -> str:
    if c in (TATWEEL, 0x200D):
        return "C"
    forms = FORMS.get(c)
    if not forms or forms[1] == 1:
        return "U"
    return "D" if forms[1] == 4 else "R"


def forms_of(c: int) -> list[int]:
    """Every shape a letter can take on screen (itself when it has no joined shapes)."""
    alone, n = FORMS.get(c, (c, 1))
    out = [alone + k for k in range(n)]
    if c == LAM:
        out += [f + k for f in LAM_ALEF.values() for k in (0, 1)]
    return out


def shapes_for(texts) -> set[str]:
    """Every joined shape the Arabic letters in `texts` can take on screen."""
    out = set()
    for text in texts:
        for ch in text:
            c = ord(ch)
            if c in FORMS:
                out.update(chr(f) for f in forms_of(c))
            elif c == TATWEEL:
                out.add(ch)
    return out


def is_arabic(c: int) -> bool:
    """A letter that joins or is left out when lines are joined: Arabic letters, their marks."""
    return c in FORMS or c == TATWEEL or is_mark(c) or is_joining(c)


def join(letters: str) -> tuple[str, list[bool]]:
    """(the letters joined, whether each touches the next): PeText_Join."""
    cps = [ord(c) for c in letters]
    keep = [not is_mark(c) for c in cps]
    types = [join_type(c) if k else "U" for c, k in zip(cps, keep)]
    nxt = [False] * len(cps)
    for i, c in enumerate(cps):
        t = types[i]
        if not keep[i]:
            continue
        if t == "U":
            if c in FORMS:                   # hamza: its one shape
                cps[i] = FORMS[c][0]
            continue
        p = i - 1
        while p >= 0 and not keep[p]:
            p -= 1
        q = i + 1
        while q < len(cps) and not keep[q]:
            q += 1
        before = p >= 0 and types[p] in "DC"
        if c == LAM and q < len(cps) and cps[q] in LAM_ALEF:
            cps[i] = LAM_ALEF[cps[q]] + (1 if before else 0)
            keep[q] = False
            types[i] = "R"
            continue
        after = t in "DC" and q < len(cps) and types[q] in "RDC"
        nxt[i] = after
        if t == "C":
            continue
        alone = FORMS[c][0]
        cps[i] = alone + (3 if before and after else 1 if before else 2 if after else 0)
    out = [(chr(c), n) for c, n, k in zip(cps, nxt, keep) if k]
    return "".join(c for c, _ in out), [n for _, n in out]


# ---- ordering (PeText_Order) ----
L, R, AL, EN, ES, ET, AN, CS, NSM, WS, ON = range(11)


def bidi_class(c: int) -> int:
    if not c:
        return ON
    if 0x30 <= c <= 0x39:
        return EN
    if c < 0x80:
        if "a" <= chr(c | 0x20) <= "z":
            return L
        return {"+": ES, "-": ES, "#": ET, "$": ET, "%": ET, ",": CS, ".": CS, ":": CS, "/": CS,
                " ": WS, "\t": WS}.get(chr(c), ON)
    if c == 0xA0:
        return CS
    if 0xA2 <= c <= 0xA5 or c in (0xB0, 0xB1, 0x2030, 0x066A) or 0x20A0 <= c <= 0x20CF:
        return ET
    if c in (0xAA, 0xB5, 0xBA):
        return L
    if c < 0xC0 or c in (0xD7, 0xF7):
        return ON
    if is_mark(c):
        return NSM
    if 0x0660 <= c <= 0x0669 or c in (0x066B, 0x066C):
        return AN
    if 0x06F0 <= c <= 0x06F9:
        return EN
    if c == 0x060C:
        return CS
    if 0x0590 <= c <= 0x05FF or 0xFB1D <= c <= 0xFB4F or c == 0x200F:
        return R
    if (0x0600 <= c <= 0x06FF or 0x0750 <= c <= 0x077F or 0x08A0 <= c <= 0x08FF or 0xFB50 <= c <= 0xFDFF
            or 0xFE70 <= c <= 0xFEFE):
        return AL
    if 0x2000 <= c <= 0x200A:
        return WS
    if c == 0x200E:
        return L
    if c == 0x2212:
        return ES
    if 0x2010 <= c <= 0x2027 or 0x2030 <= c <= 0x205E:
        return ON
    if 0x3000 <= c <= 0x303F:
        return WS if c == 0x3000 else ON
    if 0xFF01 <= c <= 0xFF0F:
        return ON
    return L


MIRROR = {"(": ")", ")": "(", "[": "]", "]": "[", "{": "}", "}": "{", "<": ">", ">": "<",
          "«": "»", "»": "«", "‹": "›", "›": "‹"}
BRACKET_CLOSES = {"(": ")", "[": "]", "{": "}"}   # brackets that pair up (Bidi_Paired_Bracket)


def pair_brackets(letters: str, cls: list[int]) -> None:
    """N0: a pair of brackets takes one direction from what is inside it (PairBrackets)."""
    stack, pairs = [], []
    for i, ch in enumerate(letters):
        if cls[i] != ON:
            continue
        if ch in BRACKET_CLOSES:
            if len(stack) == 63:
                break
            stack.append(i)
            continue
        for k in range(len(stack) - 1, -1, -1):
            if BRACKET_CLOSES[letters[stack[k]]] == ch:
                pairs.append((stack[k], i))
                del stack[k:]
                break
    for a, b in sorted(pairs):
        inside = cls[a + 1:b]
        if any(c in (R, EN, AN) for c in inside):
            d = R
        elif L in inside:
            d = next((L if c == L else R for c in reversed(cls[:a]) if c in (L, R, EN, AN)), R)
        else:
            continue
        cls[a] = cls[b] = d


def order(letters: str) -> tuple[list[int], list[bool]]:
    """(reading-order index of each letter in drawing order, left to right; whether each drawn
    letter is turned round): PeText_Order."""
    n = len(letters)
    cls = [bidi_class(ord(c)) for c in letters]
    for i in range(n):
        if cls[i] == NSM:
            cls[i] = cls[i - 1] if i else R
    last = R
    for i in range(n):
        if cls[i] in (L, R, AL):
            last = cls[i]
        elif cls[i] == EN and last == AL:
            cls[i] = AN
    cls = [R if c == AL else c for c in cls]
    for i in range(1, n - 1):
        if cls[i] == ES and cls[i - 1] == EN and cls[i + 1] == EN:
            cls[i] = EN
        elif cls[i] == CS and cls[i - 1] == cls[i + 1] and cls[i - 1] in (EN, AN):
            cls[i] = cls[i - 1]
    i = 0
    while i < n:
        if cls[i] != ET:
            i += 1
            continue
        j = i
        while j < n and cls[j] == ET:
            j += 1
        if (i and cls[i - 1] == EN) or (j < n and cls[j] == EN):
            cls[i:j] = [EN] * (j - i)
        i = j
    cls = [ON if c in (ES, ET, CS) else c for c in cls]
    last = R
    for i in range(n):
        if cls[i] in (L, R):
            last = cls[i]
        elif cls[i] == EN and last == L:
            cls[i] = L
    pair_brackets(letters, cls)
    strong =lambda c: L if c == L else R
    i = 0
    while i < n:
        if cls[i] not in (ON, WS):
            i += 1
            continue
        j = i
        while j < n and cls[j] in (ON, WS):
            j += 1
        before = strong(cls[i - 1]) if i else R
        after = strong(cls[j]) if j < n else R
        cls[i:j] = [before if before == after else R] * (j - i)
        i = j
    level = [1 if c == R else 2 for c in cls]
    k = n
    while k > 0 and bidi_class(ord(letters[k - 1])) == WS:
        level[k - 1] = 1
        k -= 1
    idx = list(range(n))
    i = 0
    while i < n:
        if level[i] != 2:
            i += 1
            continue
        j = i
        while j < n and level[j] == 2:
            j += 1
        idx[i:j] = idx[i:j][::-1]
        i = j
    idx.reverse()
    turned = [level[k] == 1 and letters[k] in MIRROR for k in idx]
    return idx, turned


def visual(letters: str) -> str:
    """A line as drawn, left to right: joined, ordered, brackets turned round."""
    joined, _ = join(letters)
    idx, turned = order(joined)
    return "".join(MIRROR[joined[k]] if t else joined[k] for k, t in zip(idx, turned))


def width(letters: str, advance, cut_width) -> int:
    """How wide a line of Unicode letters draws: `advance(ch)` for each joined letter, less the gap
    closed where letters join (the left one moves the pen its cut width less the shadow column:
    `cut_width(ch)`)."""
    joined, nxt = join(letters)
    total = 0
    for k, ch in enumerate(joined):
        a = advance(ch)
        if k and nxt[k - 1] and is_joining(ord(ch)) and is_joining(ord(joined[k - 1])):
            a = min(a, cut_width(ch) - 1)
        total += a
    return total
