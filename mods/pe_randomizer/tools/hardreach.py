"""Hard's key rule: a key may sit in any chest of its day or earlier that the player can still reach, and
that still gives its item, at every story value where the key's lock asks for it.

  hardreach.py <doors.txt> <map-exits.txt> <randomizer-exits.txt> <windows.txt> <revisit.txt>
               <pe_rand_spots.h> <pe_randomizer.c> > hard-reach.txt

Per key (revisitruns.key_times: its lock-time values and the rooms that stand for the player then), per
value: the rooms reachable from those rooms through doors.txt's doors (only V0x4a / V0x1 guards evaluated,
other story flags read as open; M367's story router and the `V0x0 & 0x4` debug warps left out) and the
New York map's stops at that value; from Day 2 on, Hard's moved doors (pe_randomizer.c s_doors) lead
where the mod sends them. A chest counts when one of its rooms is reached, a door from it leads back to
the map or a lock room, and the revisit ctest (revisitruns.py table) saw it give at that value. Values
where the map sends Aya to one stop (FORCED) are skipped: they close areas only for that moment, and no
key's lock asks last at one. spottable.py --hard reads the hard rows; behind-its-own-door and needs are
the generator's (reachable with the keys placed so far).

Run in build-mods/mods with python -I; doors.txt and map-exits.txt come from storygraph.py's listing,
randomizer-exits.txt from the mod-randomizer-exits ctest."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from keyscan import KEYS
from revisitruns import read_spots, read_windows, key_times
from spottable import AREA_NAMES

FORCED = {0xc0, 0xd0, 0xd8, 0xe0, 0xe4, 0x148, 0x160, 0x178, 0x180, 0x1c0, 0x1c8, 0x208, 0x210}
DAY2 = 0x88   # pe_randomizer.c PE_RAND_DAY2: the moved doors only from here on
# Doors whose story-flag guard is still unset whenever these keys' locks ask (by hand, hospital-map.txt):
# the hospital's power-cut shortcut M320 -> M329 needs V0x42 & 0x20, set in M329/M339, past both locks.
SHUT = {("M320", "000C10"): {"St. Francis Hospital Key", "Blue Magnetic Cardkey"}}
COND = re.compile(r"^(V0x4a|V0x1)\s*(==|!=|>=|<=|<|>)\s*(0x[0-9a-f]+|\d+)$")
OPS = {"==": lambda a, b: a == b, "!=": lambda a, b: a != b, ">=": lambda a, b: a >= b,
       "<=": lambda a, b: a <= b, "<": lambda a, b: a < b, ">": lambda a, b: a > b}


def read_doors(path):
    """room -> [(target, ways, offset)]; ways: alternatives, each a list of conditions that must all hold."""
    doors = {}
    for l in open(path):
        f = l.rstrip("\n").split("\t")
        m = re.match(r"(M\d+) \+(\w+) e(\d+) -> (M\d+)", f[0])
        if not m or m.group(4) == "M367": continue
        ways = [[c.strip()[1:-1] if c.strip().startswith("(") else c.strip() for c in w.split(" ; ")]
                for w in (f[1].split(" | ") if len(f) > 1 and f[1].strip() else [""])]
        doors.setdefault(m.group(1), []).append((m.group(4), ways, m.group(2)))
    return doors


def moved_doors(path):
    """(room, offset) -> new target room, from s_doors."""
    out = {}
    for m in re.finditer(r'\{(\d+), 0x(\w+), \d+, (\d+), 0x\w+(?:, "[^"]*")?\}', open(path).read()):
        out[(f"M{int(m.group(1)):03d}", f"{int(m.group(2), 16):06X}")] = f"M{int(m.group(3)):03d}"
    if not out: sys.exit(f"{path}: no s_doors rows (has the PeRandDoor row format changed?)")
    return out


STORY_FLAGS = re.compile(r"V0x(4[1-3]|2[c-f]|3[67]|18)(?![0-9a-f])")
PESS = os.environ.get("PESS")   # PESS=1: doors guarded by those story flags stay shut


def way_ok(way, x, came):
    if "V0x0 & 0x4" in way: return False
    if PESS and any(STORY_FLAGS.match(c.lstrip("(")) for c in way): return False
    for c in way:
        m = COND.match(c)
        val = None if not m else x if m.group(1) == "V0x4a" else came
        if val is not None and not OPS[m.group(2)](val, int(m.group(3), 0)): return False
    return True


def read_map(map_exits, exits_path):
    """x -> {room: stop}: the map's stops at story value x (M024's rows: every room shares the ladder)."""
    bands, exits, modes = [], [], []
    for l in open(map_exits):
        f = l.rstrip("\n").split("\t")
        if not f[0].startswith("M024 +001474"): continue
        m = re.search(r"V0x4a=0x(\w+)(?:-0x(\w+))? night=(\d)", f[1])
        ds = [int(d) for d in re.findall(r"d(\d)(?: if [^-]*)?->", f[2]) if d != "5"]
        bands.append((int(m.group(1), 16), int(m.group(2) or m.group(1), 16), int(m.group(3)), ds))
    for l in open(exits_path):
        if l.startswith("exit"):
            m = re.search(r"dest=(\d)\tnight=(\d)\tV0x4a=0x(\w+)-0x(\w+)\t-> (M\d+)", l)
            exits.append((int(m.group(1)), int(m.group(2)), int(m.group(3), 16), int(m.group(4), 16), m.group(5)))
        elif l.startswith("mode"):
            m = re.search(r"0x(\w+)-0x(\w+)\t(\w+)=(\d)", l)
            modes.append((int(m.group(1), 16), int(m.group(2), 16), m.group(3), int(m.group(4))))

    def stops(x):
        band = next((b for b in bands if b[0] <= x <= b[1]), None)
        if not band: return {}
        ds = band[3]
        for lo, hi, kind, d in modes:
            if lo <= x <= hi and kind == "auto": ds = [d]
        return {room: d for d in ds for dd, n, lo, hi, room in exits if dd == d and n == band[2] and lo <= x <= hi}
    return stops


def main(argv):
    if len(argv) != 8: sys.exit(__doc__)
    doors, stops = read_doors(argv[1]), read_map(argv[2], argv[3])
    gives, fires, locks = read_windows(argv[4])
    spots = read_spots(argv[6])
    moved = moved_doors(argv[7])
    kt = key_times(spots, gives, fires, locks)
    gave = {}   # (spot, key item) -> values the revisit ctest saw the chest give at
    name_item = {n: i for i, n in KEYS.items()}
    for l in open(argv[5]):
        f = l.rstrip("\n").split("\t")
        if f[0] == "chest":
            d = dict(x.split("=", 1) for x in f[3:])
            gave[(int(f[1]), name_item[d["key"]])] = {int(x, 16) for x in d["open"].split(",") if x != "-"}

    def room_doors(room, x, shut):
        for tgt, ways, off in doors.get(room, ()):
            if (room, off) in shut: continue
            yield (moved.get((room, off), tgt) if x >= DAY2 else tgt), ways

    def reach(x, starts, shut):
        seen, todo = set(starts), [(r, None) for r in starts]
        while todo:
            room, came = todo.pop()
            for tgt, ways in room_doors(room, x, shut):
                if not any(way_ok(w, x, came) for w in ways): continue
                for r in (stops(x) if tgt == "M000" else (tgt,)):
                    if r not in seen: seen.add(r); todo.append((r, int(room[1:])))
        return seen

    def back(x, room, starts, shut):
        if room in starts: return True
        seen, todo = {room}, [(room, None)]
        while todo:
            r, came = todo.pop()
            for tgt, ways in room_doors(r, x, shut):
                if not any(way_ok(w, x, came) for w in ways): continue
                if tgt == "M000" or tgt in starts: return True
                if tgt not in seen: seen.add(tgt); todo.append((tgt, int(r[1:])))
        return False

    chest_rooms = {n: sorted({r for r, _ in gives.get(n, ())}) for n, s in spots.items() if s["kind"] == "CHEST"}
    allowed, reach_cache = {}, {}
    for item, (k, when) in sorted(kt.items()):
        kday = spots[k]["day"]
        xs = sorted(x for x in when if x not in FORCED)
        shut = frozenset(d for d, names in SHUT.items() if KEYS[item] in names)
        if not xs: continue
        for n, s in spots.items():
            if s["kind"] != "CHEST" or s["key"] or not s["day"] or s["day"] > kday: continue
            ok = True
            for x in xs:
                ck = (x, tuple(sorted(when[x])), shut)
                if ck not in reach_cache: reach_cache[ck] = reach(x, when[x], shut)
                if x not in gave.get((n, item), ()) or not any(
                        r in reach_cache[ck] and back(x, r, when[x], shut) for r in chest_rooms.get(n, ())):
                    ok = False; break
            if ok: allowed.setdefault(n, set()).add(item)
    print("# Generated by mods/pe_randomizer/tools/hardreach.py: keys Hard may put in each chest (reachable and")
    print("# giving at every lock-time story value of the key, forced map moments skipped, Hard's moved doors from Day 2).")
    for n in sorted(allowed):
        s = spots[n]
        print(f"hard\t{n}\tDay {s['day']} {AREA_NAMES[s['area']]}\tkeys={','.join(f'{i:X}' for i in sorted(allowed[n]))}"
              f"\t# {', '.join(KEYS[i] for i in sorted(allowed[n]))}")
    print(f"# summary: {sum(map(len, allowed.values()))} key x chest pairs over {len(allowed)} chests, {len(kt)} keys")


if __name__ == "__main__":
    sys.stdout.reconfigure(newline="\n")
    main(sys.argv)
