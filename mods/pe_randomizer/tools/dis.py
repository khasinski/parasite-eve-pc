"""Disassemble room script around an offset: python dis.py disc map hexoff [before] [after]
(before/after are byte counts: decimal or 0x hex; default 0x200 / 0x80)"""
import os, struct, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from keyscan import Disc, layout, ROOMS
NAMES = {}
_here = os.path.dirname(os.path.abspath(__file__))
# ops.json beside this file, else the repo's docs/SCRIPT_OPCODE_TABLE.json (same entries)
for _path in (os.path.join(_here, "ops.json"), os.path.join(_here, "..", "..", "..", "docs", "SCRIPT_OPCODE_TABLE.json")):
    try:
        _table = json.load(open(_path))
        for r in _table["entries"] if isinstance(_table, dict) else _table: NAMES[r["opcode"]] = r.get("function") or "?"
        break
    except (OSError, ValueError, KeyError, TypeError): pass
if not NAMES and __name__ == "__main__": print("no opcode table found; opcode names show as ?", file=sys.stderr)
_discs = {}
def room(path, mapno):
    """Room script of M<mapno>; b"" for a map with no room script."""
    if not 1 <= mapno <= ROOMS: raise ValueError(f"map {mapno} is outside M001-M{ROOMS}")
    if path not in _discs: d = Disc(path); _discs[path] = (d,) + layout(d)[1:]
    d, img, table = _discs[path]
    start, c = struct.unpack_from("<II", table, (mapno - 1) * 8); s, t, r = c & 255, (c >> 8) & 4095, c >> 20
    return d.read(img + start + s + t, r * 2048)
def decode(blob, i):
    h, tail = struct.unpack_from("<II", blob, i); op, n = h & 0x1FFF, (h >> 13) & 15
    modes = h >> 17; a = []
    for k in range(n):
        if k == 5: modes = tail
        a.append((modes & 7, struct.unpack_from("<I", blob, i + 8 + 4 * k)[0])); modes >>= 3
    return op, a, 8 + 4 * n
def fmt(a):
    p = {0: "", 1: "A", 2: "V", 3: "T", 4: "N"}
    return ", ".join(f"{p.get(m, '?'+str(m))}{v:#x}" if m else f"{v:#x}" for m, v in a)
def listing(blob, target, before, after):
    if target % 4 or not 0 <= target < len(blob): raise ValueError(f"offset {target:#x} is not a step start inside this {len(blob):#x}-byte script")
    start = target
    for s in range(max(0, target - before), target, 4):
        i, ok = s, True
        while i < target:
            op, a, n = decode(blob, i)
            if op == 0 and not a or op > 400: ok = False; break
            i += n
        if ok and i == target: start = s; break
    i = start; out = []
    while i < min(target + after, len(blob) - 8 - 4 * 15):
        op, a, n = decode(blob, i)
        out.append(f"+{i:06X} {op:3d} {NAMES.get(op, '?'):28s} {fmt(a)}"); i += n
    return out
if __name__ == "__main__":
    if len(sys.argv) < 4: sys.exit(__doc__)
    b = room(sys.argv[1], int(sys.argv[2]))
    if not b: sys.exit(f"M{int(sys.argv[2]):03d} has no room script")
    print("\n".join(listing(b, int(sys.argv[3], 16), int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x200, int(sys.argv[5], 0) if len(sys.argv) > 5 else 0x80)))
