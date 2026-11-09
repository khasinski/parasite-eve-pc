"""Enemy census for the battle randomizer, from the mod-randomizer-rooms ctest's step listing:
python -I enemyscan.py <randomizer-rooms.txt> <pe_collection_data.h> [--disc disc.cue|.bin] [--out enemies.txt] [--check]
(the disc defaults to PE_TEST_DISC; it is read for each room's starting actors only.)

An enemy actor is an entity whose script runs op 111 (Script_InitEntityMoveState -> Entity_AllocSlot: it
takes one of the 7 battle enemy slots). Its numbers are op 90 (Entity_SetField) writes; on an enemy the codes
go through Entity_WriteFieldByCmd into its EnemyCombatant (battle/context.c, include/pe1/battle.h): 0x29/0x2a
= low/high byte of fieldId04, the creature id the bestiary keys on with the room (pe_collection_data.h
g_pe_creature_keys). Op 206 (Entity_SetFieldByTypeId) writes the same codes from another entity.

One line per enemy actor and creature id it sets:
  room (place, day), entity, script range, creature id(s) -> creature name(s) (* = boss),
  stats (op 90 code=value, named; A/V/T/N = entity field, story variable, temp, script global),
  model (the bestiary's bank slot for that creature in this room, when it has one: the slot is the entity),
  group (per spawning entity: this actor's count in each formation, a run of back-to-back op 8 / op 17 2500
  spawns, and the fight's size; "never placed" = no step places it, a dead copy or placed some other way),
  fight (op 85 sites in the spawning entities, else the room's; each with the ways to reach it, doorscan),
  kind (boss / random (op 26 in the fight's entity) / scripted; story-gated when a way tests V0x4a),
  after (story variables and doors the fight's routine reaches after op 85, forward only: what a won battle sets),
  pm (PM commands the actor starts with op 106, Script_GetEntityEffect -> Scene_LoadRoomAssets: 0-7 and
  85-114 are global code, 8-84 and 115+ come from the room's script directory and die with the room).
An actor with no creature that only creatures spawn shows as their part.
--blocks pe_rand_enemies.h: each enemy actor's stat block for the battle randomizer (STAT_CODES, the first op 90
write of each in the actor's own script: a constant, or (N0xa * k) >> 16, the difficulty scale; any other
operand, such as a rolled drop, is left out and the actor keeps its own), and its attack power (attack_power).
--check: every creature key is found as some actor's id in its room, and every enemy actor maps to a
creature (or has a reason in UNMAPPED). Exit 1 on a failure, 77 when an input is missing."""
import os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import doorscan as DS

INIT, SET, SET_BY_TYPE, FIGHT, SPAWN, RANDOM = 111, 90, 206, 85, 8, 26
# Steps that end a frame for the routine (a wait, a kill, the node freed, another slot): the setup ends there.
YIELD = {1, 2, 4, 22, 32, 34, 48, 111}
FIELD = {0x28: "atbLimit", 0x29: "id.lo", 0x2a: "id.hi", 0x2b: "atb", 0x2c: "hp", 0x2d: "prevHp",
         0x2e: "e06.hi", 0x2f: "e06.lo", 0x3c: "maxHp", 0x3d: "defense", 0x3e: "chargeStep", 0x3f: "effChance",
         0x40: "deathFrame", 0x41: "contactDmg", 0x42: "contactKnock", 0x43: "effLevel", 0x44: "effTime",
         0x45: "dot", 0x4a: "flag10", 0x50: "behavior", 0x51: "flag100000", 0x5a: "exp", 0x5b: "reward",
         0x5c: "dropSlot", 0x5d: "steal", 0x5e: "deathAsset", 0x5f: "deathKeep", 0x60: "resume",
         0x32: "atkAsset", 0x33: "eventAsset", 0x34: "deathAssetId", 0x35: "touchAsset0", 0x36: "touchAsset1",
         0x37: "touchAsset2", 0x78: "panelVal", 0x79: "panelTime", 0x7a: "panelMode", 0x7b: "loot", 0x7c: "lootAux"}
FIELD.update({c: f"status{c - 0x64}" for c in range(0x64, 0x6f)})
# Enemy actors that set no id the bestiary keys on, and why (checked by hand in the listing).
UNMAPPED = {(159, 1): "Hospital room with the Snake's id 0x0016 (Chinatown's); the bestiary has no M159 key",
            (159, 2): "its partner (same id; nothing spawns it)"}
# The stat block one creature's actor hands another (pe_rand_battle.c): numbers, not the model, AI or effects.
STAT_CODES = [0x2c, 0x2d, 0x3c, 0x3d, 0x28, 0x3e, 0x3f, 0x41, 0x42, 0x43, 0x44, 0x45, 0x5a, 0x5b, 0x5c, 0x5d] + list(range(0x64, 0x6e))
AREA = ["?", "Theater", "Sewer", "NYPD", "Central Park", "Soho", "Hospital", "Chinatown", "Sewer", "Subway",
        "Museum", "Warehouse", "Carrier", "Statue of Liberty", "Cruiser"]


def place(m):
    import spottable as SP
    if SP.area(m) == 0 and (288 <= m <= 318 or 382 <= m <= 424 or 434 <= m <= 437): return "Chrysler Building, post-game"
    if m == 350: return "Cruiser (text bank only), Day 6?"
    return f"{AREA[SP.area(m)] if SP.area(m) < len(AREA) else '?'}, Day {SP.day(m) or '?'}"


def collection(path):
    """(creatures [(name, boss)], keys {(map, id): [creature]}, models {(creature, map): [slot]})"""
    text, blocks = open(path).read(), {}
    for name in ("g_pe_creatures", "g_pe_creature_keys", "g_pe_creature_models"):
        m = re.search(name + r"\[[A-Z_]+\] = \{(.*?)\n\};", text, re.S)
        if not m: raise ValueError(f"{path}: no {name} table")
        blocks[name] = m.group(1)
    creatures = [(n, int(b)) for n, b in re.findall(r'\{ "([^"]*)", \d+, (\d+),', blocks["g_pe_creatures"])]
    keys, models = {}, {}
    # { map, id, creature } or, with the model bank slot, { map, id, actor, creature }
    for mp, i, c in re.findall(r"\{ (\d+), (0x[0-9A-Fa-f]+), (?:\d+, )?(\d+) \}", blocks["g_pe_creature_keys"]):
        found = keys.setdefault((int(mp), int(i, 16)), [])
        if int(c) not in found: found.append(int(c))
    for c, mp, a in re.findall(r"\{ (\d+), (\d+), (\d+) \}", blocks["g_pe_creature_models"]):
        models.setdefault((int(c), int(mp)), []).append(int(a))
    return creatures, keys, models


def bin_path(path):
    """The data track of a .cue (its first FILE), else the path itself."""
    if not path.lower().endswith(".cue"): return path
    for line in open(path, errors="replace"):
        m = re.match(r'\s*FILE\s+"([^"]+)"', line)
        if m: return os.path.join(os.path.dirname(path), m.group(1))
    raise ValueError(f"{path}: no FILE line")


def starting_actors(disc, m):
    """[(type, sub id)] the room places at start (spottable.placed's rule), [] without a disc."""
    if not disc: return []
    import dis as D, spottable as SP
    b = D.room(disc, m)
    if not b: return []
    base = SP.task_block(b); start = base + SP.word(b, base)
    return [(b[start + 1 + 2 * i], b[start + 2 + 2 * i]) for i in range(b[start])]


def rooms_meta(path):
    """{room: (entries, code_end)} from the listing's room lines."""
    out = {}
    for line in open(path):
        if not line.startswith("room\t"): continue
        f = line.rstrip("\n").split("\t")
        end = next((int(x.split("=")[1], 16) for x in f if x.startswith("code_end=")), 0)
        out[int(f[1][1:])] = ([int(x, 16) for x in f[6].split(",")] if len(f) > 6 else [], end)
    return out


def script_range(entries, end, t):
    start = entries[t]
    later = [e for e in entries if e > start]
    return start, min(later) if later else end


def stat_block(steps, succ, t):
    """{code: (scaled, value)}: the setup's value of each STAT_CODES code in entity t's script. The setup is the
    routine from its first op 111 up to the steps that wait (YIELD) or end it: one frame, which may set a code
    twice (M013's scripted Rat: a scaled HP, then a flat 17) or on some branches only (M207: 0x6d by rank).
    A code is in the block when every path through the setup leaves it the same value: a constant (0, v) or
    a temp the two steps before made (N0xa * k) >> 16 (1, k)."""
    mine = [(at, op, a) for at, (e, op, a, _) in sorted(steps.items()) if e == t]
    start = next((at for at, op, _ in mine if op == INIT), None)
    if start is None: return {}
    order = {at: i for i, (at, _, _) in enumerate(mine)}
    written = {}
    for i, (at, op, a) in enumerate(mine):
        if op != SET or len(a) != 2 or a[0][0] != 0 or a[0][1] not in STAT_CODES: continue
        mode, v = a[1]
        val = "?"
        if mode == 0: val = (0, v & 0xFFFFFFFF)
        elif mode == 3 and i >= 2:
            (_, op1, a1), (_, op2, a2) = mine[i - 2], mine[i - 1]
            if (op1 == 9 and op2 == 9 and len(a1) == 4 and len(a2) == 4 and a2[0] == (0, 0x12) and a2[1] == (3, v)
                    and a2[3] == (0, 0x10) and a1[0] == (0, 0xf) and a1[1] == a2[2] and a1[2] == (4, 0xa) and a1[3][0] == 0):
                val = (1, a1[3][1])
        written[at] = (a[0][1], val)
    # each step's possible values of every code on the way in (None: not set yet), to a fixpoint
    into, todo = {start: {c: {None} for c in STAT_CODES}}, [start]
    final = {c: set() for c in STAT_CODES}
    while todo:
        at = todo.pop()
        out = {c: set(s) for c, s in into[at].items()}
        if at in written: out[written[at][0]] = {written[at][1]}
        nxt = [y for y in succ.get(at, ()) if y in order]
        if (at != start and steps[at][1] in YIELD) or not nxt:
            for c in STAT_CODES: final[c] |= into[at][c] if at != start and steps[at][1] in YIELD else out[c]
            continue
        for y in nxt:
            cur = into.setdefault(y, {c: set() for c in STAT_CODES})
            if any(not out[c] <= cur[c] for c in STAT_CODES):
                for c in STAT_CODES: cur[c] |= out[c]
                todo.append(y)
    return {c: next(iter(s)) for c, s in final.items() if len(s) == 1 and None not in s and "?" not in s}


def attack_power(steps, t):
    """(scaled, value): the strongest attack power entity t's script sets (op 112/183, operand 4), compared at
    scale 0x10000: a constant (0, v), (N0xa * k) >> 16 (1, k), or a Chrysler base + N4 * 2 (0, base). None when
    no power reads that way (Blackbirds pass it as an argument)."""
    mine = [(op, a) for _, (e, op, a, _) in sorted(steps.items()) if e == t]
    best = None
    for i, (op, a) in enumerate(mine):
        if op not in (112, 183) or len(a) < 5: continue
        mode, v = a[4]
        val = None
        if mode == 0: val = (0, v & 0xFFFF)
        elif mode == 3 and i >= 2:
            (op1, a1), (op2, a2) = mine[i - 2], mine[i - 1]
            if op1 == 9 and op2 == 9 and len(a1) == 4 and len(a2) == 4 and a1[0] == (0, 0xf) and a1[1][0] == 3:
                if a2[0] == (0, 0x12) and a2[1] == (3, v) and a2[2] == a1[1] and a2[3] == (0, 0x10) \
                        and a1[2] == (4, 0xa) and a1[3][0] == 0:
                    val = (1, a1[3][1])
                elif a2[0] == (0, 0) and a2[1] == (3, v) and a2[2][0] == 0 and a2[3] == a1[1] \
                        and a1[2] == (2, 4) and a1[3] == (0, 2):
                    val = (0, a2[2][1])
        if val and (best is None or val[1] > best[1]): best = val
    return best


def write_blocks(rows, rooms, path):
    """pe_rand_enemies.h: one row per enemy actor that maps to a creature, by room and entity."""
    NL = chr(10)
    head = ["/* Generated by tools/enemyscan.py --blocks from the rooms listing: each enemy actor's stat block",
            " * (pe_rand_battle.c). have: bit i = PE_RAND_STAT_CODES[i] is set; scaled: bit i's value is k in",
            " * (N0xa * k) >> 16 (the room's difficulty scale), else the value itself. Do not edit. */",
            "#ifndef PE_RAND_ENEMIES_H", "#define PE_RAND_ENEMIES_H", "", '#include "pe_randomizer.h"', "",
            f"static const uint8_t PE_RAND_STAT_CODES[PE_RAND_STATS] = {{ {', '.join(f'{c:#04x}' for c in STAT_CODES)} }};", ""]
    out, powers, succs = [], [], {}
    def succ_of(m):
        if m not in succs:
            succs[m] = {}
            for x, y, _ in DS.graph(*rooms[m])[0]: succs[m].setdefault(x, []).append(y)
        return succs[m]
    for r in rows:
        if not r["creatures"] or not r["alloc"]: continue
        block = stat_block(rooms[r["room"]][1], succ_of(r["room"]), r["entity"])
        have = sum(1 << i for i, c in enumerate(STAT_CODES) if c in block)
        scaled = sum(1 << i for i, c in enumerate(STAT_CODES) if c in block and block[c][0])
        vals = ", ".join(str(block[c][1] if block[c][1] < 0x80000000 else block[c][1] - (1 << 32)) if c in block else "0"
                         for c in STAT_CODES)
        out.append(f"    {{ {r['room']}, {r['entity']}, {r['creatures'][0]}, {have:#09x}, {scaled:#09x}, {{ {vals} }} }},"
                   f" /* {r['who']} */")
        power = attack_power(rooms[r["room"]][1], r["entity"]) or (0, 0)
        powers.append(f"    {{ {power[1]}, {power[0]} }}, /* M{r['room']:03d} e{r['entity']} {r['who']} */")
    lines = head + [f"#define PE_RAND_ENEMIES {len(out)}", "static const PeRandEnemy g_pe_rand_enemies[PE_RAND_ENEMIES] = {"] + \
        out + ["};", "", "/* Row i: g_pe_rand_enemies[i]'s attack power { value, scaled } (enemyscan.py attack_power); 0: unknown. */",
               "static const uint16_t g_pe_rand_enemy_power[PE_RAND_ENEMIES][2] = {"] + powers + ["};", "", "#endif"]
    with open(path, "w", newline=NL) as f: f.write(NL.join(lines) + NL)
    return len(out)


def ids_of(steps, t):
    """Creature ids the entity can set: every rank (low byte) with every kind (high byte) it writes, from
    its own op 90s and other entities' op 206s at its type; a byte never written stays 0 (the slot starts
    cleared). A rank written from a temp after op 89 read the rank back is the rank plus a difficulty bonus,
    (N0xa * k) >> 16 (the op 9 multiply before it): ranks base..base+k. Other variable bytes show as
    their operand."""
    lows, highs, bonus = [], [], 0
    last_mult = 0
    reads_rank = any(e == t and op == 89 and a and a[0] == (0, 0x29) for e, op, a, _ in steps.values())
    for at in sorted(steps):
        e, op, a, _ = steps[at]
        if e == t and op == 9 and len(a) == 4 and a[0] == (0, 0xf) and (4, 0xa) in a[2:] and a[3][0] == 0:
            last_mult = a[3][1]
        if op == SET and e == t and len(a) == 2 and a[0][0] == 0: code, val = a[0][1], a[1]
        elif op == SET_BY_TYPE and len(a) == 4 and a[0] == (0, t) and a[2][0] == 0: code, val = a[2][1], a[3]
        else: continue
        if code == 0x29:
            if val[0] == 0: lows.append(val[1] & 0xFF)
            elif reads_rank: bonus = max(bonus, last_mult)
            else: lows.append(DS.operand(*val))
        elif code == 0x2a: highs.append(val[1] & 0xFF if val[0] == 0 else DS.operand(*val))
    lows = list(dict.fromkeys(lows or [0]))
    ranks = list(dict.fromkeys(r + k for r in lows if isinstance(r, int) for k in range(bonus + 1)))
    ranks += [r for r in lows if not isinstance(r, int)]
    out = []
    for h in dict.fromkeys(highs or [0]):
        for r in ranks:
            out.append(r | h << 8 if isinstance(r, int) and isinstance(h, int) else f"{h}:{r}")
    if not highs and not any(isinstance(r, int) and r for r in lows) and not bonus: return []
    return out


def census(rooms, meta, collection_data, disc):
    creatures, keys, models = collection_data
    out = []
    for m in sorted(rooms):
        entries, steps = rooms[m]
        allocs = {e for e, op, _, _ in steps.values() if op == INIT}
        writers = {e for e, op, a, _ in steps.values() if op == SET and len(a) == 2 and a[0] in ((0, 0x29), (0, 0x2a))}
        enemies = sorted(allocs | writers)
        if not enemies: continue
        room = None
        start = starting_actors(disc, m)
        forms = formations(steps)
        fights = sorted(at for at, s in steps.items() if s[1] == FIGHT)
        for t in enemies:
            lo, hi = script_range(entries, meta[m][1], t)
            stats = [(at, a) for at, (e, op, a, _) in sorted(steps.items()) if e == t and op == SET and len(a) == 2]
            spawns = [(at, e, a) for at, (e, op, a, _) in sorted(steps.items())
                      if op == SPAWN and a and a[0] == (0, t)] + \
                     [(at, e, a[1:]) for at, (e, op, a, _) in sorted(steps.items())
                      if op == 17 and len(a) > 1 and a[0] == (0, 2500) and a[1] == (0, t)]
            spawners = sorted({e for _, e, _ in spawns})
            placed = [s for s in start if s[0] == t]
            mine = [at for at in fights if steps[at][0] in spawners] or fights
            if mine and room is None:
                room = DS.Room(entries, steps)
                succ = {}
                for x, y, _ in DS.graph(entries, steps)[0]: succ.setdefault(x, []).append(y)
            ways = {at: DS.show(room.ways(at)) for at in mine}
            fighters = sorted({steps[at][0] for at in mine})
            randomized = any(op == RANDOM and e in fighters for e, op, _, _ in steps.values())
            after = []
            for at in mine:
                for x in after_fight(steps, entries, succ, at):
                    e, op, a, target = steps[x]
                    if op in (10, 41) and a and a[0][0] == 2: after.append(f"V{a[0][1]:#x}")
                    if op == 49: after.append(f"door M{target:03d}" if target is not None else "door")
            pm = sorted({a[0][1] for e, op, a, _ in steps.values() if e == t and op == 106 and a and a[0][0] == 0})
            ids = ids_of(steps, t)
            names = sorted({c for cid in ids if isinstance(cid, int) for c in keys.get((m, cid), [])})
            model = sorted({s for c in names for s in models.get((c, m), [])})
            boss = any(creatures[c][1] for c in names if c < len(creatures))
            out.append(dict(
                room=m, entity=t, lo=lo, hi=hi, ids=ids, creatures=names, alloc=t in allocs,
                who=", ".join(creatures[c][0] + ("*" if creatures[c][1] else "") for c in names if c < len(creatures)),
                stats=", ".join(f"{FIELD.get(a[0][1], hex(a[0][1]))}={DS.operand(*a[1])}" for _, a in stats
                                if a[0][0] == 0),
                model=("slot " + "/".join(map(str, model)) + (" ok" if model == [t] else " (entity differs)"))
                if model else "no form here",
                group=" ".join([f"start x{len(placed)}"] * bool(placed) + [group(forms, s, t) for s in spawners])
                      or "never placed",
                fight="; ".join(f"e{steps[at][0]} +{at:06X} if {ways[at]}" for at in mine) or "no op 85 in room",
                spawners=spawners, story="V0x4a" in " ".join(ways.values()),
                kind="no op 111" if t not in allocs else "boss" if boss else "random" if randomized else "scripted" if mine else "none",
                after=", ".join(dict.fromkeys(after)) or "-",
                pm=", ".join(f"{c}{'g' if c < 8 or 85 <= c <= 114 else 'r'}" for c in pm) or "-"))
    by = {(r["room"], r["entity"]): r for r in out}
    for r in out:   # an actor with no creature that only creatures spawn is a part or summon of them
        parents = [by.get((r["room"], s)) for s in r["spawners"]]
        if not r["creatures"] and parents and all(p and p["creatures"] for p in parents):
            r["parent"] = " + ".join(f"e{p['entity']} {p['who']}" for p in parents)
    return out


def after_fight(steps, entries, succ, site, limit=400):
    """Steps the fight's routine runs after op 85: forward branches and jumps only (a jump back is the
    encounter loop starting over), plus the children it starts there (op 18)."""
    seen, todo = set(), [site]
    while todo and len(seen) < limit:
        x = todo.pop()
        if x in seen: continue
        seen.add(x)
        t, op, a, _ = steps[x]
        if op == 18 and a and a[0][0] == 0 and entries[t] + a[0][1] * 2 in steps: todo.append(entries[t] + a[0][1] * 2)
        todo += [y for y in succ.get(x, ()) if y > x]
    return sorted(seen - {site})


def formations(steps):
    """{spawning entity: [{type: count}]}: each run of back-to-back spawn steps (op 8, op 17 2500) is
    one formation, the actors one fight places."""
    out, prev = {}, None
    for at in sorted(steps):
        e, op, a, _ = steps[at]
        if op == SPAWN and a and a[0][0] == 0: t = a[0][1]
        elif op == 17 and len(a) > 1 and a[0] == (0, 2500) and a[1][0] == 0: t = a[1][1]
        else: continue
        if prev is None or prev[0] != e or prev[1] != at: out.setdefault(e, []).append({})
        run = out[e][-1]; run[t] = run.get(t, 0) + 1
        prev = (e, at + DS.size(a))
    return out


def group(forms, spawner, t):
    """This actor's count per formation and the fight sizes, in the spawner's formations that have it."""
    runs = [r for r in forms.get(spawner, []) if t in r]
    def span(xs): return f"{min(xs)}" if min(xs) == max(xs) else f"{min(xs)}-{max(xs)}"
    return (f"e{spawner}: x{span([r[t] for r in runs])} in {len(runs)} formation{'s' * (len(runs) != 1)}, "
            f"fight of {span([sum(r.values()) for r in runs])}")


def show_id(cid): return f"{cid:#06x}" if isinstance(cid, int) else str(cid)
def show_ids(ids): return "/".join(map(show_id, ids)) or "none"


def write(rows, f):
    print("# room\tplace\tentity\tscript\tid\tcreature (* boss)\tkind\tgroup\tmodel\tfight\tafter fight\tpm (g global, r room)\tstats", file=f)
    for r in rows:
        who = r["who"] or (f"part of {r['parent']}" if r.get("parent") else UNMAPPED.get((r["room"], r["entity"]), "?"))
        print(f"M{r['room']:03d}\t{place(r['room'])}\te{r['entity']}\t+{r['lo']:06X}-{r['hi']:06X}\t{show_ids(r['ids'])}\t"
              f"{who}\t{r['kind'] + (', story-gated' if r['story'] else '')}\t{r['group']}\t{r['model']}\t{r['fight']}\t{r['after']}\t{r['pm']}\t{r['stats']}", file=f)


# Keys the bestiary has that no room script sets: M002's Stage Eve gets hers when the battle is set up
# (pe_collection_data.h says so), so the census can't see her.
UNSCRIPTED_KEYS = {(2, 0x0201)}


def check(rows, collection_data):
    creatures, keys, _ = collection_data
    bad, found = [], {(r["room"], i) for r in rows for i in r["ids"]}
    for (m, cid), cs in sorted(keys.items()):
        if (m, cid) not in found and (m, cid) not in UNSCRIPTED_KEYS:
            bad.append(f"key M{m:03d} {cid:#06x} ({', '.join(creatures[c][0] for c in cs)}) set by no enemy actor")
    for r in rows:
        if r["creatures"] or r.get("parent") or r["group"] == "never placed" or (r["room"], r["entity"]) in UNMAPPED: continue
        bad.append(f"M{r['room']:03d} e{r['entity']} id {show_ids(r['ids'])} maps to no creature")
    named = {c for cs in keys.values() for c in cs}
    unkeyed = [creatures[c][0] for c in range(len(creatures)) if c not in named]
    return bad, unkeyed


if __name__ == "__main__":
    args = sys.argv[1:]
    def opt(name, default=None):
        if name in args: i = args.index(name); v = args[i + 1]; del args[i:i + 2]; return v
        return default
    disc, out_path, blocks_path = opt("--disc", os.environ.get("PE_TEST_DISC")), opt("--out"), opt("--blocks")
    do_check = "--check" in args
    args = [a for a in args if a != "--check"]
    if len(args) != 2: sys.exit(__doc__)
    if not all(os.path.exists(p) for p in args):
        print("enemyscan: missing " + ", ".join(p for p in args if not os.path.exists(p)) + " (run mod-randomizer-rooms first)")
        sys.exit(77)
    disc = bin_path(disc) if disc else None
    if disc and not os.path.exists(disc): disc = None
    data = collection(args[1])
    rooms_steps = DS.load(args[0])
    rows = census(rooms_steps, rooms_meta(args[0]), data, disc)
    if blocks_path:
        print(f"enemyscan: {write_blocks(rows, rooms_steps, blocks_path)} stat blocks", file=sys.stderr)
    with (open(out_path, "w") if out_path else sys.stdout) as f: write(rows, f)
    rooms = {r["room"] for r in rows}
    kinds = {k: sum(1 for r in rows if r["kind"] == k) for k in ("boss", "random", "scripted", "none")}
    print(f"enemyscan: {len(rows)} enemy actor lines in {len(rooms)} rooms ({', '.join(f'{v} {k}' for k, v in kinds.items())})"
          + ("" if disc else "; no disc: starting placements not read"), file=sys.stderr)
    if do_check:
        bad, unkeyed = check(rows, data)
        for b in bad: print("FAIL " + b)
        if unkeyed: print("creatures with no key (not checked): " + ", ".join(unkeyed))
        print(f"Enemy census: {len(bad)} problems")
        sys.exit(1 if bad else 0)
