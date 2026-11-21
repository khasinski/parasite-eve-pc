/* The enemy profile guesser (mods/import/core/pe_imp_guess.c).
 *   made-up   a two-bone model (a body, an arm on top) with an idle, a walk (the arm swings), a
 *             hurt (a short jolt back), an attack (the arm swings forward), a fall, an empty move
 *             and a second attack the source marks with a hit frame: each role picked, the marked
 *             attack first; the guess builds into a pack; the profile file written, read back,
 *             changed by hand (another walk) and read again; broken ones refused
 *   real      PE_TEST_RE3_PC (the RE3 PC folder): the six monsters with hand-made profiles guessed
 *             too, guess and hand pick printed side by side; each guess builds into a pack, and
 *             Nemesis's and the zombie's deaths are the hand picks.
 *             PE_TEST_RE2_DISC / PE_TEST_RE1_DISC: every enemy model on the disc guessed, built and
 *             loaded back by the game's own loader (packs kept in <PE_TEST_DIR>/enemy-guess-test/
 *             <game>-<id> for the game test, PE_TEST_ENEMY_PACK); every one has an idle, a death
 *             and an attack, or the test says which don't.
 *   PE_GUESS_DUMP=1 prints every move's measurements (for tuning the rules). */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_enemy_build.h"
#include "pe_imp_guess.h"
#include "pe_enemy_pack.h"
#include "pe_imp.h"
#include "pe_imp_internal.h"
#include "pe_src_re1.h"
#include "pe_src_re2.h"
#include "pe_src_re3.h"

static char s_dir[1024];

static int move_of(const PeImpEnemyProfile *p, const char *name) {
    for (int i = 0; i < PE_IMP_ENEMY_ANIMS && p->anim[i].name; i++)
        if (!strcmp(p->anim[i].name, name)) return p->anim[i].move;
    return -1;
}

/* ---- made-up ---- */
static void quad(PeImpFace *f, const int (*v)[3]) {
    memset(f, 0, sizeof *f);
    for (int j = 0; j < 4; j++) {
        for (int c = 0; c < 3; c++) f->v[j][c] = (int16_t)v[j][c];
        f->uv[j][0] = (uint8_t)(j & 1 ? 7 : 0), f->uv[j][1] = (uint8_t)(j & 2 ? 3 : 0);
    }
    f->page = 0x80;
}

#define PI 3.14159265358979323846
enum { M_IDLE, M_WALK, M_HURT, M_ATTACK, M_DIE, M_EMPTY, M_MARKED, M_MOVES };

static void made_up(PeImpModel *m, PeImpAnimation *a) {
    memset(m, 0, sizeof *m);
    m->bones = 2;
    m->parent = malloc(2 * sizeof *m->parent);
    m->rel = calloc(2, sizeof *m->rel);
    m->pos = calloc(2, sizeof *m->pos);
    m->parent[0] = -1, m->parent[1] = 0;
    m->rel[0][1] = -100, m->pos[0][1] = m->pos[1][1] = -100;   /* the arm's joint on top of the body */
    m->parts = 2;
    m->part = calloc(2, sizeof *m->part);
    static const int body[4][3] = {{-10, 100, 0}, {10, 100, 0}, {-10, 0, 0}, {10, 0, 0}};   /* floor to the joint */
    static const int arm[4][3] = {{-5, 0, 0}, {5, 0, 0}, {-5, -100, 0}, {5, -100, 0}};     /* straight up */
    for (int p = 0; p < 2; p++) {
        m->part[p].quad = calloc(1, sizeof *m->part[p].quad), m->part[p].quads = 1;
        quad(&m->part[p].quad[0], p ? arm : body);
    }
    m->w = 16, m->h = 4, m->pal_rows = 1;
    m->pal = calloc(1, sizeof *m->pal);
    m->image = calloc(64, 1);
    assert(!pe_imp_model_check(m, NULL, 0));

    static const int frames[M_MOVES] = {20, 24, 10, 30, 30, 0, 20};
    memset(a, 0, sizeof *a);
    a->bones = 2, a->fps = 30, a->moves = M_MOVES;
    a->move = calloc(M_MOVES, sizeof *a->move);
    for (int mv = 0; mv < M_MOVES; mv++) {
        const int nf = frames[mv];
        a->move[mv].frames = (uint32_t)nf;
        if (!nf) continue;
        int16_t *d = a->move[mv].data = calloc((size_t)nf * 9, sizeof *d);
        a->move[mv].event = calloc((size_t)nf, sizeof *a->move[mv].event);
        for (int f = 0; f < nf; f++, d += 9) {
            const double t = (double)f / (nf - 1);
            d[1] = -100;                                          /* the root stands where it rests */
            if (mv == M_WALK) d[8] = (int16_t)lround(150 * sin(2 * PI * t));
            if (mv == M_HURT) d[8] = (int16_t)lround(-200 * sin(PI * t));
            if (mv == M_ATTACK) d[8] = (int16_t)lround(600 * sin(PI * t));
            if (mv == M_MARKED) d[6] = (int16_t)lround(500 * sin(PI * t));
            if (mv == M_DIE) d[1] = (int16_t)lround(-100 + 90 * t), d[5] = (int16_t)lround(1024 * t);   /* falls flat */
        }
        if (mv == M_MARKED) a->move[mv].event[10] = PE_IMP_EVENT_HIT;
    }
    assert(!pe_imp_animation_check(a, NULL, 0));
}

static void write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs(text, f);
    fclose(f);
}

static void test_made_up(void) {
    PeImpModel m;
    PeImpAnimation a;
    PeImpEnemyGuess g;
    char why[200] = "", dir[1100], path[1200];
    made_up(&m, &a);
    int rc = pe_imp_enemy_guess(&m, &a, "Made Up", "enemy/made-up/moves", 3072, 1000, &g, why, sizeof why);
    if (rc) printf("made-up: %s\n", why);
    assert(!rc);
    const PeImpEnemyProfile *p = &g.p;
    printf("made-up: idle %d walk %d hurt %d die %d attack %d attack2 %d; %d tall, hp %d, speed %d\n", move_of(p, "idle"),
           move_of(p, "walk"), move_of(p, "hurt"), move_of(p, "die"), move_of(p, "attack"), move_of(p, "attack2"), p->height,
           p->hp, p->speed);
    assert(move_of(p, "idle") == M_IDLE && move_of(p, "walk") == M_WALK && move_of(p, "hurt") == M_HURT);
    assert(move_of(p, "die") == M_DIE && move_of(p, "attack") == M_MARKED && move_of(p, "attack2") == M_ATTACK);
    assert(!strcmp(p->name, "Made Up") && !strcmp(p->moves, "enemy/made-up/moves") && p->yaw == 3072);
    assert(p->height == 200 && p->hp >= 30 && p->speed > 0 && p->radius >= 180);   /* 200 units at 1:1 */
    assert(p->attack[0].anim && !strcmp(p->attack[0].anim, "attack") && p->attack[1].anim && !p->attack[2].anim);

    PeImpEnemyGuess body;
    assert(!pe_imp_add_enemy_kind("test", 1, "enemy/made-up"));
    assert(!pe_imp_set_enemy_body("test", 1, PE_IMP_ENEMY_CRAWLER));
    assert(pe_imp_enemy_body("test:enemy/made-up-cdemd2") == PE_IMP_ENEMY_CRAWLER);
    assert(pe_imp_enemy_body("other:enemy/made-up") == PE_IMP_ENEMY_UPRIGHT);
    assert(!pe_imp_enemy_guess_body(&m, &a, NULL, NULL, 0, 190, PE_IMP_ENEMY_CRAWLER, &body, why, sizeof why));
    assert(move_of(&body.p, "walk") >= 0 && body.p.speed > 0 && body.p.speed <= 10 && body.p.radius == 45);
    assert(!pe_imp_enemy_guess_body(&m, &a, NULL, NULL, 0, 190, PE_IMP_ENEMY_FLYER, &body, why, sizeof why));
    assert(move_of(&body.p, "walk") >= 0 && body.p.speed >= 10 && body.p.radius == 65);
    assert(!pe_imp_enemy_guess_body(&m, &a, NULL, NULL, 0, 190, PE_IMP_ENEMY_FIXED, &body, why, sizeof why));
    assert(move_of(&body.p, "walk") < 0 && !body.p.speed && body.p.attack[0].anim);

    {   /* the game's own strength, where it gave one, over the height rule; a variant takes its base's */
        PeImpEnemyGuess known = g;
        assert(!pe_imp_enemy_known_strength("test:enemy/made-up", &known.p));    /* none registered: kept */
        assert(known.p.hp == p->hp);
        assert(!pe_imp_add_enemy_strength("test:enemy/made-up", 700, 33) && pe_imp_add_enemy_strength("no-game", 1, 0));
        assert(pe_imp_enemy_known_strength("test:enemy/made-up-cdemd2", &known.p) == 1);
        assert(known.p.hp == 700 && known.p.exp == 117 && known.p.attack[0].power == 33 && known.p.attack[1].power == 33);
        assert(!pe_imp_add_enemy_strength("test:enemy/made-up", 500, 0) && pe_imp_enemy_known_strength("test:enemy/made-up", &known.p));
        assert(known.p.hp == 500 && known.p.attack[0].power == 33);   /* power 0: the attacks keep theirs */
        printf("made-up: known strength hp %d (guessed %d)\n", known.p.hp, p->hp);
    }

    PeEnemyPack pk;
    PeEnemyBuildInfo info;
    rc = pe_enemy_build(&m, &a, p, NULL, 0, "test:enemy/made-up", &pk, &info, why, sizeof why);
    if (rc) printf("made-up build: %s\n", why);
    assert(!rc && pk.anims == 6 && pk.attacks == 2 && pk.role[PE_ANIM_DIE] >= 0 && pk.attack[0].hit == 10);
    char coverage[64];
    pe_enemy_pack_coverage(&pk, 0, coverage, sizeof coverage);
    assert(!strcmp(coverage, "All animation roles covered"));
    pe_enemy_pack_free(&pk);

    /* the profile file: written, read back the same */
    snprintf(dir, sizeof dir, "%s/enemy-guess-test/made-up", s_dir);
    assert(!pe_imp_mkdirs(dir));
    snprintf(path, sizeof path, "%s/profile.json", dir);
    assert(!pe_imp_enemy_profile_save(p, "Guessed from the moves.", path, why, sizeof why));
    PeImpEnemyGuess back;
    rc = pe_imp_enemy_profile_load(path, &back, why, sizeof why);
    if (rc) printf("made-up load: %s\n", why);
    assert(!rc && !strcmp(back.p.name, "Made Up") && !strcmp(back.p.moves, p->moves) && back.p.height == p->height);
    assert(back.p.hp == p->hp && back.p.yaw == 3072 && back.p.speed == p->speed && !back.p.sounds);
    for (int i = 0; i < PE_IMP_ENEMY_ANIMS; i++) {
        assert(!p->anim[i].name == !back.p.anim[i].name);
        if (p->anim[i].name) assert(!strcmp(p->anim[i].name, back.p.anim[i].name) && p->anim[i].move == back.p.anim[i].move);
    }
    assert(!strcmp(back.p.attack[0].anim, "attack") && back.p.attack[0].power == p->attack[0].power && !back.p.attack[2].anim);

    /* changed by hand: the attack's swing as the walk, sounds added */
    write_text(path, "{\"format\": 1, \"name\": \"Hand\", \"moves\": \"enemy/made-up/moves\", \"sounds\": \"sound/x/tone\", "
                     "\"sound_count\": 3, \"hp\": 77, \"height\": 300, \"anims\": {\"idle\": 0, \"walk\": 3, \"swing\": 6}, "
                     "\"attacks\": [{\"anim\": \"swing\", \"power\": 9, \"hit\": 12}], \"behavior\": {\"speed\": 5}}");
    assert(!pe_imp_enemy_profile_load(path, &back, why, sizeof why));
    assert(!strcmp(back.p.name, "Hand") && back.p.hp == 77 && move_of(&back.p, "walk") == 3 && back.p.sound_count == 3);
    assert(!strcmp(back.p.sounds, "sound/x/tone") && back.p.attack[0].power == 9 && back.p.attack[0].hit == 12 && back.p.speed == 5);
    rc = pe_enemy_build(&m, &a, &back.p, NULL, 0, "test:enemy/made-up", &pk, &info, why, sizeof why);
    assert(!rc && pk.anims == 3 && pk.attacks == 1 && pk.attack[0].hit == 12);
    pk.role[PE_ANIM_WALK] = -1;
    pk.attacks = 0;
    pe_enemy_pack_coverage(&pk, 0, coverage, sizeof coverage);
    assert(!strcmp(coverage, "Missing walk, hurt, death, attack"));
    pe_enemy_pack_coverage(&pk, 1, coverage, sizeof coverage);
    assert(!strcmp(coverage, "Missing hurt, death, attack"));
    pe_enemy_pack_free(&pk);

    /* refused: no idle, an attack naming a missing anim, not a profile */
    write_text(path, "{\"format\": 1, \"anims\": {\"walk\": 1}}");
    assert(pe_imp_enemy_profile_load(path, &back, why, sizeof why) && strstr(why, "idle"));
    write_text(path, "{\"format\": 1, \"anims\": {\"idle\": 0}, \"attacks\": [{\"anim\": \"bite\"}]}");
    assert(pe_imp_enemy_profile_load(path, &back, why, sizeof why) && strstr(why, "bite"));
    write_text(path, "{\"anims\": {\"idle\": 0}}");
    assert(pe_imp_enemy_profile_load(path, &back, why, sizeof why));
    write_text(path, "{\"format\": 1, \"anims\": {\"idle\": \"zero\"}}");
    assert(pe_imp_enemy_profile_load(path, &back, why, sizeof why));

    /* nothing to pick from */
    PeImpAnimation empty = {2, 30, 1, NULL};
    PeImpMove none = {0, NULL, NULL};
    empty.move = &none;
    assert(pe_imp_enemy_guess(&m, &empty, "x", "y", 0, 0, &g, why, sizeof why) == -1);
    pe_imp_animation_free(&a);
    pe_imp_model_free(&m);
}

/* ---- real ---- */
static void dump(const char *id, const PeImpModel *m, const PeImpAnimation *a) {
    PeImpMoveLook *look = calloc(a->moves, sizeof *look);
    double h = 0;
    char why[200];
    if (!look || pe_imp_enemy_look(m, a, look, &h, why, sizeof why)) { free(look); return; }
    printf("== %s  height %.0f  bones %u  moves %u\n", id, h, (unsigned)a->bones, (unsigned)a->moves);
    printf("  m   fr  loop  motion start  low   end   +x    -x    +z    -z   hit snd\n");
    for (uint32_t k = 0; k < a->moves; k++) {
        const PeImpMoveLook *l = &look[k];
        if (l->frames)
            printf("  %-3u %3d %5.2f %6.3f %5.2f %5.2f %5.2f %5.2f %5.2f %5.2f %5.2f %3d %3d\n", (unsigned)k, l->frames, l->loop,
                   l->motion, l->start, l->low, l->end, l->reach[0], l->reach[1], l->reach[2], l->reach[3], l->hits, l->sounds);
    }
    free(look);
}

/* Guess `asset` ("re2:enemy/em10"), build it, write it to <dir>/enemy-guess-test/<game>-<id> and
 * load it back. 0 built; 1 built but missing a die or an attack; -1 refused (printed). */
static int guess_one(const char *store, const char *asset, const char *name, PeImpEnemyGuess *g) {
    char moves[PE_IMP_ID_MAX + 8], why[200] = "", folder[1200];
    const char *colon = strchr(asset, ':');
    PeImpModel m;
    PeImpAnimation a;
    int yaw = 0, scale = 0, rc = -1;
    char game[16];
    snprintf(game, sizeof game, "%.*s", (int)(colon - asset), asset);
    pe_imp_enemy_defaults(game, &yaw, &scale);
    snprintf(moves, sizeof moves, "%s/moves", asset);
    if (pe_imp_catalog_model(store, asset, &m, why, sizeof why)) { printf("  %-28s model: %s\n", asset, why); return -1; }
    if (pe_imp_catalog_animation(store, moves, &a, why, sizeof why)) {
        printf("  %-28s no moves: %s\n", asset, why);
        pe_imp_model_free(&m);
        return -1;
    }
    if (getenv("PE_GUESS_DUMP")) dump(asset, &m, &a);
    PeEnemyPack pk;
    PeEnemyBuildInfo info;
    if (pe_imp_enemy_guess(&m, &a, name, moves + (colon - asset) + 1, yaw, scale, g, why, sizeof why) ||
        pe_enemy_build(&m, &a, &g->p, NULL, 0, asset, &pk, &info, why, sizeof why)) {
        printf("  %-28s refused: %s\n", asset, why);
        goto out;
    }
    snprintf(folder, sizeof folder, "%s/enemy-guess-test/%s-%s", s_dir, game, strrchr(asset, '/') + 1);
    if (pe_enemy_write(&pk, folder, why, sizeof why)) printf("  %-28s write: %s\n", asset, why), assert(0);
    pe_enemy_pack_free(&pk);
    if (pe_enemy_pack_load(&pk, folder, why, sizeof why)) printf("  %-28s load: %s\n", asset, why), assert(0);
    const PeImpEnemyProfile *p = &g->p;
    printf("  %-28s %4d tall  idle %2d walk %2d hurt %2d die %2d attack %2d %2d  (%d triangles, %d skipped)\n", asset,
           p->height, move_of(p, "idle"), move_of(p, "walk"), move_of(p, "hurt"), move_of(p, "die"), move_of(p, "attack"),
           move_of(p, "attack2"), pk.triangles, info.skipped);
    rc = pk.role[PE_ANIM_DIE] >= 0 && pk.attacks ? 0 : 1;
    pe_enemy_pack_free(&pk);
out:
    pe_imp_animation_free(&a);
    pe_imp_model_free(&m);
    return rc;
}

static void scan(const char *store, const char *image, const char *game) {
    PeImpScan sc;
    memset(&sc, 0, sizeof sc);
    int rc = pe_imp_scan(store, image, game, &sc);
    printf("real %s: scanned %d files, %d items, %d refused%s%s\n", game, sc.files_read, sc.items + sc.skipped, sc.refused,
           sc.last_error[0] ? ": " : "", sc.last_error);
    assert(!rc && sc.items + sc.skipped > 0);
}

static void test_re3(const char *store) {
    static const struct { const char *id; const PeImpEnemyProfile *hand; } k[] = {
        {"re3:enemy/nemesis", &g_re3_nemesis}, {"re3:enemy/zombie", &g_re3_zombie}, {"re3:enemy/cerberus", &g_re3_cerberus},
        {"re3:enemy/hunter_beta", &g_re3_hunter}, {"re3:enemy/spider", &g_re3_spider}, {"re3:enemy/drain_deimos", &g_re3_drain_deimos}};
    char image[1200];
    snprintf(image, sizeof image, "%s/rofs1.dat", getenv("PE_TEST_RE3_PC"));
    assert(!pe_imp_add_reader(&g_re3_emd_pc) && !pe_imp_set_naming("re3", g_re3_naming));
    assert(!pe_imp_set_enemy_defaults("re3", PE_RE_ENEMY_YAW, PE_RE_ENEMY_SCALE));
    scan(store, image, "re3");
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++) {
        PeImpEnemyGuess g;
        const PeImpEnemyProfile *h = k[i].hand;
        assert(guess_one(store, k[i].id, h->name, &g) == 0);
        printf("  %-28s %4d tall  idle %2d walk %2d hurt %2d die %2d   (by hand)\n", "", h->height, move_of(h, "idle"),
               move_of(h, "walk"), move_of(h, "hurt"), move_of(h, "die"));
        if (i < 2) assert(move_of(&g.p, "die") == move_of(h, "die"));
        assert(g.p.height * 2 > h->height && g.p.height < h->height * 3 / 2);   /* RE3's own sizes; the hand ones are by eye */
    }
}

static void test_re2_banks(const char *store, const char *first) {
    const char *other = getenv("PE_TEST_RE2_OTHER_DISC");
    if (!other || !*other) return;
    scan(store, other, "re2");
    scan(store, first, "re2");  /* verify aliases also survive a cached scan */
    static const char *const ids[] = {"re2:enemy/em10-cdemd0", "re2:enemy/em10-cdemd1"};
    static const char *const archives[] = {"CDEMD0.EMS", "CDEMD1.EMS"};
    PeImpAsset *assets = NULL;
    const int count = pe_imp_catalog_assets(store, "re2", &assets);
    const PeImpAsset *bank[2] = {0};
    for (int i = 0; i < count; i++)
        for (int j = 0; j < 2; j++)
            if (!strcmp(assets[i].id, ids[j])) bank[j] = &assets[i];
    assert(bank[0] && bank[1] && strcmp(bank[0]->sha256, bank[1]->sha256));
    for (int j = 0; j < 2; j++) {
        char moves[PE_IMP_ID_MAX], why[200];
        PeImpModel m;
        PeImpAnimation a;
        assert(strstr(bank[j]->path, archives[j]));
        snprintf(moves, sizeof moves, "%s/moves", ids[j]);
        assert(!pe_imp_catalog_model(store, ids[j], &m, why, sizeof why));
        assert(!pe_imp_catalog_animation(store, moves, &a, why, sizeof why));
        assert(m.bones == a.bones && a.moves > 0);
        pe_imp_model_free(&m);
        pe_imp_animation_free(&a);
    }
    free(assets);
    printf("real re2: both campaign enemy aliases retain their own model and moves\n");
}

static void test_disc(const char *store, const char *env, const char *game) {
    const char *image = getenv(env);
    if (!image || !*image) { printf("real %s: skipped (no %s)\n", game, env); return; }
    if (!strcmp(game, "re2")) assert(!pe_imp_add_reader(&g_re2_emd_ps1) && !pe_imp_set_naming("re2", g_re2_naming));
    else assert(!pe_imp_add_reader(&g_re1_emd_ps1) && !pe_imp_add_reader(&g_re1_monsters_ps1) && !pe_imp_set_naming("re1", g_re1_naming));
    assert(!pe_imp_set_enemy_defaults(game, PE_RE_ENEMY_YAW, PE_RE_ENEMY_SCALE));
    scan(store, image, game);
    if (!strcmp(game, "re2")) test_re2_banks(store, image);
    PeImpAsset *as = NULL;
    int n = pe_imp_catalog_assets(store, game, &as), built = 0, partial = 0, refused = 0;
    for (int i = 0; i < n; i++) {
        PeImpEnemyGuess g;
        if (strcmp(as[i].type, pe_imp_type_name(PE_IMP_MODEL)) || !strstr(as[i].id, ":enemy/")) continue;
        if (!strcmp(game, "re2") && (strstr(as[i].id, "-cdemd0") || strstr(as[i].id, "-cdemd1"))) continue;
        const int rc = guess_one(store, as[i].id, as[i].name, &g);
        if (rc < 0) refused++;
        else if (rc) partial++;
        else built++;
    }
    free(as);
    printf("real %s: %d enemies built with an idle, a death and an attack; %d built missing one; %d refused\n", game, built,
           partial, refused);
    assert(built > 0 && !refused);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *base = getenv("PE_TEST_DIR");
    char store[1100];
    snprintf(s_dir, sizeof s_dir, "%s", base && *base ? base : ".");
    test_made_up();
    printf("made-up: ok\n");
    snprintf(store, sizeof store, "%s/enemy-guess-test/store", s_dir);
    if (getenv("PE_TEST_RE3_PC") && *getenv("PE_TEST_RE3_PC")) test_re3(store);
    else printf("real re3: skipped (no PE_TEST_RE3_PC)\n");
    test_disc(store, "PE_TEST_RE2_DISC", "re2");
    test_disc(store, "PE_TEST_RE1_DISC", "re1");
    printf("ok\n");
    return 0;
}
