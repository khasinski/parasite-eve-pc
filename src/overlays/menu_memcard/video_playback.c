#include "common.h"
#include "pe1/cdrom.h"
#include "pe1/menu_memcard_display.h"
#include "pe1/psyq_press.h"

#include "menu_memcard_video.h"

#define MENUCARD_SET_VIDEO_DISPLAY_NAME MenuCard_SetVideoDisplay
#define MENUCARD_VIDEO_DISPLAY_MODE g_MenuCardScreenVideoDecodeMode
#include "video_display.inc"

#undef MENUCARD_SET_VIDEO_DISPLAY_NAME
#undef MENUCARD_VIDEO_DISPLAY_MODE

#define MENUCARD_UPDATE_VIDEO_FRAME_NAME MenuCard_UpdateVideoFrame
#define MENUCARD_VIDEO_ENTRY g_MenuCardScreenVideoEntry
#define MENUCARD_VIDEO_LAST_TIME g_MenuCardScreenVideoLastTime
#define MENUCARD_VIDEO_FINISHED g_MenuCardScreenVideoFinished
#define MENUCARD_VIDEO_SIZE g_MenuCardScreenVideoSize
#define MENUCARD_VIDEO_FILE g_MenuCardScreenVideoFile
#define MENUCARD_VIDEO_SECTOR g_MenuCardScreenVideoSector
#define MENUCARD_VIDEO_STREAM_BUFFER g_MenuCardScreenVideoStreamRing
#include "video_frame.inc"

#undef MENUCARD_UPDATE_VIDEO_FRAME_NAME
#undef MENUCARD_VIDEO_ENTRY
#undef MENUCARD_VIDEO_LAST_TIME
#undef MENUCARD_VIDEO_FINISHED
#undef MENUCARD_VIDEO_SIZE

#include "video_upload.inc"

#undef MENUCARD_UPLOAD_VIDEO_SLICE_NAME
#undef MENUCARD_VIDEO_DISPLAY
#undef MENUCARD_VIDEO_FLIP
#undef MENUCARD_VIDEO_SET_DISPLAY

#define MENUCARD_INIT_VIDEO_BUFFERS_NAME MenuCard_InitVideoBuffers
#define MENUCARD_VIDEO_PTR0 g_MenuCardScreenVideoDecodeBuffer0
#define MENUCARD_VIDEO_PTR1 g_MenuCardScreenVideoDecodeBuffer1
#define MENUCARD_VIDEO_PTR2 g_MenuCardScreenVideoSliceBuffer0
#define MENUCARD_VIDEO_PTR3 g_MenuCardScreenVideoSliceBuffer1
#define MENUCARD_VIDEO_PTR4 g_MenuCardScreenVideoVlcTable
#define MENUCARD_VIDEO_PTR5 g_MenuCardScreenVideoStreamRing
#define MENUCARD_VIDEO_LAST_TIME g_MenuCardScreenVideoLastTime
#define MENUCARD_VIDEO_SAVED_DISPLAY g_MenuCardScreenSavedDispEnv
#define MENUCARD_VIDEO_SAVED_DRAW g_MenuCardScreenSavedDrawEnv
#include "video_buffers.inc"

#undef MENUCARD_INIT_VIDEO_BUFFERS_NAME
#undef MENUCARD_VIDEO_PTR0
#undef MENUCARD_VIDEO_PTR1
#undef MENUCARD_VIDEO_PTR2
#undef MENUCARD_VIDEO_PTR3
#undef MENUCARD_VIDEO_PTR4
#undef MENUCARD_VIDEO_PTR5
#undef MENUCARD_VIDEO_LAST_TIME
#undef MENUCARD_VIDEO_SAVED_DISPLAY
#undef MENUCARD_VIDEO_SAVED_DRAW

#include "display_reset.inc"

#undef MENUCARD_RESET_DISPLAY_STATE_NAME
#undef MENUCARD_DISPLAY_PTR0
#undef MENUCARD_DISPLAY_PTR1
#undef MENUCARD_DISPLAY_PTR2
#undef MENUCARD_DISPLAY_PTR3
#undef MENUCARD_DISPLAY_PTR4
#undef MENUCARD_DISPLAY_PTR5
#undef MENUCARD_SLOT20_SOURCE
#undef MENUCARD_SLOT92_SOURCE
#undef PE1_EXT_6E76AD431068

#include "video_open.inc"

#undef MENUCARD_OPEN_VIDEO_NAME
#undef MENUCARD_VIDEO_TABLE
#undef MENUCARD_VIDEO_ENTRY
#undef MENUCARD_VIDEO_SET_DISPLAY
#undef MENUCARD_VIDEO_DIRECTORY
#undef MENUCARD_VIDEO_DIRECTORY_LATE
#undef MENUCARD_VIDEO_FILE
#undef MENUCARD_VIDEO_SECTOR
#undef MENUCARD_VIDEO_DISPLAY
#undef MENUCARD_VIDEO_PTR0
#undef MENUCARD_VIDEO_PTR1
#undef MENUCARD_VIDEO_PTR2
#undef MENUCARD_VIDEO_PTR3
#undef MENUCARD_VIDEO_PTR4
#undef MENUCARD_VIDEO_PTR5
#undef MENUCARD_VIDEO_UPLOAD
#undef MENUCARD_VIDEO_UPDATE_FRAME
#undef MENUCARD_VIDEO_FINISHED

#include "video_step.inc"

#undef MENUCARD_STEP_VIDEO_NAME
#undef MENUCARD_VIDEO_SET_DISPLAY
#undef MENUCARD_VIDEO_UPDATE_FRAME
#undef MENUCARD_VIDEO_MODE
#undef MENUCARD_VIDEO_FLIP
#undef MENUCARD_VIDEO_FILE
#undef MENUCARD_VIDEO_SECTOR
#undef MENUCARD_VIDEO_DISPLAY
#undef MENUCARD_VIDEO_PTR4
#undef MENUCARD_VIDEO_FINISHED

void Spu_SetVoiceVolume(int arg0);

void MenuCard_StopVideo(void) {
    unsigned char *counter;
    int value;

    counter = &g_DrawEnabled.bits;
    value = *counter ? *counter - 1 : 0;
    *counter = value;
    Spu_SetVoiceVolume(0);
    DecDCToutCallback(0);
}

#define MENUCARD_EVENT_FLAG_NAME MenuCard_RequestVideoDepthFlip
#define MENUCARD_EVENT_FLAG g_MenuCardScreenVideoDepthFlip
#include "event_flag.inc"

#undef MENUCARD_EVENT_FLAG_NAME
#undef MENUCARD_EVENT_FLAG

s32 MenuCard_PlayVideo(s32 index) {
    u8 *buffer;
    s32 entry = index;
    s32 result = 0;
    u32 *flags;
    /* VLC/MDEC code is linked statically; only its original work arena remains. */
    _Alignas(16) static u8 movie_buffers[0x55e00];
    if (index < 0 || index >= 47 || g_DrawEnabled.bits) return 0;
    flags=&g_GameStateFlagsWord;
    *flags |= 0x200;
    g_MenuCardScreenVideoTable[entry].wide=1;
    SetDispMask(0); DrawSync(0); ResetGraph(1);
    buffer=movie_buffers;
    if (!MenuCard_InitVideoBuffers(1,&buffer) || !MenuCard_OpenVideo((s16)entry)) {
        DecDCToutCallback(0);
        MenuCard_ResetDisplayState();
        *flags &= ~0x200;
        return 0;
    }
    {
        u8 *active=&g_DrawEnabled.bits;
        if (*active) {
            s16 *remainingPtr=&g_SeqElapsed;
            do {
                s32 left = *remainingPtr;
                if (left <= 0) {
                    DecDCToutCallback(0);
                    MenuCard_ResetDisplayState();
                    break;
                }
                Field_HandleStateTransition();
                if (!(s8)MenuCard_StepVideo()) {
                    MenuCard_ResetDisplayState();
                }
                else if (g_FieldPadBits & 0x20000004) {
                    s32 remaining;
                    MenuCard_StopVideo();
                    remaining=*remainingPtr;
                    MenuCard_ResetDisplayState();
                    if(remaining<1400) {VSync(0);SetDispMask(0);result=1;}
                }
                Draw_RenderFrame();
            } while(g_DrawEnabled.bits);
        }
    }
    { s32 returnValue = result;
        flags=&g_GameStateFlagsWord;
        *flags &= ~0x200;
        return returnValue; }
}

/* Native storage for the original title-screen movie player. */
VideoEntry *g_MenuCardScreenVideoEntry;
u8 g_MenuCardScreenVideoFinished, g_MenuCardScreenVideoDecodeMode, g_MenuCardScreenVideoDepthFlip;
s16 g_MenuCardScreenVideoLastTime, g_MenuCardScreenVideoSize[2];
u8 *g_MenuCardScreenVideoDecodeBuffer0, *g_MenuCardScreenVideoDecodeBuffer1;
u8 *g_MenuCardScreenVideoSliceBuffer0, *g_MenuCardScreenVideoSliceBuffer1;
u8 *g_MenuCardScreenVideoVlcTable, *g_MenuCardScreenVideoStreamRing;
DISPENV g_MenuCardScreenSavedDispEnv[2];
DRAWENV g_MenuCardScreenSavedDrawEnv[2];
VideoDisplay g_MenuCardScreenVideoDisplay;
PeDiscFile g_MenuCardScreenVideoFile;
u32 g_MenuCardScreenVideoSector;
char g_MenuCardScreenVideoDirectory[]="\\FMV1";
char g_MenuCardScreenVideoDirectoryLate[]="\\FMV2";

/* Authored USA menu bank 70 table at 0x801D0E00, with native name pointers. */
VideoEntry g_MenuCardScreenVideoTable[47] = {
    {"\\FMV000.STR;1",0,1,150,0,0,{64,1,240,0,0,0}},
    {"\\FMV001.STR;1",0,1,2077,0,0,{64,1,240,0,0,0}},
    {"\\FMV002.STR;1",0,1,1021,0,0,{64,1,240,0,0,0}},
    {"\\FMV003.STR;1",0,1,1151,0,0,{64,1,240,0,0,0}},
    {"\\FMV004.STR;1",0,1,561,0,0,{64,1,240,0,0,0}},
    {"\\FMV005.STR;1",0,1,231,0,0,{64,1,240,0,0,0}},
    {"\\FMV006.STR;1",0,1,744,0,0,{64,1,240,0,0,0}},
    {"\\FMV006C.STR;1",0,1,205,0,0,{64,1,240,0,0,0}},
    {"\\FMV007.STR;1",0,1,449,0,0,{64,1,240,0,0,0}},
    {"\\FMV008.STR;1",0,1,220,0,0,{64,1,240,0,0,0}},
    {"\\FMV009.STR;1",0,1,275,0,0,{64,1,240,0,0,0}},
    {"\\FMV010.STR;1",0,1,507,0,0,{64,1,240,0,0,0}},
    {"\\FMV011.STR;1",0,1,554,0,0,{64,1,240,0,0,0}},
    {"\\FMV012.STR;1",0,1,359,0,0,{64,1,240,0,0,0}},
    {"\\FMV013.STR;1",0,1,375,0,0,{64,1,240,0,0,0}},
    {"\\FMV014.STR;1",0,1,424,0,0,{64,1,240,0,0,0}},
    {"\\FMV015.STR;1",0,1,340,0,0,{64,1,240,0,0,0}},
    {"\\FMV016A.STR;1",0,1,104,0,0,{64,1,240,0,0,0}},
    {"\\FMV016B.STR;1",0,1,97,0,0,{64,1,240,0,0,0}},
    {"\\FMV017A.STR;1",0,1,459,0,0,{64,1,240,0,0,0}},
    {"\\FMV017B.STR;1",0,1,194,0,0,{64,1,240,0,0,0}},
    {"\\FMV018.STR;1",0,1,729,0,0,{64,1,240,0,0,0}},
    {"\\FMV019.STR;1",0,1,353,0,0,{64,1,240,0,0,0}},
    {"\\FMV020A.STR;1",0,1,556,0,0,{64,1,240,0,0,0}},
    {"\\FMV020B.STR;1",0,1,1113,0,0,{64,1,240,0,0,0}},
    {"\\FMV021.STR;1",0,1,186,0,0,{64,1,240,0,0,0}},
    {"\\FMV023A.STR;1",0,1,397,0,0,{64,1,240,0,0,0}},
    {"\\FMV023B.STR;1",0,1,545,0,0,{64,1,240,0,0,0}},
    {"\\FMV024A.STR;1",0,1,39,0,0,{64,1,240,0,0,0}},
    {"\\FMV024B1.STR;1",0,1,579,0,0,{64,1,240,0,0,0}},
    {"\\FMV024B3.STR;1",0,1,39,0,0,{64,1,240,0,0,0}},
    {"\\FMV024C.STR;1",0,1,120,0,0,{64,1,240,0,0,0}},
    {"\\FMV024C2.STR;1",0,1,56,0,0,{64,1,240,0,0,0}},
    {"\\FMV024E.STR;1",0,1,163,0,0,{64,1,240,0,0,0}},
    {"\\FMV024F.STR;1",0,1,665,0,0,{64,1,240,0,0,0}},
    {"\\FMV025.STR;1",0,1,367,0,0,{64,1,240,0,0,0}},
    {"\\FMV026.STR;1",0,1,310,0,0,{64,1,240,0,0,0}},
    {"\\FMV027.STR;1",0,1,497,0,0,{64,1,240,0,0,0}},
    {"\\FMV028.STR;1",0,1,120,0,0,{64,1,240,0,0,0}},
    {"\\FMV029.STR;1",0,1,438,0,0,{64,1,240,0,0,0}},
    {"\\FMV030.STR;1",0,1,296,0,0,{64,1,240,0,0,0}},
    {"\\FMV031.STR;1",0,1,451,0,0,{64,1,240,0,0,0}},
    {"\\FMV032.STR;1",0,1,226,0,0,{64,1,240,0,0,0}},
    {"\\FMV033A.STR;1",0,1,262,0,0,{64,1,240,0,0,0}},
    {"\\FMV033B.STR;1",0,1,482,0,0,{64,1,240,0,0,0}},
    {"\\FMV034.STR;1",0,1,219,0,0,{64,1,240,0,0,0}},
    {"\\FMV035.STR;1",0,1,720,0,0,{64,1,240,0,0,0}},
};
