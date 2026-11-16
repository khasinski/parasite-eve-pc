# Debug control

A small command channel for test scripts, in the mod host (`mods/host/pe_debug_control.c`). It
lets a script ask the running game for things a player would otherwise have to walk somewhere for:
go to a room, find a person, stand Aya next to them, read or set a script variable, or save the
game as it is now.

It is **off unless the environment variable `PE_DEBUG_CONTROL` names a folder**. Nothing shows in
F1 and players never meet it. Being part of the host, it answers whichever mods are on or off
(All mods off included). It changes no game source file and no gameplay.

## How it talks

The script writes one command per line to `<folder>/cmd` (write `cmd.tmp`, then rename it, so the
game never reads half a file). The game reads it every 10 frames, in any state, deletes it, and
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

### A warp gets the room, not the scene

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
