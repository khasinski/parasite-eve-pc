"""Battle messages: the few lines the boot program holds itself, not the text tables.

The Scan panel's HP line and the enemy's weaknesses, and the two lines for running away, are in
the boot program (SLUS_006.62 on Disc 1, SLUS_006.68 on Disc 2, at the same offsets), each as a
Japanese and an English version side by side; the port has them compiled in
(src/main/text/tables.c) and asks the language pack for "battle/NNN" when it shows one
(Text_LookupBattleMessage, PE_HOOK_TEXT_LOOKUP table 20).

The keys are numbered in the order the lines sit in the boot program. Only offsets are kept here,
never the text: extract_text.py reads the English from the player's own disc.

They draw as one line of the dialogue font at x 20 across the top (or bottom) bar, so a
translation must stay on one line and inside the screen.
"""
from __future__ import annotations

# key: (offset of the English line in the boot program, its slot in bytes, offset of the
# Japanese line or None). Slot sizes are the arrays in tables.c.
LINES = {
    "battle/000": (0x81C64, 16, None),      # Scan: HP not known yet (g_AnalyzeHpUnknownText)
    "battle/001": (0x81C74, 12, None),      # Scan: HP and its maximum (g_AnalyzeHpFormatText)
    "battle/002": (0x81C96, 22, 0x81C80),   # Scan: weak to poison (g_TextSusceptibleToPoison)
    "battle/003": (0x81CC0, 20, 0x81CAC),   # Scan: weak to acid
    "battle/004": (0x81CE8, 20, 0x81CD4),   # Scan: easily put to sleep (g_AnalyzeTranquilizerWeakTexts)
    "battle/005": (0x81D0E, 18, 0x81CFC),   # Scan: weak to heat
    "battle/006": (0x81D32, 18, 0x81D20),   # Scan: weak to cold
    "battle/007": (0x81D5A, 22, 0x81D44),   # Scan: items easily stolen
    "battle/008": (0x81D85, 21, 0x81D70),   # Scan: easily confused
    "battle/009": (0x81DAD, 17, 0x81D9C),   # running away failed (g_EscapeFailedTexts)
    "battle/010": (0x81DCE, 14, 0x81DC0),   # running away isn't allowed here (g_EscapeBlockedTexts)
}
# Everything a patch may rewrite to fit its lines: from the first line to the end of the last.
REGION = (0x81C64, 0x81DDC)
EXES = ("SLUS_006.62", "SLUS_006.68")

# textbox.c draws them from x 20 (Menu_SetTextCursorRect in save_slot.c, the English window);
# the screen is 320 wide.
X = 20
WIDTH = 320 - X


def is_battle(key: str) -> bool:
    return key.startswith("battle/")


def _line(exe: bytes, at: int, size: int) -> bytes:
    end = exe.find(b"\xff", at, at + size)
    if end < 0:
        raise ValueError(f"no line end at boot program offset {at:#x}: not a USA disc")
    return exe[at:end]


def english(exe: bytes) -> dict[str, bytes]:
    """The English lines, without their FF, from an unmodified USA boot program."""
    return {key: _line(exe, at, size) for key, (at, size, _) in LINES.items()}


def _pieces(exe: bytes) -> list[bytes]:
    """The region's lines in order, FF-separated. A line's leading 00 bytes are padding (a patch
    that starts its line later in the slot gives that glyph no width): no line begins with a 0."""
    start, end = REGION
    return [p.lstrip(b"\x00") for p in exe[start:end].split(b"\xff")]


def patched(original: bytes, translated: bytes) -> tuple[dict[str, bytes], str | None]:
    """({key: the patch's line}, None), or ({}, why) when they can't be lined up.

    A patch may move the lines and point the code at them, or keep the Japanese beside its own
    lines, or drop it: so this doesn't follow offsets first. It takes the region's lines in order,
    leaves out any that is still one of the original Japanese ones, and expects one per key, in
    the keys' order (the Spanish and Italian patches keep it).

    A patch may instead write each line over the English one, in its slot, and leave the rest of
    the old line (or of the Japanese line after it) behind its end (the German patch): then each
    key's line is read at its English place. That is taken only when every slot still holds a line
    end and no slot still holds its English or begins a Japanese line."""
    if len(translated) < REGION[1] or len(original) < REGION[1]:
        return {}, "the boot program is too short"
    japanese = {_line(original, ja, at - ja) for at, _, ja in LINES.values() if ja is not None}
    lines =[p for p in _pieces(translated)[:-1] if p and p not in japanese]
    if len(lines) == len(LINES):
        return dict(zip(LINES, lines)), None
    english_lines = english(original)
    try:
        in_place = {key: _line(translated, at, size) for key, (at, size, _) in LINES.items()}
    except ValueError:
        in_place = None
    if in_place and all(line and line not in japanese for line in in_place.values()) and not any(
            in_place[key] == english_lines[key] for key in LINES):
        return in_place, None
    return {}, f"{len(lines)} lines where {len(LINES)} were expected"
