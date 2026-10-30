#ifndef PE1_TEXTBOX_H
#define PE1_TEXTBOX_H

#include "common.h"
#include "pe1/render_packets.h"
#include "pe1/render_prim.h"
#include "pe1/field_bg_load.h"

/* Textbox / on-screen dialogue entry, walked by Menu_DrawTextboxEntries.
 * g_TextboxEntries is an array of native runtime entries. Each
 * frame the engine walks the active entries and types out / draws their text.
 * See include/pe1/text.h for the surrounding text subsystem notes. */

typedef struct TextboxNumber {
    u8 digits[5];
    s8 count;
} TextboxNumber;
typedef union TextboxControl {
    s32 flags;
    struct {
        u8 delay, elapsed;
        u16 bits;
    } parts;
} TextboxControl;
typedef struct TextboxEntry {
    u8 state;
    u8 pad01[3];
    u8 *message;
    u8 style, background;
    u8 pad0A[2];
    TextboxControl control;
    s16 page_id;
    u16 x, y, width, height;
    TextboxNumber numbers[5];
} TextboxEntry;

typedef struct TextboxNameGlyphs {
    u8 glyphs[9];
    s8 count;
} TextboxNameGlyphs;
extern TextboxNameGlyphs g_TextboxNameGlyphs;
typedef struct TextboxGlyphSpacing {
    u8 left, right;
} TextboxGlyphSpacing;
typedef struct TextboxPoint {
    u16 x, y;
} TextboxPoint;
typedef RenderPagedSpritePacket TextboxGlyphPacket;

extern TextboxEntry g_TextboxEntries[4];
/* Set by Menu_DrawTextboxEntries when it drew at least one box this frame
 * (even one it closed while drawing); read and cleared by Draw_RenderFrame. */
extern u8 g_TextboxDrawnThisFrame;

/* USA has 78 variable-width base glyphs; extended glyphs use fixed cells. */
extern TextboxGlyphSpacing g_TextboxGlyphSpacing[78];
/* The spacing the dialogue box draws with: the authored table above, unless a mod points it at a
 * longer one to draw glyphs past code 77 (codes from g_TextboxGlyphCount on stop the box). */
extern const TextboxGlyphSpacing *g_TextboxGlyphWidths;
extern unsigned g_TextboxGlyphCount;
enum { TEXTBOX_GLYPH_PACKET_CAPACITY = 0x2400 / 28 };

/* The dialogue choice cursor, one texture-page + sprite packet per frame
 * buffer: a 24x12 cell at u 0x70 of font page 3, placed at the selection
 * cursor plus 12 lines per choice. */
extern TextboxGlyphPacket g_TextboxChoiceCursor[2];
extern RenderModeTilePacket g_TextboxFogTilePackets[2];
/* Text colour bytes of the textbox glyphs. */
extern u8 g_TextboxGlyphColorR, g_TextboxGlyphColorG, g_TextboxGlyphColorB;
extern u8 g_TextboxHighlightColorR;
extern u8 g_TextboxHighlightColorG;
extern u8 g_TextboxHighlightColorB;

/* Runtime entries contain a native message pointer; these are not disk records. */
PE1_STATIC_ASSERT(PE1_OFFSETOF(TextboxEntry, control) ==
                  PE1_OFFSETOF(TextboxEntry, message) + sizeof(void *) + 4,
                  textbox_control_offset);
PE1_STATIC_ASSERT(PE1_OFFSETOF(TextboxEntry, numbers) ==
                  PE1_OFFSETOF(TextboxEntry, control) + 14, textbox_numbers_offset);
#if UINTPTR_MAX == UINT32_MAX
PE1_STATIC_ASSERT(sizeof(TextboxEntry) == 0x38, textbox_entry_size);
#else
PE1_STATIC_ASSERT(sizeof(TextboxEntry) == 0x40, textbox_entry_size);
#endif
PE1_STATIC_ASSERT(sizeof(TextboxNumber) == 6, textbox_number_size);
PE1_STATIC_ASSERT(sizeof(TextboxGlyphPacket) == 3 * sizeof(uintptr_t) + sizeof(RenderSpritePacket), textbox_glyph_packet_size);

void Menu_DrawTextboxEntries(void);
void Draw_ResetTextboxes(void);
void Menu_SetTextCursorRect(int x, int y, int width, int height);
void Render_SetupColorTable(int index, int mode, short *colors);
void Draw_SetCursor(int x, int y);

void Draw_CloseTextboxPage(int arg0);
s8 Draw_GetTextboxPageState(int page);
void Script_SetCollisionFlag(int value);
void Script_EnableMovement(void);
void Script_DisableMovement(void);
int Menu_GetEquipSlotIndex(void);

#endif /* PE1_TEXTBOX_H */
