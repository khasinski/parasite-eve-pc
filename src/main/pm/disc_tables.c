#include "common.h"
#include "pe1/entity_allocation.h"
#include "pe1/akao/commands.h"
#include "pe1/asset_tim.h"
#include "pe1/battle_status.h"
#include "pe1/boot.h"
#include "pe1/boot_disc_check.h"
#include "pe1/boot_display.h"
#include "pe1/boot_services.h"
#include "pe1/cdrom.h"
#include "pe1/cdrom_buffers.h"
#include "pe1/game_state.h"
#include "pe1/draw_buffers.h"
#include "pe1/draw_state.h"
#include "pe1/font.h"
#include "pe1/game_state.h"
#include "pe1/geom_state.h"
#include "pe1/menu_queue.h"
#include "pe1/pe_image.h"
#include "pe1/psyq_bios.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_buffers.h"
#include "pe1/render_animation_frame.h"
#include "pe1/render_camera.h"
#include "pe1/render_tint.h"
#include "pe1/save_tail_state.h"
#include "pe1/scene_assets.h"
#include "pe1/scene_dispatch.h"
#include "pe1/script_queue.h"


int Draw_InitDisplay(int mode) {
    int mode_reg;
    int one;
    u8 **slots;
    u8 *ptr;
    u8 *ptr2;
    int const_8000;
    int const_48000;
    RECT rect;
    u8 *tmp;

    mode_reg = mode;
    VSync(0);
    SetDispMask(0);
    PutDispEnv(g_RenderDispEnvArray);

    rect.x = 0;
    rect.y = 0;
    rect.w = 0x140;
    rect.h = 0x1C0;
    one = 1;
    ClearImage(&rect, 0, 0, one);
    DrawSync(0);

    const_48000 = 16 * ENTITY_ALLOCATION_BLOCK_SIZE;
    slots = g_OtBufferTable;

    ptr = g_BootWorkArea;
    ptr2 = ptr + 0x1800;
    g_GameState.bank_asset_source = ptr;
    ptr += 0x6000;
    g_GameState.voice_bank_base = ptr2;
    g_GameState.voice_bank_base_1400 = ptr;
    ptr += 0xE000;
    g_GameState.entity_texture_blob = (union SceneAssetView *)ptr;

    ptr = D_8010BD00;
    ptr2 = D_80120D08;
    g_GameState.texture_load_scratch = (union SceneAssetView *)ptr;
    ptr = ptr2 + BOOT_SCENE_OBJECT_WORK_BYTES;
    g_GameState.scene_object_work = ptr2;
    /* Each of the two ordering tables holds 4096 native PsyZ tags. */
    ptr2 = ptr + 0x1000 * sizeof(RenderNativeTag);
    const_8000 = 0x8000;
    g_OtBufferTable[0] = ptr;
    ptr += 2 * 0x1000 * sizeof(RenderNativeTag);
    g_RenderOtBufferBaseAlt[0] = ptr2;
    ptr2 = ptr + TEXTBOX_GLYPH_PACKET_CAPACITY * sizeof(TextboxGlyphPacket);
    g_GameState.object_texture_blob = (struct SceneAssetBlob *)ptr;
    ptr += 2 * TEXTBOX_GLYPH_PACKET_CAPACITY * sizeof(TextboxGlyphPacket);
    g_RenderScratchBufferBase[0] = ptr;
    ptr += const_48000;
    g_GameState.unk_170 = ptr2;
    ptr2 = ptr + 0x4000;
    g_RenderFrontBufferBase[0] = ptr;
    ptr += const_8000;
    g_RenderBackBufferBase[0] = ptr2;
    ptr2 = ptr + RENDER_ANIM_BUFFER_BYTES;
    g_GameState.script_sprite_buffer = ptr2;
    tmp = g_StrFileDirBuffer;
    g_RenderAnimBufferBase[0] = ptr;
    ptr += 2 * RENDER_ANIM_BUFFER_BYTES;
    g_GameState.scene_process_slots = (struct PmSlotBanks *)ptr;
    g_SceneLoadScratchBuffer[0] = g_SceneLoadScratchArea;
    g_LoadedSceneAssetBlock[0] = (struct SceneAssetBlob *)(tmp - 8);
    g_GameState.str_file_directory = tmp;

    Draw_InitBuffers();
    CdRom_SetSeekPos(2);

    do {
        ClearOTagR((RenderNativeTag *)slots[g_ActiveDrawSlot], 0x1000);
        Render_SetCDDCSlot();
        Draw_RenderFrame();
    } while ((g_ScreenTint.fadeMode & 3) != one);

    g_SceneInitSubphase = 0;
    Menu_ConsumeEquipSlotFlag();

    if (mode_reg == 1) {
        g_SceneDispatchToken = 0xA80830C8;
    } else if (mode_reg == 3) {
        g_SceneDispatchToken = 0xA80651C8;
    }
    return 0;
}

int Draw_GetDrawEnabled(void) {
    return g_DrawEnabled.value;
}

short Seq_GetElapsed(void) {
    if (g_DrawEnabled.bits != 0) {
        return g_SeqElapsed;
    }
    return -1;
}

/* Both queries are integrated here; the inlined calls leave retail's
 * otherwise empty eight-byte frame and the repeated enabled test. */
int Draw_CheckDrawStatus(void) {
    if (Draw_GetDrawEnabled()) {
        if (Seq_GetElapsed() > 0) {
            if (g_GameState.scene_audio.tracks.movie_wide != 0) {
                return 2;
            }
            return 1;
        }
    }
    return 0;
}

TimFile *Draw_GetTimTableEntry(TimRelativeTable *table, int index) {
    return (TimFile *)&table->bytes[table->offsets[(s16)index]];
}

void Draw_LoadTimTable(TimRelativeTable *table, int count) {
    int i;

    for (i = 0; i < count; i++) {
        u32 offset = table->offsets[(s16)i];
        Asset_LoadTimImage((TimFile *)&table->bytes[offset]);
    }
}

/* Boot-time disc loads called from boot/main.c: the PE.IMG asset LBA
 * tables and the boot audio banks. */

void SetDispMask(int arg0);

int Boot_LoadOverlayTables(void) {
    int status;
    u32 imageBase = g_PeImageBaseLba;
    Pe1GameState *state = &g_GameState;
    u16 *range;
    TimRelativeTable *main_table;
    TimRelativeTable *large_table;
    int i;
    u16 *ranges[] = {g_MainAssetLbaTbl,
        (g_ScriptVariableBank[SCRIPT_VAR_FLAGS] & 0x2000) ? g_LargeTexLbaTbl : g_SmallTexLbaTbl,
        g_FontUiLbaTbl};

    if (!state->scene_object_work || !state->object_texture_blob ||
        !state->scene_load_scratch || !g_StrFileDirBuffer) return -8;
    for (i = 0; i < 3; ++i) {
        range = ranges[i];
        if (range[1] <= range[0] || range[0] > UINT32_MAX - imageBase ||
            (u32)(range[1] - range[0]) * 2048 > 0x80000) return -8;
    }
    VSync(0);
    SetDispMask(0);
    if (CdRom_ReadAssetSectors(1, 0xCC, 0, state->scene_load_scratch, 0x21, 1) < 0) return -1;

    range = ranges[0];
    if (CdRom_ReadSectors(imageBase + range[0], 0, state->scene_object_work,
                         range[1] - range[0]) < 0) return -2;
    while ((status = CdRom_PollReady()) > 0) {}
    if (status < 0) return -3;
    main_table = (TimRelativeTable *)state->scene_object_work;
    for (i = 0; i < 3; ++i)
        Asset_LoadTimImage(SceneAsset_ResolveOffset(main_table, ((u32 *)main_table)[i]));

    range = ranges[1];
    if (CdRom_ReadSectors(imageBase + range[0], 0, state->object_texture_blob,
                         range[1] - range[0]) < 0) return -4;
    while ((status = CdRom_PollReady()) > 0) {}
    if (status < 0) return -5;
    large_table = (TimRelativeTable *)state->object_texture_blob;
    for (i = 0; i < 0x106; ++i)
        Asset_LoadTimImage(SceneAsset_ResolveOffset(large_table, ((u32 *)large_table)[i]));
    DrawSync(0);

    range = ranges[2];
    if (CdRom_ReadSectors(imageBase + range[0], 0, g_StrFileDirBuffer,
                         range[1] - range[0]) < 0) return -6;
    while ((status = CdRom_PollReady()) > 0) {}
    if (status < 0) return -7;
    /* The final bank remains data. Native overlay functions are linked C;
     * host code is never installed from the disc or instruction-cache flushed. */
    return 0;
}

int CdRom_LoadBootAudio(void) {
    int status;
    const u16 *range = &g_BootDisplayDataLbaTbl;
    const u8 *lines[745];void *images[31];
    size_t bytes=(size_t)(range[1]-range[0])*2048;

    memset(g_BootDisplayCreditLines,0,sizeof(g_BootDisplayCreditLines));
    memset(g_BootDisplayCreditImages,0,sizeof(g_BootDisplayCreditImages));
    g_BootDisplayPlaybackFilePath=NULL;

    if (!g_CdLoadAreaBase || range[1] <= range[0] ||
        (u32)(range[1] - range[0]) * 2048 > BOOT_DISPLAY_WORK_SIZE ||
        g_GameState.pe_image_base_lba > UINT32_MAX - range[0]) return -3;

    g_GameState.scene_audio.tracks.keys[0][1] = -1;
    g_GameState.scene_audio.tracks.keys[0][0] = -1;
    g_GameState.scene_audio.tracks.keys[1][1] = -1;
    g_GameState.scene_audio.tracks.keys[1][0] = -1;
    g_GameState.scene_audio.tracks.banks[1] = -1;
    g_GameState.scene_audio.tracks.banks[0] = -1;
    g_GameState.flags &= -0xF1;
    Akao_Cmd_F0();

    /* The former first range installed MIPS decoder code. Its complete C
     * module is linked now; only the following display-data range is read. */
    status = CdRom_ReadSectors(g_GameState.pe_image_base_lba + range[0], 0,
                              g_CdLoadAreaBase, range[1] - range[0]);
    if (status < 0) return -1;
    while ((status = CdRom_PollReady()) > 0) {}
    if (status < 0) return -2;
    /* The credits contain absolute data addresses, not host pointers. Bind
     * the complete authored tables after checking every string/image entry.
     * The Japanese entry is linked 0x540 lower and keeps its tables 0x140 later. */
    typedef struct { u32 base, lines, first_line, line_end, text_end, images, first_image; } CreditsLayout;
    static const CreditsLayout kLayouts[2] = {
        [PE_REGION_USA] = {0x80120d00,0x4f88,0x80120d1c,0x80122fa4,0x22a4,0x4ea4,0x8012e86c},
        [PE_REGION_JAPAN] = {0x801207c0,0x50c8,0x801207dc,0x80122ba4,0x23e4,0x4fe4,0x8012e46c},
    };
    const CreditsLayout layout = kLayouts[PE_JAPAN ? PE_REGION_JAPAN : PE_REGION_USA];
    if(bytes<(u32)(g_BootDisplayFontTim-g_CdLoadAreaBase)+8 || !memchr(g_CdLoadAreaBase+4,0,24))return -4;
    u32 magic;memcpy(&magic,g_BootDisplayFontTim,4);if(magic!=16)return -4;
    for(unsigned i=0;i<745;++i){
        u32 address;memcpy(&address,g_CdLoadAreaBase+layout.lines+i*4,4);
        if(address<layout.first_line || address>=layout.line_end)return -4;
        size_t offset=address-layout.base;
        const u8 *line=g_CdLoadAreaBase+offset;
        if(i==744){if(*line!=255)return -4;}
        else if(*line==255 || !memchr(line,0,layout.text_end-offset))return -4;
        lines[i]=line;
    }
    u32 previous=0;
    for(unsigned i=0;i<31;++i){
        u32 address;memcpy(&address,g_CdLoadAreaBase+layout.images+i*4,4);
        if(address<layout.first_image || address<previous || (u64)address-layout.base+8>bytes)return -4;
        images[i]=g_CdLoadAreaBase+(address-layout.base);previous=address;
    }
    memcpy(g_BootDisplayCreditLines,lines,sizeof(lines));
    memcpy(g_BootDisplayCreditImages,images,sizeof(images));
    g_BootDisplayPlaybackFilePath=(char *)g_CdLoadAreaBase+4;
    return 0;
}
