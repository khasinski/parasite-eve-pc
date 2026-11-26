#ifndef PE_BP_SETTINGS_H
#define PE_BP_SETTINGS_H
#include "bp_config.h"
unsigned pe_bp_settings_flags(void);
int pe_bp_settings_carry_mode(void);   /* BP_CARRY_* (bp_rules.h), a profile setting */
void pe_bp_menu_register(void);
#endif
