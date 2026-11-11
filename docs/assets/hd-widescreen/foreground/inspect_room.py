"""Render a room view's native layers (per entry, with tile depths) and its HD widescreen side art.

usage: python3 inspect_room.py ROOM GROUP PACK_DIR OUT_DIR
"""
import json, struct, sys
from pathlib import Path
import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[2] / 'widescreen'
sys.path.insert(0, str(REPO))
import survey  # noqa: E402
u32, u16 = survey.u32, survey.u16
DISC = '/Users/hasik/Projects/parasite-pc/disc/Parasite Eve (Disc 1).bin'


def upload(vram, b):
    directory = u32(b, 4); p = u32(b, directory + 0x28); count = p >> 22; offset = p & 0x3fffff
    for i in range(count):
        q = offset + 20 * i; source = u32(b, q + 4); rect = u32(b, q + 8); primary = source & 0xffffff
        for j, src, r in [(0, source, rect), (1, u32(b, q + 12), u32(b, q + 16))]:
            off = src & 0xffffff
            if j and not off: continue
            if j: off += primary
            height = src >> 24
            if not j and not height: height = 256
            width = r & 1023; x = (r >> 10) & 2047; y = r >> 21
            if not height or not width: continue
            a = np.frombuffer(b, dtype='<u2', count=width * height, offset=off).reshape(height, width)
            vram[y:y + height, x:x + width] = a


def to_rgba(p):
    rgb = np.stack([(p & 31) * 255 // 31, ((p >> 5) & 31) * 255 // 31, ((p >> 10) & 31) * 255 // 31], -1).astype(np.uint8)
    return np.dstack((rgb, ((p != 0) * 255).astype(np.uint8)))


def native_layers(room_id, group):
    cat = json.loads((REPO / 'catalog.json').read_text())
    room = next(r for r in cat['rooms'] if r['id'] == room_id and r['disc'] == 1)
    view = next(v for v in room['views'] if v['group'] == group)
    d = survey.Disc(DISC); img = d.find('PE.IMG')
    start, scratch, tex, sectors = room['start_sector'], room['scratch_sectors'], room['texture_sectors'], room['room_sectors']
    blob = d.read(img, (start + scratch + tex) * 2048, sectors * 2048)
    di = u32(blob, 4); rp = u32(blob, di + 0x1c) & 0x3fffff; off = u32(blob, rp + 4) & 0xffffff; length = u32(blob, rp) & 0xffffff
    g = blob[off:off + length]; ep = u32(g, 20); entries = u16(g, 6)
    vram = np.zeros((512, 1024), dtype='<u2')
    upload(vram, d.read(img, start * 2048, scratch * 2048)); upload(vram, d.read(img, (start + scratch) * 2048, tex * 2048))
    vb = vram.view(np.uint8).reshape(512, 2048)
    layers = []
    for e in range(entries):
        m = ep + 56 * e
        if g[m + 36] != group: continue
        pp = m + u32(g, m + 40); uv = m + u32(g, m + 44); sx, sy = struct.unpack_from('<hh', g, m + 12)
        count = u16(g, m + 38); depth = (u32(g, m) >> 8) & 4095
        tiles = []
        for t in range(count):
            a = u32(g, pp + 4 * t); word = u32(g, uv + 8 * t); u = g[uv + 8 * t + 4]; v = g[uv + 8 * t + 3]
            x = sx + (a >> 22); y = sy + ((a >> 12) & 1023)
            tx = (word >> 10) & 0x3c0; ty = (word >> 7) & 256; cx = (word >> 5) & 0x3f0; cy = word & 511
            idx = vb[(ty + np.arange(v, v + 16) % 256) % 512][:, (tx * 2 + np.arange(u, u + 16) % 256) % 2048]
            pixels = vram[cy, (cx + idx.astype(np.uint16)) % 1024]
            tiles.append({'x': int(x), 'y': int(y), 'depth': int(depth + (a & 4095)), 'pixels': pixels})
        layers.append({'entry': e, 'flags': g[m], 'depth': int(depth), 'tiles': tiles})
    return view, layers


def compose(view, tiles, w=None, h=None):
    w = w or view['width']; h = h or view['height']
    out = np.zeros((h, w), dtype=np.uint16)
    for t in sorted(tiles, key=lambda t: t['depth'], reverse=True):
        x, y, p = t['x'], t['y'], t['pixels']
        x0, y0, x1, y1 = max(0, x), max(0, y), min(w, x + 16), min(h, y + 16)
        if x0 >= x1 or y0 >= y1: continue
        q = p[y0 - y:y1 - y, x0 - x:x1 - x]; dst = out[y0:y1, x0:x1]; dst[q != 0] = q[q != 0]
    return out


def read_wide(path):
    b = Path(path).read_bytes(); assert b[:4] == b'PEW1'
    groups = struct.unpack_from('<I', b, 4)[0]; o = 8; out = {}
    for _ in range(groups):
        group, count = struct.unpack_from('<II', b, o); key = struct.unpack_from('<Q', b, o + 8)[0]
        l, r, t, bo = struct.unpack_from('<iiii', b, o + 16); o += 32
        xy = struct.unpack_from('<%dh' % (count * 2), b, o); o += count * 4
        out[group] = {'key': '%016x' % key, 'bounds': (l, r, t, bo), 'xy': [(xy[i * 2], xy[i * 2 + 1]) for i in range(count)]}
    assert o == len(b)
    return out


def atlas_cells(pack, key):
    p = Path(pack) / (key + '.webp')
    im = Image.open(p if p.exists() else Path(pack) / (key + '.png')).convert('RGBA')
    a = np.asarray(im); cols = a.shape[1] // 68
    hd_h = a.shape[0] if p.exists() else a.shape[0] // 2
    rows = hd_h // 68
    return [a[(c // cols) * 68 + 2:(c // cols) * 68 + 66, (c % cols) * 68 + 2:(c % cols) * 68 + 66] for c in range(cols * rows)]


if __name__ == '__main__':
    room, group, pack, out = sys.argv[1], int(sys.argv[2]), sys.argv[3], Path(sys.argv[4])
    out.mkdir(parents=True, exist_ok=True)
    view, layers = native_layers(room, group)
    alltiles = [t for L in layers if L['flags'] & 2 for t in L['tiles']]
    Image.fromarray(to_rgba(compose(view, alltiles))).save(out / f'{room}-g{group}-native.png')
    for L in layers:
        ds = [t['depth'] for t in L['tiles']]
        xs = [t['x'] for t in L['tiles']]
        print(f"entry {L['entry']} flags={L['flags']} depth={L['depth']} tile depths {min(ds)}..{max(ds)} x {min(xs)}..{max(xs)+16} tiles={len(ds)}")
        Image.fromarray(to_rgba(compose(view, L['tiles']))).save(out / f"{room}-g{group}-e{L['entry']}.png")
    manifest = json.loads((Path(pack) / 'manifest.json').read_text())
    w = next(x for x in manifest['widescreen'] if x['room'] == room and x['group'] == group)
    wide = read_wide(Path(pack) / f"{w['geometry_key']}-wide.bin")[group]
    cells = atlas_cells(pack, wide['key'])
    l, r, t, b = wide['bounds']
    canvas = np.zeros(((b - t) * 4, (r - l) * 4, 4), np.uint8)
    for i, (x, y) in enumerate(wide['xy']):
        X, Y = (x - l) * 4, (y - t) * 4
        c = cells[i][:max(0, min(64, canvas.shape[0] - Y)), :max(0, min(64, canvas.shape[1] - X))]
        canvas[Y:Y + c.shape[0], X:X + c.shape[1]] = c
    Image.fromarray(canvas).save(out / f'{room}-g{group}-sides-hd.png')
    print('wide bounds', wide['bounds'], 'cells', len(wide['xy']), 'view', view['width'], 'x', view['height'])
