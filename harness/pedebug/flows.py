"""Ways through the game's own screens, written down once so scenarios don't re-learn them.

Every step waits on the game's state where the control line shows it (menu widget modes and
selections, game flags) and otherwise gives the screen a fixed time; each flow ends with a check
that it got where it meant to, and takes a picture when it doesn't.

Learned on the native port (USA discs), headless:
  - the intro and logos play out (no skip) and the title comes up around VBlank 8800; it takes
    no input until it has faded in, so the flow waits a further 1200 frames;
  - Start shows New Game / Continue / Tutorial, Continue selected when a card has saves;
  - Continue: menu mode 2, selection 36 (Select Slot: card 1, card 2, END), Cross picks card 1,
    selection 37 lists the files (No 1 first), Cross loads ("File loaded", selection 40), Cross
    again closes it and the room starts (game flags 0x40000040 in free field).
"""
from __future__ import annotations

import math

from . import game
from .game import GameError, Session

MOVIE_PLAYING = 0x200          # game_flags bit while a movie plays


def to_title(s: Session) -> None:
    s.wait(lambda st: st.vblanks > 1000 and not st.game_flags & MOVIE_PLAYING, timeout=30000, step=30,
           what="the title screen")
    s.advance(1200)            # the title takes input only once it has faded in


def _menu_sel(st):
    return st.menu["selection"] if st.menu else None


def slot_of(card_file) -> int:
    """The file list's number for a card file: its last letter (BASLUS-006620000000C is No 3)."""
    return ord(str(card_file)[-1].upper()) - ord("A") + 1


def load_save(s: Session, number: int = 1, card: int = 1, from_boot: bool = True) -> None:
    """From the title: Continue, the card, save No `number` (the slot's place in the list: empty slots
    show as Unused File; see slot_of); returns once the room is running. from_boot=False: the
    title is already up and taking input (after soft_reset); waiting longer there lets its attract
    movie start, and Start would only skip that."""
    if from_boot:
        to_title(s)
    s.press("start")
    s.advance(100)
    s.press("cross")                                    # Continue
    s.wait(lambda st: _menu_sel(st) == 36, timeout=600, what="Select Slot")
    s.advance(60)
    if card == 2:
        s.press("down")
    s.press("cross")
    s.wait(lambda st: _menu_sel(st) == 37, timeout=600, what="the file list")
    s.advance(60)
    for _ in range(number - 1):
        s.press("down", hold=4, after=30)   # a longer hold repeats
    s.press("cross")                                    # load it
    s.wait(lambda st: _menu_sel(st) == 40, timeout=900, what='"File loaded"')
    s.advance(60)
    s.press("cross")
    in_field(s, what=f"the room of save No {number}")


SAVE_PROMPT_PAGE = 6           # "Will you save your progress?" (a phone)
DIRECTIONS = [game.UP, game.UP | game.RIGHT, game.RIGHT, game.DOWN | game.RIGHT, game.DOWN,
              game.DOWN | game.LEFT, game.LEFT, game.UP | game.LEFT]


def find_phone(s: Session) -> None:
    """A save loads with Aya where she saved, beside its phone but not always facing it: turn her
    through the eight directions (two frames each: a turn, hardly a step) and press Cross until
    the phone asks to save. Leaves the question open."""
    for d in DIRECTIONS:
        s.advance(2, d)
        s.advance(10)
        s.press("cross", hold=4, after=60)
        if any(b.get("page") == SAVE_PROMPT_PAGE for b in s.state.dialogue):
            return
        if s.state.dialogue or s.state.menu:     # something else answered: close it, go on
            s.press("circle", hold=4, after=60)
            in_field(s, timeout=600)
    s.shot("no-phone")
    raise GameError("no phone around Aya")


def _xz(actor) -> tuple[int, int]:
    return actor["position"][0], actor["position"][2]


def pad_headings(s: Session, frames: int = 24) -> list[tuple[float, float]]:
    """Which way in the room each of the eight pad directions moves Aya (unit x/z vectors). Pad
    directions follow the room's fixed camera; each is walked once and then walked back."""
    out = []
    for n, d in enumerate(DIRECTIONS):
        start = _xz(s.state.player)
        s.advance(frames, d)
        end = _xz(s.state.player)
        s.advance(frames, DIRECTIONS[(n + 4) % 8])
        s.advance(10)
        dx, dz = end[0] - start[0], end[1] - start[1]
        length = math.hypot(dx, dz) or 1.0
        out.append((dx / length, dz / length))
    return out


def walk_to(s: Session, actor_index: int, near: int = 1_200_000, steps: int = 120, stride: int = 12) -> float:
    """Walk Aya up to another actor: each stride takes the pad direction whose heading
    (pad_headings) points most nearly at it. If a stride gains nothing (something in the way),
    the next ones try the neighbouring directions; blocked every way, she has arrived (a person
    can't be walked into). Returns the distance left."""
    target = _xz(s.state.raw["actors"][actor_index])
    headings = pad_headings(s)

    def dist():
        p = s.state.player
        return math.dist(_xz(p), target) if p else float("inf")

    blocked = 0
    for _ in range(steps):
        before = dist()
        if before < near:
            break
        x, z = _xz(s.state.player)
        to = (target[0] - x, target[1] - z)
        order = sorted(range(8), key=lambda n: -(headings[n][0] * to[0] + headings[n][1] * to[1]))
        s.advance(stride, DIRECTIONS[order[min(blocked, 2)]])
        blocked = blocked + 1 if dist() > before - near / 20 else 0
        if blocked > 3:
            break
    return dist()


def talk(s: Session, pages=None) -> list[int]:
    """Turn Aya through the eight directions pressing Cross until someone answers; with `pages`,
    read on (Cross) until one of them shows. Returns the open boxes' pages; leaves them open."""
    for d in DIRECTIONS:
        s.advance(2, d)
        s.advance(8)
        s.press("cross", hold=4, after=60)
        open_pages = [b["page"] for b in s.state.dialogue]
        if open_pages:
            for _ in range(8):
                if pages is None or set(open_pages) & set(pages):
                    return open_pages
                s.press("cross", hold=4, after=60)
                open_pages = [b["page"] for b in s.state.dialogue]
            return open_pages
    return []


def save_at_phone(s: Session, number: int) -> None:
    """Save through the phone and the game's own card transfer, to save No `number` on card 1
    (overwriting it if it holds a save). Ends back in free field.
    Learned: Yes is selected; after it, Select Slot (selection 36), then the file list
    (37, cursor row = the current save's place), then "Overwrite data?" (41, No selected) for a used
    slot, then "File saved" (40)."""
    find_phone(s)
    s.press("cross", hold=4, after=60)                  # Yes
    s.wait(lambda st: _menu_sel(st) == 36, timeout=900, what="Select Slot (save)")
    s.advance(30)
    s.press("cross", hold=4, after=60)
    s.wait(lambda st: _menu_sel(st) == 37, timeout=900, what="the file list (save)")
    for _ in range(50):
        row = s.state.menu["cursor"][1]
        if row == number - 1:
            break
        s.press("down" if row < number - 1 else "up", hold=4, after=30)
    else:
        raise GameError(f"couldn't move to save No {number}")
    s.press("cross", hold=4, after=60)
    if _menu_sel(s.state) == 41:                        # Overwrite data?  No is selected
        s.press("left", hold=4, after=30)
        s.press("cross", hold=4, after=60)
    s.wait(lambda st: _menu_sel(st) == 40, timeout=1800, what='"File saved"')
    s.advance(60)
    s.press("cross", hold=4, after=60)                  # OK: back to the file list
    for _ in range(6):                                  # Circle backs out to the field
        if free_field(s.state):
            break
        s.press("circle", hold=4, after=60)
    in_field(s, what="free field after saving")


def ex_game(s: Session, number: int, card: int = 1, from_boot: bool = True) -> None:
    """From the title: EX Game, the card, Clear Data save No `number` (see slot_of), through
    "File loaded". The title's EX Game row (above New Game) appears only when a card holds clear
    data (MemCard_HasAnyPresentFile); it opens the same file list as Continue with the clear-data
    flag set (Save_SetTitleStyleFlag). Continue's list shows clear saves but won't load them."""
    if from_boot:
        to_title(s)
    s.press("start")
    s.advance(100)
    s.press("up", hold=4, after=40)                     # Continue -> New Game
    s.press("up", hold=4, after=40)                     # New Game -> EX Game
    s.press("cross")
    s.wait(lambda st: _menu_sel(st) == 36, timeout=600, what="Select Slot")
    s.advance(60)
    if card == 2:
        s.press("down")
    s.press("cross")
    s.wait(lambda st: _menu_sel(st) == 37, timeout=600, what="the file list")
    s.advance(60)
    for _ in range(number - 1):
        s.press("down", hold=4, after=30)
    s.press("cross")
    s.wait(lambda st: _menu_sel(st) == 40, timeout=900, what='"File loaded"')
    s.advance(60)
    s.press("cross")


TITLE_SCENE = 0xA9400048
RESET_COMBO = game.L1 | game.L2 | game.R1 | game.R2 | game.START | game.SELECT


def soft_reset(s: Session) -> None:
    """The game's soft reset (L1+L2+R1+R2+Start+Select) back to the title screen, ready for input."""
    s.advance(60, RESET_COMBO)
    s.advance(60)
    s.wait(lambda st: st.scene == TITLE_SCENE and not st.game_flags & MOVIE_PLAYING, timeout=30000, step=30,
           what="the title after a soft reset")
    s.advance(1500)            # as at boot: no input until it has faded in


def new_game(s: Session, from_boot: bool = True) -> None:
    """New Game from the title (Continue is selected when a card has saves: Up, then Cross), until
    Aya stands in the opening room (level 1, 45 HP). from_boot=False: the title is already up."""
    if from_boot:
        to_title(s)
    s.press("start")
    s.advance(100)
    s.press("up", hold=4, after=40)
    s.press("cross")
    s.wait(lambda st: st.player is not None and st.hp == 45 and st.max_hp == 45, timeout=20000, step=100,
           what="the new game's first room")
    s.advance(300)


def free_field(st) -> bool:
    """As the mods' pe_free_field (host/pe_game.c): no battle, menu or event holding the field."""
    return (st.player is not None and st.menu is None and not st.flags & 0x2006
            and not st.game_flags & 0xB600)


def in_field(s: Session, timeout: int = 6000, what: str = "free field") -> None:
    """Wait until Aya stands in a running room the player could walk around."""
    s.wait(free_field, timeout=timeout, what=what)
    s.advance(60)
