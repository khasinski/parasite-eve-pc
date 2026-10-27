static int original_actor_render_opcodes(void)
{
    FieldActor *previous=g_CurrentEntity,*previous_player=g_PlayerEntity,*previous_head=g_FieldActorListHead;
    ScriptNode *previous_node=g_ScriptNodePool;u32 *previous_cursor=g_ScriptCursor;
    RenderObjectEntity previous_battle=g_PlayerBattleEntity;
    int previous_lock=g_FieldMoveLock;
    u32 script[16]={0};
    const u32 table_values[]={0,1,0x7fff,0x8000,0xffff,0x10000,0x12345678,0x80000000,0xffffffff,0xffff0007,0xa5a55a5a,0x7fffffff};
    uint64_t table_hash=UINT64_C(14695981039346656037);
    if(g_ScriptQueueOpcodeHandlers[202]!=Script_SetEntityField1E6)return 0;
    for(unsigned i=0;i<sizeof(table_values)/sizeof(table_values[0]);++i){
        FieldActor actor={0};actor.render_object.table_value30=0x1234;actor.render_object.table_index=0xa55a;
        FieldActor before=actor,expected=actor;g_CurrentEntity=&actor;
        int value=(s32)table_values[i];int *args[]={&value};u32 result=Script_SetEntityField1E6(args);
        const u32 words[]={result,(u32)actor.render_object.table_value30|((u32)actor.render_object.table_index<<16)};
        for(unsigned w=0;w<2;++w)for(unsigned b=0;b<4;++b)
            table_hash=(table_hash^((words[w]>>(b*8))&255))*UINT64_C(1099511628211);
        expected.render_object.table_index=actor.render_object.table_index;
        if(memcmp(&actor,&expected,sizeof(actor)))return 0;
        actor=before;u32 program[]={202|(1u<<13),0,table_values[i],32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;
        Script_RunQueue();
        if(result!=1 || memcmp(&actor,&expected,sizeof(actor)) || node.current.script!=program+5 ||
           node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
    }
    if(table_hash!=UINT64_C(0x7b1afe75d03b90c1))return 0;
    if(g_ScriptQueueOpcodeHandlers[62]!=Script_SetEntityField224)return 0;
    uint64_t radius_hash=UINT64_C(14695981039346656037);
    for(unsigned high=0;high<65536;++high){
        FieldActor actor={0},expected;g_CurrentEntity=&actor;
        actor.render_object.hit_cylinder.radius=0x1234;
        actor.render_object.hit_cylinder.bounds_value1=(s16)0xa55a;
        expected=actor;expected.render_object.hit_cylinder.radius=(s16)high;
        int value=(s32)((high<<16)|0x5aa5);int *args[]={&value};
        u32 result=Script_SetEntityField224(args);
        const u32 words[]={result,(u16)actor.render_object.hit_cylinder.radius|
            ((u32)(u16)actor.render_object.hit_cylinder.bounds_value1<<16)};
        for(unsigned w=0;w<2;++w)for(unsigned b=0;b<4;++b)
            radius_hash=(radius_hash^((words[w]>>(b*8))&255))*UINT64_C(1099511628211);
        if(memcmp(&actor,&expected,sizeof(actor)))return 0;
    }
    if(radius_hash!=UINT64_C(0x18f5d8e8953d7b25))return 0;
    for(unsigned i=0;i<sizeof(table_values)/sizeof(table_values[0]);++i){
        FieldActor actor={0},expected=actor;g_CurrentEntity=&actor;
        expected.render_object.hit_cylinder.radius=(s16)(table_values[i]>>16);
        u32 program[]={62|(1u<<13),0,table_values[i],32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;
        Script_RunQueue();
        if(memcmp(&actor,&expected,sizeof(actor)) || node.current.script!=program+5 ||
           node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
    }
    puts("Original cylinder radius opcode 62: all 65536 signed high halfwords match USA CPU; full actor guards and dispatcher passed");
    s16 rates[]={0,1,32,-2,32767};
    for(unsigned player=0;player<2;++player) for(unsigned i=0;i<5;++i) {
        FieldActor actor={.flags=0x8040};ScriptNode node={.flags=0x8000};
        int rate=rates[i];int *args[]={&rate};
        g_CurrentEntity=&actor;g_PlayerEntity=player?&actor:NULL;g_ScriptNodePool=&node;
        g_PlayerBattleEntity=(RenderObjectEntity){0};g_FieldMoveLock=0x82;
        g_ScriptCursor=script+8;
        int duration=rate?rate:1;
        if(Script_PlayEntityAnim(args)!=0 || actor.flags!=0x8000 || node.flags!=0x8020 || node.active!=1 ||
           g_ScriptCursor!=script+5 || !(actor.render_object.flags_9C&4) ||
           actor.render_object.fade_duration!=(s8)duration || actor.render_object.fade_red_step!=(u8)(128/duration)) do { fprintf(stderr,"render opcode failure line %d\n",__LINE__); return 0; } while(0);
        if(player && (g_FieldMoveLock!=0x80 || g_PlayerBattleEntity.fade_duration!=(s8)duration ||
           !(g_PlayerBattleEntity.flags_9C&4))) do { fprintf(stderr,"render opcode failure line %d\n",__LINE__); return 0; } while(0);
        if(!player && (g_FieldMoveLock!=0x82 || g_PlayerBattleEntity.flags_9C)) do { fprintf(stderr,"render opcode failure line %d\n",__LINE__); return 0; } while(0);
        rate=123;g_ScriptCursor=script+8;
        if(Script_PlayEntityAnim(args)!=0 || g_ScriptCursor!=script+5 || actor.render_object.fade_duration!=(s8)duration) do { fprintf(stderr,"render opcode failure line %d\n",__LINE__); return 0; } while(0);
        actor.render_object.flags_9C&=~4u;g_ScriptCursor=script+8;
        if(Script_PlayEntityAnim(args)!=1 || node.flags!=0x8000 || g_ScriptCursor!=script+8) do { fprintf(stderr,"render opcode failure line %d\n",__LINE__); return 0; } while(0);
    }
    u32 saved_view[9];memcpy(saved_view,g_EntityRenderScratch,sizeof(saved_view));
    GteMatrix view={.m={{4096,0,0},{0,4096,0},{0,0,4096}}};
    memset(g_EntityRenderScratch,0,sizeof(saved_view));memcpy(g_EntityRenderScratch,&view,sizeof(view));
    for(unsigned player=0;player<2;++player) for(unsigned part_case=0;part_case<5;++part_case) {
        int raw_part=(int[]){0,1,49,65585,-65487}[part_case];unsigned index=(u16)(s16)raw_part;
        RenderMatrix matrices[50]={0};RenderVec3s bounds[100]={0};
        for(unsigned j=0;j<50;++j) {
            for(unsigned k=0;k<3;++k) matrices[j].rotation[k][k]=4096;
            matrices[j].translation[0]=-100-j*10;matrices[j].translation[1]=200+j*10;matrices[j].translation[2]=-300-j*10;
            bounds[2*j]=(RenderVec3s){-7,9,-11,10};
        }
        FieldActor owner={.type_id=7,.sub_id=11},dead={.type_id=7,.sub_id=11,.flags=0x10,.next=&owner};
        FieldActor actor={.flags=0x8000};
        owner.render_object.bounds_vertices=bounds;owner.render_object.matrices=matrices;
        g_CurrentEntity=&actor;g_PlayerEntity=player?&owner:NULL;g_FieldActorListHead=&dead;
        int type=player?0:7,id=11,part=raw_part;int *args[]={&type,&id,&part};
        if(Script_CopyEntityRenderObject(args)!=1 || actor.parent!=&owner || actor.flags!=0xa000 ||
           actor.render_object.animation_source!=&owner.render_object || actor.render_object.table_index!=index ||
           actor.pos_x!=(-107-(int)index*10)*65536 || actor.pos_y!=(209+(int)index*10)*65536 ||
           actor.pos_z!=(-311-(int)index*10)*65536) do { fprintf(stderr,"render opcode failure line %d\n",__LINE__); return 0; } while(0);
        FieldActor expected=actor;actor=(FieldActor){.flags=0x8000};
        u32 program[]={164|(3u<<13),0,(u32)type,(u32)id,(u32)part,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;
        if(g_ScriptQueueOpcodeHandlers[164]!=Script_CopyEntityRenderObject)return 0;
        Script_RunQueue();
        if(memcmp(&actor,&expected,sizeof(actor)) || node.current.script!=program+7 || node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
    }
    memcpy(g_EntityRenderScratch,saved_view,sizeof(saved_view));
    g_CurrentEntity=previous;g_PlayerEntity=previous_player;g_FieldActorListHead=previous_head;
    g_ScriptNodePool=previous_node;g_ScriptCursor=previous_cursor;g_PlayerBattleEntity=previous_battle;g_FieldMoveLock=previous_lock;
    puts("Original render table opcode 202: 12 USA CPU narrowing states, adjacent fields, whole actor and dispatcher passed");
    puts("Original actor render opcodes: animation start/wait/finish, player battle mirror and native inherited transforms with negative coordinates and opcode164 dispatcher passed");
    return 1;
}

static int original_actor_camera_opcodes(void)
{
    FieldActor *previous=g_CurrentEntity,*previous_player=g_PlayerEntity;
    ScriptNode *previous_node=g_ScriptNodePool;
    u32 *previous_cursor=g_ScriptCursor;
    u32 script[16]={0};
    unsigned angles[]={0,1,511,1024,2047,2048,2049,3584,4095};
    for(unsigned from=0;from<9;++from) for(unsigned to=0;to<9;++to) {
        unsigned start=angles[from],end=angles[to];
        FieldActor actor={.rot_y=start}; ScriptNode node={.flags=0x8000};
        int target=end,step=64;int *args[]={&target,&step};
        g_CurrentEntity=&actor;g_ScriptNodePool=&node;
        unsigned calls=0;
        for(;;) {
            unsigned before=actor.rot_y&0xfff;
            g_ScriptCursor=script+8;
            int finished=Render_CameraStepAngleFade(args);
            if(++calls>80) do { fprintf(stderr,"camera opcode failure line %d\n",__LINE__); return 0; } while(0);
            if(finished) {
                if(actor.rot_y!=end || node.flags!=0x8000 || g_ScriptCursor!=script+8) do { fprintf(stderr,"camera opcode failure line %d\n",__LINE__); return 0; } while(0);
                break;
            }
            if(calls==1) {
                int span=(int)end-(int)start;
                int direction=span<0?-1:1;
                if(span>2048 || span<-2048) direction=-direction;
                if(node.target14!=64*direction) return 0;
            }
            if(!(node.flags&0x20) || node.target18.coordinate!=(int)end || node.active!=1 ||
               g_ScriptCursor!=script+4 || (actor.rot_y&0xfff)!=((before+node.target14)&0xfff)) do { fprintf(stderr,"camera opcode failure line %d\n",__LINE__); return 0; } while(0);
            target=123;step=1; /* A yielded opcode retains its original operands. */
        }
    }
    for(unsigned player=0;player<2;++player) {
        FieldActor actor={.pos_x=100*65536,.pos_z=200*65536,.rot_y=4095,
                          .move_factor=65536,.move_speed=4096};
        ScriptNode node={.flags=0x8000};
        int x=-2*65536,z=3*65536,turn=0;int *args[]={&x,&z,&turn};
        g_CurrentEntity=&actor;g_PlayerEntity=player?&actor:NULL;g_ScriptNodePool=&node;
        unsigned calls=0;
        for(;;) {
            g_ScriptCursor=script+8;
            int finished=Render_CameraTrackRelativeOffset(args);
            if(++calls>200 || node.target18.coordinate!=98*65536 || node.target1c!=203*65536) do { fprintf(stderr,"camera opcode failure line %d\n",__LINE__); return 0; } while(0);
            if(finished) {
                if(actor.pos_x!=98*65536 || actor.pos_z!=203*65536 || actor.motion_x || actor.motion_z ||
                   node.flags!=0x8000 || g_ScriptCursor!=script+8) do { fprintf(stderr,"camera opcode failure line %d\n",__LINE__); return 0; } while(0);
                break;
            }
            if(!(node.flags&0x20) || node.active!=1 || g_ScriptCursor!=script+3) do { fprintf(stderr,"camera opcode failure line %d\n",__LINE__); return 0; } while(0);
            actor.pos_x=(s32)((u32)actor.pos_x+(u32)actor.motion_x);
            actor.pos_z=(s32)((u32)actor.pos_z+(u32)actor.motion_z);
            x=z=0;turn=1;
        }
    }
    for(unsigned player=0;player<2;++player) {
        FieldActor actor={.pos_x=100*65536,.pos_z=200*65536,.rot_y=4095,
                          .move_factor=65536,.move_speed=4096};
        ScriptNode node={.flags=0x8000};
        int x=-100*65536,z=150*65536,turn=64;int *args[]={&x,&z,&turn};
        g_CurrentEntity=&actor;g_PlayerEntity=player?&actor:NULL;g_ScriptNodePool=&node;
        for(unsigned frame=0;frame<3;++frame) {
            g_ScriptCursor=script+8;
            if(Render_CameraTrackRelativeOffset(args)!=0 || actor.rot_y!=63+64*frame ||
               node.target18.coordinate!=0 || node.target1c!=350*65536 || node.target14!=64 ||
               node.flags!=0x8020 || node.active!=1 || g_ScriptCursor!=script+3) return 0;
            actor.pos_x=(s32)((u32)actor.pos_x+(u32)actor.motion_x);
            actor.pos_z=(s32)((u32)actor.pos_z+(u32)actor.motion_z);
            x=z=0;turn=1;
        }
    }
    g_CurrentEntity=previous;g_PlayerEntity=previous_player;g_ScriptNodePool=previous_node;g_ScriptCursor=previous_cursor;
    puts("Original actor camera opcodes: angle wrap, retained operands, native cursor rewinds, relative movement and player speed path passed");
    return 1;
}

static int original_mode_wait_opcode(void)
{
    const u32 patterns[]={0,1,0x7fff,0x8000,0xffff,0x80000000,0xffffffff,
        0x010c0000,0xa5a55a5a,0x12345678,0x7fffffff,0x600000};
    const int active_values[]={0,1,255},flag_values[]={0,128,255};
    int saved_flags=g_GeomScroll.state.flags;ScriptNode *saved_node=g_ScriptNodePool;
    u32 *saved_cursor=g_ScriptCursor;FieldActor *saved_actor=g_CurrentEntity,actor={0};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[165]!=Boot_CheckModeSwitch)return 0;
    for(unsigned pattern=0;pattern<12;++pattern)for(unsigned mode=0;mode<8;++mode)
    for(unsigned active=0;active<3;++active)for(unsigned flags=0;flags<3;++flags){
        u32 program[8]={0};ScriptNode node={.active=active_values[active],.flags=flag_values[flags]};
        ScriptNode expected=node;g_ScriptNodePool=&node;g_ScriptCursor=program+4;
        g_GeomScroll.state.flags=(s32)((patterns[pattern]&~7u)|mode);
        u32 result=Boot_CheckModeSwitch(NULL);
        const u32 words[]={result,(u32)((u8 *)g_ScriptCursor-(u8 *)program),(u32)node.active,
            node.flags,(u32)g_GeomScroll.state.flags};
        for(unsigned word=0;word<5;++word)for(unsigned byte=0;byte<4;++byte)
            hash=(hash^((words[word]>>(byte*8))&255))*UINT64_C(1099511628211);
        expected.active=node.active;
        if(memcmp(&node,&expected,sizeof(node)))return 0;
        ++cases;
    }
    if(cases!=864 || hash!=UINT64_C(0x8ed4b8d9c2727b75))return 0;
    u32 program[]={165,0,32,0};ScriptNode node={.current.script=program,.active=1,.flags=0x80};
    g_CurrentEntity=&actor;g_GeomScroll.state.flags=1;g_ScriptNodePool=&node;
    Script_RunQueue();
    if(node.current.script!=program || node.active!=1 || node.flags!=0x80 || g_ScriptNodePool)return 0;
    g_GeomScroll.state.flags=4;g_ScriptNodePool=&node;Script_RunQueue();
    if(node.current.script!=program+4 || node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
    g_GeomScroll.state.flags=saved_flags;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    g_CurrentEntity=saved_actor;
    puts("Original mode wait opcode 165: 864 unhooked USA CPU states and native dispatcher yield/resume passed");
    return 1;
}

static int original_tracking_angle_opcode(void)
{
    FieldActor *saved_actor=g_CurrentEntity,*saved_player=g_PlayerEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    const u32 factors[]={0,65536,327680,0x80000000,0x7fffffff};
    const u16 speeds[]={0,1,4096,65535};
    const u32 poses[][4]={{0,0,0,0},{100u<<16,200u<<16,98u<<16,203u<<16},
        {(u32)-100<<16,50u<<16,300u<<16,(u32)-200<<16},
        {0x7fff0000,0x80000000,0x80010000,0x7ffe0000},{0,0,1,0},
        {(u32)-1000<<16,300u<<16,1000u<<16,(u32)-300<<16}};
    const s16 yaws[]={-1,0,1,2048,4095,32767};
    const u32 turns[]={0,1,64,(u32)-64,0x80000000};
    uint64_t hash=UINT64_C(14695981039346656037);
    if(g_ScriptQueueOpcodeHandlers[219]!=Render_CameraTrackEntityAngle)return 0;
    for(unsigned player=0;player<2;++player)for(unsigned resumed=0;resumed<2;++resumed)
    for(unsigned factor=0;factor<5;++factor)for(unsigned speed=0;speed<4;++speed)
    for(unsigned pose=0;pose<6;++pose)for(unsigned yaw=0;yaw<6;++yaw)for(unsigned turn=0;turn<5;++turn){
        FieldActor actor={.pos_x=(s32)poses[pose][0],.pos_z=(s32)poses[pose][1],.rot_y=yaws[yaw],
            .move_factor=(s32)factors[factor],.move_speed=speeds[speed],.motion_x=0x1234,.motion_z=0x5678,.flags=0x240};
        FieldActor before=actor;
        ScriptNode node={.flags=resumed?0xa0:0x80,.active=7,.target14=resumed?(s32)poses[pose][2]:0x1234,
            .target18.coordinate=resumed?(s32)poses[pose][3]:0x5678,.target1c=resumed?(s32)turns[turn]:0x9abc};
        ScriptNode node_before=node;u32 storage[32]={0};
        int values[]={resumed?0x11220000:(s32)poses[pose][2],resumed?0x33440000:(s32)poses[pose][3],resumed?7:(s32)turns[turn]};
        int *args[]={values,values+1,values+2};
        g_CurrentEntity=&actor;g_PlayerEntity=player?&actor:NULL;g_ScriptNodePool=&node;g_ScriptCursor=storage+16;
        u32 result=Render_CameraTrackEntityAngle(args);
        const u32 words[]={result,(u32)actor.pos_x,(u32)actor.pos_z,(u32)actor.motion_x,(u32)actor.motion_z,
            (u16)actor.rot_y,actor.flags,node.flags,(u32)node.active,(u32)node.target14,
            (u32)node.target18.coordinate,(u32)node.target1c,(u32)(g_ScriptCursorAddress-(uintptr_t)storage)};
        for(unsigned w=0;w<13;++w)for(unsigned b=0;b<4;++b)
            hash=(hash^((words[w]>>(b*8))&255))*UINT64_C(1099511628211);
        FieldActor expected=before;expected.pos_x=actor.pos_x;expected.pos_z=actor.pos_z;
        expected.motion_x=actor.motion_x;expected.motion_z=actor.motion_z;expected.rot_y=actor.rot_y;
        if(memcmp(&actor,&expected,sizeof(actor)))return 0;
        ScriptNode after=node;actor=before;node=node_before;
        u32 program[]={219|(3u<<13),0,(u32)values[0],(u32)values[1],(u32)values[2],32,0};
        node.current.script=program;node.active=1;g_ScriptNodePool=&node;Script_RunQueue();
        if(memcmp(&actor,&expected,sizeof(actor)) || node.current.script!=program+(result?7:0) ||
           node.active!=!result || node.flags!=(after.flags|(result?0x10:0)) ||
           node.target14!=after.target14 || node.target18.coordinate!=after.target18.coordinate ||
           node.target1c!=after.target1c || g_ScriptNodePool)return 0;
    }
    g_CurrentEntity=saved_actor;g_PlayerEntity=saved_player;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0xc90373fa22ae5275)){fprintf(stderr,"Tracking angle hash %016llx\n",(unsigned long long)hash);return 0;}
    puts("Original tracking opcode 219: 14400 unhooked USA CPU states, player scaling, signed wrap, retained goals, turn/arrival and native dispatcher passed");
    return 1;
}

static int original_absolute_movement_opcode(void)
{
    FieldActor *previous=g_CurrentEntity,*previous_player=g_PlayerEntity;
    ScriptNode *previous_node=g_ScriptNodePool;u32 *previous_cursor=g_ScriptCursor;
    u32 script[16]={0};
    const int ranges[][4]={{0,0,1,0},{0,1,2,0},{2,1,-2,0},{1,3,1,1},{3,0,1,1},{0,3,-1,1},{2,2,-1,0}};
    for(unsigned i=0;i<7;++i) {
        FieldActor actor={.action=3,.anim_step=ranges[i][2]*65536,.anim_frame_target=ranges[i][1]};
        actor.anim.fixed=ranges[i][0]*65536;
        ScriptNode node={.flags=0x8000};
        g_CurrentEntity=&actor;g_ScriptNodePool=&node;g_ScriptCursor=script+8;
        if(Script_WaitAnimRange(NULL)!=0 || node.active!=1 || node.flags!=0x8000 ||
           g_ScriptCursor!=script+(ranges[i][3]?6:8)) return 0;
    }
    for(unsigned player=0;player<2;++player) for(unsigned turns=0;turns<2;++turns) {
        FieldActor absolute={.pos_x=100*65536,.pos_z=200*65536,.rot_y=4095,
                             .move_factor=65536,.move_speed=4096};
        FieldActor relative=absolute;
        ScriptNode absolute_node={.flags=0x8000},relative_node=absolute_node;
        int dx=-2*65536,dz=3*65536,turn=turns?64:0;
        int x=98*65536,z=203*65536,absolute_turn=turn;
        int *abs_args[]={&x,&z,&absolute_turn},*rel_args[]={&dx,&dz,&turn};
        for(unsigned frame=0;frame<80;++frame) {
            g_CurrentEntity=&absolute;g_PlayerEntity=player?&absolute:NULL;
            g_ScriptNodePool=&absolute_node;g_ScriptCursor=script+8;
            int abs_result=Script_MoveTowardPoint(abs_args);
            u32 *abs_cursor=g_ScriptCursor;
            g_CurrentEntity=&relative;g_PlayerEntity=player?&relative:NULL;
            g_ScriptNodePool=&relative_node;g_ScriptCursor=script+8;
            int rel_result=Render_CameraTrackRelativeOffset(rel_args);
            if(abs_result!=rel_result || abs_cursor!=g_ScriptCursor ||
               absolute.pos_x!=relative.pos_x || absolute.pos_z!=relative.pos_z ||
               absolute.motion_x!=relative.motion_x || absolute.motion_z!=relative.motion_z ||
               absolute.rot_y!=relative.rot_y || absolute_node.flags!=relative_node.flags ||
               absolute_node.active!=relative_node.active) return 0;
            if(abs_result) {
                if(absolute.pos_x!=98*65536 || absolute.pos_z!=203*65536 || absolute.motion_x || absolute.motion_z) return 0;
                break;
            }
            if(abs_cursor!=script+3 || absolute_node.target14!=98*65536 ||
               absolute_node.target18.coordinate!=203*65536 || absolute_node.target1c!=(turns?64:0) || absolute_node.active!=1) return 0;
            if(!turns && frame==79) return 0;
            absolute.pos_x=(s32)((u32)absolute.pos_x+(u32)absolute.motion_x);
            absolute.pos_z=(s32)((u32)absolute.pos_z+(u32)absolute.motion_z);
            relative.pos_x=absolute.pos_x;relative.pos_z=absolute.pos_z;
            x=z=dx=dz=0;turn=absolute_turn=1;
        }
    }
    g_CurrentEntity=previous;g_PlayerEntity=previous_player;g_ScriptNodePool=previous_node;g_ScriptCursor=previous_cursor;
    puts("Original absolute movement opcode: relative/absolute targets agree across player speed, turning, yields, cached operands and completion");
    return 1;
}

static int original_resident_script_program(void)
{
    FieldActor *previous=g_CurrentEntity,*previous_player=g_PlayerEntity;
    ScriptNode *previous_node=g_ScriptNodePool;u32 *previous_cursor=g_ScriptCursor;
    u32 previous_variable=g_ScriptVariableBank[7],previous_id=g_ScriptVariableBank[8],previous_sfx=g_ScriptVariableBank[9];
    if(g_ScriptQueueOpcodeHandlers[68]!=Script_MoveTowardPoint ||
       g_ScriptQueueOpcodeHandlers[97]!=Render_CameraTrackRelativeOffset ||
       g_ScriptQueueOpcodeHandlers[146]!=Script_PlayEntityAnim) do { fprintf(stderr,"resident script failure line %d\n",__LINE__); return 0; } while(0);
    for(unsigned player=0;player<2;++player) {
        u32 script[]={39|(1u<<13),0,300,38|(1u<<13)|(2u<<17),0,8,
                      37|(1u<<13)|(2u<<17),0,9,68|(3u<<13),0,98*65536,203*65536,0,
                      10|(2u<<13)|(2u<<17),0,7,77,32,0};
        FieldActor actor={.pos_x=100*65536,.pos_z=200*65536,.rot_y=4095,
                          .move_factor=65536,.move_speed=4096};
        ScriptNode node={.current.script=script,.active=1};
        actor.field_sfx_id=54321;
        g_CurrentEntity=&actor;g_PlayerEntity=player?&actor:NULL;g_ScriptVariableBank[7]=0;
        unsigned ticks=0;
        while(!(node.flags&0x10)) {
            if(++ticks>80) do { fprintf(stderr,"resident script failure line %d\n",__LINE__); return 0; } while(0);
            g_ScriptNodePool=&node;Script_RunQueue();
            if(g_ScriptNodePool!=NULL) do { fprintf(stderr,"resident script failure line %d\n",__LINE__); return 0; } while(0);
            if(node.flags&0x10) {
                if(actor.pos_x!=98*65536 || actor.pos_z!=203*65536 || actor.motion_x || actor.motion_z ||
                   g_ScriptVariableBank[7]!=77 || node.current.script!=script+20 ||
                   actor.sub_id!=44 || g_ScriptVariableBank[8]!=44 || g_ScriptVariableBank[9]!=54321) do { fprintf(stderr,"resident script failure line %d\n",__LINE__); return 0; } while(0);
            } else {
                if(node.current.script!=script+9 || node.active!=1 || g_ScriptVariableBank[7]) do { fprintf(stderr,"resident script failure line %d\n",__LINE__); return 0; } while(0);
                actor.pos_x=(s32)((u32)actor.pos_x+(u32)actor.motion_x);
                actor.pos_z=(s32)((u32)actor.pos_z+(u32)actor.motion_z);
            }
        }
    }
    g_CurrentEntity=previous;g_PlayerEntity=previous_player;g_ScriptNodePool=previous_node;
    g_ScriptCursor=previous_cursor;g_ScriptVariableBank[7]=previous_variable;
    g_ScriptVariableBank[8]=previous_id;g_ScriptVariableBank[9]=previous_sfx;
    puts("Original resident interpreter: real dispatcher, movement, assignment, yields and node termination passed");
    return 1;
}

static int original_wait_opcode_bindings(void)
{
    const u8 enabled[]={0,1,2,127,128,255},wide[]={0,1,255};
    const s16 elapsed[]={-32768,-1,0,1,2,32767};
    const s32 targets[]={-32768,-1,0,1,2,32767,32768,INT32_MIN,INT32_MAX};
    u8 saved_enabled=g_DrawEnabled.bits,saved_wide=g_MovieWide;short saved_elapsed=g_SeqElapsed;
    uintptr_t saved_cursor=g_ScriptCursorAddress;ScriptNode *saved_node=g_ScriptNodePool;
    FieldActor *saved_actor=g_CurrentEntity;
    int (*handlers[])(int **)={Script_WaitFrameOrDraw,Entity_YieldOnDrawBusy};
    const unsigned opcodes[]={140,143};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    for(unsigned op=0;op<2;++op)for(unsigned e=0;e<6;++e)for(unsigned time=0;time<6;++time)
    for(unsigned target=0;target<9;++target)for(unsigned w=0;w<3;++w){
        u32 program[6]={0};ScriptNode node={.active=7};int value=targets[target],*args[]={&value};
        g_DrawEnabled.bits=enabled[e];g_MovieWide=wide[w];g_SeqElapsed=elapsed[time];
        g_ScriptNodePool=&node;g_ScriptCursor=program+3;
        if(g_ScriptQueueOpcodeHandlers[opcodes[op]]!=handlers[op])return 0;
        u32 result=g_ScriptQueueOpcodeHandlers[opcodes[op]](args);
        const u32 values[]={result,(u32)(g_ScriptCursorAddress-(uintptr_t)program),(u32)node.active};
        for(unsigned i=0;i<3;++i)for(unsigned b=0;b<4;++b)
            hash=(hash^((values[i]>>(b*8))&255))*UINT64_C(1099511628211);
        if(value!=targets[target])return 0;++cases;
    }
    if(cases!=1944 || hash!=UINT64_C(0x5ccf0cb9f8ead07a))return 0;
    FieldActor actor={0};g_CurrentEntity=&actor;
    u32 program[]={140|(1u<<13),0,2,143,0,32,0};
    ScriptNode node={.current.script=program,.active=1,.flags=0x80};
    g_DrawEnabled.bits=2;g_MovieWide=1;g_SeqElapsed=1;
    g_ScriptNodePool=&node;Script_RunQueue();
    if(node.current.script!=program || node.active!=1 || (node.flags&0x10))return 0;
    g_SeqElapsed=2;g_ScriptNodePool=&node;Script_RunQueue();
    if(node.current.script!=program+3 || node.active!=1 || (node.flags&0x10))return 0;
    g_DrawEnabled.bits=0;g_ScriptNodePool=&node;Script_RunQueue();
    if(node.current.script!=program+7 || !(node.flags&0x10) || g_ScriptNodePool)return 0;
    g_DrawEnabled.bits=saved_enabled;g_MovieWide=saved_wide;g_SeqElapsed=saved_elapsed;
    g_ScriptCursorAddress=saved_cursor;g_ScriptNodePool=saved_node;g_CurrentEntity=saved_actor;
    puts("Original frame/draw waits: 1944 unhooked USA MIPS states and three-tick dispatcher yield/resume/finish passed");
    return 1;
}

static int original_field_sfx_opcode_binding(void)
{
    const u32 patterns[]={0,1,127,128,255,256,0xffffffff,0x80000000};
    const u16 senders[]={0,1,65535,0x1234};
    FieldActor *saved_actor=g_CurrentEntity,*saved_head=g_FieldActorListHead;
    ScriptNode *saved_pool=g_ScriptNodePool,*saved_free=g_ScriptNodeFreeListHead;
    u32 *saved_cursor=g_ScriptCursor;u16 saved_seq=g_ScriptNodeSeqCounter[0];
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    Script_ClearSfxTable();
    if(g_ScriptQueueOpcodeHandlers[28]!=Script_PlayFieldSfx)return 0;
#define SFX_PUT(value,bytes) do {u32 v_=(u32)(value);for(unsigned b_=0;b_<(bytes);++b_){hash=(hash^(u8)v_)*UINT64_C(1099511628211);v_>>=8;}}while(0)
    for(unsigned kind=0;kind<8;++kind)for(unsigned sub=0;sub<8;++sub)
    for(unsigned task=0;task<8;++task)for(unsigned sender=0;sender<4;++sender){
        u32 callback[]={32,0};
        FieldActor actor={.field_sfx_id=senders[sender]};
        FieldActor recipient={.type_id=(u8)patterns[kind],.sub_id=(u8)patterns[sub],.script_cursor_19c=callback};
        ScriptNode available[2]={{0}};available[0].next=available+1;
        g_CurrentEntity=&actor;g_FieldActorListHead=&recipient;g_ScriptNodeFreeListHead=available;
        u32 program[]={28|(3u<<13),0,patterns[kind],patterns[sub],patterns[task],32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;
        Script_RunQueue();
        if(!(node.flags&0x10) || node.current.script!=program+7 || g_ScriptNodePool || g_FieldSfxQueueCount!=1)return 0;
        SFX_PUT(1,4);SFX_PUT(g_FieldSfxQueueCount,1);
        const u8 *payload=(const u8 *)&g_FieldSfxQueue[0];
        for(unsigned byte=0;byte<sizeof(FieldSfxQueueEntry);++byte)SFX_PUT(payload[byte],1);
        Scene_UpdateEntityList();ScriptNode *event=recipient.script_node_lists[2];
        if(g_FieldSfxQueueCount)return 0;
        if((u16)patterns[kind]>255){
            if(event || g_ScriptNodeFreeListHead!=available)return 0;
        }else if(event!=available || event->current.script!=callback || event->flags!=4 || event->active!=1 ||
           event->target14!=(u8)patterns[task] || event->trigger_value!=senders[sender] ||
           g_ScriptNodeFreeListHead!=available+1)return 0;
        ++cases;
    }
#undef SFX_PUT
    g_CurrentEntity=saved_actor;g_FieldActorListHead=saved_head;g_ScriptNodePool=saved_pool;
    g_ScriptNodeFreeListHead=saved_free;g_ScriptCursor=saved_cursor;g_ScriptNodeSeqCounter[0]=saved_seq;
    if(cases!=2048 || hash!=UINT64_C(0xafaf0f79b0115df5))return 0;
    puts("Original field-event opcode: 2048 USA MIPS payloads match full dispatcher, queue and native recipient-node delivery");
    return 1;
}

static int original_flag_opcode_bindings(void)
{
    const u32 patterns[]={0,1,2,3,0x40,0x41,0x80,0x7fffffff,0x80000000,0xffffffff,0x12345678,0xfedcba98};
    int (*handlers[])(int **)={Entity_ClearGlobalFlag1,Entity_SetGlobalFlag1,Entity_SetCurrentFlag40,Entity_ClearCurrentFlag40};
    FieldActor *saved_actor=g_CurrentEntity;ScriptNode *saved_node=g_ScriptNodePool;
    u32 *saved_cursor=g_ScriptCursor;int saved_lock=g_FieldMoveLock;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    for(unsigned op=0;op<4;++op)for(unsigned m=0;m<12;++m)for(unsigned f=0;f<12;++f){
        FieldActor actor={.flags=patterns[f]};g_CurrentEntity=&actor;g_FieldMoveLock=(s32)patterns[m];
        if(g_ScriptQueueOpcodeHandlers[63+op]!=handlers[op])return 0;
        u32 result=g_ScriptQueueOpcodeHandlers[63+op](NULL);
        const u32 values[]={result,(u32)g_FieldMoveLock,actor.flags};
        for(unsigned i=0;i<3;++i)for(unsigned b=0;b<4;++b)
            hash=(hash^((values[i]>>(b*8))&255))*UINT64_C(1099511628211);
        u32 program[]={63+op,0,32,0};ScriptNode node={.current.script=program,.active=1,.flags=0x80};
        g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || !(node.flags&0x10) || node.current.script!=program+4 || g_ScriptNodePool ||
           (u32)g_FieldMoveLock!=values[1] || actor.flags!=values[2])return 0;
        ++cases;
    }
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;g_FieldMoveLock=saved_lock;
    if(cases!=576 || hash!=UINT64_C(0x4c0f80b0374806b5))return 0;
    RenderTintState saved_tint;memcpy(&saved_tint,&g_ScreenTint,sizeof(saved_tint));
    hash=UINT64_C(14695981039346656037);
    FieldActor actor={0};g_CurrentEntity=&actor;
    if(g_ScriptQueueOpcodeHandlers[136]!=Script_ClearEntityFlag40)return 0;
    for(unsigned fade=0;fade<256;++fade){
        memcpy(&g_ScreenTint,&saved_tint,sizeof(saved_tint));g_ScreenTint.fadeMode=fade;
        u32 result=g_ScriptQueueOpcodeHandlers[136](NULL);
        for(unsigned b=0;b<4;++b)hash=(hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
        hash=(hash^g_ScreenTint.fadeMode)*UINT64_C(1099511628211);
        RenderTintState expected;memcpy(&expected,&saved_tint,sizeof(expected));expected.fadeMode=g_ScreenTint.fadeMode;
        u32 program[]={136,0,32,0};ScriptNode node={.current.script=program,.active=1,.flags=0x80};
        g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || !(node.flags&0x10) || node.current.script!=program+4 || g_ScriptNodePool ||
           memcmp(&g_ScreenTint,&expected,sizeof(expected)))return 0;
    }
    int (*noops[])(int **)={Script_Noop1,Script_Noop2,Script_Noop4,Script_Noop3};
    const unsigned opcodes[]={141,142,144,145};
    for(unsigned i=0;i<4;++i){
        if(g_ScriptQueueOpcodeHandlers[opcodes[i]]!=noops[i] || noops[i](NULL)!=1)return 0;
        u32 program[]={opcodes[i],0,32,0};ScriptNode node={.current.script=program,.active=1,.flags=0x80};
        g_ScriptNodePool=&node;Script_RunQueue();
        if(!(node.flags&0x10) || node.current.script!=program+4 || g_ScriptNodePool)return 0;
    }
    memcpy(&g_ScreenTint,&saved_tint,sizeof(saved_tint));
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0x14fd97a6d6c2c325))return 0;
    u32 saved_flags=g_GameStateFlags;
    const u32 flag_values[]={0,1,2,4,0x100,0x800,0x2000,0x80000000,0xffffffff,0xa5a55a5a};
    int (*global_handlers[])(int **)={Scene_TestGlobalFlags,Scene_SetGlobalFlags,Scene_ClearGlobalFlags};
    uint64_t global_hash=UINT64_C(14695981039346656037);
    g_CurrentEntity=&actor;
    for(unsigned op=81;op<=83;++op)for(unsigned f=0;f<10;++f)for(unsigned m=0;m<10;++m){
        int mask=(s32)flag_values[m],out=(s32)0x98765432u;int *args[]={&mask,&out};
        if(g_ScriptQueueOpcodeHandlers[op]!=global_handlers[op-81])return 0;
        g_GameStateFlags=flag_values[f];u32 result=g_ScriptQueueOpcodeHandlers[op](args);
        const u32 values[]={result,g_GameStateFlags,(u32)out};
        for(unsigned i=0;i<3;++i)for(unsigned byte=0;byte<4;++byte)
            global_hash=(global_hash^((values[i]>>(byte*8))&255))*UINT64_C(1099511628211);
        g_GameStateFlags=flag_values[f];
        u32 program[]={op|((op==81?2u:1u)<<13),0,flag_values[m],op==81?0x98765432u:32u,op==81?32u:0u,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || g_GameStateFlags!=values[1] || (op==81 && program[3]!=values[2]) ||
           !(node.flags&0x10) || node.current.script!=program+(op==81?6:5) || g_ScriptNodePool)return 0;
    }
    g_GameStateFlags=saved_flags;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(global_hash!=UINT64_C(0x7d8e36c81c7f7588))return 0;
    u8 saved_escape=g_BattleEnemyEscapeFlag;
    if(g_ScriptQueueOpcodeHandlers[207]!=Script_SignalSceneExit)return 0;
    g_CurrentEntity=&actor;g_BattleEnemyEscapeFlag=0;
    u32 exit_program[]={207,0,32,0};ScriptNode exit_node={.current.script=exit_program,.active=1,.flags=0x80};
    g_ScriptNodePool=&exit_node;Script_RunQueue();
    if(g_BattleEnemyEscapeFlag!=1 || exit_node.current.script!=exit_program+4 || exit_node.flags!=0x90 ||
       exit_node.active || g_ScriptNodePool)return 0;
    g_BattleEnemyEscapeFlag=saved_escape;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    puts("Original battle sequence signal opcode 207: native escape latch and real dispatcher completion passed");
    /* Remaining pure state/math handlers: unhooked USA instructions, including
     * the complete integer-sqrt callee, supply the independent stream hash. */
    {
        const unsigned ops[]={71,76,218,235,236};
        int (*handlers[])(int **)={Script_IsBattlePhase4,Script_Noop,Script_GetSqrt,
            Script_SetEntityShadowFlag,Script_ClearEntityShadowFlag};
        int saved_geom=g_GeomScroll.state.flags;
        uint64_t state_hash=UINT64_C(14695981039346656037);unsigned cases=0;
        g_CurrentEntity=&actor;
        for(unsigned op=0;op<5;++op)for(unsigned i=0;i<131072;++i){
            u32 value=i<65536?i:((i-65536)<<16)|0xa55au;
            struct {u32 before;int input,output;u32 after;} words={0x11223344,(s32)value,(s32)0x98765432,0xa55a5aa5};
            int *args[]={op==0?&words.output:&words.input,&words.output};
            if(g_ScriptQueueOpcodeHandlers[ops[op]]!=handlers[op])return 0;
            actor.flags=value;g_GeomScroll.state.flags=(s32)value;
            u32 result=handlers[op](args);
            const u32 expected[]={result,actor.flags,(u32)g_GeomScroll.state.flags,
                (u32)words.output,(u32)words.input,words.before,words.after};
            for(unsigned k=0;k<7;++k)for(unsigned byte=0;byte<4;++byte)
                state_hash=(state_hash^((expected[k]>>(byte*8))&255))*UINT64_C(1099511628211);
            actor.flags=value;g_GeomScroll.state.flags=(s32)value;
            u32 program[6]={ops[op],0,32,0};unsigned length=4;
            if(op==0){program[0]|=1u<<13;program[2]=0x98765432;program[3]=32;program[4]=0;length=5;}
            if(op==2){program[0]|=2u<<13;program[2]=value;program[3]=0x98765432;program[4]=32;program[5]=0;length=6;}
            ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
            if(result!=1 || actor.flags!=expected[1] || (u32)g_GeomScroll.state.flags!=expected[2] ||
               (op==0 && program[2]!=expected[3]) || (op==2 && (program[2]!=value || program[3]!=expected[3])) ||
               words.input!=(s32)value || words.before!=0x11223344 || words.after!=0xa55a5aa5 ||
               node.current.script!=program+length || node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
            ++cases;
        }
        g_GeomScroll.state.flags=saved_geom;
        g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
        if(cases!=655360 || state_hash!=UINT64_C(0x644731ef64fd8971)){
            fprintf(stderr,"State/math opcode mismatch: cases=%u hash=%016llx\n",cases,(unsigned long long)state_hash);return 0;
        }
        puts("Original opcodes 71/76/218/235/236: 655360 USA CPU states, full sqrt callee, real dispatcher and operand guards passed");
    }
    const u32 actor_values[]={0,1,8,0x100,0x400,0x508,0x7fffffff,0x80000000,0xffffffff,0x12345678,0x20,0xa5a55a5a};
    const unsigned actor_ops[]={74,79,80,185,186,193,194,195};
    int (*actor_handlers[])(int **)={Entity_TestCurrentFlags,Entity_SetCurrentFlag100,Entity_ClearCurrentFlag100,
        Script_ClearEntityFlag1,Script_SetEntityFlag1,Script_SetEntityFlag400,Script_ClearEntityFlag400,Script_WaitEntityFlags8};
    uint64_t actor_hash=UINT64_C(14695981039346656037);g_CurrentEntity=&actor;
    for(unsigned op=0;op<8;++op)for(unsigned f=0;f<12;++f)for(unsigned m=0;m<12;++m)for(unsigned phase=0;phase<2;++phase){
        if(g_ScriptQueueOpcodeHandlers[actor_ops[op]]!=actor_handlers[op])return 0;
        int mask=(s32)actor_values[m],out=(s32)0x98765432;int *args[]={&mask,&out};u32 scratch[16]={0};
        ScriptNode node={.active=7,.flags=phase?0xa0:0x80};g_ScriptNodePool=&node;g_ScriptCursor=scratch+8;actor.flags=actor_values[f];
        u32 result=actor_handlers[op](args);const u32 words[]={result,actor.flags,(u32)out,node.flags,(u32)node.active,
            (u32)(g_ScriptCursorAddress-(uintptr_t)scratch)};
        for(unsigned i=0;i<6;++i)for(unsigned b=0;b<4;++b)actor_hash=(actor_hash^((words[i]>>(b*8))&255))*UINT64_C(1099511628211);
        actor.flags=actor_values[f];u32 program[6]={actor_ops[op],0,32,0};unsigned length=4;
        if(op==0){program[0]|=2u<<13;program[2]=(u32)mask;program[3]=0x98765432;program[4]=32;length=6;}
        node=(ScriptNode){.current.script=program,.active=1,.flags=phase?0xa0:0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(actor.flags!=words[1] || (op==0 && program[3]!=(u32)out) || node.current.script!=program+(result?length:0) ||
           node.active!=(result?0:1) || node.flags!=(words[3]|(result?0x10:0)) || g_ScriptNodePool)return 0;
    }
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(actor_hash!=UINT64_C(0xab04cf6d428f3bf9)){fprintf(stderr,"Actor flag hash %016llx\n",(unsigned long long)actor_hash);return 0;}
    puts("Original actor flag opcodes 74/79/80/185/186/193/194/195: 2304 unhooked USA CPU cases, all-bit mask test, native flag updates and dispatcher wait/retry passed");
    puts("Original global flag opcodes 81/82/83: 300 USA CPU states, high-bit masks and real dispatcher passed");
    puts("Original flag opcodes: 576 USA MIPS cases, native handler ABI and full dispatcher/node completion passed");
    puts("Original fade release opcode: all 256 USA MIPS values, dispatcher completion and untouched native tint state passed");
    return 1;
}

static int original_polygon_opcode(void)
{
    const s32 polygons[][8]={{-10,-10,10,-10,10,10,-10,10},{-10,10,10,10,10,-10,-10,-10},
        {-10,-10,10,-10,0,0,-10,10},{0,0,0,0,0,0,0,0},
        {INT32_MIN,INT32_MIN,INT32_MAX,INT32_MIN,INT32_MAX,INT32_MAX,INT32_MIN,INT32_MAX},
        {INT32_MAX,-1,INT32_MIN,1,0,INT32_MAX,1,INT32_MIN}};
    const u32 points[]={0x80000000,0xffffffff,0,1,65535,65536,0x7fffffff,0x100000,0xfff00000};
    FieldActor *saved_actor=g_CurrentEntity;ScriptNode *saved_node=g_ScriptNodePool;
    u32 *saved_cursor=g_ScriptCursor;u32 saved_var=g_ScriptVariableBank[9];
    FieldActor actor={0};g_CurrentEntity=&actor;uint64_t hash=UINT64_C(14695981039346656037);
    if(g_ScriptQueueOpcodeHandlers[119]!=Script_PointInPoly)return 0;
    for(unsigned p=0;p<6;++p)for(unsigned x=0;x<9;++x)for(unsigned z=0;z<9;++z){
        int data[11];memcpy(data,polygons[p],sizeof(polygons[p]));
        data[8]=(s32)points[x];data[9]=(s32)points[z];data[10]=(s32)0xa5a5a5a5;
        int before[11];memcpy(before,data,sizeof(data));int *args[11];
        for(unsigned i=0;i<11;++i)args[i]=data+i;
        u32 result=Script_PointInPoly(args);u32 output[]={result,(u32)data[10]};
        for(unsigned i=0;i<2;++i)for(unsigned b=0;b<4;++b)
            hash=(hash^((output[i]>>(b*8))&255))*UINT64_C(1099511628211);
        if(memcmp(data,before,10*sizeof(int))){fprintf(stderr,"Polygon input changed %u %u %u\n",p,x,z);return 0;}
        u32 program[15]={119|(11u<<13),2u<<15};
        for(unsigned i=0;i<10;++i)program[i+2]=(u32)before[i];
        program[12]=9;program[13]=32;g_ScriptVariableBank[9]=0xa5a5a5a5;
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || g_ScriptVariableBank[9]!=output[1] || node.current.script!=program+15 ||
           !(node.flags&0x10) || g_ScriptNodePool){fprintf(stderr,"Polygon dispatch %u %u %u result=%u var=%u want=%u cursor=%llu flags=%x\n",p,x,z,result,g_ScriptVariableBank[9],output[1],(unsigned long long)(node.current.script-program),node.flags);return 0;}
    }
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;g_ScriptVariableBank[9]=saved_var;
    if(hash!=UINT64_C(0xd928a71a360ec985)){fprintf(stderr,"Polygon hash %016llx\n",(unsigned long long)hash);return 0;}
    puts("Original polygon opcode: 486 USA MIPS boundary, winding, degenerate and overflow states, eleven operands and native dispatcher passed");
    {
        s32 shapes[10][8];memcpy(shapes,polygons,sizeof(polygons));
        const s32 extra[][8]={{-10*65536,-10*65536,10*65536,-10*65536,10*65536,10*65536,-10*65536,10*65536},
            {-10*65536,10*65536,10*65536,10*65536,10*65536,-10*65536,-10*65536,-10*65536},
            {0,0,65536,0,65536,65536,0,65536},{-65536,-65536,65536,-65536,0,0,-65536,65536}};
        memcpy(shapes+6,extra,sizeof(extra));u32 samples[13];memcpy(samples,points,sizeof(points));
        samples[9]=10*65536;samples[10]=(u32)(-10*65536);samples[11]=0x7fff8000;samples[12]=0x80008000;
        uint64_t actor_hash=UINT64_C(14695981039346656037);unsigned cases=0;
        if(g_ScriptQueueOpcodeHandlers[21]!=Script_PlayerPointInPoly)return 0;
        for(unsigned shape=0;shape<10;++shape)for(unsigned x=0;x<13;++x)for(unsigned z=0;z<13;++z)for(unsigned alias=0;alias<5;++alias){
            FieldActor current={.pos_x=(s32)samples[x],.pos_z=(s32)samples[z],.flags=0xdecafbad};
            FieldActor before=current,expected=current;g_CurrentEntity=&current;
            int data[9];memcpy(data,shapes[shape],sizeof(shapes[shape]));data[8]=(s32)0xa5a5a5a5;
            int *outputs[]={data+8,data,&current.pos_x,&current.pos_z,data+7},*args[9];
            for(unsigned i=0;i<8;++i)args[i]=data+i;args[8]=outputs[alias];
            u32 result=Script_PlayerPointInPoly(args),words[12]={result,(u32)current.pos_x,(u32)current.pos_z};
            memcpy(words+3,data,sizeof(data));
            for(unsigned word=0;word<12;++word)for(unsigned byte=0;byte<4;++byte)
                actor_hash=(actor_hash^((words[word]>>(byte*8))&255))*UINT64_C(1099511628211);
            expected.pos_x=current.pos_x;expected.pos_z=current.pos_z;
            if(memcmp(&current,&expected,sizeof(current)))return 0;
            if(!alias){
                current=before;u32 program[13]={21|(9u<<13),2u<<9};
                for(unsigned i=0;i<8;++i)program[i+2]=(u32)shapes[shape][i];
                program[10]=9;program[11]=32;g_ScriptVariableBank[9]=0xa5a5a5a5;
                ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
                if(result!=1 || memcmp(&current,&expected,sizeof(current)) || g_ScriptVariableBank[9]!=(u32)data[8] ||
                   node.current.script!=program+13 || node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
            }
            ++cases;
        }
        g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;g_ScriptVariableBank[9]=saved_var;
        if(cases!=8450 || actor_hash!=UINT64_C(0xac99a90a44bccafe)){
            fprintf(stderr,"Actor polygon21 CPU hash %u %016llx\n",cases,(unsigned long long)actor_hash);return 0;
        }
        puts("Original opcode21: 8450 USA CPU states, winding/degenerate/boundary polygons, fractions, five aliases, actor guards and 1690 real dispatcher programs passed");
    }
    return 1;
}

static int original_static_model_matrices(void)
{
    struct {
        RenderObjectHeader header;
        RenderObjectPart parts[3];
        RenderVec3s section[2],bounds[6],origin;
        s8 commands[8];
    } model={.header={.part_count=3,.matrix_command_bytes=5},.commands={0,-1,1,-2,2}};
    model.parts[1].translation_z=100;model.parts[2].translation_z=-200;
    struct {u64 before;RenderMatrix matrices[3];u64 after;} output;
    const s16 angles[]={0,1024,2048,3072,731};
    for(unsigned angle=0;angle<5;++angle){
        memset(&output,0xa5,sizeof(output));
        RenderObjectEntity object={.active_matrix=(RenderMatrix *)(uintptr_t)1};s8 *texture=NULL;
        if(Render_SetupEntityPrims(&object,&model.header,(u8 *)output.matrices,0,0,0,0,0,&texture,0)!=1 ||
           object.active_matrix || object.matrices!=output.matrices)return 0;
        GteShortVector rotation={0,angles[angle],0,0};RotMatrix(&rotation,(GteMatrix *)&object.model_matrix);
        object.model_matrix.translation[0]=3;object.model_matrix.translation[1]=-7;object.model_matrix.translation[2]=1024;
        RenderMatrix first[3];
        for(unsigned frame=0;frame<100;++frame){
            Render_TransformVertices(&object);
            if(object.active_matrix || output.before!=UINT64_C(0xa5a5a5a5a5a5a5a5) ||
               output.after!=UINT64_C(0xa5a5a5a5a5a5a5a5))return 0;
            for(unsigned bone=0;bone<3;++bone){
                RenderMatrix *matrix=output.matrices+bone;
                if(memcmp(matrix->rotation,object.model_matrix.rotation,sizeof(matrix->rotation)))return 0;
                int distance=bone?model.parts[bone].translation_z:0;
                s32 expected[]={3+(object.model_matrix.rotation[0][2]*distance>>12),-7,
                    1024+(object.model_matrix.rotation[2][2]*distance>>12)};
                if(memcmp(matrix->translation,expected,sizeof(expected)))return 0;
            }
            if(!frame)memcpy(first,output.matrices,sizeof(first));
            else if(memcmp(first,output.matrices,sizeof(first)))return 0;
        }
    }
    puts("Original static model path: 500 repeated transforms without an animation bank, neutral bones, branched hierarchy and matrix-buffer guards passed");
    return 1;
}

static int original_script_polygon_opcode(void)
{
    const s32 polygons[][8]={{-10,-10,10,-10,10,10,-10,10},{-10,10,10,10,10,-10,-10,-10},
        {-10,-10,10,-10,0,0,-10,10},{0,0,0,0,0,0,0,0}};
    const u32 points[]={0x80000000,0xffffffff,0,1,65535,65536,0x7fffffff,0x100000,0xfff00000};
    const int counts[]={3,4,0x10004},offsets[]={-16,0,16};
    FieldActor *saved_actor=g_CurrentEntity;ScriptNode *saved_node=g_ScriptNodePool;
    u32 *saved_cursor=g_ScriptCursor;u32 saved_var=g_ScriptVariableBank[9];
    FieldActor actor={0};g_CurrentEntity=&actor;uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[226]!=Entity_CallAction){fputs("Opcode226 handler missing\n",stderr);return 0;}
    struct {u8 prefix[64];PolygonVertex vertices[4];u8 suffix[64];} blob={0};
    for(unsigned p=0;p<4;++p)for(unsigned x=0;x<9;++x)for(unsigned z=0;z<9;++z)
    for(unsigned c=0;c<3;++c)for(unsigned off=0;off<3;++off){
        for(unsigned i=0;i<4;++i)blob.vertices[i]=(PolygonVertex){0,polygons[p][i*2],0,polygons[p][i*2+1]};
        actor.script_base=(u8 *)&blob+64-offsets[off]*2;
        int data[]={(s32)points[x],(s32)points[z],counts[c],offsets[off],(s32)0xa5a5a5a5};
        int before[5];memcpy(before,data,sizeof(data));int *args[]={data,data+1,data+2,data+3,data+4};
        u32 result=Entity_CallAction(args),words[6]={result};memcpy(words+1,data,sizeof(data));
        for(unsigned i=0;i<6;++i)for(unsigned b=0;b<4;++b)
            hash=(hash^((words[i]>>(b*8))&255))*UINT64_C(1099511628211);
        if(memcmp(data,before,4*sizeof(int))){fputs("Opcode226 inputs changed\n",stderr);return 0;}
        u32 program[]={226|(5u<<13)|(2u<<29),0,(u32)before[0],(u32)before[1],(u32)before[2],(u32)before[3],9,32,0};
        g_ScriptVariableBank[9]=0xa5a5a5a5;
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || g_ScriptVariableBank[9]!=(u32)data[4] || node.current.script!=program+9 ||
           !(node.flags&0x10) || g_ScriptNodePool){
            fprintf(stderr,"Opcode226 dispatch p=%u x=%u z=%u c=%u off=%u result=%u var=%u want=%u cursor=%td flags=%x pool=%p\n",
                p,x,z,c,off,result,g_ScriptVariableBank[9],(u32)data[4],node.current.script-program,node.flags,(void *)g_ScriptNodePool);return 0;}
        ++cases;
    }
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;g_ScriptVariableBank[9]=saved_var;
    if(cases!=2916 || hash!=UINT64_C(0xd1f7c244a77c2fa5)){
        fprintf(stderr,"Script polygon226 CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original opcode226: 2916 unhooked USA CPU states, signed script offsets, u16 count narrowing, boundary/winding tests and full dispatcher passed");
    return 1;
}

static int original_follow_actor_opcode(void)
{
    const s32 poses[][2]={{0,0},{65536,0},{0,-65536},{1000*65536,-1000*65536},{65535,-65535},{-10*65536,10*65536}};
    const s16 angles[]={0,1024,3072,4095};const int turns[]={0,64,4096},factors[]={0,327680};
    FieldActor *saved_actor=g_CurrentEntity,*saved_player=g_PlayerEntity,*saved_head=g_FieldActorListHead;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[54]!=Render_CameraTrackEntityZoom)return 0;
    for(unsigned kind=0;kind<6;++kind)for(unsigned p=0;p<6;++p)for(unsigned a=0;a<4;++a)
    for(unsigned turn=0;turn<3;++turn)for(unsigned phase=0;phase<4;++phase)for(unsigned factor=0;factor<2;++factor){
        FieldActor actor={.move_factor=factors[factor],.move_speed=4096,.rot_y=angles[a],.motion_x=0x12345678,.motion_z=0x76543210};
        FieldActor target={.type_id=7,.sub_id=11,.pos_x=poses[p][0],.pos_z=poses[p][1],.field_sfx_id=42,.flags=phase==2?0x10:0};
        FieldActor dead={.type_id=7,.sub_id=11,.flags=0x10,.next=kind==4?&target:NULL};
        g_CurrentEntity=&actor;g_PlayerEntity=kind==0?&target:kind==5?&actor:NULL;
        g_FieldActorListHead=(kind==2 || kind==5)?&target:kind>=3 && kind<=4?&dead:NULL;
        ScriptNode node={.flags=phase?0xa0:0x80,.active=7,.target14=phase?turns[turn]:0,
            .target18.actor=phase?&target:NULL,.target1c=phase==3?41:phase?42:0};
        ScriptNode before_node=node;FieldActor before_actor=actor;
        u32 storage[8]={0};g_ScriptCursor=storage+5;g_ScriptNodePool=&node;
        int data[]={kind<2?0:7,11,turns[turn]};int *args[]={data,data+1,data+2};
        u32 result=Render_CameraTrackEntityZoom(args);
        const u32 words[]={result,(u32)actor.pos_x,(u32)actor.pos_z,(u32)actor.motion_x,(u32)actor.motion_z,
            (u16)actor.rot_y,node.flags,node.active,(u32)node.target14,node.target18.actor!=NULL,(u32)node.target1c,
            (u32)((uintptr_t)g_ScriptCursor-(uintptr_t)storage)};
        for(unsigned i=0;i<12;++i)for(unsigned b=0;b<4;++b)
            hash=(hash^((words[i]>>(b*8))&255))*UINT64_C(1099511628211);
        if(node.target18.actor && node.target18.actor!=&target)return 0;
        if(!phase){
            FieldActor expected=actor;ScriptNode expected_node=node;actor=before_actor;
            u32 program[]={54|(3u<<13),0,(u32)data[0],11,(u32)data[2],32,0};
            node=before_node;node.current.script=program;node.active=1;g_ScriptNodePool=&node;Script_RunQueue();
            if(memcmp(&actor,&expected,sizeof(actor)) || g_ScriptNodePool || node.target14!=expected_node.target14 ||
               node.target18.actor!=expected_node.target18.actor || node.target1c!=expected_node.target1c ||
               node.current.script!=(result?program+7:program) || node.active!=!result ||
               node.flags!=(expected_node.flags|(result?0x10:0)))return 0;
        }
        ++cases;
    }
    g_CurrentEntity=saved_actor;g_PlayerEntity=saved_player;g_FieldActorListHead=saved_head;
    g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(cases!=3456 || hash!=UINT64_C(0x174b6ba481a52f15)){
        fprintf(stderr,"Follow actor54 CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original opcode54: 3456 unhooked USA CPU states, actor/player selection, dead/reused targets, cached native pointers, motion/turn/arrival and dispatcher retry passed");
    return 1;
}

static int original_render_flag_opcodes(void)
{
    const u32 patterns[]={0,1,2,3,0x40,0x41,0x80,0x7fffffff,0x80000000,0xffffffff,0x12345678,0xfedcba98};
    const unsigned ops[]={189,190,191,192},counts[]={4,0,1,0};
    int (*handlers[])(int **)={Script_SetRenderFlag8Params,Script_ClearRenderFlag8,Script_SetRenderFlag10Value,Script_ClearRenderFlag10};
    FieldActor *saved_actor=g_CurrentEntity;ScriptNode *saved_node=g_ScriptNodePool;
    u32 *saved_cursor=g_ScriptCursor;uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    for(unsigned op=0;op<4;++op){
        if(g_ScriptQueueOpcodeHandlers[ops[op]]!=handlers[op])return 0;
        for(unsigned flag=0;flag<12;++flag)for(unsigned value=0;value<12;++value){
            FieldActor actor={.flags=0xdecafbad};g_CurrentEntity=&actor;
            actor.render_object.script_param97=0x12;actor.render_object.script_param98=0x34;
            actor.render_object.script_param99=0x56;actor.render_object.script_value9a=(s16)0x789a;
            actor.render_object.flags_9C=patterns[flag];FieldActor before=actor,expected=actor;
            int data[4];int *args[4];for(unsigned i=0;i<4;++i){data[i]=(s32)patterns[(value+i)%12];args[i]=data+i;}
            u32 result=handlers[op](args);
            for(unsigned b=0;b<4;++b)hash=(hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
            const u8 bytes[]={actor.render_object.script_param97,actor.render_object.script_param98,actor.render_object.script_param99,
                (u8)actor.render_object.script_value9a,(u8)((u16)actor.render_object.script_value9a>>8),
                (u8)actor.render_object.flags_9C,(u8)(actor.render_object.flags_9C>>8)};
            for(unsigned b=0;b<sizeof(bytes);++b)hash=(hash^bytes[b])*UINT64_C(1099511628211);
            expected.render_object.script_param97=actor.render_object.script_param97;
            expected.render_object.script_param98=actor.render_object.script_param98;
            expected.render_object.script_param99=actor.render_object.script_param99;
            expected.render_object.script_value9a=actor.render_object.script_value9a;
            expected.render_object.flags_9C=actor.render_object.flags_9C;
            if(memcmp(&actor,&expected,sizeof(actor)))return 0;
            actor=before;u32 program[8]={ops[op]|(counts[op]<<13),0};
            for(unsigned i=0;i<counts[op];++i)program[2+i]=(u32)data[i];
            program[2+counts[op]]=32;
            ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
            if(result!=1 || memcmp(&actor,&expected,sizeof(actor)) || node.current.script!=program+4+counts[op] ||
               node.flags!=0x90 || node.active || g_ScriptNodePool)return 0;
            ++cases;
        }
    }
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(cases!=576 || hash!=UINT64_C(0x33cb511103ef7831)){
        fprintf(stderr,"Render flag CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original render flags189-192: 576 unhooked USA CPU states, byte/halfword narrowing, preserved actor fields and complete dispatcher passed");
    return 1;
}

static int original_textbox_rect_opcode(void)
{
    const u32 values[]={0,1,32767,32768,65535,65536,0x80000000,0x1234abcd};
    TextboxRect saved=g_TextboxPendingRect;ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    FieldActor *saved_actor=g_CurrentEntity,actor={0};g_CurrentEntity=&actor;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[208]!=Script_DrawSaveOverlay)return 0;
    for(unsigned x=0;x<8;++x)for(unsigned y=0;y<8;++y)for(unsigned w=0;w<8;++w)for(unsigned h=0;h<8;++h){
        int data[]={(s32)values[x],(s32)values[y],(s32)values[w],(s32)values[h]};int *args[]={data,data+1,data+2,data+3};
        u32 result=Script_DrawSaveOverlay(args);TextboxRect expected=g_TextboxPendingRect;u8 output[12];
        memcpy(output,&result,4);memcpy(output+4,&expected,8);
        for(unsigned i=0;i<sizeof(output);++i)hash=(hash^output[i])*UINT64_C(1099511628211);
        memset(&g_TextboxPendingRect,0xa5,sizeof(g_TextboxPendingRect));
        u32 program[]={208|(4u<<13),0,values[x],values[y],values[w],values[h],32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || memcmp(&g_TextboxPendingRect,&expected,sizeof(expected)) || node.current.script!=program+8 || node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
        ++cases;
    }
    g_TextboxPendingRect=saved;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;g_CurrentEntity=saved_actor;
    if(cases!=4096 || hash!=UINT64_C(0xc532dce5369fff45))return 0;
    puts("Original opcode208: 4096 unhooked USA CPU textbox rectangle states, low-halfword narrowing and complete dispatcher passed");
    return 1;
}

static int original_task_pause_opcodes(void)
{
    const int heads[8][3]={{-1,-1,-1},{0,-1,-1},{0,2,4},{0,0,0},{0,1,2},{-1,4,2},{5,3,1},{1,-1,5}};
    const int links[8][7]={{-1,-1,-1,-1,-1,-1,-1},{1,2,3,4,5,-1,-1},{1,-1,3,-1,5,-1,-1},
        {1,2,3,4,5,-1,-1},{3,3,3,4,5,-1,-1},{-1,0,3,5,1,-1,-1},{-1,0,1,2,3,4,-1},{-1,4,3,-1,0,2,-1}};
    const u16 flags[]={0,0x20,0x40,0x60,0x50,0x80,0xffff,0xa0};const u32 masks[]={0,1,0x15,0x2a,0x3f};
    FieldActor *saved_actor=g_CurrentEntity;ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    int (*handlers[])(int **)={Entity_MarkAnimPending,Entity_AdvancePendingAnim};
    if(g_ScriptQueueOpcodeHandlers[95]!=handlers[0] || g_ScriptQueueOpcodeHandlers[96]!=handlers[1])return 0;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    for(unsigned op=0;op<2;++op)for(unsigned layout=0;layout<8;++layout)for(unsigned f=0;f<8;++f)
    for(unsigned m=0;m<5;++m)for(unsigned skip=0;skip<7;++skip){
        ScriptNode nodes[7],before[7];FieldActor actor={0};
        for(unsigned i=0;i<7;++i){
            memset(nodes+i,0xa5,sizeof(nodes[i]));nodes[i].current.value=0x12340000u+i*0x44;
            nodes[i].next_value=masks[m]&(1u<<i)?0x23450000u+i*0x40:0;
            nodes[i].flags=flags[(f+i)%8];nodes[i].active=(int)i-3;
            nodes[i].next=links[layout][i]<0?NULL:nodes+links[layout][i];
        }
        memcpy(before,nodes,sizeof(nodes));
        for(unsigned b=0;b<3;++b)actor.script_node_lists[b]=heads[layout][b]<0?NULL:nodes+heads[layout][b];
        FieldActor expected_actor=actor;g_CurrentEntity=&actor;g_ScriptNodePool=nodes+skip;
        u32 words[22]={handlers[op](NULL)};
        for(unsigned i=0;i<7;++i){
            words[1+i*3]=nodes[i].flags;words[2+i*3]=(u32)nodes[i].current.value;words[3+i*3]=(u32)nodes[i].active;
            ScriptNode expected=before[i];expected.flags=nodes[i].flags;expected.current=nodes[i].current;expected.active=nodes[i].active;
            if(memcmp(nodes+i,&expected,sizeof(expected)))return 0;
        }
        if(memcmp(&actor,&expected_actor,sizeof(actor)) || g_CurrentEntity!=&actor || g_ScriptNodePool!=nodes+skip || g_ScriptCursor!=saved_cursor)return 0;
        for(unsigned w=0;w<22;++w)for(unsigned b=0;b<4;++b)
            hash=(hash^((words[w]>>(b*8))&255))*UINT64_C(1099511628211);
        ++cases;
    }
    ScriptNode nodes[7]={0};FieldActor actor={0};u32 payload[7]={0},program[]={95,0,96,0,32,0};
    for(unsigned i=0;i<6;++i){nodes[i].flags=0x20;nodes[i].next_value=(intptr_t)(payload+i);nodes[i].next=i<5?nodes+i+1:NULL;}
    actor.script_node_lists[0]=nodes;actor.script_node_lists[1]=nodes+2;actor.script_node_lists[2]=nodes+4;
    nodes[6]=(ScriptNode){.current.script=program,.active=1,.flags=0x80};g_CurrentEntity=&actor;g_ScriptNodePool=nodes+6;
    Script_RunQueue();
    for(unsigned i=0;i<6;++i)if(nodes[i].current.script!=payload+i || nodes[i].next_value!=(intptr_t)(payload+i) || nodes[i].active!=1 || nodes[i].flags)return 0;
    if(nodes[6].current.script!=program+6 || nodes[6].flags!=0x90 || nodes[6].active || g_ScriptNodePool)return 0;
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(cases!=4480 || hash!=UINT64_C(0xea512f4d706a92b1)){
        fprintf(stderr,"Task pause CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original opcodes95/96: 4480 unhooked USA CPU states, three task buckets, shared tails, current-node exclusion, resume flags/cursors, unchanged node fields and full-width native script pointers through the dispatcher passed");
    return 1;
}

static int original_geometry_visibility_opcode(void)
{
    const u32 values[]={0,1,0xffffffff,2,100,0x80000000,0x7fffffff,0xffffffff};
    struct {GeomState header;GeomEntry entries[3];} geometry={0};
    GeomState *saved_geom=g_GeomState;FieldActor *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    geometry.header.entry_offset=(s32)((u8 *)geometry.entries-(u8 *)&geometry);g_GeomState=&geometry.header;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[114]!=Script_SetRenderEntryEnabled)return 0;
    for(unsigned index=0;index<3;++index)for(unsigned v=0;v<8;++v)for(unsigned f=0;f<256;++f){
        for(unsigned i=0;i<3;++i){
            GeomEntry *entry=geometry.entries+i;memset(entry,(f+i*73)&255,sizeof(*entry));
            entry->head.packed=0x12345600|((f+i)&255);entry->u30.prim=geometry.entries+i;
            entry->u34.pagePackets=(void *)(geometry.entries+(2-i));
        }
        GeomEntry before[3],after[3],expected[3];memcpy(before,geometry.entries,sizeof(before));memcpy(expected,before,sizeof(before));
        int data[]={index,(s32)values[v]};int *args[]={data,data+1};u32 result=Script_SetRenderEntryEnabled(args);
        for(unsigned b=0;b<4;++b)hash=(hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
        for(unsigned i=0;i<3;++i)for(unsigned b=0;b<48;++b)
            hash=(hash^((u8 *)(geometry.entries+i))[b])*UINT64_C(1099511628211);
        expected[index].head=geometry.entries[index].head;
        if(memcmp(expected,geometry.entries,sizeof(expected)))return 0;
        memcpy(after,geometry.entries,sizeof(after));memcpy(geometry.entries,before,sizeof(before));
        FieldActor actor={0};u32 program[]={114|(2u<<13),0,index,values[v],32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_CurrentEntity=&actor;g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || memcmp(after,geometry.entries,sizeof(after)) || node.current.script!=program+6 ||
           node.flags!=0x90 || node.active || g_ScriptNodePool)return 0;
        ++cases;
    }
    g_GeomState=saved_geom;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(cases!=6144 || hash!=UINT64_C(0xab69ffd93fa3e525))return 0;
    puts("Original opcode114: 6144 unhooked USA CPU states, every flag byte, zero/nonzero enabled values, three native entries, full-width pointer guards and real dispatcher passed");
    return 1;
}

static int original_animation_stop_slot_opcode(void)
{
    const u32 limits[]={0,1,2,3,65535,0xffffffff,0x80000000,0x12345678};
    const s16 steps[]={-32768,-256,-1,0,1,256,32767};
    const u8 flags[]={0,1,2,4,0x20,255};const u32 positions[]={0,1,255,0xabcdef12};
    struct {GeomState header;u8 pad[512-sizeof(GeomState)];GeomCtrlEntry controls[2];GeomAnimationSlot slots[2][4];u64 guard;} geometry={0};
    GeomState *saved_geom=g_GeomState;FieldActor *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    geometry.header.ctrl_offset=512;g_GeomState=&geometry.header;u8 *body=(u8 *)&geometry+512;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[154]!=Script_SetMenuSlotEntry)return 0;
    for(unsigned index=0;index<2;++index)for(unsigned slot=0;slot<4;++slot)for(unsigned l=0;l<8;++l)
    for(unsigned st=0;st<7;++st)for(unsigned f=0;f<6;++f)for(unsigned p=0;p<4;++p){
        memset(body,0xa5,48);geometry.guard=UINT64_C(0xa55aa55aa55aa55a);
        for(unsigned i=0;i<2;++i){
            geometry.controls[i].head.packed=0x12340400|flags[(f+i)%6];
            geometry.controls[i].position.packed=positions[(p+i)%4];geometry.controls[i].step=steps[st];
            geometry.controls[i].slot_offset=32-i*8;
        }
        u8 before[48],after[48];memcpy(before,body,48);
        int data[]={index,(s32)limits[l],slot};int *args[]={data,data+1,data+2};
        u32 result=Script_SetMenuSlotEntry(args);
        for(unsigned b=0;b<4;++b)hash=(hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
        for(unsigned b=0;b<48;++b)hash=(hash^body[b])*UINT64_C(1099511628211);
        memcpy(after,body,48);memcpy(body,before,48);
        FieldActor actor={0};u32 program[]={154|(3u<<13),0,index,limits[l],slot,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_CurrentEntity=&actor;g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || memcmp(after,body,48) || node.current.script!=program+7 || node.flags!=0x90 ||
           node.active || g_ScriptNodePool || geometry.guard!=UINT64_C(0xa55aa55aa55aa55a) || geometry.header.ctrl_offset!=512)return 0;
        ++cases;
    }
    g_GeomState=saved_geom;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(cases!=10752 || hash!=UINT64_C(0x7eccb673b20e4be5)){
        fprintf(stderr,"Animation slot154 CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original opcode154: 10752 unhooked USA CPU states, relative slot arrays, stop marker, unsigned frame bounds, signed direction reversal, retained group/flags and real dispatcher passed");
    return 1;
}

static int original_scene_entry_position_opcode(void)
{
    const u32 heads[]={0,1,0x7fff,0x8000,0xffff,0x80000000,0xffffffff,0x010c0000,0xa5a55a5a,0x12345678,0x7fffffff,0x600000};
    const u16 bases[][2]={{0,0},{65535,65535},{32768,32767},{123,456},{65530,7}};
    const s16 bounds[][4]={{-100,100,-50,50},{-32768,32767,-32768,32767},{500,-500,300,-300},{10,10,-20,-20}};
    const u32 values[]={0,1,0xffffffff,32767,0xffff8000,65535,0x8000ffff,0x7fffffff,0x80000000,0xffff0001,0x12345678};
    struct {GeomState header;GeomEntry entries[3];} geometry={0};
    GeomState *saved_geom=g_GeomState;FieldActor *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    geometry.header.entry_count06=3;
    geometry.header.entry_offset=(s32)((u8 *)geometry.entries-(u8 *)&geometry.header);g_GeomState=&geometry.header;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[113]!=Script_SetSceneEntryAnim)return 0;
    for(unsigned index=0;index<3;++index)for(unsigned f=0;f<12;++f)for(unsigned base=0;base<5;++base)
    for(unsigned bound=0;bound<4;++bound)for(unsigned a=0;a<11;++a){
        for(unsigned i=0;i<3;++i){
            GeomEntry *entry=geometry.entries+i;memset(entry,(i*73+f*11)&255,sizeof(*entry));
            entry->head.packed=heads[(f+i)%12];entry->base_x=bases[base][0];entry->base_y=bases[base][1];
            memcpy(&entry->bounds,bounds[bound],sizeof(entry->bounds));
            entry->u30.prim=geometry.entries+i;entry->u34.pagePackets=(void *)(geometry.entries+(2-i));
        }
        GeomEntry before[3],after[3],expected[3];memcpy(before,geometry.entries,sizeof(before));memcpy(expected,before,sizeof(before));
        int input[]={index,(s32)values[a],(s32)values[(a+3)%11],(s32)values[(a+7)%11]};
        int *args[]={input,input+1,input+2,input+3};u32 result=Script_SetSceneEntryAnim(args);
        for(unsigned b=0;b<4;++b)hash=(hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
        for(unsigned i=0;i<3;++i)for(unsigned b=0;b<48;++b)
            hash=(hash^((u8 *)(geometry.entries+i))[b])*UINT64_C(1099511628211);
        expected[index].head=geometry.entries[index].head;expected[index].scr_x=geometry.entries[index].scr_x;expected[index].scr_y=geometry.entries[index].scr_y;
        if(memcmp(expected,geometry.entries,sizeof(expected)))return 0;
        memcpy(after,geometry.entries,sizeof(after));memcpy(geometry.entries,before,sizeof(before));
        FieldActor actor={0};u32 code[]={113|(4u<<13),0,index,input[1],input[2],input[3],32,0};
        ScriptNode node={.current.script=code,.active=1,.flags=0x80};g_CurrentEntity=&actor;g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || memcmp(after,geometry.entries,sizeof(after)) || node.current.script!=code+8 ||
           node.flags!=0x90 || node.active || g_ScriptNodePool || g_GeomState!=&geometry.header)return 0;
        ++cases;
    }
    g_GeomState=saved_geom;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(cases!=7920 || hash!=UINT64_C(0x9d5d2d1d374efb69)){
        fprintf(stderr,"Scene113 CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original opcode113: 7920 unhooked USA CPU states, three native mesh records, signed operands, inverted/tight bounds, depth wrap, untouched full-width pointers and real dispatcher passed");
    return 1;
}

/* Opcode175 force-resolves the named triangle even outside its outline. */
static int original_floor_resolve_opcode(void)
{
    const u32 positions[]={0,1,65535,0xffffffff,0x80000000,0x7fffffff,0xffff0000,65536,0x12345678,0xfedcba98};
    const u32 highs[]={0,0x10000,0x80000000,0xffff0000,0x12340000,0x7fff0000};
    const s16 shapes[][6]={{-100,-100,100,-100,0,100},{0,100,100,-100,-100,-100},{0,0,0,0,0,0},
        {-32768,-32768,32767,-32768,0,32767},{-32768,-32768,32767,32767,-32768,32767}};
    const s16 heights[]={-32768,-1,0,1,32767,123,-123,1000,-1000,321,-321,42};
    const u32 coeff[]={0,1,0xffffffff,65536,0xffff0000,0x7fffffff,0x80000000,0x40000000,0xc0000000,0x12345678,0xfedcba98,0x100000};
    CollisionDatabase *saved_db=g_CollisionDb;CollisionPlane *saved_planes=g_CollisionPlaneTable;
    CollisionRegionTable saved_regions=g_RegionHeightTable;FieldActor *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    struct FloorGeometry {CollisionDatabase db;CollisionTriangleXZ flat[3];CollisionTriangleXYZ slope[3];
        CollisionVertexXZ xz[3];CollisionVertexXYZ xyz[3];CollisionPlane planes[3];CollisionRegionEntry regions[3];} geometry,before_geometry;
    CollisionFlatRegion h0={0},h1={0},h2={0};CollisionFlatRegion *flat_heights[]={&h0,&h1,&h2};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[175]!=Entity_ResolveCurrentPosition)return 0;
    for(unsigned sweep=0;sweep<2;++sweep)for(unsigned test=0;test<(sweep?65536u:216000u);++test){
        unsigned mode=sweep?test%2:test/108000,index=sweep?test%3:(test/36000)%3;
        unsigned shape=sweep?test%5:(test/1200)%5,variant=sweep?test%12:(test/100)%12;
        u32 high=sweep?test<<16:highs[(test/6000)%6];
        u32 x=sweep?positions[test%10]:positions[(test/10)%10],z=sweep?positions[(test+3)%10]:positions[test%10];
        BattleEntity actor;memset(&actor,0xa5,sizeof(actor));actor.posX.fixed=(s32)x;actor.posY.fixed=(s32)0x98765432;
        actor.posZ.fixed=(s32)z;actor.baseX=0x76543210;actor.baseY=(s32)0xdeadbeef;actor.baseZ=0x10203040;
        BattleEntity before=actor,expected=actor;g_CurrentEntity=(FieldActor *)&actor;
        memset(&geometry,0xa5,sizeof(geometry));geometry.db.regionCount=geometry.db.triangleCount=geometry.db.planeCount=3;
        geometry.db.vertices.pointer=mode?(void *)geometry.xyz:(void *)geometry.xz;
        geometry.db.triangles.pointer=mode?(void *)geometry.slope:(void *)geometry.flat;
        geometry.db.planes.pointer=mode?geometry.planes:NULL;geometry.db.rampEdges.pointer=NULL;geometry.db.regions=geometry.regions;
        for(unsigned i=0;i<3;++i){
            geometry.flat[i].header.kind=geometry.slope[i].face.kind=0x40+i;
            geometry.flat[i].header.region=geometry.slope[i].face.region=(i+1)%3;
            geometry.slope[i].face.plane=(i+2)%3;geometry.slope[i].face.distance=(s32)coeff[(variant+i+9)%12];
            for(unsigned j=0;j<3;++j)geometry.flat[i].layout.links.vertices[j]=geometry.slope[i].layout.links.vertices[j]=j;
            geometry.xz[i]=(CollisionVertexXZ){(u16)shapes[shape][i*2],(u16)shapes[shape][i*2+1]};
            geometry.xyz[i]=(CollisionVertexXYZ){(u16)shapes[shape][i*2],(u16)((int)i*17-13),(u16)shapes[shape][i*2+1]};
            geometry.planes[i]=(CollisionPlane){(s32)coeff[(variant+i)%12],(s32)coeff[(variant+i+3)%12],(s32)coeff[(variant+i+6)%12]};
            flat_heights[i]->height=heights[(variant+i)%12];flat_heights[i]->triangleCount=0;geometry.regions[i].flat=flat_heights[i];
        }
        memcpy(&before_geometry,&geometry,sizeof(geometry));g_CollisionDb=&geometry.db;
        g_CollisionPlaneTable=mode?geometry.planes:NULL;g_RegionHeightTable.entries=geometry.regions;
        struct {u32 before;int index;u32 after;} input={0x11223344,(s32)(high|index),0xa55a5aa5};int *args[]={&input.index};
        u32 result=Entity_ResolveCurrentPosition(args);unsigned face=0,mirror=0;
        for(unsigned i=0;i<3;++i){
            void *pointer=mode?(void *)&geometry.slope[i].face:(void *)&geometry.flat[i].header;
            if(actor.collisionFace==pointer)face=i+1;if(actor.collisionFaceMirror==pointer)mirror=i+1;
        }
        if(!face || !mirror)return 0;
        u32 inside=Geo_PointInTri(actor.collisionFace,actor.posX.parts.integer,actor.posZ.parts.integer);
        const u32 words[]={result,inside,(u32)actor.posX.fixed,(u32)actor.posY.fixed,(u32)actor.posZ.fixed,
            (u32)actor.baseX,(u32)actor.baseY,(u32)actor.baseZ,face,mirror};
        for(unsigned word=0;word<10;++word)for(unsigned byte=0;byte<4;++byte)
            hash=(hash^((words[word]>>(byte*8))&255))*UINT64_C(1099511628211);
        expected.posY=actor.posY;expected.baseX=actor.baseX;expected.baseY=actor.baseY;expected.baseZ=actor.baseZ;
        expected.collisionFace=actor.collisionFace;expected.collisionFaceMirror=actor.collisionFaceMirror;
        if(memcmp(&actor,&expected,sizeof(actor)) || memcmp(&geometry,&before_geometry,sizeof(geometry)) ||
           input.index!=(s32)(high|index) || input.before!=0x11223344 || input.after!=0xa55a5aa5)return 0;
        actor=before;u32 program[]={175|(1u<<13),0,high|index,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || memcmp(&actor,&expected,sizeof(actor)) || memcmp(&geometry,&before_geometry,sizeof(geometry)) ||
           program[2]!=(high|index) || node.current.script!=program+5 || node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
        for(unsigned i=0;i<3;++i)if(flat_heights[i]->height!=heights[(variant+i)%12] || flat_heights[i]->triangleCount)return 0;
        ++cases;
    }
    g_CollisionDb=saved_db;g_CollisionPlaneTable=saved_planes;g_RegionHeightTable=saved_regions;
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(cases!=281536 || hash!=UINT64_C(0x06cbb55c0c953cd0)){
        fprintf(stderr,"Floor175 CPU mismatch: %u %016llx\n",cases,(unsigned long long)hash);return 0;
    }
    puts("Original opcode175: 281536 USA CPU states, flat/sloped/degenerate/extreme triangles, full high-word domain, forced outside placement, wrapped height/cross products, full pointer/actor/geometry guards and real dispatcher passed");
    return 1;
}

/* Original render-position/reset handlers, including their complete callees. */
static int original_render_position_reset_opcodes(void)
{
    const u32 values[]={0,1,0xffffffff,32767,0xffff8000,65535,65536,0x7fffffff,0x80000000,0x12345678,0xfedcba98};
    const u16 bases[][2]={{0,0},{65535,65535},{32768,32767},{123,456},{65530,7}},depths[]={0,65535,32768,12345,7};
    const s16 bounds[][4]={{-100,100,-50,50},{-32768,32767,-32768,32767},{500,-500,300,-300},{10,10,-20,-20}};
    const u32 flags[]={0,1,0x80,0xffffff7f,0xffffffff,0x80000000,0x12345678,0xa5a55a5a};
    GeomState *saved_geom=g_GeomState;FieldActor *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;int saved_flags=g_GeomScroll.state.flags;
    FieldActor actor={0};g_CurrentEntity=&actor;uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(sizeof(GeomState)!=60 || g_ScriptQueueOpcodeHandlers[115]!=Script_SetRenderEntryPos ||
       g_ScriptQueueOpcodeHandlers[204]!=Script_ResetEntityRenderObj || g_ScriptQueueOpcodeHandlers[230]!=Script_SetActorScreenPos)return 0;
    for(unsigned sweep=0;sweep<2;++sweep)for(unsigned test=0;test<(sweep?65536u:26620u);++test){
        GeomState geometry,before,after,expected;memset(&geometry,0xa5,sizeof(geometry));
        unsigned bound=sweep?test%4:(test/1331)%4,base=test/5324;
        int data[3];
        if(sweep){
            geometry.clip_offset_x=(u16)(test*3);geometry.clip_offset_y=(u16)(test*5);geometry.depth_offset=(u16)(test*17);
            data[0]=(s32)((test<<16)|((test*7)&65535));data[1]=(s32)(((65535-test)<<16)|test);data[2]=(s32)((test<<16)|(test^0x8000));
        }else{
            geometry.clip_offset_x=bases[base][0];geometry.clip_offset_y=bases[base][1];geometry.depth_offset=depths[base];
            data[0]=(s32)values[(test/121)%11];data[1]=(s32)values[(test/11)%11];data[2]=(s32)values[test%11];
        }
        geometry.clip_min_x=bounds[bound][0];geometry.clip_max_x=bounds[bound][1];
        geometry.clip_min_y=bounds[bound][2];geometry.clip_max_y=bounds[bound][3];
        before=expected=geometry;g_GeomState=&geometry;g_GeomScroll.state.flags=(s32)flags[test%8];
        int inputs[3];memcpy(inputs,data,sizeof(data));int *args[]={data,data+1,data+2};u32 result=Script_SetRenderEntryPos(args);
        for(unsigned byte=0;byte<4;++byte)hash=(hash^((result>>(byte*8))&255))*UINT64_C(1099511628211);
        for(unsigned byte=0;byte<sizeof(geometry);++byte)hash=(hash^((u8 *)&geometry)[byte])*UINT64_C(1099511628211);
        u32 actual_flags=(u32)g_GeomScroll.state.flags;
        for(unsigned byte=0;byte<4;++byte)hash=(hash^((actual_flags>>(byte*8))&255))*UINT64_C(1099511628211);
        expected.field26=geometry.field26;expected.disp_src_x=geometry.disp_src_x;expected.disp_src_y=geometry.disp_src_y;
        if(memcmp(data,inputs,sizeof(data)) || memcmp(&geometry,&expected,sizeof(geometry)) || actual_flags!=flags[test%8])return 0;
        after=geometry;geometry=before;
        u32 program[]={115|(3u<<13),0,(u32)data[0],(u32)data[1],(u32)data[2],32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || memcmp(&geometry,&after,sizeof(geometry)) || (u32)g_GeomScroll.state.flags!=actual_flags ||
           node.current.script!=program+7 || node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
        ++cases;
    }
    if(cases!=92156 || hash!=UINT64_C(0x6a81bee8f390fd1c)){
        fprintf(stderr,"Render115 CPU mismatch: %u %016llx\n",cases,(unsigned long long)hash);return 0;
    }
    hash=UINT64_C(14695981039346656037);cases=0;
    struct PositionGeometry {GeomState header;GeomEntry entries[5];u64 guard;} mesh,before,expected,after;
    for(int index=-2;index<3;++index)for(unsigned flag=0;flag<8;++flag)for(unsigned x=0;x<11;++x)for(unsigned y=0;y<11;++y){
        memset(&mesh,0xa5,sizeof(mesh));mesh.header.entry_offset=(s32)((u8 *)(mesh.entries+2)-(u8 *)&mesh);
        for(unsigned i=0;i<5;++i){mesh.entries[i].u30.prim=mesh.entries+i;mesh.entries[i].u34.pagePackets=(void *)(mesh.entries+4-i);}
        memcpy(&before,&mesh,sizeof(mesh));memcpy(&expected,&mesh,sizeof(mesh));g_GeomState=&mesh.header;g_GeomScroll.state.flags=(s32)flags[flag];
        int data[]={index,(s32)values[x],(s32)values[y]},inputs[3];memcpy(inputs,data,sizeof(data));int *args[]={data,data+1,data+2};
        u32 result=Script_SetActorScreenPos(args),actual_flags=(u32)g_GeomScroll.state.flags;
        const u32 words[]={result,actual_flags};
        for(unsigned word=0;word<2;++word)for(unsigned byte=0;byte<4;++byte)
            hash=(hash^((words[word]>>(byte*8))&255))*UINT64_C(1099511628211);
        for(unsigned i=0;i<5;++i)for(unsigned byte=0;byte<48;++byte)
            hash=(hash^((u8 *)(mesh.entries+i))[byte])*UINT64_C(1099511628211);
        GeomEntry *entry=expected.entries+(index+2),*actual=mesh.entries+(index+2);
        entry->base_x=actual->base_x;entry->base_y=actual->base_y;entry->scr_x=actual->scr_x;entry->scr_y=actual->scr_y;
        if(memcmp(data,inputs,sizeof(data)) || memcmp(&mesh,&expected,sizeof(mesh)))return 0;
        memcpy(&after,&mesh,sizeof(mesh));memcpy(&mesh,&before,sizeof(mesh));g_GeomScroll.state.flags=(s32)flags[flag];
        u32 program[]={230|(3u<<13),0,(u32)index,values[x],values[y],32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || memcmp(&mesh,&after,sizeof(mesh)) || (u32)g_GeomScroll.state.flags!=actual_flags ||
           node.current.script!=program+7 || node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
        ++cases;
    }
    if(cases!=4840 || hash!=UINT64_C(0x1a33d0057329050e)){
        fprintf(stderr,"Render230 CPU mismatch: %u %016llx\n",cases,(unsigned long long)hash);return 0;
    }
    const u32 patterns[]={0,1,0x7fff,0x8000,0xffff,0x80000000,0xffffffff,0x010c0000,0xa5a55a5a,0x12345678,0x7fffffff,0x600000};
    hash=UINT64_C(14695981039346656037);cases=0;
    for(unsigned parts=0;parts<256;++parts)for(unsigned anim=0;anim<12;++anim)for(unsigned parent=0;parent<4;++parent)for(unsigned source=0;source<4;++source){
        FieldActor current,others[2]={{0}};memset(&current,0xa5,sizeof(current));RenderObjectHeader header;memset(&header,0xa5,sizeof(header));
        header.part_count=parts;RenderObjectHeader header_before=header;
        FieldActor *parents[]={NULL,&current,others,others+1};RenderObjectEntity *sources[]={NULL,&current.render_object,&others[0].render_object,&others[1].render_object};
        current.parent=parents[parent];current.render_object.header=&header;current.render_object.animation_source=sources[source];
        current.render_object.animation_state=(s16)patterns[anim];current.render_object.animation_id=(s16)patterns[(anim+3)%12];
        FieldActor actor_before=current,actor_expected=current;g_CurrentEntity=&current;u32 result=Script_ResetEntityRenderObj(NULL);
        const u32 words[]={result,current.parent!=NULL,current.render_object.animation_source!=NULL,
            (u16)current.render_object.animation_state,(u16)current.render_object.animation_id,current.flags};
        for(unsigned word=0;word<6;++word)for(unsigned byte=0;byte<4;++byte)
            hash=(hash^((words[word]>>(byte*8))&255))*UINT64_C(1099511628211);
        actor_expected.parent=current.parent;actor_expected.render_object.animation_source=current.render_object.animation_source;
        actor_expected.render_object.animation_state=current.render_object.animation_state;
        if(memcmp(&current,&actor_expected,sizeof(current)) || memcmp(&header,&header_before,sizeof(header)))return 0;
        current=actor_before;u32 program[]={204,0,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || memcmp(&current,&actor_expected,sizeof(current)) || memcmp(&header,&header_before,sizeof(header)) ||
           node.current.script!=program+4 || node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
        ++cases;
    }
    g_GeomState=saved_geom;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;g_GeomScroll.state.flags=saved_flags;
    if(cases!=49152 || hash!=UINT64_C(0xf55ffb13ce6e0a25)){
        fprintf(stderr,"Render204 CPU mismatch: %u %016llx\n",cases,(unsigned long long)hash);return 0;
    }
    puts("Original render opcodes115/230/204: 92156/4840/49152 USA CPU states, full callees, s16 inputs, clipping bounds/wrap, signed entry indices, all part counts, full native pointer/record guards and real dispatcher passed");
    return 1;
}

static int original_scene_entry_raw_bytes(void)
{
    const u32 heads[]={0,1,0xffffffff,0x14000011,0x12345678,0x80000000};
    const s16 bounds[][4]={{-100,100,-50,50},{-32768,32767,-32768,32767},{500,-500,300,-300},{10,10,-20,-20}};
    const u32 values[]={0,1,0xffffffff,32767,0xffff8000,65535,0x8000ffff,0x80000000};
    struct {GeomState header;u32 padding;u8 raw[224];GeomEntry entries[2];u64 guard;} geometry={0};
    Pe1GameState saved_game=g_GameState;FieldActor *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    geometry.header.entry_count06=2;
    geometry.header.entry_offset=(s32)((u8 *)geometry.entries-(u8 *)&geometry);
    geometry.guard=UINT64_C(0xa55aa55aa55aa55a);memset(geometry.entries,0xa5,sizeof(geometry.entries));
    g_GeomState=&geometry.header;g_GameState.geometry_disk_entry_offset=64;g_GameState.geometry_disk_bytes=288;
    if((u8 *)geometry.raw-(u8 *)&geometry!=64)return 0;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    for(unsigned index=2;index<4;++index)for(unsigned f=0;f<6;++f)
    for(unsigned b=0;b<4;++b)for(unsigned a=0;a<8;++a){
        for(unsigned i=0;i<224;++i)geometry.raw[i]=(u8)(i*37+f*11);
        GeomEntry record={0};u8 *target=geometry.raw+index*56;memcpy(&record,target,48);
        record.head.packed=heads[f];record.base_x=123;record.base_y=456;memcpy(&record.bounds,bounds[b],8);
        memcpy(target,&record,48);u8 before[224],after[224];memcpy(before,geometry.raw,224);
        int input[]={index,(s32)values[a],(s32)values[(a+3)%8],(s32)values[(a+5)%8]};
        int *args[]={input,input+1,input+2,input+3};u32 result=Script_SetSceneEntryAnim(args);
        for(unsigned i=0;i<4;++i)hash=(hash^((result>>(i*8))&255))*UINT64_C(1099511628211);
        for(unsigned i=0;i<224;++i)hash=(hash^geometry.raw[i])*UINT64_C(1099511628211);
        memcpy(after,geometry.raw,224);memcpy(geometry.raw,before,224);
        FieldActor actor={0};u32 program[]={113|(4u<<13),0,index,input[1],input[2],input[3],32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_CurrentEntity=&actor;g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || memcmp(after,geometry.raw,224) || node.current.script!=program+8 || node.active ||
           node.flags!=0x90 || g_ScriptNodePool || geometry.guard!=UINT64_C(0xa55aa55aa55aa55a))return 0;
        for(unsigned i=0;i<sizeof(geometry.entries);++i)if(((u8 *)geometry.entries)[i]!=0xa5)return 0;
        input[0]=4;memcpy(before,geometry.raw,224);
        if(Script_SetSceneEntryAnim(args)!=-1 || memcmp(before,geometry.raw,224))return 0;
        input[0]=-2;if(Script_SetSceneEntryAnim(args)!=-1 || memcmp(before,geometry.raw,224))return 0;
        ++cases;
    }
    g_GameState=saved_game;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(cases!=384 || hash!=UINT64_C(0xb3feaabbff457d95)){
        fprintf(stderr,"Scene113 raw CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original opcode113: 384 unhooked USA CPU states addressing retained bytes beyond the mesh table, full payload equality, untouched native meshes, real dispatcher and invalid range rejection passed");
    return 1;
}

static int original_equipment_open_opcode(void)
{
    const u16 flags[]={0,1,0x20,0x21,0x80,0xff,0x8000,0xffff};
    const u32 game[]={0,1,0x1000,0x1001,0x8000,0x9000,0x80000000,0xffffffff};
    const u32 geom[]={0,0x2000,0xc000,0xdeadbeef},block[]={0,4,0x87654321};
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    u32 saved_game=g_GameState.flags,saved_block=g_GameStateFlags;int saved_geom=g_GeomScroll.state.flags;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[238]!=Script_BeginSceneTransition)return 0;
    for(unsigned f=0;f<8;++f)for(unsigned g=0;g<8;++g)for(unsigned geo=0;geo<4;++geo)for(unsigned b=0;b<3;++b){
        if((flags[f]&0x20) && !(game[g]&0x1000))continue;
        ScriptNode node;memset(&node,0xa5,sizeof(node));node.flags=flags[f];node.active=7;
        ScriptNode expected=node;u32 code[20]={0};g_ScriptNodePool=&node;g_ScriptCursor=code+16;
        g_GameState.flags=game[g];g_GameStateFlags=block[b];g_GeomScroll.state.flags=geom[geo];
        u32 result=Script_BeginSceneTransition(NULL);
        const u32 words[]={result,node.flags,node.active,(u32)((uintptr_t)g_ScriptCursor-(uintptr_t)(code+16)),
            g_GameState.flags,(u32)g_GeomScroll.state.flags,g_GameStateFlags};
        for(unsigned w=0;w<7;++w)for(unsigned byte=0;byte<4;++byte)
            hash=(hash^((words[w]>>(byte*8))&255))*UINT64_C(1099511628211);
        expected.flags=node.flags;expected.active=node.active;
        if(memcmp(&node,&expected,sizeof(node)) || g_ScriptNodePool!=&node)return 0;
        ++cases;
    }
    FieldActor *saved_actor=g_CurrentEntity;FieldActor actor={0};u32 program[]={238|(1u<<13),0,1,32,0};
    ScriptNode node={.current.script=program,.active=1,.flags=0x80};
    g_CurrentEntity=&actor;g_ScriptNodePool=&node;g_GameState.flags=0;Script_RunQueue();
    int valid=node.current.script==program && node.flags==0xa0 && node.active==1 && !g_ScriptNodePool;
    g_GameState.flags=0x1000;g_ScriptNodePool=&node;Script_RunQueue();
    valid=valid && node.current.script==program+5 && node.flags==0xb0 && !node.active && !g_ScriptNodePool;
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    g_GameState.flags=saved_game;g_GameStateFlags=saved_block;g_GeomScroll.state.flags=saved_geom;
    if(!valid || cases!=576 || hash!=UINT64_C(0x7c7baf97c5d2fdd5))return 0;
    puts("Original opcode238: 576 unhooked USA CPU first-yield/already-loading states, native cursor rewind and dispatcher continuation passed; menu construction is exercised by the runtime replay");
    return 1;
}

static int original_memcard_dialog_opcode(void)
{
    const u16 flags[]={0,1,0x20,0x21,0x80,0xff,0x8000,0xffff};
    const u32 game[]={0,1,0x1000,0x1001,0x8000,0x9000,0x80000000,0xffffffff};
    const u32 geom[]={0,0x2000,0xc000,0xdeadbeef},block[]={0,4,0x87654321};
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    u32 saved_game=g_GameState.flags,saved_block=g_GameStateFlags;int saved_geom=g_GeomScroll.state.flags;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[231]!=Script_OpenMemCardDialog)return 0;
    for(unsigned f=0;f<8;++f)for(unsigned g=0;g<8;++g)for(unsigned geo=0;geo<4;++geo)for(unsigned b=0;b<3;++b){
        if((flags[f]&0x20) && !(game[g]&0x1000))continue;
        ScriptNode node;memset(&node,0xa5,sizeof(node));node.flags=flags[f];node.active=7;
        ScriptNode expected=node;u32 code[20]={0};g_ScriptNodePool=&node;g_ScriptCursor=code+16;
        g_GameState.flags=game[g];g_GameStateFlags=block[b];g_GeomScroll.state.flags=geom[geo];
        u32 result=Script_OpenMemCardDialog(NULL);
        const u32 words[]={result,node.flags,node.active,(u32)((uintptr_t)g_ScriptCursor-(uintptr_t)(code+16)),
            g_GameState.flags,(u32)g_GeomScroll.state.flags,g_GameStateFlags};
        for(unsigned w=0;w<7;++w)for(unsigned byte=0;byte<4;++byte)
            hash=(hash^((words[w]>>(byte*8))&255))*UINT64_C(1099511628211);
        expected.flags=node.flags;expected.active=node.active;
        if(memcmp(&node,&expected,sizeof(node)) || g_ScriptNodePool!=&node)return 0;
        ++cases;
    }
    FieldActor *saved_actor=g_CurrentEntity;FieldActor actor={0};u32 program[]={231,0,32,0};
    ScriptNode node={.current.script=program,.active=1,.flags=0x80};
    g_CurrentEntity=&actor;g_ScriptNodePool=&node;g_GameState.flags=0;Script_RunQueue();
    int valid=node.current.script==program && node.flags==0xa0 && node.active==1 && !g_ScriptNodePool;
    g_GameState.flags=0x1000;g_ScriptNodePool=&node;Script_RunQueue();
    valid=valid && node.current.script==program+4 && node.flags==0xb0 && !node.active && !g_ScriptNodePool;
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    g_GameState.flags=saved_game;g_GameStateFlags=saved_block;g_GeomScroll.state.flags=saved_geom;
    if(!valid || cases!=576 || hash!=UINT64_C(0x4016f1aa27ca1435))return 0;
    puts("Original opcode231: 576 unhooked USA CPU first-yield/already-loading states, native cursor rewind and dispatcher continuation passed; menu construction is exercised by the runtime replay");
    return 1;
}

static int original_movement_gate_opcodes(void)
{
    int (*handlers[])(int **)={Script_SetMoveFlag,Script_ClearMoveFlag};
    u8 saved_gate=g_PlayerMovementEnabled;FieldActor *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    for(unsigned op=0;op<2;++op)for(unsigned value=0;value<256;++value){
        if(g_ScriptQueueOpcodeHandlers[180+op]!=handlers[op])return 0;
        g_PlayerMovementEnabled=value;u32 result=handlers[op](NULL);
        const u32 words[]={result,g_PlayerMovementEnabled};
        for(unsigned w=0;w<2;++w)for(unsigned b=0;b<4;++b)
            hash=(hash^((words[w]>>(b*8))&255))*UINT64_C(1099511628211);
        FieldActor actor={0};u32 program[]={180+op,0,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};
        g_PlayerMovementEnabled=value;g_CurrentEntity=&actor;g_ScriptNodePool=&node;Script_RunQueue();
        if(g_PlayerMovementEnabled!=words[1] || result!=1 || node.current.script!=program+4 ||
           node.flags!=0x90 || node.active || g_ScriptNodePool)return 0;
        ++cases;
    }
    g_PlayerMovementEnabled=saved_gate;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(cases!=512 || hash!=UINT64_C(0xa6aa125c6851c325))return 0;
    puts("Original opcodes180/181: 512 unhooked USA CPU movement-gate states and real dispatcher completion passed");
    return 1;
}

static int original_inventory_storage_swaps(void)
{
    const s16 ids[]={0,260,512,513,514,515},mapping[]={4,2,5,0,3,1};const u8 flags[]={1,0x20,0xff};
    PlayerInventoryState saved_player=g_PlayerInventoryState;InventoryItemWorkspace saved_workspace=g_InventoryItemWorkspace;
    s16 *saved_items=g_InvItemPtr,*saved_active=g_InvActiveItemList,*saved_override=g_InvActiveListOverride;
    int saved_limit=g_InvSlotLimit,saved_active_count=g_InvActiveListSlotCount,saved_override_limit=g_InvOverrideSlotLimit;
    int saved_transfer=g_InvTransferItemCount,saved_bonus=g_InvBonusSlotCount,saved_mode=g_MenuEquipMode;
    int saved_pending_count=g_InvActiveListCount;short saved_pending[10];
    memcpy(saved_pending,g_InvActiveListItems,sizeof(saved_pending));
    int saved_battle=g_MenuBattleEquipMode,saved_source=g_MenuEquipSwapSource,saved_return=g_MenuEquipSwapSkipRestore;
    u32 *saved_bits=g_InvSelectionBits;int saved_words=g_InvSelectionBitWords;
    u32 saved_aya_bits[2],saved_storage_bits[4];memcpy(saved_aya_bits,g_InvSelectionBitStorage,sizeof(saved_aya_bits));memcpy(saved_storage_bits,g_InvStorageSelectionBits,sizeof(saved_storage_bits));
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    for(unsigned mode=0;mode<4;++mode)for(unsigned fi=0;fi<6;++fi)for(unsigned ti=0;ti<6;++ti)
    for(unsigned other=0;other<6;++other)for(unsigned f=0;f<3;++f)for(unsigned pair=0;pair<2;++pair){
        s16 storage[100]={ids[other],516,0,517,0,518};
        memset(&g_PlayerInventoryState,0,sizeof(g_PlayerInventoryState));memset(&g_InventoryItemWorkspace,0,sizeof(g_InventoryItemWorkspace));
        g_AyaSaveState.inventory_slot_count=6;g_AyaSaveState.max_hp=100;g_AyaSaveState.current_hp=50;g_InventoryRuntime.tracked[2]=1;
        const s16 initial[]={256,257,258,0,260,261};memcpy(g_AyaInventoryItems,initial,sizeof(initial));
        const u8 kinds[]={1,9,8,pair?8:1,4,1};
        for(unsigned i=0;i<6;++i){ItemDataRecord *r=g_InventoryRuntime.equipment+i;r->iconId=1;r->itemId=100;r->flags=flags[f];r->kind=kinds[i];}
        for(unsigned i=0;i<9;++i){ItemDataRecord *r=g_KeyItemRecords+i;r->iconId=1;r->itemId=100;r->flags=1;r->kind=19+i%4;}
        memcpy(g_InvTransferItemList,mapping,sizeof(mapping));g_InvTransferItemCount=6;
        g_InvActiveItemList=g_InvActiveListOverride=storage;g_InvActiveListSlotCount=g_InvOverrideSlotLimit=6;
        g_InvBonusSlotCount=0;g_MenuEquipMode=2;g_MenuBattleEquipMode=0;g_MenuEquipSwapSource=1;g_MenuEquipSwapSkipRestore=0;
        for(unsigned i=0;i<2;++i)g_InvSelectionBitStorage[i]=0xdeadbeef;
        for(unsigned i=0;i<4;++i)g_InvStorageSelectionBits[i]=0xdeadbeef;
        ItemDataRecord records[128];memcpy(records,g_InventoryRuntime.equipment,sizeof(records));
        const int from_modes[]={51,52,52,51},to_modes[]={51,52,51,52};
        u32 result=Inv_RebuildWithSlotLimit(from_modes[mode],fi,to_modes[mode],ti),tracked;
        memcpy(&tracked,g_InventoryRuntime.tracked,4);
        const u32 words[]={result,tracked,g_InvSlotLimit,g_InvSelectionBitWords,g_InvItemPtr==g_AyaInventoryItems?1u:2u,g_MenuEquipSwapSkipRestore};
        for(unsigned w=0;w<6;++w)for(unsigned b=0;b<4;++b)hash=(hash^((words[w]>>(b*8))&255))*UINT64_C(1099511628211);
        const u8 *regions[]={(u8 *)g_AyaInventoryItems,(u8 *)storage,(u8 *)g_InvSelectionBitStorage,(u8 *)g_InvStorageSelectionBits};
        const unsigned sizes[]={12,12,8,16};
        for(unsigned r=0;r<4;++r)for(unsigned b=0;b<sizes[r];++b)hash=(hash^regions[r][b])*UINT64_C(1099511628211);
        if(memcmp(records,g_InventoryRuntime.equipment,sizeof(records)) || g_InvActiveItemList!=storage ||
           g_InvActiveListOverride!=storage || g_InvTransferItemCount!=6 || memcmp(g_InvTransferItemList,mapping,sizeof(mapping)))return 0;
        ++cases;
    }
    /* Closing a partly collected reward list must tolerate its empty entries. */
    for(unsigned full=0;full<2;++full){
        memset(&g_PlayerInventoryState,0,sizeof(g_PlayerInventoryState));
        g_AyaSaveState.inventory_slot_count=6;g_InvBonusSlotCount=0;
        g_InvActiveListOverride=NULL;g_InvOverrideSlotLimit=0;
        const s16 initial[]={256,257,258,259,260,261};
        memcpy(g_AyaInventoryItems,initial,sizeof(initial));
        if(!full)memset(g_AyaInventoryItems+2,0,4*sizeof(s16));
        for(unsigned j=0;j<6;++j){g_InventoryRuntime.equipment[j].kind=1;g_InventoryRuntime.equipment[j].iconId=1;}
        const short pending[]={0,258,0,259};
        memcpy(g_InvActiveListItems,pending,sizeof(pending));g_InvActiveListCount=4;
        Inv_TransferItemAlt();
        const s16 collected[]={256,257,258,259,0,0};
        const short empty[4]={0};
        if(memcmp(g_AyaInventoryItems,full?initial:collected,sizeof(initial)) ||
           memcmp(g_InvActiveListItems,full?pending:empty,sizeof(pending)))return 0;
    }
    /* A weapon can be temporarily deposited in the pending reward list,
     * then taken back. Its encoded reference is not a base-table index. */
    const s16 references[]={0x100,0x106,0x17f};
    for(unsigned r=0;r<3;++r)for(unsigned occupied=0;occupied<2;++occupied){
        memset(&g_PlayerInventoryState,0,sizeof(g_PlayerInventoryState));
        memset(&g_InventoryItemWorkspace,0,sizeof(g_InventoryItemWorkspace));
        g_AyaSaveState.inventory_slot_count=6;g_AyaSaveState.max_hp=100;g_AyaSaveState.current_hp=50;
        g_EquippedWeaponSlot=-1;g_EquippedArmorSlot=-1;
        g_MenuEquipMode=0;g_MenuBattleEquipMode=0;g_InvActiveListOverride=NULL;g_InvOverrideSlotLimit=0;
        ItemDataRecord *weapon=&g_InventoryRuntime.equipment[references[r]-0x100];
        weapon->kind=6;weapon->flags=1;weapon->iconId=5;weapon->itemId=0x83;
        g_InventoryRuntime.equipment[1].kind=9;g_InventoryRuntime.equipment[1].flags=1;
        g_InventoryRuntime.equipment[1].itemId=0x96;g_InventoryRuntime.equipment[1].iconId=1;
        ItemDataRecord before=*weapon;
        g_AyaInventoryItems[0]=occupied?257:0;g_InvActiveListItems[0]=references[r];g_InvActiveListCount=1;
        if(!Inv_TransferItem(13,0,14,0) || g_AyaInventoryItems[0]!=references[r] ||
           g_InvActiveListItems[0]!=(occupied?257:0) || memcmp(weapon,&before,sizeof(before)))return 0;
        if(!Inv_TransferItem(14,0,13,0) || g_AyaInventoryItems[0]!=(occupied?257:0) ||
           g_InvActiveListItems[0]!=references[r] || memcmp(weapon,&before,sizeof(before)))return 0;
        g_InvActiveListItems[0]=0x180;
        if(Inv_TransferItem(13,0,14,0) || g_InvActiveListItems[0]!=0x180 ||
           g_AyaInventoryItems[0]!=(occupied?257:0))return 0;
    }
    /* Full storage crosses bit31 in three words; native masks must keep
     * those high bits without a signed shift, including locked records. */
    {
        s16 storage[100];
        for(unsigned i=0;i<100;++i)storage[i]=256;
        memset(&g_PlayerInventoryState,0,sizeof(g_PlayerInventoryState));
        g_InventoryRuntime.tracked[0]=-1;g_InventoryRuntime.tracked[2]=-1;
        g_InventoryRuntime.equipment[0].kind=1;
        g_InvItemPtr=storage;g_InvSlotLimit=100;
        g_InvSelectionBits=g_InvStorageSelectionBits;g_InvSelectionBitWords=4;
        const u8 lock_flags[]={1,0x20,0x40,0x80};
        for(unsigned f=0;f<4;++f){
            g_InventoryRuntime.equipment[0].flags=lock_flags[f];
            Inv_SortSlotsByPriority();
            for(unsigned w=0;w<4;++w)
                if(g_InvStorageSelectionBits[w]!=(f?0:w==3?15u:UINT32_MAX))return 0;
        }
    }
    puts("Original full storage selection: bits31/63/95 retained and high-flag locks excluded");
    puts("Pending reward equipment round trips: first, M203 and last native records, empty/occupied destinations, metadata unchanged and invalid references rejected");
    g_InvActiveListCount=saved_pending_count;memcpy(g_InvActiveListItems,saved_pending,sizeof(saved_pending));
    g_PlayerInventoryState=saved_player;g_InventoryItemWorkspace=saved_workspace;g_InvItemPtr=saved_items;g_InvActiveItemList=saved_active;g_InvActiveListOverride=saved_override;
    g_InvSlotLimit=saved_limit;g_InvActiveListSlotCount=saved_active_count;g_InvOverrideSlotLimit=saved_override_limit;g_InvTransferItemCount=saved_transfer;
    g_InvBonusSlotCount=saved_bonus;g_MenuEquipMode=saved_mode;g_MenuBattleEquipMode=saved_battle;g_MenuEquipSwapSource=saved_source;g_MenuEquipSwapSkipRestore=saved_return;
    g_InvSelectionBits=saved_bits;g_InvSelectionBitWords=saved_words;memcpy(g_InvSelectionBitStorage,saved_aya_bits,sizeof(saved_aya_bits));memcpy(g_InvStorageSelectionBits,saved_storage_bits,sizeof(saved_storage_bits));
    if(cases!=5184 || hash!=UINT64_C(0x758e388bfa3e3cb1)){
        fprintf(stderr,"Storage swap CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original inventory/storage swap: 5184 unhooked USA CPU states, empty targets in both directions, occupied/special slots, equipped/locked restrictions, filtered indices, selection masks and unchanged equipment records passed");
    puts("Original pending reward transfer: empty entries after collection, available slots and full inventory passed");
    return 1;
}

static int original_inventory_count_opcode(void)
{
    const int queries[]={0,-1,32768,65535,214,200,255,256,257,512,513,514,515,8,55,22};
    const int limits[]={0,1,3,8,16,50};const u16 ammo[]={0,1,32767,65535};
    const u16 items[]={256,257,258,200,214,0,65535,32768,55,22};
    InventoryRuntime saved_inventory=g_InventoryRuntime;InventoryItemWorkspace saved_workspace=g_InventoryItemWorkspace;
    s16 *saved_ptr=g_InvItemPtr;int saved_limit=g_InvSlotLimit,saved_base=g_InvCategoryBaseItemId;
    FieldActor *saved_actor=g_CurrentEntity;ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    u16 list[50];for(unsigned i=0;i<50;++i)list[i]=items[i%10];
    g_InvItemPtr=(s16 *)list;g_InvCategoryBaseItemId=512;
    const u8 equipment_ids[]={44,55,214};
    for(unsigned i=0;i<3;++i)g_InventoryRuntime.equipment[i].itemId=equipment_ids[i];
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[168]!=Script_CountItemsByValue)return 0;
    for(unsigned q=0;q<16;++q)for(unsigned l=0;l<6;++l)for(unsigned a=0;a<4;++a)for(unsigned alias=0;alias<2;++alias){
        g_InvSlotLimit=limits[l];for(unsigned i=0;i<3;++i)g_KeyItemRecords[i].ammo=(u16)(ammo[a]+i*113);
        InventoryRuntime before_inventory=g_InventoryRuntime;InventoryItemWorkspace before_workspace=g_InventoryItemWorkspace;
        int input[]={queries[q],0x12345678};int *args[]={input,input+(alias?0:1)};
        u32 result=Script_CountItemsByValue(args);int count=*args[1];
        const u32 words[]={result,(u32)input[0],(u32)input[1]};
        for(unsigned i=0;i<3;++i)for(unsigned b=0;b<4;++b)hash=(hash^((words[i]>>(b*8))&255))*UINT64_C(1099511628211);
        FieldActor actor={0};actor.pad_0AC[2]=0xdeadbeef;actor.pad_0AC[3]=0x12345678;actor.pad_0AC[4]=0xa5a55a5a;
        u32 program[]={168|(2u<<13)|(1u<<20),0,(u32)queries[q],3,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_CurrentEntity=&actor;g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || actor.pad_0AC[3]!=(u32)count || actor.pad_0AC[2]!=0xdeadbeef || actor.pad_0AC[4]!=0xa5a55a5a ||
           node.current.script!=program+6 || node.flags!=0x90 || node.active || g_ScriptNodePool ||
           memcmp(&before_inventory,&g_InventoryRuntime,sizeof(before_inventory)) ||
           memcmp(&before_workspace,&g_InventoryItemWorkspace,sizeof(before_workspace)) || g_InvItemPtr!=(s16 *)list ||
           g_InvSlotLimit!=limits[l] || g_InvCategoryBaseItemId!=512)return 0;
        for(unsigned i=0;i<50;++i)if(list[i]!=items[i%10])return 0;
        ++cases;
    }
    g_InventoryRuntime=saved_inventory;g_InventoryItemWorkspace=saved_workspace;g_InvItemPtr=saved_ptr;
    g_InvSlotLimit=saved_limit;g_InvCategoryBaseItemId=saved_base;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(cases!=768 || hash!=UINT64_C(0xd87c5049da16e855)){
        fprintf(stderr,"Inventory168 CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original opcode168: 768 unhooked USA CPU states, equipment item identities, signed ordinary IDs, bounded lists, special ammo counts, aliased output, retained inventory and real actor-variable dispatcher passed");
    return 1;
}

static int original_inventory_command_opcode(void)
{
    const int commands[]={1106,1107,-1,1099,1121};
    const u16 flags[]={0,1,0x20,0x21,0x80,0xff,0x8000,0xffff};
    const u32 geom[]={0,0x2000,0xc000,0xdeadbeef};
    const s16 results[]={-1,0,387};
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    s16 saved_command=g_MenuFrameCommand;int saved_geom=g_GeomScroll.state.flags;
    u32 saved_game=g_GameState.flags,saved_block=g_GameStateFlags;
    u16 saved_max=g_AyaSaveState.max_hp,saved_hp=g_AyaSaveState.current_hp;
    MenuWidgetNode *saved_head=g_MenuWidgetActiveListHead;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[233]!=Script_RunInvCommand)return 0;
    for(unsigned c=0;c<5;++c)for(unsigned f=0;f<8;++f)for(unsigned menu=0;menu<2;++menu)
    for(unsigned r=0;r<3;++r)for(unsigned g=0;g<4;++g)for(unsigned alias=0;alias<2;++alias){
        ScriptNode node={.flags=flags[f],.active=7};u32 code[20]={0};MenuWidgetNode widget={0};
        int input[]={commands[c],0x12345678,(s32)0x87654321,(s32)0xa5a5a5a5,0x1234};
        int *args[]={input,input+1,input+2,input+(alias?0:3),input+4};
        g_ScriptNodePool=&node;g_ScriptCursor=code+16;g_MenuFrameCommand=results[r];
        g_GeomScroll.state.flags=geom[g];g_GameState.flags=0x12345678;g_GameStateFlags=0x87654321;
        g_AyaSaveState.max_hp=92;g_AyaSaveState.current_hp=17;g_MenuWidgetActiveListHead=menu?&widget:NULL;
        u32 result=Script_RunInvCommand(args);
        const u32 words[]={result,node.flags,node.active,(u32)((uintptr_t)g_ScriptCursor-(uintptr_t)(code+16)),
            (u32)*args[3],(u32)g_GeomScroll.state.flags,g_GameState.flags,g_GameStateFlags,
            (u16)g_MenuFrameCommand,(u32)input[0],(u32)input[1],(u32)input[2],(u32)input[4]};
        for(unsigned w=0;w<13;++w)for(unsigned b=0;b<4;++b)
            hash=(hash^((words[w]>>(b*8))&255))*UINT64_C(1099511628211);
        ScriptNode expected={.flags=node.flags,.active=node.active};
        if(memcmp(&node,&expected,sizeof(node)) || g_ScriptNodePool!=&node ||
           g_MenuWidgetActiveListHead!=(menu?&widget:NULL) || widget.next ||
           g_AyaSaveState.max_hp!=92 || g_AyaSaveState.current_hp!=17)return 0;
        ++cases;
    }
    g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;g_MenuFrameCommand=saved_command;
    g_GeomScroll.state.flags=saved_geom;g_GameState.flags=saved_game;g_GameStateFlags=saved_block;
    g_AyaSaveState.max_hp=saved_max;g_AyaSaveState.current_hp=saved_hp;g_MenuWidgetActiveListHead=saved_head;
    if(cases!=1920 || hash!=UINT64_C(0x2fe6ce5c1827001d)){
        fprintf(stderr,"Inventory233 CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    FieldActor *saved_actor=g_CurrentEntity;FieldActor actor={0};ScriptNode node={0};
    u32 code[]={233|(5u<<13),0,1107,0,0,0,0,32,0};
    g_AyaSaveState.max_hp=92;g_CurrentEntity=&actor;node.current.script=code;node.active=1;node.flags=0x80;g_ScriptNodePool=&node;
    g_MenuWidgetActiveListHead=NULL;g_MenuFrameCommand=0;Script_RunQueue();
    int valid=code[5]==92 && node.current.script==code+9 && node.flags==0x90 && !node.active && !g_ScriptNodePool;
    MenuWidgetNode widget={0};g_MenuWidgetActiveListHead=&widget;
    node=(ScriptNode){.current.script=code,.active=1,.flags=0x80};g_ScriptNodePool=&node;code[5]=0;
    Script_RunQueue();
    valid=valid && code[5]==92 && node.current.script==code && node.flags==0xa0 && node.active==1 && !g_ScriptNodePool;
    g_AyaSaveState.max_hp=61;g_ScriptNodePool=&node;Script_RunQueue();
    valid=valid && code[5]==92 && node.current.script==code && node.flags==0xa0 && node.active==1 && !g_ScriptNodePool;
    g_MenuWidgetActiveListHead=NULL;g_MenuFrameCommand=387;g_ScriptNodePool=&node;Script_RunQueue();
    valid=valid && code[5]==387 && node.current.script==code+9 && node.flags==0x90 && !node.active && !g_ScriptNodePool;
    g_GeomScroll.state.flags=saved_geom;g_GameState.flags=saved_game;g_GameStateFlags=saved_block;
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    g_AyaSaveState.max_hp=saved_max;g_MenuWidgetActiveListHead=saved_head;g_MenuFrameCommand=saved_command;
    if(!valid)return 0;
    puts("Original opcode233: 1920 unhooked USA CPU states, HP queries, unknown commands, aliased result operands, first menu fade, menu wait, nonzero/cancel results and dispatcher completion passed");
    return 1;
}

static int original_equip_selection_opcode(void)
{
    const s16 commands[]={-32768,-2,-1,0,1,2,3,4,128,385,386,387,32767};
    const u16 flags[]={0,1,0x20,0x21,0x80,0xff,0x8000,0xffff};
    const u32 geom[]={0,0x2000,0xc000,0xdeadbeef};
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    s16 saved_command=g_MenuFrameCommand;int saved_geom=g_GeomScroll.state.flags;
    MenuWidgetNode *saved_head=g_MenuWidgetActiveListHead;
    MenuWidgetNode widget={.mode=1,.selected_base=1};g_MenuWidgetActiveListHead=&widget;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[169]!=Entity_InitWithTimeout)return 0;
    for(unsigned c=0;c<13;++c)for(unsigned f=0;f<8;++f)for(unsigned g=0;g<4;++g){
        ScriptNode node={.flags=flags[f],.active=7};u32 code[20]={0};
        g_ScriptNodePool=&node;g_ScriptCursor=code+16;g_MenuFrameCommand=commands[c];g_GeomScroll.state.flags=geom[g];
        int input=0x12345678,output=(s32)0xa5a5a5a5;int *args[]={&input,&output};
        u32 result=Entity_InitWithTimeout(args);
        const u32 words[]={result,node.flags,node.active,(u32)((uintptr_t)g_ScriptCursor-(uintptr_t)(code+16)),
            (u32)output,(u32)g_GeomScroll.state.flags};
        for(unsigned w=0;w<6;++w)for(unsigned b=0;b<4;++b)
            hash=(hash^((words[w]>>(b*8))&255))*UINT64_C(1099511628211);
        if(input!=0x12345678 || g_MenuFrameCommand!=commands[c] ||
           g_MenuWidgetActiveListHead!=&widget || widget.next || widget.mode!=1 || widget.selected_base!=1)return 0;
        ++cases;
    }
    g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;g_MenuFrameCommand=saved_command;
    g_GeomScroll.state.flags=saved_geom;g_MenuWidgetActiveListHead=saved_head;
    if(cases!=416 || hash!=UINT64_C(0x5f9e764ae2a58395)){
        fprintf(stderr,"Equip169 CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original opcode169: 416 unhooked USA CPU states, first fade, existing-menu wait, selected/cancel boundaries, cursor rewind and retained node flags passed");
    return 1;
}

static int original_polar_position_opcode(void)
{
    const u32 patterns[]={0x80000000,0xffff0000,0xffffffff,0,1,1024,4096,65535,0x7fffffff,0x12345678,0xfedcba98,0x4000};
    const u32 positions[][2]={{0,0},{65536,0xffff0000},{0x80000000,0x7fffffff},{0x7fffffff,0x80000000},{0x12345678,0xfedcba98}};
    FieldActor *saved_actor=g_CurrentEntity;ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    u32 saved_vars[]={g_ScriptVariableBank[9],g_ScriptVariableBank[10]};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[221]!=Entity_PolarToPosition)return 0;
    for(unsigned radius=0;radius<12;++radius)for(unsigned angle=0;angle<12;++angle)
    for(unsigned pose=0;pose<5;++pose)for(unsigned alias=0;alias<6;++alias){
        FieldActor actor={.pos_x=(s32)positions[pose][0],.pos_z=(s32)positions[pose][1],.flags=0xdecafbad};
        FieldActor before=actor,expected=actor;g_CurrentEntity=&actor;
        int data[]={(s32)patterns[radius],(s32)patterns[angle],(s32)0xa5a5a5a5,(s32)0x5a5a5a5a};
        int *outputs[][2]={{data+2,data+3},{data+2,data+2},{&actor.pos_x,&actor.pos_z},{data,data+1},
            {&actor.pos_z,&actor.pos_x},{data+1,data}};
        int *args[]={data,data+1,outputs[alias][0],outputs[alias][1]};u32 result=Entity_PolarToPosition(args);
        const u32 words[]={result,(u32)actor.pos_x,(u32)actor.pos_z,(u32)data[0],(u32)data[1],(u32)data[2],(u32)data[3]};
        for(unsigned word=0;word<7;++word)for(unsigned byte=0;byte<4;++byte)
            hash=(hash^((words[word]>>(byte*8))&255))*UINT64_C(1099511628211);
        expected.pos_x=actor.pos_x;expected.pos_z=actor.pos_z;
        if(memcmp(&actor,&expected,sizeof(actor)))return 0;
        if(!alias){
            actor=before;u32 program[]={221|(4u<<13)|(2u<<23)|(2u<<26),0,patterns[radius],patterns[angle],9,10,32,0};
            ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;
            g_ScriptVariableBank[9]=0xa5a5a5a5;g_ScriptVariableBank[10]=0x5a5a5a5a;Script_RunQueue();
            if(result!=1 || memcmp(&actor,&expected,sizeof(actor)) || g_ScriptVariableBank[9]!=(u32)data[2] ||
               g_ScriptVariableBank[10]!=(u32)data[3] || node.current.script!=program+8 || node.flags!=0x90 ||
               node.active || g_ScriptNodePool)return 0;
        }
        ++cases;
    }
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    g_ScriptVariableBank[9]=saved_vars[0];g_ScriptVariableBank[10]=saved_vars[1];
    if(cases!=4320 || hash!=UINT64_C(0xa8ae21be8bc9567d)){
        fprintf(stderr,"Polar221 CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original opcode221: 4320 unhooked USA CPU states, signed radius/angle wrapping, boundary positions, six input/output alias patterns and full dispatcher passed");
    {
        const s16 yaws[]={0,1,-1,1024,2048,3072,4095,4096,32767,-32768,731,-731};
        uint64_t yaw_hash=UINT64_C(14695981039346656037);unsigned count=0;
        if(g_ScriptQueueOpcodeHandlers[182]!=Entity_PolarToPosition2)return 0;
        for(unsigned sweep=0;sweep<2;++sweep)for(unsigned test=0;test<(sweep?65536u:5760u);++test){
            unsigned alias=sweep?0:test%8,pose=sweep?3:(test/8)%5,angle=(test/40)%12,radius=sweep?0:test/480;
            FieldActor actor={.pos_x=(s32)positions[pose][0],.pos_z=(s32)positions[pose][1],
                .rot_y=sweep?(s16)test:yaws[angle],.rot_z=0x1234,.flags=0xdecafbad};
            FieldActor before=actor,expected=actor;g_CurrentEntity=&actor;
            int data[]={(s32)patterns[radius],(s32)0xa5a5a5a5,0x5a5a5a5a};
            int *outputs[][2]={{data+1,data+2},{data+1,data+1},{&actor.pos_x,&actor.pos_z},{data,data+1},
                {&actor.pos_z,&actor.pos_x},{data+1,data},{&actor.pos_z,&actor.pos_z},{&actor.pos_x,&actor.pos_x}};
            int *args[]={data,outputs[alias][0],outputs[alias][1]};u32 result=Entity_PolarToPosition2(args);
            const u32 words[]={result,(u32)actor.pos_x,(u32)actor.pos_z,(u32)data[0],(u32)data[1],(u32)data[2]};
            for(unsigned word=0;word<6;++word)for(unsigned byte=0;byte<4;++byte)
                yaw_hash=(yaw_hash^((words[word]>>(byte*8))&255))*UINT64_C(1099511628211);
            expected.pos_x=actor.pos_x;expected.pos_z=actor.pos_z;
            if(memcmp(&actor,&expected,sizeof(actor)))return 0;
            if(!alias){
                actor=before;u32 program[]={182|(3u<<13)|(2u<<20)|(2u<<23),0,patterns[radius],9,10,32,0};
                g_ScriptVariableBank[9]=0xa5a5a5a5;g_ScriptVariableBank[10]=0x5a5a5a5a;
                ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
                if(result!=1 || memcmp(&actor,&expected,sizeof(actor)) || g_ScriptVariableBank[9]!=(u32)data[1] ||
                   g_ScriptVariableBank[10]!=(u32)data[2] || node.current.script!=program+7 || node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
            }
            ++count;
        }
        g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
        g_ScriptVariableBank[9]=saved_vars[0];g_ScriptVariableBank[10]=saved_vars[1];
        if(count!=71296 || yaw_hash!=UINT64_C(0xd982fab26e42f8fa)){
            fprintf(stderr,"Polar182 CPU hash %u %016llx\n",count,(unsigned long long)yaw_hash);return 0;
        }
        puts("Original opcode182: 71296 USA CPU states, all 65536 yaws, signed radius/position word wrapping, eight aliases, actor guards and 66256 real dispatcher programs passed");
    }
    return 1;
}

static int original_battle_entry_opcode(void)
{
    Pe1GameState saved_state=g_GameState;FieldActor actor={0},*saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    const u8 phases[]={0,59,60,255},stories[]={0,1,2,3,4,128,252,255};
    const u32 flags[]={0,1,0x400000,0x800000,0xa5a55a5a,0xffffffff};
    uint64_t hash=UINT64_C(14695981039346656037);g_CurrentEntity=&actor;
    if(g_ScriptQueueOpcodeHandlers[85]!=Boot_MemCardStepInit)return 0;
    for(unsigned p=0;p<4;++p)for(unsigned s=0;s<8;++s)for(unsigned f=0;f<6;++f){
        int mode=0x12340045;int *args[]={&mode};u32 scratch[16]={0};
        ScriptNode node={.active=7,.flags=0x80};g_ScriptNodePool=&node;g_ScriptCursor=scratch+8;
        g_GameState.flags=flags[f];g_GameState.memcard_init_phase=phases[p];g_GameState.story_day_flags=stories[s];
        u32 result=g_ScriptQueueOpcodeHandlers[85](args);
        const u32 words[]={result,g_GameState.flags,g_GameState.memcard_init_phase,(u32)node.active,
            (u32)(g_ScriptCursorAddress-(uintptr_t)scratch)};
        for(unsigned i=0;i<5;++i)for(unsigned b=0;b<4;++b)
            hash=(hash^((words[i]>>(b*8))&255))*UINT64_C(1099511628211);
        if(node.flags!=0x80)return 0;
        u32 program[]={85|(1u<<13),0,(u32)mode,32,0};
        node=(ScriptNode){.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;
        g_GameState.flags=flags[f];g_GameState.memcard_init_phase=phases[p];g_GameState.story_day_flags=stories[s];
        Script_RunQueue();
        if(g_GameState.flags!=words[1] || g_GameState.memcard_init_phase!=words[2] || g_ScriptNodePool ||
           node.current.script!=program+(result?5:0) || node.active!=(result?0:1) ||
           node.flags!=(result?0x90:0x80))return 0;
    }
    ScriptNode node={.active=7};u32 scratch[16]={0};int mode=0;int *args[]={&mode};
    g_ScriptNodePool=&node;g_ScriptCursor=scratch+8;g_GameState.memcard_init_phase=0x39;
    g_GameState.tim_load_state=0x34;g_GameState.scene_load_scratch=NULL;
    if(Boot_MemCardStepInit(args)!=-1 || g_GameState.memcard_init_phase!=0x39 ||
       g_GameState.tim_load_state!=0x34 || node.active!=7 || g_ScriptCursor!=scratch+8)return 0;
    g_GameState=saved_state;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0xdcc852ebc349be75))return 0;
    int saved_mode=g_BattleModeState;
    const unsigned mode_ops[]={137,138,149,150};const int modes[]={6,5,0,8};
    int (*mode_handlers[])(int **)={Menu_SetModeState6,Menu_SetModeState5,Menu_ClearModeState,Menu_SetModeState8};
    for(unsigned m=0;m<4;++m){
        unsigned op=mode_ops[m];int (*handler)(int **)=mode_handlers[m];
        if(g_ScriptQueueOpcodeHandlers[op]!=handler)return 0;
        for(unsigned i=0;i<6;++i){
            g_BattleModeState=(s32)flags[i];
            u32 program[]={op,0,32,0};ScriptNode dispatched={.current.script=program,.active=1,.flags=0x80};
            g_CurrentEntity=&actor;g_ScriptNodePool=&dispatched;Script_RunQueue();
            if(g_BattleModeState!=modes[m] || g_ScriptNodePool || dispatched.active ||
               dispatched.flags!=0x90 || dispatched.current.script!=program+4)return 0;
        }
    }
    g_BattleModeState=saved_mode;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    puts("Original battle mode opcodes 137/138/149/150: four native handlers and dispatcher completion passed");
    puts("Original battle entry opcode 85: 192 USA CPU phase/flag/yield states, real dispatcher and texture-load error propagation passed");return 1;
}

static int original_conditional_branch_opcode(void)
{
    FieldActor actor={0},*saved_actor=g_CurrentEntity;ScriptNode *saved_node=g_ScriptNodePool;
    u32 *saved_cursor=g_ScriptCursor;u32 program[10]={0};actor.script_base=(u8 *)program;g_CurrentEntity=&actor;
    const u32 offsets[]={0,1,16,0x7fffffff,0x80000000,0xffffffff};
    if(g_ScriptQueueOpcodeHandlers[29]!=Script_CondBranch)return 0;
    for(unsigned equal=0;equal<2;++equal)for(unsigned i=0;i<6;++i){
        int a=1,b=equal?1:2,offset=(s32)offsets[i];int *args[]={&a,&b,&offset};
        g_ScriptCursor=program+3;
        uintptr_t expected=equal?(uintptr_t)(program+3):(uintptr_t)program+(intptr_t)(s32)(offsets[i]<<1);
        if(g_ScriptQueueOpcodeHandlers[29](args)!=1 || g_ScriptCursorAddress!=expected)return 0;
    }
    for(unsigned equal=0;equal<2;++equal){
        u32 script[]={29|(3u<<13),0,1,equal?1u:2u,16,32,0,0,32,0};
        actor.script_base=(u8 *)script;ScriptNode node={.current.script=script,.active=1,.flags=0x80};g_ScriptNodePool=&node;
        Script_RunQueue();if(node.current.script!=script+(equal?7:10) || !(node.flags&0x10) || g_ScriptNodePool)return 0;
    }
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    puts("Original opcode 29: native conditional target, 32-bit offset wrapping, both paths and real dispatcher passed");return 1;
}

static int original_context_getter_opcodes(void)
{
    FieldActor *saved_list=g_FieldActorListHead,*saved_player=g_PlayerEntity,*saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    const u32 patterns[]={0,1,0x7fff,0x8000,0xffff,0x80000000,0xffffffff,0x010c0000,0xa5a55a5a,0x12345678,0x7fffffff,0x600000};
    FieldActor actor={0};Combatant player;EnemyCombatant enemy;
    uint64_t hash=UINT64_C(14695981039346656037);
    if(g_ScriptQueueOpcodeHandlers[89]!=Entity_GetField || g_ScriptQueueOpcodeHandlers[139]!=Entity_FindByTypeId)return 0;
    for(unsigned kind=0;kind<2;++kind)for(unsigned p=0;p<12;++p)for(unsigned cmd=0;cmd<512;++cmd){
        u32 raw[0xd8/4];for(unsigned i=0;i<0xd8/4;++i)raw[i]=patterns[p];
        raw[0]=(raw[0]&~0xe00000u)|((p%6)<<21);
        memset(&player,0,sizeof(player));memcpy(&player,raw,0x1c);
        memcpy(&player.maxHP,(u8 *)raw+0x1c,0xd8-0x1c);
        memset(&enemy,0,sizeof(enemy));memcpy(&enemy,raw,0x18);
        memcpy(enemy.records,(u8 *)raw+0x1c,0xd8-0x1c);
        actor.state=(FieldActorState *)(kind?(void *)&enemy:(void *)&player);
        actor.type_id=kind?6:0;actor.sub_id=3;actor.flags=0;actor.next=NULL;
        g_PlayerEntity=&actor;g_CurrentEntity=&actor;g_FieldActorListHead=&actor;
        int command=cmd,out=(s32)0x98765432;int *args[]={&command,&out};
        u32 result=Entity_GetField(args),post_flags=kind?enemy.statusFlags2:player.statusFlags2;
        const u32 words[]={result,(u32)out,post_flags};
        for(unsigned i=0;i<3;++i)for(unsigned b=0;b<4;++b)
            hash=(hash^((words[i]>>(b*8))&255))*UINT64_C(1099511628211);
        for(unsigned typed=0;typed<2;++typed){
            player.statusFlags2=patterns[p];enemy.statusFlags2=patterns[p];
            u32 program[8];unsigned length;
            if(typed){u32 code[]={139|(4u<<13),0,actor.type_id,3,cmd,0x98765432,32,0};memcpy(program,code,sizeof(code));length=8;}
            else{u32 code[]={89|(2u<<13),0,cmd,0x98765432,32,0};memcpy(program,code,sizeof(code));length=6;}
            ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
            if(result!=1 || program[typed?5:3]!=(u32)out || (kind?enemy.statusFlags2:player.statusFlags2)!=post_flags ||
               node.current.script!=program+length || node.flags!=0x90 || node.active || g_ScriptNodePool){
                fprintf(stderr,"Context getter kind=%u pattern=%u cmd=%u typed=%u\n",kind,p,cmd,typed);return 0;}
        }
    }
    FieldActor missing[3]={{0}};
    for(unsigned i=0;i<3;++i){missing[i].type_id=6;missing[i].sub_id=i?3:2;missing[i].next=i<2?&missing[i+1]:NULL;missing[i].flags=i?0x10:0;}
    for(unsigned mode=0;mode<3;++mode){
        g_FieldActorListHead=mode==0?NULL:missing;u32 code[]={139|(4u<<13),0,mode==2?7:6,3,43,0x98765432,32,0};
        ScriptNode node={.current.script=code,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(code[5]!=0x98765432 || node.current.script!=code+8 || node.flags!=0x90 || g_ScriptNodePool)return 0;
    }
    g_FieldActorListHead=saved_list;g_PlayerEntity=saved_player;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0xbc5b5bf975c75f8d)){fprintf(stderr,"Context getter hash %016llx\n",(unsigned long long)hash);return 0;}
    puts("Original context getters 89/139: 12288 unhooked USA CPU states, all command bytes, signed HP, event records, status consumption and dispatcher selection passed");return 1;
}

static int original_context_setter_opcodes(void)
{
    FieldActor *saved_list=g_FieldActorListHead,*saved_player=g_PlayerEntity,*saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;s16 saved_floor=g_RoomFloorY->y;
    const u32 patterns[]={0,1,0x7fff,0x8000,0xffff,0x80000000,0xffffffff,0x010c0000,0xa5a55a5a,0x12345678,0x7fffffff,0x600000};
    FieldActor actor={0};Combatant player;FieldActorState enemy;
    uint64_t hash=UINT64_C(14695981039346656037);
    if(g_ScriptQueueOpcodeHandlers[90]!=Entity_SetField)return 0;
    for(unsigned kind=0;kind<2;++kind)for(unsigned p=0;p<12;++p)for(unsigned cmd=0;cmd<512;++cmd){
        u32 raw[0xd8/4];for(unsigned i=0;i<0xd8/4;++i)raw[i]=patterns[p];
        memset(&player,0,sizeof(player));memcpy(&player,raw,0x1c);memcpy(&player.maxHP,(u8 *)raw+0x1c,0xd8-0x1c);
        memset(&enemy,0,sizeof(enemy));memcpy(&enemy,raw,0x18);memcpy(&enemy.count_limit,(u8 *)raw+0x1c,0xd8-0x1c);
        actor.state=kind?&enemy:(FieldActorState *)&player;actor.type_id=kind?6:0;actor.sub_id=3;actor.flags=0;actor.next=NULL;
        g_PlayerEntity=&actor;g_CurrentEntity=&actor;g_FieldActorListHead=&actor;g_RoomFloorY->y=0x1234;
        Combatant before_player=player;FieldActorState before_enemy=enemy;
        int command=cmd,value=(s32)patterns[(p+cmd)%12];int *args[]={&command,&value};u32 result=Entity_SetField(args);
        u8 after[0xd8];
        if(kind){memcpy(after,&enemy,0x18);memcpy(after+0x18,(u8 *)raw+0x18,4);memcpy(after+0x1c,&enemy.count_limit,0xd8-0x1c);}
        else{memcpy(after,&player,0x1c);memcpy(after+0x1c,&player.maxHP,0xd8-0x1c);}
        for(unsigned b=0;b<4;++b)hash=(hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
        for(unsigned b=0;b<sizeof(after);++b)hash=(hash^after[b])*UINT64_C(1099511628211);
        u16 floor=g_RoomFloorY->y;for(unsigned b=0;b<2;++b)hash=(hash^((floor>>(b*8))&255))*UINT64_C(1099511628211);
        Combatant after_player=player;FieldActorState after_enemy=enemy;
        for(unsigned typed=0;typed<2;++typed){
            player=before_player;enemy=before_enemy;g_RoomFloorY->y=0x1234;
            u32 program[8];unsigned length;
            if(typed){u32 code[]={206|(4u<<13),0,actor.type_id,3,cmd,(u32)value,32,0};memcpy(program,code,sizeof(code));length=8;}
            else{u32 code[]={90|(2u<<13),0,cmd,(u32)value,32,0};memcpy(program,code,sizeof(code));length=6;}
            ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
            if(result!=1 || memcmp(&player,&after_player,sizeof(player)) || memcmp(&enemy,&after_enemy,sizeof(enemy)) ||
               (u16)g_RoomFloorY->y!=floor || node.current.script!=program+length || node.flags!=0x90 || node.active || g_ScriptNodePool)return 0;
        }
    }
    g_RoomFloorY->y=saved_floor;g_FieldActorListHead=saved_list;g_PlayerEntity=saved_player;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0x2249f25ad3a196bd)){fprintf(stderr,"Context setter hash %016llx\n",(unsigned long long)hash);return 0;}
    puts("Original context setters 90/206: 12288 unhooked USA CPU states, all command bytes, complete actor/floor writes and both dispatcher forms passed");return 1;
}

static int original_script_event_entries(void)
{
    FieldActor actor={0},*saved_actor=g_CurrentEntity;ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    EntityAnimEventCore core;actor.state=(FieldActorState *)&core;g_CurrentEntity=&actor;
    const u32 patterns[]={0,1,0x7fff,0x8000,0xffff,0x80000000,0xffffffff,0x010c0000,0xa5a55a5a,0x12345678,0x7fffffff,0x600000};
    const unsigned ops[]={112,183,176},counts[]={11,5,3};
    int (*handlers[])(int **)={Script_SetEntityAnim12Args,Script_SetEntityAnim5Args,Script_SetBattleEntryCoords};
    uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned op=0;op<3;++op)for(unsigned slot=0;slot<6;++slot)for(unsigned p=0;p<12;++p){
        memset(&core,0,sizeof(core));memset(&core,0xa5,0x18);memset(core.records,0xa5,0xb4-0x1c);
        EntityAnimEventCore before=core;int values[11];int *args[11];
        for(unsigned i=0;i<11;++i){values[i]=i?(s32)patterns[(p+i)%12]:(int)(slot|((p%2)<<8));args[i]=values+i;}
        if(g_ScriptQueueOpcodeHandlers[ops[op]]!=handlers[op])return 0;
        u32 result=handlers[op](args);EntityAnimEventCore after=core;
        u8 raw[0xb4];memcpy(raw,&core,0x18);memset(raw+0x18,0xa5,4);memcpy(raw+0x1c,core.records,0xb4-0x1c);
        for(unsigned b=0;b<4;++b)hash=(hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
        for(unsigned b=0;b<sizeof(raw);++b)hash=(hash^raw[b])*UINT64_C(1099511628211);
        core=before;u32 program[15]={ops[op]|(counts[op]<<13),0};
        for(unsigned i=0;i<counts[op];++i)program[2+i]=(u32)values[i];
        program[2+counts[op]]=32;
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || memcmp(&core,&after,sizeof(core)) || node.current.script!=program+4+counts[op] ||
           node.flags!=0x90 || node.active || g_ScriptNodePool)return 0;
    }
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0xf942d785949a4441)){fprintf(stderr,"Script event setup hash %016llx\n",(unsigned long long)hash);return 0;}
    /* Pause the first matching node, or resume every match in all three
     * buckets. Addresses are compared at native width, then normalized only
     * for the independent USA CPU stream hash. */
    {
        const u32 ids[]={0,1,2,7,0x7fff,0x8000,0xffff,0x10000,0xffffffff,0x1234};
        const u16 flags[]={0,0x20,0x40,0x60,0xffff,0x8000,0x1234,0xa5a5};
        const unsigned masks[]={0,0x155,0x1ff,0xaa};
        int (*handlers[])(int **)={Entity_MarkNodeAnimPendingById,Entity_AdvanceNodeById};
        uint64_t queue_hash=UINT64_C(14695981039346656037);unsigned cases=0;
        FieldActor queued={0};ScriptNode entries[9];u32 initial[9][4]={{0}},resumed[9][4]={{0}};
        g_CurrentEntity=&queued;
        for(unsigned op=0;op<2;++op)for(unsigned layout=0;layout<4;++layout)
        for(unsigned family=0;family<3;++family)for(unsigned flag=0;flag<8;++flag)
        for(unsigned pending=0;pending<4;++pending)for(unsigned q=0;q<10;++q){
            if(g_ScriptQueueOpcodeHandlers[98+op]!=handlers[op])return 0;
            for(unsigned bucket=0;bucket<3;++bucket)
                queued.script_node_lists[bucket]=layout==0 || (layout==1 && bucket!=0) || (layout==3 && bucket==0)?NULL:entries+bucket*3;
            for(unsigned i=0;i<9;++i){
                memset(entries+i,0xa5,sizeof(*entries));entries[i].current.script=initial[i];
                entries[i].next_value=masks[pending]&(1u<<i)?(intptr_t)resumed[i]:0;
                entries[i].flags=flags[(flag+i)%8];entries[i].seq=family==0?i%4:family==1?(u16)ids[i%10]:7;
                entries[i].trigger_value=0x12345678+i;entries[i].active=(flag+i)%3;
                entries[i].target14=(s32)(0xa5a50000u+i);entries[i].target18.coordinate=0x5a5a0000+i;
                entries[i].target1c=(s32)(0x98760000u+i);entries[i].target20=0x76540000+i;
                entries[i].next=i%3!=2?entries+i+1:NULL;entries[i].prev=i%3?entries+i-1:NULL;
            }
            FieldActor actor_before=queued;ScriptNode before[9],after[9];memcpy(before,entries,sizeof(entries));
            int query=(s32)ids[q],*args[]={&query};u32 result=handlers[op](args);
            for(unsigned i=0;i<9;++i){
                ScriptNode *entry=entries+i;unsigned current;
                if(entry->current.script==initial[i])current=1;
                else if(entry->current.script==resumed[i])current=2;
                else return 0;
                if(entry->next_value && entry->next_value!=(intptr_t)resumed[i])return 0;
                ScriptNode expected=before[i];expected.current=entry->current;expected.flags=entry->flags;expected.active=entry->active;
                if(memcmp(entry,&expected,sizeof(expected)))return 0;
                const u32 words[]={current,entry->next_value?2u:0u,entry->flags|((u32)entry->seq<<16),
                    (u32)entry->trigger_value,(u32)entry->active,(u32)entry->target14,(u32)entry->target18.coordinate,
                    (u32)entry->target1c,(u32)entry->target20,entry->next?(unsigned)(entry->next-entries)+1:0,
                    entry->prev?(unsigned)(entry->prev-entries)+1:0};
                if(i==0)for(unsigned b=0;b<4;++b)queue_hash=(queue_hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
                for(unsigned k=0;k<11;++k)for(unsigned b=0;b<4;++b)
                    queue_hash=(queue_hash^((words[k]>>(b*8))&255))*UINT64_C(1099511628211);
            }
            memcpy(after,entries,sizeof(entries));memcpy(entries,before,sizeof(entries));
            u32 program[]={(98+op)|(1u<<13),0,ids[q],32,0};
            ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
            if(result!=1 || query!=(s32)ids[q] || program[2]!=ids[q] || memcmp(&queued,&actor_before,sizeof(queued)) ||
               memcmp(entries,after,sizeof(entries)) || node.current.script!=program+5 || node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
            ++cases;
        }
        if(cases!=7680 || queue_hash!=UINT64_C(0x7861a5199614bd55)){
            fprintf(stderr,"Node control CPU mismatch: %u %016llx\n",cases,(unsigned long long)queue_hash);return 0;
        }
        u32 suspended[]={32,0},resume[]={10|(2u<<13),0,0x98765432,0x12345678,32,0};
        ScriptNode waiting={.current.script=suspended,.next_value=(intptr_t)resume,.flags=0xe0,.seq=7};
        memset(queued.script_node_lists,0,sizeof(queued.script_node_lists));queued.script_node_lists[2]=&waiting;
        u32 wake[]={99|(1u<<13),0,7,32,0};ScriptNode wake_node={.current.script=wake,.active=1,.flags=0x80};
        g_ScriptNodePool=&wake_node;Script_RunQueue();
        if(waiting.current.script!=resume || waiting.next_value!=(intptr_t)resume || waiting.flags!=0x80 || waiting.active!=1)return 0;
        g_ScriptNodePool=&waiting;Script_RunQueue();
        if(resume[2]!=0x12345678 || waiting.current.script!=resume+6 || waiting.active || waiting.flags!=0x90 || g_ScriptNodePool)return 0;
        puts("Original opcodes 98/99: 7680 USA CPU states, three buckets, duplicate IDs, null/resume cursors, full native pointers and real dispatcher passed");
    }
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    puts("Original event setup opcodes 112/183/176: 216 unhooked USA CPU states, six records, narrowed operands, all parameters and full dispatcher passed");return 1;
}

static int original_animation_event_opcode(void)
{
    FieldActor *old_actor=g_CurrentEntity,*old_player=g_PlayerEntity,*old_head=g_FieldActorListHead;
    ScriptNode *old_node=g_ScriptNodePool;u32 *old_cursor=g_ScriptCursor;u32 old_flags=g_GameStateFlags;
    unsigned old_sounds=sounds;void *old_rows[48];memcpy(old_rows,g_GameState.bank_rows[0],sizeof(old_rows));
    RenderAnimationDataHeader animation={0};animation.frame_count=9;
    for(unsigned i=0;i<48;++i)g_GameState.bank_rows[0][i]=&animation;
    g_PlayerEntity=NULL;g_FieldActorListHead=NULL;g_GameStateFlags=2;
    const unsigned modes[]={0,1,2,6,7,8,9,14,15},flags[]={0,0x6000,0x40000000,0x80000000};
    const unsigned frames[][2]={{2,4},{8,9},{8,1}},kinds[]={0,2,2,4};
    uint64_t hash=UINT64_C(14695981039346656037);
    if(g_ScriptQueueOpcodeHandlers[100]!=Script_RunEntityCommand)return 0;
    for(unsigned slot=0;slot<6;++slot)for(unsigned variant=0;variant<4;++variant)for(unsigned m=0;m<9;++m)
    for(unsigned state=0;state<5;++state)for(unsigned f=0;f<3;++f)for(unsigned mask=0;mask<4;++mask){
        EntityAnimEventCore core={0};BattleEntity actors[2]={{0}};
        core.flags.raw=(slot<<21)|flags[mask];core.kind=kinds[variant];core.baseMode=4;core.assetId=1000;
        core.active=core.records+slot;
        for(unsigned i=0;i<6;++i){core.records[i].state=i==slot?state:0;core.records[i].enterMode=6;core.records[i].exitMode=7;
            core.records[i].enterStep=0x18000;core.records[i].exitStep=0x24000;core.records[i].category=slot%2?3:1;core.records[i].frame=3;}
        for(unsigned i=0;i<2;++i){actors[i].core=&core;actors[i].entityFlags=0x240;actors[i].animLastFrame=8;actors[i].animStep=0x12000;}
        if(variant>=2)actors[0].parent=actors+1;
        BattleEntity *selected=actors+(variant>=2);selected->actionMode=modes[m];
        selected->animPrev.fixed=frames[f][0]<<16;selected->animFrame=frames[f][1]<<16;
        EntityAnimEventCore before_core=core;BattleEntity before_actors[2];memcpy(before_actors,actors,sizeof(actors));
        g_CurrentEntity=(FieldActor *)actors;ScriptNode node={.active=7};u32 scratch[16]={0};
        g_ScriptNodePool=&node;g_ScriptCursor=scratch+8;sounds=0;
        int command=slot|((mask%2)<<8),out=(s32)0x98765432;int *args[]={&command,&out};
        u32 result=Script_RunEntityCommand(args);u32 words[27]={result,(u32)out,(u32)(g_ScriptCursorAddress-(uintptr_t)scratch),
            (u32)node.active,core.flags.raw,core.active?1+(u32)(core.active-core.records):0};
        for(unsigned i=0;i<6;++i)words[6+i]=core.records[i].state;
        for(unsigned i=0;i<2;++i){u32 values[]={actors[i].entityFlags,actors[i].actionMode,actors[i].animLastFrame,
            actors[i].animFrame,(u32)actors[i].animPrev.fixed,(u32)actors[i].animStep,actors[i].actionData==&animation};
            memcpy(words+12+i*7,values,sizeof(values));}
        words[26]=sounds;
        for(unsigned i=0;i<27;++i)for(unsigned b=0;b<4;++b)hash=(hash^((words[i]>>(b*8))&255))*UINT64_C(1099511628211);
        EntityAnimEventCore after_core=core;BattleEntity after_actors[2];memcpy(after_actors,actors,sizeof(actors));
        core=before_core;memcpy(actors,before_actors,sizeof(actors));sounds=0;
        u32 code[]={100|(2u<<13),0,(u32)command,0x98765432,32,0};
        node=(ScriptNode){.current.script=code,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(code[3]!=(u32)out || memcmp(&core,&after_core,sizeof(core)) || memcmp(actors,after_actors,sizeof(actors)) ||
           sounds!=words[26] || node.current.script!=code+(result?6:0) || node.active!=(result?0:1) ||
           node.flags!=(result?0x90:0x80) || g_ScriptNodePool)return 0;
    }
    g_CurrentEntity=old_actor;g_PlayerEntity=old_player;g_FieldActorListHead=old_head;g_ScriptNodePool=old_node;g_ScriptCursor=old_cursor;
    g_GameStateFlags=old_flags;sounds=old_sounds;memcpy(g_GameState.bank_rows[0],old_rows,sizeof(old_rows));
    if(hash!=UINT64_C(0xaa6e5b4256b2bf25)){fprintf(stderr,"Animation event hash %016llx\n",(unsigned long long)hash);return 0;}
    puts("Original animation event opcode 100: 12960 USA CPU cases with recorded audio boundary, parent selection, frames/wrap, six records, all state changes and dispatcher retry passed");return 1;
}

static int original_action_wait_opcode(void)
{
    FieldActor actor={0},*old_actor=g_CurrentEntity,*old_head=g_FieldActorListHead,*old_player=g_PlayerEntity;
    ScriptNode *old_node=g_ScriptNodePool;u32 *old_cursor=g_ScriptCursor,old_flags=g_GameStateFlags;
    void *old_rows[48];memcpy(old_rows,g_GameState.bank_rows[0],sizeof(old_rows));
    RenderAnimationDataHeader animation={0};animation.frame_count=9;
    for(unsigned i=0;i<48;++i)g_GameState.bank_rows[0][i]=&animation;
    g_CurrentEntity=&actor;g_FieldActorListHead=NULL;g_PlayerEntity=NULL;g_GameStateFlags=0;
    const u32 node_flags[]={0x80,0xa0},modes[]={0,1,4,0x10006,0xffff0007},flags[]={0,0x100,0x100000,0xffffffff};
    const u32 steps[]={0,65536,0x80000000,0xffffffff},frames[][2]={{0,0},{1,0},{0,1},{0x80000000,0x7fffffff},{0xffffffff,0},{0,0xffffffff}};
    const u8 lasts[]={0,1,8};uint64_t hash=UINT64_C(14695981039346656037);
    if(g_ScriptQueueOpcodeHandlers[198]!=Script_SetEntityActionAndWait)return 0;
    for(unsigned n=0;n<2;++n)for(unsigned m=0;m<5;++m)for(unsigned f=0;f<4;++f)
    for(unsigned l=0;l<3;++l)for(unsigned step=0;step<4;++step)for(unsigned frame=0;frame<6;++frame){
        actor=(FieldActor){0};actor.mode=2;actor.action=lasts[l];actor.flags=flags[f];
        actor.anim.fixed=(s32)frames[frame][0];actor.anim_prev=(s32)frames[frame][1];actor.anim_step=(s32)steps[step];
        FieldActor before=actor;ScriptNode node={.active=7,.flags=node_flags[n]};u32 scratch[16]={0};
        g_ScriptNodePool=&node;g_ScriptCursor=scratch+8;int mode=(s32)modes[m];int *args[]={&mode};
        u32 result=Script_SetEntityActionAndWait(args);FieldActor after=actor;
        const u32 words[]={result,node.flags,(u32)node.active,(u32)(g_ScriptCursorAddress-(uintptr_t)scratch),actor.flags,
            actor.mode,actor.action,(u32)actor.anim.fixed,(u32)actor.anim_prev,(u32)actor.anim_step,actor.action_data==&animation};
        for(unsigned i=0;i<11;++i)for(unsigned b=0;b<4;++b)hash=(hash^((words[i]>>(b*8))&255))*UINT64_C(1099511628211);
        actor=before;u32 code[]={198|(1u<<13),0,modes[m],32,0};
        node=(ScriptNode){.current.script=code,.active=1,.flags=node_flags[n]};g_ScriptNodePool=&node;Script_RunQueue();
        if(memcmp(&actor,&after,sizeof(actor)) || node.current.script!=code+(result?5:0) || node.active!=(result?0:1) ||
           node.flags!=(words[1]|(result?0x10:0)) || g_ScriptNodePool)return 0;
    }
    g_CurrentEntity=old_actor;g_FieldActorListHead=old_head;g_PlayerEntity=old_player;g_GameStateFlags=old_flags;
    g_ScriptNodePool=old_node;g_ScriptCursor=old_cursor;memcpy(g_GameState.bank_rows[0],old_rows,sizeof(old_rows));
    if(hash!=UINT64_C(0x27a316c7f7829479)){fprintf(stderr,"Action wait hash %016llx\n",(unsigned long long)hash);return 0;}
    puts("Original action wait opcode 198: 2880 unhooked USA CPU states, action initialization, 16-bit mode narrowing, signed direction, wrap and dispatcher retry/completion passed");return 1;
}

static int original_typed_field_opcode(void)
{
    FieldActor *saved_list=g_FieldActorListHead,*saved_player=g_PlayerEntity,*saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    FieldActor actors[3]={0},player={0};FieldActorState states[3]={0};Combatant core={0};
    for(unsigned i=0;i<3;++i){actors[i].state=&states[i];actors[i].type_id=6;
        actors[i].sub_id=i?2:1;actors[i].next=i<2?&actors[i+1]:NULL;}
    actors[1].flags=0x10;player.state=(FieldActorState *)&core;g_PlayerEntity=&player;g_CurrentEntity=&player;
    if(g_ScriptQueueOpcodeHandlers[206]!=Entity_SetFieldByTypeId)return 0;
    for(unsigned mode=0;mode<5;++mode)for(int value=0;value<5;++value){
        memset(states,0,sizeof(states));FieldActorState expected[3]={0};
        int operands[]={mode==4?7:6,mode==3?9:2,0x150,value};
        int *args[]={operands,operands+1,operands+2,operands+3};
        g_FieldActorListHead=mode==1?actors+1:mode==2?NULL:actors;
        if(mode<2){expected[2].behavior_mode=value;
            expected[2].behavior_timer=value==1?400:value==2?70:value==3?20:0;}
        if(g_ScriptQueueOpcodeHandlers[206](args)!=1 || memcmp(states,expected,sizeof(states)))return 0;
        memset(states,0,sizeof(states));
        u32 program[]={206|(4u<<13),0,(u32)operands[0],(u32)operands[1],(u32)operands[2],(u32)value,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(memcmp(states,expected,sizeof(states)) || node.current.script!=program+8 ||
           !(node.flags&0x10) || g_ScriptNodePool)return 0;
    }
    int operands[]={0,99,0x104,1234};int *args[]={operands,operands+1,operands+2,operands+3};
    if(g_ScriptQueueOpcodeHandlers[206](args)!=1 || core.curHP!=1234)return 0;
    core.curHP=0;Combatant expected_core={0};expected_core.curHP=1234;
    u32 program[]={206|(4u<<13),0,0,99,0x104,1234,32,0};
    ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
    if(memcmp(&core,&expected_core,sizeof(core)) || node.current.script!=program+8 || g_ScriptNodePool)return 0;
    s16 saved_floor=g_RoomFloorY->y;g_PlayerEntity=NULL;
    operands[2]=255;operands[3]=-1111;
    if(g_ScriptQueueOpcodeHandlers[206](args)!=1 || g_RoomFloorY->y!=-1111)return 0;
    g_RoomFloorY->y=saved_floor;
    g_FieldActorListHead=saved_list;g_PlayerEntity=saved_player;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    puts("Original opcode 206: native actor selection, dead/missing/subtype filtering, command-byte truncation, behavior timers, player HP and real dispatcher passed");
    return 1;
}

static int original_collision_gate_opcodes(void)
{
    CollisionDatabase *saved_db=g_CollisionDb;struct CollisionPlane *saved_planes=g_CollisionPlaneTable;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    FieldActor actor={0},*saved_actor=g_CurrentEntity;g_CurrentEntity=&actor;
    CollisionDatabase db={0};_Alignas(16) u8 triangles[116];
    db.triangles.pointer=triangles+16;g_CollisionDb=&db;
    const unsigned ops[]={128,129};uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned op=0;op<2;++op)for(unsigned mode=0;mode<2;++mode){
        size_t stride=mode?sizeof(CollisionTriangleXYZ):sizeof(CollisionTriangleXZ);
        g_CollisionPlaneTable=mode?(struct CollisionPlane *)triangles:NULL;
        if(g_ScriptQueueOpcodeHandlers[ops[op]]!=(op?Script_ClearObjEntryFlag80:Script_SetObjEntryFlag80))return 0;
        for(int index=0;index<3;++index)for(unsigned kind=0;kind<256;++kind){
            memset(triangles,0xa5,sizeof(triangles));
            for(unsigned i=0;i<3;++i)triangles[16+i*stride]=kind;
            u8 before[sizeof(triangles)],expected[sizeof(triangles)];memcpy(before,triangles,sizeof(before));
            int *args[]={&index};u32 result=g_ScriptQueueOpcodeHandlers[ops[op]](args);
            for(unsigned b=0;b<4;++b)hash=(hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
            for(unsigned b=0;b<sizeof(triangles);++b)hash=(hash^triangles[b])*UINT64_C(1099511628211);
            memcpy(expected,before,sizeof(expected));expected[16+index*stride]=op?kind&0x7f:kind|0x80;
            if(result!=1 || memcmp(expected,triangles,sizeof(triangles)))return 0;
            memcpy(triangles,before,sizeof(triangles));u32 program[]={ops[op]|(1u<<13),0,(u32)index,32,0};
            ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
            if(memcmp(expected,triangles,sizeof(triangles)) || node.current.script!=program+5 ||
               !(node.flags&0x10) || g_ScriptNodePool)return 0;
        }
    }
    g_CollisionDb=saved_db;g_CollisionPlaneTable=saved_planes;g_CurrentEntity=saved_actor;
    g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0x12d5c65f0d2fc325))return 0;
    PolygonVertex *saved_wall=g_EntityCollisionWallSlot,*saved_floor_polygon=g_FloorPolygonVertices;
    s16 saved_count=g_EntityCollisionWallParam;u16 saved_floor_count=g_FloorPolygonVertexCount,saved_radius=g_CollisionRadius;
    FieldActor player={0},controller={0},*saved_player=g_PlayerEntity;_Alignas(8) u8 polygon_script[128]={0};
    const PolygonVertex square[]={{0,-100,0,-100},{0,100,0,-100},{0,100,0,100},{0,-100,0,100}};
    memcpy(polygon_script+64,square,sizeof(square));controller.script_base=polygon_script;
    g_CurrentEntity=&controller;g_PlayerEntity=&player;g_FloorPolygonVertices=(PolygonVertex *)polygon_script;g_FloorPolygonVertexCount=3;
    u32 setup[]={172|(2u<<13),0,32,0x10004,32,0};ScriptNode node={.current.script=setup,.active=1,.flags=0x80};
    g_ScriptNodePool=&node;Script_RunQueue();
    if(g_EntityCollisionWallSlot!=(PolygonVertex *)(polygon_script+64) || (u16)g_EntityCollisionWallParam!=4 ||
       g_FloorPolygonVertices!=(PolygonVertex *)polygon_script || g_FloorPolygonVertexCount!=3 ||
       node.current.script!=setup+6 || node.flags!=0x90 || g_ScriptNodePool)return 0;
    const int wall_points[]={-110,-105,-100,-95,-90,0,90,95,100,105,110},speeds[]={2048,4096};
    uint64_t response_hash=UINT64_C(14695981039346656037);
    for(unsigned speed=0;speed<2;++speed)for(unsigned x=0;x<11;++x)for(unsigned z=0;z<11;++z){
        memset(&player,0,sizeof(player));player.move_speed=speeds[speed];player.render_object.hit_cylinder.radius=10;
        player.pos_x=(s32)((u32)wall_points[x]<<16);player.pos_z=(s32)((u32)wall_points[z]<<16);
        Entity_ApplyCollisionResponse(NULL);
        const u32 words[]={(u32)player.pos_x,(u32)player.pos_y,(u32)player.pos_z,g_CollisionRadius};
        for(unsigned i=0;i<4;++i)for(unsigned b=0;b<(i==3?2:4);++b)
            response_hash=(response_hash^((words[i]>>(b*8))&255))*UINT64_C(1099511628211);
    }
    g_EntityCollisionWallSlot=saved_wall;g_EntityCollisionWallParam=saved_count;g_CollisionRadius=saved_radius;
    g_FloorPolygonVertices=saved_floor_polygon;g_FloorPolygonVertexCount=saved_floor_count;
    g_PlayerEntity=saved_player;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(response_hash!=UINT64_C(0x99967d7c2a10f007)){fprintf(stderr,"Wall response hash %016llx\n",(unsigned long long)response_hash);return 0;}
    puts("Original wall binding: opcode 172 shared native state, separate floor polygon and 242 USA CPU/GTE-bridge collision responses passed");
    puts("Original collision gate opcodes: 3072 USA MIPS states, both triangle layouts, all kind bits, neighbouring records and real dispatcher passed");
    return 1;
}

static int original_player_camera_reset_opcode(void)
{
    GeomScrollRecord saved=g_GeomScroll;
    GeomState *saved_geometry=g_GeomState;
    FieldActor player={0}, *saved_player=g_PlayerEntity, *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    struct { GeomState header; CameraViewport views[2]; } geometry={0};
    GteMatrix matrix={.m={{4096,0,0},{0,4096,0},{0,0,4096}},.t={0,0,4096}};
    geometry.header.entry_offset_1C=(u8 *)geometry.views-(u8 *)&geometry;
    geometry.views[1].width=640;geometry.views[1].height=480;
    geometry.views[1].minX=geometry.views[1].maxX=-123;
    geometry.views[1].minY=geometry.views[1].maxY=234;
    g_PlayerEntity=g_CurrentEntity=&player;g_GeomState=&geometry.header;
    player.pos_x=-123*65536;player.pos_y=45*65536;player.pos_z=1024*65536;
    if(g_ScriptQueueOpcodeHandlers[72]!=Script_ResetPlayerCameraPos)return 0;
    const u32 flags[]={0,1,0x38,0x40,0x48,0xffffffff};
    for(unsigned i=0;i<sizeof(flags)/sizeof(*flags);++i){
        memset(&g_GeomScroll,0xa5,sizeof(g_GeomScroll));
        g_GeomScroll.words.viewMatrix=&matrix;
        g_GeomScroll.state.flags=flags[i];g_GeomScroll.state.position.groupSelect=1;
        GeomScrollRecord before=g_GeomScroll,expected=before;
        if(flags[i]&64){
            expected.state.flags=(flags[i]&~7u)|3;
            expected.state.position.startX=(u16)before.state.position.x;
            expected.state.position.startY=(u16)before.state.position.y;
            expected.state.position.targetX=(u16)-123;expected.state.position.targetY=234;
            expected.state.position.duration=30;expected.state.position.elapsed=1;
        }
        if(Script_ResetPlayerCameraPos(NULL)!=1 || memcmp(&expected,&g_GeomScroll,sizeof(expected)))return 0;
        g_GeomScroll=before;
        u32 program[]={72,0,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};
        g_ScriptNodePool=&node;Script_RunQueue();
        if(memcmp(&expected,&g_GeomScroll,sizeof(expected)) || node.current.script!=program+4 ||
           !(node.flags&0x10) || g_ScriptNodePool)return 0;
    }
    g_GeomScroll=saved;g_GeomState=saved_geometry;g_PlayerEntity=saved_player;g_CurrentEntity=saved_actor;
    g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    puts("Original player camera reset: opcode 72, disabled camera, selected viewport bounds, default transition and dispatcher passed");
    return 1;
}

static int original_scroll_opcodes(void)
{
    const u32 values[]={0,1,32767,32768,65535,65536,0x80000000,0xffffffff,0x12345678};
    const u32 flags[]={0,1,0x3f,0x40,0x48,0x7f,0x80000040,0xffffffff};
    const u32 durations[]={0,1,30,65535,65536};const unsigned ops[]={3,70};
    GeomScrollRecord saved=g_GeomScroll;ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    FieldActor actor={0},*saved_actor=g_CurrentEntity;g_CurrentEntity=&actor;
    uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned op=0;op<2;++op)for(unsigned f=0;f<8;++f)for(unsigned i=0;i<9;++i)for(unsigned d=0;d<5;++d){
        if(g_ScriptQueueOpcodeHandlers[ops[op]]!=(op?Script_SetFogParamsAlt:Script_SetFogParams))return 0;
        memset(&g_GeomScroll,0xa5,sizeof(g_GeomScroll));g_GeomScroll.state.flags=(s32)flags[f];
        GeomScrollRecord before=g_GeomScroll;
        int data[]={(s32)values[i],(s32)values[(i+3)%9],(s32)durations[d]};int *args[]={data,data+1,data+2};
        u32 result=g_ScriptQueueOpcodeHandlers[ops[op]](args);u16 halves[12];
        memcpy(halves,&g_GeomScroll.state.position,sizeof(halves));
        const u32 words[]={result,(u32)g_GeomScroll.state.flags};
        for(unsigned j=0;j<14;++j){u32 value=j<2?words[j]:halves[j-2];
            for(unsigned b=0;b<4;++b)hash=(hash^((value>>(b*8))&255))*UINT64_C(1099511628211);}
        GeomScrollRecord expected=before;
        if(flags[f]&0x40){
            expected.state.flags=(flags[f]&~15u)|(op?9:1);
            expected.words.start=before.words.position;
            expected.state.position.targetX=(u16)data[0];expected.state.position.targetY=(u16)data[1];
            expected.state.position.elapsed=1;expected.state.position.duration=(u16)data[2];
        }
        if(result!=1 || memcmp(&g_GeomScroll,&expected,sizeof(expected)))return 0;
        g_GeomScroll=before;u32 program[]={ops[op]|(3u<<13),0,values[i],values[(i+3)%9],durations[d],32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(memcmp(&g_GeomScroll,&expected,sizeof(expected)) || node.current.script!=program+7 ||
           !(node.flags&0x10) || g_ScriptNodePool)return 0;
    }
    g_GeomScroll=saved;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;g_CurrentEntity=saved_actor;
    if(hash!=UINT64_C(0x93501ab9832625cd)){fprintf(stderr,"Scroll opcode hash %016llx\n",(unsigned long long)hash);return 0;}
    puts("Original scroll opcodes: 720 USA MIPS states, signed coordinates, unsigned duration, disabled camera and full dispatcher passed");
    return 1;
}

static int original_motion_opcodes(void)
{
    const u32 factors[]={0,1,65536,327680,0x80000000,0x7fffffff};
    const u16 speeds[]={0,1,4096,65535};
    const s16 angles[]={-32768,-1,0,1,1024,2048,3072,4095,32767};
    const u32 patterns[]={0,1,2,3,0x40,0x41,0x80,0x7fffffff,0x80000000,0xffffffff,0x12345678,0xfedcba98};
    FieldActor *saved_actor=g_CurrentEntity,*saved_player=g_PlayerEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    uint64_t velocity=UINT64_C(14695981039346656037),reset=velocity;
    const unsigned scalar_ops[]={77,93};
    int (*scalar_handlers[])(int **)={Script_SetEntityField1C,Script_SetCurrentEntityField20};
    for(unsigned op=0;op<2;++op){
        uint64_t factor_hash=velocity;
        if(g_ScriptQueueOpcodeHandlers[scalar_ops[op]]!=scalar_handlers[op])return 0;
        for(unsigned f=0;f<6;++f)for(unsigned p=0;p<12;++p){
            FieldActor actor={.anim_step=(s32)0x98765432,.move_factor=(s32)0x98765432,.flags=patterns[p]};
            FieldActor before=actor,expected=actor;
            g_CurrentEntity=&actor;int factor=(s32)factors[f];int *args[]={&factor};
            u32 result=scalar_handlers[op](args);
            const u32 data[]={result,(u32)(op?actor.move_factor:actor.anim_step),actor.flags};
            for(unsigned i=0;i<3;++i)for(unsigned b=0;b<4;++b)
                factor_hash=(factor_hash^((data[i]>>(b*8))&255))*UINT64_C(1099511628211);
            if(op)expected.move_factor=actor.move_factor;else expected.anim_step=actor.anim_step;
            if(memcmp(&expected,&actor,sizeof(actor)))return 0;
            actor=before;u32 program[]={scalar_ops[op]|(1u<<13),0,factors[f],32,0};
            ScriptNode node={.current.script=program,.active=1,.flags=0x80};
            g_ScriptNodePool=&node;Script_RunQueue();
            if(result!=1 || memcmp(&expected,&actor,sizeof(actor)) ||
               node.current.script!=program+5 || node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
        }
        if(factor_hash!=UINT64_C(0x3bf7cb6b9837996d))return 0;
    }
    if(g_ScriptQueueOpcodeHandlers[101]!=Entity_ClearMotionVectors ||
       g_ScriptQueueOpcodeHandlers[102]!=Entity_ComputeVelocity)return 0;
    for(unsigned player=0;player<2;++player)for(unsigned f=0;f<6;++f)
    for(unsigned speed=0;speed<4;++speed)for(unsigned angle=0;angle<9;++angle){
        FieldActor actor={.move_factor=(s32)factors[f],.move_speed=speeds[speed],.rot_y=angles[angle],
            .motion_x=0x1234,.motion_z=0x5678};
        FieldActor before=actor;g_CurrentEntity=&actor;g_PlayerEntity=player?&actor:NULL;
        u32 result=Entity_ComputeVelocity(NULL);const u32 data[]={result,(u32)actor.motion_x,(u32)actor.motion_z};
        for(unsigned i=0;i<3;++i)for(unsigned b=0;b<4;++b)
            velocity=(velocity^((data[i]>>(b*8))&255))*UINT64_C(1099511628211);
        FieldActor expected=before;expected.motion_x=actor.motion_x;expected.motion_z=actor.motion_z;
        if(memcmp(&expected,&actor,sizeof(actor)))return 0;
        actor=before;u32 program[]={102,0,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || memcmp(&expected,&actor,sizeof(actor)) || node.current.script!=program+4 ||
           !(node.flags&0x10) || g_ScriptNodePool)return 0;
    }
    for(unsigned p=0;p<12;++p)for(unsigned shift=0;shift<8;++shift){
        FieldActor actor={.flags=patterns[p]},expected;g_CurrentEntity=&actor;g_PlayerEntity=NULL;
        int *vectors[]={&actor.motion_x,&actor.motion_y,&actor.motion_z,&actor.accel_x,&actor.accel_y,&actor.accel_z};
        for(unsigned i=0;i<6;++i)*vectors[i]=(s32)patterns[(p+shift+i)%12];
        FieldActor before=actor;u32 result=Entity_ClearMotionVectors(NULL);
        const u32 data[]={result,(u32)actor.motion_x,(u32)actor.motion_y,(u32)actor.motion_z,
            (u32)actor.accel_x,(u32)actor.accel_y,(u32)actor.accel_z,actor.flags};
        for(unsigned i=0;i<8;++i)for(unsigned b=0;b<4;++b)
            reset=(reset^((data[i]>>(b*8))&255))*UINT64_C(1099511628211);
        expected=before;expected.motion_x=expected.motion_y=expected.motion_z=0;
        expected.accel_x=expected.accel_y=expected.accel_z=0;
        if(memcmp(&expected,&actor,sizeof(actor)))return 0;
        actor=before;u32 program[]={101,0,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || memcmp(&expected,&actor,sizeof(actor)) || node.current.script!=program+4 ||
           !(node.flags&0x10) || g_ScriptNodePool)return 0;
    }
    uint64_t flags_hash=UINT64_C(14695981039346656037);
    for(unsigned op=120;op<=121;++op)for(unsigned bit=0;bit<32;++bit)for(unsigned inverse=0;inverse<2;++inverse){
        FieldActor actor={.flags=(1u<<bit)^(inverse?UINT32_MAX:0)},before=actor,expected=actor;
        if(op==120)expected.flags&=~0x20u;else expected.flags|=0x20;
        g_CurrentEntity=&actor;
        if(g_ScriptQueueOpcodeHandlers[op]!=(op==120?Entity_ClearCurrentFlag20:Entity_SetCurrentFlag20))return 0;
        u32 result=g_ScriptQueueOpcodeHandlers[op](NULL);const u32 data[]={result,actor.flags};
        for(unsigned i=0;i<2;++i)for(unsigned b=0;b<4;++b)
            flags_hash=(flags_hash^((data[i]>>(b*8))&255))*UINT64_C(1099511628211);
        if(result!=1 || memcmp(&actor,&expected,sizeof(actor)))return 0;
        actor=before;u32 program[]={op,0,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(memcmp(&actor,&expected,sizeof(actor)) || node.current.script!=program+4 ||
           !(node.flags&0x10) || g_ScriptNodePool)return 0;
    }
    g_CurrentEntity=saved_actor;g_PlayerEntity=saved_player;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(flags_hash!=UINT64_C(0xf3302e2482610125))return 0;
    if(velocity!=UINT64_C(0xd151bdfd099e675a) || reset!=UINT64_C(0x137482f2419b4165)){
        fprintf(stderr,"Motion opcode hashes %016llx %016llx\n",(unsigned long long)velocity,(unsigned long long)reset);return 0;}
    puts("Original motion opcodes: 432 velocity, 96 reset and 128 actor flag20 USA MIPS states, native player scaling, wrap and full dispatcher passed");
    puts("Original animation step/movement factor opcodes 77/93: 144 USA CPU states, whole-actor guards and native dispatcher passed");
    return 1;
}

static int original_dialogue_opcodes(void)
{
    const unsigned ops[]={13,34,35,209};
    int (*handlers[])(int **)={Script_StopEntityAnim,Script_YieldWhileTableEntrySet,Script_ClearTableEntry,Script_ResetEntityAnimSlot};
    const u32 open_pages[]={0,1,32767,32768,65535,65536,0x80000000,0xffffffff,0x12345678};
    const u32 wait_pages[]={5,6,7,8,0xffffffff,0,65541,0x80000000,0x7fffffff};
    const u8 movements[]={0,1,255};
    const s16 pages[4][4]={{5,6,7,8},{5,5,5,5},{-1,5,-1,5},{-1,-1,-1,-1}};
    const u8 states[8][4]={{0,0,0,0},{1,2,3,4},{0,3,2,1},{128,255,127,1},
        {255,0,3,0},{0,0,255,0},{0,0,0,255},{2,2,2,2}};
    TextboxEntry saved[4];memcpy(saved,g_TextboxEntries,sizeof(saved));
    u8 saved_movement=g_PlayerMovementEnabled,saved_choice=g_DialogueChoiceCursor;
    s8 saved_menu=g_MenuEquipSlotIndex;FieldActor *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    u8 markers[4];FieldActor actor={0};g_CurrentEntity=&actor;
    TextboxRect saved_rect=g_TextboxPendingRect;
    uint64_t hashes[3]={UINT64_C(14695981039346656037),UINT64_C(14695981039346656037),UINT64_C(14695981039346656037)};
    for(unsigned op=0;op<4;++op)for(unsigned layout=0;layout<((op==1 || op==2)?4u:16u);++layout)
    for(unsigned middle=0;middle<((op==1 || op==2)?8u:9u);++middle)for(unsigned last=0;last<((op==1 || op==2)?9u:3u);++last){
        int waits=op==1 || op==2;unsigned hash_index=op==3?2:waits;
        if(g_ScriptQueueOpcodeHandlers[ops[op]]!=handlers[op])return 0;
        memset(g_TextboxEntries,0xa5,sizeof(saved));
        for(unsigned i=0;i<4;++i){
            g_TextboxEntries[i].message=markers+i;
            g_TextboxEntries[i].state=waits?states[middle][i]:(layout&(1u<<i))?3:0;
            if(waits)g_TextboxEntries[i].page_id=pages[layout][i];
        }
        g_DialogueChoiceCursor=0x12;g_MenuEquipSlotIndex=-7;
        if(!waits)g_PlayerMovementEnabled=movements[last];
        if(op==3)g_TextboxPendingRect=(TextboxRect){-8,movements[last],296,-24};
        TextboxEntry before[4];memcpy(before,g_TextboxEntries,sizeof(before));
        int value=(s32)(waits?wait_pages[last]:open_pages[middle]);int *args[]={&value};
        u32 storage[4]={0};ScriptNode node={.active=7};g_ScriptNodePool=&node;g_ScriptCursor=storage+3;
        u32 result=handlers[op](args);uint64_t hash=hashes[hash_index];
#define DIALOGUE_PUT(value,bytes) do {u32 put_=(u32)(value); \
    for(unsigned b_=0;b_<(bytes);++b_){hash=(hash^(u8)put_)*UINT64_C(1099511628211);put_>>=8;}}while(0)
        DIALOGUE_PUT(result,4);
        if(waits){DIALOGUE_PUT((uintptr_t)g_ScriptCursor-(uintptr_t)storage,4);DIALOGUE_PUT(node.active,4);}
        for(unsigned i=0;i<4;++i){
            TextboxEntry *entry=&g_TextboxEntries[i];
            if(entry->message!=markers+i)return 0;
            for(unsigned b=0;b<4;++b)DIALOGUE_PUT(((u8 *)entry)[b],1);
            DIALOGUE_PUT(0x80180000u+i*256,4);
            for(unsigned b=0;b<48;++b)DIALOGUE_PUT(((u8 *)&entry->style)[b],1);
        }
        if(!waits){DIALOGUE_PUT(g_DialogueChoiceCursor,1);DIALOGUE_PUT(g_MenuEquipSlotIndex,1);}
#undef DIALOGUE_PUT
        hashes[hash_index]=hash;
        TextboxEntry expected[4];memcpy(expected,g_TextboxEntries,sizeof(expected));
        u8 choice=g_DialogueChoiceCursor;s8 menu=g_MenuEquipSlotIndex;
        memcpy(g_TextboxEntries,before,sizeof(before));g_DialogueChoiceCursor=0x12;g_MenuEquipSlotIndex=-7;
        u32 program[]={ops[op]|(1u<<13),0,(u32)value,32,0};
        node=(ScriptNode){.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(g_ScriptNodePool || node.current.script!=(result?program+5:program) || node.active!=!result ||
           !!(node.flags&0x10)!=!!result || memcmp(expected,g_TextboxEntries,sizeof(expected)) ||
           choice!=g_DialogueChoiceCursor || menu!=g_MenuEquipSlotIndex)return 0;
    }
    /* USA 80017DE4 executes the real signed-byte menu getter, including
     * cancellation (-1); the opcode writes a complete script word. */
    if(g_ScriptQueueOpcodeHandlers[67]!=Script_GetMenuChoice)return 0;
    uint64_t choice_hash=UINT64_C(14695981039346656037);
    u32 saved_variable=g_ScriptVariableBank[9];
    for(unsigned choice=0;choice<256;++choice){
        g_MenuEquipSlotIndex=(s8)choice;
        int output=(s32)0x98765432;int *args[]={&output};
        u32 result=Script_GetMenuChoice(args);
        const u32 words[]={result,(u32)output,(u8)g_MenuEquipSlotIndex};
        for(unsigned i=0;i<3;++i)for(unsigned b=0;b<4;++b)
            choice_hash=(choice_hash^((words[i]>>(b*8))&255))*UINT64_C(1099511628211);
        u32 program[]={67|(1u<<13)|(2u<<17),0,9,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};
        g_ScriptVariableBank[9]=0x98765432;g_ScriptNodePool=&node;
        Script_RunQueue();
        if(g_ScriptVariableBank[9]!=(u32)output || node.current.script!=program+5 ||
           node.active || node.flags!=0x90 || g_ScriptNodePool ||
           (u8)g_MenuEquipSlotIndex!=choice)return 0;
    }
    g_ScriptVariableBank[9]=saved_variable;
    if(choice_hash!=UINT64_C(0xffdfd872a5725525))return 0;
    memcpy(g_TextboxEntries,saved,sizeof(saved));g_PlayerMovementEnabled=saved_movement;
    g_DialogueChoiceCursor=saved_choice;g_MenuEquipSlotIndex=saved_menu;g_CurrentEntity=saved_actor;
    g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    g_TextboxPendingRect=saved_rect;
    if(hashes[2]!=UINT64_C(0x114538d7aed539c5))return 0;
    puts("Original styled textbox opcode 209: 432 USA CPU states, signed page IDs, pending rectangles and dispatcher passed");
    if(hashes[0]!=UINT64_C(0x76d0cc627a0cae51) || hashes[1]!=UINT64_C(0x1506f2e400f8d88f)){
        fprintf(stderr,"Dialogue opcode hashes %016llx %016llx\n",(unsigned long long)hashes[0],(unsigned long long)hashes[1]);return 0;}
    puts("Original dialogue opcodes: 432 open and 576 wait/close USA MIPS states, native pointers, duplicate pages and full dispatcher passed");
    puts("Original menu choice opcode 67: all 256 signed-byte USA CPU states and native dispatcher outputs passed");
    return 1;
}

static int original_turn_to_actor_opcode(void)
{
    const s32 positions[][2]={{0,0},{65536,0},{0,-65536},{INT32_MIN,INT32_MAX}};
    const s16 angles[]={0,1,1023,1024,2047,2048,3072,4095,-1};
    const int speeds[]={0,1,64,4096,INT32_MAX};
    FieldActor *saved_actor=g_CurrentEntity,*saved_player=g_PlayerEntity,*saved_head=g_FieldActorListHead;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    uint64_t hash=UINT64_C(14695981039346656037);
    if(g_ScriptQueueOpcodeHandlers[75]!=Render_CameraSnapToEntity)return 0;
    for(unsigned kind=0;kind<5;++kind)for(unsigned p=0;p<4;++p)
    for(unsigned a=0;a<9;++a)for(unsigned speed=0;speed<5;++speed){
        FieldActor actor={0},target={.type_id=7,.sub_id=11},dead={.type_id=7,.sub_id=11,.flags=0x10};
        if(kind==4)dead.next=&target;
        g_CurrentEntity=&actor;g_PlayerEntity=kind==0?&target:NULL;
        g_FieldActorListHead=kind==2?&target:kind>=3?&dead:NULL;
        actor.pos_x=p==3?INT32_MAX:0;actor.pos_z=p==3?INT32_MIN:0;actor.rot_y=angles[a];
        target.pos_x=positions[p][0];target.pos_z=positions[p][1];
        u32 storage[8]={0};ScriptNode node={.flags=0x80,.active=7};
        g_ScriptNodePool=&node;g_ScriptCursor=storage+5;
        int data[]={kind<2?0:7,11,speeds[speed]};int *args[]={data,data+1,data+2};
        u32 result=Render_CameraSnapToEntity(args);
        const u32 output[]={result,(u16)actor.rot_y,node.flags,(u32)node.active,
            (u32)((uintptr_t)g_ScriptCursor-(uintptr_t)storage),(u32)node.target14,node.target18.actor!=NULL};
        for(unsigned i=0;i<7;++i)for(unsigned b=0;b<4;++b)
            hash=(hash^((output[i]>>(b*8))&255))*UINT64_C(1099511628211);
        if(node.target18.actor && node.target18.actor!=&target)return 0;
        u32 program[]={75|(3u<<13),0,(u32)data[0],11,(u32)data[2],32,0};
        node=(ScriptNode){.current.script=program,.active=1,.flags=0x80};actor.rot_y=angles[a];
        g_ScriptNodePool=&node;Script_RunQueue();
        if(g_ScriptNodePool || (u16)actor.rot_y!=output[1] || node.target14!=(s32)output[5] ||
           (node.target18.actor!=NULL)!=output[6] || (node.target18.actor && node.target18.actor!=&target) ||
           node.current.script!=(result?program+7:program) || node.active!=!result ||
           !!(node.flags&0x10)!=!!result)return 0;
    }
    g_CurrentEntity=saved_actor;g_PlayerEntity=saved_player;g_FieldActorListHead=saved_head;
    g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0x34c38eb49612044f)){fprintf(stderr,"Turn opcode hash %016llx\n",(unsigned long long)hash);return 0;}
    puts("Original turn-to-actor opcode: 900 USA MIPS states, live/missing/dead lookup, native target pointers and dispatcher passed");
    return 1;
}

static int original_screen_fade_wait_opcode(void)
{
    RenderTintState saved=g_ScreenTint;FieldActor *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    FieldActor actor={0};g_CurrentEntity=&actor;uint64_t hash=UINT64_C(14695981039346656037);
    if(g_ScriptQueueOpcodeHandlers[156]!=Entity_YieldOnLoading)return 0;
    for(unsigned fade=0;fade<256;++fade){
        u32 storage[4]={0};ScriptNode node={.active=7};
        g_ScriptNodePool=&node;g_ScriptCursor=storage+3;g_ScreenTint.fadeMode=fade;
        u32 result=Entity_YieldOnLoading(NULL);
        const u32 data[]={result,(u32)((uintptr_t)g_ScriptCursor-(uintptr_t)storage),(u32)node.active};
        for(unsigned i=0;i<3;++i)for(unsigned b=0;b<4;++b)
            hash=(hash^((data[i]>>(b*8))&255))*UINT64_C(1099511628211);
        u32 program[]={156,0,32,0};node=(ScriptNode){.current.script=program,.active=1,.flags=0x80};
        g_ScriptNodePool=&node;Script_RunQueue();
        int busy=(fade&3)>=2;
        if(g_ScriptNodePool || node.current.script!=(busy?program:program+4) ||
           node.active!=busy || !!(node.flags&0x10)==busy)return 0;
        RenderTintState expected=saved;expected.fadeMode=fade;
        if(memcmp(&g_ScreenTint,&expected,sizeof(expected)))return 0;
    }
    g_ScreenTint=saved;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0x3ae97f7ab9537f25))return 0;
    puts("Original fade wait opcode: all 256 USA MIPS modes, cursor rewinds and full dispatcher yield/completion passed");
    return 1;
}

static int original_screen_fade_opcodes(void)
{
    const unsigned ops[]={133,134,135};
    int (*handlers[])(int **)={Script_SetCdRomSeekPos,Script_SetFogColor,Script_SetCdRomScreenPos};
    const u32 values[]={0,1,3,4,65535,0x80000000,0xffffffff,0x12345678};
    RenderTintState saved=g_ScreenTint;FieldActor *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    FieldActor actor={0};g_CurrentEntity=&actor;uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned op=0;op<3;++op)for(unsigned seed=0;seed<8;++seed)for(unsigned v=0;v<8;++v){
        if(g_ScriptQueueOpcodeHandlers[ops[op]]!=handlers[op])return 0;
        g_ScreenTint=saved;
        for(unsigned i=0;i<20;++i)((u8 *)&g_ScreenTint.targetR)[i]=(u8)(i*17+seed*31);
        int data[5];int *args[5];for(unsigned i=0;i<5;++i){data[i]=(s32)values[(v+i)%8];args[i]=data+i;}
        u32 result=handlers[op](args);
        for(unsigned b=0;b<4;++b)hash=(hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
        for(unsigned i=0;i<20;++i)hash=(hash^((u8 *)&g_ScreenTint.targetR)[i])*UINT64_C(1099511628211);
        RenderTintState expected=g_ScreenTint;
        if(memcmp(&g_ScreenTint,&saved,PE1_OFFSETOF(RenderTintState,targetR)))return 0;
        g_ScreenTint=saved;
        for(unsigned i=0;i<20;++i)((u8 *)&g_ScreenTint.targetR)[i]=(u8)(i*17+seed*31);
        u32 program[]={ops[op]|(5u<<13),0,(u32)data[0],(u32)data[1],(u32)data[2],(u32)data[3],(u32)data[4],32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || node.current.script!=program+9 || !(node.flags&0x10) || g_ScriptNodePool ||
           memcmp(&expected,&g_ScreenTint,sizeof(expected)))return 0;
    }
    g_ScreenTint=saved;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0x0ddf13ae684d5bb5))return 0;
    puts("Original screen fade opcodes: 192 USA MIPS states, full tint state, native packets retained and dispatcher passed");
    return 1;
}

static int original_geometry_control_opcodes(void)
{
    const unsigned ops[]={116,117,123};
    int (*handlers[])(int **)={Script_SetObjEntryLimit,Script_SetFlagSlot,Script_SetObjEntryField8};
    const u32 values[]={0,1,255,256,65535,0x80000000,0xffffffff,0x12345678};
    const u8 flags[]={0,1,2,4,0x20,255};
    GeomState *saved_geom=g_GeomState;FieldActor *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    FieldActor actor={0};g_CurrentEntity=&actor;uint64_t hash=UINT64_C(14695981039346656037);
    struct {GeomState header;GeomCtrlEntry controls[2];} geometry={0};
    geometry.header.ctrl_offset=(u8 *)geometry.controls-(u8 *)&geometry.header;g_GeomState=&geometry.header;
    for(unsigned op=0;op<3;++op)for(unsigned index=0;index<2;++index)
    for(unsigned v=0;v<8;++v)for(unsigned f=0;f<6;++f){
        if(g_ScriptQueueOpcodeHandlers[ops[op]]!=handlers[op])return 0;
        memset(geometry.controls,0xa5,sizeof(geometry.controls));geometry.controls[index].head.b.flags=flags[f];
        int data[]={index,(s32)values[v]};int *args[]={data,data+1};u32 result=handlers[op](args);
        for(unsigned b=0;b<4;++b)hash=(hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
        for(unsigned i=0;i<sizeof(geometry.controls);++i)
            hash=(hash^((u8 *)geometry.controls)[i])*UINT64_C(1099511628211);
        GeomCtrlEntry expected[2];memcpy(expected,geometry.controls,sizeof(expected));
        memset(geometry.controls,0xa5,sizeof(geometry.controls));geometry.controls[index].head.b.flags=flags[f];
        u32 program[]={ops[op]|(2u<<13),0,index,values[v],32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || node.current.script!=program+6 || !(node.flags&0x10) || g_ScriptNodePool ||
           memcmp(expected,geometry.controls,sizeof(expected)))return 0;
    }
    /* Unhooked USA 8001994C/8001998C include their actual mesh helpers.
     * Native mesh records have wide packet pointers; hash only their original
     * representation and independently require the real pointers untouched. */
    const u32 motion_values[]={0,1,255,256,65535,65536,0x80000000,0x7fffffff,
        0xffffffff,0x12345678,0xffff00ff,0x80000100};
    const u32 enabled_values[]={0,1,0xffffffff};
    struct {GeomState header;GeomEntry entries[2];} meshes={0};
    meshes.header.entry_offset=(u8 *)meshes.entries-(u8 *)&meshes.header;
    u64 wide_markers[2]={0};uint64_t mesh_hash=UINT64_C(14695981039346656037);
    unsigned mesh_cases=0;
    for(unsigned op=0;op<2;++op)for(unsigned index=0;index<2;++index)
    for(unsigned flag=0;flag<256;++flag)for(unsigned enabled=0;enabled<3;++enabled)
    for(unsigned v=0;v<12;++v){
        const unsigned opcode=187+op;
        int (*handler)(int **)=op?Script_SetEntryMirrored:Script_SetEntryScrolled;
        if(g_ScriptQueueOpcodeHandlers[opcode]!=handler)return 0;
        memset(meshes.entries,0xa5,sizeof(meshes.entries));
        for(unsigned i=0;i<2;++i){
            meshes.entries[i].u30.prim=wide_markers;
            meshes.entries[i].u34.pagePackets=(void *)(wide_markers+1);
        }
        meshes.entries[index].head.b.flags=flag;g_GeomState=&meshes.header;
        GeomEntry before[2];memcpy(before,meshes.entries,sizeof(before));
        int data[]={index,(s32)enabled_values[enabled],(s32)motion_values[v],(s32)motion_values[(v+5)%12]};
        int *args[]={data,data+1,data+2,data+3};u32 result=handler(args);
        for(unsigned b=0;b<4;++b)mesh_hash=(mesh_hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
        for(unsigned i=0;i<2;++i){
            for(unsigned b=0;b<48;++b)mesh_hash=(mesh_hash^((u8 *)&meshes.entries[i])[b])*UINT64_C(1099511628211);
            for(unsigned b=0;b<8;++b)mesh_hash=(mesh_hash^0xa5)*UINT64_C(1099511628211);
        }
        GeomEntry expected[2];memcpy(expected,before,sizeof(expected));
        expected[index].head.b.flags=meshes.entries[index].head.b.flags;
        expected[index].speedX=meshes.entries[index].speedX;
        expected[index].speedY=meshes.entries[index].speedY;
        if(result!=1 || memcmp(expected,meshes.entries,sizeof(expected)))return 0;
        memcpy(meshes.entries,before,sizeof(before));
        u32 program[]={opcode|(4u<<13),0,index,enabled_values[enabled],motion_values[v],motion_values[(v+5)%12],32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(node.current.script!=program+8 || node.active || node.flags!=0x90 ||
           g_ScriptNodePool || memcmp(expected,meshes.entries,sizeof(expected)))return 0;
        ++mesh_cases;
    }
    if(mesh_cases!=36864 || mesh_hash!=UINT64_C(0x334ad29225047b25)){
        fprintf(stderr,"Geometry mesh commands hash %016llx\n",(unsigned long long)mesh_hash);return 0;}
    uint64_t frame_hash=UINT64_C(14695981039346656037);unsigned frame_cases=0;
    if(g_ScriptQueueOpcodeHandlers[122]!=Script_GetObjEntryField6)return 0;
    for(unsigned index=0;index<2;++index)for(unsigned v=0;v<12;++v)for(unsigned target=0;target<5;++target){
        g_GeomState=&geometry.header;memset(geometry.controls,0xa5,sizeof(geometry.controls));
        geometry.controls[index].position.parts.frame=(s16)motion_values[v];
        int data[]={index,(s32)0x98765432};
        int *destinations[]={data+1,data,(int *)&geometry.controls[0].head.packed,
            (int *)&geometry.controls[0].position.packed,(int *)&geometry.controls[1].position.packed};
        int *args[]={data,destinations[target]};u32 result=Script_GetObjEntryField6(args);
        for(unsigned b=0;b<4;++b)frame_hash=(frame_hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
        for(unsigned b=0;b<sizeof(geometry.controls);++b)
            frame_hash=(frame_hash^((u8 *)geometry.controls)[b])*UINT64_C(1099511628211);
        for(unsigned b=0;b<sizeof(data);++b)frame_hash=(frame_hash^((u8 *)data)[b])*UINT64_C(1099511628211);
        ++frame_cases;
    }
    if(frame_cases!=120 || frame_hash!=UINT64_C(0xeac2f716824c248d)){
        fprintf(stderr,"Geometry frame getter hash %016llx\n",(unsigned long long)frame_hash);return 0;}
    u32 saved_operands[2]={g_ScriptOperandTable[0],g_ScriptOperandTable[1]};
    for(unsigned index=0;index<2;++index)for(unsigned v=0;v<12;++v)for(unsigned alias=0;alias<2;++alias){
        memset(geometry.controls,0xa5,sizeof(geometry.controls));
        geometry.controls[index].position.parts.frame=(s16)motion_values[v];
        g_ScriptOperandTable[0]=index;g_ScriptOperandTable[1]=0x98765432;
        u32 program[]={122|(2u<<13)|(3u<<17)|(3u<<20),0,0,alias?0u:1u,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(g_ScriptOperandTable[alias?0:1]!=(u32)(s32)(s16)motion_values[v] ||
           node.current.script!=program+6 || node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
    }
    g_ScriptOperandTable[0]=saved_operands[0];g_ScriptOperandTable[1]=saved_operands[1];
    puts("Original geometry commands 122/187/188: 36984 unhooked USA CPU cases, signed frame aliases, wide mesh-pointer guards and native dispatcher passed");
    g_GeomState=saved_geom;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0x18383904d1c6e665))return 0;
    puts("Original geometry control opcodes: 288 USA MIPS states, untouched adjacent controls and full dispatcher passed");
    return 1;
}

static int original_geometry_slot_opcodes(void)
{
    const unsigned ops[]={124,125,126,127};
    int (*handlers[])(int **)={Script_FillObjEntrySlotValues,Script_SetObjEntrySlotValue,Script_SetObjEntryFlags,Script_ClearObjEntryFlags};
    const u32 values[]={0,1,255,256,65535,0x80000000,0xffffffff,0x12345678};
    const u8 flags[]={0,1,2,4,0x20,255};const unsigned counts[]={0,1,4};
    GeomState *saved_geom=g_GeomState;FieldActor *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    FieldActor actor={0};g_CurrentEntity=&actor;uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    struct {GeomState header;u8 padding[512-sizeof(GeomState)];GeomCtrlEntry controls[2];GeomAnimationSlot slots[2][4];u64 guard;} geometry={0};
    geometry.header.ctrl_offset=512;g_GeomState=&geometry.header;
    for(unsigned op=0;op<4;++op)for(unsigned index=0;index<2;++index)for(unsigned count=0;count<3;++count)
    for(unsigned value=0;value<8;++value)for(unsigned flag=0;flag<6;++flag)
    for(unsigned slot=0;slot<(op==1?4:1);++slot){
        if(g_ScriptQueueOpcodeHandlers[ops[op]]!=handlers[op])return 0;
        memset(geometry.controls,0xa5,48);geometry.controls[0].slot_offset=32;geometry.controls[1].slot_offset=24;
        geometry.controls[index].head.packed=flags[flag]|(counts[count]<<8);geometry.guard=UINT64_C(0xa55aa55aa55aa55a);
        u8 before[48],expected[48];memcpy(before,geometry.controls,48);
        int data[]={index,op==1?(int)slot:(s32)values[value],(s32)values[value]};int *args[]={data,data+1,data+2};
        u32 result=handlers[op](args);
        for(unsigned b=0;b<4;++b)hash=(hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
        for(unsigned b=0;b<48;++b)hash=(hash^((u8 *)geometry.controls)[b])*UINT64_C(1099511628211);
        if(geometry.guard!=UINT64_C(0xa55aa55aa55aa55a) || geometry.header.ctrl_offset!=512)return 0;
        memcpy(expected,geometry.controls,48);memcpy(geometry.controls,before,48);
        unsigned n=op==1?3:2;u32 program[7]={ops[op]|(n<<13),0};
        for(unsigned i=0;i<n;++i)program[2+i]=(u32)data[i];program[2+n]=32;
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || node.current.script!=program+4+n || node.flags!=0x90 || node.active || g_ScriptNodePool ||
           memcmp(expected,geometry.controls,48) || geometry.guard!=UINT64_C(0xa55aa55aa55aa55a))return 0;
        ++cases;
    }
    g_GeomState=saved_geom;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(cases!=2016 || hash!=UINT64_C(0x9415818042796d7d)){
        fprintf(stderr,"Geometry slots CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original geometry124-127: 2016 unhooked USA CPU states, complete/per-slot duration updates, zero counts, low-byte narrowing, original OR/complement flags and dispatcher guards passed");
    return 1;
}

static int original_shadow_radius_opcode(void)
{
    const s16 radii[]={-32768,-32767,-1,0,1,123,32766,32767};
    const u32 values[]={0,1,0xffffffff,32767,0xffff8000,65535,65536,0x7fffffff,0x80000000,0x12345678,0xfedcba98};
    FieldActor *saved_actor=g_CurrentEntity;ScriptNode *saved_node=g_ScriptNodePool;
    u32 *saved_cursor=g_ScriptCursor;uint64_t hash=UINT64_C(14695981039346656037);
    if(g_ScriptQueueOpcodeHandlers[157]!=Script_ScaleAnimValue)return 0;
    for(unsigned r=0;r<8;++r)for(unsigned v=0;v<11;++v){
        RenderObjectHeader header={0},expected={0};header.shadow_radius=0x7654;
        FieldActor actor={0};actor.render_object.header=&header;
        actor.render_object.hit_cylinder.radius=radii[r];g_CurrentEntity=&actor;
        int value=(s32)values[v];int *args[]={&value};u32 result=g_ScriptQueueOpcodeHandlers[157](args);
        for(unsigned b=0;b<4;++b)hash=(hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
        u16 radius=header.shadow_radius;
        for(unsigned b=0;b<2;++b)hash=(hash^((radius>>(b*8))&255))*UINT64_C(1099511628211);
        expected.shadow_radius=radius;
        if(memcmp(&header,&expected,sizeof(header)))return 0;
        u32 program[]={157|(1u<<13),0,values[v],32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};
        header.shadow_radius=0x7654;g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || memcmp(&header,&expected,sizeof(header)) || node.current.script!=program+5 ||
           !(node.flags&0x10) || g_ScriptNodePool)return 0;
    }
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0x0cb313b7d38c9570))return 0;
    puts("Original shadow-radius opcode: 88 USA MIPS signed/overflow states and native dispatcher completion passed");
    return 1;
}

static int original_frame_clock_layout(void)
{
    const u32 values[]={0,1,0xffffffff,59,60,3599,3600,216000,0x80000000,0x7fffffff,0x8000ffff,0x12345678};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    for(unsigned seed=0;seed<12;++seed)for(unsigned pattern=0;pattern<12;++pattern)for(unsigned alias=0;alias<3;++alias){
        u32 input[]={0xa55a5aa5,values[seed],0xdeadbeef},output=values[pattern];
        GameTime_PackTime(input,alias?input+alias-1:&output);
        for(unsigned i=0;i<sizeof(input);++i)hash=(hash^((u8 *)input)[i])*UINT64_C(1099511628211);
        for(unsigned i=0;i<4;++i)hash=(hash^((output>>(i*8))&255))*UINT64_C(1099511628211);
        if(input[2]!=0xdeadbeef)return 0;
        ++cases;
    }
    if(cases!=432 || hash!=UINT64_C(0x82cfc71941e10949)){
        fprintf(stderr,"Packed clock CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    GameTimerEntry saved[4];memcpy(saved,g_GameTimers,sizeof(saved));
    void (*init[])(void)={GameTime_InitCounter0,GameTime_InitCounter1,GameTime_InitCounter2};
    hash=UINT64_C(14695981039346656037);cases=0;
    for(unsigned op=0;op<3;++op)for(unsigned seed=0;seed<12;++seed){
        u32 raw[12];for(unsigned i=0;i<12;++i)raw[i]=values[(seed+i)%12]^(i*0x01020304u);
        memcpy(g_GameTimers,raw,sizeof(raw));init[op]();
        for(unsigned i=0;i<sizeof(raw);++i)hash=(hash^((u8 *)g_GameTimers)[i])*UINT64_C(1099511628211);
        ++cases;
    }
    memcpy(g_GameTimers,saved,sizeof(saved));
    if(cases!=36 || hash!=UINT64_C(0xc9281cb7a579fbb4)){
        fprintf(stderr,"Clock init CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    hash=UINT64_C(14695981039346656037);
    for(unsigned seed=0;seed<12;++seed){
        u32 raw[12];for(unsigned i=0;i<12;++i)raw[i]=values[(seed+i)%12]^(i*0x01020304u);
        memcpy(g_GameTimers,raw,sizeof(raw));GameTime_InitCounters();
        for(unsigned i=0;i<sizeof(raw);++i)hash=(hash^((u8 *)g_GameTimers)[i])*UINT64_C(1099511628211);
    }
    memcpy(g_GameTimers,saved,sizeof(saved));
    if(hash!=UINT64_C(0x28838f78c52e3a0c)){
        fprintf(stderr,"Combined clock init CPU hash %016llx\n",(unsigned long long)hash);return 0;}
    puts("Original USA 80036DC8 combined timer initialization: 12 full-bank CPU states preserve the script timer and initialize all three running clocks independently of SDK sound initialization");
    puts("Original clock module: 432 unhooked USA packed-word/source aliases and 36 initializers preserve fixed 20/6/6-bit format and neighbouring clocks without layout-dependent bitfields or forced redundant stores");
    return 1;
}

static int original_script_timer_commands(void)
{
    const u32 values[]={0,1,0xffffffff,59,60,3599,3600,216000,0x80000000,0x7fffffff,0x8000ffff,0x12345678};
    const u32 flags[]={0,1,2,3,4,5,0x80000000,0xffffffff};
    const u32 modes[]={0,1,0xffffffff,2,0x10001,0x80000000};
    const u32 upper[]={0,0x10000,0x12345600,0x7fffff00,0x80000000,0xffffff00};
    GameTimerEntry saved[4];memcpy(saved,g_GameTimers,sizeof(saved));
    FieldActor *saved_actor=g_CurrentEntity;ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[103]!=Script_ComputeAudioTimer || g_ScriptQueueOpcodeHandlers[104]!=Script_GetCombatModeFlag ||
       g_ScriptQueueOpcodeHandlers[105]!=Script_SetCombatModeFlag || g_ScriptQueueOpcodeHandlers[211]!=GameTime_ConvertSecondsToHMS)return 0;
    for(unsigned seed=0;seed<12;++seed)for(unsigned f=0;f<8;++f)for(unsigned m=0;m<6;++m)for(unsigned alias=0;alias<7;++alias){
        u32 raw[12];for(unsigned i=0;i<12;++i)raw[i]=values[(seed+i)%12]^(i*0x01020304u);raw[9]=flags[f];
        memcpy(g_GameTimers,raw,sizeof(raw));int input[]={(s32)values[seed],(s32)values[(seed+3)%12],(s32)values[(seed+7)%12],(s32)modes[m]};
        int *args[]={input,input+1,input+2,input+3};
        if(alias==1)args[3]=&g_GameTimers[3].current;
        if(alias==2)args[0]=&g_GameTimers[3].current;
        if(alias==3)args[1]=(int *)&g_GameTimers[3].flags;
        if(alias==4)args[2]=&g_GameTimers[3].limit;
        if(alias==5)args[3]=(int *)&g_GameTimers[3].flags;
        if(alias==6)args[1]=args[0];
        u32 result=Script_ComputeAudioTimer(args);
        for(unsigned b=0;b<4;++b)hash=(hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
        for(unsigned b=0;b<sizeof(raw);++b)hash=(hash^((u8 *)g_GameTimers)[b])*UINT64_C(1099511628211);
        for(unsigned b=0;b<sizeof(input);++b)hash=(hash^((u8 *)input)[b])*UINT64_C(1099511628211);
        if(result!=1 || memcmp(raw,g_GameTimers,9*sizeof(u32)))return 0;
        if(!alias){
            GameTimerEntry expected[4];memcpy(expected,g_GameTimers,sizeof(expected));memcpy(g_GameTimers,raw,sizeof(raw));
            FieldActor actor={.flags=0x12345678};u32 program[]={103|(4u<<13),0,values[seed],values[(seed+3)%12],values[(seed+7)%12],modes[m],32,0};
            ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_CurrentEntity=&actor;g_ScriptNodePool=&node;Script_RunQueue();
            if(memcmp(expected,g_GameTimers,sizeof(expected)) || actor.flags!=0x12345678 ||
               node.current.script!=program+8 || node.flags!=0x90 || node.active || g_ScriptNodePool)return 0;
        }
        ++cases;
    }
    if(cases!=4032 || hash!=UINT64_C(0x97565315c9ec268a)){
        fprintf(stderr,"Timer103 CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    hash=UINT64_C(14695981039346656037);cases=0;
    for(unsigned high=0;high<6;++high)for(unsigned low=0;low<256;++low)for(unsigned target=0;target<13;++target){
        u32 raw[12];for(unsigned i=0;i<12;++i)raw[i]=values[i]^(i*0x01020304u);raw[9]=upper[high]|low;
        memcpy(g_GameTimers,raw,sizeof(raw));int output=(s32)0x98765432;int *destinations[13]={&output};
        for(unsigned i=0;i<4;++i){destinations[1+i*3]=(int *)&g_GameTimers[i].flags;destinations[2+i*3]=&g_GameTimers[i].current;destinations[3+i*3]=&g_GameTimers[i].limit;}
        int *args[]={destinations[target]};u32 a=Script_GetCombatModeFlag(args),first=(u32)*args[0];
        GameTimerEntry one[4],two[4];memcpy(one,g_GameTimers,sizeof(one));u32 b=Script_SetCombatModeFlag(NULL);memcpy(two,g_GameTimers,sizeof(two));
        u32 c=Script_GetCombatModeFlag(args),words[]={a,first,b,c,(u32)*args[0]};
        for(unsigned i=0;i<5;++i)for(unsigned byte=0;byte<4;++byte)hash=(hash^((words[i]>>(byte*8))&255))*UINT64_C(1099511628211);
        for(unsigned i=0;i<sizeof(one);++i)hash=(hash^((u8 *)one)[i])*UINT64_C(1099511628211);
        for(unsigned i=0;i<sizeof(two);++i)hash=(hash^((u8 *)two)[i])*UINT64_C(1099511628211);
        for(unsigned i=0;i<sizeof(raw);++i)hash=(hash^((u8 *)g_GameTimers)[i])*UINT64_C(1099511628211);
        for(unsigned i=0;i<4;++i)hash=(hash^(((u32)output>>(i*8))&255))*UINT64_C(1099511628211);
        if(a!=1 || b!=1 || c!=1)return 0;
        if(!target){
            memcpy(g_GameTimers,raw,sizeof(raw));FieldActor actor={0};u32 program[]={104|(1u<<13),0,0xa55a5a5a,105,0,104|(1u<<13),0,0x5aa5a5a5,32,0};
            ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_CurrentEntity=&actor;g_ScriptNodePool=&node;Script_RunQueue();raw[9]|=4;
            if(program[2]!=((upper[high]|low)&4?1u:0u) || program[7]!=1 || memcmp(raw,g_GameTimers,sizeof(raw)) ||
               node.current.script!=program+10 || node.flags!=0x90 || node.active || g_ScriptNodePool)return 0;
        }
        ++cases;
    }
    if(cases!=19968 || hash!=UINT64_C(0x6823bca859fc7725)){
        fprintf(stderr,"Timer104/105 CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    hash=UINT64_C(14695981039346656037);cases=0;
    for(unsigned seed=0;seed<12;++seed)for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b)for(unsigned c=0;c<4;++c){
        int input[]={(s32)values[seed],0x12345678,(s32)0x87654321,(s32)0xa5a55a5a};int *args[]={input,input+a,input+b,input+c};
        GameTimerEntry before[4];memcpy(before,g_GameTimers,sizeof(before));u32 result=GameTime_ConvertSecondsToHMS(args);
        for(unsigned i=0;i<4;++i)hash=(hash^((result>>(i*8))&255))*UINT64_C(1099511628211);
        for(unsigned i=0;i<sizeof(input);++i)hash=(hash^((u8 *)input)[i])*UINT64_C(1099511628211);
        FieldActor actor={0};u32 initial[]={values[seed],0x12345678,0x87654321,0xa5a55a5a,0xdeadbeef};memcpy(actor.pad_0AC,initial,sizeof(initial));
        u32 program[]={211|(4u<<13)|(1u<<17)|(1u<<20)|(1u<<23)|(1u<<26),0,0,a,b,c,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_CurrentEntity=&actor;g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || memcmp(input,actor.pad_0AC,sizeof(input)) || actor.pad_0AC[4]!=0xdeadbeef || memcmp(before,g_GameTimers,sizeof(before)) ||
           node.current.script!=program+8 || node.flags!=0x90 || node.active || g_ScriptNodePool)return 0;
        ++cases;
    }
    memcpy(g_GameTimers,saved,sizeof(saved));g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(cases!=768 || hash!=UINT64_C(0x2e9b02aa3f9f9c80)){
        fprintf(stderr,"Timer211 CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original timer opcodes103/104/105/211: 4032 arming states, 19968 flag/query sequences, 768 signed HMS/output aliases, retained neighbours and native dispatcher programs match unhooked USA CPU");
    return 1;
}

static int original_scene_frame_timers(void)
{
    const u32 values[]={0,1,0xffffffff,59,60,3599,3600,216000,0x80000000,0x7fffffff,0x8000ffff,0x12345678};
    const u32 upper[]={0,0x10000,0x12345600,0x7fffff00,0x80000000,0xffffff00};
    const u32 gates[]={0,1,0x40,0x80000000};
    const u16 video[][3]={{0,1,0},{1,0,0},{1,1,0},{1,1,1},{1,32767,0},{1,32768,0},{128,65535,1},{0,32767,255}};
    GameTimerEntry saved[4];memcpy(saved,g_GameTimers,sizeof(saved));u32 saved_flags=g_GameStateFlags;Pe1SceneAudioState saved_audio=g_GameState.scene_audio;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    for(unsigned seed=0;seed<12;++seed)for(unsigned flag=0;flag<256;++flag)for(unsigned gate=0;gate<4;++gate)for(unsigned v=0;v<8;++v){
        u32 raw[12];for(unsigned i=0;i<4;++i){raw[i*3]=upper[seed%6]|((flag+i*0x33)&255);raw[i*3+1]=values[(seed+i)%12];raw[i*3+2]=values[(seed+i+4)%12];}
        memcpy(g_GameTimers,raw,sizeof(raw));g_GameStateFlags=gates[gate];g_DrawEnabled.bits=(u8)video[v][0];g_SeqElapsed=video[v][1];g_GameState.scene_audio.tracks.movie_wide=(s8)video[v][2];
        Pe1SceneAudioState before=g_GameState.scene_audio;Scene_TickTimers();
        for(unsigned i=0;i<sizeof(raw);++i)hash=(hash^((u8 *)g_GameTimers)[i])*UINT64_C(1099511628211);
        if(g_GameStateFlags!=gates[gate] || memcmp(&before,&g_GameState.scene_audio,sizeof(before)))return 0;
        ++cases;
    }
    memcpy(g_GameTimers,saved,sizeof(saved));g_GameStateFlags=saved_flags;g_GameState.scene_audio=saved_audio;
    if(cases!=98304 || hash!=UINT64_C(0x250be6425a7b2025)){
        fprintf(stderr,"Frame timers CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original frame timers: 98304 unhooked USA CPU states, all flag bytes, four clocks, signed wrap, pause and real movie guards preserve original unbounded ticking");
    return 1;
}

static int original_camera_release_opcode(void)
{
    const u32 upper[]={0,0x10000,0x12345600,0x7fffff00,0x80000000,0xffffff00};
    GeomScrollRecord saved=g_GeomScroll;FieldActor *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[73]!=Script_SetBattleStateMode)return 0;
    for(unsigned i=0;i<6;++i)for(unsigned low=0;low<256;++low){
        memset(&g_GeomScroll,0xa5,sizeof(g_GeomScroll));g_GeomScroll.state.flags=(s32)(upper[i]|low);
        GeomScrollRecord before=g_GeomScroll,expected=before;expected.state.flags=(s32)(((upper[i]|low)&~7u)|0x80);
        u32 result=Script_SetBattleStateMode(NULL),words[]={result,(u32)g_GeomScroll.state.flags};
        for(unsigned w=0;w<2;++w)for(unsigned b=0;b<4;++b)hash=(hash^((words[w]>>(b*8))&255))*UINT64_C(1099511628211);
        if(result!=1 || memcmp(&g_GeomScroll,&expected,sizeof(expected)))return 0;
        g_GeomScroll=before;FieldActor actor={.flags=0x12345678};u32 program[]={73,0,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_CurrentEntity=&actor;g_ScriptNodePool=&node;Script_RunQueue();
        if(memcmp(&g_GeomScroll,&expected,sizeof(expected)) || actor.flags!=0x12345678 ||
           node.current.script!=program+4 || node.flags!=0x90 || node.active || g_ScriptNodePool)return 0;
        ++cases;
    }
    g_GeomScroll=saved;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(cases!=1536 || hash!=UINT64_C(0x9726c7e9d2657525)){
        fprintf(stderr,"Camera73 CPU hash %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    puts("Original opcode73: 1536 unhooked USA CPU states, all low flag bytes and signed/high patterns, retained camera state and real zero-operand dispatcher passed");
    return 1;
}

static int original_camera_flags_opcodes(void)
{
    const u32 patterns[]={0,1,2,3,0x40,0x41,0x80,0x7fffffff,0x80000000,0xffffffff,0x12345678,0xfedcba98};
    const unsigned ops[]={91,92,132};
    int (*handlers[])(int **)={Script_ClearBattleFlags,Script_SetBattleFlags,Script_SetScreenScrollPos};
    GeomScrollRecord saved=g_GeomScroll;FieldActor *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    uint64_t hash=UINT64_C(14695981039346656037);FieldActor actor={0};g_CurrentEntity=&actor;
    for(unsigned op=0;op<3;++op)for(unsigned x=0;x<12;++x)for(unsigned y=0;y<12;++y){
        if(g_ScriptQueueOpcodeHandlers[ops[op]]!=handlers[op])return 0;
        g_GeomScroll.state.flags=(s32)patterns[x];
        g_GeomScroll.state.position.baseAngleDigital=0x1234;
        g_GeomScroll.state.position.baseAngleAnalog=(s16)0xfedc;
        int data[]={(s32)patterns[x],(s32)patterns[y]};int *args[]={data,data+1};
        u32 result=handlers[op](args);
        u32 words[]={result,(u32)g_GeomScroll.state.flags};
        for(unsigned i=0;i<2;++i)for(unsigned b=0;b<4;++b)
            hash=(hash^((words[i]>>(b*8))&255))*UINT64_C(1099511628211);
        const u16 angles[]={(u16)g_GeomScroll.state.position.baseAngleDigital,(u16)g_GeomScroll.state.position.baseAngleAnalog};
        for(unsigned i=0;i<2;++i)for(unsigned b=0;b<2;++b)
            hash=(hash^((angles[i]>>(b*8))&255))*UINT64_C(1099511628211);
        u32 program[]={ops[op]|(2u<<13),0,patterns[x],patterns[y],32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};
        g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || node.current.script!=program+6 || !(node.flags&0x10) || g_ScriptNodePool)return 0;
    }
    g_GeomScroll=saved;g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0xc4b5a5fd1165f305)){fprintf(stderr,"Camera flags opcode hash %016llx\n",(unsigned long long)hash);return 0;}
    puts("Original camera/scroll flag opcodes: 432 USA MIPS states and native dispatcher completion passed");
    return 1;
}

static int original_animation_frame_opcode(void)
{
    const u32 values[]={0,1,255,256,65535,65536,0x80000000,0xffffffff,0x12345678};
    FieldActor *saved_actor=g_CurrentEntity;ScriptNode *saved_node=g_ScriptNodePool;
    u32 *saved_cursor=g_ScriptCursor;uint64_t hash=UINT64_C(14695981039346656037);
    if(g_ScriptQueueOpcodeHandlers[78]!=Script_SetEntityAnimSpeed)return 0;
    for(unsigned action=0;action<256;++action)for(unsigned v=0;v<9;++v){
        FieldActor actor={.action=action};actor.anim.fixed=0x12345678;g_CurrentEntity=&actor;
        int value=(s32)values[v];int *args[]={&value};u32 result=g_ScriptQueueOpcodeHandlers[78](args);
        u32 data[]={result,(u32)actor.anim.fixed};
        for(unsigned i=0;i<2;++i)for(unsigned b=0;b<4;++b)
            hash=(hash^((data[i]>>(b*8))&255))*UINT64_C(1099511628211);
        u32 program[]={78|(1u<<13),0,values[v],32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};
        actor.anim.fixed=0x12345678;g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || (u32)actor.anim.fixed!=data[1] || node.current.script!=program+5 ||
           !(node.flags&0x10) || g_ScriptNodePool)return 0;
    }
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0xfb78ff1167f295fc))return 0;
    puts("Original animation-frame opcode: 2304 USA MIPS states and native dispatcher completion passed");
    return 1;
}

static int original_attachment_opcode(void)
{
    const int animations[]={0,1,-1,32768,65536,0x12345678};const u8 parts[]={0,1,2,3,255};
    FieldActor *saved_current=g_CurrentEntity,*saved_player=g_PlayerEntity,*saved_head=g_FieldActorListHead;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[197]!=Entity_CopyAttachmentData)return 0;
    for(unsigned selector=0;selector<8;++selector)for(unsigned pi=0;pi<5;++pi)for(unsigned ai=0;ai<6;++ai){
        FieldActor actor={0},nodes[3]={{.type_id=7,.sub_id=11,.flags=0x10},{.type_id=7,.sub_id=12},{.type_id=7,.sub_id=11}};
        RenderObjectHeader header={.part_count=parts[pi]};
        nodes[0].next=nodes+1;nodes[1].next=nodes+2;
        actor.parent=nodes;actor.render_object.header=&header;actor.render_object.animation_source=&nodes[0].render_object;
        actor.render_object.animation_state=0x1234;actor.render_object.animation_id=0x5678;
        FieldActor expected=actor;g_CurrentEntity=&actor;g_PlayerEntity=selector==1?NULL:nodes+2;g_FieldActorListHead=selector==2?NULL:nodes;
        int type=selector<2?0:selector<5?7:selector==5?8:selector==6?263:-1,sub=selector==4?12:11,animation=animations[ai];int *args[]={&type,&sub,&animation};
        u32 result=g_ScriptQueueOpcodeHandlers[197](args);unsigned parent=0,source=0;
        for(unsigned i=0;i<3;++i){if(actor.parent==nodes+i)parent=i+1;if(actor.render_object.animation_source==&nodes[i].render_object)source=i+1;}
        u32 words[]={result,parent,source,(u16)actor.render_object.animation_state,(u16)actor.render_object.animation_id};
        for(unsigned i=0;i<sizeof(words)/sizeof(words[0]);++i)for(unsigned byte=0;byte<4;++byte)
            hash=(hash^((words[i]>>(byte*8))&255))*UINT64_C(1099511628211);
        expected.parent=actor.parent;expected.render_object.animation_source=actor.render_object.animation_source;
        expected.render_object.animation_state=actor.render_object.animation_state;expected.render_object.animation_id=actor.render_object.animation_id;
        if(memcmp(&actor,&expected,sizeof(actor)))return 0;
        u32 program[]={197|(3u<<13),0,(u32)type,(u32)sub,(u32)animation,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;
        Script_RunQueue();
        if(memcmp(&actor,&expected,sizeof(actor)) || node.current.script!=program+7 || node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
        ++cases;
    }
    g_CurrentEntity=saved_current;g_PlayerEntity=saved_player;g_FieldActorListHead=saved_head;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0x4b22e715ef4e6670)){fprintf(stderr,"Original attachment opcode differs: %u %016llx\n",cases,(unsigned long long)hash);return 0;}
    {
        const u32 keys[]={0,1,2,9,255,256,0xffffffff,0x10001,0x7fffffff,0x80000000};
        const u32 subs[]={0,1,7,9,255,256,0xffffffff,0x10007};
        const u32 flags[]={0,0x10,0x100000,0x600000,0xffffffff,0x80000000,0x12345678,0xa5a55a5a};
        const u8 types[]={9,1,1,255,2},parts[]={9,7,7,255,0};
        uint64_t parent_hash=UINT64_C(14695981039346656037);unsigned count=0;
        if(g_ScriptQueueOpcodeHandlers[212]!=Script_SetEntityParentLink)return 0;
        for(unsigned k=0;k<10;++k)for(unsigned s=0;s<8;++s)for(unsigned f=0;f<8;++f)
        for(unsigned layout=0;layout<4;++layout)for(unsigned player=0;player<3;++player)for(unsigned parent=0;parent<6;++parent){
            FieldActor actors[5],before[5],after[5];memset(actors,0xa5,sizeof(actors));
            for(unsigned i=0;i<5;++i){
                actors[i].type_id=types[i];actors[i].sub_id=parts[i];actors[i].next=i<4?actors+i+1:NULL;
                actors[i].flags=flags[(f+i)%8];actors[i].parent=parent?actors+(parent+i-1)%5:NULL;
            }
            memcpy(before,actors,sizeof(actors));g_CurrentEntity=actors;g_FieldActorListHead=layout?actors+layout-1:NULL;
            g_PlayerEntity=player?actors+player-1:NULL;
            int key=(s32)keys[k],sub=(s32)subs[s],*args[]={&key,&sub};u32 result=Script_SetEntityParentLink(args);
            for(unsigned b=0;b<4;++b)parent_hash=(parent_hash^((result>>(b*8))&255))*UINT64_C(1099511628211);
            for(unsigned i=0;i<5;++i){
                unsigned target=0;
                if(actors[i].parent){
                    for(unsigned j=0;j<5;++j)if(actors[i].parent==actors+j)target=j+1;
                    if(!target)return 0;
                }
                const u32 words[]={target,actors[i].flags};
                for(unsigned word=0;word<2;++word)for(unsigned b=0;b<4;++b)
                    parent_hash=(parent_hash^((words[word]>>(b*8))&255))*UINT64_C(1099511628211);
                FieldActor expected=before[i];expected.parent=actors[i].parent;expected.flags=actors[i].flags;
                if(memcmp(actors+i,&expected,sizeof(expected)))return 0;
            }
            memcpy(after,actors,sizeof(actors));memcpy(actors,before,sizeof(actors));
            u32 program[]={212|(2u<<13),0,keys[k],subs[s],32,0};
            ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
            if(result!=1 || key!=(s32)keys[k] || sub!=(s32)subs[s] || program[2]!=keys[k] || program[3]!=subs[s] ||
               memcmp(actors,after,sizeof(actors)) || node.current.script!=program+6 || node.active || node.flags!=0x90 || g_ScriptNodePool)return 0;
            ++count;
        }
        g_CurrentEntity=saved_current;g_PlayerEntity=saved_player;g_FieldActorListHead=saved_head;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
        if(count!=46080 || parent_hash!=UINT64_C(0x42ec853f3260c391)){
            fprintf(stderr,"Parent link CPU mismatch: %u %016llx\n",count,(unsigned long long)parent_hash);return 0;
        }
        puts("Original opcode212: 46080 USA CPU states, player/self/typed/dead/missing parents, full actor guards and real dispatcher passed");
    }
    puts("Original attachment opcode197: 240 unhooked USA CPU selection/animation states, whole actor guards and real dispatcher programs passed");return 1;
}

static int original_script_command_binding(void)
{
    const int ops[]={0,2100,2101,2201,2300,2401,2600,2601,2700,2900,3000,3200};
    const u32 values[]={0,1,255,256,65535,0x80000000,0xffffffff,0x12345678};
    u32 saved_flags=g_GameState.flags;int saved_collision=g_ScriptCollisionFlag;
    GeomScrollRecord saved_scroll=g_GeomScroll;u8 saved_camera[3];memcpy(saved_camera,g_ScriptCameraBytes,3);
    FieldActor *saved_actor=g_CurrentEntity,*saved_head=g_FieldActorListHead;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    uint64_t hash=UINT64_C(14695981039346656037);
    if(g_ScriptQueueOpcodeHandlers[237]!=Script_DispatchScriptCmd)return 0;
    for(unsigned op=0;op<12;++op)for(unsigned v=0;v<8;++v){
        FieldActor actor={0};g_CurrentEntity=&actor;g_FieldActorListHead=NULL;
        int data[]={ops[op],(s32)values[v],(s32)(values[v]+1u),(s32)(values[v]-1u),0,0};
        int *args[]={data,data+1,data+2,data+3,data+4,data+5};
        g_GameState.flags=0x12345678;g_GeomScroll.state.position.originX=0x1234;
        g_GeomScroll.state.position.originY=(s16)0xfedc;
        memcpy(g_ScriptCameraBytes,(u8[]){1,2,3},3);
        actor.shadow_matrix_index=4;actor.shadow_intensity=5;
        g_RoomLightTint.r=7;g_RoomLightTint.g=8;g_RoomLightTint.b=9;
        g_ScriptCollisionFlag=0x76543210;
        u32 result=g_ScriptQueueOpcodeHandlers[237](args);
#define COMMAND_PUT(value,bytes) do {u32 put_=(u32)(value); \
    for(unsigned b_=0;b_<(bytes);++b_){hash=(hash^(u8)put_)*UINT64_C(1099511628211);put_>>=8;}}while(0)
        COMMAND_PUT(result,4);COMMAND_PUT(g_GameState.flags,4);
        COMMAND_PUT(g_GeomScroll.state.position.originX,2);COMMAND_PUT(g_GeomScroll.state.position.originY,2);
        for(unsigned i=0;i<3;++i)COMMAND_PUT(g_ScriptCameraBytes[i],1);
        COMMAND_PUT(actor.shadow_matrix_index,1);COMMAND_PUT(actor.shadow_intensity,1);
        COMMAND_PUT(g_RoomLightTint.r,1);COMMAND_PUT(g_RoomLightTint.g,1);COMMAND_PUT(g_RoomLightTint.b,1);
        COMMAND_PUT(g_ScriptCollisionFlag,4);
#undef COMMAND_PUT
        u32 program[]={237|(6u<<13),0,(u32)data[0],(u32)data[1],(u32)data[2],(u32)data[3],0,0,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};
        g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || node.current.script!=program+10 || !(node.flags&0x10) || g_ScriptNodePool)return 0;
    }
    g_GameState.flags=saved_flags;g_ScriptCollisionFlag=saved_collision;g_GeomScroll=saved_scroll;
    memcpy(g_ScriptCameraBytes,saved_camera,3);g_CurrentEntity=saved_actor;g_FieldActorListHead=saved_head;
    g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0x0dd78fa3cadbad1b)){fprintf(stderr,"Original command dispatcher hash %016llx\n",(unsigned long long)hash);return 0;}
    puts("Original script command dispatcher: 96 USA MIPS states and native opcode completion passed");
    return 1;
}

static int original_clamp_opcode_binding(void)
{
    const u32 high[]={0,0xfffffe00,0x80000000,0x7ffffe00};
    int saved=g_MenuItemContextFlag;FieldActor *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    FieldActor actor={0};g_CurrentEntity=&actor;
    uint64_t hash=UINT64_C(14695981039346656037);
    if(g_ScriptQueueOpcodeHandlers[166]!=Script_ClampMenuRange)return 0;
    for(unsigned h=0;h<4;++h)for(unsigned low=0;low<512;++low){
        int value=(s32)(high[h]|low);int *args[]={&value};g_MenuItemContextFlag=0x1234;
        u32 result=g_ScriptQueueOpcodeHandlers[166](args);
        u32 values[]={result,(u32)g_MenuItemContextFlag};
        for(unsigned i=0;i<2;++i)for(unsigned b=0;b<4;++b)
            hash=(hash^((values[i]>>(b*8))&255))*UINT64_C(1099511628211);
        u32 program[]={166|(1u<<13),0,(u32)value,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};
        g_MenuItemContextFlag=0x1234;g_ScriptNodePool=&node;Script_RunQueue();
        if(result!=1 || node.current.script!=program+5 || !(node.flags&0x10) ||
           g_ScriptNodePool || (u32)g_MenuItemContextFlag!=values[1])return 0;
    }
    g_MenuItemContextFlag=saved;g_CurrentEntity=saved_actor;
    g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(hash!=UINT64_C(0x9ba3598d49695b25))return 0;
    puts("Original menu range opcode: 2048 USA MIPS states and native dispatcher completion passed");
    return 1;
}

static int original_scene_transition_opcode(void)
{
    const u32 tokens[]={0,1,0xA80830C8,0xA80810C8,0xA9400048,0xffffffff,0x80000000,0x7fffffff};
    const u32 flags[]={0,1,0x2000,0x4000,0xffffffff,0x12345678};
    const u32 counters[]={0,2000,2001,0xffffffff};
    u32 saved_flags=g_GameStateFlags,saved_counter=g_PlayTimeFrameCounter;
    int saved_token=g_SceneDispatchToken;FieldActor *saved_actor=g_CurrentEntity;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    FieldActor actor={0};g_CurrentEntity=&actor;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    if(g_ScriptQueueOpcodeHandlers[49]!=Script_EncodeBarcode)return 0;
    for(unsigned t=0;t<8;++t)for(unsigned f=0;f<6;++f)for(unsigned c=0;c<4;++c){
        if(tokens[t]==0xA9400048 && c!=1)continue;
        int value=(s32)tokens[t];int *args[]={&value};
        g_GameStateFlags=flags[f];g_SceneDispatchToken=0x1234;g_PlayTimeFrameCounter=counters[c];
        u32 result=g_ScriptQueueOpcodeHandlers[49](args);
        u32 values[]={result,g_GameStateFlags,(u32)g_SceneDispatchToken,g_PlayTimeFrameCounter};
        for(unsigned i=0;i<4;++i)for(unsigned b=0;b<4;++b)
            hash=(hash^((values[i]>>(b*8))&255))*UINT64_C(1099511628211);
        u32 program[]={49|(1u<<13),0,tokens[t],32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};
        g_GameStateFlags=flags[f];g_SceneDispatchToken=0x1234;g_PlayTimeFrameCounter=counters[c];
        g_ScriptNodePool=&node;Script_RunQueue();
        if(result || node.current.script!=program+3 || node.flags!=0x80 || node.active!=0 ||
           g_ScriptNodePool || g_GameStateFlags!=values[1] || (u32)g_SceneDispatchToken!=values[2] ||
           g_PlayTimeFrameCounter!=values[3])return 0;
        ++cases;
    }
    g_GameStateFlags=saved_flags;g_PlayTimeFrameCounter=saved_counter;g_SceneDispatchToken=saved_token;
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(cases!=174 || hash!=UINT64_C(0x8ae1600ce89fd57c))return 0;
    puts("Original scene transition opcode: 174 USA MIPS states and native dispatcher yield/cursor passed");
    return 1;
}

static int original_native_voice_release(void)
{
    AkaoSequencerBank *saved_bank=g_AkaoCurTrack;uint64_t hash=UINT64_C(14695981039346656037);
    const u32 masks[]={0,0xffffff,0x555555,0xffffffff};u8 programs[24][8];
    for(unsigned function=0;function<2;++function)for(unsigned request=0;request<27;++request)
    for(unsigned layout=0;layout<6;++layout)for(unsigned m=0;m<4;++m){
        int requested=request<25?(int)request:request==25?-1:INT32_MAX;
        AkaoTrack tracks[24],before[24];memset(tracks,0xa5,sizeof(tracks));
        for(unsigned i=0;i<24;++i){
            int id=layout==0?(int)i:layout==1?7:layout==2?(i&1)?24:(int)i:
                layout==3?(i&1)?-1:0:layout==4?(int)(i%3):(int)((i*17+layout)%28)-2;
            tracks[i].assigned_voice_index=id;tracks[i].pc=programs[i];
            for(unsigned j=0;j<4;++j)tracks[i].call_stack[j]=programs[i]+j;
        }
        memcpy(before,tracks,sizeof(before));
        AkaoSequencerBank bank={.allocated_voice_mask=masks[m]},expected_bank=bank;g_AkaoCurTrack=&bank;
        if(function)Seq_ClearTrackVoiceId(tracks,(u32)requested);else Akao_RemoveVoice(tracks,requested);
        for(unsigned i=0;i<24;++i){
            u32 id=(u32)tracks[i].assigned_voice_index;
            for(unsigned b=0;b<4;++b)hash=(hash^((id>>(b*8))&255))*UINT64_C(1099511628211);
            before[i].assigned_voice_index=tracks[i].assigned_voice_index;
        }
        for(unsigned b=0;b<4;++b)hash=(hash^((bank.allocated_voice_mask>>(b*8))&255))*UINT64_C(1099511628211);
        expected_bank.allocated_voice_mask=bank.allocated_voice_mask;
        if(memcmp(tracks,before,sizeof(tracks)) || memcmp(&bank,&expected_bank,sizeof(bank)))return 0;
    }
    g_AkaoCurTrack=saved_bank;
    if(hash!=UINT64_C(0xe2e44b8a1b490a5a)){fprintf(stderr,"Native voice release hash %016llx\n",(unsigned long long)hash);return 0;}
    puts("Original voice release: 1296 USA MIPS cases, all 24 native records and full-width program/return pointers retained");
    return 1;
}

static int original_actor_audio_state(void)
{
    if(!original_attachment_opcode() || !original_battle_entry_opcode() || !original_conditional_branch_opcode() || !original_context_getter_opcodes() || !original_context_setter_opcodes() || !original_script_event_entries() || !original_animation_event_opcode() || !original_action_wait_opcode() || !original_typed_field_opcode() || !original_collision_gate_opcodes() || !original_player_camera_reset_opcode() || !original_scroll_opcodes() || !original_native_voice_release() || !original_polygon_opcode() || !original_static_model_matrices() || !original_script_polygon_opcode() || !original_follow_actor_opcode() || !original_render_flag_opcodes() || !original_room_glow_orb() || !original_three_particle_states() || !original_three_particle_program() || !original_drop_field_states() || !original_room_effect_core_flags() || !authored_fan_sweep_cycles() || !original_textbox_rect_opcode() || !original_task_pause_opcodes() || !original_geometry_visibility_opcode() || !original_animation_stop_slot_opcode() || !original_scene_entry_position_opcode() || !original_render_position_reset_opcodes() || !original_floor_resolve_opcode() || !original_scene_entry_raw_bytes() || !original_equipment_open_opcode() || !original_memcard_dialog_opcode() || !original_movement_gate_opcodes() || !original_inventory_storage_swaps() || !original_inventory_count_opcode() || !original_inventory_command_opcode() || !original_equip_selection_opcode() || !original_polar_position_opcode() || !original_motion_opcodes() || !original_dialogue_opcodes() || !original_turn_to_actor_opcode() || !original_screen_fade_wait_opcode() || !original_screen_fade_opcodes() || !original_geometry_control_opcodes() || !original_geometry_slot_opcodes() || !original_shadow_radius_opcode() || !original_frame_clock_layout() || !original_script_timer_commands() || !original_scene_frame_timers() || !original_camera_release_opcode() || !original_camera_flags_opcodes() || !original_animation_frame_opcode() || !original_script_command_binding() || !original_clamp_opcode_binding() || !original_scene_transition_opcode() || !original_wait_opcode_bindings() || !original_field_sfx_opcode_binding() || !original_flag_opcode_bindings())return 0;
    Pe1GameState previous=g_GameState;
    FieldActor actor={.type_id=7,.sub_id=11};
    FieldActor *previous_actor=g_CurrentEntity;
    int op=0x15e,a=0x12345,b=0x187,c=-2,d=-129,result=123;
    AkaoCommandArgs args={&op,&a,&b,&c,&d,&result};
    g_CurrentEntity=&actor;
    if(Akao_ProcessCommand(args)!=1 || g_GameState.bank_value_f8!=(u16)a ||
       g_GameState.bank_value_f6!=(u8)b || g_GameState.bank_value_fa!=(u16)c ||
       g_GameState.bank_value_f7!=(u8)d || result!=123) return 0;
    op=0x195; g_GameState.bank_state_12=0;
    if(Akao_ProcessCommand(args)!=1 || g_GameState.bank_state_12!=1) return 0;
    g_GameState.bank_state_11=0;
    for(unsigned i=0;i<18;++i) {
        op=i&1?0x197:0x196; a=0x180+i;b=-129;c=0x12345;d=-2;
        if(Akao_ProcessCommand(args)!=1 || g_GameState.bank_state_11!=(i<16?i+1:16)) return 0;
        if(i<16) {
            DeferredAkaoCommand *command=&g_DeferredAkaoCommands[i];
            if(command->actor_type!=7 || command->actor_id!=11 ||
               command->command!=(s8)a || command->arg1!=(s8)b ||
               command->arg2!=(s16)c || command->arg3!=(s16)(i&1?d:c)) return 0;
        }
    }
    /* The cache hit uses the same bank pointer the original loader installs. */
    u16 sample_bank[12]={0x4b41,0x4f41,42,0,7,0};
    g_GameState.bank_work_base=(u8 *)sample_bank;
    g_GameState.bank_work_end=(u8 *)sample_bank;
    struct { u32 root[2]; SceneAssetDirectory directory; } archive={0};
    archive.root[1]=sizeof(archive.root);
    g_GameState.loaded_scene_assets=(SceneAssetView *)&archive;
    for(unsigned slot=0;slot<2;++slot) {
        g_GameState.scene_audio.tracks.keys[0][0]=slot? -1:23;
        g_GameState.scene_audio.tracks.keys[1][0]=slot?23:-1;
        g_GameState.flags&=~0xc0u;g_GameState.cd_track_phase=0;
        g_AkaoMessageQueueCount=0;
        op=0xcb;a=23;b=0x55;c=1;
        if(Akao_ProcessCommand(args)!=1 || result!=42 || g_AkaoMessageQueueCount!=2 ||
           g_AkaoMessageQueue[0].opcode.word!=0x19 ||
           g_AkaoMessageQueue[0].arg0.sample_data!=(void *)(sample_bank+8) ||
           g_AkaoMessageQueue[1].opcode.word!=0xc0 ||
           g_AkaoMessageQueue[1].arg0.value!=0x55 ||
           g_GameState.scene_audio.tracks.keys[slot][1]!=42 ||
           g_GameState.track_volumes[slot]!=0x55) return 0;
    }
    u32 sound_archive[32]={0};
    sound_archive[1]=8;
    sound_archive[(8+0x30)/4]=(1u<<22)|64;
    u8 *record=(u8 *)sound_archive+64;
    record[3]=0x10;
    sound_archive[(64+4)/4]=96;
    record[8]=0xff;record[10]=0x34;record[11]=0x12;
    g_GameState.loaded_scene_assets=(SceneAssetView *)sound_archive;
    op=0x190;a=0x1234;
    if(Akao_ProcessCommand(args)!=1 || (u8)g_GameState.scene_audio.tracks.pending_key!=0x34 ||
       (u8)g_GameState.scene_audio.tracks.pending_bank!=0xff ||
       g_GameState.bank_asset_table!=(u8 *)sound_archive+96) return 0;
    op=0x199;a=0x198;
    if(Akao_ProcessCommand(args)!=1 || g_GameState.scene_audio.tracks.movie_volume!=0x98) return 0;
    if(g_ScriptQueueOpcodeHandlers[234]!=Akao_ProcessCommand)return 0;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor,saved_variable=g_ScriptVariableBank[9];
    u32 program[]={234|(6u<<13),2,0x15e,0x23456,0x189,(u32)-3,(u32)-130,9,
                   234|(2u<<13),0,0x199,0x145,32,0};
    ScriptNode node={.current.script=program,.active=1,.flags=0x80};
    g_ScriptVariableBank[9]=0xa5a5a5a5;g_ScriptNodePool=&node;Script_RunQueue();
    if(!(node.flags&0x10) || node.current.script!=program+14 || g_ScriptNodePool ||
       g_GameState.bank_value_f8!=0x3456 || g_GameState.bank_value_f6!=0x89 ||
       g_GameState.bank_value_fa!=65533 || g_GameState.bank_value_f7!=126 ||
       g_GameState.scene_audio.tracks.movie_volume!=0x45 || g_ScriptVariableBank[9]!=0xa5a5a5a5)return 0;
    g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;g_ScriptVariableBank[9]=saved_variable;
    g_AkaoMessageQueueCount=0;g_GameState=previous;g_CurrentEntity=previous_actor;
    puts("Original actor audio: canonical positional/deferral/archive/volume state, sixteen-command limit and both cached native bank pointers passed");
    return 1;
}

static int original_burst_target_selection(void)
{
    FieldFxHandler initialize[]={BurstEffect_InitTarget,BurstEffectB_InitTarget,BurstEffectC_InitTarget};
    FieldActor **selected[]={&g_BurstEffectTarget,&g_BurstEffectBTarget,&g_BurstEffectCTarget};
    FieldActor actors[2]={0}; FieldActor *targets[]={&actors[0],&actors[1],NULL};
    unsigned int sound_header[4]={0x4f414b41u,0,0,0};
    Pe1GameState previous=g_GameState;
    int previous_page=g_FieldFxLoadedTexturePage;
    for(unsigned variant=0;variant<3;++variant) for(unsigned gate=0;gate<3;++gate) {
        FieldFxObject object={0};
        FieldFx_GetSlot(&object);
        *FieldFx_VarPtr(1)=(intptr_t)targets;
        g_GameState.cd_range_read_mode=(u8[]){0,1,255}[gate];
        g_GameState.scene_object_tables[2]=sound_header;
        g_FieldFxLoadedTexturePage=0;
        for(unsigned index=0;index<2;++index) {
            actors[index].render_object.target_x=0x1234+index;
            actors[index].render_object.target_y=0xabcd-index;
            actors[index].render_object.target_z=0x8000+index;
            g_AkaoMessageQueueCount=0;
            if(initialize[variant](&object,NULL,NULL) || *selected[variant]!=&actors[index] ||
               *FieldFx_RegPtr(13)!=(int)index+1 || *FieldFx_RegPtr(14)!=(int)index) return 0;
            u16 x=variant==0?g_BurstEffectTargetPosition.x:variant==1?g_BurstEffectBTargetPosition.x:g_BurstEffectCTargetPosition.x;
            u16 y=variant==0?g_BurstEffectTargetPosition.y:variant==1?g_BurstEffectBTargetPosition.y:g_BurstEffectCTargetPosition.y;
            u16 z=variant==0?g_BurstEffectTargetPosition.z:variant==1?g_BurstEffectBTargetPosition.z:g_BurstEffectCTargetPosition.z;
            if(x!=(u16)(0x1234+index) || y!=(u16)(0xabcd-index) || z!=(u16)(0x8000+index)) return 0;
            int expected=variant<2 && gate!=0;
            if(g_AkaoMessageQueueCount!=expected) return 0;
            if(expected && (g_AkaoMessageQueue[0].opcode.word!=0x24 ||
               g_AkaoMessageQueue[0].arg0.sequence!=(u8 *)(sound_header+1) ||
               g_AkaoMessageQueue[0].arg1!=0 || g_AkaoMessageQueue[0].arg2!=0x80 ||
               g_AkaoMessageQueue[0].arg3!=0x7f)) return 0;
        }
    }
    g_AkaoMessageQueueCount=0; g_GameState=previous; g_FieldFxLoadedTexturePage=previous_page;
    g_BurstEffectTarget=g_BurstEffectBTarget=g_BurstEffectCTarget=NULL;
    puts("Original bursts: native target lists, coordinates, final-target marker and canonical sound gate enqueue through original AKAO passed");
    return 1;
}

static int native_effect_spawn_arguments(void)
{
    struct { FieldFxObject object; u8 guard[16]; } buffer;
    PmSendCallback spawn[]={GlowEffect_Spawn6,GlowEffectB_Spawn6,
        GlowEffectC_Spawn6,FourLayerGlow_Spawn6,PointSprite_Spawn6,BurstEffect_Spawn6,BurstEffectB_Spawn6,BurstEffectC_Spawn6};
    for(unsigned method=0;method<8;++method) for(unsigned index=0;index<9;++index) {
        memset(&buffer,0xa5,sizeof(buffer));
        FieldFxState expected=buffer.object.state;
        int value=index&1?INT_MIN:INT_MAX;
        expected.vars[index]=value;
        if(spawn[method]((PmSlotHeader *)&buffer.object,0x1234,index,value,0x5678,0x9abc) ||
           memcmp(&expected,&buffer.object.state,sizeof(expected)) ||
           g_FieldFxScriptState!=&buffer.object.state) return 0;
        for(unsigned i=0;i<sizeof(buffer.guard);++i) if(buffer.guard[i]!=0xa5) return 0;
        FieldActor target={0}; FieldActor *targets[2]={&target,NULL};
        if(spawn[method]((PmSlotHeader *)&buffer.object,0,index,(intptr_t)targets,0,0) ||
           *FieldFx_VarPtr(index)!=(intptr_t)targets) return 0;
        FieldActor **received=(FieldActor **)*FieldFx_VarPtr(index);
        if(received[0]!=&target || received[1]!=NULL) return 0;
        FieldFxState saved=buffer.object.state;
        if(FieldFx_Spawn6((intptr_t)&buffer.object,0,-1,42,0,0)!=-1 ||
           FieldFx_Spawn6((intptr_t)&buffer.object,0,9,42,0,0)!=-1 ||
           FieldFx_Spawn6(0,0,0,42,0,0)!=-1 || memcmp(&saved,&buffer.object.state,sizeof(saved))) return 0;
    }
    puts("Original spawn arguments: eight entry points, native owners/target-list addresses, all variables, signed values and invalid bounds passed");
    return 1;
}

static size_t slot_write_size;
static unsigned slot_write_calls;
static int object_slot_write(void *object, void *slot, void *data)
{
    FieldFxObject *owner=object; FieldFxSlot *entry=slot;
    if(data!=owner->data+entry->data_offset || entry->data_offset<0 ||
       entry->data_offset+slot_write_size>sizeof(owner->data)) abort();
    memset(data,0x6c,slot_write_size); ++slot_write_calls;
    return 0;
}
static int native_object_slot_bounds(void)
{
    struct { FieldFxObject object; u8 guard[16]; } buffer={0};
    FieldFxObject *object=&buffer.object;
    FieldFxHandler handlers[]={object_slot_write,object_slot_write};
    u16 sizes[]={1,FIELDFX_DATA_SIZE+1};
    object->prefix.asset_type=7;
    memset(buffer.guard,0xa5,sizeof(buffer.guard));
    FieldFx_GetSlot(object); slot_write_size=1; slot_write_calls=0;
    for(unsigned i=0;i<64;++i) {
        if(FieldFx_StartSlot(object,0,sizes,handlers)!=&object->data[i] ||
           object->slots[i].flag!=1 || object->slots[i].data_offset!=i) return 0;
    }
    if(object->state.slot_count!=64 || FieldFx_StartSlot(object,0,sizes,handlers) ||
       object->state.regs[14]!=1 || slot_write_calls!=64) return 0;
    object->state.halted=1;
    if(FieldFx_FreeAllSlots()!=-1 || object->state.slot_count || FieldFx_FreeSlot(0) ||
       FieldFx_FreeSlot(64) || object->state.slot_count) return 0;
    FieldFx_GetSlot(object); slot_write_calls=0;
    FieldFxState saved=object->state;
    if(FieldFx_StartSlot(object,1,sizes,handlers) || slot_write_calls ||
       memcmp(&saved,&object->state,sizeof(saved)) || object->slots[0].flag) return 0;
    FieldFx_ActivateSlot(64,0,1); FieldFx_ActivateSlot(0,0,FIELDFX_DATA_SIZE+1);
    if(memcmp(&saved,&object->state,sizeof(saved)) || object->slots[0].flag) return 0;
    object->state.data_next=-1; saved=object->state;
    if(FieldFx_StartSlot(object,0,sizes,handlers) || memcmp(&saved,&object->state,sizeof(saved))) return 0;
    FieldFx_GetSlot(object);
    sizes[0]=FIELDFX_DATA_SIZE-1; slot_write_size=sizes[0];
    if(FieldFx_StartSlot(object,0,sizes,handlers)!=object->data ||
       object->state.data_next!=FIELDFX_DATA_SIZE-1) return 0;
    sizes[0]=2; slot_write_size=2;
    if(FieldFx_StartSlot(object,0,sizes,handlers)!=object->data || object->state.data_next!=2 ||
       object->slots[1].data_offset) return 0;
    FieldFx_GetSlot(object); object->state.data_next=8;
    sizes[0]=FIELDFX_DATA_SIZE; slot_write_size=sizes[0];
    if(FieldFx_StartSlot(object,0,sizes,handlers)!=object->data ||
       object->state.data_next!=FIELDFX_DATA_SIZE) return 0;
    for(unsigned i=0;i<FIELDFX_DATA_SIZE;++i) if((u8)object->data[i]!=0x6c) return 0;
    for(unsigned i=0;i<sizeof(buffer.guard);++i) if(buffer.guard[i]!=0xa5) return 0;
    puts("Original object slots: 64-slot limit, full arena write, original wrap, invalid reservations and double-free guards passed");
    return 1;
}

static int native_task_relocation_safety(void) {
    /* Truncated/oversized disk task tables must leave the previous native
     * table alive. Deliberately use an unaligned serialized block. */
    RelocBlock *old_tasks = g_SceneDataTable1;
    u8 task_bytes[33] = {0}; u8 *task = task_bytes + 1;
    u32 words[] = {16, 2, 20, 28}; memcpy(task, words, sizeof(words));
    RelocBlock *resolved = Script_RelocBlock(task, 32);
    if (!resolved || resolved->count != 2 || resolved->u0.base != task + 16 ||
        resolved->offsets[0] != task + 20 || resolved->offsets[1] != task + 28) return 0;
    for (unsigned fault = 0; fault < 6; ++fault) {
        memcpy(task, words, sizeof(words));
        u32 invalid = fault == 1 ? UINT32_MAX : 32;
        size_t bytes = fault == 0 ? 7 : fault == 5 ? 12 : 32;
        if (fault == 1) memcpy(task + 4, &invalid, 4);
        if (fault == 2) memcpy(task, &invalid, 4);
        if (fault == 3) memcpy(task + 8, &invalid, 4);
        if (fault == 4) { invalid = UINT32_MAX; memcpy(task + 12, &invalid, 4); }
        if (Script_RelocBlock(task, bytes) || g_SceneDataTable1 != resolved ||
            resolved->offsets[0] != task + 20) return 0;
    }
    g_SceneDataTable1 = old_tasks;
    puts("Task relocation bounds: unaligned valid block, truncated headers/tables, oversized counts and out-of-range offsets passed");
    return 1;
}

static int process_static_task_programs(void)
{
    if (!native_task_relocation_safety()) return 0;
    Pe1GameState saved=g_GameState;PmTables tables=g_PmTables;u32 flags=g_GameStateFlags;
    void *programs[104];memcpy(programs,g_FieldFxTaskPrograms,sizeof(programs));
    FieldFxTaskProgram *native[]={&g_FieldFxTaskProgramCmd108,&g_FieldFxTaskProgramCmd109,
        &g_FieldFxTaskProgramCmd110,&g_FieldFxTaskProgramCmd111,&g_FieldFxTaskProgramCmd112,
        &g_FieldFxTaskProgramCmd113,&g_FieldFxTaskProgramCmd114};
    PmCommand *handlers[86]={0};PmPrimarySlot bank[11]={0};
    g_PmTables=(PmTables){handlers,bank,NULL};g_GameStateFlags=0x80;
    g_GameState.flags&=~8u;g_GameState.tim_load_state=0;
    for(unsigned command=108;command<=114;++command){
        g_GameState.flags&=~0x10000u;glyph_reads=glyph_polls=0;
        glyph_read_retry=glyph_poll_retry=glyph_pending=1;
        g_MenuCardCrossFlashRaised=g_BattleCrossFlashStop=77;
        g_MemcardSineColor.r=77;g_MemcardCosineColor.g=88;g_MemcardGlintOrigin.x=99;
        /* No handler is installed: binding succeeds without any image I/O,
         * then the original PM dispatch reports the missing handler. */
        if(Scene_LoadRoomAssets(command,NULL)!=-8 || glyph_reads || glyph_polls || !(g_GameState.flags&0x10000))return 0;
        for(unsigned i=0;i<7;++i)if(g_FieldFxTaskPrograms[23+i]!=native[i])return 0;
        if(g_MenuCardCrossFlashRaised || g_BattleCrossFlashStop || g_MemcardSineColor.r ||
           g_MemcardCosineColor.g || g_MemcardGlintOrigin.x)return 0;
        FieldFxTaskOwner owner={0};if(FieldFx_BindTaskProgram(&owner,command-85) ||
            owner.tasks.table!=&native[command-108]->table || owner.tasks.pc.script!=native[command-108]->script)return 0;
    }
    g_MemcardSineColor.r=77;
    if(Scene_LoadRoomAssets(108,NULL)!=-8 || g_MemcardSineColor.r!=77)return 0;
    bank[0].header.command=114;bank[0].header.state=1;
    if(Pm_FreeSlot(0) || (g_GameState.flags&0x10000))return 0;
    for(unsigned i=23;i<30;++i)if(g_FieldFxTaskPrograms[i])return 0;
    if(Scene_LoadRoomAssets(108,NULL)!=-8 || g_MemcardSineColor.r)return 0;
    memcpy(g_FieldFxTaskPrograms,programs,sizeof(programs));g_GameState=saved;g_PmTables=tables;g_GameStateFlags=flags;
    glyph_reads=glyph_polls=0;glyph_read_retry=glyph_poll_retry=glyph_pending=0;
    puts("Original static task programs: seven real bindings, cold/reset versus cached state, 114 release/rebind and no executable-bank I/O passed");return 1;
}

static int process_texture_bank_failures(void)
{
    Pe1GameState saved=g_GameState; PmTables tables=g_PmTables;
    static PmCommand *handlers[86];
    g_PmTables.handlers=handlers;
    g_GameState.pe_image_base_lba=12345; g_GameState.scene_load_scratch=glyph_scratch;
    g_GameState.loaded_scene_assets=(SceneAssetView *)glyph_scratch;
    g_PeImageSectorTable[5]=7; g_PeImageSectorTable[6]=49;
    g_GameState.flags|=8;
    g_GameState.tim_load_state=0x34; glyph_reads=glyph_polls=0; glyph_read_retry=1;
    if(Scene_LoadRoomAssets(0,NULL)!=-14 || g_GameState.tim_load_state!=0x34 ||
       glyph_reads!=1 || glyph_polls || !(g_GameState.flags&8)) return 0;
    g_GameState.tim_load_state=0x35; glyph_reads=glyph_polls=0; glyph_poll_retry=glyph_pending=1;
    if(Scene_LoadRoomAssets(0,NULL)!=-14 || g_GameState.tim_load_state!=0x34 ||
       glyph_reads || glyph_polls!=2 || !(g_GameState.flags&8)) return 0;
    glyph_reads=glyph_polls=0; g_PeImageSectorTable[5]=8; g_PeImageSectorTable[6]=50;
    if(Asset_LoadTimTextures(0)!=-1 || g_GameState.tim_load_state!=0x34 || glyph_reads!=1 || glyph_polls) return 0;
    glyph_reads=0; g_PeImageSectorTable[6]=8;
    if(Asset_LoadTimTextures(0)!=-1 || glyph_reads || glyph_polls) return 0;
    g_PeImageSectorTable[6]=7;
    if(Asset_LoadTimTextures(0)!=-1 || glyph_reads || glyph_polls) return 0;
    g_PeImageSectorTable[5]=7; g_PeImageSectorTable[6]=49;
    g_GameState.pe_image_base_lba=UINT32_MAX-10;
    if(Asset_LoadTimTextures(0)!=-1 || glyph_reads || glyph_polls) return 0;
    g_GameState.pe_image_base_lba=12345; g_GameState.scene_load_scratch=NULL;
    if(Asset_LoadTimTextures(0)!=-1 || glyph_reads || glyph_polls) return 0;
    g_GameState.scene_load_scratch=glyph_scratch;
    memset(glyph_source,0,sizeof(glyph_source));
    SceneAssetBlob empty={.directoryOffset=sizeof(SceneAssetBlob)};
    memcpy(glyph_source,&empty,sizeof(empty));
    glyph_pending=1;
    /* A successful empty upload completes first; only then the missing PM
     * handler is reported. No fake handler or program is executed. */
    if(Scene_LoadRoomAssets(0,NULL)!=-8 || g_GameState.tim_load_state ||
       (g_GameState.flags&8) || glyph_reads!=1 || glyph_polls!=2) return 0;
    g_GameState=saved; g_PmTables=tables; glyph_reads=glyph_polls=0;
    glyph_read_retry=glyph_poll_retry=glyph_pending=0;
    puts("Original process texture I/O: read/pending-poll errors, retry phase, extent checks and empty-bank recovery passed");
    return 1;
}

static int native_actor_effect_gates(void)
{
    FieldFxObject object={0}; FieldActor actor={0}; FieldActorState state={0};
    u8 action=1; FieldFxHandler handlers[1]={0};
    actor.state=&state; state.control10.command_value=1; state.action_state=&action;
    object.prefix.actor=&actor;
    FieldFx_GetSlot(&object);
    for(unsigned selector=0;selector<8;++selector) for(unsigned enabled=0;enabled<2;++enabled)
    for(unsigned value=0;value<2;++value) {
        state.count_limit=state.progress_step=0; state.flags=0;
        memset(state.transition_timers,0,sizeof(state.transition_timers));
        memset(state.number_desc_1,0,sizeof(state.number_desc_1));
        memset(state.reserved74,0,sizeof(state.reserved74));
        state.substate=NULL; state.script_value8c=0;
        switch(selector) {
        case 0: state.count_limit=value; break;
        case 1: state.progress_step=value; break;
        case 2: state.transition_timers[0]=value; break;
        case 3: state.flags=value; break;
        case 4: state.number_desc_1[4]=value; break;
        case 5: state.substate=(void *)(uintptr_t)(0x100+value); break;
        case 6: state.reserved74[8]=value; break;
        case 7: state.script_value8c=value; break;
        }
        state.core_flags=(selector<<21)|(enabled?2:0); action=1;
        if(FieldFx_UpdateSlots(&object,handlers)!=(enabled && !value?-1:0) || action!=2) return 0;
    }
    puts("Original actor effect gates: all eight native fields, active masks, zero/nonzero values and action transition passed");
    return 1;
}

