#include "pe_imp_spu.h"

const PeImpReader g_re1_sound_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re1", .platform = "ps1", .format = "vab",
    .version = 1, .produces = PE_IMP_SOUND, .match = "PSXUSA/SOUND/*.HED",
    .tags = "sound sample", .companion = ".VB", .read_pair = pe_imp_vab_pair
};
