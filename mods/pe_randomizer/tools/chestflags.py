"""Opened flags shared by chests: a 'sets Vx|bit' with no gave() condition belongs to the first give within
0x100 bytes after it (chests set their flag before the give). Flags owned by several give places are printed.
python -I chestflags.py randomizer-spots.txt story-writes.txt   (task-block offsets)"""
import sys, re, collections
gives = collections.defaultdict(list)
for l in open(sys.argv[1]):
    p = l.rstrip("\n").split("\t")
    if p[0] == "give": gives[p[1]].append((int(p[4], 16), p[6][5:] if len(p) > 6 and p[6].startswith("spot=") else "-"))
owners = collections.defaultdict(set)
for l in open(sys.argv[2]):
    m = re.match(r"(M\d+) \+([0-9A-F]+) e\d+ sets (V0x[0-9a-f]+)\|(0x[0-9a-f]+)\t(.*)", l.rstrip("\n"))
    if not m or "gave(" in m.group(5): continue
    room, at = m.group(1), int(m.group(2), 16)
    after = [g for g in gives[room] if 0 < g[0] - at <= 0x100]
    if after: owners[(m.group(3), m.group(4))].add((room,) + min(after))
for (var, bit), xs in sorted(owners.items()):
    if len(xs) < 2: continue
    spots = {s for _, _, s in xs}
    flag = "  <- DIFFERENT SPOTS" if len(spots - {"-"}) > 1 else ("  <- part listed" if "-" in spots and len(spots) > 1 else "")
    print(f"{var}|{bit}: " + ", ".join(f"{r}+{o:06X} spot={s}" for r, o, s in sorted(xs)) + flag)
