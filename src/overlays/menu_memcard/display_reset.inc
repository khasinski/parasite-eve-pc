#ifdef PE_PS5_GPU_NATIVE
#include <psyz/video.h>
#endif
#include "pe1/display_env.h"
#include "pe1/psyq_gpu.h"
#include "pe1/gpu_image.h"
#include "pe1/game_state.h"
#ifndef MENUCARD_RESET_DISPLAY_STATE_NAME
#define MENUCARD_RESET_DISPLAY_STATE_NAME MenuCard_ResetDisplayState
#endif
#ifndef MENUCARD_DISPLAY_PTR0
#define MENUCARD_DISPLAY_PTR0 g_MenuCardScreenVideoDecodeBuffer0
#define MENUCARD_DISPLAY_PTR1 g_MenuCardScreenVideoDecodeBuffer1
#define MENUCARD_DISPLAY_PTR2 g_MenuCardScreenVideoSliceBuffer0
#define MENUCARD_DISPLAY_PTR3 g_MenuCardScreenVideoSliceBuffer1
#define MENUCARD_DISPLAY_PTR4 g_MenuCardScreenVideoVlcTable
#define MENUCARD_DISPLAY_PTR5 g_MenuCardScreenVideoStreamRing
#define MENUCARD_SLOT20_SOURCE g_MenuCardScreenSavedDispEnv
#define MENUCARD_SLOT92_SOURCE g_MenuCardScreenSavedDrawEnv
#endif

typedef RECT MemcardRect;


#include "display_reset_externs.h"

void SetDispMask(s32 enabled);

void MENUCARD_RESET_DISPLAY_STATE_NAME(void) {
    s8 i;
    u8 *active;
    MemcardRect rect;

    MenuCard_StopMovieAudio();
#ifdef PE_PS5_GPU_NATIVE
    Psyz_VideoSetMovieActive(0);
#endif
    active = &g_DrawEnabled.bits;
    if (*active != 0) {
        MENUCARD_DISPLAY_PTR0 = 0;
        MENUCARD_DISPLAY_PTR1 = 0;
        MENUCARD_DISPLAY_PTR5 = 0;
        MENUCARD_DISPLAY_PTR4 = 0;
        MENUCARD_DISPLAY_PTR2 = 0;
        MENUCARD_DISPLAY_PTR3 = 0;
        *active = 0;

        DrawSync(0);
        VSync(0);
        SetDispMask(0);

        i = 0;
        do {
            g_RenderDispEnvArray[i] = MENUCARD_SLOT20_SOURCE[i];
            g_RenderDrawEnvArray[i] = MENUCARD_SLOT92_SOURCE[i];
            i++;
        } while (i < 2);

        rect.x = 0x200;
        rect.y = 0;
        rect.w = 0xC0;
        rect.h = 0x100;
        MoveImage(&rect, 0x140, 0);

        if ((g_GameStateFlagsWord & 0x8000000) == 0) {
            rect.x = 0x200;
            rect.y = 0x100;
            rect.w = 0x140;
            rect.h = 0x40;
            MoveImage(&rect, 0, 0x1C0);
        }
#ifdef PE_PS5_GPU_NATIVE
        /* Retain movie presentation through the old-display VSync above. */
        Psyz_VideoMovieRestoreReady();
#endif
    }
}

#undef MENUCARD_SLOT92_SOURCE
#undef MENUCARD_SLOT20_SOURCE
#undef MENUCARD_DISPLAY_PTR5
#undef MENUCARD_DISPLAY_PTR4
#undef MENUCARD_DISPLAY_PTR3
#undef MENUCARD_DISPLAY_PTR2
#undef MENUCARD_DISPLAY_PTR1
#undef MENUCARD_DISPLAY_PTR0
#undef MENUCARD_RESET_DISPLAY_STATE_NAME
