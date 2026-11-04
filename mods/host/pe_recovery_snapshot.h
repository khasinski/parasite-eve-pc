#ifndef PE_RECOVERY_SNAPSHOT_H
#define PE_RECOVERY_SNAPSHOT_H
#include "pe_hooks.h"
/* Private host call gate is in pe_mod_host.c. The immutable snapshot layout is public in pe_hooks.h. */
int pe_recovery_snapshot_capture(const unsigned char*native,unsigned size,PeModRecoverySnapshot*out);
#endif
