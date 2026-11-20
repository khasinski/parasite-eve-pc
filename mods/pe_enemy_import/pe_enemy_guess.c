/* An enemy profile guessed from a model and its moves (see pe_enemy_guess.h). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_enemy_build.h"
#include "pe_enemy_guess.h"
#include "pe_enemy_pack.h"
#include "pe_imp_json.h"

static double top_of(const int16_t (*pose)[3], int n) {
    int top = 0;
    for (int i = 0; i < n; i++) top = pose[i][1] < top ? pose[i][1] : top;
    return -top;
}

static void extent(const int16_t (*pose)[3], int n, double e[4]) {
    e[0] = e[1] = e[2] = e[3] = -1e9;
    for (int i = 0; i < n; i++) {
        if (pose[i][0] > e[0]) e[0] = pose[i][0];
        if (-pose[i][0] > e[1]) e[1] = -pose[i][0];
        if (pose[i][2] > e[2]) e[2] = pose[i][2];
        if (-pose[i][2] > e[3]) e[3] = -pose[i][2];
    }
}

static double apart(const int16_t (*a)[3], const int16_t (*b)[3], int n) {
    double s = 0;
    for (int i = 0; i < n; i++) {
        const double dx = a[i][0] - b[i][0], dy = a[i][1] - b[i][1], dz = a[i][2] - b[i][2];
        s += sqrt(dx * dx + dy * dy + dz * dz);
    }
    return n ? s / n : 0;
}

int pe_enemy_look(const PeImpModel *m, const PeImpAnimation *a, PeEnemyMoveLook *look, double *height,
                  char *why, size_t n) {
    PeEnemyPack p;
    if (a->bones != m->bones) { snprintf(why, n, "the moves are for %u bones, not the model's", (unsigned)a->bones); return -1; }
    if (pe_enemy_build_model(&p, m, NULL, why, n)) return -1;
    const int nv = p.vertices;
    int16_t (*first)[3] = malloc((size_t)nv * sizeof *first), (*prev)[3] = malloc((size_t)nv * sizeof *prev),
            (*pose)[3] = malloc((size_t)nv * sizeof *pose);
    double *starts = calloc(a->moves + 1, sizeof *starts);
    int rc = -1, ns = 0;
    if (!first || !prev || !pose || !starts) { snprintf(why, n, "out of memory"); goto out; }
    memset(look, 0, a->moves * sizeof *look);
    p.anims = 1;
    for (uint32_t k = 0; k < a->moves; k++) {          /* first pass: source units */
        const PeImpMove *mv = &a->move[k];
        PeEnemyMoveLook *l = &look[k];
        if (!mv->frames) continue;
        p.anim[0].frames = (int)mv->frames, p.anim[0].data = mv->data, p.anim[0].event = NULL;
        double e0[4], e[4];
        pe_enemy_pose(&p, 0, 0, first);
        extent(first, nv, e0);
        l->frames = (int)mv->frames;
        l->start = l->low = l->end = top_of(first, nv);
        memcpy(prev, first, (size_t)nv * sizeof *prev);
        for (int f = 1; f < l->frames; f++) {
            pe_enemy_pose(&p, 0, f, pose);
            const double t = top_of(pose, nv);
            if (t < l->low) l->low = t;
            l->end = t;
            extent(pose, nv, e);
            for (int d = 0; d < 4; d++) if (e[d] - e0[d] > l->reach[d]) l->reach[d] = e[d] - e0[d];
            l->motion += apart(pose, prev, nv);
            memcpy(prev, pose, (size_t)nv * sizeof *prev);
        }
        l->motion /= l->frames > 1 ? l->frames - 1 : 1;
        l->loop = apart(prev, first, nv);
        const size_t words = 3 + 3 * (size_t)a->bones, last = (size_t)(l->frames - 1) * words;
        const double dx = mv->data[last] - mv->data[0], dz = mv->data[last + 2] - mv->data[2];
        l->travel = sqrt(dx * dx + dz * dz);
        for (int f = 0; f < l->frames; f++) {
            if (mv->event[f] & PE_IMP_EVENT_HIT) l->hits++;
            if (mv->event[f] & PE_IMP_EVENT_SOUND) l->sounds++;
        }
        starts[ns++] = l->start;
    }
    if (!ns) { snprintf(why, n, "every move is empty"); goto out; }
    /* how tall it stands: the most common start height (the median: a few moves start lying down) */
    for (int i = 1; i < ns; i++)
        for (int j = i; j > 0 && starts[j - 1] > starts[j]; j--) { double t = starts[j]; starts[j] = starts[j - 1], starts[j - 1] = t; }
    const double h = starts[ns / 2] > 1 ? starts[ns / 2] : 1;
    for (uint32_t k = 0; k < a->moves; k++) {
        PeEnemyMoveLook *l = &look[k];
        l->loop /= h, l->motion /= h, l->start /= h, l->low /= h, l->end /= h, l->travel /= h;
        for (int d = 0; d < 4; d++) l->reach[d] /= h;
    }
    if (height) *height = h;
    rc = 0;
out:
    p.anims = 0;                                        /* the moves' data is borrowed */
    free(first), free(prev), free(pose), free(starts);
    pe_enemy_pack_free(&p);
    return rc;
}

/* ---- picking moves ----
 * Heights and reaches are in the model's own height (pe_enemy_look), so one rule fits a dog and a
 * giant. Measured on RE2's and RE3's monsters (tests/pe_enemy_guess_test.c):
 *   idle    loops (last pose = first), stands, moves least
 *   die     starts standing, ends at half its height or lower: the first such move
 *   walk    loops, stands, moves more than the idle: the first such move
 *   hurt    short (6-45 frames), starts and ends standing, reaches out little: the one that moves most
 *   attack  starts and ends standing, reaches out furthest (any way: some lunges stretch the
 *           back legs, a swing swings both arms); a move the source marks with hit frames first */
static double reach_of(const PeEnemyMoveLook *l) {
    double r = 0;
    for (int d = 0; d < 4; d++) r = l->reach[d] > r ? l->reach[d] : r;
    return r;
}

static int taken(const int *pick, int count, int k) {
    for (int i = 0; i < count; i++) if (pick[i] == k) return 1;
    return 0;
}

enum { R_IDLE, R_WALK, R_HURT, R_DIE, R_ATTACK, R_ATTACK2, R_COUNT };

static void pick_moves(const PeEnemyMoveLook *look, int moves, int pick[R_COUNT]) {
    for (int r = 0; r < R_COUNT; r++) pick[r] = -1;
    double best = 1e9;
    for (int k = 0; k < moves; k++) {                  /* idle (earlier moves win near ties) */
        const PeEnemyMoveLook *l = &look[k];
        if (l->frames < 10 || l->loop > 0.05 || l->start < 0.6 || l->end < 0.6 * l->start) continue;
        const double key = l->motion + 0.0002 * k;
        if (key < best) best = key, pick[R_IDLE] = k;
    }
    for (int k = 0; pick[R_IDLE] < 0 && k < moves; k++) if (look[k].frames) pick[R_IDLE] = k;
    if (pick[R_IDLE] < 0) return;
    const PeEnemyMoveLook *idle = &look[pick[R_IDLE]];
    const double up = idle->start;

    for (int k = 0; k < moves && pick[R_DIE] < 0; k++) {
        const PeEnemyMoveLook *l = &look[k];
        if (k != pick[R_IDLE] && l->frames >= 8 && l->start >= 0.8 * up && l->end <= 0.5 * l->start) pick[R_DIE] = k;
    }
    best = 0.8;
    for (int k = 0; pick[R_DIE] < 0 && k < moves; k++) {     /* none falls that far: the one that sinks most */
        const PeEnemyMoveLook *l = &look[k];
        if (k != pick[R_IDLE] && l->frames >= 8 && l->start > 0 && l->end / l->start < best) best = l->end / l->start, pick[R_DIE] = k;
    }
    for (int k = 0; k < moves && pick[R_WALK] < 0; k++) {
        const PeEnemyMoveLook *l = &look[k];
        if (taken(pick, R_COUNT, k) || l->frames < 16 || l->loop > 0.05 || fabs(l->start - up) > 0.15 || l->low < up - 0.25)
            continue;
        if (l->motion >= 1.5 * idle->motion && l->motion >= 0.005 && l->motion <= 0.06) pick[R_WALK] = k;
    }
    best = 0;
    for (int k = 0; k < moves; k++) {
        const PeEnemyMoveLook *l = &look[k];
        if (taken(pick, R_COUNT, k) || l->frames < 6 || l->frames > 45 || l->hits || fabs(l->start - up) > 0.15 ||
            fabs(l->end - up) > 0.15 || l->low < 0.6 * up || l->loop > 0.3 || reach_of(l) > 0.2)
            continue;
        if (l->motion > best) best = l->motion, pick[R_HURT] = k;
    }
    for (int r = R_ATTACK; r <= R_ATTACK2; r++) {
        best = 0.12;
        for (int k = 0; k < moves; k++) {
            const PeEnemyMoveLook *l = &look[k];
            if (taken(pick, R_COUNT, k) || l->frames < 12 || l->frames > 100 || fabs(l->start - up) > 0.2 ||
                fabs(l->end - up) > 0.2 || l->low < 0.5 * up || l->loop > 0.4)
                continue;
            if (r == R_ATTACK2) {                       /* not the first one again (sources keep copies) */
                const PeEnemyMoveLook *f = &look[pick[R_ATTACK]];
                int same = f->frames == l->frames;
                for (int d = 0; d < 4; d++) same &= fabs(f->reach[d] - l->reach[d]) < 0.005;
                if (same) continue;
            }
            const double score = reach_of(l) + (l->hits ? 0.3 : 0);
            if (score > best) best = score, pick[r] = k;
        }
        if (pick[r] < 0) break;
    }
}

static int clampi(double v, int lo, int hi) {
    long r = lround(v);
    return (int)(r < lo ? lo : r > hi ? hi : r);
}

int pe_enemy_guess(const PeImpModel *m, const PeImpAnimation *a, const char *name, const char *moves_id,
                   int yaw, int scale, PeEnemyGuess *g, char *why, size_t n) {
    PeEnemyMoveLook *look = calloc(a->moves ? a->moves : 1, sizeof *look);
    double h = 0;
    int pick[R_COUNT];
    memset(g, 0, sizeof *g);
    if (!look) { snprintf(why, n, "out of memory"); return -1; }
    if (pe_enemy_look(m, a, look, &h, why, n)) { free(look); return -1; }
    pick_moves(look, (int)a->moves, pick);
    if (pick[R_IDLE] < 0) { free(look); snprintf(why, n, "every move is empty"); return -1; }
    static const char *const role[R_COUNT] = {"idle", "walk", "hurt", "die", "attack", "attack2"};
    PeImpEnemyProfile *p = &g->p;
    snprintf(g->name, sizeof g->name, "%s", name && *name ? name : "Enemy");
    snprintf(g->moves, sizeof g->moves, "%s", moves_id ? moves_id : "");
    p->name = g->name, p->moves = g->moves;
    const int tall = scale > 0 ? clampi(h * scale / 1000.0, 150, 1000) : 500;   /* PE field units */
    p->height = tall, p->yaw = yaw & 4095;
    p->hp = clampi(tall * tall / 3500.0, 30, 999), p->exp = clampi(p->hp / 6.0, 5, 999), p->defense = clampi(tall / 80.0, 1, 30);
    int na = 0, nk = 0;
    for (int r = 0; r < R_COUNT; r++) {
        if (pick[r] < 0) continue;
        snprintf(g->anim[na], sizeof g->anim[na], "%s", role[r]);
        p->anim[na].name = g->anim[na], p->anim[na].move = pick[r];
        if (r >= R_ATTACK) {
            p->attack[nk].anim = g->anim[na];
            p->attack[nk].power = clampi(10 + tall / 40.0 + 4 * nk, 5, 99);
            nk++;
        }
        na++;
    }
    p->speed = pick[R_WALK] >= 0 ? clampi(4 + look[pick[R_WALK]].motion * 600, 3, 16) : 0;
    p->charge = 40, p->radius = clampi(tall * 0.45, 180, 450), p->stagger = 1;
    free(look);
    return 0;
}

/* ---- the profile as a file ---- */
int pe_enemy_profile_save(const PeImpEnemyProfile *p, const char *note, const char *path, char *why, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) { snprintf(why, n, "can't write %s", path); return -1; }
    fprintf(f, "{\n  \"format\": 1,\n");
    if (note) fprintf(f, "  \"about\": "), pj_write_string(f, note), fprintf(f, ",\n");
    fprintf(f, "  \"name\": "), pj_write_string(f, p->name ? p->name : "Enemy");
    fprintf(f, ",\n  \"moves\": "), pj_write_string(f, p->moves ? p->moves : "");
    if (p->sounds) fprintf(f, ",\n  \"sounds\": "), pj_write_string(f, p->sounds), fprintf(f, ", \"sound_count\": %d", p->sound_count);
    fprintf(f, ",\n  \"hp\": %d, \"exp\": %d, \"defense\": %d, \"height\": %d, \"yaw\": %d,\n  \"anims\": {", p->hp, p->exp,
            p->defense, p->height, p->yaw);
    for (int i = 0; i < PE_IMP_ENEMY_ANIMS && p->anim[i].name; i++)
        fprintf(f, "%s", i ? ", " : ""), pj_write_string(f, p->anim[i].name), fprintf(f, ": %d", p->anim[i].move);
    fprintf(f, "},\n  \"attacks\": [");
    for (int i = 0; i < PE_IMP_ENEMY_ATTACKS && p->attack[i].anim; i++) {
        fprintf(f, "%s{\"anim\": ", i ? ", " : ""), pj_write_string(f, p->attack[i].anim);
        fprintf(f, ", \"power\": %d, \"range\": %d, \"hit\": %d}", p->attack[i].power, p->attack[i].range, p->attack[i].hit);
    }
    fprintf(f, "],\n  \"behavior\": {\"speed\": %d, \"charge\": %d, \"radius\": %d, \"stagger\": %d}\n}\n", p->speed,
            p->charge, p->radius, p->stagger);
    if (fclose(f)) { snprintf(why, n, "can't write %s", path); return -1; }
    return 0;
}

int pe_enemy_profile_load(const char *path, PeEnemyGuess *g, char *why, size_t n) {
    PjNode *root = pj_load(path, why, n);
    int rc = -1;
    memset(g, 0, sizeof *g);
    if (!root) return -1;
    PeImpEnemyProfile *p = &g->p;
    const PjNode *anims = pj_get(root, "anims"), *attacks = pj_get(root, "attacks"), *b = pj_get(root, "behavior");
    if (root->kind != PJ_OBJECT || pj_int(pj_get(root, "format"), 0) != 1) { snprintf(why, n, "not a profile (format 1)"); goto out; }
    snprintf(g->name, sizeof g->name, "%s", pj_str(pj_get(root, "name"), "Enemy"));
    snprintf(g->moves, sizeof g->moves, "%s", pj_str(pj_get(root, "moves"), ""));
    p->name = g->name, p->moves = g->moves;
    if (pj_get(root, "sounds")) {
        snprintf(g->sounds, sizeof g->sounds, "%s", pj_str(pj_get(root, "sounds"), ""));
        p->sounds = g->sounds, p->sound_count = (int)pj_int(pj_get(root, "sound_count"), 0);
    }
    p->hp = (int)pj_int(pj_get(root, "hp"), 100), p->exp = (int)pj_int(pj_get(root, "exp"), 10);
    p->defense = (int)pj_int(pj_get(root, "defense"), 5), p->height = (int)pj_int(pj_get(root, "height"), 0);
    p->yaw = (int)pj_int(pj_get(root, "yaw"), 0);
    int na = 0, idle = 0;
    for (const PjNode *c = anims && anims->kind == PJ_OBJECT ? anims->child : NULL; c; c = c->next) {
        if (na == PE_IMP_ENEMY_ANIMS) { snprintf(why, n, "more than %d anims", PE_IMP_ENEMY_ANIMS); goto out; }
        if (c->kind != PJ_INT || !c->key || !*c->key || strlen(c->key) >= sizeof g->anim[0]) { snprintf(why, n, "anims: each is a name and a move number"); goto out; }
        snprintf(g->anim[na], sizeof g->anim[na], "%s", c->key);
        p->anim[na].name = g->anim[na], p->anim[na].move = (int)c->i;
        idle |= !strcmp(c->key, "idle");
        na++;
    }
    if (!idle) { snprintf(why, n, "anims has no \"idle\""); goto out; }
    for (int i = 0; attacks && i < attacks->count; i++) {
        const PjNode *at = pj_at(attacks, i);
        const char *an = pj_str(pj_get(at, "anim"), "");
        int k = 0;
        while (k < na && strcmp(g->anim[k], an)) k++;
        if (k == na) { snprintf(why, n, "an attack names \"%.24s\", not in anims", an); goto out; }
        if (i == PE_IMP_ENEMY_ATTACKS) { snprintf(why, n, "more than %d attacks", PE_IMP_ENEMY_ATTACKS); goto out; }
        p->attack[i].anim = g->anim[k];
        p->attack[i].power = (int)pj_int(pj_get(at, "power"), 20), p->attack[i].range = (int)pj_int(pj_get(at, "range"), 0);
        p->attack[i].hit = (int)pj_int(pj_get(at, "hit"), 0);
    }
    p->speed = (int)pj_int(pj_get(b, "speed"), 6), p->charge = (int)pj_int(pj_get(b, "charge"), 40);
    p->radius = (int)pj_int(pj_get(b, "radius"), 250), p->stagger = (int)pj_int(pj_get(b, "stagger"), 1);
    rc = 0;
out:
    pj_free(root);
    if (rc) memset(g, 0, sizeof *g);
    return rc;
}
