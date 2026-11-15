#ifndef PE1_BOOT_H
#define PE1_BOOT_H

#include "common.h"
#include "pe1/pe_image.h"

void Boot_InitSystem(void);
void Boot_VsyncCallback(void);
/* Optional native diagnostics; the original game still reads its pad packet. */
typedef struct BootHostPadEvent { u32 vblank; u16 buttons; } BootHostPadEvent;
/* Explicit fixture-only health changes. Expected HP is checked before each
 * store so a divergent replay fails instead of silently hiding damage. */
typedef struct BootHostHealthEvent { u32 vblank; u16 expected,value; } BootHostHealthEvent;
extern BootHostHealthEvent *g_BootHostHealthEvents;
extern unsigned g_BootHostHealthEventCount,g_BootHostHealthApplied;
extern int g_BootHostActive;
/* Asks whether to skip the story movie, with the movie paused; nonzero skips.
 * Without a host prompt the skip button skips at once. */
extern int (*g_BootMovieSkipConfirm)(void);
/* Called when a scene has loaded, after the mods' scene start hook, before
 * its first frame (the crash report's room snapshot). */
extern void (*g_BootSceneStarted)(void);
/* --test-save-after N: from VBlank N, the first free field frame saves to
 * memory card 1, slot 1 through the game's own save writer (as the save menu
 * does), for tests that load the save back. g_BootTestSave returns nonzero
 * once the save is done (or failed), zero to try again next frame. */
extern u32 g_BootHostSaveAt;
extern int (*g_BootTestSave)(void);
extern u32 g_BootHostVBlankLimit,g_BootHostVBlankCount;
/* Headless diagnostic boundary; stdin controls input and graphics settings. */
extern u32 g_BootHostControlAt;
extern void (*g_BootHostHdOptions)(int enabled, const char *directory);
extern void (*g_BootHostGraphicsOptions)(int geometry, int antialiasing, int filtering);
extern const char *g_BootHostVramPath;
extern const char *g_BootHostFramePath;
extern const char *g_BootHostAudioPath;
extern BootHostPadEvent *g_BootHostPadEvents;
extern unsigned g_BootHostPadEventCount;
/* --test-crash-after / --test-abort-after: a deliberate crash at this VBlank,
 * to check that crash reports work. */
extern u32 g_BootHostCrashAt;
extern int g_BootHostCrashAbort;

/* Frame counter boot/frame_loop.c advances once per frame (an array view, so
 * -G8 units still address it absolutely). */
extern unsigned int g_GlobalFrameCounter[];

/* Boot sequence (boot/main.c) and the services it starts. */
void Boot_InitSubsystems(void);
/* 0 on success; -1..-8 identify read/poll failure in four startup stages,
 * -9 sample upload failure, -10 invalid workspace or sector extents. */
int Scene_LoadSceneData(void);
/* 0 success; -1 audio; -2/-3 main read/poll; -4/-5 texture read/poll;
 * -6/-7 directory read/poll; -8 invalid workspace or sector range. */
int Boot_LoadOverlayTables(void);
/* Boot-display data only: -1 read, -2 poll, -3 invalid range/workspace,
 * -4 invalid credits metadata.
 * Decoder code is statically linked; no executable overlay is loaded. */
int CdRom_LoadBootAudio(void);
/* -1: read failure; -2: poll failure; -3: invalid range/workspace. */
int CdRom_LoadInitialImage(void);
int Draw_InitDisplay(int mode);
int Boot_RunFrame(void);
/* Returns the room loader error before entering an incomplete scene. */
int Draw_InitPipeline(void);
/* Original statically linked overlay entry points. */
int FxCommon_RunFrameLoop(void);
int MenuCard_RunListScreen(void);

/* Disc table load (pm/disc_tables.c): PE.IMG sector ranges of the
 * startup assets (entries of the g_PeImageSectorTable sector table). */

/* Original per-frame additive RNG: 14 retail instruction constants,
 * two byte-offset cursors and 17 seeded words in one contiguous object. */
extern u32 g_DrawPrimGeneratorWords[33];
void Draw_InitGpuHwRegs(void);
u32 Draw_FlushPrimQueue(void);

/* System start-up (boot/init.c). */
/* Frame counter stepped by Boot_RunFrame; battle colour cycles, the status
 * pointer rotation and the battle srand seed read it. */
extern u32 g_FrameRngCounter;

/* Pad sampling (boot/init.c): render flag table, the
 * button mask per game bit, frames each bit has been held, the actuator
 * buffers and the cheat sequence masks. */
extern s32 g_FieldRenderFlagTable[];
extern u32 g_FieldPadHeldFrameCounts[];
/* Left-stick magnitude (0..100 percent) required to run outside battles. */
extern unsigned g_FieldAnalogRunThreshold;
extern u8 g_PadSetActBuffer[];
extern u8 g_PadActuatorAlign[];
extern u32 g_PadComboSequence[];

/* Render pipeline set-up (draw/pipeline.c): the projection distances
 * of the field and battle views. */
extern u32 g_EntityRenderScratch[9];
#define g_RenderProjectionDistance (g_EntityRenderScratch + 8)

void Draw_RenderFrame(void);
int Draw_CheckDrawStatus(void);

#endif
