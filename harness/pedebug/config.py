"""Where things are: the game build, the discs, the fixtures and the runs folder.

Read from harness/harness.json (git-ignored, written by `pe.py setup`), then overridden by
environment variables (PE_EXE, PE_DISC1, PE_DISC2, PE_FIXTURES, PE_RUNS). Anything still unset
is discovered: the newest build with the debug channel in this repository's build folders (and
in any extra search roots: folders holding builds, or checkouts with build folders), discs named
like the USA release in the search roots, harness/fixtures for the saves.
"""
from __future__ import annotations

import json
import os
from dataclasses import asdict, dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent          # harness/
REPO = ROOT.parent                                     # the game checkout
CONFIG_FILE = ROOT / "harness.json"
DISC_NAMES = ("Parasite Eve (USA) (Disc 1).cue", "Parasite Eve (USA) (Disc 2).cue")
# Strings only a build with the current debug channel holds (mods/host/debug/, docs/DEBUG_CONTROL.md).
DEBUG_MARKERS = (b"events.jsonl", b"ui-state", b"snapshot")
EXE_NAMES = ("parasite-eve-pc.exe", "parasite-eve-pc")


@dataclass
class Config:
    exe: str = ""
    disc1: str = ""
    disc2: str = ""
    fixtures: str = ""
    runs: str = ""
    search_roots: list[str] = field(default_factory=list)   # more places to look for builds and discs

    @property
    def cards(self) -> Path:
        return Path(self.fixtures) / "cards"

    @property
    def scenarios(self) -> Path:
        return Path(self.fixtures) / "scenarios"

    def save(self) -> Path:
        CONFIG_FILE.write_text(json.dumps(asdict(self), indent=2) + "\n")
        return CONFIG_FILE


def debug_capable(exe: Path) -> bool:
    """The build carries the current debug channel (event log, ui-state, snapshot)."""
    try:
        data = exe.read_bytes()
    except OSError:
        return False
    return all(m in data for m in DEBUG_MARKERS)


def _builds_in(folder: Path):
    for build in folder.glob("build*"):
        for name in EXE_NAMES:
            if (build / name).is_file():
                yield build / name


def find_builds(roots) -> list[Path]:
    """Game executables in the repository's build folders and the roots' (a root may be a
    checkout, or a folder of checkouts), newest first."""
    found = set(_builds_in(REPO))
    for root in map(Path, roots):
        if not root.is_dir():
            continue
        found.update(_builds_in(root))
        for checkout in root.iterdir():
            if checkout.is_dir() and not checkout.name.startswith((".", "_")):
                found.update(_builds_in(checkout))
    return sorted(found, key=lambda p: p.stat().st_mtime, reverse=True)


def find_discs(roots) -> tuple[str, str]:
    for root in map(Path, roots):
        for cand in (root, root / "Parasite Eve", root / "Parasite Eve Project" / "Parasite Eve"):
            d1, d2 = cand / DISC_NAMES[0], cand / DISC_NAMES[1]
            if d1.is_file() and d2.is_file():
                return str(d1), str(d2)
    return "", ""


def fixtures_dir() -> Path:
    """The fixtures folder without discovering anything else (cheap; for module constants)."""
    if os.environ.get("PE_FIXTURES"):
        return Path(os.environ["PE_FIXTURES"])
    if CONFIG_FILE.exists():
        f = json.loads(CONFIG_FILE.read_text()).get("fixtures")
        if f:
            return Path(f)
    return ROOT / "fixtures"


def discover(cfg: Config) -> Config:
    """Fill what is still empty."""
    if not cfg.exe:
        builds = [b for b in find_builds(cfg.search_roots) if debug_capable(b)]
        cfg.exe = str(builds[0]) if builds else ""
    if not cfg.disc1 or not cfg.disc2:
        cfg.disc1, cfg.disc2 = find_discs(cfg.search_roots + [str(REPO.parent)])
    if not cfg.fixtures:
        cfg.fixtures = str(ROOT / "fixtures")
    if not cfg.runs:
        cfg.runs = str(ROOT / "runs")
    return cfg


def load(discover_missing: bool = True) -> Config:
    cfg = Config()
    if CONFIG_FILE.exists():
        data = json.loads(CONFIG_FILE.read_text())
        for k, v in data.items():
            if hasattr(cfg, k):
                setattr(cfg, k, v)
    for key, env in (("exe", "PE_EXE"), ("disc1", "PE_DISC1"), ("disc2", "PE_DISC2"),
                     ("fixtures", "PE_FIXTURES"), ("runs", "PE_RUNS")):
        if os.environ.get(env):
            setattr(cfg, key, os.environ[env])
    return discover(cfg) if discover_missing else cfg
