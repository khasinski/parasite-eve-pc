/* Imported enemies in a picked fight (pe_enemy_battle.c), for the fight picker. */
#ifndef PE_ENEMY_BATTLE_H
#define PE_ENEMY_BATTLE_H
#include <stddef.h>
#include "pe_enemy_pack.h"

#define PE_ENEMY_PACKS_MAX 512

void pe_enemy_battle_init(void);
unsigned pe_enemy_choice_count(void);          /* "None" + installed packs */
const char *pe_enemy_choice_name(unsigned i);
int pe_enemy_battle_pack_path(int choice, char *out, size_t size);
/* Takes ownership of the prepared, in-memory pack on success. */
int pe_enemy_battle_preview(PeEnemyPack *pack);
/* The next battle gets `count` (1..3) enemies of pack `choice` (0: none) as enemies of their own,
 * with the room's enemies sent away when send_away is set. */
void pe_enemy_battle_arm(int choice, int count, int send_away);
/* Prepare a roster that replaces native foes. Each successful add corresponds to one source slot. */
void pe_enemy_battle_begin_roster(void);
int pe_enemy_battle_add_pack(const char *pack, const char *entrance, const char *room_move, int follow_forms);
void pe_enemy_battle_arm_summon(int parent, const char *offspring, unsigned move);
void pe_enemy_battle_arm_canister(void);
void pe_enemy_battle_arm_launcher(void);
int pe_enemy_battle_canister_detonated(void);
/* Defeated original roster positions, excluding enemies summoned during combat. */
unsigned pe_enemy_battle_defeated_mask(void);
void pe_enemy_battle_disarm(void);
void pe_enemy_status_text(char *out, unsigned size);
void pe_enemy_packs_changed(void);              /* any thread: list the packs again between fights */

#endif
