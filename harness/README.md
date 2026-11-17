# Headless harness (`harness/pe.py`)

One command for setting up, checking and diagnosing the native port headless. It builds on the
game's debug channel (`PE_DEBUG_CONTROL`, [docs/DEBUG_CONTROL.md](../docs/DEBUG_CONTROL.md)): the event log,
`ui-state`, `battle-state`, `mod-state` with mods' live state, and `snapshot`.

## First time

From this folder (`harness/`), with Python 3.10 or newer (nothing to install):

```
python pe.py build --use      # optional: builds this checkout into build-harness/ (mods + opt-in mods)
python pe.py setup --disc1 "<...> (Disc 1).cue" --disc2 "<...> (Disc 2).cue"
                              # finds the newest build with the debug channel; remembers the discs
python pe.py doctor --boot    # checks all of it and starts the game twice (title, then a save)
python pe.py diag             # the diagnostics suite; report in runs/diag/<time>/report.md
```

`setup` writes `harness.json` (git-ignored). Override any value with `--exe`, `--fixtures`,
`--disc1`/`--disc2`, or the environment (`PE_EXE`, `PE_DISC1`, `PE_DISC2`, `PE_FIXTURES`,
`PE_RUNS`). Builds are looked for in this repository's `build*` folders; add other places to
`search_roots` in `harness.json`.

`pe.py build [CHECKOUT] --use` configures and builds a checkout (default: this one) into
`build-harness/` with mods and the opt-in mods (`pe_bp;pe_challenges`), using CMake, Ninja and a
compiler from PATH or `--toolchain DIR`, and points `harness.json` at it.

Most checks start from your own saves: see [fixtures/README.md](fixtures/README.md). Without
them they are skipped (with the reason), not failed.

## Commands

| | |
|---|---|
| `doctor [--boot]` | Python, config, build (debug control, opt-in mods, not locked by a running game), discs, fixture cards, scenario saves, disk space. Every FAIL line says how to fix it. `--boot` also runs `boot-title` and `field-commands` |
| `list [-v] [--tag T]` | every check, its tags, and what it is missing here (it is then skipped, not failed) |
| `diag [--quick\|--full]` | the `diag` checks (about a minute, in parallel); `--quick`: boot and field only; `--full`: every check (saves, mods on/off, economy, challenges) |
| `run NAME... [--tag T]` | chosen checks (`battle-*` matches by prefix); `--repeat N` runs each N times to find flaky ones |
| `report [--suite S]` | the latest run's report |
| `play` | starts the game in a window with the debug channel in `runs/live-ctl` |
| `live state\|ui\|battle\|events\|watch\|snapshot [NAME]\|mod ID\|cmd "..."` | asks a running game (`play`, or any game started with `PE_DEBUG_CONTROL`; `--dir` picks the folder). `watch` prints events as they happen |
| `capture NAME [--note T]` | saves the playing game's spot as a scenario save (`fixtures/scenarios/NAME/`): stand in free field first |
| `scenarios` | the scenario saves |
| `build [CHECKOUT] [--use]` | builds a game checkout for the harness (see above) |

`--jobs N` sets how many games run at once (default: half the cores, at most 4).

## What a run leaves

`runs/<suite>/<time>/` holds `report.md`, `results.json` and `junit.xml` (for CI), and a folder
per check with each game session's own folder (cards written, logs, pictures, `ctl/` with the
event log and command answers, `inputs.jsonl`: every advance sent).

A check that fails or errors also gets `failure/`:

| File | |
|---|---|
| `why.txt` | the message and every failed expectation |
| `sN-snapshot.json` | the game's snapshot at the failure: room, ui-state, battle, bag, actors, mods' live state, last 32 commands |
| `sN-frame.png` | the last frame |
| `sN-events.jsonl` | the whole event log |
| `sN-states.jsonl` | the last 60 control states |
| `sN-inputs.jsonl` | every input sent, by VBlank |
| `sN-commands.txt`, `sN-stderr.txt`, `sN-pe_mods.log` | debug answers, the game's stderr, the mods' log |

A check that runs past its time limit has its games killed; what they left is still captured.

## Writing a check

A module under `checks/` (names starting `_` are helpers):

```python
from pedebug import flows
from pedebug.runner import check
from ._common import FAST_BATTLES, walk_into_battle

@check("my-check", tags=("diag", "battle"), needs=("card:C", "mod:pe_bp"), timeout_s=300)
def my_check(ctx):
    """One line on what it proves (shown by list -v)."""
    s = ctx.session(cards=[ctx.card("C")], ini=FAST_BATTLES)
    flows.load_save(s, flows.slot_of(ctx.card("C")))
    mark = s.events.mark()
    walk_into_battle(s)
    end = s.events.wait("battle_ended", since=mark, timeout=12000)
    ctx.expect(end["hp"] > 0, "Aya survived")       # soft: recorded, the check goes on
    ctx.require(s.battle()["active"] is False, "battle over")   # hard: the check ends
    ctx.fact("hp left", end["hp"])                  # shown in the report
```

`needs`: `card:X` (a fixture card), `scenario:NAME` (a scenario save), `mod:ID` (built into the
game). A check whose needs are missing is skipped with the reason.

Waiting: prefer `s.events.wait(name, since=mark, **fields)` over fixed frame counts. Fields match
exactly, by a callable, or for `text` by substring. `s.events.find` / `last` / `since` /
`expect_none` read without stepping. Wait for `scene_loaded`, not `room`, before acting in a new
room.

Reading the game: `s.ui()`, `s.battle()`, `s.mod_state(id)`, `s.live(id, provider)`,
`s.snapshot(name)`, `s.command("...")`, plus `pedebug.places` (warp, place, find people,
scenario saves) and `pedebug.flows` (title, loading, talking, saving, soft reset, New Game).

## Jumping to a story point

The game's own debug room (map 360) is still on the disc. `pedebug.debugroom` drives it from any
loaded save:

```python
from pedebug import debugroom
debugroom.jump(s, "Warehouse", strong=True)   # chapter start, in control; strong: God mode + level 99
debugroom.battles(s, on=False)                 # random encounters off (the game's debug switch)
debugroom.give(s, "Get Bullets")               # also Poisons, Status Item, Tools, Tune up Items, weapon
debugroom.copcar(s, "Theatre-NYPD")            # the car rides between places
```

`debugroom.STORY` lists the 31 story points (Opening ... Chrysler 77F) and `STORY_DAY` their days.
A jump runs the chapter's own start: its first room, its opening event (watched; `watch=False`
skips it, faster but the event's script doesn't run), fights it opens with (attacked), Enter Name,
the day title card, and any save offered (declined), until Aya has control. Aya keeps her level,
HP and bag: `strong=True` turns on the Cheats mod's God mode and infinite ammo and PE first (late
chapters open with fights a low-level Aya loses).

Five story points don't reach free field yet (`STORY_LIMITS`; `jump` refuses them unless
`force=True`): Theatre Exterior and NYPD -Day1- stop in the NYPD office in event mode; Cruiser's
opening fight beats Aya even with God mode; Port and Theatre Epilogue run into the ending and the
title.

Limits: the game's story day (`room`'s `day`) isn't updated by a jump; story progress (variable 74)
is whatever the chapter's start sets (some leave the save's value), so a check needing a story value
sets or checks it itself; a jump starts a chapter, so a moment inside one still needs playing
forward or a scenario save. Several chapters that are cutscenes only (Street, Aircraft Carrier,
Helicopter) play through into the next one's first room.

## The checks

| Check | Proves |
|---|---|
| `boot-title` | the build boots headless to the title |
| `field-commands` | save C loads; every debug command answers consistently; the default mods' live state |
| `bp-challenges-state` | the opt-in mods' live state (needs them built) |
| `battle-win` | a real encounter: battle-state, attacking, battle_ended with HP left, control back |
| `battle-loss` | 1 HP: player_down, the game over back to the title (battle_ended left, to_title, title) |
| `phone-save-call` | a phone save to slot B and a call: save_committed, the card written, the call's pages, heard calls |
| `wayne-menu` | Wayne's menu: the economy's extra rows, the cursor through ui-state, Cancel's row |
| `reset-new-game` | soft reset to the title, then New Game: a fresh Aya, no Junk |
| `places-warp-find-place` | variables, warp (and its refusals), finding Wayne by his script, placing Aya, a scenario saved and reopened |
| `story-jump-one` | the debug room's story menu: save C to Warehouse, in control in its first room |
| `requisition-buy` | Wayne > Workshop > Requisition: Cancel buys nothing, OK spends the price and carries the item |

`--full` adds (tags `saves`, `mods`, `economy`, `challenges`; record lists and the mod list come from the build, so new mods are covered):

| Check | Proves |
|---|---|
| `save-old-format-upgrades` | an old per-mod-block save loads and saves as the record container with the same values; the window blocks stay |
| `save-round-trip` | a current save (records and companion) saves again byte for byte |
| `save-official-window` | a build without the container wrote the key pocket: it wins, the rest stays |
| `save-damaged-record` | one overwritten record resets alone |
| `save-newer-format-refused` | a newer container format is refused at the file list (its companion can't be read), the card untouched |
| `save-vanilla-defaults` | a save without mods' data starts every mod from defaults |
| `save-phone-matches-block` | a phone save holds the same mods' data as save-block |
| `save-new-game-fresh` | New Game after a loaded save carries no Junk, unlocks, Tools or pocketed keys |
| `mods-each-off` | every mod in the build off on its own: every record and companion section kept (pe_stacks off: its stacked copies handed to the bag or still counted) |
| `mods-qol-off-returns-keys` | pe_qol off hands the pocketed keys to the bag; on again pockets them |
| `mods-all-off-and-back` | All mods off and on again: nothing lost |
| `mods-mixed-sequence` | five mixes in a row, saving each time: it ends as it started |
| `mods-full-bag-qol-off` | pe_qol off with one free slot: one key back, two kept, nothing lost |
| `wayne-handin` | Discard junk with Quick hand-in off and on: same result, much faster |
| `challenge-hardcore-start` | New Game with Hardcore: the run is active, locked and saved |
| `story-jumps` (tag `jumps`) | the 26 supported story points from save C: each lands with control in the room `STORY_MAP` lists (about 3 minutes, 4 at a time) |
| `debug-room-switches` (tag `jumps`) | Get Bullets changes the bag; Battle OFF stops encounters, Battle ON brings them back |

### Known game bugs

A check can mark an expectation a known, reported game bug breaks (`ctx.known_bug(cond, what,
bug)`): the run reports it as `known` (in the console, `report.md` and `results.json`) instead of
failing, and says so once it holds again so the marker can go. One is marked today, found by these
checks: **a damaged mods' container makes the whole save unloadable** (`save-damaged-record`), even
with its companion link intact: `pe_save_peek` and `pe_save_prepare_load` refuse any damaged
container, so the load shows "Data is unreadable" instead of the documented recovery (reset the
damaged record, keep the rest). Fixed by PR 271; the check reports when the marker can go.

Not covered yet: a Hardcore death (needs a fight without a Revive from a new game) and the EX
Game carryover (needs a cleared save).
