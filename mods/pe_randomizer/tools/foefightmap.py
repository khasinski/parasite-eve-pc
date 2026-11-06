"""Where a moved foe's fight numbers land (run in build-mods/mods after the rooms ctest, seeds below): each foe adds
level, value and count to its spawner's N vars, and the spawner divides by the count with no guard. pe_rand_ai.c maps
the vars by what they are for, read off each spawner; this does the same from the rooms ctest's listing and checks the
result: swaps by outcome, the swaps' working vars that find no var the slot's spawner zeroes (they stay 0 at the first
fight of a room but aren't zeroed for the next), then fights (formations) whose count stays 0 or runs short."""
import collections, contextlib, io, os, runpy, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
sys.argv = ["foefightvars.py"]
with contextlib.redirect_stdout(io.StringIO()):
    g = runpy.run_path(os.path.join(here, "foefightvars.py"))
steps, spawn_of, spawner_vars, hex_args, adds = [g[k] for k in "steps spawn_of spawner_vars hex_args adds".split()]
import doorscan as DS, enemyscan as ES
SEEDS = "1 2 3 123456".split()


def used(k):
    first = {}
    for a in sorted(steps.get(k, {})):
        for m, v in hex_args(steps[k][a][1]):
            if m == 4 and v not in first: first[v] = a
    return first


def shared(k, sp):
    """N vars the foe shares with its spawner, not N0xa, in the order the foe first uses them."""
    u, s = used(k), used((k[0], sp))
    return sorted([i for i in u if i in s and i != 10], key=lambda i: u[i])


def count_var(st):
    for a in sorted(st):
        o, x = st[a]
        if o == 9:
            v = hex_args(x)
            if len(v) == 4 and v[0] == (0, 0x10) and v[3][0] == 4: return v[3][1]


def flags_var(st):
    for a in sorted(st):
        o, x = st[a]
        if o == 9:
            v = hex_args(x)
            if len(v) == 4 and v[0] == (0, 3) and v[1][0] == 3 and v[2][0] == 4 and v[3][0] == 0: return v[2][1]


def zeroed(st):
    """N vars the spawner only ever sets to 0."""
    out, other = set(), set()
    for o, x in st.values():
        for i, (m, v) in enumerate(hex_args(x)):
            if m != 4: continue
            (out if o == 10 and i == 0 and hex_args(x)[1] == (0, 0) else other).add(v)
    return out - other


def roles(k):
    sp = spawn_of.get(k)
    st = steps.get((k[0], sp), {})
    sv = spawner_vars(st) if sp is not None else None
    return (sv[0], sv[1], count_var(st), flags_var(st)) if sv else None


def moved_vars(a, b):
    """(n_to for foe b in slot a, its working vars left without one of the slot's) as pe_rand_ai.c maps them."""
    ra, rb = roles(a), roles(b)
    fa, fb = shared(a, spawn_of[a]), shared(b, spawn_of[b])
    if not ra or not rb:
        return ({x: y for x, y in zip(fb, fa)} if fb and len(fa) == len(fb) else {}), 0
    n_to = {x: y for x, y in zip(rb, ra) if x is not None and y is not None and x in fb}
    za, zb = zeroed(steps[(a[0], spawn_of[a])]), zeroed(steps[(b[0], spawn_of[b])])
    mine = [v for v in fa if v in za and v not in ra]
    theirs = [v for v in fb if v in zb and v not in rb]
    n_to.update(zip(theirs, mine))
    return n_to, max(0, len(theirs) - len(mine))


def plan(seed):
    out = subprocess.run(["./test-mod-randomizer-keys.exe", "--plan", seed], capture_output=True, text=True,
                         stdin=subprocess.DEVNULL, timeout=60).stdout
    for f in (l.split("\t") for l in out.splitlines()):
        if f[0] == "foe" and (f[2], f[3]) != (f[5], f[6]): yield (f[2], int(f[3])), (f[5], int(f[6]))


res, ex, n, left = collections.Counter(), collections.defaultdict(list), 0, collections.Counter()
for seed in SEEDS:
    for a, b in plan(seed):
        n += 1
        if spawn_of.get(a) is None or spawn_of.get(b) is None: res["no spawner"] += 1; continue
        ra, rb = roles(a), roles(b)
        if not ra or not rb: res["no opening"] += 1; continue
        n_to, short = moved_vars(a, b)
        left[short] += 1
        ok = ["ok" if n_to.get(x, x) == y else "LOST" for x, y in zip(rb, ra)]
        add_a, add_b = adds(steps[a]), adds(steps[b])
        const = "ok" if ra[0] in add_a and ra[1] in add_a and rb[0] in add_b and rb[1] in add_b else "NO ADD"
        key = f"level={ok[0]} value={ok[1]} count={ok[2]} flags={ok[3]} constants={const}"
        res[key] += 1
        if len(ex[key]) < 4: ex[key].append(f"{a[0]} e{a[1]} < {b[0]} e{b[1]}: slot {ra}; foe {rb}, order {shared(b, spawn_of[b])}")
print("swaps", n)
for k, c in res.most_common():
    print(c, k)
    if "LOST" in k or "NO" in k:
        for e in ex[k]: print("   ", e)
print("working vars short of the slot's (swaps by how many):", sorted(left.items()))

rooms = DS.load("randomizer-rooms.txt")
tot, zero = collections.Counter(), []
for seed in SEEDS:
    moves = collections.defaultdict(dict)
    for a, b in plan(seed): moves[a[0]][a[1]] = b
    for room, moved in moves.items():
        for sp, forms in ES.formations(rooms[int(room[1:])][1]).items():
            st = steps.get((room, sp), {})
            if not spawner_vars(st): continue
            ca = count_var(st)
            for form in forms:
                if not form or not any(t in moved for t in form): continue
                tot["fights with a moved foe"] += 1
                lost = []
                for t in form:
                    if t not in moved: lost.append(False); continue
                    a, b = (room, t), moved[t]
                    if spawn_of.get(a) is None or spawn_of.get(b) is None or not roles(b): lost.append(False); continue
                    lost.append(moved_vars(a, b)[0].get(roles(b)[2], roles(b)[2]) != ca)
                if all(lost): tot["count 0: divide by zero"] += 1; zero.append((seed, room, sp, dict(form)))
                elif any(lost): tot["count too low"] += 1
print(dict(tot))
print(len({(r, s) for _, r, s, _ in zero}), "room spawners", zero[:12])
