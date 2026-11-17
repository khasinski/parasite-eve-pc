"""Getting somewhere (warp, find people, place Aya, script variables) and scenario saves, end to
end. Ported from pe-native-debug's places_check; the scenario it makes stays in its own folder."""
from __future__ import annotations

import math

from pedebug import flows, places
from pedebug.game import GameError
from pedebug.runner import check

WAYNE_JUNK_AT = 0x1A98            # Wayne's Junk count in his script (the disc's own offset)


@check("places-warp-find-place", tags=("diag", "places"), needs=("card:C",), timeout_s=420)
def warp_find_place(ctx):
    """From save C: variables, warp refusals, warp to Wayne's room keeping the story, find him by
    his script, stand next to him, place Aya, warp with a story value, save a scenario there and
    reopen it."""
    card = ctx.card("C")
    s = ctx.session(cards=[card], name="warp")
    flows.load_save(s, flows.slot_of(card))
    s.wait_stable(60)
    start = places.room(s)
    ctx.require(start["map"] > 0 and start["free"], f"room: {start}")
    v = places.get_var(s, 300)
    places.set_var(s, 300, v ^ 0x5A)
    ctx.expect(places.get_var(s, 300) == v ^ 0x5A, "set-var / get-var")
    places.set_var(s, 300, v)
    try:
        s.command("warp 1000")
        ctx.expect(False, "warp to a map that doesn't exist is refused")
    except GameError:
        pass
    mark = s.events.mark()
    here = places.warp(s, 48)
    ctx.expect(here["map"] == 48 and here["story"] == start["story"], f"warp 48 keeps the story ({here['story']:X})")
    ctx.expect(s.events.find("scene_loaded", since=mark, map=48), "the event log has the new room")
    wayne = places.find_person(s, places.WAYNE_JUNK_COUNT)
    off = [o for i, o in places.find_script(s, *places.WAYNE_JUNK_COUNT) if i == wayne][0]
    ctx.expect(off == WAYNE_JUNK_AT, f"find-script: Wayne is actor {wayne}, Junk count at +{off:X}")
    places.place_near(s, wayne)
    w = [a for a in places.actors(s) if a["index"] == wayne][0]
    aya = [a for a in places.actors(s) if a["player"]][0]
    d = math.dist((aya["x"], aya["z"]), (w["x"], w["z"]))
    ctx.expect(abs(d - 0x180000) < 0x8000, f"place-near: Aya {d / 65536:.1f} units from Wayne")
    places.place(s, aya["x"] + 0x100000, aya["z"])
    ctx.expect([a for a in places.actors(s) if a["player"]][0]["x"] == aya["x"] + 0x100000, "place X Z")
    try:
        s.command("warp 48")
        ctx.expect(False, "warp to the room Aya is in is refused")
    except GameError:
        pass
    places.warp(s, 47)
    here = places.warp(s, 48, story=0x1D0)
    ctx.expect(here["story"] == 0x1D0, "warp with a story value sets it")
    root = ctx.dir / "scenarios"
    places.save_scenario(s, "check_m048", note="places check: map 48, story 1D0", root=root)
    s2 = places.open_scenario(ctx.cfg.exe, ctx.dir / "reopen", "check_m048", root=root,
                              disc1=ctx.cfg.disc1, disc2=ctx.cfg.disc2)
    ctx.sessions.append(s2)
    r = places.room(s2)
    ctx.expect(r["map"] == 48 and r["story"] == 0x1D0, f"the scenario reopens in map {r['map']}, story {r['story']:X}")
