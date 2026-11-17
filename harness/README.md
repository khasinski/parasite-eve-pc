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
| `diag [--quick]` | the `diag` checks (several minutes, in parallel); `--quick`: boot and field only |
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
