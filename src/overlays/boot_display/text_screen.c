#include "pe1/boot_display.h"

#include "pe1/text_render.h"
#include "text_screen_externs.h"

s32 BootDisplay_RunDisplay(void) {
    u32 state;
    s32 fade;
    s32 result=0;
    if (g_GameStateFlags & 0x10000) {
        g_BootDisplayCallbacks = &g_BootDisplayImageCallbacks;
    } else {
        g_BootDisplayCallbacks = &g_BootDisplaySpriteCallbacks;
        g_BootDisplaySpriteCallbacks.text = (g_GameStateFlags & 0x20000)?BootDisplay_StepImageSprite:NULL;
    }
    g_BootDisplayCallbacks->init();
    if(g_BootDisplayState<0 || g_BootDisplayDone<0)return -1;
    g_BootDisplayPlaybackEnded=0;
    if (!(g_GameStateFlags & 0x10000)) {
        if(BootDisplay_InitPlaybackFile()<0 || BootDisplay_StartPlayback()<0){result=-2;goto cleanup;}
        if (g_GameStateFlags & 0x20000) {
            Spu_SetVoiceVolume(144);
            BootDisplay_SetCdFilter(1);
        } else Spu_SetVoiceVolume(218);
    }
    state = 0;
    fade = 0;
    g_BootDisplayFrameCounter = 420;
    g_BootDisplayDone = 0;
    do {
        if (g_BootDisplayCallbacks->step) g_BootDisplayCallbacks->step();
        if (g_BootDisplayCallbacks->text) g_BootDisplayCallbacks->text();
        if(g_BootDisplayState<0 || g_BootDisplayDone<0){result=-3;goto cleanup;}
        switch (state) {
        case 0:
            if (BootDisplay_DrawTextBlock()) {
                state = (g_GameStateFlags & 0x20000) ? 1 : 4;
                fade = 0;
            }
            break;
        case 1:
            g_BootDisplayCallbacks->transition((u8)fade);
            fade += 4;
            if (fade >= 256) { state = 2; fade = 0; }
            break;
        case 2:
            g_BootDisplayCallbacks->transition(255);
            if (g_BootDisplayPlaybackEnded && (g_FieldPadBits & 0xF0000004)) { state = 3; fade = 255; }
            break;
        case 3:
            g_BootDisplayCallbacks->transition((u8)fade);
            fade -= 4;
            if (fade < 0) state = 4;
            break;
        case 4: {
            u32 flags = g_GameStateFlags;
            s32 finished;
            if (flags & 0x30000) finished = 1;
            else finished = g_BootDisplayPlaybackEnded;
            g_BootDisplayDone = finished;
            break;
        }
        }
        if(g_BootDisplayState<0 || g_BootDisplayDone<0){result=-3;goto cleanup;}
        Field_HandleStateTransition();
        if (g_GameStateFlags & 0x10000) {
            if (g_FieldPadPressed & 0x4000000) BootDisplay_SetCdFilter(0);
            if (g_FieldPadPressed & 0x8000000) BootDisplay_SetCdFilter(1);
        }
        g_BootDisplayCallbacks->present();
        if(g_BootDisplayState<0 || g_BootDisplayDone<0){result=-3;goto cleanup;}
        ++g_BootDisplayFrameCounter;
    } while (g_BootDisplayDone == 0);
cleanup:
    if(BootDisplay_WaitForCdAndRestoreCallbacks()<0 && result==0)result=-4;
    g_BootDisplayCallbacks->shutdown();
    g_GameStateFlags &= ~0x10000;
    return result;
}

s32 BootDisplay_DrawTextBlock(void) {
    const u8 *line;
    u32 rows = 0;
    s32 y = g_BootDisplayTextScrollX >> 8;
    g_BootDisplayTextCursor = g_BootDisplayCreditLines;
    line = *g_BootDisplayTextCursor;
    while (*line != 255) {
        if (y >= -7) {
            PeTextCreditPen pen;
            PeTextLetter letter;
            if (y >= 225) break;
            /* the text renderer places each letter; the drawer only puts it on screen */
            pen.x = PE_TEXT_CREDIT_LEFT;
            for (; *line; ++line) {
                pen.style = g_BootDisplayFontStyle;
                if (PeText_ReadCredit(&pen, line, &letter))
                    g_BootDisplayCallbacks->glyph(letter.x, (s16)y, (u8)letter.code);
            }
            g_BootDisplayFontStyle = 0;
            ++rows;
        }
        ++g_BootDisplayTextCursor;
        line = *g_BootDisplayTextCursor;
        y += 18;
    }
    g_BootDisplayTextScrollX = (s32)((u32)g_BootDisplayTextScrollX + (u32)g_BootDisplayTextScrollStepX);
    return rows < 1;
}

/* The drawers (BootDisplay_QueueTintedSprite, BootDisplay_BrightenGlyph) ask the text renderer
 * which cell of the credits art a letter is; the roll's widths are the renderer's too. */
s32 BootDisplay_GetGlyphIndex(u8 character) {
    return (s32)PeText_CreditCell(g_BootDisplayFontStyle, character);
}

u16 BootDisplay_GetGlyphWidth(u8 character) {
    return (u16)PeText_CreditWidth(g_BootDisplayFontStyle, character);
}

/* Native pointers into the loaded credits data; no retail addresses are
 * callable or dereferenced by the host. The loader validates every entry. */
const u8 *g_BootDisplayCreditLines[745];
void *g_BootDisplayCreditImages[31];
const u8 **g_BootDisplayTextCursor;
BootDisplayCallbacks *g_BootDisplayCallbacks;
u16 g_BootDisplayFontStyle;
s32 g_BootDisplayTextScrollX,g_BootDisplayTextScrollStepX,g_BootDisplayDone;
u32 g_BootDisplayFrameCounter;
