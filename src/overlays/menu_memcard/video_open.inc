#ifdef PE_PS5_GPU_NATIVE
#include <psyz/video.h>
#include <SDL3/SDL_log.h>
#endif
#ifndef MENUCARD_OPEN_VIDEO_NAME
#define MENUCARD_OPEN_VIDEO_NAME MenuCard_OpenVideo
#define MENUCARD_VIDEO_TABLE g_MenuCardScreenVideoTable
#define MENUCARD_VIDEO_ENTRY g_MenuCardScreenVideoEntry
#define MENUCARD_VIDEO_SET_DISPLAY MenuCard_SetVideoDisplay
#define MENUCARD_VIDEO_DIRECTORY g_MenuCardScreenVideoDirectory
#define MENUCARD_VIDEO_DIRECTORY_LATE g_MenuCardScreenVideoDirectoryLate
#define MENUCARD_VIDEO_FILE g_MenuCardScreenVideoFile
#define MENUCARD_VIDEO_SECTOR g_MenuCardScreenVideoSector
#define MENUCARD_VIDEO_DISPLAY g_MenuCardScreenVideoDisplay
#define MENUCARD_VIDEO_PTR0 g_MenuCardScreenVideoDecodeBuffer0
#define MENUCARD_VIDEO_PTR1 g_MenuCardScreenVideoDecodeBuffer1
#define MENUCARD_VIDEO_PTR2 g_MenuCardScreenVideoSliceBuffer0
#define MENUCARD_VIDEO_PTR3 g_MenuCardScreenVideoSliceBuffer1
#define MENUCARD_VIDEO_PTR4 g_MenuCardScreenVideoVlcTable
#define MENUCARD_VIDEO_PTR5 g_MenuCardScreenVideoStreamRing
#define MENUCARD_VIDEO_UPLOAD MenuCard_UploadVideoSlice
#define MENUCARD_VIDEO_UPDATE_FRAME MenuCard_UpdateVideoFrame
#define MENUCARD_VIDEO_FINISHED g_MenuCardScreenVideoFinished
#endif

/* Wait for the next decoded frame and start its decode; -1 on a stall. */
static inline s16 MenuCard_OpenNextVideoFrame(VideoDisplay *dec) {
    void *frame = MENUCARD_VIDEO_UPDATE_FRAME(dec);
    if (!frame) return -1;
    dec->decodeIndex ^= 1;
    if (DecDCTvlc2(frame, dec->decode[dec->decodeIndex], MENUCARD_VIDEO_PTR4) != 0) return -1;
    g_SeqElapsed++;
    return 0;
}

/* Reset the decode environment: buffers, both display areas, first slice. */
static inline void MenuCard_OpenSetDecodeEnv(VideoDisplay *dec, s16 x0, s16 y0, s16 x1, s16 y1) {
    dec->decode[0] = MENUCARD_VIDEO_PTR0;
    dec->decode[1] = MENUCARD_VIDEO_PTR1;
    dec->decodeIndex = 0;
    dec->buffers[0] = MENUCARD_VIDEO_PTR2;
    dec->buffers[1] = MENUCARD_VIDEO_PTR3;
    dec->selector = 0;
    dec->regions[0].x = x0;
    dec->regions[0].y = y0;
    dec->regions[1].x = x1;
    dec->regions[1].y = y1;
    dec->region = g_ActiveDrawSlot;
    dec->rect.x = dec->regions[dec->region].x;
    dec->rect.y = dec->regions[dec->region].y;
    if (g_MovieWide) {
        dec->rect.w = 24;
    } else {
        dec->rect.w = 16;
    }
    dec->done = 0;
}

/* Look up the video file, set up the decoder state, start the stream and
 * decode the first frame. */
s32 MENUCARD_OPEN_VIDEO_NAME(s32 number) {
    s16 index = number;
    VideoRect size;
    char name[32];
    VideoEntry *entry;

    /* A failed replacement must not leave the preceding movie audible. */
    MenuCard_StopMovieAudio();
#ifdef PE_PS5_GPU_NATIVE
    Psyz_VideoSetMovieActive(0);
#endif
    DecDCToutCallback(NULL);
    if (number < 0 || number >= 47 || !g_CdImage || !MENUCARD_VIDEO_PTR0 || !MENUCARD_VIDEO_PTR5) {
        return 0;
    }
    g_MovieIndex = index;
    MENUCARD_VIDEO_ENTRY = &MENUCARD_VIDEO_TABLE[index];
    g_MovieWide = MENUCARD_VIDEO_ENTRY->wide;
    MENUCARD_VIDEO_SET_DISPLAY(0, g_MovieWide);
    MENUCARD_VIDEO_SET_DISPLAY(1, g_MovieWide);
    name[0] = 0;
    if ((u16)number < 21) {
        strcat(name, MENUCARD_VIDEO_DIRECTORY);
    } else {
        strcat(name, MENUCARD_VIDEO_DIRECTORY_LATE);
    }
    strcat(name, MENUCARD_VIDEO_ENTRY->name);
    if (!pe_disc_find(g_CdImage, name, &MENUCARD_VIDEO_FILE)) return 0;
    MENUCARD_VIDEO_SECTOR = 0;
    entry = MENUCARD_VIDEO_ENTRY;
    size.x = entry->x;
    size.y = entry->y;
    MenuCard_OpenSetDecodeEnv(&MENUCARD_VIDEO_DISPLAY, size.x, size.y + 240, size.x, size.y);
    DecDCTReset(0);
    DecDCToutCallback(MENUCARD_VIDEO_UPLOAD);
    Spu_SetVoiceVolume(g_GameState.scene_audio.tracks.movie_volume);
    /* Reloading the PS1 code used to reset the VLC work limit. */
    DecDCTvlcSize2(0);
    MdecDiag_ExpandDecoderTable(MENUCARD_VIDEO_PTR4);
    if (MenuCard_OpenNextVideoFrame(&MENUCARD_VIDEO_DISPLAY) == -1) {
        DecDCToutCallback(0);
        return 0;
    }
    if (!MenuCard_StartMovieAudio(g_CdImage, &MENUCARD_VIDEO_FILE)) {
        DecDCToutCallback(0);
        return 0;
    }
    MENUCARD_VIDEO_FINISHED = 0;
    g_SeqElapsed = 1;
    {
        u8 *active = &g_DrawEnabled.bits;

        (*active)++;
    }
#ifdef PE_PS5_GPU_NATIVE
    Psyz_VideoSetMovieActive(1);
    {static unsigned native_movie_starts;if(native_movie_starts++<32)
        SDL_Log("[PS5 movie] player=%s index=%d decode_rgb24=%d display=%dx%d frame_region=%d,%d/%dx%d presentation=independent-of-scene",
            __func__,number,g_MovieWide!=0,g_RenderDispEnvArray[0].disp.w,g_RenderDispEnvArray[0].disp.h,
            MENUCARD_VIDEO_DISPLAY.regions[0].x,MENUCARD_VIDEO_DISPLAY.regions[0].y,MENUCARD_VIDEO_DISPLAY.regions[0].w,MENUCARD_VIDEO_DISPLAY.regions[0].h);
    }
#endif
    return 1;
}
