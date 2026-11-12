/* The game side of showing a call: our dialogue box, the field lock, the pad, the font.
 *
 * Box: one of the game's four dialogue boxes, page id 0x7F01 (no room message uses it; Warner's
 * menu is 0x7F00), typed out at once. Band pages use the game's own dialogue band at the bottom
 * (style 0, x 20, y 174, shaded as the game's talk); window pages a window (style 1) centred near
 * the top, so the caller reads apart from Aya.
 *
 * Field lock: field event 0xFE (0xFF unlocks), sent from Aya, as a talking actor's script does.
 *
 * Pad: standing at the phone, the Cross that turns a page would also reach the phone and start its
 * save question again under our box (seen headless: the field lock doesn't stop a contact). So while
 * our boxes are up the pad is taken right after it is read, before any field logic (PE_HOOK_FRAME),
 * and handed back when the boxes draw, after the field logic: held buttons as they were (a held
 * Cross isn't a new press next frame), presses only the choice cursor's up and down if wanted. */
#include "pe_game.h"
#include "pe_phone.h"
#include "pe1/field_sfx.h"
#include "pe1/pad_state.h"
#include "pe1/textbox.h"
#include "pe1/textbox_open.h"

#define PE_PAGE_ID        0x7F01
#define PE_TEXTBOX_SHOWN  2
#define PE_FIELD_LOCK     0xFE
#define PE_FIELD_UNLOCK   0xFF
#define PE_BAND_X         0x14
#define PE_BAND_Y         0xAE
#define PE_WINDOW_Y       28

static TextboxEntry *s_box;
static int s_locked;
static u32 s_pad_pressed, s_pad_bits;
static int s_pad_taken;

/* ---- the font ---- */

int pe_phone_font_ready(void) { return g_TextboxGlyphWidths && g_TextboxGlyphCount; }

int pe_phone_font_advance(uint8_t g) {
    if (g == PE_PHONE_GLYPH_NAME) {
        int w = 0, i;
        for (i = 0; i < g_TextboxNameGlyphs.count && i < (int)sizeof g_TextboxNameGlyphs.glyphs; ++i)
            w += pe_phone_font_advance(g_TextboxNameGlyphs.glyphs[i]);
        return w;
    }
    if (!pe_phone_font_ready() || g >= g_TextboxGlyphCount) return 12;
    return (int)g_TextboxGlyphWidths[g].right - (int)g_TextboxGlyphWidths[g].left;
}

/* ---- the box ---- */

int pe_phone_box_open(void) {
    return s_box && s_box->state == PE_TEXTBOX_SHOWN && s_box->page_id == PE_PAGE_ID;
}

const uint8_t *pe_phone_box_message(void) { return pe_phone_box_open() ? s_box->message : NULL; }

int pe_phone_box_any(void) {
    int i;
    for (i = 0; i < 4; ++i) if (g_TextboxEntries[i].state) return 1;
    return 0;
}

void pe_phone_box_close(void) {
    if (pe_phone_box_open()) s_box->state = 0;
    s_box = NULL;
}

int pe_phone_box_choice(void) { return g_DialogueChoiceCursor; }

int pe_phone_box_show(const uint8_t *text, int style, int lines, int width) {
    TextboxEntry *e = pe_phone_box_open() ? s_box : NULL;
    int i;
    for (i = 0; !e && i < 4; ++i) if (g_TextboxEntries[i].state == 0) e = &g_TextboxEntries[i];
    if (!e) return 0;
    e->page_id = PE_PAGE_ID;
    e->style = (u8)(style == PE_PHONE_WINDOW ? 1 : 0);
    e->background = e->style ? 0 : 1;
    if (e->style) {
        const int w = width + 8 > 304 ? 304 : width + 8;
        e->width = (u16)w;
        e->x = (u16)((320 - w) / 2);
        e->y = PE_WINDOW_Y;
        e->height = (u16)(lines * 12 + 10);
    } else {
        e->x = PE_BAND_X;
        e->y = PE_BAND_Y;
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

/* ---- the field lock ---- */

static void pe_field_event(int value) {
    BattleEntity *aya = pe_player();
    Script_QueueFieldSfx(value, 0, 0, aya ? aya->fieldSfxId : 0, 0);
}

void pe_phone_lock(void) { if (!s_locked) { pe_field_event(PE_FIELD_LOCK); s_locked = 1; } }
void pe_phone_unlock(void) { if (s_locked) { pe_field_event(PE_FIELD_UNLOCK); s_locked = 0; } }

void pe_phone_box_forget(void) {
    pe_phone_box_close();
    s_locked = 0;          /* a room load or a loaded save starts the field afresh */
    s_pad_taken = 0;
}

/* ---- the pad ---- */

void pe_phone_pad_take(void) {
    s_pad_pressed = g_FieldPadPressed;
    s_pad_bits = g_FieldPadBits;
    g_FieldPadPressed = 0;
    g_FieldPadBits = 0;
    s_pad_taken = 1;
}

uint32_t pe_phone_pad_pressed(void) {
    if (s_pad_taken) return s_pad_pressed;
    {                                                  /* not taken this frame: keep it from the game */
        const u32 p = g_FieldPadPressed;
        g_FieldPadPressed &= ~(PE_PHONE_PAD_CROSS | PE_PHONE_PAD_CIRCLE);
        return p;
    }
}

void pe_phone_pad_give(int cursor) {
    if (!s_pad_taken) return;
    s_pad_taken = 0;
    g_FieldPadBits = s_pad_bits;
    g_FieldPadPressed = cursor ? s_pad_pressed & (PE_PHONE_PAD_UP | PE_PHONE_PAD_DOWN) : 0;
}
