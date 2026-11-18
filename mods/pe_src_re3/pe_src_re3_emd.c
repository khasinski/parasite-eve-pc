/* Resident Evil 3 enemy models (EMxx.EMD + EMxx.TIM), PC (rofs9: ROOM/EMD). Read only.
 * Layout as documented by reevengi (src/g_re3/emd.c), checked against the files; the vertex
 * binding and the step words decoded here from Nemesis (EM34). The Python prototype is
 * tools/emd3.py in the enemy importer's notes.
 *   header   u32 directory offset, u32 count (15); the directory's u32 offsets name the sections
 *   0        vertex binding: u32 length, u16, u16 entry count; entries of 4 u16 at 8 + 8 i, the first
 *            two (A, B) offsets from the entry itself (0: none). A list = records (bone u8, n u8, 3 n
 *            vertex indexes padded with repeats or 0xFF) until a 0xFF bone: A says which bone each
 *            vertex follows, B which joint vertices also follow a second bone (half and half).
 *            Entries go, in order, to the real parts (more than 3 vertices) whose own bone is in
 *            the entry. A bound part's vertices are in the rest pose's root frame.
 *   2/3 4/5 6/7  animation sets: EDD (moves) + EMR (key frames; set 1's EMR is also the skeleton)
 *            EDD: 8 bytes per move (the first's offset / 8 = the count): u16 steps, offset, first
 *            key, 0. A step u16: low byte = key from the move's first; 0x100 = an in-between step,
 *            low byte / 256 of the way from the last key to the next; 0x800 = a blow can land on
 *            this frame (0x400 marks the wind-up); the top 4 bits = the sound (tone) played, 0 none.
 *            EMR: u16 tree offset, key frames offset, bone count, key frame size; bone offsets
 *            (s16 x, y, z) at 8; the tree (per bone u16 child count, offset of the child bytes).
 *            Key frame: s16[4] (the 4th = the root's height), then 12-bit angles x, y, z per bone,
 *            back to back (two bones in 9 bytes).
 *   14       meshes, one object per bone (some 3-vertex placeholders, extras after the bones):
 *            u32 length, u32 count; 24 bytes per object from +8: vertices, normals, triangles, quads
 *            (u16 offsets from +8, each followed by a u16 count), triangle and quad counts. Vertex
 *            x, y, z, w s16. Triangle 12 bytes: u0 v0 page ? u1 v1 clut i0 u2 v2 i1 i2; quad 16:
 *            u0 v0 page ? u1 v1 clut ? u2 v2 i0 i1 u3 v3 i2 i3. Vertex indexes are bytes.
 *   TIM      8-bit with palettes, wider than 256: a texel's x = u + 2 * page.
 * Out: the Model (rigid parts in their bone's frame; bound parts skinned, in the model frame; a
 * face's clut = its palette row, page = the page byte: x = u + 2 * page) and the Animation (set 1's
 * moves, then sets 2 and 3's, at 30 frames a second; root = bone 0's x and z, the key's height). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_src_re3.h"

typedef struct { const uint8_t *b; uint32_t size; } Buf;

static int ok(const Buf *x, uint64_t at, uint64_t len) { return at <= x->size && len <= x->size - at; }
static unsigned u16at(const Buf *x, uint32_t at) { return x->b[at] | x->b[at + 1] << 8; }
static uint32_t u32at(const Buf *x, uint32_t at) { return u16at(x, at) | (uint32_t)u16at(x, at + 2) << 16; }
static int16_t s16at(const Buf *x, uint32_t at) { return (int16_t)u16at(x, at); }

#define NEED(at, len, what) do { if (!ok(x, (at), (len))) { snprintf(why, n, "EMD: %s past the end", what); return -1; } } while (0)
#define BONES_MAX 64
#define SETS 3

/* ---- the skeleton (set 1's EMR) ---- */
static int skeleton(const Buf *x, uint32_t emr, PeImpModel *m, char *why, size_t n) {
    NEED(emr, 8, "skeleton header");
    uint32_t tree = u16at(x, emr), count = u16at(x, emr + 4);
    if (!count || count > BONES_MAX) { snprintf(why, n, "EMD: %u bones", count); return -1; }
    NEED((uint64_t)emr + 8, 6 * count, "bone offsets");
    NEED((uint64_t)emr + tree, 4 * count, "bone tree");
    m->bones = count;
    m->parent = malloc(count * sizeof *m->parent);
    m->rel = calloc(count, sizeof *m->rel);
    m->pos = calloc(count, sizeof *m->pos);
    if (!m->parent || !m->rel || !m->pos) { snprintf(why, n, "out of memory"); return -1; }
    for (uint32_t i = 0; i < count; i++) {
        m->parent[i] = -1;
        for (int c = 0; c < 3; c++) m->rel[i][c] = s16at(x, emr + 8 + 6 * i + 2 * c);
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t nc = u16at(x, emr + tree + 4 * i), o = u16at(x, emr + tree + 4 * i + 2);
        NEED((uint64_t)emr + tree + o, nc, "bone children");
        for (uint32_t k = 0; k < nc; k++) {
            unsigned c = x->b[emr + tree + o + k];
            if (c < count && c != i && m->parent[c] == -1) m->parent[c] = (int16_t)i;
        }
    }
    /* positions parent first; a child listed before its parent or a loop is refused by the check */
    for (uint32_t pass = 0; pass < count; pass++)
        for (uint32_t i = 0; i < count; i++) {
            int p = m->parent[i];
            for (int c = 0; c < 3; c++) m->pos[i][c] = m->rel[i][c] + (p >= 0 && p < (int)count ? m->pos[p][c] : 0);
        }
    return 0;
}

/* ---- meshes ---- */
typedef struct { uint32_t vo, no, vc, to, qo, tc, qc; } Obj;
typedef struct { uint8_t (*skin)[2]; } Skin;

static int is_real(const Obj *o) { return o->vc > 3; }

/* one bind list: sets slot `which` (0 bone, 1 second bone) of the listed vertices */
static int bind_list(const Buf *x, uint32_t at, uint32_t end, uint8_t (*skin)[2], uint32_t vc, int which,
                     uint32_t bones, uint64_t *seen, char *why, size_t n) {
    for (int recs = 0; recs < 256; recs++) {
        if (at >= end) break;
        unsigned bone = x->b[at];
        if (bone == 0xFF) return 0;
        if (at + 2 > end) break;
        unsigned cnt = x->b[at + 1];
        if ((uint64_t)at + 2 + 3 * cnt > end) break;
        if (bone < bones) {                               /* a record for a bone it doesn't have: passed over */
            if (seen) *seen |= 1ull << bone;
            for (unsigned k = 0; k < 3 * cnt; k++) {
                unsigned v = x->b[at + 2 + k];
                if (skin && v < vc && v != 0xFF) skin[v][which] = (uint8_t)bone;
            }
        }
        at += 2 + 3 * cnt;
    }
    snprintf(why, n, "EMD: a binding list runs past its section");
    return -1;
}

static int bindings(const Buf *x, uint32_t sec, uint32_t end, const Obj *obj, uint32_t objs, uint32_t bones,
                    Skin *skin, char *why, size_t n) {
    if (!sec) return 0;
    NEED(sec, 8, "binding header");
    uint32_t count = u16at(x, sec + 6);
    if (!count || count > 64) return 0;                  /* no binding: every part rigid */
    NEED((uint64_t)sec + 8, 8 * count, "binding entries");
    uint32_t pi = 0, lim = objs < bones ? objs : bones;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t e = sec + 8 + 8 * i, a = u16at(x, e), b = u16at(x, e + 2);
        if (!a) continue;
        if ((uint64_t)e + a >= end || (b && (uint64_t)e + b >= end)) { snprintf(why, n, "EMD: binding list outside its section"); return -1; }
        uint64_t used = 0;
        if (bind_list(x, e + a, end, NULL, 0, 0, bones, &used, why, n) ||
            (b && bind_list(x, e + b, end, NULL, 0, 1, bones, &used, why, n)))
            return -1;
        while (pi < lim && !(is_real(&obj[pi]) && (used >> pi & 1))) pi++;
        if (pi >= lim) break;
        uint32_t vc = obj[pi].vc;
        if (!(skin[pi].skin = malloc(vc * sizeof *skin[pi].skin))) { snprintf(why, n, "out of memory"); return -1; }
        for (uint32_t v = 0; v < vc; v++) skin[pi].skin[v][0] = (uint8_t)pi, skin[pi].skin[v][1] = PE_IMP_NO_BONE;
        if (bind_list(x, e + a, end, skin[pi].skin, vc, 0, bones, NULL, why, n) ||
            (b && bind_list(x, e + b, end, skin[pi].skin, vc, 1, bones, NULL, why, n)))
            return -1;
        pi++;
    }
    return 0;
}

static int meshes(const Buf *x, uint32_t m14, uint32_t bind, uint32_t bind_end, PeImpModel *m, char *why, size_t n) {
    NEED(m14, 8, "mesh header");
    uint32_t objs = u32at(x, m14 + 4), base = m14 + 8;
    if (!objs || objs > PE_IMP_MODEL_PARTS_MAX) { snprintf(why, n, "EMD: %u mesh objects", objs); return -1; }
    NEED(base, 24ull * objs, "mesh objects");
    Obj *obj = calloc(objs, sizeof *obj);
    Skin *skin = calloc(objs, sizeof *skin);
    int rc = -1;
    if (!obj || !skin) { snprintf(why, n, "out of memory"); goto out; }
    for (uint32_t i = 0; i < objs; i++) {
        uint32_t e = base + 24 * i;
        Obj *o = &obj[i];
        o->vo = u16at(x, e), o->no = u16at(x, e + 4), o->vc = u16at(x, e + 8), o->to = u16at(x, e + 12);
        o->qo = u16at(x, e + 16), o->tc = u16at(x, e + 20), o->qc = u16at(x, e + 22);
        if (o->tc == o->to) o->tc = 0;                   /* as the files have it (reevengi) */
        if (o->qc > 0x100) o->qc = 0;
        if (o->vc > 256) { snprintf(why, n, "EMD: object %u has %u vertices", i, o->vc); goto out; }
        /* an offset only matters with something there (an empty list's offset can be anything) */
        if ((o->vc && (!ok(x, (uint64_t)base + o->vo, 8ull * o->vc) || !ok(x, (uint64_t)base + o->no, 8ull * o->vc))) ||
            (o->tc && !ok(x, (uint64_t)base + o->to, 12ull * o->tc)) || (o->qc && !ok(x, (uint64_t)base + o->qo, 16ull * o->qc))) {
            snprintf(why, n, "EMD: object %u past the end", i);
            goto out;
        }
    }
    if (bindings(x, bind, bind_end, obj, objs, m->bones, skin, why, n)) goto out;
    m->parts = objs;
    if (!(m->part = calloc(objs, sizeof *m->part))) { snprintf(why, n, "out of memory"); goto out; }
    for (uint32_t i = 0; i < objs; i++) {
        const Obj *o = &obj[i];
        PeImpPart *pt = &m->part[i];
        pt->tri = calloc(o->tc ? o->tc : 1, sizeof *pt->tri);
        pt->quad = calloc(o->qc ? o->qc : 1, sizeof *pt->quad);
        if (skin[i].skin) {
            pt->tri_skin = calloc(o->tc ? o->tc : 1, sizeof *pt->tri_skin);
            pt->quad_skin = calloc(o->qc ? o->qc : 1, sizeof *pt->quad_skin);
        }
        if (!pt->tri || !pt->quad || (skin[i].skin && (!pt->tri_skin || !pt->quad_skin))) { snprintf(why, n, "out of memory"); goto out; }
        for (int quad = 0; quad < 2; quad++) {
            uint32_t count = quad ? o->qc : o->tc, size = quad ? 16 : 12, at0 = base + (quad ? o->qo : o->to);
            static const int ti[3] = {7, 10, 11}, qi[4] = {10, 11, 14, 15}, tuv[3] = {0, 4, 8}, quv[4] = {0, 4, 8, 12};
            const int k = quad ? 4 : 3, *vi = quad ? qi : ti, *uv = quad ? quv : tuv;
            for (uint32_t f = 0; f < count; f++) {
                const uint8_t *r = x->b + at0 + size * f;
                int bad = 0;
                for (int j = 0; j < k; j++) bad |= r[vi[j]] >= o->vc;
                if (bad) continue;                        /* a corner past the object's vertices */
                PeImpFace *face = quad ? &pt->quad[pt->quads] : &pt->tri[pt->tris];
                for (int j = 0; j < k; j++) {
                    uint32_t v = r[vi[j]];
                    for (int c = 0; c < 3; c++) {
                        int32_t p = s16at(x, base + o->vo + 8 * v + 2 * c);
                        if (skin[i].skin) p += m->pos[0][c];     /* bound: root frame -> model frame */
                        face->v[j][c] = (int16_t)p;
                        face->n[j][c] = s16at(x, base + o->no + 8 * v + 2 * c);
                    }
                    face->uv[j][0] = r[uv[j]], face->uv[j][1] = r[uv[j] + 1];
                    if (skin[i].skin) {
                        uint8_t (*s)[2] = quad ? pt->quad_skin[pt->quads] : pt->tri_skin[pt->tris];
                        s[j][0] = skin[i].skin[v][0], s[j][1] = skin[i].skin[v][1];
                    }
                }
                if (skin[i].skin && !quad) pt->tri_skin[pt->tris][3][0] = 0, pt->tri_skin[pt->tris][3][1] = PE_IMP_NO_BONE;
                face->clut = r[6] & 3, face->page = r[2];
                if (quad) pt->quads++; else pt->tris++;
            }
        }
    }
    rc = 0;
out:
    if (skin) for (uint32_t i = 0; i < objs; i++) free(skin[i].skin);
    free(obj), free(skin);
    return rc;
}

/* ---- the TIM ---- */
int re3_tim_parse(const uint8_t *data, uint32_t size, PeImpModel *m, char *why, size_t n) {
    Buf b = {data, size}, *x = &b;
    NEED(0, 20, "TIM header");
    uint32_t flag = u32at(x, 4);
    if (u32at(x, 0) != 0x10 || (flag & 3) != 1 || !(flag & 8)) { snprintf(why, n, "TIM: not 8-bit with palettes"); return -1; }
    uint32_t len = u32at(x, 8), cw = u16at(x, 16), ch = u16at(x, 18);
    if (!cw || !ch || ch > 256 || cw > 256 || len < 12 + 2 * cw * ch) { snprintf(why, n, "TIM: palettes %ux%u", cw, ch); return -1; }
    NEED(20, 2ull * cw * ch, "TIM palettes");
    NEED(8ull + len, 12, "TIM picture header");
    uint32_t w = 2 * u16at(x, 8 + len + 8), h = u16at(x, 8 + len + 10);
    if (!w || !h || w > 1024 || h > 1024) { snprintf(why, n, "TIM: picture %ux%u", w, h); return -1; }
    NEED(8ull + len + 12, (uint64_t)w * h, "TIM picture");
    m->pal_rows = ch, m->w = w, m->h = h;
    m->pal = calloc(ch, sizeof *m->pal);
    m->image = malloc((size_t)w * h);
    if (!m->pal || !m->image) { snprintf(why, n, "out of memory"); return -1; }
    for (uint32_t r = 0; r < ch; r++) for (uint32_t c = 0; c < cw; c++) m->pal[r][c] = (uint16_t)u16at(x, 20 + 2 * (r * cw + c));
    memcpy(m->image, data + 8 + len + 12, (size_t)w * h);
    return 0;
}

/* ---- moves ---- */
typedef struct { uint32_t edd, emr, frames, size, bones, moves, end; } Set;

static void key_frame(const Buf *x, const Set *s, uint32_t key, int16_t hdr[4], uint16_t (*ang)[3], uint32_t bones) {
    uint32_t at = s->frames + key * s->size;
    for (int i = 0; i < 4; i++) hdr[i] = s16at(x, at + 2 * i);
    for (uint32_t b = 0; b < bones; b++) {
        if (b >= s->bones) { ang[b][0] = ang[b][1] = ang[b][2] = 0; continue; }
        const uint8_t *p = x->b + at + 8 + (b >> 1) * 9;
        if (!(b & 1)) {
            ang[b][0] = (uint16_t)(p[0] | (p[1] & 15) << 8);
            ang[b][1] = (uint16_t)(p[1] >> 4 | p[2] << 4);
            ang[b][2] = (uint16_t)(p[3] | (p[4] & 15) << 8);
        } else {
            ang[b][0] = (uint16_t)(p[4] >> 4 | p[5] << 4);
            ang[b][1] = (uint16_t)(p[6] | (p[7] & 15) << 8);
            ang[b][2] = (uint16_t)(p[7] >> 4 | p[8] << 4);
        }
    }
}

static int16_t signed_angle(double a) {
    long v = lround(fmod(a, 4096.0));
    if (v < 0) v += 4096;
    if (v >= 4096) v -= 4096;
    return (int16_t)(v < 2048 ? v : v - 4096);
}

static int set_open(const Buf *x, uint32_t edd, uint32_t emr, Set *s, char *why, size_t n) {
    memset(s, 0, sizeof *s);
    if (!edd || !emr) return 0;
    NEED(edd, 8, "moves");
    NEED(emr, 8, "key frames header");
    s->edd = edd, s->emr = emr;
    s->moves = u16at(x, edd + 2) / 8;
    if (!s->moves) return 0;                              /* an empty set */
    if (s->moves > PE_IMP_ANIM_MOVES_MAX) { snprintf(why, n, "EMD: %u moves", s->moves); return -1; }
    NEED(edd, 8ull * s->moves, "move list");
    s->frames = emr + u16at(x, emr + 2), s->bones = u16at(x, emr + 4), s->size = u16at(x, emr + 6);
    if (s->bones > BONES_MAX || s->size < 8 + (9 * s->bones + 1) / 2) { snprintf(why, n, "EMD: key frames of %u bytes for %u bones", s->size, s->bones); return -1; }
    if (s->frames >= x->size) { snprintf(why, n, "EMD: key frames past the end"); return -1; }
    s->end = x->size;
    return 0;
}

static int set_move(const Buf *x, const Set *s, uint32_t mv, uint32_t bones, int32_t root_x, int32_t root_z,
                    PeImpMove *out, char *why, size_t n) {
    uint32_t e = s->edd + 8 * mv, steps = u16at(x, e), off = u16at(x, e + 2), first = u16at(x, e + 4);
    memset(out, 0, sizeof *out);
    if (!steps || steps > 2000) return 0;                 /* an empty move keeps the numbering */
    NEED((uint64_t)s->edd + off, 2ull * steps, "move steps");
    uint32_t keys = (s->end - s->frames) / s->size;
    const size_t words = 3 + 3 * (size_t)bones;
    out->data = malloc(steps * words * sizeof *out->data);
    out->event = malloc(steps * sizeof *out->event);
    uint16_t (*a0)[3] = malloc(bones * sizeof *a0), (*a1)[3] = malloc(bones * sizeof *a1);
    int rc = -1;
    if (!out->data || !out->event || !a0 || !a1) { snprintf(why, n, "out of memory"); goto done; }
    uint32_t last = 0;
    for (uint32_t k = 0; k < steps; k++) {
        unsigned w = u16at(x, s->edd + off + 2 * k);
        int16_t h0[4], h1[4];
        int16_t *d = out->data + k * words;
        double t = 0;
        uint32_t key = w & 0xff;
        if (w & 0x100) t = (w & 0xff) / 256.0, key = last;
        else last = key;
        if (first + key + (t > 0) >= keys) { snprintf(why, n, "EMD: move %u names key %u of %u", mv, first + key, keys); goto done; }
        key_frame(x, s, first + key, h0, a0, bones);
        if (t > 0) key_frame(x, s, first + key + 1, h1, a1, bones);
        double height = h0[3] + (t > 0 ? (h1[3] - h0[3]) * t : 0);
        d[0] = (int16_t)root_x, d[1] = (int16_t)height, d[2] = (int16_t)root_z;
        for (uint32_t b = 0; b < bones; b++)
            for (int c = 0; c < 3; c++) {
                double v = a0[b][c];
                if (t > 0) {
                    int diff = (((int)a1[b][c] - (int)a0[b][c] + 2048) % 4096 + 4096) % 4096 - 2048;
                    v += diff * t;
                }
                d[3 + 3 * b + c] = signed_angle(v);
            }
        out->event[k] = (uint16_t)((w >> 12) | (w & 0x800 ? PE_IMP_EVENT_HIT : 0));
    }
    out->frames = steps;
    rc = 0;
done:
    free(a0), free(a1);
    if (rc) free(out->data), free(out->event), memset(out, 0, sizeof *out);
    return rc;
}

int re3_emd_parse(const uint8_t *data, uint32_t size, const uint8_t *tim, uint32_t tim_size,
                  PeImpModel *m, PeImpAnimation *anim, char *why, size_t n) {
    Buf b = {data, size}, *x = &b;
    memset(m, 0, sizeof *m);
    memset(anim, 0, sizeof *anim);
    int rc = -1;
    if (size < 16) { snprintf(why, n, "EMD: %u bytes", size); goto bad; }
    uint32_t d = u32at(x, 0), count = u32at(x, 4), off[15];
    if (count != 15 || !ok(x, d, 60)) { snprintf(why, n, "EMD: %u sections, expected 15", count); goto bad; }
    for (int i = 0; i < 15; i++) {
        off[i] = u32at(x, d + 4 * i);
        if (off[i] >= size) { snprintf(why, n, "EMD: section %d past the end", i); goto bad; }
    }
    /* the binding section ends where the next section starts */
    uint32_t bind_end = size;
    for (int i = 1; i < 15; i++) if (off[i] > off[0] && off[i] < bind_end) bind_end = off[i];
    if (skeleton(x, off[3], m, why, n) || meshes(x, off[14], off[0], bind_end, m, why, n)) goto bad;
    if (!tim || re3_tim_parse(tim, tim_size, m, why, n)) {
        if (!tim) snprintf(why, n, "EMD: no TIM beside it");
        goto bad;
    }
    if (pe_imp_model_check(m, why, n)) goto bad;

    Set set[SETS];
    uint32_t moves = 0;
    for (int s = 0; s < SETS; s++) {
        if (set_open(x, off[2 + 2 * s], off[3 + 2 * s], &set[s], why, n)) goto bad;
        moves += set[s].moves;
    }
    /* a set's key frames end where the next section after its EMR starts */
    for (int s = 0; s < SETS; s++)
        for (int i = 0; i < 15; i++) if (off[i] > set[s].frames && off[i] < set[s].end) set[s].end = off[i];
    if (!moves || moves > PE_IMP_ANIM_MOVES_MAX) { snprintf(why, n, "EMD: %u moves", moves); goto bad; }
    anim->bones = m->bones, anim->fps = 30, anim->moves = moves;
    if (!(anim->move = calloc(moves, sizeof *anim->move))) { snprintf(why, n, "out of memory"); goto bad; }
    uint32_t at = 0;
    for (int s = 0; s < SETS; s++)
        for (uint32_t mv = 0; mv < set[s].moves; mv++, at++)
            if (set_move(x, &set[s], mv, m->bones, m->rel[0][0], m->rel[0][2], &anim->move[at], why, n)) goto bad;
    if (pe_imp_animation_check(anim, why, n)) goto bad;
    return 0;
bad:
    pe_imp_model_free(m);
    pe_imp_animation_free(anim);
    return rc;
}

/* "ROOM/EMD/EM34.EMD" -> "enemy/em34" */
static int enemy_id(const char *path, char id[24]) {
    const char *stem = strrchr(path, '/');
    stem = stem ? stem + 1 : path;
    const char *dot = strrchr(stem, '.');
    if (!dot || dot == stem || dot - stem > 8) return -1;
    int k = snprintf(id, 24, "enemy/");
    for (const char *p = stem; p < dot; p++) id[k++] = (char)(*p >= 'A' && *p <= 'Z' ? *p + 'a' - 'A' : *p);
    id[k] = 0;
    return 0;
}

static int read_emd(const uint8_t *emd, uint32_t emd_size, const uint8_t *tim, uint32_t tim_size,
                    const char *path, PeImpSink *sink, char *why, size_t n) {
    char id[24], moves[40], name[48];
    if (enemy_id(path, id)) { snprintf(why, n, "EMD: odd name %s", path); return -1; }
    PeImpModel *m = calloc(1, sizeof *m);
    PeImpAnimation *a = calloc(1, sizeof *a);
    if (!m || !a) { free(m), free(a); snprintf(why, n, "out of memory"); return -1; }
    if (re3_emd_parse(emd, emd_size, tim_size ? tim : NULL, tim_size, m, a, why, n)) { free(m), free(a); return -1; }
    snprintf(moves, sizeof moves, "%s/moves", id);
    snprintf(name, sizeof name, "%s", id + 6);
    for (char *p = name; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
    if (pe_imp_emit(sink, PE_IMP_MODEL, id, name, m)) { pe_imp_animation_free(a), free(a); snprintf(why, n, "could not catalog %s", id); return -1; }
    strncat(name, " moves", sizeof name - strlen(name) - 1);
    if (pe_imp_emit(sink, PE_IMP_ANIMATION, moves, name, a)) { snprintf(why, n, "could not catalog %s", moves); return -1; }
    return 0;
}

const PeImpReader g_re3_emd_pc = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "pc", .format = "emd",
    .version = 1, .produces = PE_IMP_MODEL | PE_IMP_ANIMATION, .match = "ROOM/EMD/EM??.EMD",
    .tags = "enemy", .companion = ".TIM", .read_pair = read_emd
};
