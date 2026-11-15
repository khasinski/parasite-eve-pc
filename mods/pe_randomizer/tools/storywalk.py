"""Walk the whole game in story order from storygraph.py's listing: every place a new game can reach,
for a key solver: which steps need which keys, where each give can run.
Usage: python -I storywalk.py <story-graph.txt> <randomizer-exits.txt> [--spots ../pe_rand_spots.h]
       [--start M431] [--states out.txt] [--runs randomizer-run.txt [--zones zone-run.txt[,zone-run2.txt...]]]
       (M431: where New Game starts)

A place is the room, V0x4a (story value) and V0x1 (room left: picks the room version; 0 in rooms
that never read it). Every other story variable that matters (bit by bit: a door tests it, or a write
to one that matters does, or a key's give or take does, to a fixpoint) is kept per place as each bit
set, clear or either (the places' join), so optional events don't multiply places. In a room, any
write whose ways can hold may run, any number of times; a door moves to its target (M0000: the New
York map, each destination its V0x3 bit enables, the room the exits ctest gives for it, V0x1 = 999).
The story writes in the straight run before a step run with it (tests first, then writes, then door).
Key items are not followed: the randomizer moves them. Every item test may pass, and each step notes
the keys its cheapest way needs (item(K) read as true, and a story bit only steps needing a key set,
Graph.unlocks, read as set), so a solver (keysolve.py) can check a layout against the places. Other tests the listing can't decide (entity, scene and temp fields, area, events, a bit that
may be either) are assumed to pass: the walk over-approximates what a player can do, never under. So
a bit that closes a door (the hospital's V0x42|4) is not seen as closing it unless V0x4a changes too.
A give succeeds (gave(x) == 0).

Prints, in story order (first reached), each V0x4a value with the rooms that set it and the rooms open
while it holds; the rooms never reached; each step that needs a key; each give with the V0x4a values it
can run at and the keys it needs. --states writes every place with its bits (may be 1 / may be 0) and
its successors, each with [the keys it needs].

--zones (the run test's zone runs, zoneruns.py, with --runs): which zone opens which door, learnt from
every place of the room (Aya placed in a zone fires the door). At a place whose run tested all the
zones that open a door and found each cut off (closed floor triangles: cut=), the walk leaves the door
out. Anything else keeps doorscan's ways: a zone not tested at a place, or a door no zone run fires
(a button, an item, an event), is no evidence it's shut. Zone runs from other base runs (runlist.py
--may) go in the same list after their base run file (randomizer-run-may.txt,zone-run-may.txt,...):
the base file gives only the zones' points, the zone runs only add zone -> door links (cut= stays the
--runs file's)."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from keyscan import KEYS
from doorscan import read_exits

KEY_IDS = {v: k for k, v in KEYS.items()}
AREA, ROOM_LEFT, FLAGS = 0x4A, 0x1, 0x3
KEY_BIT_RANGE = 0x200   # a key give's own bit is written this close after it (M344: +0x120)
MAP_ROOM_LEFT = 999
DAY2 = 0x88   # pe_randomizer.c PE_RAND_DAY2: Hard's moved doors only from here on

TOKEN = re.compile(r"\s*(item\([^)]*\)|gave\([^)]*\)|op9\.\d+|[VATN]0x[0-9a-f]+|0x[0-9a-f]+|\d+|self\.\w+|"
                   r"[a-z]+|&&|\|\||==|!=|<=|>=|<<|>>|[-+*/%<>&|^!()\[\]])")
BINARY = [("||",), ("&&",), ("|",), ("^",), ("&",), ("==", "!="), ("<", ">", "<=", ">="), ("<<", ">>"),
          ("+", "-"), ("*", "/", "%")]


def tokens(s):
    out, i = [], 0
    while i < len(s):
        m = TOKEN.match(s, i)
        if not m: raise ValueError(f"can't read {s[i:]!r} in {s!r}")
        out.append(m.group(1)); i = m.end()
        while i < len(s) and s[i] == " ": i += 1
    return out


def parse(s):
    """A condition as a tree: ("num", n) ("var", kind, n) ("item", id) ("gave",) ("?",) ("!", x) (op, a, b)."""
    s = re.sub(r"(\S+)&bit\[[^\]]*\]", r"(\1 & ?)", s)
    s = re.sub(r"(\S+)&bit(\d+)", lambda m: f"({m.group(1)} & {1 << (int(m.group(2)) & 31):#x})", s)
    t = tokens(s.replace("?", "unknown")); pos = [0]
    def peek(): return t[pos[0]] if pos[0] < len(t) else None
    def take(): pos[0] += 1; return t[pos[0] - 1]
    def primary():
        x = take()
        if x == "!": return ("!", primary())
        if x == "-": return ("neg", primary())
        if x == "(":
            e = level(0)
            if take() != ")": raise ValueError(s)
            return e
        if re.fullmatch(r"0x[0-9a-f]+|\d+", x): return ("num", int(x, 0))
        if x[0] in "VATN" and x[1:3] == "0x": return ("var", x[0], int(x[1:], 16))
        if x.startswith("item("):
            name = x[5:-1]
            return ("item", KEY_IDS.get(name, int(name, 16) if name.startswith("0x") else None))
        if x.startswith("gave("): return ("gave",)
        return ("?",)
    def level(n):
        if n == len(BINARY): return primary()
        a = level(n + 1)
        while peek() in BINARY[n] or (n == 0 and peek() and peek().startswith("op9.")):
            op = take(); a = (op, a, level(n + 1))
        return a
    e = level(0)
    if pos[0] != len(t): raise ValueError(s)
    return e


OPS = {"|": lambda a, b: a | b, "^": lambda a, b: a ^ b, "&": lambda a, b: a & b,
       "==": lambda a, b: int(a == b), "!=": lambda a, b: int(a != b), "<": lambda a, b: int(a < b),
       ">": lambda a, b: int(a > b), "<=": lambda a, b: int(a <= b), ">=": lambda a, b: int(a >= b),
       "<<": lambda a, b: a << (b & 31), ">>": lambda a, b: a >> (b & 31), "+": lambda a, b: a + b,
       "-": lambda a, b: a - b, "*": lambda a, b: a * b, "/": lambda a, b: int(a / b), "%": lambda a, b: a % b}


def value(e, state):
    """The tree's value in state, or None when the listing can't tell."""
    k = e[0]
    if k == "num": return e[1]
    if k == "var": return state.var(e[2]) if e[1] == "V" else None
    if k == "&" and e[1][0] == "var" and e[1][1] == "V" and e[2][0] == "num":
        return state.bits(e[1][2], e[2][1])
    if k == "item": return None if e[1] is None or e[1] not in KEYS else state.key(e[1])
    if k == "gave": return 0
    if k == "?": return None
    if k == "!":
        v = value(e[1], state); return None if v is None else int(not v)
    if k == "neg":
        v = value(e[1], state); return None if v is None else -v
    a = value(e[1], state)
    if k == "&&":
        if a == 0: return 0
        b = value(e[2], state)
        return 0 if b == 0 else (None if a is None or b is None else 1)
    if k == "||":
        if a not in (None, 0): return 1
        b = value(e[2], state)
        return 1 if b not in (None, 0) else (None if a is None or b is None else 0)
    b = value(e[2], state)
    if a is None or b is None or k.startswith("op9."): return None
    if k in ("/", "%") and b == 0: return None
    return OPS[k](a, b)


def holds(ways, state):
    """Some way has no condition that is surely false."""
    return any(all(value(c, state) != 0 for c in w) for w in ways)


def split(s, sep):
    """s split at sep outside parentheses (a condition may hold `V0x41 | 0x1000` itself)."""
    out, depth, start, i = [], 0, 0, 0
    while i < len(s):
        if s[i] == "(": depth += 1
        elif s[i] == ")": depth -= 1
        elif depth == 0 and s.startswith(sep, i):
            out.append(s[start:i]); i += len(sep); start = i; continue
        i += 1
    return out + [s[start:]]


def tested(e, out):
    """{var: mask} the tree reads: & against a constant reads those bits, anything else the whole var."""
    k = e[0]
    if k in ("&",) and e[1][0] == "var" and e[1][1] == "V" and e[2][0] == "num":
        out[e[1][2]] = out.get(e[1][2], 0) | e[2][1]; return
    if k == "var" and e[1] == "V": out[e[2]] = -1; return
    for x in e[1:]:
        if isinstance(x, tuple): tested(x, out)


def pre(f):
    """Offsets of the writes that run before the line's step (its last field, pre=...)."""
    x = f[-1][4:] if f[-1].startswith("pre=") else ""
    return tuple(int(v, 16) for v in x.split(",") if v)


class Graph:
    def __init__(self, path):
        self.rooms = {}   # room: {"door": [...], "write": [...], "give": [...], "take": [...]}
        self.unread = set()   # conditions the parser can't read: assumed to pass
        cache = {}
        def ways(s):
            if s == "unreached": return []
            out = []
            for w in split(s, " | "):
                conds = []
                cs = [] if w == "always" else split(w, " ; ")
                # x and !x on one way: tested before and after a write in the routine; read neither.
                both = {c for c in cs if c.startswith("!") and c[1:] in cs}
                cs = [c for c in cs if c not in both and "!" + c not in both]
                for c in cs:
                    if c not in cache:
                        try: cache[c] = parse(c)
                        except (ValueError, IndexError): cache[c] = ("?",); self.unread.add(c)
                    conds.append(cache[c])
                out.append(conds)
            return out
        for line in open(path):
            f = line.rstrip("\n").split("\t")
            room, at, ent = int(f[1][1:]), int(f[2][1:], 16), f[3]
            r = self.rooms.setdefault(room, {"door": [], "write": [], "give": [], "take": [], "write_at": {}, "ent": {}})
            r["ent"][at] = int(ent[1:])
            if f[0] == "door":
                to = int(f[4][1:]) if f[4].startswith("M") else None
                if to is not None: r["door"].append((at, to, ways(f[5]), pre(f)))
            elif f[0] == "write":
                how = f[5]
                w = (at, int(f[4][1:], 16), how[0], int(how[1:], 16) & 0xFFFFFFFF, ways(f[6]), pre(f))
                r["write"].append(w); r["write_at"][at] = w
            elif f[0] == "give":
                ops = [tuple(int(x, 16) if i else int(x) for i, x in enumerate(o.split(":"))) for o in f[4].split(",")]
                item = ops[0][1] if ops and ops[0][0] == 0 else None
                r["give"].append((at, item, f[5], ways(f[6]), pre(f)))
            elif f[0] == "take":
                item = int(f[4], 16) if f[4].startswith("0x") else None
                r["take"].append((at, item, ways(f[5]), pre(f)))
        self.relevance()
        self.unlocks()
        # Rooms whose steps read V0x1: elsewhere the room left can't matter until the room writes it.
        self.reads_left = set()
        for room, r in self.rooms.items():
            found = {}
            for key in ("door", "write", "give", "take"):
                for e in r[key]:
                    for w in e[-2]:
                        for c in w: tested(c, found)
            if ROOM_LEFT in found: self.reads_left.add(room)

    def relevance(self):
        """{var: mask} of story bits the walk keeps."""
        rel = {AREA: -1, ROOM_LEFT: -1, FLAGS: -1}
        def add(ways):
            for w in ways:
                for c in w: tested(c, rel)
        for r in self.rooms.values():
            for _, _, ways, _ in r["door"]: add(ways)
            for _, item, _, ways, _ in r["give"]:
                if item in KEYS: add(ways)
            for _, item, ways, _ in r["take"]:
                if item in KEYS: add(ways)
        while True:
            before = dict(rel)
            for r in self.rooms.values():
                for _, var, kind, n, ways, _ in r["write"]:
                    if self.matters(rel, var, kind, n): add(ways)
            if rel == before: break
        self.rel = rel

    def unlocks(self):
        """{(var, bit): keys} for the story bits only steps needing those keys set (M051's Weapons Storage
        door sets V0x2d|0x40000 with the key, and its other door, for later, tests only that bit). The
        walk follows every step whatever the keys, so a place may hold such a bit before its key could be
        there: a step testing it needs the keys too. A step's keys: those every way of it needs (bits of
        this kind count, to a fixpoint). A key give's own bit, the first write after it in its routine
        (St. Francis Key M325 V0x41|0x1, which the St. Francis door tests), needs the key too: the key
        logic's key bit is the key held and its spot reached."""
        writers = {}
        for room, r in self.rooms.items():
            key_gives = sorted((at, item) for at, item, _, _, _ in r["give"] if item in KEYS)
            write_ats = sorted(at for at, *_ in r["write"])
            for at, var, kind, n, ways, _ in r["write"]:
                if kind not in "|=": continue
                given = [item for g, item in key_gives if g < at <= g + KEY_BIT_RANGE and r["ent"][g] == r["ent"][at]
                         and not any(g < w < at and r["ent"][w] == r["ent"][at] for w in write_ats)]
                if given: ways = [w + [("item", given[-1])] for w in ways or [[]]]
                for b in range(32):
                    if n >> b & 1: writers.setdefault((var, 1 << b), []).append(ways)
        self.taint = {}
        while True:
            taint = {}
            for bit, ws in writers.items():
                keys = None
                for ways in ws:
                    for w in ways or [[]]:
                        k = set()
                        for c in w: wants(c, k, taint=self.taint)
                        keys = k if keys is None else keys & k
                        if not keys: break
                    if not keys: break
                if keys: taint[bit] = frozenset(keys)
            if taint == self.taint: break
            self.taint = taint

    @staticmethod
    def matters(rel, var, kind, n):
        m = rel.get(var)
        if m is None: return False
        if kind == "|": return bool(n & m)
        if kind == "&": return bool(~n & m & 0xFFFFFFFF)
        return True


FULL = 0xFFFFFFFF


class State:
    """A place (room, V0x4a, V0x1) and its other story bits: one = bits that may be 1, zero = bits that
    may be 0, per variable (a variable not listed is 0)."""
    __slots__ = ("room", "area", "left", "one", "zero")
    def __init__(self, room, area, left, one=None, zero=None):
        self.room, self.area, self.left = room, area, left
        self.one, self.zero = dict(one or {}), dict(zero or {})
    def place(self): return (self.room, self.area, self.left)
    def copy(self, **kw):
        return State(kw.get("room", self.room), kw.get("area", self.area), kw.get("left", self.left),
                     self.one, self.zero)
    def var(self, n):
        if n == AREA: return self.area
        if n == ROOM_LEFT: return self.left
        one, zero = self.one.get(n, 0), self.zero.get(n, FULL)
        return None if one & zero else one
    def bits(self, n, mask):
        """var & mask, or None when the bits in mask aren't all known."""
        if n in (AREA, ROOM_LEFT): return self.var(n) & mask
        one, zero = self.one.get(n, 0), self.zero.get(n, FULL)
        return None if one & zero & mask else one & mask
    def key(self, k): return None   # keys may be anywhere: the step's needs say which
    def write(self, var, kind, n, rel):
        """The state after the write (a new place when it writes V0x4a or V0x1)."""
        if var in (AREA, ROOM_LEFT):
            old = self.var(var)
            v = n if kind == "=" else (old | n if kind == "|" else old & n)
            return self.copy(**{"area" if var == AREA else "left": v})
        s = self.copy()
        m = rel[var] & FULL
        one, zero = s.one.get(var, 0), s.zero.get(var, FULL)
        if kind == "=": one, zero = n & m, (~n & m) | (~m & zero)
        elif kind == "|": one, zero = one | (n & m), zero & ~(n & m)
        else: one, zero = one & (n | ~m), zero | (~n & m)
        s.one[var], s.zero[var] = one & FULL, zero & FULL
        return s
    def join(self, o):
        """Adds o's bits; True when anything changed."""
        changed = False
        for d, other, base in ((self.one, o.one, 0), (self.zero, o.zero, FULL)):
            for k in set(d) | set(other):
                v = d.get(k, base) | other.get(k, base)
                if v != d.get(k, base): d[k] = v; changed = True
        return changed


def wants(e, out, positive=True, taint=None):
    """Keys a tree needs held: item(K) read as true (not under ! or == 0), and the keys of a story bit
    only key steps set (Graph.unlocks) read as set: every bit tested set needs them."""
    k = e[0]
    if k == "item":
        if positive and e[1] in KEYS: out.add(e[1])
        return
    if k == "&" and e[1][0] == "var" and e[1][1] == "V" and e[2][0] == "num":
        bits = [taint.get((e[1][2], 1 << b)) for b in range(32) if e[2][1] >> b & 1] if positive and taint else []
        if bits and all(bits): out.update(frozenset.intersection(*bits))
        return
    if k == "!": wants(e[1], out, not positive, taint); return
    if k == "==" and e[2] == ("num", 0): wants(e[1], out, not positive, taint); return
    if k == "||": return   # either side: no single key needed
    for x in e[1:]:
        if isinstance(x, tuple): wants(x, out, positive, taint)


def needs(ways, s, taint=None):
    """The fewest keys a way that holds in s needs (frozenset), or None when no way holds."""
    best = None
    for w in ways:
        if any(value(c, s) == 0 for c in w): continue
        k = set()
        for c in w: wants(c, k, taint=taint)
        if best is None or len(k) < len(best): best = frozenset(k)
        if not best: break
    return best


def run(r, s, pre, rel):
    """s after the story writes that run before a step in its routine: each whose tests hold in s
    (tests are read on entry, before the writes), in step order."""
    t = s
    for at in pre:
        w = r["write_at"].get(at)
        if w and Graph.matters(rel, w[1], w[2], w[3]) and holds(w[4], s): t = t.write(w[1], w[2], w[3], rel)
    return t


def read_runs(path):
    """{(room, V0x4a, V0x1): (end door room or None, alive entity types, {var: value after}, fought)} from
    the mod-randomizer-run test's lines: what the room does by itself when it starts at that place. A run
    that started a battle (battle=1, or won=N: the test's win played it to a won end) has alive None and
    fought set: the test's battle is a model (a boss's later phases and scripted ends are not all in it),
    so what's left alive then is not surely what the room places, and its end is one way on, not the only
    one."""
    out = {}
    for line in open(path):
        f = dict(x.split("=", 1) for x in line.rstrip("\n").split("\t")[2:] if "=" in x)
        room = int(line.split("\t")[1][1:])
        key = (room, int(f["V0x4a"], 16), int(f["V0x1"], 16))
        if key in out or not f["end"] or f["end"].startswith("load") or f["end"] == "noroom": continue
        end = int(f["end"][6:]) if f["end"].startswith("door M") else None
        fought = bool(f.get("battle") or f.get("won"))
        alive = None if fought else {int(x) for x in f["alive"].split(",") if x}
        sets = {}
        for x in f["set"].split(","):
            if x: var, change = x.split(":"); sets[int(var[1:], 16)] = int(change.split(">")[1], 16)
        out[key] = (end, alive, sets, fought)
    return out


REACH = re.compile(r"\+([0-9A-F]+)@(-?\d+),(-?\d+)")


def read_zones(paths, runs_path):
    """({place: zones its run found cut off}, {room: {door target room: zones that open it}}). paths:
    zone-run files, comma-separated (a chain's levels). A zone run's last at= point is a reach= point
    of the run it extends (the place's own, or the level before), which names the zone. A zone some run
    of the place reached (in its reach=, any file: stepping into another zone can open floor, M070's
    +774 has the room open the way to both M072 doors) is not cut there."""
    zone_at, cut, reached = {}, {}, {}
    def lines(path):
        for line in open(path):
            yield int(line.split("\t")[1][1:]), dict(x.split("=", 1) for x in line.rstrip("\n").split("\t")[2:] if "=" in x)
    for path in [runs_path] + paths.split(","):
        for room, f in lines(path):
            got = REACH.findall(f.get("reach", ""))
            for z, x, y in got: zone_at[(room, int(x), int(y))] = int(z, 16)
            if got: reached.setdefault((room, int(f["V0x4a"], 16), int(f["V0x1"], 16)), set()).update(int(z, 16) for z, _, _ in got)
    for room, f in lines(runs_path):
        if f.get("cut"):
            place = (room, int(f["V0x4a"], 16), int(f["V0x1"], 16))
            cut.setdefault(place, set()).update(int(z, 16) for z in f["cut"].lstrip("+").split(",+"))
            cut[place] -= reached.get(place, set())
    opens = {}
    for path in paths.split(","):
        for room, f in lines(path):
            if not f["end"].startswith("door M") or not f.get("at"): continue   # a base run: reach points only
            x, y = (int(v) for v in f["at"].split(";")[-1].split(",")[:2])
            zone = zone_at.get((room, x, y))
            if zone is not None: opens.setdefault(room, {}).setdefault(int(f["end"][6:]), set()).add(zone)
    return cut, opens


def move(g, exits, t, to, what):
    """(state, what) per room a door to `to` leads to (to 0: the New York map's picks)."""
    if to:
        return [(t.copy(room=to, left=t.left if to in g.reads_left else 0), what)]
    out = []
    night = t.bits(FLAGS, 0x2000)
    nights = (0, 1) if night is None else (1 if night else 0,)
    may = t.one.get(FLAGS, 0)
    for d, bit in exits["bit"].items():
        if not may & bit: continue
        for n in nights:
            for lo, hi, room in exits["exit"].get((d, n), []):
                if lo <= t.area <= hi:
                    out.append((t.copy(room=room, left=MAP_ROOM_LEFT if room in g.reads_left else 0), f"{what} map d{d}"))
    return out


def successors(g, exits, s, runs=None, zones=None, moved=None):
    """(next state, keys the step needs, what: "door M123" / "write V0x41|0x40") per step that can run.
    With runs (the run test's results): the room's own start comes first. Its writes apply, and when the
    room leaves by itself (a cutscene), that door is the only way on; else steps of entity types the
    room didn't place are left out. A run that fought, or whose end comes back to the same place, only
    adds its end (door or writes) to the room's steps. With moved ({(room, offset): (room, V0x1)}, Hard's
    s_doors), those doors lead there from Day 2 on."""
    rel, out = g.rel, []
    r = g.rooms.get(s.room)
    if not r: return out
    info = runs.get(s.place()) if runs else None
    alive = None
    if info:
        end, alive, sets, fought = info
        t = s
        for var, v in sorted(sets.items()):
            if var in rel or var in (AREA, ROOM_LEFT): t = t.write(var, "=", v, rel)
        if end is not None:
            out = [(x, frozenset(), "start: leaves") for x, _ in move(g, exits, t, end, "start")]
        elif t.place() != s.place():
            out = [(t, frozenset(), "start: writes")]
        # A run that comes back to its own place (a door into the room itself, M249: it only changed
        # bits) is not the only way on: the run was made with the place's sure bits, the bits it set are
        # joined in now, and the room's steps read them.
        if out and not fought and all(x.place() != s.place() for x, _, _ in out): return out
    def placed(at): return alive is None or r["ent"].get(at) in alive
    for at, var, kind, n, ways, pre in r["write"]:
        if not Graph.matters(rel, var, kind, n) or not placed(at): continue
        k = needs(ways, s, g.taint)
        if k is not None: out.append((run(r, s, pre, rel).write(var, kind, n, rel), k, f"+{at:06X} V{var:#x}{kind}{n:#x}"))
    cut = zones[0].get(s.place(), set()) if zones else set()
    opens = zones[1].get(s.room, {}) if cut else {}
    for at, to, ways, pre in r["door"]:
        if not placed(at): continue
        if to in opens and opens[to] <= cut: continue   # every zone that opens it is cut off here
        k = needs(ways, s, g.taint)
        if k is None: continue
        if moved and (s.room, at) in moved and s.area >= DAY2:
            to, came = moved[(s.room, at)]
            t = run(r, s, pre, rel)
            out.append((t.copy(room=to, left=came if to in g.reads_left else 0), k, f"+{at:06X} moved door"))
            continue
        for x, what in move(g, exits, run(r, s, pre, rel), to, f"+{at:06X} door"): out.append((x, k, what))
    return out


def stays(g, runs, s, at):
    """Whether the player can stand at place s with step at's entity there: no run says otherwise, the
    room neither leaves nor changes place by itself, and it placed that entity type."""
    info = runs.get(s.place()) if runs else None
    if not info: return True
    end, alive, sets, fought = info
    if fought: return True
    if end is not None: return False
    if any(sets.get(v, s.var(v)) != s.var(v) for v in (AREA, ROOM_LEFT)): return False
    return alive is None or g.rooms[s.room]["ent"].get(at) in alive


def walk(g, exits, start, runs=None, zones=None, moved=None):
    """{place: State} for every place reached, the order places were first reached, and per place its
    edges {next place: [(keys needed, what)]}."""
    first = State(start, 0, 0)
    table, order, succ = {first.place(): first}, [first.place()], {}
    todo, queued, head = [first.place()], {first.place()}, 0
    while head < len(todo):
        p = todo[head]; head += 1; queued.discard(p)
        edges = {}
        for t, k, what in successors(g, exits, table[p], runs, zones, moved):
            q = t.place()
            edges.setdefault(q, []).append((k, what))
            if q not in table: table[q] = t; order.append(q); changed = True
            else: changed = table[q].join(t)
            if changed and q not in queued: todo.append(q); queued.add(q)
        succ[p] = edges
    return table, order, succ


def spot_items(path):
    """{spot: vanilla item} from pe_rand_spots.h."""
    out = {}
    for line in open(path):
        m = re.match(r"\s*\{ (0x[0-9a-fA-F]+), PE_RAND_\w+,.*/\* (\d+):", line)
        if m: out[int(m.group(2))] = int(m.group(1), 16)
    return out


def gives_at(g, table, order, vanilla=None, runs=None):
    """{place: [(item, keys needed, room, offset, spot)]}: the key gives (immediate key items) and the
    table's spots that can run there; a spot's item is its vanilla one from vanilla {spot: item}."""
    out = {}
    for p in order:
        s = table[p]; r = g.rooms.get(s.room)
        if not r: continue
        for at, item, spot, ways, _ in r["give"]:
            if item not in KEYS and spot == "-": continue
            if not stays(g, runs, s, at): continue
            if spot != "-" and vanilla: item = vanilla.get(int(spot.split()[0][5:]), item)
            k = needs(ways, s, g.taint)
            if k is not None: out.setdefault(p, []).append((item, k, s.room, at, spot))
    return out


def solve(start, succ, gives, item_at):
    """Places reached holding only keys found on the way: item_at(room, offset, item) is the item a give
    hands out (the layout). Returns {place: order reached} and {key: place first held}."""
    reached, held, first = {start: 0}, set(), {}
    frontier, waiting = [start], []
    while frontier or waiting:
        if not frontier:
            # Keys picked up since: retry the steps that waited for one.
            again = [(p, q) for p, q, k in waiting if k <= held and q not in reached]
            waiting = [(p, q, k) for p, q, k in waiting if not k <= held]
            if not again: break
            for p, q in again:
                if q not in reached: reached[q] = len(reached); frontier.append(q)
            continue
        p = frontier.pop(0)
        for item, k, room, at, spot in gives.get(p, ()):
            got = item_at(room, at, item)
            if got in KEYS and k <= held and got not in held: held.add(got); first[got] = p
        for q, es in succ.get(p, {}).items():
            if q in reached: continue
            k = min((k for k, _ in es), key=len)
            if k <= held: reached[q] = len(reached); frontier.append(q)
            else: waiting.append((p, q, k))
    return reached, first


def hexes(vals):
    return ",".join(f"{v:#x}" for v in sorted(vals))


def keyname(k): return KEYS.get(k, f"{k:#x}")


def main():
    a = sys.argv[1:]
    if len(a) < 2: sys.exit(__doc__)
    opt = {k: a[a.index(k) + 1] for k in ("--start", "--states", "--spots", "--runs", "--zones") if k in a}
    g = Graph(a[0]); exits = read_exits(a[1])
    runs = read_runs(opt["--runs"]) if "--runs" in opt else None
    zones = read_zones(opt["--zones"], opt["--runs"]) if "--zones" in opt else None
    start = int(opt.get("--start", "M431").lstrip("Mm"))
    table, order, succ = walk(g, exits, start, runs, zones)
    rel = ", ".join(f"V{v:#x}" + ("" if m == -1 else f"&{m:#x}") for v, m in sorted(g.rel.items()))
    print(f"# places {len(order)}; story bits kept: {rel}")
    if g.unread: print(f"# conditions not read (assumed to pass): {len(g.unread)}")
    first, rooms_at = {}, {}
    for i, p in enumerate(order):
        first.setdefault(p[1], i)
        rooms_at.setdefault(p[1], set()).add(p[0])
    setters = {}
    for room, r in g.rooms.items():
        for _, var, kind, n, _, _ in r["write"]:
            if var == AREA and kind == "=": setters.setdefault(n, set()).add(room)
    gives = gives_at(g, table, order, spot_items(opt["--spots"]) if "--spots" in opt else None, runs)
    vanilla, held = solve(order[0], succ, gives, lambda room, at, item: item)
    print("\n## Keys at their vanilla gives, picked up on the way: where each is first held")
    for k in sorted(KEYS):
        p = held.get(k)
        print(f"held\t{k:#x}\t{KEYS[k]}\t" + (f"M{p[0]:03d} V0x4a={p[1]:#x}" if p else "never"))
    v_first, v_rooms = {}, {}
    for p, i in sorted(vanilla.items(), key=lambda x: x[1]):
        v_first.setdefault(p[1], i); v_rooms.setdefault(p[1], set()).add(p[0])
    print("\n## Vanilla keys: story values in the order first reached, rooms open while each holds")
    for area in sorted(v_first, key=v_first.get):
        st = " ".join(f"M{x:03d}" for x in sorted(setters.get(area, ())))
        print(f"vanilla\t{area:#x}\tset in: {st or '-'}\topen: " + " ".join(f"M{x:03d}" for x in sorted(v_rooms[area])))
    early = {}
    for p in order:
        if p not in vanilla: early.setdefault(p[1], set()).add(p[0])
    print("\n## Rooms open at a story value only when a key comes early (not with vanilla keys): sequence-break risk")
    for area in sorted(early, key=lambda a: v_first.get(a, 1 << 30)):
        extra = early[area] - v_rooms.get(area, set())
        if extra: print(f"early\t{area:#x}\t" + " ".join(f"M{x:03d}" for x in sorted(extra)))
    print("\n## Story values (V0x4a) in the order first reached, any key anywhere: value, rooms that set it, rooms open")
    for area in sorted(first, key=first.get):
        st = " ".join(f"M{x:03d}" for x in sorted(setters.get(area, ())))
        op = " ".join(f"M{x:03d}" for x in sorted(rooms_at[area]))
        print(f"area\t{area:#x}\tset in: {st or '-'}\topen: {op}")
    never = sorted(set(g.rooms) - {p[0] for p in order})
    print(f"\n## Rooms never reached ({len(never)}): " + " ".join(f"M{x:03d}" for x in never))
    print("\n## Steps that need a key: room, step, keys, V0x4a values where they can run")
    locks = {}
    for p in order:
        for q, es in succ[p].items():
            for k, what in es:
                if k: locks.setdefault((p[0], what, k), set()).add(p[1])
    for (room, what, k), vals in sorted(locks.items()):
        print(f"lock\tM{room:03d}\t{what}\t{' + '.join(keyname(x) for x in sorted(k))}\t{hexes(vals)}")
    print("\n## Gives: room, offset, item (var = the chest's own), spot, V0x4a values it can run at, keys it needs")
    at_area = {}
    for p in order:
        s = table[p]; r = g.rooms.get(s.room)
        if not r: continue
        for at, item, spot, ways, _ in r["give"]:
            k = needs(ways, s, g.taint) if stays(g, runs, s, at) else None
            if k is not None:
                vals, ks = at_area.setdefault((s.room, at), (set(), set()))
                vals.add(s.area); ks.add(k)
    for room in sorted(g.rooms):
        for at, item, spot, ways, _ in g.rooms[room]["give"]:
            it = keyname(item) if item is not None else "var"
            vals, ks = at_area.get((room, at), (None, ()))
            need = " | ".join(" + ".join(keyname(x) for x in sorted(k)) for k in sorted(ks, key=len) if k) if ks and frozenset() not in ks else ""
            print(f"give\tM{room:03d}\t+{at:06X}\t{it}\t{spot}\t{hexes(vals) if vals else 'unreached'}\t{need}")
    if "--states" in opt:
        index = {p: i for i, p in enumerate(order)}
        with open(opt["--states"], "w") as f:
            for i, p in enumerate(order):
                s = table[p]
                bits = " ".join(f"V{n:#x}={s.one.get(n, 0):#x}/{s.zero.get(n, FULL) & (g.rel[n] & FULL):#x}"
                                for n in sorted(set(s.one) | set(s.zero)))
                nx = " ".join(f"{index[q]}" + "".join(f"[{'+'.join(f'{x:#x}' for x in sorted(k))}]" for k, _ in es if k)
                              for q, es in sorted(succ.get(p, {}).items(), key=lambda x: index[x[0]]))
                f.write(f"{i}\tM{p[0]:03d}\tV0x4a={p[1]:#x}\tV0x1={p[2]:#x}\t{bits}\t{nx}\n")


if __name__ == "__main__":
    main()
