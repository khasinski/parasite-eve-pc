"""Pickups in different rooms that share a left-behind-item variable: likely one chest in several
room versions, which the seed map must treat as one spot. A hint, not proof: confirm that the
"opened" flag matches too (dis.py) before merging. Usage: python -I spotgroups.py chests-d1.txt"""
import collections, re, sys
if len(sys.argv) != 2: sys.exit(__doc__)
by = collections.defaultdict(list)
for line in open(sys.argv[1]):
    m = re.match(r"(M\d+) \+(\w+) slot\d+ items=(\[.*?\]) vars=(.*?):", line)
    if not m: sys.exit(f"not chests.py output: {line.strip()!r}")
    for v in m.group(4).split():
        if v != "-": by[v].append(f"{m.group(1)}+{m.group(2)}{m.group(3)}")
for v, spots in sorted(by.items(), key=lambda kv: kv[1]):
    if len({s[:4] for s in spots}) > 1: print(v, " ".join(spots))
