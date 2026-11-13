#ifndef PE_BP_RULES_H
#define PE_BP_RULES_H
/* BP overhaul rules, all pure (no game globals, no I/O):
 *   bp_defs.h    tracks, lines, prices, battle numbers        (bp_defs.c)
 *   bp_record.h  ranks a save holds, save encoding, EX carry  (bp_record.c)
 *   bp_quote.h   snapshots, requests, quotes, the write plan  (bp_quote.c) */
#include "bp_defs.h"
#include "bp_record.h"
#include "bp_quote.h"
#endif
