#ifndef PE_SAVE_DISPATCH_H
#define PE_SAVE_DISPATCH_H

/* Host-private save dispatch guard. Call begin before owner hooks and finish on their return.
 * finish always releases the guard. Direct/nested writes and new registrations are refused
 * while hooks run. The existing game-thread ownership contract still applies. */
#include "pe_save_admission.h"
int pe_save_begin_write(void);
void pe_save_abort_write(void);
int pe_save_finish_write(uint8_t *block);
void pe_save_report_refusal(int result);

#endif
