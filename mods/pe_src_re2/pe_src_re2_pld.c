/* Resident Evil 2 player models (PLxx.PLD), PS1 and PC: skeleton, mesh, texture. Read only.
 * Ported from tools/re2-skin/re2_model.py (PR #4), the same layout, every offset checked:
 *   header   u32 directory offset; there: EDD, EMR, MD1, TIM offsets
 *   EMR      u16 armature offset, ?, bone count, ?; bone offsets (x, y, z s16) from byte 8;
 *            armature: per bone (u16 child count, u16 offset of its child list), children u8
 *   MD1      u32 at +8: object count; from +12, per part a triangle object then a quad object,
 *            each 7 x u32 (vertices, count, normals, count, faces, count, texture records), offsets
 *            from the end of the 12-byte header. Vertices / normals x, y, z, pad s16. Triangle
 *            faces n0 v0 n1 v1 n2 v2 u16 (quads + n3 v3); texture records u, v u8 + u16 per corner
 *            (corner 0 holds the CLUT, corner 1 the page)
 *   TIM      8-bit image with palettes; each palette row 256 colours
 * Vertices are in their bone's frame with no rest rotation: a bone's rest position is the sum of
 * the offsets up its chain. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_rig.h"
#include "pe_src_re2.h"

typedef struct { const uint8_t *b; uint32_t size; } Buf;

static int ok(const Buf *x, uint64_t at, uint64_t len) { return at <= x->size && len <= x->size - at; }
static unsigned u8at(const Buf *x, uint32_t at) { return x->b[at]; }
static unsigned u16at(const Buf *x, uint32_t at) { return x->b[at] | x->b[at + 1] << 8; }
static uint32_t u32at(const Buf *x, uint32_t at) { return u16at(x, at) | (uint32_t)u16at(x, at + 2) << 16; }
static int16_t s16at(const Buf *x, uint32_t at) { return (int16_t)u16at(x, at); }

#define NEED(at, len, what) do { if (!ok(&x, (at), (len))) { snprintf(why, n, "PLD: %s past the end", what); goto bad; } } while (0)

int re2_pld_parse(const uint8_t *data, uint32_t size, PeImpModel *m, char *why, size_t n) {
    Buf x = {data, size};
    memset(m, 0, sizeof *m);
    NEED(0, 8, "header");
    uint32_t d = u32at(&x, 0);
    NEED(d, 16, "directory");
    uint32_t emr = u32at(&x, d + 4), md1 = u32at(&x, d + 8), tim = u32at(&x, d + 12);

    /* skeleton */
    NEED(emr, 8, "skeleton header");
    uint32_t arm = u16at(&x, emr), count = u16at(&x, emr + 4);
    if (!count || count > PE_IMP_MODEL_BONES_MAX) { snprintf(why, n, "PLD: %u bones", count); goto bad; }
    NEED((uint64_t)emr + 8, (uint64_t)6 * count, "bone offsets");
    NEED((uint64_t)emr + arm, (uint64_t)4 * count, "armature");
    m->bones = count;
    m->parent = malloc(count * sizeof *m->parent);
    m->rel = calloc(count, sizeof *m->rel);
    m->pos = calloc(count, sizeof *m->pos);
    if (!m->parent || !m->rel || !m->pos) { snprintf(why, n, "out of memory"); goto bad; }
    for (uint32_t i = 0; i < count; i++) m->parent[i] = -1;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t nc = u16at(&x, emr + arm + 4 * i), off = u16at(&x, emr + arm + 4 * i + 2);
        NEED((uint64_t)emr + arm + off, nc, "child list");
        for (uint32_t k = 0; k < nc; k++) {
            uint32_t c = u8at(&x, emr + arm + off + k);
            if (c >= count) { snprintf(why, n, "PLD: bone %u has child %u of %u", i, c, count); goto bad; }
            m->parent[c] = (int16_t)i;
        }
    }
    for (uint32_t i = 0; i < count; i++) {
        for (int c = 0; c < 3; c++) m->rel[i][c] = s16at(&x, emr + 8 + 6 * i + 2 * c);
        int p = m->parent[i];
        if (p >= (int)i) { snprintf(why, n, "PLD: bone %u's parent %d comes after it", i, p); goto bad; }
        for (int c = 0; c < 3; c++) m->pos[i][c] = (p >= 0 ? m->pos[p][c] : 0) + m->rel[i][c];
    }

    /* mesh: per part a triangle object, then a quad object */
    NEED(md1, 12, "mesh header");
    uint32_t objs = u32at(&x, md1 + 8), base = md1 + 12, parts = objs / 2;
    if (parts > PE_IMP_MODEL_PARTS_MAX) { snprintf(why, n, "PLD: %u parts", parts); goto bad; }
    NEED(base, (uint64_t)56 * parts, "part table");
    m->parts = parts;
    m->part = calloc(parts ? parts : 1, sizeof *m->part);
    if (!m->part) { snprintf(why, n, "out of memory"); goto bad; }
    uint32_t total = 0;
    for (uint32_t i = 0; i < parts; i++) {
        for (int kind = 0; kind < 2; kind++) {
            uint32_t at = base + 56 * i + 28 * (uint32_t)kind, k = kind ? 4 : 3;
            uint32_t vo = u32at(&x, at), vc = u32at(&x, at + 4), no = u32at(&x, at + 8), nc = u32at(&x, at + 12);
            uint32_t fo = u32at(&x, at + 16), fc = u32at(&x, at + 20), to = u32at(&x, at + 24);
            if (fc > PE_IMP_MODEL_FACES_MAX || (total += fc) > PE_IMP_MODEL_FACES_MAX) { snprintf(why, n, "PLD: too many faces"); goto bad; }
            NEED((uint64_t)base + vo, (uint64_t)8 * vc, "vertices");
            NEED((uint64_t)base + no, (uint64_t)8 * nc, "normals");
            NEED((uint64_t)base + fo, (uint64_t)4 * k * fc, "faces");
            NEED((uint64_t)base + to, (uint64_t)4 * k * fc, "texture records");
            PeImpFace *f = fc ? calloc(fc, sizeof *f) : NULL;
            if (fc && !f) { snprintf(why, n, "out of memory"); goto bad; }
            if (kind) m->part[i].quad = f, m->part[i].quads = fc;
            else m->part[i].tri = f, m->part[i].tris = fc;
            for (uint32_t q = 0; q < fc; q++) {
                uint32_t fa = base + fo + 4 * k * q, ta = base + to + 4 * k * q;
                for (uint32_t j = 0; j < k; j++) {
                    uint32_t ni = u16at(&x, fa + 4 * j), vi = u16at(&x, fa + 4 * j + 2);
                    if (vi >= vc || ni >= nc) { snprintf(why, n, "PLD: part %u face %u points past its vertices", i, q); goto bad; }
                    for (int c = 0; c < 3; c++) {
                        f[q].v[j][c] = s16at(&x, base + vo + 8 * vi + 2 * c);
                        f[q].n[j][c] = s16at(&x, base + no + 8 * ni + 2 * c);
                    }
                    f[q].uv[j][0] = (uint8_t)u8at(&x, ta + 4 * j);
                    f[q].uv[j][1] = (uint8_t)u8at(&x, ta + 4 * j + 1);
                }
                f[q].clut = (uint16_t)u16at(&x, ta + 2);
                f[q].page = (uint16_t)u16at(&x, ta + 6);
            }
        }
    }

    /* texture: an 8-bit TIM with palettes of 256 */
    NEED(tim, 20, "texture header");
    uint32_t flag = u32at(&x, tim + 4);
    if ((flag & 3) != 1 || !(flag & 8)) { snprintf(why, n, "PLD: expected an 8-bit texture with palettes"); goto bad; }
    uint32_t ln = u32at(&x, tim + 8), cw = u16at(&x, tim + 16), ch = u16at(&x, tim + 18);
    if (cw != 256 || !ch || ch > 256) { snprintf(why, n, "PLD: palettes %ux%u", cw, ch); goto bad; }
    NEED((uint64_t)tim + 20, (uint64_t)2 * cw * ch, "palettes");
    m->pal_rows = ch;
    m->pal = calloc(ch, sizeof *m->pal);
    if (!m->pal) { snprintf(why, n, "out of memory"); goto bad; }
    for (uint32_t r = 0; r < ch; r++)
        for (uint32_t c = 0; c < 256; c++) m->pal[r][c] = (uint16_t)u16at(&x, tim + 20 + 2 * cw * r + 2 * c);
    uint64_t at = (uint64_t)tim + 8 + ln;
    NEED(at, 12, "image header");
    uint32_t w = 2u * u16at(&x, (uint32_t)at + 8), h = u16at(&x, (uint32_t)at + 10);
    if (!w || !h || w > 1024 || h > 1024) { snprintf(why, n, "PLD: image %ux%u", w, h); goto bad; }
    NEED(at + 12, (uint64_t)w * h, "image");
    m->w = w, m->h = h;
    m->image = malloc((size_t)w * h);
    if (!m->image) { snprintf(why, n, "out of memory"); goto bad; }
    memcpy(m->image, data + at + 12, (size_t)w * h);
    return pe_imp_model_check(m, why, n);
bad:
    pe_imp_model_free(m);
    return -1;
}

/* "PL1/PLD/PL01.PLD" -> proposed "player/pl01", name "PL01" */
static int read_pld(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    PeImpModel *m = calloc(1, sizeof *m);
    if (!m) return -1;
    if (re2_pld_parse(data, size, m, why, n)) { free(m); return -1; }
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

const PeImpReader g_re2_pld_ps1 = {
    {PE_IMP_API_VERSION, sizeof(PeImpReader)}, "re2", "ps1", "pld", 1, PE_IMP_MODEL,
    "PL?/PLD/PL??.PLD", "human player", read_pld};

/* RE2's 15-part human on Aya's 29-part skeleton (build_re2_skin.py PARTS and friends): each part
 * is bound to the Aya bone it matches; a segment end is a bone, the midpoint of two, or a part's
 * mesh centre. Left / right are swapped when RE2's z side doesn't match Aya's. */
#define B(x) {PE_SEG_BONE, x, 0}
#define MID(x, y) {PE_SEG_MID, x, y}
#define MESH(x) {PE_SEG_MESH, x, 0}
const PeImpRig g_re2_human_rig = {
    "re2/human", 15,
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
    /* end pieces take the turn of the part above them; the head (-1) stays upright */
    {-2, -2, -2, -2, 3, -2, -2, 6, -1, -2, -2, 10, -2, -2, 13},
    {0, 0, 0, 0, 1, 0, 0, 1},
    {0, 1}, {B(8), MID(2, 5)}, {B(10), MID(22, 26)},
    1, -120, 110, 0.7,
    4, 2, 5,
    0, 1};
#undef B
#undef MID
#undef MESH

/* Hand-kept: the reader proposes player/pl01; people know her as Claire. Never rename an id that
 * a recipe uses. Unnamed models keep their proposed id. */
const char g_re2_naming[] =
    "{\"player/pl00\": {\"id\": \"player/leon\", \"name\": \"Leon\"},"
    " \"player/pl01\": {\"id\": \"player/claire\", \"name\": \"Claire\"},"
    /* named by sight from the discs' own textures (2026-11-18); PL02-PL07 (Leon and Claire
     * variants whose in-game use isn't known) keep their proposed IDs */
    " \"player/pl08\": {\"id\": \"player/leon_tank_top\", \"name\": \"Tank Top Leon\"},"
    " \"player/pl09\": {\"id\": \"player/claire_flame_jacket\", \"name\": \"Flame Jacket Claire\"},"
    " \"player/pl0a\": {\"id\": \"player/leon_leather_jacket\", \"name\": \"Leather Jacket Leon\"},"
    " \"player/pl0c\": {\"id\": \"player/hunk\", \"name\": \"HUNK\"},"
    " \"player/pl0d\": {\"id\": \"player/tofu\", \"name\": \"Tofu\"},"
    " \"player/pl0e\": {\"id\": \"player/ada\", \"name\": \"Ada\"},"
    " \"player/pl0f\": {\"id\": \"player/sherry\", \"name\": \"Sherry\"}}";
