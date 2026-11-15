"""The Translator's "As in the game" preview for the battle lines (battle/000-010: the Scan panel's
lines and the two for running away): the line in the dialogue font on the battle bar, drawn as the
game draws it, over the room of the first fight.

Followed from the game's own code, so the picture is the game's:
- where: src/main/menu/save_slot.c (Save_DrawSlotMetadata for the Scan lines, Menu_SaveOverlayDraw for
  running away): a bar 320 x 20 across the screen (Draw_AllocColorGradient over a rectangle, the
  menus' bevel: menulook.bevel) at y 191 with the line from 20,195, or at y 11 with the line from
  20,15. Which of the two a fight uses is the fight's own (Battle_SetWindowBoundsByMode: with the HP
  panel at the top the bar is at the bottom, and the other way round); the first fight's is the bottom.
- the letters: the textbox's dialogue path (gamelook.lay_out, Menu_DrawTextboxEntries); the HP line's
  two numbers are the enemy's HP and its maximum (Render_SetupColorTable(0, 2, {hp, max, -1})).
- behind: the first fight's room as it is (M005, its third view: the stage, Melissa's fight), not
  darkened. Aya, the enemy and the HP panel are 3D and sprites the tool doesn't draw; none of them
  is under the bar in that fight.

Checked: the game's frame of "Can't escape!" (battle/010) in the first fight equals this drawing in
every pixel of the bar and below it (tests/text_renderer_game.py, part translator-battle-look).
"""
from __future__ import annotations

from dataclasses import dataclass

from . import gamelook as GL
from . import menulook as ML
from petext import battle, rtl

ROOM, VIEW = 5, 2                    # the first fight's room picture: M005, view 3 (disc.views(5)[2])
BAR_W, BAR_H = 320, 20
BOTTOM, TOP = (191, 195), (11, 15)   # (the bar's top row, the line's top row)
# The numbers the HP line shows: the widest the line can get (user's choice, 2026-11-13).
NUMBERS = {"battle/001": (9999, 9999)}


@dataclass
class Look:
    rgb: bytes
    where: str
    notes: list[str]
    bar: tuple[int, int]             # the bar's first and last row + 1


def can_draw(key: str) -> bool:
    return battle.is_battle(key)


def look(font: GL.Font, raw: bytes, key: str, background: bytes | None, top: bool = False,
         name: bytes = b"") -> Look:
    """The line on the battle bar (at the top with `top`), over `background` (the first fight's room)."""
    bar_y, line_y = TOP if top else BOTTOM
    screen = ML.Screen(background, dim=False)
    for pixels, rgb, sign in ML.bevel(0, bar_y, BAR_W, BAR_H):
        screen.blend(pixels, rgb, sign)
    numbers = NUMBERS.get(key, ())
    laid = GL.lay_out(font, raw, name, battle.X, line_y, numbers=numbers, right_to_left=rtl.is_rtl(font.pack.lang))
    notes = list(laid.notes)
    if len(laid.screens) > 1:
        notes.append("a second screen the game doesn't show here (only the first is drawn)")
    shown = laid.screens[0]
    if any(p.y != line_y for p in shown):
        notes.append("a second line: the game draws it under the bar")
    if any(p.x + k % GL.CELL >= GL.W for p in shown for k, c in enumerate(p.letter.pixels)
           if c and k % GL.CELL < p.letter.w):
        notes.append("the line runs off the screen")
    where = ("the battle bar " + ("at the top" if top else "at the bottom, as in the first fight")
             + (f", with HP {numbers[0]}/{numbers[1]}" if numbers else ""))
    return Look(GL.draw(font, shown, screen.rgb()), where, notes, (bar_y, bar_y + BAR_H))
