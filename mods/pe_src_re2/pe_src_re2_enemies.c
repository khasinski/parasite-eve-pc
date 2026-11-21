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
    {0}
};

int pe_re2_enemies_register(void) {
    int bad = 0;
    char id[32];
    /* A placement's kind is the enemy's id, and its model is "enemy/em<id>" in the main archive
     * (pe_src_re2_emd.c); 0x40 and up are people (Sherry, Ada, Annette...): not fought. */
    for (unsigned kind = 0x10; kind < 0x40; kind++) {
        snprintf(id, sizeof id, "enemy/em%02x", kind);
        bad |= pe_imp_add_enemy_kind("re2", kind, id) != 0;
        if (kind < 0x20) bad |= pe_imp_set_enemy_starts("re2", kind, k_zombie_starts) != 0;
    }
    for (unsigned i = 0; i < sizeof k_life / sizeof k_life[0]; i++) {
        snprintf(id, sizeof id, "re2:enemy/em%02x", k_life[i].em);
        bad |= pe_imp_add_enemy_strength(id, k_life[i].life, 0) != 0;
    }
    /* bio2-ds EM21/22/24/29/2F/3A: locomotion and EM2F's attached plant part. */
    static const struct { unsigned kind; PeImpEnemyBody body; } bodies[] = {
        {0x21, PE_IMP_ENEMY_FLYER}, {0x3a, PE_IMP_ENEMY_FLYER},
        {0x22, PE_IMP_ENEMY_CRAWLER}, {0x24, PE_IMP_ENEMY_CRAWLER}, {0x29, PE_IMP_ENEMY_CRAWLER},
        {0x2f, PE_IMP_ENEMY_FIXED}
    };
    for (unsigned i = 0; i < sizeof bodies / sizeof bodies[0]; i++)
        bad |= pe_imp_set_enemy_body("re2", bodies[i].kind, bodies[i].body) != 0;
    return bad ? -1 : 0;
}
