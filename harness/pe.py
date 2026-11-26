#!/usr/bin/env python3
"""pe.py: the headless test harness for the native Parasite Eve port, in one command.

  python pe.py setup                 find the build, discs and fixtures; write harness.json
  python pe.py doctor [--boot]       check everything is in place (--boot: start the game too)
  python pe.py list [--tag T]        the checks, their tags and whether they can run here
  python pe.py diag [--quick|--full] diagnostics; --full adds saves, mods on/off, economy, challenges
  python pe.py run NAME... [--tag T] [--repeat N] [--jobs N]
  python pe.py report                the latest run's report
  python pe.py build [CHECKOUT] [--extra-mods "pe_assists;pe_bp;pe_challenges"] [--toolchain DIR] [--use]
  python pe.py play                  start the game in a window with the debug channel on
  python pe.py live state|ui|battle|events|watch|snapshot [NAME]|mod ID|cmd "..."
  python pe.py capture NAME          save the playing game's spot as a scenario save
  python pe.py scenarios             the scenario saves

A failed check keeps everything under runs/<suite>/<time>/<check>/failure/: the game's snapshot,
a picture, its event log, the last states and inputs, stderr and the mods' log.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from pedebug import config  # noqa: E402

LIVE_DEFAULT = ROOT / "runs" / "live-ctl"
LIVE_MARKER = ".pe-play-channel"   # marks a debug channel folder pe.py play may clear
OK, BAD, WARN = "ok  ", "FAIL", "warn"


def _jobs_default() -> int:
    return max(1, min(4, (os.cpu_count() or 2) // 2))


# ---- setup / doctor ----

def cmd_setup(a) -> int:
    cfg = config.load(discover_missing=False)
    if a.exe:
        cfg.exe = str(Path(a.exe).resolve())
    if a.fixtures:
        cfg.fixtures = str(Path(a.fixtures).resolve())
    if a.disc1:
        cfg.disc1, cfg.disc2 = a.disc1, a.disc2 or a.disc1.replace("Disc 1", "Disc 2")
    if a.rescan:
        cfg.exe = cfg.exe if a.exe else ""
    config.discover(cfg)
    print(f"build     {cfg.exe or '(none found)'}")
    print(f"discs     {cfg.disc1 or '(none found)'}\n          {cfg.disc2}")
    print(f"fixtures  {cfg.fixtures}")
    print(f"runs      {cfg.runs}")
    path = cfg.save()
    print(f"\nwrote {path}; run `python pe.py doctor --boot` next")
    return 0 if cfg.exe and cfg.disc1 else 1


def _line(status, what, fix=""):
    print(f"[{status}] {what}" + (f"\n         fix: {fix}" if fix else ""))
    return status != BAD


def cmd_doctor(a) -> int:
    good = True
    good &= _line(OK if sys.version_info >= (3, 10) else BAD, f"Python {sys.version.split()[0]}",
                  "" if sys.version_info >= (3, 10) else "Python 3.10 or newer")
    cfg = config.load()
    _line(OK if config.CONFIG_FILE.exists() else WARN, f"config {config.CONFIG_FILE}",
          "" if config.CONFIG_FILE.exists() else "python pe.py setup (values below were discovered)")
    exe = Path(cfg.exe) if cfg.exe else None
    if not exe or not exe.is_file():
        good &= _line(BAD, "game build", "python pe.py build --use (this checkout), or pe.py setup --exe <path>")
    else:
        age_h = (time.time() - exe.stat().st_mtime) / 3600
        good &= _line(OK, f"build {exe} ({exe.stat().st_size // 2**20} MB, {age_h:.0f} h old)")
        dbg = config.debug_capable(exe)
        good &= _line(OK if dbg else BAD, "debug control with event log, ui-state, snapshot",
                      "" if dbg else "rebuild from a checkout whose debug channel has the event log (docs/DEBUG_CONTROL.md)")
        for mod in ("pe_bp", "pe_challenges"):
            has = b"\0" + mod.encode() + b"\0" in exe.read_bytes()
            _line(OK if has else WARN, f"opt-in mod {mod} {'built' if has else 'not built'}",
                  "" if has else 'pe.py build --use (opt-in mods included by default; their checks skip otherwise)')
        busy = _exe_locked(exe)
        _line(WARN if busy else OK, "build not locked by a running game" if not busy else "a running game holds the build",
              "close it before rebuilding" if busy else "")
    for n, d in ((1, cfg.disc1), (2, cfg.disc2)):
        ok = bool(d) and Path(d).is_file()
        good &= _line(OK if ok else BAD, f"disc {n} {d or '(none)'}", "" if ok else "pe.py setup --disc1 <cue>")
    cards = sorted(cfg.cards.glob("BASLUS-*")) if cfg.cards.is_dir() else []
    good &= _line(OK if cards else BAD, f"fixture cards: {len(cards)} in {cfg.cards}",
                  "" if cards else "copy your own save files to harness/fixtures/cards (see harness/fixtures/README.md)")
    have_c = any(c.name.endswith("C") for c in cards)
    _line(OK if have_c else WARN, "card C (most diag checks start from it)",
          "" if have_c else "a save in slot C at a phone, Day 2 or later (harness/fixtures/README.md)")
    scen = sorted(p.name for p in cfg.scenarios.iterdir() if (p / "scenario.json").exists()) if cfg.scenarios.is_dir() else []
    _line(OK if "wayne_day4" in scen else WARN, f"scenario saves: {', '.join(scen) or 'none'}",
          "" if "wayne_day4" in scen else "pe.py play, stand at Wayne's bench (Day 4+), pe.py capture wayne_day4")
    runs = Path(cfg.runs)
    runs.mkdir(parents=True, exist_ok=True)
    free = shutil.disk_usage(runs).free // 2**30
    good &= _line(OK if free >= 2 else BAD, f"{free} GB free for runs ({runs})", "" if free >= 2 else "free some space")
    if a.boot and good:
        from pedebug import runner
        runner.load_checks()
        out, results = runner.run(runner.select(["boot-title", "field-commands"]), suite="doctor", jobs=2, cfg=cfg)
        for r in results:
            good &= _line(OK if r.status == "pass" else BAD, f"{r.name}: {r.status} ({r.seconds:.0f} s) {r.message[:120]}",
                          "" if r.status == "pass" else f"see {Path(r.folder) / 'failure'}")
    print("\nready" if good else "\nnot ready: fix the FAIL lines above")
    return 0 if good else 1


def _exe_locked(exe: Path) -> bool:
    try:
        with open(exe, "r+b"):
            return False
    except PermissionError:
        return True
    except OSError:
        return False


# ---- checks ----

def _run(names, tags, a, suite) -> int:
    from pedebug import runner
    runner.load_checks()
    checks = runner.select(names, tags)
    if not checks:
        print("no checks match; see python pe.py list")
        return 2
    if a.repeat > 1:
        checks = [runner.Check(f"{c.name}#{i + 1}", c.fn, c.tags, c.needs, c.description, c.timeout_s)
                  for c in checks for i in range(a.repeat)]
    cfg = config.load()
    if not cfg.exe:
        print("no game build found: python pe.py setup / doctor")
        return 2
    out, results = runner.run(checks, suite=suite, jobs=a.jobs, cfg=cfg)
    bad = [r for r in results if r.status in ("fail", "error")]
    print(f"\n{len(results) - len(bad)} of {len(results)} ok; report {out / 'report.md'}")
    for r in bad:
        print(f"  {r.name}: {r.message.splitlines()[0] if r.message else r.status}\n    -> {Path(r.folder) / 'failure'}")
    return 1 if bad else 0


def cmd_diag(a) -> int:
    tags = ["all"] if a.full else ["quick"] if a.quick else ["diag"]
    return _run([], tags, a, "full" if a.full else "diag-quick" if a.quick else "diag")


def cmd_run(a) -> int:
    return _run(a.names, a.tag or [], a, a.suite)


def cmd_list(a) -> int:
    from pedebug import runner
    runner.load_checks()
    cfg = config.load()
    for c in sorted(runner.REGISTRY.values(), key=lambda c: c.name):
        if a.tag and not set(a.tag) & set(c.tags):
            continue
        missing = runner.missing_needs(c, cfg)
        print(f"{c.name:22} [{', '.join(c.tags)}]{'  SKIPS: ' + '; '.join(missing) if missing else ''}")
        if a.verbose and c.description:
            print("    " + c.description.replace("\n", "\n    "))
    return 0


def cmd_report(a) -> int:
    cfg = config.load()
    latest = Path(cfg.runs) / a.suite / "latest.txt"
    if not latest.exists():
        print(f"no {a.suite} run yet")
        return 1
    rep = Path(latest.read_text().strip()) / "report.md"
    print(rep.read_text())
    print(rep)
    return 0


# ---- build / play / live ----

def cmd_build(a) -> int:
    """Configure and build a checkout (default: this one) with mods and the opt-in mods. CMake,
    Ninja and a C compiler come from PATH, or from --toolchain DIR / PE_TOOLCHAIN (a folder with
    cmake, ninja and clang)."""
    src = Path(a.checkout).resolve() if a.checkout else config.REPO
    build = src / a.dir
    env = dict(os.environ)
    tool = a.toolchain or os.environ.get("PE_TOOLCHAIN")
    extra = []
    if tool:
        tool = Path(tool)
        env["PATH"] = str(tool) + os.pathsep + env.get("PATH", "")
        exe_ext = ".exe" if os.name == "nt" else ""
        if (tool / f"clang{exe_ext}").exists():
            extra.append(f"-DCMAKE_C_COMPILER={tool / ('clang' + exe_ext)}")
    cmake, ninja = shutil.which("cmake", path=env["PATH"]), shutil.which("ninja", path=env["PATH"])
    if not cmake:
        print("no cmake on PATH: install CMake (and Ninja), or pass --toolchain DIR")
        return 2
    if not (src / "third_party/psyz/psyz").exists():
        print("fetching submodules ...")
        subprocess.run(["git", "submodule", "update", "--init", "--recursive"], cwd=src, env=env, check=False)
    conf = [cmake, "-S", str(src), "-B", str(build)] + (["-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}"] if ninja else []) + \
        ["-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DPE_MODS=ON", f"-DBUILD_TESTING={'ON' if a.tests else 'OFF'}",
         f"-DPE_EXTRA_MODS={a.extra_mods}"] + extra
    if subprocess.run(conf, env=env).returncode:
        return 1
    target = [] if a.tests else ["--target", "parasite-eve-pc"]
    if subprocess.run([cmake, "--build", str(build), "--parallel", str(os.cpu_count() or 4)] + target, env=env).returncode:
        return 1
    exe = next((build / n for n in config.EXE_NAMES if (build / n).exists()), build / config.EXE_NAMES[0])
    print(f"built {exe}; debug control: {'yes' if config.debug_capable(exe) else 'NO'}")
    if a.use:
        cfg = config.load(discover_missing=False)
        cfg.exe = str(exe)
        config.discover(cfg).save()
        print("harness.json now uses it")
    return 0


def cmd_play(a) -> int:
    cfg = config.load()
    folder = Path(a.dir).resolve()
    folder.mkdir(parents=True, exist_ok=True)
    # Only a folder this command made is cleared: --dir pointing at one with other files is refused.
    marker = folder / LIVE_MARKER
    files = [f for f in folder.glob("*") if f.is_file() and f.name != LIVE_MARKER]
    if files and not marker.exists() and folder != LIVE_DEFAULT.resolve():
        print(f"{folder} has other files in it; give --dir an empty folder (or one pe.py play made)")
        return 1
    for f in files:
        f.unlink()
    marker.touch()
    env = dict(os.environ, PE_DEBUG_CONTROL=str(folder))
    args = [cfg.exe, "--disc", cfg.disc1, "--disc2", cfg.disc2]
    print(f"starting {cfg.exe}\n  debug channel: {folder}\n  then: python pe.py live state  (or watch, snapshot, ...)")
    subprocess.Popen(args, env=env, cwd=Path(cfg.exe).parent)
    return 0


def cmd_live(a) -> int:
    from pedebug.control import LiveGame
    from pedebug.game import GameError
    folder = Path(a.dir or os.environ.get("PE_DEBUG_CONTROL") or LIVE_DEFAULT)
    try:
        g = LiveGame(folder)
        what = a.what
        if what == "state":
            print(g.command("room"))
            ui = g.ask_json("ui-state")
            b = g.ask_json("battle-state")
            print("menu", json.dumps(ui["menu"]))
            for box in ui["dialogue"]:
                print(f"dialogue page {box['page']} {box['state']}: {box['text'][:100]}")
            p = b["player"]
            if p:
                print(f"Aya hp {p['hp']}/{p['max_hp']} level {p['level']} bp {p['bp']}; battle {b['active']} enemies {len(b['enemies'])}")
            for e in g.events.tail(5):
                print("event", json.dumps(e)[:150])
        elif what in ("ui", "battle"):
            print(json.dumps(g.ask_json("ui-state" if what == "ui" else "battle-state"), indent=1))
        elif what == "mod":
            print(json.dumps(g.ask_json(f"mod-state {a.arg}"), indent=1))
        elif what == "snapshot":
            print(g.snapshot(a.arg or ""))
        elif what == "events":
            for e in g.events.tail(a.n):
                print(json.dumps(e))
        elif what == "watch":
            g.events.poll()
            print(f"watching {g.events.path} (Ctrl+C stops)")
            while True:
                for e in g.events.poll():
                    print(json.dumps(e)[:200], flush=True)
                time.sleep(0.1)
        elif what == "cmd":
            print(g.command(a.arg))
    except GameError as e:
        print(e)
        return 1
    except KeyboardInterrupt:
        pass
    return 0


def cmd_capture(a) -> int:
    """The playing game's spot as a scenario save: the card file a save to slot A would write now
    (with its companion file), the room and the actors. Stand in free field first."""
    from pedebug.control import LiveGame
    from pedebug.game import GameError
    cfg = config.load()
    try:
        d = config.scenario_dir(cfg.scenarios, a.name)
    except ValueError as e:
        print(e)
        return 1
    folder = Path(a.dir or os.environ.get("PE_DEBUG_CONTROL") or LIVE_DEFAULT)
    try:
        g = LiveGame(folder)
        print("asking the game (click back into its window if it pauses when unfocused) ...")
        room_line = g.command("room", timeout=120)
        words = g.command("save-block 1", timeout=120).split()
        actors_line = g.command("actors", timeout=120)
    except GameError as e:
        print(e)
        return 1
    card = words[2]
    companion = next((w.split("=", 1)[1] for w in words[3:] if w.startswith("companion=")), None)
    if d.exists():
        shutil.rmtree(d)
    d.mkdir(parents=True)
    shutil.copyfile(folder / "block.bin", d / card)
    files = [card]
    if companion:
        (d / ".pe-ext").mkdir()
        shutil.copyfile(folder / "companion.pex", d / ".pe-ext" / companion)
        files.append(".pe-ext/" + companion)
    room = dict(kv.split("=", 1) for kv in room_line.split()[2:])
    meta = {"card": card, "files": files,
            "room": {"map": int(room["map"]), "name": room["name"], "token": int(room["token"], 16),
                     "story": int(room["story"], 16), "day": int(room["day"]), "free": room["free"] == "1"},
            "actors_line": actors_line, "note": a.note}
    (d / "scenario.json").write_text(json.dumps(meta, indent=1))
    print(f"saved {d} (map {meta['room']['map']}, story {meta['room']['story']:X}, day {meta['room']['day']})")
    return 0


def cmd_scenarios(a) -> int:
    cfg = config.load()
    rows = sorted(cfg.scenarios.glob("*/scenario.json")) if cfg.scenarios.is_dir() else []
    for f in rows:
        m = json.loads(f.read_text())
        print(f"{f.parent.name:24} map {m['room']['map']:3} story {m['room']['story']:X} day {m['room']['day']}  {m.get('note', '')}")
    if not rows:
        print(f"no scenario saves in {cfg.scenarios}: pe.py play, go somewhere, pe.py capture NAME")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("setup")
    p.add_argument("--exe"); p.add_argument("--fixtures"); p.add_argument("--disc1"); p.add_argument("--disc2")
    p.add_argument("--rescan", action="store_true", help="pick the newest debug-capable build again")
    p.set_defaults(fn=cmd_setup)
    p = sub.add_parser("doctor"); p.add_argument("--boot", action="store_true"); p.set_defaults(fn=cmd_doctor)
    p = sub.add_parser("list"); p.add_argument("--tag", action="append"); p.add_argument("-v", "--verbose", action="store_true")
    p.set_defaults(fn=cmd_list)
    for name, fn in (("diag", cmd_diag), ("run", cmd_run)):
        p = sub.add_parser(name)
        if name == "run":
            p.add_argument("names", nargs="*")
            p.add_argument("--tag", action="append")
            p.add_argument("--suite", default="run")
        else:
            p.add_argument("--quick", action="store_true", help="boot and field only (about a minute)")
            p.add_argument("--full", action="store_true", help="every check: saves, mods on/off, economy, challenges too")
        p.add_argument("--jobs", type=int, default=_jobs_default())
        p.add_argument("--repeat", type=int, default=1, help="run each check N times (flakiness)")
        p.set_defaults(fn=fn)
    p = sub.add_parser("report"); p.add_argument("--suite", default="diag"); p.set_defaults(fn=cmd_report)
    p = sub.add_parser("build")
    p.add_argument("checkout", nargs="?", help="a game checkout (default: this repository)")
    p.add_argument("--dir", default="build-harness")
    p.add_argument("--extra-mods", default="pe_assists;pe_bp;pe_challenges"); p.add_argument("--tests", action="store_true")
    p.add_argument("--toolchain", help="a folder with cmake, ninja and clang (or PE_TOOLCHAIN)")
    p.add_argument("--use", action="store_true", help="point harness.json at the result")
    p.set_defaults(fn=cmd_build)
    p = sub.add_parser("capture"); p.add_argument("name"); p.add_argument("--note", default="")
    p.add_argument("--dir"); p.set_defaults(fn=cmd_capture)
    p = sub.add_parser("scenarios"); p.set_defaults(fn=cmd_scenarios)
    p = sub.add_parser("play"); p.add_argument("--dir", default=str(LIVE_DEFAULT)); p.set_defaults(fn=cmd_play)
    p = sub.add_parser("live")
    p.add_argument("what", choices=["state", "ui", "battle", "mod", "snapshot", "events", "watch", "cmd"])
    p.add_argument("arg", nargs="?"); p.add_argument("--dir"); p.add_argument("-n", type=int, default=20)
    p.set_defaults(fn=cmd_live)
    a = ap.parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
