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
    "Central Park": 374, "NYPD -After CP-": 92, "Police Barricade": 92, "Soho": 92, "Museum -Day2-": 192,
    "NYPD Battle": 103, "Hospital": 125, "NYPD EVE Meeting": 42, "Chinatown": 161, "Warehouse": 260,
    "Sewer 2": 166, "Subway": 182, "Brooklyn Bridge": 189, "Museum Final": 192, "Street": 273,
    "Aircraft Carrier": 273, "Helicopter": 273, "Statue of Liberty": 273, "Chrysler  1F": 290,
    "Chrysler  71F": 312, "Chrysler  77F": 318,
}
# Story points a jump can't bring to free field yet, and why (jump() refuses them unless forced).
STORY_LIMITS = {
    "Theatre Exterior": "stops in the NYPD office (map 42) in event mode, no dialogue or menu to answer",
    "NYPD -Day1-": "stops in the NYPD office (map 42) in event mode, no dialogue or menu to answer",
    "Cruiser": "its opening fight beats Aya even with God mode and level 99; the game returns to the title",
    "Port": "plays on into the ending and the title (the game's last chapters)",
    "Theatre Epilogue": "the ending: credits, then the title",
}

SWITCHES = ["Battle OFF", "Battle ON", "Get weapon * armor", "Event skip"]
ITEMS = ["Get Poisons", "Get Bullets", "Get Status Item", "Get Tools, etc.", "Get Tune up Items"]


def _box(s: Session):
    d = s.ui()["dialogue"]
    return d[0] if d else None


def _rows(box) -> list[str]:
    return [r.strip() for r in box["text"].split(" / ")]


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
SAVE_PROMPT_PAGES = (6,)       # "Will you save your progress?" (Yes / No)


def strengthen(s: Session, level: int = 99) -> None:
    """Keep Aya alive through a late chapter's opening fight: the Cheats mod's God mode and
    infinite ammo and PE switched on in memory (ini-set; needs pe_cheats in the build), and her
    level raised (set-stat; her HP maximum doesn't follow a level set this way)."""
    for row in ("Cheats/God mode", "Cheats/Infinite ammo and PE"):
        try:
            s.command(f"ini-set {row} 1")
        except GameError:
            pass                                             # no pe_cheats: level alone
    s.command(f"set-stat level {level}")
    p = s.battle()["player"]
    s.command(f"set-stat hp {p['max_hp']}")


def jump(s: Session, name: str, watch: bool = True, strong: bool = False, budget: int = 2500,
         force: bool = False) -> dict:
    """Jump to a story point (STORY, e.g. "Warehouse") from free field anywhere: the debug room's
    story menu, then the chapter's own "Will you watch this event?" (Watch), reading on through the
    scene until Aya has control in the chapter's first room. Returns that room's scene_loaded event.
    watch=False answers Skip: faster, and the chapter's first room is the same, but its opening
    event doesn't play. Story progress (script variable 74) is whatever the chapter's start sets:
    some set it (Chinatown: 220), some leave it as the save had it (Warehouse from save C: F8). A
    check that needs a story value sets it (places.set_var) or checks what it needs.
    On the way it attacks in a fight the chapter opens with, takes the default name at New Game's
    Enter Name and declines any save. Aya is as the save had her: strong=True first raises her to
    level 99 at full HP, which late chapters' opening fights need (Cruiser kills a level-13 Aya)."""
    if name not in STORY:
        raise GameError(f"no story point {name!r}; one of {STORY}")
    if name in STORY_LIMITS and not force:
        raise GameError(f"jump {name!r} doesn't reach free field: {STORY_LIMITS[name]} (force=True to try)")
    mark = s.events.mark()
    if strong:
        strengthen(s)
    _pick(s, "story", name)
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
            s.shot(f"jump-{name}-title")
            raise GameError(f"jump {name!r}: back at the title (Aya fell? strong=True)")
        if st.flags & 0x2:                                   # a fight the chapter opens with: attack
            s.press("cross", hold=4, after=26)
        elif sel == NAME_ENTRY:                              # New Game's Enter Name: Start goes to END
            s.press("start", hold=4, after=40)
        elif sel == NAME_END:
            s.press("cross", hold=4, after=60)
        elif sel in SAVE_SCREENS:                            # a save offered on the way: don't
            s.press("circle", hold=4, after=40)
        elif b and any(x.get("page") == WATCH_PAGE for x in b) and not answered:
            answered = True
            s.advance(20)
            choose(s, 0 if watch else 1)
        elif b and any(x.get("page") in SAVE_PROMPT_PAGES for x in b):
            choose(s, 1)                                     # "Will you save?": No
        elif b:
            s.press("cross", hold=4, after=30)
        elif st.menu is not None or idle >= 8:          # a menu not listed above, or a day's title
            s.press("cross", hold=4, after=40)          # card (map 351: the field waits, no events)
            idle = 0
        else:
            s.advance(60)
    else:
        s.shot(f"jump-{name}")
        raise GameError(f"jump {name!r}: no control in the chapter's room: {s.state.short()}")
    e = s.events.last("scene_loaded")
    want = STORY_MAP.get(name)
    if want is not None and e["map"] != want:
        raise GameError(f"jump {name!r} landed in map {e['map']}, not {want}")
    return e


def copcar(s: Session, name: str, timeout: int = 6000) -> dict:
    """A car ride between places (the Copcar Event menu: "Theatre-NYPD", "NYPD- Museum1", ...)."""
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
