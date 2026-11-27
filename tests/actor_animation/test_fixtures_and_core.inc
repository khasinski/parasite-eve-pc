void DeliverEvent(unsigned event,unsigned spec);
#include "pe1/battle_runtime.h"
#include "pe1/battle_cmd.h"
#include "pe1/battle_start.h"
#include "pe1/battle_entity_anim.h"
#include "pe1/battle_level_up.h"
#include "pe1/battle_update.h"
#include "pe1/battle_status.h"
#include "pe1/battle_main.h"
#include "pe1/field_sfx.h"
#include "pe1/psyq_callbacks.h"
#include "pe1/boot.h"
#include "pe1/boot_display.h"
#include "pe1/boot_disc_check.h"
#include "pe1/cdrom_buffers.h"
#include "pe1/draw_buffers.h"
#include "pe1/save.h"
#include "pe1/game_timers.h"
#include "pe1/menu_background.h"
#include "pe1/random.h"
#include <stdint.h>
void GameTime_InitCounter0(void);
void GameTime_InitCounter1(void);
void GameTime_InitCounter2(void);
void Inv_SetDefaultItemId(void);
int Inv_ScrambleGrid(void);
#include "pe1/entity_animation.h"
#include "pe1/room_m005_drifting_sprite.h"
#include "pe1/room_m005.h"
#include "pe1/room_window.h"
#include "pe1/room_m023_effects.h"
#include "pe1/room_m023_beacon.h"
#include "pe1/room_flare.h"
#include "pe1/room_glow_orb.h"
#include "pe1/room_drop_field.h"
#include "pe1/room_effect_sequence.h"
#include "pe1/room_ember_burst.h"
#include "pe1/room_module.h"
#include "pe1/room_script_record.h"
#include "pe1/room_m005_beacon.h"
#include "../src/overlays/room_m005_theater/RoomM005_FxOrbiter_externs.h"
#include "pe1/field_sprite_params.h"
#include "pe1/field_rotated_quad.h"
#include "pe1/field_shape_quads.h"
#include "pe1/field_star_fan.h"
#include "pe1/field_texture_tables.h"
#include "../src/overlays/room_lib/room_lib.h"
#include "pe1/game_state.h"
#include "pe1/player_entity.h"
#include "pe1/scene_assets.h"
#include "pe1/scene_room.h"
#include <stdio.h>
#include <string.h>
#include "pe1/boot_services.h"
#include "pe1/room_floor.h"
#include "pe1/font.h"
#include "pe1/map_selection.h"
#include "pe1/cdrom.h"
#include "pe1/entity_allocation.h"
#include "pe1/render_buffers.h"
#include "pe1/render_packets.h"
#include "pe1/render_prim.h"
#include "pe1/render_anim_position.h"
#include "pe1/render_animation_frame.h"
#include "pe1/field_glow_line.h"
#include "pe1/render_matrix_slot.h"
#include "pe1/field_tile.h"
#include "pe1/field_textured_model.h"
#include "pe1/field_model_draw.h"
#include "pe1/field_oriented_sprite.h"
#include "pe1/field_shaded_ring.h"
#include "pe1/field_shaded_quad.h"
#include "pe1/field_glow_layers.h"
#include "pe1/field_flash_burst.h"
#include "pe1/fieldfx_state.h"
#include "pe1/field_effect_modules.h"
#include "pe1/pm.h"
#include "pe1/akao/commands.h"
#include "pe1/akao/globals.h"
#include "pe1/akao/init_state.h"
#include "pe1/akao_send.h"
#include "pe1/script_state.h"
#include "pe1/script_queue.h"
#include "pe1/akao/queue.h"
#include "pe1/psyq_bios.h"
#include "pe1/fieldfx_particle.h"
#include "pe1/field_tri_test.h"
#include "pe1/field_collision.h"
#include "pe1/field_textured_strip.h"
#include "pe1/geom_state.h"
#include "pe1/field_movement.h"
#include "pe1/pad_state.h"
#include "pe1/scene_flags.h"
#include "pe1/entity_frame_update.h"
#include "pe1/cdrom.h"
#include "pe1/gte.h"
#include "pe1/menu_widget.h"
#include "pe1/menu_queue.h"
#include "pe1/draw_state.h"
#include "pe1/draw_level_bar.h"
#include "pe1/gpu_image.h"
#include "pe1/akao.h"
#include "pe1/akao/stream.h"
#include "pe1/memcard_save_state.h"
#include "pe1/save_tail_state.h"
#include "pe1/menu_dialog.h"
#include "pe1/text.h"
#include "pe1/menu_notification_dialog.h"
#include "pe1/menu_state.h"
#include "pe1/menu_inventory.h"
#include "pe1/menu_equipment.h"
#include "pe1/inventory_slots.h"
#include "pe1/stat_modifiers.h"
#include "../src/overlays/menu_memcard/menu_memcard_screen.h"
#include "../src/overlays/menu_memcard/menu_memcard_image.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <psyz/system.h>
#include <psyz/spu.h>
#include <psyz/audio.h>
#include <psyz/cd.h>

/* Observe the original traversal while deliberately changing cursor state. */
static int widget_draw_order[4], widget_draw_x[4], widget_draw_y[4];
static unsigned widget_draw_count;
static void record_widget_draw(MenuWidgetNode *node)
{
    if (widget_draw_count>=4) abort();
    unsigned i=widget_draw_count++;
    widget_draw_order[i]=node->mode;
    widget_draw_x[i]=g_TextCursorX;
    widget_draw_y[i]=g_TextCursorY;
    g_TextCursorX=900; g_TextCursorY=-700;
}

static unsigned menu_event_count;
static int menu_event_nodes[8], menu_event_flags[8];
static int record_menu_event(MenuWidgetNode *node,unsigned flags)
{
    if(menu_event_count>=8)abort();
    menu_event_nodes[menu_event_count]=node->mode;
    menu_event_flags[menu_event_count++]=(int)flags;
    return node->mode==2;
}

static int boot_buffer_layout(void)
{
    Pe1GameState saved = g_GameState;
    Boot_InitGameState();
    if (g_GameState.bank_asset_source != g_BootWorkArea ||
        g_GameState.voice_bank_base != g_BootWorkArea + 0x1800 ||
        g_GameState.voice_bank_base_1400 != g_BootWorkArea + 0x6000 ||
        (void *)g_GameState.entity_texture_blob != g_BootWorkArea + 0x14000 ||
        g_GameState.scene_object_work != D_80120D08 ||
        g_GameState.str_file_directory != g_StrFileDirBuffer ||
        (void *)g_GameState.loaded_scene_assets != g_StrFileDirBuffer - 8 ||
        g_GameState.scene_load_scratch != g_SceneLoadScratchArea ||
        g_GameState.bank_work_base != g_GameState.voice_bank_base ||
        g_GameState.bank_work_end != g_GameState.voice_bank_base + 0x1400 ||
        g_GameState.bank_work_far_end != g_GameState.voice_bank_base + 0x2800 ||
        !g_GameState.scene_process_slots || !g_GameState.script_sprite_buffer ||
        !g_GameState.unk_170) return 0;
    if(g_AkaoBgmHandle[0]!=g_GameState.bank_work_far_end)return 0;
    void *resident=g_AkaoBgmHandle[0];u8 sound_alias[4];
    g_AkaoBgmHandle[0]=sound_alias;
    g_AkaoBgmHandle[1]=sound_alias+1;
    g_GameState.scene_object_tables[1]=sound_alias+2;
    if(g_GameState.bank_work_far_end!=sound_alias || g_GameState.scene_object_tables[0]!=sound_alias+1 ||
       g_AkaoBgmHandle[2]!=sound_alias+2)return 0;
    g_AkaoBgmHandle[0]=resident;g_GameState.scene_object_tables[0]=g_GameState.scene_object_tables[1]=NULL;
    DrawFrameBuffer saved_frames[2];
    memcpy(saved_frames,g_DrawFrameBuffers,sizeof(saved_frames));
    memset(g_DrawFrameBuffers,0xA5,sizeof(g_DrawFrameBuffers));
    Draw_InitBuffers();
    for(unsigned i=0;i<2;++i) {
        const DrawFrameBuffer *frame=&g_DrawFrameBuffers[i];
        if(frame->frontBufferBase!=(i?g_RenderBackBufferBase[0]:g_RenderFrontBufferBase[0]) ||
           (void *)frame->orderingTableBase!=(i?(void *)g_RenderOtBufferBaseAlt[0]:(void *)g_OtBufferTable[0]) ||
           frame->draw.clip.x || frame->draw.clip.y!=(i?224:0) ||
           frame->draw.clip.w!=320 || frame->draw.clip.h!=224 ||
           frame->draw.ofs[0] || frame->draw.ofs[1]!=(i?224:0) ||
           frame->draw.isbg!=1 || frame->draw.r0 || frame->draw.g0 || frame->draw.b0 ||
           frame->display.disp.x || frame->display.disp.y!=(i?0:224) ||
           frame->display.disp.w!=320 || frame->display.disp.h!=224 ||
           frame->display.screen.y!=8 || frame->display.screen.h!=224) return 0;
    }
    if(g_TextCursorX || g_TextCursorY || g_TextCursorStackPtr!=g_TextCursorStackBottom ||
       g_DrawPrimColor!=0x808080 || g_DrawColorShaded!=0x404040 ||
       g_DrawDigitFontTpageClut!=0x395D || g_DrawDigitFontBaseTexU!=0x84 ||
       g_DrawDigitFontBaseTexV!=0xA4 || g_DrawPresentImage) return 0;
    if(!SDL_Init(SDL_INIT_EVENTS))return 0;
    ResetGraph(0);
    int saved_present=g_DrawPresentEnabled,saved_index=g_DrawBufferIndex;
    RenderNativeTag *saved_base=g_DrawOrderingTableBase;
    u32 *saved_entry=g_DrawOrderingTableEntry;
    u8 *saved_cursor=g_DrawPacketCursor,*saved_arena=g_DrawPacketArenaBase;
    DrawFrameBuffer *saved_current=g_DrawCurrentFrameBuffer;
    extern u_long D_800B89E4[]; /* SDK's terminating NOP packet. */
    for(unsigned slot=0;slot<2;++slot){
        g_DrawPresentEnabled=1;g_DrawBufferIndex=1-slot;Draw_SelectBuffer();
        RenderNativeTag *ot=g_DrawFrameBuffers[slot].orderingTableBase;
        if(g_DrawBufferIndex!=(int)slot || g_DrawOrderingTableBase!=ot ||
           g_DrawOrderingTableEntry!=(u32 *)(ot+1) || g_DrawCurrentFrameBuffer!=g_DrawFrameBuffers+slot ||
           g_DrawPacketCursor!=g_DrawFrameBuffers[slot].frontBufferBase){fprintf(stderr,"Native menu selection mismatch slot=%u index=%d base=%p expected=%p entry=%p expected=%p\n",slot,g_DrawBufferIndex,(void *)g_DrawOrderingTableBase,(void *)ot,(void *)g_DrawOrderingTableEntry,(void *)(ot+1));return 0;}
        for(unsigned entry=0;entry<4096;++entry)
            if(ot[entry].length || ot[entry].address!=(entry?(uintptr_t)(ot+entry-1):(uintptr_t)D_800B89E4)){fprintf(stderr,"Native menu OT mismatch slot=%u entry=%u address=%llx length=%llu\n",slot,entry,(unsigned long long)ot[entry].address,(unsigned long long)ot[entry].length);return 0;}
    }
    Draw_SetPresentImage(&saved_present);
    if(g_DrawPresentImage!=&saved_present)return 0;
    Draw_SetPresentImage(NULL);
    g_DrawPresentEnabled=saved_present;g_DrawBufferIndex=saved_index;
    g_DrawOrderingTableBase=saved_base;g_DrawOrderingTableEntry=saved_entry;
    g_DrawPacketCursor=saved_cursor;g_DrawPacketArenaBase=saved_arena;g_DrawCurrentFrameBuffer=saved_current;
    DISPENV saved_display[2];
    memcpy(saved_display,g_RenderDispEnvArray,sizeof(saved_display));
    if(g_DrawBaseX[0]!=g_GameState.scene_audio.tracks.reserved[0] ||
       g_DrawBaseY!=g_GameState.scene_audio.tracks.reserved[1]) return 0;
    Draw_SetBaseOffsetPosition(0,15-g_DrawBaseY);
    if(g_DrawBaseY!=15 || g_GameState.scene_audio.tracks.reserved[1]!=15 ||
       g_RenderDispEnvArray[0].screen.y!=15 || g_RenderDispEnvArray[1].screen.y!=15 ||
       Draw_GetBaseY()!=15) return 0;
    Draw_SetBaseOffsetPosition(0,1);
    if(g_DrawBaseY!=15) return 0;
    Draw_SetBaseOffsetPosition(0,-15);
    if(g_DrawBaseY || Draw_GetBaseY() || g_RenderDispEnvArray[0].screen.y ||
       g_RenderDispEnvArray[1].screen.y) return 0;
    Draw_SetBaseOffsetPosition(0,-1);
    if(g_DrawBaseY) return 0;
    memcpy(g_RenderDispEnvArray,saved_display,sizeof(saved_display));
    memcpy(g_DrawFrameBuffers,saved_frames,sizeof(saved_frames));
    RenderNativeTag *first = (RenderNativeTag *)g_OtBufferTable[0];
    RenderNativeTag *second = (RenderNativeTag *)g_RenderOtBufferBaseAlt[0];
    if (D_800B0E38.ordering[0]!=first || D_800B0E38.ordering[1]!=second ||
        (void *)g_GameState.bg_tim!=g_RenderScratchBufferBase[0] ||
        (void *)g_GameState.save_background_source!=g_RenderFrontBufferBase[0] ||
        (void *)g_GameState.save_background_destination!=g_RenderBackBufferBase[0] ||
        (void *)g_GameState.hud_tim!=g_RenderAnimBufferBase[0] ||
        (void *)g_GameState.script_sprite_buffer!=g_RenderAnimBufferBase[1] ||
        first + 4096 != second ||
        (void *)(second + 4096) != (void *)g_GameState.object_texture_blob)
        return 0;
    first[4095].length = 0x1234;
    second[0].length = 0x5678;
    second[4095].length = 0x9abc;
    memset(g_EntityAllocationTable, 0, sizeof(g_EntityAllocationTable));
    if (Entity_AllocBlock(8 * ENTITY_ALLOCATION_BLOCK_SIZE + 1)) return 0;
    void *largest = Entity_AllocBlock(8 * ENTITY_ALLOCATION_BLOCK_SIZE);
    if (!largest) return 0;
    Entity_FreeAllocationBlock((uintptr_t)largest);
    for (unsigned i = 0; i < ENTITY_ALLOCATION_ORIGINAL_SLOTS; ++i) {
        u8 *block = Entity_AllocBlock(ENTITY_ALLOCATION_BLOCK_SIZE);
        if (!block || block + ENTITY_ALLOCATION_BLOCK_SIZE > g_RenderFrontBufferBase[0])
            return 0;
        block[ENTITY_ALLOCATION_BLOCK_SIZE - 1] = (u8)i;
    }
    u8 *overflow = Entity_AllocBlock(ENTITY_ALLOCATION_BLOCK_SIZE);
    if (!overflow) return 0;
    for (unsigned i = 1; i < ENTITY_ALLOCATION_SLOTS - ENTITY_ALLOCATION_ORIGINAL_SLOTS; ++i) {
        u8 *block = Entity_AllocBlock(ENTITY_ALLOCATION_BLOCK_SIZE);
        if (block != overflow + i * ENTITY_ALLOCATION_BLOCK_SIZE) return 0;
        block[ENTITY_ALLOCATION_BLOCK_SIZE - 1] = (u8)i;
    }
    if (Entity_AllocBlock(1) || first[4095].length != 0x1234 ||
        second[0].length != 0x5678 || second[4095].length != 0x9abc)
        return 0;
    memset(g_EntityAllocationTable, 0, sizeof(g_EntityAllocationTable));
    g_GameState = saved;
    return 1;
}

/* Scripted I/O exercises the original glyph-record loader, including retries. */
static u8 glyph_source[42 * 2048], glyph_scratch[42 * 2048];
static unsigned glyph_reads, glyph_polls;
static PeDisc *glyph_disc;
static PeDiscFile glyph_archive;
static int glyph_io_failed;
static int glyph_read_retry, glyph_poll_retry, glyph_pending;
static int fx_image_io;
static unsigned boot_credit_corruption;
static unsigned texture_page_corruption;
static unsigned fx_read_failure,fx_poll_failure;
int CdRom_ImageReadSectorsFromLba(u32 lba,void *buffer,u32 count);
int CdRom_ImagePollReady(void);
int CdRom_ReadSectorsFromLba(u32 lba, void *buffer, u32 count)
{
    ++glyph_reads;
    if(fx_image_io){
        int status=glyph_reads==fx_read_failure?-1:CdRom_ImageReadSectorsFromLba(lba,buffer,count);
        if(status>=0 && texture_page_corruption){
            TimFile *tim=buffer;TimBlock *image=(TimBlock *)((u8 *)&tim->first_block+44);
            switch(texture_page_corruption){
            case 1:tim->magic=0;break;
            case 2:tim->flags|=0x1000;break;
            case 3:tim->first_block.header.length=-4;break;
            case 4:image->header.length=13;break;
            case 5:image->header.rect.h=-1;break;
            case 6:image->header.rect.w=1025;break;
            case 7:tim->first_block.header.length=count*2048-8;break;
            case 8:tim->first_block.header.rect.w=32;break;
            }
        }
        return status;
    }
    if (glyph_disc) {
        if (lba < glyph_archive.sector || buffer != glyph_scratch ||
            count > sizeof(glyph_scratch) / 2048 ||
            !pe_disc_read(glyph_disc, &glyph_archive,
                          (uint64_t)(lba - glyph_archive.sector) * 2048,
                          buffer, (size_t)count * 2048)) {
            glyph_io_failed = 1;
            return -2;
        }
        return 1;
    }
    if (lba != 12352 || buffer != glyph_scratch || count != 42) return -2;
    if (glyph_read_retry) { glyph_read_retry = 0; return -1; }
    memcpy(buffer, glyph_source, sizeof(glyph_source));
    return 1;
}
int CdRom_PollReady(void)
{
    ++glyph_polls;
    if(fx_image_io){
        int ready=glyph_polls==fx_poll_failure?-1:CdRom_ImagePollReady();
        if(ready || !boot_credit_corruption)return ready;
    }
    if (glyph_pending) { glyph_pending = 0; return 1; }
    if (glyph_poll_retry) { glyph_poll_retry = 0; return -1; }
    if(boot_credit_corruption){
        u32 value=0;unsigned fault=boot_credit_corruption;boot_credit_corruption=0;
        switch(fault){
        case 1:memcpy(g_CdLoadAreaBase+0x4f88,&value,4);break;
        case 2:value=0x80122fa4;memcpy(g_CdLoadAreaBase+0x4f88,&value,4);break;
        case 3:value=0x80120d1c;memcpy(g_CdLoadAreaBase+0x4f88,&value,4);break;
        case 4:memcpy(g_CdLoadAreaBase+0x4f88+744*4,g_CdLoadAreaBase+0x4f88,4);break;
        case 5:memcpy(g_BootDisplayFontTim,&value,4);break;
        case 6:memcpy(g_CdLoadAreaBase+0x4ea4,&value,4);break;
        case 7:value=0x80200000;memcpy(g_CdLoadAreaBase+0x4ea4+30*4,&value,4);break;
        case 8:memcpy(&value,g_CdLoadAreaBase+0x4ea4+4,4);value-=4;memcpy(g_CdLoadAreaBase+0x4ea4+8,&value,4);break;
        case 9:memset(g_CdLoadAreaBase+4,'A',24);break;
        case 10:memset(g_CdLoadAreaBase+0x1c,'A',0x22a4-0x1c);break;
        }
    }
    return 0;
}
static int glyph_records(void)
{
    Pe1GameState saved = g_GameState;
    g_GameState.pe_image_base_lba = 12345;
    g_GameState.scene_load_scratch = glyph_scratch;
    g_PeImageSectorTable[79] = 7;
    g_PeImageSectorTable[80] = 49;
    for (unsigned i = 0; i < sizeof(glyph_source); ++i)
        glyph_source[i] = (u8)((i * 37 + i / 328) ^ (i >> 8));
    g_FontActiveGlyphTable.codeCount = 113;
    glyph_read_retry = glyph_poll_retry = glyph_pending = 1;
    for (unsigned code = 0; code < 256; ++code) {
        if (Render_LoadFontGlyph(code) || g_FontActiveGlyphTable.codeCount != 113 ||
            memcmp(&g_FontActiveGlyphTable.groups, glyph_source + 1 + code * 328, 328))
            return 0;
    }
    g_GameState = saved;
    return glyph_reads == 258 && glyph_polls == 258;
}

static int glyph_selection(void)
{
    Pe1GameState saved = g_GameState;
    FontGlyphRecord record = {0};
    const u8 indices[6] = {2, 0, 3, 1, 4, 3};
    const u8 groups[6] = {1, 1, 2, 2, 1, 2};
    record.groups.count = 5;
    for (unsigned i = 0; i < 5; ++i) record.groups.codes[i] = i;
    record.slots.count = 6;
    memcpy(record.slots.indices, indices, sizeof(indices));
    memcpy(record.slots.groupKeys, groups, sizeof(groups));
    memcpy(glyph_source + 1 + 328, &record, sizeof(record));
    memcpy(glyph_source + 1 + 656, &record, sizeof(record));
    g_GameState.pe_image_base_lba = 12345;
    g_GameState.scene_load_scratch = glyph_scratch;
    g_FontGlyphCodeSequence[2] = 1;
    g_FontGlyphCodeSequence[3] = 2;
    g_FontSelectionState.table = &g_FontActiveGlyphTable;
    g_FontSelectionState.active = 1;
    if (Render_SetFontGlyphByCode(2) != 3 || g_FontSelectionState.selected != 2 ||
        Render_DrawTextDigit(NULL, 0) != 2 || Render_GetFontSlotCode(4) != 4 ||
        Menu_GetEquipSlotStateOrIndex() != 2 || !Menu_IsEquipSlotActive()) return 0;
    if (Render_GetOrLoadFontGlyph(22) != 1 || g_FontSelectionState.selected != 3 ||
        Render_GetOrLoadFontGlyph(23) != 4 || g_FontSelectionState.selected != 4 ||
        Render_GetOrLoadFontGlyph(20) != 0 || g_FontSelectionState.selected != 1 ||
        Render_GetOrLoadFontGlyph(21) != 2 || g_FontSelectionState.selected != 0) return 0;
    if (Menu_FindSelectedEquipSlotItem() != 3 || g_FontSelectionState.selected != 2 ||
        Render_FindFontGlyphSlot() != 2 || g_FontSelectionState.codeIndex != 3 ||
        Render_StepFontLoad() != 1 || g_FontSelectionState.codeIndex != 2) return 0;
    g_FontSelectionState.codeIndex = 70;
    if (Render_FindFontGlyphSlot() != 255 || !g_FontSelectionState.loadFailed ||
        Menu_ConsumeEquipSlotFlag() != 0 || Menu_ConsumeEquipSlotFlag() != 255) return 0;
    g_FontSelectionState.seed = 123;
    if (Render_ConsumeFontShuffleSeed() != 0 || Render_ConsumeFontShuffleSeed() != 255 ||
        Menu_ResetEquipSlotState() || Menu_IsEquipSlotActive() ||
        g_FontSelectionState.table || g_FontSelectionState.selected != 255) return 0;
    g_GameState = saved;
    return 1;
}

/* Normalize only the native table address; all selected bytes, generator
 * words, RNG state, I/O counts and script arguments remain observable. */
typedef struct {
    FontGlyphSelectionState state;
    FontGlyphTable table;
    u8 sequence[71];
    u32 random;
} GlyphMapSnapshot;
static GlyphMapSnapshot glyph_map_snapshot(void)
{
    GlyphMapSnapshot s={.state=g_FontSelectionState,.table=g_FontActiveGlyphTable,
                        .random=Psyz_RandState()};
    memcpy(s.sequence,g_FontGlyphCodeSequence,sizeof(s.sequence));return s;
}
static void glyph_map_restore(const GlyphMapSnapshot *s)
{
    g_FontSelectionState=s->state;g_FontActiveGlyphTable=s->table;
    memcpy(g_FontGlyphCodeSequence,s->sequence,sizeof(s->sequence));srand(s->random);
}
static int glyph_map_run(unsigned op,u32 value,unsigned key,u32 current,
                         int dispatch,u8 output[596])
{
    g_GameStateFlags=0x80000001;g_GameState.flags=0x10000002;
    g_SceneDispatchCur=current;g_SceneDispatchToken=0x12345678;
    g_DrawPrimGeneratorWords[14]=64;g_DrawPrimGeneratorWords[15]=16;
    for(unsigned i=0;i<17;++i)g_DrawPrimGeneratorWords[16+i]=(key+1)*0x9e3779b9u+i*0x1234567u;
    glyph_reads=glyph_polls=0;
    struct {u32 before;int value;u32 after;} arg={0x11223344,(s32)value,0xa55a5aa5};
    int *args[]={&arg.value};int result;
    if(dispatch){
        u32 program[]={op|(1u<<13),0,value,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};
        g_ScriptNodePool=&node;Script_RunQueue();
        if(node.current.script!=program+5 || node.flags!=0x90 || node.active ||
           g_ScriptNodePool || program[2]!=value)return 0;
        result=1;
    }else result=g_ScriptQueueOpcodeHandlers[op](args);
    u32 words[]={op,value,(u32)result,g_SceneDispatchToken,g_GameStateFlags,g_GameState.flags,
        g_FontSelectionState.table!=NULL,Psyz_RandState(),glyph_reads,glyph_polls,
        arg.before,(u32)arg.value,arg.after};
    unsigned at=0;
    for(unsigned i=0;i<13;++i)for(unsigned b=0;b<4;++b)output[at++]=words[i]>>(8*b);
    memcpy(output+at,&g_FontSelectionState,12);at+=12;
    memcpy(output+at,&g_FontActiveGlyphTable,329);at+=329;
    memcpy(output+at,g_FontGlyphCodeSequence,71);at+=71;
    for(unsigned i=0;i<33;++i)for(unsigned b=0;b<4;++b)output[at++]=g_DrawPrimGeneratorWords[i]>>(8*b);
    return at==596 && !glyph_io_failed &&
        (!g_FontSelectionState.table || g_FontSelectionState.table==&g_FontActiveGlyphTable);
}
static int glyph_map_check(unsigned op,u32 value,unsigned key,uint64_t *hash,unsigned *cases)
{
    GlyphMapSnapshot before=glyph_map_snapshot();u8 direct[596],queued[596];u32 current=0;
    for(unsigned same=0;same<2;++same){
        glyph_map_restore(&before);
        if(!glyph_map_run(op,value,key,current,0,direct))return 0;
        u32 next=g_SceneDispatchToken;
        glyph_map_restore(&before);
        if(!glyph_map_run(op,value,key,current,1,queued) || memcmp(direct,queued,sizeof(direct)))return 0;
        for(unsigned i=0;i<sizeof(direct);++i)*hash=(*hash^direct[i])*UINT64_C(1099511628211);
        ++*cases;current=next;
    }
    glyph_map_restore(&before);return 1;
}
static int glyph_map_opcodes(void)
{
    Pe1GameState game=g_GameState;GlyphMapSnapshot original=glyph_map_snapshot();
    u32 flags=g_GameStateFlags,current=g_SceneDispatchCur,token=g_SceneDispatchToken;
    u32 prim[33];memcpy(prim,g_DrawPrimGeneratorWords,sizeof(prim));
    FieldActor actor={0},*saved_actor=g_CurrentEntity;ScriptNode *node=g_ScriptNodePool;
    u32 *cursor=g_ScriptCursor;g_CurrentEntity=&actor;
    const u32 seeds[]={0,1,32767,0xffffffffu};
    const u32 initial_args[]={0,1,2,20,22,70,71,255,0xa55a0002};
    const unsigned actions[]={0,20,21,22,23,255};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;int ok=1;
    if(g_ScriptQueueOpcodeHandlers[177]!=Scene_SelectMapById ||
       g_ScriptQueueOpcodeHandlers[179]!=Script_SetFloorByEntityRoll)ok=0;
    for(unsigned trial=0;trial<4 && ok;++trial){
        g_FontSelectionState=(FontGlyphSelectionState){.selected=255,.loadFailed=trial&1,
            .unknown05={0x33,0x44,0x55},.seed=seeds[trial]};
        memset(g_FontGlyphCodeSequence,0xa5,71);memset(&g_FontActiveGlyphTable,0x5a,329);srand(0x12345678);
        GlyphMapSnapshot inactive=glyph_map_snapshot();
        for(unsigned op=177;op<=179;op+=2)for(unsigned a=0;a<9 && ok;++a){
            glyph_map_restore(&inactive);ok=glyph_map_check(op,initial_args[a],trial,&hash,&cases);
        }
        glyph_map_restore(&inactive);Render_LoadFontGlyphAlt();GlyphMapSnapshot active=glyph_map_snapshot();
        for(unsigned floor=2;floor<=70 && ok;++floor){
            glyph_map_restore(&active);Render_SetFontGlyphByCode(floor);
            GlyphMapSnapshot loaded=glyph_map_snapshot();unsigned count=g_FontActiveGlyphTable.slots.count;
            for(unsigned slot=0;slot<count && ok;++slot)for(unsigned a=0;a<6 && ok;++a){
                unsigned action=actions[a];
                if((action==21 && slot==0) || (action==23 && slot==count-1))continue;
                glyph_map_restore(&loaded);g_FontSelectionState.selected=slot;
                ok=glyph_map_check(177,action|(slot&1?0xa55a0000u:0),floor+trial*71,&hash,&cases);
            }
        }
        for(unsigned value=0;value<256 && ok;++value){
            glyph_map_restore(&active);ok=glyph_map_check(179,value|(value&1?0xa55a0000u:0),value+trial*256,&hash,&cases);
        }
        printf("Original floor map trial %u: %u cases, hash %016llx\n",trial,cases,(unsigned long long)hash);
    }
    if(cases!=80060 || hash!=UINT64_C(0x84099b77cf346185))ok=0;
    glyph_map_restore(&original);g_GameState=game;g_GameStateFlags=flags;
    g_SceneDispatchCur=current;g_SceneDispatchToken=token;memcpy(g_DrawPrimGeneratorWords,prim,sizeof(prim));
    g_CurrentEntity=saved_actor;g_ScriptNodePool=node;g_ScriptCursor=cursor;
    if(ok)puts("Original floor opcodes 177/179: 80060 unhooked USA selector/load/navigation states, four seeds, 69 floors, all byte arguments, map flags, RNG and real dispatcher passed");
    return ok;
}

static int glyph_asset_records(PeDisc *disc, const PeDiscFile *exe)
{
    /* Compare every resident map byte against the mounted USA executable,
     * including the final IDs and padding, before exercising floor records. */
    const struct {u32 address; const void *data; size_t size;} map_data[] = {
        {0x8009173c, g_MapSelectIndexTable, sizeof(g_MapSelectIndexTable)},
        {0x800917e4, g_MapIdTable, sizeof(g_MapIdTable)},
        {0x8009180c, g_MapFilenameTable, sizeof(g_MapFilenameTable)},
        {0x8009cd78, g_MapNameOverrideA, sizeof(g_MapNameOverrideA)},
        {0x8009cd80, g_MapNameOverrideB, sizeof(g_MapNameOverrideB)}
    };
    u8 original_maps[sizeof(g_MapFilenameTable)];
    unsigned map_bytes = 0;
    for (unsigned i = 0; i < sizeof(map_data) / sizeof(map_data[0]); ++i) {
        if (!pe_disc_read(disc, exe, map_data[i].address - 0x80010000u + 0x800,
                          original_maps, map_data[i].size) ||
            memcmp(original_maps, map_data[i].data, map_data[i].size)) return 0;
        map_bytes += map_data[i].size;
    }
    for (unsigned row = 0; row < 7; ++row)
        for (unsigned slot = 0; slot < 24; ++slot)
            if (g_MapSelectIndexTable[row].filename_index[slot] >= 66) return 0;
    printf("Original floor map data: five resident tables, %u USA bytes, 168 valid indices\n", map_bytes);
    Pe1GameState saved = g_GameState;
    u16 range[2];
    unsigned slots = 0;
    if (!pe_disc_find(disc, "PE.IMG", &glyph_archive) ||
        !pe_disc_read(disc, exe, 0x800930d8u - 0x80010000u + 0x800 + 79 * 2,
                      range, sizeof(range)) || range[1] <= range[0]) return 0;
    size_t bytes = (size_t)(range[1] - range[0]) * 2048;
    if (bytes > sizeof(glyph_source) ||
        !pe_disc_read(disc, &glyph_archive, (uint64_t)range[0] * 2048,
                      glyph_source, bytes)) return 0;
    unsigned records = glyph_source[0];
    if (!records || 1 + records * 328 > bytes) return 0;
    g_PeImageSectorTable[79] = range[0];
    g_PeImageSectorTable[80] = range[1];
    g_GameState.pe_image_base_lba = glyph_archive.sector;
    g_GameState.scene_load_scratch = glyph_scratch;
    g_FontSelectionState.table = &g_FontActiveGlyphTable;
    g_FontActiveGlyphTable.codeCount = records;
    glyph_disc = disc;
    int ok = 1;
    for (unsigned code = 0; code < records && ok; ++code) {
        const u8 *raw = glyph_source + 1 + code * 328;
        if (Render_LoadFontGlyph(code) || glyph_io_failed ||
            memcmp(&g_FontActiveGlyphTable.groups, raw, 328) ||
            raw[2] > 24 || raw[27] > 100) { ok = 0; break; }
        for (unsigned slot = 0; slot < raw[27]; ++slot) {
            unsigned index = raw[28 + slot];
            if (index >= raw[2] || Render_GetFontSlotCode(slot) != raw[3 + index]) {
                ok = 0; break;
            }
            ++slots;
        }
        for (unsigned mode = 0; mode < 5; ++mode) {
            for (unsigned digit = 0; digit < 10; ++digit) {
                unsigned sought = mode == 0 ? 3 : mode == 1 ? (digit != 0) : mode == 2 ? 2 : 4;
                unsigned group = 0, expected = 255;
                g_FontSelectionState.codeIndex = digit;
                for (unsigned i = 0; i < raw[2]; ++i)
                    if (raw[3 + i] == sought) { group = i; break; }
                for (unsigned i = 0; i < raw[27]; ++i)
                    if (raw[28 + i] == group) { expected = i; break; }
                if (Render_DrawTextDigit(NULL, mode) != expected) ok = 0;
            }
        }
    }
    static const u32 seeds[] = {0, 1, 32767, 0xffffffffu};
    for (unsigned trial = 0; trial < 4 && ok; ++trial) {
        u8 sequence[71], seen[256] = {0};
        g_FontSelectionState.loadFailed = 1;
        g_FontSelectionState.seed = seeds[trial];
        int selected = Render_LoadFontGlyphAlt();
        unsigned rng_state = Psyz_RandState();
        memcpy(sequence, g_FontGlyphCodeSequence, sizeof(sequence));
        if (glyph_io_failed || !g_FontSelectionState.active ||
            g_FontSelectionState.codeIndex != 2 ||
            g_FontSelectionState.code != sequence[2] ||
            g_FontSelectionState.selected >= g_FontActiveGlyphTable.slots.count) {
            ok = 0; break;
        }
        for (unsigned i = 2; i < 71; ++i) {
            unsigned value = sequence[i];
            unsigned limit = i < 10 ? 104 : i < 20 ? 109 : 113;
            if (i % 10 == 0) {
                if (value != i / 10 - 1) ok = 0;
            } else {
                if (value < 7 || value >= limit || value >= records || seen[value]) ok = 0;
                seen[value] = 1;
            }
        }
        g_FontSelectionState.seed = seeds[trial];
        if (Render_LoadFontGlyphAlt() != selected ||
            memcmp(sequence, g_FontGlyphCodeSequence, sizeof(sequence)) ||
            Psyz_RandState() != rng_state) ok = 0;
    }
    if (ok) ok = glyph_map_opcodes();
    glyph_disc = NULL;
    g_GameState = saved;
    Menu_ResetEquipSlotState();
    if (ok) printf("Original glyph loader: %u real records, %u slots, %u mode queries\n",
                   records, slots, records * 50);
    return ok;
}

/* Record positional sound requests; no replacement audio ships with the game. */
static unsigned sounds;
int Asset_Find08w(int id,int mode,s16 x,s16 y,s16 z)
{
    (void)mode;(void)x;(void)y;(void)z;if(id==1000)++sounds;return 0;
}
static int host_event(Uint32 type, SDL_Scancode key) {
    SDL_Event event={0};event.type=type;event.key.scancode=key;
    if(!SDL_PushEvent(&event))return 0;
    while(SDL_PollEvent(&event))Boot_HandlePadEvent(&event);
    return 1;
}
/* Reset must clear the whole native render object while keeping mounted data
 * and allocated buffers. Poison catches the historical 49-word partial clear. */
static int game_state_reset(void)
{
    Pe1GameState saved = g_GameState;
    static u8 voice[0x3000];
    u8 zero[sizeof(RenderObjectEntity)] = {0};
    u8 tail[8] = {0};
    int ok;
    memset(&g_GameState, 0x5a, sizeof(g_GameState));
    g_GameState.voice_bank_base = voice;
    g_GameState.scene_load_scratch = voice + 32;
    g_GameState.bank_rows[9][47] = voice + 64;
    g_GameState.pe_image_base_lba = 12345;
    Asset_ResetGameState();
    ok = 1;
    if (!(!memcmp(&g_GameState.scene_object, &zero, sizeof(zero)))) { fprintf(stderr, "game-state reset check 0 failed\n"); ok = 0; }
    if (!(!memcmp(g_GameState.reserved_scene_object_tail, tail, sizeof(tail)))) { fprintf(stderr, "game-state reset check 1 failed\n"); ok = 0; }
    if (!(g_GameState.flags == 3)) { fprintf(stderr, "game-state reset check 2 failed\n"); ok = 0; }
    if (!(g_GameState.saved_state_004 == 10)) { fprintf(stderr, "game-state reset check 3 failed\n"); ok = 0; }
    if (!(g_GameState.field_bg_cache_id == -1)) { fprintf(stderr, "game-state reset check 4 failed\n"); ok = 0; }
    if (!(g_GameState.room_type == 2)) { fprintf(stderr, "game-state reset check 5 failed\n"); ok = 0; }
    if (!(g_GameState.scene_area_type_backup == 0)) { fprintf(stderr, "game-state reset check 6 failed\n"); ok = 0; }
    if (!(g_GameState.pe_image_base_lba == 12345)) { fprintf(stderr, "game-state reset check 7 failed\n"); ok = 0; }
    if (!(g_GameState.voice_bank_base == voice)) { fprintf(stderr, "game-state reset check 8 failed\n"); ok = 0; }
    if (!(g_GameState.bank_work_base == voice)) { fprintf(stderr, "game-state reset check 9 failed\n"); ok = 0; }
    if (!(g_GameState.bank_work_end == voice + 0x1400)) { fprintf(stderr, "game-state reset check 10 failed\n"); ok = 0; }
    if (!(g_GameState.bank_work_far_end == voice + 0x2800)) { fprintf(stderr, "game-state reset check 11 failed\n"); ok = 0; }
    if (!(g_GameState.scene_load_scratch == voice + 32)) { fprintf(stderr, "game-state reset check 12 failed\n"); ok = 0; }
    if (!(g_GameState.bank_rows[9][47] == voice + 64)) { fprintf(stderr, "game-state reset check 13 failed\n"); ok = 0; }
    if (!(g_GameState.scene_audio.tracks.keys[0][0] == -1)) { fprintf(stderr, "game-state reset check 14 failed\n"); ok = 0; }
    if (!(g_GameState.scene_audio.tracks.keys[1][1] == -1)) { fprintf(stderr, "game-state reset check 15 failed\n"); ok = 0; }
    if (!(g_GameState.scene_audio.tracks.movie_frame == -1)) { fprintf(stderr, "game-state reset check 16 failed\n"); ok = 0; }
    if (!(g_GameState.pending_sample_bank == -1)) { fprintf(stderr, "game-state reset check 17 failed\n"); ok = 0; }
    if (!(g_GameState.scene_init_phase == 0x20)) { fprintf(stderr, "game-state reset check 18 failed\n"); ok = 0; }
    if (!(g_GameState.track_volumes[0] == 0x7f)) { fprintf(stderr, "game-state reset check 19 failed\n"); ok = 0; }
    if (!(g_GameState.track_volumes[1] == 0x7f)) { fprintf(stderr, "game-state reset check 20 failed\n"); ok = 0; }
    if (g_GameState.fade_tile.tag.len != 3 || g_GameState.fade_tpage.tag.len != 1 ||
        g_GameState.fade_tpage.mode != 0xe1000440u || g_GameState.fade_tile.code != 0x62 ||
        g_GameState.fade_tile.r != 32 || g_GameState.fade_tile.g != 32 ||
        g_GameState.fade_tile.b != 32 || g_GameState.fade_tile.x || g_GameState.fade_tile.y ||
        g_GameState.fade_tile.w != 320 || g_GameState.fade_tile.h != 224) {
        fprintf(stderr, "game-state fade packet initialization failed\n"); ok = 0;
    }
    g_GameState = saved;
    return ok;
}

static int bios_random(void)
{
    static const int expected[] = {16838, 5758, 10113, 17515, 31051, 5627, 23010, 7419};
    srand(1);
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
        if (rand() != expected[i]) return 0;
    if (Psyz_RandState() != 0x9cfbae39u) return 0;
    unsigned saved = Psyz_RandState();
    int next = rand();
    srand(saved);
    if (rand() != next) return 0;
    static const u32 seeds[] = {0, 1, 0x80000000u, 0xffffffffu, 12345};
    for (unsigned seed = 0; seed < sizeof(seeds) / sizeof(seeds[0]); ++seed) {
        uint64_t reference = seeds[seed];
        srand(seeds[seed]);
        for (unsigned i = 0; i < 10000; ++i) {
            reference = (reference * 1103515245ULL + 12345) % 4294967296ULL;
            if (rand() != (int)((reference / 65536) % 32768) ||
                Psyz_RandState() != reference) return 0;
        }
    }
    return 1;
}

/* Controlled draw-buffer input for the original packet linker. */

static RoomEnt *expected_room_context;
static unsigned room_callback_count;
static void record_room_callback(RoomEnt *context)
{
    if(context!=expected_room_context) abort();
    ++room_callback_count;
    context->w04+=7;
}
static int room_owner_state(void)
{
    FieldActor actor={0}; FieldActorState child={0};
    RoomEnt room={0}; room.link=(RoomLink *)&actor; actor.state=&child;
    struct FieldFxTaskOwner *owner=(struct FieldFxTaskOwner *)&room;
    room.t19=4; actor.mode=1;
    if(FieldFx_IsOwnerBusy(owner)!=1) return 0;
    actor.mode=2; room.t19=2; actor.flags=0x88000;
    if(FieldFx_IsOwnerBusy(owner)!=1 || actor.flags!=0x8000 || FieldFx_IsOwnerBusy(owner)!=0) return 0;
    room.t19=1; actor.flags=0x48000;
    if(FieldFx_IsOwnerBusy(owner)!=1 || actor.flags!=0x8000 || FieldFx_IsOwnerBusy(owner)!=0) return 0;
    room.t19=0;
    child.core_flags=0x1800;
    if(FieldFx_IsOwnerBusy(owner)!=1) return 0;
    for(unsigned phase=1;phase<8;++phase) {
        child.core_flags=phase<<1;
        if(FieldFx_IsOwnerBusy(owner)!=1) return 0;
    }
    child.core_flags=0;
    if(FieldFx_IsOwnerBusy(owner)!=0) return 0;
    actor.action=10; actor.anim_prev=10<<16;
    room.active=2; room.t18=3;
    FieldFx_LoopLinkedAnimation(&room);
    if(room.active!=1 || actor.anim.fixed!=3) return 0;
    room.active=-1; room.t18=-1;
    FieldFx_LoopLinkedAnimation(&room);
    if(room.active!=-1 || actor.anim.fixed!=-1) return 0;
    room.active=0; actor.anim.fixed=123;
    FieldFx_LoopLinkedAnimation(&room);
    if(actor.anim.fixed!=123) return 0;
    room.active=2; actor.anim_prev=9<<16;
    FieldFx_LoopLinkedAnimation(&room);
    if(room.active!=2 || actor.anim.fixed!=123) return 0;
    puts("Original room owner: native busy flags, child phases and linked animation repeats passed");
    return 1;
}

typedef struct NativeListState { void *identity; int id, ticks; } NativeListState;
static unsigned list_draws;
static int record_list_callback(int mode,void *payload,intptr_t argument)
{
    NativeListState *state=payload;
    if(state->identity!=state || argument!=42 || (uintptr_t)payload%_Alignof(NativeListState)) abort();
    if(mode==1) {
        if(g_FieldFxCurrentAge!=state->ticks) abort();
        ++state->ticks;
        return state->id==1 && state->ticks==2;
    }
    if(mode!=2 || g_FieldFxCurrentAge!=state->ticks) abort();
    ++list_draws;
    return 0;
}
/* A valid separate buffer may occupy the address immediately after an arena.
 * Make the boundary deterministic rather than relying on optimizer stack layout. */
static int callback_list_adjacent_storage(void)
{
    struct {
        FieldFxTaskContext context;
        _Alignas(void *) char external[64];
    } storage = {0};
    FieldFxTaskContext *saved_context=g_FieldFxTaskContext;
    FieldFxTaskSlot *saved_slot=g_FieldFxActiveTaskSlot;
    memset(storage.context.arena,0xa5,sizeof(storage.context.arena));
    memset(storage.external,0x5a,sizeof(storage.external));
    g_FieldFxTaskContext=&storage.context;g_FieldFxActiveTaskSlot=NULL;
    int ok=storage.external==storage.context.arena+sizeof(storage.context.arena);
    if(ok)ok=FieldFx_InitCallbackList(storage.external,0,0,record_list_callback)==sizeof(FieldFxCallbackList);
    char snapshot[64];memcpy(snapshot,storage.external,sizeof(snapshot));
    g_FieldFxActiveTaskSlot=&storage.context.slots[0];
    if(ok)ok=FieldFx_InitCallbackList(storage.context.arena+sizeof(storage.context.arena),0,0,record_list_callback)==-1;
    if(ok)ok=FieldFx_InitCallbackList(storage.context.arena+sizeof(storage.context.arena)-_Alignof(FieldFxCallbackList),0,0,record_list_callback)==-1;
    if(ok)ok=!memcmp(snapshot,storage.external,sizeof(snapshot));
    for(size_t i=0;i<sizeof(storage.context.arena);++i)if((u8)storage.context.arena[i]!=0xa5)ok=0;
    g_FieldFxTaskContext=saved_context;g_FieldFxActiveTaskSlot=saved_slot;
    if(!ok)fprintf(stderr,"Callback-list adjacent external buffer or owned arena boundary failed\n");
    return ok;
}

static int callback_list_storage(void)
{
    if(!callback_list_adjacent_storage())return 0;
    _Alignas(void *) u8 arena[256]; memset(arena,0xa5,sizeof(arena));
    int bytes=FieldFx_InitCallbackList((char *)arena,sizeof(NativeListState),3,record_list_callback);
    size_t stride=(sizeof(FieldFxCallbackListEntry)+sizeof(NativeListState)+_Alignof(FieldFxCallbackListEntry)-1)/
                  _Alignof(FieldFxCallbackListEntry)*_Alignof(FieldFxCallbackListEntry);
    if(bytes!=(int)(sizeof(FieldFxCallbackList)+3*stride)) return 0;
    NativeListState *states[3];
    for(unsigned i=0;i<3;++i) {
        states[i]=FieldFx_AcquireCallbackSlot((char *)arena);
        if((u8 *)states[i]!=arena+sizeof(FieldFxCallbackList)+i*stride+sizeof(FieldFxCallbackListEntry)) return 0;
        states[i]->identity=states[i]; states[i]->id=i+1; states[i]->ticks=0;
    }
    if(FieldFx_AcquireCallbackSlot((char *)arena)) return 0;
    FieldFxTaskContext context={0}; context.argument=42;
    g_FieldFxTaskContext=&context; g_FieldFxCurrentAge=91;
    if(FieldFx_UpdateCallbackList(arena)!=3 || g_FieldFxCurrentAge!=91) return 0;
    if(FieldFx_DrawCallbackList((char *)arena)!=3 || list_draws!=3 || g_FieldFxCurrentAge!=91) return 0;
    if(FieldFx_UpdateCallbackList(arena)!=3 || g_FieldFxCurrentAge!=91) return 0;
    if(FieldFx_DrawCallbackList((char *)arena)!=2 || list_draws!=5 || g_FieldFxCurrentAge!=91) return 0;
    NativeListState *reused=FieldFx_AcquireCallbackSlot((char *)arena);
    if(reused!=states[0] || reused->ticks!=2) return 0;
    FieldFxCallbackListEntry *entry=(FieldFxCallbackListEntry *)((u8 *)reused-sizeof(FieldFxCallbackListEntry));
    if(entry->age!=0 || entry->active!=1) return 0;
    for(size_t i=bytes;i<sizeof(arena);++i) if(arena[i]!=0xa5) return 0;
    FieldFxTaskSlot slot={0}; slot.end=(char *)arena;
    g_FieldFxActiveTaskSlot=&slot;
    char *out=NULL;
    if(FieldFx_AllocCallbackList(&out,0,sizeof(NativeListState),3,record_list_callback)!=bytes || out!=(char *)arena) return 0;
    if(FieldFx_InitCallbackList((char *)arena,-1,3,record_list_callback)!=-1 ||
       FieldFx_InitCallbackList((char *)arena,16,-1,record_list_callback)!=-1 ||
       FieldFx_InitCallbackList((char *)arena,16,INT_MAX,record_list_callback)!=-1 ||
       FieldFx_InitCallbackList((char *)arena+1,16,3,record_list_callback)!=-1) return 0;
    g_FieldFxActiveTaskSlot=NULL;
    if(FieldFx_AllocCallbackList(&out,0,16,3,record_list_callback)!=-1) return 0;
    FieldFxTaskContext room_context={0},*old_room_context=g_FieldFxTaskContext;
    g_FieldFxTaskContext=&room_context;room_context.variables[0]=7;
    intptr_t record=RoomLib_SetTaskVar0(0,0,0,0);
    if(record!=(intptr_t)(g_RoomM005ScriptAnchorView-4) || g_RoomM005ScriptFadeView!=(u8 *)record+12 ||
        room_context.variables[0]!=7 || RoomLib_SetTaskVar0(1,0,0,0)!=record || room_context.variables[0]!=1)return 0;
    room_context.flags=0x5a;room_context.argument=record;
    if(FX_TASK_AS(RoomM005EventState)->triggered!=1 || FX_TASK_AS(RoomDlgState)->h12!=1 ||
        FX_TASK_AS(RoomM005BeaconEvent)->active!=0x5a || FX_TASK_AS(RoomM005BeaconEvent)->variant!=1)return 0;
    FX_TASK_AS(RoomM005BeaconEvent)->variant=23;FX_TASK_AS(RoomDlgState)->bD=0x42;
    if(room_context.variables[0]!=23 || room_context.flags!=0x42 || room_context.argument!=record ||
        room_context.pc.script || room_context.cursor)return 0;
    GteShortVector *anchor=(void *)g_RoomM005ScriptAnchorView,*fade=(void *)g_RoomM005ScriptFadeView;
    *anchor=(GteShortVector){-123,456,-789,0};*fade=(GteShortVector){12,-34,56,0};
    s16 *record_words=(s16 *)record;
    if(record_words[2]!=-123 || record_words[3]!=456 || record_words[4]!=-789 ||
        record_words[6]!=12 || record_words[7]!=-34 || record_words[8]!=56)return 0;
    FieldFxTaskProgram room_program={.initialize=RoomLib_SetTaskVar0};
    FieldFxTaskOwner room_owner={0};FieldFxObjectPrefix *old_room_owner=g_FieldFxOwner;
    room_owner.tasks.table=&room_program.table;
    FieldFx_InitTaskContext((PmSlotHeader *)&room_owner);
    if(FieldFx_InitTaskProgram((PmSlotHeader *)&room_owner,0,1,0,0,0)!=0 ||
        room_owner.tasks.argument!=record || room_owner.tasks.variables[0]!=1)return 0;
    memset((void *)record,0,20);g_FieldFxTaskContext=old_room_context;g_FieldFxOwner=old_room_owner;
    if(&D_800F336C!=&g_FieldFxSpriteParams.palette || &D_800F3374!=&g_FieldFxSpriteParams.depth) return 0;
    if(&g_FieldFxTpageSlot1!=g_FieldFxTpageSlots+1 || &g_FieldFxTpageSlot2!=g_FieldFxTpageSlots+2 ||
        &g_FieldFxTpageSlot3!=g_FieldFxTpageSlots+3 || &g_FieldFxTpageSlot4!=g_FieldFxTpageSlots+4 ||
        &g_FieldFxTpageSlot9!=g_FieldFxTpageSlots+9 || &g_FieldFxTpageSlot11!=g_FieldFxTpageSlots+11 ||
        &g_FieldFxPaletteEntry3!=g_FieldFxPaletteTable+3 || &g_FieldFxTpageTableEntry1!=g_FieldFxTpageTable+1)return 0;
    /* Run the real M005 drift callback through the original list dispatcher:
     * age 6 moves once more before terminating; halfword motion wraps. */
    memset(arena,0xa5,sizeof(arena));
    bytes=FieldFx_InitCallbackList((char *)arena,sizeof(RoomM005DriftingSpriteState),3,RoomM005_BeaconDriftCallback);
    RoomM005DriftingSpriteState *drifts[3];
    for(unsigned i=0;i<3;++i) {
        drifts[i]=FieldFx_AcquireCallbackSlot((char *)arena);
        *drifts[i]=(RoomM005DriftingSpriteState){65530,3,32767,0,13,65534,32769};
    }
    for(unsigned frame=1;frame<=7;++frame) {
        if(FieldFx_UpdateCallbackList(arena)!=3 || g_FieldFxCurrentAge!=91) return 0;
        for(unsigned i=0;i<3;++i) {
            if(drifts[i]->x!=(u16)(65530+13*frame) || drifts[i]->y!=(u16)(3+65534*frame) ||
                drifts[i]->z!=(u16)(32767+32769*frame)) return 0;
        }
    }
    if(FieldFx_UpdateCallbackList(arena)!=0) return 0;
    RoomM005DriftingSpriteState *drift_reused=FieldFx_AcquireCallbackSlot((char *)arena);
    if(drift_reused!=drifts[0]) return 0;
    FieldFxCallbackListEntry *drift_entry=(void *)((u8 *)drift_reused-sizeof(FieldFxCallbackListEntry));
    if(drift_entry->age || drift_entry->active!=1) return 0;
    for(size_t i=bytes;i<sizeof(arena);++i)if(arena[i]!=0xa5)return 0;
    puts("Original M005 drift: native callback ABI, halfword motion, terminal frame and slot reuse passed");
    _Alignas(void *) u8 original_pool[1024];memset(original_pool,0xa5,sizeof(original_pool));
    RoomM005OrbiterState controller={.armed=1,.frame=12};
    FieldFxTaskSlot original_slot={.start=(char *)&controller,.end=(char *)original_pool};
    g_FieldFxActiveTaskSlot=&original_slot;
    int original_bytes=RoomM005_Orbiter(0,&controller,0);
    FieldFxCallbackList *original_list=(void *)original_pool;
    if(original_bytes<=0 || original_bytes>sizeof(original_pool) || controller.armed || controller.frame ||
        original_list->count!=24 || original_list->callback!=RoomM005_FxOrbiter)return 0;
    for(unsigned i=original_bytes;i<sizeof(original_pool);++i)if(original_pool[i]!=0xa5)return 0;
    g_FieldFxActiveTaskSlot=NULL;
    g_FieldFxTaskContext=NULL;
    puts("Original callback lists: aligned pointer payloads, update/draw ages, completion/reuse and guards passed");
    return 1;
}

typedef struct NativeTaskState { void *identity; int ticks; } NativeTaskState;
static unsigned task_draws;
static int task_pointer_value;
static intptr_t record_native_task_argument(int mode,intptr_t value,intptr_t b,intptr_t c)
{
    if(mode==-7 && value==INT32_MIN && b==0x12345678 && c==-9)return 42;
    if(mode!=7 || value!=(intptr_t)&task_pointer_value || b!=value+sizeof(int) || c!=-9)abort();
    return value;
}
static int observe_native_task(int mode,void *payload,intptr_t argument)
{
    NativeTaskState *state=payload;
    if(argument==(intptr_t)&task_pointer_value) {
        if(mode==1)++*(int *)argument;
        if(mode==2)*(int *)argument+=10;
        return 0;
    }
    if(argument!=42 || (uintptr_t)payload%_Alignof(NativeTaskState)) abort();
    if(mode==0) { state->identity=state; state->ticks=0; return 0; }
    if(state->identity!=state) abort();
    if(mode==1) {
        if(g_FieldFxCurrentAge!=state->ticks) abort();
        return ++state->ticks==2;
    }
    if(mode!=2 || g_FieldFxCurrentAge!=state->ticks) abort();
    ++task_draws; return 0;
}
static int reject_task_allocation(int mode,void *payload,intptr_t argument)
{
    (void)mode; (void)argument;
    return FieldFx_InitCallbackList((char *)payload,16,1000,record_list_callback);
}
static int native_task_cycle(void)
{
    struct { FieldFxTaskOwner owner; u8 guard[16]; } storage;
    memset(&storage,0xa5,sizeof(storage));
    FieldFxTaskOwner *owner=&storage.owner;
    FieldFxTaskTable table={0};
    u16 script[]={1,0,9,2,0,0,4,2,0,0,0,0};
    table.callbacks[0]=observe_native_task; table.sizes[0]=sizeof(NativeTaskState);
    owner->tasks.table=&table; owner->tasks.pc.script=script; owner->tasks.argument=42;
    owner->prefix.actor=NULL; owner->prefix.reserved00=0;
    FieldFx_InitTaskContext((PmSlotHeader *)owner);
    if(owner->tasks.cursor!=owner->tasks.arena || owner->tasks.used || owner->tasks.count)
        return 0;
    for(unsigned slot=0;slot<8;++slot)
        if(owner->tasks.slots[slot].id!=0xffff || owner->tasks.slots[slot].start || owner->tasks.slots[slot].end)
            return 0;
    if(owner->tasks.table!=&table || owner->tasks.pc.script!=script) return 0;
    GteMatrix identity={0}; identity.m[0][0]=identity.m[1][1]=identity.m[2][2]=4096;
    GteMatrix *saved_view=g_ViewMatrixSlot.value; g_ViewMatrixSlot.value=&identity;
    if(FieldFx_UpdateTasks((PmSlotHeader *)owner)!=0 || owner->tasks.variables[0]!=9 || owner->tasks.count!=1 ||
       owner->tasks.delay!=2 || owner->tasks.slots[0].age!=1) return 0;
    if(FieldFx_DrawTasks((PmSlotHeader *)owner)!=0 || task_draws!=1) return 0;
    if(FieldFx_UpdateTasks((PmSlotHeader *)owner)!=0 || owner->tasks.count || owner->tasks.delay!=1 || owner->tasks.slots[0].id!=0xffff)
        return 0;
    if(FieldFx_UpdateTasks((PmSlotHeader *)owner)!=0 || owner->prefix.reserved00!=4 || owner->tasks.delay) return 0;
    g_ViewMatrixSlot.value=saved_view;
    FieldFxTaskProgram program={0};
    program.table.callbacks[0]=observe_native_task;program.table.sizes[0]=sizeof(NativeTaskState);
    program.initialize=record_native_task_argument;program.script=script;
    void *old_program=g_FieldFxTaskPrograms[32];g_FieldFxTaskPrograms[32]=&program;
    if(FieldFx_BindTaskProgram(owner,32)!=0 || owner->tasks.table!=&program.table || owner->tasks.pc.script!=script)return 0;
    FieldFx_InitTaskContext((PmSlotHeader *)owner);owner->tasks.argument=42;
    if(FieldFx_InitTaskProgram((PmSlotHeader *)owner,1,0,0,0,0)!=0 || owner->tasks.argument!=42)return 0;
    if(FieldFx_InitTaskProgram((PmSlotHeader *)owner,0,7,(intptr_t)&task_pointer_value,(intptr_t)&task_pointer_value+sizeof(int),-9)!=0 ||
        owner->tasks.argument!=(intptr_t)&task_pointer_value)return 0;
    task_pointer_value=0;
    if(FieldFx_SpawnTask(0)!=(int)sizeof(NativeTaskState))return 0;
    owner->tasks.pc.script=script+9;
    if(FieldFx_UpdateTasks((PmSlotHeader *)owner)!=0 || task_pointer_value!=1)return 0;
    g_ViewMatrixSlot.value=&identity;
    if(FieldFx_DrawTasks((PmSlotHeader *)owner)!=0 || task_pointer_value!=11)return 0;
    g_ViewMatrixSlot.value=saved_view;
    {
        PmPrimarySlot bank[11]={0};PmSecondarySlot secondary[11]={0};PmTables old_tables=g_PmTables;
        Pe1GameState old_game=g_GameState;u32 old_flags=g_GameStateFlags;
        FieldActor actor={0},*old_actor=g_CurrentEntity,*old_player=g_PlayerEntity,*old_head=g_FieldActorListHead;ScriptNode *old_node=g_ScriptNodePool;u32 *old_cursor=g_ScriptCursor;
        g_CurrentEntity=&actor;
        g_PmTables=(PmTables){g_PmCmdHandlerArray,bank,secondary};
        g_GameStateFlags=0x80;g_GameState.flags&=~8u;g_GameState.tim_load_state=0;
        if(g_PmCmdHandlerArray[85]!=&g_FieldFxTaskClass)return 0;
        program.script=script+9;task_pointer_value=0;
        u32 allocate[]={106|(2u<<13),0,117,99,32,0};
        ScriptNode dispatch={.current.script=allocate,.active=1,.flags=0x80};g_ScriptNodePool=&dispatch;Script_RunQueue();
        if(allocate[3]!=0 || dispatch.current.script!=allocate+6 || dispatch.flags!=0x90 || g_ScriptNodePool ||
            bank[0].header.owner!=&actor || bank[0].header.command!=117 || bank[0].header.state!=1 ||
            bank[0].task.tasks.table!=&program.table)return 0;
        u32 send[]={107|(5u<<13),0,0,(u32)-7,0x80000000,0x12345678,(u32)-9,32,0};
        dispatch=(ScriptNode){.current.script=send,.active=1,.flags=0x80};g_ScriptNodePool=&dispatch;Script_RunQueue();
        if(bank[0].task.tasks.argument!=42 || dispatch.current.script!=send+9 || dispatch.flags!=0x90 || g_ScriptNodePool)return 0;
        if(Pm_SendCmd(0,0,7,(intptr_t)&task_pointer_value,(intptr_t)&task_pointer_value+sizeof(int),-9)!=0)return 0;
        g_FieldFxTaskContext=&bank[0].task.tasks;
        if(FieldFx_SpawnTask(0)!=(int)sizeof(NativeTaskState))return 0;
        g_ViewMatrixSlot.value=&identity;
        if(Pm_Start(0)!=0 || Pm_Exec(0)!=0 || task_pointer_value!=11)return 0;
        bank[0].header.field02=0xa5;bank[0].header.field03=0xd2;bank[0].header.ticks=0x89abcdef;
        u32 query[]={108|(5u<<13),0,0,0,99,99,99,32,0};
        dispatch=(ScriptNode){.current.script=query,.active=1,.flags=0x80};g_ScriptNodePool=&dispatch;Script_RunQueue();
        if(query[4]!=bank[0].header.field02 || query[5]!=bank[0].header.field03 || query[6]!=bank[0].header.ticks ||
           dispatch.current.script!=query+9 || dispatch.flags!=0x90 || g_ScriptNodePool)return 0;
        u32 states[]={109|(2u<<13),0,0,4,110|(2u<<13),0,0,99,32,0};
        dispatch=(ScriptNode){.current.script=states,.active=1,.flags=0x80};g_ScriptNodePool=&dispatch;Script_RunQueue();
        if(states[7]!=4 || bank[0].header.state!=4 || dispatch.current.script!=states+10 || dispatch.flags!=0x90 || g_ScriptNodePool)return 0;
        if(Pm_Stop(0,&actor,0)!=0 || bank[0].header.state)return 0;
        FieldActor peer={.type_id=6,.sub_id=7,.flags=0x10,.next=&actor};actor.type_id=6;actor.sub_id=7;
        g_FieldActorListHead=&peer;g_PlayerEntity=&actor;
        if(Scene_LoadRoomAssets(117,&actor)!=0 || Scene_LoadRoomAssets(117,&peer)!=1)return 0;
        secondary[3].header=(PmSlotHeader){.state=1,.command=117,.owner=&actor};
        u32 selected[]={152|(2u<<13),0,6,7,32,0};
        dispatch=(ScriptNode){.current.script=selected,.active=1,.flags=0x80};g_ScriptNodePool=&dispatch;Script_RunQueue();
        if(bank[0].header.state || bank[1].header.state!=1 || secondary[3].header.state || dispatch.current.script!=selected+6 || dispatch.flags!=0x90 || g_ScriptNodePool)return 0;
        u32 stop[]={151|(1u<<13),0,1,32,0};
        dispatch=(ScriptNode){.current.script=stop,.active=1,.flags=0x80};g_ScriptNodePool=&dispatch;Script_RunQueue();
        if(bank[1].header.state!=1 || dispatch.current.script!=stop+5 || dispatch.flags!=0x90 || g_ScriptNodePool)return 0;
        g_CurrentEntity=&peer;
        dispatch=(ScriptNode){.current.script=stop,.active=1,.flags=0x80};g_ScriptNodePool=&dispatch;Script_RunQueue();
        if(bank[1].header.state || dispatch.current.script!=stop+5 || dispatch.flags!=0x90 || g_ScriptNodePool)return 0;
        if(Scene_LoadRoomAssets(117,&actor)!=0 || Scene_LoadRoomAssets(117,&peer)!=1)return 0;
        secondary[4].header=(PmSlotHeader){.state=1,.command=117,.owner=&peer};
        selected[3]=99;dispatch=(ScriptNode){.current.script=selected,.active=1,.flags=0x80};g_ScriptNodePool=&dispatch;Script_RunQueue();
        if(bank[0].header.state!=1 || bank[1].header.state!=1)return 0;
        selected[2]=0;dispatch=(ScriptNode){.current.script=selected,.active=1,.flags=0x80};g_ScriptNodePool=&dispatch;Script_RunQueue();
        if(bank[0].header.state || bank[1].header.state!=1 || secondary[4].header.state!=1)return 0;
        u32 reset[]={153,0,32,0};
        dispatch=(ScriptNode){.current.script=reset,.active=1,.flags=0x80};g_ScriptNodePool=&dispatch;Script_RunQueue();
        for(unsigned i=0;i<11;++i)if(bank[i].header.state || secondary[i].header.state)return 0;
        if(dispatch.current.script!=reset+4 || dispatch.flags!=0x90 || g_ScriptNodePool)return 0;
        g_CurrentEntity=&actor;g_PlayerEntity=old_player;g_FieldActorListHead=old_head;
        puts("Original effect cleanup opcodes 151/152/153: current/selected/player ownership, dead/missing subtype filtering and both-bank reset passed through dispatcher");
        memset(bank,0,sizeof(bank));g_FieldFxTaskPrograms[32]=NULL;
        if(Scene_LoadRoomAssets(117,NULL)!=-8 || bank[0].header.state ||
            Scene_LoadRoomAssets(191,NULL)!=-8 || bank[0].header.state ||
            FieldFx_BindTaskProgram(owner,-1)!=-1 || FieldFx_BindTaskProgram(owner,104)!=-1)return 0;
        g_ViewMatrixSlot.value=saved_view;g_PmTables=old_tables;g_GameState=old_game;g_GameStateFlags=old_flags;
        g_CurrentEntity=old_actor;g_ScriptNodePool=old_node;g_ScriptCursor=old_cursor;
    }
    g_FieldFxTaskPrograms[32]=old_program;
    FieldFx_InitTaskContext((PmSlotHeader *)owner);owner->tasks.table=&table;owner->tasks.argument=42;
    u16 bad_scripts[][6]={{1,7,99,0,0,0},{1,(u16)-1,99,0,0,0},{4,7,1,0,0,0},{3,0,7,0,0,0}};
    for(unsigned invalid=0;invalid<sizeof(bad_scripts)/sizeof(bad_scripts[0]);++invalid) {
        FieldFx_InitTaskContext((PmSlotHeader *)owner); owner->tasks.pc.script=bad_scripts[invalid];
        if(FieldFx_UpdateTasks((PmSlotHeader *)owner)!=-1 || owner->tasks.count || owner->tasks.used) return 0;
    }
    u16 decrement[]={3,0,0,0,0,0};
    FieldFx_InitTaskContext((PmSlotHeader *)owner); owner->tasks.pc.script=decrement; owner->tasks.variables[0]=2;
    if(FieldFx_UpdateTasks((PmSlotHeader *)owner)!=-1 || owner->tasks.variables[0]!=1 || owner->tasks.pc.script!=decrement+3) return 0;
    owner->tasks.pc.script=decrement; owner->tasks.variables[0]=1;
    if(FieldFx_UpdateTasks((PmSlotHeader *)owner)!=0 || owner->tasks.variables[0] || owner->tasks.pc.script!=decrement+3) return 0;
    FieldFx_InitTaskContext((PmSlotHeader *)owner); g_FieldFxTaskContext=&owner->tasks;
    for(unsigned slot=0;slot<8;++slot)
        if(FieldFx_SpawnTask(0)!=(int)sizeof(NativeTaskState)) return 0;
    if(owner->tasks.count!=8 || FieldFx_SpawnTask(0)!=-1 || FieldFx_SpawnTask(8)!=-1 || FieldFx_SpawnTask(-1)!=-1)
        return 0;
    FieldFx_InitTaskContext((PmSlotHeader *)owner);
    table.sizes[0]=sizeof(owner->tasks.arena)+1;
    if(FieldFx_SpawnTask(0)!=-1 || owner->tasks.count || owner->tasks.used) return 0;
    table.sizes[0]=sizeof(NativeTaskState);
    owner->tasks.used=sizeof(owner->tasks.arena)-1;
    owner->tasks.cursor=owner->tasks.arena+owner->tasks.used;
    if(FieldFx_SpawnTask(0)!=(int)sizeof(NativeTaskState) || owner->tasks.used!=sizeof(NativeTaskState) ||
       owner->tasks.slots[0].start!=owner->tasks.arena) return 0;
    FieldFx_InitTaskContext((PmSlotHeader *)owner);
    owner->tasks.used=1; owner->tasks.cursor=owner->tasks.arena+1;
    int padding=(int)_Alignof(void *)-1;
    if(FieldFx_SpawnTask(0)!=(int)sizeof(NativeTaskState)+padding ||
       owner->tasks.slots[0].start!=owner->tasks.arena+_Alignof(void *)) return 0;
    FieldFx_InitTaskContext((PmSlotHeader *)owner);
    table.callbacks[0]=reject_task_allocation;
    if(FieldFx_SpawnTask(0)!=-1 || owner->tasks.count || owner->tasks.used || owner->tasks.cursor!=owner->tasks.arena ||
       owner->tasks.slots[0].id!=0xffff) return 0;
    /* Retail's NULL reads see the nonzero low-memory byte observed after
     * boot. Compare owner/core state; native code never signals a missing event. */
    FieldActor idle_actor={0};FieldActorState idle_state={0};idle_actor.state=&idle_state;
    void *saved_archive=g_GameState.loaded_scene_archive;int saved_palette=g_FieldFxAltPaletteFlag;
    struct {SceneAssetBlob header;SceneAssetDirectory directory;} idle_archive={0};
    idle_archive.header.directoryOffset=sizeof(SceneAssetBlob);g_GameState.loaded_scene_archive=&idle_archive.header;
    u16 idle_scripts[][6]={{4,0,1},{5,1,0,4,0,1},{0,0,0}};
    const u32 held_flags[]={0,0x1000000,0x2000000,0x3000000,0x1000002,0x200180e};
    const int health[]={-1,0,1},idle_modes[]={0,1,4};uint64_t idle_hash=UINT64_C(14695981039346656037);
    for(unsigned script_id=0;script_id<3;++script_id)for(unsigned held=0;held<6;++held)
    for(unsigned hp=0;hp<3;++hp)for(unsigned mode=0;mode<3;++mode)for(unsigned phase=0;phase<6;++phase){
        FieldFx_InitTaskContext((PmSlotHeader *)owner);owner->prefix.actor=&idle_actor;owner->prefix.asset_type=0;
        owner->prefix.reserved00=0;owner->tasks.flags=1;owner->tasks.pc.script=idle_scripts[script_id];
        idle_state=(FieldActorState){0};idle_state.core_flags=held_flags[held];idle_state.control10.command_value=health[hp];
        u8 event_phase=phase?phase-1:0;idle_state.action_state=phase?&event_phase:NULL;idle_actor.mode=idle_modes[mode];
        u32 result=FieldFx_UpdateTasks((PmSlotHeader *)owner);
        const u32 words[]={result,owner->prefix.reserved00,idle_state.core_flags,owner->tasks.flags,
            (u32)((u8 *)owner->tasks.pc.script-(u8 *)idle_scripts[script_id]),idle_state.action_state!=NULL,
            idle_state.action_state?*idle_state.action_state:0};
        for(unsigned i=0;i<7;++i)for(unsigned b=0;b<4;++b)
            idle_hash=(idle_hash^((words[i]>>(b*8))&255))*UINT64_C(1099511628211);
    }
    /* An actor can outlive its detached combat core during death/escape.
     * Retail writes the held word through zero in this case. Keep observable
     * task state and visual lifetime, without emulating low-memory writes. */
    u16 detached_scripts[][9]={{5,0,0,4,0,1},{5,1,0,4,0,1},{0,0,0},{5,0,0,4,1,0,0,0,0}};
    u64 detached_hash=UINT64_C(14695981039346656037);unsigned detached_cases=0;
    for(unsigned script_id=0;script_id<4;++script_id)for(unsigned held=0;held<2;++held)
    for(unsigned mode=0;mode<3;++mode)for(unsigned missing=0;missing<3;++missing){
        FieldFx_InitTaskContext((PmSlotHeader *)owner);owner->prefix.reserved00=0;
        owner->prefix.actor=missing==1?NULL:&idle_actor;owner->prefix.asset_type=0;
        owner->tasks.flags=held;owner->tasks.pc.script=detached_scripts[script_id];
        idle_actor.mode=idle_modes[mode];idle_actor.state=NULL;
        idle_state.core_flags=0xa5a55a5a;u8 orphan_event=1;idle_state.action_state=&orphan_event;
        for(unsigned frame=0;frame<3;++frame){
            u32 result=FieldFx_UpdateTasks((PmSlotHeader *)owner);
            const u32 words[]={result,owner->prefix.reserved00,owner->tasks.flags,
                (u32)((u8 *)owner->tasks.pc.script-(u8 *)detached_scripts[script_id]),owner->tasks.delay};
            for(unsigned i=0;i<5;++i)for(unsigned byte=0;byte<4;++byte)
                detached_hash=(detached_hash^((words[i]>>(byte*8))&255))*UINT64_C(1099511628211);
            if(idle_state.core_flags!=0xa5a55a5a || orphan_event!=1)return 0;
            ++detached_cases;
        }
    }
    if(detached_cases!=216 || detached_hash!=UINT64_C(0x2acb79b6be8b0f35)){
        fprintf(stderr,"Detached task owner CPU hash: %u %016llx\n",detached_cases,(unsigned long long)detached_hash);return 0;}
    puts("Original detached task owners: 216 USA CPU observable held/wait/stop states, absent actor/core, preserved flags and untouched orphan state passed");
    u16 detached_visual[]={5,0,0,2,0,0,4,1,0,0,0,0};
    for(unsigned missing=0;missing<3;++missing){
        FieldFx_InitTaskContext((PmSlotHeader *)owner);owner->prefix.reserved00=0;
        owner->prefix.actor=missing==1?NULL:&idle_actor;idle_actor.state=missing==2?&idle_state:NULL;
        idle_state=(FieldActorState){0};u8 event=1;idle_state.action_state=&event;
        owner->tasks.pc.script=detached_visual;owner->tasks.argument=42;
        table.callbacks[0]=observe_native_task;table.sizes[0]=sizeof(NativeTaskState);
        unsigned draws=task_draws;g_ViewMatrixSlot.value=&identity;
        if(FieldFx_UpdateTasks((PmSlotHeader *)owner)!=0 || owner->tasks.count!=1 || owner->tasks.flags!=1 ||
           FieldFx_DrawTasks((PmSlotHeader *)owner)!=0 || task_draws!=draws+1)return 0;
        idle_actor.state=NULL;u32 orphan_flags=idle_state.core_flags;u8 old_event=event;
        if(FieldFx_UpdateTasks((PmSlotHeader *)owner)!=0 || owner->tasks.count || owner->tasks.flags!=1 ||
           FieldFx_UpdateTasks((PmSlotHeader *)owner)!=0 || owner->prefix.reserved00!=4 || owner->tasks.flags!=1 ||
           idle_state.core_flags!=orphan_flags || event!=old_event)return 0;
    }
    g_ViewMatrixSlot.value=saved_view;
    puts("Detached visual tasks: init, draw, owner/core detachment, natural child expiry and original stop preserve the held palette state");
    g_GameState.loaded_scene_archive=saved_archive;g_FieldFxAltPaletteFlag=saved_palette;
    if(idle_hash!=UINT64_C(0x15a8baf2abf03bd9)){fprintf(stderr,"Idle event task hash %016llx\n",(unsigned long long)idle_hash);return 0;}
    puts("Original task event lifetime: 972 USA CPU cases, absent/0..4 event phases, hold/stop/signal, HP and actor-mode gates passed without low-memory writes");
    for(unsigned byte=0;byte<sizeof(storage.guard);++byte) if(storage.guard[byte]!=0xa5) return 0;
    g_FieldFxTaskContext=NULL; g_FieldFxActiveTaskSlot=NULL; g_FieldFxOwner=NULL;
    puts("Original effect tasks: init, script spawn/wait/stop, update/draw/completion, eight-slot limit, native alignment/wrap and allocation failure passed");
    return 1;
}

static int canonical_render_buffers(void)
{
    RenderBufferPrefix saved=D_800B0E38;
    static RenderGpuTag tags[2];
    static char bytes[8];
    for(unsigned i=0;i<2;++i) {
        g_GameState.shared_buffers.ordering_tables[i]=(unsigned char *)&tags[i];
        if(D_800B0E38.ordering[i]!=&tags[i]) return 0;
    }
    g_GameState.texture_load_scratch=(union SceneAssetView *)&bytes[0];
    g_GameState.object_texture_blob=(struct SceneAssetBlob *)&bytes[1];
    g_GameState.unk_170=(u8 *)&bytes[2];
    g_GameState.bg_tim=(struct TimFile *)&bytes[3];
    g_GameState.save_background_source=(unsigned short *)&bytes[4];
    g_GameState.save_background_destination=(unsigned short *)&bytes[5];
    for(unsigned i=0;i<6;++i) if(D_800B0E38.other_buffers[i]!=&bytes[i]) return 0;
    D_800B0E38.packets[0]=&bytes[6]; D_800B0E38.packets[1]=&bytes[7];
    if((void *)g_GameState.hud_tim!=&bytes[6] || g_GameState.script_sprite_buffer!=(u8 *)&bytes[7]) return 0;
    D_800B0E38=saved;
    puts("Original render buffer symbol: canonical game state aliases all ten native slots");
    return 1;
}

static int native_effect_packet_links(void)
{
    struct { RenderTintMode mode; u8 guard[16]; } buffer;
    RenderTintMode *modes=&buffer.mode;
    RenderBufferPrefix saved=D_800B0E38;
    int saved_slot=g_ActiveDrawSlot, saved_offset=g_DrawPacketOffset;
    for(unsigned slot=0;slot<2;++slot) for(int mode=0;mode<4;++mode) {
        memset(&buffer,0xa5,sizeof(buffer));
        RenderGpuTag ordering={.address=(uintptr_t)0xffffff,.length=0};
        FieldTilePoint packet={0}; SetTile1(&packet);
        packet.r=128; packet.g=64; packet.b=32; packet.x=7; packet.y=9;
        D_800B0E38.packets[slot]=(char *)modes;
        g_ActiveDrawSlot=slot; g_DrawPacketOffset=0;
        FieldFx_LinkBlendedPacket(&ordering,&packet,mode);
        if(ordering.address!=(uintptr_t)modes || modes->tag.address!=(uintptr_t)&packet ||
           packet.tag.address!=0xffffff || modes->tag.length!=1 || packet.tag.length!=2 ||
           packet.command!=0x6a || g_DrawPacketOffset!=sizeof(RenderTintMode)) return 0;
        if(modes->command!=(u32)(0xe1000200|GetTPage(0,mode,0,0))) return 0;
        for(unsigned i=0;i<sizeof(buffer.guard);++i) if(buffer.guard[i]!=0xa5) return 0;
        ordering.address=0xffffff; g_DrawPacketOffset=0;
        FieldFx_LinkBlendedPacket(&ordering,&packet,255);
        if(ordering.address!=(uintptr_t)&packet || packet.tag.address!=0xffffff || g_DrawPacketOffset) return 0;
    }
    D_800B0E38=saved; g_ActiveDrawSlot=saved_slot; g_DrawPacketOffset=saved_offset;
    puts("Original effect packets: full native links, both slots, four blend modes, payload offsets and arena guards passed");
    return 1;
}

