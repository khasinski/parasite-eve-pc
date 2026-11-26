"""The economy through the game's own screens, at Wayne's bench (scenario wayne_day4): buying from
Requisition in his Workshop, and handing Junk in with Quick hand-in off and on. Checked against
the mods' live state (pe_economy "economy", pe_shops "requisition") and the bag."""
from __future__ import annotations

import concurrent.futures as cf

from pedebug import flows, places
from pedebug.runner import check

from ._common import leave_choice

WAYNE_MENU_PAGE = 164                       # Tune up weapon ... Discard junk ... Workshop ... Cancel
WAYNE_SPOT = (24500995, -1852047, 2048)     # in front of his counter, facing him
DISCARD_ROW, WORKSHOP_ROW = 3, 5
CONFIRM = 41                                # the game's Yes / No window (menu selection)
WAYNE_COUNT_VAR = 48                        # his Junk count (script variable)


def _wayne_menu(ctx, name, ini=None):
    s = places.open_scenario(ctx.cfg.exe, ctx.dir / name, "wayne_day4", disc1=ctx.cfg.disc1, disc2=ctx.cfg.disc2,
                             **({"mods_ini": ini} if ini else {}))
    ctx.sessions.append(s)
    places.place(s, *WAYNE_SPOT)
    s.advance(20)
    mark = s.events.mark()
    flows.talk(s, pages=(WAYNE_MENU_PAGE,))
    s.events.wait("dialogue_waiting", since=mark, page=WAYNE_MENU_PAGE, timeout=600)
    s.advance(30)
    return s


def _junk(s) -> int:
    return s.live("pe_economy", "economy")["junk"]


def _bag_count(s, item: int) -> int:
    return [int(x, 16) for x in s.command("bag").split()[-1].split(",") if x].count(item)


@check("requisition-buy", tags=("diag", "economy"), needs=("scenario:wayne_day4",), timeout_s=420)
def requisition_buy(ctx):
    """Wayne > Workshop > Requisition: buy the first goods row through the game's Yes / No. Junk
    drops by its price and the item is carried; Cancel buys nothing; back out to the field."""
    s = _wayne_menu(ctx, "s1")
    board = s.live("pe_shops", "requisition")["board"]
    goods = board[0]
    ctx.require(goods["blocked"] is None, f"the first row can be bought: {goods}")
    junk0, have0 = _junk(s), _bag_count(s, goods["id"])
    for _ in range(WORKSHOP_ROW):
        s.press("down", hold=4, after=10)
    s.press("cross", hold=4, after=90)                         # Workshop
    s.wait(lambda st: st.menu is not None, timeout=300, what="the Workshop screen")
    s.press("cross", hold=4, after=60)                         # Requisition
    s.press("cross", hold=4, after=60)                         # the first row
    ctx.require(flows._menu_sel(s.state) == CONFIRM, f"the Yes / No window: {s.state.menu}")
    ctx.expect(s.state.menu["cursor"][0] == 1, "Cancel is selected first")
    s.press("cross", hold=4, after=60)                         # Cancel
    ctx.expect(flows._menu_sel(s.state) != CONFIRM, "Cancel closed the window")
    s.press("cross", hold=4, after=60)                         # ask again
    s.press("left", hold=4, after=20)                          # OK
    s.press("cross", hold=4, after=90)
    for _ in range(6):                                         # back out: Requisition, then the Workshop
        if s.state.menu is None:
            break
        s.press("circle", hold=4, after=60)
    leave_choice(s, WAYNE_MENU_PAGE)                           # Wayne's menu: his Cancel row
    junk1, have1 = _junk(s), _bag_count(s, goods["id"])
    ctx.expect(junk0 - junk1 == goods["price"], f"Junk {junk0} -> {junk1} (price {goods['price']}; Cancel bought nothing)")
    ctx.fact("bought", f"{goods['name']} for {goods['price']} Junk")
    ctx.expect(have1 >= have0, f"{goods['name']} carried (bag slots {have0} -> {have1})")
    ctx.note(f"bag slots with {goods['name']}: {have0} -> {have1} (stacked items may share a slot)")


def _hand_in(ctx, quick: int) -> dict:
    ini = f"Manage mods/All mods=1\nPE Enhancements/Economy/Quick hand-in={quick}\n"
    s = _wayne_menu(ctx, f"quick-{quick}", ini)
    places.set_var(s, WAYNE_COUNT_VAR, 0)                      # the same start every run
    junk0 = _junk(s)
    for _ in range(DISCARD_ROW):
        s.press("down", hold=4, after=10)
    s.press("cross", hold=4, after=1)
    start, done = s.state.vblanks, None
    while s.state.vblanks - start < 9000:
        s.advance(30)
        if places.get_var(s, WAYNE_COUNT_VAR) >= 300:
            done = s.state.vblanks - start
            break
    for _ in range(80):                                        # his lines and the gun offer: defaults
        if flows.free_field(s.state):
            break
        if any(b.get("page") == WAYNE_MENU_PAGE for b in s.state.dialogue):
            leave_choice(s, WAYNE_MENU_PAGE)                   # his menu again: Cancel
            break
        s.press("cross", hold=4, after=40)
    flows.in_field(s, what="free field after the hand-in")
    return {"quick": quick, "frames": done, "junk_before": junk0, "junk_after": _junk(s),
            "wayne_after": places.get_var(s, WAYNE_COUNT_VAR)}


@check("wayne-handin", tags=("economy",), needs=("scenario:wayne_day4",), timeout_s=600)
def wayne_handin(ctx):
    """Discard junk at Wayne's with Quick hand-in off and on (two games at once): both reach his
    300, end with the same Junk, and Quick hand-in is much faster."""
    with cf.ThreadPoolExecutor(max_workers=2) as pool:
        off, on = pool.map(lambda q: _hand_in(ctx, q), (0, 1))
    for r in (off, on):
        ctx.expect(r["frames"] is not None, f"quick {r['quick']}: Wayne reached 300 ({r})")
    ctx.expect(off["junk_after"] == on["junk_after"], f"same Junk after: off {off['junk_after']}, on {on['junk_after']}")
    ctx.expect(off["junk_before"] - off["junk_after"] == 300, f"300 Junk handed in: {off}")
    if off["frames"] and on["frames"]:
        ctx.expect(on["frames"] * 5 < off["frames"], f"quick hand-in faster: {on['frames']} vs {off['frames']} frames")
    ctx.fact("frames to 300", f"off {off['frames']}, on {on['frames']}")
