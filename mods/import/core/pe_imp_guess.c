/* An enemy profile guessed from a model and its moves (see pe_imp_guess.h). Game-neutral: the
 * model is posed here (the Animation's layout, pe_imp.h), not through a host's enemy format. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_guess.h"
#include "pe_imp_json.h"

/* ---- posing: every face corner of the model, moved by a frame of a move ----
 * Rigid parts' corners are in their bone's frame, put at the bone's rest position; a skinned
 * corner (model.bin v2) is in the rest model frame and follows its bone, and half way a second.
 * Corners shared within a part count once. Rotations X * Y * Z, parent first (pe_imp.h). */
typedef struct { int16_t x, y, z; uint8_t bone, bone2; } Corner;
typedef struct { double m[3][3], t[3]; } Frame;

static int16_t clamp16(double v) {
    long r = lround(v);
    return (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
}

static void corner_add(Corner *c, int *count, int first, int x, int y, int z, int b1, int b2) {
    for (int i = first; i < *count; i++)
        if (c[i].x == x && c[i].y == y && c[i].z == z && c[i].bone == b1 && c[i].bone2 == b2) return;
    c[*count].x = (int16_t)x, c[*count].y = (int16_t)y, c[*count].z = (int16_t)z;
    c[*count].bone = (uint8_t)b1, c[*count].bone2 = (uint8_t)b2;
    (*count)++;
}

/* the model's corners (*out, malloc'd): their number, or -1 with a reason */
static int corners_of(const PeImpModel *m, Corner **out, char *why, size_t n) {
    size_t most = 0;
    for (uint32_t part = 0; part < m->parts && part < m->bones; part++)
        most += 3 * (size_t)m->part[part].tris + 4 * (size_t)m->part[part].quads;
    for (uint32_t i = 0; i < m->bones; i++)
        if (m->parent[i] >= (int)i || (i == 0) != (m->parent[i] < 0)) {
            snprintf(why, n, "bone %u is out of order (a parent must come first, bone 0 the root)", (unsigned)i);
            return -1;
        }
    Corner *c = malloc((most ? most : 1) * sizeof *c);
    int count = 0;
    if (!c) { snprintf(why, n, "out of memory"); return -1; }
    for (uint32_t part = 0; part < m->parts && part < m->bones; part++) {   /* extras past the bones: not drawn */
        const PeImpPart *pt = &m->part[part];
        const int first = count;
        for (uint32_t i = 0; i < pt->tris + pt->quads; i++) {
            const int quad = i >= pt->tris;
            const PeImpFace *f = quad ? &pt->quad[i - pt->tris] : &pt->tri[i];
            const uint8_t (*skin)[2] = quad ? (pt->quad_skin ? (const uint8_t (*)[2])pt->quad_skin[i - pt->tris] : NULL)
                                            : (pt->tri_skin ? (const uint8_t (*)[2])pt->tri_skin[i] : NULL);
            for (int k = 0; k < (quad ? 4 : 3); k++) {
                int x = f->v[k][0], y = f->v[k][1], z = f->v[k][2], b1 = (int)part, b2 = PE_IMP_NO_BONE;
                if (skin) b1 = skin[k][0], b2 = skin[k][1];
                else x += m->pos[part][0], y += m->pos[part][1], z += m->pos[part][2];   /* bone frame -> rest model frame */
                corner_add(c, &count, first, x, y, z, b1, b2);
            }
        }
    }
    if (count < 3) { free(c); snprintf(why, n, "the model has nothing to draw"); return -1; }
    *out = c;
    return count;
}

static void rot_xyz(double ax, double ay, double az, double out[3][3]) {
    const double k = 2.0 * acos(-1.0) / 4096.0;
    double cx = cos(ax * k), sx = sin(ax * k), cy = cos(ay * k), sy = sin(ay * k), cz = cos(az * k), sz = sin(az * k);
    double xy[3][3] = {{cy, 0, sy}, {sx * sy, cx, -sx * cy}, {-cx * sy, sx, cx * cy}};   /* X * Y */
    double z[3][3] = {{cz, -sz, 0}, {sz, cz, 0}, {0, 0, 1}};
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col) out[r][col] = xy[r][0] * z[0][col] + xy[r][1] * z[1][col] + xy[r][2] * z[2][col];
}

/* one frame of move data `d` (pe_imp.h's layout) applied to the corners */
static void pose(const PeImpModel *m, Frame *f, double (*rest)[3], const Corner *c, int count, const int16_t *d,
                 int16_t (*out)[3]) {
    for (uint32_t i = 0; i < m->bones; ++i) {
        const double off[3] = {clamp16(m->rel[i][0]), clamp16(m->rel[i][1]), clamp16(m->rel[i][2])};
        double loc[3][3];
        const int16_t *ang = d + 3 + 3 * i;
        rot_xyz(ang[0], ang[1], ang[2], loc);
        if (m->parent[i] < 0) {
            memcpy(f[i].m, loc, sizeof loc);
            for (int r = 0; r < 3; ++r) f[i].t[r] = d[r], rest[i][r] = off[r];
        } else {
            const Frame *pf = &f[m->parent[i]];
            for (int r = 0; r < 3; ++r) {
                for (int col = 0; col < 3; ++col)
                    f[i].m[r][col] = pf->m[r][0] * loc[0][col] + pf->m[r][1] * loc[1][col] + pf->m[r][2] * loc[2][col];
                f[i].t[r] = pf->t[r] + pf->m[r][0] * off[0] + pf->m[r][1] * off[1] + pf->m[r][2] * off[2];
                rest[i][r] = rest[m->parent[i]][r] + off[r];
            }
        }
    }
    for (int i = 0; i < count; ++i) {
        const int bones[2] = {c[i].bone, c[i].bone2};
        const int nb = c[i].bone2 == PE_IMP_NO_BONE ? 1 : 2;
        double acc[3] = {0, 0, 0};
        for (int k = 0; k < nb; ++k) {
            const Frame *bf = &f[bones[k]];
            const double l[3] = {c[i].x - rest[bones[k]][0], c[i].y - rest[bones[k]][1], c[i].z - rest[bones[k]][2]};
            for (int r = 0; r < 3; ++r) acc[r] += bf->t[r] + bf->m[r][0] * l[0] + bf->m[r][1] * l[1] + bf->m[r][2] * l[2];
        }
        for (int r = 0; r < 3; ++r) {
            const double w = acc[r] / nb;
            out[i][r] = (int16_t)(w > 32767 ? 32767 : w < -32768 ? -32768 : lrint(w));
        }
    }
}

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

int pe_imp_enemy_look(const PeImpModel *m, const PeImpAnimation *a, PeImpMoveLook *look, double *height,
                  char *why, size_t n) {
    Corner *corner = NULL;
    if (a->bones != m->bones) { snprintf(why, n, "the moves are for %u bones, not the model's", (unsigned)a->bones); return -1; }
    const int nv = corners_of(m, &corner, why, n);
    if (nv < 0) return -1;
    Frame *frame = malloc((m->bones ? m->bones : 1) * sizeof *frame);
    double (*rest)[3] = malloc((m->bones ? m->bones : 1) * sizeof *rest);
    int16_t (*first)[3] = malloc((size_t)nv * sizeof *first), (*prev)[3] = malloc((size_t)nv * sizeof *prev),
            (*posed)[3] = malloc((size_t)nv * sizeof *posed);
    double *starts = calloc(a->moves + 1, sizeof *starts);
    int rc = -1, ns = 0;
    if (!first || !prev || !posed || !starts || !frame || !rest) { snprintf(why, n, "out of memory"); goto out; }
    memset(look, 0, a->moves * sizeof *look);
    for (uint32_t k = 0; k < a->moves; k++) {          /* first pass: source units */
        const PeImpMove *mv = &a->move[k];
        PeImpMoveLook *l = &look[k];
        if (!mv->frames) continue;
        const size_t step = 3 + 3 * (size_t)a->bones;
        double e0[4], e[4];
        pose(m, frame, rest, corner, nv, mv->data, first);
        extent(first, nv, e0);
        l->frames = (int)mv->frames;
        l->start = l->low = l->end = top_of(first, nv);
        memcpy(prev, first, (size_t)nv * sizeof *prev);
        for (int f = 1; f < l->frames; f++) {
            pose(m, frame, rest, corner, nv, mv->data + (size_t)f * step, posed);
            const double t = top_of(posed, nv);
            if (t < l->low) l->low = t;
            l->end = t;
            extent(posed, nv, e);
            for (int d = 0; d < 4; d++) if (e[d] - e0[d] > l->reach[d]) l->reach[d] = e[d] - e0[d];
            l->motion += apart(posed, prev, nv);
            memcpy(prev, posed, (size_t)nv * sizeof *prev);
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
        PeImpMoveLook *l = &look[k];
        l->loop /= h, l->motion /= h, l->start /= h, l->low /= h, l->end /= h, l->travel /= h;
        for (int d = 0; d < 4; d++) l->reach[d] /= h;
    }
    if (height) *height = h;
    rc = 0;
out:
    free(first), free(prev), free(posed), free(starts), free(frame), free(rest), free(corner);
    return rc;
}

/* ---- picking moves ----
 * Heights and reaches are in the model's own height (pe_imp_enemy_look), so one rule fits a dog and a
 * giant. Measured on RE2's and RE3's monsters (tests/pe_enemy_guess_test.c):
 *   idle    loops (last pose = first), stands, moves least
 *   die     starts standing, ends at half its height or lower: the first such move
 *   walk    loops, stands, moves more than the idle: the first such move
 *   hurt    short (6-45 frames), starts and ends standing, reaches out little: the one that moves most
 *   attack  starts and ends standing, reaches out furthest (any way: some lunges stretch the
 *           back legs, a swing swings both arms); a move the source marks with hit frames first */
static double reach_of(const PeImpMoveLook *l) {
    double r = 0;
    for (int d = 0; d < 4; d++) r = l->reach[d] > r ? l->reach[d] : r;
    return r;
}

static int taken(const int *pick, int count, int k) {
    for (int i = 0; i < count; i++) if (pick[i] == k) return 1;
    return 0;
}

enum { R_IDLE, R_WALK, R_HURT, R_DIE, R_ATTACK, R_ATTACK2, R_COUNT };

static void pick_moves(const PeImpMoveLook *look, int moves, PeImpEnemyBody body, PeImpEnemyMoveHints hints, int pick[R_COUNT]) {
    const int low_body = body == PE_IMP_ENEMY_CRAWLER || body == PE_IMP_ENEMY_FLYER;
    for (int r = 0; r < R_COUNT; r++) pick[r] = -1;
    if (hints.attack >= 0 && hints.attack < moves && look[hints.attack].frames) pick[R_ATTACK] = hints.attack;
    if (hints.hurt >= 0 && hints.hurt < moves && look[hints.hurt].frames) pick[R_HURT] = hints.hurt;
    if (body != PE_IMP_ENEMY_FIXED && hints.walk >= 0 && hints.walk < moves && look[hints.walk].frames)
        pick[R_WALK] = hints.walk;
    double best = 1e9;
    for (int k = 0; k < moves; k++) {                  /* idle (earlier moves win near ties) */
        const PeImpMoveLook *l = &look[k];
        if (taken(pick, R_COUNT, k) || l->frames < (low_body ? 6 : 10) || l->loop > (low_body ? 0.12 : 0.05) ||
            l->start < (low_body ? 0.35 : 0.6) || l->end < 0.6 * l->start) continue;
        const double key = l->motion + 0.0002 * k;
        if (key < best) best = key, pick[R_IDLE] = k;
    }
    for (int k = 0; pick[R_IDLE] < 0 && k < moves; k++)
        if (look[k].frames && !taken(pick, R_COUNT, k)) pick[R_IDLE] = k;
    for (int k = 0; pick[R_IDLE] < 0 && k < moves; k++)
        if (look[k].frames) pick[R_IDLE] = k;
    if (pick[R_IDLE] < 0) return;
    const PeImpMoveLook *idle = &look[pick[R_IDLE]];
    const double up = idle->start;

    for (int k = 0; k < moves && pick[R_DIE] < 0; k++) {
        const PeImpMoveLook *l = &look[k];
        if (!taken(pick, R_COUNT, k) && l->frames >= 8 && l->start >= 0.8 * up && l->end <= 0.5 * l->start) pick[R_DIE] = k;
    }
    best = 0.8;
    for (int k = 0; pick[R_DIE] < 0 && k < moves; k++) {     /* none falls that far: the one that sinks most */
        const PeImpMoveLook *l = &look[k];
        if (!taken(pick, R_COUNT, k) && l->frames >= 8 && l->start > 0 && l->end / l->start < best)
            best = l->end / l->start, pick[R_DIE] = k;
    }
    best = 0;
    const int guess_walk = pick[R_WALK] < 0 && body != PE_IMP_ENEMY_FIXED;
    for (int k = 0; guess_walk && k < moves; k++) {
        const PeImpMoveLook *l = &look[k];
        if (taken(pick, R_COUNT, k) || (low_body && l->hits) ||
            l->frames < (low_body ? 6 : 16) || l->loop > (low_body ? 0.15 : 0.05) ||
            fabs(l->start - up) > (low_body ? 0.35 : 0.15) || l->low < up - (low_body ? 0.5 : 0.25))
            continue;
        if (!low_body) {
            if (l->motion >= 1.5 * idle->motion && l->motion >= 0.005 && l->motion <= 0.06) {
                pick[R_WALK] = k;
                break;
            }
        } else {
            const double score = l->motion + fmin(l->travel, 1.0) * 0.025;
            if (score > best && l->motion >= 0.004 && l->motion <= 0.35)
                best = score, pick[R_WALK] = k;
        }
    }
    best = 0;
    const int guess_hurt = pick[R_HURT] < 0;
    for (int k = 0; guess_hurt && k < moves; k++) {
        const PeImpMoveLook *l = &look[k];
        const double height_tolerance = low_body ? 0.35 : 0.15;
        if (taken(pick, R_COUNT, k) || l->frames < 6 || l->frames > 45 || l->hits ||
            fabs(l->start - up) > height_tolerance || fabs(l->end - up) > height_tolerance ||
            l->low < (low_body ? 0.35 : 0.6) * up || l->loop > (low_body ? 0.5 : 0.3) ||
            reach_of(l) > (low_body ? 0.4 : 0.2))
            continue;
        if (l->motion > best) best = l->motion, pick[R_HURT] = k;
    }
    for (int r = R_ATTACK; r <= R_ATTACK2; r++) {
        if (pick[r] >= 0) continue;
        best = low_body || body == PE_IMP_ENEMY_FIXED ? 0.08 : 0.12;
        for (int k = 0; k < moves; k++) {
            const PeImpMoveLook *l = &look[k];
            if (taken(pick, R_COUNT, k) || l->frames < (low_body ? 6 : 12) || l->frames > 100 ||
                fabs(l->start - up) > (low_body ? 0.4 : 0.2) || fabs(l->end - up) > (low_body ? 0.45 : 0.2) ||
                l->low < (low_body ? 0.25 : 0.5) * up || l->loop > (low_body ? 0.7 : 0.4))
                continue;
            if (r == R_ATTACK2) {                       /* not the first one again (sources keep copies) */
                const PeImpMoveLook *f = &look[pick[R_ATTACK]];
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

int pe_imp_enemy_guess_body(const PeImpModel *m, const PeImpAnimation *a, const char *name, const char *moves_id,
                           int yaw, int scale, PeImpEnemyBody body, PeImpEnemyMoveHints hints, PeImpEnemyGuess *g,
                           char *why, size_t n) {
    PeImpMoveLook *look = calloc(a->moves ? a->moves : 1, sizeof *look);
    double h = 0;
    int pick[R_COUNT];
    memset(g, 0, sizeof *g);
    if (!look) { snprintf(why, n, "out of memory"); return -1; }
    if (pe_imp_enemy_look(m, a, look, &h, why, n)) { free(look); return -1; }
    pick_moves(look, (int)a->moves, body, hints, pick);
    if (pick[R_IDLE] < 0) { free(look); snprintf(why, n, "every move is empty"); return -1; }
    static const char *const role[R_COUNT] = {"idle", "walk", "hurt", "die", "attack", "attack2"};
    PeImpEnemyProfile *p = &g->p;
    snprintf(g->name, sizeof g->name, "%s", name && *name ? name : "Enemy");
    snprintf(g->moves, sizeof g->moves, "%s", moves_id ? moves_id : "");
    p->name = g->name, p->moves = g->moves;
    const int minimum = body == PE_IMP_ENEMY_CRAWLER ? 45 : body == PE_IMP_ENEMY_FLYER ? 75 : 150;
    const int tall = scale > 0 ? clampi(h * scale / 1000.0, minimum, 1000) : 500;
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
    if (body == PE_IMP_ENEMY_CRAWLER && p->speed > 10) p->speed = 10;
    if (body == PE_IMP_ENEMY_FLYER && p->speed) p->speed = clampi(p->speed + 4, 10, 20);
    const int min_radius = body == PE_IMP_ENEMY_CRAWLER ? 45 : body == PE_IMP_ENEMY_FLYER ? 65 : 180;
    p->charge = 40, p->radius = clampi(tall * 0.45, min_radius, 450), p->stagger = 1;
    free(look);
    return 0;
}

int pe_imp_enemy_guess(const PeImpModel *m, const PeImpAnimation *a, const char *name, const char *moves_id,
                       int yaw, int scale, PeImpEnemyGuess *g, char *why, size_t n) {
    return pe_imp_enemy_guess_body(m, a, name, moves_id, yaw, scale, PE_IMP_ENEMY_UPRIGHT,
                                   (PeImpEnemyMoveHints){-1, -1, -1}, g, why, n);
}
int pe_imp_enemy_known_strength(const char *asset_id, PeImpEnemyProfile *p) {
    int life = 0, power = 0;
    if (pe_imp_enemy_strength(asset_id, &life, &power)) return 0;
    p->hp = clampi(life, 1, 99999), p->exp = clampi(p->hp / 6.0, 5, 999);
    for (int k = 0; power && k < PE_IMP_ENEMY_ATTACKS && p->attack[k].anim; k++) p->attack[k].power = clampi(power, 1, 999);
    return 1;
}

/* ---- the profile as a file ---- */
int pe_imp_enemy_profile_save(const PeImpEnemyProfile *p, const char *note, const char *path, char *why, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) { snprintf(why, n, "can't write %s", path); return -1; }
    fprintf(f, "{\n  \"format\": 1,\n");
    if (note) fprintf(f, "  \"about\": "), pj_write_string(f, note), fprintf(f, ",\n");
    fprintf(f, "  \"name\": "), pj_write_string(f, p->name ? p->name : "Enemy");
    fprintf(f, ",\n  \"moves\": "), pj_write_string(f, p->moves ? p->moves : "");
    if (p->next_form) fprintf(f, ",\n  \"next_form\": "), pj_write_string(f, p->next_form);
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

int pe_imp_enemy_profile_load(const char *path, PeImpEnemyGuess *g, char *why, size_t n) {
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
    if (pj_get(root, "next_form")) {
        const char *next = pj_str(pj_get(root, "next_form"), NULL);
        if (!next || strlen(next) >= sizeof g->next_form || (*next && !strchr(next, ':'))) {
            snprintf(why, n, "next_form must be an asset ID or an empty string");
            goto out;
        }
        snprintf(g->next_form, sizeof g->next_form, "%s", next);
        p->next_form = g->next_form;
    }
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
