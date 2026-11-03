#ifndef PE_SAVE_EMISSION_H
#define PE_SAVE_EMISSION_H

#include "pe_save.h"

/* Opt-in conditional records, without changing PeSaveRecord or its existing callers.
 * Register during init. Descriptor/data have the same lifetime as pe_save_register.
 * Dormant records consume a bounded registry slot, but no payload/header save bytes and no
 * unconditional byte reservation. Existing pe_save_register records always retain their policy.
 * 0 success, -1 bad/duplicate/legacy-window record, full registry, or wrong lifecycle phase. */
int pe_save_register_conditional(const PeSaveRecord *record);

enum {
    PE_SAVE_EMISSION_OK = 0,
    PE_SAVE_EMISSION_BAD_ARGUMENT = -1,
    PE_SAVE_EMISSION_CAPACITY = -2,
    PE_SAVE_EMISSION_BUSY = -3,
    PE_SAVE_EMISSION_SOURCE_UNAVAILABLE = -4,
    PE_SAVE_EMISSION_INACTIVE = -5,
    PE_SAVE_EMISSION_NOT_CONDITIONAL = -6
};
/* Game-thread, owner-only operation outside save/load/reset/check/upgrade/release callbacks.
 * Use the EXACT registered descriptor. Must succeed BEFORE mutating meaningful owned state.
 * Checks the projected live+carried union without registration, callbacks, allocation or I/O.
 * Failure leaves data, emission, pending state, and destination untouched. Idempotent success
 * sets a sticky emission bit, not a reservation: later saves still recheck admission.
 * No disable/delete API: loaded/used records remain preserved, even at default values or when
 * returning to a legacy preset. New Game or loading an absent record clears the sticky bit.
 * Newer/invalid records and uncertain/no loaded source cannot be opted into silently. */
int pe_save_enable_record(const PeSaveRecord *record);

#endif
