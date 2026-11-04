"""Everything the story walk needs from one pass over the mod-randomizer-rooms ctest's listing: per room,
each door, story variable write, GiveItem (op 167) and item take (op 233 command 0x458), with the ways
to reach it (doorscan's analysis: " | " between ways, " ; " between the conditions of one).
Usage: python -I storygraph.py <randomizer-rooms.txt> <randomizer-spots.txt> > story-graph.txt [M107 ...]
Lines (tab separated):
  door   M127 +0007DC e0 M148 <ways>
  write  M129 +000ABC e0 V0x42 |0x4 <ways>       (|mask, &mask or =value)
  give   M139 +001234 e2 <operands> spot=85 chest <ways>   (spot from randomizer-spots.txt, or -)
  take   M144 +001970 e0 0xce <ways>
Each line ends with "pre=" and the offsets of the story writes in the straight run of steps before it
(back to the first step two paths reach): a routine tests, then writes, then takes the door, so the
door's tests are read before those writes (M036: tests V0x4a == 0x78, sets 0x80, leaves).
Block offsets as in the listing (doors.txt, story-writes.txt). A full run takes a few minutes."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from doorscan import Room, bit_op, expr, load, operand, show

TAKE = 0x458


def spots_of(path):
    """{(room, block offset): "spot=N kind"} from the give lines of randomizer-spots.txt."""
    out = {}
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        if f[0] == "give" and len(f) > 6 and f[6] != "-": out[(int(f[1][1:]), int(f[4], 16))] = f"{f[6]} {f[7]}"
    return out


def is_write(r, at):
    t, op, a, _ = r.steps[at]
    return (op == 10 and len(a) == 2 and a[0][0] == 2) or (op == 42 and len(a) == 2 and a[0][0] == 2 and a[1][0] == 0)


def before(r, at):
    """Story writes in the straight run of steps before at (back through single predecessors, to the
    first step two paths reach): what a routine writes after its tests and before the step."""
    out, cur, seen = [], at, {at}
    while True:
        p = r.preds.get(cur, [])
        if len(p) != 1 or p[0][0] in seen: break
        cur = p[0][0]; seen.add(cur)
        if is_write(r, cur): out.append(cur)
    return ",".join(f"{x:#x}" for x in sorted(out))


def scan(rooms, spots, only=None):
    for room in sorted(rooms):
        if only and room not in only: continue
        r = Room(*rooms[room])
        for at in sorted(r.steps):
            line = step(r, room, at, spots)
            if line: yield f"{line}	pre={before(r, at)}"


def step(r, room, at, spots):
    """The line for step at, or None when it is none of the four."""
    t, op, a, target = r.steps[at]
    head = f"M{room:03d}\t+{at:06X}\te{t}"
    if op == 49:
        to = f"M{target:03d}" if target is not None else operand(*a[0])
        return f"door\t{head}\t{to}\t{show(r.ways(at))}"
    elif op == 10 and len(a) == 2 and a[0][0] == 2:
        how = bit_op(r, at, a[1], a[0])
        if how is None:
            e = expr(r.steps, r.preds, at, a[1], r.writers)
            how = "=" + e if e.startswith("0x") else None
        if how: return f"write\t{head}\tV{a[0][1]:#x}\t{how}\t{show(r.ways(at))}"
    elif op == 42 and len(a) == 2 and a[0][0] == 2 and a[1][0] == 0:
        return f"write\t{head}\tV{a[0][1]:#x}\t|{1 << (a[1][1] & 31):#x}\t{show(r.ways(at))}"
    elif op == 167:
        ops = ",".join(f"{m}:{v:#x}" for m, v in a)
        return f"give\t{head}\t{ops}\t{spots.get((room, at), '-')}\t{show(r.ways(at))}"
    elif op == 233 and len(a) > 1 and a[0] == (0, TAKE):
        return f"take\t{head}\t{operand(*a[1])}\t{show(r.ways(at))}"


if __name__ == "__main__":
    if len(sys.argv) < 3: sys.exit(__doc__)
    only = {int(x.lstrip("Mm")) for x in sys.argv[3:]} or None
    for line in scan(load(sys.argv[1]), spots_of(sys.argv[2]), only): print(line)
