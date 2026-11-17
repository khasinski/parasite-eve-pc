# Debug control

A small command channel for test scripts, in the mod host (`mods/host/pe_debug_control.c`). It
lets a script ask the running game for things a player would otherwise have to walk somewhere for:
go to a room, find a person, stand Aya next to them, read or set a script variable, or save the
game as it is now.

It is **off unless the environment variable `PE_DEBUG_CONTROL` names a folder**. Nothing shows in
F1 and players never meet it. Being part of the host, it answers whichever mods are on or off
(All mods off included). The native steal-result hook only observes the completed inventory
transfer. No debug commands or tracing run unless this channel is explicitly enabled.

## How it talks

### Code layout

The game side is split into separately compiled modules. All are host features, independent of
the gameplay mod list, and remain dormant until `PE_DEBUG_CONTROL` enables the channel.

| File under `mods/host/` | Responsibility |
|---|---|
| `pe_debug_control.c` | File polling, command matching, replies, JSON string escaping, frame counter and command history |
| `debug/pe_debug_field.c` | Rooms, warps, actor/script lookup, positioning and script variables |
| `debug/pe_debug_save.c` | Card-block/companion export and saved mod tail |
| `debug/pe_debug_battle.c` | Battle-state snapshots |
| `debug/pe_debug_rng.c` | Native RNG snapshot and restoration |
| `debug/pe_debug_setup.c` | Bag queries/filling, item grants, equipment and character setup |
| `debug/pe_debug_settings.c` | Registered mod values and runtime options |
| `debug/pe_debug_trace.c` | Trace enable/disable, battle hook subscriptions and frame observations |
| `debug/pe_debug_ui.c` | Native menu layer, open dialogue boxes and recently shown dialogue pages |
| `debug/pe_debug_snapshot.c` | One-file failure capture built from the other modules' writers |
| `debug/pe_debug_events.c` | Event log: rooms, dialogue, menus, battles, control, saves and loads, as they change |
| `debug/pe_debug_internal.h` | Private command/module contract and shared transport helpers |
| `pe_mod_state.c` / `pe_mod_state.h` | Mods' live-state providers (public API) and their JSON writer |

Each module owns its command table and private handlers. The core matches exact command
boundaries and passes arguments to the owning module. Add a command to the relevant module's
table without changing the dispatcher. A new module supplies a `PeDebugModule`, is listed in the
core's module array, and is added to `pe_debug_sources` in `mods/CMakeLists.txt`. That source list
is shared by the game and native regression test, so both link the same separate C files.
Optional module install/frame callbacks keep tracing's lifecycle out of the transport.

The command protocol and existing argument semantics are unchanged by this split.

### File protocol

The script writes one command per line to `<folder>/cmd` (write `cmd.tmp`, then rename it, so the
game never reads half a file). During gameplay the game claims it as `cmd.processing` every 10
frames, then removes that claimed file, and
appends one answer line per command to `<folder>/out`:

```
ok <command> <details>
err <command> <why>
```

It is a file rather than the control mode's stdin (`--control-at`) so it also works in a game
someone is playing: walk to a spot by hand, then save it as a test's starting point.
A game whose window pauses when unfocused answers once it runs again.

## Commands

| Command | Answer / effect |
|---|---|
| `ping` | `ok ping <frame>` |
| `room` | `ok room map=<N> name=<room name> token=<room id> story=<hex> day=<N> free=<0/1>`: map number (as in [ROOMS.md](ROOMS.md)), story progress (script variable 74), story day, free field |
| `warp MAP [STORY]` | go to map number `MAP` (or a room id `0x...`) through the field script's own room transfer, as `pe_warp` does; with `STORY` (hex), set story progress first. Free field only, and never the room Aya is in (that transfer never finishes; use `set-var 74`). Answers when the transfer starts: wait for `room` to name the new map |
| `actors` | `ok actors <i>:<type>.<sub>[*]@<x>,<z>,f<flags> ...`, in the order of the control-mode state's `actors`; `*` marks Aya |
| `find-script W [W ...]` | `ok find-script <i>@<offset> ...`: the actors whose script holds these consecutive words (hex), searched over the first 32 KB. Finds a person by what their script does |
| `place X Z [YAW]` | put Aya at `X`, `Z` (16.16 field units, as the state's `position`), facing `YAW` (0x1000 = a full turn) |
| `place-near I [DIST]` | put Aya `DIST` (default `0x180000`) from actor `I`, on her side of them, facing them |
| `get-var N` / `set-var N V` | script variable `N` (0..511), value in hex |
| `save-block [N]` | the card file a save to slot `N` (1 = A) would write now, built by the game's own `Save_BuildHeader` and `Save_BuildCardFile` (mods' records and CRC included), to `<folder>/block.bin`. Free field only. See below |
| `mods-tail` | the mods' save data as a save made now would hold it, to `<folder>/tail.bin` |
| `bag` | `ok bag <id>,<id>,...`: Aya's bag, slot by slot (hex, 0 = empty) |
| `fill-bag ID [LEAVE]` | put item `ID` (hex) in every empty bag slot but `LEAVE` |
| `battle-state` | one JSON object: active flag, battle frame/mode, target index, queue count, Aya HP/ATB/PE (16.16 raw)/BP/level, equipped weapon stats/mods and active enemy slots with identity, HP, status, position and stealable item |
| `rng-get` | `STATE FRAME INDEX HEX1042`: BIOS generator state, encounter frame seed, byte-generator cursor and all 521 state bytes |
| `rng-set STATE FRAME INDEX HEX1042` | restore exactly the state returned by `rng-get`; rejects malformed snapshots before mutation |
| `give ITEM` | add a hex base item ID (1..FF) through the native inventory path, including mod hooks; free field only |
| `equip BAG_SLOT` | equip existing weapon/armor in decimal bag slot and rebuild native stat/equipment caches; free field only |
| `set-stat level\|hp\|pe-raw\|bp VALUE` | decimal values; level 1..99 updates EXP and derived stats, HP/PE cannot exceed current maxima, BP 0..99999; free field only |
| `mod-state MOD_ID` | JSON of running state and registered F1 stored values/info rows; inactive callbacks are not run. A mod with live-state providers also gets `"live"`: `{"<provider>":{...}}` while running, `null` when not (see below) |
| `ui-state` | one JSON object: `menu` (native menu layer open, active mode, selected item, action submenu, memory-card dialog, save slot), `dialogue` (each open box: state `typing`/`waiting`, page, style, rect, text, choice count and cursor) and `recent_pages` (the last 8 pages a box opened, with frame, map and the text as drawn) |
| `events` | `ok events <seq>`: the sequence number of the last event written to `events.jsonl` (see below); read lines after it |
| `snapshot [NAME]` | write `snapshot[-NAME].json` in the control folder: room, ui-state, battle-state, bag, actors (position, yaw, flags), every running mod's live state and the last 32 commands and answers. Answers `ok snapshot <file>` |
| `ini-set PAGE/LABEL VALUE` | change a registered stored option in memory using its full INI key (spaces allowed). Range checked. Does not persist to disk. `restart_required=1` for Manage mods options: apply those in the next session's INI; this command does not start/stop mods |
| `trace on` / `trace off` | append events to `battle.jsonl` in the control folder; off initially |

### Dialogue and menus

`ui-state` reads the game's own dialogue boxes (`g_TextboxEntries`) and menu globals, so a test
can wait for a box to reach `waiting`, check its text, move the choice cursor with pad input and
confirm it landed. Text is decoded with the English glyph table (`pe_gametext.h`); control codes
show as `{XX}` and line breaks as ` / `. `recent_pages` is filled by a late `PE_HOOK_TEXT_PAGE`
handler (after language packs and mods replaced the page), then follows the open box's text each
frame, since some mods rewrite a box after it opened (Wayne's menu rows). It survives the box
closing.

The F1 Mods panel is modal: the game, and with it this channel, stops while it is open, so it is
not reported. Menus a mod draws in the field (Wayne's shop, the Requisition list) report through
that mod's live-state provider.

### Event log

While the channel is on, the game appends one JSON line to `<folder>/events.jsonl` whenever
something a test waits for changes, on the frame it changes (not at the 10-frame command poll):

```
{"seq":12,"frame":152,"vblank":9431,"event":"dialogue_waiting","box":0,"page":164,"choices":7,"text":"..."}
```

`seq` counts from 1 for the run, `frame` is the channel's field-frame count (as in `ping`) and
`vblank` the game's VBlank count (the control mode state's `vblanks`), so a headless runner can
wait for an event and know exactly where it happened. Send `events` first to learn the current
`seq`, then act and wait for a later line.

| Event | Fields | When |
|---|---|---|
| `started` | `map`, `free`, `battle` | first field frame after the channel starts |
| `scene_loaded` | `map` | a room finished loading, before its first frame (also the same room again) |
| `room` | `map`, `token`, `story` | the room token changed |
| `control` / `no_control` | | free field (Aya can move) began / ended |
| `menu_opened` / `menu_closed` | | the native menu layer |
| `paused` / `unpaused` | | the game's pause |
| `dialogue_opened` | `box`, `page`, `choices`, `text` | a box opened (or a box went on to another page) |
| `dialogue_waiting` | `box`, `page`, `choices`, `text` | the box finished typing and waits for a press; `text` is the full page as drawn |
| `dialogue_changed` | `box`, `page`, `choices`, `text` | an open box's text or choice count changed (a mod rewrote it, as the economy does Wayne's menu rows) |
| `dialogue_closed` | `box`, `page` (+ `choices`, `answer`) | for a choice, `answer` is the row the game's script receives, -1 if none. A mod that adds rows maps them back, so this can differ from the row on screen (Wayne's Cancel is 5 with Workshop shown) |
| `battle_started` / `battle_ended` | `hp` on end | battle flag on / off |
| `player_down` | | Aya's HP reached 0 in battle |
| `title` / `title_left` | | the title menu began running / the field ran again |
| `save_loaded` | | a card save's block was read into the game |
| `new_game` | | a new game started |
| `save_committed` | | the save screen wrote and published a card save |

Only changes are written, so an idle game writes nothing. The command channel itself only runs
on field frames; at the title it answers once the field runs again, but `title` and
`save_loaded` are still logged.

### Mods' live state

A mod registers providers from its init (`mods/host/pe_mod_state.h`):

```c
static void wallet_state(PeStateWriter *w, void *user) {
    pe_state_int(w, "junk", junk);
    pe_state_begin_list(w, "pending");
    /* ... */
    pe_state_end(w);
}
pe_mod_state_provider("economy", wallet_state, NULL);
```

Providers only read. They run only from `mod-state` and `snapshot`, only while the mod is
running, and inside `pe_mod_call_begin` so a crash in one names it. Registration costs a table
entry and nothing else when debug control is off. `pe_economy` provides `economy`: wallet Junk,
Wayne's hand-in, store status, the active path, the saved policy and its pending rewards.

### Failure capture

On a timeout or a failed check, a test runner sends `snapshot <test-name>` and attaches the file
to its report. The file is written to `<name>.tmp` and renamed, so a reader never sees half of it.
Crashes are already covered by the game's crash report (`logs\crash-<date>.txt`: stacks, game
state, the mods and their save data); the channel adds nothing there.

### Battle traces and deterministic runs

Every event is one JSON line, timestamped in game frames. Current events:

- `enemy_hp_commit`: HP before/after and actual subtraction (including overkill), native reaction
  code and critical flag. Records the final mod-adjusted hit, not a guessed damage formula.
- `hp_loss_admitted`: native incoming HP-loss admission after mod filters, with source number.
  This is **before** the caller's store/clamp, not a claim that all the admitted loss was committed.
- `player_hp_changed`: observed HP change between field frames. Multiple changes in one frame
  may be combined; use it with admissions when diagnosing incoming damage.
- `steal`: actual inventory-transfer result and item, including inventory rejection. A failed
  random steal roll does not reach this event.
- `rewards_offered`: native EXP and proposed drop list at reward dispatch. Offers can differ
  from what a mod settles or what the bag accepts.
- `resources`: initial BP/EXP/level/battle state and subsequent changes.
- `player_action`, `spell`: action/spell dispatch observations.

For reproducibility, restore RNG at the same battle phase and use the same input/frame schedule.
This restores both **native** random streams, not arbitrary mod-private RNG or the rest of a
running game's state. The command takes effect at the host's next 10-frame poll; the game can
advance after that boundary. Use identical command schedules for comparisons.

### Remaining battle work

`battle N` is not implemented. `Battle_StartEncounter(mode)` accepts a **mode**, not an enemy
group ID. Room scripts choose and create enemies and initialize encounter state. A generic
chosen-group command needs a validated encounter loader/catalog; directly passing N to that
function would produce invalid tests. Bosses still require scenario saves.

Dedicated critical-roll events (including misses), challenge-specific trigger/settlement
events, runtime hot-start/hot-stop of mods, arbitrary memory peek/poke, Junk setup,
equipment-mod replacement, and movie/dialogue skipping remain outside this implementation.
`mod-state` is the registered F1 view plus whatever live state a mod chooses to provide, not a
dump of every private variable. Live-state providers exist for `pe_economy` so far.

### A warp gets the room, not the scene

Placement refreshes Aya's floor triangle so movement does not keep using the triangle from her
previous position. Choose a point on the room's walkable mesh. `find-script` only scans bytes
inside the loaded task payload and stops at the next task, rather than reading a fixed 32 KiB.

`warp` loads the room and can set story progress. Who stands in the room, and what they say, also
follows the story day and scene flags, which a warp doesn't touch. For example, warping to Wayne's
workbench (map 48) loads his actor and script, but he is never shown unless the save was already at
that point in the story. For a person or an event, start the test from a save made there.

### save-block and the companion file

Some mods' save data lives in a companion file beside the card (`.pe-ext/<card>.<generation>.pex`,
`mods/host/pe_extension_store.c`). A card without its companion loads as "Data is unreadable".
When the save has one, `save-block` answers `ok save-block <card> companion=<name>` and copies it
to `<folder>/companion.pex`; put it at `.pe-ext/<name>` beside the card. The companion the save
staged beside the real card is then dropped, so a test's save leaves nothing in the card folder.

## A small client

```python
import pathlib, time

class DebugControl:
    def __init__(self, folder):
        self.dir = pathlib.Path(folder)

    def ask(self, cmd, timeout=30.0):
        out = self.dir / "out"
        seen = len(out.read_text().splitlines()) if out.exists() else 0
        (self.dir / "cmd.tmp").write_text(cmd + "\n")
        (self.dir / "cmd.tmp").replace(self.dir / "cmd")
        end = time.time() + timeout
        while time.time() < end:
            lines = out.read_text().splitlines() if out.exists() else []
            for line in lines[seen:]:
                if line.split()[1:2] == [cmd.split()[0]]:
                    if line.startswith("err"):
                        raise RuntimeError(line)
                    return line
            time.sleep(0.05)
        raise TimeoutError(cmd)

# PE_DEBUG_CONTROL=/tmp/pe-ctl ./parasite-eve-pc ...
ctl = DebugControl("/tmp/pe-ctl")
print(ctl.ask("room"))
ctl.ask("warp 48")
wayne = ctl.ask("find-script 1040A8 0 14").split()[2].split("@")[0]   # his Junk count
ctl.ask(f"place-near {wayne}")
```

Headless, the control mode (`--headless --control-at 1`) advances the game; send a command, then
advance until its answer appears.

## Tests

`mod-debug-control` (`tests/pe_mod_debug_control_test.c`) runs every command against a stand-in
game, through the real `cmd` / `out` files.
