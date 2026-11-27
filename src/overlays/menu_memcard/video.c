#include "common.h"
#include "pe1/cdrom.h"
#include "pe1/menu_memcard_display.h"
#include "pe1/psyq_press.h"

#include "menu_memcard_video.h"
#include "video_externs.h"

#define MENUCARD_SET_VIDEO_DISPLAY_NAME MenuCardFirst_SetVideoDisplay
#define MENUCARD_VIDEO_DISPLAY_MODE g_MenuCardVideoDecodeMode
#include "video_display.inc"

#undef MENUCARD_SET_VIDEO_DISPLAY_NAME
#undef MENUCARD_VIDEO_DISPLAY_MODE

#define MENUCARD_UPDATE_VIDEO_FRAME_NAME MenuCardFirst_UpdateVideoFrame
#define MENUCARD_VIDEO_ENTRY g_MenuCardVideoEntry
#define MENUCARD_VIDEO_LAST_TIME g_MenuCardVideoLastTime
#define MENUCARD_VIDEO_FINISHED g_MenuCardVideoFinished
#define MENUCARD_VIDEO_SIZE g_MenuCardVideoSize
#define MENUCARD_VIDEO_FILE g_MenuCardVideoFile
#define MENUCARD_VIDEO_SECTOR g_MenuCardVideoSector
#define MENUCARD_VIDEO_STREAM_BUFFER g_MenuCardVideoStreamRing
#include "video_frame.inc"

#undef MENUCARD_UPDATE_VIDEO_FRAME_NAME
#undef MENUCARD_VIDEO_ENTRY
#undef MENUCARD_VIDEO_LAST_TIME
#undef MENUCARD_VIDEO_FINISHED
#undef MENUCARD_VIDEO_SIZE

#define MENUCARD_UPLOAD_VIDEO_SLICE_NAME MenuCardFirst_UploadVideoSlice
#define MENUCARD_VIDEO_DISPLAY g_MenuCardVideoDisplay
#define MENUCARD_VIDEO_FLIP g_MenuCardVideoDepthFlip
#define MENUCARD_VIDEO_SET_DISPLAY MenuCardFirst_SetVideoDisplay
#include "video_upload.inc"

#undef MENUCARD_UPLOAD_VIDEO_SLICE_NAME
#undef MENUCARD_VIDEO_DISPLAY
#undef MENUCARD_VIDEO_FLIP
#undef MENUCARD_VIDEO_SET_DISPLAY

#define MENUCARD_INIT_VIDEO_BUFFERS_NAME MenuCardFirst_InitVideoBuffers
#define MENUCARD_VIDEO_PTR0 g_MenuCardVideoDecodeBuffer0
#define MENUCARD_VIDEO_PTR1 g_MenuCardVideoDecodeBuffer1
#define MENUCARD_VIDEO_PTR2 g_MenuCardVideoSliceBuffer0
#define MENUCARD_VIDEO_PTR3 g_MenuCardVideoSliceBuffer1
#define MENUCARD_VIDEO_PTR4 g_MenuCardVideoVlcTable
#define MENUCARD_VIDEO_PTR5 g_MenuCardVideoStreamRing
#define MENUCARD_VIDEO_LAST_TIME g_MenuCardVideoLastTime
#define MENUCARD_VIDEO_SAVED_DISPLAY g_MenuCardVideoSavedDispEnv
#define MENUCARD_VIDEO_SAVED_DRAW g_MenuCardVideoSavedDrawEnv
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

#define MENUCARD_RESET_DISPLAY_STATE_NAME MenuCardFirst_ResetDisplayState
#define MENUCARD_DISPLAY_PTR0 g_MenuCardVideoDecodeBuffer0
#define MENUCARD_DISPLAY_PTR1 g_MenuCardVideoDecodeBuffer1
#define MENUCARD_DISPLAY_PTR2 g_MenuCardVideoSliceBuffer0
#define MENUCARD_DISPLAY_PTR3 g_MenuCardVideoSliceBuffer1
#define MENUCARD_DISPLAY_PTR4 g_MenuCardVideoVlcTable
#define MENUCARD_DISPLAY_PTR5 g_MenuCardVideoStreamRing
#define MENUCARD_SLOT20_SOURCE g_MenuCardVideoSavedDispEnv
#define MENUCARD_SLOT92_SOURCE g_MenuCardVideoSavedDrawEnv
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

#define MENUCARD_OPEN_VIDEO_NAME MenuCardFirst_OpenVideo
#define MENUCARD_VIDEO_TABLE g_MenuCardVideoTable
#define MENUCARD_VIDEO_ENTRY g_MenuCardVideoEntry
#define MENUCARD_VIDEO_SET_DISPLAY MenuCardFirst_SetVideoDisplay
#define MENUCARD_VIDEO_DIRECTORY g_MenuCardVideoDirectory
#define MENUCARD_VIDEO_DIRECTORY_LATE g_MenuCardVideoDirectoryLate
#define MENUCARD_VIDEO_FILE g_MenuCardVideoFile
#define MENUCARD_VIDEO_SECTOR g_MenuCardVideoSector
#define MENUCARD_VIDEO_DISPLAY g_MenuCardVideoDisplay
#define MENUCARD_VIDEO_PTR0 g_MenuCardVideoDecodeBuffer0
#define MENUCARD_VIDEO_PTR1 g_MenuCardVideoDecodeBuffer1
#define MENUCARD_VIDEO_PTR2 g_MenuCardVideoSliceBuffer0
#define MENUCARD_VIDEO_PTR3 g_MenuCardVideoSliceBuffer1
#define MENUCARD_VIDEO_PTR4 g_MenuCardVideoVlcTable
#define MENUCARD_VIDEO_PTR5 g_MenuCardVideoStreamRing
#define MENUCARD_VIDEO_UPLOAD MenuCardFirst_UploadVideoSlice
#define MENUCARD_VIDEO_UPDATE_FRAME MenuCardFirst_UpdateVideoFrame
#define MENUCARD_VIDEO_FINISHED g_MenuCardVideoFinished
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

#define MENUCARD_STEP_VIDEO_NAME MenuCardFirst_StepVideo
#define MENUCARD_VIDEO_SET_DISPLAY MenuCardFirst_SetVideoDisplay
#define MENUCARD_VIDEO_UPDATE_FRAME MenuCardFirst_UpdateVideoFrame
#define MENUCARD_VIDEO_MODE g_MenuCardVideoDecodeMode
#define MENUCARD_VIDEO_FLIP g_MenuCardVideoDepthFlip
#define MENUCARD_VIDEO_FILE g_MenuCardVideoFile
#define MENUCARD_VIDEO_SECTOR g_MenuCardVideoSector
#define MENUCARD_VIDEO_DISPLAY g_MenuCardVideoDisplay
#define MENUCARD_VIDEO_PTR4 g_MenuCardVideoVlcTable
#define MENUCARD_VIDEO_FINISHED g_MenuCardVideoFinished
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

void MenuCardFirst_StopVideo(void) {
    unsigned char *counter;
    int value;

    counter = &g_DrawEnabled.bits;
    value = *counter ? *counter - 1 : 0;
    *counter = value;
    Spu_SetVoiceVolume(0);
    MenuCard_ClearMovieFrames();
    DecDCToutCallback(0);
}

#define MENUCARD_EVENT_FLAG_NAME MenuCardFirst_RequestVideoDepthFlip
#define MENUCARD_EVENT_FLAG g_MenuCardVideoDepthFlip
#include "event_flag.inc"

#undef MENUCARD_EVENT_FLAG_NAME
#undef MENUCARD_EVENT_FLAG

/* Original first-player state; kept separate from the memory-card copy. */
VideoEntry *g_MenuCardVideoEntry;
u8 g_MenuCardVideoFinished, g_MenuCardVideoDecodeMode, g_MenuCardVideoDepthFlip;
s16 g_MenuCardVideoLastTime, g_MenuCardVideoSize[2];
u8 *g_MenuCardVideoDecodeBuffer0, *g_MenuCardVideoDecodeBuffer1;
u8 *g_MenuCardVideoSliceBuffer0, *g_MenuCardVideoSliceBuffer1;
u8 *g_MenuCardVideoVlcTable, *g_MenuCardVideoStreamRing;
DISPENV g_MenuCardVideoSavedDispEnv[2];
DRAWENV g_MenuCardVideoSavedDrawEnv[2];
VideoDisplay g_MenuCardVideoDisplay;
PeDiscFile g_MenuCardVideoFile;
u32 g_MenuCardVideoSector;
char g_MenuCardVideoDirectory[]="\\FMV1";
char g_MenuCardVideoDirectoryLate[]="\\FMV2";

/* Authored USA first-player table at 80122438 (archive bank 68). */
VideoEntry g_MenuCardVideoTable[47] = {
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
