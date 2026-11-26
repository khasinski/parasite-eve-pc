"""Drive the native game headless, frame by frame, and see what it is doing.

The game's own control mode (--control-at) pauses at a VBlank, prints one JSON line of state
(scene, game flags, the open menu widget and its cursor, dialogue boxes, actors, Aya's HP), writes
the frame (--dump-frame), then reads one command from stdin: "advance FRAMES BUTTONS" or "quit".
A Session keeps that loop going and adds what a test needs: press a button, wait until the state
matches, take a picture, and read the mods' logs.

Each session runs in a fresh directory used as PE_CONFIG_HOME, so its preferences, pe_mods.ini,
logs and memory cards are its own: the player's real saves are never touched.
"""
from __future__ import annotations

import json
import os
import shutil
import struct
import subprocess
import time
import zlib
from dataclasses import dataclass
from pathlib import Path

# PS1 pad bits, as "advance" takes them.
SELECT, L3, R3, START = 0x0001, 0x0002, 0x0004, 0x0008
UP, RIGHT, DOWN, LEFT = 0x0010, 0x0020, 0x0040, 0x0080
L2, R2, L1, R1 = 0x0100, 0x0200, 0x0400, 0x0800
TRIANGLE, CIRCLE, CROSS, SQUARE = 0x1000, 0x2000, 0x4000, 0x8000
BUTTONS = {"select": SELECT, "start": START, "up": UP, "right": RIGHT, "down": DOWN, "left": LEFT,
           "l2": L2, "r2": R2, "l1": L1, "r1": R1, "triangle": TRIANGLE, "circle": CIRCLE,
           "cross": CROSS, "square": SQUARE}

ROOT = Path(__file__).resolve().parent.parent


class GameError(RuntimeError):
    pass


def _default_discs() -> tuple[str, str]:
    """harness.json / PE_DISC1 / PE_DISC2 (pe.py setup --disc1 ...)."""
    from . import config
    cfg = config.load()
    if not cfg.disc1 or not cfg.disc2:
        raise GameError("no disc images configured: python pe.py setup --disc1 <Disc 1 .cue> --disc2 <Disc 2 .cue>")
    return cfg.disc1, cfg.disc2


@dataclass
class State:
    """One control line, with the fields tests use most as attributes."""
    raw: dict

    vblanks = property(lambda s: s.raw["vblanks"])
    scene = property(lambda s: s.raw["scene"])
    flags = property(lambda s: s.raw["flags"])              # g_GameStateFlags
    game_flags = property(lambda s: s.raw["game_flags"])    # g_GameState.flags
    menu = property(lambda s: s.raw.get("menu"))            # {mode, selection, cursor} or None
    movie = property(lambda s: s.raw.get("movie"))
    dialogue = property(lambda s: [d for d in s.raw.get("dialogue", []) if d.get("state")])
    hp = property(lambda s: s.raw.get("combat", {}).get("player_hp"))
    max_hp = property(lambda s: s.raw.get("combat", {}).get("player_max_hp"))

    @property
    def player(self):
        for a in self.raw.get("actors", []):
            if a.get("player"):
                return a
        return None

    def short(self) -> str:
        m = self.menu
        menu = f"menu {m['mode']}/{m['selection']} @{m['cursor']}" if m else "no menu"
        p = self.player
        pos = f" aya {p['position']}" if p else ""
        hp = f" hp {self.hp}/{self.max_hp}" if self.hp is not None else ""
        talk = f" dialogue {len(self.dialogue)}" if self.dialogue else ""
        return (f"vb {self.vblanks} scene {self.scene} flags {self.flags:#x} gs {self.game_flags:#x} "
                f"{menu}{talk}{pos}{hp}")


def ppm_to_png(ppm: Path, png: Path) -> None:
    data = ppm.read_bytes()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    pixels = parts[3]
    rows = b"".join(b"\0" + pixels[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(kind, body):
        c = struct.pack(">I", len(body)) + kind + body
        return c + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    png.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                    chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


class Session:
    def __init__(self, exe, run_dir, cards=None, cards2=None, mods_ini="Manage mods/All mods=1\n",
                 disc1=None, disc2=None, frame_limit=2_000_000, log=print, extra_args=(),
                 extra_files=None, env=None):
        if disc1 is None or disc2 is None:
            d1, d2 = _default_discs()
            disc1, disc2 = disc1 or d1, disc2 or d2
        self.exe = Path(exe)
        self.dir = Path(run_dir).resolve()
        self.log = log
        if self.dir.exists():
            shutil.rmtree(self.dir)
        # Headless runs skip the user preferences: cards are bu00 / bu10 in the working directory,
        # and pe_mods.ini and the logs sit there too.
        for port, files in (("bu00", cards), ("bu10", cards2)):
            (self.dir / port).mkdir(parents=True)
            for f in files or []:
                f = Path(f)
                shutil.copyfile(f, self.dir / port / f.name)
        if mods_ini is not None:
            (self.dir / "pe_mods.ini").write_text(mods_ini)
        for name, data in (extra_files or {}).items():   # other files a mod reads at start (bytes)
            (self.dir / name).parent.mkdir(parents=True, exist_ok=True)   # e.g. "lang/fr.pelang"
            (self.dir / name).write_bytes(data)
        self.frame_ppm = self.dir / "frame.ppm"
        self.shots = 0
        self.history: list[State] = []
        self.ctl = self.dir / "ctl"          # the debug channel folder (docs/DEBUG_CONTROL.md)
        self.ctl.mkdir()
        env = dict(os.environ, PE_CONFIG_HOME=str(self.dir), PSYZ_AUDIO_CLOCK="video", SDL_AUDIODRIVER="dummy",
                   PE_DEBUG_CONTROL=str(self.ctl), **(env or {}))
        self.inputs: list[tuple[int, int, int]] = []   # (vblank, frames, buttons): every advance, for replay
        self.started = time.time()
        from .control import EventLog
        self.events = EventLog(self)
        args = [str(self.exe), "--disc", disc1, "--disc2", disc2, "--headless",
                "--frame-limit", str(frame_limit), "--control-at", "1", "--dump-frame", str(self.frame_ppm),
                "--pad-at", "1:0"] + list(extra_args)   # control buttons reach the pad only with a pad event
        self.stderr = open(self.dir / "stderr.txt", "w")
        self.proc = subprocess.Popen(args, cwd=self.dir, env=env, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=self.stderr, text=True, bufsize=1)
        self.state = self._read_state()

    # ---- the control loop ----
    def _read_state(self) -> State:
        while True:
            line = self.proc.stdout.readline()
            if not line:
                code = self.proc.wait()
                raise GameError(f"game exited ({code}); see {self.dir / 'stderr.txt'}")
            line = line.strip()
            if line.startswith('{"event":"control"') or line.startswith('{"event":"frame_limit"'):
                st = State(json.loads(line))
                self.history.append(st)
                if len(self.history) > 2000:
                    del self.history[:1000]
                return st

    def advance(self, frames: int = 1, buttons: int = 0) -> State:
        self.inputs.append((self.state.vblanks, int(frames), int(buttons)))
        self.proc.stdin.write(f"advance {int(frames)} {int(buttons)}\n")
        self.proc.stdin.flush()
        self.state = self._read_state()
        return self.state

    def press(self, button, hold: int = 10, after: int = 20) -> State:
        """Hold a button for a few frames, then release and let the game react."""
        mask = BUTTONS[button] if isinstance(button, str) else button
        self.advance(hold, mask)
        return self.advance(after, 0)

    def wait(self, pred, timeout: int = 3600, step: int = 10, buttons: int = 0, what: str = "") -> State:
        """Advance until pred(state) holds; GameError after timeout frames."""
        waited = 0
        while not pred(self.state):
            if waited >= timeout:
                self.shot(f"timeout-{what or 'wait'}")
                raise GameError(f"timed out after {timeout} frames waiting for {what or pred}: {self.state.short()}")
            self.advance(step, buttons)
            waited += step
        return self.state

    def wait_stable(self, frames: int = 60, timeout: int = 3600) -> State:
        """Advance until scene, flags and menu stay the same for `frames` frames."""
        key = lambda s: (s.scene, s.flags, s.game_flags, json.dumps(s.menu))
        last, same, waited = key(self.state), 0, 0
        while same < frames:
            self.advance(10)
            waited += 10
            k = key(self.state)
            same = same + 10 if k == last else 0
            last = k
            if waited > timeout:
                raise GameError(f"never settled: {self.state.short()}")
        return self.state

    # ---- the debug channel (PE_DEBUG_CONTROL; docs/DEBUG_CONTROL.md) ----
    def command(self, cmd: str, timeout: int = 1200) -> str:
        """Send one command; advance until the game answers it. GameError on "err" or no answer.
        The game reads commands in free field only."""
        out = self.ctl / "out"
        seen = len(out.read_text().splitlines()) if out.exists() else 0
        tmp = self.ctl / "cmd.tmp"
        tmp.write_text(cmd + "\n")
        tmp.replace(self.ctl / "cmd")
        waited = 0
        while True:
            lines = out.read_text().splitlines() if out.exists() else []
            for line in lines[seen:]:
                if line.startswith(("ok " + cmd.split()[0], "err " + cmd.split()[0])):
                    if line.startswith("err"):
                        raise GameError(f"{cmd}: {line}")
                    return line
            if waited >= timeout:
                raise GameError(f"no answer to {cmd!r} after {timeout} frames: {self.state.short()}")
            self.advance(10)
            waited += 10

    def ask_json(self, cmd: str, timeout: int = 1200):
        """A command whose answer is one JSON value: "ok <command> {...}"."""
        line = self.command(cmd, timeout)
        return json.loads(line.split(" ", 2)[2])

    def ui(self) -> dict:
        """ui-state: the menu layer, open dialogue boxes, recent pages."""
        return self.ask_json("ui-state")

    def battle(self) -> dict:
        return self.ask_json("battle-state")

    def mod_state(self, mod_id: str) -> dict:
        """mod-state: the mod's F1 rows and, when it registers providers, "live"."""
        return self.ask_json(f"mod-state {mod_id}")

    def live(self, mod_id: str, provider: str):
        """One live-state provider's object, or None (mod not running / no such provider)."""
        return (self.mod_state(mod_id).get("live") or {}).get(provider)

    def snapshot(self, name: str = "", timeout: int = 300) -> dict:
        """snapshot [NAME]: everything in one JSON file (ctl/snapshot[-NAME].json), parsed."""
        line = self.command(f"snapshot {name}".strip(), timeout)
        return json.loads((self.ctl / line.split()[2]).read_text())

    def save_inputs(self, path: Path | None = None) -> Path:
        """Every advance so far, one JSON line each ({"vb","frames","buttons"}), for replays."""
        path = path or self.dir / "inputs.jsonl"
        path.write_text("".join(json.dumps({"vb": v, "frames": f, "buttons": b}) + "\n" for v, f, b in self.inputs))
        return path

    def mods_tail(self) -> bytes:
        """The mods' data as a save made now would hold it (0x370 bytes; decode with pedebug.card)."""
        self.command("mods-tail")
        return (self.ctl / "tail.bin").read_bytes()

    def save_block(self, slot: int = 1) -> tuple[str, bytes]:
        """The card file a save to slot `slot` would write now (the game's own Save_BuildCardFile),
        and its file name. Put it on a card for the next session to load."""
        line = self.command(f"save-block {slot}")
        return line.split()[2], (self.ctl / "block.bin").read_bytes()

    def save_files(self, slot: int = 1) -> dict[str, bytes]:
        """save_block, with the save's companion file when it has one (mods' data kept in
        ".pe-ext" beside the card): {card name: block, ".pe-ext/<name>": companion}. Put them all in
        the card folder (bu00) for a load to find."""
        words = self.command(f"save-block {slot}").split()
        files = {words[2]: (self.ctl / "block.bin").read_bytes()}
        for w in words[3:]:
            if w.startswith("companion="):
                files[".pe-ext/" + w.split("=", 1)[1]] = (self.ctl / "companion.pex").read_bytes()
        return files

    def shot(self, name: str) -> Path:
        """The last frame as a PNG in the run directory."""
        self.shots += 1
        safe = "".join(c if c.isalnum() or c in "-_." else "-" for c in name)[:60]
        png = self.dir / f"{self.shots:03d}-{safe}.png"
        if self.frame_ppm.exists():
            ppm_to_png(self.frame_ppm, png)
        return png

    def quit(self) -> None:
        if self.proc.poll() is None:
            try:
                self.proc.stdin.write("quit\n")
                self.proc.stdin.flush()
                self.proc.wait(10)
            except Exception:
                self.proc.kill()
        self.stderr.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.quit()

    # ---- what the run left behind ----
    def card_files(self, port: str = "bu00") -> list[Path]:
        return sorted((self.dir / port).glob("BASLUS-00662*"))

    def mods_log(self) -> list[str]:
        p = self.dir / "pe_mods.log"
        return p.read_text(errors="replace").splitlines() if p.exists() else []

    def play_log(self) -> list[str]:
        p = self.dir / "pe_play_log.csv"
        return p.read_text(errors="replace").splitlines() if p.exists() else []
