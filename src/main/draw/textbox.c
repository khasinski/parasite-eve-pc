#include "common.h"
#include "pe_hooks.h"
#include "pe1/text_render.h"
#include "pe1/mod_text.h"
#include "pe1/draw_state.h"
#include "pe1/boot_disc_check.h"
#include "pe1/field_pad.h"
#include "pe1/menu_flags.h"
#include "pe1/psyq_bios.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_prim.h"
#include "pe1/textbox.h"
#include "pe1/textbox_open.h"
#ifdef PE_PS5_GPU_NATIVE
#include "pe1/dialogue_alpha.h"
#endif

int g_ScriptCollisionFlag;
u8 *g_TextboxMessageSource;
unsigned char g_TextboxKernGlyph0F;
TextboxRect g_TextboxPendingRect;
TextboxEntry g_TextboxEntries[4];
u8 g_TextboxDrawnThisFrame;
TextboxGlyphPacket g_TextboxChoiceCursor[2];
RenderModeTilePacket g_TextboxFogTilePackets[2];
u8 g_DialogueChoiceCursor;
u8 g_PlayerMovementEnabled;
TextboxNameGlyphs g_TextboxNameGlyphs = {{0x10, 0x48, 0x30}, 3};

/* Native paged packets retain the original mode+primitive command order. */
void Draw_InitDrawModeSprtPacket(void *raw, int tpage) {
    RenderPagedSpritePacket *packet = raw;
    SetDrawTPage(&packet->page, 0, 1, tpage);
    SetSprt(&packet->sprite);
    if (MargePrim(&packet->page, &packet->sprite) != 0) exit(-1);
}

void Draw_InitDrawModeTilePacket(void *raw, int tpage) {
    RenderModeTilePacket *packet = raw;
    SetDrawTPage(&packet->mode, 0, 1, tpage);
    SetTile(&packet->tile);
    if (MargePrim(&packet->mode, &packet->tile) != 0) exit(-1);
}

void Menu_SetEquipSlotIndex(int index) {
    g_TextboxKernGlyph0F = index;
}

void SetDrawTPage(void *, int, int, int);
void SetSprt(void *);
int MargePrim(void *, void *);
void SetShadeTex(void *, int);
void SetSemiTrans(void *, int);
void exit(int);

void Render_SetupFogLayer(void *source) {
    for (unsigned i = 0; i < 4; ++i) {
        TextboxEntry *entry = &g_TextboxEntries[i];
        entry->control.parts.delay = 0;
        entry->control.parts.elapsed = 0;
        entry->state = 0;
        entry->message = NULL;
        entry->style = 0;
        entry->background = 0;
        entry->page_id = -1;
        entry->control.flags &= 0xFFF0FFFF & 0xFFEFFFFF & 0xFFDFFFFF & 0xFE3FFFFF & 0xFDFFFFFF;
    }
    g_DialogueChoiceCursor = 0;
    g_PlayerMovementEnabled = 0;
    g_ScriptCollisionFlag = 0;
    g_TextboxMessageSource = source;
    for (unsigned i = 0; i < 2; ++i) {
        TextboxGlyphPacket *choice = &g_TextboxChoiceCursor[i];
        Draw_InitDrawModeSprtPacket(choice, g_TextboxFontPages[3].tpage);
        SetShadeTex(&choice->sprite, 1);
        choice->sprite.u = 0x70;
        choice->sprite.v = g_TextboxFontPages[3].textureY;
        choice->sprite.width = 24;
        choice->sprite.height = 12;
        choice->sprite.clut = g_TextboxFontPages[3].clut;
        RenderModeTilePacket *fog = &g_TextboxFogTilePackets[i];
        Draw_InitDrawModeTilePacket(fog, GetTPage(0, 0, 0, 0));
        fog->tile.r0 = fog->tile.g0 = fog->tile.b0 = 2;
        fog->tile.w = 320;
        fog->tile.h = 54;
        fog->tile.x0 = 0;
        fog->tile.y0 = 170;
        SetSemiTrans(&fog->tile, 1);
    }
}

void Menu_SetTextCursorRect(int x, int y, int w, int h) {
    g_TextboxPendingRect.x = x;
    g_TextboxPendingRect.y = y;
    g_TextboxPendingRect.width = w;
    g_TextboxPendingRect.height = h;
}

void Draw_CloseTextboxPage(int arg0) {
    int i;
    int idx;

    i = 0;
    arg0 = (s16)arg0;
    while ((unsigned char)i < 4) {
        idx = (unsigned char)i;
        if (g_TextboxEntries[idx].page_id == arg0) {
            if (g_TextboxEntries[idx].state != 0) {
                g_TextboxEntries[idx].state = 0;
                break;
            }
        }
        i++;
    }
}

void Draw_ResetTextboxes(void) {
    int i;
    int idx;
    u32 value;

    for (i = 0; (unsigned char)i < 4; i++) {
        idx = (unsigned char)i;
        value = g_TextboxEntries[idx].control.flags;
        g_TextboxEntries[idx].state = 0;
        value &= 0xFDFFFFFF;
        g_TextboxEntries[idx].control.flags = value;
    }
}

s8 Draw_GetTextboxPageState(int arg0) {
    int i;
    int idx;
    int value;

    value = 0;
    i = 0;
    arg0 = (s16)arg0;
    while ((unsigned char)i < 4) {
        idx = (unsigned char)i;
        if (g_TextboxEntries[idx].page_id == arg0) {
            value = g_TextboxEntries[idx].state;
            break;
        }
        i++;
    }
    return value;
}

void Script_EnableMovement(void) {
    g_PlayerMovementEnabled = 1;
}

void Script_DisableMovement(void) {
    g_PlayerMovementEnabled = 0;
}

void Script_SetCollisionFlag(int value) {
    g_ScriptCollisionFlag = value != 0;
}

/* Opens the first free textbox for message `index`. A styled box takes the
 * current text rectangle; `values` (ended by -1, at most five) fill the
 * message's number slots as decimal digits. */
void Render_SetupColorTable(int inputIndex, int inputStyle, short *inputValues)
{
    unsigned char style = inputStyle;
    short *values = inputValues;
    unsigned char i;
    short index = inputIndex;

    for (i = 0; i < 4; i++) {
        unsigned char slot;

        if (g_TextboxEntries[i].state != 0)
            continue;

        g_TextboxEntries[i].state = 1;
        g_TextboxEntries[i].background = 0;
        g_TextboxEntries[i].page_id = index;
        g_DialogueChoiceCursor = 0;
        {
            int terminator = -1;
            g_MenuEquipSlotIndex = terminator;
        }
        g_TextboxEntries[i].style = style;
        g_TextboxEntries[i].control.flags &= ~0x100000;
        g_TextboxEntries[i].control.flags &= ~0x200000;
        if (style) {
            g_TextboxEntries[i].x = g_TextboxPendingRect.x;
            g_TextboxEntries[i].y = g_TextboxPendingRect.y;
            g_TextboxEntries[i].width = g_TextboxPendingRect.width;
            g_TextboxEntries[i].height = g_TextboxPendingRect.height;
            if (g_TextboxEntries[i].style == 3)
                g_TextboxEntries[i].control.flags |= 0x100000;
        } else if (g_PlayerMovementEnabled) {
            g_TextboxEntries[i].background = 1;
        }

        index = 0;
        for (slot = index; slot < 5; slot++) {
            unsigned short inputNumber = *values++;
            short value = inputNumber;
            unsigned short quotient;
            unsigned char count;

            if (value == -1)
                return;
            count = 0;
            g_TextboxEntries[i].numbers[slot].digits[count] = value - (quotient = value / 10) * 10;
            value = quotient;
            while (value != 0) {
                unsigned short quotient;
                quotient = value / 10;
                count++;
                g_TextboxEntries[i].numbers[slot].digits[count] = value - quotient * 10;
                value = quotient;
            }
            g_TextboxEntries[i].numbers[slot].count = count + 1;
        }
        return;
    }
}

int Menu_GetEquipSlotIndex(void) {
    return g_MenuEquipSlotIndex;
}

signed char g_MenuEquipSlotIndex;
u8 g_TextboxGlyphColorR;
u8 g_TextboxGlyphColorG;
u8 g_TextboxGlyphColorB;

/* The renderer's letters as dialogue sprites at the cursor, in the box's colour, then the cursor
 * moves on by the item's advance. -1 when the packet pool is full (the box stops drawing). */
static int Textbox_EmitTextItem(const PeTextItem *item, TextboxGlyphPacket **next,
                                TextboxGlyphPacket *limit, TextboxPoint *cursor,
                                RenderNativeTag *ordering) {
    unsigned i;
    for (i = 0; i < item->count; ++i) {
        const PeTextLetter *l = &item->letter[i];
        const TextboxFontPage *page = &g_TextboxFontPages[l->page];
        TextboxGlyphPacket *p;
        if (*next >= limit) return -1;
        p = (*next)++;
        SetDrawTPage(p, 0, 1, page->tpage);
        SetSprt(&p->sprite);
        if (MargePrim(p, &p->sprite) != 0) exit(-1);
        p->sprite.x = (s16)(cursor->x + l->x);
        p->sprite.y = (s16)(cursor->y + l->y);
        p->sprite.u = l->u;
        p->sprite.v = l->v;
        p->sprite.width = l->w;
        p->sprite.height = l->h;
        p->sprite.color.bytes.r = g_TextboxGlyphColorR;
        p->sprite.color.bytes.g = g_TextboxGlyphColorG;
        p->sprite.color.bytes.b = g_TextboxGlyphColorB;
        p->sprite.clut = page->clut;
        AddPrim(ordering, p);
#ifdef PE_PS5_GPU_NATIVE
        if (l->code != PE_TEXT_NO_CODE && l->page == 0) PeDialogue_Tag(ordering, p, l->code);
#endif
    }
    cursor->x = (u16)(cursor->x + item->advance);
    return 0;
}

void Menu_DrawTextboxEntries(void) {
    TextboxPoint cursor;
    TextboxPoint uv;
    u8 textboxIndex;
    u8 sectionIndex;
    u8 backgroundDrawn;
    s32 nameIndex;
    RenderNativeTag *orderingTable;
    RenderNativeTag *linkOrdering;
    TextboxNumber *number;
    u8 stopDrawing;
    u16 baseX;
    s32 sectionMask;
    u16 glyphAdvance;
    u16 fontClut;
    TextboxGlyphPacket *linkPacket;
    unsigned char lineHeight;
    u8 glyphWidth;
    TextboxFontPage *font;
    s32 selectionFrame;
    s32 temp_v1_7;
    s32 temp_v1_9;
    s32 var_v0_2;
    s32 nextNameIndex;
    s8 temp_v0_4;
    s8 digitIndex;
    u16 temp_t1;
    u16 temp_v1_10;
    u16 var_v0;
    u16 var_v0_3;
    u32 workingValue;
    u32 temp_v0_3;
    u8 *temp_a0_2;
    u8 *stream;
    s32 temp_a0_3;
    u8 temp_a0_4;
    u8 temp_a0_5;
    u32 nameGlyph;
    u8 temp_v0;
    u8 temp_v0_2;
    u8 temp_v0_5;
    u8 temp_v0_6;
    u8 temp_v0_7;
    u8 temp_v1_11;
    u8 temp_v1_2;
    u8 temp_v1_3;
    void *temp_a1_2;
    void *temp_a1_3;
    RenderSpritePacket *selectionSprite;
    TextboxEntry *textbox;
    TextboxGlyphPacket *auxiliaryPacket;
    TextboxGlyphPacket *currentPacket;
    TextboxGlyphPacket *nextPacket;
    TextboxGlyphPacket *packetLimit;
    TextboxNumber *numberStart;

    g_TextboxDrawnThisFrame = 0;
    PE_HOOK(PE_HOOK_TEXTBOXES, NULL);
    if ((unsigned)g_ActiveDrawSlot >= 2) return;
    sectionIndex = 0;
    backgroundDrawn = 0;
    nextPacket = (TextboxGlyphPacket *)D_800B0E38.other_buffers[1 + g_ActiveDrawSlot];
    if (!nextPacket || !D_800B0E38.ordering[g_ActiveDrawSlot]) return;
    /* Taken before drawing: a box closed below (the button pressed at its
     * end) still has this frame's text linked. */
    for (textboxIndex = 0; textboxIndex < 4; ++textboxIndex) {
        if (g_TextboxEntries[textboxIndex].state != 0) g_TextboxDrawnThisFrame = 1;
    }
    packetLimit = nextPacket + TEXTBOX_GLYPH_PACKET_CAPACITY;
#ifdef PE_PS5_GPU_NATIVE
    if(g_TextboxDrawnThisFrame)PeDialogue_Prepare();
#endif
    if (g_ScriptCollisionFlag != 0) {
        AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 2,
                &g_TextboxFogTilePackets[g_ActiveDrawSlot]);
    }
    textboxIndex = 0;
    sectionMask = 0xFFF0FFFF;
    do {
        if (g_TextboxEntries[textboxIndex].state != 0) {
            orderingTable = D_800B0E38.ordering[g_ActiveDrawSlot] + 1;
            textbox = &g_TextboxEntries[textboxIndex];
            glyphWidth = 12;
            stopDrawing = 0;
            if (textbox->state == 1) {
                textbox->control.parts.delay = 0U;
                textbox->control.parts.elapsed = 0U;
                textbox->control.flags = textbox->control.flags & sectionMask;
                textbox->message = g_TextboxMessageSource;
                while (1) {
                    temp_a0_2 = textbox->message;
                    temp_v0 = *temp_a0_2;
                    if (temp_v0 == 0xFF || temp_v0 == 0xF9) {
                        textbox->message = temp_a0_2 + 2;
                        if (temp_a0_2[1] == 0xFE && temp_a0_2[2] == textbox->page_id) {
                            textbox->message = temp_a0_2 + 3;
                            break;
                        }
                    }
                    textbox->message++;
                }
                PE_HOOK(PE_HOOK_TEXT_PAGE, (&(PeHookTextPage){g_TextboxMessageSource,
                                                              (unsigned)textbox->page_id, &textbox->message}));
            }
            baseX = 0x14;
            cursor.y = 0xAE;
            cursor.x = 0x14;
            g_TextboxGlyphColorB = 0x80;
            g_TextboxGlyphColorG = 0x80;
            g_TextboxGlyphColorR = 0x80;
            temp_v1_2 = textbox->style;
            if (temp_v1_2 != 0) {
                if (temp_v1_2 == 3) {
                    cursor.y = 0xAE;
                    cursor.x = 0x14;
                    g_TextboxGlyphColorR = g_TextboxHighlightColorR;
                    g_TextboxGlyphColorG = g_TextboxHighlightColorG;
                    g_TextboxGlyphColorB = g_TextboxHighlightColorB;
                } else {
                    temp_t1 = textbox->x;
                    baseX = temp_t1;
                    cursor.x = temp_t1;
                    if (!(textbox->control.flags & 0x02000000)) {
                        var_v0 = textbox->y + 6;
                    } else {
                        var_v0 = textbox->y;
                    }
                    cursor.y = var_v0;
                    if (textbox->style == 1) {
                        Draw_SetCursor(textbox->x, textbox->y);
                        Draw_AllocColorGradient(textbox->width, textbox->height, 0, 0);
                    }
                }
            } else {
                if ((textbox->background != 0) && (backgroundDrawn == 0) && (g_ScriptCollisionFlag == 0)) {
                    backgroundDrawn = 1;
                    AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 2,
                            &g_TextboxFogTilePackets[g_ActiveDrawSlot]);
                }
            }
            numberStart = textbox->numbers;

            stream = textbox->message;
            number = numberStart;
            glyphAdvance = glyphWidth;
            currentPacket = nextPacket;
            lineHeight = 12;
            for (;;) {
                font = &g_TextboxFontPages[0];
                temp_v1_3 = *stream;
                {   /* every letter is the renderer's; the switch below only sees the box's commands */
                    PeTextItem item;
                    if (PeText_Read(PE_TEXT_DIALOGUE, stream, ~0u, 1, &item)) {
                        if (item.stop) return;
                        if (Textbox_EmitTextItem(&item, &nextPacket, packetLimit, &cursor, orderingTable) < 0) return;
                        currentPacket = nextPacket;
                        stream += item.bytes;
                        continue;
                    }
                }
                switch (temp_v1_3) {
                case 0xF7:
                    stream += 1;
                    cursor.x = baseX;
                    cursor.y += lineHeight;
                    break;
                case 0xF8:
                    if ((textbox->state == 2) && (g_FieldPadPressed & 0x100)) {
                        do {
                            temp_v0 = *stream;
                            stream++;
                        } while (temp_v0 != 0xF8);
                        textbox->message = stream;
                    }
                    stopDrawing = 1;
                    break;
                case 0xF9:
                    textbox->state = 0U;
                    stopDrawing = 1;
                    break;
                case 0xFF:
                    if ((textbox->state == 2) && (g_FieldPadPressed & 0x100) &&
                        !(textbox->control.flags & 0x02000000)) {
                        textbox->state = 0U;
                    }
                    stopDrawing = 1;
                    break;
                case 0xFA: {
                    int at = 0;
                    while (at < g_TextboxNameGlyphs.count) {
                        PeTextItem name;
                        unsigned left = (unsigned)(g_TextboxNameGlyphs.count - at);
                        if ((unsigned)at >= sizeof(g_TextboxNameGlyphs.glyphs)) return;
                        if (left > sizeof(g_TextboxNameGlyphs.glyphs) - (unsigned)at)
                            left = sizeof(g_TextboxNameGlyphs.glyphs) - (unsigned)at;
                        PeText_Read(PE_TEXT_DIALOGUE_NAME, g_TextboxNameGlyphs.glyphs + at, left, 1, &name);
                        if (name.stop) return;
                        if (Textbox_EmitTextItem(&name, &nextPacket, packetLimit, &cursor, orderingTable) < 0) return;
                        at += (int)name.bytes;
                    }
                    currentPacket = nextPacket;
                    stream += 1;
                    break;
                }
                case 0xFB:
                    stream += 1;
                    temp_a0_3 = *stream;
                    switch (temp_a0_3) {
                    case 4:
                        g_TextboxGlyphColorR = 0xFF;
                        g_TextboxGlyphColorB = 0;
                        g_TextboxGlyphColorG = 0;
                        stream += 1;
                        break;
                    case 5:
                        g_TextboxGlyphColorB = 0x80;
                        g_TextboxGlyphColorG = 0x80;
                        g_TextboxGlyphColorR = 0x80;
                        stream += 1;
                        break;
                    case 6: {
                        s32 sectionTerminator = 6;
                        if (g_FieldPadPressed & 0x100) {
                        section_again:
                            workingValue = textbox->control.flags;
                            if (sectionIndex == ((workingValue >> 16) & 15)) {
                                stream++;
                                textbox->control.flags = (workingValue & sectionMask) |
                                                         (((sectionIndex + 1) & 15) << 16);
                                stopDrawing = 1;
                                goto section_again;
                            }
                            if (*stream != sectionTerminator)
                                goto section_next;
                        } else {
                            if (sectionIndex != (textbox->control.parts.bits & 15))
                                goto section_skip;
                            stopDrawing = 1;
                            sectionIndex++;
                            break;
                        }
                        goto section_skip;
                    }
                    case 7:
                        stream++;
                        temp_v0_2 = *stream;
                        temp_a0_4 = textbox->control.parts.elapsed;
                        textbox->control.parts.delay = temp_v0_2;
                        if (temp_a0_4 < temp_v0_2) {
                            if (sectionIndex != (textbox->control.parts.bits & 15))
                                goto section_skip;
                            textbox->control.parts.elapsed = temp_a0_4 + 1;
                            stopDrawing = 1;
                        }
                        else
                        {
                    section_delay_again:
                        if (sectionIndex == (textbox->control.parts.bits & 15)) {
                            textbox->control.parts.elapsed = 0;
                            textbox->control.parts.delay = *stream;
                            temp_v0_3 = textbox->control.flags;
                            stream++;
                            textbox->control.flags = (temp_v0_3 & sectionMask) |
                                                     (((((temp_v0_3 >> 16) & 15) + 1) & 15) << 16);
                            goto section_delay_again;
                        }
                        if (textbox->control.parts.elapsed != 0) {
                    section_skip:
                        stream++;
                        }
                        }
                    section_next:
                        sectionIndex++;
                        break;
                    case 8:
                        if (number >= numberStart + 5 || number->count > 5) return;
                        for (digitIndex = number->count; digitIndex > 0; digitIndex--) {
                            PeTextItem digit;
                            digit.count = 1;
                            PeText_DialogueDigit(number->digits[digitIndex - 1], &digit.letter[0]);
                            digit.advance = glyphAdvance;
                            if (Textbox_EmitTextItem(&digit, &nextPacket, packetLimit, &cursor, orderingTable) < 0) return;
                        }
                        currentPacket = nextPacket;
                        stream += 1;
                        number++;
                        break;
                    case 9: {
                        TextboxPoint *selectionCursor = &cursor;
                        stream++;
                        temp_v1_7 = textbox->control.flags | 0x200000;
                        textbox->control.flags = temp_v1_7;
                        workingValue = (temp_v1_7 & 0xFE3FFFFF) | ((*stream & 7) << 0x16);
                        textbox->control.flags = workingValue;
                        temp_v1_9 = (workingValue >> 0x16) & 7;
                        if (g_FieldPadPressed & 0x20) {
                            temp_v0_5 = g_DialogueChoiceCursor + 1;
                            g_DialogueChoiceCursor = temp_v0_5;
                            if ((s8)temp_v0_5 >= temp_v1_9) {
                                g_DialogueChoiceCursor = temp_v1_9 - 1;
                            }
                        }
                        if (g_FieldPadPressed & 8) {
                            temp_v0_6 = g_DialogueChoiceCursor - 1;
                            g_DialogueChoiceCursor = temp_v0_6;
                            if ((s8)temp_v0_6 < 0) {
                                g_DialogueChoiceCursor = 0;
                            }
                        }
                        if (g_FieldPadPressed & 0x100) {
                            g_MenuEquipSlotIndex = g_DialogueChoiceCursor;
                        }
                        stream++;
                        selectionFrame = g_ActiveDrawSlot;
                        selectionSprite = &g_TextboxChoiceCursor[selectionFrame].sprite;
                        selectionSprite->x = selectionCursor->x;
                        selectionSprite->y = (s16)(selectionCursor->y + ((s8)g_DialogueChoiceCursor * 12));
                        AddPrim(D_800B0E38.ordering[selectionFrame] + 1,
                                &g_TextboxChoiceCursor[selectionFrame]);
                        break;
                    }
                    }
                    break;
                default:
                    stream++; /* letters are read above; nothing else reaches here */
                    break;
                }
                if (stopDrawing != 0)
                    break;
            }
            glyphWidth = 12;
            if (textbox->state == 1) {
                textbox->state = 2U;
            }
        }
        textboxIndex += 1;
    } while (textboxIndex < 4U);
}

unsigned char D_8009CEB4;
short D_8009CEB8;
short D_8009CEBC;
short D_8009CEC0;
unsigned char g_TextboxHighlightColorR;
unsigned char g_TextboxHighlightColorG;
unsigned char g_TextboxHighlightColorB;


void Draw_SetLightingParams(short arg0, short arg1, short arg2, unsigned char arg3, unsigned char arg4, unsigned char arg5) {
    D_8009CEB8 = arg0;
    D_8009CEB4 = 1;
    D_8009CEBC = arg1;
    D_8009CEC0 = arg2;
    g_TextboxHighlightColorR = arg3;
    g_TextboxHighlightColorG = arg4;
    g_TextboxHighlightColorB = arg5;
}

void Menu_SetHighlightColor(int arg0, int arg1, int arg2, int arg3) {
    g_TextboxHighlightColorR = arg1;
    g_TextboxHighlightColorG = arg2;
    g_TextboxHighlightColorB = arg3;
}
