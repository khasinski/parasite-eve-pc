"""EX Game: a finished game's Clear Data save starts a new round from the title's EX Game row.
The row (above New Game) shows only when a card holds clear data; Continue lists clear saves but
won't load them (see flows.ex_game). Fixture J is a real Clear Data save ("EX Round 2")."""
from __future__ import annotations

from pedebug import card as pc
from pedebug import flows, places
from pedebug.runner import check

from ._common import ALL_MODS, through_dialogue

OPENING_MAP = 2
OPENING_STORY = 9              # var 74 in the opening room, as for a New Game


@check("ex-game-start", tags=("story", "saves"), needs=("card:J",), timeout_s=420)
def ex_game_start(ctx):
    """EX Game from Clear Data save J, all mods on: the new round starts in the opening room at
    level 1 (45 HP) with the finished game's Bonus Points, weapon and bag carried over; the mods
    run, and a save made in the round writes a valid record container."""
    card = ctx.card("J")
    ctx.require("Clear Data" in pc.Card.load(card).title, f"save J is clear data ({pc.Card.load(card).title})")
    s = ctx.session(cards=[card], ini=ALL_MODS)
    flows.ex_game(s, flows.slot_of(card))
    ctx.expect(s.events.find("save_loaded") is not None, "the clear save loaded (save_loaded event)")
    through_dialogue(s, what="control in the EX round's opening")
    room = s.command("room")
    ctx.expect(f"map={OPENING_MAP} " in room, f"in the opening room: {room}")
    ctx.expect(places.get_var(s, 74) == OPENING_STORY, f"story at the opening (var 74 = {OPENING_STORY})")
    aya = s.battle()["player"]
    ctx.expect(aya["level"] == 1 and aya["max_hp"] == 45, f"level 1, 45 HP: {aya}")
    ctx.expect(aya["bp"] > 0, f"the finished game's Bonus Points carried over ({aya['bp']})")
    bag = s.command("bag").split()[2].split(",")
    ctx.expect(any(x != "0" for x in bag), f"the bag carried over: {bag}")
    for mod, provider in (("pe_economy", "economy"), ("pe_shops", "requisition")):
        ctx.expect(s.live(mod, provider) is not None, f"{mod} is running with live state")
    _, block = s.save_block(1)
    saved = pc.parse_tail(block[pc.TAIL_AT:])
    ctx.expect(saved.kind == "container" and saved.container_ok,
               f"a save in the round writes a valid container ({saved.kind})")
    ctx.fact("carried", f"BP {aya['bp']}, bag {','.join(bag)}")
