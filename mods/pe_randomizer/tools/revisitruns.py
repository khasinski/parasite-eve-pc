"""Revisits for Hard's key rule: does a chest still give its item when the player comes back to it at the
story values where a key's lock asks for the key? The story walk merges states from many paths, so the
rooms it enters at a value say little about a revisit; here each chest room is loaded at each such value
anyway, entered by the door its gives-ctest line uses, and the room's own scripts decide.

  revisitruns.py lines <gives-runs.txt> <gives ctest output> <walk-states.txt> <windows.txt> <windows-runs.txt>
                       <pe_rand_spots.h> > revisit-runs.txt
      Per key (a table key spot, not the Theater's), its lock-time values: as spottable.py's late, the values
      where its lock needed it (windows.txt lock rows), from its own first give on; a key with no lock row,
      every value its own spot gives it at. The story state at a value is the walk's first state of a
      lock's room there (the key's own room without a lock), else the variables of the windows-runs line
      that ran the lock there (a zone run). Per chest of the key's day or earlier, per
      give of the chest (one per room version), its gives-runs line (Aya's stops, press, win) at each value:
      V0x4a that value, V0x1 the gives-runs line's (the room Aya came from), the variables the state's
      with the give's own done bits cleared (windowruns.done_bits), then the gives-runs variables ORed in
      and, where that differs, set as they are. Lines shared by several keys run once; id= names them all.
  revisitruns.py table <revisit-runs.txt> <ctest output> <pe_rand_spots.h> > revisit.txt
      One row per chest and key: the lock-time values the chest gave its item at in some run (open=) and
      the ones it didn't (shut=). Then per area and day: how many chests stay open at every value of each
      key, and the pairs Hard could allow from rooms alone (reaching the room is not checked here).

Run in build-mods/mods with python -I. The ctest mod-randomizer-revisit runs the committed lines."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from keyscan import KEYS
from spottable import AREA_NAMES, FIXED_KEYS
from windowruns import ENTRY, done_bits, read_states, v, var_text

SPOT = re.compile(r"\{ 0x(\w+), PE_RAND_(\w+), (\d+), (\d+), (\w+), 0x(\w+), 0x(\w+), (\w+)(?:, \w+)? \}, /\* (\d+): ")


def read_spots(path):
    """spot -> {item, kind, day, area, key, needs}: pe_rand_spots.h."""
    out = {}
    for l in open(path):
        m = SPOT.search(l)
        if m:
            out[int(m.group(9))] = {"item": v(m.group(1)), "kind": m.group(2), "day": int(m.group(3)),
                                    "area": int(m.group(4)), "key": m.group(5) == "PE_RAND_KEY",
                                    "needs": v(m.group(6))}
    return out


def read_windows(path):
    """({spot: [(room, give)]}, {spot: values its gives fire at}, {lock room: (key item, needs values)})."""
    gives, fires, locks = {}, {}, {}
    lock_key = {}
    for line in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "pe_rand_locks.h")):
        f = line.strip().lstrip("{").split(",")
        if line.strip().startswith("{ ") and len(f) > 3: lock_key[int(f[0])] = int(f[2], 16)
    vals = lambda s: {int(x, 16) for x in s.split(",") if x != "-"}
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        d = dict(x.split("=", 1) for x in f[3:] if "=" in x)
        if f[0] == "give" and d["spot"] != "?":
            n = int(d["spot"])
            gives.setdefault(n, []).append((f[1], f[2][1:]))
            fires.setdefault(n, set()).update(vals(d["fires"]))
        elif f[0] == "lock":
            item, needs = locks.setdefault(f[1], (lock_key[int(f[1][1:])], set()))
            needs.update(vals(d["needs"]))
    return gives, fires, locks


def key_times(spots, gives, fires, locks):
    """key item -> (key spot, {lock-time value: [rooms whose walk state stands for the player then]})."""
    out = {}
    for k, s in sorted(spots.items()):
        if not s["key"] or s["item"] in FIXED_KEYS or not fires.get(k): continue
        first = min(fires[k])
        when = {}
        for room, (item, needs) in sorted(locks.items()):
            if item == s["item"]:
                for x in needs:
                    if x >= first: when.setdefault(x, []).append(room)
        if not any(item == s["item"] for item, _ in locks.values()):
            for x in fires[k]: when.setdefault(x, []).append(gives[k][0][0])
        out[s["item"]] = (k, when)
    return out


def lines(argv):
    gives_runs, gives_out, states_path, windows, windows_runs, spots_h = argv[:6]
    states = read_states(states_path)
    done = done_bits(gives_runs, gives_out)
    spots = read_spots(spots_h)
    gives, fires, locks = read_windows(windows)
    first = {}    # (room, V0x4a) -> the walk's first state there
    for room, ss in states.items():
        for area, left, vars_ in sorted(ss): first.setdefault((room, area), vars_)
    for l in open(windows_runs):   # a lock's zone runs: their variables where the walk has no state
        tok = l.split()
        if l.startswith("M") and "id=l" in l and "keys=none" in l:
            first.setdefault((tok[0], v(tok[1])), tuple(t for t in tok[3:] if t.startswith("V0x")))
    run_of = {}   # (room, give) -> its gives-runs tokens
    for l in open(gives_runs):
        if l.startswith("#") or not l.strip(): continue
        tok = l.split()
        for w in (t[6:] for t in tok if t.startswith("want=")): run_of.setdefault((tok[0], w), tok)
    made, order, missing = {}, [], 0
    for item, (k, when) in sorted(key_times(spots, gives, fires, locks).items()):
        for n, s in sorted(spots.items()):
            if s["kind"] != "CHEST" or s["key"] or s["day"] > spots[k]["day"]: continue
            for room, give in gives.get(n, ()):
                tok = run_of.get((room, give))
                if not tok: missing += 1; continue
                acts = [t for t in tok[3:] if t in ("win", "press") or t.startswith("at=")]
                forced = dict((t.split("=")[0], v(t.split("=")[1])) for t in tok[3:] if t.startswith("V0x"))
                came = v(tok[2])
                if (room, give) in ENTRY:
                    forced.update(ENTRY[room, give][0])
                    if ENTRY[room, give][1] is not None: came = ENTRY[room, give][1]
                clear = done.get((room, give), {})
                for x, rooms in sorted(when.items()):
                    vars_ = next((first[r, x] for r in rooms if (r, x) in first), None)
                    if vars_ is None: missing += 1; continue
                    d = {kk: v(xx) for kk, xx in (t.split("=") for t in vars_)}
                    for var, b in clear.items():
                        if var in d: d[var] &= ~(b & ~forced.get(var, 0))
                    for how in (lambda old, y: old | y, lambda old, y: y):
                        e = dict(d)
                        for var, y in forced.items(): e[var] = how(e.get(var, 0), y)
                        line = f"{room} {x:#x} {came:#x} {var_text(e)} keys=all {' '.join(acts)}".replace("  ", " ")
                        if line not in made: made[line] = (n, give, set()); order.append(line)
                        made[line][2].add(item)
    print(f"# Generated by mods/pe_randomizer/tools/revisitruns.py lines: {len(order)} runs (chest revisits at"
          f" lock-time story values, ctest mod-randomizer-revisit).")
    for line in order:
        n, give, items = made[line]
        print(f"{line} id=r{n}+{give}:{'.'.join(f'{i:X}' for i in sorted(items))}")
    sys.stderr.write(f"revisit: {len(order)} runs, {missing} chest/value pairs without a gives line or walk state\n")


def table(argv):
    runs_path, out_path, spots_h = argv[:3]
    spots = read_spots(spots_h)
    fired, tried = {}, {}   # (chest spot, key item) -> values
    for l in open(out_path):
        if not l.startswith("run\t"): continue
        f = l.rstrip("\n").split("\t")
        d = dict(x.split("=", 1) for x in f[2:] if "=" in x)
        rid = d.get("id", "")
        if not rid.startswith("r"): continue
        chest, items = rid[1:].split(":")
        n, give = chest.split("+")
        x = v(d["V0x4a"])
        gave = {int(o[1:], 16) for o in d.get("gave", "").split(",") if o}
        for item in items.split("."):
            key = (int(n), v(item))
            tried.setdefault(key, set()).add(x)
            if v(give) in gave: fired.setdefault(key, set()).add(x)
    h = lambda xs: ",".join(f"{a:#x}" for a in sorted(xs)) or "-"
    name = lambda s: f"Day {s['day']} {AREA_NAMES[s['area']]}"
    print("# Generated by mods/pe_randomizer/tools/revisitruns.py table from the mod-randomizer-revisit ctest.")
    print("# chest: the key's lock-time story values (V0x4a) where some run of the chest's rooms gave its item"
          " (open) or none did (shut).")
    areas, keys = {}, sorted({item for _, item in tried})
    for (n, item) in sorted(tried):
        s, opened = spots[n], fired.get((n, item), set())
        shut = tried[n, item] - opened
        print(f"chest\t{n}\t{name(s)}\tkey={KEYS.get(item, hex(item))}\topen={h(opened)}\tshut={h(shut)}")
        a = areas.setdefault((s["day"], s["area"]), {"chests": set(), "open": {}})
        a["chests"].add(n)
        if not shut: a["open"].setdefault(item, set()).add(n)
    total = 0
    print("# area: chests that gave their item at every lock-time value of the key / the area's chests")
    for (day, area), a in sorted(areas.items()):
        cells = [f"{KEYS.get(i, hex(i))} {len(a['open'].get(i, ()))}" for i in keys if any((n, i) in tried for n in a["chests"])]
        total += sum(len(x) for x in a["open"].values())
        print(f"area\tDay {day} {AREA_NAMES[area]}\tchests={len(a['chests'])}\t" + "\t".join(cells))
    print(f"# summary: {total} of {len(tried)} key x chest pairs (chest on the key's day or earlier) give at every lock-time value")
    sys.stderr.write(f"revisit: {total}/{len(tried)} pairs open at every value\n")


if __name__ == "__main__":
    {"lines": lines, "table": table}[sys.argv[1]](sys.argv[2:])
