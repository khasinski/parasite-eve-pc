"""The game's debug room (pedebug.debugroom): every story jump lands in a room with control, and
its switches work (random encounters off and on, items)."""
from __future__ import annotations

import concurrent.futures as cf
import json

from pedebug import debugroom, flows, places
from pedebug.game import GameError
from pedebug.runner import check

from ._common import FAST_BATTLES, through_dialogue, walk_into_battle


def _one_jump(ctx, name: str, **jump_args) -> dict:
    """From save C, or a New Game for the Day 1 chapters (STORY_EARLY)."""
    early = name in debugroom.STORY_EARLY
    s = ctx.session(cards=None if early else [ctx.card("C")],
                    name="jump-" + "".join(c if c.isalnum() else "-" for c in name).strip("-"))
    if early:
        flows.new_game(s)
        through_dialogue(s, what="control after the opening")
    else:
        flows.load_save(s, flows.slot_of(ctx.card("C")))
    try:
        e = debugroom.jump(s, name, strong=True, **jump_args)
        here = places.room(s)
        s.shot("landed")
        return {"name": name, "map": e["map"], "story": here["story"], "free": here["free"], "error": None}
    except GameError as err:
        return {"name": name, "map": None, "error": str(err).splitlines()[0]}
    finally:
        s.save_inputs()
        s.quit()
        ctx.sessions.remove(s)


@check("story-jumps", tags=("jumps",), needs=("card:C",), timeout_s=1800)
def story_jumps(ctx):
    """Every story point jump() supports (debugroom.STORY minus STORY_LIMITS), strong, from save C
    (the Day 1 chapters from a New Game), four games at once: each lands with Aya in control in the
    room STORY_MAP lists. Cruiser needs pe_assists in the build. Writes jumps.json (name, map, story)."""
    names = [n for n in debugroom.STORY if n not in debugroom.STORY_LIMITS]
    with cf.ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(lambda n: _one_jump(ctx, n), names))
    for name, why in debugroom.STORY_LIMITS.items():
        ctx.note(f"{name}: not jumped ({why})")
    (ctx.dir / "jumps.json").write_text(json.dumps(results, indent=1))
    for r in results:
        if not ctx.expect(r["error"] is None, f"{r['name']}: {r['error']}"):
            continue
        ctx.expect(r["map"] != debugroom.DEBUG_ROOM and r["free"], f"{r['name']}: landed in map {r['map']} with control")
        want = debugroom.STORY_MAP.get(r["name"])
        if want is not None:
            ctx.expect(r["map"] == want, f"{r['name']}: map {r['map']}, expected {want}")
        ctx.note(f"{r['name']} (day {debugroom.STORY_DAY[r['name']]}): map {r['map']}, story {r['story']:X}")
    ctx.fact("jumps", f"{sum(r['error'] is None for r in results)} of {len(results)}")


def _one_ride(ctx, name: str) -> dict:
    early = name in debugroom.RIDES_EARLY
    s = ctx.session(cards=None if early else [ctx.card("C")],
                    name="ride-" + "".join(c if c.isalnum() else "-" for c in name).strip("-"))
    if early:
        flows.new_game(s)
        through_dialogue(s, what="control after the opening")
    else:
        flows.load_save(s, flows.slot_of(ctx.card("C")))
    try:
        e = debugroom.ride(s, name, strong=True)
        here = places.room(s)
        s.shot("landed")
        return {"name": name, "map": e["map"], "story": here["story"], "free": here["free"], "error": None}
    except GameError as err:
        return {"name": name, "map": None, "error": str(err).splitlines()[0]}
    finally:
        s.save_inputs()
        s.quit()
        ctx.sessions.remove(s)


@check("copcar-rides", tags=("jumps",), needs=("card:C",), timeout_s=1800)
def copcar_rides(ctx):
    """Every Copcar Event ride (debugroom.RIDES), strong, from save C (Theatre-NYPD from a New
    Game), four games at once: each is
    played on until Aya has control, in the room RIDE_MAP lists. Writes rides.json."""
    with cf.ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(lambda n: _one_ride(ctx, n), debugroom.RIDES))
    (ctx.dir / "rides.json").write_text(json.dumps(results, indent=1))
    for r in results:
        if not ctx.expect(r["error"] is None, f"{r['name']}: {r['error']}"):
            continue
        ctx.expect(r["map"] != debugroom.DEBUG_ROOM and r["free"], f"{r['name']}: ended in map {r['map']} with control")
        want = debugroom.RIDE_MAP.get(r["name"])
        if want is not None:
            ctx.expect(r["map"] == want, f"{r['name']}: map {r['map']}, expected {want}")
        ctx.note(f"{r['name']}: map {r['map']}, story {r['story']:X}")
    ctx.fact("rides", f"{sum(r['error'] is None for r in results)} of {len(results)}")


@check("story-jump-one", tags=("diag", "jumps"), needs=("card:C",), timeout_s=300)
def story_jump_one(ctx):
    """A single jump in the quick suite: save C to Warehouse (Day 4), in control, with story progress
    set through jump(story=...) since Warehouse's start leaves the save's."""
    r = _one_jump(ctx, "Warehouse", story=0x22C)
    ctx.require(r["error"] is None, f"Warehouse: {r['error']}")
    ctx.expect(r["map"] != debugroom.DEBUG_ROOM and r["free"], f"landed in map {r['map']} with control")
    ctx.expect(r["story"] == 0x22C, f"story progress set with story= (Warehouse keeps the save's): {r['story']:X}")
    ctx.fact("Warehouse", f"map {r['map']}, story {r['story']:X}")


@check("debug-room-switches", tags=("jumps",), needs=("card:C",), timeout_s=600)
def switches(ctx):
    """The debug room's switches: Battle OFF means no encounter while walking (with encounters
    on every check), Battle ON brings them back; Get Bullets adds to the bag."""
    card = ctx.card("C")
    s = ctx.session(cards=[card], ini=FAST_BATTLES)
    flows.load_save(s, flows.slot_of(card))
    bag0 = s.command("bag").split()[2]
    debugroom.give(s, "Get Bullets")
    bag1 = s.command("bag").split()[2]
    ctx.expect(bag1 != bag0, f"Get Bullets changed the bag: {bag0} -> {bag1}")
    debugroom.battles(s, on=False)
    places.warp(s, 87)                                  # back to save C's park, where encounters come
    try:
        walk_into_battle(s, budget=3000)
        ctx.expect(False, "an encounter with Battle OFF")
    except GameError:
        pass
    debugroom.battles(s, on=True)
    places.warp(s, 87)
    walk_into_battle(s, budget=8000)
    ctx.note("Battle OFF: no encounter in 3000 frames; Battle ON: one came")
