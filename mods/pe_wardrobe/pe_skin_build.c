/* The skins builder target (see pe_skin_build.h). A port of tools/re2-skin/build_re2_skin.py:
 * the same steps in the same order, Python's evaluation order and rounding kept, so the files are
 * byte for byte the same (texture.bin aside: our colour reduction replaces Pillow's).
 *
 *   1. Aya from the PE disc: her jacket bundle (image 20), idle pose (row 0x15), world matrices.
 *   2. The fit: each source part placed over the Aya bone it matches (rig profile), in that bone's frame.
 *   3. Faces hidden inside the body left out; texture groups packed into Aya's 4-bit page.
 *   4. model.bin (Aya's bundle with the new mesh), day1-8.bin, texture.bin, meta.bin.
 *   5. rooms.bin: the rows rooms install for Aya's 31- and 23-part skeletons, retarget to 29. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_skin_build.h"

#define AYA_ENTRY 20
#define AYA_TEX 15
#define IDLE_ROW 0x15
#define WALK_ROW 0x16
#define AYA_FOOT 24
#define AYA_THIGH_L 22
#define AYA_THIGH_R 26
#define FP_BYTES 372
#define MAXP PE_IMP_PARTS_MAX

typedef double V3[3];

/* ---- vector helpers, as build_re2_skin.py ---- */
static void vadd(const double *a, const double *b, double *o) { V3 r = {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; memcpy(o, r, sizeof r); }
static void vsub(const double *a, const double *b, double *o) { V3 r = {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; memcpy(o, r, sizeof r); }
static void vmul(const double *a, double k, double *o) { V3 r = {a[0] * k, a[1] * k, a[2] * k}; memcpy(o, r, sizeof r); }
static double vdot(const double *a, const double *b) { return ((0.0 + a[0] * b[0]) + a[1] * b[1]) + a[2] * b[2]; }
static void vcross(const double *a, const double *b, double *o) {
    V3 r = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    memcpy(o, r, sizeof r);
}
static double vnorm(const double *a) { return sqrt(vdot(a, a)); }
static void vunit(const double *a, double *o) { double n = vnorm(a); if (n == 0) n = 1.0; V3 r = {a[0] / n, a[1] / n, a[2] / n}; memcpy(o, r, sizeof r); }
static void vflat(const double *a, double *o) { V3 r = {a[0], 0, a[2]}; memcpy(o, r, sizeof r); }
static double heading(const double *v) { return atan2(v[2], v[0]); }
static void yaw(double a, PeM3 o) { double c = cos(a), s = sin(a); PeM3 r = {{c, 0, -s}, {0, 1, 0}, {s, 0, c}}; memcpy(o, r, sizeof r); }
static void cols3(const double *a, const double *b, const double *c, PeM3 o) {
    for (int i = 0; i < 3; i++) o[i][0] = a[i], o[i][1] = b[i], o[i][2] = c[i];
}
static void about(const double *axis, double a, PeM3 o) {
    V3 u;
    vunit(axis, u);
    double x = u[0], y = u[1], z = u[2], c = cos(a), s = sin(a), t = 1 - cos(a);
    PeM3 r = {{t * x * x + c, t * x * y - s * z, t * x * z + s * y},
              {t * x * y + s * z, t * y * y + c, t * y * z - s * x},
              {t * x * z - s * y, t * y * z + s * x, t * z * z + c}};
    memcpy(o, r, sizeof r);
}
static void rot_between(const double *a0, const double *b0, PeM3 o) {
    V3 a, b, v, p;
    vunit(a0, a), vunit(b0, b);
    vcross(a, b, v);
    double c = vdot(a, b);
    if (c < -0.999999) {
        V3 t, x = {1, 0, 0}, y = {0, 1, 0};
        vcross(a, fabs(a[0]) < 0.9 ? x : y, t);
        vunit(t, p);
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) o[i][j] = 2 * p[i] * p[j] - (i == j ? 1 : 0);
        return;
    }
    double k = 1 / (1 + c);
    PeM3 vx = {{0, -v[2], v[1]}, {v[2], 0, -v[0]}, {-v[1], v[0], 0}}, vx2;
    pe_imp_mm(vx, vx, vx2);
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) o[i][j] = ((i == j ? 1 : 0) + vx[i][j]) + vx2[i][j] * k;
}

/* ---- Aya ---- */
typedef struct {
    PeImpBuf blob;
    uint32_t off, trailing;
    PeImpMd md;
    int row_of[256];
    PeImpFrame idle;
    PeM3 rot[MAXP];
    double pos[MAXP][3];
    uint8_t has[MAXP];
    double (*world)[3];
    uint8_t *wvalid;
    PeImpBuf raw;
} Aya;

static void aya_free(Aya *a) {
    pe_imp_buf_free(&a->blob), pe_imp_md_free(&a->md), free(a->world), free(a->wvalid), pe_imp_buf_free(&a->raw);
}

static int rows_of(const PeImpBuf *blob, int row_of[256]) {
    PeImpRec *r;
    int n = pe_imp_records(blob, 0x10, &r);
    for (int i = 0; i < 256; i++) row_of[i] = -1;
    if (n < 0) return -1;
    for (int i = 0; i < n; i++) row_of[r[i].slot] = (int)r[i].off;      /* a dict: the last wins */
    free(r);
    return 0;
}

static int first_model(const PeImpBuf *blob, uint32_t *off, uint32_t *trailing) {
    PeImpRec *r;
    int n = pe_imp_records(blob, 0x0C, &r);
    if (n < 1) { free(r); return -1; }
    *off = r[0].off, *trailing = r[0].group;
    free(r);
    return 0;
}

static int load_aya(PeImpPe *pe, Aya *a) {
    memset(a, 0, sizeof *a);
    uint32_t end, uv;
    if (pe_imp_pe_image(pe, AYA_ENTRY, &a->blob) || first_model(&a->blob, &a->off, &a->trailing) ||
        pe_imp_md_decode(&a->blob, a->off, &a->md) || rows_of(&a->blob, a->row_of) || a->row_of[IDLE_ROW] < 0 ||
        pe_imp_anim_frame(&a->blob, (uint32_t)a->row_of[IDLE_ROW], 0, &a->idle) ||
        pe_imp_md_extent(&a->blob, a->off, &end, &uv) || (uint64_t)a->off + end > a->blob.n)
        return -1;
    pe_imp_pose_mats(&a->md, a->idle.tr, a->idle.ang, a->idle.n, a->rot, a->pos, a->has);
    a->world = calloc((size_t)a->md.verts + 1, sizeof *a->world);
    a->wvalid = calloc((size_t)a->md.verts + 1, 1);
    a->raw.b = malloc(end ? end : 1), a->raw.n = end;
    if (!a->world || !a->wvalid || !a->raw.b) return -1;
    pe_imp_pose(&a->md, a->idle.tr, a->idle.ang, a->idle.n, a->world, a->wvalid);
    memcpy(a->raw.b, a->blob.b + a->off, end);
    return 0;
}

static void mesh_centre(const Aya *a, int part, double *o) {
    int vs = a->md.ptab[part].vs, vc = a->md.ptab[part].vc;
    V3 s = {0, 0, 0};
    for (int k = 0; k < 3; k++) {
        double t = 0;
        for (int i = vs; i < vs + vc; i++) t = t + a->world[i][k];
        s[k] = t / vc;
    }
    memcpy(o, s, sizeof s);
}

static void aya_point(const Aya *a, const PeImpSeg *p, double *o) {
    if (p->kind == PE_SEG_BONE) memcpy(o, a->pos[p->a], sizeof(V3));
    else if (p->kind == PE_SEG_MID) { V3 t; vadd(a->pos[p->a], a->pos[p->b], t); vmul(t, 0.5, o); }
    else mesh_centre(a, p->a, o);
}

/* ---- the source model ---- */
/* every corner of a part's faces, triangles first then quads (as the Python's ('tris', 'quads')) */
#define FOR_CORNERS(m, P, F, k, ...)                                                       \
    for (int kind_ = 0; kind_ < 2; kind_++) {                                              \
        const PeImpPart *pt_ = &(m)->part[P];                                              \
        int nf_ = kind_ ? (int)pt_->quads : (int)pt_->tris, nc_ = kind_ ? 4 : 3;           \
        for (int fi_ = 0; fi_ < nf_; fi_++) {                                              \
            const PeImpFace *F = kind_ ? &pt_->quad[fi_] : &pt_->tri[fi_];                 \
            for (int k = 0; k < nc_; k++) __VA_ARGS__                                      \
        }                                                                                  \
    }

static void src_point(const PeImpModel *m, const PeImpSeg *p, double *o) {
    if (p->kind == PE_SEG_BONE) { o[0] = m->pos[p->a][0], o[1] = m->pos[p->a][1], o[2] = m->pos[p->a][2]; return; }
    if (p->kind == PE_SEG_MID) {
        for (int c = 0; c < 3; c++) o[c] = (double)(m->pos[p->a][c] + m->pos[p->b][c]) * 0.5;
        return;
    }
    long long s[3] = {0, 0, 0}, n = 0;
    FOR_CORNERS(m, p->a, f, k, { for (int c = 0; c < 3; c++) s[c] += m->pos[p->a][c] + f->v[k][c]; n++; })
    for (int c = 0; c < 3; c++) o[c] = n ? (double)s[c] / (double)n : 0;
}

/* ---- the fit (frames, place) ---- */
typedef struct {
    int bone, feet, hips;
    V3 c0, a0, u, base;
    double k, w;
    PeM3 Q;
} Placed;

typedef struct {
    PeM3 G;
    double scale;
    V3 side;
    int swap;
    Placed part[PE_RIG_PARTS];
    const PeImpRig *rig;
} Fit;

static void aya_toe(const Aya *a, int foot, double *o) {
    int vs = a->md.ptab[foot].vs, vc = a->md.ptab[foot].vc;
    V3 best = {0, 0, 0}, f;
    double bn = -1;
    for (int i = vs; i < vs + vc; i++) {
        V3 d;
        vsub(a->world[i], a->pos[foot], d);
        vflat(d, f);
        double n = vnorm(f);
        if (n > bn) bn = n, memcpy(best, d, sizeof d);
    }
    vflat(best, f);
    vunit(f, o);
}

static int knee_forward(const Aya *a, int thigh, int shin, double *o) {
    PeM3 rest, t;
    pe_imp_transpose(a->rot[thigh], t);
    pe_imp_mm(t, a->rot[shin], rest);
    V3 acc = {0, 0, 0};
    int off = a->row_of[WALK_ROW];
    if (off < 0) return -1;
    int frames = a->blob.b[off + 2];
    for (int fr = 0; fr < frames; fr++) {
        PeImpFrame f;
        PeM3 rot[MAXP], d, d2, rt;
        double pos[MAXP][3];
        uint8_t has[MAXP];
        if (pe_imp_anim_frame(&a->blob, (uint32_t)off, fr, &f)) return -1;
        pe_imp_pose_mats(&a->md, f.tr, f.ang, f.n, rot, pos, has);
        pe_imp_transpose(rot[thigh], t);
        pe_imp_mm(t, rot[shin], d);
        pe_imp_transpose(rest, rt);
        pe_imp_mm(d, rt, d2);
        V3 ax = {d2[2][1] - d2[1][2], d2[0][2] - d2[2][0], d2[1][0] - d2[0][1]}, w;
        pe_imp_mv(a->rot[thigh], ax, w);
        V3 s;
        vmul(w, vdot(w, acc) >= 0 ? 1 : -1, s);
        vadd(acc, s, acc);
    }
    V3 up = {0, 1, 0}, c, f, toe;
    vcross(acc, up, c);
    vunit(c, f);
    aya_toe(a, shin + 1, toe);
    if (vdot(f, toe) > 0) memcpy(o, f, sizeof f);
    else vmul(f, -1, o);
    return 0;
}

static int knee_of(int bone, int *thigh, int *shin) {
    if (bone == 22 || bone == 23) { *thigh = 22, *shin = 23; return 1; }
    if (bone == 26 || bone == 27) { *thigh = 26, *shin = 27; return 1; }
    return 0;
}

static void frames_fit(const PeImpModel *m, const Aya *a, const PeImpRig *rig, Fit *fit) {
    PeImpSeg am = {PE_SEG_MESH, AYA_FOOT, 0}, rm = {PE_SEG_MESH, rig->ref_foot, 0};
    V3 fa, fr, down = {0, 1, 0}, t, ca, cr, rp;
    aya_point(a, &am, t);
    vsub(t, a->pos[AYA_FOOT], fa);
    fa[1] = 0;
    vunit(fa, fa);
    src_point(m, &rm, t);
    for (int c = 0; c < 3; c++) rp[c] = m->pos[rig->ref_foot][c];
    vsub(t, rp, fr);
    fr[1] = 0;
    vunit(fr, fr);
    PeM3 A, B, Bt;
    vcross(fa, down, ca);
    vcross(fr, down, cr);
    cols3(fa, down, ca, A);
    cols3(fr, down, cr, B);
    pe_imp_transpose(B, Bt);
    pe_imp_mm(A, Bt, fit->G);
    double ground_a = 0;
    int first = 1;
    for (int i = 0; i < a->md.verts; i++)
        if (a->wvalid[i] && (first || a->world[i][1] > ground_a)) ground_a = a->world[i][1], first = 0;
    long long ground_r = 0;
    first = 1;
    FOR_CORNERS(m, rig->ref_foot, f, k, {
        long long y = (long long)m->pos[rig->ref_foot][1] + f->v[k][1];
        if (first || y > ground_r) ground_r = y, first = 0;
    })
    double hip_a = ground_a - a->pos[AYA_THIGH_L][1];
    long long hip_r = ground_r - m->pos[rig->ref_thigh_l][1];
    fit->scale = hip_a / (double)hip_r;
    V3 side, gp, p2 = {m->pos[rig->ref_thigh_l][0], m->pos[rig->ref_thigh_l][1], m->pos[rig->ref_thigh_l][2]};
    vcross(fa, down, side);
    pe_imp_mv(fit->G, p2, gp);
    fit->swap = (vdot(gp, side) > 0) != (vdot(a->pos[AYA_THIGH_L], side) > 0);
    V3 d, hr, ha;
    for (int c = 0; c < 3; c++) d[c] = (double)(m->pos[rig->ref_thigh_r][c] - m->pos[rig->ref_thigh_l][c]);
    pe_imp_mv(fit->G, d, hr);
    vsub(a->pos[AYA_THIGH_R], a->pos[AYA_THIGH_L], ha);
    if (fit->swap) vmul(hr, -1, hr);
    PeM3 Y;
    yaw(heading(ha) - heading(hr), Y);
    pe_imp_mm(Y, fit->G, fit->G);
}

static double span_of(const double (*pts)[3], int n, const double *d) {
    double mx = 0, mn = 0;
    for (int i = 0; i < n; i++) {
        double x = vdot(pts[i], d);
        if (!i || x > mx) mx = x;
        if (!i || x < mn) mn = x;
    }
    return mx - mn;
}

static int place(const PeImpModel *m, const Aya *a, const PeImpRig *rig, Fit *fit, char *log, size_t logn) {
    static const PeM3 UPRIGHT = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    PeM3 turns[PE_RIG_PARTS];
    uint8_t turned[PE_RIG_PARTS] = {0};
    V3 t24, t28, fwd, up = {0, 1, 0}, tmp;
    aya_toe(a, 24, t24), aya_toe(a, 28, t28);
    vadd(t24, t28, tmp);
    vunit(tmp, fwd);
    vcross(fwd, up, tmp);
    vunit(tmp, fit->side);
    fit->rig = rig;
    int order[PE_RIG_PARTS], no = 0;
    for (int p = 0; p < rig->parts; p++) if (rig->end_piece[p] == -2) order[no++] = p;
    for (int p = 0; p < rig->parts; p++) if (rig->end_piece[p] != -2) order[no++] = p;
    size_t used = 0;
    if (logn) log[0] = 0;
    for (int oi = 0; oi < no; oi++) {
        int part = order[oi];
        int src = fit->swap && rig->swap[part] >= 0 ? rig->swap[part] : part;
        Placed *pl = &fit->part[part];
        int is_torso = part == rig->torso[0] || part == rig->torso[1];
        pl->bone = rig->part[src].bone;
        const PeImpSeg *rs = is_torso ? rig->torso_src : rig->part[part].src;
        const PeImpSeg *as = is_torso ? rig->torso_dst : rig->part[src].dst;
        V3 c1, a1, dcr, dc, da;
        src_point(m, &rs[0], pl->c0), src_point(m, &rs[1], c1);
        aya_point(a, &as[0], pl->a0), aya_point(a, &as[1], a1);
        vsub(c1, pl->c0, dcr);
        pe_imp_mv(fit->G, dcr, dc);
        vsub(a1, pl->a0, da);
        vunit(dc, pl->u);
        pl->feet = rig->foot[part], pl->hips = part == rig->hips_part;
        if (pl->feet) {
            int cap = (int)(m->part[part].tris * 3 + m->part[part].quads * 4), np = 0;
            double (*pts)[3] = malloc((size_t)(cap ? cap : 1) * sizeof *pts);
            if (!pts) return -1;
            FOR_CORNERS(m, part, f, k, {
                V3 p, q;
                for (int c = 0; c < 3; c++) p[c] = (double)(m->pos[part][c] + f->v[k][c]);
                vsub(p, pl->c0, q);
                pe_imp_mv(fit->G, q, pts[np]);
                np++;
            })
            int best = 0;
            double bn = -1;
            for (int i = 0; i < np; i++) { V3 f; vflat(pts[i], f); double nn = vnorm(f); if (nn > bn) bn = nn, best = i; }
            V3 toe;
            aya_toe(a, pl->bone, toe);
            yaw(heading(toe) - heading(pts[best]), pl->Q);
            int vs = a->md.ptab[pl->bone].vs, vc = a->md.ptab[pl->bone].vc;
            double fl = 0, dmax = 0;
            for (int i = vs; i < vs + vc; i++) if (i == vs || a->world[i][1] > fl) fl = a->world[i][1];
            fl -= pl->a0[1];
            for (int i = 0; i < np; i++) if (!i || pts[i][1] > dmax) dmax = pts[i][1];
            pl->u[0] = 0, pl->u[1] = 1, pl->u[2] = 0;
            pl->k = fl / (fit->scale * dmax);
            free(pts);
        } else if (rig->end_piece[part] != -2) {
            int par = rig->end_piece[part];
            memcpy(pl->Q, par >= 0 && turned[par] ? (const double (*)[3])turns[par] : UPRIGHT, sizeof pl->Q);
            pl->k = 1.0;
        } else {
            rot_between(dc, da, pl->Q);
            int th, sh;
            if (knee_of(pl->bone, &th, &sh)) {
                V3 want, have, ff, ud, p0, p1, cr, t1;
                if (knee_forward(a, th, sh, want)) return -1;
                vflat(fwd, ff);
                pe_imp_mv(pl->Q, ff, have);
                vunit(da, ud);
                vmul(ud, vdot(have, ud), t1), vsub(have, t1, p0);
                vunit(da, ud);
                vmul(ud, vdot(want, ud), t1), vsub(want, t1, p1);
                vcross(p0, p1, cr);
                vunit(da, ud);
                PeM3 Ab;
                about(da, atan2(vdot(cr, ud), vdot(p0, p1)), Ab);
                pe_imp_mm(Ab, pl->Q, pl->Q);
            }
            double den = fit->scale * vnorm(dc);
            if (den == 0) den = 1;
            double r = vnorm(da) / den;
            r = r < 1.6 ? r : 1.6;
            pl->k = r > 0.6 ? r : 0.6;
        }
        memcpy(turns[part], pl->Q, sizeof pl->Q), turned[part] = 1;
        for (int c = 0; c < 3; c++) pl->base[c] = m->pos[part][c];
        pl->w = 1.0;
        int th, sh;
        if (knee_of(pl->bone, &th, &sh)) {
            int cap = (int)(m->part[part].tris * 3 + m->part[part].quads * 4), np = 0;
            int vs = a->md.ptab[pl->bone].vs, vc = a->md.ptab[pl->bone].vc;
            double (*pts)[3] = malloc((size_t)(cap ? cap : 1) * sizeof *pts);
            if (!pts) return -1;
            FOR_CORNERS(m, part, f, k, {
                V3 p;
                for (int c = 0; c < 3; c++) p[c] = (double)(m->pos[part][c] + f->v[k][c]);
                pe_imp_mv(fit->G, p, pts[np]);
                np++;
            })
            double have = span_of((const double (*)[3])pts, np, fit->side) * fit->scale;
            double r = span_of((const double (*)[3])(a->world + vs), vc, fit->side) / have;
            r = r < 1.0 ? r : 1.0;
            pl->w = r > rig->slim_min ? r : rig->slim_min;
            free(pts);
        }
        if (pl->w < 1 && used < logn)
            used += (size_t)snprintf(log + used, logn - used, "%s%d %.2f", used ? ", " : "", part, pl->w);
    }
    return 0;
}

/* f(v): a source vertex (in its bone's frame) to Aya's world. */
static void place_v(const Fit *fit, const Placed *pl, const int16_t *vv, double *o) {
    double v[3] = {vv[0], vv[1], vv[2]};
    if (pl->hips && vv[1] < fit->rig->hip_ring) v[1] = vv[1] - fit->rig->hip_lift;
    V3 bv, x, t;
    for (int c = 0; c < 3; c++) bv[c] = (double)((long long)pl->base[c] + (long long)v[c]);
    vsub(bv, pl->c0, t);
    pe_imp_mv(fit->G, t, x);
    double along = vdot(x, pl->u);
    if (pl->feet) along = along > 0 ? along : 0;
    vmul(fit->side, vdot(x, fit->side) * (pl->w - 1), t);
    vadd(x, t, x);
    V3 xs, us;
    vmul(x, fit->scale, xs);
    vmul(pl->u, along * fit->scale * (pl->k - 1), us);
    vadd(xs, us, x);
    pe_imp_mv(pl->Q, x, t);
    vadd(pl->a0, t, o);
}

static void place_n(const Fit *fit, const Placed *pl, const int16_t *nn, double *o) {
    double n[3] = {nn[0], nn[1], nn[2]};
    V3 t;
    pe_imp_mv(fit->G, n, t);
    pe_imp_mv(pl->Q, t, o);
}

/* ---- faces: what's buried inside the body is left out ---- */
typedef struct { const PeImpFace *f; int part, corners, page_base; } Face;   /* page_base: the model's first page */

static int collect_faces(const PeImpModel *m, const Fit *fit, Face **out, int *count) {
    const PeImpRig *rig = fit->rig;
    int cap = 0, n = 0;
    for (int p = 0; p < rig->parts; p++) cap += (int)(m->part[p].tris + m->part[p].quads);
    Face *fs = malloc((size_t)(cap ? cap : 1) * sizeof *fs);
    if (!fs) return -1;
    double bottom = 0;
    int first = 1, low = 0;
    if (rig->bury_lids) {
        const Placed *pc = &fit->part[rig->chest_part];
        FOR_CORNERS(m, rig->chest_part, f, k, {
            V3 w;
            place_v(fit, pc, f->v[k], w);
            if (first || w[1] > bottom) bottom = w[1];
            if (first || f->v[k][1] > low) low = f->v[k][1];
            first = 0;
        })
    }
    int hips_out = 0, lid_out = 0;
    for (int p = 0; p < rig->parts; p++) {
        for (int kind = 1; kind >= 0; kind--) {                    /* quads, then triangles */
            const PeImpPart *pt = &m->part[p];
            int nf = kind ? (int)pt->quads : (int)pt->tris, nc = kind ? 4 : 3;
            for (int i = 0; i < nf; i++) {
                const PeImpFace *f = kind ? &pt->quad[i] : &pt->tri[i];
                if (rig->bury_lids && p == rig->hips_part) {
                    int all = 1;
                    for (int k = 0; k < nc; k++) { V3 w; place_v(fit, &fit->part[p], f->v[k], w); all &= w[1] < bottom; }
                    if (all) { hips_out++; continue; }
                }
                if (rig->bury_lids && p == rig->chest_part) {
                    int all = 1;
                    for (int k = 0; k < nc; k++) all &= f->v[k][1] > low - 10;
                    if (all) { lid_out++; continue; }
                }
                fs[n].f = f, fs[n].part = p, fs[n].corners = nc, fs[n].page_base = 0, n++;
            }
        }
    }
    *out = fs, *count = n;
    return 0;
}

/* ---- texture: groups of faces sharing texels, each with its own 16 colours ---- */
/* A face's texel in the source picture: pages count from the first page the model's faces use (a
 * texture page is 128 texels wide at 8 bits), so a model on pages 1 and 2 maps as one on 0 and 1.
 * The costume's texture keeps these positions, so they must stay within 256 x 255: texture_fits
 * refuses a model whose faces span more than two pages (RE2's PL0D uses three) or fall outside its
 * picture, instead of reading the wrong page or past the picture. */
static void atlas_xy(const Face *fc, int j, int *x, int *y) {
    const PeImpFace *f = fc->f;
    *x = ((f->page & 15) - fc->page_base) * 128 + f->uv[j][0];
    *y = f->uv[j][1] < 254 ? f->uv[j][1] : 254;
}

static int texture_fits(const PeImpModel *m, Face *fs, int nf, char *why, size_t n) {
    int lo = 15, hi = 0;
    for (int i = 0; i < nf; i++) {
        int pg = fs[i].f->page & 15;
        if (pg < lo) lo = pg;
        if (pg > hi) hi = pg;
    }
    if (nf && hi - lo > 1) {
        if (why && n) snprintf(why, n, "the model's faces use texture pages %d to %d; a costume's texture holds two", lo, hi);
        return -1;
    }
    int off = 0;
    for (int i = 0; i < nf; i++) {
        fs[i].page_base = nf ? lo : 0;
        for (int j = 0; j < fs[i].corners; j++) {
            int x, y;
            atlas_xy(&fs[i], j, &x, &y);
            if (x < 0 || x > 255 || (uint32_t)x >= m->w || (uint32_t)y >= m->h) { off++; break; }
        }
    }
    if (off) {
        if (why && n) snprintf(why, n, "%d of the model's faces point outside its %ux%u texture", off, (unsigned)m->w, (unsigned)m->h);
        return -1;
    }
    return 0;
}

static int in_tri(double px, double py, const int *a, const int *b, const int *c, double pad) {
    const int *e0[3] = {a, b, c}, *e1[3] = {b, c, a};
    double d[3];
    for (int i = 0; i < 3; i++) {
        double ex = e1[i][0] - e0[i][0], ey = e1[i][1] - e0[i][1], ln = sqrt(ex * ex + ey * ey);
        if (ln == 0) ln = 1;
        d[i] = ((px - e0[i][0]) * ey - (py - e0[i][1]) * ex) / ln;
    }
    return (d[0] <= pad && d[1] <= pad && d[2] <= pad) || (d[0] >= -pad && d[1] >= -pad && d[2] >= -pad);
}

typedef struct { int *px; int n; } Cover;                          /* pixel keys x * 256 + y, sorted */

static int cover_face(const Face *fc, Cover *cv) {
    int pts[4][2], n = fc->corners, mnx = 1 << 30, mxx = -1, mny = 1 << 30, mxy = -1;
    for (int j = 0; j < n; j++) {
        atlas_xy(fc, j, &pts[j][0], &pts[j][1]);
        if (pts[j][0] < mnx) mnx = pts[j][0];
        if (pts[j][0] > mxx) mxx = pts[j][0];
        if (pts[j][1] < mny) mny = pts[j][1];
        if (pts[j][1] > mxy) mxy = pts[j][1];
    }
    int x0 = mnx - 1 > 0 ? mnx - 1 : 0, x1 = mxx + 1 < 255 ? mxx + 1 : 255;
    int y0 = mny - 1 > 0 ? mny - 1 : 0, y1 = mxy + 1 < 254 ? mxy + 1 : 254;
    int cap = (x1 >= x0 && y1 >= y0) ? (x1 - x0 + 1) * (y1 - y0 + 1) : 0;
    cv->px = malloc((size_t)(cap ? cap : 1) * sizeof *cv->px), cv->n = 0;
    if (!cv->px) return -1;
    for (int x = x0; x <= x1; x++)
        for (int y = y0; y <= y1; y++) {
            int hit = in_tri(x + 0.5, y + 0.5, pts[0], pts[1], pts[2], 1.0) ||
                      (n == 4 && in_tri(x + 0.5, y + 0.5, pts[1], pts[3], pts[2], 1.0));
            if (hit) cv->px[cv->n++] = x * 256 + y;
        }
    return 0;
}

static int has_px(const Cover *c, int key) {
    int lo = 0, hi = c->n - 1;
    while (lo <= hi) { int mid = (lo + hi) / 2; if (c->px[mid] == key) return 1; if (c->px[mid] < key) lo = mid + 1; else hi = mid - 1; }
    return 0;
}

static int uf_find(int *par, int i) { while (par[i] != i) par[i] = par[par[i]], i = par[i]; return i; }

typedef struct { int r, g, b; } Rgb;
static Rgb colour(const PeImpModel *m, int x, int y, int row) {
    unsigned c = m->pal[row][m->image[(size_t)y * m->w + (size_t)x]];
    Rgb o = {0, 0, 0};
    if (c) o.r = (int)((c & 31) << 3), o.g = (int)(((c >> 5) & 31) << 3), o.b = (int)(((c >> 10) & 31) << 3);
    return o;
}

/* Median cut to at most 16 colours: deterministic, our own (the Python used Pillow's). */
typedef struct { int lo, hi; } Box;
static int ch_of(const Rgb *c, int ch) { return ch == 0 ? c->r : ch == 1 ? c->g : c->b; }
static int s_sort_ch;
static int rgb_cmp(const void *a, const void *b) {
    const Rgb *x = a, *y = b;
    int d = ch_of(x, s_sort_ch) - ch_of(y, s_sort_ch);
    if (d) return d;
    return (x->r - y->r) ? x->r - y->r : (x->g - y->g) ? x->g - y->g : x->b - y->b;
}

static void quantize16(const Rgb *cols, int n, Rgb pal[16], int *npal, uint8_t *idx) {
    Rgb *c = malloc((size_t)(n ? n : 1) * sizeof *c);
    Box box[16];
    int nb = 1;
    memcpy(c, cols, (size_t)n * sizeof *c);
    box[0].lo = 0, box[0].hi = n;
    while (nb < 16) {
        int best = -1, bch = 0, brange = 0;
        for (int i = 0; i < nb; i++) {
            if (box[i].hi - box[i].lo < 2) continue;
            for (int ch = 0; ch < 3; ch++) {
                int mn = 255, mx = 0;
                for (int k = box[i].lo; k < box[i].hi; k++) { int v = ch_of(&c[k], ch); mn = v < mn ? v : mn; mx = v > mx ? v : mx; }
                if (mx - mn > brange) brange = mx - mn, best = i, bch = ch;
            }
        }
        if (best < 0) break;
        s_sort_ch = bch;
        qsort(c + box[best].lo, (size_t)(box[best].hi - box[best].lo), sizeof *c, rgb_cmp);
        int mid = (box[best].lo + box[best].hi) / 2;
        int v = ch_of(&c[mid], bch);                                   /* split where the value changes */
        while (mid > box[best].lo && ch_of(&c[mid - 1], bch) == v) mid--;
        if (mid == box[best].lo) { mid = (box[best].lo + box[best].hi) / 2; while (mid < box[best].hi && ch_of(&c[mid], bch) == v) mid++; }
        if (mid == box[best].hi) break;
        box[nb].lo = mid, box[nb].hi = box[best].hi, box[best].hi = mid, nb++;
    }
    for (int i = 0; i < nb; i++) {
        long long s[3] = {0, 0, 0}, k = box[i].hi - box[i].lo;
        for (int j = box[i].lo; j < box[i].hi; j++) s[0] += c[j].r, s[1] += c[j].g, s[2] += c[j].b;
        pal[i].r = k ? (int)((s[0] + k / 2) / k) : 0, pal[i].g = k ? (int)((s[1] + k / 2) / k) : 0, pal[i].b = k ? (int)((s[2] + k / 2) / k) : 0;
    }
    *npal = nb;
    for (int j = 0; j < n; j++) {                                      /* nearest palette colour */
        int best = 0;
        long long bd = -1;
        for (int i = 0; i < nb; i++) {
            long long dr = cols[j].r - pal[i].r, dg = cols[j].g - pal[i].g, db = cols[j].b - pal[i].b, d = dr * dr + dg * dg + db * db;
            if (bd < 0 || d < bd) bd = d, best = i;
        }
        idx[j] = (uint8_t)best;
    }
    free(c);
}

static int clut_index(const int *rows, int nrows, int r);

static int build_texture(const PeImpModel *m, const Face *fs, int nf, uint16_t *words, uint16_t *pal, int *face_pal, int *ngroups) {
    int rows[256], nrows = 0, rc = -1;
    for (int i = 0; i < nf; i++) {
        int r = fs[i].f->clut >> 6, dup = 0;
        for (int k = 0; k < nrows; k++) dup |= rows[k] == r;
        if (!dup && nrows < 256) rows[nrows++] = r;
    }
    for (int i = 1; i < nrows; i++) for (int j = i; j > 0 && rows[j - 1] > rows[j]; j--) { int t = rows[j]; rows[j] = rows[j - 1], rows[j - 1] = t; }
    if (nrows > (int)m->pal_rows) return -1;
#define CLUT_OF(fc) clut_index(rows, nrows, (fc)->clut >> 6)
    Cover *cv = calloc((size_t)(nf ? nf : 1), sizeof *cv);
    int *par = malloc((size_t)(nf ? nf : 1) * sizeof *par), *owner = malloc(256 * 256 * sizeof *owner);
    int *gid = malloc((size_t)(nf ? nf : 1) * sizeof *gid), *gorder = malloc((size_t)(nf ? nf : 1) * sizeof *gorder);
    uint8_t *img = calloc(256 * 255, 1);
    if (!cv || !par || !owner || !gid || !gorder || !img) goto out;
    for (int i = 0; i < 256 * 256; i++) owner[i] = -1;
    for (int i = 0; i < nf; i++) { par[i] = i; if (cover_face(&fs[i], &cv[i])) goto out; }
    for (int i = 0; i < nf; i++)
        for (int k = 0; k < cv[i].n; k++) {
            int p = cv[i].px[k];
            if (owner[p] >= 0) par[uf_find(par, i)] = uf_find(par, owner[p]);
            else owner[p] = i;
        }
    /* groups in order of their first face; members ascending */
    int ng = 0;
    for (int i = 0; i < nf; i++) {
        int r = uf_find(par, i), g = -1;
        for (int k = 0; k < ng; k++) if (uf_find(par, gorder[k]) == r) { g = k; break; }
        if (g < 0) g = ng, gorder[ng++] = i;
        gid[i] = g;
    }
    /* more than 32: merge the smallest into the closest in colour (stable order by pixel count) */
    int *gpix = calloc((size_t)(ng ? ng : 1), sizeof *gpix), *slot = malloc((size_t)(ng ? ng : 1) * sizeof *slot);
    int *cnt = malloc((size_t)(ng ? ng : 1) * sizeof *cnt);
    double (*mean)[3] = malloc((size_t)(ng ? ng : 1) * sizeof *mean);
    uint8_t *mark = calloc(256 * 256, 1);
    if (!gpix || !slot || !mark || !cnt || !mean) { free(gpix), free(slot), free(mark), free(cnt), free(mean); goto out; }
    for (int g = 0; g < ng; g++) slot[g] = g;
    int live = ng;
#define PIXELS_OF(G, ...)                                                                      \
    do {                                                                                       \
        memset(mark, 0, 256 * 256);                                                            \
        for (int i_ = 0; i_ < nf; i_++) if (gid[i_] == (G)) for (int k_ = 0; k_ < cv[i_].n; k_++) mark[cv[i_].px[k_]] = 1; \
        for (int key = 0; key < 256 * 256; key++) if (mark[key]) __VA_ARGS__                    \
    } while (0)
    while (live > 32) {
        for (int s = 0; s < live; s++) { int c = 0; PIXELS_OF(slot[s], { (void)key; c++; }); cnt[s] = c; }
        for (int i = 1; i < live; i++)                                 /* stable insertion sort */
            for (int j = i; j > 0 && cnt[j - 1] > cnt[j]; j--) {
                int t = cnt[j]; cnt[j] = cnt[j - 1], cnt[j - 1] = t;
                t = slot[j]; slot[j] = slot[j - 1], slot[j - 1] = t;
            }
        int small = slot[0];
        for (int s = 0; s < live; s++) {
            double sum[3] = {0, 0, 0};
            int c = 0;
            int gg = slot[s];
            PIXELS_OF(gg, {
                int x = key / 256, y = key % 256, fi = -1;
                for (int i = 0; i < nf && fi < 0; i++) if (gid[i] == gg) fi = i;
                Rgb col = colour(m, x, y, CLUT_OF(fs[fi].f));
                sum[0] += col.r, sum[1] += col.g, sum[2] += col.b, c++;
            });
            for (int k = 0; k < 3; k++) mean[s][k] = c ? sum[k] / c : 0;
        }
        int best = 1;
        double bd = -1;
        for (int s = 1; s < live; s++) {
            double d = 0;
            for (int k = 0; k < 3; k++) d += (mean[0][k] - mean[s][k]) * (mean[0][k] - mean[s][k]);
            if (bd < 0 || d < bd) bd = d, best = s;
        }
        for (int i = 0; i < nf; i++) if (gid[i] == small) gid[i] = slot[best];
        for (int s = 0; s + 1 < live; s++) slot[s] = slot[s + 1];
        live--;
    }
    /* final order: slot order; each face's palette is its group's position */
    memset(pal, 0, 512 * sizeof *pal);
    for (int s = 0; s < live; s++) {
        int g = slot[s], n = 0;
        PIXELS_OF(g, { (void)key; n++; });
        int *keys = malloc((size_t)(n ? n : 1) * sizeof *keys);
        Rgb *cols = malloc((size_t)(n ? n : 1) * sizeof *cols);
        uint8_t *idx = malloc((size_t)(n ? n : 1));
        if (!keys || !cols || !idx) { free(keys), free(cols), free(idx); free(gpix), free(slot), free(mark), free(cnt), free(mean); goto out; }
        int k2 = 0, firstf = -1;
        for (int i = 0; i < nf && firstf < 0; i++) if (gid[i] == g) firstf = i;
        PIXELS_OF(g, { keys[k2++] = key; });                           /* sorted (x, y) */
        for (int j = 0; j < n; j++) {
            int x = keys[j] / 256, y = keys[j] % 256, row = CLUT_OF(fs[firstf].f);
            for (int i = 0; i < nf; i++) if (gid[i] == g && has_px(&cv[i], keys[j])) { row = CLUT_OF(fs[i].f); break; }
            cols[j] = colour(m, x, y, row);
        }
        Rgb qp[16];
        int np;
        quantize16(cols, n, qp, &np, idx);
        for (int k = 0; k < 16; k++) {
            unsigned c = k < np ? (unsigned)((qp[k].r >> 3) | ((qp[k].g >> 3) << 5) | ((qp[k].b >> 3) << 10)) : 0;
            pal[(s / 16) * 256 + (s % 16) * 16 + k] = (uint16_t)(c ? c : 0x0421);
        }
        for (int j = 0; j < n; j++) img[(keys[j] % 256) * 256 + keys[j] / 256] = idx[j];
        for (int i = 0; i < nf; i++) if (gid[i] == g) face_pal[i] = s;
        free(keys), free(cols), free(idx);
    }
    for (int y = 0; y < 255; y++)
        for (int hx = 0; hx < 64; hx++) {
            unsigned v = 0;
            for (int q = 0; q < 4; q++) v |= (unsigned)(img[y * 256 + hx * 4 + q] & 15) << (4 * q);
            words[y * 64 + hx] = (uint16_t)v;
        }
    *ngroups = live;
    free(gpix), free(slot), free(mark), free(cnt), free(mean);
    rc = 0;
out:
    if (cv) for (int i = 0; i < nf; i++) free(cv[i].px);
    free(cv), free(par), free(owner), free(gid), free(gorder), free(img);
    return rc;
#undef PIXELS_OF
#undef CLUT_OF
}

static int clut_index(const int *rows, int nrows, int r) {
    for (int i = 0; i < nrows; i++) if (rows[i] == r) return i;
    return 0;
}

static int write_texture(PeImpPe *pe, const uint16_t *image, const uint16_t *pal, PeImpBuf *out) {
    if (pe_imp_pe_image(pe, AYA_TEX, out)) return -1;
    uint32_t td, tdesc;
    if (pe_imp_word(out, 4, &td) || pe_imp_word(out, td + 0x28, &tdesc)) return -1;
    uint32_t tat = tdesc & 0x3fffff;
    int done_img = 0, done_pal = 0;
    for (uint32_t i = 0; i < (tdesc >> 22); i++) {
        uint32_t r = tat + 20 * i, src, rect, sec, rect2;
        if (pe_imp_word(out, r + 4, &src) || pe_imp_word(out, r + 8, &rect) || pe_imp_word(out, r + 12, &sec) || pe_imp_word(out, r + 16, &rect2)) return -1;
        uint32_t x = (rect >> 10) & 0x7ff, y = rect >> 21, w = rect & 0x3ff, h = (src >> 24) ? (src >> 24) : 256;
        if (x == 960 && y == 256 && w == 64) {
            uint32_t cnt = w * h < 64 * 255 ? w * h : 64 * 255;
            for (uint32_t k = 0; k < cnt; k++) {
                uint32_t o = (src & 0xffffff) + 2 * k;
                if (o + 2 > out->n) return -1;
                out->b[o] = (uint8_t)image[k], out->b[o + 1] = (uint8_t)(image[k] >> 8);
            }
            done_img = 1;
        }
        if (x == 0 && y == 448 && w == 256) {
            uint32_t cnt = w * h < 512 ? w * h : 512;
            for (uint32_t k = 0; k < cnt; k++) {
                uint32_t o = (src & 0xffffff) + 2 * k;
                if (o + 2 > out->n) return -1;
                out->b[o] = (uint8_t)pal[k], out->b[o + 1] = (uint8_t)(pal[k] >> 8);
            }
            done_pal = 1;
        }
        if (sec & 0xffffff) {
            uint32_t x2 = (rect2 >> 10) & 0x7ff, y2 = rect2 >> 21, w2 = rect2 & 0x3ff, h2 = sec >> 24;
            if (x2 == 0 && y2 == 448 && w2 == 256) {
                uint32_t base = (src & 0xffffff) + (sec & 0xffffff), cnt = w2 * h2 < 512 ? w2 * h2 : 512;
                for (uint32_t k = 0; k < cnt; k++) {
                    uint32_t o = base + 2 * k;
                    if (o + 2 > out->n) return -1;
                    out->b[o] = (uint8_t)pal[k], out->b[o + 1] = (uint8_t)(pal[k] >> 8);
                }
                done_pal = 1;
            }
        }
    }
    return done_img && done_pal ? 0 : -1;
}

/* ---- model.bin's mesh ---- */
static int winding_sign_i(const int16_t (*pts)[3], const long long *n) {
    long long ab[3], ac[3], c[3];
    for (int k = 0; k < 3; k++) ab[k] = pts[1][k] - pts[0][k], ac[k] = pts[2][k] - pts[0][k];
    c[0] = ab[1] * ac[2] - ab[2] * ac[1], c[1] = ab[2] * ac[0] - ab[0] * ac[2], c[2] = ab[0] * ac[1] - ab[1] * ac[0];
    return c[0] * n[0] + c[1] * n[1] + c[2] * n[2] > 0 ? 1 : -1;
}

static int aya_winding(const Aya *a, const double (*tab)[3]) {
    const PeImpMd *md = &a->md;
    int votes = 0;
    for (int d = 0; d < md->pk[0] + md->pk[1] && d < md->nd; d++) {
        const uint16_t *idx = md->D[d].idx;
        V3 nsum = {0, 0, 0}, pts[3];
        for (int j = 0; j < 3; j++) {
            if (idx[j] >= md->verts) return 1;
            uint32_t at = 0x1C + 12u * (uint32_t)md->parts + 8u * idx[j] + 6;
            int pad = (int16_t)(a->raw.b[at] | a->raw.b[at + 1] << 8);
            if (pad < 0) pad += 240;
            if (pad < 0 || pad >= 240) return 1;
            for (int c = 0; c < 3; c++) pts[j][c] = md->V[idx[j]][c];
            vadd(j ? nsum : (double[3]){0, 0, 0}, tab[pad], nsum);
        }
        V3 ab, ac, cr;
        vsub(pts[1], pts[0], ab), vsub(pts[2], pts[0], ac), vcross(ab, ac, cr);
        votes += vdot(cr, nsum) > 0 ? 1 : -1;
    }
    return votes > 0 ? 1 : -1;
}

typedef struct { int part; int16_t v[3]; double p[3], nsum[3]; int index; } VKey;

static int build_model(const Aya *a, const Fit *fit, const Face *fs, int nf, const int *face_pal,
                       const double (*tab)[3], PeImpBuf *out, PeSkinOut *info) {
    const PeImpMd *md = &a->md;
    int votes = 0;
    for (int i = 0; i < nf; i++) {
        long long n[3];
        for (int c = 0; c < 3; c++) n[c] = (long long)fs[i].f->n[0][c] + fs[i].f->n[1][c] + fs[i].f->n[2][c];
        votes += winding_sign_i(fs[i].f->v, n);
    }
    int flip = (votes > 0 ? 1 : -1) != aya_winding(a, tab);
    /* per Aya bone, the source vertices (part, position) in first-seen order */
    int cap = 0;
    for (int i = 0; i < nf; i++) cap += fs[i].corners;
    VKey *keys = malloc((size_t)(cap ? cap : 1) * sizeof *keys);
    int *bone_of = malloc((size_t)(cap ? cap : 1) * sizeof *bone_of), nk = 0;
    if (!keys || !bone_of) { free(keys), free(bone_of); return -1; }
    for (int i = 0; i < nf; i++) {
        const Placed *pl = &fit->part[fs[i].part];
        PeM3 Rt;
        pe_imp_transpose(a->rot[pl->bone], Rt);
        for (int j = 0; j < fs[i].corners; j++) {
            int k = -1;
            for (int q = 0; q < nk; q++)
                if (bone_of[q] == pl->bone && keys[q].part == fs[i].part && !memcmp(keys[q].v, fs[i].f->v[j], sizeof keys[q].v)) { k = q; break; }
            if (k < 0) {
                k = nk++;
                bone_of[k] = pl->bone, keys[k].part = fs[i].part;
                memcpy(keys[k].v, fs[i].f->v[j], sizeof keys[k].v);
                V3 w, t;
                place_v(fit, pl, fs[i].f->v[j], w);
                vsub(w, a->pos[pl->bone], t);
                pe_imp_mv(Rt, t, keys[k].p);
                keys[k].nsum[0] = keys[k].nsum[1] = keys[k].nsum[2] = 0;
            }
            V3 nw, nu, t;
            place_n(fit, pl, fs[i].f->n[j], nw);
            vunit(nw, nu);
            pe_imp_mv(Rt, nu, t);
            vadd(keys[k].nsum, t, keys[k].nsum);
        }
    }
    /* vertices bone by bone; each vertex's normal: the table entry closest to its average */
    uint8_t *vb = malloc((size_t)(nk ? nk : 1) * 8);
    uint8_t *ptab = malloc((size_t)md->parts * 12 + 1);
    int nv = 0;
    if (!vb || !ptab) { free(keys), free(bone_of), free(vb), free(ptab); return -1; }
    for (int b = 0; b < md->parts; b++) {
        int start = nv;
        for (int q = 0; q < nk; q++) {
            if (bone_of[q] != b) continue;
            V3 n;
            vunit(keys[q].nsum, n);
            int pad = 0;
            double best = 0;
            for (int i = 0; i < 240; i++) { double d = vdot(tab[i], n); if (!i || d > best) best = d, pad = i; }
            keys[q].index = nv;
            for (int c = 0; c < 3; c++) {
                double r = nearbyint(keys[q].p[c]);
                long long v = (long long)r;
                v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
                vb[8 * nv + 2 * c] = (uint8_t)v, vb[8 * nv + 2 * c + 1] = (uint8_t)((uint16_t)v >> 8);
            }
            vb[8 * nv + 6] = (uint8_t)pad, vb[8 * nv + 7] = (uint8_t)((uint16_t)pad >> 8);
            nv++;
        }
        const PeImpPtab *pt = &md->ptab[b];
        uint8_t *e = ptab + 12 * b;
        memset(e, 0, 12);
        e[0] = (uint8_t)start, e[1] = (uint8_t)(start >> 8), e[2] = (uint8_t)(nv - start), e[3] = (uint8_t)((nv - start) >> 8);
        e[4] = pt->vis;
        for (int k = 0; k < 4; k++) e[8 + k] = (uint8_t)((uint32_t)pt->tz >> 8 * k);
    }
    /* faces: quads then triangles; their palette from the texture groups */
    int nq = 0, nt = 0;
    for (int i = 0; i < nf; i++) { if (fs[i].corners == 4) nq++; else nt++; }
    uint8_t *D = malloc((size_t)(nf ? nf : 1) * 12), *UV = malloc((size_t)(nf ? nf : 1) * 16);
    size_t dn = 0, un = 0;
    int bad = !D || !UV;
    for (int pass = 0; pass < 2 && !bad; pass++) {
        for (int i = 0; i < nf && !bad; i++) {
            int quad = fs[i].corners == 4;
            if (quad != (pass == 0)) continue;
            int order[4] = {0, 1, 2, 3}, n = fs[i].corners;
            if (flip) { order[1] = 2, order[2] = 1; }
            const Placed *pl = &fit->part[fs[i].part];
            uint16_t ids[4] = {0, 0, 0, 0};
            for (int j = 0; j < n; j++) {
                int k = -1;
                for (int q = 0; q < nk; q++)
                    if (bone_of[q] == pl->bone && keys[q].part == fs[i].part && !memcmp(keys[q].v, fs[i].f->v[order[j]], sizeof keys[q].v)) { k = q; break; }
                if (k < 0) { bad = 1; break; }
                ids[j] = (uint16_t)keys[k].index;
            }
            D[dn++] = 0x80, D[dn++] = 0x80, D[dn++] = 0x80, D[dn++] = quad ? 0x0a : 0x0f;
            for (int j = 0; j < 4; j++) D[dn++] = (uint8_t)ids[j], D[dn++] = (uint8_t)(ids[j] >> 8);
            int gi = face_pal[i];
            unsigned clut = (unsigned)(((448 + gi / 16) << 6) | (gi % 16));
            int uv[4][2];
            for (int j = 0; j < n; j++) {
                atlas_xy(&fs[i], order[j], &uv[j][0], &uv[j][1]);
                if (uv[j][0] > 255) bad = 1;                              /* struct 'B' would refuse it */
            }
            unsigned rec[12] = {(unsigned)uv[0][0], (unsigned)uv[0][1], clut, (unsigned)uv[1][0], (unsigned)uv[1][1], 31,
                                (unsigned)uv[2][0], (unsigned)uv[2][1], 0, 0, 0, 0};
            if (quad) rec[9] = (unsigned)uv[3][0], rec[10] = (unsigned)uv[3][1], rec[11] = 0;
            for (int q = 0; q < (quad ? 4 : 3); q++)
                UV[un++] = (uint8_t)rec[3 * q], UV[un++] = (uint8_t)rec[3 * q + 1], UV[un++] = (uint8_t)rec[3 * q + 2], UV[un++] = (uint8_t)(rec[3 * q + 2] >> 8);
        }
    }
    const uint8_t *raw = a->raw.b;
    uint32_t parts = (uint32_t)md->parts, averts = (uint32_t)md->verts, nd = (uint32_t)(md->pk[0] + md->pk[1] + md->pk[2] + md->pk[3]);
    uint32_t at = 0x1C + 12 * parts + 8 * averts + 4 * averts + 12 * nd;
    uint32_t mcb = raw[0x18] | raw[0x19] << 8, tail_n = 16 + 16 * parts + 8, cmd_n = (mcb + 3) / 4 * 4;
    if (bad || at + tail_n + cmd_n > a->raw.n) { free(keys), free(bone_of), free(vb), free(ptab), free(D), free(UV); return -1; }
    size_t total = 0x1C + 12 * parts + (size_t)nv * 8 + (size_t)nv * 4 + dn + tail_n + cmd_n + un;
    out->b = malloc(total), out->n = (uint32_t)total;
    if (!out->b) { free(keys), free(bone_of), free(vb), free(ptab), free(D), free(UV); return -1; }
    uint8_t *o = out->b;
    memcpy(o, raw, 0x1C);
    o[4] = (uint8_t)(nq + nt), o[5] = (uint8_t)((nq + nt) >> 8), o[6] = (uint8_t)nv, o[7] = (uint8_t)(nv >> 8);
    uint16_t pk[4] = {(uint16_t)nq, (uint16_t)nt, 0, 0};
    for (int k = 0; k < 4; k++) o[8 + 2 * k] = (uint8_t)pk[k], o[9 + 2 * k] = (uint8_t)(pk[k] >> 8);
    size_t p = 0x1C;
    memcpy(o + p, ptab, 12 * parts), p += 12 * parts;
    memcpy(o + p, vb, (size_t)nv * 8), p += (size_t)nv * 8;
    const uint8_t *col = raw + 0x1C + 12 * parts + 8 * averts;
    for (int i = 0; i < nv; i++) memcpy(o + p, col, 4), p += 4;
    memcpy(o + p, D, dn), p += dn;
    memcpy(o + p, raw + at, tail_n), p += tail_n;
    memcpy(o + p, raw + at + tail_n, cmd_n), p += cmd_n;
    memcpy(o + p, UV, un);
    info->vertices = nv, info->quads = nq, info->tris = nt;
    free(keys), free(bone_of), free(vb), free(ptab), free(D), free(UV);
    return 0;
}

/* ---- rows: Aya's own, copied; rooms', retargeted ---- */
typedef struct { PeImpBuf *rows; uint8_t *ids; int n; } RowList;

static void rowlist_free(RowList *l) {
    for (int i = 0; i < l->n; i++) pe_imp_buf_free(&l->rows[i]);
    free(l->rows), free(l->ids);
    memset(l, 0, sizeof *l);
}

static int aya_rows(const PeImpBuf *blob, RowList *l) {
    PeImpRec *r;
    int n = pe_imp_records(blob, 0x10, &r);
    memset(l, 0, sizeof *l);
    if (n < 0) return -1;
    l->rows = calloc((size_t)(n ? n : 1), sizeof *l->rows), l->ids = malloc((size_t)(n ? n : 1));
    if (!l->rows || !l->ids) { free(r); return -1; }
    for (int i = 0; i < n; i++) {
        uint32_t len;
        if (pe_imp_row_length(blob->b, blob->n, r[i].off, &len)) { free(r); return -1; }
        uint32_t take = r[i].off + len <= blob->n ? len : blob->n - r[i].off;
        l->rows[i].b = malloc(take ? take : 1), l->rows[i].n = take, l->ids[i] = r[i].slot;
        if (!l->rows[i].b) { free(r); return -1; }
        memcpy(l->rows[i].b, blob->b + r[i].off, take);
        l->n++;
    }
    free(r);
    return 0;
}

static int blob_of_rows(const PeImpBlobModel *m, int nm, const RowList *l, PeImpBuf *out) {
    PeImpBlobRow *r = malloc((size_t)(l->n ? l->n : 1) * sizeof *r);
    if (!r) return -1;
    for (int i = 0; i < l->n; i++) r[i].raw = l->rows[i].b, r[i].n = l->rows[i].n, r[i].id = l->ids[i];
    int rc = pe_imp_write_blob(m, nm, r, l->n, out);
    free(r);
    return rc;
}

typedef struct { int parts; PeImpMd src; PeImpMap mp; PeImpCorr corr; int img; } Alt;

static int full_legs(const PeImpMd *src, const PeImpMd *dst, PeImpMap *mp) {
    int parent[MAXP];
    PeImpChain s[CH_COUNT], d[CH_COUNT];
    if (pe_imp_parents(src, parent) || pe_imp_chains(src, s) || pe_imp_chains(dst, d)) return -1;
    long long weight[MAXP];
    /* weight(n) = vertices of n and everything under it */
    for (int n = 0; n < MAXP; n++) weight[n] = 0;
    for (int n = 0; n < src->parts; n++) {
        for (int x = n; x >= 0 && x < MAXP; x = parent[x]) { weight[x] += src->ptab[n].vc; if (parent[x] < 0) break; }
    }
    for (int k = CH_LEGA; k <= CH_LEGB; k++) {
        int run[MAXP], nr = 0;
        run[nr++] = s[k].p[0];
        for (;;) {
            int last = run[nr - 1], best = -1;
            for (int c = 0; c < MAXP; c++)                          /* kids ascending; max: the first */
                if (parent[c] == last && (best < 0 || weight[c] > weight[best])) best = c;
            if (best < 0 || nr == MAXP) break;
            run[nr++] = best;
        }
        for (int i = 0; i < d[k].n && i < nr; i++)
            if (mp->map[d[k].p[i]] < 0) mp->map[d[k].p[i]] = run[i];
    }
    return 0;
}

static double sole(const PeImpMd *md, const int tr[3], const int (*ang)[3], int nang, const int *parts, int np, double *w, uint8_t *valid) {
    pe_imp_pose(md, tr, ang, nang, (double (*)[3])w, valid);
    double best = 0;
    int first = 1;
    for (int q = 0; q < np; q++) {
        int p = parts[q];
        for (int i = md->ptab[p].vs; i < md->ptab[p].vs + md->ptab[p].vc; i++)
            if (i < md->verts && valid[i] && (first || w[3 * i + 1] > best)) best = w[3 * i + 1], first = 0;
    }
    return best;
}

static const int FEET_SKIN[2] = {24, 28};

static int row_frames(const PeImpBuf *row, int *frames) {
    if (row->n < 12) return -1;
    *frames = row->b[2] > 1 ? row->b[2] : 1;
    return 0;
}

static int dress_row(const PeImpBuf *data, const PeImpMd *md_new, const PeImpFrame *idle, const Alt *alt, int nalt, PeImpBuf *out, int *done) {
    *done = 0;
    int parts = data->b[1] + 1;
    const Alt *al = NULL;
    for (int i = 0; i < nalt; i++) if (alt[i].parts == parts && (parts == 31 || parts == 23)) al = &alt[i];
    if (!al) return 0;
    int feet_src[2], nfs = 0, frames;
    for (int q = 0; q < 2; q++) if (al->mp.map[FEET_SKIN[q]] >= 0) feet_src[nfs++] = al->mp.map[FEET_SKIN[q]];
    uint8_t straight[MAXP];
    pe_imp_straight_parts(md_new, straight);
    if (row_frames(data, &frames)) return -1;
    double (*trs)[3] = malloc((size_t)frames * sizeof *trs), *drops = malloc((size_t)frames * sizeof *drops);
    int (*angs)[3] = malloc((size_t)frames * md_new->parts * sizeof *angs);
    int maxv = al->src.verts > md_new->verts ? al->src.verts : md_new->verts;
    double *w = malloc((size_t)(maxv + 1) * 3 * sizeof *w);
    uint8_t *valid = malloc((size_t)maxv + 1);
    int rc = -1;
    if (!trs || !drops || !angs || !w || !valid) goto out;
    for (int f = 0; f < frames; f++) {
        PeImpFrame fr;
        if (pe_imp_anim_frame(data, 0, f, &fr)) goto out;
        int skin[MAXP][3];
        memset(skin, 0, sizeof skin);
        pe_imp_retarget_world(&al->src, fr.ang, fr.n, md_new, &al->mp, idle->ang, idle->n, &al->corr, straight, skin);
        double a = sole(&al->src, fr.tr, fr.ang, fr.n, feet_src, nfs, w, valid);
        double b = sole(md_new, fr.tr, (const int (*)[3])skin, md_new->parts, FEET_SKIN, 2, w, valid);
        double d = a - b;
        d = d < 40 ? d : 40;
        drops[f] = d > -10 ? d : -10;
        for (int c = 0; c < 3; c++) trs[f][c] = fr.tr[c];
        memcpy(angs + (size_t)f * md_new->parts, skin, (size_t)md_new->parts * sizeof *angs);
    }
    for (int i = 0; i < frames; i++) {                                 /* averaged over 5 frames */
        int lo = i - 2 > 0 ? i - 2 : 0, hi = i + 3 < frames ? i + 3 : frames;
        double s = 0;
        for (int k = lo; k < hi; k++) s = s + drops[k];
        double avg = s / (hi - lo);
        trs[i][1] = trs[i][1] + nearbyint(avg);
    }
    uint16_t hit[4];
    for (int k = 0; k < 4; k++) hit[k] = (uint16_t)(data->b[4 + 2 * k] | data->b[5 + 2 * k] << 8);
    rc = pe_imp_encode_row(frames, (const double (*)[3])trs, (const int (*)[3])angs, md_new->parts, hit, out);
    if (!rc) *done = 1;
out:
    free(trs), free(drops), free(angs), free(w), free(valid);
    return rc;
}

static int retarget_row(const PeImpBuf *data, const PeImpMd *aya, const PeImpMd *md, const PeImpMap *mp,
                        const PeImpFrame *idle, const PeImpCorr *corr, const Alt *alt, int nalt, PeImpBuf *out) {
    int frames, parts = data->b[1] + 1;
    if (row_frames(data, &frames)) return -1;
    const PeImpMd *src = aya;
    const PeImpMap *smp = mp;
    const PeImpCorr *scorr = corr;
    if (parts != aya->parts)
        for (int i = 0; i < nalt; i++) if (alt[i].parts == parts) src = &alt[i].src, smp = &alt[i].mp, scorr = &alt[i].corr;
    uint8_t straight[MAXP];
    pe_imp_straight_parts(md, straight);
    double (*trs)[3] = malloc((size_t)frames * sizeof *trs);
    int (*angs)[3] = malloc((size_t)frames * md->parts * sizeof *angs);
    int rc = -1;
    if (!trs || !angs) goto out;
    for (int f = 0; f < frames; f++) {
        PeImpFrame fr;
        if (pe_imp_anim_frame(data, 0, f, &fr)) goto out;
        int skin[MAXP][3];
        memset(skin, 0, sizeof skin);
        pe_imp_retarget_world(src, fr.ang, fr.n, md, smp, idle->ang, idle->n, scorr, straight, skin);
        memcpy(angs + (size_t)f * md->parts, skin, (size_t)md->parts * sizeof *angs);
        for (int c = 0; c < 3; c++) trs[f][c] = fr.tr[c] * 1.0;
    }
    uint16_t hit[4];
    for (int k = 0; k < 4; k++) hit[k] = (uint16_t)(data->b[4 + 2 * k] | data->b[5 + 2 * k] << 8);
    rc = pe_imp_encode_row(frames, (const double (*)[3])trs, (const int (*)[3])angs, md->parts, hit, out);
out:
    free(trs), free(angs);
    return rc;
}

static int write_rooms(PeImpPe *pe, const Aya *a, const PeImpMd *md_new, PeImpBuf *out, int *count, const PeImpProgress *prog) {
    Alt alt[2];
    int nalt = 0, rc = -1;
    memset(alt, 0, sizeof alt);
    for (int area = 10; area <= 11; area++) {                          /* alt_sources: images 18, 19 */
        PeImpBuf b;
        uint32_t off, tr;
        int row_of[256];
        Alt *al = &alt[nalt];
        if (pe_imp_pe_image(pe, area + 8, &b)) return -1;
        if (first_model(&b, &off, &tr) || pe_imp_md_decode(&b, off, &al->src) || rows_of(&b, row_of)) { pe_imp_buf_free(&b); goto out; }
        PeImpFrame sidle;
        memset(&sidle, 0, sizeof sidle);
        if (row_of[IDLE_ROW] >= 0) { if (pe_imp_anim_frame(&b, (uint32_t)row_of[IDLE_ROW], 0, &sidle)) { pe_imp_buf_free(&b); goto out; } }
        else sidle.n = al->src.parts;
        pe_imp_semantic_mapping(&al->src, md_new, &al->mp);
        pe_imp_shoulder_deltas(&al->src, md_new, &al->mp, sidle.ang, sidle.n, a->idle.ang, a->idle.n, &al->corr);
        al->parts = al->src.parts, al->img = area + 8;
        nalt++;
        pe_imp_buf_free(&b);
    }
    for (int i = 0; i < nalt; i++) if (full_legs(&alt[i].src, md_new, &alt[i].mp)) goto out;
    static const struct { int parts, img; } dress[2] = {{31, 18}, {23, 19}};
    for (int q = 0; q < 2; q++) {                                      /* dress_feet */
        Alt *al = NULL;
        for (int i = 0; i < nalt; i++) if (alt[i].parts == dress[q].parts) al = &alt[i];
        if (!al) continue;
        PeImpBuf b;
        int row_of[256];
        PeImpFrame f;
        if (pe_imp_pe_image(pe, dress[q].img, &b)) goto out;
        if (rows_of(&b, row_of) || row_of[IDLE_ROW] < 0 || pe_imp_anim_frame(&b, (uint32_t)row_of[IDLE_ROW], 0, &f)) { pe_imp_buf_free(&b); goto out; }
        pe_imp_buf_free(&b);
        PeM3 sw[MAXP], tw[MAXP], t;
        uint8_t sh[MAXP], th[MAXP];
        pe_imp_world_rotations(&al->src, f.ang, f.n, sw, sh);
        pe_imp_world_rotations(md_new, a->idle.ang, a->idle.n, tw, th);
        for (int k = 0; k < 2; k++) {
            int p = FEET_SKIN[k], s = al->mp.map[p];
            if (s < 0) continue;
            pe_imp_transpose(sw[s], t);
            pe_imp_mm(t, tw[p], al->corr.m[p]);
            al->corr.has[p] = 1;
        }
    }
    /* the rows: rooms' (not already for this skeleton), then the dress bundles' own */
    PeImpRoomRow *rr = NULL;
    int nrr = 0;
    if (pe_imp_room_player_rows(pe, &rr, &nrr, prog)) goto out;
    typedef struct { uint32_t fp; const PeImpBuf *d; } Item;
    Item *items = malloc((size_t)(nrr + 512) * sizeof *items);
    RowList dl[2];
    memset(dl, 0, sizeof dl);
    int ni = 0;
    if (!items) { pe_imp_room_rows_free(rr, nrr); goto out; }
    for (int i = 0; i < nrr; i++) if (rr[i].data.b[1] + 1 != md_new->parts) items[ni].fp = rr[i].fp, items[ni].d = &rr[i].data, ni++;
    for (int q = 0; q < 2; q++) {
        PeImpBuf b;
        if (pe_imp_pe_image(pe, 18 + q, &b) || aya_rows(&b, &dl[q])) { pe_imp_buf_free(&b); goto rows_out; }
        pe_imp_buf_free(&b);
        for (int i = 0; i < dl[q].n; i++) {
            const PeImpBuf *d = &dl[q].rows[i];
            if (d->n < FP_BYTES || d->b[1] + 1 == md_new->parts) continue;
            uint32_t len, fp;
            if (pe_imp_row_length(d->b, d->n, 0, &len)) goto rows_out;
            fp = pe_imp_fingerprint(d->b, len < d->n ? len : d->n);
            int dup = 0;
            for (int k = 0; k < ni; k++) if (items[k].fp == fp) { dup = 1; break; }
            if (!dup && ni < nrr + 512) items[ni].fp = fp, items[ni].d = d, ni++;
        }
    }
    for (int i = 1; i < ni; i++)                                       /* sorted by fingerprint */
        for (int j = i; j > 0 && items[j - 1].fp > items[j].fp; j--) { Item t = items[j]; items[j] = items[j - 1], items[j - 1] = t; }
    PeImpMap mp;
    PeImpCorr corr;
    pe_imp_semantic_mapping(&a->md, md_new, &mp);
    pe_imp_shoulder_deltas(&a->md, md_new, &mp, a->idle.ang, a->idle.n, a->idle.ang, a->idle.n, &corr);
    {
        uint32_t head = 8 + 12u * (uint32_t)ni;
        size_t cap = 1 << 20, bn = 0;
        uint8_t *body = malloc(cap), *table = malloc((size_t)ni * 12 + 1);
        int ok = body && table;
        for (int i = 0; i < ni && ok; i++) {
            PeImpBuf row;
            int done;
            if (dress_row(items[i].d, md_new, &a->idle, alt, nalt, &row, &done)) { ok = 0; break; }
            if (!done && retarget_row(items[i].d, &a->md, md_new, &mp, &a->idle, &corr, alt, nalt, &row)) { ok = 0; break; }
            uint32_t vals[3] = {items[i].fp, head + (uint32_t)bn, row.n};
            for (int k = 0; k < 3; k++) for (int q = 0; q < 4; q++) table[12 * i + 4 * k + q] = (uint8_t)(vals[k] >> 8 * q);
            while (bn + row.n + 4 > cap) { cap *= 2; uint8_t *g = realloc(body, cap); if (!g) { ok = 0; break; } body = g; }
            if (ok) { memcpy(body + bn, row.b, row.n); bn += row.n; while (bn % 4) body[bn++] = 0; }
            pe_imp_buf_free(&row);
        }
        if (ok) {
            out->n = (uint32_t)(head + bn);
            out->b = malloc(out->n);
            if (out->b) {
                memcpy(out->b, "PERR", 4);
                for (int q = 0; q < 4; q++) out->b[4 + q] = (uint8_t)((uint32_t)ni >> 8 * q);
                memcpy(out->b + 8, table, (size_t)ni * 12);
                memcpy(out->b + head, body, bn);
                *count = ni;
                rc = 0;
            }
        }
        free(body), free(table);
    }
rows_out:
    rowlist_free(&dl[0]), rowlist_free(&dl[1]);
    free(items);
    pe_imp_room_rows_free(rr, nrr);
out:
    for (int i = 0; i < 2; i++) pe_imp_md_free(&alt[i].src);
    return rc;
}

/* ---- the whole build ---- */
void pe_skin_files_free(PeSkinOut *f) {
    pe_imp_buf_free(&f->model), pe_imp_buf_free(&f->texture), pe_imp_buf_free(&f->rooms), pe_imp_buf_free(&f->meta);
    for (int d = 0; d < 8; d++) pe_imp_buf_free(&f->day[d]);
}

int pe_skin_build(PeImpPe *pe, const PeImpModel *src, const PeImpRig *rig, const int16_t (*normals)[4],
                  const PeImpProgress *prog, PeSkinOut *out, char *why, size_t n) {
    memset(out, 0, sizeof *out);
    Aya a;
    Fit fit;
    Face *fs = NULL;
    int nf = 0, *face_pal = NULL, rc = -1;
    double (*tab)[3] = malloc(240 * sizeof *tab);
    uint16_t *words = malloc(64 * 255 * sizeof *words), *pal = malloc(512 * sizeof *pal);
    PeImpBuf raw = {0};
    RowList rows = {0};
    memset(&fit, 0, sizeof fit);
#define FAIL(msg) do { if (why && n) snprintf(why, n, "%s", msg); goto out; } while (0)
    if (load_aya(pe, &a)) { aya_free(&a); FAIL("Aya's bundle could not be read from the Parasite Eve disc"); }
    if (!tab || !words || !pal) FAIL("out of memory");
    if ((int)src->parts < rig->parts || (int)src->bones <= rig->ref_thigh_r) FAIL("the model doesn't match its rig profile");
    for (int i = 0; i < 240; i++) { V3 v = {normals[i][0], normals[i][1], normals[i][2]}; vunit(v, tab[i]); }
    PE_IMP_PROGRESS(prog, "fitting the model", 1, 4);
    frames_fit(src, &a, rig, &fit);
    if (place(src, &a, rig, &fit, out->log, sizeof out->log)) FAIL("fitting failed");
    PE_IMP_PROGRESS(prog, "packing the texture", 2, 4);
    if (collect_faces(src, &fit, &fs, &nf)) FAIL("out of memory");
    if (texture_fits(src, fs, nf, why, n)) goto out;                  /* its reason is in `why` */
    face_pal = malloc((size_t)(nf ? nf : 1) * sizeof *face_pal);
    if (!face_pal || build_texture(src, fs, nf, words, pal, face_pal, &out->groups)) FAIL("the texture could not be packed");
    PE_IMP_PROGRESS(prog, "writing the model", 3, 4);
    if (build_model(&a, &fit, fs, nf, face_pal, (const double (*)[3])tab, &raw, out)) FAIL("the model could not be written");
    if (aya_rows(&a.blob, &rows)) FAIL("Aya's rows could not be read");
    PeImpBlobModel bm = {raw.b, raw.n, 0, a.trailing};
    if (blob_of_rows(&bm, 1, &rows, &out->model)) FAIL("out of memory");
    for (int d = 0; d < 8; d++) {
        PeImpBuf b;
        RowList dr;
        if (pe_imp_pe_image(pe, 0x16 + 8 + d + 1, &b)) FAIL("a day's rows could not be read");
        int bad = aya_rows(&b, &dr) || blob_of_rows(NULL, 0, &dr, &out->day[d]);
        pe_imp_buf_free(&b), rowlist_free(&dr);
        if (bad) FAIL("a day's rows could not be read");
    }
    if (write_texture(pe, words, pal, &out->texture)) FAIL("Aya's texture could not be rewritten");
    out->meta.b = malloc(8), out->meta.n = 8;
    if (!out->meta.b) FAIL("out of memory");
    memcpy(out->meta.b, "PEMT\x12\0\0\0", 8);
    {
        PeImpMd md_new;
        uint32_t off, tr;
        if (first_model(&out->model, &off, &tr) || pe_imp_md_decode(&out->model, off, &md_new)) FAIL("the new model could not be read back");
        int bad = write_rooms(pe, &a, &md_new, &out->rooms, &out->room_rows, prog);
        pe_imp_md_free(&md_new);
        if (bad) FAIL("the room animations could not be retargeted");
    }
    rc = 0;
out:
#undef FAIL
    if (rc) pe_skin_files_free(out);
    aya_free(&a);
    free(fs), free(face_pal), free(tab), free(words), free(pal);
    pe_imp_buf_free(&raw);
    rowlist_free(&rows);
    return rc;
}

int pe_skin_write(const PeSkinOut *f, const char *folder, char *why, size_t n) {
    struct { const char *name; const PeImpBuf *b; } files[13] = {
        {"model.bin", &f->model}, {"texture.bin", &f->texture}, {"rooms.bin", &f->rooms}, {"meta.bin", &f->meta}};
    static const char *days[8] = {"day1.bin", "day2.bin", "day3.bin", "day4.bin", "day5.bin", "day6.bin", "day7.bin", "day8.bin"};
    for (int d = 0; d < 8; d++) files[4 + d].name = days[d], files[4 + d].b = &f->day[d];
    for (int i = 0; i < 12; i++) {
        char path[PE_IMP_PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", folder, files[i].name);
        FILE *o = fopen(path, "wb");
        if (!o || fwrite(files[i].b->b, 1, files[i].b->n, o) != files[i].b->n || fclose(o)) {
            if (o) fclose(o);
            if (why && n) snprintf(why, n, "can't write %s", path);
            return -1;
        }
    }
    return 0;
}
