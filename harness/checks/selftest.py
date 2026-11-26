"""The harness checking itself: failure capture, and the live client against a running game.
Tag "selftest" (not part of diag). `python pe.py run --tag selftest` should report
capture-demo as FAILED (on purpose) with a full failure folder, and live-client as passed."""
from __future__ import annotations

import json
import threading

from pedebug import flows
from pedebug.control import LiveGame
from pedebug.runner import check

from ._common import ALL_MODS


@check("capture-demo", tags=("selftest",), needs=("card:C",), timeout_s=240)
def capture_demo(ctx):
    """Fails on purpose after loading save C: the failure folder must hold the snapshot, the
    frame, the event log, states and inputs (verify with selftest-capture)."""
    card = ctx.card("C")
    s = ctx.session(cards=[card], ini=ALL_MODS)
    flows.load_save(s, flows.slot_of(card))
    ctx.expect(False, "this expectation fails on purpose")
    ctx.require(False, "and this one ends the check, on purpose")


@check("live-client", tags=("selftest",), needs=("card:C",), timeout_s=240)
def live_client(ctx):
    """pe.py live's client (wall-clock waits, no stepping) against a game kept running by
    another thread: commands, JSON answers, events and a snapshot."""
    card = ctx.card("C")
    s = ctx.session(cards=[card], ini=ALL_MODS)
    flows.load_save(s, flows.slot_of(card))
    stop = threading.Event()
    lock = threading.Lock()

    def keep_running():
        while not stop.is_set():
            with lock:
                s.advance(10)

    t = threading.Thread(target=keep_running, daemon=True)
    t.start()
    try:
        g = LiveGame(s.ctl)
        ctx.expect(g.command("ping").startswith("ok ping"), "ping")
        ui = g.ask_json("ui-state")
        ctx.expect("menu" in ui and "dialogue" in ui, "ui-state JSON")
        ctx.expect(g.ask_json("battle-state")["active"] is False, "battle-state JSON")
        path = g.snapshot("live")
        snap = json.loads(path.read_text())
        ctx.expect(snap["name"] == "live" and "mods" in snap, "snapshot file")
        ctx.expect(g.events.last("scene_loaded") is not None, "events read from the file")
    finally:
        stop.set()
        t.join(5)
