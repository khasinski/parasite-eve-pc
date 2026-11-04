"""Story windows for the assumed fill (roadmap step 2): at which story values (V0x4a) each table give
fires, and each table lock's key test changes how the run ends, in the room's own scripts.

  windowruns.py lines <disc 1 .bin> <gives-runs.txt> <gives ctest output> <walk-states.txt> <randomizer-spots.txt> [zr:zo ...] > windows-runs.txt
      Give lines: each gives-runs line (Aya's stops, press, win), once per distinct story state the walk
      has its room in (V0x4a, V0x1 and the state's variables; keys=all, no checks). The walk took every
      give, so the give's own done bits are cleared first: its flag= and, without one, the bits its run
      in the gives ctest set (randomizer-gives.txt, one output line per input line). The variables the
      gives-runs line sets (a second gift's first-gift bit, a sewer cell) go on top, ORed in and, where
      that differs, set as they are; the table names them (forced=). ENTRY adds a way in the walk has
      that its states lose (a variable set as it is, the room Aya came from).
      Lock lines: for each keytest (pe_rand_locks.h), with keys=none and keys=<its key>,
      - a walk zone run (zr input / zo output pairs) that ran it, once per V0x4a it ran at;
      - once per V0x4a the walk has its room at (that area's first state): Aya stands in the door
        handler's area (lock_handler) facing each way in turn and presses.
      Both with the bits the room's handlers for that key test cleared: the walk opened every door it
      could, and an opened door skips its key test.
  windowruns.py table <windows-runs.txt> <ctest output> <randomizer-spots.txt> > windows.txt
      One row per give (spot, story values it fires at / its room was run at) and per lock (story values
      the key changed how the run ended: end, gives, variables set, doors left closed / the test ran, but
      the key changed nothing / not reached), then a summary.

Run in build-mods/mods with python -I. The ctest mod-randomizer-windows runs the committed lines."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dis as D

FACINGS = (0, 1024, 2048, 3072)
# Gives the walk's states can't fire: a state holds every bit the walk ever set, in any story order. Each
# entry is a way in the walk has: (room, give) -> (variables set as they are, room entered from).
# M207: M210 (walked into M207) clears the museum bit V0x2a|0x1; set, the room frees its fight, no chest.
# M147: the Green Cardkey's actor is placed only when Aya comes from M125 (V0x1 0x7d), which the walk
# has at every story value it has M147.
ENTRY = {("M207", "1A9C"): ({"V0x2a": 0}, None), ("M147", "808"): ({}, 0x7d)}


def v(s):
    return int(s, 16)


def lock_handler(blob, test):
    """A key test's door handler, read back from the test (room offset): the last area test before it
    (op 119, a polygon of x,z pairs Aya's position must be in) and the bit tests after that ((V & bit),
    op 9 code 3: the door's opened bit, set once the key was used, skips the key test). Returns the
    polygon's centre (x, z) or None, and [(var, bit)]."""
    start = test
    for s in range(max(0, test - 0x400), test, 4):   # the first step-aligned start that decodes up to the test
        i = s
        while i < test:
            op, a, n = D.decode(blob, i)
            if op == 0 and not a or op > 400: break
            i += n
        if i == test: start = s; break
    centre, bits, i = None, [], start
    while i < test:
        op, a, n = D.decode(blob, i)
        if op == 119:
            pts = [struct.unpack("<i", struct.pack("<I", x))[0] >> 16 for m, x in a if m == 0]
            xs, zs = pts[0::2], pts[1::2]
            if xs and len(xs) == len(zs): centre, bits = (sum(xs) // len(xs), sum(zs) // len(zs)), []
        elif op == 9 and len(a) == 4 and a[0] == (0, 3) and a[2][0] == 2 and a[3][0] == 0:
            bits.append((f"V{a[2][1]:#x}", a[3][1]))
        i += n
    return centre, bits


def var_text(d):
    return " ".join(f"{k}={x:#x}" for k, x in sorted(d.items(), key=lambda kv: v(kv[0][1:])) if x)


def gives_by_listing(spots):
    """(room, listing offset) -> spot, from the spots file's give rows."""
    out = {}
    for l in open(spots):
        f = l.rstrip("\n").split("\t")
        if f[0] == "give":
            spot = next((x[5:] for x in f if x.startswith("spot=")), None)
            if spot is not None: out.setdefault((f[1], int(f[4], 16)), spot)
    return out


def read_states(path):
    """room -> {(V0x4a, V0x1, vars)}: the walk's states (walk-states.txt)."""
    states = {}
    for l in open(path):
        f = l.rstrip("\n").split("\t")
        key = (v(f[2].split("=")[1]), v(f[3].split("=")[1]),
               tuple(x.split("/")[0] for x in f[4].split()))
        states.setdefault(f[1], set()).add(key)
    return states


def done_bits(gives_runs, gives_out):
    """(room, give) -> {var: bits}: the bits taking the give sets (its flag=, else what its gives-ctest run set)."""
    done = {}
    outs = (l.split("\t") for l in open(gives_out) if l.startswith("run\t"))
    for l in open(gives_runs):
        if l.startswith("#") or not l.strip(): continue
        tok, o = l.split(), next(outs)
        if o[1] != tok[0]: sys.exit(f"{gives_out}: {o[1]} where {gives_runs} has {tok[0]}")
        bits = {}
        for t in tok:
            if t.startswith("flag="):
                var, b = t[5:].split("|"); bits[var] = bits.get(var, 0) | v(b)
        if not bits:
            for s in next((x[4:] for x in o if x.startswith("set=")), "").split(","):
                if ":" not in s or s.startswith("V0x1:"): continue
                var, (old, new) = s.split(":")[0], map(v, s.split(":")[1].split(">"))
                bits[var] = bits.get(var, 0) | new & ~old
        for w in (t[6:] for t in tok if t.startswith("want=")):
            dw = done.setdefault((tok[0], w), {})
            for var, b in bits.items(): dw[var] = dw.get(var, 0) | b
    return done


def lines(argv):
    disc, gives_runs, gives_out, states_path, spots = argv[:5]
    states = read_states(states_path)
    done = done_bits(gives_runs, gives_out)
    out = []
    seen = set()
    for l in open(gives_runs):
        if l.startswith("#") or not l.strip(): continue
        tok = l.split()
        room = tok[0]
        wants = [t[6:] for t in tok if t.startswith("want=")]
        acts = [t for t in tok[3:] if t in ("win", "press") or t.startswith("at=")]
        forced = dict((t.split("=")[0], v(t.split("=")[1])) for t in tok[3:] if t.startswith("V0x"))
        new = [w for w in wants if (room, w) not in seen]
        if not new: continue
        seen.update((room, w) for w in new)
        entry = [ENTRY[room, w] for w in new if (room, w) in ENTRY]
        for vars_, _ in entry: forced.update(vars_)
        came = {x for _, x in entry if x is not None}
        gid = "g" + room + "+" + ".".join(new) + "".join(f"~{k}={x:#x}" for k, x in forced.items())
        clear = {}
        for w in new:
            for var, b in done.get((room, w), {}).items(): clear[var] = clear.get(var, 0) | b
        made = set()
        for area, left, vars_ in sorted(states.get(room, ())):
            d = {k: v(x) for k, x in (t.split("=") for t in vars_)}
            for var, b in clear.items():
                if var in d: d[var] &= ~(b & ~forced.get(var, 0))
            for how in (lambda old, x: old | x, lambda old, x: x):
                e = dict(d)
                for var, x in forced.items(): e[var] = how(e.get(var, 0), x)
                for frm in sorted({left} | came):
                    line = f"{room} {area:#x} {frm:#x} {var_text(e)} keys=all {' '.join(acts)} id={gid}".replace("  ", " ")
                    if line not in made: made.add(line); out.append(line)
    locks = {}
    for l in open(spots):
        if l.startswith("keytest\t"):
            _, room, off, _, at, item = l.rstrip("\n").split("\t")
            locks.setdefault(room, []).append((off, at, item))
    found = {}
    for pair in argv[5:]:
        zr, zo = pair.split(":")
        with open(zr) as fi, open(zo) as fo:
            for inp, outp in zip(fi, fo):
                room = outp[4:8]
                if not outp.startswith("run\tM") or room not in locks: continue
                f = outp.split("\t")
                ss = set(next((x[6:] for x in f if x.startswith("steps=")), "").split(","))
                for off, at, item in locks[room]:
                    if at in ss:
                        found.setdefault((room, at), {}).setdefault(v(f[2].split("=")[1]), inp.split())
    for room in sorted(locks):
        blob = D.room(disc, int(room[1:]))
        first = {}
        for area, left, vars_ in sorted(states.get(room, ())): first.setdefault(area, (left, vars_))
        handlers = {off: lock_handler(blob, v(off[1:])) for off, at, item in locks[room]}
        opened = {}   # key -> {var: bits}: one door's key tests share them (M051's handler and Aya's script)
        for off, at, item in locks[room]:
            ok = opened.setdefault(item, {})
            for var, b in handlers[off][1]: ok[var] = ok.get(var, 0) | b

        def shut(d, item):
            for var, b in opened.get(item, {}).items():
                if var in d: d[var] &= ~b
            return d
        for off, at, item in locks[room]:
            lid = f"l{room}+{v(at):X}"
            keys = ("none", f"{v(item):X}")
            for area, tok in sorted(found.get((room, at), {}).items()):
                d = shut({t.split("=")[0]: v(t.split("=")[1]) for t in tok[3:] if t.startswith("V0x")}, item)
                base = tok[:3] + [var_text(d)] + [t for t in tok[3:] if not t.startswith(("V0x", "keys="))]
                for k, mode in zip(keys, ("none", "key")):
                    out.append(" ".join(base + [f"keys={k}", f"id={lid}.zone:{mode}"]).replace("  ", " "))
            centre = handlers[off][0]
            if not centre: continue
            stops = " ".join(f"at={centre[0]},{centre[1]},{a}" for a in FACINGS)
            for area, (left, vars_) in sorted(first.items()):
                d = shut({k: v(x) for k, x in (t.split("=") for t in vars_)}, item)
                for k, mode in zip(keys, ("none", "key")):
                    out.append(f"{room} {area:#x} {left:#x} {var_text(d)} keys={k} win press {stops} id={lid}.walk:{mode}".replace("  ", " "))
    print(f"# Generated by mods/pe_randomizer/tools/windowruns.py lines: {len(out)} runs (story windows, ctest mod-randomizer-windows).")
    print("\n".join(out))


def table(argv):
    runs_path, out_path, spots = argv[:3]
    spot_of = gives_by_listing(spots)
    gives, locks = {}, {}
    for l in open(out_path):
        if not l.startswith("run\t"): continue
        f = l.rstrip("\n").split("\t")
        d = dict(x.split("=", 1) for x in f[2:] if "=" in x)
        rid = d.get("id", "")
        area = v(d["V0x4a"])
        if rid.startswith("g"):
            gid, *forced = rid.split("~")
            room, offs = gid[1:].split("+")
            g = gives.setdefault(gid, {"room": room, "offs": offs.split("."), "forced": forced, "fired": {}, "ran": set()})
            g["ran"].add(area)
            gave = {int(x[1:], 16) for x in d.get("gave", "").split(",") if x}
            for o in g["offs"]:
                if int(o, 16) in gave: g["fired"].setdefault(o, set()).add(area)
        elif rid.startswith("l"):
            lid, mode = rid[1:].split(":")
            lid, src = lid.split(".")
            room, at = lid.split("+")
            k = locks.setdefault(lid, {"room": room, "at": int(at, 16), "runs": {}})
            asked = d.get("asked", "")
            k["runs"].setdefault((area, src), {})[mode] = ((d["end"], d.get("gave"), d.get("set"), d.get("closed")),
                                                           f"+{int(at, 16):06X}:" in asked)
    rows, fired_any, lock_needs = [], 0, 0
    for g in gives.values():
        for o in g["offs"]:
            fired = sorted(g["fired"].get(o, ()))
            fired_any += bool(fired)
            spot = spot_of.get((g["room"], int(o, 16)), "?")
            rows.append(f"give\t{g['room']}\t+{o}\tspot={spot}\tfires={','.join(f'{a:#x}' for a in fired) or '-'}"
                        f"\tfirst={fired[0]:#x}\tlast={fired[-1]:#x}" if fired else
                        f"give\t{g['room']}\t+{o}\tspot={spot}\tfires=-")
            rows[-1] += f"\tran={len(g['ran'])}" + (f"\tforced={','.join(g['forced'])}" if g["forced"] else "")
    for lid, k in sorted(locks.items()):
        needs, same, unasked = set(), set(), set()
        for (area, src), m in k["runs"].items():
            none, key = m.get("none"), m.get("key")
            if not none or not key or not (none[1] or key[1]): unasked.add(area)
            elif none[0] != key[0]: needs.add(area)
            else: same.add(area)
        same -= needs; unasked -= needs | same
        lock_needs += bool(needs)
        h = lambda xs: ",".join(f"{a:#x}" for a in sorted(xs)) or "-"
        rows.append(f"lock\t{k['room']}\t+{k['at']:X}\tneeds={h(needs)}\tsame={h(same)}\tunasked={h(unasked)}")
    print("# Generated by mods/pe_randomizer/tools/windowruns.py table from the mod-randomizer-windows ctest.")
    print("# give: story values (V0x4a) its room was run at in a walk state with the give firing (forced: variables")
    print("# its gives-runs line sets, put on every state). lock: values where holding the key changed how the run")
    print("# ended (needs: end, gives, variables set or doors left closed), didn't (same), or the test never ran.")
    print("\n".join(rows))
    ng = sum(len(g["offs"]) for g in gives.values())
    print(f"# summary: {fired_any} of {ng} gives fire in some walk state; {lock_needs} of {len(locks)} walked locks need their key somewhere")
    sys.stderr.write(f"windows: gives {fired_any}/{ng} fire, locks {lock_needs}/{len(locks)} need their key\n")


if __name__ == "__main__":
    {"lines": lines, "table": table}[sys.argv[1]](sys.argv[2:])
