#ifndef PE_SAVE_ADMISSION_H
#define PE_SAVE_ADMISSION_H

#include "pe_save.h"

/* Game-thread save preparation, not permission to activate new gameplay policies.
 * Current valid live/carried records must ALL fit. Existing corrupt-record recovery and
 * whole newer-container passthrough remain unchanged; queries expose those limitations. */
enum {
    PE_SAVE_WRITE_OK = 0,
    PE_SAVE_WRITE_BAD_ARGUMENT = -1,
    PE_SAVE_WRITE_CAPACITY = -2,
    PE_SAVE_WRITE_BUSY = -3
};
/* No allocation/callback/mutation. Recheck when writing; this reserves no capacity. */
int pe_save_check_write(void);
/* Stage the tail, publish and update carried state only on success. A refusal leaves both
 * destination and carried state unchanged. Caller MUST abort disk writing on a negative result.
 * No file is opened/replaced by this API. Not callable from a save-write callback. */
int pe_save_try_write(uint8_t *block);
/* Snapshot only: no owner callbacks, logging, allocation or carried-state mutation.
 * On refusal block is untouched and crash description reports the missing snapshot. */
int pe_save_try_crash_tail(uint8_t *block);

#endif
