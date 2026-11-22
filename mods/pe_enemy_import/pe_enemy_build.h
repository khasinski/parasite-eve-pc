#ifndef PE_ENEMY_BUILD_H
#define PE_ENEMY_BUILD_H
/* The enemies builder (pe_enemy_build.c) and its importer target (pe_enemy_target.c). */
#include <stddef.h>

#include "pe_enemy_pack.h"
#include "pe_imp.h"
#include "pe_imp_enemy.h"

typedef struct {
    double scale;                             /* source units -> PE field units */
    int skipped;                              /* triangles left out (texture pages) */
    int sounds, sound_events;
} PeEnemyBuildInfo;

/* The pack in memory: the model (rest pose, floor at y 0), the profile's moves with their events,
 * hit frames, sounds (sounds[k] = sound number k; NULL entries stay silent). `source` is written
 * as where it came from. 0, or -1 with a reason (p freed). */
int pe_enemy_build(const PeImpModel *m, const PeImpAnimation *a, const PeImpEnemyProfile *prof,
                   PeImpSound *const *sounds, int sound_count, const char *source, PeEnemyPack *p,
                   PeEnemyBuildInfo *info, char *why, size_t n);
/* Only the model (rest pose, source units, no anims): for posing moves while looking at them
 * (pe_imp_guess.c). 0, or -1 with a reason (p freed). */
int pe_enemy_build_model(PeEnemyPack *p, const PeImpModel *m, int *skipped, char *why, size_t n);
/* Its files into `folder` (created): what pe_enemy_pack_load reads back. */
int pe_enemy_write(const PeEnemyPack *p, const char *folder, char *why, size_t n);

/* The pack folder (under pe_enemies) an asset builds into: "re2:enemy/em31" -> "re2-em31". 0 or -1. */
int pe_enemy_pack_name(const char *asset_id, char *out, size_t n);
/* The "enemies" target for F1 > Import ("Build as an enemy"). */
int pe_enemy_target_register(void);
int pe_enemy_add_room_move(PeEnemyPack *pack, const PeImpAnimation *moves, unsigned move, const char *name);
extern const PeImpTarget g_pe_enemy_target;

#endif
