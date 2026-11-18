/* Resident Evil 3 player models (CD_DATA/PLD/PLxx.PLD), PS1. Read only.
 *   header   u32 directory offset, u32 count (5); there: EDD, EMR, MD2, skin table, TIM offsets
 *   EMR      as RE2's (bone offsets from byte 8, armature). RE3 orders the 15 bones differently:
 *            0 chest, 1 head, 2-4 and 5-7 arms, 8 waist, 9-11 and 12-14 legs
 *   MD2      u32 length, u32 object count; from +8, 24 bytes per object: vertices, normals offsets,
 *            vertex count, triangles, quads offsets (u32, from +8), triangle, quad counts (u16).
 *            Vertices and normals x, y, z, pad s16, one normal per vertex.
 *            Triangle (12 bytes): u0 v0 clut(u16) u1 v1 page i0 u2 v2 i1 i2
 *            Quad (16 bytes):     u0 v0 clut(u16) u1 v1 page pad u2 v2 i0 i1 u3 v3 i2 i3
 *   skin     u16 ?, u16 0, u16 data start, u16 group count; per group 4 u16 offsets, each from
 *            its group's own entry (8 + 8 g). The first lists bone records until 0xff: bone u8,
 *            triples u8, 3 x triples vertex indexes (the last repeated to fill a triple).
 *   TIM      as RE2's: 8-bit with palettes of 256
 * A skinned part's mesh is one piece in the root's frame (an arm from shoulder to wrist, a leg
 * from knee to toe); its group says which bone moves each vertex. Its bones' own parts are
 * placeholders (a 3-vertex triangle). The rest (hands) are in their bone's frame, as in RE2.
 * Read as RE2's 15-part human: each skinned face goes to the bone most of its corners follow, in
 * that bone's frame; vertices no record lists (seams the game blends) follow the nearest listed
 * one. Bones and parts come out in RE2's order, so RE2's rig profile applies.
 * Checked on PL00-PL0E (PL0D is empty, PL0F is not a person: both refused). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_rig.h"
#include "pe_src_re3.h"

typedef struct { const uint8_t *b; uint32_t size; } Buf;

static int ok(const Buf *x, uint64_t at, uint64_t len) { return at <= x->size && len <= x->size - at; }
static unsigned u8at(const Buf *x, uint32_t at) { return x->b[at]; }
static unsigned u16at(const Buf *x, uint32_t at) { return x->b[at] | x->b[at + 1] << 8; }
static uint32_t u32at(const Buf *x, uint32_t at) { return u16at(x, at) | (uint32_t)u16at(x, at + 2) << 16; }
static int16_t s16at(const Buf *x, uint32_t at) { return (int16_t)u16at(x, at); }

/* not one of the people (an empty file, a prop): no error, nothing to read */
#define NOT_A_PERSON(...) do { snprintf(why, n, __VA_ARGS__); rc = -2; goto bad; } while (0)
#define NEED(at, len, what) do { if (!ok(&x, (at), (len))) { snprintf(why, n, "PLD: %s past the end", what); goto bad; } } while (0)

#define BONES 15
#define VERTS 256                                 /* vertex indexes are a byte */
static const int k_to_re2[BONES] = {0, 8, 9, 10, 11, 12, 13, 14, 1, 2, 3, 4, 5, 6, 7};

typedef struct { uint32_t vo, no, vc, to, qo, tc, qc; } Obj;
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

int re3_pld_parse(const uint8_t *data, uint32_t size, PeImpModel *m, char *why, size_t n) {
    Buf x = {data, size};
    int rc = -1;
    List out[BONES][2];                           /* per RE3 bone: triangles, quads */
    memset(out, 0, sizeof out);
    memset(m, 0, sizeof *m);
    if (size < 64) NOT_A_PERSON("PLD: %u bytes", size);
    uint32_t d = u32at(&x, 0), dn = u32at(&x, 4);
    if (dn != 5) NOT_A_PERSON("PLD: %u sections, expected 5", dn);
    NEED(d, 20, "directory");
    uint32_t emr = u32at(&x, d + 4), md2 = u32at(&x, d + 8), skin = u32at(&x, d + 12), tim = u32at(&x, d + 16);

    /* skeleton, RE3's order */
    NEED(emr, 8, "skeleton header");
    uint32_t arm = u16at(&x, emr), count = u16at(&x, emr + 4);
    if (count != BONES) NOT_A_PERSON("PLD: %u bones, not RE3's 15-bone person", count);
    NEED((uint64_t)emr + 8, 6 * BONES, "bone offsets");
    NEED((uint64_t)emr + arm, 4 * BONES, "armature");
    int parent[BONES];
    int32_t rel[BONES][3], pos[BONES][3];
    for (int i = 0; i < BONES; i++) parent[i] = -1;
    for (uint32_t i = 0; i < BONES; i++) {
        uint32_t nc = u16at(&x, emr + arm + 4 * i), off = u16at(&x, emr + arm + 4 * i + 2);
        NEED((uint64_t)emr + arm + off, nc, "child list");
        for (uint32_t k = 0; k < nc; k++) {
            uint32_t c = u8at(&x, emr + arm + off + k);
            if (c >= BONES) { snprintf(why, n, "PLD: bone %u has child %u", i, c); goto bad; }
            parent[c] = (int)i;
        }
    }
    for (int i = 0; i < BONES; i++) {
        if (parent[i] >= i) { snprintf(why, n, "PLD: bone %d's parent %d comes after it", i, parent[i]); goto bad; }
        for (int c = 0; c < 3; c++) {
            rel[i][c] = s16at(&x, emr + 8 + 6 * (uint32_t)i + 2 * (uint32_t)c);
            pos[i][c] = (parent[i] >= 0 ? pos[parent[i]][c] : 0) + rel[i][c];
        }
    }
    /* the same tree as RE2's, renumbered: RE2's order must keep parents first */
    for (int i = 0; i < BONES; i++)
        if (parent[i] >= 0 && k_to_re2[parent[i]] >= k_to_re2[i]) { snprintf(why, n, "PLD: not RE3's person skeleton"); goto bad; }

    /* mesh objects (the first 15 are the bones'; the rest are small boxes the game uses itself) */
    NEED(md2, 8, "mesh header");
    uint32_t nobj = u32at(&x, md2 + 4), base = md2 + 8;
    if (nobj < BONES) { snprintf(why, n, "PLD: %u mesh objects", nobj); goto bad; }
    NEED(base, 24 * BONES, "object table");
    Obj o[BONES];
    for (uint32_t i = 0; i < BONES; i++) {
        uint32_t at = base + 24 * i;
        o[i].vo = u32at(&x, at), o[i].no = u32at(&x, at + 4), o[i].vc = u32at(&x, at + 8);
        o[i].to = u32at(&x, at + 12), o[i].qo = u32at(&x, at + 16);
        o[i].tc = u16at(&x, at + 20), o[i].qc = u16at(&x, at + 22);
        if (o[i].vc > VERTS) { snprintf(why, n, "PLD: part %u has %u vertices", i, o[i].vc); goto bad; }
        NEED((uint64_t)base + o[i].vo, (uint64_t)8 * o[i].vc, "vertices");
        NEED((uint64_t)base + o[i].no, (uint64_t)8 * o[i].vc, "normals");
        NEED((uint64_t)base + o[i].to, (uint64_t)12 * o[i].tc, "triangles");
        NEED((uint64_t)base + o[i].qo, (uint64_t)16 * o[i].qc, "quads");
    }

    /* skin groups: which bone moves each vertex of a skinned part */
    NEED(skin, 8, "skin header");
    uint32_t groups = u16at(&x, skin + 6);
    if (groups > BONES) { snprintf(why, n, "PLD: %u skin groups", groups); goto bad; }
    NEED((uint64_t)skin + 8, (uint64_t)8 * groups, "skin groups");
    int skinned[BONES], in_skin[BONES];
    int16_t bone_of[BONES][VERTS];
    memset(skinned, 0, sizeof skinned), memset(in_skin, 0, sizeof in_skin);
    for (uint32_t g = 0; g < groups; g++) {
        int16_t vb[VERTS];
        for (int v = 0; v < VERTS; v++) vb[v] = -1;
        uint32_t entry = skin + 8 + 8 * g, p = entry + u16at(&x, entry), top = 0, used = 0;
        for (;;) {
            NEED(p, 1, "skin records");
            if (u8at(&x, p) == 0xff) break;
            NEED(p, 2, "skin record");
            uint32_t bone = u8at(&x, p), nt = u8at(&x, p + 1);
            if (bone >= BONES) { snprintf(why, n, "PLD: skin record for bone %u", bone); goto bad; }
            NEED((uint64_t)p + 2, 3 * nt, "skin vertices");
            used |= 1u << bone;
            for (uint32_t k = 0; k < 3 * nt; k++) {
                uint32_t v = u8at(&x, p + 2 + k);
                if (vb[v] < 0) vb[v] = (int16_t)bone;
                if (v + 1 > top) top = v + 1;
            }
            p += 2 + 3 * nt;
        }
        /* the group's part: its lowest real bone above the root, else the root */
        int part = 0;
        for (int b = 1; b < BONES; b++) if (used >> b & 1 && o[b].vc > 3) { part = b; break; }
        if (skinned[part] || !top || top > o[part].vc) NOT_A_PERSON("PLD: skin group %u doesn't fit a part", g);
        skinned[part] = 1;
        for (int b = 0; b < BONES; b++) in_skin[b] |= (int)(used >> b & 1);
        /* seams no record lists follow the nearest listed vertex (ties: the lowest index) */
        for (uint32_t v = 0; v < o[part].vc; v++) {
            if (vb[v] >= 0) { bone_of[part][v] = vb[v]; continue; }
            int64_t best = -1;
            int bb = part;
            for (uint32_t u = 0; u < o[part].vc; u++) {
                if (vb[u] < 0) continue;
                int64_t dd = 0;
                for (int c = 0; c < 3; c++) {
                    int64_t e = (int64_t)s16at(&x, base + o[part].vo + 8 * u + 2 * (uint32_t)c) - s16at(&x, base + o[part].vo + 8 * v + 2 * (uint32_t)c);
                    dd += e * e;
                }
                if (best < 0 || dd < best) best = dd, bb = vb[u];
            }
            bone_of[part][v] = (int16_t)bb;
        }
    }

    /* faces, each into the bone that carries it */
    uint32_t total = 0;
    for (int i = 0; i < BONES; i++) {
        if (!skinned[i] && o[i].vc <= 3 && in_skin[i]) continue;           /* a skinned bone's placeholder */
        for (int kind = 0; kind < 2; kind++) {
            uint32_t fc = kind ? o[i].qc : o[i].tc, k = kind ? 4 : 3;
            for (uint32_t q = 0; q < fc; q++) {
                uint32_t a = base + (kind ? o[i].qo + 16 * q : o[i].to + 12 * q), idx[4];
                PeImpFace f;
                memset(&f, 0, sizeof f);
                if (kind) idx[0] = u8at(&x, a + 10), idx[1] = u8at(&x, a + 11), idx[2] = u8at(&x, a + 14), idx[3] = u8at(&x, a + 15);
                else idx[0] = u8at(&x, a + 7), idx[1] = u8at(&x, a + 10), idx[2] = u8at(&x, a + 11);
                static const uint8_t uv_at[2][4] = {{0, 4, 8, 0}, {0, 4, 8, 12}};
                for (uint32_t j = 0; j < k; j++) {
                    if (idx[j] >= o[i].vc) { snprintf(why, n, "PLD: part %d face %u points past its vertices", i, q); goto bad; }
                    f.uv[j][0] = (uint8_t)u8at(&x, a + uv_at[kind][j]);
                    f.uv[j][1] = (uint8_t)u8at(&x, a + uv_at[kind][j] + 1);
                }
                f.clut = (uint16_t)u16at(&x, a + 2);
                f.page = (uint16_t)u8at(&x, a + 6);
                int bone = i;
                if (skinned[i]) {                 /* most corners' bone; ties: the part's own, then the lowest */
                    int best = -1;
                    for (uint32_t j = 0; j < k; j++) {
                        int b = bone_of[i][idx[j]], cnt = 0;
                        for (uint32_t t = 0; t < k; t++) cnt += bone_of[i][idx[t]] == b;
                        int score = 2 * cnt + (b == i);
                        if (score > best || (score == best && b < bone)) best = score, bone = b;
                    }
                }
                for (uint32_t j = 0; j < k; j++)
                    for (int c = 0; c < 3; c++) {
                        int32_t v = s16at(&x, base + o[i].vo + 8 * idx[j] + 2 * (uint32_t)c);
                        if (skinned[i]) v -= pos[bone][c] - pos[0][c];
                        if (v < -32768 || v > 32767) { snprintf(why, n, "PLD: part %d vertex out of range", i); goto bad; }
                        f.v[j][c] = (int16_t)v;
                        f.n[j][c] = s16at(&x, base + o[i].no + 8 * idx[j] + 2 * (uint32_t)c);
                    }
                if (++total > PE_IMP_MODEL_FACES_MAX) { snprintf(why, n, "PLD: too many faces"); goto bad; }
                if (push(&out[bone][kind], &f)) { snprintf(why, n, "out of memory"); goto bad; }
            }
        }
    }

    /* the model, in RE2's order */
    m->bones = BONES;
    m->parent = malloc(BONES * sizeof *m->parent);
    m->rel = calloc(BONES, sizeof *m->rel);
    m->pos = calloc(BONES, sizeof *m->pos);
    m->parts = BONES;
    m->part = calloc(BONES, sizeof *m->part);
    if (!m->parent || !m->rel || !m->pos || !m->part) { snprintf(why, n, "out of memory"); goto bad; }
    for (int i = 0; i < BONES; i++) {
        int r = k_to_re2[i];
        m->parent[r] = (int16_t)(parent[i] >= 0 ? k_to_re2[parent[i]] : -1);
        memcpy(m->rel[r], rel[i], sizeof rel[i]);
    }
    for (int r = 0; r < BONES; r++)
        for (int c = 0; c < 3; c++) m->pos[r][c] = (m->parent[r] >= 0 ? m->pos[m->parent[r]][c] : 0) + m->rel[r][c];
    for (int i = 0; i < BONES; i++) {
        PeImpPart *pt = &m->part[k_to_re2[i]];
        pt->tri = out[i][0].f, pt->tris = out[i][0].count;
        pt->quad = out[i][1].f, pt->quads = out[i][1].count;
        out[i][0].f = out[i][1].f = NULL;
    }

    /* texture: an 8-bit TIM with palettes of 256 */
    NEED(tim, 20, "texture header");
    {
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
    }
    return pe_imp_model_check(m, why, n);
bad:
    for (int i = 0; i < BONES; i++) free(out[i][0].f), free(out[i][1].f);
    pe_imp_model_free(m);
    return rc;
}

/* "CD_DATA/PLD/PL08.PLD" -> proposed "player/pl08", name "PL08". Files that aren't one of the
 * people are passed over without a word. */
static int read_pld(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    PeImpModel *m = calloc(1, sizeof *m);
    if (!m) return -1;
    int rc = re3_pld_parse(data, size, m, why, n);
    if (rc) { free(m); return rc == -2 ? 0 : -1; }
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

const PeImpReader g_re3_pld_ps1 = {
    {PE_IMP_API_VERSION, sizeof(PeImpReader)}, "re3", "ps1", "pld", 1, PE_IMP_MODEL,
    "CD_DATA/PLD/PL??.PLD", "human player", read_pld};

/* The parts come out as RE2's 15-part human, so RE2's fit applies (pe_src_re2_pld.c explains each
 * line). RE3's meshes are whole, with no gap at the waist: no hip lift, no lids to hide. */
#define B(x) {PE_SEG_BONE, x, 0}
#define MID(x, y) {PE_SEG_MID, x, y}
#define MESH(x) {PE_SEG_MESH, x, 0}
const PeImpRig g_re3_human_rig = {
    "re3/human", 15,
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

/* Hand-kept: the reader proposes player/pl00; people know her as Jill. PL01-PL07 are her other
 * outfits, PL09-PL0E the other mercenaries: they keep their proposed IDs for now. */
const char g_re3_naming[] =
    "{\"player/pl00\": {\"id\": \"player/jill\", \"name\": \"Jill\"},"
    " \"player/pl08\": {\"id\": \"player/carlos\", \"name\": \"Carlos\"}}";
