"""Helpers the checks share: a fast-encounter ini, walking into a fight, the phone save with or
without staying on the line."""
from __future__ import annotations

from pedebug import flows
from pedebug.game import GameError, Session

ALL_MODS = "Manage mods/All mods=1\n"
# pe_qol's Battle frequency "Every check": an encounter within a few hundred frames of walking.
FAST_BATTLES = ALL_MODS + "PE Enhancements/Combat/Battle frequency=3\nCombat/Battle frequency=3\n"
STAY_ON_LINE_PAGE = 32513          # pe_phone's "Stay on the line?" box


def walk_into_battle(s: Session, budget: int = 8000) -> dict:
    """Walk around (fast encounters) until battle_started is logged; returns that event."""
    mark = s.events.mark()
    walked, d = 0, 0
    while walked < budget:
        for _ in range(12):
            s.advance(10, flows.DIRECTIONS[d % 8])
            walked += 10
            e = s.events.find("battle_started", since=mark)
            if e:
                return e
        d += 3
    raise GameError(f"no encounter after {walked} frames of walking: {s.state.short()}")


def phone_save(s: Session, number: int = 2, stay_on_line: bool = False, budget: int = 80) -> None:
    """Save at the phone Aya stands beside to save No `number`, through the game's own screens;
    then answer pe_phone's "Stay on the line?" (Yes plays a call, read through with Cross).
    Ends in free field."""
    flows.find_phone(s)
    s.press("cross", hold=4, after=60)                              # Yes, save
    s.wait(lambda st: flows._menu_sel(st) == 36, timeout=900, what="Select Slot (save)")
    s.advance(30)
    s.press("cross", hold=4, after=60)
    s.wait(lambda st: flows._menu_sel(st) == 37, timeout=900, what="the file list (save)")
    for _ in range(30):
        row = s.state.menu["cursor"][1]
        if row == number - 1:
            break
        s.press("down" if row < number - 1 else "up", hold=4, after=30)
    else:
        raise GameError(f"couldn't move to save No {number}")
    s.press("cross", hold=4, after=60)
    if flows._menu_sel(s.state) == 41:                              # Overwrite data? No is selected
        s.press("left", hold=4, after=30)
        s.press("cross", hold=4, after=60)
    s.wait(lambda st: flows._menu_sel(st) == 40, timeout=1800, what='"File saved"')
    s.advance(60)
    s.press("cross", hold=4, after=60)                              # OK
    answered = False
    for _ in range(budget):
        pages = [b.get("page") for b in s.state.dialogue]
        if STAY_ON_LINE_PAGE in pages and not answered:
            answered = True
            if stay_on_line:
                s.press("down", hold=4, after=20)                   # No is first
            s.press("cross", hold=4, after=60)
            continue
        if s.state.dialogue:
            s.press("cross", hold=4, after=50)
        elif s.state.menu:
            s.press("circle", hold=4, after=50)
        elif flows.free_field(s.state):
            break
        else:
            s.advance(30)
    flows.in_field(s, what="free field after the phone")
