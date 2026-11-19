/* Source game: Resident Evil 2. Registers its readers and naming table with the importer; the
 * importer itself never names a game. Import mode only: nothing here runs during play. */
#include "pe_mod_host.h"
#include "pe_src_re2.h"

/* Built by themselves once their disc is in the import folder (F1 > Import). */
static const PeImpAuto k_autos[] = {
    {"re2:player/claire", "skins", "Resident Evil 2 disc 2 (Claire)"},
    {"re2:player/leon", "skins", "Resident Evil 2 disc 1 (Leon)"},
};

static int init(void) {
    for (unsigned i = 0; i < sizeof k_autos / sizeof k_autos[0]; i++) pe_imp_add_auto(&k_autos[i]);
    if (pe_imp_add_game("re2", "Resident Evil 2", "Resident Evil") || pe_imp_add_reader(&g_re2_pld_ps1) ||
        pe_imp_add_reader(&g_re2_sound_ps1) || pe_imp_add_reader(&g_re2_music_ps1) || pe_imp_add_reader(&g_re2_rooms_ps1) || pe_imp_add_reader(&g_re2_logic_ps1) || pe_imp_add_reader(&g_re2_door_ps1) || pe_imp_add_reader(&g_re2_items_ps1) || pe_imp_add_reader(&g_re2_item_pictures_ps1) || pe_imp_add_reader(&g_re2_movies_ps1) || pe_imp_add_reader(&g_re2_files_ps1) || pe_imp_add_reader(&g_re2_maps_ps1) || pe_imp_add_reader(&g_re2_objects_ps1) || pe_imp_add_reader(&g_re2_door_models_ps1) || pe_imp_add_reader(&g_re2_rbj_ps1) || pe_imp_add_reader(&g_re2_emd_ps1) || pe_imp_add_reader(&g_re2_player_moves_ps1) || pe_imp_add_reader(&g_re2_weapons_ps1) || pe_imp_add_reader(&g_re2_effects_ps1) || pe_imp_add_reader(&g_re2_core_effects_ps1) || pe_imp_add_reader(&g_re2_player_movies_ps1) ||
        pe_imp_set_naming("re2", g_re2_naming) || pe_imp_add_rig("re2", "player/", &g_re2_human_rig)) {
        pe_mod_log("pe_src_re2", "readers refused by the importer");
        return 1;
    }
    return 0;
}

const PeMod pe_mod_pe_src_re2 = {"pe_src_re2", "Import source: Resident Evil 2", "0.1", NULL, init, NULL};
