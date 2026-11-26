"""Saving at a phone (with a call), Wayne's menu, soft reset and New Game, through the game's own
screens, checked against the event log, the cards written and the mods' live state."""
from __future__ import annotations

from pedebug import flows, places
from pedebug.runner import check

from ._common import ALL_MODS, STAY_ON_LINE_PAGE, phone_save

WAYNE_MENU_PAGE = 164
WAYNE_SPOT = (24500995, -1852047, 2048)


@check("phone-save-call", tags=("diag", "save", "phone"), needs=("card:C",), timeout_s=480)
def phone_save_call(ctx):
    """Save to slot B at the phone, stay on the line, hear the call: save_committed, card B on
    disk, the call's pages logged, and pe_phone's heard list grows by one."""
    card = ctx.card("C")
    s = ctx.session(cards=[card], ini=ALL_MODS)
    flows.load_save(s, flows.slot_of(card))
    before = s.live("pe_phone", "calls")
    ctx.require(before is not None, "pe_phone is running with live state")
    mark = s.events.mark()
    phone_save(s, number=2, stay_on_line=True)
    ctx.expect(s.events.find("save_committed", since=mark), "save_committed logged")
    ctx.expect(any(f.name.endswith("B") for f in s.card_files()), "card file B written")
    q = s.events.find("dialogue_waiting", since=mark, page=STAY_ON_LINE_PAGE)
    ctx.expect(q and "Stay on the line" in q["text"], "the phone asked to stay on the line")
    pages = [e for e in s.events.since(mark) if e["event"] == "dialogue_changed" and e["page"] == STAY_ON_LINE_PAGE]
    ctx.expect(len(pages) >= 2, f"the call's pages were shown ({len(pages)})")
    after = s.live("pe_phone", "calls")
    ctx.expect(after["heard_count"] == before["heard_count"] + 1, "one more call heard")
    ctx.fact("call", ", ".join(sorted(set(after["heard"]) - set(before["heard"]))) or "?")
    ctx.fact("last phone", after["last_phone"])


@check("wayne-menu", tags=("diag", "dialogue"), needs=("scenario:wayne_day4",), timeout_s=300)
def wayne_menu(ctx):
    """Talk to Wayne: his menu waits with the economy's extra rows (dialogue_changed), the cursor
    moves with the pad (ui-state), Cancel closes it with the game's own row number."""
    s = places.open_scenario(ctx.cfg.exe, ctx.dir / "s1", "wayne_day4")
    ctx.sessions.append(s)
    places.place(s, *WAYNE_SPOT)
    s.advance(20)
    mark = s.events.mark()
    flows.talk(s, pages=(WAYNE_MENU_PAGE,))
    s.events.wait("dialogue_waiting", since=mark, page=WAYNE_MENU_PAGE, timeout=600)
    changed = s.events.wait("dialogue_changed", since=mark, page=WAYNE_MENU_PAGE, timeout=120)
    ctx.expect(changed["choices"] == 7 and "Workshop" in changed["text"], "the economy adds Workshop (7 rows)")
    box = s.ui()["dialogue"][0]
    ctx.expect(box["choices"] == 7 and box["choice"] == 0, "ui-state: 7 choices, cursor on the first")
    s.press("down", hold=4, after=10)
    ctx.expect(s.ui()["dialogue"][0]["choice"] == 1, "Down moved the cursor to row 1")
    for _ in range(5):
        s.press("down", hold=4, after=10)
    s.press("cross", hold=4, after=30)                              # Cancel
    closed = s.events.wait("dialogue_closed", since=mark, page=WAYNE_MENU_PAGE, timeout=300)
    ctx.expect(closed.get("answer") == 5, f"Cancel reaches the script as the game's row 5 ({closed.get('answer')})")
    ctx.fact("junk", (s.live("pe_economy", "economy") or {}).get("junk"))


@check("reset-new-game", tags=("diag", "title"), needs=("card:C",), timeout_s=480)
def reset_new_game(ctx):
    """Soft reset from the field back to the title, then New Game into the opening rooms."""
    card = ctx.card("C")
    s = ctx.session(cards=[card], ini=ALL_MODS)
    flows.load_save(s, flows.slot_of(card))
    mark = s.events.mark()
    flows.soft_reset(s)
    left = s.events.wait("to_title", since=mark, timeout=600)
    ctx.expect(left["from_map"] == 87, f"left from save C's room (87): {left['from_map']}")
    s.events.wait("title", since=mark, timeout=12000, step=60)
    mark = s.events.mark()
    flows.new_game(s, from_boot=False)
    ctx.expect(s.events.find("new_game", since=mark), "new_game logged")
    room = s.events.wait("scene_loaded", since=mark, timeout=600)
    ctx.fact("first room", room["map"])
    ctx.expect(s.state.hp == 45 and s.state.max_hp == 45, "a new Aya: 45/45 HP")
    econ = s.live("pe_economy", "economy")
    ctx.expect(econ is None or econ["junk"] == 0, "a new game starts with no Junk")
