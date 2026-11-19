/* The fit layer (pe_imp_fit.h, docs/IMPORT_MODELS.md). Rules from the model audit
 * (tests/pe_model_audit.c): bone count isn't a body type, so a person is RE2's human tree at a
 * standing height; a costume's texture holds two pages (texel x = (page - first page) * 128 + u);
 * placeholder parts and zero-area faces carry nothing a costume needs. Read only on its input. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_fit.h"

/* RE2's human skeleton, each bone's parent as every RE2 PLxx.PLD stores it (RE1's and RE3's players
 * are read into this order): a root with three chains of three below one child (2-4, 5-7 hang from
 * 1), one leaf (8) and two chains of three (9-11, 12-14) from the root */
static const int16_t k_human[15] = {-1, 0, 1, 2, 3, 1, 5, 6, 0, 0, 9, 10, 0, 12, 13};
/* standing joint heights seen: 1,659 (RE2 PL0F, a child) to 2,359; RE1's and RE3's in between */
#define PERSON_MIN 1200
#define PERSON_MAX 3200
#define STUB_BOX 16

#define DENY_MAX 64
static struct { char id[PE_IMP_ID_MAX]; unsigned uses; char reason[96]; } s_deny[DENY_MAX];
static int s_denies;

void pe_imp_fit_reset(void) { s_denies = 0; }

int pe_imp_fit_deny(const char *asset_id, unsigned uses, const char *reason) {
    if (!asset_id || !*asset_id || strlen(asset_id) >= PE_IMP_ID_MAX || !uses || s_denies == DENY_MAX) return -1;
    snprintf(s_deny[s_denies].id, sizeof s_deny[0].id, "%s", asset_id);
    snprintf(s_deny[s_denies].reason, sizeof s_deny[0].reason, "%s", reason ? reason : "excluded");
    s_deny[s_denies++].uses = uses;
    return 0;
}

static const char *use_name(PeImpUse u) {
    return u == PE_IMP_USE_COSTUME ? "a costume" : u == PE_IMP_USE_ENEMY ? "an enemy" : u == PE_IMP_USE_WEAPON ? "a held weapon"
                                                                                                          : "a prop";
}

static int face_corners(const PeImpPart *pt, uint32_t i) { return i < pt->tris ? 3 : 4; }
static const PeImpFace *face_at(const PeImpPart *pt, uint32_t i) { return i < pt->tris ? &pt->tri[i] : &pt->quad[i - pt->tris]; }

static int flat_tri(const PeImpFace *f, int a, int b, int c) {
    long ax = f->v[b][0] - f->v[a][0], ay = f->v[b][1] - f->v[a][1], az = f->v[b][2] - f->v[a][2];
    long bx = f->v[c][0] - f->v[a][0], by = f->v[c][1] - f->v[a][1], bz = f->v[c][2] - f->v[a][2];
    return !(ay * bz - az * by) && !(az * bx - ax * bz) && !(ax * by - ay * bx);
}

/* zero area: a triangle, or both triangles of a quad (0 1 2 and 1 3 2, the PS1's order) */
static int degenerate(const PeImpFace *f, int k) { return flat_tri(f, 0, 1, 2) && (k == 3 || flat_tri(f, 1, 3, 2)); }

static int off_texture(const PeImpModel *m, const PeImpFace *f, int k, uint32_t first) {
    for (int j = 0; j < k; j++) {
        uint32_t pg = f->page & 15u;
        if (pg < first) return 1;
        uint32_t x = (pg - first) * 128u + f->uv[j][0];
        if (x >= m->w || f->uv[j][1] >= m->h) return 1;
    }
    return 0;
}

static int stub(const PeImpPart *pt) {
    uint32_t nf = pt->tris + pt->quads;
    if (nf != 1) return 0;
    const PeImpFace *f = face_at(pt, 0);
    int k = face_corners(pt, 0);
    for (int c = 0; c < 3; c++) {
        int lo = f->v[0][c], hi = lo;
        for (int j = 1; j < k; j++) { if (f->v[j][c] < lo) lo = f->v[j][c]; if (f->v[j][c] > hi) hi = f->v[j][c]; }
        if (hi - lo > STUB_BOX) return 0;
    }
    return 1;
}

static int copy_model(const PeImpModel *s, PeImpModel *d) {
    memset(d, 0, sizeof *d);
    d->bones = s->bones, d->parts = s->parts, d->pal_rows = s->pal_rows, d->w = s->w, d->h = s->h;
    d->parent = malloc(s->bones * sizeof *d->parent);
    d->rel = malloc(s->bones * sizeof *d->rel), d->pos = malloc(s->bones * sizeof *d->pos);
    d->part = calloc(s->parts ? s->parts : 1, sizeof *d->part);
    d->pal = malloc((s->pal_rows ? s->pal_rows : 1) * sizeof *d->pal);
    d->image = malloc((size_t)s->w * s->h ? (size_t)s->w * s->h : 1);
    if (!d->parent || !d->rel || !d->pos || !d->part || !d->pal || !d->image) return -1;
    memcpy(d->parent, s->parent, s->bones * sizeof *d->parent);
    memcpy(d->rel, s->rel, s->bones * sizeof *d->rel), memcpy(d->pos, s->pos, s->bones * sizeof *d->pos);
    memcpy(d->pal, s->pal, s->pal_rows * sizeof *d->pal), memcpy(d->image, s->image, (size_t)s->w * s->h);
    for (uint32_t p = 0; p < s->parts; p++) {
        const PeImpPart *a = &s->part[p];
        PeImpPart *b = &d->part[p];
        b->tris = a->tris, b->quads = a->quads;
        if (a->tris && !(b->tri = malloc(a->tris * sizeof *b->tri))) return -1;
        if (a->quads && !(b->quad = malloc(a->quads * sizeof *b->quad))) return -1;
        if (a->tris) memcpy(b->tri, a->tri, a->tris * sizeof *b->tri);
        if (a->quads) memcpy(b->quad, a->quad, a->quads * sizeof *b->quad);
        if (a->tri_skin && a->tris && !(b->tri_skin = malloc(a->tris * sizeof *b->tri_skin))) return -1;
        if (a->quad_skin && a->quads && !(b->quad_skin = malloc(a->quads * sizeof *b->quad_skin))) return -1;
        if (b->tri_skin) memcpy(b->tri_skin, a->tri_skin, a->tris * sizeof *b->tri_skin);
        if (b->quad_skin) memcpy(b->quad_skin, a->quad_skin, a->quads * sizeof *b->quad_skin);
    }
    return 0;
}

/* keep the faces `keep` says (per face of the part, triangles then quads), skins in step */
static void keep_faces(PeImpPart *pt, const uint8_t *keep) {
    uint32_t t = 0, q = 0;
    for (uint32_t i = 0; i < pt->tris; i++)
        if (keep[i]) { pt->tri[t] = pt->tri[i]; if (pt->tri_skin) memcpy(pt->tri_skin[t], pt->tri_skin[i], sizeof pt->tri_skin[0]); t++; }
    for (uint32_t i = 0; i < pt->quads; i++)
        if (keep[pt->tris + i]) { pt->quad[q] = pt->quad[i]; if (pt->quad_skin) memcpy(pt->quad_skin[q], pt->quad_skin[i], sizeof pt->quad_skin[0]); q++; }
    pt->tris = t, pt->quads = q;
}

static int32_t joint_height(const PeImpModel *m) {
    int32_t lo = 0, hi = 0;
    for (uint32_t b = 0; b < m->bones; b++) {
        if (!b || m->pos[b][1] < lo) lo = m->pos[b][1];
        if (!b || m->pos[b][1] > hi) hi = m->pos[b][1];
    }
    return hi - lo;
}

int pe_imp_fit_model(const PeImpModel *src, const char *asset_id, PeImpUse use, PeImpModel *out, PeImpFitInfo *info,
                     char *why, size_t n) {
    PeImpFitInfo in;
    memset(&in, 0, sizeof in);
    memset(out, 0, sizeof *out);
#define REFUSE(...) do { if (why && n) snprintf(why, n, __VA_ARGS__); if (info) *info = in; return -1; } while (0)
    if (use < PE_IMP_USE_COSTUME || use > PE_IMP_USE_PROP) REFUSE("no such use");
    for (int i = 0; asset_id && i < s_denies; i++)
        if (!strcmp(s_deny[i].id, asset_id) && (s_deny[i].uses >> use & 1)) REFUSE("%s isn't used as %s: %s", asset_id, use_name(use), s_deny[i].reason);
    char check[160];
    if (pe_imp_model_check(src, check, sizeof check)) REFUSE("%s", check);

    /* measure */
    in.height = joint_height(src);
    in.facing = asset_id && (!strncmp(asset_id, "re1:", 4) || !strncmp(asset_id, "re2:", 4) || !strncmp(asset_id, "re3:", 4)) ? -1 : 0;
    in.person = src->bones == 15 && in.height >= PERSON_MIN && in.height <= PERSON_MAX;
    for (uint32_t b = 0; in.person && b < 15; b++) in.person = src->parent[b] == k_human[b];
    uint32_t lo = 15, hi = 0;
    for (uint32_t p = 0; p < src->parts; p++) {
        const PeImpPart *pt = &src->part[p];
        in.skinned |= pt->tri_skin || pt->quad_skin;
        for (uint32_t i = 0; i < pt->tris + pt->quads; i++) {
            uint32_t pg = face_at(pt, i)->page & 15u;
            if (pg < lo) lo = pg;
            if (pg > hi) hi = pg;
        }
    }
    in.page_first = lo <= hi ? lo : 0, in.pages = lo <= hi ? hi - lo + 1 : 0;

    /* classify */
    int wearable = use == PE_IMP_USE_COSTUME || use == PE_IMP_USE_WEAPON;
    if (wearable) {
        if (src->bones != 15) REFUSE("%u bones: not the RE human skeleton", (unsigned)src->bones);
        for (uint32_t b = 0; b < 15; b++)
            if (src->parent[b] != k_human[b]) REFUSE("bone %u hangs from %d: not the RE human skeleton", (unsigned)b, src->parent[b]);
        if (in.height < PERSON_MIN || in.height > PERSON_MAX)
            REFUSE("a skeleton %d units tall: not standing at a person's height (%d-%d)", (int)in.height, PERSON_MIN, PERSON_MAX);
        if (in.skinned) REFUSE("skinned: the costume builder takes rigid parts");
        if (in.pages > 2) REFUSE("its faces use texture pages %u to %u; a costume's texture holds two", lo, hi);
    }

    /* normalize, on a copy */
    if (copy_model(src, out)) { pe_imp_model_free(out); REFUSE("out of memory"); }
    for (uint32_t p = 0; p < out->parts; p++) {
        PeImpPart *pt = &out->part[p];
        uint32_t nf = pt->tris + pt->quads;
        if (!nf) continue;
        if (wearable && stub(pt)) { in.dropped_stub += nf; pt->tris = pt->quads = 0; continue; }
        uint8_t *keep = malloc(nf);
        if (!keep) { pe_imp_model_free(out); REFUSE("out of memory"); }
        for (uint32_t i = 0; i < nf; i++) {
            const PeImpFace *f = face_at(pt, i);
            int k = face_corners(pt, i);
            keep[i] = 1;
            if (degenerate(f, k)) keep[i] = 0, in.dropped_degenerate++;
            else if (off_texture(out, f, k, in.page_first)) {
                if (wearable) {
                    unsigned w = (unsigned)out->w, h = (unsigned)out->h;          /* before the copy goes */
                    free(keep), pe_imp_model_free(out);
                    REFUSE("a face of part %u points outside its %ux%u texture", (unsigned)p, w, h);
                }
                if (use == PE_IMP_USE_PROP) keep[i] = 0, in.dropped_off_texture++;
            }
        }
        keep_faces(pt, keep);
        free(keep);
    }
    for (uint32_t p = 0; p < out->parts; p++) in.faces += out->part[p].tris + out->part[p].quads;
    if (!in.faces) { pe_imp_model_free(out); REFUSE("no faces left"); }
    if (pe_imp_model_check(out, check, sizeof check)) { pe_imp_model_free(out); REFUSE("%s", check); }
    if (info) *info = in;
    return 0;
#undef REFUSE
}

int pe_imp_fit(const char *root, const char *asset_id, PeImpUse use, PeImpModel *out, PeImpFitInfo *info, char *why,
               size_t n) {
    PeImpModel m;
    memset(out, 0, sizeof *out);
    if (pe_imp_catalog_model(root, asset_id, &m, why, n)) return -1;
    int rc = pe_imp_fit_model(&m, asset_id, use, out, info, why, n);
    pe_imp_model_free(&m);
    return rc;
}
