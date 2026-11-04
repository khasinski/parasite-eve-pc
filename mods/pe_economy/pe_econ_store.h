/* Owns the applied Economy settings and saves them as the pe_economy/policy companion section.
 * A save without the section stays on Enhanced and gets none until the player first leaves it. */
#ifndef PE_ECON_STORE_H
#define PE_ECON_STORE_H
#include "pe_econ_config.h"

typedef enum {
    PE_ECON_STORE_READY,
    PE_ECON_STORE_DAMAGED,      /* this save's section failed its checks: kept as it is, on Enhanced */
    PE_ECON_STORE_UNSUPPORTED,  /* its section is from a newer build, or its companion can't be used */
} PeEconStoreStatus;

void pe_econ_store_register(const char *mod_id);
const PeEconState *pe_econ_store_state(void);
PeEconStoreStatus pe_econ_store_status(void);
/* Refused: settings, progress and the section stay as they were. */
PeEconResult pe_econ_store_apply(const PeEconConfig *target);
/* The new path's settings, or NULL while Enhanced is active. */
const PeEconConfig *pe_econ_store_active(void);
/* The reward engine's result, in one step; crash reports copy it as it stands. */
void pe_econ_store_commit(const PeEconProgress *progress);

#ifdef PE_ECON_STORE_TEST
uint8_t *pe_econ_store_test_bytes(void);
#endif
#endif
