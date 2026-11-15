#include "common.h"
#include "pe1/menu_widget.h"
#include <string.h>
#include "pe1/akao.h"
#include "pe1/asset_find.h"
#include "pe1/asset_tim.h"
#include "pe1/boot_disc_check.h"
#include "pe1/boot_services.h"
#include "pe1/cdrom.h"
#include "pe1/cdrom_buffers.h"
#include "pe1/draw_buffers.h"
#include "pe1/font.h"
#include "pe1/game_state.h"
#include "pe1/entity_animation.h"
#include "pe1/entity_allocation.h"
#include "pe1/player_entity.h"
#include "pe1/pe_image.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_animation_frame.h"
#include "pe1/render_buffers.h"
#include "pe1/render_animation_frame.h"
#include "pe1/scene_assets.h"
#include "pe1/scene_room.h"
#include "pe1/script_queue.h"

/* Original USA archive/marker paths, now resolved directly in file images. */
char g_PeDisc1MarkerPath[] = "\\FMV1\\PEDISC01.IDF;1";
char g_PeImagePath[] = "\\PE.IMG;1";
char g_PeDisc2MarkerPath[] = "\\FMV2\\PEDISC02.IDF;1";
PeDisc *g_CdImageVolumes[2];
int (*g_CdImageRequest)(unsigned identity);

/* USA resident records at 0x80094488; boot clears IDs, retaining the
 * authored action/frame pair for the four player footstep triggers. */
AkaoVoiceBankEntry g_AkaoVoiceBankEntries[20] = {
    {0,0,0x16,0,{1000,1000}}, {0,0,0x16,0x13,{1001,1001}},
    {0,0,0x17,0,{1000,1000}}, {0,0,0x17,9,{1001,1001}}
};

extern const char *g_MemCardFilenameFormatPtr;
extern const char g_MemCardFilenameFormatUsa[], g_MemCardFilenameFormatJapan[];

/* Where each release keeps the five tables read below: rooms, PE.IMG sectors, the audio
 * directory and the two menu widget sets. The Japanese executable is the same program with
 * these 0x538 bytes earlier. */
typedef struct { u32 rooms, sectors, audio, simple, grid; } PeExecutableTables;
static const PeExecutableTables kExecutableTables[2] = {
    [PE_REGION_USA] = {0x80093378u, 0x800930d8u, 0x8009317cu, 0x80092478u, 0x80092888u},
    [PE_REGION_JAPAN] = {0x80092e40u, 0x80092ba0u, 0x80092c44u, 0x80091f40u, 0x80092350u},
};

/* SLPS_012.30 / SLPS_012.31 are the Japanese discs; everything else is read as USA. */
static unsigned Asset_ExecutableRegion(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p; ++p) if (*p == '\\' || *p == '/') name = p + 1;
    char upper[16];
    size_t n = 0;
    for (; name[n] && n + 1 < sizeof(upper); ++n)
        upper[n] = (char)(name[n] >= 'a' && name[n] <= 'z' ? name[n] - 32 : name[n]);
    upper[n] = 0;
    return !strcmp(upper, "SLPS_012.30") || !strcmp(upper, "SLPS_012.31") ? PE_REGION_JAPAN : PE_REGION_USA;
}

static int Asset_ReadPeImage(PeDisc *disc, unsigned *identity, unsigned *region_out, int mount)
{
    PeDiscFile archive, marker;
    unsigned flags = 0;
    if (!disc || !disc->stream) return -1;
    if (pe_disc_find(disc, g_PeDisc1MarkerPath, &marker) && !marker.directory)
        flags |= 1;
    if (pe_disc_find(disc, g_PeDisc2MarkerPath, &marker) && !marker.directory)
        flags |= 2;
    if (!flags || !pe_disc_find(disc, g_PeImagePath, &archive) || archive.directory)
        return -2;
    /* The executable contributes resident data, never executable host code. */
    PeDiscFile config_file, executable;
    char config[1025], executable_path[128];
    if (!pe_disc_find(disc, "SYSTEM.CNF", &config_file) || config_file.directory ||
        !config_file.size || config_file.size >= sizeof(config) ||
        !pe_disc_read(disc, &config_file, 0, config, config_file.size)) return -3;
    config[config_file.size] = 0;
    char *boot = strstr(config, "cdrom:");
    if (!boot) boot = strstr(config, "CDROM:");
    if (!boot) return -3;
    boot += 6;
    size_t path_length = 0;
    while (boot[path_length] && boot[path_length] != ';' && boot[path_length] != '\r' &&
           boot[path_length] != '\n' && boot[path_length] != ' ' && boot[path_length] != '\t') {
        if (path_length + 1 >= sizeof(executable_path)) return -3;
        executable_path[path_length] = boot[path_length];
        ++path_length;
    }
    executable_path[path_length] = 0;
    u8 header[32], room_data[SCENE_ROOM_COUNT * 8], sector_data[162], audio_data[506];
    SceneRoomSectorRange ranges[SCENE_ROOM_COUNT];u16 sectors[81];
    MenuWidgetSimpleDescriptor simple[0x41];
    MenuWidgetGridDescriptor grid[0x41];
    if (!path_length || !pe_disc_find(disc, executable_path, &executable) || executable.directory ||
        !pe_disc_read(disc, &executable, 0, header, sizeof(header)) || memcmp(header,"PS-X EXE",8)) return -3;
    u32 base=(u32)header[24]|(u32)header[25]<<8|(u32)header[26]<<16|(u32)header[27]<<24;
    u32 bytes=(u32)header[28]|(u32)header[29]<<8|(u32)header[30]<<16|(u32)header[31]<<24;
    const unsigned region = Asset_ExecutableRegion(executable_path);
    const PeExecutableTables *at = &kExecutableTables[region];
    if (base>at->simple || bytes<sizeof(room_data) || at->rooms-base>bytes-sizeof(room_data) ||
        (u64)bytes+0x800>executable.size ||
        !pe_disc_read(disc,&executable,0x800ull+at->rooms-base,room_data,sizeof(room_data)) ||
        !pe_disc_read(disc,&executable,0x800ull+at->sectors-base,sector_data,sizeof(sector_data)) ||
        !pe_disc_read(disc,&executable,0x800ull+at->audio-base,audio_data,sizeof(audio_data)) ||
        !pe_disc_read(disc,&executable,0x800ull+at->simple-base,simple,sizeof(simple)) ||
        !pe_disc_read(disc,&executable,0x800ull+at->grid-base,grid,sizeof(grid))) return -3;
    for(unsigned i=0;i<SCENE_ROOM_COUNT;++i) {
        const u8 *p=room_data+i*8;
        ranges[i].start=(u32)p[0]|(u32)p[1]<<8|(u32)p[2]<<16|(u32)p[3]<<24;
        u32 counts=(u32)p[4]|(u32)p[5]<<8|(u32)p[6]<<16|(u32)p[7]<<24;
        ranges[i].scratchSectors=counts&255;ranges[i].textureSectors=(counts>>8)&4095;ranges[i].roomSectors=counts>>20;
        if (((u64)ranges[i].start+ranges[i].scratchSectors+ranges[i].textureSectors+ranges[i].roomSectors)*2048>archive.size) return -3;
    }
    for(unsigned i=0;i<81;++i) {
        sectors[i]=(u16)sector_data[i*2]|(u16)sector_data[i*2+1]<<8;
        if ((u64)sectors[i]*2048>archive.size) return -3;
    }
    u32 audio_base=(u32)audio_data[0]|(u32)audio_data[1]<<8|(u32)audio_data[2]<<16|(u32)audio_data[3]<<24;
    u16 previous=0;
    for(unsigned i=0;i<=250;++i) {
        u16 boundary=(u16)audio_data[4+i*2]|(u16)audio_data[5+i*2]<<8;
        if (boundary<previous || ((u64)audio_base+boundary)*2048>archive.size) return -3;
        previous=boundary;
    }
    if(identity)*identity=flags;
    if(region_out)*region_out=region;
    if(!mount)return 0;
    g_GameRegion = (u8)region;
    g_MemCardFilenameFormatPtr = region == PE_REGION_JAPAN ? g_MemCardFilenameFormatJapan : g_MemCardFilenameFormatUsa;
    memcpy(g_RoomSectorTable,ranges,sizeof(ranges));memcpy(g_PeImageSectorTable,sectors,sizeof(sectors));
    memcpy(g_SceneSectorDirectory,audio_data,sizeof(audio_data));
    memcpy(g_MenuWidgetSimpleDescriptors,simple,sizeof(simple));
    memcpy(g_MenuWidgetGridDescriptors,grid,sizeof(grid));
    CdRom_BindImage(disc, &archive);
    g_PeImageBaseLba = archive.sector;
    g_PeImageMountFlags = (u8)flags;
    disc->error[0] = 0;
    return 0;
}

int Asset_ValidatePeImage(PeDisc *disc, unsigned *identity)
{
    if(identity)*identity=0;
    return Asset_ReadPeImage(disc,identity,NULL,0);
}
int Asset_ValidatePeImageRegion(PeDisc *disc, unsigned *identity, unsigned *region)
{
    if(identity)*identity=0;
    if(region)*region=PE_REGION_USA;
    return Asset_ReadPeImage(disc,identity,region,0);
}
int Asset_OpenPeImage(void)
{
    g_PeImageMountFlags=0;
    return Asset_ReadPeImage(g_CdImage,NULL,NULL,1);
}

/* File images need no tray notice or physical-disc command queue. */
int Render_InitDisplayLists(int mode)
{
    if (mode != 1 && mode != 2) return -4;
    if(g_CdImageVolumes[0] || g_CdImageVolumes[1]){
        if(!g_CdImageVolumes[mode-1] &&
           (!g_CdImageRequest || !g_CdImageRequest((unsigned)mode) || !g_CdImageVolumes[mode-1]))return -4;
        if(g_CdImage!=g_CdImageVolumes[mode-1])CdRom_BindImage(g_CdImageVolumes[mode-1],NULL);
    }
    int status = Asset_OpenPeImage();
    if (status < 0) return status;
    if (!(g_PeImageMountFlags & mode)) return -4;
    RECT rect = {0, 0, 320, 448};
    VSync(0);
    SetDispMask(0);
    ClearImage(&rect, 0, 0, 1);
    DrawSync(0);
    return 0;
}

/* End-of-frame overlay pass (draw the game-state primitives into the
 * current draw buffer with background clear off and send the queued AKAO
 * commands), soft-reset shutdown and screen vertical-offset update. These
 * contiguous routines share the default compiler profile and display state. */

void Boot_FinishFrame(void) {
    s32 one;

    if (!(g_GameState.flags & 0x200) && (g_GameStateFlags & 0x10)) {
        one = 1;
        g_RenderDrawEnvArray[g_ActiveDrawSlot].isbg = 0;
        g_RenderDrawEnvArray[g_ActiveDrawSlot].dfe = one;
        PutDrawEnv(&g_RenderDrawEnvArray[g_ActiveDrawSlot]);
        DrawPrim(&g_GameState.fade_tpage);
        DrawPrim(&g_GameState.fade_tile);
        g_RenderDrawEnvArray[g_ActiveDrawSlot].isbg = one;
        g_RenderDrawEnvArray[g_ActiveDrawSlot].dfe = 0;
        Akao_Cmd_99_9B_9D(0);
    }
    /* The Japanese release sends the second fade command only in a frame without the first one. */
    if ((g_GameStateFlags & 0x20) &&
        (!PE_JAPAN || (!(g_GameState.flags & 0x200) && !(g_GameStateFlags & 0x10)))) {
        Akao_Cmd_98_9A_9C(0);
    }
}

void Boot_Shutdown(void) {
    int old;

    Akao_Cmd_D8(0);
    Akao_Cmd_F0();
    Akao_Cmd_F1();
    Spu_Shutdown();

    if (Menu_IsEquipSlotActive() & 0xFF) {
        Menu_ResetEquipSlotState();
    }

    old = g_SceneDispatchToken;
    g_SceneDispatchToken = 0xA9400048;
    g_ScriptVariableBank[SCRIPT_VAR_ROOM] = old;
    g_GameState.flags |= 0x100;
}

/* Shared draw-environment vertical offset (0..15). */

int Draw_SetBaseY(int unused, u32 y)
{
  u32 v;
  if (y < 16)
  {
    v = y;
    g_DrawBaseY = (g_RenderDispEnvArray[0].screen.y = (g_RenderDispEnvArray[1].screen.y = v));
  }
  return 0;
}



void Boot_SyncShutdown(void)
{
    Spu_InitHardware();
    Akao_Cmd_F0();
    Akao_Cmd_F1();
    Akao_Cmd_98_9A_9C(0);


}

/* Lay out the memory buffers, then reset the scene voice-bank state. */
void Boot_InitGameState(void)
{
    Boot_InitMemoryLayout();
    Asset_ResetGameState();
}

void Asset_ResetGameState(void)
{
    int j;
    Pe1GameState *base = &g_GameState;
    int minusOne;
    AkaoVoiceBankEntry *pair;
    int pairOffset;
    u8 flags;
    int fill;
    u8 *voiceBase;
    u32 drawMode;
    base->flags = 3;
    base->saved_state_004 = 10;
    base->field_bg_cache_id = -1;
    base->room_type = 2;
    (minusOne = -1);
    base->requested_entity_bank = 11;
    base->field_bg_cache_bank = minusOne;
    base->loaded_entity_bank = 0;
    base->current_story_day = minusOne;
    base->pending_story_day = 1;
    base->story_day_flags = 0;
    base->unk_00f = 0;
    base->cd_range_read_mode = 0;
    base->bank_state_11 = 0;
    base->bank_state_12 = 0;
    base->scene_area_type_backup = 0;
    /* Retail clears the complete render object and its eight-byte tail.
     * Native pointers enlarge the object; PS1 word offsets cannot address it. */
    memset(&base->scene_object, 0, sizeof(base->scene_object));
    memset(base->reserved_scene_object_tail, 0, sizeof(base->reserved_scene_object_tail));
    base->scene_audio.tracks.reserved[1] = 8;
    base->scene_audio.tracks.reserved[0] = 0;
    base->scene_audio.tracks.banks[1] = -1;
    base->scene_audio.tracks.banks[0] = -1;
    j = 0;
    do {
        base->scene_audio.tracks.keys[j][1] = -1;
        base->scene_audio.tracks.keys[j][0] = -1;
        j++;
    } while (j < 2);
    pair = g_AkaoVoiceBankEntries;
    pairOffset = 0;
    base->track_volumes[1] = 0x7F;
    base->track_volumes[0] = 0x7F;
    base->scene_audio.tracks.pending_key = 0x27;
    base->scene_audio.tracks.pending_bank = 0x0D;
    base->scene_audio.tracks.movie_wide = 1;
    base->scene_audio.tracks.movie_active = 0;
    base->scene_audio.tracks.movie_frame = -1;
    base->scene_audio.tracks.movie_volume = 0x98;
    base->scene_audio.tracks.movie_index = -1;
    base->pending_sample_bank = -1;
    base->pending_stream_banks[1] = 0;
    base->pending_stream_banks[0] = 0;
    for (; pairOffset < 4; pairOffset++, pair++) {
        pair->ids[1] = 0;
        g_AkaoVoiceBankEntries[pairOffset].ids[0] = 0;
    }
    drawMode = 0xE1000440;
    j = 2;
    base->bank_value_f6 = 0x30;
    base->bank_value_f7 = 0x7F;
    base->bank_value_f8 = 0x100;
    base->bank_value_fa = 0x800;
    base->fade_tile.tag.len = 3;
    base->fade_tile.code = 0x60;
    base->fade_tile.w = 0x140;
    base->fade_tile.h = 0xE0;
    base->fade_tpage.tag.len = 1;
    flags = base->fade_tile.code;
    fill = 0x20;
    base->scene_init_phase = fill;
    base->fade_tile.r = fill;
    base->fade_tile.g = fill;
    base->fade_tile.b = fill;
    voiceBase = base->voice_bank_base;
    base->entity_texture_phase = 0;
    base->scene_init_subphase = 0;
    base->tim_load_state = 0;
    base->cd_read_phase = 0;
    base->cd_track_phase = 0;
    base->memcard_init_phase = 0;
    base->cd_transition_phase = 0;
    base->memcard_init_phase = 0;
    base->fade_tile.x = 0;
    base->fade_tile.y = 0;
    base->fade_tpage.mode = drawMode;
    base->scene_object_model = 0;
    base->room_geometry_table = 0;
    base->bank_asset_table = 0;
    base->fade_tile.code = flags | 2;
    base->bank_work_base = voiceBase;
    base->bank_work_end = voiceBase + 0x1400;
    base->bank_work_far_end = voiceBase + 0x2800;
    for (; j >= 0; j--)
        base->scene_object_tables[j] = 0;
    for (j = 1; j >= 0; j--)
        base->startup_resource_tables[j] = 0;
    base->startup_resource_blob = 0;
}

void Boot_InitMemoryLayout(void) {
    u8 *ptr;
    u8 *ptr2;
    s32 const_8000;
    s32 const_48000;
    u8 *tmp;

    const_48000 = 16 * ENTITY_ALLOCATION_BLOCK_SIZE;

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
}

int Akao_LoadVoiceBank(BattleEntity *actor) {
    BattleEntity *actor_reg = actor;
    int frame;
    int step;
    int target;
    int i;
    int entry;
    Pe1GameState *state;
    int id;

    if (actor_reg == 0) {
        return -1;
    }

    state = &g_GameState;
    frame = ((u32)actor_reg->animFrame >> 16);
    step = actor_reg->animStep;
    target = actor_reg->animPrev.parts.integer;

    if (step > 0 && frame < target) {
        int next_frame;
        int extra;
        next_frame = frame + 1;
        extra = actor_reg->animLastFrame;
        frame = next_frame + extra;
    } else if (step < 0 && target < frame) {
        int next_frame;
        int extra;
        next_frame = frame - 1;
        extra = actor_reg->animLastFrame;
        frame = next_frame - extra;
    }

    if (actor_reg == ((BattleEntity *)g_PlayerEntity) && (g_GameStateFlags & 2) == 0 && (state->flags & 0x800000) == 0) {
        u16 *entry_ids;
        entry_ids = g_AkaoVoiceBankEntries[0].ids;
        entry = 0;
        for (i = 0; i < 4; entry_ids += 4, i++, entry++) {
            if (g_AkaoVoiceBankEntries[entry].index == actor_reg->actionMode) {
                int entry_frame = g_AkaoVoiceBankEntries[entry].frame;
                if ((step > 0 && entry_frame >= target && entry_frame < frame) ||
                    (step < 0 && frame < entry_frame && entry_frame <= target)) {
                    id = entry_ids[state->bank_state_12];
                    if (id != 0) {
                        Asset_Find08w(id, 0, actor_reg->posX.parts.integer,
                                      actor_reg->posY.parts.integer, actor_reg->posZ.parts.integer);
                    }
                }
            }
        }
    }

    i = 4;
    if (i >= state->bank_state_11 + 4) {
        return 0;
    }

    entry = 4;
    do {
        if (g_AkaoVoiceBankEntries[entry].group == actor_reg->modelIndex &&
            g_AkaoVoiceBankEntries[entry].set == actor_reg->teamId &&
            g_AkaoVoiceBankEntries[entry].index == actor_reg->actionMode) {
            int entry_frame = g_AkaoVoiceBankEntries[entry].frame;
            if ((step > 0 && entry_frame >= target && entry_frame < frame) ||
                (step < 0 && frame < entry_frame && entry_frame <= target)) {
                id = g_AkaoVoiceBankEntries[entry].ids[state->bank_state_12];
                if (id != 0) {
                    Asset_Find08w(id, 0, actor_reg->posX.parts.integer,
                                  actor_reg->posY.parts.integer, actor_reg->posZ.parts.integer);
                }
            }
        }

        entry++;
        i++;
    } while (i < state->bank_state_11 + 4);

    return 0;
}
