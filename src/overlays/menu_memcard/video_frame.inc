#include <stdio.h>
#include <stdlib.h>
#include <psyz/video.h>
#include <psyz/audio.h>
#include <psyz/cd.h>

/* The MDEC input frame advances before the finished upload is presented. */
static unsigned movieFrameNumber;
static char movieReplacementDir[1024];

static void MenuCard_ClearMovieFrames(void) {
#ifdef PSYZ_HAS_MOVIE_FRAME_REPLACEMENT
    if (movieReplacementDir[0]) Psyz_VideoSetMovieFrame(-1, NULL);
#endif
    movieReplacementDir[0] = 0;
}

static void MenuCard_SetMovieDirectory(const char *name) {
    const char *root = getenv("PE_FMV_DIR");
    if (!root || !*root) return;
    if (*name == '\\') ++name;
    size_t length = strcspn(name, ".");
    int n = snprintf(movieReplacementDir, sizeof(movieReplacementDir), "%s/%.*s", root, (int)length, name);
    if (n < 0 || (size_t)n >= sizeof(movieReplacementDir)) movieReplacementDir[0] = 0;
}

static void MenuCard_PresentMovieFrame(int pageY, unsigned number) {
#ifdef PSYZ_HAS_MOVIE_FRAME_REPLACEMENT
    if (movieReplacementDir[0]) {
        char path[sizeof(movieReplacementDir) + 24];
        snprintf(path, sizeof(path), "%s/%06u.webp", movieReplacementDir, number);
        Psyz_VideoSetMovieFrame(pageY, path);
    }
#endif
}

/* Both original movie players read image sectors directly. Feed their XA
 * track to the existing SDK decoder independently of video-frame assembly. */
static PeDisc *movieAudioImage;
static PeDiscFile movieAudioFile;
static int MenuCard_ReadMovieAudio(struct PsyzCdRead *read) {
    if(!movieAudioImage || movieAudioImage!=g_CdImage || !movieAudioImage->stream || !read || !read->buffer || read->size!=2352 ||
       read->sector<movieAudioFile.sector ||
       read->sector-movieAudioFile.sector>=((u64)movieAudioFile.size+2047)/2048)
        return -1;
    return pe_disc_read_raw(movieAudioImage,&movieAudioFile,
               read->sector-movieAudioFile.sector,read->buffer)?2352:-1;
}
/* CD commands raise kernel events. Never hold audio while issuing one:
 * the IRQ thread holds the kernel while the AKAO timer writes SPU registers. */
static void MenuCard_StopMovieAudio(void) {
    Psyz_AudioLock();
    int active=movieAudioImage!=NULL;
    if(active){
        Psyz_CdSetReadCB(NULL);
        movieAudioImage=NULL;
        memset(&movieAudioFile,0,sizeof(movieAudioFile));
    }
    Psyz_AudioUnlock();
    if(active)CdControlB(8,NULL,NULL);
}
static int MenuCard_StartMovieAudio(PeDisc *image,const PeDiscFile *file) {
    u8 raw[2352],filter[2],mode=200;
    CdlLOC position;
    MenuCard_StopMovieAudio();
    if(!image || !image->stream || !file || file->directory || image->sector_size!=2352)return 0;
    u32 count=(u32)(((u64)file->size+2047)/2048),sector;
    for(sector=0;sector<count;sector++){
        if(!pe_disc_read_raw(image,file,sector,raw))return 0;
        if((raw[18]&0x44)==0x44)break;
    }
    if(sector==count)return 1;
    filter[0]=raw[16];filter[1]=raw[17];
    if(Psyz_AudioInit()<0)return 0;
    Psyz_AudioLock();
    movieAudioImage=image;movieAudioFile=*file;
    Psyz_CdSetReadCB(MenuCard_ReadMovieAudio);
    Psyz_AudioUnlock();
    CdIntToPos(file->sector,&position);
    int ok=CdControlB(14,&mode,NULL)>0 && CdControlB(13,filter,NULL)>0 &&
           CdControlB(2,(u8 *)&position,NULL)>0 && CdControlB(27,NULL,NULL)>0;
    if(!ok)MenuCard_StopMovieAudio();
    return ok;
}

#ifndef MENUCARD_UPDATE_VIDEO_FRAME_NAME
#define MENUCARD_UPDATE_VIDEO_FRAME_NAME MenuCard_UpdateVideoFrame
#define MENUCARD_VIDEO_ENTRY g_MenuCardScreenVideoEntry
#define MENUCARD_VIDEO_LAST_TIME g_MenuCardScreenVideoLastTime
#define MENUCARD_VIDEO_FINISHED g_MenuCardScreenVideoFinished
#define MENUCARD_VIDEO_SIZE g_MenuCardScreenVideoSize
#endif

void *MENUCARD_UPDATE_VIDEO_FRAME_NAME(VideoDisplay *display) {
    VideoFrame header;
    VideoFrame *frame = (VideoFrame *)MENUCARD_VIDEO_STREAM_BUFFER;
    u8 raw[2352];
    u8 *data;
    u32 part = 0;
    s32 end, remaining;
    VideoRect clear;
    if (!g_CdImage || !display || !MENUCARD_VIDEO_ENTRY || !MENUCARD_VIDEO_STREAM_BUFFER) return 0;
    data = MENUCARD_VIDEO_STREAM_BUFFER + sizeof(VideoFrame);
    while (MENUCARD_VIDEO_SECTOR < ((u64)MENUCARD_VIDEO_FILE.size + 2047) / 2048) {
        if (!pe_disc_read_raw(g_CdImage, &MENUCARD_VIDEO_FILE, MENUCARD_VIDEO_SECTOR++, raw)) return 0;
        memcpy(&header, raw + 24, sizeof(header));
        if (header.id != 0x160 || header.type != 0x8001) continue; /* Interleaved XA. */
        if (!part) {
            if (header.secCount) return 0;
            if (header.frameCount > 32767 || !header.nSectors || header.nSectors > (0x20000 - sizeof(*frame)) / 2016 ||
                header.frameSize < 12 || header.frameSize > (u32)header.nSectors * 2016 ||
                !header.width || header.width > 320 || !header.height || header.height > 240)
                return 0;
            *frame = header;
        }
        if (header.secCount != part || header.frameCount != frame->frameCount ||
            header.nSectors != frame->nSectors || header.frameSize != frame->frameSize ||
            header.width != frame->width || header.height != frame->height) return 0;
        memcpy(data + part * 2016, raw + 56, 2016);
        if (++part == frame->nSectors) break;
    }
    if (!part || part != frame->nSectors) return 0;
    u16 words;
    memcpy(&words, data, sizeof(words));
    if (!words || 4u + (u32)words * 4 > 0xfa00) return 0;
    end = MENUCARD_VIDEO_ENTRY->end;
    if (frame->frameCount >= (end - 16)) {
        s32 position = frame->frameCount + 16;

        remaining = position - end;
        remaining = 14 - remaining;
        if (remaining < 0) {
            remaining = 0;
        }
        Spu_SetVoiceVolume(g_GameState.scene_audio.tracks.movie_volume * remaining / 14);
    }
    if (frame->frameCount < MENUCARD_VIDEO_LAST_TIME || frame->frameCount >= MENUCARD_VIDEO_ENTRY->end) {
        MENUCARD_VIDEO_FINISHED = 1;
    }
    MENUCARD_VIDEO_LAST_TIME = frame->frameCount;
    if (MENUCARD_VIDEO_SIZE[0] != frame->width || MENUCARD_VIDEO_SIZE[1] != frame->height) {
        if (g_MovieWide) {
            clear.x = 0;
            clear.y = 0;
            clear.w = 480;
            clear.h = 480;
        } else {
            clear.x = 0;
            clear.y = 0;
            clear.w = 320;
            clear.h = 480;
        }
        ClearImage(&clear, 0, 0, 0);
        MENUCARD_VIDEO_SIZE[0] = frame->width;
        MENUCARD_VIDEO_SIZE[1] = frame->height;
    }
    if (g_MovieWide) {
        display->regions[0].w = display->regions[1].w = MENUCARD_VIDEO_SIZE[0] * 3 / 2;
    } else {
        display->regions[0].w = display->regions[1].w = MENUCARD_VIDEO_SIZE[0];
    }
    display->regions[0].h = display->regions[1].h = MENUCARD_VIDEO_SIZE[1];
    display->rect.h = MENUCARD_VIDEO_SIZE[1];
    movieFrameNumber = frame->frameCount;
    return data;
}

#undef MENUCARD_VIDEO_SIZE
#undef MENUCARD_VIDEO_FINISHED
#undef MENUCARD_VIDEO_LAST_TIME
#undef MENUCARD_VIDEO_ENTRY
#undef MENUCARD_UPDATE_VIDEO_FRAME_NAME

#undef MENUCARD_VIDEO_FILE
#undef MENUCARD_VIDEO_SECTOR
#undef MENUCARD_VIDEO_STREAM_BUFFER
