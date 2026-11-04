"""AI script scan for the battle randomizer (section 4: enemy B's actor script runs in A's room):
python -I enemyai.py <randomizer-rooms.txt> <enemies.txt> [--disc disc.cue|.bin] [--source room_load.c] [--out enemy-ai.txt] [--check]
                   [--table pe_rand_foes.h --models randomizer-models.txt --writes story-writes.txt [--deny foe-deny.txt]]
(the disc defaults to PE_TEST_DISC, the source to the checkout's src/main/scene/room_load.c.)

Each actor type has its own script block (Scene_LoadMap: script_base = the room task block's RelocBlock
offsets[type]) and every jump or data ref is script_base + v*2, so B's block runs as is from B's room buffer.
What does not come with it is anything the room or other entities give it. Per census enemy actor (enemies.txt,
enemyscan.py), over the steps of its own block:
  ops    every op it runs,
  pm     op 106 (Script_GetEntityEffect -> Scene_LoadRoomAssets) PM slots: 0-7 and 85-114 global, 8-84 and
         115+ installed per room from the directory's script records (SceneRoom_InstallAndResetScriptDescriptor),
  ent    entity numbers it addresses (the type operand of the ops in ENTITY_ARG): 0 = the player or the battle
         context; s = its own number (its siblings: A's number once moved, an operand to patch); p = its spawner
         (A's spawner once moved: patch); A0x.. = a number read at run time; anything else is another actor,
  events op 28 (named Script_PlayFieldSfx, but it queues an event for entity (type, sub): sfx_queue.c starts
         that entity's event routine with operand 2) sent to the spawner: the operand 2 values,
  akao   op 234 (Akao) commands (sounds from the room's sample bank),
  vars   N/T/V operands it shares with its spawner (the entities that place it: op 8, op 17 2500),
  text   op 34/35 text pages, assets: op 90 codes 0x32-0x37 (effect asset ids).
Per room: the PM slots its directory installs and the native handler SceneRoom_ResolveScriptDescriptor gives
each (read from room_load.c: slot and address -> symbol; "@address" when the source binds none).
Per (A slot <- B actor) pair of placed enemy actors with a creature, the conflicts:
  pm   a room slot B uses that A's room fills with a different handler (one A's room lacks is only added),
  ent    B addresses another actor (not 0, itself or its spawner),
  events B sends its spawner an event value A's own actor never sends A's spawner,
  vars   B shares an N or V operand with its spawner that A's spawner does not touch (the spawner stays A's).
Self and spawner numbers, run-time numbers and sounds are listed, not counted.
--table: the battle randomizer's foe table (pe_rand_foes.h, section 5): every placed actor with a creature, a
model placed in the enemy VRAM blocks (randomizer-models.txt, the rooms ctest) and no V var write of its own
(story-writes.txt: replacing its script would drop the write; such actors stay), boss or not (the boss pool:
a creature marked * in enemies.txt), with its room's VRAM use; and per slot the foes that may take it: same
pool, another creature, no ent or events conflict, the same spawner events (a fight's spawner counts on them),
both with one spawner or both not (pe_rand_ai.c patches one), a scripted slot only a foe with a lead sub id
(a scripted foe nowhere), and not in --deny (lines `MNNN<MBBB:E`: room
NNN's slots don't take room BBB's actor E; swaps whose fights fail headless, mod-randomizer-foefights).
--check: every actor has steps in its block, every room slot an actor starts is installed in its own room
with a handler the source binds, and each slot number has one handler in every room (what makes "pm" 0: B's
slots only need installing in A's room). Exit 1 on a failure, 77 when an input (or the disc) is missing."""
import os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import doorscan as DS
import enemyscan as ES

PREFIX = DS.PREFIX
# op -> index of the operand that holds an entity type number (the next one is its sub id).
ENTITY_ARG = {8: 0, 28: 0, 14: 1, 54: 0, 69: 0, 75: 0, 84: 0, 94: 1, 139: 0, 152: 0, 164: 0, 197: 0, 206: 0, 212: 0}
# op 17 / 237 sub commands with an entity type in operand 1 (2500 = spawn, 3000 = wait for an entity).
DISPATCH = {2500, 3000}
AKAO_ENTITY = 0x160   # op 234 command that names an entity type in operand 2
ASSET_CODES = range(0x32, 0x38)
SOURCE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "src", "main", "scene", "room_load.c")


def is_global(slot): return slot < 8 or 85 <= slot <= 114


def natives(path):
    """{(slot, address): symbol} from SceneRoom_ResolveScriptDescriptor's bindings (any room)."""
    text = open(path).read()
    i = text.find("SceneRoom_ResolveScriptDescriptor(int")
    body = text[i:text.find("\n}\n", i)]
    var = {}
    for name, val in re.findall(r"\b(\w+) = (0x[0-9a-fA-F]+)u?;", body): var.setdefault(name, []).append(int(val, 16))
    out = {}
    def bind(slot, expr, sym):
        adds = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", expr)] or var.get(expr.strip(), [])
        for a in adds: out[(int(slot), a)] = sym
    for slot, expr, sym in re.findall(r"slot == (\d+) && address == (\([^)]*\)|[\w]+)\)\s*(?:\{[^}]*?)?native = &?([\w\[\]]+)", body):
        bind(slot, expr, sym)
    for slot, a, sym in re.findall(r"\{(\d+), (0x[0-9a-fA-F]+)u, &([\w\[\]]+)\}", body): bind(slot, a, sym)
    arrays = {n: [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", v)] for n, v in re.findall(r"(\w+)\[\d+\] = \{([^}]*)\}", body)}
    for lo, hi, arr, sym in re.findall(r"slot >= (\d+) && slot <= (\d+) && address == (\w+)\[slot - \d+\]\)\s*native = &(\w+)\[slot - \d+\]", body):
        for k, slot in enumerate(range(int(lo), int(hi) + 1)): out[(slot, arrays[arr][k])] = f"{sym}[{k}]"
    return out


def room_slots(disc, m, table):
    """{slot: handler} the room's directory installs, {} without a disc or room."""
    if not disc: return {}
    import dis as D
    b = D.room(disc, m)
    if not b: return {}
    d = struct.unpack_from("<I", b, 4)[0]
    w = struct.unpack_from("<I", b, d + 4)[0]
    at, n = w & 0x3FFFFF, w >> 22
    out = {}
    for k in range(n):
        slot, addr = b[at + 12 * k + 7], struct.unpack_from("<I", b, at + 12 * k + 8)[0]
        out[slot] = table.get((slot, addr), f"@{addr:#x}")
    return out


def census(path):
    """[(room, entity, place, creature, kind)] from enemies.txt."""
    out = []
    for line in open(path):
        if line.startswith("#"): continue
        f = line.rstrip("\n").split("\t")
        out.append(dict(room=int(f[0][1:]), entity=int(f[2][1:]), place=f[1], ids=f[4], who=f[5], kind=f[6], group=f[7]))
    return out


def touched(steps, t):
    """{"N"/"T"/"V": {index}} the operands entity t's steps use."""
    out = {"N": set(), "T": set(), "V": set()}
    for e, op, a, _ in steps.values():
        if e != t: continue
        for mode, v in a:
            if mode in (2, 3, 4): out[PREFIX[mode]].add(v)
    return out


def scan(steps, t):
    mine = [(at, op, a) for at, (e, op, a, _) in sorted(steps.items()) if e == t]
    r = dict(ops=sorted({op for _, op, _ in mine}), pm=set(), ent=set(), events=set(), akao=set(), text=set(), assets=set())
    subs = set()
    for _, op, a in mine:
        k = lambda i: a[i] if len(a) > i else None
        if op == 106 and k(0): r["pm"].add(a[0][1] if a[0][0] == 0 else DS.operand(*a[0]))
        idx = ENTITY_ARG.get(op)
        if op in (17, 237) and k(0) and a[0][0] == 0 and a[0][1] in DISPATCH: idx = 1
        if op == 234 and k(0) and a[0][0] == 0:
            r["akao"].add(a[0][1])
            if a[0][1] == AKAO_ENTITY: idx = 2
        if idx is not None and k(idx):
            m, v = a[idx]
            r["ent"].add(v if m == 0 else DS.operand(m, v))
        if op == 28 and len(a) > 2: r["events"].add((a[0][1] if a[0][0] == 0 else DS.operand(*a[0]), DS.operand(*a[2])))
        if op == 38 and k(0) and k(0)[0] == 1: subs.add(a[0][1])
        if op in (34, 35) and k(0): r["text"].add(DS.operand(*a[0]))
        if op == 90 and len(a) == 2 and a[0][0] == 0 and a[0][1] in ASSET_CODES:
            r["assets"].add(f"{a[0][1]:#x}={DS.operand(*a[1])}")
    # lead: the sub id (op 38) the actor must have to send its spawner 0x70, the event that brings the battle in:
    # op 9 `T0 = A == K`, op 5 jump if zero T0, op 28 (spawner, 0, 0x70); None if no such gate.
    r["lead"] = None
    for (_, o1, a1), (_, o2, a2), (_, o3, a3) in zip(mine, mine[1:], mine[2:]):
        if (o1 == 9 and len(a1) == 4 and a1[0] == (0, 0xb) and a1[2][0] == 1 and a1[2][1] in subs and a1[3][0] == 0
                and o2 == 5 and a2 and a2[0] == a1[1] and o3 == 28 and len(a3) > 2 and a3[2] == (0, 0x70)):
            r["lead"] = a1[3][1]
    return r


def spawners(steps, t):
    return sorted({e for e, op, a, _ in steps.values()
                   if (op == 8 and a and a[0] == (0, t)) or (op == 17 and len(a) > 1 and a[0] == (0, 2500) and a[1] == (0, t))})


def show(xs): return ",".join(x if isinstance(x, str) else f"{x:#x}" if x > 9 else str(x) for x in sorted(xs, key=str)) or "-"


def ref(r, x):
    if x == 0: return "0"
    if x == r["entity"]: return "s"
    if x in r["spawners"]: return "p"
    return x


def to_spawner(r): return {v for t, v in r["events"] if t in r["spawners"]}


def pair(a, b, slots):
    """Conflicts of B's script in A's slot: {"pm", "ent", "events", "vars"} -> list."""
    pa, pb = slots.get(a["room"], {}), slots.get(b["room"], {})
    pm = [s for s in b["pm"] if isinstance(s, int) and not is_global(s) and s in pa and pa[s] != pb.get(s)]
    pm += [s for s in b["pm"] if not isinstance(s, int)]
    ent = [x for x in b["ent"] if isinstance(x, int) and ref(b, x) == x]
    if any(ref(b, x) == "p" for x in b["ent"]) and len(a["spawners"]) != 1: ent.append("spawner")
    events = sorted(to_spawner(b) - to_spawner(a))
    var = [v for v in b["shared"] if v not in a["spawner_vars"]]
    return {"pm": pm, "ent": ent, "events": events, "vars": var}


def models(path):
    """{room: {slot: (block, count, row)}} from randomizer-models.txt (block -1: not placed in the blocks)."""
    out = {}
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        if len(f) < 8 or f[1] != "model": continue
        v = {k: int(x) for k, x in (kv.split("=") for kv in f[3:8])}
        x, y, n = v["x"], v["y"], v["mode"]
        block = (x - 512) // 64 * 2 + y // 128 if n and 512 <= x < 768 and y < 256 else -1
        out.setdefault(int(f[0][1:]), {})[int(f[2])] = (block, n, v["pal"] - 450)
    return out


def story_writers(path):
    out = set()
    for line in open(path):
        m = re.match(r"M(\d+) \+\w+ e(\d+) sets V", line)
        if m: out.add((int(m[1]), int(m[2])))
    return out


def deny_list(path):
    out = set()
    for line in open(path) if path else []:
        m = re.match(r"M(\d+)<M(\d+):(\d+)", line)
        if m: out.add((int(m[1]), int(m[2]), int(m[3])))
    return out


def write_table(pool, model, writers, path, deny=frozenset()):
    """pe_rand_foes.h; returns the foe count."""
    foes = []
    for r in pool:
        slots = model.get(r["room"], {})
        if (r["room"], r["entity"]) in writers or slots.get(r["entity"], (-1,))[0] < 0: continue
        block, count, _ = slots[r["entity"]]
        users = [0] * 8
        end = rows_end = 0
        for b, n, row in slots.values():
            if n and 0 <= row < 8: rows_end = max(rows_end, row + n)
            if b < 0: continue
            end = max(end, b + n)
            for k in range(b, min(b + n, 8)): users[k] += 1
        shared = any(users[k] > 1 for k in range(block, min(block + count, 8)))
        foes.append(dict(r=r, boss="*" in r["who"], block=block, count=count, shared=shared, end=end, rows_end=rows_end))
    def ok(a, b):
        if a is b or a["r"]["ids"] == b["r"]["ids"] or a["boss"] != b["boss"]: return False
        # A scripted fight (M013's Rat) is spawned by op 8 with sub id 0 and its actor sends 0x70 at once; a random
        # foe sends it only with its lead sub id (else the battle never comes in: black screen). So a scripted slot
        # takes a random foe with a lead (pe_rand_ai.c gives the slot's op 8 that sub id); a scripted foe goes nowhere.
        if b["r"]["kind"] == "scripted" or (a["r"]["kind"] == "scripted" and b["r"]["lead"] is None): return False
        if (a["r"]["room"], b["r"]["room"], b["r"]["entity"]) in deny: return False
        c = pair(a["r"], b["r"], {})
        return (not c["ent"] and not c["events"] and to_spawner(a["r"]) == to_spawner(b["r"])
                and (len(a["r"]["spawners"]) == 1) == (len(b["r"]["spawners"]) == 1))
    words = (len(foes) + 31) // 32
    head = ["/* Generated by tools/enemyai.py --table from the AI scan, the rooms ctest's models listing and the story",
            " * writes: the battle randomizer's foes (pe_rand_gen.c pe_rand_foes_generate), room order. ok[a] bit b: foe",
            " * b's script and model may take foe a's slot. Do not edit. */",
            "#ifndef PE_RAND_FOES_H", "#define PE_RAND_FOES_H", "", '#include "pe_randomizer.h"', "",
            f"#define PE_RAND_FOES {len(foes)}", f"#define PE_RAND_FOE_WORDS {words}",
            "static const PeRandFoe g_pe_rand_foes[PE_RAND_FOES] = {"]
    for f in foes:
        r = f["r"]
        name = r["who"].replace("*", "").replace('"', "")
        head.append(f'    {{ {r["room"]}, {r["entity"]}, {int(f["boss"])}, {f["block"]}, {f["count"]}, {int(f["shared"])}, '
                    f'{f["end"]}, {f["rows_end"]}, "{name}", "{r["place"]}" }},')
    head += ["};", "static const uint32_t g_pe_rand_foe_ok[PE_RAND_FOES][PE_RAND_FOE_WORDS] = {"]
    for a in foes:
        bits = [0] * words
        for j, b in enumerate(foes):
            if ok(a, b): bits[j // 32] |= 1 << (j % 32)
        head.append("    { " + ", ".join(f"0x{w:08x}" for w in bits) + " },")
    head += ["};", "", "#endif", ""]
    open(path, "w", newline="\n").write("\n".join(head))
    return len(foes)


def main():
    args = sys.argv[1:]
    def opt(name, default=None):
        if name in args: i = args.index(name); v = args[i + 1]; del args[i:i + 2]; return v
        return default
    disc, source, out_path = opt("--disc", os.environ.get("PE_TEST_DISC")), opt("--source", SOURCE), opt("--out")
    table_path, models_path, writes_path, deny_path = opt("--table"), opt("--models"), opt("--writes"), opt("--deny")
    do_check = "--check" in args
    args = [a for a in args if a != "--check"]
    if len(args) != 2: sys.exit(__doc__)
    missing = [p for p in args + [source] if not os.path.exists(p)]
    if missing: print("enemyai: missing " + ", ".join(missing)); sys.exit(77)
    disc = ES.bin_path(disc) if disc else None
    if disc and not os.path.exists(disc): disc = None
    if do_check and not disc: print("enemyai: no disc (PE_TEST_DISC)"); sys.exit(77)
    rooms = DS.load(args[0])
    rows = census(args[1])
    table = natives(source)
    slots = {m: room_slots(disc, m, table) for m in sorted({r["room"] for r in rows})}
    for r in rows:
        steps = rooms[r["room"]][1]
        r.update(scan(steps, r["entity"]))
        sp = spawners(steps, r["entity"])
        mine, theirs = touched(steps, r["entity"]), {"N": set(), "T": set(), "V": set()}
        for s in sp:
            for k, v in touched(steps, s).items(): theirs[k] |= v
        r["spawners"] = sp
        r["shared"] = [f"{k}{v:#x}" for k in ("N", "V") for v in sorted(mine[k] & theirs[k])]
        r["sharedT"] = sorted(mine["T"] & theirs["T"])
        r["spawner_vars"] = {f"{k}{v:#x}" for k in ("N", "V") for v in theirs[k]}
    f = open(out_path, "w") if out_path else sys.stdout
    print("# actor\troom\tplace\tentity\tcreature\tkind\tspawners\tpm (r = room slot: handler)\tent (0 player, s self, p spawner)\tevents to spawner\takao\tvars shared with spawner\tT shared\ttext\tassets\tops", file=f)
    for r in rows:
        pr = slots.get(r["room"], {})
        pm = ",".join(f"{s}g" if is_global(s) else f"{s}r:{pr.get(s, 'none')}" for s in sorted(r["pm"], key=str) if isinstance(s, int))
        pm += "".join("," + s for s in r["pm"] if not isinstance(s, int))
        print(f"actor\tM{r['room']:03d}\t{r['place']}\te{r['entity']}\t{r['who']}\t{r['kind']}\t{show(r['spawners'])}\t"
              f"{pm.strip(',') or '-'}\t{show({str(ref(r, x)) if ref(r, x) != x else x for x in r['ent']})}\t{show(to_spawner(r))}\t{show(r['akao'])}\t{','.join(r['shared']) or '-'}\t"
              f"{show(r['sharedT'])}\t{show(r['text'])}\t{','.join(sorted(r['assets'])) or '-'}\t{','.join(map(str, r['ops']))}", file=f)
    for m, s in slots.items():
        print(f"room\tM{m:03d}\t" + (",".join(f"{k}:{v}" for k, v in sorted(s.items())) or "-"), file=f)
    # Pairs: placed actors with a creature (parts and never-placed copies come with their creature later).
    pool = [r for r in rows if r["who"] and not r["who"].startswith(("part of", "?")) and r["group"] != "never placed"]
    names = ("pm", "ent", "events", "vars")
    hist, clean, per, adds = {}, [], {n: 0 for n in names}, 0
    for a in pool:
        for b in pool:
            if a is b or a["ids"] == b["ids"]: continue
            c = pair(a, b, slots)
            key = tuple(n for n in names if c[n])
            hist[key] = hist.get(key, 0) + 1
            for n in key: per[n] += 1
            if any(isinstance(x, int) and not is_global(x) and x not in slots.get(a["room"], {}) for x in b["pm"]): adds += 1
            if not key: clean.append((a, b))
    total = sum(hist.values())
    print(f"pairs\t{total} (A slot <- B actor: {len(pool)} placed actors with a creature, different creature ids)", file=f)
    print("pairs\tby conflict\t" + ", ".join(f"{n} {per[n]}" for n in names)
          + f" (pm: each slot has one handler in every room; {adds} pairs add a slot A's room lacks)", file=f)
    for k in sorted(hist, key=lambda k: -hist[k]):
        print(f"pairs\t{'+'.join(k) or 'clean'}\t{hist[k]}", file=f)
    random_ = [(a, b) for a, b in clean if a["kind"] == "random" and b["kind"] == "random"]
    fits = {}
    for a, b in random_: fits.setdefault((b["room"], b["entity"], b["who"]), set()).add((a["room"], a["entity"]))
    for (m, e, who), x in sorted(fits.items(), key=lambda x: -len(x[1])):
        print(f"fits\tM{m:03d} e{e} {who}\tclean in {len(x)} random A slots", file=f)
    for a, b in random_:
        print(f"clean\tM{a['room']:03d} e{a['entity']} {a['who']} <- M{b['room']:03d} e{b['entity']} {b['who']}", file=f)
    if out_path: f.close()
    if table_path:
        if not models_path or not writes_path: sys.exit("--table needs --models and --writes")
        n = write_table(pool, models(models_path), story_writers(writes_path), table_path, deny_list(deny_path))
        print(f"enemyai: {n} foes in {table_path}", file=sys.stderr)
    print(f"enemyai: {len(rows)} actors, {len(slots)} rooms, {total} pairs, {hist.get((), 0)} clean "
          f"({len(random_)} random <- random)" + ("" if disc else "; no disc: room PM slots not read"), file=sys.stderr)
    if do_check:
        bad, seen = [], {}
        for r in rows:
            where = f"M{r['room']:03d} e{r['entity']}"
            if not r["ops"]: bad.append(f"{where}: no steps in its block")
            mine = slots.get(r["room"], {})
            for x in r["pm"]:
                if isinstance(x, int) and not is_global(x) and not str(mine.get(x, "@")).startswith("g_"):
                    bad.append(f"{where}: room slot {x} is {mine.get(x, 'not installed')} in its room")
        for m, sl in slots.items():
            for x, h in sl.items():
                if not h.startswith("@"): seen.setdefault(x, {}).setdefault(h, []).append(m)
        for x, hs in sorted(seen.items()):
            if len(hs) > 1: bad.append(f"slot {x} has {len(hs)} handlers: " + "; ".join(f"{h} M{v[0]:03d}" for h, v in hs.items()))
        for b in bad: print("FAIL " + b)
        print(f"Enemy AI: {len(bad)} problems")
        sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
