#ifndef PE_SAVE_RECOVERY_H
#define PE_SAVE_RECOVERY_H
#include <stdint.h>
/* Patch only the caller's already-packed supported tail. Never changes owner/carried state. */
int pe_save_recovery_patch(uint8_t *block,const char *name,unsigned version,const void *bytes,unsigned size);
#endif
