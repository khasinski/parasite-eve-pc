"""Checks, run headless in parallel, with everything kept when one fails.

A check is a function taking a Ctx, registered with @check in a module under checks/:

    @check("battle-win", tags=("diag", "battle"), needs=("card:C",))
    def battle_win(ctx):
        s = ctx.session(cards=[ctx.card("C")], ini=FAST_BATTLES)
        flows.load_save(s, flows.slot_of(ctx.card("C")))
        ...
        ctx.expect(e["hp"] > 0, "Aya survived")

ctx.session() starts a game in the check's own folder and is closed for you. ctx.expect() records
a soft failure and goes on; an exception (GameError, AssertionError, anything) ends the check.

When a check fails, every session it still has open is captured into <case>/failure/: the game's
snapshot (room, ui, battle, bag, actors, mods' live state, last commands), a picture, the event
log, the last control states, the inputs sent, stderr and the mods' log. The run writes
report.md, results.json and junit.xml.
"""
from __future__ import annotations

import concurrent.futures as cf
import importlib
import json
import pkgutil
import shutil
import threading
import time
import traceback
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from xml.sax.saxutils import escape

from . import config as config_mod
from .game import GameError, Session

ROOT = config_mod.ROOT


# ---- the registry ----

@dataclass
class Check:
    name: str
    fn: object
    tags: tuple = ()
    needs: tuple = ()
    description: str = ""
    timeout_s: float = 600.0


REGISTRY: dict[str, Check] = {}


def check(name: str, tags=(), needs=(), timeout_s: float = 600.0):
    def wrap(fn):
        REGISTRY[name] = Check(name, fn, tuple(tags), tuple(needs), (fn.__doc__ or "").strip(), timeout_s)
        return fn
    return wrap


def load_checks(package: str = "checks") -> dict[str, Check]:
    pkg = importlib.import_module(package)
    for mod in pkgutil.iter_modules(pkg.__path__):
        if not mod.name.startswith("_"):
            importlib.import_module(f"{package}.{mod.name}")
    return REGISTRY


def select(names=(), tags=()) -> list[Check]:
    """By name (exact or prefix*), or by tag; none given: tag "diag"."""
    if not names and not tags:
        tags = ("diag",)
    if "all" in tags:                                   # everything but the harness's own selftest
        return [c for c in REGISTRY.values() if "selftest" not in c.tags]
    out = []
    for c in REGISTRY.values():
        hit = any(c.name == n or (n.endswith("*") and c.name.startswith(n[:-1])) for n in names)
        hit = hit or any(t in c.tags for t in tags)
        if hit:
            out.append(c)
    return out


# ---- what a check gets ----

class Expectation(AssertionError):
    pass


class Ctx:
    def __init__(self, chk: Check, cfg, folder: Path, log):
        self.check = chk
        self.cfg = cfg
        self.dir = folder
        self.log = log
        self.sessions: list[Session] = []
        self.failures: list[str] = []
        self.known: list[str] = []      # failures of known, reported game bugs (status "known")
        self.notes: list[str] = []
        self.facts: dict = {}           # values worth seeing in the report (timings, counts)
        self.vblanks = 0

    def card(self, letter: str) -> Path:
        hits = sorted(self.cfg.cards.glob(f"BASLUS-00662*{letter.upper()}"))
        if not hits:
            raise GameError(f"no fixture card {letter} in {self.cfg.cards}")
        return hits[0]

    def scenario(self, name: str) -> Path:
        return self.cfg.scenarios / name

    def session(self, cards=None, ini: str | None = "Manage mods/All mods=1\n", name: str | None = None, **kw) -> Session:
        n = len(self.sessions) + 1
        s = Session(self.cfg.exe, self.dir / (name or f"s{n}"), cards=cards, mods_ini=ini,
                    disc1=self.cfg.disc1, disc2=self.cfg.disc2, log=self.log, **kw)
        self.sessions.append(s)
        return s

    def note(self, text: str) -> None:
        self.notes.append(text)
        self.log(f"  {self.check.name}: {text}")

    def fact(self, key: str, value) -> None:
        self.facts[key] = value

    def expect(self, cond, what: str) -> bool:
        """A soft check: recorded, and the check goes on."""
        if not cond:
            self.failures.append(what)
            self.log(f"  {self.check.name}: FAILED {what}")
        return bool(cond)

    def known_bug(self, cond, what: str, bug: str) -> bool:
        """An expectation a known game bug breaks: recorded as "known" (shown in every report, not a
        failure). When it holds again, that is said too, so the marker can go."""
        if not cond:
            self.known.append(f"{what} [known bug: {bug}]")
            self.log(f"  {self.check.name}: KNOWN BUG {what}")
        else:
            self.notes.append(f"known bug no longer seen ({bug}): {what}; remove the marker")
        return bool(cond)

    def require(self, cond, what: str) -> None:
        """A hard check: the check ends here."""
        if not cond:
            raise Expectation(what)


# ---- results ----

@dataclass
class Result:
    name: str
    status: str                  # pass / known / fail / error / skip
    seconds: float = 0.0
    message: str = ""
    failures: list = field(default_factory=list)
    known: list = field(default_factory=list)
    notes: list = field(default_factory=list)
    facts: dict = field(default_factory=dict)
    folder: str = ""
    captured: list = field(default_factory=list)


def missing_needs(chk: Check, cfg) -> list[str]:
    missing = []
    for need in chk.needs:
        kind, _, value = need.partition(":")
        if kind == "card" and not sorted(cfg.cards.glob(f"BASLUS-00662*{value.upper()}")):
            missing.append(f"fixture card {value} (in {cfg.cards})")
        elif kind == "scenario" and not (cfg.scenarios / value / "scenario.json").exists():
            missing.append(f"scenario save {value} (pe.py capture {value}; fixtures/README.md)")
        elif kind == "mod" and not _exe_has_mod(cfg.exe, value):
            missing.append(f"mod {value} in the build (PE_EXTRA_MODS)")
    return missing


_mod_cache: dict = {}


def _exe_has_mod(exe: str, mod_id: str) -> bool:
    """The build holds the mod: its id as a whole C string (PeMod.id)."""
    key = (exe, mod_id)
    if key not in _mod_cache:
        try:
            _mod_cache[key] = b"\0" + mod_id.encode() + b"\0" in Path(exe).read_bytes()
        except OSError:
            _mod_cache[key] = False
    return _mod_cache[key]


# ---- capture ----

def capture(ctx: Ctx, why: str) -> list[str]:
    """Everything each open session can still tell, into <case>/failure/."""
    out = ctx.dir / "failure"
    out.mkdir(parents=True, exist_ok=True)
    saved = []
    (out / "why.txt").write_text(why)
    for n, s in enumerate(ctx.sessions, 1):
        tag = f"s{n}"
        alive = s.proc.poll() is None
        if alive:
            try:
                snap = s.snapshot(f"failure-{tag}", timeout=200)
                (out / f"{tag}-snapshot.json").write_text(json.dumps(snap, indent=1))
                saved.append(f"{tag}-snapshot.json")
            except Exception as e:                       # not in the field, or the game stopped answering
                (out / f"{tag}-snapshot-error.txt").write_text(str(e))
            try:
                png = s.shot("failure")
                if png.exists():
                    shutil.copyfile(png, out / f"{tag}-frame.png")
                    saved.append(f"{tag}-frame.png")
            except Exception:
                pass
        for src, dst in ((s.ctl / "events.jsonl", f"{tag}-events.jsonl"), (s.ctl / "out", f"{tag}-commands.txt"),
                         (s.dir / "pe_mods.log", f"{tag}-pe_mods.log"), (s.dir / "stderr.txt", f"{tag}-stderr.txt")):
            if src.exists():
                shutil.copyfile(src, out / dst)
                saved.append(dst)
        (out / f"{tag}-states.jsonl").write_text("".join(json.dumps(st.raw) + "\n" for st in s.history[-60:]))
        s.save_inputs(out / f"{tag}-inputs.jsonl")
        saved += [f"{tag}-states.jsonl", f"{tag}-inputs.jsonl"]
        (out / f"{tag}-state.txt").write_text(s.state.short() + ("" if alive else "\n(game had exited)"))
    return saved


# ---- running ----

def run_one(chk: Check, cfg, folder: Path, log) -> Result:
    folder.mkdir(parents=True, exist_ok=True)
    missing = missing_needs(chk, cfg)
    if missing:
        return Result(chk.name, "skip", message="needs " + "; ".join(missing), folder=str(folder))
    ctx = Ctx(chk, cfg, folder, log)
    t0 = time.time()
    status, message = "pass", ""
    timer = threading.Timer(chk.timeout_s, lambda: [s.proc.kill() for s in ctx.sessions if s.proc.poll() is None])
    timer.start()
    try:
        chk.fn(ctx)
        if ctx.failures:
            status, message = "fail", f"{len(ctx.failures)} expectation(s) failed"
        elif ctx.known:
            status, message = "known", f"{len(ctx.known)} known game bug(s): " + ctx.known[0]
    except Expectation as e:
        status, message = "fail", str(e)
    except GameError as e:
        status, message = "fail", str(e)
    except Exception as e:
        status, message = "error", f"{type(e).__name__}: {e}"
        (folder / "traceback.txt").write_text(traceback.format_exc())
    finally:
        timer.cancel()
    if time.time() - t0 >= chk.timeout_s:
        message = f"timed out after {chk.timeout_s:.0f} s; " + message
    captured = []
    if status != "pass":
        try:
            captured = capture(ctx, message + "".join("\n" + f for f in ctx.failures + ctx.known))
        except Exception as e:
            (folder / "capture-error.txt").write_text(traceback.format_exc())
    for s in ctx.sessions:
        try:
            s.save_inputs()
            s.quit()
        except Exception:
            pass
    return Result(chk.name, status, time.time() - t0, message, ctx.failures, ctx.known, ctx.notes, ctx.facts,
                  str(folder), captured)


def run(checks: list[Check], suite: str = "diag", jobs: int = 3, cfg=None, log=print) -> tuple[Path, list[Result]]:
    cfg = cfg or config_mod.load()
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out = Path(cfg.runs) / suite / stamp
    out.mkdir(parents=True, exist_ok=True)
    (Path(cfg.runs) / suite / "latest.txt").write_text(str(out) + "\n")
    lock = threading.Lock()

    def say(msg):
        with lock:
            log(msg)

    results: list[Result] = []
    t0 = time.time()
    say(f"{len(checks)} check(s), {jobs} at a time -> {out}")
    with cf.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        futures = {pool.submit(run_one, c, cfg, out / c.name, say): c for c in checks}
        for f in cf.as_completed(futures):
            r = f.result()
            results.append(r)
            say(f"[{r.status.upper():5}] {r.name} ({r.seconds:.0f} s){'  ' + r.message.splitlines()[0] if r.message else ''}")
    order = {c.name: i for i, c in enumerate(checks)}
    results.sort(key=lambda r: order[r.name])
    write_reports(out, suite, results, time.time() - t0, cfg)
    return out, results


def write_reports(out: Path, suite: str, results: list[Result], seconds: float, cfg) -> None:
    counts = {k: sum(r.status == k for r in results) for k in ("pass", "known", "fail", "error", "skip")}
    (out / "results.json").write_text(json.dumps({
        "suite": suite, "exe": cfg.exe, "seconds": round(seconds, 1), "counts": counts,
        "results": [r.__dict__ for r in results]}, indent=1))
    lines = [f"# {suite}: {counts['pass']} passed, {counts['known']} known bugs, {counts['fail']} failed, "
             f"{counts['error']} errors, {counts['skip']} skipped ({seconds:.0f} s)", "", f"Build: `{cfg.exe}`", "",
             "| Check | Result | Time | Detail |", "|---|---|---|---|"]
    for r in results:
        detail = (r.message.splitlines()[0] if r.message else "").replace("|", "\\|")[:150]
        lines.append(f"| {r.name} | {r.status} | {r.seconds:.0f} s | {detail} |")
    for r in results:
        if not (r.failures or r.known or r.notes or r.facts or r.captured or r.status in ("fail", "error")):
            continue
        lines += ["", f"## {r.name}: {r.status}", ""]
        if r.message:
            lines += ["```", r.message, "```", ""]
        lines += [f"- expected, not seen: {f}" for f in r.failures]
        lines += [f"- KNOWN GAME BUG: {k}" for k in r.known]
        lines += [f"- {k}: {v}" for k, v in r.facts.items()]
        lines += [f"- {n}" for n in r.notes]
        if r.captured:
            lines += ["", f"Captured in `{Path(r.folder) / 'failure'}`: " + ", ".join(r.captured)]
    (out / "report.md").write_text("\n".join(lines) + "\n")
    cases = []
    for r in results:
        body = ""
        if r.status in ("fail", "error"):
            body = f'<failure message="{escape(r.message[:200], {chr(34): "&quot;"})}">{escape(r.message)}\n' + \
                   escape("\n".join(r.failures)) + "</failure>"
        elif r.status == "skip":
            body = f'<skipped message="{escape(r.message, {chr(34): "&quot;"})}"/>'
        cases.append(f'  <testcase classname="{suite}" name="{escape(r.name)}" time="{r.seconds:.1f}">{body}</testcase>')
    (out / "junit.xml").write_text(
        f'<?xml version="1.0" encoding="utf-8"?>\n<testsuite name="{suite}" tests="{len(results)}" '
        f'failures="{counts["fail"]}" errors="{counts["error"]}" skipped="{counts["skip"]}" time="{seconds:.1f}">\n'
        + "\n".join(cases) + "\n</testsuite>\n")
