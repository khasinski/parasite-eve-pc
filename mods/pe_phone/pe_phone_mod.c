/* Phone Calls: after Aya saves at a phone, she can stay on the line for a short call.
 * See pe_phone.h for the files; pe_phone_spot.c and pe_phone_flow.c for how it works in the game. */
#include <stdio.h>

#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_phone.h"

#define PE_PHONE_PAGE "Phone Calls"

static int s_mode = PE_PHONE_ASK;
static int s_tone = PE_TONE_MIXED;
static const char *const k_modes[] = {"Ask after saving", "Always call", "Off"};
static const char *const k_tones[] = {"Mixed", "Serious only", "Funny only"};

static int s_sounds = 1;

int pe_phone_mode(void) { return s_mode; }
int pe_phone_sounds_on(void) { return s_sounds; }

int pe_phone_tone_mask(void) {
    if (s_tone == PE_TONE_SERIOUS) return 1 << PE_CALL_SERIOUS;
    if (s_tone == PE_TONE_FUNNY) return 1 << PE_CALL_FUNNY;
    return (1 << PE_CALL_SERIOUS) | (1 << PE_CALL_FUNNY);
}

static void pe_forget(void) {
    pe_phone_calls()->reset();
    pe_toast("Phone: this save's heard calls forgotten (kept at the next save)");
}

static void pe_info_last(char *out, unsigned size) { pe_phone_spot_status(out, size); }
static void pe_debug_ask(void) { pe_phone_flow_debug(PE_PHONE_ASK); }
static void pe_debug_call(void) { pe_phone_flow_debug(PE_PHONE_ALWAYS); }
static void pe_info_calls(char *out, unsigned size) {
    snprintf(out, size, "%d registered, %u one-time heard", pe_phone_calls_count(), pe_phone_heard_count());
}
static void pe_check(void) {
    const int bad = pe_phone_flow_check_calls();
    char msg[80];
    if (bad < 0) snprintf(msg, sizeof msg, "Phone: not now (no font yet, or a call is up)");
    else snprintf(msg, sizeof msg, "Phone: %d call(s) need a look (pe_mods.log)", bad);
    pe_toast(msg);
}

static int pe_phone_init(void) {
    pe_phone_service_register(PE_PHONE_MOD_ID);
    pe_calls_pe1_register(pe_phone_calls());
    pe_phone_spot_register(PE_PHONE_MOD_ID);
    pe_phone_flow_register(PE_PHONE_MOD_ID);
    pe_phone_hold_register(PE_PHONE_MOD_ID);

    pe_menu_begin(PE_PHONE_PAGE);
    pe_menu_top_page(PE_PHONE_PAGE, "After saving at a phone, Aya can stay on the line for a short call.", 50);
    pe_menu_register_choice(0, "Phone calls",
                            "Ask after saving: \"Stay on the line?\" (No first). Always call: no question. Off: nothing.",
                            &s_mode, k_modes, 3);
    pe_menu_register_choice(0, "Call tone", "Which calls can come up: the sensible ones, the silly ones, or both.",
                            &s_tone, k_tones, 3);
    pe_menu_register_toggle(0, "Phone sounds", "The phone's own sounds: picking up and dialling, then putting it down.",
                            &s_sounds, NULL);
    pe_menu_register_action(0, "Forget heard calls",
                            "This save forgets the calls it heard: one-time calls can come back.", pe_forget);
    pe_menu_register_info(0, "Last phone", "The last save phone seen: room, story value and day.", pe_info_last);
    pe_menu_register_info(0, "Calls", "Calls registered by every pack, and one-time calls this save heard.", pe_info_calls);
    pe_menu_page("Debug", "Test rows: ask or call without saving, check the calls fit the boxes.", 90);
    pe_menu_register_action("Debug", "Ask now", "At the next free field moment: \"Stay on the line?\" as after a save.",
                            pe_debug_ask);
    pe_menu_register_action("Debug", "Call now", "At the next free field moment: a call, as after a save.",
                            pe_debug_call);
    pe_menu_register_action("Debug", "Check calls", "Measure every call with the game's font; problems go to pe_mods.log.",
                            pe_check);
    pe_menu_begin(0);
    pe_mod_log(PE_PHONE_MOD_ID, "%d calls registered", pe_phone_calls_count());
    return 0;
}

const PeMod pe_mod_pe_phone = {PE_PHONE_MOD_ID, "Phone Calls", "0.1", NULL, pe_phone_init, NULL};
