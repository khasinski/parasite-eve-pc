/* Resident Evil 3's enemies as Parasite Eve fights them: profiles for the enemies builder
 * (pe_enemy_import). Moves are numbered across the EMD's three sets (set 1's first). Each enemy's
 * sounds are its own bank E_<nn> (the step words' top 4 bits name a tone, so at most 16 are used).
 * Only Nemesis (and Drain Deimos's swipe) mark the frame a blow lands (step flag 0x800); elsewhere
 * the builder takes the frame where the attack reaches furthest forward, or the profile names it
 * where that guess lands badly (the 4th number of an attack). Reach (the 3rd) is 0: the builder
 * measures how far in front the model gets at the blow frame. Moves picked by eye from
 * rendered strips (2026-11-19); sizes keep RE3's proportions next to Aya (about 500). */
#include "pe_src_re3.h"

/* Nemesis = EM34 (coat, normal form): m0 idle, m1 walk, m7-m10 the attacks (the only moves with hit
 * frames), m19 hurt, m21 death; footsteps on the walk, swings on the attacks. */
const PeImpEnemyProfile g_re3_nemesis = {
    .name = "Nemesis",
    .moves = "enemy/nemesis/moves",
    .sounds = "sound/e_34/tone", .sound_count = 16,
    .hp = 240, .exp = 60, .defense = 10,
    .height = 680,                            /* Aya is about 500 */
    .yaw = 3072,                              /* RE3 models face +x (Cerberus's head) */
    .anim = {{"idle", 0}, {"walk", 1}, {"hurt", 19}, {"die", 21}, {"punch", 7}, {"swing", 10}},
    .attack = {{"punch", 32, 0}, {"swing", 40, 0}},
    .speed = 9, .charge = 40, .radius = 300, .stagger = 1,
};

/* Zombie = EM10 (the man in the tan shirt; EM11-1F, 2E and 2F are other zombies with the same moves):
 * m0 idle, m3 arms-out shamble, m21 reach and grab, m22 lunge and bite, m12 recoil, m9 collapse. */
const PeImpEnemyProfile g_re3_zombie = {
    .name = "Zombie",
    .moves = "enemy/zombie/moves",
    .sounds = "sound/e_10/tone", .sound_count = 16,
    .hp = 90, .exp = 15, .defense = 4,
    .height = 560,
    .yaw = 3072,
    .anim = {{"idle", 0}, {"walk", 3}, {"hurt", 12}, {"die", 9}, {"grab", 21}, {"bite", 22}},
    .attack = {{"grab", 18, 0, 20}, {"bite", 22, 0, 30}},     /* arms furthest out, head down */
    .speed = 4, .charge = 60, .radius = 250, .stagger = 1,
};

/* Cerberus = EM20 (zombie dog): m0 idle, m1 walk, m15 and m20 leaping bites, m10 rears back,
 * m17 flips over. */
const PeImpEnemyProfile g_re3_cerberus = {
    .name = "Cerberus",
    .moves = "enemy/cerberus/moves",
    .sounds = "sound/e_20/tone", .sound_count = 12,
    .hp = 60, .exp = 12, .defense = 2,
    .height = 300,
    .yaw = 3072,
    .anim = {{"idle", 0}, {"walk", 1}, {"hurt", 10}, {"die", 17}, {"leap", 15}, {"lunge", 20}},
    .attack = {{"leap", 14, 0, 16}, {"lunge", 16, 0, 14}},    /* both bite mid-air (top of the jump) */
    .speed = 14, .charge = 30, .radius = 220, .stagger = 1,
};

/* Hunter beta = EM22: m17 idle (standing), m0 walk, m29 and m30 claw swipes, m7 recoil, m8 falls.
 * Its 0x800 flags sit on walks and leaps (m0, m2, m3), not on the swipes. */
const PeImpEnemyProfile g_re3_hunter = {
    .name = "Hunter",
    .moves = "enemy/hunter_beta/moves",
    .sounds = "sound/e_22/tone", .sound_count = 15,
    .hp = 160, .exp = 40, .defense = 8,
    .height = 560,
    .yaw = 3072,
    .anim = {{"idle", 17}, {"walk", 0}, {"hurt", 7}, {"die", 8}, {"swipe", 29}, {"slash", 30}},
    .attack = {{"swipe", 28, 0}, {"slash", 34, 0}},
    .speed = 11, .charge = 35, .radius = 280, .stagger = 1,
};

/* Giant spider = EM25: m0 idle, m2 walk, m10 rears up and strikes (its one attack: m9, which looks
 * like a lunge, only settles down from rearing), m8 flinch, m12 flips over (long: about 7 seconds). */
const PeImpEnemyProfile g_re3_spider = {
    .name = "Giant Spider",
    .moves = "enemy/spider/moves",
    .sounds = "sound/e_25/tone", .sound_count = 11,
    .hp = 120, .exp = 25, .defense = 6,
    .height = 250,
    .yaw = 3072,
    .anim = {{"idle", 0}, {"walk", 2}, {"hurt", 8}, {"die", 12}, {"strike", 10}},
    .attack = {{"strike", 22, 0, 30}},                          /* down from the top of the rear */
    .speed = 6, .charge = 45, .radius = 300, .stagger = 1,
};

/* Drain Deimos = EM28: m3 idle (crouched), m1 crawl, m11 swipe (marks its own hit frame), m29 rake,
 * m7 flinch, m18 collapses. */
const PeImpEnemyProfile g_re3_drain_deimos = {
    .name = "Drain Deimos",
    .moves = "enemy/drain_deimos/moves",
    .sounds = "sound/e_28/tone", .sound_count = 15,
    .hp = 140, .exp = 35, .defense = 6,
    .height = 320,
    .yaw = 3072,
    .anim = {{"idle", 3}, {"walk", 1}, {"hurt", 7}, {"die", 18}, {"swipe", 11}, {"rake", 29}},
    .attack = {{"swipe", 24, 0}, {"rake", 30, 0, 10}},        /* the rake: just after its wind-up (3-8) */
    .speed = 8, .charge = 40, .radius = 260, .stagger = 1,
};
