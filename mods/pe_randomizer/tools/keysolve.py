"""Key layouts checked over the story walk (storywalk.py), and key logic made from it. Run in
build-mods/mods (each ~4 min for the walk, then ~2 min per 1,000 layouts):

  test-pe-mod-randomizer-keys1 --layouts 2000 > layouts.txt     (run_pe_randomizer_test.ps1 builds it)
  python -I keysolve.py story-graph.txt randomizer-exits.txt ../../mods/pe_randomizer/pe_rand_spots.h
         --runs rwin/base-fix.txt --zones "$(cat rwin/zones.list)" --layouts layouts.txt
  python -I keysolve.py <the same, without --layouts> > keys-solved.txt

--layouts: lines `<seed> <difficulty> <spot>=<item> ...` (the generator test's --layouts mode: the
spots that give a key not their own). A layout fails when, solved with its keys, a key the vanilla solve
holds isn't reached, a place appears that vanilla keys don't reach, or a moved key can be walked past
where its own spot can't (below) or sits in a chest the walk never reaches (M126's spot 84 spawns only
after a touch in the room, V0x42|0x1, a bit the walk doesn't keep). Exit 1 on any failure.

--hard <pe_randomizer.c>: Hard layouts only (others skipped), walked with Hard's moved doors (s_doors, from
Day 2), and a moved key's own rule is Hard's (hardreach.py's): it can't be met at its lock without the
chest still ahead. With the key never found, every place still reached where a step needs the key (its
lock asks) and that has a way on to a place where the key's own spot gives has one to a place where the
chest gives. Relative to the key's own spot as above; the chest may be passed by anywhere else.

Without --layouts it writes key logic in keylogic.py's format for spottable.py (per spot needs= and
before=), from the walk alone. The walk's places with the keys found on the way (storywalk's solve,
vanilla items). The walk settles
which steps need which keys, not story order (its places over-approximate: the vanilla solve reaches
every place the walk has, and in no story order), so a spot's needs are the keys it can't be reached
without: one solve per key held back, the spot's gives run at no place reached. Those keys, with the
needs of their own spots, must then reach it on their own (a solve with every other key held back),
else the spot stays unknown (two ways in behind different keys). Then, per chest and per key whose spot
has the chest's story day and save area (the generator's day rule, and Medium's area rule for every
difficulty: an area is where the walk can't tell an early key from an on-time one), the walk again with
that key in the chest (the chest's item at the key's spot). The chest may take the key (before=) when:
- it doesn't need the key;
- every key the vanilla solve holds is still held (the key, and every key behind it, is reached);
- no place is reached that the vanilla solve doesn't reach (a guard only: with the walk as it is, the
  vanilla solve reaches every place, which is why the area bound is there);
- it can't be walked past for good where its own spot can't: with the key never found, every place still
  reached that has a way on (other keys held) to a place where the key's own spot gives has one to a place
  where the chest gives. Relative to the key's own spot, so places the walk leaves in a dead end (a
  cutscene's door it doesn't follow, a main-loop scene) count against neither; and a key whose lock a
  player may never meet still has to pass it.
Spots no solve reaches stay unknown (no line: they never take a key). This logic is only as good as the
walk's lock graph: a flag-gated door whose flag some room also sets unconditionally (M329's start sets
the St. Francis door's V0x41|0x1) reads as open without the key, so compare it with keys-by-hand.txt
before using it."""
import collections, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from keyscan import KEYS
from doorscan import read_exits
from storywalk import Graph, read_runs, read_zones, walk, gives_at

SPOT = re.compile(r"\s*\{ 0x([0-9A-F]+), PE_RAND_(\w+), (\d+), (\d+), ([\w |]+), [^/]*/\* (\d+): ((?:M\d+\+[0-9A-F]+ ?)+)")


def read_spots(path):
    """{spot: (vanilla item, kind, day, area, [(room, room offset) of each give])} from pe_rand_spots.h."""
    out = {}
    for line in open(path):
        m = SPOT.match(line)
        if m:
            gives = [(int(x[1:x.index("+")]), int(x[x.index("+") + 1:], 16)) for x in m.group(7).split()]
            out[int(m.group(6))] = (int(m.group(1), 16), m.group(2), int(m.group(3)), int(m.group(4)), gives)
    return out


def spot_of(spot):
    """The table spot of a story-graph give (its "spot=63 gift" field), or None."""
    return int(spot.split()[0][5:]) if spot.startswith("spot=") else None


def solve(start, succ, gives, item_at, withheld=()):
    """storywalk's solve with the story gates' rule: {place: order reached}, {key: place first held}. A
    key opens its locks once picked up and its own spot's give has run (the mod's gate; for a key in its
    own spot that's the same give, and every key test the walk meets is a gated lock). A give whose
    keys come later runs when they do. Keys in withheld are never picked up."""
    reached, held, first = {start: 0}, set(), {}
    picked, own = set(), set()
    frontier, waiting, later = collections.deque([start]), [], []
    def give(p, item, k, spot):
        if not k <= held: later.append((p, item, k, spot)); return
        got = item_at(spot, item)
        if got in KEYS and got not in withheld: picked.add(got)
        if item in KEYS: own.add(item)
        for key in sorted((picked & own) - held): held.add(key); first[key] = p
    while frontier or waiting or later:
        if not frontier:
            n = len(held)
            todo, later = later, []
            for g in todo: give(*g)
            again = [(p, q) for p, q, k in waiting if k <= held and q not in reached]
            waiting = [(p, q, k) for p, q, k in waiting if not k <= held]
            if not again and len(held) == n: break
            for p, q in again:
                if q not in reached: reached[q] = len(reached); frontier.append(q)
            continue
        p = frontier.popleft()
        for item, k, room, off, spot in gives.get(p, ()): give(p, item, k, spot)
        for q, es in succ.get(p, {}).items():
            if q in reached: continue
            k = min((k for k, _ in es), key=len)
            if k <= held: reached[q] = len(reached); frontier.append(q)
            else: waiting.append((p, q, k))
    return reached, first


def layout_item(layout):
    """item_at for a layout {spot: item}: table spots give the layout's item, other gives their own."""
    return lambda spot, item: layout.get(spot_of(spot), item) if spot_of(spot) is not None else item


def can_return(succ, reached, targets, without):
    """Places of reached from which some place in targets is reached again, over steps needing no key in
    without (other keys count as held: the player can fetch them)."""
    back = {}
    for p in reached:
        for q, es in succ.get(p, {}).items():
            if q in reached and any(not (k & without) for k, _ in es): back.setdefault(q, []).append(p)
    seen, todo = set(targets), list(targets)
    while todo:
        for p in back.get(todo.pop(), ()):
            if p not in seen: seen.add(p); todo.append(p)
    return seen


def moved_doors(path):
    """{(room, offset): (room, V0x1)}: Hard's moved doors, from pe_randomizer.c's s_doors."""
    out = {(int(m.group(1)), int(m.group(2), 16)): (int(m.group(3)), int(m.group(4), 16))
            for m in re.finditer(r'\{(\d+), 0x(\w+), \d+, (\d+), 0x(\w+)(?:, "[^"]*")?\}', open(path).read())}
    if not out: sys.exit(f"{path}: no s_doors rows (has the PeRandDoor row format changed?)")
    return out


def main():
    a = sys.argv[1:]
    if len(a) < 3: sys.exit(__doc__)
    opt = {k: a[a.index(k) + 1] for k in ("--runs", "--zones", "--layouts", "--hard") if k in a}
    g, exits, spots = Graph(a[0]), read_exits(a[1]), read_spots(a[2])
    runs = read_runs(opt["--runs"]) if "--runs" in opt else None
    zones = read_zones(opt["--zones"], opt["--runs"]) if "--zones" in opt else None
    moved = moved_doors(opt["--hard"]) if "--hard" in opt else None
    table, order, succ = walk(g, exits, 431, runs, zones, moved)
    gives = gives_at(g, table, order, {n: s[0] for n, s in spots.items()}, runs)
    vanilla = {n: s[0] for n, s in spots.items()}
    base, base_first = solve(order[0], succ, gives, layout_item(vanilla))
    keys_held = set(base_first)
    # Places where each spot gives, with the keys its way needs.
    where = {}
    for p, gs in gives.items():
        for item, k, room, off, spot in gs:
            n = spot_of(spot)
            if n is not None: where.setdefault(n, []).append((p, k))

    key_spot = {s[0]: n for n, s in spots.items() if s[0] in KEYS}
    missable = {}
    def can_miss(c, key):
        """Why chest c holding key (its item at the key's spot) is unsafe, or "": the walk never reaches the
        chest, or it can be walked past where the key's own spot can't."""
        if (c, key) not in missable and c not in where: missable[(c, key)] = "the walk never reaches the chest"
        if (c, key) not in missable:
            k_spot = key_spot[key]
            layout = dict(vanilla); layout[c], layout[k_spot] = key, vanilla[c]
            lost, _ = solve(order[0], succ, gives, layout_item(layout), withheld={key})
            own = {p for p, _ in where.get(k_spot, ())} & set(lost)
            targets = {p for p, _ in where.get(c, ())} & set(lost)
            past = can_return(succ, lost, own, {key}) - can_return(succ, lost, targets, {key})
            if moved is not None:   # Hard: only where its lock asks
                past = {p for p in past if any(all(key in k for k, _ in es) for es in succ.get(p, {}).values())}
                missable[(c, key)] = ("out of reach when its lock asks, e.g. " + " ".join(f"M{p[0]:03d}@{p[1]:#x}" for p in sorted(past)[:3])) if past else ""
            else: missable[(c, key)] = "can be walked past" if past else ""
        return missable[(c, key)]

    if "--layouts" in opt:
        bad = 0
        lines = [l.split() for l in open(opt["--layouts"]) if l.strip() and not l.startswith("#")]
        if moved is not None: lines = [f for f in lines if f[1] == "2"]   # PE_RAND_HARD
        for f in lines:
            layout, moved = dict(vanilla), []
            for x in f[2:]:
                n, item = x.split("="); layout[int(n)] = int(item, 16)
                if int(item, 16) in KEYS: moved.append((int(n), int(item, 16)))
            got, first = solve(order[0], succ, gives, layout_item(layout))
            missing, extra = keys_held - set(first), set(got) - set(base)
            missed = [(c, key) for c, key in moved if can_miss(c, key)]
            if missing or extra or missed:
                bad += 1
                print(f"FAIL seed {f[0]} difficulty {f[1]}:"
                      + (f" keys not reached: {', '.join(KEYS[k] for k in sorted(missing))};" if missing else "")
                      + (f" {len(extra)} places only with keys early, e.g. " + " ".join(f"M{p[0]:03d}@{p[1]:#x}" for p in sorted(extra)[:5]) + ";" if extra else "")
                      + (" " + ", ".join(f"{KEYS[key]} in spot {c} ({can_miss(c, key)})" for c, key in missed) if missed else ""))
        print(f"# {len(lines)} layouts, {bad} failed; {len(missable)} chest / key pairs checked")
        sys.exit(1 if bad else 0)

    def reached_spots(withheld):
        got, first = solve(order[0], succ, gives, layout_item(vanilla), withheld)
        held = set(first)
        return {n for n, ps in where.items() if any(p in got and k <= held for p, k in ps)}
    reach = reached_spots(())
    without = {key: reached_spots({key}) for key in sorted(keys_held)}
    needs = {}
    for n in sorted(reach):
        need = {key for key in keys_held if n not in without[key]}
        while True:   # with the needs of those keys' own spots
            more = {k2 for key in need if key in key_spot for k2 in keys_held if key_spot[key] not in without[k2]}
            if more <= need: break
            need |= more
        if n in reached_spots(keys_held - need): needs[n] = need
        else: print(f"spot {n}: no single set of keys reaches it, left unknown", file=sys.stderr)
    before = {n: set() for n in needs}
    tried = ok = 0
    for c, (item, kind, day, area, _) in sorted(spots.items()):
        if kind != "CHEST" or c not in needs: continue
        for key, k_spot in sorted(key_spot.items()):
            if spots[k_spot][2:4] != (day, area) or key in needs[c] or k_spot not in needs: continue
            tried += 1
            layout = dict(vanilla); layout[c], layout[k_spot] = key, item
            item_at = layout_item(layout)
            got, first = solve(order[0], succ, gives, item_at)
            if keys_held - set(first) or set(got) - set(base) or can_miss(c, key): continue
            before[c].add(key); ok += 1
    print(f"# Generated by mods/pe_randomizer/tools/keysolve.py from the story walk: {len(needs)} of {len(spots)} spots"
          f" reached, {ok} of {tried} chest / key pairs of a day and area may take the key.")
    hexes = lambda ks: ",".join(f"{x:02X}" for x in sorted(ks))
    for n in sorted(needs):
        item, kind, day, area, members = spots[n]
        room, off = members[0]
        name = f" {KEYS[item]}" if item in KEYS else ""
        print(f"M{room:03d}+{off:06X} needs={hexes(needs[n])} before={hexes(before[n])}   # {n} {kind.lower()} day {day}{name}")


if __name__ == "__main__":
    sys.stdout.reconfigure(newline="\n")
    main()
