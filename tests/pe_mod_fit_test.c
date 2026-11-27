/* The fit layer (pe_imp_fit.h) on made-up models: a person accepted as a costume; the wrong tree,
 * height, skinning or three texture pages refused with a reason; placeholder parts and zero-area
 * faces dropped; props dropping faces outside the texture; enemies taking any model; the deny list.
 * No disc data. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_fit.h"

static const int16_t k_human[15] = {-1, 0, 1, 2, 3, 1, 5, 6, 0, 0, 9, 10, 0, 12, 13};

/* a 15-bone person about 2,200 units tall (arms 1,200 up, legs 999 down): one 100-unit triangle
 * per part, a 256 x 256 texture, 2 palettes */
static void person(PeImpModel *m) {
    memset(m, 0, sizeof *m);
    m->bones = 15, m->parts = 15;
    m->parent = malloc(15 * sizeof *m->parent);
    m->rel = calloc(15, sizeof *m->rel), m->pos = calloc(15, sizeof *m->pos);
    m->part = calloc(15, sizeof *m->part);
    for (int b = 0; b < 15; b++) {
        m->parent[b] = k_human[b];
        m->rel[b][1] = b == 0 ? 0 : (b >= 9 ? 1000 / 3 : -300);       /* legs down, the rest up */
        m->pos[b][1] = (k_human[b] >= 0 ? m->pos[k_human[b]][1] : 0) + m->rel[b][1];
        m->part[b].tris = 1;
        m->part[b].tri = calloc(1, sizeof *m->part[b].tri);
        PeImpFace *f = m->part[b].tri;
        f->v[1][0] = 100, f->v[2][1] = 100;
        f->uv[0][0] = 10, f->uv[0][1] = 10, f->uv[1][0] = 60, f->uv[1][1] = 10, f->uv[2][0] = 10, f->uv[2][1] = 60;
        f->page = (uint16_t)(b & 1), f->clut = 480 << 6;
    }
    m->pal_rows = 2, m->pal = calloc(2, sizeof *m->pal);
    m->w = 256, m->h = 256, m->image = calloc(384 * 256, 1);          /* room for a test to widen it to 384 */
}

static int count_faces(const PeImpModel *m) {
    int n = 0;
    for (uint32_t p = 0; p < m->parts; p++) n += (int)(m->part[p].tris + m->part[p].quads);
    return n;
}

static void expect_refused(const PeImpModel *m, PeImpUse use, const char *want) {
    PeImpModel out;
    PeImpFitInfo info;
    char why[200] = "";
    assert(pe_imp_fit_model(m, "re2:player/test", use, &out, &info, why, sizeof why) != 0);
    printf("  refused: %s\n", why);
    assert(strstr(why, want));
}

int main(void) {
    PeImpModel m, out;
    PeImpFitInfo info;
    char why[200];

    person(&m);                                                         /* a person: a costume */
    assert(!pe_imp_fit_model(&m, "re2:player/test", PE_IMP_USE_COSTUME, &out, &info, why, sizeof why));
    printf("person: height %d, pages %u from %u, %u faces, facing %d\n", info.height, info.pages, info.page_first, info.faces, info.facing);
    int32_t lo = 0, hi = 0;                                             /* the joints' height, as the fit measures it */
    for (int b = 0; b < 15; b++) { if (m.pos[b][1] < lo) lo = m.pos[b][1]; if (m.pos[b][1] > hi) hi = m.pos[b][1]; }
    assert(info.person && !info.skinned && info.height == hi - lo && info.pages == 2 && info.faces == 15 && info.facing == -1);
    assert(count_faces(&out) == 15 && count_faces(&m) == 15);
    pe_imp_model_free(&out);

    m.parent[8] = 1;                                                    /* a bone moved: not the human */
    expect_refused(&m, PE_IMP_USE_COSTUME, "not the RE human skeleton");
    assert(!pe_imp_fit_model(&m, "re2:enemy/test", PE_IMP_USE_ENEMY, &out, &info, why, sizeof why) && !info.person);
    pe_imp_model_free(&out);
    m.parent[8] = 0;

    for (int b = 0; b < 15; b++) m.pos[b][1] /= 10;                     /* 200 units: no one stands that short */
    expect_refused(&m, PE_IMP_USE_COSTUME, "person's height");
    for (int b = 0; b < 15; b++) m.pos[b][1] *= 10;

    m.part[3].tri[0].page = 2, m.w = 384;                               /* pages 0 to 2: the builder repacks */
    assert(!pe_imp_fit_model(&m, "re2:player/test", PE_IMP_USE_COSTUME, &out, &info, why, sizeof why) && info.pages == 3);
    pe_imp_model_free(&out);
    m.w = 256;                                                          /* page 2 past a 256-wide picture */
    expect_refused(&m, PE_IMP_USE_COSTUME, "outside its 256x256 texture");
    m.part[3].tri[0].page = 1;

    m.part[4].tri[0].uv[1][1] = 255;                                    /* inside 256 x 256 still */
    m.h = 200;                                                          /* now past the texture */
    expect_refused(&m, PE_IMP_USE_COSTUME, "outside its 256x200 texture");
    assert(!pe_imp_fit_model(&m, "re2:object/test", PE_IMP_USE_PROP, &out, &info, why, sizeof why));
    printf("prop: %u faces outside the texture dropped\n", info.dropped_off_texture);
    assert(info.dropped_off_texture == 1 && count_faces(&out) == 14);
    pe_imp_model_free(&out);
    m.h = 256, m.part[4].tri[0].uv[1][1] = 10;

    PeImpFace *f = m.part[5].tri;                                       /* a zero-area face */
    memset(f->v, 0, sizeof f->v), f->v[1][0] = 50, f->v[2][0] = 100;
    PeImpFace *g = m.part[6].tri;                                       /* a placeholder: one face in a 16-unit box */
    g->v[1][0] = 4, g->v[2][1] = 4;
    assert(!pe_imp_fit_model(&m, "re2:player/test", PE_IMP_USE_COSTUME, &out, &info, why, sizeof why));
    printf("costume: %u zero-area, %u placeholder faces dropped\n", info.dropped_degenerate, info.dropped_stub);
    assert(info.dropped_degenerate == 1 && info.dropped_stub == 1 && count_faces(&out) == 13);
    assert(out.parts == 15 && out.bones == 15);                         /* the bones stay */
    pe_imp_model_free(&out);
    assert(!pe_imp_fit_model(&m, "re2:enemy/test", PE_IMP_USE_ENEMY, &out, &info, why, sizeof why));
    assert(info.dropped_stub == 0 && info.dropped_degenerate == 1 && count_faces(&out) == 14);   /* an enemy keeps placeholders */
    pe_imp_model_free(&out);

    m.part[7].tri_skin = calloc(1, sizeof *m.part[7].tri_skin);         /* skinned: an enemy, not a costume */
    expect_refused(&m, PE_IMP_USE_COSTUME, "skinned");
    assert(!pe_imp_fit_model(&m, "re2:enemy/test", PE_IMP_USE_ENEMY, &out, &info, why, sizeof why) && info.skinned);
    pe_imp_model_free(&out);
    free(m.part[7].tri_skin), m.part[7].tri_skin = NULL;

    pe_imp_fit_reset();                                                 /* a person's call, as data */
    assert(!pe_imp_fit_deny("re2:player/test", 1u << PE_IMP_USE_COSTUME, "a test exclusion"));
    expect_refused(&m, PE_IMP_USE_COSTUME, "a test exclusion");
    assert(!pe_imp_fit_model(&m, "re2:player/test", PE_IMP_USE_ENEMY, &out, &info, why, sizeof why));
    pe_imp_model_free(&out);
    pe_imp_fit_reset();

    pe_imp_model_free(&m);
    printf("fit passed\n");
    return 0;
}
