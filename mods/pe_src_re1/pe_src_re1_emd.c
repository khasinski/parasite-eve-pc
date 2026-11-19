/* Resident Evil (Director's Cut) people: <disc folder>/ENEMY/CHAR1x.EMD, PS1. Read only.
 *   directory  the file's last 20 bytes: 5 u32 section offsets. People's files: 0, the skeleton
 *              (0), the animation, the mesh, the texture; the directory itself ends the texture
 *   EMR        as RE2's (u16 armature offset, ?, bone count, ?; bone offsets from byte 8;
 *              armature: per bone u16 child count, u16 child list offset). RE1 orders the 15
 *              bones: 0 chest, 1 head, 2 waist, 3-5 and 6-8 legs, 9-11 and 12-14 arms
 *   TMD        u32 ?, u32 ?, u32 object count; from +12, 28 bytes per object: vertices offset,
 *              count, normals offset, count, triangles offset, count, ? (u32, offsets from +12).
 *              Vertices and normals x, y, z, pad s16. Triangle (28 bytes): u32 GPU packet,
 *              u0 v0 clut(u16) u1 v1 page(u16) u2 v2 pad(u16), n0 v0 n1 v1 n2 v2 (u16)
 *   TIM        as RE2's: 8-bit with palettes of 256
 * Parts are rigid and in their bone's frame, as in RE2. Bones and parts come out in RE2's order
 * (the same tree, sides as RE2's: BioRand's RE1 -> RE2 part table agrees), so RE2's rig applies;
 * objects past the 15th are kept after them as extra pieces. Checked on CHAR10-CHAR13. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_rig.h"
#include "pe_src_re1.h"

typedef struct { const uint8_t *b; uint32_t size; } Buf;

static int ok(const Buf *x, uint64_t at, uint64_t len) { return at <= x->size && len <= x->size - at; }
static unsigned u8at(const Buf *x, uint32_t at) { return x->b[at]; }
static unsigned u16at(const Buf *x, uint32_t at) { return x->b[at] | x->b[at + 1] << 8; }
static uint32_t u32at(const Buf *x, uint32_t at) { return u16at(x, at) | (uint32_t)u16at(x, at + 2) << 16; }
static int16_t s16at(const Buf *x, uint32_t at) { return (int16_t)u16at(x, at); }

#define NEED(at, len, what) do { if (!ok(&x, (at), (len))) { snprintf(why, n, "EMD: %s past the end", what); goto bad; } } while (0)

#define BONES 15
/* RE2's part i is RE1's part k_from_re1[i] */
static const int k_from_re1[BONES] = {0, 2, 6, 7, 8, 3, 4, 5, 1, 12, 13, 14, 9, 10, 11};

/* the model from the EMR at emr, the TMD at tmd and the TIM at tim. person: RE1's 15-bone person,
 * bones and parts put in RE2's order (k_from_re1); else any skeleton (the monsters), as stored, part
 * i on bone i */
static int parse(const uint8_t *data, uint32_t size, uint32_t emr, uint32_t tmd, uint32_t tim, int person, PeImpModel *m,
                 char *why, size_t n) {
    Buf x = {data, size};
    memset(m, 0, sizeof *m);
    NEED(emr, 8, "skeleton header");
    uint32_t arm = u16at(&x, emr), count = u16at(&x, emr + 4);
    if (person ? count != BONES : !count || count > PE_IMP_MODEL_BONES_MAX) {
        snprintf(why, n, "EMD: %u bones%s", count, person ? ", not RE1's 15-bone person" : "");
        goto bad;
    }
    NEED((uint64_t)emr + 8, 6ull * count, "bone offsets");
    NEED((uint64_t)emr + arm, 4ull * count, "armature");
    int parent[PE_IMP_MODEL_BONES_MAX], to_re2[PE_IMP_MODEL_BONES_MAX], from[PE_IMP_MODEL_BONES_MAX];
    for (uint32_t i = 0; i < count; i++) from[i] = person ? k_from_re1[i] : (int)i;
    for (uint32_t i = 0; i < count; i++) parent[i] = -1, to_re2[from[i]] = (int)i;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t nc = u16at(&x, emr + arm + 4 * i), off = u16at(&x, emr + arm + 4 * i + 2);
        NEED((uint64_t)emr + arm + off, nc, "child list");
        for (uint32_t k = 0; k < nc; k++) {
            uint32_t c = u8at(&x, emr + arm + off + k);
            if (c >= count) { snprintf(why, n, "EMD: bone %u has child %u", i, c); goto bad; }
            parent[c] = (int)i;
        }
    }
    for (int i = 0; i < (int)count; i++)
        if (parent[i] >= i || (parent[i] >= 0 && to_re2[parent[i]] >= to_re2[i]) || (person && i && parent[i] < 0)) {
            snprintf(why, n, "EMD: %s", person ? "not RE1's person skeleton" : "a bone before its parent");
            goto bad;
        }

    /* mesh */
    NEED(tmd, 12, "mesh header");
    uint32_t nobj = u32at(&x, tmd + 8), base = tmd + 12;
    if (nobj < count || nobj > PE_IMP_MODEL_PARTS_MAX) { snprintf(why, n, "EMD: %u mesh objects", nobj); goto bad; }
    NEED(base, (uint64_t)28 * nobj, "object table");
    m->bones = count;
    m->parent = malloc(count * sizeof *m->parent);
    m->rel = calloc(count, sizeof *m->rel);
    m->pos = calloc(count, sizeof *m->pos);
    m->parts = nobj;
    m->part = calloc(nobj, sizeof *m->part);
    if (!m->parent || !m->rel || !m->pos || !m->part) { snprintf(why, n, "out of memory"); goto bad; }
    for (int r = 0; r < (int)count; r++) {
        int i = from[r];
        m->parent[r] = (int16_t)(parent[i] >= 0 ? to_re2[parent[i]] : -1);
        for (int c = 0; c < 3; c++) m->rel[r][c] = s16at(&x, emr + 8 + 6 * (uint32_t)i + 2 * (uint32_t)c);
    }
    for (int r = 0; r < (int)count; r++)
        for (int c = 0; c < 3; c++) m->pos[r][c] = (m->parent[r] >= 0 ? m->pos[m->parent[r]][c] : 0) + m->rel[r][c];
    uint32_t total = 0;
    for (uint32_t p = 0; p < nobj; p++) {
        uint32_t i = p < count ? (uint32_t)from[p] : p, at = base + 28 * i;
        uint32_t vo = u32at(&x, at), vc = u32at(&x, at + 4), no = u32at(&x, at + 8), nc = u32at(&x, at + 12);
        uint32_t fo = u32at(&x, at + 16), fc = u32at(&x, at + 20);
        if (fc > PE_IMP_MODEL_FACES_MAX || (total += fc) > PE_IMP_MODEL_FACES_MAX) { snprintf(why, n, "EMD: too many faces"); goto bad; }
        NEED((uint64_t)base + vo, (uint64_t)8 * vc, "vertices");
        NEED((uint64_t)base + no, (uint64_t)8 * nc, "normals");
        NEED((uint64_t)base + fo, (uint64_t)28 * fc, "triangles");
        PeImpFace *f = fc ? calloc(fc, sizeof *f) : NULL;
        if (fc && !f) { snprintf(why, n, "out of memory"); goto bad; }
        m->part[p].tri = f, m->part[p].tris = fc;
        for (uint32_t q = 0; q < fc; q++) {
            uint32_t fa = base + fo + 28 * q;
            for (uint32_t j = 0; j < 3; j++) {
                uint32_t ni = u16at(&x, fa + 16 + 4 * j), vi = u16at(&x, fa + 18 + 4 * j);
                if (vi >= vc || ni >= nc) { snprintf(why, n, "EMD: object %u face %u points past its vertices", i, q); goto bad; }
                for (int c = 0; c < 3; c++) {
                    f[q].v[j][c] = s16at(&x, base + vo + 8 * vi + 2 * (uint32_t)c);
                    f[q].n[j][c] = s16at(&x, base + no + 8 * ni + 2 * (uint32_t)c);
                }
                f[q].uv[j][0] = (uint8_t)u8at(&x, fa + 4 + 4 * j);
                f[q].uv[j][1] = (uint8_t)u8at(&x, fa + 5 + 4 * j);
            }
            f[q].clut = (uint16_t)u16at(&x, fa + 6);
            f[q].page = (uint16_t)u16at(&x, fa + 10);
        }
    }

    /* texture: an 8-bit TIM with palettes of 256 */
    NEED(tim, 20, "texture header");
    uint32_t flag = u32at(&x, tim + 4);
    if ((flag & 3) != 1 || !(flag & 8)) { snprintf(why, n, "EMD: expected an 8-bit texture with palettes"); goto bad; }
    uint32_t ln = u32at(&x, tim + 8), cw = u16at(&x, tim + 16), ch = u16at(&x, tim + 18);
    if (cw != 256 || !ch || ch > 256) { snprintf(why, n, "EMD: palettes %ux%u", cw, ch); goto bad; }
    NEED((uint64_t)tim + 20, (uint64_t)2 * cw * ch, "palettes");
    m->pal_rows = ch;
    m->pal = calloc(ch, sizeof *m->pal);
    if (!m->pal) { snprintf(why, n, "out of memory"); goto bad; }
    for (uint32_t r = 0; r < ch; r++)
        for (uint32_t c = 0; c < 256; c++) m->pal[r][c] = (uint16_t)u16at(&x, tim + 20 + 2 * cw * r + 2 * c);
    uint64_t at = (uint64_t)tim + 8 + ln;
    NEED(at, 12, "image header");
    uint32_t w = 2u * u16at(&x, (uint32_t)at + 8), h = u16at(&x, (uint32_t)at + 10);
    if (!w || !h || w > 1024 || h > 1024) { snprintf(why, n, "EMD: image %ux%u", w, h); goto bad; }
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

int re1_emd_parse(const uint8_t *data, uint32_t size, PeImpModel *m, char *why, size_t n) {
    memset(m, 0, sizeof *m);
    if (size < 64) { snprintf(why, n, "EMD: %u bytes", size); return -1; }
    Buf b = {data, size};
    uint32_t dir = size - 20, emr = u32at(&b, dir + 4), tmd = u32at(&b, dir + 12), tim = u32at(&b, dir + 16);
    if (!(emr < tmd && tmd < tim && tim < dir)) { snprintf(why, n, "EMD: sections out of order"); return -1; }
    return parse(data, size, emr, tmd, tim, 1, m, why, n);
}

/* "PSXUSA/ENEMY/CHAR10.EMD" -> proposed "player/char10", name "CHAR10" */
static int read_emd(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    PeImpModel *m = calloc(1, sizeof *m);
    if (!m) return -1;
    if (re1_emd_parse(data, size, m, why, n)) { free(m); return -1; }
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

const PeImpReader g_re1_emd_ps1 = {
    {PE_IMP_API_VERSION, sizeof(PeImpReader)}, "re1", "ps1", "emd", 1, PE_IMP_MODEL,
    "*/ENEMY/CHAR1?.EMD", "human player", read_emd};
/* The other people, as the game's NPCs (EM1020-EM102E: the team, Wesker, Kenneth, Forrest,
 * Richard, Enrico) and the Director's Cut's other outfits (EM1030-EM1035). Same layout, same
 * 15-bone person; the ones found lying or sitting keep their rest pose's low root, which the fit
 * doesn't use. */
const PeImpReader g_re1_emd_npc_ps1 = {
    {PE_IMP_API_VERSION, sizeof(PeImpReader)}, "re1", "ps1", "emd-npc", 1, PE_IMP_MODEL,
    "*/ENEMY/EM102?.EMD", "human", read_emd};
const PeImpReader g_re1_emd_alt_ps1 = {
    {PE_IMP_API_VERSION, sizeof(PeImpReader)}, "re1", "ps1", "emd-alt", 1, PE_IMP_MODEL,
    "*/ENEMY/EM103?.EMD", "human player", read_emd};

/* RE2's fit (pe_src_re2_pld.c explains each line). RE1's chest reaches below the waist's top,
 * with no gap between them: no hip lift, no lids to hide. */
#define B(x) {PE_SEG_BONE, x, 0}
#define MID(x, y) {PE_SEG_MID, x, y}
#define MESH(x) {PE_SEG_MESH, x, 0}
const PeImpRig g_re1_human_rig = {
    "re1/human", 15,
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

/* Hand-kept: the reader proposes player/char10 and so on; people know them by name. */
const char g_re1_naming[] =
    "{\"player/char10\": {\"id\": \"player/chris\", \"name\": \"Chris\"},"
    " \"player/char11\": {\"id\": \"player/jill\", \"name\": \"Jill\"},"
    " \"player/char12\": {\"id\": \"player/barry\", \"name\": \"Barry\"},"
    " \"player/char13\": {\"id\": \"player/rebecca\", \"name\": \"Rebecca\"},"
    /* NPCs and outfits, named by BioRand's character IDs and by sight (2026-11-18). Left as
     * proposed: EM1020-23 and EM102A-2D (the team again, as NPCs), EM1029 (Kenneth with empty
     * hands), EM102E (Wesker's second model). */
    " \"player/em1024\": {\"id\": \"player/wesker\", \"name\": \"Wesker\"},"
    " \"player/em1025\": {\"id\": \"player/kenneth\", \"name\": \"Kenneth\"},"
    " \"player/em1026\": {\"id\": \"player/forrest\", \"name\": \"Forrest\"},"
    " \"player/em1027\": {\"id\": \"player/richard\", \"name\": \"Richard\"},"
    " \"player/em1028\": {\"id\": \"player/enrico\", \"name\": \"Enrico\"},"
    " \"player/em1030\": {\"id\": \"player/chris_leather_jacket\", \"name\": \"Leather Jacket Chris\"},"
    " \"player/em1031\": {\"id\": \"player/jill_black_top\", \"name\": \"Black Top Jill\"},"
    " \"player/em1032\": {\"id\": \"player/chris_camo\", \"name\": \"Camo Chris\"},"
    " \"player/em1033\": {\"id\": \"player/jill_denim\", \"name\": \"Denim Jill\"},"
    " \"player/em1035\": {\"id\": \"player/rebecca_red\", \"name\": \"Red Rebecca\"}}";

/* The monsters (and the other non-people: EM10xx and EM11xx but EM102x / EM103x, the people above).
 * The directory, the last 20 bytes: u32 D0..D4, with a section implied at 0. D3 is the TMD, D4 the
 * TIM. The monster's own skeleton and moves are the EMR at D1 with its EDD at D2 (10 to 20 bones).
 * When D0 isn't 0, the EMR at 0 with its EDD at D0 is a second set of 15 bones: the player's moves
 * for this monster (grabbed, bitten); a person's file has D0 = D1 = 0 and only that set. Moves as
 * RE1's room animations (pe_src_re1_anims.c, s16 angles). "enemy/em1000", its "moves" and, when
 * there, its "player-moves". */
static int emit_moves(PeImpSink *sink, const char *id, const char *what, const char *stem, PeImpAnimation *a, char *why,
                      size_t n) {
    char mid[48], name[48];
    snprintf(mid, sizeof mid, "%s/%s", id, what);
    snprintf(name, sizeof name, "%s %s", stem, what);
    if (pe_imp_emit(sink, PE_IMP_ANIMATION, mid, name, a)) { snprintf(why, n, "could not catalog %s", mid); return -1; }
    return 0;
}

static int read_monster(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    const char *s = strrchr(path, '/');
    s = s ? s + 1 : path;
    if (strncmp(s, "EM1", 3) || (s[3] == '0' && (s[4] == '2' || s[4] == '3'))) return 0;   /* the people */
    if (size < 64) return 0;
    Buf b = {data, size};
    uint32_t dir = size - 20, d[5];
    for (int i = 0; i < 5; i++) d[i] = u32at(&b, dir + 4 * (uint32_t)i);
    if (!(d[3] < d[4] && d[4] < dir) || d[1] >= d[2] || d[2] >= d[3] || (d[0] && d[0] >= d[1])) return 0;
    PeImpModel *m = calloc(1, sizeof *m);
    if (!m) { snprintf(why, n, "out of memory"); return -1; }
    char quiet[160];
    if (parse(data, size, d[1], d[3], d[4], 0, m, quiet, sizeof quiet)) { free(m); return 0; }
    PeImpAnimation *own = re1_moves(data, d[3], d[1], d[2]), *player = d[0] ? re1_moves(data, d[1], 0, d[0]) : NULL;
    if (own && own->bones != m->bones) pe_imp_animation_free(own), free(own), own = NULL;
    char stem[16] = "", id[32];
    size_t k = 0;
    while (s[k] && s[k] != '.' && k + 1 < sizeof stem) stem[k] = s[k], k++;
    stem[k] = 0;
    snprintf(id, sizeof id, "enemy/");
    for (size_t j = 0; stem[j]; j++) id[6 + j] = (char)(stem[j] >= 'A' && stem[j] <= 'Z' ? stem[j] + 32 : stem[j]), id[7 + j] = 0;
    int rc = 0;
    if (pe_imp_emit(sink, PE_IMP_MODEL, id, stem, m)) { snprintf(why, n, "could not catalog %s", id); rc = -1; }
    if (own) {
        if (!rc) rc = emit_moves(sink, id, "moves", stem, own, why, n);
        else pe_imp_animation_free(own), free(own);
    }
    if (player) {
        if (!rc) rc = emit_moves(sink, id, "player-moves", stem, player, why, n);
        else pe_imp_animation_free(player), free(player);
    }
    return rc;
}

const PeImpReader g_re1_monsters_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re1", .platform = "ps1", .format = "emd-monster",
    .version = 1, .produces = PE_IMP_MODEL | PE_IMP_ANIMATION, .match = "*/ENEMY/EM1???.EMD",
    .tags = "enemy", .read = read_monster, .path_key = 1
};

/* The people's moves (CHAR1x, EM102x, EM103x): their one set, the EMR at D1 (0) with its EDD at D2,
 * bones put in RE2's order as their models' are (k_from_re1). "player/char10/moves". */
static int read_person_moves(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    const char *s = strrchr(path, '/');
    s = s ? s + 1 : path;
    int person = !strncmp(s, "CHAR1", 5) || (!strncmp(s, "EM10", 4) && (s[4] == '2' || s[4] == '3'));
    if (!person || size < 64) return 0;
    Buf b = {data, size};
    uint32_t dir = size - 20, emr = u32at(&b, dir + 4), edd = u32at(&b, dir + 8), tmd = u32at(&b, dir + 12);
    if (!(emr < edd && edd < tmd && tmd < dir)) return 0;
    PeImpAnimation *a = re1_moves(data, tmd, emr, edd);
    if (!a) return 0;
    if (a->bones != BONES) { pe_imp_animation_free(a), free(a); return 0; }
    for (uint32_t mv = 0; mv < a->moves; mv++)                         /* RE1's bone order -> RE2's */
        for (uint32_t f = 0; f < a->move[mv].frames; f++) {
            int16_t *d = a->move[mv].data + (size_t)f * (3 + 3 * BONES), old[3 * BONES];
            memcpy(old, d + 3, sizeof old);
            for (int r = 0; r < BONES; r++) memcpy(d + 3 + 3 * r, old + 3 * k_from_re1[r], 3 * sizeof *old);
        }
    char stem[16] = "", id[40], name[40];
    size_t k = 0;
    while (s[k] && s[k] != '.' && k + 1 < sizeof stem) stem[k] = s[k], k++;
    stem[k] = 0;
    snprintf(id, sizeof id, "player/");
    for (size_t j = 0; stem[j]; j++) id[7 + j] = (char)(stem[j] >= 'A' && stem[j] <= 'Z' ? stem[j] + 32 : stem[j]), id[8 + j] = 0;
    strncat(id, "/moves", sizeof id - strlen(id) - 1);
    snprintf(name, sizeof name, "%s moves", stem);
    if (pe_imp_emit(sink, PE_IMP_ANIMATION, id, name, a)) { snprintf(why, n, "could not catalog %s", id); return -1; }
    return 0;
}

const PeImpReader g_re1_person_moves_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re1", .platform = "ps1", .format = "emd-moves",
    .version = 1, .produces = PE_IMP_ANIMATION, .match = "*/ENEMY/*.EMD",
    .tags = "human player", .read = read_person_moves, .path_key = 1
};
