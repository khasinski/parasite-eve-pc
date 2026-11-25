/* Imported enemies in a picked fight (pe_enemy_battle.c), for the fight picker. */
#ifndef PE_ENEMY_BATTLE_H
#define PE_ENEMY_BATTLE_H
#include <stddef.h>
#include <stdint.h>
#include "../../pe_enemy_pack.h"

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
/* Roster enemy i starts dx, dz PE field units from Aya (where it stood in an imported room);
 * after begin_roster, before the battle. */
void pe_enemy_battle_set_start(int i, double dx, double dz);
/* Roster enemy i stands for this room slot (an imported room's battle, reported back by slot). */
void pe_enemy_battle_set_room_slot(int i, int slot);
/* A running battle takes in one more of the room's enemies: its pack (folder under the enemy packs),
 * at x, z (16.16 field units). The foe's index, or -1 (no battle, no place, no VRAM). */
int pe_enemy_battle_join(const char *pack, int room_slot, int x, int z);
typedef struct { int room_slot; double x, z; int alive; } PeEnemyBattleFoe;
/* Foes tied to room placements. alive: 1 live, 0 confirmed dead, -1 released during cleanup. */
int pe_enemy_battle_foe_slots(PeEnemyBattleFoe *out, int max);
/* D10: the targets in this order of room slots (on screen first, nearest first), others after. */
void pe_enemy_battle_order_targets(const uint8_t *order, int n);
/* The chosen target's position (16.16) while the player picks one: 0, or -1. */
int pe_enemy_battle_chosen_target(int *x, int *z);
int pe_enemy_battle_add_pack(const char *pack, const char *entrance, const char *room_move, int follow_forms);
void pe_enemy_battle_arm_summon(int parent, const char *offspring, unsigned move);
void pe_enemy_battle_arm_canister(void);
void pe_enemy_battle_arm_launcher(void);
int pe_enemy_battle_canister_detonated(void);
/* In a battle, and the imported enemy has taken the room's enemies' place (after the opening). */
int pe_enemy_battle_joined(void);
/* A battle right here, without a room battle: start_here arms it (mode: the battle-entry opcode's
 * argument, 2 for the field's random battles); here_step runs one field frame of the entry,
 * 1 once the battle is on, 0 meanwhile, -1 if it failed. Arm the enemies first. */
int pe_enemy_battle_start_here(int mode);
int pe_enemy_battle_here_step(void);
/* Defeated original roster positions, excluding enemies summoned during combat. */
unsigned pe_enemy_battle_defeated_mask(void);
void pe_enemy_battle_disarm(void);
void pe_enemy_status_text(char *out, unsigned size);
void pe_enemy_packs_changed(void);              /* any thread: list the packs again between fights */

#endif
