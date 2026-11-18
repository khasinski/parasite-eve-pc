#include "pe_imp_spu.h"

const PeImpReader g_re2_sound_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "vab",
    .version = 1, .produces = PE_IMP_SOUND, .match = "COMMON/SOUND/*.EDH",
    .tags = "sound sample", .companion = ".VB", .read_pair = pe_imp_vab_pair
};
