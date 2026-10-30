/* Warp menu, from the Pathogen Eve port: F1 > Warp sends Aya to a story checkpoint.
 *
 * The transfer is the field script's own room transfer (Script_EncodeBarcode), with the story
 * variable of that point (script variable 74). It runs on the first free field frame after the
 * panel closes: never in a battle, an event, a room change or a movie. */
#include <stdio.h>

#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe1/game_state.h"
#include "pe1/game_timers.h"
#include "pe1/scene_dispatch.h"
#include "pe1/script_queue.h"

#define MOD_ID "pe_warp"
#define PE_PAGE_WARP "Warp"
#define PE_SCRIPT_VAR_STORY 74

typedef struct { const char *name; unsigned token, story; } PeWarp;

/* Story checkpoints: the room token of their first entry and the story variable there, read from
 * play-throughs of the Pathogen Eve port (the same retail tokens). */
static const PeWarp k_warps[] = {
    {"Day 1: Carnegie Hall (field start)", 0xA8000148u, 0x08u},
    {"Day 1: Stage (M002)",                0xA80002C8u, 0x18u},
    {"Day 1: M011",                        0xA80011C8u, 0x40u},
    {"Day 1: Eve, first fight (M023)",     0xA80021C8u, 0x5Bu},
    {"Day 1: Sewer entry (M026)",          0xA8002348u, 0x68u},
    {"Day 1: Sewer (M027)",                0xA80023C8u, 0x68u},
    {"Day 1: Sewer hall (M028)",           0xA8002448u, 0x68u},
    {"Day 1: M031",                        0xA80030C8u, 0x68u},
    {"Day 1: M032",                        0xA8003148u, 0x68u},
    {"Day 1: M033",                        0xA80031C8u, 0x68u},
    {"Day 1: Alligator (M034)",            0xA8003248u, 0x6Cu},
    {"Day 1: after the boss (M359)",       0xA80654C8u, 0x70u},
    {"Day 1: exit (M036)",                 0xA8003348u, 0x78u},
    {"Day 2: start (M351)",                0xA80650C8u, 0x80u},
    {"Day 2: Wayne, first visit (M052)",   0xA8005148u, 0x90u},
    {"Day 2: press conference (M047)",     0xA80043C8u, 0xA6u},
    {"Day 2: Museum (M195)",               0xA80292C8u, 0xC8u},
    {"Day 2: Central Park (M374)",         0xA8067248u, 0xE4u},
    {"Day 2: Zoo (M068)",                  0xA8006448u, 0xE8u},
    {"Day 3: Museum, Klamp's office",      0xA80434C8u, 0x170u},
    {"Day 3: police station, infected",    0xA80201C8u, 0x178u},
    {"Day 3: police station, Sheeva",      0xA8004248u, 0x190u},
    {"Day 3: after Sheeva's change",       0xA80220C8u, 0x190u},
    {"Day 4: hospital",                    0xA8022348u, 0x1D0u},
    {"Day 4: jet scene",                   0xA80470C8u, 0x1F6u},
    {"Day 4: rooftop",                     0xA80680C8u, 0x1F8u},
    {"Day 5: start",                       0xA8065148u, 0x218u},
    {"Day 5: warehouse",                   0xA8046048u, 0x220u},
    {"Day 5: Chinatown",                   0xA80260C8u, 0x220u},
    {"Day 5: Chinatown sewer",             0xA8026348u, 0x238u},
    {"Day 5: Chinatown sewer, deep",       0xA80831C8u, 0x240u},
    {"Day 5: subway",                      0xA8028148u, 0x254u},
    {"Day 5: Disc 2 start",                0xA80284C8u, 0x258u},
    {"Day 5: Disc 2 Museum",               0xA8029148u, 0x258u},
    {"Day 5: T-Rex movie",                 0xA80432C8u, 0x260u},
    {"Day 5: T-Rex fight",                 0xA80452C8u, 0x278u},
    {"Day 5: before Eve",                  0xA8044348u, 0x278u},
    {"Day 5: Eve movie",                   0xA80444C8u, 0x278u},
    {"Day 6: Navy",                        0xA80470C8u, 0x2A8u},
    {"Day 6: final",                       0xA8048448u, 0x2E2u},
    {"Finale",                             0xA8000348u, 0x0E2u},
};
#define PE_WARPS ((int)(sizeof k_warps / sizeof k_warps[0]))

/* Tokens with no field room loop: the title (menu and attract movies), New Game set-up, the
 * boot room, and the New Game prologue (its movie hands over to the first room itself). */
#define PE_TOKEN_TITLE    0xA9400048u
#define PE_TOKEN_NEW_GAME 0xAA108448u
#define PE_TOKEN_BOOT     0xA8000048u
#define PE_TOKEN_PROLOGUE 0xA80830C8u

static int s_warp, s_pending_warp;

static int pe_in_field_room(unsigned token) {
    return token && token != PE_TOKEN_TITLE && token != PE_TOKEN_NEW_GAME && token != PE_TOKEN_BOOT;
}
/* 1 = transfer started, 0 = wait (room change, battle, event or movie), -1 = not in the field. */
static int pe_warp_now(const PeWarp *w) {
    const unsigned token = (unsigned)g_SceneDispatchToken;
    int target = (int)w->token, *args[1] = {&target};
    if (!pe_in_field_room(token)) return -1;
    if ((unsigned)g_SceneDispatchCur != token || token == PE_TOKEN_PROLOGUE) return 0;
    if (!pe_free_field() || g_GameState.scene_audio.tracks.movie_active) return 0;
    g_ScriptVariableBank[PE_SCRIPT_VAR_STORY] = w->story;
    (void)Script_EncodeBarcode(args);
    return 1;
}
static void pe_warp_step(void) {
    const PeWarp *w;
    int r;
    if (!s_pending_warp) return;
    w = &k_warps[s_pending_warp - 1];
    r = pe_warp_now(w);
    if (r == 0) return;
    s_pending_warp = 0;
    if (r < 0) { pe_toast("Warp: only in the field"); return; }
    {
        char msg[80];
        snprintf(msg, sizeof msg, "Warp: %s", w->name);
        pe_toast(msg);
    }
    pe_log("cheat_warp", "token=%08X story=0x%X", w->token, w->story);
}

static int pe_on_frame(void *ctx, void *user) {
    (void)ctx; (void)user;
    pe_warp_step();
    return 0;
}
static int pe_on_new_game(void *ctx, void *user) {
    (void)ctx; (void)user;
    s_pending_warp = 0;
    return 0;
}

static void pe_ask_warp(void) {
    if (!pe_game_loaded()) { pe_toast("Warp: load a game first"); return; }
    s_pending_warp = s_warp + 1;   /* runs on the next free field frame */
}
static unsigned pe_warp_count(void) { return PE_WARPS; }
static const char *pe_warp_name(unsigned i) { return i < (unsigned)PE_WARPS ? k_warps[i].name : ""; }

static int warp_init(void) {
    pe_hook_add(PE_HOOK_FRAME, pe_on_frame, NULL, PE_PRIORITY_LATE, MOD_ID);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_on_new_game, NULL, PE_PRIORITY_NORMAL, MOD_ID);

    pe_menu_begin(PE_PAGE_WARP);
    pe_menu_top_page(PE_PAGE_WARP, "Go to a story checkpoint.", 50);
    pe_menu_register_dynamic_choice(0, "Warp to", "A story checkpoint. Warping sets the story to that point.",
                                    &s_warp, PE_WARPS, pe_warp_count, pe_warp_name);
    pe_menu_register_action(0, "Warp", "Go to the chosen checkpoint (from the field, once the panel closes).",
                            pe_ask_warp);
    pe_menu_begin(0);
    return 0;
}

const PeMod pe_mod_pe_warp = {MOD_ID, "Warp", "0.1", NULL, warp_init, NULL};
