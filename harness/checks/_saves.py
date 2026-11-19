"""Helpers for the save-data checks: a visit (load a card, settle, read, save through the game's
own card builder), the records save most checks start from (made once per run), the mods in
the build, and comparisons of the mods' data on the card and in its companion file."""
from __future__ import annotations

import re
import threading
from dataclasses import dataclass, field
from pathlib import Path

from pedebug import card as pc
from pedebug import flows
from pedebug.game import GameError

ALL_ON = "Manage mods/All mods=1\n"
KEYS = ["0xC8", "0xC9", "0xCA"]          # save C's pocketed key items
# Records whose bytes change on every save by design (the companion's generation link).
VOLATILE = {"pe_host.extlink"}


def ini(off_names=(), all_on: bool = True, extra: str = "") -> str:
    """pe_mods.ini: All mods on or off, and these mods (by their Manage mods name) off."""
    lines = [f"Manage mods/All mods={1 if all_on else 0}"] + [f"Manage mods/{n}=0" for n in off_names]
    return "\n".join(lines) + "\n" + extra


@dataclass
class SaveFiles:
    """A save as the game would write it: the card file and its companion files."""
    folder: Path
    card: Path
    companions: dict = field(default_factory=dict)       # ".pe-ext/<name>": bytes

    @property
    def extra_files(self) -> dict:
        """For Session(extra_files=): the companions beside the card in bu00."""
        return {"bu00/" + k: v for k, v in self.companions.items()}

    def mods(self) -> pc.ModData:
        return pc.Card.load(self.card).mods()

    def sections(self) -> dict:
        out = {}
        for data in self.companions.values():
            out.update(pc.parse_companion(data))
        return out


@dataclass
class Visit:
    bag: list
    live: pc.ModData
    saved: SaveFiles
    log: list
    started: dict                 # mod id -> Manage mods name


def bag_of(s) -> list[str]:
    return [f"0x{int(x, 16):X}" for x in s.command("bag").split()[-1].split(",") if x]


def keys_in(bag) -> list[str]:
    return [k for k in KEYS if k in bag]


def started_mods(log) -> dict[str, str]:
    """{mod id: Manage mods name} from the mods' log ("pe_x: started <Name> <version>")."""
    out = {}
    for line in log:
        m = re.match(r"(\w+): started (.+) (\S+)$", line)
        if m:
            out[m.group(1)] = m.group(2)
    return out


def visit(ctx, step: str, save: Path | SaveFiles, mods_ini: str = ALL_ON, before_save=None) -> Visit:
    """Load `save` (a card file, or SaveFiles with its companion), let the room settle, read the
    bag and the live mods' data, then save as the game would (save-block, companion included).
    before_save(s) may change things first. The session stays open for capture until the check
    ends; it is closed here once done."""
    card = save.card if isinstance(save, SaveFiles) else Path(save)
    extra = save.extra_files if isinstance(save, SaveFiles) else None
    s = ctx.session(cards=[card], ini=mods_ini, name=step, extra_files=extra)
    flows.load_save(s, flows.slot_of(card))
    s.advance(120)
    if before_save:
        before_save(s)
    bag = bag_of(s)
    live = pc.parse_tail(s.mods_tail())
    files = s.save_files(1)
    out = ctx.dir / step / "saved"
    out.mkdir(parents=True, exist_ok=True)
    card_name = next(k for k in files if "/" not in k)
    (out / card_name).write_bytes(files[card_name])
    comps = {k: v for k, v in files.items() if "/" in k}
    for k, v in comps.items():
        (out / k).parent.mkdir(parents=True, exist_ok=True)
        (out / k).write_bytes(v)
    log = s.mods_log()
    s.quit()
    ctx.sessions.remove(s)
    return Visit(bag, live, SaveFiles(out, out / card_name, comps), log, started_mods(log))


_records_lock = threading.Lock()
_records_cache: dict = {}


def records_save(ctx) -> Visit:
    """Save C loaded with every mod on and saved again: the current-format save (records and
    companion) the other checks start from. Made once per run and build, shared by checks."""
    key = (ctx.cfg.exe, str(ctx.dir.parent))
    with _records_lock:
        if key not in _records_cache:
            _records_cache[key] = visit(ctx, "0-records-save", ctx.card("C"))
        return _records_cache[key]


# ---- comparing the mods' data ----

def records(md: pc.ModData) -> dict[str, tuple[int, bytes]]:
    return {r.name: (r.version, r.data) for r in md.records}


def record_diffs(a: pc.ModData, b: pc.ModData, only=None, skip=VOLATILE) -> list[str]:
    """Records whose version or bytes differ (or that one side lacks), as readable lines."""
    ra, rb = records(a), records(b)
    names = sorted(set(ra) | set(rb)) if only is None else list(only)
    out = []
    for n in names:
        if n in skip:
            continue
        x, y = ra.get(n), rb.get(n)
        if x != y:
            if x is None or y is None:
                out.append(f"{n}: {'missing' if x is None else 'present'} -> {'missing' if y is None else 'present'}")
            else:
                out.append(f"{n}: v{x[0]} {len(x[1])} bytes -> v{y[0]} {len(y[1])} bytes")
    return out


def section_diffs(a: SaveFiles, b: SaveFiles) -> list[str]:
    sa, sb = a.sections(), b.sections()
    return [f"companion {k}: {'missing' if k not in sa else 'v%d %d bytes' % (sa[k][0], len(sa[k][1]))} -> "
            f"{'missing' if k not in sb else 'v%d %d bytes' % (sb[k][0], len(sb[k][1]))}"
            for k in sorted(set(sa) | set(sb)) if sa.get(k) != sb.get(k)]


def values(md: pc.ModData) -> dict:
    """Decoded values by record name (layout fields left out), for readable messages."""
    out = {}
    for r in md.records:
        d = dict(pc.decode(r))
        d.pop("layout", None)
        out[r.name] = d
    return out


def require_records(ctx):
    """GameError when the build's saves aren't the record container (an old build)."""
    base = records_save(ctx)
    if base.saved.mods().kind != "container":
        raise GameError(f"the build saved {base.saved.mods().kind}, not the record container")
    return base
