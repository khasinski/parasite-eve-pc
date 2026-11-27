"""chests.py's item trace, but over walked steps only (randomizer-rooms.txt): for each slot GiveItem, the
immediates that reach its slot through op 10 AssignValue in the same entity, back to the previous give.
python -I walkfeeds.py randomizer-rooms.txt randomizer-spots.txt"""
import sys, collections
want = collections.defaultdict(list)   # (room, entity) -> [(give block offset, spot, slot)]
for l in open(sys.argv[2]):
    p = l.rstrip("\n").split("\t")
    if p[0] != "give": continue
    m0, v0 = p[5].split(",")[0].split(":")
    if m0 == "1": want[(p[1], p[3])].append((int(p[4], 16), p[6] if len(p) > 6 and p[6] != "-" else "-", int(v0, 16)))
steps = collections.defaultdict(list)
for l in open(sys.argv[1]):
    if not l.startswith("step"): continue
    p = l.rstrip("\n").split("\t")
    if (p[1], p[2]) in want: steps[(p[1], p[2])].append((int(p[3], 16), int(p[4]), p[5] if len(p) > 5 else ""))
for key, gs in sorted(want.items()):
    ss = sorted(steps[key])
    for g, spot, slot in gs:
        lo = max([a for a, op, _ in ss if op == 167 and a < g] + [0])
        assigns = [(a, args) for a, op, args in ss if op == 10 and lo < a < g]
        feeds = {(1, slot): "A"}; items = {}; vars_ = set()
        for _ in range(3):
            for a, args in assigns:
                (m0, d0), (m1, d1) = [(int(x.split(":")[0]), int(x.split(":")[1], 16)) for x in args.split(",")[:2]]
                via = feeds.get((m0, d0))
                if via is None: continue
                if m1 in (1, 2, 4):
                    feeds.setdefault((m1, d1), "V" if m1 == 2 else via)
                    if m1 == 2: vars_.add(d1)
                elif m1 == 0 and via == "A": items[a] = d1
        print(f"{key[0]} e{key[1]} {g:#06x} {spot:8s} items=[{', '.join(f'{v:#04x}' for v in sorted(set(items.values())))}] "
              f"sites={','.join(f'{a:#x}' for a in sorted(items))} vars={','.join(f'V{v:#x}' for v in sorted(vars_)) or '-'}")
