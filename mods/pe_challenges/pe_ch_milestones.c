/* Challenges: the moments a run's history is written.
 *
 *   A save       every real save (the card menu's, a quick save: both build the card file in
 *                g_MemCardFileBuffer) refreshes the run's line in the history with its numbers now.
 *                The crash reporter's room snapshot runs the same hook into a block of its own; it
 *                is skipped, so rooms don't write the file.
 *   Game cleared the save the game offers after the credits is a Clear Data save: the card menu
 *                sets g_SaveTitleStyleFlag from its clear prompt (g_MenuMemCardConfirmPending) just
 *                before writing it. Both set at a real save: the run is marked cleared and
 *                "challenges.run_cleared" is announced. A Clear Data save loaded at the title also
 *                sets the flag, without the prompt, so the prompt is required too.
 *   Fallen       written by pe_ch_hardcore.c at the game over.
 * The save record itself is untouched here; nothing is added to the card.
 */
#include "pe_challenges.h"
#include "pe_mod_host.h"
#include "pe1/memcard.h"
#include "pe1/menu_flags.h"
#include "pe1/save.h"

static int pe_is_clear_save(void) {
    return g_SaveTitleStyleFlag != 0 && g_MenuMemCardConfirmPending != 0;
}

static int pe_milestones_save(void *ctx, void *user) {
    const unsigned char *block = ctx;
    (void)user;
    if (block != g_MemCardFileBuffer || !g_ch_run.ever) return 0;   /* not a real save, or no challenge run */
    if (pe_is_clear_save()) {
        const PeChRunEntry *before = pe_ch_history_find(g_ch_run.run_id);
        const int first = !before || before->state != CH_RUN_CLEARED;
        pe_ch_history_note(CH_RUN_CLEARED);
        if (first) {
            pe_log("challenge_cleared", "run=%08x active=%x assisted=%d battles=%u seconds=%u",
                   (unsigned)g_ch_run.run_id, (unsigned)g_ch_run.active, pe_ch_assisted(),
                   (unsigned)g_ch_run.battles, (unsigned)g_ch_run.seconds);
            pe_toast(pe_ch_assisted() ? "Challenge run cleared (assisted)" : "Challenge run cleared!");
            pe_ch_emit_cleared(g_ch_run.run_id);
        }
    } else {
        pe_ch_history_note(CH_RUN_PLAYING);
    }
    return 0;
}

void pe_ch_milestones_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_SAVE_WRITE, pe_milestones_save, NULL, PE_PRIORITY_LATE, mod_id);
}
