"""Challenges (opt-in mod): a Hardcore run starts from New Game, locks, and is saved with the game.
Headless, New Game takes the challenge rows as set (pe_ch_start: no picker without a window)."""
from __future__ import annotations

from pedebug import card as pc
from pedebug import flows
from pedebug.runner import check

from ._common import through_dialogue

CH_HARDCORE = 5
HARDCORE_INI = "Manage mods/All mods=1\nChallenges/Hardcore=1\n"


@check("challenge-hardcore-start", tags=("challenges",), needs=("mod:pe_challenges",), timeout_s=420)
def hardcore_start(ctx):
    """New Game with the Hardcore row on: the run is active with Hardcore, locked, has a run id,
    and a save made in it holds the pe_challenges.run record."""
    s = ctx.session(cards=None, ini=HARDCORE_INI)
    flows.new_game(s)
    through_dialogue(s, what="control after the opening")
    run = s.live("pe_challenges", "run")
    ctx.require(run is not None, "pe_challenges is running with live state")
    ids = [c["id"] for c in run["active"]]
    ctx.expect(CH_HARDCORE in ids, f"Hardcore is on: {run['active_names']}")
    ctx.expect(run["locked"], "the run is locked (Hardcore can't be lowered)")
    ctx.expect(run["run_id"] != 0, "the run has an id")
    ctx.expect(not run["assisted"], "not assisted")
    _, block = s.save_block(1)
    names = [r.name for r in pc.parse_tail(block[pc.TAIL_AT:]).records]
    ctx.expect("pe_challenges.run" in names, f"the save holds the run record: {names}")
    ctx.fact("run", f"{run['run_id']:08X}, {run['active_names']}")
