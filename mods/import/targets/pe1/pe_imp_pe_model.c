/* Parasite Eve's model and animation-row formats, and the maths the bridge uses on them
 * (render_models.py, build_skin.py). Kept to Python's evaluation order: sums start at 0 and add
 * left to right, round() is half to even, // floors. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_pe.h"

static const double PI = 3.141592653589793;   /* Python's math.pi */

static int fits(uint32_t size, uint64_t at, uint64_t len) { return at <= size && len <= size - at; }
static unsigned u8(const uint8_t *b, uint32_t at) { return b[at]; }
static unsigned u16(const uint8_t *b, uint32_t at) { return b[at] | b[at + 1] << 8; }
static int s16(const uint8_t *b, uint32_t at) { return (int16_t)u16(b, at); }
static uint32_t u32(const uint8_t *b, uint32_t at) { return u16(b, at) | (uint32_t)u16(b, at + 2) << 16; }

int pe_imp_word(const PeImpBuf *b, uint32_t at, uint32_t *out) {
    if (!fits(b->n, at, 4)) return -1;
    *out = u32(b->b, at);
    return 0;
}

int pe_imp_records(const PeImpBuf *blob, uint32_t field, PeImpRec **recs) {
    uint32_t d, desc;
    *recs = NULL;
    if (pe_imp_word(blob, 4, &d) || pe_imp_word(blob, d + field, &desc) || (uint64_t)d + field > 0xFFFFFFFFu) return -1;
    uint32_t at = desc & 0x3fffff, n = desc >> 22;
    PeImpRec *r = malloc((n ? n : 1) * sizeof *r);
    if (!r) return -1;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t w;
        if (pe_imp_word(blob, at + 12 * i + 4, &w) || !fits(blob->n, (uint64_t)at + 12 * i + 11, 1)) { free(r); return -1; }
        r[i].off = w & 0xffffff, r[i].slot = (uint8_t)(w >> 24), r[i].group = blob->b[at + 12 * i + 11];
    }
    *recs = r;
    return (int)n;
}

void pe_imp_md_free(PeImpMd *md) {
    free(md->V), free(md->D), free(md->cmds);
    memset(md, 0, sizeof *md);
}

int pe_imp_md_decode(const PeImpBuf *blob, uint32_t off, PeImpMd *md) {
    const uint8_t *b = blob->b;
    memset(md, 0, sizeof *md);
    if (!fits(blob->n, off, 0x1C)) return -1;
    md->parts = b[off + 2], md->verts = (int)u16(b, off + 6);
    for (int k = 0; k < 4; k++) md->pk[k] = (int)u16(b, off + 8 + 2 * k);
    int mcb = (int)u16(b, off + 0x18);
    if (md->parts > PE_IMP_PARTS_MAX) return -1;
    uint64_t at = (uint64_t)off + 0x1C;
    md->nd = md->pk[0] + md->pk[1] + md->pk[2] + md->pk[3];
    uint64_t need = 12ull * md->parts + 12ull * md->verts + 12ull * md->nd + (2 + 2ull * md->parts + 1) * 8 + (uint64_t)(mcb + 3) / 4 * 4;
    if (!fits(blob->n, at, need)) return -1;
    for (int i = 0; i < md->parts; i++) {
        uint32_t a = (uint32_t)at + 12u * (uint32_t)i;
        md->ptab[i].vs = (uint16_t)u16(b, a), md->ptab[i].vc = (uint16_t)u16(b, a + 2), md->ptab[i].vis = b[a + 4];
        md->ptab[i].tz = (int32_t)u32(b, a + 8);
    }
    at += 12ull * md->parts;
    md->V = malloc((md->verts ? md->verts : 1) * sizeof *md->V);
    md->D = malloc((md->nd ? md->nd : 1) * sizeof *md->D);
    md->cmds = malloc(mcb ? (size_t)mcb : 1);
    if (!md->V || !md->D || !md->cmds) { pe_imp_md_free(md); return -1; }
    for (int i = 0; i < md->verts; i++)
        for (int c = 0; c < 3; c++) md->V[i][c] = (int16_t)s16(b, (uint32_t)at + 8u * (uint32_t)i + 2u * (uint32_t)c);
    at += 8ull * md->verts;
    at += 4ull * md->verts;
    for (int i = 0; i < md->nd; i++) {
        uint32_t a = (uint32_t)at + 12u * (uint32_t)i;
        md->D[i].kind = b[a + 3];
        for (int k = 0; k < 4; k++) md->D[i].idx[k] = (uint16_t)u16(b, a + 4 + 2u * (uint32_t)k);
    }
    at += 12ull * md->nd;
    at += (2 + 2ull * md->parts + 1) * 8;
    md->ncmds = mcb;
    for (int i = 0; i < mcb; i++) md->cmds[i] = (int8_t)b[at + (uint32_t)i];
    return 0;
}

int pe_imp_md_extent(const PeImpBuf *blob, uint32_t off, uint32_t *end, uint32_t *uv_start) {
    if (!fits(blob->n, off, 0x1C)) return -1;
    const uint8_t *b = blob->b;
    uint64_t parts = b[off + 2], verts = u16(b, off + 6), pk[4], mcb = u16(b, off + 0x18);
    for (int k = 0; k < 4; k++) pk[k] = u16(b, off + 8 + 2u * (uint32_t)k);
    uint64_t at = 0x1C + 12 * parts + 8 * verts + 4 * verts + 12 * (pk[0] + pk[1] + pk[2] + pk[3]) + (2 + 2 * parts + 1) * 8;
    at += (mcb + 3) / 4 * 4;
    *uv_start = (uint32_t)at;
    *end = (uint32_t)(at + 16 * pk[0] + 12 * pk[1]);
    return 0;
}

int pe_imp_anim_frame(const PeImpBuf *blob, uint32_t off, int frame, PeImpFrame *out) {
    const uint8_t *b = blob->b;
    uint32_t n = blob->n;
    if (!fits(n, off, 12)) return -1;
    unsigned flags = b[off], last = b[off + 1], frames = b[off + 2];
    int mx = (int)frames - 1 > 0 ? (int)frames - 1 : 0;
    frame = frame < 0 ? 0 : frame > mx ? mx : frame;
    if (last + 1 > PE_IMP_PARTS_MAX) return -1;
    uint64_t cur = (uint64_t)off + 12, stride = (frames >> 1) + 1;
    for (int k = 0; k < 3; k++) {
        if (!fits(n, cur, 4)) return -1;
        int mark = s16(b, (uint32_t)cur);
        if (!mark && !fits(n, cur + 2 + 2 * (uint64_t)frame, 2)) return -1;
        out->tr[k] = mark ? s16(b, (uint32_t)cur + 2) : s16(b, (uint32_t)(cur + 2 + 2 * (uint64_t)frame));
        cur += mark ? 4 : stride * 4;
    }
    int is_short = (flags & 3) == 2;
    uint64_t bstride = !is_short ? ((frames >> 2) + 1) * 4ull : stride * 4;
    out->n = (int)last + 1;
    for (unsigned bone = 0; bone <= last; bone++) {
        for (int k = 0; k < 3; k++) {
            if (is_short) {
                if (!fits(n, cur, 4)) return -1;
                int mark = s16(b, (uint32_t)cur);
                if (!mark && !fits(n, cur + 2 + 2 * (uint64_t)frame, 2)) return -1;
                out->ang[bone][k] = (int)(mark ? u16(b, (uint32_t)cur + 2) : u16(b, (uint32_t)(cur + 2 + 2 * (uint64_t)frame)));
                cur += mark ? 4 : bstride;
            } else {
                if (!fits(n, cur, 2)) return -1;
                unsigned mark = b[cur];
                if (!mark && !fits(n, cur + 1 + (uint64_t)frame, 1)) return -1;
                out->ang[bone][k] = (int)(mark ? b[cur + 1] : b[cur + 1 + (uint64_t)frame]) * 16;
                cur += mark ? 4 : bstride;
            }
        }
    }
    return 0;
}

int pe_imp_row_length(const uint8_t *d, uint32_t size, uint32_t off, uint32_t *len) {
    if (!fits(size, off, 3)) return -1;
    unsigned flags = d[off], last = d[off + 1], frames = d[off + 2];
    uint64_t stride = ((frames >> 1) + 1) * 4ull;
    int is_short = (flags & 3) == 2;
    uint64_t bstride = is_short ? stride : ((frames >> 2) + 1) * 4ull, cur = (uint64_t)off + 12;
    for (int k = 0; k < 3; k++) {
        if (!fits(size, cur, 2)) return -1;
        cur += s16(d, (uint32_t)cur) ? 4 : stride;
    }
    for (unsigned k = 0; k < (last + 1) * 3; k++) {
        if (!fits(size, cur, is_short ? 2 : 1)) return -1;
        cur += (is_short ? s16(d, (uint32_t)cur) != 0 : d[cur] != 0) ? 4 : bstride;
    }
    *len = (uint32_t)(cur - off);
    return 0;
}

uint32_t pe_imp_fingerprint(const uint8_t *row, uint32_t len) {
    uint32_t h = 0x811C9DC5u;
    for (uint32_t i = 0; i < len; i++) h = (h ^ row[i]) * 0x01000193u;
    return h;
}

/* ---- growing byte buffers ---- */
typedef struct { uint8_t *b; size_t n, cap; int bad; } Out;
static void out_put(Out *o, const void *p, size_t n) {
    if (o->bad) return;
    if (o->n + n > o->cap) {
        size_t cap = (o->n + n) * 2 + 256;
        uint8_t *g = realloc(o->b, cap);
        if (!g) { o->bad = 1; return; }
        o->b = g, o->cap = cap;
    }
    memcpy(o->b + o->n, p, n), o->n += n;
}
static void o8(Out *o, unsigned v) { uint8_t c = (uint8_t)v; out_put(o, &c, 1); }
static void o16(Out *o, unsigned v) { o8(o, v), o8(o, v >> 8); }
static void o32(Out *o, uint32_t v) { o16(o, v & 0xffff), o16(o, v >> 16); }
static int out_done(Out *o, PeImpBuf *b) {
    if (o->bad) { free(o->b); b->b = NULL, b->n = 0; return -1; }
    b->b = o->b ? o->b : malloc(1), b->n = (uint32_t)o->n;
    return b->b ? 0 : -1;
}

static long long floordiv(long long a, long long b) { long long q = a / b; return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q; }

/* encode_row: angles are flat, [frame * parts + part][axis]. */
int pe_imp_encode_row(int frames, const double (*tr)[3], const int (*ang)[3], int parts,
                      const uint16_t hit[4], PeImpBuf *res) {
    Out o = {0};
    o8(&o, 1), o8(&o, (unsigned)(parts - 1)), o8(&o, (unsigned)frames), o8(&o, 0);
    for (int k = 0; k < 4; k++) o16(&o, hit[k]);
    for (int axis = 0; axis < 3; axis++) {
        int same = 1, v0 = 0;
        for (int f = 0; f < frames; f++) {
            double x = tr[f][axis];
            long long t = (long long)x;                      /* int(): toward zero */
            int v = (int)(t < -32768 ? -32768 : t > 32767 ? 32767 : t);
            if (!f) v0 = v;
            else same &= v == v0;
        }
        if (same) { o16(&o, 1), o16(&o, (uint16_t)v0); continue; }
        size_t start = o.n, size = (size_t)((frames >> 1) + 1) * 4;
        o16(&o, 0);
        for (int f = 0; f < frames; f++) {
            long long t = (long long)tr[f][axis];
            o16(&o, (uint16_t)(int)(t < -32768 ? -32768 : t > 32767 ? 32767 : t));
        }
        while (o.n - start < size) o8(&o, 0);
    }
    for (int bone = 0; bone < parts; bone++) {
        for (int axis = 0; axis < 3; axis++) {
            int same = 1, v0 = 0;
            for (int f = 0; f < frames; f++) {
                int v = (int)(floordiv((long long)ang[f * parts + bone][axis] + 8, 16) & 255);
                if (!f) v0 = v;
                else same &= v == v0;
            }
            if (same) { o8(&o, 1), o8(&o, (unsigned)v0), o8(&o, 0), o8(&o, 0); continue; }
            size_t start = o.n, size = (size_t)((frames >> 2) + 1) * 4;
            o8(&o, 0);
            for (int f = 0; f < frames; f++) o8(&o, (unsigned)(floordiv((long long)ang[f * parts + bone][axis] + 8, 16) & 255));
            while (o.n - start < size) o8(&o, 0);
        }
    }
    while (o.n % 4) o8(&o, 0);
    return out_done(&o, res);
}

int pe_imp_write_blob(const PeImpBlobModel *m, int nm, const PeImpBlobRow *r, int nr, PeImpBuf *res) {
    Out o = {0};
    for (int i = 0; i < 8; i++) o8(&o, 0);
    uint32_t *rec = malloc((size_t)(nm + nr + 1) * 3 * sizeof *rec);
    if (!rec) return -1;
    int k = 0;
    for (int i = 0; i < nm; i++) {
        uint32_t off = (uint32_t)o.n;
        out_put(&o, m[i].raw, m[i].n);
        while (o.n % 4) o8(&o, 0);
        rec[3 * k] = m[i].n, rec[3 * k + 1] = off | (uint32_t)m[i].slot << 24, rec[3 * k + 2] = m[i].trailing, k++;
    }
    for (int i = 0; i < nr; i++) {
        uint32_t off = (uint32_t)o.n;
        out_put(&o, r[i].raw, r[i].n);
        while (o.n % 4) o8(&o, 0);
        rec[3 * k] = r[i].n, rec[3 * k + 1] = off | (uint32_t)r[i].id << 24, rec[3 * k + 2] = 0, k++;
    }
    uint32_t dir_off = (uint32_t)o.n, tables = dir_off + 13 * 4;
    uint32_t at_m = tables, at_r = at_m + 12u * (uint32_t)nm, end = at_r + 12u * (uint32_t)nr;
    uint32_t words[13] = {0, end, end, at_m | (uint32_t)nm << 22, at_r | (uint32_t)nr << 22};
    for (int i = 5; i < 13; i++) words[i] = end;
    for (int i = 0; i < 13; i++) o32(&o, words[i]);
    for (int i = 0; i < 3 * k; i++) o32(&o, rec[i]);
    free(rec);
    if (!o.bad) {
        for (int i = 0; i < 4; i++) o.b[i] = (uint8_t)(end >> 8 * i), o.b[4 + i] = (uint8_t)(dir_off >> 8 * i);
    }
    return out_done(&o, res);
}

/* ---- maths ---- */
void pe_imp_mm(const PeM3 a, const PeM3 b, PeM3 out) {
    PeM3 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r[i][j] = ((0.0 + a[i][0] * b[0][j]) + a[i][1] * b[1][j]) + a[i][2] * b[2][j];
    memcpy(out, r, sizeof r);
}

void pe_imp_mv(const PeM3 a, const double v[3], double out[3]) {
    double r[3];
    for (int i = 0; i < 3; i++) r[i] = ((0.0 + a[i][0] * v[0]) + a[i][1] * v[1]) + a[i][2] * v[2];
    memcpy(out, r, sizeof r);
}

void pe_imp_transpose(const PeM3 a, PeM3 out) {
    PeM3 r;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r[i][j] = a[j][i];
    memcpy(out, r, sizeof r);
}

void pe_imp_rot_yxz(const int a[3], PeM3 out) {
    double x = (double)(a[0] * 2) * PI / 4096, y = (double)(a[1] * 2) * PI / 4096, z = (double)(a[2] * 2) * PI / 4096;
    double cx = cos(x), sx = sin(x), cy = cos(y), sy = sin(y), cz = cos(z), sz = sin(z);
    PeM3 Ry = {{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}};
    PeM3 Rx = {{1, 0, 0}, {0, cx, -sx}, {0, sx, cx}};
    PeM3 Rz = {{cz, -sz, 0}, {sz, cz, 0}, {0, 0, 1}};
    PeM3 t;
    pe_imp_mm(Ry, Rx, t);
    pe_imp_mm(t, Rz, out);
}

static int round_turn(double a) { return (int)((long long)nearbyint(a * 4096 / (2 * PI)) & 4095); }

/* Inverse of RotMatrixYXZ (M = Ry Rx Rz). At a quarter-turn pitch (gimbal lock) y and z only
 * matter together and the bridge's atan2s read rounding noise, giving an arbitrary twist (and
 * different bits on every maths library). There z = 0 and y carries the whole turn: with
 * x = +-90 degrees, M[0][0] = cos(y -+ z) and M[0][1] = +-sin(y -+ z). Everywhere else this is the
 * bridge's to_yxz exactly. */
#define GIMBAL_EPS 1e-6
void pe_imp_to_yxz(const PeM3 m, int out[3]) {
    double sx = -m[1][2];
    sx = sx < -1.0 ? -1.0 : sx > 1.0 ? 1.0 : sx;
    double x = asin(sx), y, z;
    if (sqrt(m[0][2] * m[0][2] + m[2][2] * m[2][2]) < GIMBAL_EPS) {
        y = atan2(sx * m[0][1], m[0][0]);
        z = 0;
    } else {
        y = atan2(m[0][2], m[2][2]);
        z = atan2(m[1][0], m[1][1]);
    }
    out[0] = round_turn(x), out[1] = round_turn(y), out[2] = round_turn(z);
}

static const PeM3 I3 = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

void pe_imp_pose_mats(const PeImpMd *md, const int tr[3], const int (*ang)[3], int nang,
                      PeM3 *rot, double (*pos)[3], uint8_t *has) {
    PeM3 cr, stack_r[64];
    double ct[3] = {0, 0, 0}, stack_t[64][3];
    int sp = 0;
    memcpy(cr, I3, sizeof cr);
    memset(has, 0, PE_IMP_PARTS_MAX);
    for (int i = 0; i < md->ncmds; i++) {
        int c = md->cmds[i];
        if (c == -1) { if (sp < 64) memcpy(stack_r[sp], cr, sizeof cr), memcpy(stack_t[sp], ct, sizeof ct), sp++; continue; }
        if (c == -2) { if (sp > 0) sp--, memcpy(cr, stack_r[sp], sizeof cr), memcpy(ct, stack_t[sp], sizeof ct); continue; }
        if (c < 0 || c >= PE_IMP_PARTS_MAX) continue;
        PeM3 R, nr;
        if (c < nang) pe_imp_rot_yxz(ang[c], R);
        else memcpy(R, I3, sizeof R);
        pe_imp_mm(cr, R, nr);
        double vec[3] = {c == 0 ? tr[0] : 0, c == 0 ? tr[1] : 0, c == 0 ? tr[2] : (double)md->ptab[c].tz}, mvv[3];
        pe_imp_mv(cr, vec, mvv);
        double nt[3] = {mvv[0] + ct[0], mvv[1] + ct[1], mvv[2] + ct[2]};
        memcpy(cr, nr, sizeof cr), memcpy(ct, nt, sizeof ct);
        memcpy(rot[c], cr, sizeof cr), memcpy(pos[c], ct, sizeof ct), has[c] = 1;
    }
}

void pe_imp_pose(const PeImpMd *md, const int tr[3], const int (*ang)[3], int nang, double (*w)[3], uint8_t *valid) {
    PeM3 rot[PE_IMP_PARTS_MAX];
    double pos[PE_IMP_PARTS_MAX][3];
    uint8_t has[PE_IMP_PARTS_MAX];
    pe_imp_pose_mats(md, tr, ang, nang, rot, pos, has);
    memset(valid, 0, (size_t)md->verts);
    for (int p = 0; p < md->parts; p++) {
        const double *t = has[p] ? pos[p] : (const double[3]){0, 0, 0};
        const double (*R)[3] = has[p] ? (const double (*)[3])rot[p] : I3;
        for (int i = md->ptab[p].vs; i < md->ptab[p].vs + md->ptab[p].vc; i++) {
            if (i >= md->verts) continue;
            double v[3] = {md->V[i][0], md->V[i][1], md->V[i][2]}, r[3];
            pe_imp_mv(R, v, r);
            w[i][0] = r[0] + t[0], w[i][1] = r[1] + t[1], w[i][2] = r[2] + t[2];
            valid[i] = 1;
        }
    }
}
