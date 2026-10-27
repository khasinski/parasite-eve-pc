static int original_tool_equipped_donor(void)
{
    for (int fallback = -1; fallback < 15; ++fallback) {
        if (fallback == 0 || fallback == 1) continue;
        memset(&g_PlayerInventoryState, 0, sizeof(g_PlayerInventoryState));
        g_PlayerEntity = NULL;
        g_AyaSaveState.inventory_slot_count = 15;
        g_AyaInventoryItems[0] = 256;
        g_AyaInventoryItems[1] = 257;
        g_InventoryRuntime.tracked[0] = 0;
        g_InventoryRuntime.tracked[2] = -1;
        ItemDataRecord *source = &g_InventoryRuntime.equipment[0];
        ItemDataRecord *dest = &g_InventoryRuntime.equipment[1];
        source->iconId = dest->iconId = 1;
        source->kind = 1;
        dest->kind = 4;
        source->bonusStats[0] = 4; source->bonusStats[1] = 2;
        dest->bonusStats[0] = 1; dest->bonusStats[1] = 2; dest->bonusStats[2] = 1;
        if (fallback >= 0) {
            g_AyaInventoryItems[fallback] = 258;
            g_InventoryRuntime.equipment[2].iconId = 1;
            g_InventoryRuntime.equipment[2].kind = 8;
        }
        g_InvActiveListOverride = NULL;
        g_InvSavedSelectionFromStorage[0] = g_InvSavedSelectionFromStorage[1] = 0;
        g_InvSavedSelectionIndex[0] = 0;
        g_InvSavedSelectionIndex[1] = 1;
        /* Waive the tool cost only in this isolated lookup regression. The
         * recorded runtime replay verifies actual Tool consumption. */
        Inv_TransferItemBetweenLists(0, -1, 0, 999);
        if (g_AyaInventoryItems[0] || source->iconId ||
            g_InventoryRuntime.tracked[0] != fallback ||
            dest->bonusStats[0] != 5 || dest->bonusStats[1] != 4 || dest->bonusStats[2] != 1 ||
            source->bonusStats[0] || source->bonusStats[1] || source->bonusStats[2]) return 0;
    }
    puts("Original Tool transfer: equipped donor removal, every fallback club position and no-club result passed");
    return 1;
}

static int original_attack_effect_status_gate(void)
{
    Combatant active = {0}; BattleAction action = {0};
    BattleEntity target = {0}; EnemyCombatant core = {0};
    g_ActiveActor = &active; active.action = &action;
    g_BattleAilmentPmSlot = -1;
    for (unsigned word = 0; word < 65536; ++word) {
        action.turnWord = word;
        if (!(word & 0x1F00)) {
            target.core = NULL;
            Battle_StartEnemyAttackEffect(&target);
        }
        target.core = &core;
        core.statusFlags2 = 0x14015;
        EnemyCombatant before = core;
        Battle_StartEnemyAttackEffect(&target);
        if (memcmp(&core, &before, sizeof(core)) || g_BattleAilmentPmSlot != -1) return 0;
    }
    puts("Original attack effects: released target core without ailment bits and all 65536 already-applied status gates passed");
    return 1;
}

static int original_hospital_falling_states(void)
{
    FieldFxState *old_script=g_FieldFxScriptState;int old_floor=g_RoomFloorY->y;u32 old_rng=Psyz_RandState();
    FieldFxState script={0};RoomFallingBurstClock clock={.tick=100};
    script.current_context=&clock;g_FieldFxScriptState=&script;g_RoomFloorY->y=0;
    FieldFxHandler callbacks[]={g_RoomInitList__falling[2],g_RoomInitList__falling[3],
        g_RoomInitList__falling[4],g_RoomUpdateList__falling[2],g_RoomUpdateList__falling[4],g_RoomUpdateList__falling[1]};
    const u8 fills[]={0,1,0xa5,0xff};uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned callback=0;callback<6;++callback)for(unsigned fill=0;fill<4;++fill)for(unsigned variant=0;variant<12;++variant){
        _Alignas(void *) u8 data[512];u8 control[]={37,0xa5,variant*7,0};
        memset(data,fills[fill],sizeof(data));srand(variant*0x1234567u);
        if(callback==4){
            s16 live=6;memcpy(data+164,&live,2);
            for(unsigned i=0;i<6;++i){s16 position[]={100,-100,200,0},velocity[]={30,-80+(int)variant*20,40,0},active=i%2?1:0;
                memcpy(data+32+i*8,position,8);memcpy(data+80+i*8,velocity,8);memcpy(data+152+i*2,&active,2);}
        }
        if(callback==5){s16 position[]={100,-100,200};memcpy(data,position,6);}
        callbacks[callback](NULL,control,data);u32 random=Psyz_RandState();
        const void *parts[]={data,control,&random};const unsigned lengths[]={512,4,4};
        for(unsigned part=0;part<3;++part)for(unsigned j=0;j<lengths[part];++j)hash=(hash^((const u8 *)parts[part])[j])*UINT64_C(1099511628211);
    }
    g_FieldFxScriptState=old_script;g_RoomFloorY->y=old_floor;srand(old_rng);
    if(hash!=UINT64_C(0x6c0c8a4195cf042a)){fprintf(stderr,"Hospital falling CPU hash %016llx\n",(unsigned long long)hash);return 0;}
    puts("Original hospital falling burst: 288 unhooked USA CPU/BIOS/GTE initializer and update states, RNG, halfword wrap and full 512-byte guards passed");return 1;
}

static int original_hospital_object_cycles(void)
{
    PmPrimarySlot bank[11]={0};PmTables old_tables=g_PmTables;RenderBufferPrefix old_buffers=D_800B0E38;
    FieldActor *old_player=g_PlayerEntity;GteMatrix *old_view=g_GeomScroll.words.viewMatrix;
    FieldFxState *old_state=g_FieldFxScriptState;FieldFxObject *old_object=g_FieldFxScriptObject;
    FieldFxSlot *old_slots=g_FieldFxSlotTable;char *old_data=g_FieldFxScriptData;int *old_commands=g_FieldFxCommandTable;
    u32 old_flags=g_GameStateFlags,old_random=Psyz_RandState();int old_slot=g_ActiveDrawSlot,old_offset=g_DrawPacketOffset;
    FieldActor actor={0},player={0};FieldActorState core={0};Combatant player_core={0};RenderMatrix matrices[27]={0};
    u8 cue=1;actor.state=&core;actor.render_object.matrices=matrices;core.control10.command_value=120;core.action_state=&cue;
    player.state=(void *)&player_core;player.pos_x=player.pos_z=10000*65536;player.render_object.hit_cylinder.radius=63;
    for(unsigned i=0;i<27;++i){for(unsigned axis=0;axis<3;++axis)matrices[i].rotation[axis][axis]=4096;
        matrices[i].translation[0]=20;matrices[i].translation[1]=-200;matrices[i].translation[2]=1024;}
    GteMatrix view={.m={{4096,0,0},{0,4096,0},{0,0,4096}}};
    static RenderGpuTag ordering[4096];struct {_Alignas(void *) u8 bytes[65536];u8 guard[16];} packets;
    g_PmTables=(PmTables){old_tables.handlers,bank,NULL};g_PlayerEntity=&player;g_GeomScroll.words.viewMatrix=&view;
    g_GameStateFlags=0x80;Psyz_GteCtrlWrite(26,256);Psyz_GteCtrlWrite(27,1);Psyz_GteCtrlWrite(28,0);
    Psyz_GteCtrlWrite(29,1365);Psyz_GteCtrlWrite(30,1024);gte_SetGeomOffset(160,120);FieldFx_InitTexturePageTable();
    const unsigned commands[]={37,40},expected[]={0x2b,0x1f};
    for(unsigned effect=0;effect<2;++effect){
        memset(bank,0,sizeof(bank));cue=1;core.core_flags=0;srand(1);
        int handle=Scene_LoadRoomAssets(commands[effect],&actor);if(handle!=0){fprintf(stderr,"Hospital cycle allocate cmd%u result%d\n",commands[effect],handle);return 0;}
        const int ground[]={0,20,0,1024,256,30},falling[]={1,8,100,30,0,0};const int *vars=effect?falling:ground;
        for(unsigned i=0;i<6;++i)if(Pm_SendCmd(handle,0,i,vars[i],0,0))return 0;
        unsigned drawn=0,seen=0,frames=0;
        while(bank[handle].header.state && frames<240){
            memset(&packets,0xa5,sizeof(packets));for(unsigned depth=0;depth<4096;++depth)ordering[depth]=(RenderGpuTag){.address=0xffffff};
            g_ActiveDrawSlot=frames&1;g_DrawPacketOffset=0;D_800B0E38.ordering[g_ActiveDrawSlot]=ordering;
            D_800B0E38.packets[g_ActiveDrawSlot]=(char *)packets.bytes;
            if(Pm_Start(handle) || Pm_Exec(handle)){fprintf(stderr,"Hospital cycle cmd%u frame%u PM failure\n",commands[effect],frames);return 0;}
            for(unsigned i=0;i<64;++i)if(bank[handle].effect.slots[i].flag==1)seen|=1u<<bank[handle].effect.slots[i].handler_id;
            if(g_DrawPacketOffset<0 || g_DrawPacketOffset>sizeof(packets.bytes))return 0;drawn+=g_DrawPacketOffset!=0;
            for(unsigned depth=0;depth<4096;++depth){uintptr_t address=ordering[depth].address;unsigned chain=0;
                while(address!=0xffffff){if(address<(uintptr_t)packets.bytes || address+sizeof(RenderGpuTag)>(uintptr_t)packets.bytes+g_DrawPacketOffset || address%_Alignof(RenderGpuTag) || ++chain>1024)return 0;
                    address=((RenderGpuTag *)address)->address;}}
            for(unsigned i=0;i<16;++i)if(packets.guard[i]!=0xa5)return 0;++frames;
        }
        if(bank[handle].header.state || !drawn || (seen&expected[effect])!=expected[effect] || cue!=(effect?4:2)){
            fprintf(stderr,"Hospital cycle cmd%u frames%u state%u drawn%u seen%x cue%u\n",commands[effect],frames,bank[handle].header.state,drawn,seen,cue);return 0;}
    }
    g_PmTables=old_tables;D_800B0E38=old_buffers;g_PlayerEntity=old_player;g_GeomScroll.words.viewMatrix=old_view;
    g_FieldFxScriptState=old_state;g_FieldFxScriptObject=old_object;g_FieldFxSlotTable=old_slots;g_FieldFxScriptData=old_data;g_FieldFxCommandTable=old_commands;
    g_GameStateFlags=old_flags;g_ActiveDrawSlot=old_slot;g_DrawPacketOffset=old_offset;srand(old_random);
    puts("Original M146 ground/falling modules: real PM creation, original scripts, all authored slots, draw/update packet guards and complete expiry passed");return 1;
}

static int original_hospital_homing_throw_states(void)
{
    FieldFxObjectPrefix *old_owner=g_FieldFxOwner;FieldActor actor={0};FieldActorState state={0};
    FieldFxObjectPrefix owner={.actor=&actor};g_FieldFxOwner=&owner;uint64_t hash=UINT64_C(14695981039346656037);
    const int states[]={0,1,-1},finished[]={0,1,23,24},cues[]={-1,0,1,2,4},steps[]={65536,-65536},modes[]={8,9,2},frames[]={0,22,23,27,28,30,31};
    const u32 flags[]={0,0x1800,2,0x40000000};
    for(unsigned variant=0;variant<1024;++variant){
        struct {RoomHomingModel model;u8 guard[452];} data;memset(&data,0x5a,sizeof(data));memset(&data.model,0,sizeof(data.model));
        RoomHomingModel *model=&data.model;model->state=states[variant%3];model->finished=finished[(variant/3)%4];
        model->released=(variant>>4)&1;model->armed=(variant>>5)&1;
        memset(&actor,0,sizeof(actor));memset(&state,0,sizeof(state));
        int cue=cues[(variant/7)%5];u8 event=cue<0?0:cue;
        actor.state=variant%97?&state:NULL;state.core_flags=flags[(variant/11)%4];
        state.control10.command_value=variant%53?120:0;state.action_state=cue<0?NULL:&event;
        actor.anim_step=steps[(variant>>1)&1];actor.mode=modes[(variant/13)%3];actor.action=32;actor.anim.parts.integer=frames[(variant/17)%7];
        u32 result=RoomEffect_HomingProjectileThrow(1,model,0);
        const void *parts[]={&result,&data,&actor.anim_step,&event,&state.core_flags};const unsigned bytes[]={4,512,4,1,4};
        for(unsigned part=0;part<5;++part)for(unsigned j=0;j<bytes[part];++j)hash=(hash^((const u8 *)parts[part])[j])*UINT64_C(1099511628211);
    }
    g_FieldFxOwner=old_owner;
    if(hash!=UINT64_C(0x96842f7cd6e5bb01)){fprintf(stderr,"Hospital homing throw CPU hash %016llx\n",(unsigned long long)hash);return 0;}
    puts("Original hospital homing throw: 1024 USA CPU states, animation gates, interruption, absent event/core and full 512-byte guards passed");return 1;
}

static int original_hospital_homing_flight_states(void)
{
    FieldFxTaskContext *old_context=g_FieldFxTaskContext;FieldFxObjectPrefix *old_owner=g_FieldFxOwner;
    FieldFxTaskSlot *old_slot=g_FieldFxActiveTaskSlot;FieldActor *old_player=g_PlayerEntity;
    PolygonVertex *old_vertices=g_FloorPolygonVertices;u16 old_count=g_FloorPolygonVertexCount;int old_floor=g_RoomFloorY->y;
    FieldFxTaskContext context={0};FieldActor actor={0},player={.pos_x=30000*65536,.pos_z=30000*65536};
    FieldFxObjectPrefix owner={.actor=&actor};FieldFxTaskSlot slot={0};
    PolygonVertex vertices[]={{0,-1000,0,-1000},{0,1000,0,-1000},{0,1000,0,1000},{0,-1000,0,1000}};
    g_FieldFxTaskContext=&context;g_FieldFxOwner=&owner;g_FieldFxActiveTaskSlot=&slot;
    g_PlayerEntity=&player;g_FloorPolygonVertices=vertices;g_FloorPolygonVertexCount=4;g_RoomFloorY->y=0;
    const int coordinates[][2]={{0,0},{999,999},{-1200,0},{0,-1200}},lifts[]={0,100,-100,-16};
    uint64_t hash=UINT64_C(14695981039346656037);
    for(int angle=0;angle<4096;angle+=512)for(unsigned point=0;point<4;++point)for(unsigned mode=0;mode<4;++mode)for(unsigned escaped=0;escaped<2;++escaped){
        RoomHomingModel model={0};_Alignas(void *) char pool[1024];memset(pool,0x5a,sizeof(pool));slot.end=pool;
        model.position[0].word=coordinates[point][0]*65536;
        model.position[1].word=(mode==1?-1000:0)*65536;
        model.position[2].word=coordinates[point][1]*65536;
        model.speed=0x100000;model.scriptArg0=71;model.scriptArg1=93;model.drag=100;
        model.lift=lifts[mode]*65536;model.gravity=mode?0x60000:0;model.soundHandle=-1;
        model.angle=angle;model.state=1;model.matrixIndex=9;model.escaped=escaped;model.bounce=10;model.damping=8;
        RoomHomingModel *address=&model;memcpy(&context.variables[1],&address,sizeof(address));
        if(FieldFx_InitCallbackList(pool,8,4,RoomEffect_HomingProjectileSpark)<0)return 0;
        u32 result=RoomEffect_HomingProjectile(1,NULL,0);if(result)return 0;
        const void *parts[]={&result,&model};const unsigned lengths[]={4,60};
        for(unsigned part=0;part<2;++part)for(unsigned j=0;j<lengths[part];++j)hash=(hash^((const u8 *)parts[part])[j])*UINT64_C(1099511628211);
        FieldFxCallbackList *list=(void *)pool;
        for(unsigned i=0;i<4;++i){
            FieldFxCallbackListEntry *entry=(void *)(list->entries+i*list->stride);
            const void *fields[]={entry,entry->state};const unsigned bytes[]={4,8};
            for(unsigned part=0;part<2;++part)for(unsigned j=0;j<bytes[part];++j)hash=(hash^((const u8 *)fields[part])[j])*UINT64_C(1099511628211);
        }
    }
    g_FieldFxTaskContext=old_context;g_FieldFxOwner=old_owner;g_FieldFxActiveTaskSlot=old_slot;g_PlayerEntity=old_player;
    g_FloorPolygonVertices=old_vertices;g_FloorPolygonVertexCount=old_count;g_RoomFloorY->y=old_floor;
    if(hash!=UINT64_C(0xf0e781cbc727084d)){fprintf(stderr,"Hospital homing CPU hash %016llx\n",(unsigned long long)hash);return 0;}
    puts("Original hospital homing flight: 256 USA CPU/SDK/GTE states, floor edges, reflection, drag, bounce and spark payloads passed");return 1;
}

static int original_hospital_eruption_states(void)
{
    FieldFxState *old_script=g_FieldFxScriptState;int old_floor=g_RoomFloorY->y;u32 old_rng=Psyz_RandState();
    FieldFxState script={0};RoomEruptionActor actor={0};actor.matrix.t[0]=100;actor.matrix.t[1]=-100;actor.matrix.t[2]=200;
    script.current_context=&actor;g_FieldFxScriptState=&script;g_RoomFloorY->y=0;
    FieldFxHandler callbacks[]={g_RoomEruptionInitList[2],g_RoomEruptionInitList[3],g_RoomEruptionInitList[4],
        g_RoomEruptionUpdateList[2],g_RoomEruptionUpdateList[3],g_RoomEruptionUpdateList[5]};
    const u8 fills[]={0,1,0xa5,0xff};uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned callback=0;callback<6;++callback)for(unsigned fill=0;fill<4;++fill)for(unsigned variant=0;variant<12;++variant){
        _Alignas(void *) u8 data[512];u8 control[]={37,0xa5,variant*7,0};
        memset(data,fills[fill],sizeof(data));srand(variant*0x1234567u);
        if(callback==4){
            RoomEruptionRing *ring=(void *)data;ring->radius=1024;ring->depth=128;ring->frame=variant*7;
            ring->phase=variant*19;ring->speed=800;ring->acceleration=-60;
        }
        if(callback==5)for(unsigned i=0;i<6;++i){
            RoomEruptionDebrisState *debris=(void *)data;
            debris->position[i]=(GteShortVector){100+i,-100+(int)i,200+i,0x1234};
            debris->velocity[i]=(GteShortVector){30,-80+(int)variant*20,40,0x2345};
            debris->gravityStep[i]=(int)variant*50-300;debris->visible[i]=i%2?1:0;
        }
        callbacks[callback](NULL,control,data);u32 random=Psyz_RandState();
        const void *parts[]={data,control,&random};const unsigned lengths[]={512,4,4};
        for(unsigned part=0;part<3;++part)for(unsigned j=0;j<lengths[part];++j)hash=(hash^((const u8 *)parts[part])[j])*UINT64_C(1099511628211);
    }
    g_FieldFxScriptState=old_script;g_RoomFloorY->y=old_floor;srand(old_rng);
    if(hash!=UINT64_C(0xba517b063e65beed)){fprintf(stderr,"Hospital eruption CPU hash %016llx\n",(unsigned long long)hash);return 0;}
    puts("Original hospital eruption: 288 unhooked USA CPU/BIOS/GTE leaf states and full 512-byte guards passed");return 1;
}

#include "pe1/room_floor_walker.h"
#include "pe1/fieldfx_callback_list.h"
/* Isolated PM cycle using the last real loaded room's descriptor bank.
 * This checks the authored task and ring rendering, not campaign progress. */
static int original_timed_spawn_cycle(void)
{
    PmPrimarySlot bank[11]={0};PmTables old_tables=g_PmTables;RenderBufferPrefix old_buffers=D_800B0E38;
    GteMatrix *old_view=g_GeomScroll.words.viewMatrix;void *old_archive=g_GameState.loaded_scene_archive;
    FieldActor actor={0};FieldActorState core={0};RenderMatrix matrices[27]={0};u8 cue=1;
    actor.state=&core;actor.render_object.matrices=matrices;core.control10.command_value=120;core.action_state=&cue;
    for(unsigned i=0;i<27;++i){for(unsigned axis=0;axis<3;++axis)matrices[i].rotation[axis][axis]=4096;matrices[i].translation[2]=1024;}
    GteMatrix view={.m={{4096,0,0},{0,4096,0},{0,0,4096}}};static RenderGpuTag ordering[4096];
    struct {_Alignas(void *) u8 bytes[65536];u8 guard[16];} packets;
    g_PmTables=(PmTables){old_tables.handlers,bank,NULL};g_GeomScroll.words.viewMatrix=&view;
    g_GameStateFlags=0x80;g_RoomFloorY->y=0;
    Psyz_GteCtrlWrite(26,256);Psyz_GteCtrlWrite(27,1);Psyz_GteCtrlWrite(28,0);Psyz_GteCtrlWrite(29,1365);Psyz_GteCtrlWrite(30,1024);gte_SetGeomOffset(160,120);FieldFx_InitTexturePageTable();
    int valid=Scene_LoadRoomAssets(187,&actor)==0 && Pm_SendCmd(0,0,0,0,0,0)==0;unsigned drawn=0,linked=0,seen=0;
    for(unsigned frame=0;valid && frame<120;++frame){
        memset(&packets,0xa5,sizeof(packets));for(unsigned depth=0;depth<4096;++depth)ordering[depth]=(RenderGpuTag){.address=0xffffff};
        g_ActiveDrawSlot=frame&1;g_DrawPacketOffset=0;D_800B0E38.ordering[g_ActiveDrawSlot]=ordering;D_800B0E38.packets[g_ActiveDrawSlot]=(char *)packets.bytes;
        valid=Pm_Start(0)==0 && Pm_Exec(0)==0;
        for(unsigned slot=0;slot<8;++slot)if(bank[0].task.tasks.slots[slot].id!=0xffff)seen|=1u<<bank[0].task.tasks.slots[slot].id;
        if(g_DrawPacketOffset<0 || g_DrawPacketOffset>sizeof(packets.bytes)){valid=0;break;}drawn+=g_DrawPacketOffset!=0;
        for(unsigned depth=0;valid && depth<4096;++depth){uintptr_t address=ordering[depth].address;unsigned chain=0;
            while(address!=0xffffff){if(address<(uintptr_t)packets.bytes || address+sizeof(RenderGpuTag)>(uintptr_t)packets.bytes+g_DrawPacketOffset || address%_Alignof(RenderGpuTag) || ++chain>1024){valid=0;break;}
                ++linked;address=((RenderGpuTag *)address)->address;}}
        for(unsigned i=0;i<16;++i)if(packets.guard[i]!=0xa5)valid=0;
    }
    valid=valid && drawn && linked && seen==1 && !bank[0].task.tasks.count;
    if(!valid)fprintf(stderr,"Timed spawn PM cycle drawn%u linked%u seen%x remaining%d\n",drawn,linked,seen,bank[0].task.tasks.count);
    g_PmTables=old_tables;D_800B0E38=old_buffers;g_GeomScroll.words.viewMatrix=old_view;g_GameState.loaded_scene_archive=old_archive;
    g_FieldFxTaskContext=NULL;g_FieldFxOwner=NULL;g_FieldFxActiveTaskSlot=NULL;
    if(valid)puts("Original timed spawn: complete authored PM task, ring drawing, OT/packet guards and callback expiry passed");return valid;
}

static int original_floor_walker_assets(void)
{
    const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return 0;
    PeDisc disc={0};PeDiscFile exe,archive;
    if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path) || !pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive))return 0;
    ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 0;
    Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
    const unsigned rooms[]={145,332,152,153,154},sizes[]={17160,17160,20564,26532,26532};
    const uint64_t hashes[]={UINT64_C(0x7d00803c30c0112c),UINT64_C(0x7d00803c30c0112c),UINT64_C(0x38fff0ca5c9f3fc2),UINT64_C(0x175125898b12d28f),UINT64_C(0x175125898b12d28f)};
    const u32 scripts[]={0x80193280,0x80193280,0x80193fc4,0x80195648,0x80195648};
    for(unsigned index=0;index<5;++index){
        SceneRoomSectorRange range;u8 module[26532];
        if(!pe_disc_read(&disc,&exe,0x800ull+0x80093378u-0x80010000u+(rooms[index]-1)*8,&range,8) ||
           !pe_disc_read(&disc,&archive,(u64)(range.start+range.scratchSectors+range.textureSectors)*2048+8,module,sizes[index]))return 0;
        uint64_t hash=UINT64_C(14695981039346656037);for(unsigned i=0;i<sizes[index];++i)hash=(hash^module[i])*UINT64_C(1099511628211);
        if(hash!=hashes[index] || memcmp(module+(scripts[index]-0x8018eff0),g_RoomTimedSpawnScript,32) ||
           memcmp(module+(scripts[index]+32-0x8018eff0),RoomLib_TimedRenderTable,32))return 0;
        if(index>=3 && (memcmp(module+(0x8019553c-0x8018eff0),g_RoomEruptionScript,18*sizeof(int)) ||
                       memcmp(module+(0x801956c0-0x8018eff0),g_RoomHomingScript,sizeof(g_RoomHomingScript))))return 0;
        if(index==2 && memcmp(module+(0x80193f68-0x8018eff0),g_RoomDroppedFlareScript,sizeof(g_RoomDroppedFlareScript)))return 0;
        char name[16];snprintf(name,sizeof(name),"m%04ui",rooms[index]);u32 token=Text_ParseBase32Id((signed char *)name);
        for(unsigned pass=0;pass<2;++pass){
            memset(RoomLib_TimedRenderTable,0xa5,8);for(unsigned offset=12;offset<32;offset+=8)memset(RoomLib_TimedRenderTable+offset,0xa5,4);
            memset(&g_RoomHomingLaunch,0xa5,sizeof(g_RoomHomingLaunch));memset(g_RoomDroppedFlareArgs,0xa5,sizeof(g_RoomDroppedFlareArgs));
            int loaded=Scene_LoadRoom(token);
            if(loaded || g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2] || g_PmCmdHandlerTable[79]!=&g_RoomFloorWalkerHandlers ||
               g_FieldFxTaskPrograms[102]!=&g_RoomTimedSpawnProgram ||
               memcmp(module+(scripts[index]+32-0x8018eff0),RoomLib_TimedRenderTable,32)){fprintf(stderr,"Floor walker room%u pass%u load%d\n",rooms[index],pass,loaded);return 0;}
            if(index==2){if(g_FieldFxTaskPrograms[37]!=&g_RoomDroppedFlareProgram)return 0;for(unsigned i=0;i<sizeof(g_RoomDroppedFlareArgs);++i)if(((u8 *)g_RoomDroppedFlareArgs)[i])return 0;}
            if(index>=3){if(g_FieldFxTaskPrograms[81]!=&g_RoomHomingProgram || g_PmCmdHandlerTable[37]!=&g_RoomGroundEruptionHandlers)return 0;for(unsigned i=0;i<sizeof(g_RoomHomingLaunch);++i)if(((u8 *)&g_RoomHomingLaunch)[i])return 0;}
        }
    }
    CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
    puts("Original M145/M332/M152/M153/M154: full USA module fingerprints, scripts, complete descriptor banks and repeated color/launch reset passed");return original_timed_spawn_cycle();
}

static int original_timed_spawn_states(void)
{
    FieldFxTaskContext *old_context=g_FieldFxTaskContext;FieldFxTaskSlot *old_slot=g_FieldFxActiveTaskSlot;
    FieldFxObjectPrefix *old_owner=g_FieldFxOwner;FieldActor *old_player=g_PlayerEntity;
    int old_age=g_FieldFxCurrentAge;s16 old_floor=g_RoomFloorY->y;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;int ok=0;
#define TIMED_HASH(pointer,length) do {const u8 *p_=(const u8 *)(pointer);for(size_t i_=0;i_<(length);++i_)hash=(hash^p_[i_])*UINT64_C(1099511628211);}while(0)
    const u32 flags[]={0x01001234,0x82005678,0x41009abc};
    for(int age=0;age<41;++age)for(unsigned held=0;held<2;++held)for(unsigned f=0;f<3;++f){
        u16 data[]={0x1234,0x5678,11,22,33,0xaaaa};FieldFxTaskContext context={0};u8 *pool=(u8 *)context.arena;memset(pool,0x5a,1024);
        FieldFxTaskSlot slot={.end=(char *)pool};FieldFxObjectPrefix owner={0};
        FieldActor actor={0},player={0};FieldActorState state={0};Combatant player_state={0};
        context.flags=held;owner.actor=&actor;actor.state=&state;state.core_flags=flags[f];player.state=(void *)&player_state;player_state.stateFlags=0x123;
        g_FieldFxTaskContext=&context;g_FieldFxActiveTaskSlot=&slot;g_FieldFxOwner=&owner;g_PlayerEntity=&player;g_FieldFxCurrentAge=age;g_RoomFloorY->y=-123;
        if(FieldFx_InitCallbackList((char *)pool,12,8,RoomLib_UpdateTimedRender)<0){fprintf(stderr,"Timed callback allocation failed\n");goto done;}
        int ret=RoomLib_ControlTimedSpawn(1,data,0);TIMED_HASH(&ret,4);TIMED_HASH(data,12);TIMED_HASH(&state.core_flags,4);TIMED_HASH(&player_state.stateFlags,4);
        FieldFxCallbackList *list=(void *)pool;
        for(unsigned i=0;i<8;++i){FieldFxCallbackListEntry *entry=(void *)(list->entries+i*list->stride);TIMED_HASH(entry,4);TIMED_HASH(entry->state,12);}++cases;
    }
    if(hash!=UINT64_C(0x788c12a3dfd829f9)){fprintf(stderr,"Original timed controller: %u states hash %016llx\n",cases,(unsigned long long)hash);goto done;}
    hash=UINT64_C(14695981039346656037);cases=0;
    const s16 disabled[]={-1,0,1};const u16 frames[]={0,1,22,23,24,32766,32767,65535};const int modes[]={0,1,3};
    for(unsigned d=0;d<3;++d)for(unsigned f=0;f<8;++f)for(unsigned m=0;m<3;++m){
        RoomFxTimedRenderState state={.x=11,.y=22,.z=33,.pad06=0xaaaa,.disabled=disabled[d],.frame=frames[f]};
        int ret=RoomLib_UpdateTimedRender(modes[m],&state,0);TIMED_HASH(&ret,4);TIMED_HASH(&state,12);++cases;
    }
    if(hash!=UINT64_C(0x38aef67fe18b9687)){fprintf(stderr,"Original timed ring: %u states hash %016llx\n",cases,(unsigned long long)hash);goto done;}
    ok=1;
done:
    g_FieldFxTaskContext=old_context;g_FieldFxActiveTaskSlot=old_slot;g_FieldFxOwner=old_owner;g_PlayerEntity=old_player;g_FieldFxCurrentAge=old_age;g_RoomFloorY->y=old_floor;
#undef TIMED_HASH
    if(ok)puts("Original timed spawn: 318 USA CPU controller/callback states, spawn cadence and held status flags passed");return ok;
}

static int original_floor_walker_states(void)
{
    PolygonVertex *old_polygon=g_FloorPolygonVertices;
    u16 old_count=g_FloorPolygonVertexCount;
    FieldActor *old_player=g_PlayerEntity,player={0};
    s16 old_floor=g_RoomFloorY->y;
    PolygonVertex polygon[]={{0,-1000,0,-1000},{0,1000,0,-1000},{0,1000,0,1000},{0,-1000,0,1000}};
    const int positions[][2]={{0,0},{999,999},{-1200,0},{0,-1200},{999,-999}};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    g_FloorPolygonVertices=polygon;g_FloorPolygonVertexCount=4;g_RoomFloorY->y=0;
    player.pos_x=300*65536;player.pos_z=-300*65536;g_PlayerEntity=&player;
#define WALKER_HASH(pointer,length) do {const u8 *p_=(const u8 *)(pointer);for(size_t i_=0;i_<(length);++i_)hash=(hash^p_[i_])*UINT64_C(1099511628211);}while(0)
    for(unsigned angle=0;angle<4096;angle+=512)for(unsigned xy=0;xy<5;++xy)
    for(unsigned phase=0;phase<=2;phase+=2)for(unsigned started=0;started<2;++started)
    for(unsigned rest=0;rest<=3;rest+=3)for(unsigned flag=0;flag<=0x40000;flag+=0x40000){
        RoomFloorWalkerObject object={0};FieldActor actor={0};RoomFloorWalkerState *state=&object.state;
        object.header.owner=&actor;actor.mode=phase;actor.rot_y=angle;
        actor.pos_x=positions[xy][0]*65536;actor.pos_y=(started?-10:0)*65536;actor.pos_z=positions[xy][1]*65536;
        actor.motion_y=12345;actor.gravity_y=67890;actor.flags=flag|0x123;
        state->started=started;state->speed=0x38000;state->hopVelocity=0x20000;state->velocity=-0x18000;state->gravity=0x4000;
        state->angle=angle+256;state->rest=rest;state->restTime=6;state->turning=started;
        RoomLib_HandlerGFloorWalker((struct RoomEnt *)&object);
        WALKER_HASH(&state->started,1);WALKER_HASH(&state->speed,24);
        WALKER_HASH(&actor.pos_x,12);WALKER_HASH(&actor.motion_y,4);WALKER_HASH(&actor.gravity_y,4);WALKER_HASH(&actor.flags,4);++cases;
    }
#undef WALKER_HASH
    g_FloorPolygonVertices=old_polygon;g_FloorPolygonVertexCount=old_count;g_PlayerEntity=old_player;g_RoomFloorY->y=old_floor;
    if(hash!=UINT64_C(0x1888ca2de21b88ad)){fprintf(stderr,"Original floor walker: %u states hash %016llx\n",cases,(unsigned long long)hash);return 0;}
    hash=UINT64_C(14695981039346656037);cases=0;
    const int angles[]={0,512,1536,3584},scales[]={0,1024,4096,8192},rests[]={0,1,3,6};
    for(unsigned ai=0;ai<4;++ai)for(unsigned si=0;si<4;++si)for(unsigned ri=0;ri<4;++ri){
        RoomFloorWalkerObject object={0};FieldActor actor={0};object.header.owner=&actor;
        object.state.callback=RoomLib_HandlerGFloorWalker;object.state.started=1;object.state.rest=rests[ri];object.state.restTime=6;
        actor.move_speed=scales[si];actor.pos_x=-123456789;actor.pos_y=987654321;actor.pos_z=-409600000;
        actor.rot_x=angles[ai];actor.rot_y=angles[ai]+256;actor.rot_z=angles[ai]+512;
        if(RoomLib_UpdateHandlerG(&object.header))return 0;
        const u8 *parts[]={(const u8 *)&actor.render_object.model_matrix,(const u8 *)&object.state.rest};
        const unsigned sizes[]={32,2};for(unsigned p=0;p<2;++p)for(unsigned b=0;b<sizes[p];++b)hash=(hash^parts[p][b])*UINT64_C(1099511628211);++cases;
    }
    if(hash!=UINT64_C(0x96ccb2e1f39bd42c)){fprintf(stderr,"Original floor walker transforms: %u states hash %016llx\n",cases,(unsigned long long)hash);return 0;}
    /* Native pointer ABI, actor initialization and release retain the full
     * pointer and touch only the original authored fields. */
    FieldActor actor={0},expected={0};RoomFloorWalkerObject object={0};object.header.owner=&actor;
    actor.flags=expected.flags=0xabc00000;actor.render_object.flags_9C=expected.render_object.flags_9C=0x123;
    RoomLib_InitHandlerG(&object.header);expected.flags|=0x10002;expected.render_object.flags_9C|=0x400;
    if(memcmp(&actor,&expected,sizeof(actor)) || object.state.callback!=RoomLib_HandlerG || object.state.active ||
       object.state.variant!=-1 || object.state.frame!=-1 || object.state.enabled!=-1 || object.state.flags!=3 ||
       object.state.started || object.state.signal || object.state.rest || object.state.turning)return 0;
    int signal=-17;
    if(g_RoomFloorWalkerHandlers.send(&object.header,1,25,(intptr_t)&signal,0,0) || object.state.signal!=&signal || signal!=1)return 0;
    g_RoomFloorWalkerHandlers.send(&object.header,0,4,0x12345678,0,0);
    g_RoomFloorWalkerHandlers.send(&object.header,0,12,-12345,6789,19);
    if(object.state.speed!=0x12345678 || object.state.hopVelocity!=-12345 || object.state.gravity!=6789 || object.state.restTime!=19)return 0;
    if(g_RoomFloorWalkerHandlers.stop(&object.header) || signal || object.header.state!=4)return 0;
    FieldActor *old_head=g_FieldActorListHead,leader={0},dead={0};FieldActorState leader_state={0};RenderMatrix matrices[5]={0};
    leader.type_id=dead.type_id=6;leader.sub_id=dead.sub_id=3;dead.flags=0x10;dead.next=&leader;
    leader.state=&leader_state;leader_state.control10.command_value=120;leader.render_object.matrices=matrices;
    matrices[4].translation[0]=-123;matrices[4].translation[1]=456;matrices[4].translation[2]=-789;
    leader.rot_x=111;leader.rot_y=222;leader.rot_z=333;leader.pad_03E[0]=0x5a;leader.pad_03E[1]=0xa5;
    leader.mode=7;leader.anim.parts.integer=3;leader.anim_prev=2*65536;
    g_FieldActorListHead=&dead;RoomLib_InitHandlerG(&object.header);
    int selected=g_RoomFloorWalkerHandlers.send(&object.header,0,0,6,3,0)==0 && object.state.leader==&leader;
    if(selected)RoomLib_HandlerG((struct RoomEnt *)&object);
    g_FieldActorListHead=old_head;
    if(!selected || actor.pos_x!=-123*65536 || actor.pos_y!=456*65536 || actor.pos_z!=-789*65536 ||
       memcmp(&actor.rot_x,&leader.rot_x,8) || object.state.callback!=RoomLib_HandlerGFloorWalker)return 0;
    leader.state=NULL;object.state.signal=&signal;signal=1;RoomLib_HandlerG((struct RoomEnt *)&object);
    if(signal || object.header.state!=4)return 0;
    puts("Original floor walker: 704 USA CPU/GTE movement, reflection, hop and squash states plus native pointer ABI passed");return 1;
}


/* Verify relocation equivalence of complete original modules before reusing
 * their native procedures. These helpers are fixture-only asset readers. */
static u32 original_chrysler_word(const u8 *module,u32 address)
{
    u32 value;memcpy(&value,module+(address-0x8018eff0u),4);return value;
}
static int original_chrysler_read(PeDisc *disc,PeDiscFile *exe,PeDiscFile *archive,unsigned room,u8 *module,unsigned bytes)
{
    SceneRoomSectorRange range;
    return pe_disc_read(disc,exe,0x800ull+0x80093378u-0x80010000u+(room-1)*8,&range,8) &&
        pe_disc_read(disc,archive,(u64)(range.start+range.scratchSectors+range.textureSectors)*2048+8,module,bytes);
}
typedef struct OriginalChryslerRelocation {unsigned offset;s32 delta;} OriginalChryslerRelocation;
static int original_chrysler_code(const u8 *module,u32 dest,const u8 *reference,u32 source,unsigned bytes,const OriginalChryslerRelocation *reloc,unsigned count)
{
    s32 delta=(s32)(dest-source);
    for(unsigned offset=0;offset<bytes;offset+=4){
        u32 expected=original_chrysler_word(reference,source+offset),actual=original_chrysler_word(module,dest+offset);
        unsigned opcode=expected>>26,target=(expected&0x3ffffff)<<2;
        if((opcode==2 || opcode==3) && target>=(source&0xfffffff) && target<((source+bytes)&0xfffffff))expected+=(u32)(delta/4);
        for(unsigned i=0;i<count;++i)if(offset==reloc[i].offset)expected=(expected&0xffff0000)|(u16)((u16)expected+reloc[i].delta);
        if(actual!=expected){fprintf(stderr,"Chrysler original code %08x+%x: %08x expected %08x\n",dest,offset,actual,expected);return 0;}
    }
    return 1;
}
static int original_chrysler_particle_assets(void)
{
    const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return 0;
    PeDisc disc={0};PeDiscFile exe,archive;int valid=0;unsigned room=0;
    if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path) || !pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive))return 0;
    u8 three[4276],actor[21476],line[3876],falling[14084],walker[26532],module[26532];
#define CHRYSLER_REQUIRE(condition) do {if(!(condition)){fprintf(stderr,"Original Chrysler room%u check line%d failed\n",room,__LINE__);goto done;}}while(0)
#define CHRYSLER_BYTES(buffer,address) ((buffer)+((u32)(address)-0x8018eff0u))
#define CHRYSLER_WORD(buffer,address) original_chrysler_word(buffer,(address))
    CHRYSLER_REQUIRE(original_chrysler_read(&disc,&exe,&archive,65,three,sizeof(three)) &&
        original_chrysler_read(&disc,&exe,&archive,75,actor,sizeof(actor)) &&
        original_chrysler_read(&disc,&exe,&archive,104,line,sizeof(line)) &&
        original_chrysler_read(&disc,&exe,&archive,146,falling,sizeof(falling)) &&
        original_chrysler_read(&disc,&exe,&archive,153,walker,sizeof(walker)));
    ResetGraph(0);CdRom_BindImage(&disc,NULL);CHRYSLER_REQUIRE(Asset_OpenPeImage()==0);
    Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
    static const struct {unsigned room,bytes;uint64_t hash;u32 actor,three,falling,walker,timed,line;} rooms[]={
        {391,21876,UINT64_C(0xdcec40a3f540c42),0x801943fc,0x80194360,0,0,0,0x8019448c},
        {393,22708,UINT64_C(0x5a5f2f7aa1df38cc),0x801947c4,0x8019466c,0x80194724,0,0,0},
        {394,21876,UINT64_C(0xdcec40a3f540c42),0x801943fc,0x80194360,0,0,0,0x8019448c},
        {395,21876,UINT64_C(0xdcec40a3f540c42),0x801943fc,0x80194360,0,0,0,0x8019448c},
        {396,22708,UINT64_C(0x5a5f2f7aa1df38cc),0x801947c4,0x8019466c,0x80194724,0,0,0},
        {399,21436,UINT64_C(0x21bd8cb7dd4dba1b),0x80194288,0x801941ec,0,0x801942f8,0x80194354,0},
        {401,26140,UINT64_C(0xd3a9af747a8c7a18),0x80195498,0x80195340,0x801953f8,0x80195508,0x80195564,0},
        {402,21436,UINT64_C(0x21bd8cb7dd4dba1b),0x80194288,0x801941ec,0,0x801942f8,0x80194354,0}
    };
    const unsigned data_offsets[]={0x14,0x7c,0xc8,0xdc,0xe4,0x980};
    const unsigned work_offsets[]={0x264,0x270,0x27c,0x288,0x290,0x298,0x2a0,0x2a8,0x2b0,0x2b8,0x2c0,0x2c8,0x2d0,0x2d8,0x2e0,0x2e8,0x988,0x994,0x9b8,0xa94,0xb60};
    const unsigned falling_data[]={0x1c,0x84,0xd0,0xe4,0xec,0x848,0x854};
    const unsigned falling_seeds[]={0x460,0x488,0x740,0x9d4,0xb9c,0xdfc};
    const unsigned falling_work[]={0x244,0x250,0x25c,0x268,0x274,0x27c,0x284,0x28c,0x294,0x29c,0x2a4,0x2ac,0x2b4,0x2bc,0x2c4,0x2cc,0x2d4,0x2dc,0x2e4,0x2ec,0x2f4,0x2fc,0x304,0x30c,0x314,0x31c,0x324,0x32c,0x334,0x33c,0x344,0x34c,0x354,0x360,0x36c,0x374,0x37c,0x384,0x38c,0x394,0x6c8,0x780,0xa8c,0xac8,0xc5c,0xf60};
    for(unsigned index=0;index<sizeof(rooms)/sizeof(rooms[0]);++index){
        room=rooms[index].room;CHRYSLER_REQUIRE(original_chrysler_read(&disc,&exe,&archive,room,module,rooms[index].bytes));
        uint64_t hash=UINT64_C(14695981039346656037);for(unsigned i=0;i<rooms[index].bytes;++i)hash=(hash^module[i])*UINT64_C(1099511628211);
        CHRYSLER_REQUIRE(hash==rooms[index].hash);
        u32 start=CHRYSLER_WORD(module,rooms[index].three+4);s32 delta=(s32)(start-0x8018effc),data_delta=(s32)(rooms[index].three-0x80190024);
        s32 work_delta=(u16)(CHRYSLER_WORD(module,start+0x264)-CHRYSLER_WORD(three,0x8018effc+0x264));
        OriginalChryslerRelocation relocs[64];unsigned count=0;
        for(unsigned i=0;i<6;++i)relocs[count++]=(OriginalChryslerRelocation){data_offsets[i],data_delta};
        for(unsigned i=0;i<21;++i)relocs[count++]=(OriginalChryslerRelocation){work_offsets[i],work_delta};
        CHRYSLER_REQUIRE(original_chrysler_code(module,start,three,0x8018effc,0xffc,relocs,count));
        for(unsigned k=0;k<7;++k)CHRYSLER_REQUIRE(CHRYSLER_WORD(module,rooms[index].three+4*k)==CHRYSLER_WORD(three,0x80190024+4*k)+(u32)delta);
        for(unsigned k=0;k<11;++k){u32 expected=CHRYSLER_WORD(three,0x80190024-44+4*k);if(k<9 && expected!=0xffffffff)expected+=(u32)delta;
            CHRYSLER_REQUIRE(CHRYSLER_WORD(module,rooms[index].three-44+4*k)==expected);}
        CHRYSLER_REQUIRE(!memcmp(CHRYSLER_BYTES(module,rooms[index].three+28),g_RoomThreeParticleScript,56) &&
            !memcmp(CHRYSLER_BYTES(module,rooms[index].three+84),RoomLib_ThreeParticleTable,16));
        start=CHRYSLER_WORD(module,rooms[index].actor);delta=(s32)(start-0x80191974);
        relocs[0]=(OriginalChryslerRelocation){0x34,delta};relocs[1]=(OriginalChryslerRelocation){0x260,delta};relocs[2]=(OriginalChryslerRelocation){0x418,delta};
        relocs[3]=(OriginalChryslerRelocation){0xa4,(s32)(u16)CHRYSLER_WORD(module,start+0xa4)-(s32)(u16)CHRYSLER_WORD(actor,0x80191974+0xa4)};
        CHRYSLER_REQUIRE(original_chrysler_code(module,start,actor,0x80191974,0xaa4,relocs,4));
        u32 jump=0x80190000u+(s32)(s16)CHRYSLER_WORD(module,start+0xa4);
        for(unsigned k=0;k<33;++k)CHRYSLER_REQUIRE(CHRYSLER_WORD(module,jump+4*k)==CHRYSLER_WORD(actor,0x8018f08c+4*k)+(u32)delta);
        for(unsigned k=0;k<7;++k)CHRYSLER_REQUIRE(CHRYSLER_WORD(module,rooms[index].actor+4*k)==CHRYSLER_WORD(actor,0x80194048+4*k)+(u32)delta);
        if(rooms[index].walker){
            start=CHRYSLER_WORD(module,rooms[index].walker);delta=(s32)(start-0x80193a04);
            relocs[0]=(OriginalChryslerRelocation){0x30,delta};relocs[1]=(OriginalChryslerRelocation){0x398,delta};
            CHRYSLER_REQUIRE(original_chrysler_code(module,start,walker,0x80193a04,0x7f0,relocs,2));
            for(unsigned k=0;k<7;++k)CHRYSLER_REQUIRE(CHRYSLER_WORD(module,rooms[index].walker+4*k)==CHRYSLER_WORD(walker,0x8019562c+4*k)+(u32)delta);
            start=CHRYSLER_WORD(module,rooms[index].timed)-0x198;delta=(s32)(start-0x801941f4);
            u32 script=CHRYSLER_WORD(module,rooms[index].timed+52),seed=0x80190000u+(s32)(s16)CHRYSLER_WORD(module,start+0x20);
            relocs[0]=(OriginalChryslerRelocation){0x20,(s32)(seed-0x8018f21c)};
            relocs[1]=(OriginalChryslerRelocation){0x104,(s32)(script+32-0x80195668)};relocs[2]=(OriginalChryslerRelocation){0x2b8,delta};
            CHRYSLER_REQUIRE(original_chrysler_code(module,start,walker,0x801941f4,0x4dc,relocs,3));
            CHRYSLER_REQUIRE(!memcmp(CHRYSLER_BYTES(module,seed),CHRYSLER_BYTES(walker,0x8018f21c),8) &&
                !memcmp(CHRYSLER_BYTES(module,script),g_RoomTimedSpawnScript,32) &&
                !memcmp(CHRYSLER_BYTES(module,script+32),CHRYSLER_BYTES(walker,0x80195668),32));
            for(unsigned k=0;k<14;++k){u32 expected=CHRYSLER_WORD(walker,0x80195688+4*k);if(k==0)expected+=(u32)delta;if(k==13)expected=script;
                CHRYSLER_REQUIRE(CHRYSLER_WORD(module,rooms[index].timed+4*k)==expected);}
        }
        if(rooms[index].falling){
            start=CHRYSLER_WORD(module,rooms[index].falling);delta=(s32)(start-0x80190540);data_delta=(s32)(rooms[index].falling-0x80192564);
            work_delta=(u16)(CHRYSLER_WORD(module,start+0x244)-0x26d0);count=0;
            for(unsigned i=0;i<7;++i)relocs[count++]=(OriginalChryslerRelocation){falling_data[i],data_delta};
            for(unsigned i=0;i<6;++i)relocs[count++]=(OriginalChryslerRelocation){falling_seeds[i],-0x48};
            for(unsigned i=0;i<46;++i)relocs[count++]=(OriginalChryslerRelocation){falling_work[i],work_delta};
            CHRYSLER_REQUIRE(original_chrysler_code(module,start,falling,0x80190540,0x111c,relocs,count));
            for(unsigned k=0;k<7;++k)CHRYSLER_REQUIRE(CHRYSLER_WORD(module,rooms[index].falling+4*k)==CHRYSLER_WORD(falling,0x80192564+4*k)+(u32)delta);
            for(unsigned k=0;k<21;++k){u32 expected=CHRYSLER_WORD(falling,0x80192564-84+4*k);if(k<18 && expected!=0xffffffff)expected+=(u32)delta;
                CHRYSLER_REQUIRE(CHRYSLER_WORD(module,rooms[index].falling-84+4*k)==expected);}
            CHRYSLER_REQUIRE(!memcmp(CHRYSLER_BYTES(module,rooms[index].falling+28),g_RoomScript__falling,sizeof(g_RoomScript__falling)) &&
                !memcmp(CHRYSLER_BYTES(module,0x8018effc),CHRYSLER_BYTES(falling,0x8018f044),48));
            for(unsigned k=0;k<108;++k)CHRYSLER_REQUIRE(!CHRYSLER_BYTES(module,0x80192688u+(u32)work_delta)[k]);
        }
        if(rooms[index].line){
            delta=(s32)(CHRYSLER_WORD(module,rooms[index].line)-0x8018f3a4);start=0x8018f008u+(u32)delta;
            relocs[0]=(OriginalChryslerRelocation){0x20,0x1e0};relocs[1]=(OriginalChryslerRelocation){0x3c8,0x1e0};relocs[2]=(OriginalChryslerRelocation){0x3f0,0x1e0};
            relocs[3]=(OriginalChryslerRelocation){0x524,delta};relocs[4]=(OriginalChryslerRelocation){0xdf8,(s32)(rooms[index].line+60-0x8018fe94)};
            CHRYSLER_REQUIRE(original_chrysler_code(module,start,line,0x8018f008,0xe34,relocs,5));
            u32 script=CHRYSLER_WORD(module,rooms[index].line+52);
            CHRYSLER_REQUIRE(!memcmp(CHRYSLER_BYTES(module,0x8018f1d4),CHRYSLER_BYTES(line,0x8018eff4),20) && !memcmp(CHRYSLER_BYTES(module,script),g_RoomLineBurstScript,32));
            for(unsigned k=0;k<14;++k){u32 expected=CHRYSLER_WORD(line,0x8018fe5c+4*k);if(k==0 || k==12)expected+=(u32)delta;if(k==13)expected=script;
                CHRYSLER_REQUIRE(CHRYSLER_WORD(module,rooms[index].line+4*k)==expected);}
            for(unsigned k=0;k<128;++k)CHRYSLER_REQUIRE(!CHRYSLER_BYTES(module,rooms[index].line+60)[k]);
        }
        char name[16];snprintf(name,sizeof(name),"m%04ui",room);u32 token=Text_ParseBase32Id((signed char *)name);
        for(unsigned pass=0;pass<2;++pass){
            RoomFxSpritePacket *packets[]={&RoomLib_ThreeParticlePacket,&RoomLib_ThreeParticleFloorPacket,&g_RoomFallingBurstShadow,&g_RoomFallingBurstShimmer,&g_RoomFallingBurstSprite,&g_RoomFallingBurstPulse,&g_RoomFallingBurstParticle};
            for(unsigned i=0;i<7;++i)memset(packets[i],0xa5,sizeof(*packets[i]));memset(RoomLib_SlotTable,0xa5,8*sizeof(RoomSlotRec));
            memset(RoomLib_TimedRenderTable,0xa5,8);for(unsigned offset=12;offset<32;offset+=8)memset(RoomLib_TimedRenderTable+offset,0xa5,4);
            CHRYSLER_REQUIRE(Scene_LoadRoom(token)==0 && g_PmCmdHandlerTable[75]==&g_RoomActorClassTable[2] && g_PmCmdHandlerTable[10]==&g_RoomThreeParticleHandlers);
            for(unsigned i=0;i<(rooms[index].falling?7:2);++i)for(unsigned b=0;b<sizeof(*packets[i]);++b)CHRYSLER_REQUIRE(!((u8 *)packets[i])[b]);
            if(rooms[index].falling)CHRYSLER_REQUIRE(g_PmCmdHandlerTable[40]==&g_RoomFallingBurstHandlers);
            if(rooms[index].walker)CHRYSLER_REQUIRE(g_PmCmdHandlerTable[79]==&g_RoomFloorWalkerHandlers && g_FieldFxTaskPrograms[102]==&g_RoomTimedSpawnProgram &&
                !memcmp(RoomLib_TimedRenderTable,CHRYSLER_BYTES(walker,0x80195668),32));
            if(rooms[index].line){CHRYSLER_REQUIRE(g_FieldFxTaskPrograms[51]==&g_RoomLineBurstProgram);
                for(unsigned b=0;b<8*sizeof(RoomSlotRec);++b)CHRYSLER_REQUIRE(!((u8 *)RoomLib_SlotTable)[b]);}
        }
    }
    CHRYSLER_REQUIRE(original_three_particle_states() && original_three_particle_program() && original_timed_spawn_cycle());valid=1;
done:
    CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
#undef CHRYSLER_WORD
#undef CHRYSLER_BYTES
#undef CHRYSLER_REQUIRE
    if(valid)puts("Original Chrysler M391/M393/M394/M395/M396/M399/M401/M402: 22101 complete original instructions with only audited relocations, all methods/scripts/seeds/work data, 16 room loads, descriptor banks and repeated reset passed");return valid;
}

/* CPU oracle hashes include each whole 64-byte record, all 64 serialized
 * callback entries, return value and actual BIOS RNG state (no draw hooks). */
static int original_hospital_spark_particle_states(void)
{
    FieldFxTaskCallback callbacks[]={RoomEffect_BurstOrbParticle,RoomEffect_DampedSparkParticle,RoomEffect_BeamBounceParticle,RoomEffect_JitterSparkParticle};
    const uint64_t expected[]={UINT64_C(0xa02cc554e27b765a),UINT64_C(0x818487d9e34c3d0d),UINT64_C(0x11bbe210a94a5062),UINT64_C(0x9391bb57790f740b)};
    const s16 values[]={-32768,-1000,-1,0,1,1000,32767,17};const u16 timers[]={0,7,15,23,31,32767,65535};
    FieldFxTaskContext context={0};FieldFxTaskSlot slot={0};FieldActor player={0};
    static _Alignas(void *) char pool[4096];_Alignas(void *) u8 data[64];
    PolygonVertex floor[]={{0,-10000,0,-10000},{0,10000,0,-10000},{0,10000,0,10000},{0,-10000,0,10000}};
    FieldFxTaskContext *old_context=g_FieldFxTaskContext;FieldFxTaskSlot *old_slot=g_FieldFxActiveTaskSlot;FieldActor *old_player=g_PlayerEntity;
    PolygonVertex *old_polygon=g_FloorPolygonVertices;u16 old_count=g_FloorPolygonVertexCount;s16 old_floor=g_RoomFloorY->y,old_heading=g_RoomBurstOrbHeading;
    int old_age=g_FieldFxCurrentAge;u32 old_rng=Psyz_RandState();int valid=1;
    g_FieldFxTaskContext=&context;g_FieldFxActiveTaskSlot=&slot;slot.end=pool;g_PlayerEntity=&player;
    player.pos_x=player.pos_z=30000*65536;player.render_object.hit_cylinder.radius=63;
    g_FloorPolygonVertices=floor;g_FloorPolygonVertexCount=4;
    for(unsigned callback=0;callback<4 && valid;++callback){
        uint64_t hash=UINT64_C(14695981039346656037);
        for(unsigned state=0;state<7;++state)for(unsigned t=0;t<7;++t)for(unsigned v=0;v<8;++v){
            memset(data,0xa5,sizeof(data));u16 fields[]={values[v],values[(v+2)%8],values[(v+4)%8],v*511,values[(v+1)%8],values[(v+3)%8],values[(v+5)%8],v+1,state,timers[t]};memcpy(data,fields,20);
            memset(pool,0xa5,sizeof(pool));if(FieldFx_InitCallbackList(pool,20,64,callbacks[callback])<=0){valid=0;break;}
            g_RoomFloorY->y=values[(v+6)%8];g_FieldFxCurrentAge=v*7;g_RoomBurstOrbHeading=v*511-1000;srand(v*0x1234567u);
            u32 result=callbacks[callback](1,data,0),random=Psyz_RandState();
            const void *parts[]={&result,&random,data};const unsigned sizes[]={4,4,64};
            for(unsigned part=0;part<3;++part)for(unsigned j=0;j<sizes[part];++j)hash=(hash^((const u8 *)parts[part])[j])*UINT64_C(1099511628211);
            FieldFxCallbackList *list=(void *)pool;
            for(unsigned i=0;i<64;++i){FieldFxCallbackListEntry *entry=(void *)(list->entries+i*list->stride);
                for(unsigned j=0;j<4;++j)hash=(hash^((u8 *)entry)[j])*UINT64_C(1099511628211);
                for(unsigned j=0;j<20;++j)hash=(hash^entry->state[j])*UINT64_C(1099511628211);}
        }
        if(hash!=expected[callback]){fprintf(stderr,"Hospital spark CPU callback%u hash%016llx expected%016llx\n",callback,(unsigned long long)hash,(unsigned long long)expected[callback]);valid=0;}
    }
    g_FieldFxTaskContext=old_context;g_FieldFxActiveTaskSlot=old_slot;g_PlayerEntity=old_player;g_FloorPolygonVertices=old_polygon;g_FloorPolygonVertexCount=old_count;
    g_RoomFloorY->y=old_floor;g_RoomBurstOrbHeading=old_heading;g_FieldFxCurrentAge=old_age;srand(old_rng);
    if(valid)puts("Original hospital sparks: 1568 USA MIPS/BIOS/GTE update states, full record/pool guards, return and RNG passed");return valid;
}

/* Asset-backed PM lifecycle fixture; actors/floor are isolated test inputs,
 * not a campaign shortcut. All callbacks, scripts and draws are original. */
static int original_hospital_spark_cycles(void)
{
    PmTables old_pm=g_PmTables;RenderBufferPrefix old_buffers=D_800B0E38;
    FieldActor *old_player=g_PlayerEntity;GteMatrix *old_view=g_GeomScroll.words.viewMatrix;
    void *old_archive=g_GameState.loaded_scene_archive;u8 *old_effects=g_FieldEffectAssetTable;
    PolygonVertex *old_floor=g_FloorPolygonVertices;u16 old_count=g_FloorPolygonVertexCount;
    static PmPrimarySlot bank[11];static RenderGpuTag ordering[4096];
    static struct {_Alignas(void *) u8 bytes[0x40000];u8 guard[64];} packets;
    FieldActor actor={0},player={0};FieldActorState core={0};Combatant player_core={0};RenderMatrix matrices[32]={0};u8 cue=1;
    actor.state=&core;actor.render_object.matrices=matrices;core.control10.command_value=120;core.action_state=&cue;
    player.state=(void *)&player_core;player.pos_x=player.pos_z=10000*65536;player.render_object.hit_cylinder.radius=63;
    actor.render_object.target_x=20;actor.render_object.target_y=-200;actor.render_object.target_z=1024;
    for(unsigned i=0;i<32;++i){for(unsigned axis=0;axis<3;++axis)matrices[i].rotation[axis][axis]=4096;matrices[i].translation[0]=20;matrices[i].translation[1]=-200;matrices[i].translation[2]=1024;}
    PolygonVertex floor[]={{0,-10000,0,-10000},{0,10000,0,-10000},{0,10000,0,10000},{0,-10000,0,10000}};
    GteMatrix view={.m={{4096,0,0},{0,4096,0},{0,0,4096}}};
    g_PmTables=(PmTables){old_pm.handlers,bank,NULL};g_PlayerEntity=&player;g_GeomScroll.words.viewMatrix=&view;
    g_GameState.loaded_scene_archive=(struct SceneAssetBlob *)g_GameState.loaded_scene_assets;g_FieldEffectAssetTable=(u8 *)g_GameState.loaded_scene_assets;
    g_FloorPolygonVertices=floor;g_FloorPolygonVertexCount=4;g_RoomFloorY->y=0;g_GameStateFlags=0x80;
    if(!Asset_FindTable08ByU32Key(g_GameState.loaded_scene_archive,0xc5485704))return 0;
    Psyz_GteCtrlWrite(26,256);Psyz_GteCtrlWrite(27,1);Psyz_GteCtrlWrite(28,0);Psyz_GteCtrlWrite(29,1365);Psyz_GteCtrlWrite(30,1024);gte_SetGeomOffset(160,120);FieldFx_InitTexturePageTable();
    const unsigned commands[]={129,130,126},masks[]={3,1,1};int valid=1;unsigned particle_states[4]={0};
    for(unsigned effect=0;effect<3 && valid;++effect){
        memset(bank,0,sizeof(bank));for(unsigned i=0;i<11;++i)memset(bank[i].task.tasks.arena,0xa5,sizeof(bank[i].task.tasks.arena));
        cue=1;core.core_flags=0;player_core.stateFlags=0;srand(1);
        int handle=Scene_LoadRoomAssets(commands[effect],&actor);if(handle!=0){fprintf(stderr,"Spark PM allocate cmd%u=%d\n",commands[effect],handle);valid=0;break;}
        if(effect==0)valid=Pm_SendCmd(handle,0,0,24,0,0)==0;
        else if(effect==1)valid=Pm_SendCmd(handle,0,1,0,36,8)==0 && Pm_SendCmd(handle,0,0,0,20,1024)==0;
        else valid=Pm_SendCmd(handle,0,0,500,0,0)==0;
        unsigned frames=0,seen=0,drawn=0,linked=0,peak=0,beam_phases=0;
        while(valid && bank[handle].header.state && frames<300){
            memset(&packets,0xa5,sizeof(packets));for(unsigned i=0;i<4096;++i)ordering[i]=(RenderGpuTag){.address=0xffffff};
            g_ActiveDrawSlot=frames&1;g_DrawPacketOffset=0;D_800B0E38.ordering[g_ActiveDrawSlot]=ordering;D_800B0E38.packets[g_ActiveDrawSlot]=(char *)packets.bytes;
            if(Pm_Start(handle) || Pm_Exec(handle)){valid=0;break;}
            FieldFxTaskContext *ctx=&bank[handle].task.tasks;
            if(ctx->used>sizeof(ctx->arena) || g_DrawPacketOffset<0 || g_DrawPacketOffset>sizeof(packets.bytes)){valid=0;break;}
            drawn+=g_DrawPacketOffset!=0;
            for(unsigned i=0;i<8;++i)if(ctx->slots[i].id!=0xffff){
                FieldFxTaskSlot *slot=&ctx->slots[i];seen|=1u<<slot->id;
                if(slot->start<ctx->arena || slot->start>=ctx->arena+sizeof(ctx->arena) || slot->end<slot->start || slot->end>=ctx->arena+sizeof(ctx->arena)){valid=0;break;}
                if(effect==1)beam_phases|=1u<<((RoomBeamSpark *)slot->start)->state;
                FieldFxCallbackList *list=(void *)slot->end;unsigned active=0;
                for(int j=0;j<list->count;++j){FieldFxCallbackListEntry *entry=(void *)(list->entries+(size_t)j*list->stride);active+=entry->active!=0;
                    if(entry->active){unsigned kind=list->callback==RoomEffect_BurstOrbParticle?0:list->callback==RoomEffect_DampedSparkParticle?1:list->callback==RoomEffect_BeamBounceParticle?2:3;
                        s16 state;memcpy(&state,entry->state+16,2);if(state<0 || state>5){valid=0;break;}particle_states[kind]|=1u<<state;}}
                if(active>peak)peak=active;
            }
            for(unsigned i=ctx->used;i<sizeof(ctx->arena);++i)if((u8)ctx->arena[i]!=0xa5)valid=0;
            for(unsigned i=0;i<64;++i)if(packets.guard[i]!=0xa5)valid=0;
            for(unsigned depth=0;valid && depth<4096;++depth){uintptr_t address=ordering[depth].address;unsigned chain=0;
                while(address!=0xffffff){if(address<(uintptr_t)packets.bytes || address+sizeof(RenderGpuTag)>(uintptr_t)packets.bytes+g_DrawPacketOffset || address%_Alignof(RenderGpuTag) || ++chain>4096){valid=0;break;}++linked;address=((RenderGpuTag *)address)->address;}}
            ++frames;
        }
        if(bank[handle].header.state || !drawn || !linked || !peak || seen!=masks[effect] || (effect==1 && beam_phases!=15))valid=0;
        fprintf(stderr,"Spark PM cmd%u frames%u state%u seen%x peak%u draw%u links%u phases%x valid%d\n",commands[effect],frames,bank[handle].header.state,seen,peak,drawn,linked,beam_phases,valid);
    }
    if(particle_states[0]!=0x3f || particle_states[1]!=3 || particle_states[2]!=3 || particle_states[3]!=7)valid=0;
    fprintf(stderr,"Spark PM particle phases %x/%x/%x/%x valid%d\n",particle_states[0],particle_states[1],particle_states[2],particle_states[3],valid);
    g_PmTables=old_pm;D_800B0E38=old_buffers;g_PlayerEntity=old_player;g_GeomScroll.words.viewMatrix=old_view;
    g_GameState.loaded_scene_archive=old_archive;g_FieldEffectAssetTable=old_effects;g_FloorPolygonVertices=old_floor;g_FloorPolygonVertexCount=old_count;
    g_FieldFxOwner=NULL;g_FieldFxTaskContext=NULL;g_FieldFxActiveTaskSlot=NULL;
    if(valid)puts("Original hospital sparks: three complete authored PM programs, both orb/spray callbacks, all beam phases, particle expiry, real model and OT/arena/packet guards passed");return valid;
}

static int original_hospital_spark_assets(void)
{
    const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return 0;
    PeDisc disc={0};PeDiscFile exe,archive;
    if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path) || !pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive))return 0;
    ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 0;
    Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
    const unsigned rooms[]={156,291,380};
    const struct {u32 address;const void *native;size_t size;} scripts[]={
        {0x80192adc,g_RoomBurstOrbScript,sizeof(g_RoomBurstOrbScript)},
        {0x80192b40,g_RoomBeamSparkScript,sizeof(g_RoomBeamSparkScript)},
        {0x80192b9c,g_RoomJitterSparkScript,sizeof(g_RoomJitterSparkScript)}};
    FieldFxTaskProgram *programs[]={&g_RoomBurstOrbProgram,&g_RoomBeamSparkProgram,&g_RoomJitterSparkProgram};
    FieldFxTaskCallback callbacks[]={RoomEffect_BurstOrbController,RoomEffect_DampedSparkController,RoomEffect_BeamSparkController,RoomEffect_JitterSparkController};
    if(programs[0]->table.callbacks[0]!=callbacks[0] || programs[0]->table.callbacks[1]!=callbacks[1] ||
       programs[1]->table.callbacks[0]!=callbacks[2] || programs[2]->table.callbacks[0]!=callbacks[3] ||
       programs[0]->initialize!=RoomLib_SetArg1 || programs[1]->initialize!=RoomLib_SlotSet || programs[2]->initialize!=RoomLib_CaptureFrameSample)return 0;
    for(unsigned index=0;index<3;++index){
        SceneRoomSectorRange range;u8 module[15432];
        if(!pe_disc_read(&disc,&exe,0x800ull+0x80093378u-0x80010000u+(rooms[index]-1)*8,&range,8) ||
           !pe_disc_read(&disc,&archive,(u64)(range.start+range.scratchSectors+range.textureSectors)*2048+8,module,sizeof(module)))return 0;
        uint64_t hash=UINT64_C(14695981039346656037);for(unsigned i=0;i<sizeof(module);++i)hash=(hash^module[i])*UINT64_C(1099511628211);
        if(hash!=UINT64_C(0xf70fe1e97ba8ce7c))return 0;
        const u32 descriptors[]={0x80192b08,0x80192b64,0x80192bc0};
        for(unsigned program=0;program<3;++program){
            if(memcmp(module+(scripts[program].address-0x8018eff0),scripts[program].native,scripts[program].size) ||
               programs[program]->script!=scripts[program].native ||
               memcmp(module+(descriptors[program]-0x8018eff0)+32,programs[program]->table.sizes,16))return 0;
        }
        char name[16];snprintf(name,sizeof(name),"m%04ui",rooms[index]);u32 token=Text_ParseBase32Id((signed char *)name);
        for(unsigned pass=0;pass<2;++pass){
            g_RoomBurstOrbHeading=123;g_RoomBeamSparkAsset=module;g_RoomScriptArg=19;
            memset(RoomLib_SlotTable,0xa5,8*sizeof(*RoomLib_SlotTable));memset(&g_RoomSparkCapture,0xa5,sizeof(g_RoomSparkCapture));
            if(Scene_LoadRoom(token) || g_FieldFxTaskPrograms[44]!=programs[0] || g_FieldFxTaskPrograms[45]!=programs[1] ||
               (rooms[index]==380 && g_FieldFxTaskPrograms[41]!=programs[2]) ||
               g_RoomBurstOrbHeading || g_RoomBeamSparkAsset || g_RoomScriptArg)return 0;
            for(unsigned i=0;i<8*sizeof(*RoomLib_SlotTable);++i)if(((u8 *)RoomLib_SlotTable)[i])return 0;
            for(unsigned i=0;i<sizeof(g_RoomSparkCapture);++i)if(((u8 *)&g_RoomSparkCapture)[i])return 0;
        }
    }
    int cycles=original_hospital_spark_cycles();
    CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
    if(!cycles)return 0;
    puts("Original M156/M291/M380: whole USA module fingerprint, three complete task scripts/tables, callback ABI, six room loads and repeated work-data reset passed");return 1;
}

static int original_sewer_twelve_assets(void)
{
    const char *path = getenv("PE_TEST_DISC");
    if (!path || !*path) return 0;
    PeDisc disc = {0}; PeDiscFile exe, archive;
    if (!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path) ||
        !pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive)) return 0;
#define TWELVE_REQUIRE(c) do {if (!(c)) {fprintf(stderr,"Twelve element integration line %u failed\n",__LINE__);return 0;}} while (0)
    u8 module[17132];
    TWELVE_REQUIRE(original_chrysler_read(&disc,&exe,&archive,167,module,sizeof(module)));
    const u32 methods[] = {0x8018f3a4,0x8018f1ec,0x8018f218,0x8018f244,0x8018f28c,0x8018f318,0x8018f39c};
    TWELVE_REQUIRE(!memcmp(module+(0x801931b0u-0x8018eff0u),methods,sizeof(methods)) &&
        !memcmp(module+(0x801931ccu-0x8018eff0u),RoomLib_TwelveEffectTable,60));
    ResetGraph(0); CdRom_BindImage(&disc,NULL);
    TWELVE_REQUIRE(Asset_OpenPeImage()==0);
    Boot_InitGameState(); D_8009CDC8='m'; fx_image_io=1;
    const unsigned rooms[]={167,168,169,173,178,295,299,309,310};
    for (unsigned room_index=0;room_index<sizeof(rooms)/sizeof(*rooms);++room_index) {
        unsigned room=rooms[room_index]; u8 other[sizeof(module)];
        TWELVE_REQUIRE(original_chrysler_read(&disc,&exe,&archive,room,other,sizeof(other)) &&
            !memcmp(other+4,module+4,sizeof(module)-4));
        char name[16]; snprintf(name,sizeof(name),"m%04ui",room);
    for (unsigned pass=0;pass<2;++pass) {
        memset(&RoomLib_TwelveEffectPrimaryPacket,0xa5,sizeof(RoomLib_TwelveEffectPrimaryPacket));
        memset(&RoomLib_TwelveEffectSecondaryPacket,0xa5,sizeof(RoomLib_TwelveEffectSecondaryPacket));
        g_PmCmdHandlerTable[11]=g_PmCmdHandlerTable[70]=g_PmCmdHandlerTable[75]=NULL;
        TWELVE_REQUIRE(Scene_LoadRoom(Text_ParseBase32Id((signed char *)name))==0 &&
            g_PmCmdHandlerTable[11]==&g_RoomTwelveElementHandlers &&
            g_PmCmdHandlerTable[70]==&g_RoomActorClassTable[0] && g_PmCmdHandlerTable[75]==&g_RoomActorClassTable[2]);
        if (room==168 || room==169) TWELVE_REQUIRE(g_PmCmdHandlerTable[73]==&g_RoomActorClassTable[4]);
        for (unsigned b=0;b<sizeof(RoomFxSpritePacket);++b)
            TWELVE_REQUIRE(!((u8 *)&RoomLib_TwelveEffectPrimaryPacket)[b] && !((u8 *)&RoomLib_TwelveEffectSecondaryPacket)[b]);
    }
    }
    CdRom_BindImage(NULL,NULL); pe_disc_close(&disc);
    _Alignas(8) u8 assets[128]={0}; ((SceneAssetBlob *)assets)->directoryOffset=8;
    ((SceneAssetDirectory *)(assets+8))->keyEntries=(1u<<22)|64;
    ((AssetU32KeyRecord *)(assets+64))->packedOffset=80;
    ((AssetU32KeyRecord *)(assets+64))->key=0x73decd80;
    ((TimPackedImage *)(assets+80))->key.bytes.type=11;
    ((TimPackedImage *)(assets+80))->geometry=(832u<<10)|(256u<<21);
    g_GameState.loaded_scene_archive=(struct SceneAssetBlob *)assets;
    GteMatrix view={.m={{4096,0,0},{0,4096,0},{0,0,4096}}}; s32 projection=320;
    g_GeomScroll.words.viewMatrix=&view; g_GeomScroll.words.projectionDistance=&projection; g_RoomFloorY->y=0;
    PolygonVertex polygon[4]={{.x=-2000,.z=-2000},{.x=2000,.z=-2000},{.x=2000,.z=2000},{.x=-2000,.z=2000}};
    g_FloorPolygonVertices=polygon; g_FloorPolygonVertexCount=4;
    _Alignas(void *) static u8 arena[65536]; static RenderGpuTag ordering[4096];
    for (unsigned draw=0;draw<2;++draw) {
        for (unsigned i=0;i<32;++i) {Psyz_GteCtrlWrite(i,0); Psyz_GteDataWrite(i,0);}
        Psyz_GteCtrlWrite(24,160u<<16); Psyz_GteCtrlWrite(25,112u<<16);
        Psyz_GteCtrlWrite(26,320); Psyz_GteCtrlWrite(29,1365); Psyz_GteCtrlWrite(30,1024);
        FieldFxObject object={0}; FieldActor actor={0}; FieldActorState state={0}; u8 action=2;
        RenderMatrix matrix={.rotation={{4096,0,0},{0,4096,0},{0,0,4096}},.translation={0,-100,1200}};
        actor.state=&state; state.control10.command_value=1; state.action_state=&action;
        actor.render_object.matrices=&matrix; actor.pos_x=8000*65536; actor.pos_z=8000*65536;
        g_PlayerEntity=&actor; object.prefix.actor=&actor;
        g_RoomTwelveElementHandlers.init(); g_RoomTwelveElementHandlers.initialize((PmSlotHeader *)&object);
        TWELVE_REQUIRE(!memcmp(object.state.commands,module+(0x80193208u-0x8018eff0u),16));
        const intptr_t vars[]={10,60,100,20,0,64,4096};
        for (unsigned v=0;v<7;++v) g_RoomTwelveElementHandlers.send((PmSlotHeader *)&object,0,v,vars[v],0,0);
        intptr_t wide=sizeof(intptr_t)>4?(intptr_t)UINT64_C(0x1234567887654321):0x7654321;
        g_RoomTwelveElementHandlers.send((PmSlotHeader *)&object,0,8,wide,0,0);
        TWELVE_REQUIRE(object.state.vars[8]==wide);
        unsigned seen=0,active=0,drawn=0,ticks=0;
        for (;ticks<180;++ticks) {
            memset(arena,0xa5,sizeof(arena));
            for (unsigned i=0;i<4096;++i) ordering[i]=(RenderGpuTag){.address=0xffffff};
            g_ActiveDrawSlot=draw; g_DrawPacketOffset=0;
            D_800B0E38.ordering[draw]=ordering; D_800B0E38.packets[draw]=(char *)arena;
            g_RoomTwelveElementHandlers.start((PmSlotHeader *)&object);
            g_RoomTwelveElementHandlers.execute((PmSlotHeader *)&object);
            for (unsigned i=0;i<64;++i) if (object.slots[i].flag) {
                FieldFxSlot *slot=&object.slots[i]; seen|=1u<<slot->handler_id;
                TWELVE_REQUIRE(slot->handler_id<=1 && slot->data_offset>=0);
                if (slot->handler_id==1) {
                    RoomLibTick12Rec *data=(void *)(object.data+slot->data_offset);
                    TWELVE_REQUIRE((uintptr_t)data%_Alignof(RoomLibTick12Rec)==0);
                    for (unsigned j=0;j<12;++j) if (data->state[j]==1) active|=1u<<j;
                }
            }
            TWELVE_REQUIRE(g_DrawPacketOffset>=0 && g_DrawPacketOffset<sizeof(arena));
            drawn+=g_DrawPacketOffset!=0;
            for (unsigned layer=0;layer<4096;++layer) {
                uintptr_t link=ordering[layer].address; unsigned links=0;
                while (link!=0xffffff) {
                    TWELVE_REQUIRE(link>=(uintptr_t)arena && link+sizeof(RenderGpuTag)<=(uintptr_t)arena+g_DrawPacketOffset && ++links<=48);
                    link=((RenderGpuTag *)link)->address;
                }
            }
            for (unsigned b=g_DrawPacketOffset+sizeof(FieldStripPacket);b<sizeof(arena);++b) TWELVE_REQUIRE(arena[b]==0xa5);
            if (object.prefix.reserved00==4) break;
        }
        if (ticks>=180 || seen!=3 || active!=4095 || !drawn) fprintf(stderr,"Twelve draw%u ticks%u seen%u active%x drawn%u\n",draw,ticks,seen,active,drawn);
        TWELVE_REQUIRE(ticks<180 && seen==3 && active==4095 && drawn>0);
        state.core_flags=0x3fabcdef;
        g_RoomTwelveElementHandlers.stop((PmSlotHeader *)&object); g_RoomTwelveElementHandlers.unload();
        TWELVE_REQUIRE(state.core_flags==0x00abcdef && action==4);
    }
    puts("Original twelve-element library: nine exact USA modules, all real CUE descriptors and reload resets, full authored PM script, both buffers, all twelve elements, native OT chains and expiry passed");
    return 1;
#undef TWELVE_REQUIRE
}

static int original_all_room_loads(void)
{
    PeDisc disc={0},disc2={0};
    const char *first=getenv("PE_TEST_DISC"),*second=getenv("PE_TEST_DISC2");
    if(!first || !second)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
    if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,first))return 1;
    if(!pe_disc_open(&disc2,second)){pe_disc_close(&disc);return 1;}
    ResetGraph(0);g_CdImageVolumes[0]=&disc;g_CdImageVolumes[1]=&disc2;
    int result=1;
    struct CachedProgram { const u8 *data; size_t size; u64 hash; };
    static struct CachedProgram programs[8192];
    unsigned program_count = 0, program_lookups = 0;
    size_t program_bytes = 0;
    u64 lookup_ticks = 0;
    for(unsigned volume=0;volume<2;++volume){
    CdRom_BindImage(volume?&disc2:&disc,NULL);
    if(Asset_OpenPeImage()!=0)goto done;
    Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
    unsigned loaded=0,empty=0;
    for(unsigned map=1;map<=SCENE_ROOM_COUNT;++map){
        if(!g_RoomSectorTable[map-1].roomSectors){++empty;continue;}
        int homing=map==162 || map==163 || map==398 || map==400 ||
                   map==403 || map==434 || map==435;
        if(homing)memset(g_RoomScriptArgs,0xa5,sizeof(g_RoomScriptArgs));
        char name[16];snprintf(name,sizeof(name),"m%04ui",map);
        int status=Scene_LoadRoom(Text_ParseBase32Id((signed char *)name));
        if(status){fprintf(stderr,"Room catalog M%03u load status%d\n",map,status);goto done;}
        if(homing && (g_PmCmdHandlerTable[70]!=&g_RoomActorClassTable[0] ||
           g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2] ||
           g_FieldFxTaskPrograms[38]!=&g_RoomHomingFlashProgram))goto done;
        if(homing)for(unsigned i=0;i<sizeof(g_RoomScriptArgs);++i)
            if(((unsigned char *)g_RoomScriptArgs)[i])goto done;
        /* Catalogue real SFX programs on both discs. Retain their hashes
         * across all room-buffer reuse and verify deduplication explicitly. */
        SceneAssetBlob *archive = (SceneAssetBlob *)g_GameState.loaded_scene_assets;
        SceneAssetDirectory *directory = SceneAsset_ResolveOffset(archive, archive->directoryOffset);
        u32 packed = directory->streamEntries;
        SceneCdStreamRecord *streams = SceneAsset_ResolveOffset(archive, packed & 0x3FFFFF);
        for (unsigned entry = 0; entry < packed >> 22; ++entry) {
            size_t size = streams[entry].reserved & 0xFFFFFF;
            if (size < 8) continue;
            const u8 *source = SceneAsset_ResolveOffset(archive, streams[entry].offset & 0xFFFFFF);
            u64 start = SDL_GetPerformanceCounter();
            const u8 *copy = Asset_FindTable2CByU16Key((char *)archive, streams[entry].key);
            const u8 *again = Asset_FindTable2CByU16Key((char *)archive, streams[entry].key);
            lookup_ticks += SDL_GetPerformanceCounter() - start; program_lookups += 2;
            if (!copy || copy != again || memcmp(copy, source, size)) goto done;
            unsigned i = 0; while (i < program_count && programs[i].data != copy) ++i;
            if (i == program_count) {
                if (program_count == 8192) goto done;
                u64 hash = UINT64_C(14695981039346656037);
                for (size_t byte = 0; byte < size; ++byte) hash = (hash ^ copy[byte]) * UINT64_C(1099511628211);
                programs[program_count++] = (struct CachedProgram){copy, size, hash};
                program_bytes += size;
            }
        }
        ++loaded;
    }
    if(loaded!=414 || empty!=36){fprintf(stderr,"Room catalog counts %u/%u\n",loaded,empty);goto done;}
    printf("Original disc%u room catalog: all 450 entries, 414 nonempty loads, 36 empty records, including all seven HomingFlash bindings and resets passed\n",volume+1);
    }
    for (unsigned i = 0; i < program_count; ++i) {
        u64 hash = UINT64_C(14695981039346656037);
        for (size_t byte = 0; byte < programs[i].size; ++byte)
            hash = (hash ^ programs[i].data[byte]) * UINT64_C(1099511628211);
        if (hash != programs[i].hash) goto done;
    }
    printf("SFX cache catalogue: %u unique immutable programs, %zu payload bytes, %u lookups in %.3f ms; all hashes survived both disc catalogues\n",
        program_count, program_bytes, program_lookups, 1000.0 * lookup_ticks / SDL_GetPerformanceFrequency());
    result=0;
done:
    fx_image_io=0;CdRom_BindImage(NULL,NULL);g_CdImageVolumes[0]=g_CdImageVolumes[1]=NULL;
    pe_disc_close(&disc);pe_disc_close(&disc2);
    return result;
}

static int original_native_shared_storage(void) {
    int saved_counter=g_RenderCounter[0];
    const int inputs[]={0,INT_MAX,-1};
    const int outputs[]={1,INT_MIN,0};
    for(unsigned i=0;i<3;i++){
        g_RenderCounter[0]=inputs[i];Render_IncrementCounter();
        if(g_RenderCounter[0]!=outputs[i])return 0;
    }
    g_RenderCounter[0]=saved_counter;
    if(g_FieldPlacementUpAxis.x || g_FieldPlacementUpAxis.y!=-4096 ||
       g_FieldPlacementUpAxis.z || g_FieldPlacementUpAxis.pad)return 0;
    void *saved_pages[2]={g_GameState.startup_resource_tables[0],g_GameState.startup_resource_tables[1]};
    void *saved_third=g_GameState.field_resource_slots[2];
    int tokens[3]={0};
    g_GameState.startup_resource_tables[0]=&tokens[0];g_GameState.startup_resource_tables[1]=&tokens[1];
    g_GameState.field_resource_slots[2]=&tokens[2];
    int valid=FieldFx_GetEffectResource(0)==&tokens[0] && FieldFx_GetEffectResource(1)==&tokens[1] &&
              FieldFx_GetEffectResource(2)==&tokens[2] && g_GameState.bank_reset_958[0]==&tokens[2] &&
              !FieldFx_GetEffectResource(-1) && !FieldFx_GetEffectResource(3);
    g_GameState.startup_resource_tables[0]=saved_pages[0];g_GameState.startup_resource_tables[1]=saved_pages[1];
    g_GameState.field_resource_slots[2]=saved_third;
    CdlLOC previous=*DsLastPos(NULL),expected={0},actual={0};
    DsIntToPos(24789,&expected);CdControlB(2,(u8 *)&expected,NULL);
    if(DsLastPos(&actual)!=&actual || DsPosToInt(&actual)!=24789 || DsPosToInt(DsLastPos(NULL))!=24789)valid=0;
    CdControlB(2,(u8 *)&previous,NULL);
    if(valid)puts("Native shared storage: resident counter wraps, USA up axis, third pointer alias and image cursor passed");
    return valid;
}

