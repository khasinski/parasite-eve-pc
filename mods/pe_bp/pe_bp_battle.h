#ifndef PE_BP_BATTLE_H
#define PE_BP_BATTLE_H
#include "bp_rules.h"
/* Registers the battle hooks; record() returns the current ranks. */
void pe_bp_battle_init(const char *mod_id, const BpRecord *(*record)(void));
#endif
