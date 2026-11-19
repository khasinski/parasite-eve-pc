/* Resident Evil 3's enemies as Parasite Eve fights them: profiles for the enemies builder
 * (pe_enemy_import). Nemesis = EM34 (coat, normal form). His moves (set 1): m0 idle, m1 walk,
 * m7-m10 the attacks (the only moves with hit frames: step flag 0x800), m19 hurt, m21 death; his
 * sounds are E_34's 16 tones (footsteps on the walk, swings on the attacks). */
#include "pe_src_re3.h"

const PeImpEnemyProfile g_re3_nemesis = {
    .name = "Nemesis",
    .moves = "enemy/nemesis/moves",
    .sounds = "sound/e_34/tone", .sound_count = 16,
    .hp = 240, .exp = 60, .defense = 10,
    .height = 680,                            /* Aya is about 500 */
    .yaw = 2048,                              /* RE3 models face -z */
    .anim = {{"idle", 0}, {"walk", 1}, {"hurt", 19}, {"die", 21}, {"punch", 7}, {"swing", 10}},
    .attack = {{"punch", 32, 900}, {"swing", 40, 900}},
    .speed = 9, .charge = 40, .radius = 300, .stagger = 1,
};
