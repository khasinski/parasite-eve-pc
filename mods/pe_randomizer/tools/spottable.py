"""Spot table for the pe_randomizer mod, read from a USA disc and the mod-randomizer-rooms ctest's walk:
python -I spottable.py <disc.bin> <randomizer-rooms.txt> [needs.txt ...] [--windows windows.txt] [--revisit revisit.txt] [--hard hard-reach.txt]
       [--hard-drop hard-drop.txt] > pe_rand_spots.h
needs.txt (keylogic.py, keysolve.py or keys-by-hand.txt) gives the keys each spot needs and the keys it
may take; spots no file lists stay unknown. With several files a spot needs every key any of them
names (the safe side), and takes the keys of the last file that lists it. The table is made with
keys-by-hand.txt keys-walk.txt: the walk's before= (solved with the story gates' rule, a key opens its
lock once held and its own spot reached) replaces the hand table's, and the hand table's needs stay
(the walk reads the St. Francis door as open: M329 sets its flag).
--windows windows.txt (windowruns.py table) adds each spot's late= keys: keys of the same or a later
story day the chest can take, because the walk opened it at every story value where the key's lock needed the key
(counted from the key's first give on: before that, the vanilla game can't ask for it either; a key with
no lock row: every value its own spot gives it at). The spot index is windows.txt's spot=. A needs file's
late= (empty) keeps a spot from them (chest 84: the walk opens it, play needs a touch first). Hospital
chests take no key of their own day this way: a window is when a chest opens, not whether the player can
still walk back to it, and the power-off rooms are one way (hospital-map.txt keeps that by hand).
--revisit revisit.txt (revisitruns.py table) drops a key from a chest's before= and late= when the revisit
ctest saw the chest shut at one of that key's lock-time values (museum 156/157 for the Klamp Key).
--hard hard-reach.txt (hardreach.py) gives each chest's hard= keys: the keys Hard may put there (reached
through doors and the map, Hard's moved doors included, and giving at every lock-time value of the key);
a needs file's late= (empty) keeps a spot from them too.
--hard-drop hard-drop.txt (hardwalk.py table --deny, its FAIL pair rows) takes those keys out of those chests'
hard= keys: hardreach's doors fired there only on flags the player may not have at the key's lock.

A spot is one place that hands out an item; a site is one script word the mod rewrites for it
(a spot in several room versions, or with a roll of up to three items, has several sites). Every site
carries the 16 bytes it expects (step header, mode word, two operands), so the mod only patches
a room whose script is exactly what was scanned here. Sites come from walked steps only (the ctest's
listing): a raw scan of the room bytes also finds words inside data that look like steps.

  chest   item set at room start, given through an actor slot, with a left-behind-item variable
          (full bag: Aya can swap, the chest keeps her item).
  pickup  item set just before the give (no left-behind variable): keys on the floor, rolls of two.
  gift    GiveItem with the item as an immediate, plus its "You got X" popup when it has one and
          a full-bag swap menu (op 169) that names the item itself.

Chests and pickups are one spot when they share a left-behind variable or an opened / taken flag
(a V|bit set next to the give): one chest in several room versions or on several days. A spot over
several days takes the earliest; versions that share only a variable across days stay vanilla (a
reused variable, not one chest).

Gives in an actor the room never places (not a starting actor, no spawn step for its type) are left out:
their scripts never run (dead copies of a chest in later room versions, two NYPD pickups in M120).

Wayne's rooms (M048, M052, M108) give his three chests (one flag each across the rooms) and his gifts
(M16A1 on Day 2, M92F on Day 3); his Junk trades (M048's immediates) stay vanilla. They come after the
Theater keys, so every older spot keeps its number.

Left out (they stay vanilla): the debug room (M360), the Chrysler Building, the shared museum and cruiser routines, the Theater keys (their
doors check a story flag), and spots whose versions hold different items."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dis as D
from keyscan import KEYS

# Save label per map (docs/ROOMS.md, "Map numbers by label"): the area of a spot.
LABELS = [(1, 25, 1), (26, 34, 2), (35, 40, 0), (41, 56, 3), (57, 89, 4), (90, 97, 5), (98, 124, 3),
          (125, 160, 6), (161, 165, 7), (166, 181, 8), (182, 190, 9), (191, 259, 10), (260, 270, 11),
          (271, 271, 12), (272, 273, 13), (274, 287, 14), (319, 332, 6), (333, 333, 11), (334, 334, 2),
          (336, 338, 3), (339, 339, 6), (340, 346, 10), (348, 348, 2), (349, 349, 14), (356, 357, 3),
          (358, 358, 4), (359, 359, 1), (364, 364, 10), (367, 368, 1), (369, 370, 3), (371, 371, 10),
          (372, 373, 1), (374, 375, 4), (376, 376, 3), (377, 378, 1), (379, 379, 10), (380, 381, 6),
          (425, 430, 14), (431, 431, 1), (432, 432, 13), (433, 433, 8)]
AREA_NAMES = ["?", "Theater", "Sewer", "NYPD", "Central Park", "Soho", "Hospital", "Chinatown", "Sewer2",
              "Subway", "Museum", "Warehouse", "Carrier", "Liberty", "Cruiser"]
# Story day by area for maps 288 and up (319-381 repeat earlier places); NYPD is visited on two days.
AREA_DAY = {1: 1, 2: 1, 4: 2, 5: 3, 6: 3, 7: 4, 8: 4, 9: 4, 10: 5, 11: 5, 12: 6, 13: 6, 14: 6}
SKIP_MAPS = {360} | set(range(288, 319)) | set(range(382, 425)) | set(range(434, 438))
GIFT_MAPS = {1, 52, 99, 100, 102, 108, 111, 116, 117, 118, 154}   # character gifts checked by hand (dis.py)
WAYNE_MAPS = {48, 52, 108}
FIXED_KEYS = {0xC8, 0xC9}                                  # Theater: the doors check a story flag
CUT_KEYS = {0xD4, 0xD8, 0xD9, 0xDC}
# The Theater keys move on Hard only: pe_randomizer.c points M012's doors at V0x19 bits the mod sets when Aya
# gets the key. No window or revisit data (their locks test a flag), so their rules are by hand, from doors.txt
# on Day 1 (M012's values 0x28-0x68): M014, M016, M018 and M022 are behind the Dressing Room Key's doors, M319
# and everything after it (M023, the sewer) behind the Rehearsal Room Key's, and Day 1 can't end without both
# (M018 sets 0x54, M023 0x5e). Without either key M005, M009 and M020 stay reachable from M012 at all of them.
DRESSING, REHEARSAL = 0xC8, 0xC9
BEHIND_DRESSING = {14, 16, 18, 22}
THEATER_HARD = {(5, 0x02327C): {DRESSING, REHEARSAL}, (9, 0x004738): {DRESSING, REHEARSAL},
                (20, 0x0137D0): {DRESSING, REHEARSAL}, (14, 0x0128EC): {REHEARSAL}, (16, 0x016884): {REHEARSAL}}

def theater_needs(members):
    """The Theater keys a spot needs: both from M023 / the sewer and every later day, the Dressing Room Key
    behind its doors (any version of the spot will do: the one needing fewest)."""
    def one(m):
        if day(m) > 1 or area(m) == 2 or m in (23, 319): return {DRESSING, REHEARSAL}
        return {DRESSING} if m in BEHIND_DRESSING else set()
    return min((one(x["map"]) for x in members), key=len)

def area(m): return next((a for lo, hi, a in LABELS if lo <= m <= hi), 0)
def day(m):
    if m <= 34: return 1
    if m <= 89: return 2
    if m <= 160: return 3
    if m <= 190: return 4
    if m <= 270: return 5
    if m <= 287: return 6
    return AREA_DAY.get(area(m), 0)

def word(b, i): return struct.unpack_from("<I", b, i)[0]
def is_step(b, i, op, argc): h = word(b, i); return (h & 0x1FFF) == op and ((h >> 13) & 15) == argc
def site(b, i, operand): return (i, b[i:i + 16], operand)   # patch the operand word (0 or 1) at i + 8 + 4 * operand

def task_block(b):
    """Offset of the room's task block in the room data: the listing's offsets count from it."""
    table = word(b, word(b, 4) + 0x14) & 0x3FFFFF
    return word(b, table + 4) & 0xFFFFFF

def read_walk(path):
    """{map: {entity: [(offset, op, [(mode, value)])]}} from the rooms ctest listing, sorted by offset."""
    rooms = {}
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        if f[0] != "step": continue
        args = [(int(m), int(v, 16)) for m, v in (x.split(":") for x in f[5].split(","))] if len(f) > 5 and f[5] else []
        rooms.setdefault(int(f[1][1:]), {}).setdefault(int(f[2]), []).append((int(f[3], 16), int(f[4]), args))
    for entities in rooms.values():
        for steps in entities.values(): steps.sort()
    return rooms

def slot_feeds(steps, g, slot):
    """(item sites, left-behind variables) feeding the slot given at g: the entity's op 10 AssignValue
    steps since its previous give that pass an immediate on to the slot (chests.py's trace, walked steps only)."""
    lo = max([a for a, op, _ in steps if op == 167 and a < g] + [-1])
    assigns = [(a, args) for a, op, args in steps if op == 10 and lo < a < g and len(args) == 2]
    feeds = {(1, slot): "A"}; items = {}
    for _ in range(3):
        for a, ((m0, d0), (m1, d1)) in assigns:
            via = feeds.get((m0, d0))
            if via is None: continue
            if m1 in (1, 2, 4): feeds.setdefault((m1, d1), "V" if m1 == 2 else via)
            elif m1 == 0 and d1 < 0x200 and via == "A": items[a] = d1
    return items, sorted(d for (m, d) in feeds if m == 2)

def slot_flags(steps):
    """{give offset: {(variable, bit)}}: the flags an entity sets (op 9 ORs a bit into a temp, op 10 stores
    it back) within 0x100 bytes of one of its slot gives, owned by the nearest. Chests set their opened
    flag just before the give, pickups their taken flag just after. mod-randomizer-rooms uses the same rule."""
    gives = [a for a, op, args in steps if op == 167 and len(args) == 2 and args[0][0] == 1]
    out = {}
    for (a, op, args), (_, op2, args2) in zip(steps, steps[1:]):
        if op != 9 or len(args) != 4 or args[0] != (0, 2) or args[1][0] != 3 or args[2][0] != 2 or args[3][0] != 0: continue
        if op2 != 10 or args2 != [args[2], args[1]]: continue
        near = [g for g in gives if abs(g - a) <= 0x100]
        if near: out.setdefault(min(near, key=lambda g: abs(g - a)), set()).add((args[2][1], args[3][1]))
    return out

def placed(b, base, entities):
    """Actor types the room ever places: its starting actors (Script_DrawSyncAndFlush: at the task block's
    first word, a count byte, then type and sub id pairs) and the types of its walked spawn steps (op 8,
    op 17 2500). The rooms ctest checks the table by the same rule."""
    start = base + word(b, base)
    out = {b[start + 1 + 2 * i] for i in range(b[start])}
    for steps in entities.values():
        for _, op, args in steps:
            if op == 8 and args and args[0][0] == 0: out.add(args[0][1] & 0xFF)
            if op == 17 and len(args) > 1 and args[0] == (0, 2500) and args[1][0] == 0: out.add(args[1][1] & 0xFF)
    return out

def scan(path, walk):
    chests, gifts = [], []
    for m in range(1, D.ROOMS + 1):
        if m in SKIP_MAPS or m not in walk: continue
        b = D.room(path, m); base = task_block(b)
        walked = {base + a for steps in walk[m].values() for a, _, _ in steps}
        types = placed(b, base, walk[m])
        for entity, steps in walk[m].items():
            if entity not in types:   # its script never runs: a give there hands out nothing
                if any(op == 167 for _, op, _ in steps): print(f"left out (actor never placed): M{m:03d} e{entity}", file=sys.stderr)
                continue
            flags = slot_flags(steps)
            for g, op, args in steps:
                if op != 167 or len(args) != 2: continue
                mode, v = args[0]
                if mode == 1:
                    items, vars_ = slot_feeds(steps, g, v)
                    vals = sorted(set(items.values()))
                    if not 1 <= len(vals) <= 3 or 0 in vals: continue
                    if vars_: kind = "CHEST"
                    elif len(vals) <= 2 and g - min(items) <= 0x40: kind = "PICKUP"
                    else: continue
                    last = max(items)   # a roll: the value set last in script order
                    chests.append(dict(map=m, give=base + g, kind=kind, vars=vars_, flags=flags.get(g, set()),
                                       vanilla=items[last], sites=[site(b, base + i, 1) for i in sorted(items)]))
                elif mode == 0:
                    if v in CUT_KEYS: continue
                    if not (v in KEYS or m in GIFT_MAPS): continue
                    g += base
                    sites = [site(b, g, 0)]
                    # its popup (before the give in M344) and a full-bag swap (op 169) naming the item
                    for p in range(max(0, g - 0x40), min(len(b) - 16, g + 0x100), 4):
                        if p != g and p in walked and word(b, p + 8) == v and (word(b, p) == (232 | 1 << 13) or
                                (p > g and is_step(b, p, 169, 2) and not (word(b, p) >> 17) & 7)):
                            sites.append(site(b, p, 0))
                    if m in WAYNE_MAPS:   # his popup comes after his talk, further on: owned by the give just before it
                        later = [a for a, op2, args2 in steps if op2 == 167 and args2[:1] == [(0, v)] and base + a > g]
                        sites += [site(b, base + a, 0) for a, op2, args2 in steps if op2 == 232 and args2 == [(0, v)]
                                  and base + a > g and not any(x < a for x in later)
                                  and all(base + a != x for x, _, _ in sites)]
                    gifts.append(dict(map=m, give=g, kind="GIFT", vars=[], vanilla=v, sites=sites))
    return chests, gifts

def spots(chests, gifts):
    out = []
    # chests and pickups sharing a left-behind variable or an opened / taken flag are one spot
    parent = list(range(len(chests)))
    def find(x):
        while parent[x] != x: parent[x] = parent[parent[x]]; x = parent[x]
        return x
    first = {}
    for i, c in enumerate(chests):
        for key in [("V", v) for v in c["vars"]] + [("F",) + f for f in c["flags"]]:
            if key in first: parent[find(i)] = find(first[key])
            else: first[key] = i
    groups = {}
    for i, c in enumerate(chests): groups.setdefault(find(i), []).append(c)
    for members in groups.values():
        members.sort(key=lambda c: (day(c["map"]), c["map"], c["give"]))   # the spot's day: the earliest
        days = {day(c["map"]) for c in members}
        rooms = " ".join(f"M{c['map']:03d}+{c['give']:06X}" for c in members)
        if 0 in days or len({c["vanilla"] for c in members}) != 1:
            print(f"left vanilla (unknown day or different items): {rooms}", file=sys.stderr)
        elif len(days) > 1 and not set.intersection(*(c["flags"] for c in members)):
            print(f"left vanilla (a variable over several days, no shared flag): {rooms}", file=sys.stderr)
        else: out.append(members)
    # gifts: one spot per key (it may be given in several rooms or branches), else per room and item
    by_gift = {}
    for c in gifts:
        by_gift.setdefault(c["vanilla"] if c["vanilla"] in KEYS else (c["map"], c["vanilla"]), []).append(c)
    out += by_gift.values()
    # the Theater keys' gifts, then Wayne's spots last, so every other spot keeps its number (hard-reach.txt, windows.txt, ...)
    return sorted(out, key=lambda ms: (ms[0]["map"] in WAYNE_MAPS, ms[0]["vanilla"] in FIXED_KEYS, ms[0]["map"], ms[0]["give"]))

NO_LATE = set()   # (map, give) a needs file marks late= (empty): it takes no key from a later day

def read_needs(path):
    """{(map, give): (needs, before)}, sets of key items, from keylogic.py output."""
    out = {}
    for line in open(path):
        line = line.split("#")[0].strip()
        if not line: continue
        place, *parts = line.split()
        m, give = place[1:].split("+")
        f = dict(x.split("=", 1) for x in parts)
        sets = [{int(x, 16) for x in f.get(name, "").split(",") if x} for name in ("needs", "before")]
        if not (sets[0] | sets[1]) <= set(KEYS): sys.exit(f"{path}: not a key item in {line!r}")
        out[(int(m), int(give, 16))] = tuple(sets)
        if f.get("late") == "": NO_LATE.add((int(m), int(give, 16)))
    return out

def read_windows(path):
    """({spot: story values its gives fire at}, {key item: values where its lock needs it}) from windows.txt."""
    lock_key = {}   # map -> key item, from the story gates table next to the tools
    for line in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "pe_rand_locks.h")):
        f = line.strip().lstrip("{").split(",")
        if line.strip().startswith("{ ") and len(f) > 3: lock_key[int(f[0])] = int(f[2], 16)
    fires, locks = {}, {}
    vals = lambda s: {int(x, 16) for x in s.split(",") if x != "-"}
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        d = dict(x.split("=", 1) for x in f[3:] if "=" in x)
        if f[0] == "give" and d["spot"] != "?":
            fires.setdefault(int(d["spot"]), set()).update(vals(d["fires"]))
        elif f[0] == "lock":
            locks.setdefault(lock_key[int(f[1][1:])], set()).update(vals(d["needs"]))
    return fires, locks

def late_keys(spot_list, needs, windows):
    """{spot: key items}: keys of the same or a later day a chest with key logic data is open for (see the docstring)."""
    fires, locks = windows
    out = {}
    for k, ms in enumerate(spot_list):
        c = ms[0]
        if c["vanilla"] not in KEYS or c["vanilla"] in FIXED_KEYS or not fires.get(k): continue
        first = min(fires[k])
        want = {x for x in locks.get(c["vanilla"], fires[k]) if x >= first}
        for n, members in enumerate(spot_list):
            x = members[0]
            same_day = day(x["map"]) == day(c["map"])
            if (want and x["kind"] == "CHEST" and x["vanilla"] not in KEYS and day(x["map"]) <= day(c["map"])
                    and not (same_day and AREA_NAMES[area(x["map"])] == "Hospital")
                    and any((y["map"], y["give"]) in needs for y in members) and want <= fires.get(n, set())
                    and not any((y["map"], y["give"]) in NO_LATE for y in members)):
                out.setdefault(n, set()).add(c["vanilla"])
    return out

def read_shut(path):
    """{spot: key items} the revisit ctest saw the chest shut for at some lock-time value."""
    name_item = {n: i for i, n in KEYS.items()}
    out = {}
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        if f[0] != "chest": continue
        d = dict(x.split("=", 1) for x in f[3:])
        if d["shut"] != "-": out.setdefault(int(f[1]), set()).add(name_item[d["key"]])
    return out

def read_hard(path):
    """{spot: key items} from hardreach.py's hard rows."""
    out = {}
    for line in open(path):
        f = line.split("#")[0].rstrip().split("\t")
        if f[0] == "hard": out[int(f[1])] = {int(x, 16) for x in f[3].split("=", 1)[1].split(",") if x}
    return out

def read_drop(path):
    """{spot: key items} from hardwalk.py table's FAIL pair rows."""
    name_item = {n: i for i, n in KEYS.items()}
    out = {}
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        if f[0] == "pair" and len(f) > 3 and f[3] == "FAIL": out.setdefault(int(f[1]), set()).add(name_item[f[2]])
    return out

def emit(spot_list, disc, needs=None, windows=None, hard=None, shut=None):
    sites, gives = [], []
    key_order = [ms[0]["vanilla"] for ms in spot_list if ms[0]["vanilla"] in KEYS]
    late = late_keys(spot_list, needs or {}, windows) if windows else {}
    print("/* Generated by mods/pe_randomizer/tools/spottable.py from", os.path.basename(disc).replace("*/", ""), "- do not edit.")
    print(" * Places only: map numbers, script offsets, the bytes expected there and vanilla item ids. */")
    print("#pragma once")
    print('#include "pe_randomizer.h"\n')
    print(f"#define PE_RAND_SPOTS {len(spot_list)}")
    print("static const PeRandSpot g_pe_rand_spots[PE_RAND_SPOTS] = {")
    for n, members in enumerate(spot_list):
        c = members[0]; m = c["map"]; v = c["vanilla"]
        flags = ["PE_RAND_KEY"] if v in KEYS else []
        known = [needs[(x["map"], x["give"])] for x in members if needs and (x["map"], x["give"]) in needs]
        mask, before, lat, hrd = "PE_RAND_NEEDS_UNKNOWN", "0", "0", "0"
        bits = lambda keys: f"0x{sum(1 << key_order.index(k) for k in keys if k in key_order):08X}"
        if not known and v in FIXED_KEYS: known = [(set(), set())]   # the Theater keys' own gifts
        if known:   # any version a player reached with these keys will do: the one needing fewest
            need, bef = min(known, key=lambda nb: len(nb[0]))
            mask, before = bits(need | theater_needs(members)), bits(bef - (shut or {}).get(n, set()))
        if n in late: lat = bits(late[n] - (shut or {}).get(n, set()))
        if hard and n in hard and known and not any((x["map"], x["give"]) in NO_LATE for x in members):
            hrd = bits(hard[n])
        if known and any((x["map"], x["give"]) in THEATER_HARD for x in members):
            hrd = bits((hard or {}).get(n, set()) | set.union(*(THEATER_HARD.get((x["map"], x["give"]), set()) for x in members)))
        rooms = " ".join(f"M{x['map']:03d}+{x['give']:06X}" for x in members)
        print(f"    {{ 0x{v:02X}, PE_RAND_{c['kind']}, {day(m)}, {area(m)}, {' | '.join(flags) or 0}, {mask}, {before}, {lat}, {hrd} }}, "
              f"/* {n}: {rooms} {AREA_NAMES[area(m)]}{' ' + KEYS[v] if v in KEYS else ''} */")
        for x in members:
            for at, raw, operand in x["sites"]: sites.append((x["map"], at, raw, operand, n))
            gives.append((x["map"], x["give"], n))
    print("};\n")
    sites.sort()
    print(f"#define PE_RAND_SITES {len(sites)}")
    print("/* Sorted by map, then offset. */")
    print("static const PeRandSite g_pe_rand_sites[PE_RAND_SITES] = {")
    for m, at, raw, operand, n in sites:
        w = struct.unpack("<4I", raw)
        print(f"    {{ {m}, {n}, 0x{at:06X}, {operand}, {{ 0x{w[0]:08X}, 0x{w[1]:08X}, 0x{w[2]:08X}, 0x{w[3]:08X} }} }},")
    print("};\n")
    gives.sort()
    print(f"#define PE_RAND_GIVES {len(gives)}")
    print("/* Each spot's GiveItem steps (map, offset), sorted: the mod logs which spot gives, for keylogic.py. */")
    print("static const PeRandGive g_pe_rand_gives[PE_RAND_GIVES] = {")
    for m, at, n in gives: print(f"    {{ {m}, {n}, 0x{at:06X} }},")
    print("};")

if __name__ == "__main__":
    if len(sys.argv) < 3: sys.exit(__doc__)
    sys.stdout.reconfigure(newline="\n")
    c, g = scan(sys.argv[1], read_walk(sys.argv[2]))
    needs, windows, hard, shut, args = {}, None, None, None, sys.argv[3:]
    if "--windows" in args:
        i = args.index("--windows")
        windows = read_windows(args[i + 1])
        del args[i:i + 2]
    if "--revisit" in args:
        i = args.index("--revisit")
        shut = read_shut(args[i + 1])
        del args[i:i + 2]
    if "--hard" in args:
        i = args.index("--hard")
        hard = read_hard(args[i + 1])
        del args[i:i + 2]
    if "--hard-drop" in args:
        i = args.index("--hard-drop")
        for n, keys in read_drop(args[i + 1]).items(): hard[n] = hard.get(n, set()) - keys
        del args[i:i + 2]
    for path in args:
        for place, (need, bef) in read_needs(path).items():
            needs[place] = (needs.get(place, (set(), set()))[0] | need, bef)
    emit(spots(c, g), sys.argv[1], needs or None, windows, hard, shut)
