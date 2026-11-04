"""Hard's key rule walked in the room scripts: hardreach.py reads doors from the listing (only V0x4a / V0x1
guards, other story flags open); here every door on the way is run headless at the lock-time value, in the
story state of the key's lock room then, and only doors that fire count.

  hardwalk.py arrive <doors.txt> <map-exits.txt> <randomizer-exits.txt> <windows.txt> <pe_rand_spots.h>
                     <pe_randomizer.c> <walk-states.txt> <windows-runs.txt> <story-graph.txt> > walk-arrive-runs.txt
      Per key, per lock-time value (FORCED skipped), the rooms hardreach's doors reach from the lock rooms,
      each with every V0x1 a door into it leaves (999 from the map, a moved door's own; 0 in rooms that
      never read it, as storywalk.py): one run per
      (room, value, V0x1, state), Aya standing still. Its reach= points are the zones she can walk to.
  hardwalk.py zones <randomizer-rooms.txt> <walk-arrive-runs.txt> <arrive ctest output> > walk-zone-runs.txt
      Per arrival run that stays, one run per reach= point and facing (as zoneruns.py), win: which door
      fires there; also each zone centre the run's reach= and cut= leave out (a doorway on a closed floor
      triangle). In rooms with a moved door from Day 2 on, gate lines (the real mod, seed 123456 Hard).
  hardwalk.py zones <randomizer-rooms.txt> <walk-zone-runs.txt> <zone output> <walk-arrive-runs.txt> <arrive output>
                    > walk-zone2-runs.txt
      A chain level (zoneruns.py's): repeat with each level and the one before it until no runs are left.
      --press (first level and its chains): Aya also presses examine at each stop; every facing goes on.
      A press run that ran a menu step (op 67, which the run test otherwise skips: the answer stays item 0)
      runs again at the next level once per answer (menu=0..3, id ...u<N>), whatever its end: M132's elevator
      asks for a floor from V0x4a 0x1ee on, and only item 1 goes down to the first-floor hall M127; the museum
      car M258 needs item 3 for M250.
  hardwalk.py table <the arrive args> <hard-reach.txt> <revisit.txt> <walk-arrive-runs.txt> <arrive output>
                    <zone runs> <zone output> [<zone runs> <zone output> ...] > hard-walk.txt  [proof lines to $HARDWALK_RUNS]
      Per key and value, the rooms reached from the lock rooms by doors that fired (an arrival that ends at
      a door counts as one; the map's stops at that value), and per chest of hard-reach.txt: reached, and a
      way back to the map or a lock room, at every value. Writes the doors of each pair's walk (first found,
      breadth first) as checked lines (door=) for the ctest mod-randomizer-hardwalk.
      --writes <story-writes.txt>: a failing value is excused (pair skip:<why>, not FAIL) by the rules below.
      --deny <hardwalk-deny.txt>: doors the walk may not use (room, target, the state they fired in: a the
      walk's, m the sure bits'; below a V0x4a): ones that fired only on a flag the player may not have then.
      spottable.py --hard-drop takes the FAIL pairs out of Hard.

Excused, not a hole: needs (the chest's needs bit has the key: the generator never puts it there, e.g. a chest
behind the key's own door); before (a value before the key's vanilla day, DAY_START: the windows ctest also ran
locks in walk states the player can't be in); charm (no lock: the give room stands in; their real use is the
cruiser at the end, not walked here); free=<value> (with --writes: a room the walk reaches sets V0x4a on in a
branch that tests nothing but V0x4a / area / event, those hold, it doesn't test the key given, and the pair
passes there).

A key with no key test (pe_rand_locks.h) but a step that takes it (the fuse box, the elevator) asks there:
its values are the walk's states of that room where the take's story bits can hold (from its first give on),
not every value its own chest gives at (revisitruns.key_times, hardreach). Holds every key (keys=all): a chest
behind the key's own door is the generator's to reject. Run in build-mods/mods with python -I."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from keyscan import KEYS
from hardreach import DAY2, FORCED, SHUT, read_doors, read_map, way_ok
from revisitruns import read_spots, read_windows, key_times
from storywalk import Graph, State, holds
from windowruns import read_states, v, var_text

MAP_LEFT = 999      # V0x1 after a map stop (storywalk)
DAY_START = {1: 0, 2: DAY2, 3: 0x190, 4: 0x208}   # V0x4a: M108 jumps to Day 3, M160 to Day 4; later days not set
CHARMS = {0xCB, 0xCC, 0xCD}   # the Good Luck Charms: M430's take holds in no walk state
SET_X = re.compile(r"^(M\d+) \S+ e\d sets V0x4a=(0x\w+)\t(.*)")
TEST_X = re.compile(r"V0x4a (==|!=|<=|>=|<|>) (0x\w+)")
PLAIN = re.compile(r"\(?\((V0x4a|area|event) (==|!=|<=|>=|<|>) (0x\w+|-?\d+)\)\)?")
TESTS = {"==": int.__eq__, "!=": int.__ne__, "<=": int.__le__, ">=": int.__ge__, "<": int.__lt__, ">": int.__gt__}
GATE = "gate seed=123456,2"


def moved_doors(path):
    """(room, offset) -> (new target room, V0x1 the mod sets there): pe_randomizer.c s_doors."""
    out = {}
    for m in re.finditer(r'\{(\d+), 0x(\w+), \d+, (\d+), 0x(\w+)(?:, "[^"]*")?\}', open(path).read()):
        out[(f"M{int(m.group(1)):03d}", f"{int(m.group(2), 16):06X}")] = (f"M{int(m.group(3)):03d}", int(m.group(4), 16))
    if not out: sys.exit(f"{path}: no s_doors rows (has the PeRandDoor row format changed?)")
    return out


def walk_setup(argv):
    """argv: doors, map-exits, exits, windows, spots.h, pe_randomizer.c, walk-states, windows-runs, story-graph.
    -> (spots, gives, {key item: {value: (state vars, [(lock room, V0x1)])}}, edges(x, room, came, shut), moved)."""
    doors, stops = read_doors(argv[0]), read_map(argv[1], argv[2])
    gives, fires, locks = read_windows(argv[3])
    spots, moved = read_spots(argv[4]), moved_doors(argv[5])
    graph = Graph(argv[8])
    reads_left = {f"M{r:03d}" for r in graph.reads_left}
    first = {}    # (room, V0x4a) -> (V0x1, vars): the walk's first state there
    for room, ss in read_states(argv[6]).items():
        for area, left, vars_ in sorted(ss): first.setdefault((room, area), (left, vars_))
    sure = {}     # (room, V0x4a, V0x1, vars) -> vars with only the bits the walk has set on every way there
    takes = {}    # key item -> {V0x4a: {rooms}}: where a walk state lets a step take the key (a fuse box)
    take_steps = {(f"M{room:03d}", item): ways for room, r in graph.rooms.items() for _, item, ways, _ in r["take"]}
    for l in open(argv[6]):
        f = l.rstrip("\n").split("\t")
        st = State(int(f[1][1:]), v(f[2].split("=")[1]), v(f[3].split("=")[1]),
                   {int(x.split("=")[0][1:], 16): v(x.split("=")[1].split("/")[0]) for x in f[4].split()},
                   {int(x.split("=")[0][1:], 16): v(x.split("/")[1]) for x in f[4].split()})
        for (room, item), ways in take_steps.items():
            if room == f[1] and holds(ways, st): takes.setdefault(item, {}).setdefault(st.area, set()).add(room)
        one =tuple(x.split("/")[0] for x in f[4].split())
        sets = tuple(f"{x.split('=')[0]}={v(x.split('=')[1].split('/')[0]) & ~v(x.split('/')[1]):#x}" for x in f[4].split())
        sure.setdefault((f[1], v(f[2].split("=")[1]), v(f[3].split("=")[1]), one), tuple(t for t in sets if not t.endswith("=0x0")))
    for l in open(argv[7]):   # a lock's zone runs: their variables where the walk has no state
        tok = l.split()
        if l.startswith("M") and "id=l" in l and "keys=none" in l:
            first.setdefault((tok[0], v(tok[1])), (v(tok[2]), tuple(t for t in tok[3:] if t.startswith("V0x"))))
    times = {}
    for item, (k, when) in sorted(key_times(spots, gives, fires, locks).items()):
        if not any(it == item for it, _ in locks.values()) and item in takes:
            # no key test: the lock asks where a step takes the key, from the key's first give on
            when = {x: sorted(rooms) for x, rooms in takes[item].items() if x >= min(fires[k])}
        for x, rooms in sorted(when.items()):
            if x in FORCED: continue
            st = [(r, first[r, x]) for r in rooms if (r, x) in first]
            if st:
                (r, (left, vars_)) = st[0]
                times.setdefault(item, {})[x] = (vars_, [(r, first[r, x][0] if r in reads_left else 0)
                                                         for r in rooms if (r, x) in first], sure.get((r, x, left, vars_), vars_))

    def edges(x, room, came, shut):
        """[(target room, V0x1 there)] the listing's doors allow (the map: its stops)."""
        out = []
        for tgt, ways, off in doors.get(room, ()):
            if (room, off) in shut or not any(way_ok(w, x, came) for w in ways): continue
            left = int(room[1:])
            if x >= DAY2 and (room, off) in moved: tgt, left = moved[room, off]
            out += [(r, MAP_LEFT) for r in stops(x)] if tgt == "M000" else [(tgt, left)]
        return [(r, left if r in reads_left else 0) for r, left in out]
    return spots, gives, times, edges, stops, moved


def key_shut(item):
    return frozenset(d for d, names in SHUT.items() if KEYS[item] in names)


def arrive(argv):
    spots, gives, times, edges, stops, moved = walk_setup(argv[:9])
    gate_rooms = {r for r, _ in moved}
    made = {}
    for item, by_x in times.items():
        shut = key_shut(item)
        for x, (vars_, starts, sure_vars) in by_x.items():
            seen, todo = set(starts), list(starts)
            while todo:
                room, came = todo.pop()
                for nxt in edges(x, room, came, shut):
                    if nxt not in seen: seen.add(nxt); todo.append(nxt)
            for room, came in seen:
                gate = f" {GATE}" if x >= DAY2 and room in gate_rooms else ""
                line = f"{room} {x:#x} {came:#x} {' '.join(vars_)} keys=all win{gate}".replace("  ", " ")
                made.setdefault(line, f"{room} {x:#x} {came:#x} {' '.join(sure_vars)} keys=all win{gate}".replace("  ", " "))
    print(f"# Generated by mods/pe_randomizer/tools/hardwalk.py arrive: {len(made)} arrivals at Hard's lock-time values"
          f" (a: the walk's state, every bit it may have set; m: only the bits set on every way).")
    for i, (line, sure_line) in enumerate(made.items()):
        print(f"{line} id=a{i}")
        if sure_line != line: print(f"{sure_line} id=m{i}")
    sys.stderr.write(f"hardwalk arrive: {len(made)} runs\n")


ANGLES = (0, 0x400, 0x800, 0xC00)   # zoneruns.py: the four quarters door tests split facing into
MENUS = (0, 1, 2, 3)                # menu=N answers a press run's menu steps (M132: 3 floors, M257/M258: 4)
AT_MAX = 8                          # the run test's stops in one line
REACH = re.compile(r"\+([0-9A-F]+)@(-?\d+),(-?\d+)")


def runs_by_id(runs_path, out_path):
    """{id: (runs line without id=, output fields)}: outputs paired by their echoed id= (crashed runs left out)."""
    lines = {}
    for l in open(runs_path):
        if l.startswith("M"):
            w = l.split()
            lines[w[-1][3:]] = " ".join(w[:-1])
    out = {}
    for l in open(out_path):
        f = dict(x.split("=", 1) for x in l.rstrip("\n").split("\t")[2:] if "=" in x)
        if f.get("id") in lines: out[f["id"]] = (lines[f["id"]], f)
    return out


def room_zones(path):
    """room -> [(zone test offset, x, z)]: each PointInPoly step (op 119) with a constant polygon, its centre."""
    out = {}
    for l in open(path):
        f = l.rstrip("\n").split("\t")
        if f[0] != "step" or f[4] != "119": continue
        a = f[5].split(",")[:8]
        if not all(x.startswith("0:") for x in a): continue
        c = [((int(x[2:], 16) ^ 0x80000000) - 0x80000000) >> 16 for x in a]
        out.setdefault(f[1], []).append((int(f[3], 16), sum(c[0::2]) // 4, sum(c[1::2]) // 4))
    return out


def room_menus(path):
    """room -> {listing offsets of its menu steps (op 67, Script_GetMenuChoice)}."""
    out = {}
    for l in open(path):
        f = l.rstrip("\n").split("\t")
        if f[0] == "step" and f[4] == "67": out.setdefault(f[1], set()).add(int(f[3], 16))
    return out


def zones(argv):
    """argv: randomizer-rooms.txt, runs, output[, parent runs, parent output]: with a parent level, a chain
    (zoneruns.py): a facing-0 zone run that stays and can reach zones its parent couldn't (a routine its zone
    started) gets one more stop. First level: also the centre of each zone of the room the run named in neither
    reach= nor cut= (a doorway on a closed floor triangle: the test finds no reached point inside it)."""
    press = "--press" in argv
    argv = [a for a in argv if a != "--press"]
    centres, menus = room_zones(argv[0]), room_menus(argv[0])
    argv = argv[1:]
    n, parent = 0, {}
    if len(argv) > 2:
        for i, (line, f) in runs_by_id(argv[2], argv[3]).items():
            parent[i] = {z for z, _, _ in REACH.findall(f.get("reach", ""))}
    print("# Generated by mods/pe_randomizer/tools/hardwalk.py zones: each run's newly reachable zones, four facings.")
    for i, (line, f) in runs_by_id(argv[0], argv[1]).items():
        room = line.split()[0]
        if parent and " press" in line and "menu=" not in line and menus.get(room, set()) & \
                {int(x, 16) for x in f.get("steps", "").split(",") if x}:
            for k in MENUS:
                print(f"{line} menu={k} id={i}u{k}"); n += 1
        if f.get("end") != "idle": continue
        known, stops = set(), [w for w in line.split() if w.startswith("at=")]
        if parent:
            if (not press and not i.endswith(".0")) or len(stops) >= AT_MAX: continue
            up = i.rsplit("z", 1)[0]
            known = parent.get(up, parent.get(up.rstrip("p"), set()))
        been = {",".join(w[3:].split(",")[:2]) for w in stops}
        if press and not parent: line, i = line + " press", i + "p"
        points = REACH.findall(f.get("reach", ""))
        if not parent:
            named = {int(z, 16) for z, _, _ in points} | {int(z[1:], 16) for z in f.get("cut", "").split(",") if z}
            points += [(f"{z:X}", str(x), str(y)) for z, x, y in centres.get(room, ()) if z not in named]
        for z, x, y in points:
            if z in known or f"{x},{y}" in been: continue
            for angle in ANGLES:
                print(f"{line} at={x},{y},{angle} id={i}z{z}.{angle:x}"); n += 1
    sys.stderr.write(f"hardwalk zones: {n} runs\n")


def door_end(f):
    """The room a run ended at a door to, or None (M000: the New York map)."""
    e = f.get("end", "")
    return e[5:] if e.startswith("door M") else None


def story_steps(path, item, x, rooms):
    """Values a room in rooms sets V0x4a to at x without testing the key given (story-writes.txt)."""
    out = set()
    for l in open(path):
        m = SET_X.match(l.rstrip("\n"))
        if not m or m.group(1) not in rooms or v(m.group(2)) <= x or f"gave({item:#x}) != 0x0" in m.group(3): continue
        # one branch with nothing but V0x4a / area / event tests, all holding at x (any other flag: not sure)
        for alt in m.group(3).split(" | "):
            conds = [c for c in alt.split(" ; ") if c and c != "always"]
            if all(PLAIN.fullmatch(c) for c in conds) and all(TESTS[op](x, v(n)) for op, n in TEST_X.findall(alt)):
                out.add(v(m.group(2)))
    return out


def read_deny(path):
    """[(room, target, state a/m, below V0x4a)]: doors the walk may not use (hardwalk-deny.txt)."""
    out = []
    for l in open(path):
        w = l.split("#")[0].split()
        if w: out.append((w[0], w[1], w[2], v(w[3]) if len(w) > 3 else 1 << 16))
    return out


def table(argv):
    writes = argv[argv.index("--writes") + 1] if "--writes" in argv else None
    if writes: argv = argv[:argv.index("--writes")] + argv[argv.index("--writes") + 2:]
    deny = read_deny(argv[argv.index("--deny") + 1]) if "--deny" in argv else []
    if "--deny" in argv: argv = argv[:argv.index("--deny")] + argv[argv.index("--deny") + 2:]
    spots, gives, times, edges, stops, moved = walk_setup(argv[:9])
    reads_left = {f"M{r:03d}" for r in Graph(argv[8]).reads_left}
    hard_path, gives_runs, arrive_runs, arrive_out = argv[9:13]
    allowed = {}
    for l in open(hard_path):
        f = l.split("\t")
        if f[0] == "hard": allowed[int(f[1])] = {v(i) for i in f[3][5:].split(",")}
    entry = {}    # (room, give) -> V0x1 its gives-runs line enters by (the room version that gives)
    for l in open(gives_runs):
        if l.startswith("M"):
            tok = l.split()
            for w in (t[6:] for t in tok if t.startswith("want=")): entry.setdefault((tok[0], w), v(tok[2]))
    moved_to = {(room, tgt): came for (room, _), (tgt, came) in moved.items()}

    node = {}     # (room, V0x4a, V0x1, state vars) -> arrival id
    arrivals = runs_by_id(arrive_runs, arrive_out)
    for i, (line, f) in arrivals.items():
        w = line.split()
        if i.startswith("a"): node[w[0], v(w[1]), v(w[2]), tuple(t for t in w[3:] if t.startswith("V0x"))] = i
    versions = {}   # (room, V0x4a, vars) -> its arrival ids, one per V0x1
    for (room, x, came, vars_), i in node.items(): versions.setdefault((room, x, vars_), []).append(i)
    fired = {}    # arrival id -> {target room or M000: run line that fired it}
    for i, (line, f) in arrivals.items():
        if door_end(f): fired.setdefault(i, {}).setdefault(door_end(f), line)
    for zone_runs, zone_out in zip(argv[13::2], argv[14::2]):
        for zid, (line, f) in runs_by_id(zone_runs, zone_out).items():
            if door_end(f): fired.setdefault(zid.split("z")[0].rstrip("p"), {}).setdefault(door_end(f), line)
    either = bool(os.environ.get("HARDWALK_EITHER"))
    out_edges = {}   # a-id -> [(target, the lines that fired it: the walk's state, and the sure bits' where run)]
    for i in node.values():
        a, m = fired.get(i, {}), fired.get("m" + i[1:], {}) if "m" + i[1:] in arrivals else None
        if m is None: out_edges[i] = [(t, [l]) for t, l in a.items()]
        elif either: out_edges[i] = [(t, [l]) for t, l in a.items()] + [(t, [l]) for t, l in m.items() if t not in a]
        else: out_edges[i] = [(t, [l, m[t]]) for t, l in a.items() if t in m]

    for i, edges_i in out_edges.items():   # --deny: a door that fired only in a walk state the player can't be in
        w = arrivals[i][0].split()
        out_edges[i] = [(t, ls) for t, ls in edges_i
                        if not any((w[0], t) == d[:2] and v(w[1]) < d[3] and
                                   d[2] == ("a" if any(tok.startswith("V0x") for tok in ls[0].split()[3:]) else "m") for d in deny)]

    def succ(x, vars_, room, i):
        for tgt, line in out_edges.get(i, ()):
            if tgt == "M000":
                nxt = [(r, MAP_LEFT) for r in stops(x)]
            else:
                came = moved_to.get((room, tgt), int(room[1:])) if x >= DAY2 else int(room[1:])
                nxt = [(tgt, came)]
            for r, came in nxt:
                came = came if r in reads_left else 0
                yield r, came, node.get((r, x, came, vars_)), tgt, line

    proof, rows, missing, rows_reach, frontier, reach = {}, [], set(), [], {}, {}
    for item, by_x in sorted(times.items()):
        for x, (vars_, starts, _) in sorted(by_x.items()):
            start_ids = {node.get((r, x, c, vars_)) for r, c in starts} - {None}
            start_rooms = {r for r, _ in starts}
            parent = {i: None for i in start_ids}
            todo = list(start_ids)
            while todo:   # breadth first: short walks
                i = todo.pop(0)
                w = arrivals[i][0].split()
                for r, came, j, tgt, line in succ(x, vars_, w[0], i):
                    if j is None: missing.add((r, x, came)); continue
                    if j not in parent: parent[j] = (i, line, tgt); todo.append(j)
            reached = sorted({arrivals[i][0].split()[0] for i in parent})
            reach[item, x] = set(reached)
            for i in parent:   # listed doors out of the walk: where it stops short of hardreach
                w = arrivals[i][0].split()
                for r, came in edges(x, w[0], v(w[2]), key_shut(item)):
                    j = node.get((r, x, came, vars_))
                    if j not in parent: frontier.setdefault((w[0], r), set()).add((item, x))
            rows_reach.append(f"reach	{KEYS[item]}	{x:#x}	from={','.join(sorted(start_rooms))}	rooms={len(reached)}	{','.join(reached)}")
            outs = {}     # arrival id -> the walk back out (to the map or a lock room), or None

            def back(i):
                if i in outs: return outs[i]
                seen, todo, par = {i: None}, [i], None
                if arrivals[i][0].split()[0] in start_rooms: outs[i] = []; return []
                while todo and par is None:
                    a = todo.pop(0)
                    for r, came, j, tgt, line in succ(x, vars_, arrivals[a][0].split()[0], a):
                        if tgt == "M000" or r in start_rooms: par = (a, line, tgt); break
                        if j is not None and j not in seen: seen[j] = (a, line, tgt); todo.append(j)
                if par is None: outs[i] = None; return None
                walk, (a, line, tgt) = [(line, tgt)], par
                while seen[a]: a, line, tgt = seen[a]; walk.append((line, tgt))
                outs[i] = walk[::-1]; return outs[i]

            for n in sorted(allowed):
                if item not in allowed[n]: continue
                ok = None
                for room, give in gives.get(n, ()):
                    # the room version its gives line ran (V0x1), where a listed door leads to it; else any
                    came = entry.get((room, give), 0) if room in reads_left else 0
                    js = [node[room, x, came, vars_]] if (room, x, came, vars_) in node else versions.get((room, x, vars_), [])
                    j = next((j for j in js if j in parent and back(j) is not None), None)
                    if j is None: continue
                    way_back = back(j)
                    walk, a = [], j
                    while parent[a]: a, line, tgt = parent[a]; walk.append((line, tgt))
                    ok = walk[::-1] + way_back; break
                rows.append((item, n, x, ok))
                for lines, tgt in ok or ():
                    for line in lines: proof.setdefault(f"{line} door={tgt}", set()).add(item)
    pairs = {}
    for item, n, x, ok in rows: pairs.setdefault((item, n), []).append((x, ok))
    key_spots = [k for k, s in sorted(spots.items()) if s["key"]]
    home = {s["item"]: k for k, s in sorted(spots.items(), reverse=True) if s["key"]}   # the key's vanilla spot

    def excuse(item, n, x, xs):
        """Why a failing value is not a hole, or None."""
        if spots[n]["needs"] & (1 << key_spots.index(home[item])): return "needs"
        if x < DAY_START[min(spots[home[item]]["day"], max(DAY_START))]: return "before"
        if item in CHARMS: return "charm"
        passes = {y for y, ok in xs if ok}
        for y in sorted(story_steps(writes, item, x, reach[item, x]) if writes else ()):
            if y in passes: return f"free={y:#x}"
        return None
    bad, skipped = 0, {}
    print("# Generated by mods/pe_randomizer/tools/hardwalk.py table: each Hard key x chest pair walked by doors that")
    print("# fired in the room scripts at every lock-time value of the key (doors on the way: rooms passed through).")
    for (item, n), xs in sorted(pairs.items()):
        why = {x: excuse(item, n, x, xs) for x, ok in xs if ok is None}
        fail = [x for x, w in why.items() if w is None]
        bad += bool(fail)
        kind = "FAIL" if fail else "skip:" + ",".join(sorted({w.split("=")[0] for w in why.values()})) if why else "ok"
        if kind.startswith("skip"): skipped[kind] = skipped.get(kind, 0) + 1
        longest = max((len(ok) for x, ok in xs if ok), default=0)
        print(f"pair\t{n}\t{KEYS[item]}\t{kind}\tvalues={len(xs)}\tdoors<={longest}"
              + (f"\tfails={','.join(f'{x:#x}' for x in fail)}" if fail else "")
              + (f"\texcused={','.join(f'{x:#x}:{w}' for x, w in why.items() if w)}" if any(why.values()) else ""))
    for r in rows_reach: print(r)
    failing = {(item, x) for item, n, x, ok in rows if ok is None}
    for (a, b), kx in sorted(frontier.items(), key=lambda e: -len(e[1] & failing)):
        if kx & failing: print(f"frontier	{a} -> {b}	failing key-values={len(kx & failing)}")
    print(f"# summary ({'a door fires in either state' if either else 'a door fires in both states'}): {len(pairs)} pairs, {bad} fail"
          f" ({', '.join(f'{k} {c}' for k, c in sorted(skipped.items())) or 'none'} excused); {len(proof)} doors in the walks;"
          f" {len(missing)} rooms a door reached without an arrival run")
    if os.environ.get("HARDWALK_RUNS"):
        with open(os.environ["HARDWALK_RUNS"], "w", newline="\n") as f:
            f.write("# Generated by mods/pe_randomizer/tools/hardwalk.py table: every door on the walks to Hard's key chests,"
                    " at the key's lock-time values (ctest mod-randomizer-hardwalk).\n")
            for line, items in proof.items():
                f.write(f"{line} id=h{'.'.join(f'{i:X}' for i in sorted(items))}\n")


if __name__ == "__main__":
    sys.stdout.reconfigure(newline="\n")
    {"arrive": arrive, "zones": zones, "table": table}[sys.argv[1]](sys.argv[2:])
