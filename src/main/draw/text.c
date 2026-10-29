#include "common.h"
#include "pe1/bounds_check.h"
#include "pe1/draw_area.h"
#include "pe1/draw_buffers.h"
#include "pe1/draw_level_bar.h"
#include "pe1/draw_state.h"
#include "pe1/menu_inventory.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_prim.h"
#include "pe1/text.h"

/* Shared native state; external variables have one storage definition. */
int g_DrawGlyphAdvance;
int g_TextRenderMode;
extern unsigned char *g_DrawPacketCursor;
extern unsigned char *g_DrawPacketArenaBase;
extern int g_DrawTextDimmed;
extern unsigned int g_DrawPrimColor;
extern unsigned int g_DrawColorShaded;
extern unsigned int *g_DrawOrderingTableEntry;
int g_TextCursorX;
int g_TextCursorY;
int *g_TextCursorStackPtr;
int g_DrawDigitFontTpageClut;
int g_DrawDigitFontBaseTexU;
int g_DrawDigitFontBaseTexV;
int g_DrawBufferIndex;
int g_DrawDigitFontBaseTexU;
int g_DrawDigitFontBaseTexV;
int g_DrawDigitFontTpageClut;
int g_DrawTextBoxWidth;
extern int *g_TextCursorStackPtr;
extern int g_TextRenderMode;

int Draw_SetBaseY(int arg0, unsigned int arg1);

void Draw_SetBaseOffsetPosition(int x, int y) {
    Draw_SetBaseY(g_DrawBaseX[0] + x, g_DrawBaseY + y);
}

int Draw_GetBaseY(void) {
    return g_DrawBaseY;
}

extern int g_TextCursorX;
extern int g_TextCursorY;

void Draw_SetCursor(int arg0, int arg1) {
    g_TextCursorX = arg0;
    g_TextCursorY = arg1;
}

void Draw_OffsetCursor(int x, int y) {
    g_TextCursorX += x;
    g_TextCursorY += y;
}

void Draw_StatePush(void) {
    DrawTextCursorPair *cursor;
    int x;
    int y;

    cursor = (DrawTextCursorPair *)g_TextCursorStackPtr;
    if (cursor < (DrawTextCursorPair *)g_TextCursorStackTop) {
        x = g_TextCursorX;
        y = g_TextCursorY;
        g_TextCursorStackPtr = (int *)(cursor + 1);
        cursor->x = x;
        cursor->y = y;
    } else {
        Inv_BoundsCheckAssertStub(2);
    }
}

void Draw_StatePop(void) {
    DrawTextCursorPair *cursor;
    int x;
    int y;

    cursor = (DrawTextCursorPair *)g_TextCursorStackPtr;
    if ((DrawTextCursorPair *)g_TextCursorStackBottom < cursor) {
        x = cursor[-1].x;
        y = cursor[-1].y;
        g_TextCursorStackPtr = (int *)(cursor - 1);
        g_TextCursorX = x;
        g_TextCursorY = y;
    } else {
        Inv_BoundsCheckAssertStub(3);
    }
}

/* The glyph renderers move the cursor at g_TextCursorX/g_TextCursorY and save it on
 * the same stack as Draw_StatePush. */
static inline void SetCursor(int x, int y)
{
    g_TextCursorX = x;
    g_TextCursorY = y;
}

/* Moves the cursor right by `dx`, rewriting y as the stored pair. */
static inline void AdvanceCursor(int dx)
{
    int x = g_TextCursorX;
    int y = g_TextCursorY;

    g_TextCursorX = x + dx;
    g_TextCursorY = y;
}

static inline void PushCursor(void)
{
    DrawTextCursorPair *stack = (DrawTextCursorPair *)g_TextCursorStack;
    if (stack < (DrawTextCursorPair *)g_TextCursorStackTop) {
        int x = g_TextCursorX, y = g_TextCursorY;
        g_TextCursorStack = (int *)(stack + 1);
        stack->x = x;
        stack->y = y;
    } else {
        Inv_BoundsCheckAssertStub(2);
    }
}

/* Saves a known cursor position: the entry is filled before the stack moves. */
static inline void SaveCursor(int x, int y)
{
    DrawTextCursorPair *stack = (DrawTextCursorPair *)g_TextCursorStack;
    if (stack < (DrawTextCursorPair *)g_TextCursorStackTop) {
        stack->x = x;
        stack->y = y;
        g_TextCursorStack = (int *)(stack + 1);
    } else {
        Inv_BoundsCheckAssertStub(2);
    }
}

static inline void PopCursor(void)
{
    DrawTextCursorPair *stack = (DrawTextCursorPair *)g_TextCursorStack;
    if ((DrawTextCursorPair *)g_TextCursorStackBottom < stack) {
        g_TextCursorStack = (int *)(stack - 1);
        SetCursor(stack[-1].x, stack[-1].y);
    } else {
        Inv_BoundsCheckAssertStub(3);
    }
}

extern unsigned int g_DrawPrimColor;
extern unsigned int g_DrawColorShaded;

void Draw_SetColor(int value) {
    g_DrawPrimColor = value;
    g_DrawColorShaded = (value >> 1) & 0x7F7F7F;
}

void Draw_SetStatCompareColor(int arg0, int arg1) {
    unsigned int color;

    if (arg0 >= arg1) {
        color = 0x808080;
        if (arg1 < arg0) {
            color = 0x404080;
        }
    } else {
        color = 0x408080;
    }

    g_DrawPrimColor = color;
    g_DrawColorShaded = color >> 1;
}

#define NULL ((void *)0)

void Draw_AllocPrimRectFull(s32 arg0, s32 arg1, s32 arg2, s32 arg3) {
    DrawAreaRect rect;
    u8 *oldPacket;
    u8 *nextPacket;
    GpuCmdPacket *packet;
    DrawAreaRect *rectPtr;

    if (g_DrawBufferIndex != 0) {
        arg1 += 0xE0;
    }
    packet = NULL;
    rect.x = arg0;
    oldPacket = g_DrawPacketCursor;
    rectPtr = &rect;
    rect.y = arg1;
    rect.w = arg2;
    nextPacket = oldPacket + sizeof(GpuCmdPacket);
    rect.h = arg3;
    if (nextPacket < (g_DrawPacketArenaBase + 0x4000)) {
        g_DrawPacketCursor = nextPacket;
        packet = (GpuCmdPacket *) oldPacket;
    } else {
        Inv_BoundsCheckAssertStub(1);
    }
    if (packet != NULL) {
        SetDrawArea(packet, rectPtr);
    }
    Draw_LinkDrawArea(g_DrawOrderingTableEntry, packet);
}

void Draw_AllocPrimRect(void) {
    DrawAreaRect rect;
    u8 *oldPacket;
    u8 *nextPacket;
    GpuCmdPacket *packet;
    DrawAreaRect *rectPtr;

    rect.w = 0x140;
    rect.x = 0;
    rect.y = 0;
    rect.h = 0xE0;
    if (g_DrawBufferIndex != 0) {
        rect.y = 0xE0;
    }
    packet = NULL;
    oldPacket = g_DrawPacketCursor;
    nextPacket = oldPacket + sizeof(GpuCmdPacket);
    rectPtr = &rect;
    if (nextPacket < (g_DrawPacketArenaBase + 0x4000)) {
        g_DrawPacketCursor = nextPacket;
        packet = (GpuCmdPacket *) oldPacket;
    } else {
        Inv_BoundsCheckAssertStub(1);
    }
    if (packet != NULL) {
        SetDrawArea(packet, rectPtr);
    }
    Draw_LinkDrawArea(g_DrawOrderingTableEntry, packet);
}

extern int g_DrawTextDimmed;

void Draw_SetTextDimmed(int value) {
    g_DrawTextDimmed = value;
}

void Draw_AllocSprite(int index) {
    DrawGlyphDescriptor *glyph = Draw_LookupGlyphDescriptor(index);
    size_t used = (size_t)(g_DrawPacketCursor - g_DrawPacketArenaBase);
    size_t bytes = sizeof(RenderSpritePacket) + sizeof(RenderDrawModePacket);
    if (used >= 0x4000 || bytes >= 0x4000 - used) {
        Inv_BoundsCheckAssertStub(1);
        return;
    }
    RenderSpritePacket *sprite = (RenderSpritePacket *)g_DrawPacketCursor;
    g_DrawPacketCursor += sizeof(*sprite);
    sprite->color.word = g_DrawColorSelect ? g_DrawAlternateColor : g_DrawPrimaryColor;
    sprite->tag.length = 4;
    sprite->color.bytes.code = 0x64;
    sprite->x = g_DrawSpriteX;
    sprite->y = g_DrawSpriteY;
    sprite->u = glyph->u;
    sprite->v = glyph->v;
    sprite->clut = glyph->clut;
    sprite->width = glyph->width;
    sprite->height = glyph->height;
    RenderNativeTag *ordering = (RenderNativeTag *)g_DrawOrderingTableEntry;
    sprite->tag.address = ordering->address;
    ordering->address = (uintptr_t)sprite;
    RenderDrawModePacket *page = (RenderDrawModePacket *)g_DrawPacketCursor;
    g_DrawPacketCursor += sizeof(*page);
    SetDrawTPage(page, 0, 0, ((glyph->mode & 3) << 7) | 7);
    page->tag.address = ordering->address;
    ordering->address = (uintptr_t)page;
}

void Draw_EmitGlyph(s32 arg0, s32 arg1) {
    DrawGlyphDescriptor *glyph;
    u8 *oldPacket;
    u8 *nextPacket;
    RenderTexturedQuad *packet;
    u16 base_x;
    u16 base_y;
    s32 temp;

    packet = 0;
    glyph = Draw_LookupGlyphDescriptor(arg0);
    /* Preserve the descriptor's across packet setup. */
    oldPacket = g_DrawPacketCursor;
    nextPacket = oldPacket + sizeof(RenderTexturedQuad);
    if (nextPacket < (g_DrawPacketArenaBase + 0x4000)) {
        g_DrawPacketCursor = nextPacket;
        packet = (RenderTexturedQuad *)oldPacket;
    } else {
        Inv_BoundsCheckAssertStub(1);
    }

    if (!packet) return;
    if (packet != 0) {
        if (g_DrawTextDimmed != 0) {
            packet->color.word = g_DrawColorShaded;
        } else {
            packet->color.word = g_DrawPrimColor;
        }
        packet->tag.length = 9;
        packet->color.bytes.code = 0x2C;
    }

    {
        RenderTexturedQuad *ptr;

        base_x = (u16)g_TextCursorX;
        base_y = (u16)g_TextCursorY;
        ptr = packet;
        ptr->x2 = base_x;
        ptr->x0 = base_x;
        ptr->y1 = base_y;
        ptr->y0 = base_y;

        {
            s32 sum;
            s32 glyphDim;

            glyphDim = glyph->width;
            sum = ptr->x0;
            temp = sum + glyphDim;
        }
        ptr->x3 = temp;
        ptr->x1 = temp;

        {
            s32 sum;
            s32 glyphDim;

            glyphDim = glyph->height;
            sum = ptr->y0;
            temp = sum + glyphDim;
        }
        ptr->y3 = temp;
        ptr->y2 = temp;

        if (arg1 == 2) {
            temp = glyph->u + glyph->width - 1;
            ptr->u2 = temp;
            ptr->u0 = temp;
            temp = glyph->v + glyph->height - 1;
            ptr->v1 = temp;
            ptr->v0 = temp;
            temp = glyph->u - 1;
            ptr->u3 = temp;
            ptr->u1 = temp;
            temp = glyph->v - 1;
            ptr->v3 = temp;
            ptr->v2 = temp;
        }
    }

    {
        RenderNativeTag *ot;

        packet->clut = glyph->clut;
        packet->tpage = 7;
        ot = (RenderNativeTag *)g_DrawOrderingTableEntry;
        packet->tag.address = ot->address;
        ot->address = (uintptr_t)packet;
    }
}

extern int g_DrawGlyphAdvance;

void Draw_SetGlyphAdvance(int arg0) {
    g_DrawGlyphAdvance = arg0;
}

/* Allocates one bar quad from the packet arena and sets its colour and
 * command (0x2C: textured four-point polygon). */
static inline RenderTexturedQuad *AllocBarQuad(void)
{
    RenderTexturedQuad *prim = 0;
    u8 *old = g_DrawPacketCursor;
    size_t used = (size_t)(old - g_DrawPacketArenaBase);

    if (used < 0x4000 && sizeof(RenderTexturedQuad) < 0x4000 - used) {
        g_DrawPacketCursor = old + sizeof(RenderTexturedQuad);
        prim = (RenderTexturedQuad *)old;
    } else {
        Inv_BoundsCheckAssertStub(1);
    }
    if (prim != 0) {
        if (g_DrawTextDimmed != 0)
            prim->color.word = g_DrawColorShaded;
        else
            prim->color.word = g_DrawPrimColor;
        prim->tag.length = 9;
        prim->color.bytes.code = 0x2C;
    }
    return prim;
}

static inline int DecodeGlyph(int input) {
    int code = input;
    int glyph;
    glyph = (u8)code;
    if (g_TextRenderMode) {
        int page = g_TextRenderMode << 8;
        glyph += page;
        g_TextRenderMode = 0;
    }
    if ((u8)code >= 250) {
        g_TextRenderMode = (u8)code - 250;
        glyph = -1;
    }
    return glyph;
}

void Draw_AllocTexturedQuad(int code) {
    int low;
    int glyph;
    int raw = code;
    int current;
    int metrics;
    int width;
    int nibble;
    int u;
    int v;
    int spacing;
    int column;
    int row;
    int arg0;
    RenderTexturedQuad *prim;
    u8 *old, *next;

    current = raw & 0xFF;
    if (g_TextRenderMode != 0) {
        current += g_TextRenderMode << 8;
        g_TextRenderMode = 0;
    }

    low = raw & 0xFF;
    if ((unsigned int)low >= 0xFA) {
        g_TextRenderMode = low - 0xFA;
        current = -1;
    }

    glyph = current;
    if (glyph == 0x100) {
        int x, y;
        Draw_AllocSprite(0x77);
        AdvanceCursor(0xC);
        return;
    }

    if (glyph < 0) {
        return;
    }

    if (glyph >= 0x101) {
        glyph -= 0x13;
    }

    glyph %= 0x1B9;
    row = glyph / 0x15;
    column = glyph - row * 0x15;

    prim = 0;
    old = g_DrawPacketCursor;
    next = old + sizeof(RenderTexturedQuad);
    if (next < g_DrawPacketArenaBase + 0x4000) {
        g_DrawPacketCursor = next;
        prim = (RenderTexturedQuad *)old;
    } else {
        Inv_BoundsCheckAssertStub(1);
    }
    if (!prim) return;
    if (prim != 0) {
        if (g_DrawTextDimmed != 0) {
            prim->color.word = g_DrawColorShaded;
        } else {
            prim->color.word = g_DrawPrimColor;
        }
        prim->tag.length = 9;
        prim->color.bytes.code = 0x2C;
    }

    metrics = Draw_LookupGlyphMetrics(glyph);
    nibble = metrics & 0xF;
    width = (metrics >> 4) & 0xF;
    spacing = 0;
    if (glyph < 10 || glyph == 15) {
        spacing = 1;
    }
    u = column * 0xC + nibble;
    v = row * 0xC;
    {
        int uBase;
        int vBase;
        int half;
        int cursorX;
        int cursorY;
        arg0 = 0;
        cursorX = g_TextCursorX;
        cursorY = g_TextCursorY;
        spacing++;
        g_DrawGlyphAdvance = spacing;

        prim->u2 = u;
        prim->u0 = u;
        prim->clut = 0x89C;
        half = spacing >> 1;
        prim->v1 = v;
        prim->v0 = v;
        /* Use the bytes stored in the packet after truncation. */
        uBase = *(volatile u8 *)&prim->u0;
        vBase = *(volatile u8 *)&prim->v0;
        uBase += width;
        vBase += 0xC;
        cursorX += half;
        cursorY++;
        prim->u3 = uBase;
        prim->u1 = prim->u3;
        prim->x0 = cursorX;
        prim->x2 = prim->x0;
        prim->y1 = cursorY;
        prim->y0 = prim->y1;
        prim->v3 = vBase;
        prim->v2 = prim->v3;
        prim->x3 = prim->x0 + width;
        prim->x1 = prim->x3;
        prim->y3 = prim->y0 + 0xC;
        prim->y2 = prim->y3;
    }
    prim->tpage = GetTPage(arg0, 0, 0x140, 0);
    {
        RenderNativeTag *ot = (RenderNativeTag *)g_DrawOrderingTableEntry;

        prim->tag.address = ot->address;
        ot->address = (uintptr_t)prim;
    }
    AdvanceCursor(width + g_DrawGlyphAdvance);
}

inline int Draw_MeasureTextWidth(u8 *text) {
    u8 *cursor = text;
    int width = 0;

    while (*cursor != 255) {
        int glyph = DecodeGlyph(*cursor++);
        if (glyph >= 0) {
            int narrow = 0;
            if (glyph < 10 || glyph == 15) narrow = 1;
            g_DrawGlyphAdvance = narrow + 1;
            if (glyph >= 256) glyph -= 19;
            width += ((Draw_LookupGlyphMetrics(glyph) >> 4) & 15) + g_DrawGlyphAdvance;
        }
    }
    return width;
}

void Draw_PrintRawText(u8 *text) {
    if (text) {
        PushCursor();
        while (*text != 255) {
            Draw_AllocTexturedQuad(*text++);
        }
        PopCursor();
    }
}

/* Draws text from the current cursor position without saving it first. */
static inline void PrintTextHere(u8 *text) {
    if (text) {
        SaveCursor(g_TextCursorX, g_TextCursorY);
        while (*text != 255) {
            Draw_AllocTexturedQuad(*text++);
        }
        PopCursor();
    }
}

void Draw_PrintCenteredTextInWidth(u8 *text, int width) {
    int measured;

    PushCursor();
    measured = Draw_MeasureTextWidth(text) + 4;
    SetCursor(g_TextCursorX + ((width - measured) >> 1), g_TextCursorY);
    PrintTextHere(text);
    PopCursor();
}

void Draw_PrintCenteredText(u8 *text) {
    Draw_PrintCenteredTextInWidth(text, g_DrawTextBoxWidth);
}

void Draw_PrintTextById(unsigned int textId) {
    u8 *text = Text_LookupTable4(textId);

    if (text) {
        PushCursor();
        while (*text != 255) {
            Draw_AllocTexturedQuad(*text++);
        }
        PopCursor();
    }
}

void Draw_PrintTextWrapped(u8 *text, int width) {
    while (*text != 0xFF) {
        int used;
        DrawTextCursorPair *stack = (DrawTextCursorPair *)g_TextCursorStackPtr;
        if (stack < (DrawTextCursorPair *)g_TextCursorStackTop) {
            int x = g_DrawSpriteX, y = g_DrawSpriteY;
            g_TextCursorStackPtr = (int *)(stack + 1);
            stack->x = x;
            stack->y = y;
        } else Inv_BoundsCheckAssertStub(2);
        used = 0;
        while (used < width) {
            int code, glyph, narrow;
            if (*text == 0xFF) break;
            Draw_AllocTexturedQuad(*text);
            code = *text++;
            glyph = (u8)code;
            if (g_TextRenderMode) {
                glyph += g_TextRenderMode << 8;
                g_TextRenderMode = 0;
            }
            if ((u8)code >= 250) {
                g_TextRenderMode = (u8)code - 250;
                glyph = -1;
            }
            if (glyph >= 0) {
                narrow = 0;
                if (glyph < 10 || glyph == 15) narrow = 1;
                g_DrawGlyphAdvance = narrow + 1;
                if (glyph >= 256) glyph -= 19;
                used += ((Draw_LookupGlyphMetrics(glyph) >> 4) & 15) + g_DrawGlyphAdvance;
            }
        }
        {
            DrawTextCursorPair *restore = (DrawTextCursorPair *)g_TextCursorStackPtr;
            if ((DrawTextCursorPair *)g_TextCursorStackBottom < restore) {
                int x = restore[-1].x, y = restore[-1].y;
                g_TextCursorStackPtr = (int *)(restore - 1);
                g_DrawSpriteX = x;
                g_DrawSpriteY = y;
            } else Inv_BoundsCheckAssertStub(3);
        }
        {
            int y = g_DrawSpriteY;
            /* Preserve the retail read/write of X during a vertical move. */
            int x = g_DrawSpriteX;
            g_DrawSpriteX = x;
            g_DrawSpriteY = y + 14;
        }
    }
}

void Draw_SetFontVariant(int variant) {
    g_DrawDigitFontTpageClut = variant != 0 ? 0x3A1C : 0x395D;
    g_DrawDigitFontBaseTexU = variant != 0 ? 0xCC : 0x84;
    g_DrawDigitFontBaseTexV = 0xA4;
}

void Draw_EmitDigitSprite(int digit) {
    RenderTexturedQuad *quad = 0;
    u8 *old = g_DrawPacketCursor;
    u8 *next = old + sizeof(RenderTexturedQuad);

    if (next < g_DrawPacketArenaBase + 0x4000) {
        g_DrawPacketCursor = next;
        quad = (RenderTexturedQuad *)old;
    } else {
        Inv_BoundsCheckAssertStub(1);
    }
    if (!quad) return;
    if (quad) {
        if (g_DrawColorSelect) quad->color.word = g_DrawAlternateColor;
        else quad->color.word = g_DrawPrimaryColor;
        quad->tag.length = 9;
        quad->color.bytes.code = 0x2C;
    }
    {
        RenderTexturedQuad *p;
        u16 x = (u16)g_DrawSpriteX, y = (u16)g_DrawSpriteY;
        p = quad;
        p->x0 = p->x2 = x;
        p->y0 = p->y1 = y;
        p->x1 = p->x3 = p->x0 + 5;
        p->y2 = p->y3 = p->y0 + 7;
        if (digit >= 0) p->u2 = g_DrawDigitFontBaseTexU + (digit % 10) * 5;
        else p->u2 = 88;
        {
            u8 value = p->u2;
            p->u2 = value;
            p->u0 = value;
        }
        if (digit >= 0) p->v1 = g_DrawDigitFontBaseTexV;
        else p->v1 = 164;
        {
            u8 value = p->v1;
            p->v0 = value;
            p->v1 = value;
            p->u1 = p->u3 = p->u0 + 5;
            p->v2 = p->v3 = p->v0 + 7;
        }
        p->clut = g_DrawDigitFontTpageClut;
        p->tpage = 7;
        {
            RenderNativeTag *ot = (RenderNativeTag *)g_DrawOrderingTableEntry;
            p->tag.address = ot->address;
            ot->address = (uintptr_t)p;
        }
    }
}

void Draw_PrintNumberWidth2Unk(int value) {
    int width = 2;
    int place = 1;
    int i;

    if (value < 0) {
        value = (int)(0u - (u32)value);
        Draw_AllocSprite(0x52);
        width = 1;
        AdvanceCursor(5);
    }
    for (i = 1; i < width; i++) {
        place *= 10;
    }
    for (i = 0; i < width; i++) {
        int q = value / place;
        int digit = (i < width - 1 && q == 0) ? -1 : q;
        Draw_EmitDigitSprite(digit);
        AdvanceCursor(5);
        place /= 10;
    }
}

void Draw_PrintNumberWidth3Unk(int value) {
    int width = 3;
    int place = 1;
    int i;

    if (value < 0) {
        value = -value;
        Draw_AllocSprite(0x52);
        width = 2;
        AdvanceCursor(5);
    }
    for (i = 1; i < width; i++) {
        place *= 10;
    }
    for (i = 0; i < width; i++) {
        int q = value / place;
        int digit = (i < width - 1 && q == 0) ? -1 : q;
        Draw_EmitDigitSprite(digit);
        AdvanceCursor(5);
        place /= 10;
    }
}

void Draw_PrintSignedNumberWidth3(int value) {
    int width = 3;
    int place = 1;
    int i;
    int sprite;

    if (value < 0) {
        value = -value;
        sprite = 0x52;
        Draw_AllocSprite(sprite);
        width = 2;
        AdvanceCursor(5);
    } else if (value > 0) {
        sprite = 0x89;
        Draw_AllocSprite(sprite);
        width = 2;
        AdvanceCursor(5);
    }
    for (i = 1; i < width; i++) {
        place *= 10;
    }
    for (i = 0; i < width; i++) {
        int q = value / place;
        int digit = (i < width - 1 && q == 0) ? -1 : q;
        Draw_EmitDigitSprite(digit);
        AdvanceCursor(5);
        place /= 10;
    }
}

void Draw_PrintNumberWidth4Unk(int value) {
    int width = 4;
    int place = 1;
    int i;

    if (value < 0) {
        value = -value;
        Draw_AllocSprite(0x52);
        width = 3;
        AdvanceCursor(5);
    }
    for (i = 1; i < width; i++) {
        place *= 10;
    }
    for (i = 0; i < width; i++) {
        int q = value / place;
        int digit = (i < width - 1 && q == 0) ? -1 : q;
        Draw_EmitDigitSprite(digit);
        AdvanceCursor(5);
        place /= 10;
    }
}

void Draw_PrintSignedNumberWidth4(int value) {
    int width = 4;
    int place = 1;
    int i;
    int sprite;

    if (value < 0) {
        value = -value;
        sprite = 0x52;
        Draw_AllocSprite(sprite);
        width = 3;
        AdvanceCursor(5);
    } else if (value > 0) {
        sprite = 0x89;
        Draw_AllocSprite(sprite);
        width = 3;
        AdvanceCursor(5);
    }
    for (i = 1; i < width; i++) {
        place *= 10;
    }
    for (i = 0; i < width; i++) {
        int q = value / place;
        int digit = (i < width - 1 && q == 0) ? -1 : q;
        Draw_EmitDigitSprite(digit);
        AdvanceCursor(5);
        place /= 10;
    }
}

/* hh:mm:ss from a count of tenths of a second, clamped to 99:59:59.9 when it
 * exceeds 100 hours; `flag` selects the alternate digit font and separator. */
void Draw_PrintTimeValue(int value, int flag) {
    if (value > 360000) {
        value = 359999;
    }
    g_DrawDigitFontTpageClut = flag != 0 ? 0x3A1C : 0x395D;
    g_DrawDigitFontBaseTexU = flag != 0 ? 0xCC : 0x84;
    g_DrawDigitFontBaseTexV = 0xA4;

    Draw_EmitDigitSprite(value / 36000);
    AdvanceCursor(5);
    Draw_EmitDigitSprite(value / 3600);
    AdvanceCursor(5);
    if (flag) {
        Draw_AllocSprite(0x9B);
        AdvanceCursor(3);
    } else {
        Draw_AllocSprite(0x4F);
        AdvanceCursor(5);
    }
    Draw_EmitDigitSprite((value / 600) % 6);
    AdvanceCursor(5);
    Draw_EmitDigitSprite(value / 60);
    AdvanceCursor(5);
    if (flag) {
        Draw_AllocSprite(0x9B);
        AdvanceCursor(3);
    } else {
        Draw_AllocSprite(0x4F);
        AdvanceCursor(5);
    }
    Draw_EmitDigitSprite((value / 10) % 6);
    AdvanceCursor(5);
    Draw_EmitDigitSprite(value);

    g_DrawDigitFontTpageClut = 0x395D;
    g_DrawDigitFontBaseTexU = 0x84;
    g_DrawDigitFontBaseTexV = 0xA4;
}

static inline void PrintSign(u8 *text)
{
    if (text) {
        PushCursor();
        while (*text != 255) {
            Draw_AllocTexturedQuad(*text++);
        }
        PopCursor();
    }
}

static inline int Digit(int leading, int value)
{
    if (!leading || value != 0) {
        return (u8)(value % 10);
    }
    return 15;
}

/* Print a signed decimal field; glyph 15 is padding, table entry 0x70 the
 * minus sign. The sign occupies one place and restores its own text cursor.
 * Retail callers use bounded widths; powers of ten and negation must fit int.
 */
void Draw_AllocTexturedRect(int value, int width)
{
    int divisor = 1;
    int i;

    for (i = 1; i < width; i++) {
        divisor *= 10;
    }
    if (value < 0) {
        value = -value;
        divisor /= 10;
        width--;
        while (value < divisor) {
            Draw_AllocTexturedQuad(15);
            divisor /= 10;
            width--;
        }
        PrintSign(Text_LookupTable4(0x70));
        SetCursor(g_TextCursorX + 9u, g_TextCursorY);
    }
    for (i = 0; i < width; i++) {
        Draw_AllocTexturedQuad(Digit(i < width - 1, value / divisor));
        divisor /= 10;
    }
}

void Draw_PrintNumberWidth6(int value) {
    Draw_AllocTexturedRect(value, 6);
    AdvanceCursor(6);
}

void Draw_PrintNumberWidth5(int value) {
    Draw_AllocTexturedRect(value, 5);
    AdvanceCursor(5);
}

void Draw_PrintNumberWidth4(int value) {
    Draw_AllocTexturedRect(value, 4);
    AdvanceCursor(4);
}

void Draw_PrintNumberWidth3(int value) {
    Draw_AllocTexturedRect(value, 3);
    AdvanceCursor(3);
}

void Draw_PrintNumberWidth2(int value) {
    Draw_AllocTexturedRect(value, 2);
    AdvanceCursor(2);
}

/* Moves the cursor and saves the new position. */
static inline void PushCursorAt(int x, int y)
{
    DrawTextCursorPair *stack = (DrawTextCursorPair *)g_TextCursorStack;

    SetCursor(x, y);
    if (stack < (DrawTextCursorPair *)g_TextCursorStackTop) {
        stack->x = x;
        stack->y = y;
        g_TextCursorStack = (int *)(stack + 1);
    } else {
        Inv_BoundsCheckAssertStub(2);
    }
}

/* One textured quad of the bar: glyph texels starting `u` columns into the
 * glyph, from `left` to the right edge computed from the stored left edge. */
#define DRAW_BAR_SEGMENT(left, right_from_x0, u_offset) \
    { \
        quad = AllocBarQuad(); \
        if (quad) { \
        quad->x0 = quad->x2 = left; \
        quad->y0 = quad->y1 = g_TextCursorY + 1; \
        quad->x1 = quad->x3 = right_from_x0; \
        quad->y2 = quad->y3 = quad->y0 + glyph->height; \
        quad->u0 = quad->u2 = glyph->u + (u_offset); \
        quad->v0 = quad->v1 = glyph->v; \
        quad->u1 = quad->u3 = quad->u0; \
        quad->v2 = quad->v3 = quad->v0 + glyph->height; \
        quad->clut = glyph->clut; \
        quad->tpage = 7; \
        quad->tag.address = ((RenderNativeTag *)g_DrawOrderingTableEntry)->address; \
        ((RenderNativeTag *)g_DrawOrderingTableEntry)->address = (uintptr_t)quad; \
        } \
    }

/* Draws a stat value followed by a 48-pixel level bar filled up to
 * 48 - width pixels, built from slices of glyph 0x48. */
void Draw_AllocTexturedRectAlt(int value, int width)
{
    DrawGlyphDescriptor *glyph = Draw_LookupGlyphDescriptor(0x48);
    RenderDrawModePacket *mode;
    RenderTexturedQuad *quad;
    int drawMode;
    u8 *old;
    u32 *ot;

    PushCursor();
    PushCursorAt(g_TextCursorX, g_TextCursorY + 2);
    SetCursor(g_TextCursorX + 0x27, g_TextCursorY - 2);
    Draw_PrintNumberWidth2Unk(value);
    g_DrawPrimColor = 0x808080;
    g_DrawColorShaded = 0x404040;
    PopCursor();
    Draw_AllocSprite(0x49);

    if (width < 0x2F)
        DRAW_BAR_SEGMENT(g_TextCursorX + 1, quad->x0 + 1, 0);
    if (width < 0x2E)
        DRAW_BAR_SEGMENT(g_TextCursorX + 2, (s16)quad->x0 + 0x2E - width, 1);
    if (width < 0x30)
        DRAW_BAR_SEGMENT(g_TextCursorX + 0x30 - width, quad->x0 + 1, 2);
    if (width > 0)
        DRAW_BAR_SEGMENT(g_TextCursorX + 0x31 - width, quad->x0 + 1, 3);
    if (width >= 3)
        DRAW_BAR_SEGMENT(g_TextCursorX + 0x32 - width, (s16)quad->x0 - 1 + width, 4);
    if (width >= 2)
        DRAW_BAR_SEGMENT(g_TextCursorX + 0x31, quad->x0 + 1, 5);
    PopCursor();

    drawMode = glyph->mode;
    mode = 0;
    old = g_DrawPacketCursor;
    size_t used = (size_t)(old - g_DrawPacketArenaBase);
    if (used < 0x4000 && sizeof(RenderDrawModePacket) < 0x4000 - used) {
        g_DrawPacketCursor = old + sizeof(RenderDrawModePacket);
        mode = (RenderDrawModePacket *)old;
    } else {
        Inv_BoundsCheckAssertStub(1);
    }
    if (mode != 0)
        SetDrawTPage(mode, 0, 0, ((drawMode & 3) << 7) | 7);
    ot = g_DrawOrderingTableEntry;
    if (mode) {
        RenderNativeTag *ordering = (RenderNativeTag *)ot;
        mode->tag.address = ordering->address;
        ordering->address = (uintptr_t)mode;
    }
}
