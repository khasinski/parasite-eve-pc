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

/* Players and weapons. PL?/PLD/PLxx.PLD and PLxxWyy.PLW share a layout: u32 directory offset; there
 * EDD, EMR, MD1, TIM offsets. A PLD's EDD/EMR are the player's moves ("player/pl00/moves", bones as
 * the model's); a PLW's are the player's moves with that weapon (aim, fire, reload) and its MD1 + TIM
 * the weapon in hand, a one-bone Model of its parts as stored, each in the frame of the arm bone it
 * replaces (which bones isn't mapped yet): "weapon/pl00w02" and "weapon/pl00w02/moves". */
static int read_player(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
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
    if (weapon) {
        PeImpModel *m = calloc(1, sizeof *m);
        char quiet[160];
        if (m && !re2_object_parse(data, size, md1, tim, m, quiet, sizeof quiet)) {
            snprintf(id, sizeof id, "weapon/%s", low);
            snprintf(name, sizeof name, "%s", stem);
            if (pe_imp_emit(sink, PE_IMP_MODEL, id, name, m)) {
                if (a) pe_imp_animation_free(a), free(a);
                snprintf(why, n, "could not catalog %s", id);
                return -1;
            }
        } else free(m);
    }
    if (!a) return 0;
    snprintf(id, sizeof id, "%s/%s/moves", weapon ? "weapon" : "player", low);
    snprintf(name, sizeof name, "%s moves", stem);
    if (pe_imp_emit(sink, PE_IMP_ANIMATION, id, name, a)) { snprintf(why, n, "could not catalog %s", id); return -1; }
    return 0;
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
    .version = 1, .produces = PE_IMP_MODEL | PE_IMP_ANIMATION, .match = "PL?/PLD/PL??W??.PLW",
    .tags = "weapon", .read = read_player
};
