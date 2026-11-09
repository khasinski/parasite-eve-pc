#include "pe1/movie_profile.h"
#ifndef MENUCARD_STEP_VIDEO_NAME
#define MENUCARD_STEP_VIDEO_NAME MenuCard_StepVideo
#define MENUCARD_VIDEO_SET_DISPLAY MenuCard_SetVideoDisplay
#define MENUCARD_VIDEO_UPDATE_FRAME MenuCard_UpdateVideoFrame
#define MENUCARD_VIDEO_MODE g_MenuCardScreenVideoDecodeMode
#define MENUCARD_VIDEO_FLIP g_MenuCardScreenVideoDepthFlip
#define MENUCARD_VIDEO_FILE g_MenuCardScreenVideoFile
#define MENUCARD_VIDEO_SECTOR g_MenuCardScreenVideoSector
#define MENUCARD_VIDEO_DISPLAY g_MenuCardScreenVideoDisplay
#define MENUCARD_VIDEO_PTR4 g_MenuCardScreenVideoVlcTable
#define MENUCARD_VIDEO_FINISHED g_MenuCardScreenVideoFinished
#endif

/* Wait for the next decoded frame and start its decode; -1 on a stall. */
static inline s16 MenuCard_NextVideoFrame(VideoDisplay *dec) {
    PE_MOVIE_BEGIN(read);
    void *frame = MENUCARD_VIDEO_UPDATE_FRAME(dec);
    PE_MOVIE_END(read, PE_MOVIE_READ);
    if (!frame) return -1;
    dec->decodeIndex ^= 1;
    PE_MOVIE_BEGIN(vlc);
    int vlc_result = DecDCTvlc2(frame, dec->decode[dec->decodeIndex], MENUCARD_VIDEO_PTR4);
    PE_MOVIE_END(vlc, PE_MOVIE_VLC);
    if (vlc_result != 0) return -1;
    g_SeqElapsed++;
    return 0;
}

/* Wait for the slice upload; on a timeout skip to the other frame buffer. */
static inline void MenuCard_SyncVideoUpload(VideoDisplay *dec) {
    volatile s32 wait = 0x800000;

    while (dec->done == 0) {
        if (--wait == 0) {
            dec->done = 1;
            dec->region ^= 1;
            dec->rect.x = dec->regions[dec->region].x;
            dec->rect.y = dec->regions[dec->region].y;
        }
    }
    dec->done = 0;
}

/* Start decode/upload, read the next image frame, then finish the upload.
 * A missing or invalid next frame terminates playback without a drive retry. */
s32 MENUCARD_STEP_VIDEO_NAME(void) {
    s8 finished;
    u8 *flip;

    if (g_DrawEnabled.bits >= 2) {
        flip = &MENUCARD_VIDEO_FLIP;
        finished = 0;
        if (*flip == 2) {
            MENUCARD_VIDEO_SET_DISPLAY(g_ActiveDrawSlot ^ 1, g_MovieWide);
            *flip = 0;
        }
        PE_MOVIE_BEGIN(mdec);
        DecDCTin(MENUCARD_VIDEO_DISPLAY.decode[MENUCARD_VIDEO_DISPLAY.decodeIndex], MENUCARD_VIDEO_MODE);
        DecDCTout(MENUCARD_VIDEO_DISPLAY.buffers[MENUCARD_VIDEO_DISPLAY.selector],
                      MENUCARD_VIDEO_DISPLAY.rect.w * MENUCARD_VIDEO_DISPLAY.rect.h / 2);
        PE_MOVIE_END(mdec, PE_MOVIE_MDEC);
        if (MenuCard_NextVideoFrame(&MENUCARD_VIDEO_DISPLAY) == -1) finished = 1;
        PE_MOVIE_BEGIN(sync);
        MenuCard_SyncVideoUpload(&MENUCARD_VIDEO_DISPLAY);
        PE_MOVIE_END(sync, PE_MOVIE_SYNC);
        if (MENUCARD_VIDEO_FINISHED == 1) {
            finished = 1;
        }
        if (!finished) {
            return 1;
        }
        {
            u8 *active = &g_DrawEnabled.bits;

            (*active)--;
        }
        MenuCard_StopMovieAudio();
        DecDCToutCallback(0);
    }
    return 0;
}
