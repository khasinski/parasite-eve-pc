/* Source game: Resident Evil 2. Registers its readers and naming table with the importer; the
 * importer itself never names a game. Import mode only: nothing here runs during play. */
#include <stdio.h>

#include "pe_mod_host.h"
#include "pe_imp_enemy.h"
#include "pe_src_re2.h"

/* Built by themselves once their disc is in the import folder (F1 > Import). */
static const PeImpAuto k_autos[] = {
    {"re2:player/claire", "skins", "Resident Evil 2 disc 2 (Claire)"},
    {"re2:player/leon", "skins", "Resident Evil 2 disc 1 (Leon)"},
};

static int enemy_model(uint8_t kind, char *local, size_t n) {
    if (kind < 0x10 || kind >= 0x40) return -1;
    const int written = snprintf(local, n, "enemy/em%02x", kind);
    return written < 0 || (size_t)written >= n ? -1 : 0;
}

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

static int init(void) {
    for (unsigned i = 0; i < sizeof k_autos / sizeof k_autos[0]; i++) pe_imp_add_auto(&k_autos[i]);
    if (pe_imp_add_game("re2", "Resident Evil 2", "Resident Evil") || pe_imp_add_reader(&g_re2_pld_ps1) ||
        pe_imp_add_reader(&g_re2_sound_ps1) || pe_imp_add_reader(&g_re2_music_ps1) || pe_imp_add_reader(&g_re2_rooms_ps1) || pe_imp_add_reader(&g_re2_logic_ps1) || pe_imp_add_reader(&g_re2_door_ps1) || pe_imp_add_reader(&g_re2_items_ps1) || pe_imp_add_reader(&g_re2_item_pictures_ps1) || pe_imp_add_reader(&g_re2_movies_ps1) || pe_imp_add_reader(&g_re2_files_ps1) || pe_imp_add_reader(&g_re2_maps_ps1) || pe_imp_add_reader(&g_re2_objects_ps1) || pe_imp_add_reader(&g_re2_door_models_ps1) || pe_imp_add_reader(&g_re2_rbj_ps1) || pe_imp_add_reader(&g_re2_emd_ps1) || pe_imp_add_reader(&g_re2_player_moves_ps1) || pe_imp_add_reader(&g_re2_weapons_ps1) || pe_imp_add_reader(&g_re2_effects_ps1) || pe_imp_add_reader(&g_re2_core_effects_ps1) || pe_imp_add_reader(&g_re2_player_movies_ps1) ||
        pe_imp_set_naming("re2", g_re2_naming) || pe_imp_add_rig("re2", "player/", &g_re2_human_rig) ||
        pe_imp_set_enemy_defaults("re2", PE_RE_ENEMY_YAW, PE_RE_ENEMY_SCALE) ||
        pe_imp_set_enemy_models("re2", enemy_model)) {
        pe_mod_log("pe_src_re2", "readers refused by the importer");
        return 1;
    }
    for (unsigned i = 0; i < sizeof k_life / sizeof k_life[0]; i++) {
        char id[32];
        snprintf(id, sizeof id, "re2:enemy/em%02x", k_life[i].em);
        pe_imp_add_enemy_strength(id, k_life[i].life, 0);
    }
    return 0;
}

const PeMod pe_mod_pe_src_re2 = {"pe_src_re2", "Import source: Resident Evil 2", "0.1", NULL, init, NULL};
