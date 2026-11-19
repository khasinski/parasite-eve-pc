/* Resident Evil 2 enemies, PS1: PL0/PLD/CDEMD0.EMS and CDEMD2.EMS hold them, each a TIM then its
 * EMD, both on 2 KB boundaries (the archive's own index holds RAM addresses and isn't read; the
 * pairs are found by their headers: 57 and 15). An EMD (layout as in biohazard-utils' EmdFile, MIT,
 * Ted John): u32 directory offset; there 8 u32 section offsets: 0 unknown, then three move sets
 * (EDD, EMR) at 1-2, 3-4, 5-6, and the MD1 mesh at 7. The first set's EMR is the skeleton. The model
 * is read as a PLD's (pe_src_re2_pld.c) with the TIM before it; the sets' moves as RE2's room
 * animations (pe_src_re2_rbj.c), back to back in one Animation at 30 frames a second. Each becomes
 * "enemy/<archive>-<n>" (n in archive order; which EM number each is isn't known yet) and its moves.
 * Read only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_src_re2.h"

static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int is_tim8(const uint8_t *p, uint32_t avail) { return avail >= 20 && le32(p) == 0x10 && le32(p + 4) == 9; }

/* an EMD at `at`: its 8 section offsets (absolute), else 0 */
static int emd_at(const uint8_t *b, uint32_t size, uint32_t at, uint32_t sec[9]) {
    if (at + 8 > size) return 0;
    uint32_t dir = le32(b + at);
    if (dir < 64 || dir > 0x100000 || at + dir + 32 > size) return 0;
    for (int i = 0; i < 8; i++) {
        uint32_t o = le32(b + at + dir + 4 * (uint32_t)i);
        if (o < 8 || o >= dir || (i && o < sec[i - 1] - at)) return 0;
        sec[i] = at + o;
    }
    sec[8] = at + dir;                                                  /* the mesh runs to the directory */
    return 1;
}

/* the three sets' moves, back to back */
static PeImpAnimation *moves(const uint8_t *b, uint32_t size, const uint32_t sec[9], uint32_t bones) {
    PeImpAnimation *set[3] = {0};
    uint32_t total = 0;
    for (int s = 0; s < 3; s++) {
        uint32_t edd = sec[1 + 2 * s], emr = sec[2 + 2 * s], next = sec[3 + 2 * s];
        if (emr - edd < 4 || next - emr < 8) continue;                  /* an empty set */
        set[s] = re2_moves(b, size, emr, edd, emr, next);
        if (set[s] && set[s]->bones != bones) pe_imp_animation_free(set[s]), free(set[s]), set[s] = NULL;
        if (set[s]) total += set[s]->moves;
    }
    PeImpAnimation *a = total && total <= PE_IMP_ANIM_MOVES_MAX ? calloc(1, sizeof *a) : NULL;
    if (a && !(a->move = calloc(total, sizeof *a->move))) free(a), a = NULL;
    if (a) a->bones = bones, a->fps = 30;
    for (int s = 0; s < 3; s++) {
        if (!set[s]) continue;
        for (uint32_t m = 0; a && m < set[s]->moves; m++) a->move[a->moves++] = set[s]->move[m];
        if (a) free(set[s]->move), set[s]->move = NULL, set[s]->moves = 0;     /* moved into a */
        pe_imp_animation_free(set[s]);
        free(set[s]);
    }
    return a;
}

static int read_ems(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char stem[16] = "";
    size_t k = 0;
    for (const char *c = base; *c && *c != '.' && k + 1 < sizeof stem; c++) stem[k++] = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
    stem[k] = 0;
    uint32_t tim = 0, made = 0;
    int have_tim = 0;
    for (uint32_t at = 0; at + 64 <= size; at += 2048) {
        uint32_t sec[9];
        if (is_tim8(data + at, size - at)) { tim = at, have_tim = 1; continue; }
        if (!have_tim || !emd_at(data, size, at, sec)) continue;
        have_tim = 0;                                                   /* each TIM goes with the EMD after it */
        PeImpModel *m = calloc(1, sizeof *m);
        if (!m) { snprintf(why, n, "out of memory"); return -1; }
        char quiet[160], id[40], mid[48], name[48];
        if (re2_skeleton_mesh(data, size, sec[2], sec[7], tim, m, quiet, sizeof quiet)) { free(m); continue; }
        PeImpAnimation *a = moves(data, size, sec, m->bones);
        snprintf(id, sizeof id, "enemy/%s-%u", stem, (unsigned)made);
        snprintf(name, sizeof name, "%s enemy %u", stem, (unsigned)made);
        made++;
        if (pe_imp_emit(sink, PE_IMP_MODEL, id, name, m)) {
            if (a) pe_imp_animation_free(a), free(a);
            snprintf(why, n, "could not catalog %s", id);
            return -1;
        }
        if (!a) continue;
        snprintf(mid, sizeof mid, "%s/moves", id);
        strncat(name, " moves", sizeof name - strlen(name) - 1);
        if (pe_imp_emit(sink, PE_IMP_ANIMATION, mid, name, a)) { snprintf(why, n, "could not catalog %s", mid); return -1; }
    }
    return 0;
}

const PeImpReader g_re2_emd_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "ems",
    .version = 1, .produces = PE_IMP_MODEL | PE_IMP_ANIMATION, .match = "PL?/PLD/CDEMD?.EMS",
    .tags = "enemy", .read = read_ems
};


/* BioRand (biohazard-utils' emdui, MIT, Ted John): a weapon file's first mesh part replaces the
 * player model's part 11 (the right hand, here in RE2's part order), and its 56 x 32 picture goes into
 * page 1 of the player's texture at (72, 224), each colour the nearest in palette row 1 (TimExtensions
 * .WithWeaponTexture). */
static uint32_t w_le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned w_le16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }

static int weapon_texture(PeImpModel *m, const uint8_t *t, uint32_t size) {
    if (size < 20 || w_le32(t) != 0x10 || (w_le32(t + 4) & 3) != 1 || !(w_le32(t + 4) & 8) || w_le16(t + 16) != 256) return -1;
    uint32_t ln = w_le32(t + 8);
    if (ln < 12 + 512 || 8ull + ln + 12 > size) return -1;
    const uint8_t *img = t + 8 + ln;
    uint32_t w = 2u * w_le16(img + 8), h = w_le16(img + 10);
    if (w != 56 || h != 32 || 8ull + ln + 12 + (uint64_t)w * h > size) return -1;
    if (m->w < 256 || m->h < 256 || m->pal_rows < 2) return -1;
    int16_t best[256];
    for (int i = 0; i < 256; i++) best[i] = -1;
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            unsigned idx = img[12 + y * w + x];
            if (best[idx] < 0) {
                unsigned c = w_le16(t + 20 + 2 * idx);
                long bd = -1;
                for (int k = 0; k < 256; k++) {
                    unsigned p = m->pal[1][k];
                    if ((p == 0) != (c == 0)) continue;                     /* see-through stays see-through */
                    long dr = (long)(c & 31) - (long)(p & 31), dg = (long)(c >> 5 & 31) - (long)(p >> 5 & 31),
                         db = (long)(c >> 10 & 31) - (long)(p >> 10 & 31), dist = dr * dr + dg * dg + db * db;
                    if (bd < 0 || dist < bd) bd = dist, best[idx] = (int16_t)k;
                }
                if (best[idx] < 0) best[idx] = 0;
            }
            m->image[(size_t)(224 + y) * m->w + 128 + 72 + x] = (uint8_t)best[idx];
        }
    return 0;
}

/* m's part `to` takes w's part 0 (its faces move over) */
static void swap_hand(PeImpModel *m, uint32_t to, PeImpModel *w) {
    free(m->part[to].tri), free(m->part[to].quad);
    m->part[to].tri = w->part[0].tri, m->part[to].tris = w->part[0].tris;
    m->part[to].quad = w->part[0].quad, m->part[to].quads = w->part[0].quads;
    w->part[0].tri = w->part[0].quad = NULL, w->part[0].tris = w->part[0].quads = 0;
}

/* "<dir>/PLxxWyy.PLW" -> "<dir>/PLxx.PLD" */
static int find_pld(PeImpDisc *d, const char *path) {
    char p[PE_IMP_PATH_MAX];
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *w = base + 4;                                           /* "PLxx" then 'W' */
    if (strncmp(base, "PL", 2) || strlen(base) < 5 || *w != 'W' || (size_t)(w - path) + 5 > sizeof p) return -1;
    snprintf(p, sizeof p, "%.*s.PLD", (int)(w - path), path);
    return pe_imp_disc_find(d, p);
}

/* Players and weapons. PL?/PLD/PLxx.PLD and PLxxWyy.PLW share a layout: u32 directory offset; there
 * EDD, EMR, MD1, TIM offsets. A PLD's EDD/EMR are the player's moves ("player/pl00/moves", bones as
 * the model's). A PLW's are the player's moves with that weapon (aim, fire, reload), and the weapon
 * Model "weapon/pl00w02" is the player PLxx.PLD holding it: part 11 swapped for the PLW's first part,
 * the PLW's picture in the texture (above). Its moves play on it as they are. */
static int emit_weapon(const uint8_t *data, uint32_t size, const uint8_t *pld, uint32_t pld_size, const char *path,
                       PeImpSink *sink, char *why, size_t n) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char stem[16] = "", low[16] = "", id[48], name[48];
    size_t k = 0;
    for (const char *c = base; *c && *c != '.' && k + 1 < sizeof stem; c++, k++) stem[k] = *c, low[k] = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
    stem[k] = low[k] = 0;
    int weapon = strchr(base, '.') && !strcmp(strchr(base, '.'), ".PLW");
    if (size < 8) return 0;
    uint32_t dir = le32(data);
    if (dir < 16 || dir + 16 > size) return 0;
    uint32_t edd = le32(data + dir), emr = le32(data + dir + 4), md1 = le32(data + dir + 8), tim = le32(data + dir + 12);
    if (!(edd < emr && emr < md1 && md1 < tim && tim < dir)) return 0;
    PeImpAnimation *a = re2_moves(data, size, emr, edd, emr, md1);
    if (weapon && pld) {
        PeImpModel *m = calloc(1, sizeof *m), *w = calloc(1, sizeof *w);
        char quiet[160];
        int ok = m && w && !re2_pld_parse(pld, pld_size, m, quiet, sizeof quiet);
        int wok = ok && !re2_object_parse(data, size, md1, tim, w, quiet, sizeof quiet);
        if (wok && m->parts > 11 && w->parts >= 1 && !weapon_texture(m, data + tim, dir - tim)) {
            swap_hand(m, 11, w);
            pe_imp_model_free(w), free(w), w = NULL;
            snprintf(id, sizeof id, "weapon/%s", low);
            snprintf(name, sizeof name, "%s", stem);
            if (pe_imp_emit(sink, PE_IMP_MODEL, id, name, m)) {
                if (a) pe_imp_animation_free(a), free(a);
                snprintf(why, n, "could not catalog %s", id);
                return -1;
            }
            m = NULL;
        }
        if (m) { if (ok) pe_imp_model_free(m); free(m); }
        if (w) { if (wok) pe_imp_model_free(w); free(w); }
    }
    if (!a) return 0;
    snprintf(id, sizeof id, "%s/%s/moves", weapon ? "weapon" : "player", low);
    snprintf(name, sizeof name, "%s moves", stem);
    if (pe_imp_emit(sink, PE_IMP_ANIMATION, id, name, a)) { snprintf(why, n, "could not catalog %s", id); return -1; }
    return 0;
}

static int read_player(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    return emit_weapon(data, size, NULL, 0, path, sink, why, n);
}

static int read_weapon(const uint8_t *data, uint32_t size, const uint8_t *pld, uint32_t pld_size, const char *path,
                       PeImpSink *sink, char *why, size_t n) {
    return emit_weapon(data, size, pld, pld_size, path, sink, why, n);
}

const PeImpReader g_re2_player_moves_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "pld-moves",
    .version = 1, .produces = PE_IMP_ANIMATION, .match = "PL?/PLD/PL??.PLD",
    .tags = "human player", .read = read_player, .path_key = 1
};

const PeImpReader g_re2_weapons_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "plw",
    .version = 2, .produces = PE_IMP_MODEL | PE_IMP_ANIMATION, .match = "PL?/PLD/PL??W??.PLW",
    .tags = "weapon", .read_pair = read_weapon, .find_companion = find_pld
};
