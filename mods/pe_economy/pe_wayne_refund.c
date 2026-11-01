/* Wayne's failed 300-Junk craft refund, from the recomp's pe_wayne_refund.c.
 * With the Junk economy on, Super/Duper Junk already pay their bundle value;
 * this feature only refunds vanilla crafts while that economy is off.
 * Transient craft tracking resets on scene changes, loads and new games. */
#include "pe_economy.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe1/game_timers.h"

enum { PE_CRAFT_COST = 300 };
static int s_enabled; /* Off by default: Super/Duper Junk provide the partial refund. */
static int s_valid, s_armed;
static unsigned s_prev_counter, s_junk_before;

static unsigned pe_bad_roll_count(void) {
    unsigned n = 0;
    int i;
    for (i = 0; i < 50; ++i)
        n += g_AyaInventoryItems[i] == 0x0B || g_AyaInventoryItems[i] == 0x0C;
    for (i = 0; i < 100; ++i)
        n += g_WayneStorageItems[i] == 0x0B || g_WayneStorageItems[i] == 0x0C;
    return n;
}

static int pe_refund_reset(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    s_valid = s_armed = 0;
    return 0;
}

static void pe_refund_tick(void) {
    unsigned counter;
    if (!s_enabled || !pe_game_loaded() || pe_loot_enabled()) {
        s_valid = s_armed = 0;
        return;
    }
    counter = pe_wayne_count();
    if (!s_valid) {
        s_valid = 1;
        s_armed = 0;
    } else if (counter > s_prev_counter) {
        if (!s_armed) s_junk_before = pe_bad_roll_count();
        s_armed = 1;
    } else if (s_armed && s_prev_counter >= counter + PE_CRAFT_COST) {
        s_armed = 0;
        if (pe_bad_roll_count() > s_junk_before) {
            counter += PE_CRAFT_COST;
            pe_set_wayne_count(counter);
            pe_log("wayne_refund", "back=%u", (unsigned)PE_CRAFT_COST);
        }
    }
    s_prev_counter = counter;
}

void pe_wayne_refund_register(const char *mod_id) {
    pe_menu_register_toggle(PE_PAGE_ECONOMY, "Wayne junk refund",
        "Failed craft refunds 300 Junk when Junk economy is off.", &s_enabled, pe_refund_tick);
    pe_hook_add(PE_HOOK_SCENE_START, pe_refund_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_refund_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_refund_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_economy.h"
int pe_refund_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){if((!in&&!out)||(0))return 0;if(apply&&(!in||!pe_refund_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};
PE_CK_U(w,s_enabled,0,1,1);
PE_CK_U(w,s_valid,0,1,0);
PE_CK_U(w,s_armed,0,1,0);
PE_CK_U(w,s_prev_counter,0,UINT32_MAX,0);
PE_CK_U(w,s_junk_before,0,UINT32_MAX,0);

if(apply&&w.ok){}return w.ok&&w.at==20u;}
#endif
