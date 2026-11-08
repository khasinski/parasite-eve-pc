/* After a phone save: "Stay on the line?" and the call.
 *
 * Once the spotter (pe_phone_spot.c) has a committed phone save and the field has been settled for
 * a few frames (free field, no box, no menu), the field is locked and the question shows with No
 * first, so one Cross goes straight back to the game; Circle is No too. Yes (or the "Always call"
 * setting) plays a call: the service picks it, the pure core lays it out as pages with the game's
 * font, and the box module shows them one at a time. Cross goes on, Circle hangs up (straight to
 * "*click*"). The ring page plays the phone's own pick-up and dial sounds, "*click*" its put-down
 * sounds (pe_phone_sound.c). The field stays locked and the pad ours until the last box closes. */
#include <string.h>

#include "pe_game.h"
#include "pe_gametext.h"
#include "pe_mod_host.h"
#include "pe_phone.h"
#include "pe1/game_state.h"
#include "pe1/scene_dispatch.h"

#define PE_ASK_FREE 8   /* settled frames after the save before asking */

enum { ST_IDLE, ST_ASK, ST_CALL };

static int s_state = ST_IDLE;
static int s_token = -1;
static int s_wait;
static int s_force;                    /* debug: PE_PHONE_ASK or PE_PHONE_ALWAYS whatever the setting, or -1 */
static char s_force_key[64];           /* debug: play this call ("" picks as usual) */
static PePhoneSpot s_spot;
static const PePhoneCall *s_call;
static int s_page, s_hung_up, s_checked;
static int s_ev_start = -1, s_ev_end = -1;
static PePhoneScript s_script;
static uint8_t s_question[96];

static int pe_busy(void) { return s_state == ST_ASK || s_state == ST_CALL; }

static void pe_to_idle(void) {
    s_state = ST_IDLE;
    s_wait = 0;
    s_force = -1;
    s_force_key[0] = 0;
    s_call = NULL;
    pe_phone_spot_quiet(0);
}

static void pe_reset(const char *why) {
    if (pe_busy()) pe_mod_log(PE_PHONE_MOD_ID, "call ended: %s", why);
    pe_phone_sound_stop();
    pe_phone_hold_forget();
    pe_phone_box_forget();
    pe_to_idle();
}

static void pe_end(const char *how) {
    pe_phone_box_close();
    pe_phone_hold_release();                              /* No, or a box gone: she hangs up now */
    pe_phone_unlock();
    if (s_state == ST_CALL && s_call) {
        pe_phone_calls_played(s_call);
        pe_event_emit(s_ev_end, (void *)s_call);
    }
    pe_log("phone_call", "end=%s key=%s", how, s_call ? s_call->key : "-");
    pe_to_idle();
}

static int pe_show_page(int index) {
    const PePhonePage *p = &s_script.pages[index];
    s_page = index;
    if (index == 0) pe_phone_sound_play(PE_PHONE_SOUND_DIAL, s_spot.room, pe_phone_hold_active());
    else if (index == s_script.click) {                   /* she puts the phone down */
        const int held = pe_phone_hold_active();
        pe_phone_hold_release();
        pe_phone_sound_play(PE_PHONE_SOUND_HANG_UP, s_spot.room, held);
    } else pe_phone_sound_stop();                         /* picked up: no more ringing */
    return pe_phone_box_show(p->text, p->style, p->lines, p->width);
}

static int pe_show_question(void) {
    PeText t;
    pe_t_init(&t);
    pe_t_str(&t, "Stay on the line?");
    pe_t_byte(&t, G_NEWLINE);
    pe_t_choice(&t, 2);
    pe_t_option(&t, 1); pe_t_str(&t, "No");
    pe_t_option(&t, 0); pe_t_str(&t, "Yes");
    pe_t_byte(&t, G_END);
    memcpy(s_question, t.b, t.n < sizeof s_question ? t.n : sizeof s_question);
    return pe_phone_box_show(s_question, PE_PHONE_BAND, 3, 0);
}

static void pe_start_call(void) {
    PePhoneContext x;
    x.room = s_spot.room;
    x.story = s_spot.story;
    x.day = pe_phone_story_day(s_spot.story);
    x.tone_mask = pe_phone_tone_mask();
    s_call = s_force_key[0] ? pe_phone_calls()->find(s_force_key) : pe_phone_calls()->pick(&x);
    pe_phone_script_build(&s_script, s_call, pe_phone_font_advance);
    s_hung_up = 0;
    if (!pe_show_page(0)) { pe_end("no box free"); return; }
    s_state = ST_CALL;
    if (s_call) pe_event_emit(s_ev_start, (void *)s_call);
    pe_log("phone_call", "key=%s room=%d story=0x%X day=%d boxes=%d", s_call ? s_call->key : "-",
           x.room, x.story, x.day, s_script.count);
}

static void pe_begin(void) {
    const int mode = s_force >= 0 ? s_force : pe_phone_mode();
    pe_phone_spot_clear();
    if (mode == PE_PHONE_OFF || !pe_player()) { pe_phone_hold_release(); pe_to_idle(); return; }
    pe_phone_spot_quiet(1);
    if (!pe_phone_hold_active()) {                        /* held: the phone's own lock holds Aya */
        pe_phone_lock();
        (void)pe_phone_hold_pose(s_spot.room);            /* a phone that let her go: its pose again */
    }
    if (mode == PE_PHONE_ALWAYS) { pe_start_call(); return; }
    if (!pe_show_question()) { pe_end("no box free"); return; }
    s_state = ST_ASK;
}

static void pe_step_ask(void) {
    const uint32_t pressed = pe_phone_pad_pressed();
    if (pe_phone_box_message() != s_question) { pe_end("question closed"); return; }
    if (pressed & PE_PHONE_PAD_CROSS) {
        const int yes = pe_phone_box_choice() == 1;
        pe_phone_box_close();
        if (yes) pe_start_call();
        else pe_end("no");
    } else if (pressed & PE_PHONE_PAD_CIRCLE) {
        pe_end("no");
    }
}

static void pe_step_call(void) {
    const uint32_t pressed = pe_phone_pad_pressed();
    if (!pe_phone_box_open()) { pe_end("box closed"); return; }
    if (pressed & PE_PHONE_PAD_CROSS) {
        if (s_page + 1 < s_script.count) {
            if (!pe_show_page(s_page + 1)) pe_end("no box free");
        } else {
            pe_end(s_hung_up ? "hung up" : "done");
        }
    } else if ((pressed & PE_PHONE_PAD_CIRCLE) && s_page < s_script.click) {
        s_hung_up = 1;
        if (!pe_show_page(s_script.click)) pe_end("no box free");
    }
}

static int pe_settled(void) {
    return pe_free_field() && !pe_phone_box_any() && !(g_GameState.flags & 0x9000);
}

/* Right after the pad is read, before any field logic: while our boxes are up, the pad is ours. */
static int pe_on_frame(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    if (pe_busy()) pe_phone_pad_take();
    return 0;
}

/* Every frame, before the boxes draw (after the field logic). */
static int pe_on_textboxes(void *ctx, void *user) {
    int token;
    (void)ctx;
    (void)user;
    if (!pe_game_loaded()) return 0;
    token = g_SceneDispatchToken;
    if (token != s_token) { s_token = token; pe_reset("room changed"); }
    pe_phone_sound_tick();
    if (pe_busy()) pe_phone_hold_tick();
    if (!s_checked && pe_phone_font_ready() && !pe_busy()) {
        s_checked = 1;
        (void)pe_phone_flow_check_calls();
    }
    switch (s_state) {
    case ST_ASK: pe_step_ask(); break;
    case ST_CALL: pe_step_call(); break;
    default: {
        const int settled = pe_settled();
        /* Held at the phone, the field stays the phone's (locked): ready once the save screen and
         * its boxes are gone. Otherwise once she has hung up and the field is free. */
        const int ready = pe_phone_hold_active()
            ? !(g_GameState.flags & 0x9000) && !pe_phone_box_any() && pe_player() != NULL
            : settled;
        pe_phone_spot_tick(settled);
        if (pe_phone_spot_ready(&s_spot) && ready) { if (++s_wait >= PE_ASK_FREE) pe_begin(); }
        else s_wait = 0;
        break;
    }
    }
    pe_phone_pad_give(s_state == ST_ASK);                 /* the field logic has run: hand the pad back */
    return 0;
}

static int pe_on_reset(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    pe_reset("save loaded or new game");
    return 0;
}

int pe_phone_flow_check_calls(void) {
    int bad = 0, i, n = pe_phone_calls_count();
    if (!pe_phone_font_ready()) return -1;
    if (pe_busy()) return -1;                             /* s_script is showing */
    for (i = 0; i < n; ++i) {
        const PePhoneCall *c = pe_phone_call_at(i);
        int wide, ran;
        pe_phone_script_build(&s_script, c, pe_phone_font_advance);
        if (pe_phone_script_problems(&s_script, c, &wide, &ran)) {
            ++bad;
            pe_mod_log(PE_PHONE_MOD_ID, "call %s: %d box(es) too wide, %d line(s) ran on into another box", c->key, wide, ran);
        }
    }
    s_script.count = 0;
    pe_mod_log(PE_PHONE_MOD_ID, "checked %d calls with the game's font: %d need a look", n, bad);
    return bad;
}

void pe_phone_flow_debug(int mode, const char *key) {
    if (!pe_game_loaded()) { pe_toast("Phone: only in a game"); return; }
    if (pe_busy()) return;
    pe_phone_spot_inject();
    s_force = mode;
    snprintf(s_force_key, sizeof s_force_key, "%s", key ? key : "");
    s_wait = 0;
}

int pe_phone_flow_busy(void) { return pe_busy(); }

void pe_phone_flow_register(const char *mod_id) {
    s_force = -1;
    s_ev_start = pe_event_id("phone.call_start");
    s_ev_end = pe_event_id("phone.call_end");
    pe_hook_add(PE_HOOK_FRAME, pe_on_frame, NULL, PE_PRIORITY_LATE, mod_id);   /* after other mods read the pad */
    pe_hook_add(PE_HOOK_TEXTBOXES, pe_on_textboxes, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_on_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_on_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
}
