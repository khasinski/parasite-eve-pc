#include <psyz/gpu.h>
#include "pe1/geom_state.h"
#ifdef PE_PS5_GPU_NATIVE
#include "pe1/particle_augment.h"
#endif
#include "pe1/battle_status.h"
#include <psyz/gpu.h>
#include "pe1/boot.h"
#include "pe1/boot_disc_check.h"
#include "pe1/draw_buffers.h"
#include "pe1/game_state.h"
#include "pe1/geom_state.h"
#include "pe1/menu_background.h"
#include "pe1/menu_widget.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_packets.h"
#include "pe_hooks.h"
#include "pe1/textbox.h"

int g_ActiveDrawSlot;
DISPENV g_RenderDispEnvArray[2];
DRAWENV g_RenderDrawEnvArray[2];

short Seq_GetElapsed(void);

static inline DISPENV *DispAddress(int index) {
    return &g_RenderDispEnvArray[index];
}

static inline DRAWENV *DrawAddress(int index) {
    return &g_RenderDrawEnvArray[index];
}

/* Highest ordering-table entry of the 2D layers widescreen keeps centred:
 * 2 when a dialogue was drawn this frame (its backdrop links there), 1 while
 * a menu is open, else 0. */
static int Draw_WidescreenOverlayEntry(void) {
    int drawn = g_TextboxDrawnThisFrame;

    g_TextboxDrawnThisFrame = 0;
    if (drawn) return 2;
    return g_MenuWidgetCurrentNode != 0;
}

void Draw_RenderFrame(void) {
    int displayFlags;
    int idx;
    int state;
    int status;

    DrawSync(0);
    Menu_DrawSaveBg();
    PE_HOOK(PE_HOOK_FRAME_DRAWN, NULL);

    Psyz_GpuPrepareInterpolatedPresent(-1);
    displayFlags = g_GameState.flags & 0x200;
    if (displayFlags) {
        VSync(4);
        if (Seq_GetElapsed() >= 3) {
            SetDispMask(1);
        }
    } else {
        Psyz_GpuPrepareInterpolatedPresent(Draw_CheckDrawStatus()==0 ?
            g_RenderDrawEnvArray[1-g_ActiveDrawSlot].clip.y : -1);
        VSync(2);
    }

    ResetGraph(1);

    idx = g_ActiveDrawSlot;
    PutDispEnv(DispAddress(idx));

    status = Draw_CheckDrawStatus();
    state = g_GameState.flags;
    if ((status & 0xff) != 0 || (state & 0x200) != 0) {
        idx = g_ActiveDrawSlot;
        PutDrawEnv(DrawAddress(idx));
        Render_CommitWidescreenPicture(idx, -1);
#ifdef PE_PS5_GPU_NATIVE
        FieldFx_ParticleEndFrame();
#endif
        g_ActiveDrawSlot = g_ActiveDrawSlot < 1U;
        return;
    }
draw_buffer:
    {
        int drawSlot;
        RenderNativeTag *orderingTable;

        drawSlot = g_ActiveDrawSlot;
        orderingTable = (RenderNativeTag *)g_GameState.shared_buffers.ordering_tables[drawSlot];
        Render_LinkFieldSubpixel(drawSlot, orderingTable);
        Render_CommitWidescreenPicture(drawSlot, g_RenderDrawEnvArray[drawSlot].clip.y);
        Render_CentreWidescreenOverlay(drawSlot, orderingTable, Draw_WidescreenOverlayEntry());
        /* All original packet writers/hooks have completed. Admit optional
         * debris only into the real remaining arena, immediately before DMA. */
#ifdef PE_PS5_GPU_NATIVE
        FieldFx_FlushParticleAugment();
        FieldFx_ApplyParticleLights();
#endif
        Psyz_GpuBeginInterpolationFrame();
        Psyz_GpuSetInterpolationCamera(g_GeomScroll.state.position.x,g_GeomScroll.state.position.y);
        DrawOTagEnv(orderingTable + 4095, &g_RenderDrawEnvArray[drawSlot]);
#ifdef PE_PS5_GPU_NATIVE
        /* Native addque2 executes GPU_Enqueue synchronously and copies the
         * packet words. GPU resources may remain live; original CPU colours
         * can now be restored without changing the queued draw. */
        FieldFx_RestoreParticleLights();
#endif
    }

done:
#ifdef PE_PS5_GPU_NATIVE
    FieldFx_ParticleEndFrame();
#endif
    g_ActiveDrawSlot = g_ActiveDrawSlot < 1U;
}
