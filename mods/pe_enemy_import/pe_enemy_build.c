/* The enemies module's builder (F1 > Import, "Build as an enemy"): a catalogued model with an
 * enemy profile, its moves and its sounds become an enemy pack (pe_enemy_pack.h), on the player's
 * PC from their own files. The C port of the enemy importer's tools/build_enemy_pack.py, with the
 * moves' own events: a blow lands on the attack move's first hit frame, sounds play on their frames.
 * No game state: unit-tested on made-up data (tests/pe_enemy_build_test.c). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_enemy_build.h"
#include "pe_enemy_pack.h"
#include "pe_imp.h"
#include "pe_imp_enemy.h"

static int fail(char *why, size_t n, const char *fmt, const char *a, long b) {
    if (why && n) snprintf(why, n, fmt, a, b);
    return -1;
}

static int16_t clamp16(double v) {
    long r = lround(v);
    return (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
}

/* ---- the model: corners shared within a part become one vertex ---- */
static int add_vertex(PeEnemyPack *p, int x, int y, int z, int b1, int b2, int first) {
    for (int i = first; i < p->vertices; i++) {
        const PeEnemyVertex *v = &p->vertex[i];
        if (v->x == x && v->y == y && v->z == z && v->bone == b1 && v->bone2 == b2) return i;
    }
    if (p->vertices == PE_ENEMY_MAX_VERTICES) return -1;
    PeEnemyVertex *v = &p->vertex[p->vertices];
    v->x = (int16_t)x, v->y = (int16_t)y, v->z = (int16_t)z, v->bone = (uint8_t)b1, v->bone2 = (uint8_t)b2;
    return p->vertices++;
}

/* A texel's x in the model's picture: its page counted from the first page the faces use (a PS1
 * texture page word: 8-bit pages are 128 texels wide), plus u. Its palette: the CLUT word's row
 * counted from the first row the faces use (the picture's palettes are rows one under another). */
static int add_face(PeEnemyPack *p, const PeImpModel *m, int part, const PeImpFace *f, const uint8_t (*skin)[2],
                    int corners, int first, int page0, int row0, int *skipped) {
    static const int split[2][3] = {{0, 1, 2}, {1, 3, 2}};
    int idx[4];
    for (int k = 0; k < corners; k++) {
        int x = f->v[k][0], y = f->v[k][1], z = f->v[k][2], b1 = part, b2 = PE_ENEMY_NO_BONE;
        if (skin) b1 = skin[k][0], b2 = skin[k][1] == PE_IMP_NO_BONE ? PE_ENEMY_NO_BONE : skin[k][1];
        else x += m->pos[part][0], y += m->pos[part][1], z += m->pos[part][2];   /* bone frame -> rest model frame */
        if ((idx[k] = add_vertex(p, x, y, z, b1, b2, first)) < 0) return -1;
    }
    for (int t = 0; t < (corners == 4 ? 2 : 1); t++) {
        int u[3], lo = 1 << 30, hi = 0;
        for (int k = 0; k < 3; k++) {
            u[k] = ((f->page & 15) - page0) * 128 + f->uv[split[t][k]][0];
            lo = u[k] < lo ? u[k] : lo, hi = u[k] > hi ? u[k] : hi;
        }
        if (lo < 0 || hi - lo / 128 * 128 > 255 || hi >= p->tex_w) { ++*skipped; continue; }   /* drawn from 8-bit pages 128 apart */
        if (p->triangles == PE_ENEMY_MAX_TRIANGLES) return -1;
        PeEnemyTriangle *tr = &p->triangle[p->triangles++];
        for (int k = 0; k < 3; k++) {
            tr->v[k] = (uint16_t)idx[split[t][k]];
            tr->u[k] = (uint16_t)u[k];
            tr->t[k] = f->uv[split[t][k]][1];
        }
        const int row = (f->clut >> 6) - row0;
        tr->palette = (uint8_t)(row >= 0 && row < p->palettes ? row : 0);
    }
    return 0;
}

static int model_in(PeEnemyPack *p, const PeImpModel *m, int *skipped, char *why, size_t n) {
    if (m->bones > PE_ENEMY_MAX_BONES) return fail(why, n, "%s%ld bones: an enemy has at most 32", "", (long)m->bones);
    if (m->w > 512 || m->h > 256 || m->pal_rows > 8) return fail(why, n, "%stexture too large for an enemy (512 x 256, 8 palettes)", "", 0);
    p->bones = (int)m->bones;
    for (uint32_t i = 0; i < m->bones; i++) {
        if (m->parent[i] >= (int)i || (i == 0) != (m->parent[i] < 0))
            return fail(why, n, "%sbone %ld is out of order (a parent must come first, bone 0 the root)", "", (long)i);
        p->bone[i].parent = m->parent[i];
        p->bone[i].x = clamp16(m->rel[i][0]), p->bone[i].y = clamp16(m->rel[i][1]), p->bone[i].z = clamp16(m->rel[i][2]);
    }
    p->tex_w = (int)m->w, p->tex_h = (int)m->h, p->palettes = (int)m->pal_rows;
    p->vertex = calloc(PE_ENEMY_MAX_VERTICES, sizeof *p->vertex);
    p->triangle = calloc(PE_ENEMY_MAX_TRIANGLES, sizeof *p->triangle);
    p->palette = calloc(256u * m->pal_rows, sizeof *p->palette);
    p->pixels = malloc((size_t)m->w * m->h);
    if (!p->vertex || !p->triangle || !p->palette || !p->pixels) return fail(why, n, "%sout of memory", "", 0);
    for (uint32_t r = 0; r < m->pal_rows; r++) memcpy(p->palette + 256 * r, m->pal[r], 512);
    memcpy(p->pixels, m->image, (size_t)m->w * m->h);
    int page0 = 15, row0 = 1 << 10;
    for (uint32_t part = 0; part < m->parts && part < m->bones; part++) {
        const PeImpPart *pt = &m->part[part];
        for (uint32_t i = 0; i < pt->tris + pt->quads; i++) {
            const PeImpFace *f = i < pt->tris ? &pt->tri[i] : &pt->quad[i - pt->tris];
            page0 = (f->page & 15) < page0 ? f->page & 15 : page0;
            row0 = (f->clut >> 6) < row0 ? f->clut >> 6 : row0;
        }
    }
    for (uint32_t part = 0; part < m->parts && part < m->bones; part++) {   /* extras past the bones: not drawn */
        const PeImpPart *pt = &m->part[part];
        int first = p->vertices;
        for (uint32_t i = 0; i < pt->tris; i++)
            if (add_face(p, m, (int)part, &pt->tri[i], pt->tri_skin ? (const uint8_t (*)[2])pt->tri_skin[i] : NULL, 3, first, page0, row0, skipped)) goto full;
        for (uint32_t i = 0; i < pt->quads; i++)
            if (add_face(p, m, (int)part, &pt->quad[i], pt->quad_skin ? (const uint8_t (*)[2])pt->quad_skin[i] : NULL, 4, first, page0, row0, skipped)) goto full;
    }
    if (p->vertices < 3 || !p->triangles) return fail(why, n, "%sthe model has nothing to draw", "", 0);
    return 0;
full:
    return fail(why, n, "%sthe model has more than %ld vertices or triangles", "", PE_ENEMY_MAX_VERTICES);
}

int pe_enemy_build_model(PeEnemyPack *p, const PeImpModel *m, int *skipped, char *why, size_t n) {
    int ignore = 0;
    memset(p, 0, sizeof *p);
    for (int i = 0; i < PE_ANIM_ROLES; i++) p->role[i] = -1;
    if (model_in(p, m, skipped ? skipped : &ignore, why, n)) { pe_enemy_pack_free(p); return -1; }
    return 0;
}

/* ---- moves ---- */
static int anim_in(PeEnemyPack *p, const PeImpAnimation *a, const char *name, int move, char *why, size_t n) {
    if (move < 0 || (uint32_t)move >= a->moves || !a->move[move].frames)
        return fail(why, n, "the profile's \"%s\" names move %ld, which is empty or missing", name, move);
    if (p->anims == PE_ENEMY_MAX_ANIMS) return fail(why, n, "%stoo many anims", "", 0);
    const PeImpMove *mv = &a->move[move];
    PeEnemyAnim *an = &p->anim[p->anims];
    size_t words = 3 + 3 * (size_t)p->bones;
    an->frames = (int)mv->frames;
    an->data = malloc(words * mv->frames * sizeof *an->data);
    an->event = malloc(mv->frames * sizeof *an->event);
    if (!an->data || !an->event) { free(an->data), free(an->event); an->data = NULL, an->event = NULL; return fail(why, n, "%sout of memory", "", 0); }
    memcpy(an->data, mv->data, words * mv->frames * sizeof *an->data);
    for (uint32_t k = 0; k < mv->frames; k++) {
        unsigned e = mv->event ? mv->event[k] : 0, s = e & PE_IMP_EVENT_SOUND;
        an->event[k] = (uint16_t)((s < PE_ENEMY_MAX_SOUNDS ? s : 0) | (e & PE_IMP_EVENT_HIT ? PE_ENEMY_EVENT_HIT : 0));
    }
    snprintf(an->name, sizeof an->name, "%s", name);
    p->anims++;
    return 0;
}

int pe_enemy_add_room_move(PeEnemyPack *p, const PeImpAnimation *moves, const char *name) {
    char why[100];
    if (moves->bones != (unsigned)p->bones || anim_in(p, moves, name, 0, why, sizeof why)) return -1;
    PeEnemyAnim *anim = &p->anim[p->anims - 1];
    const size_t words = 3 + 3 * (size_t)p->bones;
    for (int i = 0; i < anim->frames; i++) {
        int16_t *frame = anim->data + i * words;
        frame[0] = frame[1] = frame[2] = 0; /* choreography uses a room position; keep the PE battle position */
        anim->event[i] = 0;                 /* room sound IDs are not enemy pack IDs */
    }
    return p->anims - 1;
}

/* floor to the top of the head in the idle pose (source units, y down) */
static int height_of(const PeEnemyPack *p, int idle, int16_t (*pose)[3]) {
    if (pe_enemy_pose(p, idle, 0, pose)) return 0;
    int top = 0, hi = -32768, lo = 32767;
    for (int i = 0; i < p->vertices; i++) {
        top = pose[i][1] < top ? pose[i][1] : top;
        lo = pose[i][1] < lo ? pose[i][1] : lo, hi = pose[i][1] > hi ? pose[i][1] : hi;
    }
    /* how far it stands up from its origin; a model hung or floating under its origin (nothing above
     * it, as RE1's EM1008 and EM100E): its own top-to-bottom size */
    return -top > 0 ? -top : (p->vertices && hi > lo ? hi - lo : 0);
}

/* how far in front of its centre (source units) frame k of an anim reaches. In front is where the
 * profile's yaw turns to face Aya: the model's (-sin yaw, cos yaw) in x and z (2048: -z, 3072: +x). */
static int front_of(const PeEnemyPack *p, int anim, int k, int16_t (*pose)[3]) {
    const double ang = p->yaw * 2 * 3.14159265358979323846 / 4096.0, fx = -sin(ang), fz = cos(ang);
    double front = 0;
    if (pe_enemy_pose(p, anim, k, pose)) return -1;
    for (int i = 0; i < p->vertices; i++) {
        const double d = pose[i][0] * fx + pose[i][2] * fz;
        if (d > front) front = d;
    }
    return (int)lrint(front);
}

/* the frame a blow lands: the move's first hit frame, else where it reaches furthest forward */
static int blow_frame(const PeEnemyPack *p, int anim, int16_t (*pose)[3]) {
    const PeEnemyAnim *a = &p->anim[anim];
    for (int k = 0; a->event && k < a->frames; k++) if (a->event[k] & PE_ENEMY_EVENT_HIT) return k;
    int best = 0, front = -1;
    for (int k = 0; k < a->frames; k++) {
        const int f = front_of(p, anim, k, pose);
        if (f < 0) break;
        if (f > front) front = f, best = k;
    }
    return best;
}

#define PE_AYA_HALF_WIDTH 120   /* PE field units: a blow lands on her body, not her centre */

static void scale(PeEnemyPack *p, double sc) {
    for (int i = 0; i < p->bones; i++)
        p->bone[i].x = clamp16(p->bone[i].x * sc), p->bone[i].y = clamp16(p->bone[i].y * sc), p->bone[i].z = clamp16(p->bone[i].z * sc);
    for (int i = 0; i < p->vertices; i++)
        p->vertex[i].x = clamp16(p->vertex[i].x * sc), p->vertex[i].y = clamp16(p->vertex[i].y * sc), p->vertex[i].z = clamp16(p->vertex[i].z * sc);
    size_t words = 3 + 3 * (size_t)p->bones;
    for (int a = 0; a < p->anims; a++)
        for (int k = 0; k < p->anim[a].frames; k++)
            for (int c = 0; c < 3; c++) p->anim[a].data[k * words + c] = clamp16(p->anim[a].data[k * words + c] * sc);
}

int pe_enemy_build(const PeImpModel *m, const PeImpAnimation *a, const PeImpEnemyProfile *prof,
                   PeImpSound *const *sounds, int sound_count, const char *source, PeEnemyPack *p,
                   PeEnemyBuildInfo *info, char *why, size_t n) {
    memset(p, 0, sizeof *p);
    memset(info, 0, sizeof *info);
    for (int i = 0; i < PE_ANIM_ROLES; i++) p->role[i] = -1;
    int16_t (*pose)[3] = NULL;
    int rc = -1;
    if (a->bones != m->bones) { fail(why, n, "%sthe moves are for %ld bones, not the model's", "", (long)a->bones); goto out; }
    snprintf(p->name, sizeof p->name, "%s", prof->name ? prof->name : "Enemy");
    snprintf(p->folder, sizeof p->folder, "%s", source ? source : "");
    snprintf(p->source, sizeof p->source, "%s", source ? source : "");
    if (prof->next_form) snprintf(p->next_form, sizeof p->next_form, "%s", prof->next_form);
    p->hp = prof->hp, p->exp = prof->exp, p->defense = prof->defense, p->fps = (int)a->fps, p->yaw = prof->yaw;
    p->speed = prof->speed, p->charge = prof->charge, p->radius = prof->radius, p->stagger = prof->stagger;
    if (model_in(p, m, &info->skipped, why, n)) goto out;
    for (int i = 0; i < PE_IMP_ENEMY_ANIMS && prof->anim[i].name; i++)
        if (anim_in(p, a, prof->anim[i].name, prof->anim[i].move, why, n)) goto out;
    const PeImpEnemyStart *start = pe_imp_enemy_starts(source);
    for (; start && start->name; start++) {
        if (p->anims == PE_ENEMY_MAX_ANIMS) break;
        if (start->move < 0 || (uint32_t)start->move >= a->moves || !a->move[start->move].frames) continue;
        if (pe_enemy_pack_anim(p, start->name) < 0 && anim_in(p, a, start->name, start->move, why, n)) goto out;
        const int wake = start->wake_move;
        if (p->anims == PE_ENEMY_MAX_ANIMS || wake < 0 || (uint32_t)wake >= a->moves || !a->move[wake].frames) continue;
        char name[sizeof p->anim[0].name];
        const int written = snprintf(name, sizeof name, "%s" PE_IMP_ENEMY_WAKE_SUFFIX, start->name);
        if (written < 0 || (size_t)written >= sizeof name || pe_enemy_pack_anim(p, name) >= 0) continue;
        if (anim_in(p, a, name, wake, why, n)) goto out;
    }
    static const char *const roles[PE_ANIM_ROLES] = {"idle", "walk", "hurt", "die"};
    for (int i = 0; i < PE_ANIM_ROLES; i++) p->role[i] = pe_enemy_pack_anim(p, roles[i]);
    if (p->role[PE_ANIM_IDLE] < 0) { fail(why, n, "%sthe profile has no \"idle\" move", "", 0); goto out; }
    if (!(pose = malloc((size_t)p->vertices * sizeof *pose))) { fail(why, n, "%sout of memory", "", 0); goto out; }
    int h = height_of(p, p->role[PE_ANIM_IDLE], pose);
    if (h <= 0) { fail(why, n, "%sthe idle pose has no height", "", 0); goto out; }
    info->scale = prof->height > 0 ? (double)prof->height / h : 1.0;
    for (int i = 0; i < PE_IMP_ENEMY_ATTACKS && prof->attack[i].anim; i++) {
        PeEnemyAttack *at = &p->attack[p->attacks];
        if ((at->anim = pe_enemy_pack_anim(p, prof->attack[i].anim)) < 0) { fail(why, n, "an attack names \"%s\", not in the profile's moves", prof->attack[i].anim, 0); goto out; }
        at->power = at->damage = prof->attack[i].power, at->range = prof->attack[i].range;
        int hit = prof->attack[i].hit;                 /* the profile's own frame, when it has one */
        at->hit = hit > 0 && hit < p->anim[at->anim].frames ? hit : blow_frame(p, at->anim, pose);
        if (at->range <= 0) {   /* never inside its own body (radius): it would stand in Aya */
            const int front = (int)lrint(front_of(p, at->anim, at->hit, pose) * info->scale);
            at->range = (front > p->radius ? front : p->radius) + PE_AYA_HALF_WIDTH;
        }
        p->attacks++;
    }
    scale(p, info->scale);
    for (int i = 0; i < sound_count && i < PE_ENEMY_MAX_SOUNDS; i++) {
        PeEnemySound *s = &p->sound[p->sounds++];
        if (!sounds[i]) continue;                       /* a gap keeps the numbering: silent */
        if (!(s->pcm = malloc(sounds[i]->frames * sizeof *s->pcm))) { fail(why, n, "%sout of memory", "", 0); goto out; }
        memcpy(s->pcm, sounds[i]->pcm, sounds[i]->frames * sizeof *s->pcm);
        s->rate = (int)sounds[i]->rate, s->frames = (int)sounds[i]->frames;
        info->sounds++;
    }
    if (prof->cues) {
        for (int i = 0; i < p->anims; i++)
            for (int k = 0; k < p->anim[i].frames; k++) p->anim[i].event[k] &= (uint16_t)~PE_ENEMY_EVENT_SOUND;
        for (int i = 0; i < p->attacks; i++)
            p->anim[p->attack[i].anim].event[p->attack[i].hit] |= PE_IMP_CUE_ATTACK + 1;
        if (p->role[PE_ANIM_HURT] >= 0) p->anim[p->role[PE_ANIM_HURT]].event[0] |= PE_IMP_CUE_HURT + 1;
        else if (p->sounds > PE_IMP_CUE_HURT + 1 && p->sound[PE_IMP_CUE_HURT + 1].pcm) p->hurt_sound = PE_IMP_CUE_HURT + 1;
        if (p->role[PE_ANIM_DIE] >= 0) p->anim[p->role[PE_ANIM_DIE]].event[0] |= PE_IMP_CUE_DIE + 1;
        else if (p->sounds > PE_IMP_CUE_DIE + 1 && p->sound[PE_IMP_CUE_DIE + 1].pcm) p->death_sound = PE_IMP_CUE_DIE + 1;
    }
    for (int i = 0; i < p->anims; i++)
        for (int k = 0; k < p->anim[i].frames; k++) {
            unsigned s = p->anim[i].event[k] & PE_ENEMY_EVENT_SOUND;
            if (s && (s >= (unsigned)p->sounds || !p->sound[s].pcm)) p->anim[i].event[k] &= (uint16_t)~PE_ENEMY_EVENT_SOUND;
            else if (s) info->sound_events++;
        }
    rc = 0;
out:
    free(pose);
    if (rc) pe_enemy_pack_free(p);
    return rc;
}

/* ---- writing ---- */
typedef struct { FILE *f; int bad; } Out;
static void w8(Out *o, unsigned v) { o->bad |= fputc((int)(v & 255), o->f) == EOF; }
static void w16(Out *o, unsigned v) { w8(o, v), w8(o, v >> 8); }
static void w32(Out *o, uint32_t v) { w16(o, v & 0xffff), w16(o, v >> 16); }

static int open_out(Out *o, const char *folder, const char *name, char *why, size_t n) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", folder, name);
    o->f = fopen(path, "wb"), o->bad = 0;
    return o->f ? 0 : fail(why, n, "can't write %s", name, 0);
}

static int close_out(Out *o, const char *name, char *why, size_t n) {
    o->bad |= fclose(o->f) != 0;
    return o->bad ? fail(why, n, "can't write %s", name, 0) : 0;
}

static void json_text(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') fputc('\\', f);
        if ((unsigned char)*s >= 32) fputc(*s, f);
    }
    fputc('"', f);
}

int pe_enemy_write(const PeEnemyPack *p, const char *folder, char *why, size_t n) {
    char sub[1200], name[64];
    Out o;
    snprintf(sub, sizeof sub, "%s/anims", folder);
    if (pe_imp_mkdirs(sub)) return fail(why, n, "can't create %s", sub, 0);
    if (p->sounds) {
        snprintf(sub, sizeof sub, "%s/sounds", folder);
        if (pe_imp_mkdirs(sub)) return fail(why, n, "can't create %s", sub, 0);
    }
    if (open_out(&o, folder, "model.bin", why, n)) return -1;
    fwrite("PEEM", 1, 4, o.f);
    w16(&o, 1), w16(&o, (unsigned)p->bones), w16(&o, (unsigned)p->vertices), w16(&o, (unsigned)p->triangles);
    w16(&o, (unsigned)p->tex_w), w16(&o, (unsigned)p->tex_h), w16(&o, (unsigned)p->palettes), w16(&o, 0);
    for (int i = 0; i < p->bones; i++)
        w16(&o, (uint16_t)p->bone[i].parent), w16(&o, (uint16_t)p->bone[i].x), w16(&o, (uint16_t)p->bone[i].y), w16(&o, (uint16_t)p->bone[i].z);
    for (int i = 0; i < p->vertices; i++) {
        const PeEnemyVertex *v = &p->vertex[i];
        w16(&o, (uint16_t)v->x), w16(&o, (uint16_t)v->y), w16(&o, (uint16_t)v->z), w8(&o, v->bone), w8(&o, v->bone2);
    }
    for (int i = 0; i < p->triangles; i++) {
        const PeEnemyTriangle *t = &p->triangle[i];
        for (int k = 0; k < 3; k++) w16(&o, t->v[k]);
        for (int k = 0; k < 3; k++) w16(&o, t->u[k]);
        for (int k = 0; k < 3; k++) w8(&o, t->t[k]);
        w8(&o, t->palette);
    }
    if (close_out(&o, "model.bin", why, n)) return -1;

    if (open_out(&o, folder, "texture.bin", why, n)) return -1;
    fwrite("PEET", 1, 4, o.f);
    w16(&o, 1), w16(&o, (unsigned)p->tex_w), w16(&o, (unsigned)p->tex_h), w16(&o, (unsigned)p->palettes);
    for (int i = 0; i < 256 * p->palettes; i++) w16(&o, p->palette[i]);
    o.bad |= fwrite(p->pixels, 1, (size_t)p->tex_w * p->tex_h, o.f) != (size_t)p->tex_w * p->tex_h;
    if (close_out(&o, "texture.bin", why, n)) return -1;

    size_t words = 3 + 3 * (size_t)p->bones;
    for (int a = 0; a < p->anims; a++) {
        const PeEnemyAnim *an = &p->anim[a];
        snprintf(name, sizeof name, "anims/%s.bin", an->name);
        if (open_out(&o, folder, name, why, n)) return -1;
        fwrite("PEEA", 1, 4, o.f);
        w16(&o, an->event ? 2 : 1), w16(&o, (unsigned)p->bones), w16(&o, (unsigned)an->frames), w16(&o, 0);
        for (size_t i = 0; i < words * (size_t)an->frames; i++) w16(&o, (uint16_t)an->data[i]);
        for (int k = 0; an->event && k < an->frames; k++) w16(&o, an->event[k]);
        if (close_out(&o, name, why, n)) return -1;
    }
    for (int s = 0; s < p->sounds; s++) {
        const PeEnemySound *snd = &p->sound[s];
        int16_t silence = 0;
        const int16_t *pcm = snd->pcm ? snd->pcm : &silence;
        uint32_t frames = snd->pcm ? (uint32_t)snd->frames : 1, rate = snd->pcm ? (uint32_t)snd->rate : 22050;
        snprintf(name, sizeof name, "sounds/%02d.wav", s);
        if (open_out(&o, folder, name, why, n)) return -1;
        fwrite("RIFF", 1, 4, o.f), w32(&o, 36 + 2 * frames), fwrite("WAVEfmt ", 1, 8, o.f);
        w32(&o, 16), w16(&o, 1), w16(&o, 1), w32(&o, rate), w32(&o, 2 * rate), w16(&o, 2), w16(&o, 16);
        fwrite("data", 1, 4, o.f), w32(&o, 2 * frames);
        for (uint32_t i = 0; i < frames; i++) w16(&o, (uint16_t)pcm[i]);
        if (close_out(&o, name, why, n)) return -1;
    }

    char path[1200];
    snprintf(path, sizeof path, "%s/enemy.json", folder);
    FILE *f = fopen(path, "wb");
    if (!f) return fail(why, n, "can't write %s", "enemy.json", 0);
    fprintf(f, "{\n  \"format\": 1,\n  \"name\": ");
    json_text(f, p->name);
    fprintf(f, ",\n  \"source\": ");
    json_text(f, p->source);
    if (p->next_form[0]) {
        fprintf(f, ",\n  \"next_form\": ");
        json_text(f, p->next_form);
    }
    fprintf(f, ",\n  \"hp\": %d, \"exp\": %d, \"defense\": %d, \"fps\": %d, \"yaw\": %d,\n", p->hp, p->exp, p->defense, p->fps, p->yaw);
    fprintf(f, "  \"model\": \"model.bin\", \"texture\": \"texture.bin\",\n  \"anims\": {");
    for (int a = 0; a < p->anims; a++) fprintf(f, "%s\"%s\": \"anims/%s.bin\"", a ? ", " : "", p->anim[a].name, p->anim[a].name);
    fprintf(f, "},\n  \"attacks\": [");
    for (int i = 0; i < p->attacks; i++) {
        const PeEnemyAttack *at = &p->attack[i];
        fprintf(f, "%s{\"anim\": \"%s\", \"damage\": %d, \"power\": %d, \"range\": %d, \"hit\": %d}", i ? ", " : "",
                p->anim[at->anim].name, at->damage, at->power, at->range, at->hit);
    }
    fprintf(f, "],\n  \"behavior\": {\"speed\": %d, \"charge\": %d, \"radius\": %d, \"stagger\": %d, \"altitude\": %d}",
            p->speed, p->charge, p->radius, p->stagger, p->altitude);
    if (p->sounds) {
        fprintf(f, ",\n  \"sounds\": [");
        for (int s = 0; s < p->sounds; s++) fprintf(f, "%s\"sounds/%02d.wav\"", s ? ", " : "", s);
        fprintf(f, "]");
    }
    if (p->hurt_sound) fprintf(f, ",\n  \"hurt_sound\": %d", p->hurt_sound);
    if (p->death_sound) fprintf(f, ",\n  \"death_sound\": %d", p->death_sound);
    fprintf(f, ",\n  \"audio_roles\": %u\n}\n", pe_enemy_pack_audio_roles(p));
    return fclose(f) ? fail(why, n, "can't write %s", "enemy.json", 0) : 0;
}
