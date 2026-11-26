/* Debug-control event log: one JSON line per change a test waits for, in <folder>/events.jsonl,
 * so a runner can wait on "dialogue page 164 waiting" or "battle ended" instead of polling.
 * See docs/DEBUG_CONTROL.md. Observes only. */
#include "pe_debug_internal.h"
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include "pe_game.h"
#include "pe_hooks.h"
#include "pe_mod_host.h"
#include "pe_gametext.h"
#include "pe1/boot.h"
#include "pe1/game_state.h"
#include "pe1/player_entity.h"
#include "pe1/scene_dispatch.h"
#include "pe1/script_queue.h"
#include "pe1/textbox.h"
#include "pe1/textbox_open.h"

#define PE_TITLE_TOKEN 0xA9400048u   /* the title scene (Boot_Shutdown, main.c's scene switch) */

static unsigned s_seq;
static int s_ready;              /* the first frame records the starting state without events */
static int s_title;              /* the title menu ran since the last field frame */
static int s_left;               /* the field loop ended for the title: wait for a field frame */
static struct {
    int token, free, menu, battle, paused, hp_zero;
    int map, hp;     /* as of the last field frame: gone once the field loop ends */
} s_last;
static struct { unsigned char state; short page; int choices; unsigned sum; } s_box[4];

/* A box's text, to notice a mod rewriting it after it opened (FNV-1a up to the end code). */
static unsigned pe_text_sum(const unsigned char *m) {
    unsigned h = 2166136261u, i;
    for (i = 0; m && i < 512 && m[i] != 0xFF; ++i) h = (h ^ m[i]) * 16777619u;
    return h ^ (unsigned)(uintptr_t)m;
}

/* {"seq":N,"frame":F,"vblank":V,"event":"<name>"<,fields>} */
static void pe_event(const char *name, const char *fields_fmt, ...) {
    char path[1100];
    FILE *f;
    va_list ap;
    pe_debug_path(path, sizeof path, "events.jsonl");
    if (!(f = fopen(path, "a"))) return;
    fprintf(f, "{\"seq\":%u,\"frame\":%u,\"vblank\":%u,\"event\":\"%s\"", ++s_seq, pe_debug_frame(),
            (unsigned)g_BootHostVBlankCount, name);
    if (fields_fmt) {
        fputc(',', f);
        va_start(ap, fields_fmt);
        vfprintf(f, fields_fmt, ap);
        va_end(ap);
    }
    fputs("}\n", f);
    fclose(f);
}

static void pe_event_text(const char *name, int box, int page, int choices, const unsigned char *message) {
    char path[1100], text[400];
    FILE *f;
    pe_debug_path(path, sizeof path, "events.jsonl");
    if (!(f = fopen(path, "a"))) return;
    text[0] = 0;
    if (message) pe_text_to_ascii(text, sizeof text, message);
    fprintf(f, "{\"seq\":%u,\"frame\":%u,\"vblank\":%u,\"event\":\"%s\",\"box\":%d,\"page\":%d,\"choices\":%d,\"text\":",
            ++s_seq, pe_debug_frame(), (unsigned)g_BootHostVBlankCount, name, box, page, choices);
    pe_debug_json_string(f, text);
    fputs("}\n", f);
    fclose(f);
}

static int pe_box_choices(const TextboxEntry *box) {
    return box->control.flags & 0x200000 ? (box->control.flags >> 0x16) & 7 : 0;
}

static void pe_events_boxes(void) {
    int i;
    unsigned sum;
    for (i = 0; i < 4; ++i) {
        const TextboxEntry *box = &g_TextboxEntries[i];
        int choices = box->state ? pe_box_choices(box) : 0;
        if (s_box[i].state && (!box->state || box->page_id != s_box[i].page)) {
            /* The answer is the cursor Cross confirmed (g_MenuEquipSlotIndex, -1 until then). */
            if (s_box[i].choices)
                pe_event("dialogue_closed", "\"box\":%d,\"page\":%d,\"choices\":%d,\"answer\":%d", i, s_box[i].page,
                         s_box[i].choices, (int)g_MenuEquipSlotIndex);
            else
                pe_event("dialogue_closed", "\"box\":%d,\"page\":%d", i, s_box[i].page);
            s_box[i].state = 0;
        }
        if (!box->state) continue;
        sum = pe_text_sum(box->message);
        if (!s_box[i].state) pe_event_text("dialogue_opened", i, box->page_id, choices, box->message);
        else if (sum != s_box[i].sum || choices != s_box[i].choices)
            pe_event_text("dialogue_changed", i, box->page_id, choices, box->message);
        if (box->state == 2 && s_box[i].state != 2)
            pe_event_text("dialogue_waiting", i, box->page_id, choices, box->message);
        s_box[i].state = box->state;
        s_box[i].page = box->page_id;
        s_box[i].choices = choices;
        s_box[i].sum = sum;
    }
}

static void pe_events_frame(void) {
    Combatant *p = g_PlayerEntity ? (Combatant *)g_PlayerEntity->state : NULL;
    int token = g_SceneDispatchToken, free = pe_free_field(), menu = !!(g_GameStateFlags & 4);
    int battle = !!(g_GameStateFlags & 2), paused = !!(g_GameStateFlags & 1);
    int hp_zero = battle && p && p->curHP == 0;
    if (!s_ready) {
        s_last.token = token; s_last.free = free; s_last.menu = menu;
        s_last.battle = battle; s_last.paused = paused; s_last.hp_zero = hp_zero;
        s_ready = 1;
        pe_event("started", "\"map\":%d,\"free\":%s,\"battle\":%s", pe_room_number(),
                 free ? "true" : "false", battle ? "true" : "false");
    }
    if (s_title) { s_title = 0; pe_event("title_left", NULL); }
    if (token != s_last.token && (unsigned)token == PE_TITLE_TOKEN) {
        /* The field loop is ending for the title (a game over or a soft reset): its frames
         * stop, so close what is open now. Seen from the last field frame's FRAME_DRAWN. */
        if (s_last.battle) pe_event("battle_ended", "\"hp\":%d,\"left\":true", s_last.hp);
        pe_event("to_title", "\"from_map\":%d", s_last.map);
        s_last.token = token; s_last.battle = battle = 0;
        s_left = 1;
        return;               /* nothing else of this frame's field state means anything now */
    } else if (token != s_last.token)
        pe_event("room", "\"map\":%d,\"token\":\"%08X\",\"story\":%u", pe_room_number(), (unsigned)token,
                 (unsigned)g_ScriptVariableBank[74]);
    if (battle != s_last.battle) {
        if (battle) pe_event("battle_started", NULL);
        else pe_event("battle_ended", "\"hp\":%d", p ? (int)p->curHP : -1);
    }
    if (hp_zero && !s_last.hp_zero) pe_event("player_down", NULL);
    if (menu != s_last.menu) pe_event(menu ? "menu_opened" : "menu_closed", NULL);
    if (paused != s_last.paused) pe_event(paused ? "paused" : "unpaused", NULL);
    pe_events_boxes();
    if (free != s_last.free) pe_event(free ? "control" : "no_control", NULL);
    s_last.token = token; s_last.free = free; s_last.menu = menu;
    s_last.battle = battle; s_last.paused = paused; s_last.hp_zero = hp_zero;
    s_last.map = pe_room_number(); s_last.hp = p ? (int)p->curHP : -1;
}

static int pe_events_hook(void *ctx, void *user) {
    int kind = (int)(intptr_t)user;
    if (kind == PE_HOOK_FRAME_DRAWN) {
        /* Also after the frame's game logic, so a field loop that ends this frame is seen. */
        if (s_ready && !s_title && !s_left) pe_events_frame();
    } else if (kind == PE_HOOK_SCENE_START) pe_event("scene_loaded", "\"map\":%d", pe_room_number());
    else if (kind == PE_HOOK_SAVE_READ) pe_event("save_loaded", NULL);
    else if (kind == PE_HOOK_NEW_GAME) pe_event("new_game", NULL);
    else if (kind == PE_HOOK_TITLE_FRAME) {
        if (!s_title) pe_event("title", NULL);
        s_title = 1;
    } else {
        pe_event("save_committed", NULL);   /* PE_EVENT_SAVE_COMMITTED */
    }
    (void)ctx;
    return 0;
}

static void pe_events_install(void) {
    static const int hooks[] = {PE_HOOK_SCENE_START, PE_HOOK_SAVE_READ, PE_HOOK_NEW_GAME, PE_HOOK_TITLE_FRAME,
                                PE_HOOK_FRAME_DRAWN};
    unsigned i;
    int saved = pe_event_id(PE_EVENT_SAVE_COMMITTED);
    for (i = 0; i < sizeof hooks / sizeof *hooks; ++i)
        pe_hook_add(hooks[i], pe_events_hook, (void *)(intptr_t)hooks[i], 10000, "host");
    if (saved >= 0) pe_hook_add(saved, pe_events_hook, (void *)(intptr_t)saved, 10000, "host");
}

static void pe_events_field_frame(void) {
    s_left = 0;               /* the field runs again (a save loaded, a new game) */
    pe_events_frame();
}

static void pe_ctl_events(const char *args) {
    char detail[32];
    (void)args;
    snprintf(detail, sizeof detail, "%u", s_seq);
    pe_debug_reply("ok %s %s", "events", detail);
}

static const PeDebugCommand s_commands[] = {
    {"events", PE_DEBUG_EXACT, pe_ctl_events},
};

const PeDebugModule pe_debug_events_module = {
    "events", s_commands, sizeof s_commands / sizeof *s_commands, pe_events_install, pe_events_field_frame
};
