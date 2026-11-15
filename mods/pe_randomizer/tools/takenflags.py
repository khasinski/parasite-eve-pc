"""(offsets are task-block offsets, as story-writes.txt has them) Taken flags shared by gives: each 'sets Vx|bit' write conditioned on gave(...) belongs to the nearest give
before it in the same room. Prints flags owned by more than one give place, with the spots the table gives them.
python -I takenflags.py randomizer-spots.txt story-writes.txt"""
import sys, re, collections
gives = collections.defaultdict(list)   # room -> [(offset, spot or None)]
for l in open(sys.argv[1]):
    p = l.rstrip("\n").split("\t")
    if p[0] != "give": continue
    gives[p[1]].append((int(p[4], 16), p[6][5:] if len(p) > 6 and p[6].startswith("spot=") else "-"))
owners = collections.defaultdict(set)
for l in open(sys.argv[2]):
    m = re.match(r"(M\d+) \+([0-9A-F]+) e\d+ sets (V0x[0-9a-f]+)\|(0x[0-9a-f]+)\t(.*)", l.rstrip("\n"))
    if not m or "gave(" not in m.group(5): continue
    room, at = m.group(1), int(m.group(2), 16)
    before = [g for g in gives[room] if 0 < at - g[0] <= 0x100]
    if not before: continue
    off, spot = max(before)
    owners[(m.group(3), m.group(4))].add((room, off, spot))
for (var, bit), xs in sorted(owners.items()):
    if len(xs) < 2: continue
    spots = {s for _, _, s in xs}
    flag = "  <- DIFFERENT SPOTS" if len(spots - {"-"}) > 1 else ""
    print(f"{var}|{bit}: " + ", ".join(f"{r}+{o:06X} spot={s}" for r, o, s in sorted(xs)) + flag)
