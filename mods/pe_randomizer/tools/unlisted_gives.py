"""Sort the GiveItem steps the randomizer table leaves alone: the "-" give lines of randomizer-spots.txt
(mod-randomizer-rooms ctest). python -I unlisted_gives.py build-mods/mods/randomizer-spots.txt mods/pe_randomizer/tools"""
import sys, collections
sys.path.insert(0, sys.argv[2])
from spottable import SKIP_MAPS, GIFT_MAPS, FIXED_KEYS, CUT_KEYS, area, AREA_NAMES
from keyscan import KEYS

rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1]) if l.startswith("give")]
listed_maps = {int(r[1][1:]) for r in rows if r[-1] != "-" and r[-2].startswith("spot=")}
cats = collections.defaultdict(list)
for r in rows:
    if r[-1] != "-": continue
    m = int(r[1][1:]); ops = [tuple(int(x, 16) if x.startswith("0x") else int(x) for x in o.split(":")) for o in r[5].split(",")]
    mode, val = ops[0]
    if m in SKIP_MAPS:
        c = "skip-map (Chrysler/Wayne/debug)"
    elif mode == 0 and val in FIXED_KEYS | CUT_KEYS:
        c = "fixed/cut key"
    elif mode == 0 and val in KEYS:
        c = "key gift not listed"
    elif mode == 0 and m in GIFT_MAPS:
        c = "gift map, immediate not listed"
    elif mode == 0:
        c = "immediate give, map not in GIFT_MAPS"
    elif mode == 1:
        c = "slot give (chest/pickup) not listed"
    else:
        c = f"mode {mode} give"
    cats[c].append((m, r[2], mode, val, r[3]))
for c, xs in sorted(cats.items(), key=lambda kv: -len(kv[1])):
    maps = sorted({x[0] for x in xs})
    print(f"\n== {c}: {len(xs)} steps in {len(maps)} rooms")
    if c.startswith("skip-map"): print("   rooms:", " ".join(f"M{m:03d}" for m in maps)); continue
    vals = collections.Counter(x[3] for x in xs if x[2] == 0)
    if vals: print("   items:", ", ".join(f"{v:#04x}{'('+KEYS[v]+')' if v in KEYS else ''} x{n}" for v, n in vals.most_common()))
    for x in xs[:400]:
        print(f"   M{x[0]:03d} {x[1]} ent{x[4]} {'A' if x[2]==1 else 'N' if x[2]==4 else ''}{x[3]:#x} {AREA_NAMES[area(x[0])]}")
