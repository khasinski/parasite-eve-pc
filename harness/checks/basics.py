"""The game boots headless, loads a save and answers every debug command with sane values."""
from __future__ import annotations

import time

from pedebug import flows
from pedebug.runner import check

from ._common import ALL_MODS

# Live-state providers in a default build: mod id -> provider name -> keys it must report.
DEFAULT_PROVIDERS = {
    "pe_economy": ("economy", ("junk", "store", "path", "progress")),
    "pe_shops": ("requisition", ("board", "rescue_needed")),
    "pe_phone": ("calls", ("mode", "calls", "heard")),
}


@check("boot-title", tags=("diag", "quick", "boot"), timeout_s=240)
def boot_title(ctx):
    """Boots headless to the title; the event log records it."""
    t0 = time.time()
    s = ctx.session(cards=None, ini=ALL_MODS)
    flows.to_title(s)
    s.events.wait("title", since=0, timeout=3000)
    ctx.require(s.state.scene == flows.TITLE_SCENE, f"at the title scene, not {s.state.scene:#x}")
    ctx.fact("boot to title", f"{s.state.vblanks} VBlanks, {time.time() - t0:.0f} s")


@check("field-commands", tags=("diag", "quick", "field"), needs=("card:C",), timeout_s=300)
def field_commands(ctx):
    """Loads save C; ping, room, actors, bag, ui-state, battle-state, snapshot and the default
    mods' live state all answer with consistent values."""
    t0 = time.time()
    card = ctx.card("C")
    s = ctx.session(cards=[card], ini=ALL_MODS)
    flows.load_save(s, flows.slot_of(card))
    loaded = s.events.wait("scene_loaded", since=0, timeout=600)
    ctx.expect(s.events.find("save_loaded"), "save_loaded logged")
    ctx.fact("boot to field", f"{s.state.vblanks} VBlanks, {time.time() - t0:.0f} s")
    room = s.command("room")
    ctx.expect(f"map={loaded['map']}" in room, f"room agrees with scene_loaded (map {loaded['map']}): {room}")
    ctx.expect("free=1" in room, "free field after loading")
    actors = s.command("actors").split()[2:]
    ctx.expect(any(a.split("@")[0].endswith("*") for a in actors), "Aya is among the actors")
    bag = s.command("bag").split()[2].split(",")
    ctx.expect(len(bag) >= 10, f"bag has slots: {len(bag)}")
    ui = s.ui()
    ctx.expect(ui["menu"]["open"] is False and ui["dialogue"] == [], "no menu or dialogue in free field")
    battle = s.battle()
    ctx.expect(battle["active"] is False and battle["player"] is not None, "no battle; Aya's stats readable")
    ctx.expect(battle["weapon"] is not None, "an equipped weapon")
    snap = s.snapshot("field")
    for key in ("room", "ui", "battle", "bag", "actors", "mods", "history"):
        ctx.expect(key in snap, f"snapshot has {key}")
    ctx.expect(snap["room"]["map"] == loaded["map"], "snapshot room matches")
    for mod, (provider, keys) in DEFAULT_PROVIDERS.items():
        st = s.mod_state(mod)
        live = (st.get("live") or {}).get(provider)
        if ctx.expect(st["running"] and live is not None, f"{mod} running with live {provider}"):
            for k in keys:
                ctx.expect(k in live, f"{mod}.{provider} reports {k}")
        ctx.expect(mod in snap["mods"], f"snapshot carries {mod}'s live state")
    ctx.fact("junk", (s.live("pe_economy", "economy") or {}).get("junk"))
    rng = s.command("rng-get").split()
    ctx.expect(len(rng) == 6 and len(rng[5]) == 1042, "rng-get returns state, seed, index and 521 bytes")


@check("bp-challenges-state", tags=("diag", "mods"), needs=("card:C", "mod:pe_bp", "mod:pe_challenges"), timeout_s=300)
def opt_in_state(ctx):
    """The opt-in mods' live state (BP, Challenges) answers on a loaded save."""
    card = ctx.card("C")
    s = ctx.session(cards=[card], ini=ALL_MODS)
    flows.load_save(s, flows.slot_of(card))
    bp = s.live("pe_bp", "bp")
    if ctx.expect(bp is not None, "pe_bp live bp"):
        ctx.expect(bp["readable"] and isinstance(bp["bp"], int), "BP readable")
        ctx.expect(len(bp["mastery"]) == 12 and len(bp["defense"]) == 5, "12 Mastery lines, 5 Defense tracks")
        ctx.fact("unspent BP", bp["bp"])
    run = s.live("pe_challenges", "run")
    if ctx.expect(run is not None, "pe_challenges live run"):
        ctx.expect(isinstance(run["active"], list), "active challenges listed")
