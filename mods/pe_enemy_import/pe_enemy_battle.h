/* Imported enemies in a picked fight (pe_enemy_battle.c), for the fight picker. */
#ifndef PE_ENEMY_BATTLE_H
#define PE_ENEMY_BATTLE_H

void pe_enemy_battle_init(void);
unsigned pe_enemy_choice_count(void);          /* "None" + installed packs */
const char *pe_enemy_choice_name(unsigned i);
/* The next battle gets `count` (1..3) enemies of pack `choice` (0: none) as enemies of their own,
 * with the room's enemies sent away when send_away is set. */
void pe_enemy_battle_arm(int choice, int count, int send_away);
/* Prepare a roster that replaces native foes. Each successful add corresponds to one source slot. */
void pe_enemy_battle_begin_roster(void);
int pe_enemy_battle_add_pack(const char *pack, const char *entrance);
/* Number of room foes that spawned, if all of them died. */
int pe_enemy_battle_room_defeated(void);
void pe_enemy_battle_disarm(void);
void pe_enemy_status_text(char *out, unsigned size);
void pe_enemy_packs_changed(void);              /* any thread: list the packs again between fights */

#endif
