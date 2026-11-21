#include "pe_imp_spu.h"
#include "pe_imp_enemy.h"
#include "pe_src_re1.h"

int pe_re1_enemy_sounds_register(void) {
    /* Curated cues from the first zombie room's VAB, shared by both pairs of early zombie models. */
    static const PeImpEnemyCues zombie = {{"sound/room102/wave05", "sound/room102/wave06", "sound/room102/wave07"}};
    return pe_imp_add_enemy_cues("re1:enemy/em1000", &zombie) |
           pe_imp_add_enemy_cues("re1:enemy/em1001", &zombie) |
           pe_imp_add_enemy_cues("re1:enemy/em1100", &zombie) |
           pe_imp_add_enemy_cues("re1:enemy/em1101", &zombie);
}

const PeImpReader g_re1_sound_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re1", .platform = "ps1", .format = "vab",
    .version = 1, .produces = PE_IMP_SOUND, .match = "PSXUSA/SOUND/*.HED",
    .tags = "sound sample", .companion = ".VB", .read_pair = pe_imp_vab_pair
};
