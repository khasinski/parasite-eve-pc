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
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prepare import read_sidecar, encode_sidecar, digest

room, group, src, dst, maskdir = sys.argv[1], int(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]), Path(sys.argv[5])
strips = [(a.split(':')[0], int(a.split(':')[1])) for a in sys.argv[6:]]
manifest = json.loads((src / 'manifest.json').read_text())
w = next(x for x in manifest['widescreen'] if x['room'] == room and x['group'] == group)
geom = w['geometry_key']

if not dst.exists():
    dst.mkdir(parents=True)
    for f in src.iterdir():
        if f.is_file(): os.link(f, dst / f.name)

wide_all = read_sidecar(src / f'{geom}-wide.bin')
wide = next(r for r in wide_all if r['group'] == group and r.get('kind', 0) == 0)
wide = dict(wide, xy=wide['positions'])
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
atlas_path = dst / f'{key:016x}.png'
temporary = atlas_path.with_suffix('.partial.png')
Image.fromarray(atlas).save(temporary); temporary.replace(atlas_path)

# Replace this view's foreground only; preserve all other groups and depths.
if not front_xy:
    raise ValueError('Foreground masks contain no side cells')
records = [r for r in wide_all if not (r['group'] == group and r.get('kind', 0) == 1)]
xs = [p[0] for p in front_xy]; ys = [p[1] for p in front_xy]
records.append(dict(group=group, key=f'{key:016x}', kind=1,
    bounds=[min(xs),max(xs)+1,min(ys),max(ys)+1], positions=front_xy, depths=front_depth))
sidecar = dst / f'{geom}-wide.bin'
temporary = sidecar.with_suffix('.partial')
temporary.write_bytes(encode_sidecar(records)); temporary.replace(sidecar)
# A development pack must pass the same payload checks as an installed pack.
files = {atlas_path.name:'widescreen-foreground-atlas',sidecar.name:'widescreen-index'}
extras = [r for r in manifest.get('extras',[]) if r['file'] not in files]
extras.extend(dict(file=name,kind=kind,sha256=digest(dst/name)) for name,kind in files.items())
manifest['extras'] = extras
fronts = [r for r in manifest.get('widescreen_foreground',[])
          if (r['geometry_key'],r['group']) != (geom,group)]
fronts.append(dict(room=room,group=group,geometry_key=geom,key=f'{key:016x}',
    cells=len(front_xy),strips=[dict(side=side,entry=entry,
    mask_sha256=digest(maskdir/f'{room}-{side}-mask.png')) for side,entry in strips]))
manifest['widescreen_foreground'] = fronts
manifest_path = dst/'manifest.json'; temporary = manifest_path.with_suffix('.partial')
temporary.write_text(json.dumps(manifest,indent=2)+'\n'); temporary.replace(manifest_path)
print(f'{room} g{group}: {len(front_xy)} foreground cells, atlas {key:016x}, depths',
      sorted(set(front_depth))[:6], '...', max(front_depth))
