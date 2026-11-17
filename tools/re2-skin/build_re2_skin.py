"""Build a Parasite Eve Wardrobe pack from an RE2 PC player model (PLxx.PLD), offline.

Unlike the bridge's build_skin.py (a PE character on its own skeleton, Aya's animations retargeted
to it), the RE2 mesh goes onto Aya's own 29-part skeleton: each RE2 part is placed over the Aya bone
it matches in Aya's idle pose (row 0x15 frame 0) and its vertices are stored in that bone's frame.
Aya's animations then play unchanged, so model.bin and day1-8.bin carry Aya's own rows and rooms.bin
only needs the rows rooms install for her other two skeletons (31-part evening dress, 23-part long
dress), retargeted by build_skin.

Placement per part: a segment on each side (joint -> next joint, or joint -> mesh centre for the
hand, foot and head); the RE2 part is turned so its segment lies along Aya's, scaled by the
hip-height ratio and stretched along the segment so the joints meet (not for end pieces).

Texture: RE2's 256x256 (two 128-wide 8-bit pages, a 256-colour palette each) becomes the player's
4-bit page (256x255) with up to 32 palettes of 16: faces that share texels form a group, each group
gets its own 16 colours.

    python -I tools/re2-skin/build_re2_skin.py <biorand>/data/re2/pld1/claire.re2/PL01.PLD [--check]

Output: a Wardrobe pack folder (pack.json, LICENSE, characters/<folder>/ with character.json,
model.bin, day1-8.bin, rooms.bin, texture.bin, meta.bin) to copy into <preferences>/packs/.
Built from the player's own files; the result is Capcom's model and must not be shared.
See README.md.
"""
import argparse, json, math, os, re, struct, sys
sys.path.insert(0, os.path.dirname(__file__))
import pe_disc
pe_disc.install()
import render_models as R
import retarget as T
import build_skin as B
import re2_model

HERE = os.path.dirname(os.path.abspath(__file__))
OBJECT_C = os.path.join(HERE, '..', '..', 'src', 'main', 'render', 'object.c')   # g_RenderNormalTable
AYA_ENTRY = 20          # player bundle, jacket (29 parts)
AYA_TEX = 15            # player texture blob
IDLE_ROW = 0x15
WALK_ROW = 0x16

# RE2 part -> (Aya bone it is bound to, RE2 segment, Aya segment). A segment end is a bone index,
# ('mid', a, b) for the midpoint of two joints, or ('mesh', part) for the centre of a part's mesh.
# Left / right sides are swapped by side() when RE2's z side doesn't match Aya's.
PARTS = {
    0:  (1,  (0, 8),               (1, 10)),                        # chest
    1:  (20, (1, ('mid', 2, 5)),   (20, ('mid', 22, 26))),          # hips
    2:  (22, (2, 3),               (22, 23)),                       # thigh
    3:  (23, (3, 4),               (23, 24)),                       # shin
    4:  (24, (4, ('mesh', 4)),     (24, ('mesh', 24))),             # foot
    5:  (26, (5, 6),               (26, 27)),
    6:  (27, (6, 7),               (27, 28)),
    7:  (28, (7, ('mesh', 7)),     (28, ('mesh', 28))),
    8:  (11, (8, ('mesh', 8)),     (10, ('mesh', 11))),             # head (with the neck)
    9:  (4,  (9, 10),              (4, 5)),                         # upper arm
    10: (5,  (10, 11),             (5, 6)),                         # forearm
    11: (6,  (11, ('mesh', 11)),   (6, ('mesh', 6))),               # hand
    12: (14, (12, 13),             (14, 15)),
    13: (15, (13, 14),             (15, 16)),
    14: (16, (14, ('mesh', 14)),   (16, ('mesh', 16))),
}
SWAP = {2: 5, 3: 6, 4: 7, 5: 2, 6: 3, 7: 4, 9: 12, 10: 13, 11: 14, 12: 9, 13: 10, 14: 11}
# End pieces take the turn of the part above them (their joint keeps RE2's own angle): their mesh
# centres sit differently on the two models (a foot's sole), so lining those up tips them. The head
# (None) stays upright: the chest's turn follows Aya's idle lean and tips the face up.
END_PIECES = {4: 3, 7: 6, 8: None, 11: 10, 14: 13}
FEET = {4, 7}                               # set level by their own rules (place())
# Chest and hips are placed as one piece, neck -> hip joints: Aya's waist joint sits higher between them
# than Claire's, so meeting it per part stretched the hips (lower vest, belt, shorts) 1.5x, squashed the
# chest 0.68x and tilted the two apart. Their seam lands a little below Aya's waist joint.
TORSO = (0, 1)
TORSO_SEG = ((8, ('mid', 2, 5)), (10, ('mid', 22, 26)))     # RE2 segment, Aya segment
HIP_RING = -120                             # RE2 hips vertices above this (y down) are its top ring + lid
HIP_LIFT = 110      # RE2 leaves a slit under the vest, filled by the hips' black lid (left out, buried());
                    # raise the ring so it tucks under the vest instead
SLIM_MIN = 0.7
KNEES = {22: (22, 23), 23: (22, 23), 26: (26, 27), 27: (26, 27)}   # Aya leg bone -> (thigh, shin)
UPRIGHT = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]


# ---- small vector helpers ----
def add(a, b): return [x + y for x, y in zip(a, b)]
def sub(a, b): return [x - y for x, y in zip(a, b)]
def mul(a, k): return [x * k for x in a]
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def norm(a): return math.sqrt(dot(a, a))
def unit(a): n = norm(a) or 1.0; return [x / n for x in a]
def transpose(M): return [[M[j][i] for j in range(3)] for i in range(3)]
def cols(a, b, c): return [[a[i], b[i], c[i]] for i in range(3)]


def rot_between(a, b):
    """Smallest rotation taking direction a to direction b (Rodrigues)."""
    a, b = unit(a), unit(b)
    v, c = cross(a, b), dot(a, b)
    if c < -0.999999:                       # opposite: turn half way round any perpendicular
        p = unit(cross(a, [1, 0, 0]) if abs(a[0]) < 0.9 else cross(a, [0, 1, 0]))
        return [[2 * p[i] * p[j] - (1 if i == j else 0) for j in range(3)] for i in range(3)]
    k = 1 / (1 + c)
    vx = [[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]]
    vx2 = R.mm(vx, vx)
    return [[(1 if i == j else 0) + vx[i][j] + vx2[i][j] * k for j in range(3)] for i in range(3)]


def normal_table():
    body = open(OBJECT_C).read().split('g_RenderNormalTable[240] = {', 1)[1].split('};', 1)[0]
    t = [tuple(int(x) for x in m) for m in re.findall(r'\{(-?\d+),(-?\d+),(-?\d+),-?\d+\}', body)]
    if len(t) != 240: raise ValueError('normal table: %d entries' % len(t))
    return [unit(list(n)) for n in t]


def pose_mats(md, tr, ang):
    """World (rotation, translation) of every part, as render_models.pose walks the commands."""
    I = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
    cur, stack, mats = (I, [0, 0, 0]), [], {}
    for c in md['cmds']:
        if c == -1: stack.append(cur); continue
        if c == -2: cur = stack.pop(); continue
        Rm = R.mm(cur[0], R.rot_yxz(ang[c]) if c < len(ang) else I)
        vec = tr if c == 0 else [0, 0, md['ptab'][c][3]]
        cur = (Rm, add(R.mv(cur[0], vec), cur[1]))
        mats[c] = cur
    return mats


# ---- Aya ----
def load_aya():
    blob = R.image_blob(AYA_ENTRY)
    off, _, trailing = R.records(blob, 0x0C)[0]
    md = R.model(blob, off)
    rows = {s: o for o, s, g in R.records(blob, 0x10)}
    tr, idle = R.anim_frame_at(blob, rows[IDLE_ROW], 0)
    mats = pose_mats(md, tr, idle)
    world = R.pose(md, tr, idle)
    end, _ = B.model_extent(blob, off)
    return dict(blob=blob, off=off, trailing=trailing, md=md, rows=rows, idle=idle, mats=mats,
                world=world, raw=bytes(blob[off:off + end]))


def aya_point(aya, p):
    if isinstance(p, int): return aya['mats'][p][1]
    if p[0] == 'mid': return mul(add(aya['mats'][p[1]][1], aya['mats'][p[2]][1]), 0.5)
    vs, vc = aya['md']['ptab'][p[1]][:2]
    pts = [aya['world'][i] for i in range(vs, vs + vc)]
    return [sum(q[k] for q in pts) / len(pts) for k in range(3)]


# ---- RE2 ----
def re2_point(m, p):
    if isinstance(p, int): return list(m['pos'][p])
    if p[0] == 'mid': return mul(add(m['pos'][p[1]], m['pos'][p[2]]), 0.5)
    part = p[1]
    pts = [add(m['pos'][part], v) for k in ('tris', 'quads') for f in m['parts'][part][k] for v in f['v']]
    return [sum(q[k] for q in pts) / len(pts) for k in range(3)]


def frames(m, aya):
    """Global turn G (RE2 axes -> Aya axes), scale, and whether left / right swap."""
    # RE2: y down, feet point +x. Aya: y down, forward from her foot's mesh centre in front of the ankle.
    fwd_a = sub(aya_point(aya, ('mesh', 24)), aya['mats'][24][1]); fwd_a[1] = 0; fwd_a = unit(fwd_a)
    fwd_r = sub(re2_point(m, ('mesh', 4)), m['pos'][4]); fwd_r[1] = 0; fwd_r = unit(fwd_r)
    down = [0, 1, 0]
    G = R.mm(cols(fwd_a, down, cross(fwd_a, down)), transpose(cols(fwd_r, down, cross(fwd_r, down))))
    ground_a = max(w[1] for w in aya['world'] if w)
    ground_r = max(m['pos'][4][1] + v[1] for k in ('tris', 'quads') for f in m['parts'][4][k] for v in f['v'])
    hip_a = ground_a - aya['mats'][22][1][1]
    hip_r = ground_r - m['pos'][2][1]
    scale = hip_a / hip_r
    side = cross(fwd_a, down)
    swap = (dot(R.mv(G, m['pos'][2]), side) > 0) != (dot(aya['mats'][22][1], side) > 0)
    # the foot centres only settle forward and the sides; the facing comes from the hip lines
    hr = R.mv(G, sub(m['pos'][5], m['pos'][2])); ha = sub(aya['mats'][26][1], aya['mats'][22][1])
    if swap: hr = mul(hr, -1)
    G = R.mm(yaw(heading(ha) - heading(hr)), G)
    return G, scale, swap


def heading(v): return math.atan2(v[2], v[0])


def yaw(a):
    """Turn about the vertical that adds a to heading()."""
    c, s = math.cos(a), math.sin(a)
    return [[c, 0, -s], [0, 1, 0], [s, 0, c]]


def about(axis, a):
    """Turn by a about a unit axis (Rodrigues)."""
    x, y, z = unit(axis); c, s, t = math.cos(a), math.sin(a), 1 - math.cos(a)
    return [[t * x * x + c, t * x * y - s * z, t * x * z + s * y],
            [t * x * y + s * z, t * y * y + c, t * y * z - s * x],
            [t * x * z - s * y, t * y * z + s * x, t * z * z + c]]


def flat(v): return [v[0], 0, v[2]]
def span(pts, d): return max(dot(p, d) for p in pts) - min(dot(p, d) for p in pts)


def knee_forward(aya, thigh, shin):
    """Which way Aya's knee points in her idle pose: level and square to the axis it bends about
    in the walk (row 0x16), on the side her toes are."""
    blob, md, off = aya['blob'], aya['md'], aya['rows'][WALK_ROW]
    rest = R.mm(transpose(aya['mats'][thigh][0]), aya['mats'][shin][0])
    acc = [0, 0, 0]
    for fr in range(blob[off + 2]):
        tr, ang = R.anim_frame_at(blob, off, fr)
        mats = pose_mats(md, tr, ang)
        d = R.mm(R.mm(transpose(mats[thigh][0]), mats[shin][0]), transpose(rest))
        ax = [d[2][1] - d[1][2], d[0][2] - d[2][0], d[1][0] - d[0][1]]       # 2 sin(bend) * axis
        w = R.mv(aya['mats'][thigh][0], ax)
        acc = add(acc, mul(w, 1 if dot(w, acc) >= 0 else -1))
    f = unit(cross(acc, [0, 1, 0]))
    return f if dot(f, aya_toe(aya, shin + 1)) > 0 else mul(f, -1)


def aya_toe(aya, foot):
    """Level direction from Aya's ankle to the tip of her shoe, idle pose."""
    vs, vc = aya['md']['ptab'][foot][:2]; j = aya['mats'][foot][1]
    return unit(flat(max((sub(aya['world'][i], j) for i in range(vs, vs + vc)), key=lambda d: norm(flat(d)))))


def place(m, aya, G, scale, swap):
    """{RE2 part: (Aya bone, f(re2 vertex in its bone frame) -> world, g(normal) -> world)}."""
    out, turns = {}, {}
    fwd = unit(add(aya_toe(aya, 24), aya_toe(aya, 28)))
    side, slims = unit(cross(fwd, [0, 1, 0])), {}
    for part in sorted(PARTS, key=lambda p: p in END_PIECES):      # end pieces after their parents
        src = SWAP.get(part, part) if swap else part
        bone, _, as_ = PARTS[src]
        rs = TORSO_SEG[0] if part in TORSO else PARTS[part][1]
        if part in TORSO: as_ = TORSO_SEG[1]
        c0, c1 = re2_point(m, rs[0]), re2_point(m, rs[1])
        a0, a1 = aya_point(aya, as_[0]), aya_point(aya, as_[1])
        dc = R.mv(G, sub(c1, c0)); da = sub(a1, a0)
        u = unit(dc)
        if part in FEET:
            # level, toes where Aya's point, sole stretched down to her floor
            pts = [R.mv(G, sub(add(m['pos'][part], v), c0)) for k_ in ('tris', 'quads') for f_ in m['parts'][part][k_] for v in f_['v']]
            toe = max(pts, key=lambda d: norm(flat(d)))
            Q = yaw(heading(aya_toe(aya, bone)) - heading(toe))
            vs, vc = aya['md']['ptab'][bone][:2]
            floor = max(aya['world'][i][1] for i in range(vs, vs + vc)) - a0[1]
            u, k = [0, 1, 0], floor / (scale * max(d[1] for d in pts))
        elif part in END_PIECES: Q, k = turns.get(END_PIECES[part], UPRIGHT), 1.0
        else:
            Q = rot_between(dc, da)
            if bone in KNEES:
                # twist about the limb so the knee faces the way Aya's bends
                want, have = knee_forward(aya, *KNEES[bone]), R.mv(Q, flat(fwd))
                p = [sub(x, mul(unit(da), dot(x, unit(da)))) for x in (have, want)]
                Q = R.mm(about(da, math.atan2(dot(cross(p[0], p[1]), unit(da)), dot(p[0], p[1]))), Q)
            k = max(0.6, min(1.6, norm(da) / (scale * norm(dc) or 1)))
        turns[part] = Q
        base = m['pos'][part]
        # legs no wider across than Aya's: hers are on narrower hips and her knees pass close
        w = 1.0
        if bone in KNEES:
            vs, vc = aya['md']['ptab'][bone][:2]
            have = span([R.mv(G, add(base, v)) for k_ in ('tris', 'quads') for f_ in m['parts'][part][k_] for v in f_['v']], side) * scale
            w = max(SLIM_MIN, min(1.0, span([aya['world'][i] for i in range(vs, vs + vc)], side) / have))
        slims[part] = w

        def f(v, c0=c0, a0=a0, Q=Q, u=u, k=k, base=base, feet=part in FEET, w=w, hips=part == 1):
            if hips and v[1] < HIP_RING: v = (v[0], v[1] - HIP_LIFT, v[2])
            x = R.mv(G, sub(add(base, v), c0))
            along = dot(x, u)
            if feet: along = max(0, along)                     # stretch below the ankle only
            x = add(x, mul(side, dot(x, side) * (w - 1)))
            x = add(mul(x, scale), mul(u, along * scale * (k - 1)))
            return add(a0, R.mv(Q, x))

        def g(n, Q=Q):
            return R.mv(Q, R.mv(G, n))
        out[part] = (bone, f, g)
    print("  legs slimmed across:", ", ".join("%d %.2f" % (p, w) for p, w in sorted(slims.items()) if w < 1) or "none")
    return out


# ---- texture ----
def atlas_xy(face, j):
    u, v = face['uv'][j]
    return (face['page'] & 1) * 128 + u, min(v, 254)


def raster(pts):
    """Pixels covered by a UV polygon (with a one-pixel edge), as a set of (x, y)."""
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    tris = [(pts[0], pts[1], pts[2])] + ([(pts[1], pts[3], pts[2])] if len(pts) == 4 else [])
    out = set()
    for y in range(max(0, min(ys) - 1), min(254, max(ys) + 1) + 1):
        for x in range(max(0, min(xs) - 1), min(255, max(xs) + 1) + 1):
            for a, b, c in tris:
                if in_tri((x + 0.5, y + 0.5), a, b, c, 1.0): out.add((x, y)); break
    return out


def in_tri(p, a, b, c, pad):
    def edge(p0, p1):
        ex, ey = p1[0] - p0[0], p1[1] - p0[1]
        ln = math.hypot(ex, ey) or 1
        return ((p[0] - p0[0]) * ey - (p[1] - p0[1]) * ex) / ln
    d = [edge(a, b), edge(b, c), edge(c, a)]
    return all(x <= pad for x in d) or all(x >= -pad for x in d)


def build_texture(m, faces):
    """Returns (image 64x255 halfwords, palettes 512 halfwords, per face palette index)."""
    def colour(x, y, clut_row):
        c = m['pal'][clut_row][m['image'][y][x]]
        return re2_model.rgb(c)
    rows = sorted({f['clut'] >> 6 for f in faces})
    clut_of = {r: i for i, r in enumerate(rows)}          # RE2 palette row -> TIM palette index
    covers = [raster([atlas_xy(f, j) for j in range(len(f['uv']))]) for f in faces]
    parent = list(range(len(faces)))
    def find(i):
        while parent[i] != i: parent[i] = parent[parent[i]]; i = parent[i]
        return i
    owner = {}
    for i, cov in enumerate(covers):
        for p in cov:
            if p in owner: parent[find(i)] = find(owner[p])
            else: owner[p] = i
    groups = {}
    for i in range(len(faces)): groups.setdefault(find(i), []).append(i)
    groups = list(groups.values())
    def pixels(g): return {p for i in g for p in covers[i]}
    def mean(g):
        cs = [colour(x, y, clut_of[faces[g[0]]['clut'] >> 6]) or (0, 0, 0) for x, y in pixels(g)]
        return [sum(c[k] for c in cs) / len(cs) for k in range(3)]
    print('  texture groups', len(groups), end='')
    while len(groups) > 32:                    # merge the smallest into the closest in colour
        groups.sort(key=lambda g: len(pixels(g)))
        small = groups.pop(0); ms = mean(small)
        j = min(range(len(groups)), key=lambda j: sum((a - b) ** 2 for a, b in zip(ms, mean(groups[j]))))
        groups[j] = groups[j] + small
    print(' ->', len(groups))
    from PIL import Image
    image = [[0] * 256 for _ in range(255)]
    pal = [0] * 512
    face_pal = [0] * len(faces)
    for gi, g in enumerate(groups):
        px = sorted(pixels(g))
        cols_ = []
        for x, y in px:
            row = clut_of[faces[g[0]]['clut'] >> 6] if x >= 0 else 0
            # a pixel is coloured by the palette of the faces that use it (its own half)
            row = next((clut_of[faces[i]['clut'] >> 6] for i in g if (x, y) in covers[i]), row)
            cols_.append(colour(x, y, row) or (0, 0, 0))
        strip = Image.new('RGB', (len(cols_), 1)); strip.putdata(cols_)
        q = strip.quantize(16, method=Image.Quantize.MEDIANCUT)
        qp = q.getpalette()[:48]
        for k in range(16):
            r, gg, b = (qp[3 * k:3 * k + 3] + [0, 0, 0])[:3]
            c = (r >> 3) | ((gg >> 3) << 5) | ((b >> 3) << 10)
            pal[(gi // 16) * 256 + (gi % 16) * 16 + k] = c or 0x0421      # 0 would be see-through
        idx = list(q.getdata())
        for (x, y), k in zip(px, idx): image[y][x] = k
        for i in g: face_pal[i] = gi
    words = []
    for y in range(255):
        for hx in range(64):
            v = 0
            for n in range(4): v |= (image[y][hx * 4 + n] & 15) << (4 * n)
            words.append(v)
    return words, pal, face_pal


def write_texture(path, image, pal):
    """Aya's texture blob with the player page and palettes replaced (as build_skin.build)."""
    tex = bytearray(R.image_blob(AYA_TEX))
    td = R.word(tex, 4); tdesc = R.word(tex, td + 0x28); tat = tdesc & 0x3fffff
    done = set()
    for i in range(tdesc >> 22):
        r = tat + 20 * i
        src = R.word(tex, r + 4); rect = R.word(tex, r + 8); sec = R.word(tex, r + 12); rect2 = R.word(tex, r + 16)
        x, y, w, h = (rect >> 10) & 0x7ff, rect >> 21, rect & 0x3ff, src >> 24 or 256
        if (x, y, w) == (960, 256, 64):
            for k, v in enumerate(image[:w * h]): struct.pack_into('<H', tex, (src & 0xffffff) + 2 * k, v)
            done.add('image')
        if (x, y, w) == (0, 448, 256):
            for k, v in enumerate(pal[:w * h]): struct.pack_into('<H', tex, (src & 0xffffff) + 2 * k, v)
            done.add('palette')
        if sec & 0xffffff:
            x2, y2, w2, h2 = (rect2 >> 10) & 0x7ff, rect2 >> 21, rect2 & 0x3ff, sec >> 24
            if (x2, y2, w2) == (0, 448, 256):
                o = (src & 0xffffff) + (sec & 0xffffff)
                for k, v in enumerate(pal[:w2 * h2]): struct.pack_into('<H', tex, o + 2 * k, v)
                done.add('palette')
    if done != {'image', 'palette'}: raise ValueError('texture blob: found only %s' % sorted(done))
    open(path, 'wb').write(bytes(tex))
    return len(tex)


# ---- model ----
def winding_sign(points, normal):
    a, b, c = points[:3]
    return 1 if dot(cross(sub(b, a), sub(c, a)), normal) > 0 else -1


def aya_winding(aya):
    md = aya['md']; tab = normal_table(); votes = 0
    V = md['V']
    pads = []
    raw, parts, verts = aya['raw'], md['parts'], len(md['V'])
    for i in range(verts): pads.append(struct.unpack_from('<h', raw, 0x1C + 12 * parts + 8 * i + 6)[0])
    for kind, idx in md['D'][:sum(md['pk'][:2])]:
        pts = [V[i] for i in idx[:3]]
        # all three in one part (same frame) for a fair test
        n = add(add(tab[pads[idx[0]]], tab[pads[idx[1]]]), tab[pads[idx[2]]])
        votes += winding_sign(pts, n)
    return 1 if votes > 0 else -1


def build_model(m, aya, placed, face_pal, faces_src):
    md = aya['md']; tab = normal_table()
    inv = {b: (transpose(Rm), t) for b, (Rm, t) in aya['mats'].items()}
    # RE2 winding vs Aya's: the same convention gives the same sign against the normals
    votes = sum(winding_sign(f['v'], add(add(f['n'][0], f['n'][1]), f['n'][2])) for f, p in faces_src)
    flip = (1 if votes > 0 else -1) != aya_winding(aya)
    per_bone = {}                               # Aya bone -> {re2 (part, vertex): [local xyz, normal sum]}
    for f, part in faces_src:
        bone, place_v, place_n = placed[part]
        Rt, t = inv[bone]
        d = per_bone.setdefault(bone, {})
        for v, n in zip(f['v'], f['n']):
            key = (part, v)
            if key not in d:
                d[key] = [R.mv(Rt, sub(place_v(v), t)), [0, 0, 0]]
            d[key][1] = add(d[key][1], R.mv(Rt, unit(place_n(n))))
    verts, index, ptab = [], {}, []
    for b in range(md['parts']):
        start = len(verts)
        for key, (p, nsum) in per_bone.get(b, {}).items():
            n = unit(nsum)
            pad = max(range(240), key=lambda i: dot(tab[i], n))
            index[key] = len(verts)
            verts.append(([max(-32768, min(32767, int(round(c)))) for c in p], pad))
        vs, vc, vis, tz = md['ptab'][b]
        ptab.append(struct.pack('<HHBxxxi', start, len(verts) - start, vis, tz))
    quads = [(f, p, i) for i, (f, p) in enumerate(faces_src) if len(f['v']) == 4]
    tris = [(f, p, i) for i, (f, p) in enumerate(faces_src) if len(f['v']) == 3]
    D, UV = bytearray(), bytearray()
    for group, code in ((quads, 0x0a), (tris, 0x0f)):
        for f, part, i in group:
            order = ([0, 2, 1, 3] if len(f['v']) == 4 else [0, 2, 1]) if flip else list(range(len(f['v'])))
            ids = [index[(part, f['v'][j])] for j in order] + [0] * (4 - len(order))
            D += bytes([0x80, 0x80, 0x80, code]) + struct.pack('<4H', *ids)
            gi = face_pal[i]
            clut = ((448 + gi // 16) << 6) | (gi % 16)
            uv = [atlas_xy(f, j) for j in order]
            rec = [uv[0][0], uv[0][1], clut, uv[1][0], uv[1][1], 31, uv[2][0], uv[2][1], 0]
            if len(order) == 4:
                rec += [uv[3][0], uv[3][1], 0]
                UV += struct.pack('<BBHBBHBBHBBH', *rec)
            else:
                UV += struct.pack('<BBHBBHBBH', *rec)
    # Aya's model block around it: header, part table, then ours, then her trailing sections
    raw = aya['raw']; parts = md['parts']; averts = len(md['V']); nd = sum(md['pk'])
    at = 0x1C + 12 * parts + 8 * averts + 4 * averts + 12 * nd
    tail = raw[at:at + 16 + 16 * parts + 8]                          # sec14, bounds, projection
    mcb = R.u16(raw, 0x18)
    cmds = raw[at + 16 + 16 * parts + 8: at + 16 + 16 * parts + 8 + (mcb + 3) // 4 * 4]
    head = bytearray(raw[:0x1C])
    struct.pack_into('<H', head, 4, len(quads) + len(tris))
    struct.pack_into('<H', head, 6, len(verts))
    struct.pack_into('<4H', head, 8, len(quads), len(tris), 0, 0)
    vbytes = b''.join(struct.pack('<hhhh', *p, pad) for p, pad in verts)
    cbytes = raw[0x1C + 12 * parts + 8 * averts: 0x1C + 12 * parts + 8 * averts + 4] * len(verts)
    out = bytes(head) + b''.join(ptab) + vbytes + cbytes + bytes(D) + tail + cmds + bytes(UV)
    print('  model: %d vertices, %d quads, %d triangles, %d bytes (Aya %d / %d / %d, %d bytes)%s' % (
        len(verts), len(quads), len(tris), len(out), averts, md['pk'][0], md['pk'][1], len(raw),
        ', winding flipped' if flip else ''))
    return out


def buried(m, placed):
    """Hip faces wholly above the jacket's lowest point (RE2's black waist lid). RE2 hides them with
    its depth buffer; PE sorts whole polygons, so they flicker through the back of the jacket."""
    chest = [placed[0][1](v) for k in ('tris', 'quads') for f in m['parts'][0][k] for v in f['v']]
    bottom = max(p[1] for p in chest)                       # y down
    out = {id(f) for k in ('tris', 'quads') for f in m['parts'][1][k]
           if all(placed[1][1](v)[1] < bottom for v in f['v'])}
    # and the jacket's own black bottom lid (2 flat quads at its lowest edge), inside the hips' top ring:
    # sorted whole, it drew over the hips' front as a black band just above the belt
    low = max(v[1] for k in ('tris', 'quads') for f in m['parts'][0][k] for v in f['v'])
    lid = {id(f) for k in ('tris', 'quads') for f in m['parts'][0][k] if all(v[1] > low - 10 for v in f['v'])}
    print('  buried, left out: %d hip faces in the jacket, %d jacket faces in the hips' % (len(out), len(lid)))
    return out | lid


def aya_rows(blob):
    return [(bytes(blob[o:o + B.row_length(blob, o)]), s) for o, s, g in R.records(blob, 0x10)]


def full_legs(src, dst, mp):
    """retarget.chains() ends a leg at its first fork. The evening dress (31 parts) hangs a torn-skirt
    flap off the right thigh (26 -> 27 shin + 29 flap), so the skin's right shin and foot got no
    motion and held their idle angle (Day 1 opera stage: legs splayed, knee bent the wrong way).
    Follow the branch carrying the most vertices (the shin and foot, not the 7-vertex flap)."""
    kids = T.tree(src['cmds'])
    def weight(n): return src['ptab'][n][1] + sum(weight(k) for k in kids.get(n, []))
    s, d = T.chains(src['cmds']), T.chains(dst['cmds'])
    for k in ('legA', 'legB'):
        run = [s[k][0]]
        while kids.get(run[-1]): run.append(max(kids[run[-1]], key=weight))
        for p, q in zip(d[k], run): mp.setdefault(p, q)
    return mp


DRESS_IMAGES = {31: 18, 23: 19}           # dress skeleton part count -> its bundle
FEET_SKIN = (24, 28)


def dress_feet(md_new, idle):
    """Aya's dress skeletons stand in heels: their feet rest toe-down and their rows carry the body
    ~30 higher than the jacket's (idle root y -368 vs -338). Copied as they are, Claire's boots tip
    down and she floats above the floor (Day 1 limo arrival). Re-base each foot on the skin's own
    rest angle, as shoulder_deltas does for the shoulders."""
    for parts, img in DRESS_IMAGES.items():
        if parts not in B.ALT_SOURCES: continue
        src, mp, corr = B.ALT_SOURCES[parts]
        b = R.image_blob(img); rows = {s: o for o, s, g in R.records(b, 0x10)}
        sw, _ = T.world_rotations(src, R.anim_frame_at(b, rows[IDLE_ROW], 0)[1])
        tw, _ = T.world_rotations(md_new, idle)
        for p in FEET_SKIN:
            if p in mp: corr[p] = R.mm(transpose(sw[mp[p]]), tw[p])


def sole(md, tr, ang, parts):
    W = R.pose(md, tr, ang)
    return max(W[i][1] for p in parts for i in range(md['ptab'][p][0], md['ptab'][p][0] + md['ptab'][p][1]) if W[i])


def dress_row(data, md_new, idle):
    """A dress-skeleton row retargeted (as B.retarget_row) with the root dropped each frame so the
    skin's soles sit where the dress model's do (heels on the floor, or the car floor when seated);
    the drop is held to [-10, 40]. None for other rows."""
    parts = data[1] + 1
    if parts not in DRESS_IMAGES or parts not in B.ALT_SOURCES: return None
    src, mp, corr = B.ALT_SOURCES[parts]
    feet_src = [mp[p] for p in FEET_SKIN if p in mp]
    straight = T.straight_parts(md_new)
    trs, angs, drops = [], [], []
    for f in range(max(1, data[2])):
        tr, ang = R.anim_frame_at(data, 0, f)
        skin = T.retarget_world(src, ang, md_new, mp, idle, corr, straight=straight)
        drops.append(max(-10, min(40, sole(src, tr, ang, feet_src) - sole(md_new, tr, skin, FEET_SKIN))))
        trs.append(tr); angs.append(skin)
    # averaged over 5 frames: the planted foot switching in a stride would make her bob
    n = len(drops)
    drops = [sum(drops[max(0, i - 2):i + 3]) / len(drops[max(0, i - 2):i + 3]) for i in range(n)]
    trs = [[tr[0], tr[1] + round(dr), tr[2]] for tr, dr in zip(trs, drops)]
    return B.encode_row(trs, angs, md_new['parts'], struct.unpack_from('<4H', data, 4))


def write_rooms(path, aya, md_new):
    """rooms.bin with the rooms' and dress bundles' rows for Aya's 31- and 23-part skeletons
    retargeted to the 29-part one (29-part rows are left out: the game's own already fit)."""
    idle = aya['idle']
    B.alt_sources(md_new, idle)
    for src, mp, corr in B.ALT_SOURCES.values(): full_legs(src, md_new, mp)
    dress_feet(md_new, idle)
    rows = {fp: d for fp, d in B.room_player_rows().items() if d[1] + 1 != md_new['parts']}
    # The evening (image 18) and Day 1 long dress (19) bundles' own rows: the skin's model.bin only
    # covers the jacket's slots, so the long dress's 29-34 would play on the 29-part skin as they are.
    # Rows shorter than B.FP_BYTES can't be matched (the loader holds the skin's idle pose for them).
    for img in (18, 19):
        for d, s in aya_rows(R.image_blob(img)):
            if d[1] + 1 != md_new['parts'] and len(d) >= B.FP_BYTES: rows.setdefault(B.fingerprint(d), d)
    mp = T.semantic_mapping(aya['md'], md_new)
    corr = T.shoulder_deltas(aya['md'], md_new, mp, idle, idle)
    items = sorted(rows.items())
    body = bytearray(); table = []
    head = 8 + 12 * len(items)
    for fp, data in items:                  # B.write_room_rows, with dress rows grounded
        out = dress_row(data, md_new, idle) or B.retarget_row(data, 0, aya['md'], md_new, mp, idle, corr, 1.0)
        table.append((fp, head + len(body), len(out))); body += out
        while len(body) % 4: body.append(0)
    open(path, 'wb').write(b'PERR' + struct.pack('<I', len(items)) +
                           b''.join(struct.pack('<III', *t) for t in table) + bytes(body))
    return len(items), head + len(body)


PACK = {"format": 1, "id": "re2-local", "name": "Resident Evil 2 (built locally)", "version": "1",
        "author": "built with tools/re2-skin", "license": "personal use only, see LICENSE",
        "kind": "assets", "min_wardrobe": 1}
PACK_LICENSE = """These costumes were built on this computer by tools/re2-skin (parasite-eve-pc) from the
player's own copy of a Resident Evil 2 model. The models and textures are Capcom's.
For personal use only: do not share, upload or sell this pack.
"""
FOLDER_OK = re.compile(r'^[A-Za-z0-9_$-][A-Za-z0-9._$-]{0,47}$')
PACK_ID_OK = re.compile(r'^[a-z0-9][a-z0-9-]{0,30}$')


def default_folder(src):
    """BioRand keeps one folder per character (data/re2/pld1/claire.re2/PL01.PLD): use its name."""
    parent = os.path.basename(os.path.dirname(os.path.abspath(src)))
    if FOLDER_OK.match(parent) and '.' in parent: return parent
    return os.path.splitext(os.path.basename(src))[0].lower() + '.re2'


def default_name(folder):
    """claire.re2 -> Claire (RE2); claire.re2$biker -> Claire (RE2, biker)."""
    base, _, variant = folder.partition('$')
    who, _, game = base.partition('.')
    label = (game.upper() + (', ' + variant if variant else '')) if game else variant
    return who[:1].upper() + who[1:] + (' (%s)' % label if label else '')


def write_pack_info(pack, folder, name, group, tier):
    """pack.json and LICENSE once (an existing pack keeps its own), then the character's entry."""
    os.makedirs(os.path.join(pack, 'characters', folder), exist_ok=True)
    if not os.path.exists(os.path.join(pack, 'pack.json')):
        info = dict(PACK)
        own = os.path.basename(os.path.abspath(pack)).lower()
        if PACK_ID_OK.match(own) and own not in ('pe1', 'local'): info['id'] = own
        with open(os.path.join(pack, 'pack.json'), 'w') as f: json.dump(info, f, indent=2)
    if not os.path.exists(os.path.join(pack, 'LICENSE')):
        with open(os.path.join(pack, 'LICENSE'), 'w') as f: f.write(PACK_LICENSE)
    with open(os.path.join(pack, 'characters', folder, 'character.json'), 'w') as f:
        json.dump({"name": name, "group": group, "tier": tier}, f, indent=2)


def main():
    ap = argparse.ArgumentParser(description='Build a Wardrobe pack costume from an RE2 PC player model (PLxx.PLD).')
    ap.add_argument('pld', help='the RE2 PC player model, e.g. <biorand>/data/re2/pld1/claire.re2/PL01.PLD')
    ap.add_argument('--pack', default='re2-local', help='pack folder to write or add to (default: re2-local)')
    ap.add_argument('--folder', help='character folder in the pack (default: the PLD folder name, e.g. claire.re2)')
    ap.add_argument('--name', help='name in the Wardrobe (default from the folder, e.g. "Claire (RE2)")')
    ap.add_argument('--group', default='Resident Evil', help='Wardrobe group (default: Resident Evil)')
    ap.add_argument('--tier', default='extra', choices=('extra', 'outfit', 'supporting', 'main', 'special'),
                    help='price tier when Economy charges for costumes (default: extra)')
    ap.add_argument('--check', action='store_true', help='also draw Aya and the skin side by side (needs Pillow)')
    a = ap.parse_args()
    folder = a.folder or default_folder(a.pld)
    if not FOLDER_OK.match(folder): sys.exit('bad folder name %r: letters, digits, . _ - $' % folder)
    name = a.name or default_name(folder)
    out_dir = os.path.join(a.pack, 'characters', folder)
    m = re2_model.load(a.pld)
    write_pack_info(a.pack, folder, name, a.group, a.tier)
    aya = load_aya()
    G, scale, swap = frames(m, aya)
    print('%s: scale %.3f, sides %s' % (folder, scale, 'swapped' if swap else 'kept'))
    placed = place(m, aya, G, scale, swap)
    hidden = buried(m, placed)
    faces_src = [(f, p) for p in sorted(PARTS) for k in ('quads', 'tris') for f in m['parts'][p][k]
                 if id(f) not in hidden]
    image, pal, face_pal = build_texture(m, [f for f, p in faces_src])
    raw = build_model(m, aya, placed, face_pal, faces_src)
    bundle = B.write_blob([(raw, 0, aya['trailing'])], aya_rows(aya['blob']))
    open(os.path.join(out_dir, 'model.bin'), 'wb').write(bundle)
    for day in range(1, 9):
        open(os.path.join(out_dir, 'day%d.bin' % day), 'wb').write(B.write_blob([], aya_rows(R.image_blob(0x16 + 8 + day))))
    print('  texture.bin', write_texture(os.path.join(out_dir, 'texture.bin'), image, pal), 'bytes')
    open(os.path.join(out_dir, 'meta.bin'), 'wb').write(b'PEMT' + struct.pack('<I', 0x12))
    md_new = R.model(bundle, R.records(bundle, 0x0C)[0][0])
    n, size = write_rooms(os.path.join(out_dir, 'rooms.bin'), aya, md_new)
    print('  rooms.bin', n, 'room animations,', size, 'bytes')
    print('%s "%s" written to %s; copy that folder into <preferences>/packs/' % (folder, name, a.pack))
    if a.check: check(out_dir, folder, aya)


def check(out_dir, name, aya):
    """Decode what we wrote and draw Aya and the skin in a few of her animations, front and side."""
    from PIL import Image, ImageDraw
    b = open(os.path.join(out_dir, 'model.bin'), 'rb').read()
    t = open(os.path.join(out_dir, 'texture.bin'), 'rb').read()
    md = R.model(b, R.records(b, 0x0C)[0][0])
    vram = [0] * (1024 * 512); R.upload_tims(t, vram)
    avram = [0] * (1024 * 512); R.upload_tims(R.image_blob(AYA_TEX), avram)
    rows = {s: o for o, s, g in R.records(b, 0x10)}
    R.NOTEX = False
    picks = [(0x15, 0, 'front'), (0x15, 0, 'side'), (0, 10, 'front'), (13, 8, 'side'), (16, 9, 'front'), (22, 20, 'side')]
    cell = 220
    sheet = Image.new('RGB', (cell * len(picks), 2 * cell + 20), (10, 10, 12)); dr = ImageDraw.Draw(sheet)
    for i, (s, f, view) in enumerate(picks):
        if s not in rows: continue
        tr, ang = R.anim_frame_at(b, rows[s], f)
        for j, (model, vr) in enumerate(((aya['md'], avram), (md, vram))):
            im = R.render(model, R.pose(model, tr, ang), vr, view, cell)
            if im: sheet.paste(im, (cell * i, 20 + cell * j))
        dr.text((cell * i + 4, 4), 'row %d f%d %s' % (s, f, view), fill=(255, 255, 0))
    p = '%s_check.png' % name.replace('$', '_')      # in the current folder, not in the pack
    sheet.save(p); print('  check', p)


if __name__ == '__main__':
    main()
