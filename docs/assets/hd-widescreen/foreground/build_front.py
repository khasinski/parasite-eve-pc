"""Build a depth-sorted foreground side layer (PEW2 kind 1) for one room view.

build_front.py ROOM GROUP SRC_PACK OUT_PACK MASKDIR STRIP:ENTRY [STRIP:ENTRY ...]
STRIP is left|right (MASKDIR/ROOM-STRIP-mask.png, 224 HD px wide, from cut.py);
ENTRY is the native foreground entry whose seam tiles give the depths.
OUT_PACK is a hard-linked copy of SRC_PACK whose ROOM sidecar is rewritten as PEW2."""
import hashlib, json, os, shutil, struct, sys
from pathlib import Path
import numpy as np
from PIL import Image
import inspect_room as ir

room, group, src, dst, maskdir = sys.argv[1], int(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]), Path(sys.argv[5])
strips = [(a.split(':')[0], int(a.split(':')[1])) for a in sys.argv[6:]]
manifest = json.loads((src / 'manifest.json').read_text())
w = next(x for x in manifest['widescreen'] if x['room'] == room and x['group'] == group)
geom = w['geometry_key']

if not dst.exists():
    dst.mkdir(parents=True)
    for f in src.iterdir():
        if f.is_file(): os.link(f, dst / f.name)

wide_all = ir.read_wide(src / f'{geom}-wide.bin')
wide = wide_all[group]
l, r, t, b = wide['bounds']
cells = ir.atlas_cells(src, wide['key'])
canvas = np.zeros(((b - t) * 4, (r - l) * 4, 4), np.uint8)
for i, (x, y) in enumerate(wide['xy']):
    X, Y = (x - l) * 4, (y - t) * 4
    c = cells[i][:max(0, min(64, canvas.shape[0] - Y)), :max(0, min(64, canvas.shape[1] - X))]
    canvas[Y:Y + c.shape[0], X:X + c.shape[1]] = c
alpha = np.zeros(canvas.shape[:2], np.uint8)
view, layers = ir.native_layers(room, group)
entry_of = {}
for side, entry in strips:
    m = np.asarray(Image.open(maskdir / f'{room}-{side}-mask.png').convert('L')) > 127
    x0 = 0 if side == 'left' else (320 - l) * 4
    alpha[:m.shape[0], x0:x0 + 224][m] = 255
    entry_of[side] = entry
# Never draw over the original 4:3 picture: that area belongs to the native layers.
alpha[:, (0 - l) * 4:(320 - l) * 4] = 0


def seam_depth(entry, side, x, y):
    """Native depth of ENTRY's tiles nearest the seam in tile row y, extrapolated linearly to x."""
    tiles = next(L for L in layers if L['entry'] == entry)['tiles']
    rows = sorted({tt['y'] for tt in tiles}, key=lambda ty: abs(ty - y))
    for ty in rows:
        row = sorted((tt['x'], tt['depth']) for tt in tiles if tt['y'] == ty)
        edge = row[:2] if side == 'left' else row[-2:]
        if side == 'left' and edge[0][0] > 15: continue
        if side == 'right' and edge[-1][0] + 16 < 304: continue
        if len(edge) == 2 and edge[1][0] != edge[0][0]:
            slope = (edge[1][1] - edge[0][1]) / (edge[1][0] - edge[0][0])
            ref = edge[0] if side == 'left' else edge[1]
            d = ref[1] + slope * (x - ref[0])
        else:
            d = edge[0][1]
        # Stay within the range the layer itself uses; never jump behind the room.
        ds = [tt['depth'] for tt in tiles]
        return int(round(min(max(d, min(ds) - 16, 9), max(ds))))
    raise SystemExit('no seam tiles for entry %d' % entry)


front_xy, front_depth, front_cells = [], [], []
for x, y in wide['xy']:
    X, Y = (x - l) * 4, (y - t) * 4
    a = alpha[Y:Y + 64, X:X + 64]
    if a.shape != (64, 64) or not a.any(): continue
    side = 'left' if x < 0 else 'right'
    if side not in entry_of: continue
    rgba = canvas[Y:Y + 64, X:X + 64].copy(); rgba[..., 3] = a
    front_xy.append((x, y)); front_cells.append(rgba)
    front_depth.append(seam_depth(entry_of[side], side, x, y))

# Atlas: 16 columns of 68x68 cells (2 px replicated gutter); PNG holds HD over a reference half.
cols = 16; rows = (len(front_cells) + cols - 1) // cols
half = rows * 68
atlas = np.zeros((half * 2, cols * 68, 4), np.uint8)
for i, c in enumerate(front_cells):
    X, Y = (i % cols) * 68, (i // cols) * 68
    padded = np.pad(c, ((2, 2), (2, 2), (0, 0)), mode='edge')
    atlas[Y:Y + 68, X:X + 68] = padded
    atlas[half + Y:half + Y + 68, X:X + 68] = padded
key_bytes = hashlib.sha256(atlas.tobytes() + b'pe-front').digest()[:8]
key = int.from_bytes(key_bytes, 'little') or 1
Image.fromarray(atlas).save(dst / f'{key:016x}.png')

# PEW2: every original record as a kind-0 backdrop, then the foreground layer.
out = bytearray(b'PEW2' + struct.pack('<I', len(wide_all) + 1))
for g, rec in wide_all.items():
    out += struct.pack('<IIQiiiiI', g, len(rec['xy']), int(rec['key'], 16), *rec['bounds'], 0)
    out += b''.join(struct.pack('<hh', *p) for p in rec['xy'])
xs = [p[0] for p in front_xy]; ys = [p[1] for p in front_xy]
out += struct.pack('<IIQiiiiI', group, len(front_xy), key, min(xs), max(xs) + 1, min(ys), max(ys) + 1, 1)
out += b''.join(struct.pack('<hh', *p) for p in front_xy)
out += b''.join(struct.pack('<H', d) for d in front_depth)
sidecar = dst / f'{geom}-wide.bin'
if sidecar.exists(): sidecar.unlink()   # a hard link to the source pack: never write through it
sidecar.write_bytes(out)
print(f'{room} g{group}: {len(front_xy)} foreground cells, atlas {key:016x}, depths',
      sorted(set(front_depth))[:6], '...', max(front_depth))
