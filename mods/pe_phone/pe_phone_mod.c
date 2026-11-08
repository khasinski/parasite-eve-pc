/* Phone Calls: after Aya saves at a phone, she can stay on the line for a short call.
 * See pe_phone.h for the files; pe_phone_spot.c and pe_phone_flow.c for how it works in the game;
 * calls/pe1.txt for how calls are written. */
#include <stdio.h>

#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_phone.h"

#define PE_PHONE_PAGE "Phone Calls"
#define PE_PAGE_WRITE "Writing calls"

static int s_mode = PE_PHONE_ASK;
static int s_tone = PE_TONE_MIXED;
static int s_sounds = 1;
static int s_preview;                     /* 0: a call picked as usual; n: call n-1 */
static const char *const k_modes[] = {"Ask after saving", "Always call", "Off"};
static const char *const k_tones[] = {"Mixed", "Serious only", "Funny only"};

int pe_phone_mode(void) { return s_mode; }
int pe_phone_sounds_on(void) { return s_sounds; }

int pe_phone_tone_mask(void) {
    if (s_tone == PE_TONE_SERIOUS) return 1 << PE_CALL_SERIOUS;
    if (s_tone == PE_TONE_FUNNY) return 1 << PE_CALL_FUNNY;
    return (1 << PE_CALL_SERIOUS) | (1 << PE_CALL_FUNNY);
}

/* ---- F1 rows ---- */

static void pe_forget(void) {
    pe_phone_calls()->reset();
    pe_toast("Phone: this save's heard calls forgotten (kept at the next save)");
}

static void pe_info_last(char *out, unsigned size) { pe_phone_spot_status(out, size); }
static void pe_info_calls(char *out, unsigned size) {
    snprintf(out, size, "%d calls, %d problem(s), %u one-time heard", pe_phone_calls_count(),
             pe_phone_packs_problems(), pe_phone_heard_count());
}

static void pe_reload(void) {
    char msg[96];
    int problems;
    if (pe_phone_flow_busy()) { pe_toast("Phone: not during a call"); return; }
    problems = pe_phone_packs_load();
    s_preview = 0;
    if (problems) snprintf(msg, sizeof msg, "Phone: %d calls, %d problem(s): see pe_mods.log", pe_phone_calls_count(), problems);
    else snprintf(msg, sizeof msg, "Phone: %d calls loaded", pe_phone_calls_count());
    pe_toast(msg);
}

static void pe_open_folder(void) {
    if (!pe_phone_packs_open_folder()) pe_toast("Phone: couldn't open the pe_calls folder");
}

static unsigned pe_preview_count(void) { return (unsigned)pe_phone_calls_count() + 1u; }
static const char *pe_preview_name(unsigned i) {
    const PePhoneCall *c = i ? pe_phone_call_at((int)i - 1) : NULL;
    return i == 0 ? "Any (picked as usual)" : c ? c->key : "?";
}
static const char *pe_preview_key(void) {
    const PePhoneCall *c = s_preview ? pe_phone_call_at(s_preview - 1) : NULL;
    return c ? c->key : NULL;
}

static void pe_play_now(void) { pe_phone_flow_debug(PE_PHONE_ALWAYS, pe_preview_key()); }
static void pe_ask_now(void) { pe_phone_flow_debug(PE_PHONE_ASK, pe_preview_key()); }

static void pe_check(void) {
    const int bad = pe_phone_flow_check_calls();
    char msg[80];
    if (bad < 0) snprintf(msg, sizeof msg, "Phone: not now (no font yet, or a call is up)");
    else snprintf(msg, sizeof msg, "Phone: %d call(s) need a look (pe_mods.log)", bad);
    pe_toast(msg);
}

static int pe_phone_init(void) {
    pe_phone_service_register(PE_PHONE_MOD_ID);
    (void)pe_phone_packs_load();
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
    pe_menu_register_info(0, "Calls", "Calls loaded, mistakes in the call files, one-time calls this save heard.", pe_info_calls);

    pe_menu_page(PE_PAGE_WRITE, "Your own calls: edit the text files in pe_calls, reload, try one.", 80);
    pe_menu_register_action(PE_PAGE_WRITE, "Open calls folder",
                            "The pe_calls folder beside your preferences: pe1.txt is the game's calls, add your own .txt.",
                            pe_open_folder);
    pe_menu_register_action(PE_PAGE_WRITE, "Reload calls", "Read the call files again (no restart). Mistakes: pe_mods.log.",
                            pe_reload);
    pe_menu_register_dynamic_choice(PE_PAGE_WRITE, "Try call", "The call Play it now plays (Any: picked as usual).",
                                    &s_preview, 256, pe_preview_count, pe_preview_name);
    pe_menu_register_action(PE_PAGE_WRITE, "Play it now", "At the next free field moment, without saving: the call chosen above.",
                            pe_play_now);
    pe_menu_register_action(PE_PAGE_WRITE, "Ask now", "At the next free field moment: \"Stay on the line?\", then the call chosen above.",
                            pe_ask_now);
    pe_menu_register_action(PE_PAGE_WRITE, "Check calls", "Measure every call with the game's font; problems go to pe_mods.log.",
                            pe_check);
    pe_menu_begin(0);
    return 0;
}

const PeMod pe_mod_pe_phone = {PE_PHONE_MOD_ID, "Phone Calls", "0.2", NULL, pe_phone_init, NULL};
