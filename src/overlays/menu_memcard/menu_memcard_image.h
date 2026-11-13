#ifndef MENU_MEMCARD_IMAGE_H
#define MENU_MEMCARD_IMAGE_H

#include "common.h"
#include "pe1/pe_image.h"
#include "pe1/menu_sound.h"
#include "pe1/cdrom_buffers.h"

typedef struct MemcardImage { u8 header[16]; s16 width, height; } MemcardImage;
typedef struct MemcardImageNode {
    struct MemcardImageNode *next;
    s16 x, y, width, height;
    void (*draw)(struct MemcardImageNode *);
    void (*update)(struct MemcardImageNode *, u8 *, s32, s32);
    void (*field14)(struct MemcardImageNode *);
    MemcardImage *volatile image;
    s32 intensity, targetY, stepY, parameter, kind, freePending;
} MemcardImageNode;
typedef struct MemcardImagePreset { s32 x, y, parameter; } MemcardImagePreset;
extern MemcardImageNode *g_MemcardFreeImageNodes, *g_MemcardPendingNodeTail, *g_MemcardPendingNodeHead, *g_MemcardActiveNodeHead;
extern MemcardImageNode *g_MemcardActiveNodeTail; /* Active-list tail. */
extern MemcardImageNode g_MemcardImageNodePool[8];
/* The title bank keeps its authored offsets and preset data as data, in PE.IMG entry 70 after the
 * title code. The Japanese entry has less code before it (no publisher logo, MenuCard_ShowLogoFade)
 * and one image fewer (8: that logo), so its bank and presets start earlier. */
#define g_MemcardImageBundle (g_StrFileDirBuffer + (PE_JAPAN ? 0x3EEC : 0x4264))
#define g_MemcardImagePresets ((MemcardImagePreset *)(g_MemcardImageBundle + (PE_JAPAN ? 0x3BAC4 : 0x3DB08)))
#define g_MemcardImageOffsets ((s32 *)(g_MemcardImageBundle + 4))

MemcardImageNode *MenuCard_CreateImageNode(s32 kind);
void MenuCard_CreateFixedImageNode(void);
void MenuCard_CreateImageGroup(void);

void MenuCard_BlendImage(MemcardImageNode *node, u8 *destination, s32 padding, s32 stride);

void MenuCard_BlendRotatedImage(MemcardImageNode *node, u8 *destination, s32 padding, s32 stride);

void MenuCard_InitFadeForNode(MemcardImageNode *node);
void MenuCard_UpdateFadeCounter(MemcardImageNode *node);
void MenuCard_AdvanceFade(MemcardImageNode *node);
void MenuCard_UpdateFadeDisplay(MemcardImageNode *node);
void MenuCard_DecreaseFade(MemcardImageNode *node);
void MenuCard_CreateFadeImageNode(MemcardImageNode *node);
void MenuCard_AttachFadePayload(MemcardImageNode *node);
void MenuCard_StepFadeToImages(MemcardImageNode *node);
void MenuCard_RedrawImageNodes(void);
void MenuCard_HandleListInput(void);

/* Memory card list screen input. */
extern s32 g_MemcardPrevPad; /* Pad state of the previous frame. */
extern s32 g_MemcardScreenTimer;
s32 Draw_RemapStatusFlags(void); /* Draw_RemapStatusFlags: pad state */
s32 MemCard_IsPortPresent(s32 port); /* MemCard_IsPortPresent */
s32 MemCard_HasAnyPresentFile(void);

#endif
