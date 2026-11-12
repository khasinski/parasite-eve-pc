#ifndef PE1_BOOT_DISC_CHECK_H
#define PE1_BOOT_DISC_CHECK_H

#include "common.h"
#include "pe1/cdrom.h"
#include "pe1/display_env.h"
#include "pe1/game_state.h"
#include "pe1/gpu_image.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_packets.h"
#include "pe1/textbox.h"

/* Validate the selected file image and clear the two field display regions.
 * 0 succeeds; -1..-3 are image/archive errors; -4 is invalid/missing disc mode.
 * Missing native images may be supplied through g_CdImageRequest; no drive is emulated. */

extern u16 g_PeImageSectorTable[];
extern s32 g_ActiveDrawSlot;
/* Ordering table of each draw slot (Boot_InitMemoryLayout). */

void Boot_BuildRenderFlagTable(void);
void Render_SetupFogLayer(void *source);
void ResetGraph(int mode);
/* Inspect a candidate without changing the active archive, tables or game. */
int Asset_ValidatePeImage(PeDisc *disc, unsigned *identity);
/* The same, also giving the release (PE_REGION_*), so Disc 1 and Disc 2 can be matched. */
int Asset_ValidatePeImageRegion(PeDisc *disc, unsigned *identity, unsigned *region);
int Asset_OpenPeImage(void);
int Render_InitDisplayLists(int mode);

#endif
