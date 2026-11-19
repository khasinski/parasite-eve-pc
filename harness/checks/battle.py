"""Real battles from save C with encounters on every check: a win, and a loss back to the title."""
from __future__ import annotations

from pedebug import flows
from pedebug.runner import check

from ._common import FAST_BATTLES, walk_into_battle


@check("battle-win", tags=("diag", "battle"), needs=("card:C",), timeout_s=480)
def battle_win(ctx):
    """Walk into an encounter and attack until it ends; Aya survives and control comes back."""
    card = ctx.card("C")
    s = ctx.session(cards=[card], ini=FAST_BATTLES)
    flows.load_save(s, flows.slot_of(card))
    s.events.wait("scene_loaded", since=0, timeout=600)
    start = walk_into_battle(s)
    st = s.battle()
    ctx.expect(st["active"], "battle-state says active")
    ctx.expect(len(st["enemies"]) > 0, "enemies listed")
    ctx.fact("enemies", ", ".join(f"{e['type']}.{e['sub']} hp {e['hp']}" for e in st["enemies"]))
    mark = s.events.mark()
    end = None
    for _ in range(400):                                    # tap Cross: attack whenever the gauge is full
        s.press("cross", hold=4, after=26)
        end = s.events.find("battle_ended", since=mark)
        if end:
            break
    ctx.require(end is not None, f"the battle never ended: {s.state.short()}")
    ctx.expect(not end.get("left"), "the battle ended in the field, not by leaving it")
    ctx.expect(end["hp"] > 0, f"Aya survived (hp {end['hp']})")
    s.events.expect_none("player_down", since=mark)
    s.events.wait("control", since=mark, timeout=1200)
    ctx.fact("battle length", f"{end['vblank'] - start['vblank']} VBlanks")
    ctx.expect(not s.battle()["active"], "battle-state inactive after")


@check("battle-loss", tags=("diag", "battle"), needs=("card:C",), timeout_s=720)
def battle_loss(ctx):
    """At 1 HP, stand in an encounter: Aya goes down (a revive may intervene), the field loop
    ends for the title (battle_ended left, to_title), and the title menu comes up."""
    card = ctx.card("C")
    s = ctx.session(cards=[card], ini=FAST_BATTLES)
    flows.load_save(s, flows.slot_of(card))
    s.events.wait("scene_loaded", since=0, timeout=600)
    s.command("set-stat hp 1")
    walk_into_battle(s)
    mark = s.events.mark()
    s.events.wait("player_down", since=mark, timeout=6000, step=30)
    left = s.events.wait("to_title", since=mark, timeout=12000, step=60)
    ended = s.events.find("battle_ended", since=mark)
    ctx.expect(ended and ended.get("left"), "battle_ended with left before to_title")
    ctx.expect(ended and ended.get("hp") == 0, "HP 0 at the end")
    ctx.fact("downs", len([e for e in s.events.since(mark) if e["event"] == "player_down"]))
    ctx.fact("left from map", left.get("from_map"))
    s.events.wait("title", since=mark, timeout=15000, step=120)
