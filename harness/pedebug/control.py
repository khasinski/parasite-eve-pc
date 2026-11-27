"""The game's debug-control channel beyond single commands (docs/DEBUG_CONTROL.md in the game).

EventLog reads <ctl>/events.jsonl, the line per change the game writes on the frame it happens
(rooms, dialogue, menus, battles, control, saves, the title), so a test waits for what it means
("Wayne's menu is waiting", "the battle ended") instead of guessing frame counts:

    mark = s.events.mark()
    flows.talk(s)
    box = s.events.wait("dialogue_waiting", since=mark, page=164)

LiveGame talks to a game someone is playing (PE_DEBUG_CONTROL set, not headless): nothing steps
it, so it waits on the wall clock. `pe.py live` uses it.
"""
from __future__ import annotations

import json
import time
from pathlib import Path

from .game import GameError


def _matches(event: dict, name, fields: dict, where) -> bool:
    if name is not None:
        names = (name,) if isinstance(name, str) else tuple(name)
        if event.get("event") not in names:
            return False
    for k, v in fields.items():
        got = event.get(k)
        if callable(v):
            if not v(got):
                return False
        elif isinstance(v, str) and isinstance(got, str) and k == "text":
            if v not in got:                     # text matches by substring
                return False
        elif got != v:
            return False
    return where(event) if where else True


class EventReader:
    """events.jsonl read incrementally: complete lines only, each parsed once."""

    def __init__(self, path: Path):
        self.path = Path(path)
        self._pos = 0
        self._buf = b""
        self.events: list[dict] = []

    def poll(self) -> list[dict]:
        if not self.path.exists():
            return []
        with open(self.path, "rb") as f:
            f.seek(self._pos)
            data = f.read()
        self._pos += len(data)
        self._buf += data
        new = []
        *lines, self._buf = self._buf.split(b"\n")
        for line in lines:
            if line.strip():
                try:
                    new.append(json.loads(line))
                except json.JSONDecodeError:
                    new.append({"event": "unparsed", "line": line.decode(errors="replace")})
        self.events += new
        return new

    def mark(self) -> int:
        """A position: events after it are what happens next."""
        self.poll()
        return len(self.events)

    def since(self, mark: int = 0) -> list[dict]:
        self.poll()
        return self.events[mark:]

    def find(self, name=None, since: int = 0, where=None, **fields) -> dict | None:
        for e in self.since(since):
            if _matches(e, name, fields, where):
                return e
        return None

    def last(self, name=None, **fields) -> dict | None:
        for e in reversed(self.since(0)):
            if _matches(e, name, fields, None):
                return e
        return None

    def tail(self, n: int = 15) -> list[dict]:
        return self.since(0)[-n:]


class EventLog(EventReader):
    """A headless Session's event log: wait() steps the game until the event shows."""

    def __init__(self, session):
        super().__init__(session.ctl / "events.jsonl")
        self.s = session

    def wait(self, name=None, since: int | None = None, timeout: int = 3600, step: int = 5,
             where=None, buttons: int = 0, **fields) -> dict:
        """Advance until an event named `name` (a name or a tuple of names) with these field values
        appears after `since` (default: now). Field values match exactly, or by a callable, or for
        "text" by substring. GameError with the recent events after `timeout` frames."""
        start = self.mark() if since is None else since
        waited = 0
        while True:
            e = self.find(name, since=start, where=where, **fields)
            if e is not None:
                return e
            if waited >= timeout:
                self.s.shot(f"timeout-event-{name}")
                recent = "\n  ".join(json.dumps(x)[:160] for x in self.tail(8))
                raise GameError(f"no {name} {fields or ''} within {timeout} frames ({self.s.state.short()}); "
                                f"last events:\n  {recent}")
            self.s.advance(step, buttons)
            waited += step

    def expect_none(self, name, since: int, **fields) -> None:
        """GameError if such an event happened after `since` (e.g. no player_down)."""
        e = self.find(name, since=since, **fields)
        if e is not None:
            raise GameError(f"unexpected {name}: {json.dumps(e)}")


class LiveGame:
    """A game someone is playing with PE_DEBUG_CONTROL=<folder>. Commands are answered at the game's
    10-frame poll while the field runs; a paused or unfocused game answers when it runs again."""

    def __init__(self, folder):
        self.dir = Path(folder)
        self.events = EventReader(self.dir / "events.jsonl")
        if not self.dir.is_dir():
            raise GameError(f"no control folder {self.dir}: start the game with PE_DEBUG_CONTROL={self.dir}")

    def command(self, cmd: str, timeout: float = 10.0) -> str:
        out = self.dir / "out"
        seen = len(out.read_text().splitlines()) if out.exists() else 0
        tmp = self.dir / "cmd.tmp"
        tmp.write_text(cmd + "\n")
        tmp.replace(self.dir / "cmd")
        end = time.time() + timeout
        word = cmd.split()[0]
        while time.time() < end:
            lines = out.read_text().splitlines() if out.exists() else []
            for line in lines[seen:]:
                if line.startswith(("ok " + word, "err " + word)):
                    if line.startswith("err"):
                        raise GameError(f"{cmd}: {line}")
                    return line
            time.sleep(0.05)
        raise GameError(f"no answer to {cmd!r} in {timeout:.0f} s: is the game running, in the field, focused?")

    def ask_json(self, cmd: str, timeout: float = 10.0):
        return json.loads(self.command(cmd, timeout).split(" ", 2)[2])

    def snapshot(self, name: str = "", timeout: float = 10.0) -> Path:
        line = self.command(f"snapshot {name}".strip(), timeout)
        return self.dir / line.split()[2]
