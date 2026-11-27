#ifndef PE1_SAVE_H
#define PE1_SAVE_H

#include "common.h"
#include "pe1/inventory.h"
#include "pe1/save_blob.h"

PE1_STATIC_ASSERT(sizeof(SaveMetadataWindow) == 0x10,
                  save_metadata_window_size);

PE1_STATIC_ASSERT(sizeof(SaveBytes12E4) == 0x12E4, save_runtime_blob_size);
PE1_STATIC_ASSERT(sizeof(SaveRuntimeState) == 0x12E4, save_runtime_state_size);
PE1_STATIC_ASSERT(PE1_OFFSETOF(SaveRuntimeState, windows.metadata[1]) == 0x10,
                  save_second_metadata_window_offset);
PE1_STATIC_ASSERT(PE1_OFFSETOF(SaveRuntimeState, drawBlendColor) == 0x64,
                  save_draw_blend_color_offset);
PE1_STATIC_ASSERT(PE1_OFFSETOF(SaveRuntimeState, categoryCounts) == 0x10CC,
                  save_category_counts_offset);
PE1_STATIC_ASSERT(PE1_OFFSETOF(SaveRuntimeState, cursorMetadata) == 0x12C4,
                  save_cursor_metadata_offset);
PE1_STATIC_ASSERT(PE1_OFFSETOF(SaveRuntimeState, player) == 0x20,
                  save_player_state_offset);
PE1_STATIC_ASSERT(PE1_OFFSETOF(SaveRuntimeState, player.runtime.inventory.equipment) == 0xCC,
                  save_equipment_records_offset);
void Save_InitMetadataState(void);
void Save_ResetMetadataWindow(void);
/* Slot cell picked on the save screen and the deferred read/write of it. */
extern int g_SaveSlotCellIndex;
void Save_StartSelectedWriteSlot(void);
void Save_StartSelectedReadSlot(void);

/* Save-slot metadata window selected by the current frontend operation. */
extern int g_SaveMetadataWindowIndex;
extern u8 *g_SaveMetadataCursor;
extern int g_SaveMetadataWindowLength;
extern ItemDataRecord *g_SaveMetadataSourceRecord;
extern ItemDataRecord *g_MenuRenameTargetRecord;

int Save_GetMetadataWindowIndex(void);
void Save_AppendMetadataChar(unsigned char ch);
int Save_DeleteMetadataChar(void);
char *Save_GetActiveMetadataBuffer(void);
u8 *Save_GetMetadataCursor(void);
u32 Save_GetMetadataRemainingChars(void);
/* A name entry grid letter of one or two bytes (pe1/text_render.h). */
void Save_AppendMetadataLetter(const unsigned char *letter, unsigned bytes);
void Save_SelectMetadataWindow(ItemDataRecord *record);
void Save_LoadMetadataWindowText(void);

/* Save-notification state and screen-layout selector. These globals are
 * shared with older frontend/battle callers under their retail names. */
extern u8 g_BattleSaveOverlayActive;
extern unsigned char g_MenuActiveMode;
extern unsigned char g_SaveOverlayTimer;
extern u8 *g_CurItemEffectData;
void Menu_SaveOverlayDraw(void);

/* The save-title formatter consumes four numeric fields and two variant
 * arguments. Numeric fields remain 32-bit; variants hold native text pointers. */
typedef union SaveSjisFormatArgument {
    int number;
    unsigned char *text;
} SaveSjisFormatArgument;

typedef struct SaveSjisFormatArguments {
    int chapter;
    int hours;
    int minutes;
    int seconds;
    SaveSjisFormatArgument primary;
    SaveSjisFormatArgument secondary;
} SaveSjisFormatArguments;

PE1_STATIC_ASSERT(sizeof(SaveSjisFormatArgument) == sizeof(void *),
                  save_sjis_format_argument_size);
PE1_STATIC_ASSERT(sizeof(SaveSjisFormatArguments) == 0x10 + 2 * sizeof(SaveSjisFormatArgument),
                  save_sjis_format_arguments_size);
PE1_STATIC_ASSERT(PE1_OFFSETOF(SaveSjisFormatArguments, primary) == 0x10,
                  save_sjis_primary_argument_offset);
PE1_STATIC_ASSERT(PE1_OFFSETOF(SaveSjisFormatArguments, secondary) == 0x10 + sizeof(SaveSjisFormatArgument),
                  save_sjis_secondary_argument_offset);

extern SaveSjisFormatArguments g_SaveSjisFormatArguments;
void Save_SprintfSjis(unsigned char *dst, unsigned char *fmt);

/* Save title and card file assembly (memcard/save_flow.c). */
extern u8 g_SaveTitleBuffer[];
extern u8 *g_SaveTitleFormatLongPtr;
extern u8 *g_SaveTitleFormatShortPtr;
extern int g_SaveTitleStyleFlag;
/* Write/read cursor of the card file image being built or parsed. */
extern u8 *g_SaveIoCursor;

int Save_ClassifyPlaytime(int frames);
int Save_GetCurrentMapNumber(void);
u8 *Save_FormatTitle(int chapter, int playtime_seconds);
void Save_BuildHeader(void);
void Save_RestoreHeader(void);
/* 0: copied; -1: unsupported record links, no tail/runtime mutation. */
int Save_SerializeTail(void);
/* The same bytes without the save write hook: no mod sees it. */
int Save_SerializeTailData(void);
int Save_DeserializeTail(void);

#endif /* PE1_SAVE_H */
