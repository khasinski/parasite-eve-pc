"""Read an RE2 PC player model (PLxx.PLD): skeleton, mesh, texture. Read only.

Layout (pld_info.py has the header details):
  EMR  bone offsets (x, y, z s16 from byte 8), tree from the armature lists.
  MD1  per part a triangle and a quad object, each 7 x u32 (offsets from the end of the 12-byte
       header): vertices (x, y, z, pad s16), normals (same), faces (triangle: n0 v0 n1 v1 n2 v2;
       quad: n0 v0 n1 v1 n2 v2 n3 v3, u16), texture records (triangle: u0 v0 clut, u1 v1 page,
       u2 v2 pad; quad: + u3 v3 pad, bytes / u16 as the PSX).
  TIM  8-bit image with its palettes.
Vertices are in their bone's frame, with no rest rotation: a bone's rest position is the sum of
the offsets up its chain.
"""
import struct


def load(path):
    b = open(path, 'rb').read()
    d, _ = struct.unpack_from('<II', b, 0)
    edd, emr, md1, tim = struct.unpack_from('<4I', b, d)

    arm, _, count, _ = struct.unpack_from('<4H', b, emr)
    parent = [-1] * count
    for i in range(count):
        nc, off = struct.unpack_from('<HH', b, emr + arm + 4 * i)
        for c in b[emr + arm + off:emr + arm + off + nc]:
            parent[c] = i
    rel = [struct.unpack_from('<3h', b, emr + 8 + 6 * i) for i in range(count)]
    pos = []
    for i in range(count):
        p = parent[i]
        base = pos[p] if p >= 0 else (0, 0, 0)
        pos.append(tuple(base[k] + rel[i][k] for k in range(3)))

    objs = struct.unpack_from('<I', b, md1 + 8)[0]
    base = md1 + 12
    parts = []
    for i in range(objs // 2):
        part = dict(tris=[], quads=[])
        for kind, at in (('tris', base + 56 * i), ('quads', base + 56 * i + 28)):
            vo, vc, no, nc, fo, fc, to = struct.unpack_from('<7I', b, at)
            V = [struct.unpack_from('<3h', b, base + vo + 8 * k) for k in range(vc)]
            N = [struct.unpack_from('<3h', b, base + no + 8 * k) for k in range(nc)]
            n = 3 if kind == 'tris' else 4
            for k in range(fc):
                idx = struct.unpack_from('<%dH' % (2 * n), b, base + fo + 4 * n * k)
                uv = struct.unpack_from('<' + 'BBH' * n, b, base + to + 4 * n * k)
                part[kind].append(dict(
                    v=[V[idx[2 * j + 1]] for j in range(n)],
                    n=[N[idx[2 * j]] for j in range(n)],
                    uv=[(uv[3 * j], uv[3 * j + 1]) for j in range(n)],
                    clut=uv[2], page=uv[5]))
        parts.append(part)

    flag = struct.unpack_from('<I', b, tim + 4)[0]
    if flag & 3 != 1 or not flag & 8: raise ValueError('expected an 8-bit TIM with palettes')
    ln, _, _, cw, ch = struct.unpack_from('<I4H', b, tim + 8)
    pal = [list(struct.unpack_from('<%dH' % cw, b, tim + 20 + 2 * cw * r)) for r in range(ch)]
    at = tim + 8 + ln
    _, _, _, w, h = struct.unpack_from('<I4H', b, at)
    image = [list(b[at + 12 + 2 * w * y: at + 12 + 2 * w * (y + 1)]) for y in range(h)]
    return dict(parent=parent, rel=rel, pos=pos, parts=parts, pal=pal, image=image)


def rgb(c):
    """PSX 15-bit colour -> (r, g, b) 0..255, None for transparent (0)."""
    if c == 0: return None
    return ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)


if __name__ == '__main__':
    import sys
    m = load(sys.argv[1])
    faces = [f for p in m['parts'] for k in ('tris', 'quads') for f in p[k]]
    print('bones', len(m['pos']), 'rest positions', m['pos'])
    print('faces', len(faces), 'cluts', sorted({f['clut'] for f in faces}), 'pages', sorted({f['page'] for f in faces}))
    print('u range', min(u for f in faces for u, v in f['uv']), max(u for f in faces for u, v in f['uv']),
          'v range', min(v for f in faces for u, v in f['uv']), max(v for f in faces for u, v in f['uv']))
    print('image', len(m['image'][0]), 'x', len(m['image']), 'palettes', len(m['pal']))
    for i, p in enumerate(m['parts']):
        vs = [v for k in ('tris', 'quads') for f in p[k] for v in f['v']]
        if vs: print('part', i, 'faces', len(p['tris']) + len(p['quads']), 'x', min(v[0] for v in vs), max(v[0] for v in vs),
                     'y', min(v[1] for v in vs), max(v[1] for v in vs), 'z', min(v[2] for v in vs), max(v[2] for v in vs))
