"""HD silhouettes for a room's static foreground entries, from the HD art itself.

hd_matte.py ROOM GROUP SRC_PACK OUT_PACK ENTRY [ENTRY ...]
The native silhouette is stair-stepped and the upscaled art inside those stairs often shows the
background (it was painted from the whole scene), so the native coverage pastes background-coloured
steps over actors. This builds the room's HD composite, then cuts each ENTRY with GrabCut inside a
band from 2 native px inside to 1 px outside its native outline, and writes the entry's atlas as RGBA PNG plus a PEM1 static-matte marker so the
silhouette may leave the native coverage. Only for static entries (no animation controllers) with
a full background beneath. Writes OUT_PACK/../matte-ROOM-eE.png overlays for review."""
import json, os, sys
from pathlib import Path
import numpy as np, cv2
from PIL import Image
import inspect_room as ir

room, group, src, dst = sys.argv[1], int(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4])
entries = [int(e) for e in sys.argv[5:]]
manifest = json.loads((src / 'manifest.json').read_text())
if not dst.exists():
    dst.mkdir(parents=True)
    for f in src.iterdir():
        if f.is_file(): os.link(f, dst / f.name)
view, layers = ir.native_layers(room, group)
W, H = view['width'], view['height']


def atlas_of(e):
    rec = next(a for a in manifest['atlases'] if a['room'] == room and a['group'] == group and a['entry'] == e)
    hd = np.asarray(Image.open(src / f"{rec['key']}.webp").convert('RGB'))
    ref = np.asarray(Image.open(src / f"{rec['key']}.ref.png").convert('RGB'))
    return rec['key'], hd, ref


# HD composite of every drawn entry, painter's order by tile depth (far first).
canvas = np.zeros((H * 4, W * 4, 3), np.uint8)
cells = []
for L in layers:
    if not L['flags'] & 2: continue
    key, hd, _ = atlas_of(L['entry']); cols = hd.shape[1] // 68
    for i, t in enumerate(L['tiles']):
        cells.append((t['depth'], t['x'], t['y'], t['pixels'], hd[(i // cols) * 68 + 2:(i // cols) * 68 + 66, (i % cols) * 68 + 2:(i % cols) * 68 + 66]))
for d, x, y, p, c in sorted(cells, key=lambda c: -c[0]):
    m = np.repeat(np.repeat(p != 0, 4, 0), 4, 1)
    X0, Y0 = x * 4, y * 4
    sub = canvas[max(0, Y0):Y0 + 64, max(0, X0):X0 + 64]
    cm = m[max(0, -Y0):max(0, -Y0) + sub.shape[0], max(0, -X0):max(0, -X0) + sub.shape[1]]
    cc = c[max(0, -Y0):max(0, -Y0) + sub.shape[0], max(0, -X0):max(0, -X0) + sub.shape[1]]
    sub[cm] = cc[cm]

import hashlib, struct
under_alpha = np.zeros((H * 4, W * 4), np.uint8)   # stair pixels the cuts give up, to draw behind the room
behind = np.zeros((H, W), bool)                      # native coverage of every other drawn entry
for L in layers:
    if L['flags'] & 2 and L['entry'] not in entries:
        for t in L['tiles']:
            x0, y0 = max(0, t['x']), max(0, t['y'])
            p = t['pixels'][y0 - t['y']:y0 - t['y'] + H - y0, x0 - t['x']:x0 - t['x'] + W - x0]
            behind[y0:y0 + p.shape[0], x0:x0 + p.shape[1]] |= p != 0
behind4 = np.repeat(np.repeat(behind, 4, 0), 4, 1)
for e in entries:
    key, hd, ref = atlas_of(e); cols = hd.shape[1] // 68; rows = hd.shape[0] // 68
    tiles = next(L for L in layers if L['entry'] == e)['tiles']
    native = np.zeros((H, W), np.uint8)
    for t in tiles:
        x0, y0 = max(0, t['x']), max(0, t['y'])
        p = t['pixels'][y0 - t['y']:y0 - t['y'] + H - y0, x0 - t['x']:x0 - t['x'] + W - x0]
        native[y0:y0 + p.shape[0], x0:x0 + p.shape[1]] |= (p != 0).astype(np.uint8)
    native4 = np.repeat(np.repeat(native, 4, 0), 4, 1)
    # Removal up to 2 native px inside the outline; growth at most 1 px outside it, so a cut
    # cannot claim shadow or street beneath an object and hide an actor's feet.
    sure_fg = cv2.erode(native4, np.ones((17, 17), np.uint8)); sure_bg = cv2.dilate(native4, np.ones((9, 9), np.uint8)) == 0
    m = np.where(native4 > 0, cv2.GC_PR_FGD, cv2.GC_PR_BGD).astype(np.uint8)
    m[sure_fg > 0] = cv2.GC_FGD; m[sure_bg] = cv2.GC_BGD
    bgd = np.zeros((1, 65)); fgd = np.zeros((1, 65))
    cv2.grabCut(cv2.cvtColor(canvas, cv2.COLOR_RGB2BGR), m, None, bgd, fgd, 6, cv2.GC_INIT_WITH_MASK)
    fg = ((m == cv2.GC_FGD) | (m == cv2.GC_PR_FGD)).astype(np.uint8)
    fg = cv2.morphologyEx(fg, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))
    alpha = cv2.GaussianBlur(fg.astype(np.float32), (0, 0), 0.8)
    alpha = np.clip(alpha * 255 + 0.5, 0, 255).astype(np.uint8)
    # Overlay for review: red where the native layer covered but the HD cut says background.
    ov = canvas.copy(); lost = (native4 > 0) & (fg == 0); gained = (native4 == 0) & (fg > 0)
    ov[lost] = (ov[lost] * 0.3 + np.array([255, 0, 0]) * 0.7).astype(np.uint8)
    ov[gained] = (ov[gained] * 0.3 + np.array([0, 255, 0]) * 0.7).astype(np.uint8)
    Image.fromarray(ov).save(dst.parent / f'matte-{room}-e{e}.png')
    atlas = np.zeros((rows * 136, cols * 68, 4), np.uint8)
    for i, t in enumerate(tiles):
        X0, Y0 = t['x'] * 4, t['y'] * 4
        a = np.zeros((64, 64), np.uint8)
        xs0, ys0 = max(0, -X0), max(0, -Y0)
        sub = alpha[max(0, Y0):Y0 + 64, max(0, X0):X0 + 64]
        a[ys0:ys0 + sub.shape[0], xs0:xs0 + sub.shape[1]] = sub
        c = hd[(i // cols) * 68 + 2:(i // cols) * 68 + 66, (i % cols) * 68 + 2:(i % cols) * 68 + 66]
        ax, ay = (i % cols) * 68, (i // cols) * 68
        atlas[ay:ay + 68, ax:ax + 68] = np.pad(np.dstack((c, a)), ((2, 2), (2, 2), (0, 0)), mode='edge')
        r = np.dstack((ref[ay + 2:ay + 66, ax + 2:ax + 66], np.full((64, 64), 255, np.uint8)))
        atlas[rows * 68 + ay:rows * 68 + ay + 68, ax:ax + 68] = np.pad(r, ((2, 2), (2, 2), (0, 0)), mode='edge')
    under_alpha[(native4 > 0) & (alpha < 255) & ~behind4] = 255
    for suffix in ('.webp', '.ref.png', '.png'):
        f = dst / (key + suffix)
        if f.exists(): f.unlink()
    Image.fromarray(atlas).save(dst / f'{key}.png')
    marker = dst / f'{key}-matte.bin'
    if marker.exists(): marker.unlink()
    marker.write_bytes(b'PEM1')
    print(f'{room} e{e} {key}: native px {int((native4 > 0).sum())}, HD cut removes {int(lost.sum())}, adds {int(gained.sum())}')

# Underlay: where a cut removed native pixels with nothing of the room behind them, the old
# pixels (they already show the wall or street) are drawn behind everything instead of over actors.
geom = next(w['geometry_key'] for w in manifest['widescreen'] if w['room'] == room and w['group'] == group)
under_alpha = cv2.dilate(under_alpha, np.ones((5, 5), np.uint8))
cells_xy, cells_img = [], []
for ty in range(0, H, 16):
    for tx in range(0, W, 16):
        a = under_alpha[ty * 4:ty * 4 + 64, tx * 4:tx * 4 + 64]
        if a.shape != (64, 64) or not a.any(): continue
        cells_xy.append((tx, ty)); cells_img.append(np.dstack((canvas[ty * 4:ty * 4 + 64, tx * 4:tx * 4 + 64], a)))
if cells_xy:
    ucols = 16; urows = (len(cells_img) + ucols - 1) // ucols
    atlas = np.zeros((urows * 136, ucols * 68, 4), np.uint8)
    for i, c in enumerate(cells_img):
        X, Y = (i % ucols) * 68, (i // ucols) * 68
        padded = np.pad(c, ((2, 2), (2, 2), (0, 0)), mode='edge')
        atlas[Y:Y + 68, X:X + 68] = padded; atlas[urows * 68 + Y:urows * 68 + Y + 68, X:X + 68] = padded
    ukey = int.from_bytes(hashlib.sha256(atlas.tobytes() + b'pe-under').digest()[:8], 'little') or 1
    Image.fromarray(atlas).save(dst / f'{ukey:016x}.png')
    side = dst / f'{geom}-under.bin'
    assert not (src / f'{geom}-under.bin').exists(), 'room already has an underlay: merge it by hand'
    if side.exists(): side.unlink()
    out = bytearray(b'PEW1' + struct.pack('<I', 1)) + struct.pack('<IIQiiii', group, len(cells_xy), ukey, 0, W, 0, H)
    out += b''.join(struct.pack('<hh', *p) for p in cells_xy)
    side.write_bytes(out)
    print(f'underlay: {len(cells_xy)} cells, {int((under_alpha > 0).sum())} px, atlas {ukey:016x}')
