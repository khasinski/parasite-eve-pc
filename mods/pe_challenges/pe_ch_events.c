/* Challenges: what the mod announces to other mods, through host events (pe_event_id /
 * pe_event_emit). A listener needs no header of this mod: names and payloads are plain.
 *
 *   "challenges.changed"    ctx: const uint32_t *  the challenges on now, one bit per id
 *                           (ids are stable: see the README's table). After a load, a New Game
 *                           and every change of the rows that reached the run.
 *   "challenges.assisted"   ctx: const char *      what marked the run assisted ("God mode",
 *                           "Warp", "lowered: No PE"). Once per run, when it first happens.
 *   "challenges.run_ended"  ctx: const uint32_t *  the run id of a Hardcore run that just ended
 *                           in a game over (already written to the per-run file).
 *   "challenges.run_cleared" ctx: const uint32_t * the run id of a challenge run whose Clear Data
 *                           save (after the credits) is being written. Once per run.
 *
 * Events, not a service, until a "challenges" service table can go into host/pe_services.h,
 * which the PS5 port pins by checksum.
 */
#include "pe_challenges.h"
#include "pe_mod_host.h"

static int s_changed = -1, s_assisted = -1, s_ended = -1, s_cleared = -1;

void pe_ch_events_register(void) {
    s_changed = pe_event_id(CH_EVENT_CHANGED);
    s_assisted = pe_event_id(CH_EVENT_ASSISTED);
    s_ended = pe_event_id(CH_EVENT_RUN_ENDED);
    s_cleared = pe_event_id(CH_EVENT_CLEARED);
}

void pe_ch_emit_cleared(uint32_t run_id) {
    if (s_cleared >= 0) pe_event_emit(s_cleared, &run_id);
}

void pe_ch_emit_changed(void) {
    uint32_t active = g_ch_run.active;
    if (s_changed >= 0) pe_event_emit(s_changed, &active);
}

void pe_ch_emit_assisted(const char *what) {
    if (s_assisted >= 0) pe_event_emit(s_assisted, (void *)what);
}

void pe_ch_emit_run_ended(uint32_t run_id) {
    if (s_ended >= 0) pe_event_emit(s_ended, &run_id);
}
