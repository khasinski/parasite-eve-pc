#ifndef PE1_BOOT_DISPLAY_H
#define PE1_BOOT_DISPLAY_H

#include "common.h"
#include "pe1/cdrom.h"
#include "pe1/pe_image.h"
#include "pe1/asset_tim.h"
#include "pe1/cdrom_buffers.h"
#include "pe1/display_env.h"
#include "pe1/gpu_image.h"
#include "pe1/psyq_callbacks.h"
#include "pe1/psyq_cd.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_packets.h"

/* Three byte channels in the boot screen image buffer. */
typedef struct BootRgb24Pixel {
    u8 red, green, blue;
} BootRgb24Pixel;

PE1_STATIC_ASSERT(sizeof(BootRgb24Pixel) == 3, boot_rgb24_pixel_size);

typedef struct {
    RenderNativeTag tag;
    uintptr_t code;
} BootDisplayDrawModePrim;

typedef struct {
    RenderNativeTag tag;
    u8 r0;
    u8 g0;
    u8 b0;
    u8 code;
    u16 x0;
    u16 y0;
    u8 u0;
    u8 v0;
    u16 clut;
    u16 w;
    u16 h;
} BootDisplaySpritePrim;

typedef struct {
    BootDisplayDrawModePrim draw_mode;
    BootDisplaySpritePrim sprite;
} BootDisplaySpritePacket;
enum { BOOT_DISPLAY_SPRITE_CAPACITY=300 };
PE1_STATIC_ASSERT(sizeof(BootDisplaySpritePacket)==3*sizeof(uintptr_t)+sizeof(RenderNativeTag)+16, boot_sprite_native_size);

/* Native record per display buffer: GPU position, pixels and decode target. */
typedef struct {
    u16 x, y;
    char *pixels;
    u16 transferX, transferY;
    char *decodeTarget;
} BootDisplayImageBuffer;
extern BootDisplayImageBuffer g_BootDisplayImageBuffers[2];

typedef struct {
    void (*init)(void);
    void (*step)(void);
    void (*text)(void);
    void (*glyph)(s16, s16, u8);
    void (*transition)(u8);
    void (*present)(void);
    void (*shutdown)(void);
} BootDisplayCallbacks;
extern BootDisplayCallbacks *g_BootDisplayCallbacks;
extern u16 g_BootDisplayFontStyle;
/* All three historical biased width symbols address this same alphabet. */
extern const u8 g_BootDisplayUppercaseWidths[26][4];
#define g_BootDisplayLowercaseWidths g_BootDisplayUppercaseWidths
#define g_BootDisplayUppercaseWidthsStyle2 g_BootDisplayUppercaseWidths
extern const u8 *g_BootDisplayCreditLines[745];
extern void *g_BootDisplayCreditImages[31];
extern const u8 **g_BootDisplayTextCursor;

extern RenderNativeTag *g_BootDisplayOrderingTable;
extern u16 g_BootDisplayTPage;
extern u16 g_BootDisplayClut;
extern char *g_BootDisplayPrimitiveCursor;
extern char *g_BootDisplayPrimitiveEnd;
extern s32 g_BootDisplayImageBrightness;
extern s32 g_BootDisplayTextScrollX;
extern s32 g_BootDisplayTextScrollStepX;
extern s32 g_BootDisplayTransition;
extern s32 g_BootDisplayBufferIndex;
extern s16 g_BootDisplayStateTimer;
extern s16 g_BootDisplayTransferProgress;
extern s32 g_BootDisplayState;
extern s32 g_BootDisplayDone;
extern u32 g_BootDisplayFrameCounter;
extern RECT *g_BootDisplayFontImageRect;
extern RECT *g_BootDisplayFontClutRect;
extern u8 *g_BootDisplayFontImagePixels;
extern u16 *g_BootDisplayFontClutPixels;
/* The credits font, after the entry's tables (0x140 bytes later on the Japanese discs). */
#define g_BootDisplayFontTim (g_CdLoadAreaBase + (PE_JAPAN ? 0x5c6c : 0x5b2c))
extern s32 g_BootDisplayPlaybackCurrentPos;
extern s32 g_BootDisplayPlaybackEndPos;
extern s32 g_BootDisplayPlaybackStartPos;
extern s32 g_BootDisplayPlaybackEnded;
extern PeDiscFile g_BootDisplayPlaybackFile;
void BootDisplay_RecordReadPosition(u8 mode, const u8 *value);
extern u16 g_BootDisplayPlaybackRingIndex;
extern u16 g_BootDisplayPlaybackAltCount;
extern char *g_BootDisplayPlaybackSectorBuffer;
extern s32 g_BootDisplayPlaybackReadCount;
extern char *g_BootDisplayPlaybackFilePath;
extern s32 g_ActiveDrawSlot;

void SetSemiTrans(void *primitive, s32 code);
void ResetGraph(s32 value);
s32 VSync(s32 value);
void Spu_SetVoiceVolume(s32 value);
int BootDisplay_IssuePlaybackRead(int command, int parameter);
int BootDisplay_SetCdFilter(int code);
s32 BootDisplay_WaitForCdAndRestoreCallbacks(void);
int BootDisplay_DetachPlayback(void); /* internal cleanup; caller holds audio lock */
s32 BootDisplay_StartPlayback(void);
void BootDisplay_CheckPlaybackTime(void);
struct PsyzCdRead;
s32 BootDisplay_StepPlaybackTransfer(struct PsyzCdRead *read);
extern s32 g_BootDisplayPlaybackActive;
/* -1 missing image/file, -2 invalid extent/workspace; no state commit on error. */
s32 BootDisplay_InitPlaybackFile(void);
s32 BootDisplay_RunDisplay(void);
s32 BootDisplay_DrawTextBlock(void);
void SetGraphDebug(s32 value);
void SetDispMask(s32 value);
void DecDCTReset(s32 value);
void MdecDiag_ExpandDecoderTable(void *address);
void BootDisplay_PresentFrame(void);
void BootDisplay_SetImageDisplay(u16 index);
void BootDisplay_InitDisplayBuffers(void);
void BootDisplay_StepImageDisplay(void);
void BootDisplay_BrightenGlyph(s16 x,s16 y,u8 character);
void BootDisplay_BrightenImageRegion(u8 intensity);
void BootDisplay_CallInitSystem(void);
extern BootDisplayCallbacks g_BootDisplayImageCallbacks,g_BootDisplaySpriteCallbacks;
void BootDisplay_InitDisplayResources(void);
void BootDisplay_SelectFrameBuffers(void);
void BootDisplay_DisplayNoOp(void);
void BootDisplay_StepImageSprite(void);
void BootDisplay_QueueTintedSprite(s16 x,s16 y,u8 character);
void BootDisplay_InitSprite16(BootDisplaySpritePacket *packet,s16 x,s16 y,u16 u,u16 v);
void BootDisplay_QueueFadeSprite(u8 color);
void BootDisplay_PresentSpriteFrame(void);

/* Callable entry points inside neighboring boot-display code. */
s32 BootDisplay_GetGlyphIndex(u8 character);
u16 BootDisplay_GetGlyphWidth(u8 character);

#endif
