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
 *     (pe_wayne_menu.c) recognises it and adds Mod Permit Card and Workshop above Cancel;
 *   - when it closes, the field is unlocked (event 0xFF, as his script ends) and, on Store
 *     equipment or Store item, the storage screen opens the way his script opcode would
 *     (Script_BeginSceneTransition: Render_BeginSceneLoad, Menu_OpenEquipScreen, menu flags).
 *     Talk to him again for more.
 * Nothing in his script or the story changes.
 */
#include <string.h>

#include "pe_economy.h"
#include "pe_game.h"
#include "pe_gametext.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe1/field_sfx.h"
#include "pe1/scene_dispatch.h"
#include "pe1/scene_transition.h"
#include "pe1/script_node.h"
#include "pe1/textbox.h"
#include "pe1/textbox_open.h"

void Menu_OpenEquipScreen(int mode);

#define PE_WARNER_MODEL   0x04
#define PE_CONTACT_OFF    0x154   /* his contact task, from the script base */
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

/* Is his contact task running now (a script node inside it)? */
static int pe_talk_running(void) {
    const u8 *from = s_warner->scriptBase + PE_CONTACT_OFF, *to = from + PE_TASK_BYTES;
    int list, guard;
    for (list = 0; list < 3; ++list) {
        const ScriptNode *n = s_warner->scriptNodeLists[list];
        for (guard = 0; n && guard < 16; ++guard, n = n->next) {
            const u8 *at = (const u8 *)n->current.script;
            if (at >= from && at < to) return 1;
        }
    }
    return 0;
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
        if (e->state == 0 || !e->message || e->message == s_last_message) continue;
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
        s_menu_box = e;
        pe_field_event(PE_FIELD_LOCK);
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
    if (token != s_token) { s_token = token; pe_forget(); s_pending = -1; }

    /* Our menu box closed: the dialogue box left the chosen row in g_MenuEquipSlotIndex. */
    if (s_menu_box && (s_menu_box->state != PE_TEXTBOX_SHOWN || s_menu_box->page_id != PE_BOX_PAGE)) {
        if (g_MenuEquipSlotIndex == PE_STORE_EQUIPMENT || g_MenuEquipSlotIndex == PE_STORE_ITEMS) {
            s_pending = g_MenuEquipSlotIndex;
            s_pending_wait = PE_STORAGE_DELAY;
        }
        s_menu_box = NULL;
        if (s_warner) pe_field_event(PE_FIELD_UNLOCK);
    }
    if (!s_enabled || !pe_loot_enabled()) {             /* an open menu still unlocks as it closes */
        if (!s_menu_box) pe_forget();
        return 0;
    }
    if (s_warner && !pe_is_warner(s_warner)) pe_forget();   /* a room reload brings a fresh actor */
    if (!s_warner) {
        /* Found by his retail talk script (checked word for word), in whatever room he stands. */
        if (!(s_warner = pe_find_warner())) return 0;
        pe_mod_log("pe_economy", "Warner found in room %d: storage and Workshop offered", pe_room_number());
    }
    if (pe_talk_running()) {
        pe_talk_tick();
    } else if (s_boxes_this_talk) {                     /* his talk just ended: our menu */
        s_boxes_this_talk = 0;
        s_last_message = NULL;
        if (!s_menu_box) pe_open_menu();
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
int pe_field_wayne_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){if((!in&&!out)||(s_menu_box||s_pending>=0||s_boxes_this_talk||s_last_message))return 0;if(apply&&(!in||!pe_field_wayne_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};
PE_CK_U(w,s_enabled,0,1,1);

if(apply&&w.ok){s_token=-1;s_warner=NULL;s_boxes_this_talk=0;s_last_message=NULL;s_menu_box=NULL;s_pending=-1;s_pending_wait=0;s_text_ready=0;memset(s_text,0,sizeof s_text);}return w.ok&&w.at==4u;}
#endif
