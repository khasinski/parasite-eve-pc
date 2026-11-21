/* The enemies builder (mods/pe_enemy_import/pe_enemy_build.c).
 *   made-up   a two-bone model (a rigid quad, a skinned triangle with a joint corner), moves with a
 *             sound and a hit frame, a profile, two sounds (the first a gap): the pack in memory
 *             (shared corners one vertex, the quad two triangles, rest model frame, scale to the
 *             profile's height, the blow on the hit frame), written and loaded back by the game's
 *             own loader (events, sounds); a profile naming a missing move is refused
 *   real      with PE_TEST_RE3_PC: the RE3 PC folder scanned into a store (the PC readers), then
 *             Nemesis built from the catalog as the importer's target does: 16 bones, his moves,
 *             the blows on m7 and m10's first hit frames, E_34's tones as sounds, footsteps on the
 *             walk; the pack loads back. Kept in <PE_TEST_DIR>/enemy-build-test/nemesis for the
 *             game test (PE_TEST_ENEMY_PACK). Nothing in the folder refused; Zombie, Cerberus, Hunter,
 *             Giant Spider and Drain Deimos built too (bones, their anims, each blow inside its move,
 *             their own sounds), each kept in its own folder there. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_enemy_build.h"
#include "pe_enemy_pack.h"
#include "pe_imp.h"
#include "pe_imp_internal.h"
#include "pe_src_re3.h"

static char s_dir[1024];

static void face(PeImpFace *f, const int (*v)[3], int k, int page, int clut) {
    memset(f, 0, sizeof *f);
    for (int j = 0; j < k; j++) {
        for (int c = 0; c < 3; c++) f->v[j][c] = (int16_t)v[j][c];
        f->uv[j][0] = (uint8_t)(j * 2), f->uv[j][1] = (uint8_t)j;
    }
    f->page = (uint16_t)page, f->clut = (uint16_t)clut;
}

static void made_up(PeImpModel *m, PeImpAnimation *a) {
    memset(m, 0, sizeof *m);
    m->bones = 2;
    m->parent = malloc(2 * sizeof *m->parent);
    m->rel = calloc(2, sizeof *m->rel);
    m->pos = calloc(2, sizeof *m->pos);
    m->parent[0] = -1, m->parent[1] = 0;
    m->rel[0][1] = -100, m->rel[1][1] = -50;
    m->pos[0][1] = -100, m->pos[1][1] = -150;
    m->parts = 2;
    m->part = calloc(2, sizeof *m->part);
    /* part 0 rigid (bone 0's frame): a quad from the floor (y +100 below the root) up to the root */
    static const int quad[4][3] = {{-10, 100, 0}, {10, 100, 0}, {-10, 0, 0}, {10, 0, 0}};
    m->part[0].quad = calloc(1, sizeof *m->part[0].quad), m->part[0].quads = 1;
    face(&m->part[0].quad[0], quad, 4, 0x81, 0x7800);   /* CLUT words: rows 480 and 481 */
    /* part 1 skinned (model frame): a triangle over the head, one corner joint between 1 and 0 */
    static const int tri[3][3] = {{0, -200, 0}, {5, -150, 0}, {-5, -150, 0}};
    m->part[1].tri = calloc(1, sizeof *m->part[1].tri), m->part[1].tris = 1;
    face(&m->part[1].tri[0], tri, 3, 0x80, 0x7840);
    m->part[1].tri_skin = calloc(1, sizeof *m->part[1].tri_skin);
    m->part[1].quad_skin = calloc(1, sizeof *m->part[1].quad_skin);
    uint8_t (*s)[2] = m->part[1].tri_skin[0];
    s[0][0] = 1, s[0][1] = PE_IMP_NO_BONE, s[1][0] = 1, s[1][1] = 0, s[2][0] = 1, s[2][1] = PE_IMP_NO_BONE;
    m->w = 144, m->h = 4, m->pal_rows = 2;                 /* the quad on the second page */
    m->pal = calloc(2, sizeof *m->pal);
    m->image = calloc(576, 1);
    for (int i = 0; i < 576; i++) m->image[i] = (uint8_t)i;
    m->pal[1][3] = 0x7fff;
    assert(!pe_imp_model_check(m, NULL, 0));

    memset(a, 0, sizeof *a);
    a->bones = 2, a->fps = 30, a->moves = 3;
    a->move = calloc(3, sizeof *a->move);
    const int frames[3] = {2, 0, 4};                      /* move 1 empty: the numbering is kept */
    for (int mv = 0; mv < 3; mv++) {
        a->move[mv].frames = (uint32_t)frames[mv];
        if (!frames[mv]) continue;
        a->move[mv].data = calloc((size_t)frames[mv] * 9, sizeof *a->move[mv].data);
        a->move[mv].event = calloc((size_t)frames[mv], sizeof *a->move[mv].event);
        for (int k = 0; k < frames[mv]; k++) a->move[mv].data[k * 9 + 1] = -100;   /* root height */
    }
    a->move[2].event[1] = 1;                              /* sound 1 */
    a->move[2].event[2] = PE_IMP_EVENT_HIT;
    a->move[2].event[3] = PE_IMP_EVENT_HIT | 9;           /* sound 9: no such sound, dropped */
    assert(!pe_imp_animation_check(a, NULL, 0));
}

static const PeImpEnemyProfile k_profile = {
    .name = "Test", .moves = "enemy/test/moves", .hp = 77, .exp = 5, .defense = 3, .height = 400, .yaw = 2048,
    .anim = {{"idle", 0}, {"punch", 2}},
    .attack = {{"punch", 30, 500}},
    .speed = 7, .charge = 50, .radius = 120, .stagger = 1,
};

static void test_made_up(void) {
    PeImpModel m;
    PeImpAnimation a;
    made_up(&m, &a);
    PeImpSound *snd[2] = {NULL, pe_imp_sound_alloc(22050, 100)};
    for (int i = 0; i < 100; i++) snd[1]->pcm[i] = (int16_t)(i * 10);
    PeEnemyPack p;
    PeEnemyBuildInfo info;
    char why[200] = "";
    int rc = pe_enemy_build(&m, &a, &k_profile, snd, 2, "test:enemy/test", &p, &info, why, sizeof why);
    if (rc) printf("build: %s\n", why);
    assert(!rc);
    assert(p.bones == 2 && p.vertices == 7 && p.triangles == 3 && info.skipped == 0);
    assert(fabs(info.scale - 2.0) < 1e-9);                /* 200 tall (floor 0 to -200) -> 400 */
    /* rigid corner moved to the rest model frame and scaled: (10, 100 - 100, 0) * 2 */
    int found = 0;
    for (int i = 0; i < p.vertices; i++)
        found |= p.vertex[i].x == 20 && p.vertex[i].y == 0 && p.vertex[i].bone == 0 && p.vertex[i].bone2 == PE_ENEMY_NO_BONE;
    assert(found);
    found = 0;
    for (int i = 0; i < p.vertices; i++) found |= p.vertex[i].y == -300 && p.vertex[i].bone == 1 && p.vertex[i].bone2 == 0;
    assert(found);                                        /* the joint corner keeps its second bone */
    assert(p.triangle[0].u[1] == 128 + 2 && p.triangle[2].palette == 1);   /* page 1 of 0: 128 + u */
    assert(p.anims == 2 && p.role[PE_ANIM_IDLE] == 0 && p.role[PE_ANIM_WALK] == -1);
    assert(p.anim[1].data[1] == -200);                    /* root height scaled */
    assert(p.attacks == 1 && p.attack[0].hit == 2 && p.attack[0].power == 30 && p.attack[0].range == 500);
    assert(p.anim[1].event[1] == 1 && p.anim[1].event[2] == PE_ENEMY_EVENT_HIT && p.anim[1].event[3] == PE_ENEMY_EVENT_HIT);
    assert(p.sounds == 2 && !p.sound[0].pcm && p.sound[1].frames == 100 && info.sounds == 1 && info.sound_events == 1);

    {   /* range 0: measured, how far in front the scaled model gets at the blow frame (at least
         * its radius) + 120; yaw
         * 3072 (as RE3's): in front is +x */
        PeImpEnemyProfile measured = k_profile;
        PeEnemyPack q;
        PeEnemyBuildInfo qi;
        int16_t pose[16][3];
        int front = 0;
        measured.attack[0].range = 0;
        measured.yaw = 3072;
        rc = pe_enemy_build(&m, &a, &measured, snd, 2, "test:enemy/test", &q, &qi, why, sizeof why);
        assert(!rc && q.vertices <= 16);
        assert(!pe_enemy_pose(&q, q.attack[0].anim, q.attack[0].hit, pose));
        for (int i = 0; i < q.vertices; i++) front = pose[i][0] > front ? pose[i][0] : front;
        assert(abs(q.attack[0].range - ((front > q.radius ? front : q.radius) + 120)) <= 2);
        pe_enemy_pack_free(&q);
    }

    char folder[1100];
    snprintf(folder, sizeof folder, "%s/made-up", s_dir);
    rc = pe_enemy_write(&p, folder, why, sizeof why);
    if (rc) printf("write: %s\n", why);
    assert(!rc);
    pe_enemy_pack_free(&p);
    PeEnemyPack back;
    char err[200] = "";
    rc = pe_enemy_pack_load(&back, folder, err, sizeof err);
    if (rc) printf("load: %s\n", err);
    assert(!rc);
    assert(back.hp == 77 && back.yaw == 2048 && back.vertices == 7 && back.triangles == 3 && back.palettes == 2);
    assert(back.attacks == 1 && back.attack[0].hit == 2 && back.speed == 7 && back.charge == 50);
    int punch = pe_enemy_pack_anim(&back, "punch");
    assert(punch >= 0 && back.anim[punch].event && back.anim[punch].event[1] == 1);
    assert(back.sounds == 2 && back.sound[1].rate == 22050 && back.sound[1].frames == 100 && back.sound[1].pcm[50] == 500);
    assert(back.sound[0].frames == 1);                    /* the gap: one silent frame */
    pe_enemy_pack_free(&back);

    /* a profile naming an empty move */
    PeImpEnemyProfile bad = k_profile;
    bad.anim[1].move = 1;
    assert(pe_enemy_build(&m, &a, &bad, snd, 2, "x", &p, &info, why, sizeof why) == -1 && strstr(why, "punch"));
    bad = k_profile;
    bad.attack[0].anim = "kick";
    assert(pe_enemy_build(&m, &a, &bad, snd, 2, "x", &p, &info, why, sizeof why) == -1 && strstr(why, "kick"));
    /* the profile's own hit frame wins over the move's flag; one past the move is passed over */
    PeImpEnemyProfile own = k_profile;
    own.attack[0].hit = 1;
    assert(!pe_enemy_build(&m, &a, &own, snd, 2, "x", &p, &info, why, sizeof why) && p.attack[0].hit == 1);
    pe_enemy_pack_free(&p);
    own.attack[0].hit = 999;
    assert(!pe_enemy_build(&m, &a, &own, snd, 2, "x", &p, &info, why, sizeof why) && p.attack[0].hit == 2);
    pe_enemy_pack_free(&p);
    free(snd[1]);
    pe_imp_model_free(&m);
    pe_imp_animation_free(&a);
    printf("made-up: built, written and loaded back with events and sounds\n");
}

/* RE3's other enemies with a profile: built the same way, each pack kept beside Nemesis's */
typedef struct { const char *id, *folder; const PeImpEnemyProfile *p; int bones; } More;
static const More k_more[] = {
    {"enemy/nemesis", "nemesis", &g_re3_nemesis, 16},
    {"enemy/zombie", "zombie", &g_re3_zombie, 15},
    {"enemy/cerberus", "cerberus", &g_re3_cerberus, 17},
    {"enemy/hunter_beta", "hunter_beta", &g_re3_hunter, 20},
    {"enemy/spider", "spider", &g_re3_spider, 20},
    {"enemy/drain_deimos", "drain_deimos", &g_re3_drain_deimos, 21},
};

static void build_more(const char *store, const More *e) {
    char asset[80], moves[80], why[200] = "";
    snprintf(asset, sizeof asset, "re3:%s", e->id);
    snprintf(moves, sizeof moves, "re3:%s", e->p->moves);
    const PeImpEnemyProfile *prof = pe_imp_enemy_for(asset);
    assert(prof == e->p);
    PeImpModel m;
    PeImpAnimation a;
    if (pe_imp_catalog_model(store, asset, &m, why, sizeof why) || pe_imp_catalog_animation(store, moves, &a, why, sizeof why)) {
        printf("real: %s: %s\n", asset, why);
        assert(0);
    }
    PeImpSound *snd[PE_ENEMY_MAX_SOUNDS] = {0};
    for (int i = 0; i < prof->sound_count; i++) {
        char id[80];
        snprintf(id, sizeof id, "re3:%s%02d", prof->sounds, i);
        if (pe_imp_catalog_sound(store, id, &snd[i], why, sizeof why)) printf("real: %s: %s\n", id, why), assert(0);
    }
    PeEnemyPack p;
    PeEnemyBuildInfo info;
    int rc = pe_enemy_build(&m, &a, prof, snd, prof->sound_count, asset, &p, &info, why, sizeof why);
    if (rc) printf("real: %s: %s\n", prof->name, why);
    assert(!rc);
    printf("real: %s %d bones, %d triangles, scale %.4f, hits %d/%d, %d sounds, %d sound frames\n", prof->name,
           p.bones, p.triangles, info.scale, p.attack[0].hit, p.attacks > 1 ? p.attack[1].hit : -1, info.sounds,
           info.sound_events);
    int anims = 0, attacks = 0;
    for (int k = 0; k < PE_IMP_ENEMY_ANIMS; k++) anims += prof->anim[k].name != NULL;
    for (int k = 0; k < PE_IMP_ENEMY_ATTACKS; k++) attacks += prof->attack[k].anim != NULL;
    assert(p.bones == e->bones && p.anims == anims && p.attacks == attacks && info.sounds == prof->sound_count);
    for (int k = 0; k < attacks; k++) {                        /* the blow lands inside its move, not on its first frame */
        int an = p.attack[k].anim;
        assert(p.attack[k].hit > 0 && p.attack[k].hit < p.anim[an].frames);
    }
    char folder[1100];
    snprintf(folder, sizeof folder, "%s/%s", s_dir, e->folder);
    assert(!pe_enemy_write(&p, folder, why, sizeof why));
    pe_enemy_pack_free(&p);
    PeEnemyPack back;
    char err[200] = "";
    rc = pe_enemy_pack_load(&back, folder, err, sizeof err);
    if (rc) printf("real: load %s: %s\n", folder, err);
    assert(!rc && back.bones == e->bones);
    pe_enemy_pack_free(&back);
    for (int i = 0; i < prof->sound_count; i++) free(snd[i]);
    pe_imp_model_free(&m);
    pe_imp_animation_free(&a);
}

static void test_real(void) {
    const char *dir = getenv("PE_TEST_RE3_PC");
    if (!dir || !*dir) { printf("real: PE_TEST_RE3_PC not set, skipped\n"); return; }
    char image[1200], store[1100], why[200] = "";
    snprintf(image, sizeof image, "%s/rofs1.dat", dir);
    snprintf(store, sizeof store, "%s/store", s_dir);
    pe_imp_registry_reset();
    assert(!pe_imp_add_game("re3", "Resident Evil 3", "Resident Evil"));
    assert(!pe_imp_add_reader(&g_re3_emd_pc) && !pe_imp_add_reader(&g_re3_tones_pc));
    assert(!pe_imp_set_naming("re3", g_re3_naming));
    for (unsigned i = 0; i < sizeof k_more / sizeof k_more[0]; i++) assert(!pe_imp_add_enemy("re3", k_more[i].id, k_more[i].p));
    PeImpScan sc;
    int rc = pe_imp_scan(store, image, NULL, &sc);
    printf("real: scanned %d files, %d items, %d refused%s%s\n", sc.files_read, sc.items + sc.skipped, sc.refused,
           sc.last_error[0] ? ": " : "", sc.last_error);
    assert(!rc && !strcmp(sc.game, "re3") && sc.refused == 0);   /* every enemy model and sound bank reads */

    const PeImpEnemyProfile *prof = pe_imp_enemy_for("re3:enemy/nemesis");
    assert(prof == &g_re3_nemesis);
    PeImpModel m;
    PeImpAnimation a;
    assert(!pe_imp_catalog_model(store, "re3:enemy/nemesis", &m, why, sizeof why));
    assert(!pe_imp_catalog_animation(store, "re3:enemy/nemesis/moves", &a, why, sizeof why));
    PeImpSound *snd[PE_ENEMY_MAX_SOUNDS] = {0};
    for (int i = 0; i < prof->sound_count; i++) {
        char id[80];
        snprintf(id, sizeof id, "re3:%s%02d", prof->sounds, i);
        assert(!pe_imp_catalog_sound(store, id, &snd[i], why, sizeof why));
    }
    PeEnemyPack p;
    PeEnemyBuildInfo info;
    rc = pe_enemy_build(&m, &a, prof, snd, prof->sound_count, "re3:enemy/nemesis", &p, &info, why, sizeof why);
    if (rc) printf("real: %s\n", why);
    assert(!rc);
    printf("real: Nemesis %d bones, %d vertices, %d triangles (%d left out), scale %.4f, %d anims, hits %d/%d, %d sounds, %d sound frames\n",
           p.bones, p.vertices, p.triangles, info.skipped, info.scale, p.anims, p.attack[0].hit, p.attack[1].hit,
           info.sounds, info.sound_events);
    assert(p.bones == 16 && p.anims == 6 && p.attacks == 2 && info.sounds == 16);
    assert(p.attack[0].hit == 13 && p.attack[1].hit == 14);   /* m7 and m10: first frame flagged 0x800 */
    int walk = p.role[PE_ANIM_WALK];
    assert(walk >= 0 && p.anim[walk].frames == 52 && (p.anim[walk].event[2] & PE_ENEMY_EVENT_SOUND) == 2);
    char folder[1100];
    snprintf(folder, sizeof folder, "%s/nemesis", s_dir);
    assert(!pe_enemy_write(&p, folder, why, sizeof why));
    pe_enemy_pack_free(&p);
    PeEnemyPack back;
    char err[200] = "";
    rc = pe_enemy_pack_load(&back, folder, err, sizeof err);
    if (rc) printf("real: load: %s\n", err);
    assert(!rc && back.sounds == 16 && back.bones == 16);
    pe_enemy_pack_free(&back);
    for (int i = 0; i < prof->sound_count; i++) free(snd[i]);
    pe_imp_model_free(&m);
    pe_imp_animation_free(&a);
    printf("real: pack in %s\n", folder);
    for (unsigned i = 1; i < sizeof k_more / sizeof k_more[0]; i++) build_more(store, &k_more[i]);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *base = getenv("PE_TEST_DIR");
    snprintf(s_dir, sizeof s_dir, "%s/enemy-build-test", base && *base ? base : ".");
    pe_imp_mkdirs(s_dir);
    test_made_up();
    test_real();
    printf("ok\n");
    return 0;
}
