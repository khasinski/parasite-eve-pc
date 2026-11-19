/* Imported Models for the viewers. See pe_model_imp.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_model_imp.h"

#define PE_VRAM_W 1024
#define PE_VRAM_H 512
#define PE_PAL_Y  256                        /* palettes from this VRAM row */

static const PeImpFace *face_at(const PeImpPart *p, uint32_t i) { return i < p->tris ? &p->tri[i] : &p->quad[i - p->tris]; }
static int skinned(const PeImpPart *p, uint32_t i) { return i < p->tris ? p->tri_skin != NULL : p->quad_skin != NULL; }

int pe_model_from_imp(const PeImpModel *m, int part, PeModel *out, char *why, size_t n) {
    int rows[256], nrows = 0;
    uint32_t first = 16, faces = 0, verts = 0, p, i, k;
    memset(out, 0, sizeof *out);
    if (!m || part >= (int)m->parts) { snprintf(why, n, "no part %d", part); return -1; }
    for (p = 0; p < m->parts; p++)                       /* the model's pages and palette rows */
        for (i = 0; i < m->part[p].tris + m->part[p].quads; i++) {
            const PeImpFace *f = face_at(&m->part[p], i);
            const int r = f->clut >> 6;
            int dup = 0;
            if ((f->page & 15u) < first) first = f->page & 15u;
            for (k = 0; k < (uint32_t)nrows; k++) dup |= rows[k] == r;
            if (!dup && nrows < 256) rows[nrows++] = r;
            if (part < 0 || (int)p == part) faces++, verts += i < m->part[p].tris ? 3 : 4;
        }
    for (int a = 1; a < nrows; a++) for (int b = a; b > 0 && rows[b - 1] > rows[b]; b--) { int t = rows[b]; rows[b] = rows[b - 1], rows[b - 1] = t; }
    if (!faces) { snprintf(why, n, "no faces to show"); return -1; }
    if (verts > 65535) { snprintf(why, n, "%u corners, more than a view takes", verts); return -1; }
    out->parts = 1, out->verts = (int)verts, out->faces = (int)faces, out->commands = 1;
    out->part = calloc(1, sizeof *out->part);
    out->vert = calloc(verts, sizeof *out->vert);
    out->face = calloc(faces, sizeof *out->face);
    out->command = calloc(1, 1);
    out->vram = calloc((size_t)PE_VRAM_W * PE_VRAM_H, sizeof *out->vram);
    if (!out->part || !out->vert || !out->face || !out->command || !out->vram) {
        pe_model_free(out);
        snprintf(why, n, "out of memory");
        return -1;
    }
    out->part[0].vertex_count = (uint16_t)verts, out->part[0].visible = 1;
    uint32_t v = 0, fo = 0;
    for (p = 0; p < m->parts; p++) {
        if (part >= 0 && (int)p != part) continue;
        const PeImpPart *pt = &m->part[p];
        for (i = 0; i < pt->tris + pt->quads; i++) {
            const PeImpFace *f = face_at(pt, i);
            const int quad = i >= pt->tris, corners = quad ? 4 : 3;
            const int moved = p < m->bones && !skinned(pt, i);     /* a rigid part sits at its bone */
            PeModelFace *g = &out->face[fo++];
            int rank = 0;
            for (k = 0; k < (uint32_t)nrows; k++) if (rows[k] == f->clut >> 6) rank = (int)k;
            g->quad = (uint8_t)quad, g->textured = 1;
            g->tpage = (uint16_t)(0x80u | (((f->page & 15u) - first) & 15u));   /* 8-bit, x = page * 64 */
            g->clut = (uint16_t)((PE_PAL_Y + rank) << 6);
            for (int c = 0; c < corners; c++, v++) {
                for (int a = 0; a < 3; a++) out->vert[v][a] = (int16_t)(f->v[c][a] + (moved ? m->pos[p][a] : 0));
                g->v[c] = (uint16_t)v, g->u[c] = f->uv[c][0], g->t[c] = f->uv[c][1];
            }
        }
    }
    for (uint32_t y = 0; y < m->h && y < 256; y++)       /* the picture: two indexes a VRAM word */
        for (uint32_t x = 0; x < m->w && x < 2 * PE_VRAM_W; x++)
            out->vram[y * PE_VRAM_W + x / 2] |= (uint16_t)(m->image[y * m->w + x] << ((x & 1) * 8));
    for (uint32_t r = 0; r < m->pal_rows && PE_PAL_Y + r < PE_VRAM_H; r++)
        memcpy(&out->vram[(PE_PAL_Y + r) * PE_VRAM_W], m->pal[r], sizeof m->pal[r]);
    return 0;
}
