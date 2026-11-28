"""Key logic for one area from a hand-checked map (hospital-map.txt), in keys-by-hand.txt's format.
Usage: python -I hospital_logic.py <map.txt> <pe_rand_spots.h> [keys-by-hand.txt]
Prints one line per spot of the map's rooms (needs = keys to reach it, before = keys it may take; chests
only take keys). With keys-by-hand.txt, then lists where the two differ for those spots.
Needs come from the earliest stage and zone a spot can be reached in. A key may go to a chest reachable,
without that key, in the stage its lock is in: earlier stages are cut off by then. early=breaks keys also
skip chests an earlier stage reaches. `later` keys take any chest reachable in the last stage."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from keyscan import KEYS


def keyset(text): return {int(x, 16) for x in text.split(",") if x}


def load_map(path):
    stages, locks, later, base = [], {}, set(), set()
    for line in open(path):
        f = line.split("#")[0].split()
        if not f: continue
        opts = dict(x.split("=", 1) for x in f[1:] if "=" in x)
        words = [x for x in f[1:] if "=" not in x]
        if f[0] == "stage": stages.append((keyset(opts.get("needs", "")), []))
        elif f[0] == "zone":
            rooms = {int(r[1:]) for r in words[1:]}
            stages[-1][1].append((words[0], rooms, stages[-1][0] | keyset(opts.get("needs", ""))))
        elif f[0] == "lock": locks[int(words[0], 16)] = (int(opts["stage"]) - 1, opts.get("early") == "breaks")
        elif f[0] == "later": later |= keyset(",".join(words))
        elif f[0] == "base": base |= keyset(",".join(words))
        else: sys.exit(f"{path}: {line.strip()!r}")
    for k in set(locks) | later:
        if k not in KEYS: sys.exit(f"{path}: {k:02X} is not a key item")
    return stages, locks, later, base


def load_spots(path):
    """[(number, kind, vanilla, [(map, give)])] from pe_rand_spots.h."""
    out = []
    for m in re.finditer(r"\{ 0x(\w\w), PE_RAND_(\w+),.*?/\* (\d+): ((?:M\d{3}\+\w{6} ?)+)", open(path).read()):
        places = [(int(a), int(b, 16)) for a, b in re.findall(r"M(\d{3})\+(\w{6})", m[4])]
        out.append((int(m[3]), m[2], int(m[1], 16), places))
    return out


def solve(stages, locks, later, base, spots):
    """{spot number: (needs, before)} for spots in the map's rooms."""
    out = {}
    for n, kind, vanilla, places in spots:
        maps = {p for p, _ in places}
        # (stage, zone needs) for each stage/zone the spot is in
        where = [(s, needs) for s, (_, zones) in enumerate(stages) for _, rooms, needs in zones if maps & rooms]
        if not where: continue
        need = min((nd for _, nd in where), key=lambda nd: (len(nd), sorted(nd)))
        before = set()
        if kind == "CHEST":
            first = min(s for s, _ in where)
            for k, (stage, breaks) in locks.items():
                if any(s == stage and k not in nd for s, nd in where) and not (breaks and first < stage):
                    before.add(k)
            if any(s == len(stages) - 1 for s, _ in where): before |= later
        out[n] = (base | need, before)
    return out


def names(keys): return ",".join(f"{k:02X}" for k in sorted(keys))


def main():
    if len(sys.argv) not in (3, 4): sys.exit(__doc__)
    stages, locks, later, base = load_map(sys.argv[1])
    spots = load_spots(sys.argv[2])
    got = solve(stages, locks, later, base, spots)
    for n, kind, vanilla, places in spots:
        if n not in got: continue
        need, before = got[n]
        m, give = places[0]
        tag = KEYS.get(vanilla, "")
        print(f"M{m:03d}+{give:06X} needs={names(need)} before={names(before)}   # {n} {kind.lower()} "
              f"{' '.join(f'M{p:03d}' for p, _ in places)}{' ' + tag if tag else ''}")
    if len(sys.argv) == 4:
        hand = {}
        for line in open(sys.argv[3]):
            f = line.split("#")[0].split()
            if not f: continue
            opts = dict(x.split("=", 1) for x in f[1:])
            m, give = f[0][1:].split("+")
            hand[(int(m), int(give, 16))] = (keyset(opts.get("needs", "")), keyset(opts.get("before", "")))
        print("\n# differences from", os.path.basename(sys.argv[3]))
        for n, kind, vanilla, places in spots:
            if n not in got: continue
            h = next((hand[p] for p in places if p in hand), None)
            if h is None:
                print(f"# {n} M{places[0][0]:03d}: new (no data before)")
            elif h != got[n]:
                print(f"# {n} M{places[0][0]:03d}: needs {names(h[0])} -> {names(got[n][0])}, "
                      f"before {names(h[1]) or '-'} -> {names(got[n][1]) or '-'}")


if __name__ == "__main__":
    main()
