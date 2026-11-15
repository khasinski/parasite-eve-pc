#include <psyz/video.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "pe1/boot_disc_check.h"
#include "pe1/boot_display.h"
#include "pe1/psyq_gpu.h"
#include "pe1/signed_rect.h"

#include "menu_memcard_image.h"
#include "menu_memcard_screen.h"
#include "page_flip_externs.h"

void ResetGraph(int);

/* Retain the immutable title reference; the disc workspace is reused later. */
static unsigned char title_reference[320*204*3];
static int title_reference_valid;
void MenuCard_RefreshHdBackground(void) {
    if (!title_reference_valid) return;
    const char *pack=SDL_getenv("PE_HD_BACKGROUNDS");
    if (pack && *pack) {
        char filename[4096];const char *formats[]={"webp","png"};
        for(unsigned format=0;format<2;++format) {
            int length=snprintf(filename,sizeof(filename),"%s/title.%s",pack,formats[format]);
            if(length>=0 && (size_t)length<sizeof(filename) &&
               Psyz_VideoSetRgb24Backdrop(filename,title_reference,320,204,20,16)==1)break;
        }
    }
}

void MenuCard_FlipScreenPages(void) {
    int i;
    int page;
    int y;
    MemcardScreenBuffer *screen;
    RECT main_rect;

    memcpy(title_reference,g_MemcardImageBundle+g_MemcardImageOffsets[0]+sizeof(MemcardImage),sizeof(title_reference));
    title_reference_valid=1;
    MenuCard_RefreshHdBackground();
    for (i = 0; i < 2; i++) {
        page = g_MemcardScreenPage == 0;
        g_MemcardCurrentScreen = g_MemcardScreenBuffers[page];
        g_MemcardScreenPage = page;
        main_rect.x = 0;
        y = PE_JAPAN ? 0x13 : 0x14;            /* the Japanese picture area sits a row higher */
        if (page == 0) y = PE_JAPAN ? 0x103 : 0x104;
        main_rect.w = 0x1E0;
        main_rect.h = 0xCC;
        main_rect.y = y;
        LoadImage(&main_rect, g_MemcardImageBundle + g_MemcardImageOffsets[0] + sizeof(MemcardImage));
        DrawSync(0);

        screen = g_MemcardCurrentScreen;
        if (screen->overlay.w > 0) {
            RECT clipped;
            clipped = screen->overlay;
            clipped.x = (clipped.x * 3) >> 1;
            if (g_MemcardScreenPage == 0) clipped.y = (u16)clipped.y + 0xF0;
            clipped.w = (clipped.w * 3) >> 1;
            LoadImage(&clipped, g_MemcardCurrentScreen->overlayImage);
        }

        VSync(0);
        ResetGraph(1);
        PutDrawEnv(&g_MemcardCurrentScreen->draw);
        PutDispEnv(&g_MemcardCurrentScreen->disp);
    }
}
