"""Build a depth-sorted foreground side layer (PEW2 kind 1) for one room view.

build_front.py ROOM GROUP SRC_PACK OUT_PACK MASKDIR STRIP:ENTRY [STRIP:ENTRY ...]
STRIP is left|right (MASKDIR/ROOM-STRIP-mask.png, 224 HD px wide, from cut.py);
ENTRY is the native foreground entry whose seam tiles give the depths.
OUT_PACK is a hard-linked copy of SRC_PACK whose ROOM sidecar is rewritten as PEW2.
An optional ROOM-STRIP-depth.json selects a local surface with entry and anchors
[[native_x,native_y,native_depth], ...]. Every anchor must exist in the original
entry; three non-collinear anchors define its depth plane. Use this only after
reviewing a surface whose neighbouring tiles have discontinuous depths."""
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
depth_guides = {}
for side, entry in strips:
    guide_path = maskdir / f'{room}-{side}-depth.json'
    if not guide_path.exists(): continue
    guide = json.loads(guide_path.read_text())
    anchors = guide.get('anchors', [])
    if guide.get('entry') != entry or len(anchors) < 3:
        raise ValueError('Depth guide needs an entry and at least three native anchors')
    tiles = next(L for L in layers if L['entry'] == entry)['tiles']
    native = {(tt['x'], tt['y'], tt['depth']) for tt in tiles}
    if any(len(a) != 3 or any(type(v) is not int for v in a) or tuple(a) not in native for a in anchors):
        raise ValueError('Depth guide anchor does not match an original tile')
    matrix = np.array([[x, y, 1] for x, y, _ in anchors], dtype=float)
    values = np.array([d for _, _, d in anchors], dtype=float)
    coefficients, _, rank, _ = np.linalg.lstsq(matrix, values, rcond=None)
    if rank != 3 or np.max(np.abs(matrix @ coefficients - values)) > 2:
        raise ValueError('Depth guide anchors do not describe a stable local plane')
    phase = (anchors[0][0] % 16, anchors[0][1] % 16)
    if any((x % 16, y % 16) != phase for x, y, _ in anchors):
        raise ValueError('Depth guide anchors do not share the native tile grid')
    depth_guides[side] = dict(coefficients=coefficients, path=guide_path, phase=phase)

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
    if side in depth_guides:
        d = int(round(float(depth_guides[side]['coefficients'] @ [x, y, 1])))
        if not 1 <= d <= 4095:
            raise ValueError('Depth guide extrapolation leaves the PEW2 foreground range')
        return d
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
positions = [(x, y, 'left' if x < 0 else 'right') for x, y in wide['xy']
             if ('left' if x < 0 else 'right') not in depth_guides]
for side, guide in depth_guides.items():
    # Match the source foreground grid, which may be offset from the backdrop.
    # M208's rail starts at y=23: using the backdrop's y=0 grid would move its
    # depth boundaries by seven pixels across the original-picture seam.
    gx, gy = guide['phase']
    lo, hi = (l, 0) if side == 'left' else (320, r)
    positions.extend((x, y, side) for y in range((t-gy)//16*16+gy, b, 16)
                     for x in range((lo-gx)//16*16+gx, hi, 16))
for x, y, side in positions:
    if side not in entry_of: continue
    X, Y = (x - l) * 4, (y - t) * 4
    x0, y0 = max(0, X), max(0, Y)
    x1, y1 = min(canvas.shape[1], X+64), min(canvas.shape[0], Y+64)
    if x0 >= x1 or y0 >= y1: continue
    rgba = np.zeros((64, 64, 4), np.uint8)
    rgba[y0-Y:y1-Y, x0-X:x1-X] = canvas[y0:y1, x0:x1]
    rgba[y0-Y:y1-Y, x0-X:x1-X, 3] = alpha[y0:y1, x0:x1]
    if not rgba[..., 3].any(): continue
    front_xy.append((x, y)); front_cells.append(rgba)
    front_depth.append(seam_depth(entry_of[side], side, x, y))

# Atlas: 16 columns of 68x68 cells (2 px replicated gutter); PNG holds HD over a reference half.
cols = 16; rows = (len(front_cells) + cols - 1) // cols
half = rows * 68
atlas = np.zeros((half * 2, cols * 68, 4), np.uint8)
for i, c in enumerate(front_cells):
    X, Y = (i % cols) * 68, (i // cols) * 68
    padded = np.pad(c, ((2, 2), (2, 2), (0, 0)), mode='edge')
    # Extension commands use binary alpha testing (<0.5 is discarded), not
    # alpha blending. Keep transparent samples below that threshold but nonzero:
    # the GPU loader otherwise extrudes foreground RGB into alpha-zero pixels,
    # replacing the coherent background RGB needed by bilinear edge samples.
    padded[..., 3] = np.where(padded[..., 3] == 0, 1, padded[..., 3])
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
    mask_sha256=digest(maskdir/f'{room}-{side}-mask.png'),
    **(dict(depth_guide_sha256=digest(depth_guides[side]['path']),
           native_grid_phase=list(depth_guides[side]['phase'])) if side in depth_guides else {}))
    for side,entry in strips]))
manifest['widescreen_foreground'] = fronts
manifest_path = dst/'manifest.json'; temporary = manifest_path.with_suffix('.partial')
temporary.write_text(json.dumps(manifest,indent=2)+'\n'); temporary.replace(manifest_path)
print(f'{room} g{group}: {len(front_xy)} foreground cells, atlas {key:016x}, depths',
      sorted(set(front_depth))[:6], '...', max(front_depth))
