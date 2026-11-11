"""Zone runs for the mod-randomizer-run test: per place that stays (end=idle), one runs line per zone
Aya can walk to (the run's reach= points) and facing, with at=x,z,angle: the place's own line plus where
she stands. The test then says which door, if any, that zone opens. Zone runs hold every key (keys=all):
which key a door needs is storywalk's part, from doorscan's ways.
Usage: python -I zoneruns.py <runs.txt> <randomizer-run.txt> > zone-runs.txt
(randomizer-run.txt from runall.sh: the outputs in runs order, without the runs that left none.)
Then: runall.sh test-mod-randomizer-run.exe zone-runs.txt zone-run.txt

Chains: python -I zoneruns.py <zone-runs.txt> <zone-run.txt> <runs.txt> <randomizer-run.txt> > zone-runs2.txt
takes the level before as parent: a zone run that stays and tested zones its parent didn't (a routine
started by stepping into the zone) gets one more stop per new zone. Only the facing-0 run of a stop goes
on (facing picks a door, not what starts). Repeat with level 2 as runs and level 1 as parent, and so on.

--press (first level): the zone runs also press the examine / talk button at each stop (the test's
press). Facing then picks what starts too (a talk needs Aya to face the speaker), so in a press chain
every facing that tested new zones goes on, not only facing 0."""
import re
import sys

ANGLES = (0, 0x400, 0x800, 0xC00)   # the four quarters door tests split facing into
REACH = re.compile(r"\+([0-9A-F]+)@(-?\d+),(-?\d+)")


def outputs(runs_path, out_path):
    """[(runs line, output fields)] for the lines that ran. Outputs come in runs order; a runs line that
    left none (it crashed, or isn't a room: M5000) is passed over. Matched on (room, V0x4a, V0x1, at=),
    not on <out>.crashed: that has only the text, and runs lines can repeat."""
    out, pairs = open(out_path), []
    head = out.readline()
    for line in (x.rstrip("\n") for x in open(runs_path) if x.strip() and not x.startswith("#")):
        if not same(line, head): continue
        pairs.append((line, dict(x.split("=", 1) for x in head.rstrip("\n").split("\t")[2:] if "=" in x)))
        head = out.readline()
    if head: sys.exit(f"{out_path}: output left over at {head[:60]!r}: not from {runs_path}?")
    return pairs


def same(line, out_line):
    """Whether out_line is the test's output for runs line `line`."""
    w, f = line.split(), out_line.rstrip("\n").split("\t")
    if len(f) < 4 or f[1] != f"M{int(w[0][1:]):03d}": return False
    if int(f[2].split("=")[1], 16) != int(w[1], 16) or int(f[3].split("=")[1], 16) != int(w[2], 16): return False
    at = ";".join(x[3:] for x in w if x.startswith("at="))
    fields = dict(x.split("=", 1) for x in f[4:] if "=" in x)
    return fields.get("at", "") == at and ("press" in w) == ("press" in fields)


def plain(words):
    """A runs line without keys=all and press, so a zone run finds its place's line."""
    return " ".join(w for w in words if w not in ("keys=all", "press"))


def main():
    args = [a for a in sys.argv[1:] if a != "--press"]
    if len(args) < 2: sys.exit(__doc__)
    parent = {}
    if len(args) >= 4:
        for line, f in outputs(args[2], args[3]):
            parent[plain(line.split())] = {z for z, _, _ in REACH.findall(f.get("reach", ""))}
    for line, f in outputs(args[0], args[1]):
        if f.get("end") != "idle": continue
        words = line.split()
        stops = [w for w in words if w.startswith("at=")]
        if parent:
            if not stops or ("press" not in words and stops[-1].split(",")[2] != str(ANGLES[0])): continue
            known = parent.get(plain(words[:len(words) - 1 - words[::-1].index(stops[-1])]))
            if known is None: continue
        else:
            known = set()
            if "keys=all" not in words: line += " keys=all"
            if "--press" in sys.argv[1:] and "press" not in words: line += " press"
        been = {",".join(w[3:].split(",")[:2]) for w in stops}
        for z, x, y in REACH.findall(f.get("reach", "")):
            if z in known or f"{x},{y}" in been: continue
            for angle in ANGLES: print(f"{line} at={x},{y},{angle}")


if __name__ == "__main__":
    main()
