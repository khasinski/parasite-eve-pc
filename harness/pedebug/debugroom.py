"""The game's own debug room (map 360), left on the disc: jump to any chapter of the story, ride a
car between places, switch random encounters, skip events and get items, from any save.

    from pedebug import debugroom
    debugroom.jump(s, "Warehouse")          # the chapter's first room, story progress set
    debugroom.battles(s, on=False)          # no random encounters
    debugroom.give(s, "Get Bullets")

The room is a black space with six boxes; each opens a menu (a dialogue choice box). Walking up to
a box is unreliable (Aya stops where another box answers), so each menu is opened from a spot she
reached by walking (on the floor: placing her off it crashes the floor code), facing one way.

What a jump does: it runs the chapter's own start (room, story progress and the scene's flags), with
Aya as she is (level, HP, bag). The game's story day (shown by `room`) is not updated, so anything
reading the day (the save screen, Phone Calls' day rules) can be off until a real day change. A jump
starts the chapter; a moment inside it still needs playing forward or a scenario save.
"""
from __future__ import annotations

from pathlib import Path

from . import flows, game, places
from .game import GameError, Session

DEBUG_ROOM = 360

# Where each menu opens: Aya's spot (x, z), the pad direction to face, and words its rows hold.
MENUS = {
    "story":    ((-3132382, 12960045), game.RIGHT, "Opening"),
    "copcar":   ((28442142, 15072988), game.UP, "Copcar"),
    "switches": ((-26785106, 8287430), game.UP | game.RIGHT, "Battle OFF"),
    "items":    ((27118776, -20292195), game.DOWN | game.RIGHT, "Get Bullets"),
}

# The story menu's pages, in order (row 0 Cancel, row 1 Next page, then these; the last page has
# no Next page). Days as the game counts them.
STORY_PAGES = [
    ["Opening", "Under Theatre", "Sewer 1", "Theatre Exterior", "NYPD -Day1-"],
    ["Museum -Day1-", "NYPD -Meeting-", "Central Park", "NYPD -After CP-", "Police Barricade"],
    ["Soho", "Museum -Day2-", "NYPD Battle", "Hospital", "NYPD EVE Meeting"],
    ["Chinatown", "Warehouse", "Sewer 2", "Subway", "Brooklyn Bridge"],
    ["Museum Final", "Street", "Aircraft Carrier", "Helicopter", "Statue of Liberty"],
    ["Cruiser", "Port", "Theatre Epilogue", "Chrysler  1F", "Chrysler  71F", "Chrysler  77F"],
]
STORY = [name for page in STORY_PAGES for name in page]
STORY_DAY = {**{n: 1 for n in STORY_PAGES[0] + ["Museum -Day1-"]},
             **{n: 2 for n in ["NYPD -Meeting-", "Central Park", "NYPD -After CP-", "Police Barricade"]},
             **{n: 3 for n in STORY_PAGES[2]}, **{n: 4 for n in STORY_PAGES[3]},
             **{n: 5 for n in STORY_PAGES[4]}, **{n: 6 for n in STORY_PAGES[5]}}

# Where each jump lands with Aya in control (map number), from the story-jumps check (save C,
# strong=True, Watch). A jump that lands elsewhere is an error. Cutscene-only chapters (Street,
# Aircraft Carrier, Helicopter) play into Statue of Liberty's room; NYPD -After CP-, Police
# Barricade and Soho all reach Soho's first room (92) through the day's title card.
STORY_MAP: dict[str, int] = {
    "Opening": 2, "Under Theatre": 11, "Sewer 1": 26, "Museum -Day1-": 191, "NYPD -Meeting-": 46,
    "Theatre Exterior": 41, "NYPD -Day1-": 41, "Central Park": 374, "NYPD -After CP-": 92, "Police Barricade": 92, "Soho": 92, "Museum -Day2-": 192,
    "NYPD Battle": 103, "Hospital": 125, "NYPD EVE Meeting": 42, "Chinatown": 161, "Warehouse": 260,
    "Sewer 2": 166, "Subway": 182, "Brooklyn Bridge": 189, "Museum Final": 192, "Street": 273,
    "Aircraft Carrier": 273, "Helicopter": 273, "Statue of Liberty": 273, "Cruiser": 275, "Chrysler  1F": 290,
    "Chrysler  71F": 312, "Chrysler  77F": 318,
}
# Chapters whose start leaves story progress (variable 74) as the save had it: the Day the save
# screen and Phone Calls show comes from it, so pass jump(story=...) when that matters.
STORY_KEEPS = ["Under Theatre", "Warehouse", "Subway", "Brooklyn Bridge", "Chrysler  1F", "Chrysler  71F",
               "Chrysler  77F"]
# Day 1 chapters: they play on into the next morning at the precinct (map 42), whose scene waits
# forever when the save's story is already past Day 1. Jump to them from a New Game (or a Day 1
# save): story progress (variable 74) at most EARLY_MAX. They land in Baker's office (map 41).
STORY_EARLY = ["Theatre Exterior", "NYPD -Day1-"]
EARLY_MAX = 0x88
# Chapters whose opening fight beats even God mode (a hit bigger than Aya's maximum HP): strong=True
# with pe_assists in the build (its No HP loss, see strengthen).
STORY_STRONG = ["Cruiser"]
# Story points a jump doesn't bring to free field (jump() refuses them unless forced).
STORY_LIMITS = {
    "Port": "plays on into the ending and the title (the game's last chapters)",
    "Theatre Epilogue": "the ending: credits, then the Clear Data save (finish_game)",
}

SWITCHES = ["Battle OFF", "Battle ON", "Get weapon * armor", "Event skip"]
ITEMS = ["Get Poisons", "Get Bullets", "Get Status Item", "Get Tools, etc.", "Get Tune up Items"]


def _box(s: Session):
    d = s.ui()["dialogue"]
    return d[0] if d else None


def _rows(box) -> list[str]:
    """The box's choice rows, in choice order: a title line above them (the Copcar menu's
    "Copcar  Event") is left out. The first choice carries the cursor's {FB}9x code."""
    rows = [r.strip() for r in box["text"].split(" / ")]
    first = next((i for i, r in enumerate(rows) if r.startswith("{FB}9")), 0)
    return rows[first:]


def choose(s: Session, row: int) -> None:
    """Move the open choice box's cursor to `row` (0 = the first choice) and confirm."""
    for _ in range(12):
        b = _box(s)
        if not b or b["choice"] == row:
            break
        s.press("down" if b["choice"] < row else "up", hold=4, after=10)
    s.press("cross", hold=4, after=40)


def close(s: Session) -> None:
    """Cancel an open debug menu (its first row) and read on to free field."""
    if _box(s) and _box(s)["choices"]:
        choose(s, 0)
    for _ in range(10):
        if not _box(s):
            break
        s.press("cross", hold=4, after=30)
    flows.in_field(s, what="free field after the debug menu")


def enter(s: Session) -> None:
    """Into the debug room (a warp), unless Aya is there."""
    if places.room(s)["map"] != DEBUG_ROOM:
        places.warp(s, DEBUG_ROOM)


def open_menu(s: Session, which: str) -> dict:
    """Stand at the menu's spot, face its box and open it; returns the open box (ui-state)."""
    (x, z), face, marker = MENUS[which]
    enter(s)
    places.place(s, x, z)
    s.advance(10)
    for d in [face] + [d for d in flows.DIRECTIONS if d != face]:
        s.advance(2, d)
        s.advance(8)
        s.press("cross", hold=4, after=50)
        b = _box(s)
        if b and marker in b["text"]:
            return b
        if b:
            close(s)
            places.place(s, x, z)
            s.advance(10)
    s.shot(f"no-{which}-menu")
    raise GameError(f"the debug room's {which} menu didn't open: {s.state.short()}")


def _pick(s: Session, which: str, name: str) -> None:
    """Open menu `which` and choose the row whose text starts with `name`, paging with Next page."""
    b = open_menu(s, which)
    for _ in range(12):
        rows = _rows(b)
        hit = [i for i, r in enumerate(rows) if r.replace("{FB}97", "").replace("{FB}95", "").replace("{FB}96", "")
               .strip().lower().startswith(name.lower())]
        if hit:
            choose(s, hit[0])
            return
        nxt = [i for i, r in enumerate(rows) if r.lower().startswith("next page")]
        if not nxt:
            break
        choose(s, nxt[0])
        s.advance(20)
        b = _box(s)
        if not b:
            break
    close(s)
    raise GameError(f"no {name!r} in the debug room's {which} menu")


WATCH_PAGE = 7                 # a chapter's "Will you watch this event?" (Watch / Skip) as it starts
NAME_ENTRY, NAME_END = 17, 25  # New Game's Enter Name (menu selections): the grid, its END button
SAVE_SCREENS = (36, 37, 41)    # the card's Select Slot / file list / Overwrite (menu selections)
SAVE_PROMPT_TEXT = "Will you save your progress?"   # Yes / No: at a phone, after a day's title card


def _save_prompt(s: Session, boxes) -> bool:
    """The game's save question is open, by its text (its page is 6 at a phone, 182 after a day's
    title card, and narration screens reuse low page numbers)."""
    e = s.events.last("dialogue_waiting")
    return bool(e and e.get("choices") and SAVE_PROMPT_TEXT in (e.get("text") or "")
                and any(x.get("page") == e.get("page") for x in boxes))


STRONG_ROWS = ("Cheats/God mode", "Cheats/Infinite ammo and PE",   # pe_cheats
               "Playtest assists/No HP loss (battle)")                 # pe_assists (opt-in)


def strengthen(s: Session, level: int = 99) -> None:
    """Keep Aya alive through a late chapter's opening fight: switched on in memory (ini-set), the
    Cheats mod's God mode and infinite ammo and PE, and the Assists mod's No HP loss (battle); her
    level raised (set-stat; her HP maximum doesn't follow). God mode alone refills HP once a battle
    frame, so one hit bigger than her maximum still downs her (Cruiser's boss); No HP loss cancels
    the hit itself (PE_HOOK_HP_LOSS). pe_assists is opt-in: built with PE_EXTRA_MODS."""
    for row in STRONG_ROWS:
        try:
            s.command(f"ini-set {row} 1")
        except GameError:
            pass                                             # that mod isn't in the build
    s.command(f"set-stat level {level}")
    p = s.battle()["player"]
    s.command(f"set-stat hp {p['max_hp']}")


def _watch_prompt(s: Session, boxes) -> bool:
    """A chapter's "Will you watch this event?" is open: page 7 alone also matches narration
    pages in rooms where the debug channel isn't polled (map 0), so its text decides."""
    e = s.events.last("dialogue_waiting")
    return bool(e and e.get("page") == WATCH_PAGE and e.get("choices") and "watch" in (e.get("text") or "").lower()
                and any(x.get("page") == WATCH_PAGE for x in boxes))


def _to_control(s: Session, mark: int, what: str, watch: bool, budget: int, where: str) -> dict:
    """Play on from a debug-room menu choice until Aya has control outside the debug room: attack
    in fights, take New Game's default name, answer the chapter's Watch prompt, decline saves, read
    on through dialogue and day title cards. Returns the last scene_loaded event."""
    answered = False
    idle, last_seq = 0, -1
    for _ in range(budget):
        seq = len(s.events.since(0))
        idle = idle + 1 if seq == last_seq else 0
        last_seq = seq
        here = s.events.find("scene_loaded", since=mark, where=lambda ev: ev["map"] != DEBUG_ROOM)
        if here and flows.free_field(s.state) and not s.state.dialogue:
            s.advance(60)
            if flows.free_field(s.state) and not s.state.dialogue:
                break
        st, b, sel = s.state, s.state.dialogue, flows._menu_sel(s.state)
        if s.events.find("to_title", since=mark):
            s.shot(f"{what}-title")
            raise GameError(f"{what}: back at the title (Aya fell? strong=True)")
        if st.flags & 0x2:                                   # a fight on the way: attack
            s.press("cross", hold=4, after=26)
        elif sel == NAME_ENTRY:                              # New Game's Enter Name: Start goes to END
            s.press("start", hold=4, after=40)
        elif sel == NAME_END:
            s.press("cross", hold=4, after=60)
        elif sel in SAVE_SCREENS:                            # a save offered on the way: don't
            s.press("circle", hold=4, after=40)
        elif b and not answered and _watch_prompt(s, b):
            answered = True
            s.advance(20)
            choose(s, 0 if watch else 1)
        elif b and _save_prompt(s, b):
            choose(s, 1)                                     # "Will you save?": No
        elif b:
            s.press("cross", hold=4, after=30)
        elif st.menu is not None or idle >= 8:          # a menu not listed above, or a day's title
            s.press("cross", hold=4, after=40)          # card (map 351: the field waits, no events)
            idle = 0
        else:
            s.advance(60)
    else:
        s.shot(what)
        raise GameError(f"{what}: no control in {where}: {s.state.short()}")
    return s.events.last("scene_loaded")


def jump(s: Session, name: str, watch: bool = True, strong: bool = False, budget: int = 5000,
         force: bool = False, story: int | None = None, day: int | None = None) -> dict:
    """Jump to a story point (STORY, e.g. "Warehouse") from free field anywhere: the debug room's
    story menu, then the chapter's own "Will you watch this event?" (Watch), reading on through the
    scene until Aya has control in the chapter's first room. Returns that room's scene_loaded event.
    watch=False answers Skip: faster, and the chapter's first room is the same, but its opening
    event doesn't play. Story progress (script variable 74) is whatever the chapter's start sets:
    some set it (Chinatown: 220), some leave it as the save had it (Warehouse from save C: F8). A
    check that needs a story value sets it (places.set_var) or checks what it needs.
    On the way it attacks in a fight the chapter opens with, takes the default name at New Game's
    Enter Name and declines any save. story= sets story progress afterwards (STORY_KEEPS lists the
    chapters that leave it as it was); day= the game's day index (set-day). Aya is as the save had
    her: strong=True first raises her to
    level 99 at full HP with God mode and No HP loss (strengthen), which late chapters' opening
    fights need (STORY_STRONG). STORY_EARLY chapters need a Day 1 game (a New Game)."""
    if name not in STORY:
        raise GameError(f"no story point {name!r}; one of {STORY}")
    if name in STORY_LIMITS and not force:
        raise GameError(f"jump {name!r} doesn't reach free field: {STORY_LIMITS[name]} (force=True to try)")
    if name in STORY_EARLY and not force and places.get_var(s, 74) > EARLY_MAX:
        raise GameError(f"jump {name!r} needs a Day 1 game (story at most {EARLY_MAX:X}, e.g. New Game): "
                        f"from later the next morning's precinct scene waits forever")
    if name in STORY_STRONG and not strong and not force:
        raise GameError(f"jump {name!r} needs strong=True (its opening fight beats a normal Aya)")
    mark = s.events.mark()
    if strong:
        strengthen(s)
    _pick(s, "story", name)
    e = _to_control(s, mark, f"jump {name!r}", watch, budget, "the chapter's room")
    if story is not None:                 # story progress the chapter's start didn't set (STORY_KEEPS)
        places.set_var(s, 74, story)
    if day is not None:                   # the game's day index (set-day), not the shown Day
        s.command(f"set-day {day}")
    want = STORY_MAP.get(name)
    if want is not None and e["map"] != want:
        raise GameError(f"jump {name!r} landed in map {e['map']}, not {want}")
    return e


def finish_game(s: Session, budget: int = 4000) -> Path:
    """Finish the game from free field: the story menu's "Theatre Epilogue" (the ending, then about
    28,000 frames of credits), and the Clear Data save the game offers after them, to the card's
    first slot (row 0; it must be empty). Returns the clear save's card file (BASLUS-...0001<slot>).
    The EX Game's start command (Inv_DispatchCommand INV_CMD_START_NEW_GAME) runs before that
    save, so the mods have had their New Game (kind EX) and the clear save holds their reset data.
    Aya is strengthened first (the epilogue opens with a fight). Takes several minutes headless."""
    mark = s.events.mark()
    strengthen(s)
    _pick(s, "story", "Theatre Epilogue")
    saved = None
    for _ in range(budget):
        st, sel = s.state, flows._menu_sel(s.state)
        saved = s.events.find("save_committed", since=mark)
        if saved:
            break
        if s.events.find("to_title", since=mark):
            raise GameError("finish_game: back at the title before the Clear Data save")
        if st.flags & 0x2:
            s.press("cross", hold=4, after=26)
        elif sel == NAME_ENTRY:
            s.press("start", hold=4, after=40)
        elif sel == 37:                                      # the file list: row 0 (slot A)
            if st.menu["cursor"][1] > 0:
                s.press("up", hold=4, after=30)
            else:
                s.press("cross", hold=4, after=60)
        elif sel == 41:                                      # Overwrite? never: slot A must be empty
            raise GameError("finish_game: the card's first slot isn't empty")
        elif sel is not None or st.dialogue:
            s.press("cross", hold=4, after=40)
        else:
            s.advance(60)
    if not saved:
        s.shot("finish-game")
        raise GameError(f"finish_game: no Clear Data save: {s.state.short()}")
    s.advance(60)
    clear = [f for f in s.card_files() if f.name[18] == "1"]
    if not clear:
        raise GameError(f"finish_game: saved, but no clear-data card file: {[f.name for f in s.card_files()]}")
    return clear[0]


# Rides from Day 1 (Theatre-NYPD) play on into the next morning's precinct scene like the Day 1
# chapters (STORY_EARLY): ride them from a New Game.
RIDES_EARLY = ["Theatre-NYPD"]
RIDES = ["Theatre-NYPD", "NYPD- Museum1", "Museum1-NYPD", "NYPD-Central Park", "Soho-Museum2",
         "Museum2-NYPD Btl", "NYPD- Hospital", "Hospital-NYPD", "Museum3-Big Blobby", "NAVY Attack!"]
# Where each ride leaves Aya in control (map number), from the copcar-rides check (save C, strong).
RIDE_MAP: dict[str, int] = {
    "Theatre-NYPD": 41, "NYPD- Museum1": 191, "Museum1-NYPD": 46, "NYPD-Central Park": 374,
    "Soho-Museum2": 192, "Museum2-NYPD Btl": 103, "NYPD- Hospital": 125, "Hospital-NYPD": 46,
    "Museum3-Big Blobby": 273, "NAVY Attack!": 273,
}


def ride(s: Session, name: str, watch: bool = True, strong: bool = False, budget: int = 5000) -> dict:
    """A Copcar Event ride (RIDES: the car scenes between places, some with the story events
    after them), played on like jump() until Aya has control. Returns that room's scene_loaded."""
    if name not in RIDES:
        raise GameError(f"no ride {name!r}; one of {RIDES}")
    if name in RIDES_EARLY and places.get_var(s, 74) > EARLY_MAX:
        raise GameError(f"ride {name!r} needs a Day 1 game (story at most {EARLY_MAX:X}, e.g. New Game)")
    mark = s.events.mark()
    if strong:
        strengthen(s)
    _pick(s, "copcar", name)
    e = _to_control(s, mark, f"ride {name!r}", watch, budget, "the ride's end")
    want = RIDE_MAP.get(name)
    if want is not None and e["map"] != want:
        raise GameError(f"ride {name!r} ended in map {e['map']}, not {want}")
    return e


def copcar(s: Session, name: str, timeout: int = 6000) -> dict:
    """Start a Copcar Event ride and return its first room's scene_loaded (no playing on; ride()
    plays to control)."""
    mark = s.events.mark()
    _pick(s, "copcar", name)
    return s.events.wait("scene_loaded", since=mark, timeout=timeout, step=10,
                         where=lambda ev: ev.get("map") != DEBUG_ROOM)


def battles(s: Session, on: bool) -> None:
    """Random encounters on or off (the game's own debug switch)."""
    _pick(s, "switches", "Battle ON" if on else "Battle OFF")
    close(s)


def event_skip(s: Session) -> None:
    _pick(s, "switches", "Event skip")
    close(s)


def give(s: Session, what: str) -> None:
    """One of ITEMS ("Get Bullets", ...), or "Get weapon" (the switches menu's weapon and armor)."""
    _pick(s, "switches" if what.lower().startswith("get weapon") else "items", what)
    close(s)
