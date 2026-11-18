/* The Model type: check, free, and its store form.
 *
 * Store folder: model.bin (everything, little-endian, below) and model.json (a summary for people:
 * counts only; the loader never trusts it).
 *   "PEIM" u32 version, bones, parts, pal_rows, w, h
 *   bones x (parent s16), bones x (rel s32[3]), bones x (pos s32[3])
 *   parts x (tris u32, quads u32)
 *   per part, its triangles then its quads, each: v s16[k][3], n s16[k][3], uv u8[k][2], clut u16, page u16
 *   pal_rows x 256 u16, then w * h u8
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"

static int fail(char *why, size_t n, const char *fmt, long a, long b) {
    if (why && n) snprintf(why, n, fmt, a, b);
    return -1;
}

void pe_imp_model_free(PeImpModel *m) {
    if (!m) return;
    if (m->part)
        for (uint32_t p = 0; p < m->parts; p++) free(m->part[p].tri), free(m->part[p].quad);
    free(m->parent), free(m->rel), free(m->pos), free(m->part), free(m->pal), free(m->image);
    memset(m, 0, sizeof *m);
}

int pe_imp_model_check(const PeImpModel *m, char *why, size_t n) {
    if (!m->bones || m->bones > PE_IMP_MODEL_BONES_MAX) return fail(why, n, "model: %ld bones", (long)m->bones, 0);
    if (m->parts > PE_IMP_MODEL_PARTS_MAX) return fail(why, n, "model: %ld parts", (long)m->parts, 0);
    if (!m->parent || !m->rel || !m->pos || (m->parts && !m->part)) return fail(why, n, "model: missing arrays", 0, 0);
    for (uint32_t i = 0; i < m->bones; i++) {
        int p = m->parent[i];
        if (p != -1 && (p < 0 || (uint32_t)p >= m->bones || (uint32_t)p == i))
            return fail(why, n, "model: bone %ld has parent %ld", (long)i, p);
        uint32_t b = i, steps = 0;                    /* every chain ends at a root */
        while (m->parent[b] != -1) {
            b = (uint32_t)m->parent[b];
            if (++steps > m->bones) return fail(why, n, "model: bone %ld's chain loops", (long)i, 0);
        }
    }
    uint32_t faces = 0;
    for (uint32_t p = 0; p < m->parts; p++) {
        const PeImpPart *pt = &m->part[p];
        if ((pt->tris && !pt->tri) || (pt->quads && !pt->quad)) return fail(why, n, "model: part %ld has no faces", (long)p, 0);
        faces += pt->tris + pt->quads;
        if (faces > PE_IMP_MODEL_FACES_MAX) return fail(why, n, "model: more than %ld faces", PE_IMP_MODEL_FACES_MAX, 0);
    }
    if (!m->w || !m->h || m->w > 1024 || m->h > 1024 || !m->image) return fail(why, n, "model: image %ldx%ld", (long)m->w, (long)m->h);
    if (!m->pal_rows || m->pal_rows > 256 || !m->pal) return fail(why, n, "model: %ld palettes", (long)m->pal_rows, 0);
    return 0;
}

/* ---- writing ---- */
typedef struct { FILE *f; int bad; } Out;
static void w8(Out *o, unsigned v) { o->bad |= fputc((int)(v & 255), o->f) == EOF; }
static void w16(Out *o, unsigned v) { w8(o, v), w8(o, v >> 8); }
static void w32(Out *o, uint32_t v) { w16(o, v & 0xffff), w16(o, v >> 16); }

static void face_out(Out *o, const PeImpFace *f, int k) {
    for (int j = 0; j < k; j++) for (int c = 0; c < 3; c++) w16(o, (uint16_t)f->v[j][c]);
    for (int j = 0; j < k; j++) for (int c = 0; c < 3; c++) w16(o, (uint16_t)f->n[j][c]);
    for (int j = 0; j < k; j++) w8(o, f->uv[j][0]), w8(o, f->uv[j][1]);
    w16(o, f->clut), w16(o, f->page);
}

int pe_imp_model_save(const PeImpModel *m, const char *folder, char *why, size_t n) {
    if (pe_imp_model_check(m, why, n)) return -1;
    char path[PE_IMP_PATH_MAX];
    snprintf(path, sizeof path, "%s/model.bin", folder);
    Out o = {fopen(path, "wb"), 0};
    if (!o.f) return fail(why, n, "can't write the store (model.bin)", 0, 0);
    fwrite("PEIM", 1, 4, o.f);
    w32(&o, PE_IMP_MODEL_VERSION), w32(&o, m->bones), w32(&o, m->parts), w32(&o, m->pal_rows), w32(&o, m->w), w32(&o, m->h);
    for (uint32_t i = 0; i < m->bones; i++) w16(&o, (uint16_t)m->parent[i]);
    for (uint32_t i = 0; i < m->bones; i++) for (int c = 0; c < 3; c++) w32(&o, (uint32_t)m->rel[i][c]);
    for (uint32_t i = 0; i < m->bones; i++) for (int c = 0; c < 3; c++) w32(&o, (uint32_t)m->pos[i][c]);
    for (uint32_t p = 0; p < m->parts; p++) w32(&o, m->part[p].tris), w32(&o, m->part[p].quads);
    for (uint32_t p = 0; p < m->parts; p++) {
        for (uint32_t i = 0; i < m->part[p].tris; i++) face_out(&o, &m->part[p].tri[i], 3);
        for (uint32_t i = 0; i < m->part[p].quads; i++) face_out(&o, &m->part[p].quad[i], 4);
    }
    for (uint32_t r = 0; r < m->pal_rows; r++) for (int c = 0; c < 256; c++) w16(&o, m->pal[r][c]);
    o.bad |= fwrite(m->image, 1, (size_t)m->w * m->h, o.f) != (size_t)m->w * m->h;
    o.bad |= fclose(o.f) != 0;
    if (o.bad) return fail(why, n, "can't write the store (model.bin)", 0, 0);

    snprintf(path, sizeof path, "%s/model.json", folder);
    FILE *f = fopen(path, "wb");
    if (!f) return fail(why, n, "can't write the store (model.json)", 0, 0);
    uint32_t tris = 0, quads = 0;
    for (uint32_t p = 0; p < m->parts; p++) tris += m->part[p].tris, quads += m->part[p].quads;
    fprintf(f, "{\"type\": \"Model\", \"version\": %d, \"bones\": %u, \"parts\": %u, \"triangles\": %u, "
               "\"quads\": %u, \"palettes\": %u, \"image\": [%u, %u], \"file\": \"model.bin\"}\n",
            PE_IMP_MODEL_VERSION, m->bones, m->parts, tris, quads, m->pal_rows, m->w, m->h);
    return fclose(f) ? fail(why, n, "can't write the store (model.json)", 0, 0) : 0;
}

/* ---- reading: every count is checked against what is left in the file ---- */
typedef struct { const uint8_t *p, *end; int bad; } In;
static unsigned r8(In *i) { if (i->p >= i->end) { i->bad = 1; return 0; } return *i->p++; }
static unsigned r16(In *i) { unsigned a = r8(i); return a | r8(i) << 8; }
static uint32_t r32(In *i) { uint32_t a = r16(i); return a | (uint32_t)r16(i) << 16; }
static int left(In *i, size_t need) { return (size_t)(i->end - i->p) >= need; }

static void face_in(In *in, PeImpFace *f, int k) {
    memset(f, 0, sizeof *f);
    for (int j = 0; j < k; j++) for (int c = 0; c < 3; c++) f->v[j][c] = (int16_t)r16(in);
    for (int j = 0; j < k; j++) for (int c = 0; c < 3; c++) f->n[j][c] = (int16_t)r16(in);
    for (int j = 0; j < k; j++) f->uv[j][0] = (uint8_t)r8(in), f->uv[j][1] = (uint8_t)r8(in);
    f->clut = (uint16_t)r16(in), f->page = (uint16_t)r16(in);
}

int pe_imp_model_load(const char *folder, PeImpModel *m, char *why, size_t n) {
    memset(m, 0, sizeof *m);
    char path[PE_IMP_PATH_MAX];
    snprintf(path, sizeof path, "%s/model.bin", folder);
    FILE *f = fopen(path, "rb");
    if (!f) return fail(why, n, "the store has no model.bin here", 0, 0);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 28 || size > (long)PE_IMP_FILE_MAX) { fclose(f); return fail(why, n, "model.bin: %ld bytes", size, 0); }
    uint8_t *buf = malloc((size_t)size);
    size_t got = buf ? fread(buf, 1, (size_t)size, f) : 0;
    fclose(f);
    if (!buf || got != (size_t)size) { free(buf); return fail(why, n, "model.bin: read error", 0, 0); }
    In in = {buf + 4, buf + size, 0};
    int rc = -1;
    if (memcmp(buf, "PEIM", 4) || r32(&in) != PE_IMP_MODEL_VERSION) { fail(why, n, "model.bin: not a Model v%ld", PE_IMP_MODEL_VERSION, 0); goto out; }
    m->bones = r32(&in), m->parts = r32(&in), m->pal_rows = r32(&in), m->w = r32(&in), m->h = r32(&in);
    if (!m->bones || m->bones > PE_IMP_MODEL_BONES_MAX || m->parts > PE_IMP_MODEL_PARTS_MAX ||
        m->pal_rows > 256 || m->w > 1024 || m->h > 1024 || !left(&in, (size_t)m->bones * 26 + (size_t)m->parts * 8)) {
        fail(why, n, "model.bin: bad header", 0, 0);
        goto out;
    }
    m->parent = calloc(m->bones, sizeof *m->parent);
    m->rel = calloc(m->bones, sizeof *m->rel);
    m->pos = calloc(m->bones, sizeof *m->pos);
    m->part = calloc(m->parts ? m->parts : 1, sizeof *m->part);
    if (!m->parent || !m->rel || !m->pos || !m->part) { fail(why, n, "out of memory", 0, 0); goto out; }
    for (uint32_t i = 0; i < m->bones; i++) m->parent[i] = (int16_t)r16(&in);
    for (uint32_t i = 0; i < m->bones; i++) for (int c = 0; c < 3; c++) m->rel[i][c] = (int32_t)r32(&in);
    for (uint32_t i = 0; i < m->bones; i++) for (int c = 0; c < 3; c++) m->pos[i][c] = (int32_t)r32(&in);
    uint64_t faces = 0;
    for (uint32_t p = 0; p < m->parts; p++) {
        m->part[p].tris = r32(&in), m->part[p].quads = r32(&in);
        faces += (uint64_t)m->part[p].tris + m->part[p].quads;
    }
    if (in.bad || faces > PE_IMP_MODEL_FACES_MAX) { fail(why, n, "model.bin: %ld faces", (long)faces, 0); goto out; }
    for (uint32_t p = 0; p < m->parts; p++) {
        PeImpPart *pt = &m->part[p];
        if (!left(&in, (size_t)pt->tris * 46 + (size_t)pt->quads * 60)) {             /* bytes per face */ fail(why, n, "model.bin: part %ld is cut short", (long)p, 0); goto out; }
        pt->tri = pt->tris ? calloc(pt->tris, sizeof *pt->tri) : NULL;
        pt->quad = pt->quads ? calloc(pt->quads, sizeof *pt->quad) : NULL;
        if ((pt->tris && !pt->tri) || (pt->quads && !pt->quad)) { fail(why, n, "out of memory", 0, 0); goto out; }
        for (uint32_t i = 0; i < pt->tris; i++) face_in(&in, &pt->tri[i], 3);
        for (uint32_t i = 0; i < pt->quads; i++) face_in(&in, &pt->quad[i], 4);
    }
    if (!left(&in, (size_t)m->pal_rows * 512 + (size_t)m->w * m->h)) { fail(why, n, "model.bin: cut short", 0, 0); goto out; }
    m->pal = calloc(m->pal_rows ? m->pal_rows : 1, sizeof *m->pal);
    m->image = malloc((size_t)m->w * m->h ? (size_t)m->w * m->h : 1);
    if (!m->pal || !m->image) { fail(why, n, "out of memory", 0, 0); goto out; }
    for (uint32_t r = 0; r < m->pal_rows; r++) for (int c = 0; c < 256; c++) m->pal[r][c] = (uint16_t)r16(&in);
    memcpy(m->image, in.p, (size_t)m->w * m->h);
    in.p += (size_t)m->w * m->h;
    if (in.bad || in.p != in.end) { fail(why, n, "model.bin: wrong length", 0, 0); goto out; }
    rc = pe_imp_model_check(m, why, n);
out:
    free(buf);
    if (rc) pe_imp_model_free(m);
    return rc;
}
