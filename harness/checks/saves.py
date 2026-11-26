"""The mods' save data end to end: loads through the Continue screen, saves through the game's own
card builder (save-block), and the card files and companion files checked record by record.
Ported from pe-native-debug's mod_saves and edge_cases suites; record lists come from the build
itself, so new mods' records are covered without editing these checks."""
from __future__ import annotations

from pedebug import card as pc
from pedebug.game import GameError
from pedebug import flows
from pedebug.runner import check

from . import _saves as sv
from ._common import phone_save, through_dialogue

# Known game bug (fixed by khasinski/parasite-eve-pc PR 271): pe_save_peek and pe_save_prepare_load
# (mods/host/pe_save.c) refuse any damaged container, even with the companion link record intact,
# so pe_mod_load_admit refuses the whole save ("Data is unreadable") instead of the recovery
# pe_save_on_read documents (reset the damaged record, keep the rest). One bad record locks the
# player out of the save.
LOAD_BUG = "a damaged mods' container makes the whole save unloadable, link intact (pe_save_peek, pe_save_prepare_load)"


def _visit_or_refused(ctx, step, files):
    """visit(), or None when the game refused the save at the file list (the known bug)."""
    try:
        return sv.visit(ctx, step, files)
    except GameError as e:
        if "the room of save" in str(e) and ctx.sessions and ctx.sessions[-1].state.menu and                 ctx.sessions[-1].state.menu.get("selection") == 37:
            ctx.known_bug(False, "the save loads", LOAD_BUG)
            return None
        raise


@check("save-old-format-upgrades", tags=("saves",), needs=("card:C",), timeout_s=300)
def old_format_upgrades(ctx):
    """Save C is in the old per-mod block format: it loads into every mod and saves as the record
    container with the same values, keeping the key pocket and sort blocks in the window."""
    old = pc.Card.load(ctx.card("C")).mods()
    ctx.require(old.kind == "legacy", f"save C is in the old block format ({old.kind})")
    base = sv.records_save(ctx)
    new = base.saved.mods()
    ctx.expect(pc.Card.load(base.saved.card).crc_ok, "the saved card's game CRC")
    ctx.expect(new.kind == "container" and new.container_ok, f"saved as a valid container ({new.kind})")
    vo, vn = sv.values(old), sv.values(new)
    for name, fields in vo.items():
        for k, v in fields.items():
            if k in vn.get(name, {}) and vn[name][k] != v:
                ctx.expect(False, f"{name}.{k}: {v} became {vn[name][k]}")
    ctx.expect(set(pc.window_blocks(base.saved.card.read_bytes()[pc.TAIL_AT:])) >= {"pe_qol.keys", "pe_qol.sort"},
               "PKEY and SORT blocks kept in the window for builds without the container")
    ctx.expect(not sv.keys_in(base.bag), f"the pocketed keys stay out of the bag: {base.bag}")
    ctx.fact("records", ", ".join(f"{r.name} v{r.version}" for r in new.records))
    ctx.fact("companion sections", ", ".join(sorted(base.saved.sections())) or "none")


@check("save-round-trip", tags=("saves",), needs=("card:C",), timeout_s=300)
def round_trip(ctx):
    """A current-format save (records and companion) loads back exactly: saving it again gives the
    same records byte for byte and the same companion sections."""
    base = sv.require_records(ctx)
    again = sv.visit(ctx, "1-reload", base.saved)
    diffs = sv.record_diffs(base.saved.mods(), again.saved.mods()) + sv.section_diffs(base.saved, again.saved)
    ctx.expect(not diffs, "changed on the round trip: " + "; ".join(diffs))
    bad = [l for l in again.log if " save: " in l and ("upgraded" in l or l.endswith("reset"))]
    ctx.expect(not bad, "records upgraded or reset: " + "; ".join(bad))


@check("save-official-window", tags=("saves",), needs=("card:C",), timeout_s=300)
def official_window(ctx):
    """A build without the record container saved over the card: its key pocket (in the window) is
    newer than the record and wins; every other record stays."""
    base = sv.require_records(ctx)
    keys = [int(k, 16) for k in sv.values(base.saved.mods())["pe_qol.keys"]["keys"]] + [0xCB]
    c = pc.Card.load(base.saved.card)
    c.set_tail(pc.write_window_block(c.tail, "pe_qol.keys", pc.pkey_block(keys)))
    fix = ctx.dir / "fixture" / base.saved.card.name
    fix.parent.mkdir(parents=True, exist_ok=True)
    c.save(fix)
    ctx.require(pc.Card.load(fix).mods().window_changed, "fixture: the window changed")
    got = sv.visit(ctx, "1-load", sv.SaveFiles(fix.parent, fix, base.saved.companions))
    pocket = sv.values(got.live)["pe_qol.keys"]["keys"]
    ctx.expect("0xCB" in pocket, f"the other build's key 0xCB is in the pocket: {pocket}")
    others = [n for n in sv.records(base.saved.mods()) if n not in ("pe_qol.keys",)]
    diffs = sv.record_diffs(base.saved.mods(), got.saved.mods(), only=others)
    ctx.expect(not diffs, "other records changed: " + "; ".join(diffs))
    ctx.expect(not got.saved.mods().window_changed, "the next save writes the window CRC afresh")


@check("save-damaged-record", tags=("saves",), needs=("card:C",), timeout_s=300)
def damaged_record(ctx):
    """Another program wrote over one record: only that record resets, the rest load unchanged."""
    base = sv.require_records(ctx)
    victim = "pe_economy.catalog"
    t = pc.Card.load(base.saved.card).tail
    lin, at = bytearray(pc.gather(t)), pc.HEADER_SIZE
    for rec in pc.parse_tail(t).records:
        if rec.name == victim:
            lin[at + pc.RECORD_HEADER_SIZE] ^= 0xFF
            break
        at += pc.RECORD_HEADER_SIZE + len(rec.data)
    else:
        ctx.require(False, f"no {victim} record to damage")
    c = pc.Card.load(base.saved.card)
    c.set_tail(pc.scatter(bytes(lin), t[pc.WINDOW_AT:pc.WINDOW_END]))
    fix = ctx.dir / "fixture" / base.saved.card.name
    fix.parent.mkdir(parents=True, exist_ok=True)
    c.save(fix)
    got = _visit_or_refused(ctx, "1-load", sv.SaveFiles(fix.parent, fix, base.saved.companions))
    if got is None:
        return
    v = sv.values(got.live)
    ctx.expect(v[victim]["gun_effects"] == 0 and v[victim]["rebuild_count"] == 0, f"the damaged record reset: {v[victim]}")
    others = [n for n in sv.records(base.saved.mods()) if n != victim]
    diffs = sv.record_diffs(base.saved.mods(), got.live, only=others)
    ctx.expect(not diffs, "other records changed: " + "; ".join(diffs))
    ctx.expect(any("partly overwritten" in l for l in got.log), "the log reports the partial damage")


@check("save-newer-format-refused", tags=("saves",), needs=("card:C",), timeout_s=300)
def newer_format_refused(ctx):
    """A save whose mods' container is a newer format than this build reads is refused at the
    file list ("Data is unreadable"): its companion link and identity ledger can't be read, and
    the save's native data can't be interpreted safely without them. The card is left untouched;
    the newer build still loads it."""
    base = sv.require_records(ctx)
    c = pc.Card.load(base.saved.card)
    t = bytearray(c.tail)
    t[4] = 2
    c.set_tail(bytes(t))
    fix = ctx.dir / "fixture" / base.saved.card.name
    fix.parent.mkdir(parents=True, exist_ok=True)
    c.save(fix)
    before = fix.read_bytes()
    try:
        sv.visit(ctx, "1-load", sv.SaveFiles(fix.parent, fix, base.saved.companions))
        ctx.expect(False, "the newer-format save was loaded")
    except GameError as e:
        s = ctx.sessions[-1]
        ctx.expect("the room of save" in str(e) and flows._menu_sel(s.state) == 37,
                   f"refused back to the file list: {s.state.short()}")
        card = s.card_files()[0]
        ctx.expect(card.read_bytes() == before, "the card was left untouched")


@check("save-vanilla-defaults", tags=("saves",), needs=("card:C",), timeout_s=300)
def vanilla_defaults(ctx):
    """A save without mods' data: every mod starts from its defaults (no Junk, empty pocket) and
    the next save holds records."""
    c = pc.Card.load(ctx.card("C"))
    c.set_tail(b"\0" * pc.TAIL_SIZE)
    fix = ctx.dir / "fixture" / ctx.card("C").name
    fix.parent.mkdir(parents=True, exist_ok=True)
    c.save(fix)
    got = sv.visit(ctx, "1-load", fix)
    v = sv.values(got.saved.mods())
    ctx.expect(got.saved.mods().kind == "container", "saved as records")
    ctx.expect(v.get("pe_economy.loot", {}).get("junk") == 0, f"no Junk: {v.get('pe_economy.loot')}")
    ctx.expect(v.get("pe_qol.keys", {}).get("count") == 0, f"an empty pocket: {v.get('pe_qol.keys')}")


@check("save-phone-matches-block", tags=("saves",), needs=("card:C",), timeout_s=420)
def phone_matches_block(ctx):
    """A save through the phone (the game's own card transfer) holds the same mods' data as
    save-block made just before it."""
    base = sv.require_records(ctx)
    s = ctx.session(cards=[base.saved.card], ini=sv.ALL_ON, extra_files=base.saved.extra_files, name="1-phone")
    from pedebug import flows
    flows.load_save(s, flows.slot_of(base.saved.card))
    _, block = s.save_block(2)
    phone_save(s, number=2)
    files = {f.name[-1]: f for f in s.card_files()}
    ctx.require("B" in files, f"the phone wrote save No 2: {sorted(files)}")
    phone = pc.Card.load(files["B"])
    ctx.expect(phone.crc_ok, "the phone's card has a good game CRC")
    ctx.expect(phone.mods().kind == "container" and phone.mods().container_ok, "it holds a valid container")
    diffs = sv.record_diffs(pc.parse_tail(block[pc.TAIL_AT:]), phone.mods())
    ctx.expect(not diffs, "phone save and save-block differ: " + "; ".join(diffs))


@check("save-new-game-fresh", tags=("saves", "title"), needs=("card:C",), timeout_s=480)
def new_game_fresh(ctx):
    """After a loaded save, soft reset and New Game: every mod starts fresh. The new game's save
    holds no Junk, no Catalog unlocks, no Tools bought and an empty key pocket (the title's New
    Game sends no script command; PE_HOOK_NEW_GAME resets the records)."""
    from pedebug import flows
    base = sv.require_records(ctx)
    s = ctx.session(cards=[base.saved.card], ini=sv.ALL_ON, extra_files=base.saved.extra_files, name="1-new-game")
    flows.load_save(s, flows.slot_of(base.saved.card))
    flows.soft_reset(s)
    flows.new_game(s, from_boot=False)
    through_dialogue(s, what="control after the opening")            # the opening hands control to Aya
    _, block = s.save_block(2)
    v = sv.values(pc.parse_tail(block[pc.TAIL_AT:]))
    bad = []
    if v.get("pe_economy.loot", {}).get("junk"):
        bad.append(f"Junk {v['pe_economy.loot']['junk']}")
    if v.get("pe_qol.keys", {}).get("count"):
        bad.append(f"pocket {v['pe_qol.keys']['keys']}")
    cat = v.get("pe_economy.catalog", {})
    if cat.get("gun_effects") or cat.get("armor_effects") or cat.get("rebuild_count"):
        bad.append(f"Catalog {cat}")
    if v.get("pe_economy.shop", {}).get("tools_bought"):
        bad.append(f"{v['pe_economy.shop']['tools_bought']} Tools bought")
    ctx.expect(not bad, "carried into the new game: " + "; ".join(bad))
    ctx.expect(s.events.find("new_game"), "new_game logged")
