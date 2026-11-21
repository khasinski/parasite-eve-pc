#ifndef PE_IMP_ENEMY_H
#define PE_IMP_ENEMY_H
/* An enemy profile: how a source game's monster fights in Parasite Eve. Data owned by the source
 * game's mod (pe_src_re3 holds Nemesis's), the counterpart of a rig for costumes; the enemies
 * builder (pe_enemy_import) turns the asset's Model + Animation + sounds into an enemy pack. */
#include <stddef.h>
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
    /* power: the game's attack power; range: reach in PE field units (0: measured, how far in front
     * the model gets at the blow frame, at least its radius, plus Aya's half width); hit: the frame
     * the blow lands
     * (0: the move's first frame marked PE_IMP_EVENT_HIT, else where it reaches furthest forward) */
    struct { const char *anim; int power, range, hit; } attack[PE_IMP_ENEMY_ATTACKS];
    int speed, charge, radius, stagger;       /* as the pack's "behavior" (pe_enemy_pack.h) */
} PeImpEnemyProfile;

/* Assets "<game>:<local ID>" fight with this profile ("re3", "enemy/nemesis"): exact match. */
int pe_imp_add_enemy(const char *game, const char *local_id, const PeImpEnemyProfile *p);
const PeImpEnemyProfile *pe_imp_enemy_for(const char *asset_id);

/* A game's monsters that no profile names fight with a guessed one (pe_imp_guess.c); what the
 * guess can't see is the game's: `yaw` turns its models to face Aya, `scale` (per mille) takes its
 * units to PE field units. A game without these still builds (yaw 0, 500 units tall). */
int pe_imp_set_enemy_defaults(const char *game, int yaw, int scale);
int pe_imp_enemy_defaults(const char *game, int *yaw, int *scale);     /* 0, or -1: none set */

/* A monster's strength as its own game sets it, where that's known: `life` (its hit points) and,
 * optionally, `power` for its attacks (0: guessed). It overrides the guess's height rule (a story
 * boss isn't just a tall zombie); unknown models keep the guess. Assets "<game>:<local ID>"
 * ("re2:enemy/em31"); a variant ("re2:enemy/em31-cdemd2") takes its base model's. */
int pe_imp_add_enemy_strength(const char *asset_id, int life, int power);
int pe_imp_enemy_strength(const char *asset_id, int *life, int *power);   /* 0, or -1: not known */

/* Which model a room's enemy placement of `kind` is (the number the game's room scripts place it by,
 * RE2's enemy id): a game registers its own ("re2", 0x31, "enemy/em31"), so a fight started from an
 * imported room can bring that room's creature. `asset_id` gets "<game>:<local ID>". 0, or -1. */
int pe_imp_add_enemy_kind(const char *game, unsigned kind, const char *local_id);
int pe_imp_enemy_kind_asset(const char *game, unsigned kind, char *asset_id, size_t n);

/* Optional opening poses for a placed creature. A source reader owns the static table,
 * terminated by a NULL name. Move indices use the catalogued animation's order. */
typedef struct {
    const char *name;
    uint16_t mask, value;
    int move;
} PeImpEnemyStart;
int pe_imp_set_enemy_starts(const char *game, unsigned kind, const PeImpEnemyStart *starts);
const PeImpEnemyStart *pe_imp_enemy_starts(const char *asset_id);

/* The Resident Evil games' (1, 2 and 3 share the EMD's axes and units): their models face +x, and a
 * person about 2900 units tall stands about 560 next to Aya (about 500), as RE3's zombie does. */
#define PE_RE_ENEMY_YAW 3072
#define PE_RE_ENEMY_SCALE 190

#endif
