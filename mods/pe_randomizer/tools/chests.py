"""Every pickup that gives through an actor slot: list its candidate item values
(AssignValue A0, imm / A[k], imm found before the give, back to the previous give step or 0x1400 bytes).
items = values that reach the give through actor slots (the pickup's own item; two values = a random roll).
vars = values that reach it through a global variable: chests keep the item Aya swapped out on a full
bag there and give that instead (e.g. V0x79 in M030), so 0 in vars is "nothing left behind", not an item.
A line marked ?? has no item, or more than the two items of a roll: check it with dis.py."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dis as D
if len(sys.argv) != 2: sys.exit("usage: python -I chests.py <disc.bin>")
path = sys.argv[1]
def is_give(b, i):
    h = struct.unpack_from("<I", b, i)[0]
    return (h & 0x1FFF) == 167 and ((h >> 13) & 15) == 2
for m in range(1, D.ROOMS + 1):
    if m == 360: continue  # debug room: gives every key
    b = D.room(path, m)
    for g in range(0, len(b) - 24, 4):
        h = struct.unpack_from("<I", b, g)[0]
        if not is_give(b, g) or ((h >> 17) & 7) != 1: continue
        src = struct.unpack_from("<I", b, g + 8)[0]
        # what feeds the give slot: A[src] = A[x] or immediate; stop at the previous pickup's give
        lo = max(0, g - 0x1400)
        lo = next((i + 4 for i in range(g - 4, lo - 1, -4) if is_give(b, i)), lo)
        feeds = {(1, src): "A"}; vals = {"A": set(), "V": set()}
        for _ in range(3):
            for i in range(lo, g, 4):
                hh, tail = struct.unpack_from("<II", b, i)
                if (hh & 0x1FFF) != 10 or ((hh >> 13) & 15) != 2 or tail != 0 or (hh >> 23): continue
                m0, m1 = (hh >> 17) & 7, (hh >> 20) & 7
                d0, d1 = struct.unpack_from("<II", b, i + 8)
                via = feeds.get((m0, d0))
                if via is None: continue
                if m1 in (1, 2, 4): feeds.setdefault((m1, d1), "V" if m1 == 2 else via)
                elif m1 == 0 and d1 < 0x200: vals[via].add(d1)
        items = sorted(vals["A"])
        flag = "" if 1 <= len(items) <= 2 and 0 not in items else " ??"
        var = " ".join(f"V{d:#x}" for (mm, d) in feeds if mm == 2)
        print(f"M{m:03d} +{g:06X} slot{src} items={items} vars={var or '-'}:{sorted(vals['V'])}{flag}")
