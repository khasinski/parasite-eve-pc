/* Model audit: every Model the importer reads from the RE discs, checked and measured, to plan the
 * clean-up layer between the readers and the mods (costumes, enemies, props). Read only; never
 * part of the test suite's pass/fail (it only fails on a crash or an unreadable catalog).
 *   PE_TEST_RE1_DISC / PE_TEST_RE2_DISC / PE_TEST_RE3_DISC   the discs (any subset)
 *   PE_AUDIT_OUT   the CSV report (default pe-model-audit/report.csv)
 * Per model: its game, category (id prefix), bones, parts, faces, empty and stub parts, skinned
 * parts, degenerate faces, texture size and palette rows, the palette rows its faces use, the
 * texture pages its faces use, faces whose texels fall outside the picture under the costume
 * builder's rule (x = (page & 1) * 128 + u) and under a page-relative rule (x = (page - first page
 * used) * 128 + u), and its size (bounding box of the rest pose). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_internal.h"
#include "pe_src_re1.h"
#include "pe_src_re2.h"
#include "pe_src_re3.h"

extern const PeImpReader g_re1_emd_ps1, g_re1_emd_npc_ps1, g_re1_emd_alt_ps1, g_re1_monsters_ps1, g_re1_objects_ps1,
    g_re1_door_models_ps1, g_re1_weapons_ps1;
extern const PeImpReader g_re2_pld_ps1, g_re2_emd_ps1, g_re2_objects_ps1, g_re2_door_models_ps1, g_re2_weapons_ps1;
extern const PeImpReader g_re3_pld_ps1, g_re3_emd_ps1, g_re3_objects_ps1, g_re3_door_models_ps1, g_re3_weapons_ps1;

typedef struct {
    int models, human15, uv_builder, uv_relative, clut_over, degenerate, stubs, skinned, wide;
    double min_h, max_h;
} Tally;

static const char *category(const char *id) {
    const char *c = strchr(id, ':');
    c = c ? c + 1 : id;
    static const char *const k[] = {"player/", "enemy/", "weapon/", "object/", "item/", "doormodel/"};
    for (size_t i = 0; i < sizeof k / sizeof *k; i++)
        if (!strncmp(c, k[i], strlen(k[i]))) return k[i];
    return "other";
}

static int cat_index(const char *c) {
    static const char *const k[] = {"player/", "enemy/", "weapon/", "object/", "item/", "doormodel/", "other"};
    for (int i = 0; i < 7; i++) if (!strcmp(c, k[i])) return i;
    return 6;
}

static void audit_game(const char *game, const char *cue, const PeImpReader *const *readers, FILE *csv, Tally t[7]) {
    char root[64];
    snprintf(root, sizeof root, "pe-model-audit/%s", game);
    pe_imp_registry_reset();
    for (int i = 0; readers[i]; i++) assert(!pe_imp_add_reader(readers[i]));
    PeImpScan sc;
    if (pe_imp_scan(root, cue, game, &sc)) { printf("%s: scan failed: %s\n", game, sc.last_error); return; }
    printf("%s: %d files read, %d items, %d refused%s%s\n", game, sc.files_read, sc.items, sc.refused,
           sc.refused ? ", last: " : "", sc.refused ? sc.last_error : "");
    PeImpAsset *a;
    int n = pe_imp_catalog_assets(root, game, &a);
    for (int i = 0; i < n; i++) {
        if (strcmp(a[i].type, "Model")) continue;
        char why[200];
        PeImpModel m;
        if (pe_imp_catalog_model(root, a[i].id, &m, why, sizeof why)) {
            fprintf(csv, "%s,%s,%s,LOAD FAILED: %s\n", game, a[i].id, category(a[i].id), why);
            continue;
        }
        uint32_t faces = 0, empty = 0, stubs = 0, skinned = 0, degenerate = 0, uv_builder = 0, uv_relative = 0;
        unsigned rows_used[64] = {0}, nrows = 0, pages_used = 0, first_page = 0xFFFF, last_page = 0;
        for (uint32_t p = 0; p < m.parts; p++)
            for (uint32_t q = 0; q < m.part[p].tris + m.part[p].quads; q++) {
                const PeImpFace *f = q < m.part[p].tris ? &m.part[p].tri[q] : &m.part[p].quad[q - m.part[p].tris];
                unsigned pg = f->page & 15;
                if (pg < first_page) first_page = pg;
                if (pg > last_page) last_page = pg;
                pages_used |= 1u << pg;
            }
        double lo[3] = {1e9, 1e9, 1e9}, hi[3] = {-1e9, -1e9, -1e9};
        for (uint32_t p = 0; p < m.parts; p++) {
            const PeImpPart *pt = &m.part[p];
            uint32_t nf = pt->tris + pt->quads;
            faces += nf;
            if (!nf) empty++;
            else if (nf <= 1) stubs++;
            if (pt->tri_skin || pt->quad_skin) skinned++;
            const int32_t *base = !pt->tri_skin && !pt->quad_skin && p < m.bones ? m.pos[p] : NULL;
            for (uint32_t q = 0; q < nf; q++) {
                const PeImpFace *f = q < pt->tris ? &pt->tri[q] : &pt->quad[q - pt->tris];
                int k = q < pt->tris ? 3 : 4, out_b = 0, out_r = 0;
                for (int j = 0; j < k; j++) {
                    for (int c = 0; c < 3; c++) {
                        double v = f->v[j][c] + (base ? base[c] : 0);
                        if (v < lo[c]) lo[c] = v;
                        if (v > hi[c]) hi[c] = v;
                    }
                    uint32_t xb = (f->page & 1) * 128u + f->uv[j][0], xr = ((f->page & 15) - first_page) * 128u + f->uv[j][0];
                    if (xb >= m.w || f->uv[j][1] >= m.h) out_b = 1;
                    if (xr >= m.w || f->uv[j][1] >= m.h) out_r = 1;
                }
                uv_builder += out_b, uv_relative += out_r;
                /* degenerate: two corners the same, or a zero-area triangle */
                long ax = f->v[1][0] - f->v[0][0], ay = f->v[1][1] - f->v[0][1], az = f->v[1][2] - f->v[0][2];
                long bx = f->v[2][0] - f->v[0][0], by = f->v[2][1] - f->v[0][1], bz = f->v[2][2] - f->v[0][2];
                long cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
                if (!cx && !cy && !cz) degenerate++;
                unsigned row = f->clut >> 6, seen = 0;
                for (unsigned r = 0; r < nrows; r++) seen |= rows_used[r] == row;
                if (!seen && nrows < 64) rows_used[nrows++] = row;
            }
        }
        double height = hi[1] - lo[1], width = hi[0] - lo[0], depth = hi[2] - lo[2];
        if (faces == 0) height = width = depth = 0;
        int npages = 0;
        for (int b = 0; b < 16; b++) npages += pages_used >> b & 1;
        int human15 = m.bones == 15;
        fprintf(csv, "%s,%s,%s,%u,%u,%u,%u,%u,%u,%u,%ux%u,%u,%u,%d,%u-%u,%u,%u,%.0f,%.0f,%.0f,%s\n", game, a[i].id,
                category(a[i].id), m.bones, m.parts, faces, empty, stubs, skinned, degenerate, m.w, m.h, m.pal_rows, nrows,
                npages, first_page == 0xFFFF ? 0 : first_page, last_page, uv_builder, uv_relative, width, height, depth, a[i].tags);
        Tally *x = &t[cat_index(category(a[i].id))];
        x->models++;
        x->human15 += human15;
        x->uv_builder += uv_builder > 0;
        x->uv_relative += uv_relative > 0;
        x->clut_over += nrows > m.pal_rows;
        x->degenerate += degenerate > 0;
        x->stubs += stubs > 0;
        x->skinned += skinned > 0;
        x->wide += m.w > 256;
        if (faces && (x->models == 1 || height < x->min_h)) x->min_h = height;
        if (faces && height > x->max_h) x->max_h = height;
        pe_imp_model_free(&m);
    }
    free(a);
}

int main(void) {
    const char *out = getenv("PE_AUDIT_OUT");
    pe_imp_mkdirs("pe-model-audit");
    FILE *csv = fopen(out && *out ? out : "pe-model-audit/report.csv", "w");
    assert(csv);
    fprintf(csv, "game,id,category,bones,parts,faces,empty_parts,stub_parts,skinned_parts,degenerate_faces,texture,"
                 "pal_rows,rows_used,pages_used,page_range,uv_out_builder,uv_out_relative,width,height,depth,tags\n");
    static const PeImpReader *const re1[] = {&g_re1_emd_ps1, &g_re1_emd_npc_ps1, &g_re1_emd_alt_ps1, &g_re1_monsters_ps1,
                                             &g_re1_objects_ps1, &g_re1_door_models_ps1, &g_re1_weapons_ps1, NULL};
    static const PeImpReader *const re2[] = {&g_re2_pld_ps1, &g_re2_emd_ps1, &g_re2_objects_ps1, &g_re2_door_models_ps1,
                                             &g_re2_weapons_ps1, NULL};
    static const PeImpReader *const re3[] = {&g_re3_pld_ps1, &g_re3_emd_ps1, &g_re3_objects_ps1, &g_re3_door_models_ps1,
                                             &g_re3_weapons_ps1, NULL};
    struct { const char *game, *env; const PeImpReader *const *r; } g[] = {
        {"re1", "PE_TEST_RE1_DISC", re1}, {"re2", "PE_TEST_RE2_DISC", re2}, {"re3", "PE_TEST_RE3_DISC", re3}};
    static const char *const cats[] = {"player/", "enemy/", "weapon/", "object/", "item/", "doormodel/", "other"};
    for (int k = 0; k < 3; k++) {
        const char *cue = getenv(g[k].env);
        if (!cue || !*cue) { printf("%s not set: %s skipped\n", g[k].env, g[k].game); continue; }
        Tally t[7];
        memset(t, 0, sizeof t);
        audit_game(g[k].game, cue, g[k].r, csv, t);
        for (int c = 0; c < 7; c++) {
            if (!t[c].models) continue;
            printf("  %s %-11s %4d models: 15-bone %d, skinned %d, texture wider than 256 %d, texels off (builder rule) %d,"
                   " (page-relative) %d, palette rows over %d, degenerate faces %d, stub parts %d, height %.0f-%.0f\n",
                   g[k].game, cats[c], t[c].models, t[c].human15, t[c].skinned, t[c].wide, t[c].uv_builder, t[c].uv_relative,
                   t[c].clut_over, t[c].degenerate, t[c].stubs, t[c].min_h, t[c].max_h);
        }
    }
    fclose(csv);
    printf("report: %s\n", out && *out ? out : "pe-model-audit/report.csv");
    return 0;
}
