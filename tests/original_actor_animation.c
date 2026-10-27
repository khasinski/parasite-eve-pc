#include "pe1/room_homing_flash.h"
#include "../src/overlays/scene_e08_chrysler/scene_e08_modules.h"
#include "pe1/scene_e20_hover_orb.h"
#include "pe1/scene_e20_flare_draw.h"
#include "pe1/room_spark_ring_burst.h"
#include "../src/overlays/room_m273_liberty/room_m273.h"
#include "../src/overlays/room_m273_liberty/room_m273_boss_state.h"
#include "../src/overlays/room_m273_liberty/room_m273_sweep.h"
#include "../src/overlays/room_m273_liberty/room_m273_sway.h"
#include "pe1/room_orbit_set.h"
#include "pe1/room_spark_barrage.h"
#include "pe1/room_twelve_effect.h"
#include "pe1/room_grab_attack.h"
#include "pe1/room_sound_burst.h"
#include "pe1/room_trail_burst.h"
#include "pe1/room_ground_eruption.h"
#include "pe1/room_spark_attacks.h"
#include "pe1/room_homing_model.h"
#include "pe1/room_falling_burst.h"
#include "../src/overlays/room_m318_chrysler/m318_externs.h"
#include "pe1/room_ember_burst.h"
#include "pe1/room_ember_spiral.h"
#include "pe1/room_shake_burst.h"
#include "pe1/room_twin_ribbon.h"
#include "pe1/field_orbit_point.h"
#include "pe1/room_m123.h"
#include "pe1/room_m349_effects.h"
#include "pe1/room_m404.h"
#include "pe1/room_m256.h"
#include "pe1/room_m256_drop.h"
#include "pe1/room_m256_effects.h"
#include "pe1/room_m256_swing.h"
#include "pe1/room_model_pulse.h"
#include "pe1/room_orbiting_pair.h"
#include "pe1/room_m404_drop.h"
#include "pe1/room_m404_effects.h"
#include "../src/overlays/room_m350_cruiser/room_m350_data.h"
#include "pe1/room_m350_sweep_trap.h"
#include "pe1/scene_room_load.h"
#include "pe1/room_m123_beacon.h"
#include "pe1/room_m123_glow_burst.h"
#include "pe1/room_paired_glow.h"
#include "pe1/room_line_burst.h"
#include "pe1/room_debris_triplet.h"
#include "pe1/room_m086_seeker.h"
#include "pe1/room_flare.h"
#include "pe1/room_m089_model_pulse.h"
#include "pe1/room_m089_trail.h"
#include "pe1/room_three_particle_class.h"
#include "pe1/menu_item_record.h"
#include "../src/overlays/fx_common/camera_externs.h"
#include "../src/overlays/fx_common/fx_common_model.h"
#include "../src/overlays/fx_common/fx_common_motion.h"
#include <signal.h>
#include "../src/overlays/menu_memcard/menu_memcard_link.h"
#include "../src/overlays/menu_memcard/menu_memcard_trail.h"
#include "pe1/akao/pan3d.h"
#include "pe1/akao/voice_masks.h"
#include "pe1/akao/note_step.h"
#include "pe1/akao/voice_state.h"
#include "../src/overlays/menu_memcard/menu_memcard_glint.h"
#include "../src/overlays/menu_memcard/menu_memcard_video.h"
#include "pe1/resident_tables.h"
#include "pe1/draw_area.h"
#include "pe1/textbox.h"
#include "pe1/textbox_open.h"
#include "pe1/menu_flags.h"
#include "pe1/menu_sound.h"
#include "pe1/draw_wipe_rect.h"
#include "pe1/memcard.h"
#include "pe1/reloc_block.h"
#include "pe1/inventory_sort.h"
#include "pe1/memcard_save_state.h"
#ifdef PE_MODS
#include "../mods/host/pe_mod_host.h"
#endif

#include "actor_animation/test_fixtures_and_core.inc"
#include "actor_animation/test_fieldfx_and_glow.inc"
#include "actor_animation/test_script_opcodes.inc"
#include "actor_animation/test_room_overlays.inc"
#include "actor_animation/test_render_and_fx_common.inc"
#include "actor_animation/test_combat_media_and_spu.inc"
#include "actor_animation/test_native_replays.inc"
#include "actor_animation/test_hospital_and_chrysler.inc"

int main(int argc, char **argv)
{
#include "actor_animation/test_cli_modes.inc"
    if(!original_seeker_particle_lifetimes() || !original_m123_pulse_contact() || !original_paired_glow_states() || !original_paired_glow_program_flow() || !original_line_burst_states() || !original_dropped_flare_states() || !original_m089_states() || !original_m086_states() || !original_motion_states() || !original_motion_program() || !original_window_lifecycle() || !original_window_motion() || !original_room_e_init() || !original_room_e_update() || !original_effect_sequence_updates() || !original_effect_sequence_initializers() || !room_callback_context() || !native_room_actor_class_table() || !original_room_arc_offsets() || !original_contact_action_callback() || !original_room_d_update() || !original_room_d_motion() || !original_m023_drift_states() || !room_actor_view() || !room_owner_state() || !callback_list_storage() || !native_task_cycle() || !canonical_render_buffers() || !native_effect_packet_links() || !native_screen_tint_packets() || !native_effect_model_packets() || !native_oriented_sprite_packets() || !native_ring_packets() || !native_effect_collision_coordinates() || !original_glow_resident_constants() || !native_glow_layer_packets() || !native_object_script_cycle() || !authored_glow_script_cycle() || !authored_m005_effect_cycle() || !authored_orbit_flare_cycle() || !original_actor_render_opcodes() || !original_actor_camera_opcodes() || !original_mode_wait_opcode() || !original_tracking_angle_opcode() || !original_absolute_movement_opcode() || !original_resident_script_program() || !original_actor_audio_state() || !original_burst_target_selection() || !native_effect_spawn_arguments() || !native_object_slot_bounds() || !process_static_task_programs() || !process_texture_bank_failures() || !native_actor_effect_gates()) return 123;
    (void)argv;
    if (!original_combat_timers() || !original_combat_angles() || !original_player_turn() || !original_enemy_damage() || !original_enemy_facing() || !original_enemy_death()) return 141;
    if (!original_startup_fade()) return 126;
    if (!original_save_metadata_storage()) return 125;
    if (!original_frame_random()) return 128;
    if (!original_item_random()) return 124;
    if (!bios_random()) return 58;
    if (!glyph_records()) return 55;
    if (!glyph_selection()) return 56;
    if (!boot_buffer_layout()) return 54;
    if (!game_state_reset()) return 53;
    if(!SDL_Init(SDL_INIT_EVENTS))return 43;
    if(!script_movie_error_exit(argv[0]))return 142;
    if(!original_system_start())return 130;
    if(!original_display_start())return 127;
    if(!original_menu_draw_areas())return 131;
    if(!original_digit_packets() || !original_color_quad_packets())return 132;
    if(!original_m123_counters())return 158;
    if(!original_subsystem_start())return 129;
    if(!original_native_shared_storage())return 202;
    const u32 lzcs[]={0,1,0x7fffffff,0x80000000,0xffffffff};
    const u32 lzcr[]={32,31,1,1,32};
    for(unsigned i=0;i<5;++i) {
        Psyz_GteDataWrite(30,lzcs[i]);
        if(Psyz_GteDataRead(30)!=lzcs[i] || Psyz_GteDataRead(31)!=lzcr[i])return 33;
        Psyz_GteDataWrite(31,123);
        if(Psyz_GteDataRead(31)!=lzcr[i])return 34;
    }
    if(argc>1) {
        const char *path=getenv("PE_TEST_DISC");
        if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe;u32 pointers[50];
        if(!pe_disc_open(&disc,path) || !pe_disc_find(&disc,"SLUS_006.62",&exe) ||
           !pe_disc_read(&disc,&exe,0x943c0-0x10000+0x800,pointers,sizeof(pointers)))return 20;
        if (!original_paired_glow_assets(&disc,&exe) || !original_line_burst_assets(&disc,&exe) || !original_dropped_flare_assets(&disc,&exe) || !original_m089_assets(&disc,&exe) || !original_m086_assets(&disc,&exe) || !original_three_particle_assets(&disc,&exe) || !original_motion_assets(&disc,&exe) || !original_window_assets(&disc,&exe) || !original_scene_object_workspace_assets(&disc,&exe) || !original_effect_sequence_assets(&disc,&exe) || !original_room_actor_assets(&disc,&exe) || !original_m023_assets(&disc,&exe) || !original_m028_assets(&disc,&exe))return 157;
        if (!original_m256_assets(&disc, &exe) || !original_m404_assets(&disc, &exe) || !original_m123_assets(&disc, &exe) || !original_m349_assets(&disc, &exe) || !original_m350_assets(&disc, &exe)) return 159;
        if (!glyph_asset_records(&disc, &exe)) return 57;
        if (!original_fx_resources(&disc,&exe)) return 144;
        if (!original_icon_packets(&disc,&exe)) return 133;
        if (!original_task_bank_data(&disc) || !original_glint_particles(&disc) || !original_positional_audio_projection() || !original_glint_lifecycle()) return 141;
        if (!original_video_buffers(&disc)) return 135;
        if (!original_video_decoder(&disc)) return 136;
        if (!original_video_stream_errors()) return 137;
        if (!original_movie_uploads(&disc) || !original_script_movie(&disc)) return 138;
        if (!original_akao_resident_tables(&disc,&exe) || !original_image_display_switch(&disc,&exe) || !original_boot_display_data(&disc) || !original_menu_sound_commands(&disc,&exe) || !original_enemy_slots(&disc,&exe)) return 140;
        if (!original_title_screen(&disc)) return 134;
        for(unsigned mode=0;mode<50;++mode) {
            SceneFlagRecord *native=g_EntityInputRulesByMode[mode];
            if(!pointers[mode]) {if(native)return 21;continue;}
            if(!native)return 22;
            for(unsigned i=0;i<4;++i) {
                u32 raw[4];
                if(!pe_disc_read(&disc,&exe,pointers[mode]-0x80010000+0x800+i*16,raw,sizeof(raw)))return 23;
                void (*callback)(BattleEntity *,int *)=0;
                switch(raw[3]) {
                case 0:break;
                case 0x80071034:callback=Scene_Init;break;
                case 0x800710a4:callback=Scene_UpdatePlayerEntity;break;
                case 0x8007136c:callback=Scene_UpdateEntityFacing;break;
                case 0x800716a4:callback=Scene_SyncEntityAction;break;
                case 0x80071754:callback=Scene_UpdateEntityFacingFromPad;break;
                default:return 24;
                }
                if(native[i].flags!=raw[0] || native[i].mask_all!=raw[1] ||
                   native[i].mask_any!=raw[2] || native[i].callback!=callback)return 25;
                if(!raw[0])break;
                if(i==3)return 26;
            }
        }
        pe_disc_close(&disc);
        puts("All 50 original input table slots match executable records");
    }
    BattleEntity parent={0},child={0};
    RenderAnimationDataHeader actions[2]={{0}};
    actions[0].frame_count=4;actions[1].frame_count=8;
    for (unsigned i=0;i<2;++i) {
        g_GameState.bank_rows[i][0]=actions;g_GameState.bank_rows[i][1]=actions+1;
    }
    parent.next=&child;child.parent=&parent;child.modelIndex=1;
    g_FieldActorListHead=(FieldActor *)&parent;
    parent.entityFlags=0x100000;child.entityFlags=0x200000;
    Entity_SetActionMode(&parent,1);
    if(parent.animLastFrame!=7 || child.animLastFrame!=7 || child.actionMode!=1 || parent.actionData!=actions+1) return 1;
    parent.animFrame=0x18000;Entity_SetAction(&parent,1);
    if(parent.animFrame!=0x18000) return 2;
    parent.animStep=0x10000;Entity_AdvanceAnim(&child);
    if(parent.animFrame!=0x28000 || child.animFrame!=parent.animFrame || !(parent.entityFlags&0x800000)) return 3;
    parent.entityFlags=0;parent.animFrame=7<<16;Entity_AdvanceAnim(&parent);
    if(parent.animFrame || !(parent.entityFlags&8)) return 4;
    parent.entityFlags=0;parent.animStep=-0x10000;Entity_AdvanceAnim(&parent);
    if(parent.animFrame!=(7<<16) || !(parent.entityFlags&8)) return 5;
    parent.entityFlags=0x200;parent.animFrame=0;parent.animStep=0x30000;parent.animStopFrame=2;
    Entity_AdvanceAnim(&parent);
    if(parent.animFrame!=(2<<16)) return 6;
    parent.entityFlags=0;parent.actionMode=1;parent.animFrame=2<<16;parent.animPrev.fixed=0;parent.animStep=0x10000;
    g_PlayerEntity=(FieldActor *)&parent;
    g_AkaoVoiceBankEntries[0].index=1;g_AkaoVoiceBankEntries[0].frame=1;g_AkaoVoiceBankEntries[0].ids[0]=1000;
    Entity_AdvanceAnim(&parent);
    if(sounds!=1) return 7;
    g_PlayerEntity=0;
    g_GameState.bank_state_11=1;g_GameState.bank_state_12=1;
    g_AkaoVoiceBankEntries[4].group=parent.modelIndex;
    g_AkaoVoiceBankEntries[4].set=parent.teamId;
    g_AkaoVoiceBankEntries[4].index=parent.actionMode;
    g_AkaoVoiceBankEntries[4].frame=1;g_AkaoVoiceBankEntries[4].ids[1]=1000;
    parent.entityFlags=0;parent.animFrame=0;parent.animPrev.fixed=3<<16;parent.animStep=-0x10000;
    Entity_AdvanceAnim(&parent);
    if(sounds!=2) return 8;
    g_GameState.bank_state_11=0;g_GameState.bank_state_12=0;g_FieldActorListHead=0;
    {
        BattleEntity actor={0}; Combatant core={0};
        RenderAnimationDataHeader action={0};action.frame_count=8;
        for(unsigned mode=0;mode<24;++mode) g_GameState.bank_rows[0][mode]=&action;
        actor.core=&core;actor.moveFactor=0xCCC0;actor.moveSpeed=4096;
        g_PlayerEntity=(FieldActor *)&actor;
        g_GeomScroll.state.position.cameraBasis.m[0][0]=4096;
        g_GeomScroll.state.position.cameraBasis.m[1][1]=4096;
        g_GeomScroll.state.position.cameraBasis.m[2][2]=4096;
        const unsigned pads[]={FIELD_PAD_UP,FIELD_PAD_LEFT,FIELD_PAD_DOWN,FIELD_PAD_RIGHT};
        const int dx[]={0,-1,0,1},dz[]={-1,0,1,0};
        for(unsigned run=0;run<2;++run) for(unsigned dir=0;dir<4;++dir) {
            int state=-1, speed=run?FIELD_RUN_SPEED:FIELD_WALK_SPEED;
            g_FieldPadBits=pads[dir];
            if(run)Scene_UpdateEntityFacing(&actor,&state);
            else Scene_UpdatePlayerEntity(&actor,&state);
            if(actor.motionX!=dx[dir]*speed || actor.motionZ!=dz[dir]*speed || actor.motionY ||
               actor.facingAngle!=dir*1024 || actor.actionMode!=(run?23:22) ||
               actor.actionData!=&action) return 9;
        }
        int state=22;g_FieldPadBits=0;
        Scene_UpdatePlayerEntity(&actor,&state);
        if(actor.motionX || actor.motionY || actor.motionZ) return 10;
        g_FieldPadBits=FIELD_PAD_LEFT;g_FieldMoveLock=FIELD_MOVE_CONFUSED;
        Scene_UpdatePlayerEntity(&actor,&state);
        if(actor.motionX!=FIELD_WALK_SPEED || actor.facingAngle!=1024) return 11;
        g_FieldMoveLock=0;
        g_PadPackets[0].header=0x7000;
        const unsigned ax[]={0,128,255,128},ay[]={128,0,128,255};
        const int mx[]={-1,0,1,0},mz[]={0,1,0,-1};
        for(unsigned dir=0;dir<4;++dir) {
            g_PadPackets[0].leftX=ax[dir];g_PadPackets[0].leftY=ay[dir];
            Scene_UpdatePlayerEntity(&actor,&state);
            if(actor.motionX!=mx[dir]*FIELD_WALK_SPEED || actor.motionZ!=mz[dir]*FIELD_WALK_SPEED) return 12;
        }
        Scene_Init(&actor,&state);
        if(actor.actionMode!=21 || actor.motionX || actor.motionZ) return 13;
        g_PadPackets[0].header=0;
        actor.posX.fixed=actor.posY.fixed=actor.posZ.fixed=0;
        Boot_BuildRenderFlagTable();
        Boot_ResetPadInput();
        if(!host_event(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_S))return 44;
        Field_HandleStateTransition();
        if(g_FieldPadBits!=FIELD_PAD_UP || g_FieldPadPressed!=FIELD_PAD_UP){fprintf(stderr,"pad %x pressed %x flags %x\n",g_FieldPadBits,g_FieldPadPressed,g_GameStateFlags);return 30;}
        Entity_IntegratePositionFull((FieldActor *)&actor);
        if(actor.posZ.fixed!=-FIELD_WALK_SPEED || actor.actionMode!=22)return 14;
        if(!host_event(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_LSHIFT))return 45;
        Field_HandleStateTransition();
        if(!(g_FieldPadBits&1) || !(g_FieldPadPressed&1))return 31;
        Entity_IntegratePositionFull((FieldActor *)&actor);
        if(actor.posZ.fixed!=-FIELD_WALK_SPEED-FIELD_RUN_SPEED || actor.actionMode!=23)return 15;
        if(!host_event(SDL_EVENT_KEY_UP,SDL_SCANCODE_S) ||
           !host_event(SDL_EVENT_KEY_UP,SDL_SCANCODE_LSHIFT))return 46;
        Field_HandleStateTransition();
        if(g_FieldPadBits || !(g_FieldPadReleased&FIELD_PAD_UP))return 32;
        Entity_IntegratePositionFull((FieldActor *)&actor);
        if(actor.posZ.fixed!=-FIELD_WALK_SPEED-FIELD_RUN_SPEED || actor.actionMode!=21 || actor.motionZ)return 16;
        g_PadPackets[0].header=0x7000;
        g_PadPackets[0].leftX=89;g_PadPackets[0].leftY=128;
        Field_HandleStateTransition();
        if(g_FieldPadBits!=FIELD_PAD_LEFT)return 35;
        Field_HandleStateTransition();
        if(g_FieldPadPressed || g_FieldPadReleased)return 36;
        g_PadPackets[0].leftX=19;Field_HandleStateTransition();
        if(g_FieldPadBits!=(FIELD_PAD_LEFT|1))return 37;
        MenuWidget_InitPool();
        for(unsigned descriptor=0;descriptor<0x41;++descriptor) {
            MenuWidgetNode *reused=g_MenuWidgetFreeListHead;
            reused->appearance.gradientPoints=(u8 *)&g_MenuWidgetNodePool[23];
            MenuWidgetNode *panel=MenuWidget_CreateSimpleNode(descriptor,NULL,NULL,1);
            MenuWidgetSimpleDescriptor *desc=MenuWidget_LookupSimpleDescriptor(descriptor);
            if(panel!=reused || panel->appearance.gradientPoints || panel->mode!=1 ||
               panel->grid_width!=desc->width || panel->visible_rows!=desc->height) {
                fprintf(stderr,"Reused simple panel %u retained gradient pointer %p\n",descriptor,(void *)panel->appearance.gradientPoints);
                return 124;
            }
            MenuWidget_DestroyNodeRecursive(panel);
        }
        puts("Original simple panels: all 65 descriptors clear the full native gradient pointer on node reuse");
        MenuWidget_InitPool();
        for (unsigned slot=0; slot<24; ++slot) {
            MenuWidgetNode *allocated=MenuWidget_AllocNode(slot?&g_MenuWidgetNodePool[0]:NULL,
                                                        slot>0 && slot<5?&g_MenuWidgetNodePool[0]:NULL);
            if (allocated!=&g_MenuWidgetNodePool[slot] ||
                allocated->next!=(slot?&g_MenuWidgetNodePool[slot-1]:NULL)) return 86;
            for (unsigned child=0; child<4; ++child)
                if (allocated->children[child]) return 87;
        }
        if (g_MenuWidgetFreeListHead || g_MenuWidgetActiveListHead!=&g_MenuWidgetNodePool[23]) return 88;
        for (unsigned child=0; child<4; ++child)
            if (g_MenuWidgetNodePool[0].children[child]!=&g_MenuWidgetNodePool[child+1]) return 89;
        memset(g_MenuWidgetColumnLayoutTable, 0x5a, sizeof(g_MenuWidgetColumnLayoutTable));
        Menu_InitStateBuffer();
        for (unsigned slot=0;slot<72;++slot) {
            int unsaved=slot==6 || slot==16 || slot==20 || slot==22 ||
                        slot==24 || slot==25 || slot==49 || slot==53;
            MenuWidgetSavedLayout *layout=&g_MenuWidgetColumnLayoutTable[slot];
            if (layout->cursorX!=(unsaved?-1:0) || layout->cursorY ||
                layout->scrollY || layout->reserved) return 98;
        }
        MenuWidgetNode *root=&g_MenuWidgetNodePool[0], *child=&g_MenuWidgetNodePool[1];
        child->mode=2; child->aux_index=6; child->layout_flags=0x20;
        child->cursor_x=1; child->cursor_y=9; child->scroll_y=7;
        g_MenuWidgetCurrentNode=&g_MenuWidgetNodePool[2];
        g_MenuWidgetNodePool[23].mode=2;
        g_MenuWidgetNodePool[23].linkedNext=root;
        g_MenuWidgetNodePool[23].linkedPrevious=child;
        MenuWidget_DestroyNodeRecursive(root);
        if (g_MenuWidgetCurrentNode || g_MenuWidgetNodePool[23].linkedNext ||
            g_MenuWidgetNodePool[23].linkedPrevious) return 91;
        if (g_MenuWidgetColumnLayoutTable[6].cursorX!=1 ||
            g_MenuWidgetColumnLayoutTable[6].cursorY!=9 ||
            g_MenuWidgetColumnLayoutTable[6].scrollY!=7) return 92;
        unsigned remaining=0;
        for (MenuWidgetNode *node=g_MenuWidgetActiveListHead;node;node=node->next) {
            if (node->parent || ++remaining>19) return 93;
        }
        if (remaining!=19) return 94;
        for (unsigned slot=0;slot<5;++slot)
            if (MenuWidget_AllocNode(NULL,NULL)!=&g_MenuWidgetNodePool[4-slot]) return 95;
        while (g_MenuWidgetActiveListHead) MenuWidget_DestroyNodeRecursive(g_MenuWidgetActiveListHead);
        unsigned free_mask=0;
        for (MenuWidgetNode *node=g_MenuWidgetFreeListHead;node;node=node->next) {
            unsigned slot=(unsigned)(node-g_MenuWidgetNodePool);
            if (slot>=24 || (free_mask & (1u<<slot))) return 96;
            free_mask|=1u<<slot;
        }
        if (free_mask!=0xffffff) return 97;
        MenuWidget_InitPool();
        if (g_MenuWidgetFreeListHead!=g_MenuWidgetNodePool || g_MenuWidgetNodePool[23].next) return 90;
        MenuWidgetNode draw_nodes[4]={0};
        draw_nodes[0].children[0]=&draw_nodes[1];
        draw_nodes[0].children[2]=&draw_nodes[3];
        draw_nodes[1].children[1]=&draw_nodes[2];
        for (unsigned i=0;i<4;++i) {
            draw_nodes[i].mode=i;
            draw_nodes[i].x=(int)i+1; draw_nodes[i].y=-((int)i+1);
            draw_nodes[i].draw=record_widget_draw;
        }
        g_TextCursorX=10; g_TextCursorY=20;
        g_TextCursorStackPtr=g_TextCursorStackBottom;
        for(unsigned i=0;i<12;++i)g_TextCursorStackTop[i]=0x12345678;
        Draw_PushPrimToList(draw_nodes);
        const int want_x[4]={11,13,16,15},want_y[4]={19,17,14,15};
        if(widget_draw_count!=4 || g_TextCursorX!=10 || g_TextCursorY!=20 ||
           g_TextCursorStackPtr!=g_TextCursorStackBottom)return 99;
        for(unsigned i=0;i<4;++i)
            if(widget_draw_order[i]!=(int)i || widget_draw_x[i]!=want_x[i] || widget_draw_y[i]!=want_y[i])return 100;
        for(unsigned i=0;i<12;++i)if(g_TextCursorStackTop[i]!=0x12345678)return 101;
        MenuInput_InitQueue();
        for(int i=0;i<20;++i)Queue_Enqueue(i&1?2:1,100+i);
        if(g_MenuEventQueueFreeList)return 102;
        Queue_Enqueue(1,999);
        MenuQueueEntry event;
        Queue_DequeueByMask(2,&event);
        if(event.payload.input.flags!=101)return 103;
        Queue_Enqueue(4,123);
        for(int i=0;i<20;++i) {
            Queue_DequeueByMask(-1,&event);
            int want=i==19?123:100+i+(i>0);
            if(event.payload.input.flags!=want)return 104;
        }
        if(g_MenuEventQueueHead || g_MenuEventQueueTail)return 105;
        MenuWidgetNode event_parent={0},event_child={0};
        event_parent.mode=2;event_parent.update=record_menu_event;
        event_child.mode=1;event_child.update=record_menu_event;event_child.parent=&event_parent;
        g_MenuWidgetCurrentNode=&event_child;g_MenuInputPollingPaused=1;
        Queue_Enqueue(1,0x40);MenuInput_DispatchQueuedEvents();
        Queue_Enqueue(2,0x1000);MenuInput_DispatchQueuedEvents();
        Queue_Enqueue(4,0x20);MenuInput_DispatchQueuedEvents();
        if(menu_event_count!=6)return 106;
        for(unsigned i=0;i<6;++i)
            if(menu_event_nodes[i]!=(i%2?2:1) || menu_event_flags[i]!=(i<2?0x40:i<4?0x21000:0x10000))return 107;
        g_MenuWidgetCurrentNode=NULL;g_MenuInputPollingPaused=0;
        MenuInput_DispatchQueuedEvents();
        MenuWidgetNode menu={0};menu.mode=1;g_MenuWidgetActiveListHead=&menu;
        MenuWidgetListNavigation navigation={0};
        navigation.list=&menu;
        menu.visible_rows=4; menu.y_limit=16; menu.disabled=8; menu.scroll_y=4;
        MenuWidget_EaseNodePosition(NULL);
        MenuWidget_EaseNodePosition(&navigation);
        if (navigation.draw_state!=8 || navigation.visible_rows!=8 ||
            navigation.list!=&menu || navigation.mode!=0) return 85;
        Field_HandleStateTransition();
        if(g_FieldPadBits!=FIELD_PAD_LEFT)return 38;
        g_PadPackets[0].leftX=20;Field_HandleStateTransition();
        if(g_FieldPadBits)return 39;
        g_MenuWidgetActiveListHead=0;
        g_PadPackets[0].header=0x00ff;Field_HandleStateTransition();
        if(g_FieldPadPressed!=4 || g_FieldPadBits!=4 || !(g_GameStateFlags&0x4000))return 40;
        Field_HandleStateTransition();
        if(g_FieldPadBits || g_FieldPadPressed)return 41;
        g_PadPackets[0].header=0x4000;Field_HandleStateTransition();
        if(g_FieldPadBits || (g_GameStateFlags&0x4000))return 42;
        Boot_ResetPadInput();
        if(!host_event(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_W) ||
           !host_event(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_UP) ||
           !host_event(SDL_EVENT_KEY_UP,SDL_SCANCODE_W))return 47;
        if(g_PadPackets[0].buttons&0x10)return 48;
        if(!host_event(SDL_EVENT_WINDOW_FOCUS_LOST,SDL_SCANCODE_UNKNOWN))return 49;
        Field_HandleStateTransition();
        if(g_FieldPadBits || g_PadPackets[0].buttons!=0xffff)return 50;
        if(!host_event(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_W) ||
           !host_event(SDL_EVENT_WINDOW_FOCUS_GAINED,SDL_SCANCODE_UNKNOWN))return 51;
        Field_HandleStateTransition();
        if(g_FieldPadBits)return 52;
        /* SDL events -> original pad mapper -> original menu repeat/dispatch. */
        Boot_ResetPadInput(); Boot_BuildRenderFlagTable(); MenuInput_InitQueue();
        menu_event_count=0;
        event_parent.flags=0; g_MenuWidgetCurrentNode=&event_parent;
        Field_HandleStateTransition(); MenuInput_DispatchQueuedEvents();
        if(menu_event_count || !g_MenuInputActive)return 108;
        if(!host_event(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_UP))return 109;
        Field_HandleStateTransition(); MenuInput_DispatchQueuedEvents();
        if(menu_event_count!=1 || menu_event_flags[0]!=0x1000)return 110;
        for(unsigned frame=1;frame<=9;++frame) {
            Field_HandleStateTransition(); MenuInput_DispatchQueuedEvents();
            if(menu_event_count!=(frame<9?1u:2u))return 111;
        }
        if(menu_event_flags[1]!=0x21000)return 112;
        if(!host_event(SDL_EVENT_KEY_UP,SDL_SCANCODE_UP))return 113;
        Field_HandleStateTransition(); MenuInput_DispatchQueuedEvents();
        if(menu_event_count!=2)return 114;
        if(!host_event(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_X))return 115;
        Field_HandleStateTransition(); MenuInput_DispatchQueuedEvents();
        if(menu_event_count!=3 || menu_event_flags[2]!=0x20) { fprintf(stderr,"X press: count=%u flags=%x pad=%x\n",menu_event_count,menu_event_flags[2],g_FieldPadBits); return 116; }
        if(!host_event(SDL_EVENT_KEY_UP,SDL_SCANCODE_X))return 117;
        Field_HandleStateTransition(); MenuInput_DispatchQueuedEvents();
        if(menu_event_count!=4 || menu_event_flags[3]!=0x10000)return 118;
        if(!host_event(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_V))return 119;
        Field_HandleStateTransition(); MenuInput_DispatchQueuedEvents();
        if(menu_event_count!=5 || menu_event_flags[4]!=0x80)return 120;
        if(!host_event(SDL_EVENT_WINDOW_FOCUS_LOST,SDL_SCANCODE_UNKNOWN))return 121;
        Field_HandleStateTransition(); MenuInput_DispatchQueuedEvents();
        if(menu_event_count!=5 || g_MenuInputHeldStatusMask || g_FieldPadBits)return 122;
        g_MenuWidgetCurrentNode=NULL;
        g_PlayerEntity=0;
    }
    if(!original_scene_teardown())return 139;
    if(argc>1 && !original_boot_audio_lifecycle())return 141;
    puts("Original action/clock/audio trigger and digital/analog player motion passed");
    SDL_Quit();
    return 0;
}
