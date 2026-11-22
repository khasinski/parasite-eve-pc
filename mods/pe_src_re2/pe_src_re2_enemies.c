/* Resident Evil 2's enemies for the importer (pe_imp_enemy.h): which model each room placement kind
 * is, and the strength the game gives each. Portable: the importer's registry only. */
#include <stdio.h>

#include "pe_imp_enemy.h"
#include "pe_src_re2.h"

/* Each enemy's life as RE2 sets it when it appears (normal difficulty), from what bio2-ds's EMxx.C
 * Init code does (read for behaviour; no code taken): it overrides the guess's height rule. Where
 * the game rolls one from a 16-entry table, a typical value; a boss's scene-only padding (the
 * alligator's and Mr. X's +20000 / +25000 that keep them up until their scene) left out. */
static const struct { unsigned em; int life; } k_life[] = {
    {0x10, 80}, {0x11, 250}, {0x12, 80}, {0x13, 80}, {0x14, 80}, {0x15, 80}, {0x16, 80}, {0x17, 80},
    {0x18, 80}, {0x19, 80}, {0x1A, 80}, {0x1B, 80}, {0x1C, 80}, {0x1D, 80}, {0x1E, 80}, {0x1F, 80},   /* zombies */
    {0x20, 80},    /* dog */
    {0x21, 10},    /* crow */
    {0x22, 100},   /* licker */
    {0x23, 300},   /* alligator */
    {0x24, 110},   /* licker (the other) */
    {0x25, 100},
    {0x28, 600},   /* G adult (it spawns embryos) */
    {0x29, 10},    /* roaches */
    {0x2A, 250},   /* Mr. X */
    {0x2B, 200},   /* Mr. X, the last form */
    {0x2D, 250},
    {0x2E, 100},   /* ivy */
    {0x30, 500},
    {0x31, 700},   /* G, first form */
    {0x33, 900},
    {0x34, 1100},
    {0x36, 600},
    {0x3A, 150},   /* moth */
};

/* Consulted bio2-ds EM10.C (Em10_Init and Em10_mv07) for the opening and
 * get-up moves. Type 8 starts in a script pose without this get-up path. */
static const PeImpEnemyStart k_zombie_starts[] = {
    {"start_prone_2", 0x3f, 2, 0x16, 0x09},
    {"start_prone_4", 0x3f, 4, 0x17, 0x08},
    {"start_scripted_8", 0x3f, 8, 0x12, -1},
    {"start_standing", 0x3f, 6, 1, -1},
    {0}
};

/* EM23.C's entrance motion after the room releases the Alligator. */
static const PeImpEnemyStart k_alligator_starts[] = {
    {"start_emerging", 0x3f, 0, 6, 6},
    {0}
};

int pe_re2_enemies_register(void) {
    int bad = 0;
    char id[32];
    /* bio2-ds EM10.C and SND.C, consulted for cue behaviour. EDT cues 8, 9 and 12
     * resolve to these waveforms in the disc's first ENEMSE set. */
    static const PeImpEnemyCues zombie = {{"sound/enemy00/wave07", "sound/enemy00/wave08", "sound/enemy00/wave14"}};
    /* A placement's kind is the enemy's id, and its model is "enemy/em<id>" in the main archive
     * (pe_src_re2_emd.c); 0x40 and up are people (Sherry, Ada, Annette...): not fought. */
    for (unsigned kind = 0x10; kind < 0x40; kind++) {
        snprintf(id, sizeof id, "enemy/em%02x", kind);
        bad |= pe_imp_add_enemy_kind("re2", kind, id) != 0;
        if (kind < 0x20) {
            bad |= pe_imp_set_enemy_starts("re2", kind, k_zombie_starts) != 0;
            snprintf(id, sizeof id, "re2:enemy/em%02x", kind);
            bad |= pe_imp_add_enemy_cues(id, &zombie) != 0;
        }
    }
    for (unsigned i = 0; i < sizeof k_life / sizeof k_life[0]; i++) {
        snprintf(id, sizeof id, "re2:enemy/em%02x", k_life[i].em);
        bad |= pe_imp_add_enemy_strength(id, k_life[i].life, 0) != 0;
    }
    bad |= pe_imp_set_enemy_starts("re2", 0x23, k_alligator_starts) != 0;
    /* Consulted bio2-ds for locomotion and the scene-only overlays.
     * EM2D pivots in place to grab, EM2F and EM37 are attached, and
     * EM2C/3E/3F play scripted poses without combat handlers. */
    static const struct { unsigned kind; PeImpEnemyBody body; } bodies[] = {
        {0x21, PE_IMP_ENEMY_FLYER}, {0x3a, PE_IMP_ENEMY_FLYER},
        {0x22, PE_IMP_ENEMY_CRAWLER}, {0x24, PE_IMP_ENEMY_CRAWLER}, {0x26, PE_IMP_ENEMY_CRAWLER},
        {0x29, PE_IMP_ENEMY_CRAWLER}, {0x3b, PE_IMP_ENEMY_CRAWLER},
        {0x2d, PE_IMP_ENEMY_FIXED}, {0x2f, PE_IMP_ENEMY_FIXED}, {0x37, PE_IMP_ENEMY_FIXED},
        {0x2c, PE_IMP_ENEMY_SCENE}, {0x3e, PE_IMP_ENEMY_SCENE}, {0x3f, PE_IMP_ENEMY_SCENE}
    };
    for (unsigned i = 0; i < sizeof bodies / sizeof bodies[0]; i++)
        bad |= pe_imp_set_enemy_body("re2", bodies[i].kind, bodies[i].body) != 0;
    /* Consulted bio2-ds for the source moves. EM26 slides using its only pose,
     * jumps to attack, and has a procedural defeat. EM3B crawls on pose 0 and
     * holds pose 1 before releasing its attack effect. The Ivy forms use
     * the same frontal heavy-hit move. EM2D uses move 4 for its retreat on
     * damage or defeat. EM27/29/37 have no separate flinch. */
    enum { A = PE_IMP_MOVE_GUESS, N = PE_IMP_MOVE_NONE };
    static const struct { unsigned kind; PeImpEnemyMoveHints hints; } moves[] = {
        {0x21, {.attack = 5, .hurt = 9, .walk = A, .die = A}},
        {0x23, {.attack = 4, .hurt = 10, .walk = 0, .die = A}},
        {0x26, {.attack = 0, .hurt = N, .walk = 0, .die = N,
                .attack_frames = 16, .leap_height = 1100, .leap_reach = 1400}},
        {0x27, {.attack = 7, .hurt = N, .walk = 1, .die = 9}},
        {0x28, {.attack = 4, .hurt = 9, .walk = A, .die = 14}},
        {0x29, {.attack = 2, .hurt = N, .walk = A, .die = A}},
        {0x2b, {.attack = A, .hurt = 6, .walk = 1, .die = 5}},
        {0x2d, {.attack = 5, .hurt = 4, .walk = N, .die = 4}},
        {0x2e, {.attack = A, .hurt = 3, .walk = 1, .die = A}},
        {0x2f, {.attack = 1, .hurt = 5, .walk = A, .die = 6}},
        {0x36, {.attack = 9, .hurt = 8, .walk = 1, .die = 10}},
        {0x37, {.attack = 2, .hurt = N, .walk = A, .die = A}},
        {0x39, {.attack = A, .hurt = 3, .walk = 1, .die = A}},
        {0x3a, {.attack = 4, .hurt = A, .walk = A, .die = A}},
        {0x3b, {.attack = 1, .hurt = N, .walk = 0, .die = N, .attack_frames = 30}}
    };
    for (unsigned i = 0; i < sizeof moves / sizeof moves[0]; i++)
        bad |= pe_imp_set_enemy_move_hints("re2", moves[i].kind, moves[i].hints) != 0;
    const PeImpEnemyMoveHints scene = {.attack = N, .hurt = N, .walk = N, .die = N};
    bad |= pe_imp_set_enemy_move_hints("re2", 0x2c, scene) != 0;
    bad |= pe_imp_set_enemy_move_hints("re2", 0x3e, scene) != 0;
    bad |= pe_imp_set_enemy_move_hints("re2", 0x3f, scene) != 0;
    bad |= pe_imp_set_enemy_next_form("re2", 0x31, 0x33) != 0;
    bad |= pe_imp_set_enemy_next_form("re2", 0x33, 0x34) != 0;
    bad |= pe_imp_set_enemy_next_form("re2", 0x34, 0x36) != 0;
    /* The room placements select sound types 12, 14 and 51 respectively.
     * Consulted bio2-ds SCE0.C, EM20.C, EM22.C and EM24.C for set selection and cue use.
     * These are the solo sets, resolved to VAB waves from the discs' EDT and tone tables. */
    static const PeImpEnemyCues dog = {{"sound/enemy06/wave04", "sound/enemy06/wave12", "sound/enemy06/wave13"}};
    static const PeImpEnemyCues licker = {{"sound/enemy08/wave12", "sound/enemy08/wave11", "sound/enemy08/wave09"}};
    static const PeImpEnemyCues evolved = {{"sound/enemy09/wave12", "sound/enemy09/wave11", "sound/enemy09/wave09"}};
    bad |= pe_imp_add_enemy_cues("re2:enemy/em20", &dog) != 0;
    bad |= pe_imp_add_enemy_cues("re2:enemy/em22", &licker) != 0;
    bad |= pe_imp_add_enemy_cues("re2:enemy/em24", &evolved) != 0;
    /* Crow and G role cues were checked in EM21.C and the EM31/33/34/36
     * handlers, then mapped through the corresponding disc VAB tone tables. */
    static const PeImpEnemyCues crow = {{"sound/enemy07/wave04", "sound/enemy07/wave05", "sound/enemy07/wave07"}};
    static const PeImpEnemyCues roach = {{"sound/enemy10/wave05", "sound/enemy10/wave03", "sound/enemy10/wave02"}};
    static const PeImpEnemyCues g1 = {{"sound/enemy21/wave07", "sound/enemy21/wave03", "sound/enemy21/wave12"}};
    static const PeImpEnemyCues g2 = {{"sound/enemy23/wave03", "sound/enemy23/wave02", "sound/enemy23/wave15"}};
    static const PeImpEnemyCues g3 = {{"sound/enemy24/wave20", NULL, NULL}};
    static const PeImpEnemyCues g4 = {{"sound/enemy25/wave06", "sound/enemy25/wave10", "sound/enemy25/wave01"}};
    static const PeImpEnemyCues moth = {{"sound/enemy18/wave09", "sound/enemy18/wave03", "sound/enemy18/wave05"}};
    bad |= pe_imp_add_enemy_cues("re2:enemy/em21", &crow) != 0;
    bad |= pe_imp_add_enemy_cues("re2:enemy/em29", &roach) != 0;
    bad |= pe_imp_add_enemy_cues("re2:enemy/em31", &g1) != 0;
    bad |= pe_imp_add_enemy_cues("re2:enemy/em33", &g2) != 0;
    bad |= pe_imp_add_enemy_cues("re2:enemy/em34", &g3) != 0;
    bad |= pe_imp_add_enemy_cues("re2:enemy/em36", &g4) != 0;
    bad |= pe_imp_add_enemy_cues("re2:enemy/em3a", &moth) != 0;
    /* Room sound types and VAB waves were checked against both US discs.
     * bio2-ds EM23/25/27/28/2A/2B/2D/2E/2F/30/37/3B describe the individual actions.
     * SCE0.C pairs sound types 20/21 and 5/17 in banks 59 and 42 respectively. */
    static const struct { unsigned kind; PeImpEnemyCues cues; } other_cues[] = {
        {0x23, {{NULL, "sound/enemy17/wave07", "sound/enemy17/wave06"}}},
        {0x25, {{"sound/enemy11/wave05", NULL, NULL}}},
        {0x27, {{"sound/enemy16/wave05", "sound/enemy16/wave10", "sound/enemy16/wave11"}}},
        {0x28, {{"sound/enemy59/wave15", NULL, "sound/enemy59/wave14"}}},
        {0x2a, {{"sound/enemy12/wave04", NULL, NULL}}},
        {0x2b, {{"sound/enemy26/wave09", NULL, NULL}}},
        {0x2d, {{"sound/enemy42/wave18", "sound/enemy42/wave14", "sound/enemy42/wave15"}}},
        {0x2e, {{"sound/enemy13/wave08", "sound/enemy13/wave05", "sound/enemy13/wave11"}}},
        {0x2f, {{"sound/enemy14/wave04", NULL, "sound/enemy14/wave10"}}},
        {0x30, {{"sound/enemy20/wave04", "sound/enemy20/wave06", "sound/enemy20/wave02"}}},
        {0x37, {{"sound/enemy25/wave13", NULL, NULL}}},
        {0x39, {{"sound/enemy13/wave08", "sound/enemy13/wave05", "sound/enemy13/wave11"}}},
        {0x3b, {{"sound/enemy27/wave02", NULL, "sound/enemy27/wave03"}}},
    };
    for (unsigned i = 0; i < sizeof other_cues / sizeof other_cues[0]; ++i) {
        snprintf(id, sizeof id, "re2:enemy/em%02x", other_cues[i].kind);
        bad |= pe_imp_add_enemy_cues(id, &other_cues[i].cues) != 0;
    }
    return bad ? -1 : 0;
}
