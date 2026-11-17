"""Getting somewhere: warp to a room, find a person, stand Aya next to them, read and set script
variables; and scenario saves, for when a room alone isn't enough.

Uses the debug channel's commands (mods/host/debug/pe_debug_field.c, docs/DEBUG_CONTROL.md):
room, warp, actors, find-script, place, place-near, get-var, set-var, save-block.

What a warp can and can't do. It loads the room through the field script's own transfer and can set
story progress (script variable 74). Who stands in the room, and what they say, also follows the
story day and scene flags, which a warp doesn't touch. Warping to Wayne's
workbench (map 48) from Day 2, 4 and 5 saves loaded his actor and script but never showed him. For
a person or an event, start from a save made there: a scenario (save_scenario / open_scenario, or
pe.py capture from a game you are playing).

Positions are the field's 16.16 units, as the control state's "position" ([x, y, z]).
"""
from __future__ import annotations

import json
import re
import shutil
from pathlib import Path

from . import config, flows
from .game import GameError, Session

ROOT = Path(__file__).resolve().parent.parent
SCENARIOS = config.fixtures_dir() / "scenarios"  # kept out of git, like fixtures/cards (harness.json: fixtures)

# Script words that find people by what their script does (find_script).
WAYNE_JUNK_COUNT = (0x001040A8, 0, 0x14)          # CountItemsByValue(Junk) -> a local: Discard junk


def _fields(line: str) -> dict:
    return dict(kv.split("=", 1) for kv in line.split()[2:] if "=" in kv)


def room(s: Session) -> dict:
    """{map, name, token, story, day, free}: where Aya is (map -1 outside a numbered room)."""
    f = _fields(s.command("room"))
    return {"map": int(f["map"]), "name": f["name"], "token": int(f["token"], 16),
            "story": int(f["story"], 16), "day": int(f["day"]), "free": f["free"] == "1"}


def warp(s: Session, map_number: int, story: int | None = None, timeout: int = 3000) -> dict:
    """Go to map `map_number` (docs/ROOMS.md numbering); with `story`, set story progress first.
    Returns room() once Aya is in the new room's free field."""
    cmd = f"warp {map_number}" + (f" {story:X}" if story is not None else "")
    s.command(cmd)
    waited = 0
    while waited < timeout:
        s.advance(30)
        waited += 30
        try:
            r = room(s)
        except GameError:
            continue
        if r["map"] == map_number and r["free"]:
            s.wait_stable(60)
            return room(s)
    s.shot(f"warp-{map_number}-timeout")
    raise GameError(f"warp {map_number}: not there after {timeout} frames: {s.state.short()}")


def actors(s: Session) -> list[dict]:
    """The room's actors, in the control state's order: {index, type, sub, player, x, z, flags}."""
    out = []
    for item in s.command("actors").split()[2:]:
        m = re.fullmatch(r"(\d+):(\d+)\.(\d+)(\*?)@(-?\d+),(-?\d+),f([0-9A-F]+)", item)
        if m:
            out.append({"index": int(m[1]), "type": int(m[2]), "sub": int(m[3]), "player": bool(m[4]),
                        "x": int(m[5]), "z": int(m[6]), "flags": int(m[7], 16)})
    return out


def find_script(s: Session, *words: int) -> list[tuple[int, int]]:
    """Actors whose script holds these consecutive words: [(actor index, byte offset)]."""
    line = s.command("find-script " + " ".join(f"{w:X}" for w in words))
    return [(int(i), int(o, 16)) for i, o in re.findall(r"(\d+)@([0-9A-F]+)", line)]


def find_person(s: Session, words) -> int:
    """The one non-player actor whose script holds `words` (e.g. WAYNE_JUNK_COUNT)."""
    player = {a["index"] for a in actors(s) if a["player"]}
    hits = [i for i, _ in find_script(s, *words) if i not in player]
    if not hits:
        raise GameError(f"nobody here runs {['%X' % w for w in words]}: {s.state.short()}")
    return hits[0]


def place(s: Session, x: int, z: int, yaw: int | None = None) -> None:
    s.command(f"place {x} {z}" + (f" {yaw}" if yaw is not None else ""))
    s.advance(2)


def place_near(s: Session, index: int, dist: int = 0x180000) -> tuple[int, int]:
    """Stand Aya `dist` from actor `index`, on her side of them, facing them. Returns her x, z."""
    x, z, _ = s.command(f"place-near {index} {dist}").split()[-1].split(",")
    s.advance(2)
    return int(x), int(z)


def get_var(s: Session, n: int) -> int:
    return int(s.command(f"get-var {n}").split("=")[-1], 16)


def set_var(s: Session, n: int, value: int) -> None:
    s.command(f"set-var {n} {value:X}")


# ---- scenario saves: a real save made at the spot, with a note of where it is ----
def save_scenario(s: Session, name: str, note: str = "") -> Path:
    """Save the game as it is now (free field) as scenario `name`: the card file a save to slot A
    would write, its companion file when it has one (in .pe-ext/), and scenario.json (room, story,
    day, actors, note)."""
    files = s.save_files(1)
    d = SCENARIOS / name
    if d.exists():
        shutil.rmtree(d)
    d.mkdir(parents=True)
    for rel, data in files.items():
        (d / rel).parent.mkdir(parents=True, exist_ok=True)
        (d / rel).write_bytes(data)
    card = next(k for k in files if "/" not in k)
    (d / "scenario.json").write_text(json.dumps({"card": card, "files": sorted(files), "room": room(s),
                                                 "actors": actors(s), "note": note}, indent=1))
    return d


def scenario(name: str) -> tuple[Path, dict]:
    d = SCENARIOS / name
    if not (d / "scenario.json").exists():
        raise GameError(f"no scenario {name!r} in {SCENARIOS}")
    meta = json.loads((d / "scenario.json").read_text())
    return d / meta["card"], meta


def open_scenario(exe, run_dir, name: str, **session_args) -> Session:
    """A Session loaded into scenario `name`, in its room's free field (the caller closes it)."""
    card, meta = scenario(name)
    extra = dict(session_args.pop("extra_files", None) or {})
    for rel in meta.get("files", []):
        if "/" in rel:                                   # the companion: beside the card, in bu00
            extra["bu00/" + rel] = (card.parent / rel).read_bytes()
    s = Session(exe, run_dir, cards=[card], extra_files=extra, **session_args)
    try:
        flows.load_save(s, flows.slot_of(card))
        s.wait_stable(60)
        here = room(s)
        if here["map"] != meta["room"]["map"]:
            raise GameError(f"scenario {name}: loaded into map {here['map']}, saved in {meta['room']['map']}")
    except Exception:
        s.quit()
        raise
    return s
