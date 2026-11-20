#ifndef PE_IMP_ENEMY_H
#define PE_IMP_ENEMY_H
/* An enemy profile: how a source game's monster fights in Parasite Eve. Data owned by the source
 * game's mod (pe_src_re3 holds Nemesis's), the counterpart of a rig for costumes; the enemies
 * builder (pe_enemy_import) turns the asset's Model + Animation + sounds into an enemy pack. */
#include <stdint.h>

#define PE_IMP_ENEMY_ANIMS 8
#define PE_IMP_ENEMY_ATTACKS 4

typedef struct PeImpEnemyProfile {
    const char *name;                         /* "Nemesis" */
    const char *moves;                        /* the Animation's local ID ("enemy/nemesis/moves") */
    const char *sounds;                       /* sound IDs: this followed by the number (two digits,
                                               * "sound/e_34/tone"), NULL: none */
    int sound_count;
    int hp, exp, defense;
    int height;                               /* floor to the top of the head, PE field units */
    int yaw;                                  /* turn so +z faces Aya (4096 = a full turn) */
    /* roles idle, walk, hurt, die and every attack's own name -> a move of the Animation */
    struct { const char *name; int move; } anim[PE_IMP_ENEMY_ANIMS];
    /* power: the game's attack power; range: reach in PE field units; the blow lands on the move's
     * first frame marked PE_IMP_EVENT_HIT (else half way) */
    struct { const char *anim; int power, range; } attack[PE_IMP_ENEMY_ATTACKS];
    int speed, charge, radius, stagger;       /* as the pack's "behavior" (pe_enemy_pack.h) */
} PeImpEnemyProfile;

/* Assets "<game>:<local ID>" fight with this profile ("re3", "enemy/nemesis"): exact match. */
int pe_imp_add_enemy(const char *game, const char *local_id, const PeImpEnemyProfile *p);
const PeImpEnemyProfile *pe_imp_enemy_for(const char *asset_id);

#endif
