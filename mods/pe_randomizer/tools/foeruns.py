"""Seed plans of the battle randomizer run as fights headless (ctest mod-randomizer-foefights): each room whose
slots a seed's plan moves runs its fight in the mod-randomizer-run test with the real mod, every moved slot as
ai=E<MNNN:F (that foe's script and model), and must win it. Run in build-mods/mods (PE_TEST_DISC set):

  python -I foeruns.py <test-mod-randomizer-keys> <test-mod-randomizer-run> SEED[,SEED...] enemies.txt
         randomizer-rooms.txt <pe_rand_enemies.h> [--out foefights] [--lines]

The plan is the real generator's (`test-mod-randomizer-keys --plan SEED`). Per foe room, the fight: the first of
its foes' fights in enemies.txt whose V tests can be met (top-level tests only: V0x4a compares give a value in
range, (V & bit) sets the bit, !(V & bit) leaves it clear; everything else starts at 0). Where Aya stands
(at=): each op 119 polygon of the fight's spawner routine in randomizer-rooms.txt, in turn (its encounter
zones), at the centre; a spawner with none gets every zone of the room. Then:
1. Each foe room's own fight (no ai=, `stats`): <out>-plain.txt. A room not won gets a second try at the floor
   points the run found inside the same zones (reach=), and one with no zones at the room's other zones.
   Rooms won by neither are left out (listed: their fight isn't reachable headless that way).
2. The seeds' lines in the won rooms, with the same stops, plus the plain lines again (battlestats.py compares
   the unmoved slots with them): <out>-runs.txt, run into <out>.txt, checked by battlestats.py.
A plan line fails when a swap isn't taken, a moved actor runs none of its script, a script divides by an N var
holding 0 (the fight's foe count; the game has no guard), no battle is won, or the fight opens on other level and
value sums or another foe count than the room's own fight (mean=: a Bear and its part count as 2).
--known file: swaps (M005<M418:3, one per line, # the reason) whose failing lines are listed as KNOWN, not
problems. --lines: only write <out>-runs.txt (no plan run). Exit 1 on a failure, 77 when an input is missing.

--pairs <pe_rand_foes.h> (ctest mod-randomizer-foepairs; SEED list ignored, give 0): not seeds' plans but every
swap any seed can plan: each slot with each foe the table allows it (g_pe_rand_foe_ok; boss slots stay as they
are). Round k gives every slot its k-th allowed foe, so a room's line moves all its slots at once; rounds run
PAIR_ROUNDS at a time (<out>-runs.txt and <out>.txt hold the last batch; failing lines go to <out>-fails.txt).
A swap the mod refuses in that company (the room's enemy VRAM is full) is retried alone before it counts."""
import os, re, subprocess, sys

SEED = "seed=123456,2"   # the gate lines' item seed (the run test takes one per file)
ALT = re.compile(r"(?:^|; )e(\d+) \+([0-9A-F]+) if ")
VTEST = re.compile(r"(!?)\(V0x([0-9a-f]+) (&|==|>=|<=|<|>|!=) (-?0x[0-9a-f]+|-?\d+)\)")
ENV = dict(os.environ, PSYZ_HEADLESS="1", SDL_AUDIODRIVER="dummy")


def plan(exe, seed):
    out = subprocess.run([exe, "--plan", str(seed)], capture_output=True, text=True, check=True).stdout
    return [(f[2], int(f[3]), f[5], int(f[6])) for f in (l.split("\t") for l in out.splitlines()) if f[0] == "foe"]


PAIR_ROUNDS = 12


def pairs(table):
    """Rounds of (room, entity, foe room, foe entity): round k = each slot's k-th allowed foe (pe_rand_foes.h)."""
    text = open(table).read()
    foes = [(f"M{int(r):03d}", int(e), int(b)) for r, e, b in
            re.findall(r"^\s*\{ (\d+), (\d+), (\d+), [^{}]*\},$", text.split("g_pe_rand_foe_ok")[0], re.M)]
    rows = [[int(w, 16) for w in re.findall(r"0x[0-9a-f]{8}", r)] for r in
            re.findall(r"^\s*\{ (0x[0-9a-f]{8}(?:, 0x[0-9a-f]{8})*) \},$", text.split("g_pe_rand_foe_ok")[1], re.M)]
    if not foes or len(foes) != len(rows): sys.exit(f"foeruns: {table}: {len(foes)} foes, {len(rows)} rows")
    rounds = []
    for i, (room, ent, boss) in enumerate(foes):
        if boss: continue
        allowed = [b for b in range(len(foes)) if b != i and rows[i][b >> 5] >> (b & 31) & 1]
        for k, b in enumerate(allowed):
            if k == len(rounds): rounds.append([])
            rounds[k].append((room, ent, foes[b][0], foes[b][1]))
    return rounds


def fights(path):
    """(room, entity) -> [(spawner, conditions)] from enemies.txt's fight column."""
    out = {}
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        if f[0].startswith("#") or len(f) < 10: continue
        parts = ALT.split(f[9])   # '', e, off, cond, e, off, cond ...
        out[(f[0], int(f[2][1:]))] = [(int(parts[i]), parts[i + 2]) for i in range(1, len(parts) - 2, 3)]
    return out


def solve(cond):
    """V values that meet the condition's top-level V tests, or None."""
    lo, hi, vals, bits_set, bits_clear = 0, 0xFFFF, {}, {}, {}
    for neg, var, op, n in (m.groups() for m in map(VTEST.fullmatch, cond.split(" ; ")) if m):
        var, n = int(var, 16), int(n, 0)
        if op == "&":
            (bits_clear if neg else bits_set)[var] = (bits_clear if neg else bits_set).get(var, 0) | (n & 0xFFFFFFFF)
        elif var == 0x4a and not neg:
            if op == "==": lo, hi = max(lo, n), min(hi, n)
            elif op == ">=": lo = max(lo, n)
            elif op == ">": lo = max(lo, n + 1)
            elif op == "<": hi = min(hi, n - 1)
            elif op == "<=": hi = min(hi, n)
            elif op == "!=" and lo == n: lo += 1
        elif op == "==" and not neg: vals[var] = n
        elif op == "!=" and not neg: vals[var] = n + 1 if vals.get(var, 0) == n else vals.get(var, 0)
        else: return None
    if lo > hi: return None
    for var, b in bits_set.items():
        if b & bits_clear.get(var, 0): return None
        vals[var] = vals.get(var, 0) | b
    for var, b in bits_clear.items():
        if vals.get(var, 0) & b: return None
    vals[0x4a] = lo
    return vals


def zones(path, rooms):
    """room -> [(routine, +offset, centre)] of each op 119 polygon with constant corners, in listing order."""
    out = {}
    for line in open(path):
        if not line.startswith("step\t"): continue
        f = line.rstrip("\n").split("\t")
        if len(f) < 6 or f[4] != "119" or f[1] not in rooms: continue
        ops = f[5].split(",")[:8]
        if len(ops) < 8 or any(not o.startswith("0:") for o in ops): continue
        v = [int(o[2:], 16) >> 16 for o in ops]
        v = [x - 0x10000 if x & 0x8000 else x for x in v]
        c = (int(sum(v[0::2]) / 4), int(sum(v[1::2]) / 4))
        if any(v) and c not in [z for _, _, z in out.get(f[1], [])]:
            out.setdefault(f[1], []).append((int(f[2]), int(f[3], 16), c))
    return out


def outputs(path):
    """id -> the run test's output fields."""
    out = {}
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        if f[0] == "run":
            kv = dict(x.split("=", 1) for x in f[2:] if "=" in x)
            out[kv.get("id", "")] = kv
    return out


def run(exe, out, runs):
    """Runs the lines; their failures are read from the output (the test prints only its first 40)."""
    p = subprocess.run([exe, out, runs], env=ENV, capture_output=True, text=True, timeout=600)
    if p.returncode == 77: print(p.stdout + p.stderr); sys.exit(77)
    if "Randomizer runs:" not in p.stdout and " failures" not in p.stderr:
        print(p.stdout + p.stderr[-3000:]); sys.exit(f"foeruns: the run test failed ({p.returncode})")
    return outputs(out)


def slots(kv):
    """Actor types that took a fight slot (combat=)."""
    return {int(e.split("#")[0][1:]) for e in kv.get("combat", "").split(";") if e}


def line(room, vals, stops, extra, name):
    var = "".join(f" V{v:#x}={n:#x}" for v, n in sorted(vals.items()) if v not in (0x4a, 0x1) and n)
    at = "".join(f" at={x},{z}" for x, z in stops)
    return f"{room} {vals[0x4a]:#x} {vals.get(1, 0):#x}{var} win stats gate {SEED}{at}{extra} id={name}\n"


def main():
    args = sys.argv[1:]
    out_name, only_lines = "foefights", "--lines" in args
    if only_lines: args.remove("--lines")
    if "--out" in args:
        i = args.index("--out"); out_name = args[i + 1]; del args[i:i + 2]
    pair_table = None
    if "--pairs" in args:
        i = args.index("--pairs"); pair_table = args[i + 1]; del args[i:i + 2]
    skip = set()   # --known: swaps (M005<M418:3, one per line, # the reason) whose failing lines are known, not problems
    if "--known" in args:
        i = args.index("--known"); skip = {l.split("#")[0].strip() for l in open(args[i + 1])} - {""}; del args[i:i + 2]
    if len(args) != 6: sys.exit(__doc__)
    keys, run_exe, seeds, enemies, rooms, table = args
    if not all(os.path.exists(p) for p in (enemies, rooms, table)):
        print("foeruns: missing " + ", ".join(p for p in (enemies, rooms, table) if not os.path.exists(p))); sys.exit(77)
    seeds = [int(s) for s in seeds.split(",")]
    fight = fights(enemies)
    if pair_table:
        seeds = list(range(len(rounds := pairs(pair_table))))
        plans = dict(enumerate(rounds))
        print(f"Pairs: {sum(map(len, rounds))} allowed swaps of {len({r[:2] for q in rounds for r in q})} slots, {len(rounds)} rounds")
    else: plans = {s: plan(keys, s) for s in seeds}
    picked = {}
    for room, ent, *_ in plans[seeds[0]]:   # one fight per room
        if room in picked: continue
        for spawner, cond in fight.get((room, ent), []):
            vals = solve(cond)
            if vals is not None: picked[room] = (spawner, vals); break
        else: print(f"{room}: no fight whose V tests can be met")
    zone = zones(rooms, set(picked))

    # 1. each room's own fight: at the spawner's zones (centres), or with no at= when it has none
    tries = {room: [z for r, _, z in zone.get(room, []) if r == sp] for room, (sp, _) in picked.items()}
    with open(f"{out_name}-try1.txt", "w") as f:
        for room, (_, vals) in picked.items(): f.write(line(room, vals, tries[room], "", f"plain-{room}"))
    got = run(run_exe, f"{out_name}-try1-out.txt", f"{out_name}-try1.txt")
    stops, plain, retry = {}, {}, {}
    for room, (spawner, vals) in picked.items():
        kv = got.get(f"plain-{room}", {})
        if kv.get("won"): stops[room], plain[room] = tries[room], kv; continue
        # 2nd try: the floor points the run found inside the same zones (reach=+OFF@x,z), else the room's other zones
        reach = {int(o, 16): (int(x), int(z)) for o, x, z in re.findall(r"\+([0-9A-F]+)@(-?\d+),(-?\d+)", kv.get("reach", ""))}
        own = [reach[o] for r, o, _ in zone.get(room, []) if r == spawner and o in reach]
        other = [reach.get(o, z) for r, o, z in zone.get(room, []) if r != spawner]
        st = (own if own and own != tries[room] else other)[:8]
        if st: retry[room] = st
    if retry:
        with open(f"{out_name}-try2.txt", "w") as f:
            for room, st in retry.items(): f.write(line(room, picked[room][1], st, "", f"plain-{room}"))
        got = run(run_exe, f"{out_name}-try2-out.txt", f"{out_name}-try2.txt")
        for room, st in retry.items():
            if got.get(f"plain-{room}", {}).get("won"): stops[room], plain[room] = st, got[f"plain-{room}"]
    print(f"Foe rooms: {len(picked)}, own fight won headless in {len(stops)}; not reached: "
          + (" ".join(sorted(set(picked) - set(stops))) or "-"))

    # 2. the seeds' plans in those rooms: ai= for each moved slot whose actor took a fight slot in the room's own run
    n, idle, lines = 0, 0, {}
    for s in seeds:
        swaps = {}
        for room, ent, src, src_ent in plans[s]:
            if (src, src_ent) == (room, ent) or room not in stops: continue
            if ent in slots(plain[room]): swaps.setdefault(room, []).append(f"ai={ent}<{src}:{src_ent}")
            else: idle += 1
        for room, ai in swaps.items(): lines[f"seed{s}-{room}"] = ai; n += 1
    print(f"Plan lines: {n} ({sum(map(len, lines.values()))} swaps run; {idle} moved slots not in their room's own fight there)")

    def batch(names, title):
        """Writes and runs those lines (after the plain ones); the lines' problems, by name."""
        with open(f"{out_name}-runs.txt", "w") as f:
            f.write(f"# foeruns.py: {title}, plain lines then each moved room\n")
            for room in stops: f.write(line(room, picked[room][1], stops[room], "", f"plain-{room}"))
            for name in names:
                room = name.split("-")[1]
                f.write(line(room, picked[room][1], stops[room], " " + " ".join(lines[name]), name))
        if only_lines: return {}, 0
        got, why = run(run_exe, f"{out_name}.txt", f"{out_name}-runs.txt"), {}
        for name in names:
            kv, own = got.get(name), plain[name.split("-")[1]]
            w = ("no output (a swap not taken)" if kv is None else
                 f"divides by zero ({kv['div0']})" if kv.get("div0") else
                 "no battle won" if not kv.get("won") else
                 "a moved actor ran none of its script" if any(a.endswith(":0") for a in kv.get("aisteps", "").split(",")) else
                 "a moved actor took no fight slot" if any(int(a[3:].split("<")[0]) not in slots(kv) for a in lines[name]) else
                 f"opens on {kv.get('mean', 'no mean')}, the room's own fight on {own.get('mean', 'no mean')}"
                 if kv.get("mean") != own.get("mean") else "")
            if w: why[name] = w
        b = subprocess.run([sys.executable, "-I", os.path.join(os.path.dirname(os.path.abspath(__file__)), "battlestats.py"),
                            f"{out_name}.txt", table], capture_output=True, text=True)
        print(b.stdout.strip(), flush=True)
        return why, b.returncode != 0

    names, why, stats_bad = list(lines), {}, 0
    if not pair_table:
        why, stats_bad = batch(names, "seeds " + ",".join(map(str, seeds)))
    else:
        for i in range(0, len(seeds), PAIR_ROUNDS):
            part = [x for x in names if i <= int(x.split("-")[0][4:]) < i + PAIR_ROUNDS]
            w, b = batch(part, f"allowed swaps, rounds {i}-{i + PAIR_ROUNDS - 1}")
            why.update(w); stats_bad += b
            if only_lines: break
        # a room's slots moved together may not fit the room's enemy VRAM: the failing lines' swaps again, each alone
        again = {}
        for name in [x for x in why if len(lines[x]) > 1]:
            for k, ai in enumerate(lines[name]): again[f"{name.split('-')[0]}x{k}-{name.split('-')[1]}"] = [ai]
            del why[name]
        if again and not only_lines:
            lines.update(again)
            w, b = batch(list(again), "failing lines' swaps, each alone")
            why.update(w); stats_bad += b
    if only_lines: return
    bad, known = [], []
    for name, w in why.items():
        ai = lines[name]
        (known if any(name.split("-")[1] + "<" + a.split("<")[1] in skip for a in ai) else bad).append(f"{name} {' '.join(ai)}: {w}")
    for b in known: print("KNOWN " + b)
    for b in bad: print("FAIL " + b)
    if pair_table:
        with open(f"{out_name}-fails.txt", "w") as f: f.write("".join(x + "\n" for x in known + bad))
    problems = len(bad) + (stats_bad != 0)
    print(f"Foe fights: {n} plan lines, {n - len(bad) - len(known)} won, {len(known)} known failures, {problems} problems")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
