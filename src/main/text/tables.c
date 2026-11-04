#include "common.h"
#include "pe_hooks.h"
#include "pe1/aya.h"
#include "pe1/draw_state.h"
#include "pe1/inventory.h"
#include "pe1/menu_widget.h"
#include "pe1/resident_tables.h"
#include "pe1/text.h"
#include "pe1/save_slot_metadata.h"
#include "pe1/textbox.h"

MenuWidgetSimpleDescriptor g_MenuWidgetSimpleDescriptors[0x41];
MenuWidgetGridDescriptor g_MenuWidgetGridDescriptors[0x41];

MenuWidgetSimpleDescriptor *MenuWidget_LookupSimpleDescriptor(unsigned int index) {
    if (index >= 0x41) {
        return 0;
    }
    return &g_MenuWidgetSimpleDescriptors[index];
}

MenuWidgetGridDescriptor *MenuWidget_LookupGridDescriptor(unsigned int index) {
    if (index >= 0x41) {
        return 0;
    }
    return &g_MenuWidgetGridDescriptors[index];
}

DrawGlyphDescriptor *Draw_LookupGlyphDescriptor(int index) {
    s32 *slot = &g_ResidentDataPack.directory.glyphDescriptors;
    s32 offset = *slot;
    DrawGlyphDescriptor *table = (DrawGlyphDescriptor *)&g_ResidentDataPack.bytes[offset];

    return &table[index];
}

void *Text_LookupTable0(unsigned int index) {
    TextOffsetTable *table;
    s16 offset;

    table = RESIDENT_TABLE(TextOffsetTable, text);
    if (index < table->header.count) {
        offset = table->header.offsets[index];
        return &table->bytes[offset];
    }

    return 0;
}

void *Aya_GetParasiteSpellUnlockTable(void) {
    return RESIDENT_TABLE(ParasiteSpellEntry, parasiteSpells);
}

int Draw_LookupGlyphMetrics(int glyph) {
    return g_ResidentDataPack.bytes[glyph + g_ResidentDataPack.directory.glyphMetrics];
}

/* The text a PE_HOOK_TEXT_LOOKUP handler gave in place of the last lookup's resident entry, or
 * NULL. Handlers own that text and end it with FF; copies that bound resident text to the pack
 * accept it through Text_IsLookupReplacement. */
static const void *s_TextLookupReplacement;

static void *Text_NoteLookup(void *resident, void *text) {
    s_TextLookupReplacement = text != resident ? text : NULL;
    return text;
}

int Text_IsLookupReplacement(const void *text) {
    return text && text == s_TextLookupReplacement;
}

void *Text_LookupTable4(unsigned int index) {
    ResidentDataPack *pack = &g_ResidentDataPack;
    TextTableDirectory *directory;
    TextOffsetTable *table;
    s16 offset;

    directory = (TextTableDirectory *)&pack->bytes[pack->directory.text];
    table = (TextOffsetTable *)&directory->bytes[directory->header.tableOffsets[0]];
    if (index >= table->header.count) {
        return 0;
    }

    offset = table->header.offsets[index];
    void *resident = &table->bytes[offset], *text = resident;
    PE_HOOK(PE_HOOK_TEXT_LOOKUP, (&(PeHookTextLookup){4, index, &text}));
    return Text_NoteLookup(resident, text);
}

/* Saves keep the edited name as bytes. Providers must avoid pack-local glyph IDs here;
 * ordinary inventory lookups still receive the translated display name. */
void *Text_LookupEditableName(unsigned int kind, unsigned int index) {
    if (kind != 4 && kind != 8) return NULL;
    ResidentDataPack *pack = &g_ResidentDataPack;
    TextTableDirectory *directory = (TextTableDirectory *)&pack->bytes[pack->directory.text];
    unsigned slot = kind == 4 ? 0 : 1;
    TextOffsetTable *table = (TextOffsetTable *)&directory->bytes[directory->header.tableOffsets[slot]];
    if (index >= table->header.count) return NULL;
    void *resident = &table->bytes[table->header.offsets[index]], *text = resident;
    PE_HOOK(PE_HOOK_TEXT_LOOKUP, (&(PeHookTextLookup){kind, index, &text, 1}));
    return Text_NoteLookup(resident, text);
}

void *Text_LookupTable8(unsigned int index) {
    ResidentDataPack *pack = &g_ResidentDataPack;
    TextTableDirectory *directory;
    TextOffsetTable *table;
    s16 offset;

    directory = (TextTableDirectory *)&pack->bytes[pack->directory.text];
    table = (TextOffsetTable *)&directory->bytes[directory->header.tableOffsets[1]];
    if (index >= table->header.count) {
        return 0;
    }

    offset = table->header.offsets[index];
    void *resident = &table->bytes[offset], *text = resident;
    PE_HOOK(PE_HOOK_TEXT_LOOKUP, (&(PeHookTextLookup){8, index, &text}));
    return Text_NoteLookup(resident, text);
}

void *Text_LookupTableC(unsigned int index) {
    ResidentDataPack *pack = &g_ResidentDataPack;
    TextTableDirectory *directory;
    TextOffsetTable *table;
    s16 offset;

    directory = (TextTableDirectory *)&pack->bytes[pack->directory.text];
    table = (TextOffsetTable *)&directory->bytes[directory->header.tableOffsets[2]];
    if (index >= table->header.count) {
        return 0;
    }

    offset = table->header.offsets[index];
    void *resident = &table->bytes[offset], *text = resident;
    PE_HOOK(PE_HOOK_TEXT_LOOKUP, (&(PeHookTextLookup){12, index, &text}));
    return Text_NoteLookup(resident, text);
}

void *Text_LookupTable10(unsigned int index) {
    ResidentDataPack *pack = &g_ResidentDataPack;
    TextTableDirectory *directory;
    TextOffsetTable *table;
    s16 offset;

    directory = (TextTableDirectory *)&pack->bytes[pack->directory.text];
    table = (TextOffsetTable *)&directory->bytes[directory->header.tableOffsets[3]];
    if (index >= table->header.count) {
        return 0;
    }

    offset = table->header.offsets[index];
    void *resident = &table->bytes[offset], *text = resident;
    PE_HOOK(PE_HOOK_TEXT_LOOKUP, (&(PeHookTextLookup){16, index, &text}));
    return Text_NoteLookup(resident, text);
}

/* Text-table-3 entry naming the location of map `map`. */
void *Text_LookupMapLocationText(int map) {
    TextTableDirectory *directory;
    TextOffsetTable *table;
    int index;
    s16 offset;

    index = g_ResidentDataPack.bytes[map + g_ResidentDataPack.directory.mapLocationIds];
    if (index == 0) {
        return 0;
    }

    directory = RESIDENT_TABLE(TextTableDirectory, text);
    table = (TextOffsetTable *)&directory->bytes[directory->header.tableOffsets[3]];
    index += 0x7F;
    if ((unsigned int)index >= table->header.count) {
        return 0;
    }

    offset = table->header.offsets[index];
    void *resident = &table->bytes[offset], *text = resident;
    PE_HOOK(PE_HOOK_TEXT_LOOKUP, (&(PeHookTextLookup){16, (unsigned)index, &text}));
    return Text_NoteLookup(resident, text);
}

/* Location name of map `map` (-1 selects name 15); names are NUL-separated. */
u8 *Text_GetMapLocationName(int map) {
    u8 *name;

    name = RESIDENT_TABLE(u8, locationNames);
    if (map >= 0) {
        map = g_ResidentDataPack.bytes[map + g_ResidentDataPack.directory.mapLocationIds];
    } else {
        map = 0xF;
    }

    if (map == 0) {
        return 0;
    }

    do {
        if (*name++ == 0) {
            map--;
        }
    } while (map > 0);

    return name;
}

/* Memory-card icon block copied into each save file header. */
u8 *Text_GetSaveIconData(void) {
    return RESIDENT_TABLE(u8, saveIcon);
}


ResidentDataPack g_ResidentDataPack;

void *Aya_GetStatGrowthTable(int category) {
    return &RESIDENT_TABLE(u8, statGrowth)[category << 9];
}

AyaLevelStats *Aya_LookupLevelStats(int level) {
    if (level < 0) {
        level = 0;
    } else if (level >= 0x63) {
        level = 0x62;
    }

    return &RESIDENT_TABLE(AyaLevelStats, levelStats)[level];
}

void *Aya_GetLevelExpTable(void) {
    return RESIDENT_TABLE(s32, levelExp);
}


ItemDataRecord *Item_LookupBaseData(unsigned int index) {
    s32 *endSlot = &g_ResidentDataPack.directory.statGrowth;
    s32 end = *endSlot;
    s32 begin = g_ResidentDataPack.directory.itemBaseData;

    if (index >= (unsigned int)(end - begin) / sizeof(ItemDataRecord)) {
        return 0;
    }
    return (ItemDataRecord *)&g_ResidentDataPack.bytes[begin] + index;
}

/* Original USA Scan panel glyph streams and per-window line variants. */
u8 g_AnalyzeHpUnknownText[16] = {0x17,0x1f,0x0f,0x2c,0x2c,0x2c,0x2c,0x4c,0x2c,0x2c,0x2c,0x2c,0xff,0x00,0x00,0x00};
u8 g_AnalyzeHpFormatText[12] = {0x17,0x1f,0x0f,0xfb,0x08,0x4c,0xfb,0x08,0xff,0x00,0x00,0x00};
u8 g_TextSusceptibleToPoison[2][22] = {
    {0xfb,0x88,0xfb,0x18,0xfb,0x83,0x82,0xfb,0x17,0x7f,0xfb,0x0a,0x57,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x22,0x44,0x42,0x32,0x34,0x3f,0x43,0x38,0x31,0x3b,0x34,0x0f,0x43,0x3e,0x0f,0x3f,0x3e,0x38,0x42,0x3e,0x3d,0xff},
};
u8 g_TextSusceptibleToAcid[2][20] = {
    {0xfb,0x15,0x7f,0xfb,0x0a,0x57,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x22,0x44,0x42,0x32,0x34,0x3f,0x43,0x38,0x31,0x3b,0x34,0x0f,0x43,0x3e,0x0f,0x30,0x32,0x38,0x33,0xff},
};
u8 g_AnalyzeTranquilizerWeakTexts[2][20] = {
    {0xfb,0x7b,0xfb,0x14,0x65,0xfb,0x18,0x62,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x14,0x30,0x42,0x38,0x3b,0x48,0x0f,0x43,0x41,0x30,0x3d,0x40,0x44,0x38,0x3b,0x38,0x49,0x34,0x33,0xff},
};
u8 g_AnalyzeHeatWeakTexts[2][18] = {
    {0xfb,0x1b,0x7f,0xfb,0x0a,0x57,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x22,0x34,0x3d,0x42,0x38,0x43,0x38,0x45,0x34,0x0f,0x43,0x3e,0x0f,0x37,0x34,0x30,0x43,0xff},
};
u8 g_AnalyzeColdWeakTexts[2][18] = {
    {0xfb,0x16,0xfb,0xc5,0x7f,0xfb,0x0a,0x57,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x22,0x34,0x3d,0x42,0x38,0x43,0x38,0x45,0x34,0x0f,0x43,0x3e,0x0f,0x32,0x3e,0x3b,0x33,0xff},
};
u8 g_AnalyzeStealWeakTexts[2][22] = {
    {0xe1,0xd2,0xa4,0xfb,0xbe,0x9e,0xa1,0x97,0x6c,0x57,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x1e,0x31,0x39,0x34,0x32,0x43,0x42,0x0f,0x34,0x30,0x42,0x38,0x3b,0x48,0x0f,0x42,0x43,0x3e,0x3b,0x34,0x3d,0xff},
};
u8 g_AnalyzeConfusionWeakTexts[2][21] = {
    {0xfb,0x0e,0xfb,0x0c,0x6b,0x97,0x6c,0x57,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x16,0x34,0x43,0x42,0x0f,0x32,0x3e,0x3d,0x35,0x44,0x42,0x34,0x33,0x0f,0x34,0x30,0x42,0x38,0x3b,0x48,0xff},
};

/* Original shared font/background VRAM slots and USA base-glyph spacing. */
FieldFontTextureState g_FieldFontTextureState = {.font = {{
    {0x0,0x140,0x0,0x140,0xfc,0x0,0x0},
    {0x0,0x180,0x0,0x150,0xfc,0x0,0x0},
    {0x0,0x110,0x1d4,0x130,0x1de,0x0,0x0},
    {0x0,0x11c,0x1d4,0x130,0x1dd,0x0,0x0}
}, 0}};
TextboxGlyphSpacing g_TextboxGlyphSpacing[78] = {
    {0,7},{0,7},{0,7},{0,7},{0,7},{0,7},{0,7},{0,7},{0,7},{0,7},{0,7},{0,6},{0,6},
    {0,8},{0,11},{0,14},{0,9},{0,8},{0,7},{0,8},{0,7},{0,7},{0,8},{0,9},{0,6},{0,7},
    {0,9},{0,8},{0,11},{0,9},{0,7},{0,8},{0,7},{0,9},{0,7},{0,7},{0,9},{0,9},{0,11},
    {0,7},{0,9},{0,7},{0,9},{0,4},{0,7},{0,6},{0,4},{0,4},{0,8},{0,8},{0,7},{0,7},
    {0,7},{0,7},{0,8},{0,9},{0,5},{0,4},{0,8},{0,5},{0,11},{0,9},{0,7},{0,8},{0,8},
    {0,7},{0,6},{0,5},{0,9},{0,9},{0,11},{0,7},{0,9},{0,6},{0,4},{0,4},{0,7},{0,0},
};
const TextboxGlyphSpacing *g_TextboxGlyphWidths = g_TextboxGlyphSpacing;
unsigned g_TextboxGlyphCount = sizeof(g_TextboxGlyphSpacing) / sizeof(*g_TextboxGlyphSpacing);

/* Original USA per-window escape failure and prohibition messages. */
u8 g_EscapeFailedTexts[34] = {0xfb,0x19,0x68,0xa0,0x82,0x7f,0xfb,0x0b,0xfb,0x11,0x6b,0x74,0xff,0x00,0x00,0x00,0x00,0x15,0x30,0x38,0x3b,0x34,0x33,0x0f,0x43,0x3e,0x0f,0x34,0x42,0x32,0x30,0x3f,0x34,0xff};
u8 g_EscapeBlockedTexts[28] = {0x0f,0xfb,0x19,0x68,0x9e,0xa1,0x7e,0x57,0x2b,0xff,0x00,0x00,0x00,0x00,0x12,0x30,0x3d,0x2e,0x43,0x0f,0x34,0x42,0x32,0x30,0x3f,0x34,0x2b,0xff};
