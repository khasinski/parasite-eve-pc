"""Challenges (opt-in mod): a Hardcore run starts from New Game, locks, and is saved with the game;
a game over ends it for good. Headless, New Game takes the challenge rows as set (pe_ch_start: no picker without a window)."""
from __future__ import annotations

from pedebug import card as pc
from pedebug import flows, places
from pedebug.runner import check

from ._common import FAST_BATTLES, through_dialogue, walk_into_battle

CH_HARDCORE = 5
ENCOUNTER_MAP = 87             # Central Park: random encounters, from a fresh New Game too
HISTORY = "pe_challenges.ini"  # the per-run history, in the mods' settings folder (pe_ch_history.c)
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


@check("challenge-hardcore-death", tags=("challenges",), needs=("mod:pe_challenges",), timeout_s=600)
def hardcore_death(ctx):
    """A Hardcore run's game over ends it for good: at 1 HP in an encounter Aya falls, the run's
    history line becomes "fallen" before the game over, and loading a save from the run shows the
    "This Hardcore run has ended" notice and goes back to the title instead of playing on."""
    s = ctx.session(cards=None, ini=FAST_BATTLES + "Challenges/Hardcore=1\n")
    flows.new_game(s)
    through_dialogue(s, what="control after the opening")
    run = s.live("pe_challenges", "run")
    ctx.require(run is not None and CH_HARDCORE in [c["id"] for c in run["active"]], "a Hardcore run")
    run_id = f"{run['run_id']:08x}"
    for name, data in s.save_files(1).items():          # a save of the run, on the card for later
        path = s.dir / "bu00" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    places.warp(s, ENCOUNTER_MAP)
    flows.in_field(s)
    s.command("set-stat hp 1")
    bag = s.command("bag")
    ctx.require(",12," not in f",{bag.split()[2]},", f"no Revive in the bag: {bag}")
    walk_into_battle(s)
    mark = s.events.mark()
    s.events.wait("player_down", since=mark, timeout=6000, step=30)
    s.events.wait("to_title", since=mark, timeout=12000, step=60)
    history = (s.dir / HISTORY).read_text()
    line = next((l for l in history.splitlines() if l.startswith(run_id)), "")
    ctx.expect("st=fallen" in line, f"the run is fallen in {HISTORY}: {line or history}")
    s.events.wait("title", since=mark, timeout=15000, step=120)
    s.advance(1500)                                      # as after a soft reset: let the title fade in
    loaded = s.events.mark()
    s.press("start")
    s.advance(100)
    s.press("cross")                                     # Continue
    s.wait(lambda st: flows._menu_sel(st) == 36, timeout=600, what="Select Slot")
    s.advance(60)
    s.press("cross")
    s.wait(lambda st: flows._menu_sel(st) == 37, timeout=600, what="the file list")
    s.advance(60)
    s.press("cross")
    s.wait(lambda st: flows._menu_sel(st) == 40, timeout=900, what='"File loaded"')
    s.advance(60)
    s.press("cross")
    notice = s.events.wait("dialogue_waiting", since=loaded, timeout=3000, step=30)
    ctx.expect("Hardcore run has ended" in notice.get("text", ""), f"the dead run's notice: {notice.get('text')}")
    s.press("cross", hold=4, after=40)
    s.events.wait("to_title", since=loaded, timeout=3000, step=30)
    ctx.fact("history", line)
