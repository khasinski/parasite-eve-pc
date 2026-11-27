/* Imported enemy packs (mods/pe_enemy_import/pe_enemy_pack.c): loading, refusals and posing.
 * A small pack is written here; with PE_TEST_ENEMY_PACK set (a pack built from the player's own
 * files), that pack is loaded too and its idle pose checked (standing on the floor, its height). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_enemy_pack.h"

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

static char s_root[900];

static void put16(FILE *f, int v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

static void write_file(const char *dir, const char *name, const void *data, size_t n) {
    char path[1200];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    assert(f);
    fwrite(data, 1, n, f);
    fclose(f);
}

/* Two bones: root at (0, -100, 0), a child 50 above it. Vertices: 0 on the root, 1 on the child
 * (at the child's joint + 10 in x), 2 a joint vertex on both, 3 on the root. */
static void write_model(const char *dir, int bad_bone, int bad_uv) {
    char path[1200];
    FILE *f;
    static const int v[4][5] = {{0, -100, 0, 0, 255}, {10, -150, 0, 1, 255}, {10, -150, 0, 0, 1}, {0, 0, 0, 0, 255}};
    snprintf(path, sizeof path, "%s/model.bin", dir);
    f = fopen(path, "wb");
    assert(f);
    fwrite("PEEM", 1, 4, f);
    put16(f, 1); put16(f, 2); put16(f, 4); put16(f, 2); put16(f, 16); put16(f, 8); put16(f, 1); put16(f, 0);
    put16(f, -1); put16(f, 0); put16(f, -100); put16(f, 0);   /* bone 0 */
    put16(f, 0); put16(f, 0); put16(f, -50); put16(f, 0);     /* bone 1 */
    for (int i = 0; i < 4; ++i) {
        put16(f, v[i][0]); put16(f, v[i][1]); put16(f, v[i][2]);
        fputc(i == 1 && bad_bone ? 7 : v[i][3], f);
        fputc(v[i][4], f);
    }
    for (int t = 0; t < 2; ++t) {
        put16(f, 0); put16(f, 1 + t); put16(f, 2 + t * 1);
        put16(f, 0); put16(f, bad_uv && t ? 16 : 15); put16(f, 3);
        fputc(0, f); fputc(7, f); fputc(4, f); fputc(0, f);
    }
    fclose(f);
}

static void write_texture(const char *dir) {
    unsigned char b[12 + 512 + 16 * 8] = "PEET";
    b[4] = 1; b[6] = 16; b[8] = 8; b[10] = 1;
    for (int i = 0; i < 256; ++i) { b[12 + 2 * i] = (unsigned char)i; b[13 + 2 * i] = 0x7C; }
    for (int i = 0; i < 16 * 8; ++i) b[12 + 512 + i] = (unsigned char)i;
    write_file(dir, "texture.bin", b, sizeof b);
}

/* frames x (root xyz, bone 0 xyz, bone 1 xyz) */
static void write_anim(const char *dir, const char *name, int frames, const short *data) {
    char path[1200];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    assert(f);
    fwrite("PEEA", 1, 4, f);
    put16(f, 1); put16(f, 2); put16(f, frames); put16(f, 0);
    for (int i = 0; i < frames * 9; ++i) put16(f, data[i]);
    fclose(f);
}

static void make_pack(const char *name, const char *json, int bad_bone, int bad_uv) {
    char dir[1000], anims[1100];
    static const short idle[] = {0, -100, 0, 0, 0, 0, 0, 0, 0};
    /* frame 0 rest; frame 1: the child turned a quarter about y; frame 2: the root lifted 20 */
    static const short walk[] = {0, -100, 0, 0, 0, 0, 0, 0, 0,
                                 0, -100, 0, 0, 0, 0, 0, 1024, 0,
                                 0, -120, 0, 0, 0, 0, 0, 0, 0};
    snprintf(dir, sizeof dir, "%s/%s", s_root, name);
    MKDIR(dir);
    snprintf(anims, sizeof anims, "%s/anims", dir);
    MKDIR(anims);
    write_model(dir, bad_bone, bad_uv);
    write_texture(dir);
    write_anim(dir, "anims/idle.bin", 1, idle);
    write_anim(dir, "anims/walk.bin", 3, walk);
    write_file(dir, "enemy.json", json, strlen(json));
}

static const char *k_good =
    "{\"format\":1,\"name\":\"Tester\",\"hp\":321,\"exp\":45,\"defense\":7,\"fps\":30,\"yaw\":2048,"
    "\"anims\":{\"idle\":\"anims/idle.bin\",\"walk\":\"anims/walk.bin\",\"swing\":\"anims/walk.bin\"},"
    "\"attacks\":[{\"anim\":\"swing\",\"damage\":12,\"range\":900}]}";

static int load(const char *name, PeEnemyPack *p, char *err) {
    char dir[1000];
    snprintf(dir, sizeof dir, "%s/%s", s_root, name);
    return pe_enemy_pack_load(p, dir, err, 200);
}

static void expect_refused(const char *name, const char *json, int bad_bone, int bad_uv, const char *why) {
    PeEnemyPack p;
    char err[200] = "";
    make_pack(name, json, bad_bone, bad_uv);
    assert(load(name, &p, err) == -1);
    if (!strstr(err, why)) { fprintf(stderr, "%s: expected '%s', got '%s'\n", name, why, err); assert(0); }
    assert(p.vertex == NULL && p.anims == 0);   /* nothing kept after a refusal */
}

static int near(int a, int b) { return abs(a - b) <= 1; }

static void test_good(void) {
    PeEnemyPack p;
    char err[200] = "";
    int16_t out[4][3];
    make_pack("good", k_good, 0, 0);
    if (load("good", &p, err)) { fprintf(stderr, "good: %s\n", err); assert(0); }
    assert(!strcmp(p.name, "Tester") && !strcmp(p.folder, "good"));
    assert(p.hp == 321 && p.exp == 45 && p.defense == 7 && p.fps == 30 && p.yaw == 2048);
    assert(p.bones == 2 && p.vertices == 4 && p.triangles == 2 && p.tex_w == 16 && p.tex_h == 8 && p.palettes == 1);
    assert(p.palette[3] == 0x7C03 && p.pixels[17] == 17);
    assert(p.anims == 3 && p.role[PE_ANIM_IDLE] == pe_enemy_pack_anim(&p, "idle"));
    assert(p.role[PE_ANIM_WALK] == pe_enemy_pack_anim(&p, "walk") && p.role[PE_ANIM_HURT] == -1 && p.role[PE_ANIM_DIE] == -1);
    assert(p.attacks == 1 && p.attack[0].anim == pe_enemy_pack_anim(&p, "swing") && p.attack[0].damage == 12 && p.attack[0].range == 900);
    /* no "behavior", no "power" or "hit": plain defaults; power is the old damage, the blow half way */
    assert(p.speed == 10 && p.charge == 45 && p.radius == 250 && p.stagger == 1);
    assert(p.attack[0].power == 12 && p.attack[0].hit == 1);
    assert(p.triangle[0].u[1] == 15 && p.triangle[0].t[1] == 7 && p.vertex[2].bone2 == 1);

    /* rest pose: every vertex where the model put it */
    assert(pe_enemy_pose(&p, p.role[PE_ANIM_WALK], 0, out) == 0);
    for (int i = 0; i < 4; ++i)
        assert(out[i][0] == p.vertex[i].x && out[i][1] == p.vertex[i].y && out[i][2] == p.vertex[i].z);
    /* the child a quarter turn about y: (10, 0, 0) from its joint goes to (0, 0, -10); the joint
     * vertex is halfway between where each bone puts it; root vertices stay */
    assert(pe_enemy_pose(&p, p.role[PE_ANIM_WALK], 1, out) == 0);
    assert(near(out[1][0], 0) && near(out[1][1], -150) && near(out[1][2], -10));
    assert(near(out[2][0], 5) && near(out[2][1], -150) && near(out[2][2], -5));
    assert(out[0][0] == 0 && out[0][1] == -100 && out[3][1] == 0);
    /* the root lifted: everything moves with it; past the last frame stays on the last */
    assert(pe_enemy_pose(&p, p.role[PE_ANIM_WALK], 99, out) == 0);
    assert(out[3][1] == -20 && out[1][1] == -170);
    assert(pe_enemy_pose(&p, 7, 0, out) == -1);
    pe_enemy_pack_free(&p);
    assert(p.vertex == NULL && p.anims == 0);
}

/* How it fights: "behavior" and per attack "power" and "hit", kept inside their limits. */
static void test_behavior(void) {
    PeEnemyPack p;
    char err[200] = "";
    make_pack("fights",
              "{\"format\":1,\"anims\":{\"idle\":\"anims/idle.bin\",\"swing\":\"anims/walk.bin\"},"
              "\"behavior\":{\"speed\":14,\"charge\":70,\"radius\":300,\"stagger\":0},"
              "\"attacks\":[{\"anim\":\"swing\",\"damage\":5,\"power\":40,\"hit\":2,\"range\":800},"
              "{\"anim\":\"swing\",\"hit\":99}]}", 0, 0);
    if (load("fights", &p, err)) { fprintf(stderr, "fights: %s\n", err); assert(0); }
    assert(p.speed == 14 && p.charge == 70 && p.radius == 300 && p.stagger == 0);
    assert(p.attacks == 2 && p.attack[0].power == 40 && p.attack[0].damage == 5 && p.attack[0].hit == 2);
    assert(p.attack[1].hit == 2 && p.attack[1].power == 1);   /* past the anim's end: its last frame */
    pe_enemy_pack_free(&p);
}

static void test_refusals(void) {
    PeEnemyPack p;
    char err[200], dir[1000];
    snprintf(dir, sizeof dir, "%s/none", s_root);
    assert(pe_enemy_pack_load(&p, dir, err, sizeof err) == -1 && strstr(err, "enemy.json"));
    expect_refused("bone", k_good, 1, 0, "bone the model doesn't have");
    expect_refused("uv", k_good, 0, 1, "past the texture");
    expect_refused("format", "{\"format\":2,\"anims\":{\"idle\":\"anims/idle.bin\"}}", 0, 0, "format must be 1");
    expect_refused("noidle", "{\"format\":1,\"anims\":{\"walk\":\"anims/walk.bin\"}}", 0, 0, "\"idle\"");
    expect_refused("attack", "{\"format\":1,\"anims\":{\"idle\":\"anims/idle.bin\"},\"attacks\":[{\"anim\":\"bite\"}]}",
                   0, 0, "missing anim");
    expect_refused("escape", "{\"format\":1,\"anims\":{\"idle\":\"../good/anims/idle.bin\"}}", 0, 0, "missing or unreadable");
    expect_refused("model", "{\"format\":1,\"model\":\"nope.bin\",\"anims\":{\"idle\":\"anims/idle.bin\"}}", 0, 0, "nope.bin");
    expect_refused("json", "{\"format\":1,", 0, 0, "not JSON");
}

/* A real pack: idle stands on the floor and is about as tall as it was built. */
static void test_real(const char *dir) {
    PeEnemyPack p;
    char err[200] = "";
    static int16_t out[PE_ENEMY_MAX_VERTICES][3];
    int top = 0, bottom = -32768;
    if (pe_enemy_pack_load(&p, dir, err, sizeof err)) { fprintf(stderr, "%s: %s\n", dir, err); assert(0); }
    assert(p.role[PE_ANIM_IDLE] >= 0 && p.role[PE_ANIM_WALK] >= 0 && p.role[PE_ANIM_HURT] >= 0 && p.role[PE_ANIM_DIE] >= 0);
    assert(p.attacks >= 1);
    for (int a = 0; a < p.anims; ++a)
        for (int f = 0; f < p.anim[a].frames; ++f) assert(pe_enemy_pose(&p, a, f, out) == 0);
    pe_enemy_pose(&p, p.role[PE_ANIM_IDLE], 0, out);
    for (int i = 0; i < p.vertices; ++i) {
        if (out[i][1] < top) top = out[i][1];
        if (out[i][1] > bottom) bottom = out[i][1];
    }
    printf("real pack %s: %s, %d vertices, %d triangles, %d anims, idle from y %d to %d\n", dir, p.name, p.vertices,
           p.triangles, p.anims, top, bottom);
    assert(bottom > -40 && bottom < 40);        /* feet on the floor */
    assert(top < -300 && top > -2000);          /* a standing body */
    /* dying ends lower than it starts */
    pe_enemy_pose(&p, p.role[PE_ANIM_DIE], 9999, out);
    {
        int dead_top = 0;
        for (int i = 0; i < p.vertices; ++i) if (out[i][1] < dead_top) dead_top = out[i][1];
        assert(dead_top > top / 2);
    }
    pe_enemy_pack_free(&p);
}

int main(void) {
    const char *base = getenv("PE_TEST_DIR"), *real = getenv("PE_TEST_ENEMY_PACK");
    snprintf(s_root, sizeof s_root, "%s/enemy-pack-test", base && *base ? base : ".");
    MKDIR(s_root);
    test_good();
    test_behavior();
    test_refusals();
    if (real && *real) test_real(real);
    else printf("PE_TEST_ENEMY_PACK not set: the real-pack check is skipped\n");
    printf("ok\n");
    return 0;
}
