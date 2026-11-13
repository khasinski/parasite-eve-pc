"""The Translator's work with no window: open a .pelang, read it as rows a person can edit, check
them, and write a .pelang the game loads.

Everything here runs the port's own pack code (pelang.py, petext/), so a file the Translator
writes is the file `pelang.py build` and `bundle` write from the same text. Nothing needs a disc:
the keys, the room each line has and the controls it must keep come from data/manifest.json, which
holds none of the game's words. The English a translator reads comes from an English pack
(`pelang.py english`, made from their own discs) that they open; it is never copied into the
translation.

A translation is one .pelang, a zip:
  text.bin              the compiled pack: what the game reads
  pictures.bin          its pictures, when it has any (kept as they are)
  readme.txt            the translator's note to players (kept as it is)
  translator/work.json  the translator's own text as typed (before the compiler's line breaks),
                        rows that don't pass the checks yet, notes, and which rules the pack is
                        built with; the game leaves it alone

A pack made from a fan patch (the player packs: `import-patch` then `build --from-patch`) keeps
the rules it was made with (Rules): a line's waits may differ from the English, a menu line a
little too wide is drawn narrower, and the patch's own letter pictures and widths are kept. All it
needs for that is in the pack itself: its letter table and CJK dictionary hold those pictures
(letters_in_pack), so the tool writes such a pack back byte for byte without the import's files.
"""
from __future__ import annotations

import contextlib
import csv
import hashlib
import io
import json
import re
import struct
import sys
import tempfile
import threading
import zipfile
from dataclasses import dataclass, field, replace
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
import pelang  # noqa: E402
from petext import alphabets, battle, citymap, cjk, gametext as gt, layout, menu, names as namelib, rtl, stretch  # noqa: E402
from petext import letters as lettertable, manifest as srcman, pictures as picturebin, validation  # noqa: E402

MANIFEST = ROOT / "data" / "manifest.json"
# Every file under localization/ other than code that the Translator reads: the download carries
# these and nothing else (localization/translator/package.py). None of them holds game text.
DATA_FILES = ("data/manifest.json", "data/menu_layouts.json", "data/script_opcodes.json")
DISC_DATA_FILES = ("data/script_opcodes.json",)  # read only once a disc is chosen (the game look's rooms)
WORK = "translator/work.json"
WORK_FORMAT, WORK_VERSION = "pe-translator-work", 3
# 2 added "rules" (a version 1 file is a strict one); 3 a row's "layout": a city map line's own
# spacing, which an older Translator would not keep (so it reads such a file from its pack instead)
WORK_READS = (1, 2, 3)
PATCH_IMPORT = "patch import"                # the notes import-patch gives a fan patch's rows
CJK_LANGS = ("zh", "ja", "ko")               # languages whose letters may need a font file
PICTURES, README = "pictures.bin", "readme.txt"
ZIP_DATE = (1980, 1, 1, 0, 0, 0)            # fixed, so the same work always makes the same file
PAGE_END = 0xF9
MAP_AS_BYTES = ("this file still keeps the city map's place titles as raw bytes (it was made before they "
                "were lines), so the game shows those, not this line; a newer copy of the pack has them as lines")

# pelang.py is one compiler with state of its own (the pack being built) and it reports by
# printing: one build at a time.
_LOCK = threading.RLock()

KINDS = {"dlg": "Dialogue", "sys": "System and menus", "item": "Item names", "desc": "Descriptions",
         "misc": "Help and other lines", "battle": "Battle messages"}


class NotAPack(ValueError):
    """The file isn't a .pelang the pack code reads."""


class WouldChange(RuntimeError):
    """A save would change rows of a pack nobody edited (Translation.locked says which)."""


# ---- the game's rows, without its words ------------------------------------------------------------

class Game:
    """What every translation is checked against: the committed source manifest."""

    def __init__(self, manifest: Path = MANIFEST) -> None:
        self.manifest = Path(manifest)
        self.rows, self.banks, self.metrics = srcman.load(self.manifest)
        self.layouts = menu.load_layouts()
        self.keys = sorted(self.rows)
        self.by_hash: dict[int, str] = {}
        for key in self.keys:
            h = pelang.fnv1a(key)
            if h in self.by_hash:
                raise ValueError(f"{key} and {self.by_hash[h]} have the same hash")
            self.by_hash[h] = key
        self.bank_hashes = {pelang.fnv1a(f"bank/{b['crc32']}"): b["key"] for b in self.banks}

    def group(self, key: str) -> str:
        """The list a row is shown under: its table, or its dialogue bank."""
        parts = key.split("/")
        return f"dlg/{parts[1]}" if parts[0] == "dlg" else parts[0]

    def groups(self) -> list[tuple[str, str, int]]:
        """(group, its name for a person, rows in it), menus first, then the dialogue banks."""
        count: dict[str, int] = {}
        for key in self.keys:
            count[self.group(key)] = count.get(self.group(key), 0) + 1
        order = [g for g in ("sys", "item", "desc", "misc", "battle") if g in count]
        order += sorted(g for g in count if g.startswith("dlg/"))
        return [(g, group_name(g), count[g]) for g in order]

    def must_keep(self, key: str) -> list[str]:
        """The tags a translation of `key` has to keep, in order (waits, choices, the name...):
        the English's own. Line breaks aren't among them: they can move."""
        shape = self.rows[key].get("structure")
        return [] if not shape else [gt.decode(c) for c in shape]

    def writable(self, key: str) -> bool:
        """False for the few rows whose English has controls the pack code can't write."""
        return self.rows[key].get("structure") is not None

    def room(self, key: str) -> str:
        """Where a row is drawn and how much room it has, in words."""
        row = self.rows[key]
        if key.startswith("dlg/"):
            boxes = row.get("boxes") or []
            if boxes:
                return "; ".join(box_words(b) for b in boxes)
            possible = row.get("boxes_possible") or []
            if possible:
                return "one of: " + "; ".join(box_words(b) for b in possible)
            return "box not known"
        if key.startswith("battle/"):
            return f"battle message: one line, {battle.WIDTH} px"
        return pelang.menu_limits(key, self.layouts)


def group_name(group: str) -> str:
    if group.startswith("dlg/"):
        return f"Dialogue {group[4:]}"
    return KINDS.get(group, group)


def box_words(box: str) -> str:
    limit = layout.bounds(box)
    if limit is None:
        return f"{box} (size not known)"
    return f"{'the usual text box' if box == 'default' else box}: {limit[0]} px wide, {limit[1]} lines"


# ---- reading a compiled pack as text ---------------------------------------------------------------

def pack_forms(data: bytes) -> dict[bytes, str]:
    """The bytes of each letter in a compiled pack's text -> its Unicode letter: the fixed codes,
    and over them the pack's own letters (a pack decides what the codes past the English mean)."""
    forms = stretch.letter_map(None)
    fixed = pelang.fixed_letter_forms()
    for ch, raw in pelang.pack_letter_forms(data).items():
        if fixed.get(ch) != raw:             # the pack's own: its table's letters, its dictionary's
            forms[raw] = ch
    return forms


def text_of(raw: bytes, forms: dict[bytes, str]) -> str:
    """One entry's bytes (as pelang.read_pack gives them) as the tagged text a translator types.
    The compiler's own marks for a line drawn narrower are left out: the compiler puts them back
    where they are needed."""
    out, i = [], 0
    while i < len(raw):
        if raw[i:i + 4] == stretch.LINE_BREAK:
            out.append("{br}")
            i += 4
        elif raw[i:i + 3] == stretch.NARROW and i + 4 <= len(raw):
            i += 4
        elif raw[i] == 0xF1 and raw[i:i + 4] in forms:
            out.append(forms[raw[i:i + 4]])
            i += 4
        elif raw[i] < 0xF1 and raw[i:i + 1] in forms:
            out.append(forms[raw[i:i + 1]])
            i += 1
        else:
            n = stretch._command_length(raw, i)
            out.append(gt.decode(raw[i:i + n]))
            i += n
    return "".join(out)


def name_text_of(raw: bytes, numbers: dict[bytes, str]) -> str:
    """A name row (the default name, the name entry grids) of a pack with name letters: each
    letter is a byte of the English set or a two-byte name letter (petext/names.py)."""
    out, i = [], 0
    while i < len(raw):
        if 0xFB <= raw[i] <= 0xFE and raw[i:i + 2] in numbers:
            out.append(numbers[raw[i:i + 2]])
            i += 2
        else:
            out.append(gt.decode(raw[i:i + 1]))
            i += 1
    return "".join(out)


def split_page_end(raw: bytes) -> tuple[bytes, bool]:
    """(a dialogue entry's text, whether it ends in F9): the pack keeps a page's own end after its
    text when the end closes the box by itself."""
    i, last = 0, None
    while i < len(raw):
        last = i
        i += stretch._command_length(raw, i) if raw[i] >= 0xF1 else 1
    if last is not None and raw[last:] == bytes([PAGE_END]):
        return raw[:last], True
    return raw, False


@dataclass
class PackText:
    lang: str
    name: str
    texts: dict[str, str]                    # key -> tagged text
    unknown: int                             # entries whose key the manifest doesn't have
    version: int


def read_pack_text(data_or_path, game: Game) -> PackText:
    """A compiled pack (plain, or a zipped .pelang) as key -> text. A dialogue page whose end isn't
    the game's own gets {end F9} or {end FF}, as `pelang.py build` takes it."""
    try:
        if isinstance(data_or_path, (bytes, bytearray)):
            with tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp) / "pack.bin"
                path.write_bytes(data_or_path)
                data = pelang.pack_bytes(path)
                lang, name, entries = pelang.read_pack(path)
        else:
            data = pelang.pack_bytes(data_or_path)
            lang, name, entries = pelang.read_pack(data_or_path)
    except (OSError, ValueError, struct.error, IndexError, KeyError, zipfile.BadZipFile) as e:
        raise NotAPack(f"not a .pelang language pack: {e}") from e
    version = struct.unpack_from("<I", data, 4)[0]
    forms = pack_forms(data)
    numbers = {namelib.pair(n): ch for ch, n in namelib.read_numbers(data).items()}
    texts, unknown = {}, 0
    for h, raw in entries.items():
        key = game.by_hash.get(h)
        if key is None:
            unknown += h not in game.bank_hashes
            continue
        if version >= 4 and key in namelib.KEYS:
            texts[key] = name_text_of(raw, numbers)
            continue
        end = ""
        if key.startswith("dlg/"):
            raw, closes = split_page_end(raw)
            own = game.rows[key].get("end", "FF") == "F9"
            if closes != own:
                end = "{end F9}" if closes else "{end FF}"
        texts[key] = text_of(raw, forms) + end
    return PackText(lang, name, texts, unknown, version)


def _as_words(texts: dict[str, str]) -> tuple[dict[str, str], dict[str, str]]:
    """(texts with the city map's lines as words, key -> the spaced text of each line that had
    spacing of its own)."""
    words, spaced = {}, {}
    for key, text in texts.items():
        words[key] = citymap.shown_words(key, text)
        if words[key] != text:
            spaced[key] = text
    return words, spaced


def read_english(path, game: Game) -> tuple[dict[str, str], int]:
    """(key -> the game's English, rows that aren't the game's) of an English pack. The second
    number is 0 for a pack `pelang.py english` made from the real discs: every row's text has the
    hash the manifest keeps of the game's English."""
    pack = read_pack_text(path, game)
    wrong = sum(1 for key, text in pack.texts.items()
                if srcman.english_hash(text) != game.rows[key]["english_hash"])
    # The city map's lines as a translator reads and types them: words, without their spacing.
    return {k: citymap.shown_words(k, t) for k, t in pack.texts.items()}, wrong + pack.unknown


# ---- languages -------------------------------------------------------------------------------------

@dataclass(frozen=True)
class LanguageChoice:
    code: str
    name: str
    state: str                               # "ready", "letters missing", "not yet"
    note: str = ""


def languages() -> list[LanguageChoice]:
    """The languages of petext/alphabets.py with what `pelang.py languages` says of each."""
    from petext import letters
    out = []
    for lang in alphabets.LANGUAGES:
        if lang.code in alphabets.NEEDS_LAYOUT:
            out.append(LanguageChoice(lang.code, lang.name, "not yet", "the game can't lay this writing out yet"))
            continue
        gone = alphabets.missing(lang)
        if gone:
            out.append(LanguageChoice(lang.code, lang.name, "not yet", f"no letters yet for: {gone[:40]}"))
            continue
        unseen = "".join(c for c in dict.fromkeys(alphabets.full_alphabet(lang))
                         if c in letters.CATALOG and letters.CATALOG[c].art == letters.PLACEHOLDER)
        if unseen:
            out.append(LanguageChoice(lang.code, lang.name, "letters missing",
                                      f"{len(unseen)} letters show as ? until someone draws them: {unseen}"))
        elif lang.code in alphabets.CJK:
            out.append(LanguageChoice(lang.code, lang.name, "ready", "needs a font file for its characters"))
        else:
            out.append(LanguageChoice(lang.code, lang.name, "ready"))
    return out


# ---- checking and compiling ------------------------------------------------------------------------

@dataclass(frozen=True)
class Message:
    level: str                               # "error" or "warning"
    key: str | None                          # None: about the whole pack
    text: str


@dataclass
class Built:
    ok: bool
    messages: list[Message]
    pack: bytes | None                       # the compiled pack (text.bin) when ok
    said: str                                # everything the compiler printed
    extra: object = None                     # what build's `then` returned

    def errors(self, key: str | None = None) -> list[str]:
        return [m.text for m in self.messages if m.level == "error" and (key is None or m.key == key)]

    def warnings(self, key: str | None = None) -> list[str]:
        return [m.text for m in self.messages if m.level == "warning" and (key is None or m.key == key)]

    def keys_with(self, level: str) -> set[str]:
        return {m.key for m in self.messages if m.level == level and m.key}


_KEY = r"(?:sys|item|desc|misc|battle)/\d+|dlg/M\d+/\d+"
_ROW = re.compile(rf"(?:consistency: )?({_KEY}): (.*)", re.S)
_JOINED = re.compile(r"(joined text[^:]*): (.*)", re.S)


def messages_of(said: str, translated=()) -> list[Message]:
    """The compiler's printed warnings and errors, each with the row it is about. A message about
    text the game joins (an item's name and a suffix) goes to each row it names that is
    translated."""
    out = []
    for line in said.splitlines():
        level, _, rest = line.partition(": ")
        if level not in ("error", "warning"):
            continue
        m = _ROW.match(rest)
        if m:
            out.append(Message(level, m[1], m[2]))
            continue
        j = _JOINED.match(rest)
        if j:
            named = re.findall(_KEY, j[2].split(": ")[0]) or re.findall(_KEY, j[2])
            mine = [k for k in dict.fromkeys(named) if k in translated] or list(dict.fromkeys(named))
            out += [Message(level, k, f"{j[1]}: {j[2]}") for k in mine] or [Message(level, None, rest)]
            continue
        out.append(Message(level, None, in_plain_words(rest)))
    return out


# The compiler's words for a missing font speak of its command line; a translator has a button.
_FONT_WORDS = (
    (re.compile(r"CJK font: \d+ CJK character\(s\) have no letter picture and no --cjk-font was given: (.+)"),
     "this pack has no picture of {0} and no font file is chosen: pick one that has them with "
     "“Font…” at the top"),
    (re.compile(r"CJK font: CJK text needs --cjk-font.*"),
     "these characters need a font file: pick one with “Font…” at the top"),
    (re.compile(r"CJK font: no supplied font contains '(.+?)' U\+[0-9A-F]+"),
     "the chosen font file has no {0}: pick another with “Font…” at the top"),
)


def in_plain_words(text: str) -> str:
    for pattern, words in _FONT_WORDS:
        m = pattern.fullmatch(text.strip())
        if m:
            return words.format(*m.groups())
    return text


@dataclass(frozen=True)
class Rules:
    """How a pack is built, beyond its rows: what `pelang.py build` is told besides the text.
    `letters` and `cjk` are the pack's own letter pictures (letters_in_pack), kept as they are."""
    from_patch: bool = False                 # a fan patch's looser rules (build --from-patch)
    letters: tuple = ()                      # (letter, 12 picture rows, dialogue advance, menu metric)
    cjk: tuple = ()                          # (character, 12 picture rows): the CJK dictionary's
    fonts: tuple = ()                        # font files for CJK characters no picture has
    patch_rows: frozenset = frozenset()      # rows still as the fan patch had them


def _picture_rows(body: bytes) -> tuple[str, ...]:
    """A record's 72 bytes (two pixels a byte, low half first) as 12 rows of ' ', '#', '+'."""
    values = []
    for b in body:
        values += [b & 15, b >> 4]
    return tuple("".join(" #+"[v] for v in values[y * 12:(y + 1) * 12]) for y in range(12))


def letters_in_pack(data: bytes) -> tuple[tuple, tuple]:
    """(letters, cjk) for Rules: the letter pictures a compiled pack carries that the compiler
    wouldn't draw by itself. `letters`: records of its letter table that are pictures of the pack's
    own (a fan patch's look: `build --patch-letters`), not the catalog's letter for that code.
    `cjk`: every picture of its CJK dictionary (a patch's own, or drawn from a font when it was
    built), so the pack builds again with no font file."""
    version = struct.unpack_from("<I", data, 4)[0]
    letters, pictures = [], []
    if version > 2:
        at, count = struct.unpack_from("<II", data, 80)
        for k in range(count):
            r = data[at + k * lettertable.RECORD:at + (k + 1) * lettertable.RECORD]
            ch, code, advance, base, ops = chr(struct.unpack_from("<I", r)[0]), r[4], r[5], r[6], r[7]
            if base != lettertable.NO_BASE:
                continue                     # a catalog letter: a game letter with marks
            known = lettertable.CATALOG.get(ch)
            if known is not None and known.record(code) == r:
                continue                     # the catalog's own picture of it
            letters.append((ch, _picture_rows(r[8:]), advance, ops))
    if version > 1:
        at, count = struct.unpack_from("<II", data, 64)
        for k in range(count):
            r = data[at + k * cjk.RECORD:at + (k + 1) * cjk.RECORD]
            pictures.append((chr(struct.unpack_from("<I", r)[0]), _picture_rows(r[4:])))
    return tuple(letters), tuple(pictures)


def build(game: Game, texts: dict[str, str], lang: str, name: str, *, keep_names_from: bytes | None = None,
          rules: Rules = Rules(), allow_overflow: bool = False, then=None) -> Built:
    """Run `pelang.py build` on `texts` (key -> translation; empty ones are left out) against the
    manifest, with `rules`. `then(pack bytes)` runs right after a build that wrote a pack, while
    the compiler still holds that pack's letters, and its result is kept as Built.extra."""
    rows = {k: t for k, t in texts.items() if t}
    with _LOCK, tempfile.TemporaryDirectory() as tmp:
        sheet, out = Path(tmp) / "translation.csv", Path(tmp) / "text.bin"
        with open(sheet, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=pelang.SUBMISSION_COLUMNS)
            w.writeheader()
            for key in sorted(rows):
                w.writerow({"key": key, "translation": rows[key],
                            "notes": PATCH_IMPORT if key in rules.patch_rows else ""})
        args = ["build", str(sheet), "--strings", str(game.manifest), "--lang", lang, "--name", name,
                "-o", str(out), "--max-messages", "1000000"]
        if keep_names_from:
            old = Path(tmp) / "previous.bin"
            old.write_bytes(keep_names_from)
            args += ["--keep-names-from", str(old)]
        if rules.from_patch:
            args.append("--from-patch")
        if rules.letters:
            own = Path(tmp) / "letters.json"
            own.write_text(json.dumps({"letters": [{"letter": ch, "picture": list(pic), "dialogue": adv,
                                                    "menu": metric} for ch, pic, adv, metric in rules.letters]},
                                      ensure_ascii=False), encoding="utf-8")
            args += ["--patch-letters", str(own)]
        if rules.cjk:
            names, pictures = Path(tmp) / "cjk-letters.tsv", Path(tmp) / "cjk-pictures.json"
            with open(names, "w", newline="", encoding="utf-8") as f:
                w = csv.writer(f, delimiter="	")
                w.writerow(["picture", "char", "how"])
                w.writerows([f"p{k}", ch, "pack"] for k, (ch, _) in enumerate(rules.cjk))
            pictures.write_text(json.dumps({"pictures": {f"p{k}": {"rows": list(pic)}
                                                         for k, (_, pic) in enumerate(rules.cjk)}}),
                                encoding="utf-8")
            args += ["--cjk-letters", str(names), "--cjk-pictures", str(pictures)]
        for font in rules.fonts:
            args += ["--cjk-font", str(font)]
        if allow_overflow:
            args.append("--allow-overflow")
        said = io.StringIO()
        try:
            with contextlib.redirect_stdout(said), contextlib.redirect_stderr(said):
                code = pelang.main(args)
        except SystemExit as e:              # the compiler's own refusal (a language code too long...)
            print(f"error: {e}", file=said)
            code = 1
        pack = out.read_bytes() if not code and out.is_file() else None
        built = Built(pack is not None, messages_of(said.getvalue(), rows), pack, said.getvalue())
        if not built.ok and not built.errors():
            built.messages.append(Message("error", None, said.getvalue().strip()[-400:] or "the build failed"))
        built.extra = then(pack) if then and pack is not None else None
        return built


# ---- how a line will look ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Piece:
    text: str                                # the letter, or what stands for a control
    x: int                                   # where the pen is, from the line's start
    width: int
    red: bool = False
    kind: str = "letter"                     # letter, name, number, symbol, other


@dataclass
class Screen:
    lines: list[list[Piece]]
    widths: list[int]
    choice_line: int | None = None           # the line a choice starts on: the lines after are its options


@dataclass
class Box:
    name: str                                # "default", or "styled x,y,w,h"
    x: int
    y: int
    width: int | None                        # px a line may take, None when not known
    lines: int | None
    edge: int | None                         # px before the screen edge
    known: bool                              # False: no script ties the row to this box


@dataclass
class Preview:
    dialogue: bool
    screens: list[Screen] = field(default_factory=list)
    boxes: list[Box] = field(default_factory=list)
    name_px: int = layout.NAME_WIDTH
    menu_px: int | None = None               # a menu row: its width as the menus draw it
    final: str = ""                          # the text as the pack holds it (with the compiler's breaks)
    wrapped: bool = False
    right_to_left: bool = False
    key: str = ""
    pack: bytes = b""                        # the pack it was built in: the game look draws from it
    shown_name: str = "Aya"


def boxes_of(game: Game, key: str) -> list[Box]:
    row = game.rows[key]
    known = bool(row.get("boxes"))
    out = []
    for box in row.get("boxes") or row.get("boxes_possible") or []:
        limit, edge = layout.bounds(box), layout.bounds(box, screen=True)
        m = re.fullmatch(r"(?:styled )?(-?\d+),(-?\d+),(\d+),(\d+)", box)
        x, y = (20, 174) if box == "default" else (int(m[1]), int(m[2]) + 6) if m else (20, 174)
        out.append(Box(box, x, y, limit and limit[0], limit and limit[1], edge and edge[0], known))
    return out


def _screens(raw: bytes, forms: dict[bytes, str], name: str, name_px: int) -> list[Screen]:
    screens, lines, line, widths = [], [], [], []
    x, red, choice = 0, False, None

    def end_line():
        nonlocal line, x
        lines.append(line)
        widths.append(x)
        line, x = [], 0

    def end_screen():
        nonlocal lines, widths, choice
        screens.append(Screen(lines, widths, choice))
        lines, widths, choice = [], [], None

    for c in validation.commands(raw):
        op = c[0]
        if op == 0xF7:
            end_line()
            continue
        if op == 0xF8:
            end_line()
            end_screen()
            continue
        w = layout.advance(c, name_px)
        if op == 0xFA:
            line.append(Piece(name, x, w, red, "name"))
        elif op == 0xFB:
            if c[1] == 4:
                red = True
            elif c[1] == 5:
                red = False
            elif c[1] == 8:
                line.append(Piece("00000", x, w, red, "number"))
            elif c[1] <= 3:
                line.append(Piece("□", x, w, red, "symbol"))
            elif c[1] == 9:
                choice = len(lines)
        elif c in forms:
            line.append(Piece(forms[c], x, w, red))
        elif w:
            line.append(Piece("□", x, w, red, "other"))
        x += w
    end_line()
    end_screen()
    return screens


@dataclass
class RowCheck:
    errors: list[str]
    warnings: list[str]
    preview: Preview | None                  # None when the text can't be compiled at all


def check_row(game: Game, key: str, text: str, lang: str, name: str = "Preview", *, default_name: str = "",
              rules: Rules = Rules(), context: dict[str, str] | None = None) -> RowCheck:
    """One row checked by the compiler, and how the game will draw it. `default_name` is the
    translation's sys/030 (what {name} shows in a new game); it sets how wide {name} is. `context`:
    other rows the game shows beside this one (key -> text, menulook.context_keys), put in the
    preview's pack only; what is said about the row is the same with or without them."""
    if not text:
        return RowCheck([], [], None)
    rows = {key: text}
    if default_name and key != namelib.DEFAULT_NAME:
        rows[namelib.DEFAULT_NAME] = default_name
    shown_name = rows.get(namelib.DEFAULT_NAME) or "Aya"

    def look(pack: bytes) -> Preview:
        forms = pack_forms(pack)
        data = read_pack_text(pack, game)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "pack.bin"
            path.write_bytes(pack)
            raw = pelang.read_pack(path)[2].get(pelang.fnv1a(key), b"")
        final = data.texts.get(key, "")
        back = {ch: b for b, ch in forms.items()}
        try:
            name_px = layout.name_width(b"".join(back[ch] for ch in shown_name))
        except KeyError:
            name_px = layout.NAME_WIDTH
        if not key.startswith(("dlg/", "battle/")):
            px = menu.width(raw, game.metrics) if game.metrics is not None and key not in namelib.KEYS else None
            return Preview(False, menu_px=px, final=final, name_px=name_px, key=key, pack=pack,
                           shown_name=shown_name)
        if key.startswith("dlg/"):
            raw = split_page_end(raw)[0]
        screens = _screens(raw, forms, shown_name, name_px)
        boxes = boxes_of(game, key) if key.startswith("dlg/") else [
            Box("battle", battle.X, 20, battle.WIDTH, 1, battle.WIDTH, True)]
        typed = pelang.split_page_end(text)[0]
        # (a map line's spacing is the compiler's own placing, not a change to the words)
        as_built = citymap.shown_words(key, final.replace("{end F9}", "").replace("{end FF}", ""))
        return Preview(True, screens, boxes, name_px, final=final,
                       wrapped=as_built != citymap.shown_words(key, typed),
                       right_to_left=rtl.is_rtl(lang), key=key, pack=pack, shown_name=shown_name)

    with _LOCK:
        strict = build(game, rows, lang, name, rules=rules, then=look)
        if not strict.ok and len(rows) > 1 and not strict.errors(key):
            rows = {key: text}               # the default name is what's wrong: check this row without it
            shown_name = "Aya"
            strict = build(game, rows, lang, name, rules=rules, then=look)
        preview = strict.extra
        if not strict.ok:
            # Too long for its room: still worth seeing. Controls that are wrong can't be drawn.
            loose = build(game, rows, lang, name, rules=rules, allow_overflow=True, then=look)
            preview = loose.extra
        extra = {k: t for k, t in (context or {}).items() if t and k not in rows}
        if extra and preview is not None:
            beside = build(game, {**rows, **extra}, lang, name, rules=rules, allow_overflow=True, then=look)
            if beside.extra is not None:
                preview = beside.extra
    whole = [m.text for m in strict.messages if m.key is None]
    return RowCheck(strict.errors(key) + (whole if not strict.ok else []), strict.warnings(key), preview)


# ---- a translation: one .pelang --------------------------------------------------------------------

@dataclass
class SaveReport:
    path: Path
    in_pack: int                             # rows the game will show translated
    left_out: dict[str, list[str]]           # rows that don't pass yet -> why; the game shows its own text
    warnings: dict[str, list[str]]
    whole: list[str]                         # problems about the whole pack


class Translation:
    """A language being translated: its rows as typed, and the .pelang it is kept in."""

    def __init__(self, game: Game, lang: str, name: str) -> None:
        self.game = game
        self.lang, self.name = lang, name
        self.texts: dict[str, str] = {}      # key -> the translator's text ("" or missing: not translated)
        self.notes: dict[str, str] = {}
        self.parts: dict[str, bytes] = {}    # the file's other parts, kept as they are
        self.pack: bytes | None = None       # the compiled pack of the file it was opened from
        self.path: Path | None = None
        self.cjk_fonts: list[str] = []       # font files for CJK characters the pack has no picture of
        self.from_work = False               # opened from its own work part (else read back from the pack)
        self.unknown = 0
        # The rules it is built with (Rules): a fan patch's looser ones for a pack made from one, the
        # pack's own letter pictures, and the rows still as the patch had them (key -> that text).
        self.from_patch = False
        self.own_letters: tuple = ()
        self.own_cjk: tuple = ()
        self.patch_text: dict[str, str] = {}
        # The city map's lines are shown and typed as words (citymap.shown_words); a line the file
        # holds with its own spacing (a fan patch's, or one saved before) keeps it while its words
        # stay the same: key -> that text.
        self.layouts: dict[str, str] = {}
        # A pack read back from its compiled text (made by other means: a fan patch import, the
        # English pack): the rows a save would not write back as the file has them -> why. While
        # there are any, save refuses, so opening and saving can never quietly change a pack.
        self.locked: dict[str, str] = {}

    # -- opening --

    @classmethod
    def open(cls, path, game: Game) -> "Translation":
        path = Path(path)
        try:
            data = path.read_bytes()
        except OSError as e:
            raise NotAPack(f"can't read {path.name}: {e}") from e
        parts, work = {}, None
        if data.startswith(b"PK\x03\x04"):
            try:
                with zipfile.ZipFile(io.BytesIO(data)) as z:
                    for info in z.infolist():
                        if info.is_dir() or info.filename == pelang.BUNDLE_TEXT:
                            continue
                        if info.filename == WORK:
                            work = json.loads(z.read(info).decode("utf-8"))
                        else:
                            parts[info.filename] = z.read(info)
            except (zipfile.BadZipFile, ValueError, UnicodeDecodeError) as e:
                raise NotAPack(f"{path.name} is damaged: {e}") from e
        read = read_pack_text(path, game)
        self = cls(game, read.lang, read.name)
        self.path, self.parts, self.unknown = path, parts, read.unknown
        self.pack = pelang.pack_bytes(path)
        self.own_letters, self.own_cjk = letters_in_pack(self.pack)
        words, self.layouts = _as_words(read.texts)
        self.texts = dict(words)
        patch_rows: list[str] = []
        if isinstance(work, dict) and work.get("format") == WORK_FORMAT and work.get("version") in WORK_READS:
            rows = work.get("rows") or {}
            self.notes = {k: r["note"] for k, r in rows.items() if k in game.rows and r.get("note")}
            typed, kept = _as_words({k: r["text"] for k, r in rows.items() if k in game.rows and r.get("text")})
            kept.update({k: r["layout"] for k, r in rows.items() if k in game.rows and r.get("layout")})
            rules = work.get("rules") or {}
            self.from_patch = rules.get("from_patch") is True
            patch_rows = [k for k in rules.get("patch_rows") or [] if k in game.rows]
            if work.get("text_sha256") == hashlib.sha256(self.pack).hexdigest():
                self.texts, self.layouts, self.from_work = typed, kept, True
            else:
                # The pack was rebuilt by something else since: its text is what the game shows, so
                # it wins; rows only the work part has (they didn't pass the checks) are kept.
                self.texts.update({k: t for k, t in typed.items() if k not in self.texts})
        self.patch_text = {k: self.texts[k] for k in patch_rows if self.texts.get(k)}
        if not self.from_work:
            self.locked = self.untouched_changes()
            if self.locked and not self.from_patch:
                # Made by other means, most likely from a fan patch with its looser rules: if those
                # write every row back as the pack has it, the pack keeps them (and every line edited
                # or added later follows them too).
                self.from_patch, self.patch_text = True, dict(words)
                looser = self.untouched_changes()
                if len(looser) < len(self.locked) or not looser:
                    self.locked = looser
                else:
                    self.from_patch, self.patch_text = False, {}
        return self

    def untouched_changes(self) -> dict[str, str]:
        """key -> why, for each row the compiler would not write back as the opened pack has it
        ("" as the key: the pack as a whole). Empty: saving the file untouched gives the same rows."""
        if self.pack is None:
            return {}
        built, left, _ = self.compile()
        out = {key: "would go back to the game's text: " + (why[0] if why else "it doesn't pass the checks")
               for key, why in left.items()}
        if not built.ok:
            out[""] = "; ".join(m.text for m in built.messages if m.level == "error" and m.key is None)[:400]
            return out
        was, now = pelang_entries(self.pack), pelang_entries(built.pack)
        for h, raw in was.items():
            key = self.game.by_hash.get(h)
            if key and key not in out and now.get(h) != raw:
                out[key] = "would be written differently (its line breaks or letters)"
        return out

    # -- rows --

    def text(self, key: str) -> str:
        return self.texts.get(key, "")

    def set_text(self, key: str, text: str) -> None:
        if key not in self.game.rows:
            raise KeyError(key)
        if text:
            self.texts[key] = text
        else:
            self.texts.pop(key, None)

    def translated(self) -> int:
        return sum(1 for t in self.texts.values() if t)

    @property
    def default_name(self) -> str:
        return self.texts.get(namelib.DEFAULT_NAME, "")

    @property
    def rules(self) -> Rules:
        """What the pack is built with: see Rules. A row stays the patch's only while its text is."""
        return Rules(self.from_patch, self.own_letters, self.own_cjk, tuple(self.cjk_fonts),
                     frozenset(k for k, t in self.patch_text.items() if self.texts.get(k) == t))

    def rules_for(self, key: str, text: str) -> Rules:
        """The rules for checking `text` as the row `key` (it may not be the row's text yet)."""
        rules = self.rules
        rows = rules.patch_rows - {key}
        if self.patch_text.get(key) == text:
            rows |= {key}
        return replace(rules, patch_rows=frozenset(rows))

    def needs_font(self) -> bool:
        """Whether the language's letters may need a font file (CJK_LANGS)."""
        return self.lang.split("-")[0].lower() in CJK_LANGS

    # -- checking --

    def as_built(self, key: str, text: str) -> str:
        """What the compiler is given for a row: a map line whose words are still those of its own
        spacing (self.layouts) keeps that spacing; any other text as it is (the compiler places a
        map line typed as words)."""
        kept = self.layouts.get(key)
        return kept if kept is not None and citymap.shown_words(key, kept) == text else text

    def _kept_layout(self, key: str) -> str:
        """The spacing a map line keeps (see as_built), "" when it has none."""
        text = self.texts.get(key, "")
        return self.layouts[key] if text and key in self.layouts and self.as_built(key, text) != text else ""

    def _built_texts(self, texts: dict[str, str]) -> dict[str, str]:
        return {k: self.as_built(k, t) for k, t in texts.items()}

    def check(self) -> Built:
        """Every translated row through the compiler, as a save would."""
        return build(self.game, self._built_texts(self.texts), self.lang, self.name, keep_names_from=self.pack,
                     rules=self.rules)

    def check_row(self, key: str, text: str | None = None) -> RowCheck:
        text = self.text(key) if text is None else text
        checked = check_row(self.game, key, self.as_built(key, text), self.lang, self.name,
                            default_name=self.default_name, rules=self.rules_for(key, text))
        if text and citymap.placed(key) and self.map_as_bytes:
            checked.warnings.append(MAP_AS_BYTES)
        return checked

    @property
    def map_as_bytes(self) -> bool:
        """Whether the file's pictures.bin still carries the city map's entry (its text as bytes, as
        packs made before the dlg/MAP lines did): the game then shows those bytes, not the lines."""
        try:
            pieces = picturebin.read(self.parts[PICTURES]) if PICTURES in self.parts else []
        except ValueError:
            return False
        return any(citymap.USA_SECTORS[0] <= p.sector < citymap.USA_SECTORS[1] for p in pieces)

    # -- saving --

    def compile(self) -> tuple[Built, dict[str, list[str]], Built]:
        """(the build of every row that passes, the rows left out -> why, the first build with
        every row). A row with an error is left out, so the game shows its own text there, and the
        rest is built again, until what is left builds."""
        first = built = self.check()
        left: dict[str, list[str]] = {}
        texts = {k: t for k, t in self.texts.items() if t}
        for _ in range(8):
            if built.ok:
                break
            bad = built.keys_with("error") & texts.keys()
            if not bad:
                break
            for key in bad:
                left[key] = built.errors(key)
                del texts[key]
            built = build(self.game, self._built_texts(texts), self.lang, self.name, keep_names_from=self.pack,
                          rules=self.rules)
        return built, left, first

    def save(self, path=None) -> SaveReport:
        """Write the .pelang: the pack of every row that passes the checks, the file's other parts
        as they were, and the work part with every row as typed. Nothing typed is ever lost: when
        no pack can be built at all, the file holds an empty pack and the work. Refused
        (WouldChange) for a pack the compiler wouldn't write back as it is (self.locked)."""
        if self.locked:
            raise WouldChange(f"saving would change {len(self.locked)} line(s) nobody edited")
        path = Path(path or self.path)
        built, left, first = self.compile()
        whole = [m.text for m in built.messages if m.level == "error" and m.key is None] if not built.ok else []
        if not built.ok:
            for key, text in self.texts.items():
                if text and key not in left:
                    left[key] = ["the pack could not be built: " + "; ".join(whole)[:300]]
            built = build(self.game, {}, self.lang, self.name)
            if not built.ok:
                raise RuntimeError("the pack code could not write even an empty pack:\n" + built.said[-800:])
        pack = built.pack
        held = pelang_entries(pack)
        missing = [k for k, t in self.texts.items() if t and k not in left and pelang.fnv1a(k) not in held]
        if missing:
            raise RuntimeError(f"the built pack is missing rows it should hold: {missing[:10]}")
        work = {"format": WORK_FORMAT, "version": WORK_VERSION, "lang": self.lang, "name": self.name,
                "text_sha256": hashlib.sha256(pack).hexdigest(),
                "rows": {k: {f: v for f, v in (("text", self.texts.get(k, "")), ("note", self.notes.get(k, "")),
                                                ("layout", self._kept_layout(k))) if v}
                         for k in sorted(set(self.texts) | set(self.notes))
                         if self.texts.get(k) or self.notes.get(k)}}
        if self.from_patch:                  # font files stay on this computer: not in the file
            work["rules"] = {"from_patch": True, "patch_rows": sorted(self.rules.patch_rows)}
        blob = io.BytesIO()
        with zipfile.ZipFile(blob, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr(zipfile.ZipInfo(pelang.BUNDLE_TEXT, ZIP_DATE), pack, zipfile.ZIP_DEFLATED)
            for part in sorted(self.parts):
                z.writestr(zipfile.ZipInfo(part, ZIP_DATE), self.parts[part], zipfile.ZIP_DEFLATED)
            z.writestr(zipfile.ZipInfo(WORK, ZIP_DATE),
                       json.dumps(work, ensure_ascii=False, indent=1, sort_keys=True).encode("utf-8"),
                       zipfile.ZIP_DEFLATED)
        data = blob.getvalue()
        if len(pack) > pelang.PACK_MAX or len(data) > pelang.BUNDLE_MAX:
            raise RuntimeError("the file would be larger than the game opens")
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_name(path.name + ".saving")
        tmp.write_bytes(data)
        if path.is_file():                   # the version before this save, in case this one is regretted
            backup = path.with_name(path.name + ".before-last-save")
            backup.write_bytes(path.read_bytes())
        tmp.replace(path)
        self.path, self.pack, self.from_work = path, pack, True
        warnings: dict[str, list[str]] = {}
        for m in built.messages:
            if m.level == "warning" and m.key:
                warnings.setdefault(m.key, []).append(m.text)
        return SaveReport(path, sum(1 for h in held if h in self.game.by_hash), left, warnings, whole)


def pelang_entries(pack: bytes) -> dict[int, bytes]:
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "pack.bin"
        path.write_bytes(pack)
        return pelang.read_pack(path)[2]
