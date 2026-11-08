/* Spotting a phone save: the phone's question, then a real save.
 *
 * The question is one page copied into 16 message banks: "Will you save your progress?" with a
 * choice of two. It is recognised when its box finds the page (PE_HOOK_TEXT_PAGE, ahead of every
 * other page handler, so a language pack's text doesn't hide it) and logged with the room and story
 * (script variable 74): "phone room=<n> story=0x<v> day=<d>" in pe_play.log, the room list phase 2
 * needs. The story save near the end starts with Aya's name and is only logged. The phone then
 * opens the save screen; when a card save is published the host announces "save.committed" (quick
 * saves and room checkpoints never commit). A question with no save after it (No, or backing out of
 * the save screen) is forgotten once the field has been settled a while, and on a room change. */
#include <stdio.h>

#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_phone.h"
#include "pe1/scene_dispatch.h"
#include "pe1/script_queue.h"

#define PE_STORY_VAR    74
#define PE_FORGET_FREE  30   /* settled frames after a question with no save */

enum { SPOT_IDLE, SPOT_ASKED, SPOT_SAVED };

static int s_state = SPOT_IDLE;
static int s_quiet;                    /* our call is up: the phone's pages aren't a new phone */
static int s_free;
static int s_token = -1;
static PePhoneSpot s_spot = {-1, 0};
static PePhoneSpot s_seen = {-1, 0};   /* the last phone seen, for F1 */

static PePhoneSpot pe_here(void) {
    PePhoneSpot p;
    p.room = pe_room_number();
    p.story = g_ScriptVariableBank[PE_STORY_VAR];
    return p;
}

static int pe_on_page(void *ctx, void *user) {
    PeHookTextPage *page = ctx;
    int kind;
    (void)user;
    if (s_quiet || !pe_game_loaded() || !page || !page->message || !*page->message) return 0;
    kind = pe_phone_save_question(*page->message);
    if (!kind) return 0;
    s_seen = pe_here();
    pe_log("phone", "room=%d story=0x%X day=%d page=%u%s", s_seen.room, s_seen.story, pe_phone_story_day(s_seen.story),
           page->page, kind == 2 ? " story_save=1" : "");
    if (kind == 1 && s_state == SPOT_IDLE) {
        s_spot = s_seen;
        s_state = SPOT_ASKED;
        s_free = 0;
    }
    return 0;
}

static int pe_on_committed(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    if (s_state != SPOT_ASKED) return 0;
    pe_log("phone", "room=%d saved=1", s_spot.room);
    s_state = SPOT_SAVED;
    return 0;
}

static int pe_on_reset(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    pe_phone_spot_clear();
    return 0;
}

void pe_phone_spot_tick(int settled) {
    const int token = g_SceneDispatchToken;
    if (token != s_token) { s_token = token; pe_phone_spot_clear(); return; }
    if (s_state != SPOT_ASKED) return;
    if (!settled) { s_free = 0; return; }
    if (++s_free >= PE_FORGET_FREE) {
        pe_log("phone", "room=%d saved=0", s_spot.room);
        pe_phone_spot_clear();
    }
}

int pe_phone_spot_ready(PePhoneSpot *out) {
    if (s_state != SPOT_SAVED) return 0;
    if (out) *out = s_spot;
    return 1;
}

void pe_phone_spot_clear(void) {
    s_state = SPOT_IDLE;
    s_free = 0;
}

void pe_phone_spot_quiet(int on) { s_quiet = on; }

void pe_phone_spot_inject(void) {
    s_spot = pe_here();
    s_state = SPOT_SAVED;
}

void pe_phone_spot_status(char *out, unsigned size) {
    if (s_seen.room < 0) snprintf(out, size, "none yet");
    else snprintf(out, size, "M%03d, story 0x%X, Day %d", s_seen.room, s_seen.story, pe_phone_story_day(s_seen.story));
}

void pe_phone_spot_register(const char *mod_id) {
    /* ahead of every other page handler (a language pack's is EARLY), like Wayne's menu */
    pe_hook_add(PE_HOOK_TEXT_PAGE, pe_on_page, NULL, PE_PRIORITY_EARLY - 100, mod_id);
    pe_hook_add(pe_event_id(PE_EVENT_SAVE_COMMITTED), pe_on_committed, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_on_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_on_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
}
