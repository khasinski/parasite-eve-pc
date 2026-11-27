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
    void *frame = MENUCARD_VIDEO_UPDATE_FRAME(dec);
    if (!frame) return -1;
    dec->decodeIndex ^= 1;
    if (DecDCTvlc2(frame, dec->decode[dec->decodeIndex], MENUCARD_VIDEO_PTR4) != 0) return -1;
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
        unsigned currentFrame = movieFrameNumber;
        int pageY = MENUCARD_VIDEO_DISPLAY.region == 0 ? 240 : 0;
        DecDCTin(MENUCARD_VIDEO_DISPLAY.decode[MENUCARD_VIDEO_DISPLAY.decodeIndex], MENUCARD_VIDEO_MODE);
        DecDCTout(MENUCARD_VIDEO_DISPLAY.buffers[MENUCARD_VIDEO_DISPLAY.selector],
                      MENUCARD_VIDEO_DISPLAY.rect.w * MENUCARD_VIDEO_DISPLAY.rect.h / 2);
        if (MenuCard_NextVideoFrame(&MENUCARD_VIDEO_DISPLAY) == -1) finished = 1;
        MenuCard_SyncVideoUpload(&MENUCARD_VIDEO_DISPLAY);
        MenuCard_PresentMovieFrame(pageY, currentFrame);
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
        MenuCard_ClearMovieFrames();
        MenuCard_StopMovieAudio();
        DecDCToutCallback(0);
    }
    return 0;
}
