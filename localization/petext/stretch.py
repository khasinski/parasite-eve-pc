"""Stretches for the game's text renderer (include/pe1/text_render.h, mods/pe_lang/pe_lang_pack.c).

Every run of letters in a translation becomes one 4-byte token, F1 C6 hi lo, and the run's letters
are kept once in the pack as Unicode. The renderer draws the run whole, with each letter's own
width, so a sentence takes 4 bytes in the game's fixed text fields however long it is, and a letter
needs no one-byte code of its own.

Only the letters change form. The box's commands (line breaks, waits, colours, pauses, numbers,
choices, the inserted name) stay bytes between the runs, so they work as before. So do the button
icons and the extra font pages (FB 00-03, FB nn, FC, FD), which have no Unicode letter.

Text a game screen edits letter by letter keeps one byte per letter: the default player name
(sys/030), the rename grids (sys/115-117) and item names, which the rename screen copies in.
"""
from __future__ import annotations

from . import gametext as gt

TOKEN = b"\xf1\xc6"
EXTRA = b"\xf1\xc7"          # a letter with no code (petext/letters.py): only ever in a stretch
# Narrowing (petext/menu.py narrow): in the tool's bytes, F1 C8 00 k before a menu line draws the
# letters after it at (128 + k) / 256 of their width. In the pack it becomes the stretch's first
# letter, U+10FE80 + k (PE_TEXT_NARROW_FIRST, include/pe1/text_render.h), so it costs no bytes.
NARROW = b"\xf1\xc8\x00"
NARROW_FIRST = 0x10FE80
# A line break (petext/menu.py split): in the tool's bytes F1 C8 01 00 where a message box starts a
# new line; in the pack a stretch holding only U+10FE7F (PE_TEXT_LINE_BREAK), so it costs 4 bytes.
LINE_BREAK = b"\xf1\xc8\x01\x00"
LINE_BREAK_LETTER = 0x10FE7F
MAX_STRETCHES = 240 * 240
MAX_LETTERS = 96            # PE_TEXT_ITEM_LETTERS, PE_LANG_STRETCH_MAX

# Keys whose letters stay bytes (see above), and keys that aren't text at all.
KEEP_BYTES = ("sys/030", "sys/115", "sys/116", "sys/117")
KEEP_PREFIXES = ("item/", "bank/", "mod/")


def stays_bytes(key: str) -> bool:
    return key in KEEP_BYTES or key.startswith(KEEP_PREFIXES)


def letter_map(atlas=None) -> dict[bytes, str]:
    """Each one-letter byte form the encoder writes, to its Unicode letter."""
    # Cyrillic, then Latin Extended, then the game's own letters: a Cyrillic letter that looks like
    # a Latin one shares its cell (Ё and Ë, А and A), and the stretch keeps the cell's own letter,
    # the one the game resolves (kPeTextUnicode)
    from . import cyrillic, latin
    out = {}
    for table in (cyrillic.CODE_OF, latin.CODE_OF):
        out.update({bytes([code]): ch for ch, code in table.items() if code not in gt.CHARS})
    out.update({bytes([code]): ch for code, ch in gt.CHARS.items()})
    if atlas is not None:
        if atlas.letters:
            out.update({raw: ch for ch, raw in atlas.letters.codes.items()})
        out.update(atlas.characters)
    return out


def _letter_at(raw: bytes, i: int, letters: dict[bytes, str]):
    """(letter, bytes it takes) at raw[i], or None for a command or a byte with no Unicode letter."""
    if raw[i] == 0xF1 and i + 4 <= len(raw) and raw[i:i + 4] in letters:
        return letters[raw[i:i + 4]], 4
    one = raw[i:i + 1]
    if raw[i] < 0xF1 and one in letters:
        return letters[one], 1
    return None


def _command_length(raw: bytes, i: int) -> int:
    c = raw[i]
    if c == 0xFB:
        return 3 if i + 1 < len(raw) and raw[i + 1] in (7, 9) else 2
    if c in (0xFC, 0xFD):
        return 2
    if c == 0xF1:
        return 4
    return 1


class Stretches:
    """The pack's stretches, each kept once."""

    def __init__(self) -> None:
        self.runs: list[str] = []
        self._ids: dict[str, int] = {}

    def token(self, run: str) -> bytes:
        if run not in self._ids:
            if len(self.runs) >= MAX_STRETCHES:
                raise ValueError(f"more than {MAX_STRETCHES} different stretches")
            self._ids[run] = len(self.runs)
            self.runs.append(run)
        return TOKEN + bytes(divmod(self._ids[run], 240))

    def convert(self, raw: bytes, letters: dict[bytes, str]) -> bytes:
        """`raw` with each run of two or more letters as a stretch (a lone letter stays a byte:
        a token would be bigger)."""
        out, i, run, run_bytes = bytearray(), 0, [], bytearray()
        mark = ""                    # the narrowing letter every later stretch starts with

        def flush() -> None:
            text = "".join(run)
            room = MAX_LETTERS - len(mark)
            while text:
                piece, text = text[:room], text[room:]
                lone = len(run) == 1 and not run_bytes.startswith(EXTRA) and not mark
                out.extend(run_bytes if lone else self.token(mark + piece))
            run.clear()
            run_bytes.clear()

        while i < len(raw):
            if raw[i:i + 4] == LINE_BREAK:
                flush()
                out.extend(self.token(chr(LINE_BREAK_LETTER)))
                i += 4
                continue
            if raw[i:i + 3] == NARROW and i + 4 <= len(raw):
                flush()
                mark = chr(NARROW_FIRST + raw[i + 3])
                i += 4
                continue
            found = _letter_at(raw, i, letters)
            if found:
                ch, n = found
                run.append(ch)
                run_bytes.extend(raw[i:i + n])
                i += n
                continue
            flush()
            n = _command_length(raw, i)
            out += raw[i:i + n]
            i += n
        flush()
        return bytes(out)

    def section(self) -> tuple[bytes, int]:
        """(index then letters, letter count): count + 1 rising u32 starts, then u32 letters."""
        starts, letters, at = [], [], 0
        for run in self.runs:
            starts.append(at)
            letters += [ord(c) for c in run]
            at += len(run)
        starts.append(at)
        data = b"".join(n.to_bytes(4, "little") for n in starts + letters)
        return data, len(letters)


def convert_entries(entries: dict[str, bytes], atlas=None) -> tuple[dict[str, bytes], Stretches]:
    """Every text entry with its letters as stretches, and the stretches they use."""
    stretches, letters = Stretches(), letter_map(atlas)
    out = {}
    for key in sorted(entries):
        raw = entries[key]
        out[key] = raw if stays_bytes(key) else stretches.convert(raw, letters)
    return out, stretches


def expand(raw: bytes, stretches: Stretches, back: dict[str, bytes]) -> bytes:
    """`raw` with its stretches as the bytes they stand for (back: letter -> byte form)."""
    out, i, mark = bytearray(), 0, None
    while i < len(raw):
        if raw[i:i + 2] == TOKEN and i + 4 <= len(raw) and \
                stretches.runs[raw[i + 2] * 240 + raw[i + 3]] == chr(LINE_BREAK_LETTER):
            out += LINE_BREAK
            i += 4
            continue
        if raw[i:i + 2] == TOKEN and i + 4 <= len(raw):
            for ch in stretches.runs[raw[i + 2] * 240 + raw[i + 3]]:
                if ord(ch) >= NARROW_FIRST:
                    if ch != mark:               # once for the line, as build wrote it
                        out += NARROW + bytes([ord(ch) - NARROW_FIRST])
                        mark = ch
                    continue
                out += back[ch]
            i += 4
            continue
        n = _command_length(raw, i) if raw[i] >= 0xF1 else 1
        out += raw[i:i + n]
        i += n
    return bytes(out)
