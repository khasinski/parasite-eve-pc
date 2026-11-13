/* The BP record a save holds: three companion sections ("pe_bp/ranks", "pe_bp/run",
 * "pe_bp/carry"), read on load, cleared by the host on a New Game, carried into an EX Game per
 * the F1 setting. The record is written only through bp_store_set, after every check passed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_bp_internal.h"
#include "pe_bp_settings.h"
#include "pe_extension_store.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe1/aya.h"

static const char *s_mod = "pe_bp";
static uint8_t s_ranks_bytes[BP_RANKS_BYTES], s_run_bytes[BP_RUN_BYTES], s_carry_bytes[BP_CARRY_BYTES];
static const PeExtensionSection s_ranks = {"pe_bp/ranks", 1, 1, BP_RANKS_BYTES, s_ranks_bytes};
static const PeExtensionSection s_run = {"pe_bp/run", 1, 1, BP_RUN_BYTES, s_run_bytes};
static const PeExtensionSection s_carry = {"pe_bp/carry", 1, 1, BP_CARRY_BYTES, s_carry_bytes};
static BpRecord s_rec;
static int s_damaged;          /* this save's sections failed their checks: kept, not written */
static int s_rec_valid;        /* s_rec holds a loaded save's ranks (an EX Game may carry them) */

const BpRecord *bp_store_record(void) { return &s_rec; }
int bp_store_readable(void) { return pe_game_loaded() && !s_damaged; }

static int on_save_read(void *ctx, void *user) {
    (void)ctx; (void)user;
    s_damaged = 0;
    memset(&s_rec, 0, sizeof s_rec);
    if (bp_record_decode(s_ranks_bytes, s_run_bytes, &s_rec) || bp_carry_decode(s_carry_bytes, &s_rec)) {
        memset(&s_rec, 0, sizeof s_rec);
        s_damaged = 1;
        pe_mod_log(s_mod, "save: BP sections refused (out of range); kept unchanged, purchases off");
    }
    s_rec_valid = !s_damaged;
#ifdef PE_BP_TEST_HOOKS
    if(getenv("PE_BP_SETTINGS_TEST")){printf("BP_SETTINGS_LOAD flags=%u bp=%u poison=%u heal=%u resets=%u damaged=%d\n",pe_bp_settings_flags(),(unsigned)g_AyaSaveState.bonus_points,s_rec.defense[0],s_rec.mastery[0],s_rec.full_resets,s_damaged);fflush(stdout);}
#endif
    bp_api_notify("load", bp_game_bp(), s_rec.carried_pool);
    return 0;
}
int bp_store_ready(void *user) {
    (void)user;
    if (s_damaged) return 0;
    return pe_extension_enable(&s_ranks) == PE_REG_OK && pe_extension_enable(&s_run) == PE_REG_OK &&
           pe_extension_enable(&s_carry) == PE_REG_OK;
}
void bp_store_set(const BpRecord *rec, void *user) {
    (void)user;
    s_rec = *rec;
    bp_record_encode(&s_rec, s_ranks_bytes, s_run_bytes);
    bp_carry_encode(&s_rec, s_carry_bytes);
}

/* The host has already cleared every section, for any new game. An EX Game starts from a loaded
 * Clear Data save, whose ranks are still in s_rec here: carry them per the F1 setting. A title New
 * Game or Tutorial always starts fresh. Unspent BP is the game's own (it keeps it in an EX Game). */
static int on_new_game(void *ctx, void *user) {
    const PeHookNewGame *ng = ctx;
    const int ex = ng && ng->kind == PE_NEW_GAME_EX, mode = pe_bp_settings_carry_mode();
    const BpRecord last = s_rec;
    BpRecord next;
    (void)user;
    memset(&s_rec, 0, sizeof s_rec);
    s_damaged = 0;
    if (!ex || !s_rec_valid || mode == BP_CARRY_OFF) {
        if (ex) pe_mod_log(s_mod, "EX Game: carryover %s; ranks start fresh", s_rec_valid ? "Off" : "skipped (no readable BP data)");
        s_rec_valid = 1;
        bp_api_notify("new_game", bp_game_bp(), last.carried_pool);
        return 0;
    }
    next = bp_ex_carry(&last, mode);
    if (!bp_record_equal(&next, &s_rec)) {
        if (!bp_store_ready(NULL)) {
            pe_mod_log(s_mod, "EX Game: BP sections unavailable; ranks start fresh");
            bp_api_notify("new_game", bp_game_bp(), last.carried_pool);
            return 0;
        }
        bp_store_set(&next, NULL);
    }
    pe_mod_log(s_mod, "EX Game: %s; ranks worth %u BP carried, upgrade-only pool %u BP",
               bp_carry_name(mode), bp_record_rank_value(&next, 0), next.carried_pool);
    if (mode == BP_CARRY_CONVERT && (unsigned long)last.carried_pool + bp_record_rank_value(&last, 0) > BP_MAX)
        pe_mod_log(s_mod, "EX Game: converted BP capped at %u", (unsigned)BP_MAX);
    s_rec_valid = 1;
    bp_api_notify("ex_carry", bp_game_bp(), last.carried_pool);
    return 0;
}

#ifdef PE_BP_TEST_HOOKS
/* Battle probe only (preview test build): set ranks without a purchase. Never in game builds. */
void pe_bp_test_set_record(const BpRecord *rec) { s_rec = *rec; }
const BpRecord *pe_bp_test_record(void) { return &s_rec; }
#endif

void bp_store_init(const char *mod_id) {
    s_mod = mod_id;
    if (pe_extension_register(&s_ranks) < 0 || pe_extension_register(&s_run) < 0 ||
        pe_extension_register(&s_carry) < 0)
        pe_mod_log(mod_id, "save sections not registered: rank purchases will be refused");
    pe_hook_add(PE_HOOK_SAVE_READ, on_save_read, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, on_new_game, NULL, PE_PRIORITY_NORMAL, mod_id);
}
