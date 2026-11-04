"""Doors and what guards them, from the mod-randomizer-rooms ctest's step listing.
Usage: python -I doorscan.py <randomizer-rooms.txt> [--bits | --kills | --exits <randomizer-exits.txt>] [M107 ...]
(default: every room)
--bits lists story variable writes instead (V|mask bit sets, V=value), with the ways to reach them.
--kills lists op 24 kills (object locks: a key check stops the routine that blocks the way), the ways to
reach the kill and what the killed routine does.
One line per door: room, offset, entity, target, then the ways to reach it, split by " | ": each way is the
conditions (" ; ") that hold on every path along it.
A routine starts at an entity entry, an op 18 child or an op 20 handler. Within a routine, conditions come
from branches (op 5 jump if zero, op 29 branch if different). A child (op 18, op 20 selectors 1 and 3) is
reached the ways each place that starts it is. Op 20 selector 2 is the entity's event handler: op 28
(entity, sub id, value) starts it on every entity of that task index (Scene_UpdateEntityList; entity 0 is
the player), op 31 reads the value, shown as `event`. Each op 28 in the room whose value passes the
handler's tests is a way in; a test with no sender in the room stays as `(event == n)`.
Op 25 stores the running routine's id in a field; op 24 kills the routine of the same entity with that id.
A tested value is shown as the step that computed it: item(...) = op 168 count, V0x18&bit5 = op 41 on a
script variable, scene&mask = op 81, area = op 119 point-in-quad (where the player stands), others by op number.
Door targets M0000 / M5000 / M8888 are the effect scene, the title and the boot display.
--exits resolves the doors to M0000, the New York map, with the mod-randomizer-exits ctest's listing: per
V0x4a the door can run at, the map bits of V0x3 the room sets (each write whose V0x4a tests hold there, in
step order: rooms set the whole cumulative list before the map, and night = 0x2000), the first and forced
destination, and the room each enabled destination loads.
At most 8 ways are kept per step, shortest first."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from keyscan import KEYS

END = {0, 1, 32}
PREFIX = {0: "", 1: "A", 2: "V", 3: "T", 4: "N"}
BINARY = {0: "+", 1: "-", 2: "|", 3: "&", 4: "^", 5: "||", 6: "&&", 9: ">", 10: "<", 11: "==", 12: ">=",
          13: "<=", 14: "!=", 15: "*", 16: "/", 17: "<<", 18: ">>", 22: "%"}
NEGATE = {"==": "!=", "!=": "==", ">": "<=", "<": ">=", ">=": "<", "<=": ">"}
DST = {9: 1, 10: 0, 41: 2, 81: 1, 168: 1, 167: 1, 74: 1, 44: 0, 119: 10, 31: 0, 25: 0}
WAYS = 8
AREA_END = 0x1000   # V0x4a values the mod-randomizer-exits ctest probes


def load(path):
    """{room: (entries, {offset: (entity, op, [(mode, value)], target)})}"""
    rooms = {}
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        if f[0] == "room":
            rooms[int(f[1][1:])] = ([int(x, 16) for x in f[6].split(",")] if len(f) > 6 else [], {})
        elif f[0] == "step":
            ops = [(int(m), int(v, 16)) for m, v in (o.split(":") for o in f[5].split(","))] if len(f) > 5 and f[5] else []
            rooms[int(f[1][1:])][1][int(f[3], 16)] = (int(f[2]), int(f[4]), ops, int(f[6][1:]) if len(f) > 6 else None)
    return rooms


def size(ops): return 8 + 4 * len(ops)


def graph(entries, steps):
    """Edges inside routines (from, to, condition or None; a condition is (branch offset, taken)) and
    routine starts {start: [(kind, site)]}, kind "entry", "child" or "event" (op 20 selector 2)."""
    edges, starts = [], {e: [("entry", None)] for e in entries}
    for at, (t, op, a, _) in steps.items():
        base = entries[t]
        nxt = at + size(a)
        def code(k): return base + a[k][1] * 2 if len(a) > k and a[k][0] == 0 and a[k][1] < 0x80000000 else None
        if op == 0:
            if code(0) is not None: edges.append((at, code(0), None))
            continue
        if op == 5 and not (a[0][0] == 0 and a[0][1]):
            if code(1) is not None: edges.append((at, code(1), (at, True)))
            if not (a[0][0] == 0 and a[0][1] == 0) and nxt in steps: edges.append((at, nxt, (at, False)))
            continue
        if op == 29:
            if code(2) is not None: edges.append((at, code(2), (at, True)))
            if nxt in steps: edges.append((at, nxt, (at, False)))
            continue
        if op == 18 and code(0) is not None: starts.setdefault(code(0), []).append(("child", at))
        if op == 20 and len(a) > 1 and a[0][0] == 0 and 1 <= a[0][1] <= 3 and code(1) is not None:
            starts.setdefault(code(1), []).append(("event" if a[0][1] == 2 else "child", at))
        if op not in END and nxt in steps: edges.append((at, nxt, None))
    return [(x, y, c) for x, y, c in edges if y in steps], {s: k for s, k in starts.items() if s in steps}


def must_hold(succ, preds, root):
    """Conditions that hold on every path from root to each step it reaches (iterative intersection)."""
    reach, todo = {root}, [root]
    while todo:
        for y, _ in succ.get(todo.pop(), ()):
            if y not in reach: reach.add(y); todo.append(y)
    held = {root: frozenset()}
    order = sorted(reach); changed = True
    while changed:
        changed = False
        for at in order:
            if at == root: continue
            ins = [held[x] | ({c} if c else frozenset()) for x, c in preds.get(at, []) if x in held]
            if not ins: continue
            new = frozenset.intersection(*ins)
            if held.get(at) != new: held[at] = new; changed = True
    return held


def operand(m, v):
    if m == 0: return f"{v:#x}" if v < 0x80000000 else str(v - (1 << 32))
    return f"{PREFIX.get(m, '?')}{v:#x}"


def expr(steps, preds, at, loc, writers=None, depth=0):
    """Symbolic value of operand loc as it stands before step at (walks back through single predecessors;
    an entity field whose only writer is op 31 is the event value)."""
    if loc[0] == 0 or depth > 3: return operand(*loc)
    def defined(cur):
        t, op, a, _ = steps[cur]
        def arg(k): return expr(steps, preds, cur, a[k], writers, depth + 1)
        dst = DST.get(op)
        if dst is None or dst >= len(a) or a[dst] != loc: return None
        if op == 9:
            o = a[0][1] if a[0][0] == 0 else None
            if o == 7: return f"!{arg(2)}"
            if o == 19: return arg(2)
            return f"({arg(2)} {BINARY.get(o, f'op9.{o}')} {arg(3)})"
        if op == 10: return arg(1)
        if op == 41: return f"{arg(0)}&bit{a[1][1]}" if a[1][0] == 0 else f"{arg(0)}&bit[{arg(1)}]"
        if op == 81: return f"scene&{arg(0)}"
        if op == 168:
            item = a[0][1] if a[0][0] == 0 else None
            return f"item({KEYS.get(item, f'{item:#x}') if item is not None else arg(0)})"
        if op == 167: return f"gave({arg(0)})"
        if op == 74: return f"self.flags&{arg(0)}"
        if op == 44: return "movelock"
        if op == 119: return "area"
        if op == 31: return "event"
        if op == 25: return "self.id"
    cur = at
    for _ in range(64):
        p = preds.get(cur, [])
        if len(p) != 1: break
        cur = p[0][0]
        e = defined(cur)
        if e is not None: return e
    w = writers.get((steps[at][0], loc), ()) if writers and loc[0] == 1 else ()
    if len(w) == 1 and steps[w[0]][1] == 31: return "event"
    return operand(*loc)


def opposite(c):
    if c.startswith("(") and c.split(" ")[1] in NEGATE:
        l, o, r = c[1:-1].split(" ", 2); return f"({l} {NEGATE[o]} {r})"
    return c[1:] if c.startswith("!") else f"!{c}"


def condition(steps, preds, branch, taken, writers=None):
    t, op, a, _ = steps[branch]
    if op == 5:
        e = expr(steps, preds, branch, a[0], writers)
        return opposite(e) if taken else e
    l, r = expr(steps, preds, branch, a[0], writers), expr(steps, preds, branch, a[1], writers)
    return f"({l} {'!=' if taken else '=='} {r})"


def fits(cond, value):
    """Whether a test on the event value holds for value: True, False, or None when it tests more than that."""
    if value is None: return None
    s = cond.replace("event", str(value))
    if not re.fullmatch(r"[\s\da-fx()!=<>|&-]+", s): return None
    s = re.sub(r"!(?!=)", " not ", s.replace("||", " or ").replace("&&", " and "))
    try: return bool(eval(s, {"__builtins__": {}}))
    except Exception: return None


def minimal(ways):
    """Drop ways implied by a shorter one; keep at most WAYS. A way that holds both x and !x stays: values
    like area or entity fields are tested by different routines at different times."""
    out = []
    for w in sorted(set(ways), key=lambda w: (len(w), sorted(w))):
        if any(o <= w for o in out): continue
        out.append(w)
    return out[:WAYS]


class Room:
    def __init__(self, entries, steps):
        self.steps = steps
        edges, self.starts = graph(entries, steps)
        self.preds, succ = {}, {}
        for x, y, c in edges: self.preds.setdefault(y, []).append((x, c)); succ.setdefault(x, []).append((y, c))
        self.writers = {}
        for at, (t, op, a, _) in steps.items():
            if op in DST and DST[op] < len(a): self.writers.setdefault((t, a[DST[op]]), []).append(at)
        self.held, self.roots = {}, {}
        for r in self.starts:
            self.held[r] = {at: frozenset(condition(steps, self.preds, b, k, self.writers) for b, k in h)
                            for at, h in must_hold(succ, self.preds, r).items()}
            for at in self.held[r]: self.roots.setdefault(at, []).append(r)
        self.sends = {}
        for at, (t, op, a, _) in steps.items():
            if op == 28 and len(a) == 3 and a[0][0] == 0 and at in self.roots:
                self.sends.setdefault(a[0][1] & 0xFFFF, []).append((a[2][1] & 0xFF if a[2][0] == 0 else None, at))
        # Ways into every place that starts a routine, to a fixpoint (routines start each other in loops).
        sites = sorted({s for k in self.starts.values() for _, s in k if s is not None} |
                       {s for v in self.sends.values() for _, s in v})
        self.memo = {}
        for _ in range(32):
            changed = False
            for s in sites:
                w = self.reach(s)
                if self.memo.get(s) != w: self.memo[s] = w; changed = True
            if not changed: break

    def ways(self, at):
        """Ways to reach step at: condition sets, one per way in (empty when unreached)."""
        return self.memo[at] if at in self.memo else self.reach(at)

    def reach(self, at):
        out = []
        for r in self.roots.get(at, ()):
            local = self.held[r][at]
            for kind, site in self.starts[r]:
                if kind == "entry": out.append(local)
                elif kind == "child": out += [w | local for w in self.memo.get(site, [])]
                else: out += [w | v for w in self.memo.get(site, []) for v in self.events(r, local)]
        return minimal(out)

    def events(self, handler, local):
        """Ways into an event handler given the conditions held inside it: one per op 28 to its entity whose
        value passes the event tests; the tests themselves when no sender passes them."""
        tests = [c for c in local if "event" in c]
        if not tests: return [local]
        rest, out, sent = local - set(tests), [], False
        for value, send in self.sends.get(self.steps[handler][0], ()):
            fit = [fits(c, value) for c in tests]
            if False in fit: continue
            sent = True
            out += [w | rest | {c for c, f in zip(tests, fit) if f is None} for w in self.memo.get(send, [])]
        return out if sent else [local]

    def does(self, root):
        """What a routine does: its events sent, doors and kills."""
        out = []
        for at in sorted(self.held[root]):
            t, op, a, target = self.steps[at]
            if op == 28 and len(a) == 3: out.append(f"e{operand(*a[0])} event {operand(*a[2])}")
            if op == 49: out.append(f"-> M{target:03d}" if target is not None else f"-> {operand(*a[0])}")
            if op == 24 and a: out.append(f"kills {operand(*a[0])}")
        return ", ".join(out) or "nothing listed"

    def kills(self):
        """(site, entity, killed routine starts) per op 24: the routines that ran op 25 into the field
        op 24 reads, in the same entity."""
        out = []
        for at in sorted(self.roots):
            t, op, a, _ = self.steps[at]
            if op != 24 or not a or a[0][0] == 0: continue
            killed = sorted({r for s in self.writers.get((t, a[0]), ()) if self.steps[s][1] == 25 for r in self.roots.get(s, ())})
            out.append((at, t, killed))
        return out


def show(ways): return " | ".join(" ; ".join(sorted(w)) or "always" for w in ways) if ways else "unreached"


def doors(rooms, only=None):
    out = []
    for room in sorted(rooms):
        if only and room not in only: continue
        r = Room(*rooms[room])
        for at in sorted(r.steps):
            t, op, a, target = r.steps[at]
            if op == 49: out.append((room, at, t, target if target is not None else operand(*a[0]), r.ways(at)))
    return out


def bit_op(r, at, loc, var):
    """`|mask` or `&mask` when loc, assigned to var at at, was made by op 9 `var | constant` or `var & constant`
    on the single path into at, else None. Read from that step itself: expr() also expands var through
    its earlier writes, so a run of bit sets (V0x3 before the map) would read ((V0x3 | 0x4000) | 0x8000)."""
    cur = at
    for _ in range(64):
        p = r.preds.get(cur, [])
        if len(p) != 1: return None
        cur = p[0][0]
        _, op, a, _ = r.steps[cur]
        dst = DST.get(op)
        if dst is None or dst >= len(a) or a[dst] != loc: continue
        if op == 9 and len(a) == 4 and a[0] in ((0, 2), (0, 3)) and a[2] == var and a[3][0] == 0:
            return f"{BINARY[a[0][1]]}{a[3][1]:#x}"
        return None
    return None


def bits(rooms, only=None):
    """Story variables written: (room, offset, entity, var, how, ways), how = "|0x20" for a bit set
    (EvalExpr(|, T, V, mask) then Assign(V, T), or op 42 SetBit on V), "&0xffffdfff" for a clear the
    same way, or "=0x90" for a constant
    (V0x1, the room just left, is written this way)."""
    out = []
    for room in sorted(rooms):
        if only and room not in only: continue
        r = Room(*rooms[room])
        for at in sorted(r.steps):
            t, op, a, _ = r.steps[at]
            var = how = None
            if op == 10 and len(a) == 2 and a[0][0] == 2:
                how = bit_op(r, at, a[1], a[0])
                if how is not None: var = a[0][1]
                else:
                    e = expr(r.steps, r.preds, at, a[1], r.writers)
                    if e.startswith("0x"): var, how = a[0][1], "=" + e
            elif op == 42 and len(a) == 2 and a[0][0] == 2 and a[1][0] == 0: var, how = a[0][1], f"|{1 << (a[1][1] & 31):#x}"
            if var is None: continue
            out.append((room, at, t, var, how, r.ways(at)))
    return out


def kills(rooms, only=None):
    out = []
    for room in sorted(rooms):
        if only and room not in only: continue
        r = Room(*rooms[room])
        for at, t, killed in r.kills():
            what = ", ".join(f"+{k:06X} ({r.does(k)})" for k in killed) or "no op 25 stores to it in this entity"
            out.append((room, at, t, r.steps[at][2][0], what, r.ways(at)))
    return out


def read_exits(path):
    """The ctest's map listing: {"bit": {d: bit}, "start": {V0x1: d}, "mode": [(lo, hi, {kind: d})],
    "exit": {(d, night): [(lo, hi, room)]}}."""
    m = {"bit": {}, "start": {}, "mode": [], "exit": {}}
    def rng(x): lo, hi = x.split("-"); return int(lo, 16), int(hi, 16)
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        if f[0] == "dest": m["bit"][int(f[1])] = int(f[2][4:], 16)
        elif f[0] == "start": m["start"][int(f[1][1:])] = int(f[2][5:])
        elif f[0] == "mode": m["mode"].append((*rng(f[1]), {k: int(v) for k, v in (x.split("=") for x in f[2:])}))
        elif f[0] == "exit":
            d, night = int(f[1][5:]), int(f[2][6:])
            m["exit"].setdefault((d, night), []).append((*rng(f[3][6:]), int(f[4][4:])))
    return m


def holds_at(way, area):
    """Whether a way can hold with V0x4a == area: its V0x4a tests are evaluated, other tests assumed."""
    return all(fits(c.replace("V0x4a", "event"), area) is not False for c in way if "V0x4a" in c)


def exits(rooms, path, only=None):
    """(room, offset, entity, (lo, hi), night, enabled dests, start, modes, {d: exit rooms}, {d: other tests its
    bit write needs}) per door to M0000 and V0x4a band: each value the door tests for, or when it tests none, each band between the V0x4a
    thresholds of the room's map writes (map bits and night read at the band's first value)."""
    m, out = read_exits(path), []
    def at_area(table, area): return next((x for lo, hi, x in table if lo <= area <= hi), None)
    for room in sorted(rooms):
        if only and room not in only: continue
        r = Room(*rooms[room])
        writes = []
        for at in sorted(r.steps):
            t, op, a, _ = r.steps[at]
            if op == 10 and len(a) == 2 and a[0] == (2, 3):
                how = bit_op(r, at, a[1], a[0])
                if how: writes.append((at, t, how[0], int(how[1:], 16), r.ways(at)))
        for at in sorted(r.steps):
            t, op, a, target = r.steps[at]
            if op != 49 or target != 0: continue
            mine = [w for w in writes if w[1] == t and w[0] < at]
            pinned = sorted({int(c.split(" == ")[1][:-1], 16) for w in r.ways(at) for c in w
                             if re.fullmatch(r"\(V0x4a == 0x[0-9a-f]+\)", c)})
            if pinned: bands = [(n, n) for n in pinned]
            else:
                # No single value: one band per V0x4a threshold the room's map writes test (their ladder).
                cuts = sorted({0} | {int(x, 16) + (1 if o in (">", "<=") else 0) for *_, wways in mine
                                     for w in wways for c in w for o, x in re.findall(r"V0x4a (\S+) (0x[0-9a-f]+)", c)})
                bands = [(lo, hi - 1) for lo, hi in zip(cuts, cuts[1:] + [AREA_END])]
            own = {c for w in r.ways(at) for c in w}
            def extra(c): return "V0x4a" not in c and "area" not in c and c not in own and re.search(r"[A-Zi]", c)
            for lo, hi in bands:
                v3, needs = 0, {}
                for wat, wt, kind, mask, wways in mine:
                    held = [w for w in wways if holds_at(w, lo)]
                    if not held: continue
                    v3 = v3 | mask if kind == "|" else v3 & mask
                    if kind == "|":   # tests the write needs beyond the door's own (none on some way: always)
                        needs[mask] = min((frozenset(filter(extra, w)) for w in held), key=len)
                dests = [d for d, bit in sorted(m["bit"].items()) if v3 & bit]
                night = 1 if v3 & 0x2000 else 0
                modes = at_area(m["mode"], lo) if lo == hi else None
                to = {d: sorted({x for elo, ehi, x in m["exit"].get((d, night), []) if elo <= hi and lo <= ehi})
                      for d in dests}
                cond = {d: sorted(needs.get(m["bit"][d], ())) for d in dests}
                out.append((room, at, t, (lo, hi), night, dests, m["start"].get(room, 7), modes or {}, to, cond))
    return out


if __name__ == "__main__":
    if len(sys.argv) < 2: sys.exit(__doc__)
    mode = next((x for x in sys.argv[2:] if x in ("--bits", "--kills", "--exits")), None)
    args = [x for x in sys.argv[2:] if x != mode]
    exits_path = args.pop(0) if mode == "--exits" and args else None
    if mode == "--exits" and not exits_path: sys.exit(__doc__)
    only = {int(x.lstrip("Mm")) for x in args} or None
    rooms = load(sys.argv[1])
    if mode == "--exits":
        for room, at, t, area, night, dests, start, modes, to, cond in exits(rooms, exits_path, only):
            where = f"V0x4a={area[0]:#x}" if area[0] == area[1] else f"V0x4a={area[0]:#x}-{area[1]:#x}"
            forced = "".join(f" {k}=d{v}" for k, v in sorted(modes.items()))
            picks = ", ".join(f"d{d}" + (f" if {' ; '.join(cond[d])}" if cond[d] else "") + "->" +
                              "/".join(f"M{x:03d}" for x in to[d]) for d in dests) or "no map bits set here"
            print(f"M{room:03d} +{at:06X} e{t} -> M000 map\t{where} night={night} start=d{start}{forced}\t{picks}")
    elif mode == "--bits":
        for room, at, t, var, how, ways in bits(rooms, only):
            print(f"M{room:03d} +{at:06X} e{t} sets V{var:#x}{how}\t" + show(ways))
    elif mode == "--kills":
        for room, at, t, field, what, ways in kills(rooms, only):
            print(f"M{room:03d} +{at:06X} e{t} kills {operand(*field)} = {what}\t" + show(ways))
    else:
        for room, at, t, target, ways in doors(rooms, only):
            to = f"M{target:03d}" if isinstance(target, int) else target
            print(f"M{room:03d} +{at:06X} e{t} -> {to}\t" + show(ways))
