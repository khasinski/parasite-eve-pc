#ifndef PE1_BOOT_SERVICES_H
#define PE1_BOOT_SERVICES_H

#include "common.h"
#include "pe1/memory_budget.h"

/* Soft-reset and start-up services (asset/game_state.c). */
extern int g_SceneDispatchToken;
void Boot_SyncShutdown(void);
void Boot_Shutdown(void);
int Draw_SetBaseY(int unused, u32 y);
void Boot_InitMemoryLayout(void);
void Boot_InitGameState(void);
/* Resets g_GameState and the scene voice-bank entries to start-up values. */
void Asset_ResetGameState(void);
/* End-of-frame pass: draws the fade primitives and sends queued AKAO commands. */
void Boot_FinishFrame(void);

/* Native storage carved by the original Boot_InitMemoryLayout. Overlay
 * workspaces live independently; native tags and actor pointers enlarge
 * the scene arena. The loaded-scene/directory buffer keeps its 8-byte
 * header before g_StrFileDirBuffer. */
extern u8 g_BootWorkArea[];
/* Room TIM transfers have a 12-bit sector count; the same workspace later
 * holds both native mesh-packet buffers. */
enum {
    BOOT_TEXTURE_WORKSPACE_BYTES = PE_BOOT_TEXTURE_WORKSPACE_BYTES,
    /* The original backdrop arena precedes the OT. Native model packets
     * carry wider links; keep their two buffers out of the ordering table. */
    BOOT_SCENE_OBJECT_WORK_BYTES = 0x1C98 * (sizeof(uintptr_t) / sizeof(u32))
};
extern u8 D_8010BD00[BOOT_TEXTURE_WORKSPACE_BYTES];
extern u8 D_80120D08[];
extern u8 g_SceneLoadScratchArea[];

#endif /* PE1_BOOT_SERVICES_H */
