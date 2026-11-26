"""Mods turned off and on in any mix keep every mod's save data; pe_qol off hands the pocketed
key items back to the bag. Ported from pe-native-debug's mods_on_off and edge_cases suites; the
mod list comes from the build's own log, so every mod it has is covered."""
from __future__ import annotations

import concurrent.futures as cf

from pedebug.runner import check

from . import _saves as sv

# Records whose owner rewrites them when it runs, so they may differ after it was off and on.
QOL_RECORDS = ("pe_qol.keys", "pe_qol.sort")


def _stack_pairs(save) -> dict[int, int]:
    """pe_stacks/counts in a save's companion: {item id: extra copies}."""
    sec = save.sections().get("pe_stacks/counts")
    if not sec:
        return {}
    b = sec[1]
    out = {}
    for k in range(b[0] + b[1] + b[2]):
        out[b[4 + 2 * k]] = out.get(b[4 + 2 * k], 0) + b[5 + 2 * k]
    return out


def _carried(ctx, base, got, label, skip=(), stacks_off=False):
    """Every record and companion section came through. With pe_stacks off the host hands the
    stacked extra copies to free bag slots (pe_extension_refill_stacks) and keeps counting only
    what didn't fit: then every copy must be in the bag or still counted."""
    diffs = sv.record_diffs(base.saved.mods(), got.saved.mods(), skip=sv.VOLATILE | set(skip))
    sections = sv.section_diffs(base.saved, got.saved)
    if stacks_off:
        sections = [d for d in sections if not d.startswith("companion pe_stacks/")]
        before, after = _stack_pairs(base.saved), _stack_pairs(got.saved)
        bag0 = [int(x, 16) for x in base.bag]
        bag1 = [int(x, 16) for x in got.bag]
        for item, extra in before.items():
            placed = bag1.count(item) - bag0.count(item)
            ctx.expect(placed + after.get(item, 0) == extra,
                       f"{label}: item {item:#x}: {extra} stacked copies -> {placed} in the bag + {after.get(item, 0)} still counted")
    diffs += sections
    ctx.expect(not diffs, f"{label}: records changed: " + "; ".join(diffs))
    return not diffs


@check("mods-each-off", tags=("mods", "saves"), needs=("card:C",), timeout_s=900)
def each_off(ctx):
    """Every mod in the build turned off on its own (load, save): every record and companion
    section comes through unchanged, and only pe_qol off puts the keys back in the bag."""
    base = sv.require_records(ctx)
    mods = {i: n for i, n in base.started.items()}
    ctx.require(len(mods) >= 5, f"the build's mods from its log: {mods}")

    def one(item):
        mod_id, name = item
        return mod_id, sv.visit(ctx, f"off-{mod_id}", base.saved, sv.ini([name]))

    with cf.ThreadPoolExecutor(max_workers=4) as pool:
        for mod_id, got in pool.map(one, sorted(mods.items())):
            skip = QOL_RECORDS if mod_id == "pe_qol" else ()
            ok = _carried(ctx, base, got, f"{mod_id} off", skip, stacks_off=mod_id == "pe_stacks")
            if mod_id == "pe_qol":
                ctx.expect(sv.keys_in(got.bag) == sv.KEYS, f"pe_qol off: the keys are back in the bag: {got.bag}")
            else:
                ctx.expect(not sv.keys_in(got.bag), f"{mod_id} off: the keys stay pocketed: {got.bag}")
            ctx.note(f"{mod_id} off: {'ok' if ok else 'CHANGED'}")
    ctx.fact("mods covered", len(mods))


@check("mods-qol-off-returns-keys", tags=("mods", "saves"), needs=("card:C",), timeout_s=420)
def qol_off_returns_keys(ctx):
    """pe_qol off: the pocket goes back into the bag at load and the save holds an empty pocket
    with no PKEY block left; pe_qol on again pockets them."""
    from pedebug import card as pc
    base = sv.require_records(ctx)
    qol = base.started.get("pe_qol")
    ctx.require(qol, "pe_qol is in the build")
    off = sv.visit(ctx, "1-qol-off", base.saved, sv.ini([qol]))
    ctx.expect(sv.keys_in(off.bag) == sv.KEYS, f"the keys are back in the bag: {off.bag}")
    ctx.expect(sv.values(off.saved.mods())["pe_qol.keys"]["count"] == 0, "the saved pocket is empty")
    ctx.expect("pe_qol.keys" not in pc.window_blocks(pc.Card.load(off.saved.card).tail), "no PKEY block left")
    ctx.expect(any("released" in l for l in off.log), "the log shows the release")
    on = sv.visit(ctx, "2-qol-on", off.saved)
    ctx.expect(not sv.keys_in(on.bag), f"pe_qol on again: the keys left the bag: {on.bag}")
    ctx.expect(sv.values(on.live)["pe_qol.keys"]["keys"] == sv.KEYS, "and are pocketed again")


@check("mods-all-off-and-back", tags=("mods", "saves"), needs=("card:C",), timeout_s=420)
def all_off_and_back(ctx):
    """All mods off: the keys back in the bag, every other record carried unchanged; all on again:
    every mod gets its data back."""
    base = sv.require_records(ctx)
    off = sv.visit(ctx, "1-all-off", base.saved, sv.ini(all_on=False))
    ctx.expect(sv.keys_in(off.bag) == sv.KEYS, f"the keys are back in the bag: {off.bag}")
    _carried(ctx, base, off, "all off", skip=QOL_RECORDS, stacks_off=True)
    on = sv.visit(ctx, "2-all-on", off.saved)
    diffs = sv.record_diffs(base.saved.mods(), on.live, skip=sv.VOLATILE)
    ctx.expect(not diffs, "all on again: data differs from the start: " + "; ".join(diffs))
    ctx.expect(not sv.keys_in(on.bag), f"keys pocketed again: {on.bag}")


@check("mods-mixed-sequence", tags=("mods", "saves"), needs=("card:C",), timeout_s=900)
def mixed_sequence(ctx):
    """A run of mixes, saving each time (economy off; pe_qol and collection off; all off; two more
    off; all on): nothing lost along the way, and it ends as it started."""
    base = sv.require_records(ctx)
    m = base.started
    steps = [([m.get("pe_economy")], True), ([m.get("pe_qol"), m.get("pe_collection")], True), ([], False),
             ([m.get("pe_rebalance"), m.get("pe_shops")], True), ([], True)]
    save = base.saved
    for i, (names, all_on) in enumerate(steps, 1):
        names = [n for n in names if n]
        label = "all off" if not all_on else ("all on" if not names else "off: " + ", ".join(names))
        got = sv.visit(ctx, f"{i}-step", save, sv.ini(names, all_on))
        qol_on = all_on and m.get("pe_qol") not in names
        if qol_on:
            ctx.expect(not sv.keys_in(got.bag), f"step {i} ({label}): keys pocketed: {got.bag}")
        else:
            ctx.expect(sv.keys_in(got.bag) == sv.KEYS, f"step {i} ({label}): keys in the bag: {got.bag}")
        econ = [r for r in sv.records(base.saved.mods()) if r.startswith("pe_economy.")]
        diffs = sv.record_diffs(base.saved.mods(), got.saved.mods(), only=econ)
        ctx.expect(not diffs, f"step {i} ({label}): economy " + "; ".join(diffs))
        ctx.note(f"{i}. {label}")
        save = got.saved
    end = sv.record_diffs(base.saved.mods(), save.mods(), skip=sv.VOLATILE)
    ctx.expect(not end, "the end differs from the start: " + "; ".join(end))


@check("mods-full-bag-qol-off", tags=("mods", "saves"), needs=("card:C",), timeout_s=420)
def full_bag_qol_off(ctx):
    """pe_qol off with one free bag slot: the first key goes back, the other two stay in the
    record (nothing lost); pe_qol on again pockets all three."""
    base = sv.require_records(ctx)
    qol, stacks = base.started.get("pe_qol"), base.started.get("pe_stacks")
    no_stacks = [stacks] if stacks else []                   # pe_stacks would fold fill-bag's copies
    full = sv.visit(ctx, "0-fill", base.saved, sv.ini(no_stacks), before_save=lambda s: s.command("fill-bag 7 1"))
    ctx.require(full.bag.count("0x0") == 1, f"bag filled to one free slot: {full.bag}")
    off = sv.visit(ctx, "1-qol-off", full.saved, sv.ini([qol] + no_stacks))
    ctx.expect(sv.keys_in(off.bag) == sv.KEYS[:1], f"one key fits: {off.bag}")
    left = sv.values(off.saved.mods())["pe_qol.keys"]["keys"]
    ctx.expect(left == sv.KEYS[1:], f"the other two stay in the record: {left}")
    ctx.expect(any("left (bag full)" in l for l in off.log), "the log reports the keys that didn't fit")
    on = sv.visit(ctx, "2-qol-on", off.saved, sv.ini(no_stacks))
    ctx.expect(not sv.keys_in(on.bag) and sorted(sv.values(on.live)["pe_qol.keys"]["keys"]) == sv.KEYS,
               f"pe_qol on again: all three pocketed: {on.bag}")
