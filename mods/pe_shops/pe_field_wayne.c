/*
 * Wayne's services from Warner, where he stands guard (found by his retail talk script, not by
 * room). Ported from the psxrecomp build (src/mods/pe_field_wayne.c): Store equipment, Store
 * item and the Workshop (Requisition, Rebuild, Mod Permit Card); not Discard junk -- making guns
 * stays Wayne's.
 *
 * Warner's talk is an ordinary actor script: Cross next to him runs his contact task (script base
 * + 0x154), which locks the field, shows his two lines and unlocks it again. His task is checked
 * word for word against the retail script (s_orig) before anything is done, read-only.
 *
 * Native design (the recomp patched his bytecode to jump into a script of ours; native jumps are
 * offsets from the script's own buffer, so that doesn't carry over):
 *   - his talk plays untouched, both lines;
 *   - when his task ends, the field is locked again the way his script does it (field event 0xFE
 *     from him, Script_QueueFieldSfx) and a dialogue box of ours opens: "Torres sent supplies."
 *     and Store equipment / Store item / Cancel, in Wayne's window style. Wayne's Workshop
 *     (pe_wayne_menu.c) recognises it and adds Workshop above Cancel;
 *   - when it closes, the field is unlocked (event 0xFF, as his script ends) and, on Store
 *     equipment or Store item, the storage screen opens the way his script opcode would
 *     (Script_BeginSceneTransition: Render_BeginSceneLoad, Menu_OpenEquipScreen, menu flags).
 *     Talk to him again for more.
 *   - his talk's field lock is taken over rather than unlocked and locked again: at the end of
 *     his last line his task skips its unlock (we do its pose reset), and our menu keeps Aya
 *     held until it closes (see pe_take_over);
 *   - once his lines have been heard, later talks skip them: as soon as his talk has locked the
 *     field, any box of his already open is closed, his task is taken over the same way, and
 *     our menu opens straight away.
 * Nothing in his script or the story changes.
 *
 * Standing next to him, the game starts a fresh contact task every frame; it checks the pad and
 * the field state (0x154 .. 0x258) and ends unless Cross was pressed. Only a task past those
 * checks is his talk, so the menu no longer waits for the player to walk away.
 */
#include <string.h>

#include "pe_shops.h"
#include "pe_game.h"
#include "pe_gametext.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe1/entity_animation.h"
#include "pe1/field_actor.h"
#include "pe1/field_sfx.h"
#include "pe1/scene_dispatch.h"
#include "pe1/scene_transition.h"
#include "pe1/script_node.h"
#include "pe1/textbox.h"
#include "pe1/textbox_open.h"

void Menu_OpenEquipScreen(int mode);

#define PE_WARNER_MODEL   0x04
#define PE_CONTACT_OFF    0x154   /* his contact task, from the script base */
#define PE_TALK_OFF       0x258   /* past the pad and field checks: his talk proper */
#define PE_LOCKED_OFF     0x274   /* past his field lock (event 0xFE at 0x260) */
#define PE_ENDING_OFF     0x33C   /* his ending: idle pose, flag 0x20 off, unlock, node free */
#define PE_LINE2_WAIT_OFF 0x328   /* waiting for his second line's box to close */
#define PE_FREE_OFF       0x364   /* his last op: node free */
#define PE_FROZEN         0x7FFF  /* node countdown that keeps his task waiting for us */
#define PE_LINE_PAGE_1    30      /* the room message pages his two lines use */
#define PE_LINE_PAGE_2    31
#define PE_NODE_FREE      0x10
#define PE_TEXTBOX_SHOWN  2
#define PE_STORE_EQUIPMENT 0
#define PE_STORE_ITEMS    1
#define PE_FIELD_LOCK     0xFE    /* field events his talk sends at its start and end */
#define PE_FIELD_UNLOCK   0xFF
#define PE_BOX_PAGE      0x7F00  /* our box's page id: no room message uses it */
#define PE_STORAGE_DELAY  2       /* frames after the unlock before the storage screen opens */

/* His contact task, base + 0x154 .. 0x36C, as read 2026-10-14 (retail). */
static const uint32_t s_orig[] = {
    0x01308009,0x00000000,0x00000003,0x00000000,0x00000036,0x00010000,0x01B08009,0x00000000,0x00000007,0x00000001,
    0x00000000,0x00000000,0x00064005,0x00000000,0x00000001,0x000001B2,0x01308009,0x00000000,0x0000000C,0x00000002,
    0x0000004A,0x000000E8,0x00064005,0x00000000,0x00000002,0x000001B2,0x00124024,0x00000000,0x00000000,0x00000001,
    0x00B08009,0x00000000,0x0000000B,0x00000000,0x00000000,0x00000000,0x00064005,0x00000000,0x00000000,0x000001B2,
    0x02308009,0x00000000,0x0000000B,0x00000001,0x00000012,0x00000000,0x00064005,0x00000000,0x00000001,0x000001B2,
    0x00806011,0x00000000,0x00000001,0x00000100,0x00000002,0x00B08009,0x00000000,0x0000000E,0x00000000,0x00000002,
    0x00000000,0x00064005,0x00000000,0x00000000,0x000001B2,0x000000AA,0x00000000,0x0000601C,0x00000000,0x00000000,
    0x00000000,0x000000FE,0x00000079,0x00000000,0x0000202E,0x00000000,0x00000001,0x0000604B,0x00000000,0x00000000,
    0x00000000,0x0000002D,0x0000202E,0x00000000,0x00000000,0x00000078,0x00000000,0x0000200D,0x00000000,0x0000001E,
    0x0000202E,0x00000000,0x00000005,0x0000204E,0x00000000,0x00000000,0x0000202F,0x00000000,0x00000077,0x00002022,
    0x00000000,0x0000001E,0x00002002,0x00000000,0x00000077,0x0000200D,0x00000000,0x0000001F,0x0000202E,0x00000000,
    0x00000003,0x0000204E,0x00000000,0x00000000,0x0000202F,0x00000000,0x00000050,0x00002022,0x00000000,0x0000001F,
    0x00000079,0x00000000,0x0000202E,0x00000000,0x00000000,0x00000078,0x00000000,0x0000601C,0x00000000,0x00000000,
    0x00000000,0x000000FF,0x00000020,0x00000000,
};
#define PE_TASK_BYTES ((unsigned)sizeof s_orig)

static int s_enabled = 1;
static u8 s_text[128];            /* our menu text */
static int s_text_ready;
static BattleEntity *s_warner;
static int s_token = -1;          /* the room we looked in */
static int s_boxes_this_talk;     /* dialogue boxes his current talk has opened */
static u8 *s_last_message;        /* the box message seen last frame */
static TextboxEntry *s_menu_box;  /* the box showing our menu */
static int s_pending = -1;        /* storage screen to open: PE_STORE_*, or -1 */
static int s_pending_wait;        /* frames left before it opens */
static int s_heard;               /* his lines were heard once this session: later talks skip them */
static u8 s_menu_cursor;          /* our menu's row, kept in case a line of his resets the cursor */
static int s_swallow;           /* quiet frames left in which a new talk is the Cross that closed our menu */
#define PE_SWALLOW_FRAMES 10
static int s_held;                /* field locks of his we took over and still owe an unlock for */
static ScriptNode *s_frozen;      /* his task, held at his last line until it closes */

static void pe_field_event(int value);

/* Is `e` Warner, with his retail contact task? */
static int pe_is_warner(BattleEntity *e) {
    return e->modelIndex == PE_WARNER_MODEL && e->scriptBase &&
           e->scriptCursor1A0 == (u32 *)(e->scriptBase + PE_CONTACT_OFF) &&
           memcmp(e->scriptBase + PE_CONTACT_OFF, s_orig, PE_TASK_BYTES) == 0;
}

static BattleEntity *pe_find_warner(void) {
    BattleEntity *e = (BattleEntity *)g_FieldActorListHead;
    int guard;
    for (guard = 0; e && guard < 40; ++guard, e = e->next)
        if (pe_is_warner(e)) return e;
    return NULL;
}

/* His talk's node, if he is talking now: a live node of his past the contact checks. The
 * contact task the game starts each frame while Aya stands next to him (still at the checks,
 * or ending at once) is not a talk. */
static ScriptNode *pe_talk_node(void) {
    const u8 *from = s_warner->scriptBase + PE_TALK_OFF;
    const u8 *to = s_warner->scriptBase + PE_CONTACT_OFF + PE_TASK_BYTES;
    int list, guard;
    for (list = 0; list < 3; ++list) {
        ScriptNode *n = s_warner->scriptNodeLists[list];
        for (guard = 0; n && guard < 16; ++guard, n = n->next) {
            const u8 *at = (const u8 *)n->current.script;
            if (!(n->flags & PE_NODE_FREE) && at >= from && at < to) return n;
        }
    }
    return NULL;
}

static int pe_talk_running(void) { return pe_talk_node() != NULL; }

/* Once his talk has locked the field: close any line of his that is up, do his ending's
 * pose reset (0x33C: idle action mode, flags 0x100/0x20 off) and send his task straight to
 * its last op (node free), leaving out his unlock. We now hold his lock (s_held) and release
 * it ourselves. A lock and an unlock queued in the same frame start in reverse order (newest
 * node first), so an unlock from him next to a lock from us would leave Aya free to walk.
 * Returns 1 if it took the talk over. */
static int pe_take_over(ScriptNode *n) {
    const u8 *at = (const u8 *)n->current.script;
    FieldActor *a = (FieldActor *)s_warner;
    if (at < s_warner->scriptBase + PE_LOCKED_OFF || at >= s_warner->scriptBase + PE_ENDING_OFF) return 0;
    if (Draw_GetTextboxPageState(PE_LINE_PAGE_1)) Draw_CloseTextboxPage(PE_LINE_PAGE_1);
    if (Draw_GetTextboxPageState(PE_LINE_PAGE_2)) Draw_CloseTextboxPage(PE_LINE_PAGE_2);
    Entity_SetActionMode(s_warner, 0);
    a->flags &= ~0x120u;
    n->current.script = (unsigned int *)(s_warner->scriptBase + PE_FREE_OFF);
    n->active = 1;                                      /* runs next pass, not after a wait */
    if (n == s_frozen) s_frozen = NULL;
    ++s_held;
    return 1;
}

/* Send the unlocks for every lock of his we hold. */
static void pe_release(void) {
    if (!s_warner) { s_held = 0; return; }
    while (s_held > 0) { pe_field_event(PE_FIELD_UNLOCK); --s_held; }
}

/* Let a task we were holding at his last line carry on by itself (his own unlock ends it). */
static void pe_unfreeze(void) {
    if (s_frozen && !(s_frozen->flags & PE_NODE_FREE)) s_frozen->active = 1;
    s_frozen = NULL;
}

static void pe_write_text(void) {
    PeText t;
    if (s_text_ready) return;
    pe_t_init(&t);
    pe_t_str(&t, "Torres sent supplies.");
    pe_t_byte(&t, G_NEWLINE);
    pe_t_choice(&t, 3);
    pe_t_option(&t, 1); pe_t_str(&t, "Store equipment");
    pe_t_option(&t, 0); pe_t_str(&t, "Store item");
    pe_t_option(&t, 0); pe_t_str(&t, "Cancel");
    pe_t_byte(&t, G_END);
    memcpy(s_text, t.b, t.n < sizeof s_text ? t.n : sizeof s_text);
    s_text_ready = 1;
}

/* For pe_wayne_menu.c: a box showing this is Wayne's menu (Warner's version). */
const u8 *pe_field_wayne_text(void) { return s_text_ready ? s_text : NULL; }

/* Lock or unlock the field the way his talk does: a field event sent from him. */
static void pe_field_event(int value) {
    Script_QueueFieldSfx(value, 0, 0, s_warner->fieldSfxId, 0);
}

static void pe_forget(void) {
    s_warner = NULL;
    s_boxes_this_talk = 0;
    s_last_message = NULL;
    s_menu_box = NULL;
}

/* During his talk: count (and log) the lines he says; they are left as they are. */
static void pe_talk_tick(void) {
    int i;
    for (i = 0; i < 4; ++i) {
        TextboxEntry *e = &g_TextboxEntries[i];
        char said[256];
        if (e->state == 0 || !e->message || e->message == s_last_message || e->message == s_text) continue;
        s_last_message = e->message;
        pe_log("field_wayne", "line %d: %s", ++s_boxes_this_talk, pe_text_to_ascii(said, sizeof said, e->message));
        return;
    }
}

/* After his talk: our menu in a box of its own, typed out at once (Render_SetupColorTable's
 * setup, without looking the text up in the room's messages). */
static void pe_open_menu(void) {
    int i;
    for (i = 0; i < 4; ++i) {
        TextboxEntry *e = &g_TextboxEntries[i];
        if (e->state != 0) continue;
        pe_write_text();
        e->page_id = PE_BOX_PAGE;
        e->style = 1;                                   /* a window, like Wayne's */
        e->background = 0;
        e->x = 56;                                      /* near the middle of the 320x240 screen */
        e->y = 80;
        e->width = 208;
        e->height = 82;                                 /* Wayne's Workshop sizes it, keeping the bottom */
        e->control.flags &= ~0x000F0000u & ~0x00100000u & ~0x00200000u;
        e->control.parts.delay = 0;
        e->control.parts.elapsed = 0;
        e->message = s_text;
        e->state = PE_TEXTBOX_SHOWN;
        g_DialogueChoiceCursor = 0;
        g_MenuEquipSlotIndex = -1;
        s_menu_cursor = 0;
        s_menu_box = e;
        if (s_held == 0) {                              /* else his lock, taken over, holds Aya */
            pe_field_event(PE_FIELD_LOCK);
            s_held = 1;
        }
        pe_log("field_wayne", "menu shown");
        return;
    }
}

/* Before the dialogue boxes draw (ahead of Wayne's Workshop): Warner, his talk, his box. */
static int pe_on_textboxes(void *ctx, void *user) {
    int token;
    (void)ctx;
    (void)user;
    if (!pe_game_loaded()) return 0;
    token = g_SceneDispatchToken;
    if (token != s_token) { s_token = token; pe_forget(); s_pending = -1; s_held = 0; s_frozen = NULL; }

    /* Our menu box closed: the dialogue box left the chosen row in g_MenuEquipSlotIndex. */
    if (s_menu_box && (s_menu_box->state != PE_TEXTBOX_SHOWN || s_menu_box->page_id != PE_BOX_PAGE)) {
        if (g_MenuEquipSlotIndex == PE_STORE_EQUIPMENT || g_MenuEquipSlotIndex == PE_STORE_ITEMS) {
            s_pending = g_MenuEquipSlotIndex;
            s_pending_wait = PE_STORAGE_DELAY;
        }
        s_menu_box = NULL;
        s_swallow = PE_SWALLOW_FRAMES;
        if (s_warner) pe_release();
    }
    /* The Cross that picks a row can also start his talk this frame, before the boxes draw;
     * his first line opening resets the shared choice cursor to the top row (Store
     * equipment). Put our row back so the Cross picks what the player chose. */
    if (s_menu_box) {
        if (Draw_GetTextboxPageState(PE_LINE_PAGE_1) || Draw_GetTextboxPageState(PE_LINE_PAGE_2)) {
            g_DialogueChoiceCursor = s_menu_cursor;
            g_MenuEquipSlotIndex = -1;
        } else {
            s_menu_cursor = g_DialogueChoiceCursor;
        }
    }
    if (!s_enabled || !pe_shopw_enabled()) {             /* an open menu still unlocks as it closes */
        if (!s_menu_box) { pe_unfreeze(); pe_release(); pe_forget(); }
        return 0;
    }
    if (s_warner && !pe_is_warner(s_warner)) { s_frozen = NULL; s_held = 0; pe_forget(); }   /* a room reload brings a fresh actor */
    if (!s_warner) {
        /* Found by his retail talk script (checked word for word), in whatever room he stands. */
        if (!(s_warner = pe_find_warner())) return 0;
        pe_mod_log("pe_economy", "Warner found in room %d: storage and Workshop offered", pe_room_number());
    }
    {
        ScriptNode *talk = pe_talk_node();
        if (talk && (s_menu_box || s_swallow > 0) && !s_boxes_this_talk) {
            /* The Cross that picked a row in our menu started a talk: end it quietly, no menu
             * (its lock is released with the menu's, or below if the menu is already gone). */
            if (pe_take_over(talk)) pe_log("field_wayne", "talk from the menu's Cross ended");
        } else if (talk && s_heard) {
            if (pe_take_over(talk)) {
                pe_log("field_wayne", "lines skipped (heard before)");
                if (!s_boxes_this_talk) s_boxes_this_talk = 1;   /* the menu follows */
            }
        } else if (talk) {
            pe_talk_tick();
            if ((const u8 *)talk->current.script == s_warner->scriptBase + PE_LINE2_WAIT_OFF) {
                if (Draw_GetTextboxPageState(PE_LINE_PAGE_2)) {
                    talk->active = PE_FROZEN;           /* wait for his last line ourselves */
                    s_frozen = talk;
                }
                else
                    pe_take_over(talk);                 /* it closed: his lock carries on into our menu */
            }
        } else if (s_boxes_this_talk) {                 /* his talk just ended: our menu */
            if (s_boxes_this_talk >= 2) s_heard = 1;    /* both lines seen once: skip them from now on */
            s_boxes_this_talk = 0;
            s_last_message = NULL;
            if (!s_menu_box) pe_open_menu();
        } else {
            if (s_swallow > 0) --s_swallow;             /* counts only frames with no talk */
            if (!s_menu_box) pe_release();              /* a lock taken over with no menu to hold it */
        }
    }
    return 0;
}

/* Every frame: once the field is unlocked again, open the storage screen chosen. */
static void pe_field_wayne_tick(void) {
    if (!pe_game_loaded() || !s_warner || pe_talk_running() || s_pending < 0) return;
    if (s_pending_wait > 0) { --s_pending_wait; return; }
    if (pe_free_field()) {
        const int mode = s_pending;
        s_pending = -1;
        Render_BeginSceneLoad();
        Menu_OpenEquipScreen(mode);
        g_GameState.flags |= 0x9000;
        g_GameStateFlags |= 4;
        pe_log("field_wayne", "storage mode=%d", mode);
    }
}

void pe_field_wayne_register(const char *mod_id) {
    pe_menu_register_toggle(PE_PAGE_ECONOMY, "Warner helps out", "Warner offers storage and the Workshop wherever he stands guard.",
                            &s_enabled, pe_field_wayne_tick);
    pe_hook_add(PE_HOOK_TEXTBOXES, pe_on_textboxes, NULL, PE_PRIORITY_EARLY, mod_id);  /* before pe_wayne_menu */
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_economy.h"
int pe_field_wayne_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){if((!in&&!out)||(s_menu_box||s_pending>=0||s_boxes_this_talk||s_last_message||s_held||s_frozen))return 0;if(apply&&(!in||!pe_field_wayne_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};
PE_CK_U(w,s_enabled,0,1,1);

if(apply&&w.ok){s_token=-1;s_warner=NULL;s_boxes_this_talk=0;s_last_message=NULL;s_menu_box=NULL;s_pending=-1;s_pending_wait=0;s_text_ready=0;memset(s_text,0,sizeof s_text);}return w.ok&&w.at==4u;}
#endif
