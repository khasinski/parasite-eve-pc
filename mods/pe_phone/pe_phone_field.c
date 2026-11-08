/* Phone calls in the field: spotting the save phone, "Stay on the line?", and playing a call.
 *
 * Spotting (R1-R3 of the plan). The phone's question is one page copied into 16 message banks:
 * "Will you save your progress?" with a choice of two (Yes / No). It is recognised when its box
 * finds the page (PE_HOOK_TEXT_PAGE, ahead of every other page handler, so a language pack's text
 * doesn't hide it), and the room (pe_room_number) and story (script variable 74) are logged:
 * "phone room=<n> story=0x<v> day=<d>" in pe_play.log, the data phase 2 needs. The story save near
 * the end ("{name}: ...But... Will you save your progress?") starts with Aya's name and gets no
 * call. The phone script then opens the save screen (opcode 239); when a card save is published
 * the host announces "save.committed". Quick saves and room checkpoints never commit.
 *
 * Asking. After that save, once the save screen has closed and the field has been free for a few
 * frames, the field is locked the way a talking actor's script does it (field event 0xFE, here
 * sent from Aya; 0xFF unlocks) and a dialogue box asks "Stay on the line?" with No first: one
 * Cross goes straight back to the game. Circle is No too. A phone question with no save after it
 * (the player said No, or backed out of the save screen) is forgotten once the field is free.
 *
 * The call. One box per page, in the game's glyphs: "*dial... ring... ring...*", the call's lines
 * ("{name}: ..." in the game's own dialogue band at the bottom, the caller's in a window higher
 * up), then "*click*". Lines are wrapped with the game's own glyph widths (and the player's name)
 * to at most three lines a box; a longer line runs on into another box with the speaker's name
 * again. Cross goes on, Circle hangs up (straight to "*click*"). The field stays locked until the
 * last box closes, so Aya can't walk off and no battle starts. Nothing in the game's scripts is
 * changed and nothing is saved here (the heard record is pe_phone_calls.c's). */
#include <stdio.h>
#include <string.h>

#include "pe_game.h"
#include "pe_gametext.h"
#include "pe_mod_host.h"
#include "pe_phone.h"
#include "pe1/field_sfx.h"
#include "pe1/game_state.h"
#include "pe1/pad_state.h"
#include "pe1/scene_dispatch.h"
#include "pe1/script_queue.h"
#include "pe1/textbox.h"
#include "pe1/textbox_open.h"

#define PE_PAGE_ID        0x7F01   /* our boxes' page id: no room message uses it (Warner's is 0x7F00) */
#define PE_TEXTBOX_SHOWN  2
#define PE_PAD_CROSS      0x0100u  /* field action bits (Boot_BuildRenderFlagTable): confirm */
#define PE_PAD_CIRCLE     0x0400u  /* cancel */
#define PE_PAD_UP         0x0008u  /* the dialogue choice cursor's up and down (Menu_DrawTextboxEntries) */
#define PE_PAD_DOWN       0x0020u
#define PE_FIELD_LOCK     0xFE
#define PE_FIELD_UNLOCK   0xFF
#define PE_STORY_VAR      74
#define PE_GLYPH_NAME     0xFAu    /* the player's name */
#define PE_GLYPH_SPACE    0x0Fu
#define PE_GLYPH_STAR     0x0Du
#define PE_ASK_FREE       8        /* free field frames after the save before asking */
#define PE_FORGET_FREE    30       /* free field frames after a phone question with no save */
#define PE_MAX_LINES      3        /* lines per box */
#define PE_BAND_X         0x14     /* the game's dialogue band: text from x 20 */
#define PE_TEXT_MAX_W     272      /* widest line, in pixels (the game's own run to about 275) */
#define PE_WINDOW_Y       28       /* the caller's window, higher than Aya's band (y 174) */
#define PE_MAX_PAGES      48
#define PE_PAGE_BYTES     240

enum { ST_IDLE, ST_ARMED, ST_SAVED, ST_ASK, ST_CALL };

typedef struct PePage {
    u8 text[PE_PAGE_BYTES];
    u8 style;          /* 0: the game's dialogue band; 1: a window */
    u8 lines;
    u16 width;         /* widest line, pixels */
} PePage;

static int s_state = ST_IDLE;
static int s_token = -1;
static int s_free;                   /* free field frames counted in ST_ARMED / ST_SAVED */
static int s_force;                  /* debug: 1 ask, 2 call, whatever the setting */
static int s_locked;                 /* we sent a field lock and owe the unlock */
static TextboxEntry *s_box;          /* the box showing our page */
static PePage s_pages[PE_MAX_PAGES];
static int s_page_count, s_page_at, s_click_page;
static const PePhoneCall *s_call;
static int s_hung_up;
static int s_room = -1, s_seen_room = -1;
static unsigned s_story, s_seen_story;
static int s_checked;
static int s_ev_start = -1, s_ev_end = -1;
static u32 s_pad_pressed, s_pad_bits;   /* the pad, taken from the field while our boxes are up */
static int s_pad_taken;

/* ---- text in the game's glyphs ---- */

static u8 pe_glyph_of(char c) { return c == '*' ? PE_GLYPH_STAR : pe_glyph(c); }

static int pe_advance(u8 g) {
    if (g == PE_GLYPH_NAME) {
        int w = 0, i;
        for (i = 0; i < g_TextboxNameGlyphs.count && i < (int)sizeof g_TextboxNameGlyphs.glyphs; ++i)
            w += pe_advance(g_TextboxNameGlyphs.glyphs[i]);
        return w;
    }
    if (!g_TextboxGlyphWidths || g >= g_TextboxGlyphCount) return 12;
    return (int)g_TextboxGlyphWidths[g].right - (int)g_TextboxGlyphWidths[g].left;
}

/* ASCII (with "{name}") to glyphs; returns the count. */
static int pe_encode_text(u8 *out, int cap, const char *s) {
    int n = 0;
    while (s && *s && n < cap) {
        if (!strncmp(s, "{name}", 6)) { out[n++] = PE_GLYPH_NAME; s += 6; continue; }
        out[n++] = pe_glyph_of(*s++);
    }
    return n;
}

static int pe_width(const u8 *g, int n) {
    int w = 0, i;
    for (i = 0; i < n; ++i) w += pe_advance(g[i]);
    return w;
}

static void pe_page_start(PePage *p, int style) {
    memset(p, 0, sizeof *p);
    p->style = (u8)style;
}

/* Builds the boxes for one speaker's line into s_pages: the prefix ("{name}:" / "Caller:") then
 * the words, wrapped at `max` pixels, at most PE_MAX_LINES lines a box, the prefix again on each
 * box. Returns the boxes made, -1 when out of room. */
static int pe_add_line(const char *prefix_ascii, int aya, const char *text, int style) {
    u8 prefix[48], body[512];
    int np, nb, i = 0, boxes = 0;
    const int margin = style ? pe_advance(PE_GLYPH_SPACE) : 0;
    const int max = PE_TEXT_MAX_W - 2 * margin;
    if (aya) { prefix[0] = PE_GLYPH_NAME; prefix[1] = pe_glyph(':'); np = 2; }
    else {
        np = pe_encode_text(prefix, (int)sizeof prefix - 1, prefix_ascii);
        prefix[np++] = pe_glyph(':');
    }
    nb = pe_encode_text(body, (int)sizeof body, text);
    while (i < nb && body[i] == PE_GLYPH_SPACE) ++i;
    while (i < nb || boxes == 0) {
        const int space = pe_advance(PE_GLYPH_SPACE);
        PePage *p;
        int line_w, at = 0, k, words_on_line = 1;           /* the prefix counts as the first word */
        if (s_page_count >= PE_MAX_PAGES) return -1;
        p = &s_pages[s_page_count++];
        pe_page_start(p, style);
        ++boxes;
        p->lines = 1;
        if (margin) p->text[at++] = PE_GLYPH_SPACE;
        memcpy(p->text + at, prefix, (size_t)np);
        at += np;
        line_w = pe_width(prefix, np);
        p->width = (u16)line_w;
        while (i < nb) {
            int j = i, ww;
            while (j < nb && body[j] != PE_GLYPH_SPACE) ++j;               /* the word body[i..j) */
            ww = pe_width(body + i, j - i);
            if (at + (j - i) + 4 >= PE_PAGE_BYTES) break;                  /* no room: the next box */
            if (words_on_line && line_w + space + ww > max) {
                if (p->lines >= PE_MAX_LINES) break;                      /* the next box */
                p->text[at++] = G_NEWLINE;
                ++p->lines;
                if (margin) p->text[at++] = PE_GLYPH_SPACE;
                line_w = 0;
                words_on_line = 0;
            }
            if (words_on_line) { p->text[at++] = PE_GLYPH_SPACE; line_w += space; }
            for (k = i; k < j; ++k) p->text[at++] = body[k];
            line_w += ww;
            ++words_on_line;
            if ((unsigned)line_w > p->width) p->width = (u16)line_w;
            i = j;
            while (i < nb && body[i] == PE_GLYPH_SPACE) ++i;
        }
        p->width = (u16)(p->width + 2 * margin);
        p->text[at] = G_END;
        if (words_on_line == 1 && p->lines == 1 && i < nb) return -1;   /* a word that fits no box */
    }
    return boxes;
}

static int pe_add_plain(const char *text) {
    PePage *p;
    int n;
    if (s_page_count >= PE_MAX_PAGES) return -1;
    p = &s_pages[s_page_count++];
    pe_page_start(p, 0);
    n = pe_encode_text(p->text, PE_PAGE_BYTES - 1, text);
    p->text[n] = G_END;
    p->lines = 1;
    p->width = (u16)pe_width(p->text, n);
    return 1;
}

/* All the pages of `call` (NULL: nobody answers). */
static void pe_build_call(const PePhoneCall *call) {
    int i;
    s_page_count = 0;
    pe_add_plain("*dial... ring... ring...*");
    if (!call) {
        pe_add_plain("*ring... ring...*");
        pe_add_plain("Nobody's picking up.");
    } else {
        for (i = 0; i < call->line_count; ++i) {
            const PePhoneLine *l = &call->lines[i];
            if (pe_add_line(call->caller, l->speaker == PE_CALL_AYA, l->text, l->speaker == PE_CALL_AYA ? 0 : 1) < 0) break;
        }
    }
    if (s_page_count >= PE_MAX_PAGES) s_page_count = PE_MAX_PAGES - 1;
    s_click_page = s_page_count;
    pe_add_plain("*click*");
}

int pe_phone_field_check_calls(void) {
    int bad = 0, i, n = pe_phone_calls_count();
    if (!g_TextboxGlyphWidths || !g_TextboxGlyphCount) return -1;
    for (i = 0; i < n; ++i) {
        const PePhoneCall *c = pe_phone_call_at(i);
        int k, ran_on = 0, too_wide = 0;
        if (!c) continue;
        pe_build_call(c);
        for (k = 0; k < s_page_count; ++k) if (s_pages[k].width > PE_TEXT_MAX_W) ++too_wide;
        ran_on = s_page_count - 2 - c->line_count;
        if (too_wide || ran_on > 0) {
            ++bad;
            pe_mod_log(PE_PHONE_MOD_ID, "call %s: %d box(es) too wide, %d line(s) ran on into another box", c->key, too_wide, ran_on);
        }
    }
    s_page_count = 0;
    pe_mod_log(PE_PHONE_MOD_ID, "checked %d calls with the game's font: %d need a look", n, bad);
    return bad;
}

/* ---- boxes and the field lock ---- */

static void pe_field_event(int value) {
    BattleEntity *aya = pe_player();
    Script_QueueFieldSfx(value, 0, 0, aya ? aya->fieldSfxId : 0, 0);
}

static void pe_lock(void) {
    if (!s_locked) { pe_field_event(PE_FIELD_LOCK); s_locked = 1; }
}

static void pe_unlock(void) {
    if (s_locked) { pe_field_event(PE_FIELD_UNLOCK); s_locked = 0; }
}

static int pe_box_ours(void) {
    return s_box && s_box->state == PE_TEXTBOX_SHOWN && s_box->page_id == PE_PAGE_ID;
}

static int pe_any_box(void) {
    int i;
    for (i = 0; i < 4; ++i) if (g_TextboxEntries[i].state) return 1;
    return 0;
}

static void pe_close_box(void) {
    if (pe_box_ours()) s_box->state = 0;
    s_box = NULL;
}

/* Shows `text` (style 0 band or a window `width` x `lines`) in our box, opening one if needed. */
static int pe_show(const u8 *text, int style, int lines, int width) {
    TextboxEntry *e = pe_box_ours() ? s_box : NULL;
    int i;
    for (i = 0; !e && i < 4; ++i) if (g_TextboxEntries[i].state == 0) e = &g_TextboxEntries[i];
    if (!e) return 0;
    e->page_id = PE_PAGE_ID;
    e->style = (u8)style;
    e->background = style ? 0 : 1;                       /* the band's shade, as the game's own talk */
    if (style) {
        const int w = width + 8 > 304 ? 304 : width + 8;
        e->width = (u16)w;
        e->x = (u16)((320 - w) / 2);
        e->y = PE_WINDOW_Y;
        e->height = (u16)(lines * 12 + 10);
    } else {
        e->x = PE_BAND_X;
        e->y = 0xAE;
        e->width = 280;
        e->height = 54;
    }
    e->control.flags &= ~(0x000F0000u | 0x00100000u | 0x00200000u | 0x01C00000u | 0x02000000u);
    e->control.parts.delay = 0;
    e->control.parts.elapsed = 0;
    e->message = (u8 *)text;
    e->state = PE_TEXTBOX_SHOWN;
    g_DialogueChoiceCursor = 0;
    g_MenuEquipSlotIndex = -1;
    s_box = e;
    return 1;
}

static u8 s_question[96];

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
    return pe_show(s_question, 0, 3, 0);
}

static int pe_show_page(int index) {
    const PePage *p = &s_pages[index];
    s_page_at = index;
    return pe_show(p->text, p->style, p->lines, p->width);
}

/* ---- the flow ---- */

static void pe_to_idle(void) {
    s_state = ST_IDLE;
    s_free = 0;
    s_force = 0;
    s_call = NULL;
}

static void pe_reset(const char *why) {
    if (s_state == ST_ASK || s_state == ST_CALL) pe_mod_log(PE_PHONE_MOD_ID, "call ended: %s", why);
    pe_close_box();
    s_locked = 0;                    /* a room load or a loaded save starts the field afresh */
    pe_to_idle();
}

static void pe_end(const char *how) {
    pe_close_box();
    pe_unlock();
    if (s_state == ST_CALL && s_call) {
        pe_phone_calls_played(s_call);
        pe_event_emit(s_ev_end, (void *)s_call);
    }
    pe_log("phone_call", "end=%s key=%s", how, s_call ? s_call->key : "-");
    pe_to_idle();
}

static PePhoneContext pe_context(void) {
    PePhoneContext x;
    x.room = s_room;
    x.story = s_story;
    x.day = pe_phone_story_day(s_story);
    x.tone_mask = pe_phone_tone_mask();
    return x;
}

static void pe_start_call(void) {
    const PePhoneContext x = pe_context();
    s_call = pe_phone_calls()->pick(&x);
    pe_build_call(s_call);
    s_hung_up = 0;
    if (!pe_show_page(0)) { pe_end("no box free"); return; }
    s_state = ST_CALL;
    if (s_call) pe_event_emit(s_ev_start, (void *)s_call);
    pe_log("phone_call", "key=%s room=%d story=0x%X day=%d boxes=%d", s_call ? s_call->key : "-",
           x.room, x.story, x.day, s_page_count);
}

static void pe_begin(void) {
    const int mode = s_force == 1 ? PE_PHONE_ASK : s_force == 2 ? PE_PHONE_ALWAYS : pe_phone_mode();
    if (mode == PE_PHONE_OFF) { pe_to_idle(); return; }
    if (!pe_player()) { pe_to_idle(); return; }
    pe_lock();
    if (mode == PE_PHONE_ALWAYS) { pe_start_call(); return; }
    if (!pe_show_question()) { pe_end("no box free"); return; }
    s_state = ST_ASK;
}

/* This frame's presses: taken from the field at PE_HOOK_FRAME (pe_on_frame), the game never
 * sees them. */
static u32 pe_pressed(void) {
    if (s_pad_taken) return s_pad_pressed;
    {
        const u32 p = g_FieldPadPressed;
        g_FieldPadPressed &= ~(PE_PAD_CROSS | PE_PAD_CIRCLE);
        return p;
    }
}

static void pe_step_ask(void) {
    const u32 pressed = pe_pressed();
    if (!pe_box_ours() || s_box->message != s_question) { pe_end("question closed"); return; }
    if (pressed & PE_PAD_CROSS) {
        const int yes = g_DialogueChoiceCursor == 1;
        pe_close_box();
        if (yes) pe_start_call();
        else pe_end("no");
    } else if (pressed & PE_PAD_CIRCLE) {
        pe_end("no");
    }
}

static void pe_step_call(void) {
    const u32 pressed = pe_pressed();
    if (!pe_box_ours()) { pe_end("box closed"); return; }
    if (pressed & PE_PAD_CROSS) {
        if (s_page_at + 1 < s_page_count) {
            if (!pe_show_page(s_page_at + 1)) pe_end("no box free");
        } else {
            pe_end(s_hung_up ? "hung up" : "done");
        }
    } else if (pressed & PE_PAD_CIRCLE) {
        if (s_page_at < s_click_page) {
            s_hung_up = 1;
            if (!pe_show_page(s_click_page)) pe_end("no box free");
        }
    }
}

/* While our boxes are up, the pad is ours. Standing at the phone, the Cross that turns a page
 * would also reach the phone and start its save question again under our box (seen headless:
 * the field lock doesn't stop a contact). So the pad is taken right after it is read, before any
 * field logic (PE_HOOK_FRAME), and handed back when the boxes draw, after the field logic: held
 * buttons as they were (so a held Cross isn't a new press next frame), presses only the choice
 * cursor's up and down. */
static int pe_on_frame(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    s_pad_taken = 0;
    if (s_state != ST_ASK && s_state != ST_CALL) return 0;
    s_pad_pressed = g_FieldPadPressed;
    s_pad_bits = g_FieldPadBits;
    g_FieldPadPressed = 0;
    g_FieldPadBits = 0;
    s_pad_taken = 1;
    return 0;
}

static int pe_settled(void) {
    return pe_free_field() && !pe_any_box() && !(g_GameState.flags & 0x9000);
}

/* Every frame, before the boxes draw. */
static int pe_on_textboxes(void *ctx, void *user) {
    int token;
    (void)ctx;
    (void)user;
    if (!pe_game_loaded()) return 0;
    token = g_SceneDispatchToken;
    if (token != s_token) { s_token = token; pe_reset("room changed"); }
    if (!s_checked && g_TextboxGlyphWidths && g_TextboxGlyphCount && s_state == ST_IDLE) {
        s_checked = 1;
        (void)pe_phone_field_check_calls();
    }
    switch (s_state) {
    case ST_ARMED:                                   /* a phone question, no save yet */
        if (pe_settled()) { if (++s_free >= PE_FORGET_FREE) { pe_log("phone", "room=%d saved=0", s_room); pe_to_idle(); } }
        else s_free = 0;
        break;
    case ST_SAVED:                                   /* saved: wait for the field to settle */
        if (pe_settled()) { if (++s_free >= PE_ASK_FREE) pe_begin(); }
        else s_free = 0;
        break;
    case ST_ASK: pe_step_ask(); break;
    case ST_CALL: pe_step_call(); break;
    default: break;
    }
    if (s_pad_taken) {                               /* the field logic has run: hand the pad back */
        s_pad_taken = 0;
        g_FieldPadBits = s_pad_bits;
        g_FieldPadPressed = s_state == ST_ASK ? s_pad_pressed & (PE_PAD_UP | PE_PAD_DOWN) : 0;
    }
    return 0;
}

/* ---- spotting the phone ---- */

/* 1: the phone's save question; 2: the story save (starts with Aya's name); 0: anything else. */
static int pe_phone_question(const u8 *m) {
    char ascii[400];
    int i, choice2 = 0;
    for (i = 0; i < 380 && m[i] != G_END && m[i] != 0xF9; ++i)
        if (m[i] == G_CODE && m[i + 1] == G_CHOICE && m[i + 2] == 2) choice2 = 1;
    if (!choice2) return 0;
    pe_text_to_ascii(ascii, sizeof ascii, m);
    if (!strstr(ascii, "Will you save your progress?")) return 0;
    return m[0] == PE_GLYPH_NAME ? 2 : 1;
}

/* A box found its page, before any language pack swaps the text. */
static int pe_on_page(void *ctx, void *user) {
    PeHookTextPage *page = ctx;
    int kind;
    (void)user;
    if (!pe_game_loaded() || !page || !page->message || !*page->message) return 0;
    if (s_state == ST_ASK || s_state == ST_CALL) return 0;
    kind = pe_phone_question(*page->message);
    if (!kind) return 0;
    s_room = pe_room_number();
    s_story = g_ScriptVariableBank[PE_STORY_VAR];
    s_seen_room = s_room;
    s_seen_story = s_story;
    pe_log("phone", "room=%d story=0x%X day=%d page=%u%s", s_room, s_story, pe_phone_story_day(s_story),
           page->page, kind == 2 ? " story_save=1" : "");
    if (kind == 1 && s_state == ST_IDLE) {
        s_state = ST_ARMED;
        s_free = 0;
    }
    return 0;
}

/* The host: a card save was written. */
static int pe_on_committed(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    if (s_state != ST_ARMED) return 0;
    pe_log("phone", "room=%d saved=1", s_room);
    s_state = ST_SAVED;
    s_free = 0;
    return 0;
}

static int pe_on_reset(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    pe_reset("save loaded or new game");
    return 0;
}

/* ---- debug and F1 ---- */

static void pe_debug(int force) {
    if (!pe_game_loaded()) { pe_toast("Phone: only in a game"); return; }
    if (s_state == ST_ASK || s_state == ST_CALL) return;
    s_room = pe_room_number();
    s_story = g_ScriptVariableBank[PE_STORY_VAR];
    s_force = force;
    s_free = 0;
    s_state = ST_SAVED;
}

void pe_phone_field_debug_ask(void) { pe_debug(1); }
void pe_phone_field_debug_call(void) { pe_debug(2); }

void pe_phone_field_status(char *out, unsigned size) {
    if (s_seen_room < 0) snprintf(out, size, "none yet");
    else snprintf(out, size, "M%03d, story 0x%X, Day %d", s_seen_room, s_seen_story, pe_phone_story_day(s_seen_story));
}

void pe_phone_field_register(const char *mod_id) {
    s_ev_start = pe_event_id("phone.call_start");
    s_ev_end = pe_event_id("phone.call_end");
    pe_hook_add(PE_HOOK_FRAME, pe_on_frame, NULL, PE_PRIORITY_LATE, mod_id);   /* after other mods read the pad */
    pe_hook_add(PE_HOOK_TEXTBOXES, pe_on_textboxes, NULL, PE_PRIORITY_NORMAL, mod_id);
    /* ahead of every other page handler (a language pack's is EARLY), like Wayne's menu */
    pe_hook_add(PE_HOOK_TEXT_PAGE, pe_on_page, NULL, PE_PRIORITY_EARLY - 100, mod_id);
    pe_hook_add(pe_event_id(PE_EVENT_SAVE_COMMITTED), pe_on_committed, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_on_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_on_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
}
