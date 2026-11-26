/* Source game: Resident Evil 3: Nemesis. Registers its reader, naming table and rig with the
 * importer. Import mode only: nothing here runs during play. */
#include "pe_mod_host.h"
#include "pe_src_re3.h"

/* Built by themselves once the disc is in the import folder (F1 > Import). */
static const PeImpAuto k_autos[] = {
    {"re3:player/jill", "skins", "Resident Evil 3 disc (Jill)"},
    {"re3:player/carlos", "skins", "Resident Evil 3 disc (Carlos)"},
    {"re3:enemy/nemesis", "enemies", "Resident Evil 3 PC folder (Nemesis)"},
    {"re3:enemy/zombie", "enemies", "Resident Evil 3 PC folder (Zombie)"},
    {"re3:enemy/cerberus", "enemies", "Resident Evil 3 PC folder (Cerberus)"},
    {"re3:enemy/hunter_beta", "enemies", "Resident Evil 3 PC folder (Hunter)"},
    {"re3:enemy/spider", "enemies", "Resident Evil 3 PC folder (Giant Spider)"},
    {"re3:enemy/drain_deimos", "enemies", "Resident Evil 3 PC folder (Drain Deimos)"},
};

/* RE3's enemies with a profile: asset local ID -> how it fights (pe_src_re3_enemies.c) */
static const struct { const char *id; const PeImpEnemyProfile *p; } k_enemies[] = {
    {"enemy/nemesis", &g_re3_nemesis},
    {"enemy/zombie", &g_re3_zombie},
    {"enemy/cerberus", &g_re3_cerberus},
    {"enemy/hunter_beta", &g_re3_hunter},
    {"enemy/spider", &g_re3_spider},
    {"enemy/drain_deimos", &g_re3_drain_deimos},
};

static int init(void) {
    for (unsigned i = 0; i < sizeof k_autos / sizeof k_autos[0]; i++) pe_imp_add_auto(&k_autos[i]);
    if (pe_imp_add_game("re3", "Resident Evil 3", "Resident Evil") || pe_imp_add_reader(&g_re3_pld_ps1) ||
        pe_imp_add_reader(&g_re3_sound_ps1) || pe_imp_add_reader(&g_re3_bgm_ps1) ||
        pe_imp_add_reader(&g_re3_room_music_ps1) || pe_imp_add_reader(&g_re3_rooms_ps1) || pe_imp_add_reader(&g_re3_logic_ps1) || pe_imp_add_reader(&g_re3_door_ps1) || pe_imp_add_reader(&g_re3_items_ps1) || pe_imp_add_reader(&g_re3_item_pictures_ps1) || pe_imp_add_reader(&g_re3_movies_ps1) || pe_imp_add_reader(&g_re3_files_ps1) || pe_imp_add_reader(&g_re3_maps_ps1) || pe_imp_add_reader(&g_re3_objects_ps1) || pe_imp_add_reader(&g_re3_door_models_ps1) ||
        pe_imp_set_naming("re3", g_re3_naming) || pe_imp_add_rig("re3", "player/", &g_re3_human_rig) ||
        pe_imp_add_reader(&g_re3_emd_pc) || pe_imp_add_reader(&g_re3_emd_ps1) || pe_imp_add_reader(&g_re3_room_anims_ps1) || pe_imp_add_reader(&g_re3_player_moves_ps1) || pe_imp_add_reader(&g_re3_weapons_ps1) || pe_imp_add_reader(&g_re3_effects_ps1) || pe_imp_add_reader(&g_re3_core_effects_ps1) || pe_imp_add_reader(&g_re3_tones_pc) ||
        pe_imp_set_enemy_defaults("re3", PE_RE_ENEMY_YAW, PE_RE_ENEMY_SCALE)) {
        pe_mod_log("pe_src_re3", "readers refused by the importer");
        return 1;
    }
    for (unsigned i = 0; i < sizeof k_enemies / sizeof k_enemies[0]; i++)
        if (pe_imp_add_enemy("re3", k_enemies[i].id, k_enemies[i].p)) {
            pe_mod_log("pe_src_re3", "enemy profiles refused by the importer");
            return 1;
        }
    return 0;
}

const PeMod pe_mod_pe_src_re3 = {"pe_src_re3", "Import source: Resident Evil 3", "0.1", NULL, init, NULL};
