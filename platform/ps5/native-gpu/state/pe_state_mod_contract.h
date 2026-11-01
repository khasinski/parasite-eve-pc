#ifndef PE_STATE_MOD_CONTRACT_H
#define PE_STATE_MOD_CONTRACT_H
#include <stdint.h>
#include <stddef.h>
#define PE_MOD_STATE_MAGIC "PEMOD005"
#define PE_MOD_STATE_BYTES 10272u
int pe_mod_state_walk(const unsigned char *,unsigned char *,size_t,int);
/* Readonly exact supported-mod identity; no handler callbacks dispatched. */
int pe_mod_state_options(uint32_t [4]);
void pe_mod_state_after_restore(void);
const char *pe_mod_state_blocker(void);
#endif
