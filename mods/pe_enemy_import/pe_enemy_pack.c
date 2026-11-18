/* Imported enemy packs: loading and posing (pe_enemy_pack.h). No game state here. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "pe_enemy_pack.h"

#define PE_PACK_FILE_MAX 0x800000u
#define PE_PI 3.14159265358979323846

static const char *const k_roles[PE_ANIM_ROLES] = {"idle", "walk", "hurt", "die"};

static unsigned rd16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static int rds16(const uint8_t *p) { return (int16_t)rd16(p); }

static int fail(char *err, unsigned size, const char *what, const char *file) {
    if (err && size) snprintf(err, size, "%s: %s", file, what);
    return -1;
}

static uint8_t *read_file(const char *dir, const char *name, unsigned *size) {
    char path[1024];
    FILE *f;
    long n;
    uint8_t *data;
    if (strstr(name, "..") || name[0] == '/' || name[0] == '\\' || strchr(name, ':')) return NULL;
    if (snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path) return NULL;
    if (!(f = fopen(path, "rb"))) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || (unsigned long)n > PE_PACK_FILE_MAX || !(data = malloc((size_t)n))) { fclose(f); return NULL; }
    if (fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); fclose(f); return NULL; }
    fclose(f);
    *size = (unsigned)n;
    return data;
}

static int load_model(PeEnemyPack *p, const char *dir, const char *file, char *err, unsigned es) {
    unsigned size = 0, i, need;
    uint8_t *b = read_file(dir, file, &size), *at;
    if (!b) return fail(err, es, "missing or unreadable", file);
    if (size < 20 || memcmp(b, "PEEM", 4) || rd16(b + 4) != 1) { free(b); return fail(err, es, "not a version 1 model", file); }
    p->bones = (int)rd16(b + 6);
    p->vertices = (int)rd16(b + 8);
    p->triangles = (int)rd16(b + 10);
    p->tex_w = (int)rd16(b + 12);
    p->tex_h = (int)rd16(b + 14);
    p->palettes = (int)rd16(b + 16);
    if (p->bones < 1 || p->bones > PE_ENEMY_MAX_BONES || p->vertices < 3 || p->vertices > PE_ENEMY_MAX_VERTICES ||
        p->triangles < 1 || p->triangles > PE_ENEMY_MAX_TRIANGLES) {
        free(b);
        return fail(err, es, "too many or too few bones, vertices or triangles", file);
    }
    need = 20u + 8u * p->bones + 8u * p->vertices + 16u * p->triangles;
    if (size < need) { free(b); return fail(err, es, "shorter than its counts", file); }
    p->vertex = calloc((size_t)p->vertices, sizeof *p->vertex);
    p->triangle = calloc((size_t)p->triangles, sizeof *p->triangle);
    if (!p->vertex || !p->triangle) { free(b); return fail(err, es, "out of memory", file); }
    at = b + 20;
    for (i = 0; i < (unsigned)p->bones; ++i, at += 8) {
        PeEnemyBone *bn = &p->bone[i];
        bn->parent = (int16_t)rds16(at);
        bn->x = (int16_t)rds16(at + 2); bn->y = (int16_t)rds16(at + 4); bn->z = (int16_t)rds16(at + 6);
        if (bn->parent >= (int)i || bn->parent < -1 || (i == 0) != (bn->parent < 0)) {
            free(b);
            return fail(err, es, "bone tree out of order (a parent must come before its child, bone 0 the root)", file);
        }
    }
    for (i = 0; i < (unsigned)p->vertices; ++i, at += 8) {
        PeEnemyVertex *v = &p->vertex[i];
        v->x = (int16_t)rds16(at); v->y = (int16_t)rds16(at + 2); v->z = (int16_t)rds16(at + 4);
        v->bone = at[6]; v->bone2 = at[7];
        if (v->bone >= p->bones || (v->bone2 != PE_ENEMY_NO_BONE && v->bone2 >= p->bones)) {
            free(b);
            return fail(err, es, "a vertex follows a bone the model doesn't have", file);
        }
    }
    for (i = 0; i < (unsigned)p->triangles; ++i, at += 16) {
        PeEnemyTriangle *t = &p->triangle[i];
        int k;
        for (k = 0; k < 3; ++k) {
            t->v[k] = (uint16_t)rd16(at + 2 * k);
            t->u[k] = (uint16_t)rd16(at + 6 + 2 * k);
            t->t[k] = at[12 + k];
            if (t->v[k] >= p->vertices) { free(b); return fail(err, es, "a triangle uses a missing vertex", file); }
        }
        t->palette = at[15];
    }
    free(b);
    return 0;
}

static int load_texture(PeEnemyPack *p, const char *dir, const char *file, char *err, unsigned es) {
    unsigned size = 0, need, i;
    uint8_t *b = read_file(dir, file, &size);
    if (!b) return fail(err, es, "missing or unreadable", file);
    if (size < 12 || memcmp(b, "PEET", 4) || rd16(b + 4) != 1) { free(b); return fail(err, es, "not a version 1 texture", file); }
    if ((int)rd16(b + 6) != p->tex_w || (int)rd16(b + 8) != p->tex_h || (int)rd16(b + 10) != p->palettes ||
        p->tex_w < 1 || p->tex_w > 512 || p->tex_h < 1 || p->tex_h > 256 || p->palettes < 1 || p->palettes > 8) {
        free(b);
        return fail(err, es, "size or palettes differ from the model's (at most 512 x 256, 8 palettes)", file);
    }
    need = 12u + 512u * p->palettes + (unsigned)p->tex_w * p->tex_h;
    if (size < need) { free(b); return fail(err, es, "shorter than its size", file); }
    p->palette = malloc(512u * p->palettes);
    p->pixels = malloc((size_t)p->tex_w * p->tex_h);
    if (!p->palette || !p->pixels) { free(b); return fail(err, es, "out of memory", file); }
    for (i = 0; i < 256u * p->palettes; ++i) p->palette[i] = (uint16_t)rd16(b + 12 + 2 * i);
    memcpy(p->pixels, b + 12 + 512u * p->palettes, (size_t)p->tex_w * p->tex_h);
    free(b);
    for (i = 0; i < (unsigned)p->triangles; ++i) {
        const PeEnemyTriangle *t = &p->triangle[i];
        int k;
        if (t->palette >= p->palettes) return fail(err, es, "a triangle uses a missing palette", file);
        for (k = 0; k < 3; ++k)
            if (t->u[k] >= p->tex_w || t->t[k] >= p->tex_h) return fail(err, es, "a triangle reaches past the texture", file);
    }
    return 0;
}

static int load_anim(PeEnemyPack *p, const char *dir, const char *name, const char *file, char *err, unsigned es) {
    unsigned size = 0, per, i;
    PeEnemyAnim *a;
    uint8_t *b;
    if (p->anims == PE_ENEMY_MAX_ANIMS) return fail(err, es, "too many animations", file);
    b = read_file(dir, file, &size);
    if (!b) return fail(err, es, "missing or unreadable", file);
    unsigned version = size >= 12 && !memcmp(b, "PEEA", 4) ? rd16(b + 4) : 0;
    if (version != 1 && version != 2) { free(b); return fail(err, es, "not a version 1 or 2 animation", file); }
    if ((int)rd16(b + 6) != p->bones) { free(b); return fail(err, es, "made for another skeleton", file); }
    a = &p->anim[p->anims];
    a->frames = (int)rd16(b + 8);
    per = 3u + 3u * p->bones;
    if (a->frames < 1 || size < 12u + 2u * (per + (version == 2)) * a->frames) { free(b); return fail(err, es, "no frames or shorter than its count", file); }
    if (!(a->data = malloc(2u * per * a->frames))) { free(b); return fail(err, es, "out of memory", file); }
    for (i = 0; i < per * a->frames; ++i) a->data[i] = (int16_t)rds16(b + 12 + 2 * i);
    if (version == 2) {
        if (!(a->event = malloc(2u * a->frames))) { free(a->data); a->data = NULL; free(b); return fail(err, es, "out of memory", file); }
        for (i = 0; i < (unsigned)a->frames; ++i) a->event[i] = (uint16_t)rd16(b + 12 + 2 * (per * a->frames + i));
    }
    free(b);
    snprintf(a->name, sizeof a->name, "%s", name);
    p->anims++;
    return 0;
}

static unsigned rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

int pe_enemy_wav_parse(const uint8_t *b, unsigned size, PeEnemySound *s) {
    unsigned at = 12, rate = 0, fmt_ok = 0;
    memset(s, 0, sizeof *s);
    if (size < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) return -1;
    while (at + 8 <= size) {
        unsigned len = rd32(b + at + 4), body = at + 8;
        if (len > size - body) return -1;
        if (!memcmp(b + at, "fmt ", 4) && len >= 16) {
            rate = rd32(b + body + 4);
            fmt_ok = rd16(b + body) == 1 && rd16(b + body + 2) == 1 && rd16(b + body + 14) == 16;
        } else if (!memcmp(b + at, "data", 4)) {
            unsigned frames = len / 2;
            if (!fmt_ok || rate < 8000 || rate > 48000 || !frames || frames > rate * 10) return -1;
            if (!(s->pcm = malloc(2u * frames))) return -1;
            for (unsigned i = 0; i < frames; ++i) s->pcm[i] = (int16_t)rds16(b + body + 2 * i);
            s->rate = (int)rate, s->frames = (int)frames;
            return 0;
        }
        at = body + len + (len & 1);
    }
    return -1;
}

static int load_sound(PeEnemyPack *p, const char *dir, const char *file, char *err, unsigned es) {
    unsigned size = 0;
    uint8_t *b;
    if (p->sounds == PE_ENEMY_MAX_SOUNDS) return fail(err, es, "too many sounds", file);
    if (!(b = read_file(dir, file, &size))) return fail(err, es, "missing or unreadable", file);
    int rc = pe_enemy_wav_parse(b, size, &p->sound[p->sounds]);
    free(b);
    if (rc) return fail(err, es, "not a mono 16-bit PCM WAV of 8000-48000 Hz, at most 10 seconds", file);
    p->sounds++;
    return 0;
}

int pe_enemy_pack_anim(const PeEnemyPack *pack, const char *name) {
    for (int i = 0; i < pack->anims; ++i)
        if (!strcmp(pack->anim[i].name, name)) return i;
    return -1;
}

static int json_int(const cJSON *j, const char *key, int fallback, int lo, int hi) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    int n = cJSON_IsNumber(v) ? (int)v->valuedouble : fallback;
    return n < lo ? lo : n > hi ? hi : n;
}

static const char *json_str(const cJSON *j, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

int pe_enemy_pack_load(PeEnemyPack *p, const char *dir, char *err, unsigned es) {
    unsigned size = 0;
    uint8_t *text;
    cJSON *j = NULL, *it;
    const cJSON *anims, *attacks;
    const char *s;
    int i, rc = -1;

    memset(p, 0, sizeof *p);
    for (i = 0; i < PE_ANIM_ROLES; ++i) p->role[i] = -1;
    s = strrchr(dir, '/');
    if (!s) s = strrchr(dir, '\\');
    snprintf(p->folder, sizeof p->folder, "%s", s ? s + 1 : dir);
    if (!(text = read_file(dir, "enemy.json", &size))) return fail(err, es, "missing or unreadable", "enemy.json");
    j = cJSON_ParseWithLength((const char *)text, size);
    free(text);
    if (!cJSON_IsObject(j)) { cJSON_Delete(j); return fail(err, es, "not JSON", "enemy.json"); }
    if (json_int(j, "format", 0, 0, 99) != 1) { fail(err, es, "format must be 1", "enemy.json"); goto done; }
    s = json_str(j, "name");
    snprintf(p->name, sizeof p->name, "%s", s && *s ? s : p->folder);
    p->hp = json_int(j, "hp", 100, 1, 99999);
    p->exp = json_int(j, "exp", 0, 0, 99999);
    p->defense = json_int(j, "defense", -1, -1, 9999);
    p->fps = json_int(j, "fps", 30, 1, 60);
    p->yaw = json_int(j, "yaw", 0, -4096, 4096);
    {   /* how it fights: missing fields keep plain defaults, so format 1 packs still load */
        const cJSON *b = cJSON_GetObjectItemCaseSensitive(j, "behavior");
        p->speed = json_int(b, "speed", 10, 0, 200);
        p->charge = json_int(b, "charge", 45, 1, 2000);
        p->radius = json_int(b, "radius", 250, 20, 4000);
        p->stagger = json_int(b, "stagger", 1, 0, 1);
    }
    if (load_model(p, dir, (s = json_str(j, "model")) ? s : "model.bin", err, es)) goto done;
    if (load_texture(p, dir, (s = json_str(j, "texture")) ? s : "texture.bin", err, es)) goto done;
    anims = cJSON_GetObjectItemCaseSensitive(j, "anims");
    if (!cJSON_IsObject(anims)) { fail(err, es, "no anims", "enemy.json"); goto done; }
    cJSON_ArrayForEach(it, anims) {
        if (!cJSON_IsString(it) || !it->string) { fail(err, es, "an anim isn't a file name", "enemy.json"); goto done; }
        if (load_anim(p, dir, it->string, it->valuestring, err, es)) goto done;
    }
    for (i = 0; i < PE_ANIM_ROLES; ++i) p->role[i] = pe_enemy_pack_anim(p, k_roles[i]);
    if (p->role[PE_ANIM_IDLE] < 0) { fail(err, es, "needs an \"idle\" anim", "enemy.json"); goto done; }
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(j, "sounds")) {
        if (!cJSON_IsString(it)) { fail(err, es, "a sound isn't a file name", "enemy.json"); goto done; }
        if (load_sound(p, dir, it->valuestring, err, es)) goto done;
    }
    attacks = cJSON_GetObjectItemCaseSensitive(j, "attacks");
    cJSON_ArrayForEach(it, attacks) {
        PeEnemyAttack *a;
        if (p->attacks == PE_ENEMY_MAX_ATTACKS) break;
        a = &p->attack[p->attacks];
        s = json_str(it, "anim");
        if (!s || (a->anim = pe_enemy_pack_anim(p, s)) < 0) { fail(err, es, "an attack names a missing anim", "enemy.json"); goto done; }
        a->damage = json_int(it, "damage", 1, 0, 9999);
        a->power = json_int(it, "power", a->damage, 0, 9999);
        a->range = json_int(it, "range", 0, 0, 30000);
        a->hit = json_int(it, "hit", p->anim[a->anim].frames / 2, 0, p->anim[a->anim].frames - 1);
        p->attacks++;
    }
    rc = 0;
done:
    cJSON_Delete(j);
    if (rc) pe_enemy_pack_free(p);
    return rc;
}

void pe_enemy_pack_free(PeEnemyPack *p) {
    int i;
    free(p->vertex);
    free(p->triangle);
    free(p->palette);
    free(p->pixels);
    for (i = 0; i < p->anims; ++i) free(p->anim[i].data), free(p->anim[i].event);
    for (i = 0; i < p->sounds; ++i) free(p->sound[i].pcm);
    memset(p, 0, sizeof *p);
    for (i = 0; i < PE_ANIM_ROLES; ++i) p->role[i] = -1;
}

/* ---- posing ---- */

typedef struct { double m[3][3], t[3]; } Frame;

static void rot_xyz(double ax, double ay, double az, double out[3][3]) {
    const double k = 2.0 * PE_PI / 4096.0;
    double cx = cos(ax * k), sx = sin(ax * k), cy = cos(ay * k), sy = sin(ay * k), cz = cos(az * k), sz = sin(az * k);
    /* X * Y * Z */
    double xy[3][3] = {{cy, 0, sy}, {sx * sy, cx, -sx * cy}, {-cx * sy, sx, cx * cy}};
    double z[3][3] = {{cz, -sz, 0}, {sz, cz, 0}, {0, 0, 1}};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) out[r][c] = xy[r][0] * z[0][c] + xy[r][1] * z[1][c] + xy[r][2] * z[2][c];
}

int pe_enemy_pose(const PeEnemyPack *p, int anim, int frame, int16_t (*out)[3]) {
    Frame f[PE_ENEMY_MAX_BONES];
    double rest[PE_ENEMY_MAX_BONES][3];
    const int16_t *d;
    const PeEnemyAnim *a;
    int i, r, c;

    if (anim < 0 || anim >= p->anims) return -1;
    a = &p->anim[anim];
    if (frame < 0) frame = 0;
    if (frame >= a->frames) frame = a->frames - 1;
    d = a->data + (size_t)frame * (3 + 3 * p->bones);
    for (i = 0; i < p->bones; ++i) {
        const PeEnemyBone *b = &p->bone[i];
        double loc[3][3];
        const int16_t *ang = d + 3 + 3 * i;
        rot_xyz(ang[0], ang[1], ang[2], loc);
        if (b->parent < 0) {
            memcpy(f[i].m, loc, sizeof loc);
            f[i].t[0] = d[0]; f[i].t[1] = d[1]; f[i].t[2] = d[2];
            rest[i][0] = b->x; rest[i][1] = b->y; rest[i][2] = b->z;
        } else {
            const Frame *pf = &f[b->parent];
            const double off[3] = {b->x, b->y, b->z};
            for (r = 0; r < 3; ++r) {
                for (c = 0; c < 3; ++c)
                    f[i].m[r][c] = pf->m[r][0] * loc[0][c] + pf->m[r][1] * loc[1][c] + pf->m[r][2] * loc[2][c];
                f[i].t[r] = pf->t[r] + pf->m[r][0] * off[0] + pf->m[r][1] * off[1] + pf->m[r][2] * off[2];
                rest[i][r] = rest[b->parent][r] + off[r];
            }
        }
    }
    for (i = 0; i < p->vertices; ++i) {
        const PeEnemyVertex *v = &p->vertex[i];
        const int bones[2] = {v->bone, v->bone2};
        const int n = v->bone2 == PE_ENEMY_NO_BONE ? 1 : 2;
        double acc[3] = {0, 0, 0};
        for (int k = 0; k < n; ++k) {
            const Frame *bf = &f[bones[k]];
            const double l[3] = {v->x - rest[bones[k]][0], v->y - rest[bones[k]][1], v->z - rest[bones[k]][2]};
            for (r = 0; r < 3; ++r) acc[r] += bf->t[r] + bf->m[r][0] * l[0] + bf->m[r][1] * l[1] + bf->m[r][2] * l[2];
        }
        for (r = 0; r < 3; ++r) {
            double w = acc[r] / n;
            out[i][r] = (int16_t)(w > 32767 ? 32767 : w < -32768 ? -32768 : lrint(w));
        }
    }
    return 0;
}
