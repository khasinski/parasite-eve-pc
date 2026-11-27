"""Print what an RE2 PC player file (PLxx.PLD) holds: skeleton, mesh and texture. Read only.

    python -I tools/pld_info.py input/data/re2/pld1/claire.re2/PL01.PLD

PLD layout: u32 directory offset, u32 count (4); the directory lists EDD (animations), EMR
(skeleton), MD1 (mesh), TIM (texture).
EMR: u16 armature offset, u16 frames offset, u16 bone count, u16 frame size; bone offsets (3 x s16)
from byte 8; armature = per bone (u16 child count, u16 offset of its child list), child bytes.
MD1: u32 length, u32 0, u32 object count (a triangle and a quad object per part); each object is
7 x u32 (vertex offset, vertex count, normal offset, normal count, face offset, face count,
texture offset), offsets from the end of the 12-byte header.
"""
import struct, sys


def main(path):
    b = open(path, 'rb').read()
    d, _ = struct.unpack_from('<II', b, 0)
    edd, emr, md1, tim = struct.unpack_from('<4I', b, d)

    arm, _, count, size = struct.unpack_from('<4H', b, emr)
    parent = [-1] * count
    for i in range(count):
        nc, off = struct.unpack_from('<HH', b, emr + arm + 4 * i)
        for c in b[emr + arm + off:emr + arm + off + nc]:
            parent[c] = i
    rel = [struct.unpack_from('<3h', b, emr + 8 + 6 * i) for i in range(count)]
    print(path)
    print('  skeleton: %d bones, %d-byte frames' % (count, size))
    for i in range(count):
        print('    bone %2d  parent %2d  offset %s' % (i, parent[i], rel[i]))

    objs = struct.unpack_from('<I', b, md1 + 8)[0]
    tris = quads = 0
    print('  mesh: %d parts' % (objs // 2))
    for i in range(objs // 2):
        t = struct.unpack_from('<7I', b, md1 + 12 + 56 * i)
        q = struct.unpack_from('<7I', b, md1 + 12 + 56 * i + 28)
        print('    part %2d  %3d vertices  %3d triangles  %3d quads' % (i, t[1], t[5], q[5]))
        tris += t[5]; quads += q[5]
    print('    total %d triangles, %d quads' % (tris, quads))

    flag = struct.unpack_from('<I', b, tim + 4)[0]
    bpp = {0: 4, 1: 8, 2: 16, 3: 24}[flag & 3]
    at, s = tim + 8, '%d-bit' % bpp
    if flag & 8:
        ln, _, _, cw, ch = struct.unpack_from('<I4H', b, at)
        s += ', %d palettes of %d colours' % (ch, cw)
        at += ln
    _, _, _, w, h = struct.unpack_from('<I4H', b, at)
    print('  texture: %s, %dx%d pixels' % (s, w * 16 // bpp, h))


if __name__ == '__main__':
    for p in sys.argv[1:]:
        main(p)
