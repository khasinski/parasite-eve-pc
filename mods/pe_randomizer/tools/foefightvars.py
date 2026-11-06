"""What each enemy feeds the fight's opening: run in build-mods/mods after the rooms ctest.

  python -I foefightvars.py [--plan keys.exe SEED[,SEED...]] > enemy-fight-values.txt

Every encounter spawner adds up its foes' two constants (each foe's setup: N += k) and divides by their number; it
then sets Aya's opening AT gauge (context field 6, full at 9000) to 9000 * A * B * R * T >> 48:
A from Aya's level - mean foe level (> 1: 1.2, 1: 1.1, 0: 1.0, -1: 0.9, lower: 0.8), B from mean foe value / Aya's
stat22 (>= 2: .9, 1.5: .8, 1.2: .7, .8: .5, .5: .3, lower: .1), R random, T by Aya's level.
A moved foe adds the slot's two constants instead of its own (pe_rand_ai.c), so the slot's fight opens as it does at home.
Lines: spawner, foe (level, value, or ? when its script has no such constant), swap (with --plan)."""
import collections, re, subprocess, sys

meta, steps = {}, collections.defaultdict(dict)
for l in open("randomizer-rooms.txt", errors="replace"):
    f = l.rstrip("\n").split("\t")
    if f[0] == "room":
        end = next(int(x.split("=")[1], 16) for x in f if x.startswith("code_end="))
        meta[f[1]] = ([int(x, 16) for x in f[6].split(",")] if len(f) > 6 else [], end)
    elif f[0] == "step":
        steps[(f[1], int(f[2]))][int(f[3], 16)] = (int(f[4]), f[5] if len(f) > 5 else "")


def hex_args(s): return [tuple(int(y, 16) for y in x.split(":")) for x in s.split(",") if x]


def spawner_vars(st):
    """(level var, value var) of a spawner with the opening: the N subtracted from Aya's level, the N shifted up."""
    if not any(o == 206 and x.startswith("0:0,0:0,0:0x6,") for o, x in st.values()): return None
    lv = val = None
    for a in sorted(st):
        o, x = st[a]
        if o != 9: continue
        v = hex_args(x)
        if len(v) == 4 and v[0] == (0, 1) and v[1] == (3, 0) and v[2][0] == 1 and v[3][0] == 4: lv = v[3][1]
        if len(v) == 4 and v[0] == (0, 0x11) and v[1] == (3, 0) and v[2][0] == 4 and v[3] == (0, 0x10): val = v[2][1]
    return (lv, val) if lv is not None and val is not None else None


def adds(st):
    """{N: k} of the actor's first `N += k`."""
    out, ats = {}, sorted(st)
    for i, a in enumerate(ats[:-1]):
        o, x = st[a]
        if o != 9: continue
        v = hex_args(x)
        if len(v) == 4 and v[0] == (0, 0) and v[1] == (3, 0) and v[2][0] == 4 and v[3][0] == 0:
            o2, x2 = st[ats[i + 1]]
            if o2 == 10 and hex_args(x2)[:2] == [(4, v[2][1]), (3, 0)]: out.setdefault(v[2][1], v[3][1])
    return out


foes, spawn_of = {}, {}
for l in open("enemies.txt", errors="replace"):
    f = l.rstrip("\n").split("\t")
    if len(f) < 8 or not re.match(r"M\d+$", f[0]): continue
    foes[(f[0], int(f[2][1:]))] = (f[1], f[5], f[6])
    m = re.search(r"\be(\d+): x", f[7])
    if m: spawn_of[(f[0], int(f[2][1:]))] = int(m.group(1))

value = {}
print("# room\tentity\tplace\tcreature\tkind\tspawner\tlevel\tvalue")
for k, (place, creature, kind) in sorted(foes.items()):
    sp = spawn_of.get(k)
    sv = spawner_vars(steps.get((k[0], sp), {})) if sp is not None else None
    add = adds(steps.get(k, {}))
    lv, val = add.get(sv[0]) if sv else None, add.get(sv[1]) if sv else None
    value[k] = (lv, val, add)
    print(f"foe\t{k[0]}\te{k[1]}\t{place}\t{creature}\t{kind}\t{'e%d' % sp if sp is not None else '-'}\t"
          f"{'?' if lv is None else lv}\t{'?' if val is None else val}\t{dict(add)}")

argv = sys.argv[1:]
if argv[:1] == ["--plan"]:
    n, same, unknown = 0, 0, 0
    delta, ratio = collections.Counter(), []
    for seed in argv[2].split(","):
        out = subprocess.run([argv[1], "--plan", seed], capture_output=True, text=True, check=True).stdout
        for f in (l.split("\t") for l in out.splitlines()):
            if f[0] != "foe": continue
            a, b = (f"M{int(f[2][1:] if f[2].startswith('M') else f[2]):03d}", int(f[3])), (f"M{int(f[5][1:] if f[5].startswith('M') else f[5]):03d}", int(f[6]))
            if a == b: continue
            n += 1
            va, vb = value.get(a), value.get(b)
            if not va or not vb or None in va[:2]: unknown += 1; print(f"swap\t{seed}\t{a[0]} e{a[1]} < {b[0]} e{b[1]}\t?"); continue
            bl, bv = vb[:2] if None not in vb[:2] else ((list(vb[2].values()) + [None, None])[:2])
            if bl is None or bv is None: unknown += 1; print(f"swap\t{seed}\t{a[0]} e{a[1]} < {b[0]} e{b[1]}\t? foe"); continue
            same += (va[0], va[1]) == (bl, bv)
            delta[bl - va[0]] += 1; ratio.append(bv / va[1] if va[1] else 0)
            print(f"swap\t{seed}\t{a[0]} e{a[1]} < {b[0]} e{b[1]}\tlevel {va[0]}>{bl}\tvalue {va[1]}>{bv}")
    ratio.sort()
    print(f"# swaps {n}, unknown {unknown}, same both {same}, level change min/median/max "
          f"{min(delta)}/{sorted(delta.elements())[len(list(delta.elements())) // 2]}/{max(delta)}, value ratio "
          f"min/median/max {ratio[0]:.2f}/{ratio[len(ratio) // 2]:.2f}/{ratio[-1]:.2f}")
