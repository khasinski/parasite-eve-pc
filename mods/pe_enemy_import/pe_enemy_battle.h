/* Imported enemies in a picked fight (pe_enemy_battle.c), for the fight picker. */
#ifndef PE_ENEMY_BATTLE_H
#define PE_ENEMY_BATTLE_H

void pe_enemy_battle_init(void);
unsigned pe_enemy_choice_count(void);          /* "None" + installed packs */
const char *pe_enemy_choice_name(unsigned i);
/* The next battle gets `count` (1..3) enemies of pack `choice` (0: none) as enemies of their own,
 * with the room's enemies sent away when send_away is set. */
void pe_enemy_battle_arm(int choice, int count, int send_away);
/* The same with the pack in folder `pack` (under pe_enemies: "re2-em31"), the list read again
 * first (a pack built a moment ago). 1 armed, 0 not there or refused. */
int pe_enemy_battle_arm_pack(const char *pack, int count, int send_away);
void pe_enemy_battle_disarm(void);
void pe_enemy_status_text(char *out, unsigned size);
void pe_enemy_packs_changed(void);              /* any thread: list the packs again between fights */

#endif
