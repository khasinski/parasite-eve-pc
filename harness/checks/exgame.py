"""EX Game: a finished game's Clear Data save starts a new round from the title's EX Game row.
The row (above New Game) shows only when a card holds clear data; Continue lists clear saves but
won't load them (see flows.ex_game). Fixture J is a real Clear Data save ("EX Round 2").

When the mods reset: the EX Game's start command (INV_CMD_START_NEW_GAME) runs at the end of the
credits, before the Clear Data save is offered, and the host sends it to the mods as New Game (kind
EX): every record resets (pe_save.h), BP Overhaul carries its ranks per its F1 setting. So the clear
save already holds the reset data, and loading it changes nothing more."""
from __future__ import annotations

from pedebug import card as pc
from pedebug import debugroom, flows, places
from pedebug.runner import check

from ._common import ALL_MODS, through_dialogue

OPENING_MAP = 2
OPENING_STORY = 9              # var 74 in the opening room, as for a New Game


def _round_start(ctx, s):
    """In control in the EX round's opening: the room, level 1 and the game's carryover."""
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
    ctx.fact("carried", f"BP {aya['bp']}, bag {','.join(bag)}")
    return aya


@check("ex-game-start", tags=("story", "saves"), needs=("card:J",), timeout_s=420)
def ex_game_start(ctx):
    """EX Game from Clear Data save J, all mods on: the new round starts in the opening room at
    level 1 (45 HP) with the finished game's Bonus Points and bag carried over; the mods run, and a
    save made in the round writes a valid record container."""
    card = ctx.card("J")
    ctx.require("Clear Data" in pc.Card.load(card).title, f"save J is clear data ({pc.Card.load(card).title})")
    s = ctx.session(cards=[card], ini=ALL_MODS)
    flows.ex_game(s, flows.slot_of(card))
    _round_start(ctx, s)
    for mod, provider in (("pe_economy", "economy"), ("pe_shops", "requisition")):
        ctx.expect(s.live(mod, provider) is not None, f"{mod} is running with live state")
    _, block = s.save_block(1)
    saved = pc.parse_tail(block[pc.TAIL_AT:])
    ctx.expect(saved.kind == "container" and saved.container_ok,
               f"a save in the round writes a valid container ({saved.kind})")


@check("ex-game-from-ending", tags=("jumps", "ex"), needs=("card:C",), timeout_s=1500)
def ex_game_from_ending(ctx):
    """The whole EX path from save C (Junk and pocketed keys): finish the game (the debug room's
    Theatre Epilogue, the credits), save the Clear Data the game offers, then start the EX Game
    from it. The mods reset at the EX start, before that save: it holds no Junk, no pocketed keys
    and a fresh challenge run, and the new round starts with them so. Several minutes."""
    card = ctx.card("C")
    old = pc.Card.load(card).mods()
    junk0 = pc.decode(old.get("pe_economy.loot"))["junk"] if old.get("pe_economy.loot") else 0
    keys0 = pc.decode(old.get("pe_qol.keys"))["count"] if old.get("pe_qol.keys") else 0
    ctx.require(junk0 > 0 or keys0 > 0, f"save C has mods' data to reset (Junk {junk0}, keys {keys0})")
    s = ctx.session(cards=[card], ini=ALL_MODS, name="ending")
    flows.load_save(s, flows.slot_of(card))
    clear = debugroom.finish_game(s)
    log = s.mods_log()
    if any(line.startswith("pe_bp:") for line in log):          # opt-in mod: built with PE_EXTRA_MODS
        ctx.expect(any("EX Game:" in line for line in log), "BP Overhaul heard the EX start (New Game, kind EX)")
    ext = s.dir / "bu00" / ".pe-ext"
    files = {f"bu00/.pe-ext/{f.name}": f.read_bytes() for f in ext.glob(clear.name + ".*")} if ext.exists() else {}
    s.quit()
    made = pc.Card.load(clear)
    ctx.expect("Clear Data" in made.title and made.crc_ok, f"a Clear Data save with a good CRC: {made.title}")
    md = made.mods()
    ctx.expect(md.kind == "container" and md.container_ok, f"its mods' data is a valid container ({md.kind})")
    loot, keys = md.get("pe_economy.loot"), md.get("pe_qol.keys")
    ctx.expect(loot is not None and pc.decode(loot)["junk"] == 0, f"no Junk in the clear save (C had {junk0})")
    ctx.expect(keys is None or pc.decode(keys)["count"] == 0, f"no pocketed keys in the clear save (C had {keys0})")
    ctx.fact("clear save", f"{made.title}; records {', '.join(r.name for r in md.records)}")

    s2 = ctx.session(cards=[clear], ini=ALL_MODS, name="ex", extra_files=files)
    flows.ex_game(s2, flows.slot_of(clear))
    _round_start(ctx, s2)
    economy = s2.live("pe_economy", "economy")
    ctx.expect(economy is not None and economy["junk"] == 0, f"the EX round starts with no Junk: {economy}")
