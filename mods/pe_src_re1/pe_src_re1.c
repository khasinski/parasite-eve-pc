/* Source game: Resident Evil (Director's Cut). Registers its reader, naming table and rig with
 * the importer. Import mode only: nothing here runs during play. */
#include "pe_mod_host.h"
#include "pe_src_re1.h"

/* Built by themselves once the disc is in the import folder (F1 > Import). Barry and Rebecca
 * are built by hand. */
static const PeImpAuto k_autos[] = {
    {"re1:player/chris", "skins", "Resident Evil disc (Chris)"},
    {"re1:player/jill", "skins", "Resident Evil disc (Jill)"},
};

static int init(void) {
    for (unsigned i = 0; i < sizeof k_autos / sizeof k_autos[0]; i++) pe_imp_add_auto(&k_autos[i]);
    if (pe_imp_add_game("re1", "Resident Evil", "Resident Evil") || pe_imp_add_reader(&g_re1_emd_ps1) ||
        pe_imp_add_reader(&g_re1_sound_ps1) ||
        pe_imp_set_naming("re1", g_re1_naming) || pe_imp_add_rig("re1", "player/", &g_re1_human_rig)) {
        pe_mod_log("pe_src_re1", "readers refused by the importer");
        return 1;
    }
    return 0;
}

const PeMod pe_mod_pe_src_re1 = {"pe_src_re1", "Import source: Resident Evil", "0.1", NULL, init, NULL};
