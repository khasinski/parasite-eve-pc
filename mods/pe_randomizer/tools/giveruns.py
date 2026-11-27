"""Runs lines that check the randomizer's spots in the mod-randomizer-run test (ctest mod-randomizer-gives):
every table give a run reaches hands out the seed's item, and a spot seen in several rooms is one chest or
pickup there: taking it in one room sets its flag, and with that flag set the other rooms' copies give
nothing. Run in build-mods/mods:

  python -I giveruns.py cands randomizer-spots.txt randomizer-rooms.txt runs.txt randomizer-run.txt [zone-runs.txt:zone-run.txt ...] > give-cands.txt
  test-mod-randomizer-run give-cands-out.txt give-cands.txt          (PE_TEST_DISC set; seconds)
  python -I giveruns.py pick randomizer-spots.txt give-cands.txt give-cands-out.txt > gives-runs.txt

randomizer-spots.txt and randomizer-rooms.txt come from mod-randomizer-rooms, runs.txt from runlist.py and
randomizer-run.txt is runall.sh's output for it. With win on every runs.txt line (and the zone runs made
from those), each battle is won and the candidates carry win: chests a room places only after its fight
(M207, M232) are reached. cands: per table give, Aya stands on the give's actor (at=@E:
the touch that opens a chest or starts a talk) and presses, at 4 facings, at up to 4 idle places of the room
where that actor is placed (spread over V0x4a) and at V0x4a = 0. Runs and outputs given as pairs (the zone
runs of zoneruns.py, which reach floor pickups by walking into their zones) add the line with the fewest stops
that ran each give there (minutes over the full zone runs). Last come the variables a way to the give needs
(doorscan's ways to it and to each step that spawns its actor, op 8 or op 17 2500: (V == n) sets V, (V & bit)
sets the bit; e.g. a sewer chest placed only at one V0x37/V0x38 cell, a chest spawned once a story bit is set),
at V0x4a = 0 and two idle places of the room. An actor with no spawn step is a starting actor (the room's
scene table) or never placed. Then the touches a give's way names: most gifts run in Aya's own script
(type 0), in her contact task, which reads the type of the actor she touched into a field (op 36) and tests
it (A0x1 == 2: the NYPD gifts); she stands on that actor at places where it is placed, or, in Aya's script
with no such test, on each other actor placed there. Each touch also gets one long talk (TALK stops on the
same actor: the M344 Klamp Key talk runs ~1,000 frames). Last, the variable sets with those touches (and
the spawns of the touched actors: M100's second gift spawns its giver on V0x2c&0x4000000), and on the lines
that ran another give of the same actor in the room (a second gift tests the first one's bit: M121 V0x2e&1,
M154's Elevator Key V0x43&0x10). pick: per give, the first candidate that ran it,
with want=; a give whose spot has an opened / taken flag (the flag lines) also gets flag=, and each copy
of the spot in another room a run with that flag set and never=. Each line is written once per seed in
SEEDS. Gives no candidate reached are listed on stderr."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import doorscan

SEEDS = ((111111, 0), (222222, 1), (333333, 2))   # Easy, Medium, Hard
FACINGS = (0, 0x400, 0x800, 0xC00)
EQ = re.compile(r"\(V0x([0-9a-f]+) == 0x([0-9a-f]+)\)")
BIT = re.compile(r"\(V0x([0-9a-f]+) & 0x([0-9a-f]+)\)")
TOUCHED = re.compile(r"\(A0x([0-9a-f]+) == 0x([0-9a-f]+)\)")
SETTINGS = 4   # variable sets tried per give
AYA = 0        # Aya's own script: actor type 0 in every room
TALK = 4       # stops in a row on the same actor for a long talk (AT_FRAMES each in the run test)


def settings(way):
    """[(values, bits)] one way needs, from its (V == n) and (V & bit) conditions (negated and or-ed ones are
    left at 0). A variable tested against two values (it changes along the way: a sewer cell's routine tests
    V0x38 before and after moving) gives one set per value."""
    values, bits = {}, {}
    for c in way:
        if c.startswith("!") or "||" in c: continue
        for v, n in EQ.findall(c): values.setdefault(int(v, 16), set()).add(int(n, 16))
        for v, bit in BIT.findall(c): bits[int(v, 16)] = bits.get(int(v, 16), 0) | int(bit, 16)
    out = [{}]
    for v, ns in sorted(values.items()): out = [dict(o, **{str(v): n}) for o in out for n in sorted(ns)][:SETTINGS]
    return [({int(v): n for v, n in o.items()}, bits) for o in out]


def spawn_ways(room, entity):
    """Ways to the steps that spawn an actor type, op 8 or op 17 2500 ([always] for a starting actor)."""
    spawns = [at for at, (_, op, a, _) in room.steps.items()
              if (op == 8 and a and a[0] == (0, entity)) or (op == 17 and len(a) > 1 and a[0] == (0, 2500) and a[1] == (0, entity))]
    return [w for at in spawns for w in room.ways(at)] if spawns else [frozenset()]


def touched(room, entity, give_at):
    """Actor types a way to the give tests its actor touched: op 36 reads the type of the actor in contact
    into an entity field, and the way tests that field (A0x1 == 2: Aya's talk with type 2 in M116)."""
    fields = {a[0][1] for t, op, a, _ in room.steps.values() if t == entity and op == 36 and a and a[0][0] == 1}
    return sorted({int(n, 16) for w in room.ways(give_at) for c in w if not c.startswith("!")
                   for k, n in TOUCHED.findall(c) if int(k, 16) in fields})


def need_sets(room, entity, give_at, touch=()):
    """Distinct non-empty variable sets the ways to a give and to the spawn steps of its actor and of the
    actors it must touch need, fewest first."""
    ways = [frozenset()]
    for t in (entity, *touch): ways = [w | s for w in ways for s in spawn_ways(room, t)][:64]
    out = {}
    for sw in ways:
        for gw in room.ways(give_at):
            for values, bits in settings(sw | gw):
                if values or bits: out.setdefault((tuple(sorted(values.items())), tuple(sorted(bits.items()))), len(sw | gw))
    return sorted(out, key=out.get)[:SETTINGS]


def with_vars(line, values, bits=()):
    """A runs line with V = n set (V0x4a and V0x1 are its own fields) and bits ORed into V, on top of the
    line's own value of that variable."""
    tokens = line.split()
    for v, n in values:
        if v in (0x4a, 0x1): tokens[1 if v == 0x4a else 2] = f"{n:#x}"; continue
        tokens = [t for t in tokens if not (t.startswith("V") and "=" in t and int(t[1:t.index("=")], 16) == v)]
        tokens.insert(3, f"V{v:#x}={n:#x}")
    for v, bit in bits:
        for i, t in enumerate(tokens):
            if t.startswith("V") and "=" in t and int(t[1:t.index("=")], 16) == v:
                tokens[i] = f"V{v:#x}={int(t.split('=')[1], 16) | bit:#x}"; break
        else: tokens.insert(3, f"V{v:#x}={bit:#x}")
    return " ".join(tokens)


def table_gives(path):
    """{(room, block offset): (room offset, spot, entity)} and {room offset key: (var, bit)} from randomizer-spots.txt."""
    gives, flags = {}, {}
    for line in open(path):
        p = line.rstrip("\n").split("\t")
        if p[0] == "give" and len(p) > 6 and p[6].startswith("spot="):
            gives[(p[1], int(p[4], 16))] = (int(p[2][1:], 16), int(p[6][5:]), p[3])
        elif p[0] == "flag" and p[4] != "-":
            var, bit = p[3][1:].split("|")
            flags[(p[1], int(p[2][1:], 16))] = (int(var, 16), int(bit, 16))
    return gives, flags


def cands(spots, rooms_path, runs, outs, *pairs):
    gives, _ = table_gives(spots)
    listing, scripts = doorscan.load(rooms_path), {}
    rooms, found = {room for room, _ in gives}, {}
    for pair in pairs:   # the line with the fewest stops, then the lowest V0x4a, that ran each give
        zone_runs, zone_outs = pair.split(":")
        for li, lo in zip(open(zone_runs), open(zone_outs)):
            if "\tgave=+" not in lo or lo.split("\t", 2)[1] not in rooms: continue
            room = lo.split("\t", 2)[1]
            if not li.startswith(room): sys.exit(f"{zone_runs} and {zone_outs} don't pair up: {li.strip()!r}")
            for x in lo.split("\tgave=", 1)[1].split("\t", 1)[0].split(","):
                key, score = (room, int(x[1:], 16)), (li.count("at="), int(li.split()[1], 16))
                if key in gives and (key not in found or score < found[key][0]): found[key] = (score, li.strip())
    places = {}
    for li, lo in zip(open(runs), open(outs)):
        f = lo.rstrip("\n").split("\t")
        if "end=idle" not in f or "battle=1" in f: continue
        alive = next((x[6:] for x in f if x.startswith("alive=")), "").split(",")
        places.setdefault(f[1], []).append((int(li.split()[1], 16), li.strip(), set(alive)))
    for (room, at), (room_at, spot, entity) in sorted(gives.items()):
        ps = sorted({(a, line) for a, line, alive in places.get(room, []) if entity in alive})
        pick = [ps[i * (len(ps) - 1) // 3] for i in range(4)] if len(ps) > 4 else ps
        print(f"# spot {spot} {room} +{room_at:06X} e{entity}")
        if (room, at) in found: print(found[(room, at)][1])
        for line in dict.fromkeys([line for _, line in pick] + [f"{room} 0x0 0x0"]):
            for facing in FACINGS: print(f"{line} keys=all press at=@{entity},{facing}")
        n = int(room[1:])
        if n not in scripts: scripts[n] = doorscan.Room(*listing[n])
        idle = sorted({(a, line) for a, line, _ in places.get(room, [])})
        bases = [f"{room} 0x0 0x0"] + [line for _, line in (idle[::max(1, len(idle) // 2)][:2] if idle else [])]
        for values, bits in need_sets(scripts[n], int(entity), at):
            for line in dict.fromkeys(with_vars(b, values, bits) for b in bases):
                for facing in FACINGS: print(f"{line} keys=all press at=@{entity},{facing}")
        # After those (a give reached before keeps its line): the actors the give's way says its actor
        # touched, or, for a give in Aya's own script that names none, each other actor placed there;
        # one long talk per actor; the variable sets again with those touches, and on the lines that ran
        # another give of the same actor in this room (a second gift that needs the first one's bit).
        touch, aya = touched(scripts[n], int(entity), at), int(entity) == AYA
        alive_at = {line: alive for _, line, alive in places.get(room, [])}
        if touch:   # places where the actors to touch are placed too
            ps = sorted({(a, line) for a, line, alive in places.get(room, []) if {entity, *map(str, touch)} <= alive})
            pick = [ps[i * (len(ps) - 1) // 3] for i in range(4)] if len(ps) > 4 else ps
        for line in dict.fromkeys([line for _, line in pick] + [f"{room} 0x0 0x0"]):
            types = touch or (sorted({int(t) for t in alive_at.get(line, ()) if t} - {AYA}) if aya else [int(entity)])
            for t in types:
                if touch or aya:
                    for facing in FACINGS: print(f"{line} keys=all press at=@{t},{facing}")
                print(f"{line} keys=all press" + f" at=@{t},0" * TALK)
        siblings = [found[k][1] for k, g in sorted(gives.items()) if k in found and k != (room, at) and k[0] == room and g[2] == entity]
        for values, bits in need_sets(scripts[n], int(entity), at, touch):
            for line in dict.fromkeys(with_vars(b, values, bits) for b in bases if touch):
                for t in touch:
                    for facing in FACINGS: print(f"{line} keys=all press at=@{t},{facing}")
            for line in dict.fromkeys(with_vars(b, values, bits) for b in siblings): print(line)


def pick(spots, cand_path, out_path):
    gives, flags = table_gives(spots)
    lines = [l.strip() for l in open(cand_path) if l.strip() and not l.startswith("#")]
    outs = [l.rstrip("\n") for l in open(out_path)]
    if len(lines) != len(outs): sys.exit(f"{len(lines)} candidates, {len(outs)} outputs")
    first = {}
    for line, out in zip(lines, outs):
        room = out.split("\t")[1]
        ran = out.split("\tgave=", 1)[1].split("\t", 1)[0] if "\tgave=" in out else ""
        for x in ran.split(","):
            if x and (room, int(x[1:], 16)) in gives: first.setdefault((room, int(x[1:], 16)), line)
    by_spot = {}
    for key, (room_at, spot, _) in gives.items(): by_spot.setdefault(spot, []).append(key)
    checks = []
    for spot, members in sorted(by_spot.items()):
        for key in sorted(members):
            room, at = key
            if key not in first: print(f"no run reaches spot {spot}: {room} +{gives[key][0]:06X}", file=sys.stderr); continue
            flag = flags.get((room, gives[key][0]))
            checks.append(f"{first[key]} want=+{at:X}" + (f" flag=V{flag[0]:#x}|{flag[1]:#x}" if flag else ""))
            if not flag: continue
            for other in sorted(members):
                if other[0] == room or other not in first: continue
                checks.append(f"{with_vars(first[other], (), [flag])} never=+{other[1]:X}")
    print(f"# Generated by mods/pe_randomizer/tools/giveruns.py: {len(first)} of {len(gives)} table gives reached.")
    print("#cover: every table spot must be handed out by a seeded line (the run test checks)")
    for seed, difficulty in SEEDS:
        for line in checks: print(f"{line} seed={seed},{difficulty}")


if __name__ == "__main__":
    sys.stdout.reconfigure(newline="\n")
    if len(sys.argv) >= 6 and sys.argv[1] == "cands": cands(*sys.argv[2:])
    elif len(sys.argv) == 5 and sys.argv[1] == "pick": pick(*sys.argv[2:])
    else: sys.exit(__doc__)
