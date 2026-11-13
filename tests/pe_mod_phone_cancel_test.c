/* Regression coverage for the real phone spotter with a minimal host. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define PE_GAME_H
#define PE_MOD_HOST_H
#define PE_PHONE_H
#define PE1_GAME_STATE_H
#define PE1_SCENE_DISPATCH_H
#define PE1_SCRIPT_QUEUE_H
typedef struct { int room, story; } PePhoneSpot;
typedef struct { const uint8_t **message; unsigned page; } PeHookTextPage;
enum { PE_PHONE_ASK, PE_PHONE_ALWAYS, PE_PHONE_OFF };
enum { PE_HOOK_FRAME, PE_HOOK_TEXT_PAGE, PE_HOOK_SAVE_READ, PE_HOOK_NEW_GAME, PE_PRIORITY_EARLY, PE_PRIORITY_NORMAL };
#define PE_EVENT_SAVE_COMMITTED "save.committed"
static struct { unsigned flags; } g_GameState;
static int g_SceneDispatchToken, g_ScriptVariableBank[128];
static int mode, unsaved, held, can_hold, attempts;
static int pe_game_loaded(void) { return 1; }
static int pe_room_number(void) { return 20; }
static int pe_phone_save_question(const uint8_t *p) { return *p; }
static int pe_phone_use_option(const uint8_t *p) { (void)p; return -1; }
static int pe_phone_sound_base(int room) { return room == 20; }
static int pe_phone_story_day(int story) { (void)story; return 1; }
static int pe_phone_mode(void) { return mode; }
static int pe_phone_unsaved_on(void) { return unsaved; }
static int pe_phone_hold_active(void) { return held; }
static int pe_phone_hold_begin(void) { ++attempts; return held = can_hold; }
static void pe_phone_hold_release(void) { held = 0; }
#define pe_log(...) ((void)0)
static int pe_event_id(const char *s) { (void)s; return 10; }
static void pe_hook_add(int id, int (*cb)(void *, void *), void *u, int priority, const char *mod) {
    (void)id; (void)cb; (void)u; (void)priority; (void)mod;
}
void pe_phone_spot_clear(void);
#include "../mods/pe_phone/pe_phone_spot.c"
static void question(void) {
    static const uint8_t text[] = {1};
    const uint8_t *message = text;
    PeHookTextPage page = {&message, 0};
    pe_phone_spot_clear(); s_token = g_SceneDispatchToken;
    mode = PE_PHONE_ASK; unsaved = 1; held = 0; can_hold = 1; attempts = 0; g_GameState.flags = 0;
    pe_on_page(&page, NULL);
    assert(s_state == SPOT_ASKED);
}
int main(void) {
    question();
    pe_on_frame(NULL, NULL); assert(!held && !attempts); /* pickup must finish */
    g_GameState.flags = 0x9000; pe_on_frame(NULL, NULL); assert(held && attempts == 1);
    pe_on_frame(NULL, NULL); assert(attempts == 1);
    g_GameState.flags = 0; pe_on_frame(NULL, NULL);
    assert(held && pe_phone_spot_ready(NULL)); /* cancel is ready before field animation */
    question(); mode = PE_PHONE_ALWAYS; g_GameState.flags = 0x9000;
    pe_on_frame(NULL, NULL); g_GameState.flags = 0; pe_on_frame(NULL, NULL);
    assert(held && pe_phone_spot_ready(NULL));
    question(); g_GameState.flags = 0x9000; pe_on_frame(NULL, NULL);
    pe_on_committed(NULL, NULL); g_GameState.flags = 0; pe_on_frame(NULL, NULL);
    assert(held && pe_phone_spot_ready(NULL)); /* a real save stays ready */
    question(); unsaved = 0; g_GameState.flags = 0x9000; pe_on_frame(NULL, NULL); assert(!held && !attempts);
    question(); mode = PE_PHONE_OFF; g_GameState.flags = 0x9000; pe_on_frame(NULL, NULL); assert(!held && !attempts);
    question(); g_GameState.flags = 0x9000; pe_on_frame(NULL, NULL); unsaved = 0;
    pe_on_frame(NULL, NULL); assert(!held);
    question(); can_hold = 0; g_GameState.flags = 0x9000; pe_on_frame(NULL, NULL);
    g_GameState.flags = 0; pe_on_frame(NULL, NULL); assert(!pe_phone_spot_ready(NULL));
    for (int i = 0; i < PE_NO_SAVE_GAP; ++i) pe_phone_spot_tick(1);
    assert(pe_phone_spot_ready(NULL)); /* non-holding phones keep their fallback */
    question();
    for (int i = 0; i < PE_NO_SAVE_GAP; ++i) { pe_on_frame(NULL, NULL); pe_phone_spot_tick(1); }
    assert(!attempts && pe_phone_spot_ready(NULL)); /* No to saving */
    puts("phone save-cancel regression checks passed");
    return 0;
}
