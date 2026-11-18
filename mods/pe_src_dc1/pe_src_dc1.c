/* Source game: Dino Crisis. Registers its reader, naming table and rig with the importer. Import
 * mode only: nothing here runs during play. */
#include "pe_mod_host.h"
#include "pe_src_dc1.h"

/* Built by itself once the disc is in the import folder (F1 > Import). Her other outfits are
 * built by hand. */
static const PeImpAuto k_autos[] = {
    {"dc1:player/regina", "skins", "Dino Crisis disc (Regina)"},
};

static int init(void) {
    for (unsigned i = 0; i < sizeof k_autos / sizeof k_autos[0]; i++) pe_imp_add_auto(&k_autos[i]);
    /* Dino Crisis 2 has the same CORE??.DAT names: the disc is told apart by its executable */
    if (pe_imp_add_game("dc1", "Dino Crisis", "Dino Crisis") || pe_imp_add_disc_id("dc1", "SLUS_009.22") ||
        pe_imp_add_disc_id("dc1", "SLES_022.08") || pe_imp_add_disc_id("dc1", "SLPS_021.80") || pe_imp_add_reader(&g_dc1_core_ps1) ||
        pe_imp_add_reader(&g_dc1_sound_ps1) || pe_imp_add_reader(&g_dc1_music_ps1) ||
        pe_imp_set_naming("dc1", g_dc1_naming) || pe_imp_add_rig("dc1", "player/", &g_dc1_human_rig)) {
        pe_mod_log("pe_src_dc1", "readers refused by the importer");
        return 1;
    }
    return 0;
}

const PeMod pe_mod_pe_src_dc1 = {"pe_src_dc1", "Import source: Dino Crisis", "0.1", NULL, init, NULL};
