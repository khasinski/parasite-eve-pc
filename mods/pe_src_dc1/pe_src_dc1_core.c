/* Dino Crisis (PS1) Regina: PSX/DATA/CORE0x.DAT, one outfit each. Read only.
 * Worked out from the disc (no public documentation found):
 *   archive   a 2 KiB index of 16-byte entries (type, size, a, b) until "dummy header"; each block
 *             starts on the next 2 KiB boundary. Type 8: an image (a = VRAM x, b's low half = width
 *             in 16-bit units), type 2: palettes (b = height << 16 | width), type 7: data loaded at
 *             RAM address a. Types 7 and 8 are compressed.
 *   compression  a flag byte, low bit first: 1 = a literal byte, 0 = a 2-byte reference, distance
 *             b0 | (b1 & 15) << 8 back from the end of the output, length (b1 >> 4) + 2
 *   image     stored in 64 x 32 tiles, row by row; the model's 256 x 256 page is the top 256 rows
 *   model     the block loaded at 0x80180000 (pointers are RAM addresses). Its header: vertex pool,
 *             normal pool, triangle and quad pointers, u16 triangles, quads, bones (15), 0; then per
 *             bone 20 bytes: x, y, z s16 offset from the parent, u8 parent (255: root), u8 q, its
 *             triangles and quads (pointers, u16 counts). The pools are shared: bone k owns the next
 *             3 x q vertices, in its frame; a face may use a neighbour's vertices (the joint).
 *             Triangle (16 bytes): i0 i1 i2 (u16), u0 v0, u1 v1, clut, u2 v2, page.
 *             Quad (20 bytes): i0 i1 i2 i3, u0 v0, clut, u1 v1, page, u2 v2, u3 v3.
 * Dino Crisis's x is RE's z and z is RE's x (turned a quarter: (x, y, z) -> (z, y, -x)); its bones
 * come out in RE2's order. Each face goes to the bone most of its corners follow, in that bone's
 * frame, as RE3's skinned parts do. Regina's right hand always holds a gun (the hands come with the
 * weapon); her left hand, mirrored, stands in for it. Checked on CORE00-CORE03. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_rig.h"
#include "pe_src_dc1.h"

#define BONES 15
#define MODEL_AT 0x80180000u
#define OUT_MAX (1u << 20)                         /* a decompressed block */
/* RE2's part i is Dino Crisis's part k_from_dc1[i] (sides as RE2's once turned) */
static const int k_from_dc1[BONES] = {0, 8, 9, 10, 11, 12, 13, 14, 7, 1, 2, 3, 4, 5, 6};

typedef struct { const uint8_t *b; uint32_t size; } Buf;
static int ok(const Buf *x, uint64_t at, uint64_t len) { return at <= x->size && len <= x->size - at; }
static unsigned u16at(const Buf *x, uint32_t at) { return x->b[at] | x->b[at + 1] << 8; }
static uint32_t u32at(const Buf *x, uint32_t at) { return u16at(x, at) | (uint32_t)u16at(x, at + 2) << 16; }
static int16_t s16at(const Buf *x, uint32_t at) { return (int16_t)u16at(x, at); }

int dc1_lz(const uint8_t *src, uint32_t n, uint8_t **out, uint32_t *size) {
    uint32_t cap = 4096, len = 0, i = 0;
    uint8_t *o = malloc(cap);
    if (!o) return -1;
    while (i < n) {
        unsigned fl = src[i++];
        for (int k = 0; k < 8 && i < n; k++) {
            unsigned lit = fl >> k & 1, run = 1, d = 0;
            if (!lit) {
                if (i + 1 >= n) break;
                d = src[i] | (src[i + 1] & 15u) << 8, run = (src[i + 1] >> 4) + 2u, i += 2;
                if (!d || d > len) { free(o); return -1; }
            }
            if (len + run > OUT_MAX) { free(o); return -1; }
            if (len + run > cap) {
                while (len + run > cap) cap *= 2;
                uint8_t *g = realloc(o, cap);
                if (!g) { free(o); return -1; }
                o = g;
            }
            if (lit) o[len++] = src[i++];
            else for (unsigned j = 0; j < run; j++, len++) o[len] = o[len - d];
        }
    }
    *out = o, *size = len;
    return 0;
}

typedef struct { PeImpFace *f; uint32_t count, cap; } List;
static int push(List *l, const PeImpFace *f) {
    if (l->count == l->cap) {
        uint32_t cap = l->cap ? 2 * l->cap : 32;
        PeImpFace *g = realloc(l->f, cap * sizeof *g);
        if (!g) return -1;
        l->f = g, l->cap = cap;
    }
    l->f[l->count++] = *f;
    return 0;
}

/* (x, y, z) Dino Crisis -> RE's frame */
static void turn(const int32_t in[3], int32_t out[3]) { out[0] = in[2], out[1] = in[1], out[2] = -in[0]; }

#define NEED(at, len, what) do { if (!ok(&x, (at), (len))) { snprintf(why, n, "CORE: %s past the end", what); goto bad; } } while (0)

/* the model header: four rising pointers into the block, then triangles, quads, 15 bones, 0, with
 * the bones' counts adding up */
static int find_header(const Buf *x) {
    for (uint32_t h = 0; h + 24 + 20 * BONES <= x->size; h += 4) {
        uint32_t p[4];
        int fits = 1;
        for (int k = 0; k < 4; k++) {
            p[k] = u32at(x, h + 4 * (uint32_t)k);
            fits &= p[k] >= MODEL_AT && p[k] - MODEL_AT < x->size && (!k || p[k] > p[k - 1]);
        }
        if (!fits || u16at(x, h + 20) != BONES || u16at(x, h + 22) || (p[1] - p[0]) % 8 || (p[2] - p[1]) % 8) continue;
        uint32_t nt = 0, nq = 0;
        for (uint32_t k = 0; k < BONES; k++) nt += u16at(x, h + 24 + 20 * k + 16), nq += u16at(x, h + 24 + 20 * k + 18);
        if (nt == u16at(x, h + 16) && nq == u16at(x, h + 18)) return (int)h;
    }
    return -1;
}

int dc1_core_parse(const uint8_t *data, uint32_t size, PeImpModel *m, char *why, size_t n) {
    uint8_t *model = NULL, *image = NULL;
    uint32_t model_size = 0, image_size = 0, image_w = 0, pal_at = 0, pal_rows = 0;
    List out[BONES][2];                            /* per Dino Crisis bone: triangles, quads */
    memset(out, 0, sizeof out), memset(m, 0, sizeof *m);
    {
        Buf x = {data, size};
        NEED(0, 0x800, "archive index");
        uint32_t at = 0x800;
        for (uint32_t i = 0; i < 128 && memcmp(data + 16 * i, "dummy", 5); i++) {
            uint32_t t = u32at(&x, 16 * i), sz = u32at(&x, 16 * i + 4), a = u32at(&x, 16 * i + 8), b = u32at(&x, 16 * i + 12);
            NEED(at, sz, "archive block");
            if (t == 8 && !image) {
                if (dc1_lz(data + at, sz, &image, &image_size)) { snprintf(why, n, "CORE: image doesn't decompress"); goto bad; }
                image_w = 2 * (b & 0xffff);
            } else if (t == 2 && !pal_rows) {
                pal_at = at, pal_rows = sz / 512;
            } else if (t == 7 && a == MODEL_AT && !model) {
                if (dc1_lz(data + at, sz, &model, &model_size)) { snprintf(why, n, "CORE: model doesn't decompress"); goto bad; }
            }
            at += (sz + 2047) / 2048 * 2048;
        }
        if (!model || !image || !pal_rows) { snprintf(why, n, "CORE: no model, image or palette"); goto bad; }
        if (image_w != 256 || image_size < 256 * 256) { snprintf(why, n, "CORE: image %u wide, %u bytes", image_w, image_size); goto bad; }
        m->pal_rows = pal_rows > 256 ? 256 : pal_rows;
        m->pal = calloc(m->pal_rows, sizeof *m->pal);
        m->image = malloc(256 * 256);
        if (!m->pal || !m->image) { snprintf(why, n, "out of memory"); goto bad; }
        for (uint32_t r = 0; r < m->pal_rows; r++)
            for (uint32_t c = 0; c < 256; c++) m->pal[r][c] = (uint16_t)u16at(&x, pal_at + 512 * r + 2 * c);
        for (uint32_t y = 0; y < 256; y++)
            for (uint32_t xx = 0; xx < 256; xx++)
                m->image[y * 256 + xx] = image[((y / 32) * 4 + xx / 64) * 2048 + (y % 32) * 64 + xx % 64];
        m->w = m->h = 256;
    }
    {
        Buf x = {model, model_size};
        int h = find_header(&x);
        if (h < 0) { snprintf(why, n, "CORE: no model header"); goto bad; }
        uint32_t hd = (uint32_t)h, pv = u32at(&x, hd) - MODEL_AT, pn = u32at(&x, hd + 4) - MODEL_AT;
        uint32_t nv = (pn - pv) / 8, nn = (u32at(&x, hd + 8) - MODEL_AT - pn) / 8;
        int parent[BONES], owner_end[BONES], to_re2[BONES];
        int32_t pos[BONES][3];
        uint32_t owned = 0;
        for (int k = 0; k < BONES; k++) to_re2[k_from_dc1[k]] = k;
        for (int k = 0; k < BONES; k++) {
            uint32_t e = hd + 24 + 20 * (uint32_t)k;
            int p = x.b[e + 6];
            parent[k] = p == 255 ? -1 : p;
            if (parent[k] >= k || (k && parent[k] < 0) || (parent[k] >= 0 && to_re2[parent[k]] >= to_re2[k])) {
                snprintf(why, n, "CORE: not Regina's skeleton");
                goto bad;
            }
            for (int c = 0; c < 3; c++) pos[k][c] = (parent[k] >= 0 ? pos[parent[k]][c] : 0) + s16at(&x, e + 2 * (uint32_t)c);
            owned += 3u * x.b[e + 7];
            owner_end[k] = (int)owned;
        }
        if (owned != nv || nn < nv) { snprintf(why, n, "CORE: %u vertices owned of %u", owned, nv); goto bad; }
        uint32_t total = 0;
        for (int k = 0; k < BONES; k++) {
            uint32_t e = hd + 24 + 20 * (uint32_t)k;
            for (int kind = 0; kind < 2; kind++) {
                uint32_t p = u32at(&x, e + 8 + 4 * (uint32_t)kind), cnt = u16at(&x, e + 16 + 2 * (uint32_t)kind);
                uint32_t corners = kind ? 4 : 3, rec = kind ? 20 : 16;
                if (!cnt) continue;
                if (p < MODEL_AT) { snprintf(why, n, "CORE: bad face pointer"); goto bad; }
                NEED((uint64_t)p - MODEL_AT, (uint64_t)rec * cnt, "faces");
                for (uint32_t q = 0; q < cnt; q++) {
                    uint32_t a = p - MODEL_AT + rec * q, ix[4], own[4];
                    PeImpFace f;
                    memset(&f, 0, sizeof f);
                    static const uint8_t uv3[3] = {6, 8, 12}, uv4[4] = {8, 12, 16, 18};
                    for (uint32_t j = 0; j < corners; j++) {
                        ix[j] = u16at(&x, a + 2 * j);
                        if (ix[j] >= nv) { snprintf(why, n, "CORE: bone %d face %u points past the vertices", k, q); goto bad; }
                        own[j] = 0;
                        while ((int)ix[j] >= owner_end[own[j]]) own[j]++;
                        uint32_t u = a + (kind ? uv4[j] : uv3[j]);
                        f.uv[j][0] = x.b[u], f.uv[j][1] = x.b[u + 1];
                    }
                    f.clut = (uint16_t)u16at(&x, a + 10);
                    f.page = 0x80;                     /* one 256-wide page: (page & 1) picks no second half */
                    /* the bone most corners follow; ties: the face's own, then the lowest */
                    int bone = k, best = -1;
                    for (uint32_t j = 0; j < corners; j++) {
                        int b = (int)own[j], c = 0;
                        for (uint32_t t = 0; t < corners; t++) c += (int)own[t] == b;
                        int score = 2 * c + (b == k);
                        if (score > best || (score == best && b < bone)) best = score, bone = b;
                    }
                    for (uint32_t j = 0; j < corners; j++) {
                        int32_t w[3], v[3], nv3[3], nt3[3];
                        for (int c = 0; c < 3; c++) {
                            w[c] = s16at(&x, pv + 8 * ix[j] + 2 * (uint32_t)c) + pos[own[j]][c] - pos[bone][c];
                            nv3[c] = s16at(&x, pn + 8 * ix[j] + 2 * (uint32_t)c);
                        }
                        turn(w, v), turn(nv3, nt3);
                        for (int c = 0; c < 3; c++) {
                            if (v[c] < -32768 || v[c] > 32767) { snprintf(why, n, "CORE: vertex out of range"); goto bad; }
                            f.v[j][c] = (int16_t)v[c], f.n[j][c] = (int16_t)nt3[c];
                        }
                    }
                    if (++total > PE_IMP_MODEL_FACES_MAX || push(&out[bone][kind], &f)) { snprintf(why, n, "too many faces"); goto bad; }
                }
            }
        }
        /* the right hand (bone 3) from the left (6): mirrored across the body, winding kept */
        for (int kind = 0; kind < 2; kind++) {
            static const int order[2][4] = {{0, 2, 1, 3}, {1, 0, 3, 2}};
            out[3][kind].count = 0;
            for (uint32_t i = 0; i < out[6][kind].count; i++) {
                PeImpFace f, s = out[6][kind].f[i];
                memset(&f, 0, sizeof f);
                for (int j = 0; j < 3 + kind; j++) {
                    int o = order[kind][j];
                    for (int c = 0; c < 3; c++) f.v[j][c] = s.v[o][c], f.n[j][c] = s.n[o][c];
                    f.v[j][2] = (int16_t)-s.v[o][2], f.n[j][2] = (int16_t)-s.n[o][2];
                    f.uv[j][0] = s.uv[o][0], f.uv[j][1] = s.uv[o][1];
                }
                f.clut = s.clut, f.page = s.page;
                if (push(&out[3][kind], &f)) { snprintf(why, n, "out of memory"); goto bad; }
            }
        }
        m->bones = BONES;
        m->parent = malloc(BONES * sizeof *m->parent);
        m->rel = calloc(BONES, sizeof *m->rel);
        m->pos = calloc(BONES, sizeof *m->pos);
        m->parts = BONES;
        m->part = calloc(BONES, sizeof *m->part);
        if (!m->parent || !m->rel || !m->pos || !m->part) { snprintf(why, n, "out of memory"); goto bad; }
        for (int r = 0; r < BONES; r++) {
            int k = k_from_dc1[r];
            int32_t rel[3];
            for (int c = 0; c < 3; c++) rel[c] = pos[k][c] - (parent[k] >= 0 ? pos[parent[k]][c] : 0);
            m->parent[r] = (int16_t)(parent[k] >= 0 ? to_re2[parent[k]] : -1);
            turn(rel, m->rel[r]);
            m->part[r].tri = out[k][0].f, m->part[r].tris = out[k][0].count;
            m->part[r].quad = out[k][1].f, m->part[r].quads = out[k][1].count;
            out[k][0].f = out[k][1].f = NULL;
        }
        for (int r = 0; r < BONES; r++)
            for (int c = 0; c < 3; c++) m->pos[r][c] = (m->parent[r] >= 0 ? m->pos[m->parent[r]][c] : 0) + m->rel[r][c];
    }
    free(model), free(image);
    return pe_imp_model_check(m, why, n);
bad:
    for (int i = 0; i < BONES; i++) free(out[i][0].f), free(out[i][1].f);
    free(model), free(image);
    pe_imp_model_free(m);
    return -1;
}

/* "PSX/DATA/CORE00.DAT" -> proposed "player/core00", name "CORE00" */
static int read_core(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    PeImpModel *m = calloc(1, sizeof *m);
    if (!m) return -1;
    if (dc1_core_parse(data, size, m, why, n)) { free(m); return -1; }
    const char *s = strrchr(path, '/');
    s = s ? s + 1 : path;
    char stem[16] = "", id[32];
    size_t k = 0;
    while (s[k] && s[k] != '.' && k + 1 < sizeof stem) stem[k] = s[k], k++;
    stem[k] = 0;
    for (k = 0; stem[k]; k++) id[k] = (char)(stem[k] >= 'A' && stem[k] <= 'Z' ? stem[k] + 32 : stem[k]);
    id[k] = 0;
    char proposed[48];
    snprintf(proposed, sizeof proposed, "player/%s", id);
    return pe_imp_emit(sink, PE_IMP_MODEL, proposed, stem, m);
}

const PeImpReader g_dc1_core_ps1 = {
    {PE_IMP_API_VERSION, sizeof(PeImpReader)}, "dc1", "ps1", "core", 1, PE_IMP_MODEL,
    "PSX/DATA/CORE0?.DAT", "human player", read_core};

/* RE2's fit (pe_src_re2_pld.c explains each line). Regina's mesh is whole at the waist: no hip
 * lift, no lids. */
#define B(x) {PE_SEG_BONE, x, 0}
#define MID(x, y) {PE_SEG_MID, x, y}
#define MESH(x) {PE_SEG_MESH, x, 0}
const PeImpRig g_dc1_human_rig = {
    "dc1/human", 15,
    {
        {1,  {B(0), B(8)},          {B(1), B(10)}},              /* chest */
        {20, {B(1), MID(2, 5)},     {B(20), MID(22, 26)}},       /* hips */
        {22, {B(2), B(3)},          {B(22), B(23)}},             /* thigh */
        {23, {B(3), B(4)},          {B(23), B(24)}},             /* shin */
        {24, {B(4), MESH(4)},       {B(24), MESH(24)}},          /* foot */
        {26, {B(5), B(6)},          {B(26), B(27)}},
        {27, {B(6), B(7)},          {B(27), B(28)}},
        {28, {B(7), MESH(7)},       {B(28), MESH(28)}},
        {11, {B(8), MESH(8)},       {B(10), MESH(11)}},          /* head (with the neck) */
        {4,  {B(9), B(10)},         {B(4), B(5)}},               /* upper arm */
        {5,  {B(10), B(11)},        {B(5), B(6)}},               /* forearm */
        {6,  {B(11), MESH(11)},     {B(6), MESH(6)}},            /* hand */
        {14, {B(12), B(13)},        {B(14), B(15)}},
        {15, {B(13), B(14)},        {B(15), B(16)}},
        {16, {B(14), MESH(14)},     {B(16), MESH(16)}},
    },
    {-1, -1, 5, 6, 7, 2, 3, 4, -1, 12, 13, 14, 9, 10, 11},
    {-2, -2, -2, -2, 3, -2, -2, 6, -1, -2, -2, 10, -2, -2, 13},
    {0, 0, 0, 0, 1, 0, 0, 1},
    {0, 1}, {B(8), MID(2, 5)}, {B(10), MID(22, 26)},
    1, -32768, 0, 0.7,
    4, 2, 5,
    0, 0};
#undef B
#undef MID
#undef MESH

/* Regina's outfits, named by sight from the disc's textures (2026-11-18) */
const char g_dc1_naming[] =
    "{\"player/core00\": {\"id\": \"player/regina\", \"name\": \"Regina\"},"
    " \"player/core01\": {\"id\": \"player/regina_camo\", \"name\": \"Camo Regina\"},"
    " \"player/core02\": {\"id\": \"player/regina_red\", \"name\": \"Red Regina\"},"
    " \"player/core03\": {\"id\": \"player/regina_leopard\", \"name\": \"Leopard Regina\"}}";
