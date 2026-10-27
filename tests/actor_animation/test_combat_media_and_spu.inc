static int original_enemy_death(void)
{
    extern void Battle_StepEntityAnimState(BattleEntity *);
    Combatant actor={0};BattleEntity entity,child;RenderObjectHeader header={0};
    Combatant *old_actor=g_ActiveActor;FieldActor *old_player=g_PlayerEntity,*old_head=g_FieldActorListHead;
    BattleEnemySlot old_slot=g_BattleEnemySlots[0];s8 old_live=g_BattleLiveEnemyCount;
    u32 old_exp=g_BattleExpReward;u16 old_pe=g_BattlePeBonusReward;int old_draw=g_ActiveDrawSlot;
    BattleRewardSlot old_rewards[10];memcpy(old_rewards,g_BattleRewardSlots,sizeof(old_rewards));
    int queued=g_AkaoMessageQueueCount;AkaoQueueEntry old_message=g_AkaoMessageQueue[0];AkaoStagedCommand staged=g_AkaoCmd;
    g_ActiveActor=&actor;g_PlayerEntity=NULL;g_FieldActorListHead=(FieldActor *)&entity;
    const u8 kinds[]={0,2},frames[]={0,7,8},fades[]={0,1,7,8,9};
    const u16 rewards[][3]={{9,2,3},{0,65535,65535},{65535,1,1}};
    uint64_t hash=UINT64_C(14695981039346656037);
#define HASH_DEATH(value,bytes) do { \
    u32 word=(u32)(value); \
    for(unsigned byte=0;byte<(bytes);++byte)hash=(hash^((word>>(byte*8))&255))*UINT64_C(1099511628211); \
} while(0)
    for(unsigned phase=0;phase<4;++phase)for(unsigned persist=0;persist<2;++persist)
    for(unsigned visible=0;visible<2;++visible)for(unsigned kind=0;kind<2;++kind)
    for(unsigned frame=0;frame<3;++frame)for(unsigned fade=0;fade<5;++fade)
    for(unsigned reward=0;reward<3;++reward)for(unsigned full=0;full<2;++full)
    for(unsigned aux=0;aux<2;++aux)for(unsigned slot=0;slot<2;++slot) {
        EnemyCombatant *core=&g_BattleEnemySlots[0].combatant;
        memset(core,0,sizeof(*core));memset(&entity,0,sizeof(entity));memset(&child,0,sizeof(child));
        entity.core=core;entity.next=&child;entity.entityFlags=0x240;entity.animFrame=frames[frame]<<16;
        entity.motionX=100;entity.motionY=101;entity.motionZ=102;child.parent=&entity;child.entityFlags=0x20;
        entity.renderObject.header=child.renderObject.header=&header;
        entity.renderObject.draw_count=child.renderObject.draw_count=1;entity.renderObject.variant_visible=visible;
        core->field04.bytes.kind=kinds[kind];core->slotBit=4;core->hpMirror=65535;core->deathAnimFrame=8;
        core->deathAnimPhase=phase;core->deathFadeStep=fades[fade];core->deathPersist=persist;
        core->rewardBase=rewards[reward][0];core->rewardFactorA=rewards[reward][1];core->rewardFactorB=rewards[reward][2];
        core->rewardSlotId=0x44;core->lootItemId=300;core->lootItemAux=aux?77:-1;
        for(unsigned i=0;i<10;++i)g_BattleRewardSlots[i]=(BattleRewardSlot){full?55:0,-1};
        g_BattleEnemySlots[0].active=1;g_BattleLiveEnemyCount=1;g_ActiveDrawSlot=slot;
        g_BattleExpReward=0xfffffff0;g_BattlePeBonusReward=65530;g_AkaoMessageQueueCount=0;
        memset(&g_AkaoMessageQueue[0],0,sizeof(g_AkaoMessageQueue[0]));
        Battle_StepEntityAnimState(&entity);
        HASH_DEATH(core->deathAnimPhase,1);HASH_DEATH(core->deathFadeStep,1);
        HASH_DEATH(entity.core==NULL,1);HASH_DEATH(g_BattleEnemySlots[0].active,4);
        HASH_DEATH((u8)g_BattleLiveEnemyCount,1);HASH_DEATH(g_BattleExpReward,4);HASH_DEATH(g_BattlePeBonusReward,2);
        BattleEntity *entities[]={&entity,&child};
        for(unsigned i=0;i<2;++i) {
            BattleEntity *e=entities[i];RenderObjectEntity *o=&e->renderObject;
            HASH_DEATH(e->entityFlags,4);HASH_DEATH(e->motionX,4);HASH_DEATH(e->motionY,4);HASH_DEATH(e->motionZ,4);
            HASH_DEATH((u8)o->fade_duration,1);HASH_DEATH(o->fade_red_step,1);HASH_DEATH(o->fade_green_step,1);
            HASH_DEATH(o->fade_blue_step,1);HASH_DEATH(o->primitive_red,1);HASH_DEATH(o->primitive_green,1);
            HASH_DEATH(o->primitive_blue,1);HASH_DEATH(o->flags_9C,2);
        }
        for(unsigned i=0;i<10;++i) {HASH_DEATH((u16)g_BattleRewardSlots[i].id,2);HASH_DEATH((u16)g_BattleRewardSlots[i].extra,2);}
        HASH_DEATH(g_AkaoMessageQueueCount,4);
        if(g_AkaoMessageQueueCount) {HASH_DEATH(g_AkaoMessageQueue[0].opcode.word,4);HASH_DEATH(g_AkaoMessageQueue[0].arg0.value,4);HASH_DEATH(g_AkaoMessageQueue[0].arg1,4);}
    }
#undef HASH_DEATH
    g_ActiveActor=old_actor;g_PlayerEntity=old_player;g_FieldActorListHead=old_head;g_BattleEnemySlots[0]=old_slot;
    g_BattleLiveEnemyCount=old_live;g_BattleExpReward=old_exp;g_BattlePeBonusReward=old_pe;g_ActiveDrawSlot=old_draw;
    memcpy(g_BattleRewardSlots,old_rewards,sizeof(old_rewards));g_AkaoMessageQueueCount=queued;g_AkaoMessageQueue[0]=old_message;g_AkaoCmd=staged;
    if(hash!=UINT64_C(0x85c5db394010ef9d)) {
        fprintf(stderr,"Original enemy death hash %016llx differs from USA MIPS\n",(unsigned long long)hash);return 0;
    }
    puts("Original enemy death: 11,520 phase/fade/child/visibility/reward cases match unhooked USA MIPS");
    return 1;
}

static int original_enemy_damage(void)
{
    Combatant actor={0};EnemyCombatant enemy;BattleEntity entity;BattleAction action={0};
    Combatant *old_actor=g_ActiveActor;u32 old_random=Psyz_RandState();s8 old_count=g_BattleTargetCount;
    void *old_animation=g_GameState.bank_rows[0][2];
    RenderAnimationDataHeader animation={0};animation.frame_count=9;
    g_GameState.bank_rows[0][2]=&animation;g_ActiveActor=&actor;g_BattleTargetCount=4;
    const u32 abilities[]={0,0x80000,0x100000},statuses[]={0,0x155555,0xaaaaaa,0xffffff};
    const u16 stats[]={25,100,9999,65535},shots[]={1,2,3,5,7,10};
    const u16 elements[]={0,0x100,0x200,0x300,0x400,0x800,0x1000,0x1f00},defenses[]={0,10,200,65535};
    const u32 flags[]={0,0x8000,0x18000,0x20000,0x80000,0xc0000,0x90000,0xd8000};
    const u32 seeds[]={0x12345678,0x87654321};
    actor.field04.fieldId04=99;actor.exp_or_acc=100<<16;actor.maxAtk=200<<16;actor.action=&action;
    action.powerBonus=75;action.actionCode.actionId=6;
    uint64_t hash=UINT64_C(14695981039346656037);
#define HASH_DAMAGE(value,bytes) do { \
    u32 word=(u32)(value); \
    for(unsigned byte=0;byte<(bytes);++byte)hash=(hash^((word>>(byte*8))&255))*UINT64_C(1099511628211); \
} while(0)
    for(unsigned ability=0;ability<3;++ability)for(unsigned stat=0;stat<4;++stat)
    for(unsigned shot=0;shot<6;++shot)for(unsigned element=0;element<8;++element)
    for(unsigned status=0;status<4;++status)for(unsigned flag=0;flag<8;++flag)
    for(unsigned seed=0;seed<2;++seed)for(unsigned defense=0;defense<4;++defense) {
        memset(&enemy,0,sizeof(enemy));memset(&entity,0,sizeof(entity));
        actor.stateFlags=abilities[ability];actor.stat1E=stats[stat];action.turnWord=shots[shot]|elements[element];
        entity.core=&enemy;entity.entityFlags=0x240;entity.actionMode=9;entity.animLastFrame=5;
        entity.animFrame=7<<16;entity.animPrev.fixed=3<<16;
        enemy.coreFlags=flags[flag];enemy.field04.bytes.kind=1;enemy.lowAndEntityId.bytes.low=2;
        enemy.hpAlive=200000;enemy.defense=defenses[defense];enemy.statusFlags2=statuses[status];
        Psyz_Srand(seeds[seed]);Battle_StepEnemyMovement(&entity);
        HASH_DAMAGE(enemy.coreFlags,4);HASH_DAMAGE(enemy.statusFlags2,4);HASH_DAMAGE(enemy.hpAlive,4);
        HASH_DAMAGE(entity.entityFlags,4);HASH_DAMAGE(entity.actionMode,1);HASH_DAMAGE(entity.animLastFrame,1);
        HASH_DAMAGE(entity.animFrame,4);HASH_DAMAGE(entity.animPrev.fixed,4);
        HASH_DAMAGE(entity.actionData==&animation,1);HASH_DAMAGE(Psyz_RandState(),4);
    }
#undef HASH_DAMAGE
    g_ActiveActor=old_actor;g_GameState.bank_rows[0][2]=old_animation;g_BattleTargetCount=old_count;Psyz_Srand(old_random);
    if(hash!=UINT64_C(0x19758ebd45a2608d)) {
        fprintf(stderr,"Original enemy damage hash %016llx differs from USA MIPS\n",(unsigned long long)hash);return 0;
    }
    puts("Original enemy damage: 147,456 stat/PE/shot/resistance/RNG cases match USA MIPS including real soft-float callees");
    {
        /* The reverse damage path is a separate original function. Keep its
         * USA CPU oracle separate from damage dealt to an enemy above. The
         * hit-animation guard leaves the real damage, RNG and barrier paths
         * running without requiring a loaded room's animation/effect banks. */
        extern void Entity_ResolveDropTable(void *);
        FieldActor *saved_player=g_PlayerEntity;
        BattleEntity player={0};BattleAttributes attributes={0};EnemyActionEffect effect={0};
        const u16 player_stats[]={0,4,123,32767,65535},powers[]={0,30,100,65535};
        const u32 player_flags[]={0x2000,0x2100,0x3000,0x3100,0x2200,0x2300,0x3200,0x3300};
        const u32 parameters[]={0,17|(91<<10)|(30<<20),1023|(1023<<10)|(255<<20)};
        const s32 pe_caps[]={234<<16,0x20000000,0x7fffffff};
        const u8 selected_effects[]={0,2,3,7},chances[]={0,70,255};
        const s32 gauges[]={-1,0,1};
        uint64_t player_hash=UINT64_C(14695981039346656037);
        unsigned player_cases=0;
        g_ActiveActor=&actor;g_PlayerEntity=(FieldActor *)&player;
        for(unsigned stat=0;stat<5;++stat)for(unsigned flag=0;flag<8;++flag)
        for(unsigned selected=0;selected<4;++selected)for(unsigned category=0;category<3;++category)
        for(unsigned power=0;power<4;++power)for(unsigned parameter=0;parameter<3;++parameter)
        for(unsigned chance=0;chance<3;++chance)for(unsigned gauge=0;gauge<3;++gauge)
        for(unsigned initial=0;initial<2;++initial)for(unsigned cap=0;cap<3;++cap) {
            memset(&actor,0,sizeof(actor));memset(&enemy,0,sizeof(enemy));
            memset(&entity,0,sizeof(entity));memset(&player,0,sizeof(player));
            memset(&effect,0,sizeof(effect));attributes.parameterWord.raw=parameters[parameter];
            actor.attributes=&attributes;actor.curHP=83;actor.hpMirror=83;actor.maxHP=189;
            actor.stat20=(s16)player_stats[stat];actor.stateFlags=player_flags[flag];
            actor.atbGauge=gauges[gauge];actor.exp_or_acc=100<<16;actor.maxAtk=pe_caps[cap];
            entity.core=&enemy;enemy.coreFlags=(u32)selected_effects[selected]<<21;
            enemy.effect=&effect;enemy.effectChance=chances[chance];
            effect.state=1;effect.power=powers[power];effect.category=category;
            player.core=&actor;player.entityFlags=0x100;player.actionMode=4;
            Psyz_Srand(seeds[initial]);Entity_ResolveDropTable(&entity);
            const u32 values[]={actor.curHP,(u32)actor.exp_or_acc,(u32)actor.stateFlags,effect.state,Psyz_RandState()};
            const u8 widths[]={2,4,4,1,4};
            for(unsigned word=0;word<5;++word)for(unsigned byte=0;byte<widths[word];++byte)
                player_hash=(player_hash^((values[word]>>(byte*8))&255))*UINT64_C(1099511628211);
            ++player_cases;
        }
        g_PlayerEntity=saved_player;g_ActiveActor=old_actor;Psyz_Srand(old_random);
        if(player_cases!=311040 || player_hash!=UINT64_C(0x10c0a3385de763dd)) {
            fprintf(stderr,"Original player damage: %u cases, hash %016llx differs from USA MIPS\n",player_cases,(unsigned long long)player_hash);return 0;
        }
        puts("Original player damage: 311,040 unhooked USA CPU defense/critical/RNG/barrier/HP/PE-wrap cases passed (hit animation and status effects excluded)");
    }
    return 1;
}

static int original_enemy_facing(void)
{
    Combatant actor={0};EnemyCombatant enemy;BattleEntity entity,player;
    Combatant *old_actor=g_ActiveActor;FieldActor *old_player=g_PlayerEntity;
    void *old_animations[]={g_GameState.bank_rows[0][0],g_GameState.bank_rows[0][1]};
    RenderAnimationDataHeader animation={0};animation.frame_count=9;
    g_GameState.bank_rows[0][0]=g_GameState.bank_rows[0][1]=&animation;
    g_ActiveActor=&actor;g_PlayerEntity=(FieldActor *)&player;
    const s16 positions[][2]={{100,100},{-100,100},{100,-100},{-100,-100},{0,0}};
    const s16 yaws[]={-32768,-1,0,1024,2048,4095,32767};
    uint64_t hash=UINT64_C(14695981039346656037);
#define HASH_FACING(value,bytes) do { \
    u32 word=(u32)(value); \
    for(unsigned byte=0;byte<(bytes);++byte)hash=(hash^((word>>(byte*8))&255))*UINT64_C(1099511628211); \
} while(0)
    for(unsigned kind=0;kind<2;++kind)for(unsigned resume=0;resume<3;++resume)
    for(unsigned motion=0;motion<4;++motion)for(unsigned ability=0;ability<2;++ability)
    for(unsigned event=0;event<2;++event)for(unsigned selected=0;selected<6;++selected)
    for(unsigned knockback=0;knockback<2;++knockback)for(unsigned position=0;position<5;++position)
    for(unsigned yaw=0;yaw<7;++yaw) {
        memset(&enemy,0,sizeof(enemy));memset(&entity,0,sizeof(entity));memset(&player,0,sizeof(player));
        actor.stateFlags=ability*0x80000;entity.core=&enemy;entity.entityFlags=0x240;
        entity.actionMode=9;entity.animLastFrame=5;entity.animFrame=7<<16;entity.animPrev.fixed=3<<16;entity.animStep=0x18000;
        enemy.coreFlags=(selected<<21)|(knockback*0x100000);enemy.field04.bytes.kind=kind;
        enemy.records[selected].state=event;enemy.resumePhase=resume;
        enemy.motionPhase=motion;enemy.motionFrames=7;enemy.motionAmplitude=123;enemy.motionAngle=-100;
        player.posX.fixed=(s32)positions[position][0]*65536;player.posZ.fixed=(s32)positions[position][1]*65536;
        player.facingAngle=yaws[yaw];Battle_UpdateEntityFacing(&entity);
        HASH_FACING(enemy.resumePhase,1);HASH_FACING(enemy.savedActionMode,1);HASH_FACING(enemy.savedInterpolation,1);
        HASH_FACING(enemy.savedAnimFrame,4);HASH_FACING(enemy.savedAnimPrev,4);HASH_FACING(enemy.savedAnimStep,4);
        HASH_FACING(enemy.motionPhase,1);HASH_FACING(enemy.motionFrames,1);
        HASH_FACING(enemy.motionAmplitude,2);HASH_FACING((u16)enemy.motionAngle,2);HASH_FACING(entity.entityFlags,4);
        HASH_FACING(entity.actionMode,1);HASH_FACING(entity.animLastFrame,1);HASH_FACING(entity.animFrame,4);
        HASH_FACING(entity.animPrev.fixed,4);HASH_FACING(entity.animStep,4);
        HASH_FACING(entity.posX.fixed,4);HASH_FACING(entity.posZ.fixed,4);HASH_FACING(entity.actionData==&animation,1);
    }
#undef HASH_FACING
    g_ActiveActor=old_actor;g_PlayerEntity=old_player;
    g_GameState.bank_rows[0][0]=old_animations[0];g_GameState.bank_rows[0][1]=old_animations[1];
    if(hash!=UINT64_C(0x6496fbe76c786105)) {
        fprintf(stderr,"Original enemy facing hash %016llx differs from USA MIPS\n",(unsigned long long)hash);return 0;
    }
    puts("Original enemy hit reaction: 40,320 kind/record/phase/position/facing cases match unhooked USA MIPS");
    return 1;
}

static int original_enemy_slots(PeDisc *disc,PeDiscFile *exe)
{
    extern void Battle_SlotFree(BattleEntity *);
    u8 original[0xD8],native[0xD8];
    if(!pe_disc_read(disc,exe,0x109b0-0x10000+0x800,original,sizeof(original)))return 0;
    memcpy(native,&g_EnemyCombatantInitial,0x18);memset(native+0x18,0,4);
    memcpy(native+0x1c,(const u8 *)&g_EnemyCombatantInitial+PE1_OFFSETOF(EnemyCombatant,records),0xD8-0x1c);
    if(memcmp(native,original,sizeof(native)) || g_EnemyCombatantInitial.effect ||
       g_EnemyCombatantInitial.action || g_EnemyCombatantInitial.attributes || g_EnemyCombatantInitial.field70)return 0;
    BattleEnemySlot old_slots[7];memcpy(old_slots,g_BattleEnemySlots,sizeof(old_slots));
    u8 old_counter=g_EntityInstanceCounter;s8 old_live=g_BattleLiveEnemyCount;
    void *old_animation=g_GameState.bank_rows[0][2];
    RenderAnimationDataHeader animation={0};animation.frame_count=9;
    g_GameState.bank_rows[0][2]=&animation;
    BattleEntity actors[8];const u8 starts[]={0,127,254,255};
    FieldActor *saved_actor=g_CurrentEntity;ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    if(g_ScriptQueueOpcodeHandlers[111]!=Script_InitEntityMoveState)return 0;
    uint64_t hash=UINT64_C(14695981039346656037);
#define HASH_SLOT(value,bytes) do { \
    u32 word=(u32)(value); \
    for(unsigned byte=0;byte<(bytes);++byte)hash=(hash^((word>>(byte*8))&255))*UINT64_C(1099511628211); \
} while(0)
#define HASH_SLOT_ACTOR(index) do { \
    BattleEntity *actor=&actors[index];EnemyCombatant *core=actor->core; \
    if(core) { \
        memcpy(native,core,0x18);u32 pointer=(core->effect==&core->records[0]);memcpy(native+0x18,&pointer,4); \
        memcpy(native+0x1c,(const u8 *)core+PE1_OFFSETOF(EnemyCombatant,records),0xD8-0x1c); \
        for(unsigned byte=0;byte<sizeof(native);++byte)HASH_SLOT(native[byte],1); \
    } \
    HASH_SLOT(core!=NULL,1);HASH_SLOT(actor->entityFlags,4);HASH_SLOT(actor->actionMode,1); \
    HASH_SLOT(actor->animLastFrame,1);HASH_SLOT(actor->animFrame,4);HASH_SLOT(actor->animPrev.fixed,4); \
    HASH_SLOT(actor->actionData==&animation,1);HASH_SLOT(g_EntityInstanceCounter,1);HASH_SLOT((u8)g_BattleLiveEnemyCount,1); \
} while(0)
    for(unsigned mask=0;mask<128;++mask)for(unsigned start=0;start<4;++start) {
        Battle_ClearMotionTable();g_EntityInstanceCounter=starts[start];g_BattleLiveEnemyCount=0;
        memset(actors,0,sizeof(actors));
        for(unsigned i=0;i<8;++i) {
            actors[i].entityFlags=0x240|(((mask>>i)&1)*0x2000);actors[i].actionMode=9;actors[i].animLastFrame=5;
            actors[i].animFrame=7<<16;actors[i].animPrev.fixed=3<<16;
            if(i&1){
                u32 program[]={111,0,32,0};ScriptNode node={.current.script=program,.active=1,.flags=0x80};
                g_CurrentEntity=(FieldActor *)&actors[i];g_ScriptNodePool=&node;Script_RunQueue();
                if(node.current.script!=program+4 || node.flags!=0x90 || node.active || g_ScriptNodePool)return 0;
            }else Entity_AllocSlot(&actors[i]);
            HASH_SLOT_ACTOR(i);
        }
        for(unsigned i=1;i<7;i+=2)Battle_SlotFree(&actors[i]);
        for(unsigned i=0;i<7;++i) {HASH_SLOT(g_BattleEnemySlots[i].active,4);HASH_SLOT(actors[i].core!=NULL,1);}
        for(unsigned i=1;i<7;i+=2) {
            actors[i].entityFlags^=0x2000;Entity_AllocSlot(&actors[i]);HASH_SLOT_ACTOR(i);
        }
        Battle_ClearMotionTable();
        for(unsigned i=0;i<7;++i) {HASH_SLOT(g_BattleEnemySlots[i].active,4);HASH_SLOT(actors[i].core!=NULL,1);}
    }
#undef HASH_SLOT_ACTOR
#undef HASH_SLOT
    memcpy(g_BattleEnemySlots,old_slots,sizeof(old_slots));g_EntityInstanceCounter=old_counter;g_BattleLiveEnemyCount=old_live;
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    g_GameState.bank_rows[0][2]=old_animation;
    if(hash!=UINT64_C(0x81c2f5af25011535)) {
        fprintf(stderr,"Original enemy-slot hash %016llx differs from USA MIPS\n",(unsigned long long)hash);return 0;
    }
    puts("Original enemy slots: complete template and 5,632 allocations, opcode 111 dispatcher, exhaustion, reuse, ID wrapping and clears match unhooked USA MIPS");
    return 1;
}

static int original_player_turn(void)
{
    extern void Battle_UpdatePlayerTurn(void);
    const u8 modes[]={0,4,6,7,8,9,10,11,12,13,15,17};
    const s16 heights[]={-900,-1,0,1,900};
    Combatant actor={0};EnemyCombatant enemy={0},dead_enemy={0};BattleEntity player,target,dead_target={0};
    dead_target.core=&dead_enemy;
    const u8 frames[]={0,7,8},indices[]={0,3,11,42};
    BattleAction action={0};RenderAnimationDataHeader animation={0};animation.frame_count=9;
    void *old_animations[48];memcpy(old_animations,g_GameState.bank_rows[0],sizeof(old_animations));
    BattleInitSlot old_queue[45];memcpy(old_queue,g_BattleActionQueue,sizeof(old_queue));
    Combatant *old_actor=g_ActiveActor;FieldActor *old_player=g_PlayerEntity;
    u32 old_flags=g_GameStateFlags;u8 old_count=g_BattleActionQueueCount,old_index=g_BattleActionIndex;
    u8 old_remaining=g_BattleRemainingActions,old_hit_timer=g_BattleHitTimer;
    u8 old_modes[4];memcpy(old_modes,g_BattleActionAnimModes,sizeof(old_modes));
    int old_attack=g_BattleAttackPmSlot,old_impact=g_BattleImpactPmSlot;
    s16 old_height=g_BattlePlayerAimHeight;
    for(unsigned i=0;i<48;++i)g_GameState.bank_rows[0][i]=&animation;
    g_ActiveActor=&actor;g_PlayerEntity=(FieldActor *)&player;
    action.actionCode.actionId=8;action.turnWord=1;actor.action=&action;
    actor.actionMode12=4;actor.actionMode13=5;
    actor.targetAngleActionModes[0]=6;actor.targetAngleActionModes[1]=8;actor.targetAngleActionModes[2]=10;
    enemy.hpAlive=100;
    uint64_t hash=UINT64_C(14695981039346656037);
#define HASH_TURN(value,bytes) do { \
    u32 word=(u32)(value); \
    for(unsigned byte=0;byte<(bytes);++byte)hash=(hash^((word>>(byte*8))&255))*UINT64_C(1099511628211); \
} while(0)
    for(unsigned mode=0;mode<12;++mode)for(unsigned phase=0;phase<3;++phase)
    for(unsigned height=0;height<5;++height)for(unsigned committed=0;committed<2;++committed)
    for(unsigned position=0;position<4;++position)for(unsigned next_kind=0;next_kind<2;++next_kind) {
        unsigned index=indices[position];
        memset(&player,0,sizeof(player));memset(&target,0,sizeof(target));
        actor.stateFlags=committed*0x200000;player.core=&actor;target.core=&enemy;
        player.actionMode=modes[mode];player.animLastFrame=8;player.animFrame=frames[phase]<<16;
        player.animPrev.fixed=(modes[mode]==12?8:frames[phase])<<16;player.entityFlags=0x240;
        player.posX.fixed=100<<16;player.posZ.fixed=-(20<<16);
        target.renderObject.target_x=200;target.renderObject.target_y=heights[height];target.renderObject.target_z=-200;
        memset(g_BattleActionQueue,0,sizeof(g_BattleActionQueue));
        g_BattleActionQueue[index]=(BattleInitSlot){&target,1,0};
        g_BattleActionQueue[index+1]=(BattleInitSlot){next_kind?&dead_target:NULL,next_kind?1:3,0};
        g_BattleActionIndex=index;g_BattleActionQueueCount=index+1+next_kind;
        for(unsigned i=0;i<4;++i)g_BattleActionAnimModes[i]=4+i;
        g_GameStateFlags=2;g_BattleAttackPmSlot=g_BattleImpactPmSlot=-1;g_BattlePlayerAimHeight=17;
        Battle_UpdatePlayerTurn();
        HASH_TURN(player.actionMode,1);HASH_TURN(player.animLastFrame,1);
        HASH_TURN(player.animFrame,4);HASH_TURN(player.animPrev.fixed,4);
        HASH_TURN((u16)player.facingAngle,2);HASH_TURN(player.entityFlags,4);
        HASH_TURN(actor.stateFlags,4);HASH_TURN(g_BattleActionIndex,1);
        for(unsigned i=0;i<4;++i)HASH_TURN(g_BattleActionAnimModes[i],1);
        HASH_TURN(player.actionData==&animation,1);
    }
#undef HASH_TURN
    memcpy(g_GameState.bank_rows[0],old_animations,sizeof(old_animations));
    memcpy(g_BattleActionQueue,old_queue,sizeof(old_queue));
    g_ActiveActor=old_actor;g_PlayerEntity=old_player;g_GameStateFlags=old_flags;
    g_BattleActionQueueCount=old_count;g_BattleActionIndex=old_index;
    g_BattleRemainingActions=old_remaining;g_BattleHitTimer=old_hit_timer;
    memcpy(g_BattleActionAnimModes,old_modes,sizeof(old_modes));
    g_BattleAttackPmSlot=old_attack;g_BattleImpactPmSlot=old_impact;g_BattlePlayerAimHeight=old_height;
    if(hash!=UINT64_C(0x782dd423afe0eac1)) {
        fprintf(stderr,"Original player turn hash %016llx differs from USA MIPS\n",(unsigned long long)hash);return 0;
    }
    puts("Original player turn: 2,880 animation/aim/committed-state/queue cases match unhooked USA MIPS");
    return 1;
}

static int original_combat_angles(void)
{
    extern int Battle_CalcRelativeAngle(BattleEntity *,BattleEntity *);
    const s32 coords[]={0,1,-1,65536,-65536,0x3fffffff,-0x3fffffff,0x12345678,-0x12345678};
    const s16 yaws[]={-32768,-8193,-4096,-1,0,1,1023,2048,4095,4096,8193,32767};
    BattleEntity target={0},viewer={0};uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned x=0;x<9;++x)for(unsigned z=0;z<9;++z)for(unsigned offset=0;offset<9;++offset)
    for(unsigned yaw=0;yaw<12;++yaw) {
        target.posX.fixed=coords[x];target.posZ.fixed=coords[z];
        viewer.posX.fixed=coords[offset];viewer.posZ.fixed=-coords[offset];viewer.facingAngle=yaws[yaw];
        u16 result=Battle_CalcRelativeAngle(&target,&viewer);
        for(unsigned byte=0;byte<2;++byte)hash=(hash^((result>>(byte*8))&255))*UINT64_C(1099511628211);
    }
    /* Unhooked USA 800305C8 and its real SDK ratan2 callee. */
    if(hash!=UINT64_C(0x37f5f1d4d13308e7)) {
        fprintf(stderr,"Original battle angle hash %016llx differs from USA MIPS\n",(unsigned long long)hash);return 0;
    }
    puts("Original relative battle angle: 8,748 fixed-position/facing cases match USA MIPS");
    return 1;
}

/* Actual USA Entity_UpdateTimers 8001F9C4, including its real poison and
 * animation callees, supplies this oracle. Auto medicine/cure are excluded here. */
static int original_combat_timers(void)
{
    extern void Entity_UpdateTimers(void);
    extern unsigned char g_AyaStatusEffectTimer;
    const u16 times[]={0,1,32767,32768,65535,3,50,9000};
    const u16 steps[]={0,1,2,32767,32768,65535,50,9001};
    const u16 hp[]={0,1,50,200,65535,32767,32768,199};
    const u32 energy[]={0,65536,65537,200000,5000000,0x7fffffff,0x80000000,0xffffffff};
    const u8 transient[]={0,1,127,128,255,3,50,90};
    Combatant actor;BattleEntity player;BattleAttributes attributes={0};
    RenderAnimationDataHeader animation={0};animation.frame_count=9;
    void *old_animation=g_GameState.bank_rows[0][4];
    Combatant *old_actor=g_ActiveActor;FieldActor *old_player=g_PlayerEntity;
    u32 old_flags=g_GameStateFlags;int old_slot=g_ActiveDrawSlot,old_lock=D_8009D2E8;
    u16 old_haste=g_ActorEffectFlag100Timer;u8 old_transient=g_AyaStatusEffectTimer;
    uint64_t hash=UINT64_C(14695981039346656037);
    g_GameState.bank_rows[0][4]=&animation;g_ActiveActor=&actor;
    g_PlayerEntity=(FieldActor *)&player;
#define HASH_TIMER(value,bytes) do { \
    u32 word=(u32)(value); \
    for(unsigned byte=0;byte<(bytes);++byte)hash=(hash^((word>>(byte*8))&255))*UINT64_C(1099511628211); \
} while(0)
    for(unsigned low=0;low<256;++low)for(unsigned buffs=0;buffs<16;++buffs)
    for(unsigned slot=0;slot<2;++slot)for(unsigned phase=0;phase<8;++phase) {
        memset(&actor,0,sizeof(actor));memset(&player,0,sizeof(player));
        actor.attributes=&attributes;actor.field04.fieldId04=2;
        actor.exp_or_acc=(s32)energy[phase];actor.curHP=hp[phase];actor.hpMirror=198;
        actor.maxHP=200;actor.maxAtk=100<<16;
        actor.stateFlags=low|((buffs&7)<<8)|((buffs>>3)<<24);
        actor.actionMode12=4;actor.subActionStep=3;actor.subActionCounter=(phase&1)?3:2;
        actor.subActionPeriod=3;actor.statusStep3C=actor.statusStep3E=steps[phase];
        actor.statusTimer40=actor.statusTimer42=actor.statusTimer44=actor.statusTimer46=times[phase];
        player.core=&actor;player.actionMode=(phase&1)?8:17;player.animLastFrame=5;
        player.animFrame=7<<16;player.animPrev.fixed=3<<16;player.entityFlags=0x240;
        player.motionX=100;player.motionY=-200;player.motionZ=300;
        player.renderObject.projected_x=123;player.renderObject.projected_y=234;
        g_GameStateFlags=2;D_8009D2E8=0x1234;g_ActorEffectFlag100Timer=times[phase];
        g_AyaStatusEffectTimer=transient[phase];g_ActiveDrawSlot=slot;
        Entity_UpdateTimers();
        HASH_TIMER(actor.stateFlags,4);HASH_TIMER(actor.curHP,2);HASH_TIMER(actor.hpMirror,2);
        HASH_TIMER(actor.exp_or_acc,4);HASH_TIMER(actor.subActionCounter,1);
        HASH_TIMER(actor.statusTimer40,2);HASH_TIMER(actor.statusTimer42,2);
        HASH_TIMER(actor.statusTimer44,2);HASH_TIMER(actor.statusTimer46,2);
        HASH_TIMER(g_ActorEffectFlag100Timer,2);HASH_TIMER(g_AyaStatusEffectTimer,1);HASH_TIMER(D_8009D2E8,4);
        HASH_TIMER(player.motionX,4);HASH_TIMER(player.motionY,4);HASH_TIMER(player.motionZ,4);
        HASH_TIMER(player.actionMode,1);HASH_TIMER(player.animLastFrame,1);
        HASH_TIMER(player.animFrame,4);HASH_TIMER(player.animPrev.fixed,4);
        HASH_TIMER(player.actionData==&animation,1);
        HASH_TIMER(actor.panelAux_val,2);HASH_TIMER(actor.panelAux_x,2);HASH_TIMER(actor.panelAux_y,2);
        HASH_TIMER(actor.panelAux_timer,1);HASH_TIMER(actor.panelAux_mode,1);
    }
#undef HASH_TIMER
    g_GameState.bank_rows[0][4]=old_animation;g_ActiveActor=old_actor;g_PlayerEntity=old_player;
    g_GameStateFlags=old_flags;g_ActiveDrawSlot=old_slot;D_8009D2E8=old_lock;
    g_ActorEffectFlag100Timer=old_haste;g_AyaStatusEffectTimer=old_transient;
    if(hash!=UINT64_C(0xa1f411039c73e485)) {
        fprintf(stderr,"Original combat timer hash %016llx differs from USA MIPS\n",(unsigned long long)hash);return 0;
    }
    puts("Original combat timers: 65,536 status/buff/phase cases match USA MIPS");
    return 1;
}

static int original_spell_effects(PeDisc *disc,PeDiscFile *executable)
{
    u8 effect_ids[20];
    if(!pe_disc_read(disc,executable,0x1074c-0x10000+0x800,effect_ids,sizeof(effect_ids)) ||
       memcmp(effect_ids,g_BattleActionEffectIds.bytes,sizeof(effect_ids)))return 0;
    u8 sound_ids[40];
    if(!pe_disc_read(disc,executable,0x10760-0x10000+0x800,sound_ids,sizeof(sound_ids)) ||
       memcmp(sound_ids,&g_BattleActionSoundIds,sizeof(sound_ids)))return 0;
    Combatant actor={0};EnemyCombatant target={0};BattleEntity entity={0};BattleAttributes attributes={0};
    RenderAnimationDataHeader animation={0};animation.frame_count=9;
    void *old_animation=g_GameState.bank_rows[0][4];g_GameState.bank_rows[0][4]=&animation;
    Combatant *old_actor=g_ActiveActor;g_ActiveActor=&actor;
    u32 old_flags=g_GameStateFlags,old_random=Psyz_RandState();int old_lock=D_8009D2E8;
    u16 old_timer=g_ActorEffectFlag100Timer;SavePromptBlock old_scan=g_BattleScanPanel;
    const u16 hp[][2]={{45,0},{45,44},{9999,9999},{45,65530}};
    const s32 energy[]={0,100<<16,INT32_MIN};
    const u32 pairs[]={0,0xAAAAA,0x55555,0xFFFFF},seeds[]={0x12345678,0x87654321};
    uint64_t hash=UINT64_C(14695981039346656037);
#define HASH_EFFECT(value,bytes) do { \
    u32 word=(u32)(value); \
    for(unsigned byte=0;byte<(bytes);++byte)hash=(hash^((word>>(byte*8))&255))*UINT64_C(1099511628211); \
} while(0)
    for(unsigned spell=0;spell<20;++spell)for(unsigned status=0;status<256;++status)
    for(unsigned reduced=0;reduced<2;++reduced)for(unsigned pair=0;pair<4;++pair)for(unsigned seed=0;seed<2;++seed) {
        memset(&actor,0,sizeof(actor));memset(&target,0,sizeof(target));memset(&entity,0,sizeof(entity));
        attributes.effectFlags=reduced*0x200;actor.attributes=&attributes;
        actor.maxHP=hp[status/64][0];actor.curHP=hp[status/64][1];actor.exp_or_acc=energy[status%3];
        actor.maxAtk=100<<16;actor.stateFlags=0x12341000|status;
        target.coreFlags=0xA5A55A5A;target.statusFlags2=pairs[pair];entity.core=&target;
        entity.entityFlags=0x240;entity.actionMode=1;entity.animFrame=7<<16;entity.animPrev.fixed=3<<16;
        g_GameStateFlags=2;g_BattleResultFlags=0xA5A55A5A;D_8009D2E8=0x1234;
        g_ActorEffectFlag100Timer=77;g_BattleActiveEntity=NULL;Psyz_Srand(seeds[seed]);
        Battle_ApplySpellEffect(spell,&entity);
        HASH_EFFECT(actor.curHP,2);HASH_EFFECT(actor.exp_or_acc,4);HASH_EFFECT(actor.stateFlags,4);
        HASH_EFFECT(target.coreFlags,4);HASH_EFFECT(g_BattleResultFlags,4);HASH_EFFECT(D_8009D2E8,4);
        HASH_EFFECT(g_ActorEffectFlag100Timer,2);HASH_EFFECT(g_BattleActiveEntity==&entity,1);
        HASH_EFFECT(entity.entityFlags,4);HASH_EFFECT(entity.actionMode,1);HASH_EFFECT(entity.animLastFrame,1);
        HASH_EFFECT(entity.animFrame,4);HASH_EFFECT(entity.animPrev.fixed,4);HASH_EFFECT(entity.actionData==&animation,1);
        HASH_EFFECT(Psyz_RandState(),4);
    }
#undef HASH_EFFECT
    g_GameState.bank_rows[0][4]=old_animation;g_ActiveActor=old_actor;g_GameStateFlags=old_flags;
    D_8009D2E8=old_lock;g_ActorEffectFlag100Timer=old_timer;g_BattleScanPanel=old_scan;Psyz_Srand(old_random);
    /* Actual USA 80024250 and Entity_SetActionMode 8001A680, with only the
     * PS1 BIOS rand service emulated. Animation input is a valid 9-frame header. */
    if(hash!=UINT64_C(0x8ac4e547f4235e4d)) {
        fprintf(stderr,"Original PE effects hash %016llx differs from USA MIPS\n",(unsigned long long)hash);return 0;
    }
    puts("Original PE effects: 81,920 cases for all 20 commands match USA MIPS, including Scan, Confuse animation and PE cost");
    return 1;
}

static int original_save_title_images(void)
{
    int scene=g_SceneDispatchToken,style=g_SaveTitleStyleFlag,stage=g_PlayTimeFrameCounter,size=D_800A5D50;
    SaveRuntimeState runtime=g_SaveRuntimeState;AyaBattleState aya=g_AyaBattleState;
    SaveSjisFormatArguments arguments=g_SaveSjisFormatArguments;u8 title[0x44];memcpy(title,g_SaveTitleBuffer,sizeof(title));
    u8 *cursor=g_SaveIoCursor;uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0,full=0,maximum=0;
    const unsigned seconds[]={0,4930,359999};g_PlayTimeFrameCounter=232;
    for(unsigned room=1;room<=SCENE_ROOM_COUNT;++room){
        char name[8];snprintf(name,sizeof(name),"m%04ui",room);g_SceneDispatchToken=Text_ParseBase32Id((signed char *)name);
        unsigned full_room=0;
        for(unsigned mode=0;mode<2;++mode)for(unsigned t=0;t<3;++t){
            g_SaveTitleStyleFlag=mode;const u8 *data=Save_FormatTitle(6,seconds[t]);unsigned length=0;
            while(length<sizeof(title) && data[length])++length;
            if(length>maximum)maximum=length;if(length==64)full_room=1;
            for(unsigned i=0;i<sizeof(title);++i)hash=(hash^data[i])*UINT64_C(1099511628211);
            ++cases;
        }
        full+=full_room;
    }
    uint64_t title_hash=hash;
    int valid=cases==2700 && hash==UINT64_C(0x89861520334f7fe0) && maximum==64 && full==36;
    g_SceneDispatchToken=0xa8006248;g_SaveTitleStyleFlag=0;
    g_SaveRuntimeState.windows.header.playTimeMinutes=4930;g_AyaBattleState.action=NULL;g_AyaBattleState.attributes=NULL;
    char path[]="bu00:BASLUS-006620000000F";
    if(Save_BuildCardFile(path)!=0)valid=0;
    hash=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<sizeof(MemCardFileHeader);++i)hash=(hash^g_MemCardFileBuffer[i])*UINT64_C(1099511628211);
    if(hash!=UINT64_C(0x8864e135c46b03c4) || memcmp(g_MemCardIconHeader.title,g_SaveTitleBuffer,64))valid=0;
    for(unsigned i=0;i<sizeof(g_MemCardIconHeader.reserved);++i)if(g_MemCardIconHeader.reserved[i])valid=0;
    g_SceneDispatchToken=scene;g_SaveTitleStyleFlag=style;g_PlayTimeFrameCounter=stage;D_800A5D50=size;
    g_SaveRuntimeState=runtime;g_AyaBattleState=aya;g_SaveSjisFormatArguments=arguments;g_SaveIoCursor=cursor;
    memcpy(g_SaveTitleBuffer,title,sizeof(title));
    if(!valid){fprintf(stderr,"Original save titles/header differ from USA CPU: cases=%u title=%016llx max=%u full=%u header=%016llx\n",cases,(unsigned long long)title_hash,maximum,full,(unsigned long long)hash);return 0;}
    puts("Original save titles: 2700 USA CPU states across all450 room names, 36 full64-byte titles and complete M064 card header passed");return 1;
}

static void original_card_update_crc(u8 *image) {
    memset(image + 0x1C8C, 0, 4);
    u16 crc = 0xFFFF;
    for (unsigned i = 0; i < 0x2000; ++i) {
        crc ^= (u16)image[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (u16)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    u32 stored = crc ^ 0xFFFFu;
    memcpy(image + 0x1C8C, &stored, 4);
}
#ifdef PE_MODS
static unsigned card_read_hooks;
static int card_read_hook(void *ctx, void *user) {
    (void)user;
    if (!g_MemCardLoadSucceeded || memcmp(&g_SaveRuntimeState, (u8 *)ctx + 0x100, sizeof(g_SaveRuntimeState)))
        card_read_hooks += 1000;
    ++card_read_hooks;
    return 0;
}
#endif

static int native_save_hook_safety(void) {
    memset(g_MemCardFileBuffer, 0, sizeof(g_MemCardFileBuffer));
    SaveRuntimeState *candidate = (SaveRuntimeState *)(g_MemCardFileBuffer + 0x100);
    candidate->windows.header.saveName[0] = 0xFF;
    candidate->windows.header.ayaName[0] = 0xFF;
    candidate->player.aya.inventory_slot_count = 50;
    candidate->player.aya.equipped_weapon_slot = -1;
    candidate->player.aya.equipped_armor_slot = -1;
    g_SaveMetadataWindowIndex = 0;
    original_card_update_crc(g_MemCardFileBuffer);
#ifdef PE_MODS
    PeHookInit init = {0, "/tmp/pe-save-hook-test/parasite-eve.toml"};
    pe_host_init(&init);
    if (pe_hook_add(PE_HOOK_SAVE_READ, card_read_hook, NULL, 0, "host")) return 0;
    card_read_hooks = 0;
#endif
    MenuWidget_InitPool(); Menu_CreateMemCardProgressWidget(0);
    Save_LoadCardFileIntoRuntime();
    if (!g_MemCardLoadSucceeded) return 0;
    SaveRuntimeState before = g_SaveRuntimeState;
    for (unsigned fault = 0; fault < 3; ++fault) {
        u32 link = fault == 0 ? 0x12345678 : 0;
        memcpy(g_MemCardFileBuffer + 0x13E4 + 0x818 + 0x68, &link, 4);
        if (fault == 2) candidate->player.aya.equipped_weapon_slot = 50;
        original_card_update_crc(g_MemCardFileBuffer);
        if (fault == 1) g_MemCardFileBuffer[0x100] ^= 1;
        MenuWidget_InitPool(); Menu_CreateMemCardProgressWidget(0);
        Save_LoadCardFileIntoRuntime();
        if (g_MemCardLoadSucceeded || memcmp(&before, &g_SaveRuntimeState, sizeof(before))) return 0;
#ifdef PE_MODS
        if (card_read_hooks != 1) return 0;
#endif
    }
    MenuWidget_InitPool(); g_MenuDeferredCallback = NULL;
    puts("Save load transaction: CRC/link/inventory failures preserve runtime; read hooks observe committed state exactly once");
    return 1;
}

static int original_save_card_images(PeDisc *disc)
{
    PeDiscFile archive,exe;
    if(!pe_disc_find(disc,"PE.IMG",&archive) || !pe_disc_find(disc,"SLUS_006.62",&exe)) return 0;
    CdRom_BindImage(disc,&archive);
    if(Asset_OpenPeImage()!=0 || !original_save_title_images()) return 0;
    MenuWidgetSimpleDescriptor simple[0x41];MenuWidgetGridDescriptor grid[0x41];
    if(!pe_disc_read(disc,&exe,0x92478-0x10000+0x800,simple,sizeof(simple)) ||
       !pe_disc_read(disc,&exe,0x92888-0x10000+0x800,grid,sizeof(grid)) ||
       memcmp(simple,g_MenuWidgetSimpleDescriptors,sizeof(simple)) ||
       memcmp(grid,g_MenuWidgetGridDescriptors,sizeof(grid))) return 0;
    /* Complete USA 80040B80 execution with retail bzero/memcpy/strcpy
     * replaced by their ordinary memory operations in the MIPS reference. */
    const uint64_t hashes[]={UINT64_C(0x936685394f80bede),UINT64_C(0xe5f81aab0f7bf953),UINT64_C(0xb61aaf5cd3ece884),UINT64_C(0x7c2fb8b4765a32aa),UINT64_C(0xcec572cd58919327),UINT64_C(0x35824b0bdf247d70),UINT64_C(0x7dce7284bb2cb877),UINT64_C(0xe89666a7d248ca42),UINT64_C(0x65bcde6175fd7dd),UINT64_C(0x38786e1cec33fc5b),UINT64_C(0x6b73adf727616b9e),UINT64_C(0x306ce1fd0927f959),UINT64_C(0x982926f768839710),UINT64_C(0x73a40725c205f9fa),UINT64_C(0xf21eb1b1eef2a01c),UINT64_C(0x5cd836de2ed5054c),UINT64_C(0x85fc33ce4d3ac0d6),UINT64_C(0x2e35122d654262a8),UINT64_C(0xd0abcde83a45c645),UINT64_C(0x2456ac875900e287),UINT64_C(0xa1789c690daf8765),UINT64_C(0x4125cc56ff810af9),UINT64_C(0xe416e4a8fa0d45b3),UINT64_C(0xba006f1659b2ff21)};
    GameTimerEntry saved_timers[4];memcpy(saved_timers,g_GameTimers,sizeof(saved_timers));
    ItemDataRecord saved_keys[9];memcpy(saved_keys,g_KeyItemRecords,sizeof(saved_keys));
    u32 saved_blend[2];memcpy(saved_blend,g_DrawBlendColorPair,sizeof(saved_blend));
    DISPENV saved_display[2];memcpy(saved_display,g_RenderDispEnvArray,sizeof(saved_display));
    TextboxNameGlyphs saved_name=g_TextboxNameGlyphs;
    MenuWidgetSavedLayout saved_layouts[72];memcpy(saved_layouts,g_MenuWidgetColumnLayoutTable,sizeof(saved_layouts));
    int saved_layout_mode=MenuWidget_GetColumnLayoutMode(),saved_context=g_MenuItemContextFlag;
    int saved_window=g_SaveMetadataWindowIndex,saved_success=g_MemCardLoadSucceeded;
    unsigned saved_submenu=g_MenuActionSubmenuSel;
    SaveRuntimeState saved_runtime=g_SaveRuntimeState;
    Pe1GameState saved_state=g_GameState;
    AyaBattleState saved_aya=g_AyaBattleState;
    BattleStateTail saved_tail=g_SavedBattleStateTail;
    BattleAttributes saved_attributes=g_BattleEquipStateBlock;
    u32 saved_variables[512];memcpy(saved_variables,g_ScriptVariableBank,sizeof(saved_variables));
    int saved_move=D_8009D2E8,saved_token=g_SceneDispatchToken,saved_style=g_SaveTitleStyleFlag;
    u32 saved_flags=g_GameStateFlags;
    u8 saved_fade=g_ScreenTint.fadeMode,*saved_cursor=g_SaveIoCursor;
    SaveSjisFormatArguments saved_arguments=g_SaveSjisFormatArguments;
    u8 title[0x44];memcpy(title,g_SaveTitleBuffer,sizeof(title));
    int valid=&g_PlayTimeFrameCounter==g_ScriptVariableBank+74;
    const unsigned seconds[]={0,3661,359999},play[]={0,400,800};
    unsigned index=0;
    for(unsigned style=0;style<2;++style) for(unsigned links=0;links<4;++links)
    for(unsigned t=0;t<3;++t,++index) {
        for(size_t i=0;i<sizeof(g_SaveRuntimeState);++i)((u8 *)&g_SaveRuntimeState)[i]=(u8)(i*17+3);
        g_SaveRuntimeState.windows.header.playTimeMinutes=seconds[t];
        for(unsigned i=0;i<0x800;++i)((u8 *)g_ScriptVariableBank)[i]=(u8)(i*13+9);
        g_PlayTimeFrameCounter=play[t];
        for(unsigned i=0;i<16;++i)((u8 *)&g_GameState)[i]=(u8)(i*11+0x55);
        g_GameStateFlags=0xA5A55A5A;D_8009D2E8=0x01020309;g_SceneDispatchToken=0xCD4002A8;
        g_ScreenTint.fadeMode=0xD3;
        u8 aya[0x70];for(unsigned i=0;i<sizeof(aya);++i)aya[i]=(u8)(i*7+19);
        memset(&g_AyaBattleState,0,sizeof(g_AyaBattleState));
        memcpy(g_AyaBattleState.words,aya,0x1C);
        memcpy((u8 *)g_AyaBattleState.words+PE1_OFFSETOF(Combatant,maxHP),aya+0x1C,0x4C);
        g_AyaBattleState.action=links&1?&g_SavedBattleStateTail:NULL;
        g_AyaBattleState.attributes=links&2?&g_BattleEquipStateBlock:NULL;
        for(unsigned i=0;i<sizeof(g_SavedBattleStateTail);++i)((u8 *)&g_SavedBattleStateTail)[i]=(u8)(i*5+23);
        for(unsigned i=0;i<sizeof(g_BattleEquipStateBlock);++i)((u8 *)&g_BattleEquipStateBlock)[i]=(u8)(i*3+31);
        AyaBattleState expected_aya=g_AyaBattleState;
        g_SaveTitleStyleFlag=style;
        char path[]="bu00:BASLUS-006620000001O";
        if(Save_BuildCardFile(path)!=0 || g_SaveIoCursor!=g_MemCardFileBuffer+0x1C90)valid=0;
        uint64_t hash=UINT64_C(14695981039346656037);
        for(unsigned i=0;i<sizeof(g_MemCardFileBuffer);++i)hash=(hash^g_MemCardFileBuffer[i])*UINT64_C(1099511628211);
        if(hash!=hashes[index]) {
            fprintf(stderr,"Original save image %u hash %016llx differs from USA MIPS\n",index,(unsigned long long)hash);valid=0;
        }
        /* Independent bitwise CRC, with its four on-disc bytes excluded. */
        u16 crc=0xFFFF;
        for(unsigned i=0;i<sizeof(g_MemCardFileBuffer);++i) {
            unsigned value=i>=0x1C8C && i<0x1C90?0:g_MemCardFileBuffer[i];
            crc^=value<<8;
            for(unsigned b=0;b<8;++b)crc=(u16)((crc&0x8000)?(crc<<1)^0x1021:crc<<1);
        }
        u32 recorded;memcpy(&recorded,g_MemCardFileBuffer+0x1C8C,4);
        if(recorded!=(u32)(crc^0xFFFF))valid=0;
        static u8 tail[0x8A8+16];memset(tail,0xA5,sizeof(tail));
        g_SaveIoCursor=tail+1;
        if(Save_SerializeTail()!=0 || g_SaveIoCursor!=tail+1+0x8A8 ||
           memcmp(tail+1,g_MemCardFileBuffer+0x13E4,0x8A8))valid=0;
        for(unsigned i=1+0x8A8;i<sizeof(tail);++i)if(tail[i]!=0xA5)valid=0;
        if(tail[0]!=0xA5)valid=0;
        u8 expected_fields[15];memcpy(expected_fields,&g_GameState,sizeof(expected_fields));
        BattleStateTail expected_tail=g_SavedBattleStateTail;BattleAttributes expected_attributes=g_BattleEquipStateBlock;
        memset(g_ScriptVariableBank,0xA5,0x800);memset(&g_AyaBattleState,0xA5,sizeof(g_AyaBattleState));
        memset((u8 *)&g_GameState+4,0xA5,11);g_GameStateFlags=0xFFFFFFFF;
        D_8009D2E8=-1;g_SceneDispatchToken=0;g_ScreenTint.fadeMode=0;
        memset(&g_SavedBattleStateTail,0xA5,sizeof(g_SavedBattleStateTail));
        memset(&g_BattleEquipStateBlock,0xA5,sizeof(g_BattleEquipStateBlock));
        g_SaveIoCursor=tail+1;
        if(Save_DeserializeTail()!=0 || g_SaveIoCursor!=tail+1+0x8A8 ||
           memcmp(&g_AyaBattleState,&expected_aya,sizeof(expected_aya)) ||
           memcmp(g_ScriptVariableBank,g_MemCardFileBuffer+0x13E4,0x800) ||
           memcmp(expected_fields,&g_GameState,sizeof(expected_fields)) ||
           memcmp(&g_SavedBattleStateTail,&expected_tail,sizeof(expected_tail)) ||
           memcmp(&g_BattleEquipStateBlock,&expected_attributes,sizeof(expected_attributes)) ||
           (u32)g_SceneDispatchToken!=0xCD4002A8u || g_ScreenTint.fadeMode!=0xD3 ||
           g_GameStateFlags!=(0xA5A55A5Au&0xFFFF2679u) || D_8009D2E8!=0x01020300)valid=0;
        Pe1GameState before_state=g_GameState;AyaBattleState before_aya=g_AyaBattleState;
        u32 before_variables[512];memcpy(before_variables,g_ScriptVariableBank,sizeof(before_variables));
        u32 bad=0x12345678;memcpy(tail+1+0x818+0x68,&bad,4);g_SaveIoCursor=tail+1;
        if(Save_DeserializeTail()!=-1 || g_SaveIoCursor!=tail+1 ||
           memcmp(&g_GameState,&before_state,sizeof(before_state)) || memcmp(&g_AyaBattleState,&before_aya,sizeof(before_aya)) ||
           memcmp(g_ScriptVariableBank,before_variables,sizeof(before_variables)))valid=0;
        g_AyaBattleState.action=(BattleStateTail *)(uintptr_t)1;g_SaveIoCursor=tail+1;
        memset(tail,0xA5,sizeof(tail));
        if(Save_SerializeTail()!=-1 || g_SaveIoCursor!=tail+1)valid=0;
        for(unsigned i=0;i<sizeof(tail);++i)if(tail[i]!=0xA5)valid=0;
    }
    /* Execute the complete original loader, including real modal widgets.
     * Synthetic image payloads deliberately exercise 32-bit timer wrapping. */
    /* The byte-pattern fixtures above test lossless serialization, not valid
     * inventory contents. Give the loader a structurally valid inventory. */
    SaveRuntimeState *serialized = (SaveRuntimeState *)(g_MemCardFileBuffer + 0x100);
    memset(&serialized->player, 0, sizeof(serialized->player));
    serialized->player.aya.inventory_slot_count = 50;
    serialized->player.aya.equipped_weapon_slot = -1;
    serialized->player.aya.equipped_armor_slot = -1;
    original_card_update_crc(g_MemCardFileBuffer);
#ifdef PE_MODS
    static int hook_registered;
    if (!hook_registered) {
        PeHookInit init = {0, "/tmp/pe-save-hook-test/parasite-eve.toml"};
        pe_host_init(&init);
        if (pe_hook_add(PE_HOOK_SAVE_READ, card_read_hook, NULL, 0, "host")) return 0;
        hook_registered = 1;
    }
    card_read_hooks = 0;
#endif
    static u8 image[0x2000];memcpy(image,g_MemCardFileBuffer,sizeof(image));
    int queued=g_AkaoMessageQueueCount;
    g_SaveMetadataWindowIndex=0;
    MenuWidget_InitPool();Menu_CreateMemCardProgressWidget(0);
    Save_LoadCardFileIntoRuntime();
    MenuWidgetNode *child=MenuWidget_GetCurrentNode();
    if(!g_MemCardLoadSucceeded || memcmp(&g_SaveRuntimeState,image+0x100,sizeof(g_SaveRuntimeState)) ||
       g_SaveIoCursor!=g_MemCardFileBuffer+0x1C90 || g_MenuDeferredCallback!=Save_CancelUiFlow ||
       MenuWidget_FindByModeAndSelectedBase(1,0x27) || !child || !child->parent ||
       child->parent->selected_base!=0x28 || child->parent->update!=Menu_HandleDeferredCallbackInput ||
       GAME_TIME_COUNTER(2)!=(s32)((u32)g_SaveRuntimeState.windows.header.playTimeMinutes*60u) ||
       GAME_TIME_COUNTER(0)!=(s32)((u32)g_SaveRuntimeState.windows.header.gameTimeMinutes*60u))valid=0;
    for(unsigned i=0;i<6;++i)if(g_KeyItemRecords[i].ammo!=g_SaveRuntimeState.categoryCounts[i])valid=0;
#ifdef PE_MODS
    if (card_read_hooks != 1) valid = 0;
#endif
    const unsigned damaged[]={0,4,0x100,0x13E4,0x1C8C,0x1C90,0x1FFF};
    for(unsigned fault=0;fault<20;++fault) {
        memcpy(g_MemCardFileBuffer,image,sizeof(image));
        if(fault<7) g_MemCardFileBuffer[damaged[fault]]^=0x40;
        else {
            if (fault == 7) {
                u32 bad = 0x12345678; memcpy(g_MemCardFileBuffer + 0x13E4 + 0x818 + 0x68, &bad, 4);
            } else {
                SaveRuntimeState *candidate = (SaveRuntimeState *)(g_MemCardFileBuffer + 0x100);
                AyaSaveState *aya = &candidate->player.aya;
                InventoryRuntime *inv = &candidate->player.runtime.inventory;
                switch (fault) {
                case 8: inv->equipment[0] = (ItemDataRecord){.iconId=1,.itemId=0x78,.kind=255}; break;
                case 9: inv->equipment[0] = (ItemDataRecord){.iconId=1,.itemId=0x78,.kind=1,.tailCount=11}; break;
                case 10: inv->equipment[0] = (ItemDataRecord){.iconId=1,.itemId=0x78,.kind=1,.tailCapacity=255}; break;
                case 11: aya->inventory_items[0] = 0x180; break;
                case 12: inv->storage[99] = -1; break;
                case 13: inv->specialStorage[81] = 0x209; break;
                case 14: aya->inventory_items[0] = 0x100; break;
                case 15: aya->equipped_weapon_slot = -128; break;
                case 16: aya->equipped_armor_slot = 50; break;
                case 17: aya->inventory_slot_count = 51; break;
                case 18: aya->equipped_weapon_slot = 0; break;
                case 19:
                    inv->equipment[0] = (ItemDataRecord){.iconId=1,.itemId=0x78,.kind=1};
                    aya->inventory_items[0] = 0x100; aya->equipped_armor_slot = 0; break;
                }
            }
            original_card_update_crc(g_MemCardFileBuffer);
        }
        SaveRuntimeState before_runtime=g_SaveRuntimeState;Pe1GameState before_game=g_GameState;
        AyaBattleState before_aya=g_AyaBattleState;u32 before_variables[512];
        memcpy(before_variables,g_ScriptVariableBank,sizeof(before_variables));
        u32 before_flags=g_GameStateFlags;int before_lock=D_8009D2E8,before_scene=g_SceneDispatchToken;
        GameTimerEntry timers[4];memcpy(timers,g_GameTimers,sizeof(timers));
        MenuWidget_InitPool();Menu_CreateMemCardProgressWidget(0);g_MemCardLoadSucceeded=1;
        Save_LoadCardFileIntoRuntime();
        child=MenuWidget_GetCurrentNode();
#ifdef PE_MODS
        if (card_read_hooks != 1) valid = 0;
#endif
        if(g_MemCardLoadSucceeded || memcmp(&before_runtime,&g_SaveRuntimeState,sizeof(before_runtime)) ||
           memcmp(&before_game,&g_GameState,sizeof(before_game)) || memcmp(&before_aya,&g_AyaBattleState,sizeof(before_aya)) ||
           memcmp(before_variables,g_ScriptVariableBank,sizeof(before_variables)) ||
           memcmp(timers,g_GameTimers,sizeof(timers)) || g_GameStateFlags!=before_flags || D_8009D2E8!=before_lock ||
           g_SceneDispatchToken!=before_scene || g_MenuDeferredCallback || MenuWidget_FindByModeAndSelectedBase(1,0x27) ||
           !child || !child->parent || child->parent->draw!=Menu_DrawTwoLineDialogText)valid=0;
        const u8 *line0=Text_LookupTable4(0x55),*line1=Text_LookupTable4(0x56);
        unsigned len0=0,len1=0;while(line0[len0]!=0xFF && len0<63)++len0;while(line1[len1]!=0xFF && len1<63)++len1;
        if(memcmp(line0,g_MenuTwoLineDialogText[0],len0+1) || memcmp(line1,g_MenuTwoLineDialogText[1],len1+1))valid=0;
        if(child && child->parent) {
            MenuWidgetNode *parent=child->parent;
            if(parent->update(parent,0x10040)!=1 || g_MenuWidgetActiveListHead || MenuWidget_GetCurrentNode())valid=0;
        }
    }
    memcpy(g_MemCardFileBuffer,image,sizeof(image));MenuWidget_InitPool();Menu_CreateMemCardProgressWidget(0);
    Save_LoadCardFileIntoRuntime();
    if(!g_MemCardLoadSucceeded || memcmp(image,g_MemCardFileBuffer,sizeof(image)))valid=0;
#ifdef PE_MODS
    if (card_read_hooks != 2) valid = 0;
#endif
    MenuWidget_InitPool();g_MenuDeferredCallback=NULL;g_AkaoMessageQueueCount=queued;
    if(!original_card_file_services(g_MemCardFileBuffer))valid=0;
    /* A valid New Game image can also finish the original success callback.
     * At the title screen no field actor exists yet; scene spawn follows later. */
    FieldActor *saved_player=g_PlayerEntity;g_PlayerEntity=NULL;
    int scale[7];memcpy(scale,g_StatScaleBase,sizeof(scale));
    int saved_bonus=g_InvBonusSlotCount,saved_limit=g_InvSlotLimit;
    s16 *saved_items=g_InvItemPtr,*saved_override=g_InvActiveListOverride;
    int saved_override_limit=g_InvOverrideSlotLimit,saved_bit_words=g_InvSelectionBitWords;
    u32 *saved_bits=g_InvSelectionBits;
    Inv_BuildItemGridFromCategory();Save_InitMetadataState();Save_BuildHeader();
    char path[]="bu00:BASLUS-006620000001O";
    if(Save_BuildCardFile(path)!=0)valid=0;
    SaveRuntimeState initialized=g_SaveRuntimeState;
    memset(&g_SaveRuntimeState,0xA5,sizeof(g_SaveRuntimeState));
    MenuWidget_InitPool();Menu_CreateMemCardProgressWidget(0);Menu_ClearCommandResult();
    Save_LoadCardFileIntoRuntime();child=MenuWidget_GetCurrentNode();
    if(!g_MemCardLoadSucceeded || memcmp(&initialized,&g_SaveRuntimeState,sizeof(initialized)) ||
       !child || !child->parent || g_MenuDeferredCallback!=Save_CancelUiFlow)valid=0;
    else {
        MenuWidgetNode *parent=child->parent;
        if(parent->update(parent,0)!=1 || !g_MenuDeferredCallback || Menu_GetCommandResult()!=0)valid=0;
        if(parent->update(parent,0x10040)!=1 || g_MenuDeferredCallback || Menu_GetCommandResult()!=2 ||
           MenuWidget_GetCurrentNode() || g_MenuWidgetActiveListHead || g_InvItemPtr!=g_AyaInventoryItems ||
           g_AyaSaveState.max_hp!=Aya_LookupLevelStats(0)->hp ||
           g_AyaEquippedWeaponSlot[0]!=0 || g_AyaEquippedArmorSlot[0]!=1)valid=0;
    }
    Menu_ClearCommandResult();g_AkaoMessageQueueCount=queued;
    /* Exercise the newly linked original screen-adjust callbacks through the
     * actual widget input ABI, including both limits and cancel/confirm. */
    for(int base_y=0;base_y<16;++base_y)for(int direction=-1;direction<=1;direction+=2)
    for(unsigned confirm=0;confirm<2;++confirm) {
        Draw_SetBaseOffsetPosition(0,base_y-Draw_GetBaseY());
        MenuWidget_InitPool();Menu_OpenScreenAdjustView(NULL);
        MenuWidgetNode *node=MenuWidget_GetCurrentNode();
        int expected=base_y+direction;if(expected<0)expected=0;if(expected>15)expected=15;
        if(!node || !node->update)valid=0;
        else {
            if(node->update(node,0)!=1 || Draw_GetBaseY()!=base_y)valid=0;
            if(node->update(node,direction<0?0x1000:0x4000)!=1 || Draw_GetBaseY()!=expected)valid=0;
            if(node->update(node,confirm?0x10000:0x40)!=1 ||
               Draw_GetBaseY()!=(confirm?expected:base_y) || MenuWidget_GetCurrentNode() || g_MenuWidgetActiveListHead)valid=0;
        }
    }
    int paused=g_MenuInputPollingPaused,pending=g_MenuInputPausePending;
    for(unsigned flags=0;flags<8;++flags) {
        g_DiscChangeFlags=flags;MenuInput_SetPollingPaused(1);g_MenuInputPausePending=1;
        Menu_DeferredExec();
        if(g_MenuInputPollingPaused!=((flags&3)!=0) || g_MenuInputPausePending!=((flags&3)!=0))valid=0;
    }
    MenuInput_SetPollingPaused(paused);g_MenuInputPausePending=pending;g_AkaoMessageQueueCount=queued;
    g_PlayerEntity=saved_player;memcpy(g_StatScaleBase,scale,sizeof(scale));
    g_InvBonusSlotCount=saved_bonus;g_InvSlotLimit=saved_limit;g_InvItemPtr=saved_items;
    g_InvActiveListOverride=saved_override;g_InvOverrideSlotLimit=saved_override_limit;
    g_InvSelectionBitWords=saved_bit_words;g_InvSelectionBits=saved_bits;
    memcpy(g_GameTimers,saved_timers,sizeof(saved_timers));memcpy(g_KeyItemRecords,saved_keys,sizeof(saved_keys));
    memcpy(g_DrawBlendColorPair,saved_blend,sizeof(saved_blend));memcpy(g_RenderDispEnvArray,saved_display,sizeof(saved_display));
    g_TextboxNameGlyphs=saved_name;g_MenuItemContextFlag=saved_context;g_MenuActionSubmenuSel=saved_submenu;
    MenuWidget_SetColumnLayoutMode(saved_layout_mode);memcpy(g_MenuWidgetColumnLayoutTable,saved_layouts,sizeof(saved_layouts));
    g_SaveMetadataWindowIndex=saved_window;g_MemCardLoadSucceeded=saved_success;
    g_SaveRuntimeState=saved_runtime;g_GameState=saved_state;g_GameStateFlags=saved_flags;g_AyaBattleState=saved_aya;
    g_SavedBattleStateTail=saved_tail;g_BattleEquipStateBlock=saved_attributes;
    memcpy(g_ScriptVariableBank,saved_variables,sizeof(saved_variables));D_8009D2E8=saved_move;
    g_SceneDispatchToken=saved_token;g_SaveTitleStyleFlag=saved_style;g_ScreenTint.fadeMode=saved_fade;
    g_SaveIoCursor=saved_cursor;g_SaveSjisFormatArguments=saved_arguments;memcpy(g_SaveTitleBuffer,title,sizeof(title));
    if(valid)puts("Original save image: 24 full 8 KiB USA MIPS images, SJIS titles/icons, CRC, unaligned tails and full loader with modal dialogs, CRC/link/inventory rejection, committed-state hooks and retry passed");
    return valid;
}

static unsigned unload_log[32], unload_count;
#define UNLOAD_PROBE(id) static void unload_probe_##id(void) { unload_log[unload_count++]=id; }
UNLOAD_PROBE(0) UNLOAD_PROBE(1) UNLOAD_PROBE(2) UNLOAD_PROBE(3)
UNLOAD_PROBE(4) UNLOAD_PROBE(5) UNLOAD_PROBE(6) UNLOAD_PROBE(7)
UNLOAD_PROBE(8) UNLOAD_PROBE(84) UNLOAD_PROBE(85)
#undef UNLOAD_PROBE

static int original_scene_teardown(void)
{
    struct Archive { SceneAssetBlob header;SceneAssetDirectory directory;SceneAssetRecord entries[8]; } archive={0};
    archive.header.directoryOffset=offsetof(struct Archive,directory);
    archive.directory.entries=offsetof(struct Archive,entries) | 8u<<22;
    const u8 ids[]={0,7,8,84,85,191,255,8};
    for(unsigned i=0;i<8;++i)archive.entries[i].handlerId=ids[i];
    PmCommand handlers[86]={{0}};PmCommand *table[86];
    void (*probes[])(void)={unload_probe_0,unload_probe_1,unload_probe_2,unload_probe_3,
        unload_probe_4,unload_probe_5,unload_probe_6,unload_probe_7};
    for(unsigned i=0;i<8;++i)handlers[i].unload=probes[i];
    handlers[8].unload=unload_probe_8;handlers[84].unload=unload_probe_84;
    handlers[85].unload=unload_probe_85;
    PmTables saved=g_PmTables;u32 flags=g_GameStateFlags;
    SceneAssetView *loaded=g_GameState.loaded_scene_assets;
    void *programs[104];memcpy(programs,g_FieldFxTaskPrograms,sizeof(programs));
    int valid=1;
    for(unsigned mode=0;mode<4;++mode) {
        for(unsigned i=0;i<86;++i)table[i]=&handlers[i];
        for(unsigned i=0;i<104;++i)g_FieldFxTaskPrograms[i]=&table[i%86];
        g_PmTables.handlers=mode==3?NULL:table;
        g_GameStateFlags=0x31415900u | (mode?0x80:0);
        g_GameState.loaded_scene_assets=mode==2?NULL:(SceneAssetView *)&archive;
        unload_count=0;
        int result=Asset_UnloadTableEntries();
        if(mode>=2) {
            if(result!=-1 || unload_count || g_GameStateFlags!=0x31415980u)valid=0;
        } else {
            const unsigned expected[]={0,1,2,3,4,5,6,7,85,8,84,8};
            if(result || g_GameStateFlags!=0x31415900u ||
               unload_count!=(mode?12u:0u) ||
               (mode && memcmp(unload_log,expected,sizeof(expected))))valid=0;
        }
        for(unsigned i=0;i<86;++i)
            if(table[i]!=(mode<2 && i>=8 && i<85?NULL:&handlers[i]))valid=0;
        for(unsigned i=0;i<104;++i)
            if(g_FieldFxTaskPrograms[i]!=(mode<2 && i>=30?NULL:&table[i%86]))valid=0;
    }
    g_PmTables=saved;g_GameStateFlags=flags;g_GameState.loaded_scene_assets=loaded;
    memcpy(g_FieldFxTaskPrograms,programs,sizeof(programs));
    if(!valid)fprintf(stderr,"original scene teardown regression\n");
    else puts("Original scene teardown: native callback order, duplicate/boundary IDs, retained resident handlers, task ranges and missing-state guards passed");
    return valid;
}

static struct {s16 x,y;u8 character;u16 style;} credit_glyphs[4096];
static unsigned credit_glyph_count;
static void credit_record_glyph(s16 x,s16 y,u8 character)
{
    if(credit_glyph_count<4096){credit_glyphs[credit_glyph_count].x=x;credit_glyphs[credit_glyph_count].y=y;
        credit_glyphs[credit_glyph_count].character=character;credit_glyphs[credit_glyph_count].style=g_BootDisplayFontStyle;}
    ++credit_glyph_count;
}
static int original_credit_text(PeDisc *disc)
{
    g_CdImage=disc;glyph_pending=glyph_poll_retry=0;fx_image_io=1;
    if(Asset_OpenPeImage() || CdRom_LoadBootAudio())return fprintf(stderr,"Credits failure at line %d\n",__LINE__),0;
    if(memcmp(g_BootDisplayUppercaseWidths,g_CdLoadAreaBase+0x4f20,104))return fprintf(stderr,"Credits failure at line %d\n",__LINE__),0;
    for(unsigned i=0;i<745;++i){u32 pointer;memcpy(&pointer,g_CdLoadAreaBase+0x4f88+4*i,4);
        if(g_BootDisplayCreditLines[i]!=g_CdLoadAreaBase+(pointer-0x80120d00u))return fprintf(stderr,"Credits failure at line %d\n",__LINE__),0;}
    for(unsigned i=0;i<31;++i){u32 pointer;memcpy(&pointer,g_CdLoadAreaBase+0x4ea4+4*i,4);
        if(g_BootDisplayCreditImages[i]!=g_CdLoadAreaBase+(pointer-0x80120d00u))return fprintf(stderr,"Credits failure at line %d\n",__LINE__),0;}
    PeDiscFile file;
    char *authored_path=g_BootDisplayPlaybackFilePath;
    if(!pe_disc_find(disc,authored_path,&file)){
        if(BootDisplay_InitPlaybackFile()!=-1)return fprintf(stderr,"Credits failure at line %d\n",__LINE__),0;
        puts("Credits XA absent on supplied Disc 1: missing-file failure confirmed; testing extent setup with the real FMV000 stream");
        g_BootDisplayPlaybackFilePath="FMV1/FMV000.STR";
        if(!pe_disc_find(disc,g_BootDisplayPlaybackFilePath,&file))return fprintf(stderr,"Credits failure at line %d\n",__LINE__),0;
    }
    g_BootDisplayPlaybackRingIndex=g_BootDisplayPlaybackAltCount=5;g_BootDisplayPlaybackReadCount=123;
    if(BootDisplay_InitPlaybackFile()!=0 || g_BootDisplayPlaybackStartPos!=(s32)file.sector ||
       g_BootDisplayPlaybackEndPos!=(s32)(file.sector+(file.size>>11)) || g_BootDisplayPlaybackCurrentPos!=(s32)file.sector ||
       g_BootDisplayPlaybackRingIndex || g_BootDisplayPlaybackAltCount || g_BootDisplayPlaybackReadCount ||
       g_BootDisplayPlaybackSectorBuffer!=(char *)g_CdLoadAreaBase+((g_MainAssetLbaTbl[0]-g_BootDisplayDataLbaTbl)*2048+0x34800))return fprintf(stderr,"Credits failure at line %d\n",__LINE__),0;
    g_BootDisplayPlaybackFilePath=authored_path;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0,glyphs=0;
#define CREDIT_PUT(value,bytes) do {uint64_t v_=(uint64_t)(value); \
    for(unsigned b_=0;b_<(bytes);++b_){hash=(hash^(u8)v_)*UINT64_C(1099511628211);v_>>=8;}}while(0)
    const u16 styles[]={0,1,2,3,4,65535};
    for(unsigned style=0;style<6;++style){g_BootDisplayFontStyle=styles[style];
        for(unsigned character=0;character<256;++character){CREDIT_PUT(BootDisplay_GetGlyphIndex(character),1);++cases;}}
    for(unsigned style=0;style<4;++style){g_BootDisplayFontStyle=style;
        for(unsigned character=0;character<256;++character){CREDIT_PUT(BootDisplay_GetGlyphWidth(character),2);++cases;}}
    if(cases!=2560 || hash!=UINT64_C(0x6fa8e01cd1156db8)){fprintf(stderr,"Credits glyphs: %u %016llx\n",cases,(unsigned long long)hash);return fprintf(stderr,"Credits failure at line %d\n",__LINE__),0;}
    BootDisplayCallbacks callbacks={.glyph=credit_record_glyph};g_BootDisplayCallbacks=&callbacks;
    const s32 steps[]={-256,-0x123,0,256,INT32_MAX};const int first_y[]={225,224,223,0,-7,-8,-223};
    hash=UINT64_C(14695981039346656037);cases=0;
    for(unsigned style=0;style<4;++style)for(unsigned position=0;position<132;++position)
    for(unsigned fraction=0;fraction<2;++fraction)for(unsigned step=0;step<5;++step){
        int y=position<7?first_y[position]:224-(int)(position-7)*6*18;
        g_BootDisplayFontStyle=style;g_BootDisplayTextScrollX=(s32)(((u32)y<<8)+(fraction?255:0));
        g_BootDisplayTextScrollStepX=steps[step];credit_glyph_count=0;
        int result=BootDisplay_DrawTextBlock();if(credit_glyph_count>4096)return fprintf(stderr,"Credits failure at line %d\n",__LINE__),0;
        CREDIT_PUT(credit_glyph_count,2);
        for(unsigned i=0;i<credit_glyph_count;++i){CREDIT_PUT(credit_glyphs[i].x,2);CREDIT_PUT(credit_glyphs[i].y,2);
            CREDIT_PUT(credit_glyphs[i].character,1);CREDIT_PUT(credit_glyphs[i].style,2);}
        CREDIT_PUT(result,1);CREDIT_PUT(g_BootDisplayTextScrollX,4);CREDIT_PUT(g_BootDisplayFontStyle,2);
        CREDIT_PUT(g_BootDisplayTextCursor-g_BootDisplayCreditLines,2);
        glyphs+=credit_glyph_count;++cases;
    }
#undef CREDIT_PUT
    g_BootDisplayCallbacks=NULL;
    if(cases!=5280 || glyphs!=656800 || hash!=UINT64_C(0x8bd818222def553d)){fprintf(stderr,"Credits layout: %u glyphs=%u %016llx\n",cases,glyphs,(unsigned long long)hash);return fprintf(stderr,"Credits failure at line %d\n",__LINE__),0;}
    for(unsigned style=4;style<8;++style){g_BootDisplayFontStyle=style;if(BootDisplay_GetGlyphWidth('A')!=0)return fprintf(stderr,"Credits failure at line %d\n",__LINE__),0;}
    for(unsigned fault=1;fault<=10;++fault){
        boot_credit_corruption=fault;
        int status=CdRom_LoadBootAudio();
        if(status!=-4 || boot_credit_corruption || g_BootDisplayPlaybackFilePath){fprintf(stderr,"Credits metadata fault=%u status=%d pending=%u\n",fault,status,boot_credit_corruption);return 0;}
        for(unsigned i=0;i<745;++i)if(g_BootDisplayCreditLines[i])return fprintf(stderr,"Credits failure at line %d\n",__LINE__),0;
        for(unsigned i=0;i<31;++i)if(g_BootDisplayCreditImages[i])return fprintf(stderr,"Credits failure at line %d\n",__LINE__),0;
    }
    if(CdRom_LoadBootAudio()!=0)return fprintf(stderr,"Credits failure at line %d\n",__LINE__),0;
    authored_path=g_BootDisplayPlaybackFilePath;
    g_BootDisplayPlaybackFilePath="FMV1/FMV000.STR";
    if(BootDisplay_InitPlaybackFile()!=0)return fprintf(stderr,"Credits failure at line %d\n",__LINE__),0;
    s32 start=g_BootDisplayPlaybackStartPos,end=g_BootDisplayPlaybackEndPos;
    char *buffer=g_BootDisplayPlaybackSectorBuffer;
    u8 *workspace=g_CdLoadAreaBase;
    for(unsigned fault=0;fault<5;++fault){
        if(fault==0)g_CdImage=NULL;
        if(fault==1)g_BootDisplayPlaybackFilePath="missing.xa";
        if(fault==2)g_BootDisplayPlaybackFilePath="FMV1";
        if(fault==3)g_CdLoadAreaBase=NULL;
        if(fault==4)g_PeImageSectorTable[72]=0;
        if(BootDisplay_InitPlaybackFile()!=(fault<2?-1:-2) || g_BootDisplayPlaybackStartPos!=start ||
           g_BootDisplayPlaybackEndPos!=end || g_BootDisplayPlaybackSectorBuffer!=buffer)return fprintf(stderr,"Credits failure at line %d\n",__LINE__),0;
        g_CdImage=disc;g_BootDisplayPlaybackFilePath="FMV1/FMV000.STR";g_CdLoadAreaBase=workspace;g_PeImageSectorTable[72]=1276;
    }
    g_BootDisplayPlaybackFilePath=authored_path;
    puts("Original credits text: 745 native line bindings, 31 image bindings, image-file extent setup, 2560 USA glyph cases and 5280 layouts/656800 positioned glyphs match");return 1;
}

static int original_credit_sprite_packets(void)
{
    static struct {u64 before;BootDisplaySpritePacket packet;u64 after;} arena;
    static RenderNativeTag ot[16];
    const s16 poses[][2]={{-32768,32767},{-1,-1},{0,0},{320,224},{32767,-32768},{123,-456}};
    const u16 uv[]={0,1,255,256,65535,0x1234};
    const s32 brightness[]={INT32_MIN,-129,-4,-1,0,1,4,124,127,128,129,255,INT32_MAX};
    const u16 timers[]={0,1,2,3,28,29,30,65535};const u32 frames[]={0,1,356,357,358,449,450,UINT32_MAX};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;int valid=1;
#define SPRITE_PUT(value,bytes) do {uint64_t v_=(uint64_t)(value);for(unsigned b_=0;b_<(bytes);++b_){hash=(hash^(u8)v_)*UINT64_C(1099511628211);v_>>=8;}}while(0)
#define SPRITE_RESET() do {memset(&arena,0xa5,sizeof(arena));for(unsigned o_=0;o_<16;++o_)ot[o_]=(RenderNativeTag){0xffffff,0}; \
    g_BootDisplayOrderingTable=ot;g_BootDisplayPrimitiveCursor=(char *)&arena.packet;g_BootDisplayPrimitiveEnd=(char *)(&arena.packet+1); \
    g_BootDisplayTPage=0x4567;g_BootDisplayClut=0x1234;g_BootDisplayState=5;g_BootDisplayDone=0;}while(0)
#define SPRITE_PACKET() do {SPRITE_PUT((u32)arena.packet.draw_mode.code,4); \
    const u8 *p_=&arena.packet.sprite.r0;for(unsigned b_=0;b_<16;++b_)SPRITE_PUT(p_[b_],1); \
    if(arena.packet.draw_mode.tag.length!=(sizeof(BootDisplaySpritePacket)-sizeof(RenderNativeTag))/4 || \
       (arena.packet.sprite.tag.address & UINT64_C(0xff000000ff000000)) || (arena.packet.sprite.tag.length & UINT64_C(0xff000000ff000000)) || arena.before!=UINT64_C(0xa5a5a5a5a5a5a5a5) || arena.after!=UINT64_C(0xa5a5a5a5a5a5a5a5))valid=0;++cases;}while(0)
#define SPRITE_LINK(depth) do {if(ot[depth].address!=(uintptr_t)&arena.packet || arena.packet.draw_mode.tag.address!=0xffffff || \
    g_BootDisplayPrimitiveCursor!=(char *)(&arena.packet+1))valid=0;for(unsigned o_=0;o_<16;++o_)if(ot[o_].length || (o_!=(depth) && ot[o_].address!=0xffffff))valid=0;}while(0)
    for(unsigned pose=0;pose<6;++pose)for(unsigned i=0;i<6;++i){SPRITE_RESET();BootDisplay_InitSprite16(&arena.packet,poses[pose][0],poses[pose][1],uv[i],uv[(i*5+1)%6]);SPRITE_PACKET();}
    for(unsigned style=0;style<4;++style)for(unsigned pose=0;pose<6;++pose)for(unsigned character=0;character<256;++character){
        SPRITE_RESET();g_BootDisplayFontStyle=style;BootDisplay_QueueTintedSprite(poses[pose][0],poses[pose][1],character);SPRITE_PACKET();SPRITE_LINK(11);}
    for(unsigned color=0;color<256;++color){SPRITE_RESET();BootDisplay_QueueFadeSprite(color);SPRITE_PACKET();SPRITE_LINK(11);}
    if(!valid || cases!=6436 || hash!=UINT64_C(0x0de15ffbec1c8bad)){fprintf(stderr,"Credits sprites: cases=%u hash=%016llx valid=%d\n",cases,(unsigned long long)hash,valid);return 0;}
    for(unsigned state=5;state<9;++state)for(unsigned transition=0;transition<4;++transition)
    for(unsigned timer=0;timer<8;++timer)for(unsigned frame=0;frame<8;++frame)for(unsigned slot=0;slot<2;++slot)for(unsigned value=0;value<13;++value){
        SPRITE_RESET();g_BootDisplayState=state;g_BootDisplayTransition=transition;g_BootDisplayStateTimer=timers[timer];
        g_BootDisplayFrameCounter=frames[frame];g_BootDisplayBufferIndex=slot;g_BootDisplayImageBrightness=brightness[value];
        BootDisplay_StepImageSprite();SPRITE_PACKET();SPRITE_LINK(14);
        SPRITE_PUT(g_BootDisplayState,4);SPRITE_PUT(g_BootDisplayTransition,4);SPRITE_PUT(g_BootDisplayImageBrightness,4);
        SPRITE_PUT(g_BootDisplayStateTimer,2);SPRITE_PUT(g_BootDisplayBufferIndex,4);
    }
#undef SPRITE_PUT
#undef SPRITE_PACKET
#undef SPRITE_LINK
    if(!valid || cases!=33060 || hash!=UINT64_C(0xf412ed31c8597b85)){fprintf(stderr,"Credits sprite state: cases=%u hash=%016llx valid=%d\n",cases,(unsigned long long)hash,valid);return 0;}
    for(unsigned kind=0;kind<3;++kind)for(size_t remaining=0;remaining<sizeof(BootDisplaySpritePacket);++remaining){
        SPRITE_RESET();g_BootDisplayPrimitiveEnd=g_BootDisplayPrimitiveCursor+remaining;BootDisplaySpritePacket previous=arena.packet;
        if(kind==0)BootDisplay_QueueTintedSprite(1,2,'A');else if(kind==1)BootDisplay_QueueFadeSprite(128);else BootDisplay_StepImageSprite();
        if(g_BootDisplayState!=-1 || g_BootDisplayDone!=-1 || memcmp(&previous,&arena.packet,sizeof(previous)) ||
           g_BootDisplayPrimitiveCursor!=(char *)&arena.packet)return 0;
        for(unsigned i=0;i<16;++i)if(ot[i].address!=0xffffff || ot[i].length)return 0;
    }
#undef SPRITE_RESET
    puts("Original credits sprite packets: 33060 USA constructor/glyph/fade/image states match semantic payloads, native combined packet headers, full OT links and 168 atomic arena failures");return 1;
}

static unsigned credits_vblanks,credits_pcm_frames,credits_pcm_nonzero;
static u32 credits_seen_images,credits_probe_frame;
static unsigned credits_pixel_probes,credits_visible_pixels;
static int credits_force_bad_buffer;
static void credits_drive_audio(void)
{
    s16 samples[735*2];
    ++credits_vblanks;
    /* Disc 1 has no CREDITS.XA: this transport fixture uses FMV000's
     * channel 1, including when exercising the text-only callback table. */
    if(credits_vblanks==1 && !(g_GameStateFlags&0x20000))BootDisplay_SetCdFilter(1);
    size_t count=Psyz_CdPullSamples(samples,735);
    credits_pcm_frames+=(unsigned)count;
    for(size_t i=0;i<count*2;++i)if(samples[i])++credits_pcm_nonzero;
    g_PadPackets[0].header=0x4000;
    g_PadPackets[0].buttons=g_BootDisplayPlaybackEnded?0xfff7:0xffff;
    if((g_GameStateFlags&0x20000) && (g_BootDisplayState==5 || g_BootDisplayState==7) &&
       (unsigned)g_BootDisplayStateTimer<30)credits_seen_images|=1u<<g_BootDisplayStateTimer;
    if(g_BootDisplayFrameCounter%450==225 && credits_probe_frame!=g_BootDisplayFrameCounter){
        static _Alignas(8) u16 pixels[1024*512];RECT all={0,0,1024,512};
        StoreImage(&all,pixels);DrawSync(0);++credits_pixel_probes;
        for(unsigned y=0;y<448;++y)for(unsigned x=0;x<320;++x)
            if(pixels[y*1024+x]&0x7fff)++credits_visible_pixels;
        credits_probe_frame=g_BootDisplayFrameCounter;
    }
    if(credits_force_bad_buffer && credits_vblanks==1)g_BootDisplayBufferIndex=2;
    if(credits_vblanks>40000)g_BootDisplayState=g_BootDisplayDone=-1;
}

static int original_scene_texture_pages(PeDisc *disc)
{
    PeDiscFile archive;g_CdImage=disc;fx_image_io=1;
    fx_read_failure=fx_poll_failure=0;glyph_pending=glyph_poll_retry=0;
    if(Asset_OpenPeImage() || !pe_disc_find(disc,"PE.IMG",&archive) || !SDL_Init(SDL_INIT_EVENTS))return 0;
    ResetGraph(0);Boot_InitMemoryLayout();
    static _Alignas(8) u16 expected[1024*512],actual[1024*512];
    static _Alignas(16) u8 source[0x20000];
    RECT all={0,0,1024,512};
#define PAGE_CHECK(test) do {if(!(test)){fprintf(stderr,"Texture page failure at line %d\n",__LINE__);return 0;}}while(0)
    for(unsigned page=2;page<24;++page){
        unsigned index=43+page,bytes=(g_PeImageSectorTable[index+1]-g_PeImageSectorTable[index])*2048;
        PAGE_CHECK(bytes<=sizeof(source) && pe_disc_read(disc,&archive,(u64)g_PeImageSectorTable[index]*2048,source,bytes));
        for(unsigned i=0;i<1024*512;++i)expected[i]=0x1357;
        LoadImage(&all,expected);DrawSync(0);
        unsigned flags=source[4],offset=8;
        for(unsigned block=0;block<((flags&8)?2u:1u);++block){
            u32 length;RECT rect;memcpy(&length,source+offset,4);memcpy(&rect,source+offset+4,sizeof(rect));
            PAGE_CHECK(rect.x>=0 && rect.y>=0 && rect.w>0 && rect.h>0 && rect.x+rect.w<=1024 && rect.y+rect.h<=512 &&
                offset+length<=bytes && (unsigned)rect.w*rect.h*2<=length-12);
            for(unsigned y=0;y<(unsigned)rect.h;++y)for(unsigned x=0;x<(unsigned)rect.w;++x){
                unsigned pixel=offset+12+(y*rect.w+x)*2;
                expected[(rect.y+y)*1024+rect.x+x]=source[pixel]|(u16)source[pixel+1]<<8;
            }
            offset+=length;
        }
        g_GameState.room_type=page;g_LoadedTexturePageId=-1;glyph_reads=glyph_polls=0;
        PAGE_CHECK(Scene_StreamTexturePage()==0 && g_LoadedTexturePageId==(int)page && glyph_reads==1 && glyph_polls==1);
        PAGE_CHECK(memcmp(g_GameState.scene_load_scratch,source,bytes)==0);
        DrawSync(0);StoreImage(&all,actual);DrawSync(0);PAGE_CHECK(memcmp(expected,actual,sizeof(actual))==0);
        u32 marker=0x2222;RECT corner={1023,511,1,1};LoadImage(&corner,&marker);DrawSync(0);
        expected[511*1024+1023]=0x2222;
        PAGE_CHECK(Scene_StreamTexturePage()==0 && glyph_reads==1 && glyph_polls==1);
        StoreImage(&all,actual);DrawSync(0);PAGE_CHECK(memcmp(expected,actual,sizeof(actual))==0);
    }
    u32 base=g_GameState.pe_image_base_lba;u8 *scratch=g_GameState.scene_load_scratch;
    u16 first=g_PeImageSectorTable[45],last=g_PeImageSectorTable[46];
    for(unsigned fault=0;fault<18;++fault){
        CdRom_BindImage(disc,&archive);g_GameState.room_type=2;g_LoadedTexturePageId=23;
        g_GameState.scene_load_scratch=scratch;g_GameState.pe_image_base_lba=base;
        g_PeImageSectorTable[46]=last;fx_read_failure=fx_poll_failure=texture_page_corruption=0;
        glyph_reads=glyph_polls=0;int wanted=-3;
        if(fault==0)g_GameState.room_type=127;
        if(fault==1)g_GameState.room_type=200;
        if(fault==2)g_GameState.scene_load_scratch=NULL;
        if(fault==3)g_GameState.scene_load_scratch=scratch+1;
        if(fault==4)g_PeImageSectorTable[46]=first;
        if(fault==5)g_PeImageSectorTable[46]=first+65;
        if(fault==6)g_GameState.pe_image_base_lba=UINT32_MAX;
        if(fault==7){fx_read_failure=1;wanted=-1;}
        if(fault==8){fx_poll_failure=1;wanted=-2;}
        if(fault>=9 && fault<17){texture_page_corruption=fault-8;wanted=-4;}
        if(fault==17){g_PeImageSectorTable[46]=first+1;wanted=-4;}
        for(unsigned i=0;i<1024*512;++i)expected[i]=0x1357;
        LoadImage(&all,expected);DrawSync(0);
        PAGE_CHECK(Scene_StreamTexturePage()==wanted && g_LoadedTexturePageId==23);
        StoreImage(&all,actual);DrawSync(0);PAGE_CHECK(memcmp(expected,actual,sizeof(actual))==0);
    }
    CdRom_BindImage(disc,&archive);g_GameState.scene_load_scratch=scratch;g_GameState.pe_image_base_lba=base;
    g_PeImageSectorTable[46]=last;fx_read_failure=fx_poll_failure=texture_page_corruption=0;
    PAGE_CHECK(Scene_StreamTexturePage()==0 && g_LoadedTexturePageId==2);
#undef PAGE_CHECK
    puts("Original shared texture pages: all 22 USA TIM banks, full VRAM, cache hits, 18 atomic failures and recovery passed");
    return 1;
}

static int original_credit_video_clock(PeDisc *disc)
{
    g_CdImage=disc;glyph_pending=glyph_poll_retry=0;fx_image_io=1;
    if(Asset_OpenPeImage() || CdRom_LoadBootAudio() ||
       !SDL_Init(SDL_INIT_EVENTS))return 0;
    Boot_InitSystem();Boot_InitMemoryLayout();Psyz_AudioDestroy();
    if(!SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"PSYZ_AUDIO_CLOCK","video",true))return 0;
    g_BootDisplayPlaybackFilePath="FMV1/FMV000.STR";
    if(BootDisplay_InitPlaybackFile())return 0;
    g_BootDisplayPlaybackEndPos=g_BootDisplayPlaybackStartPos+12;
    if(BootDisplay_StartPlayback())return 0;
    Psyz_AudioPause();
    s32 position=g_BootDisplayPlaybackCurrentPos;
    for(unsigned i=0;i<8;++i)VSync(0);
    if(g_BootDisplayPlaybackCurrentPos!=position)return 0;
    Psyz_AudioUnpause();
    for(unsigned i=0;i<256 && !g_BootDisplayPlaybackEnded;++i)VSync(0);
    int valid=g_BootDisplayPlaybackEnded && g_BootDisplayState>=0 &&
        g_BootDisplayPlaybackCurrentPos>=g_BootDisplayPlaybackStartPos &&
        BootDisplay_WaitForCdAndRestoreCallbacks()==0;
    Psyz_AudioDestroy();
    if(valid)puts("Original credits video clock: paused XA does not advance; real mounted-image XA reaches EOF through VBlank without an audio device");
    return valid;
}

static int original_credit_playback(PeDisc *disc)
{
    g_CdImage=disc;glyph_pending=glyph_poll_retry=0;fx_image_io=1;
    if(Asset_OpenPeImage() || CdRom_LoadBootAudio())return 0;
    if(!SDL_Init(SDL_INIT_EVENTS))return 0;
    Boot_InitSystem();Boot_InitMemoryLayout();
    g_GameStateFlags=0x10000;
    int status=BootDisplay_RunDisplay();
    printf("Original credits full image flow: status=%d frame=%u timer=%d state=%d done=%d flags=%08x\n",
        status,g_BootDisplayFrameCounter,g_BootDisplayStateTimer,g_BootDisplayState,g_BootDisplayDone,g_GameStateFlags);
    if(status || g_BootDisplayDone!=1 || g_BootDisplayPlaybackActive ||
       (g_GameStateFlags&0x10000) || g_BootDisplayFrameCounter!=14028 ||
       g_BootDisplayStateTimer!=31 || g_BootDisplayState!=7)return 0;
    const char *authored=g_BootDisplayPlaybackFilePath;
    g_BootDisplayPlaybackFilePath="FMV1/FMV000.STR";
    if(BootDisplay_InitPlaybackFile())return 0;
    /* Compare real XA PCM from the CUE backend with the original module's
     * mounted-image callback. Both deliberately use the same PsyZ decoder. */
    enum {frames=88200};static s16 expected[frames*2],actual[frames*2];
    if(Psyz_CdSetDiskPath(getenv("PE_TEST_DISC")))return 0;
    u8 mode=200,filter[2]={1,1};CdlLOC position;
    CdControlB(14,&mode,NULL);CdControlB(13,filter,NULL);Psyz_AudioPause();
    CdlATV mix={128,0,128,0};CdMix(&mix);
    CdIntToPos(g_BootDisplayPlaybackStartPos,&position);CdControlB(2,(u8 *)&position,NULL);CdControlB(27,NULL,NULL);
    size_t reference=Psyz_CdPullSamples(expected,frames);
    CdControlB(8,NULL,NULL);Psyz_CdSetDiskPath(NULL);
    /* Campaign FMVs can reach credits without opening an SDL audio stream. */
    Psyz_AudioDestroy();
    int start_status=BootDisplay_StartPlayback();
    if(start_status){fprintf(stderr,"Credits playback start failed: %d\n",start_status);return 0;}
    if(BootDisplay_SetCdFilter(1))return 0;
    size_t observed=Psyz_CdPullSamples(actual,frames);unsigned nonzero=0;
    for(size_t i=0;i<observed*2;++i)if(actual[i])++nonzero;
    if(reference!=frames || observed!=reference || !nonzero || memcmp(expected,actual,observed*4) ||
       BootDisplay_WaitForCdAndRestoreCallbacks() || g_BootDisplayPlaybackActive){
        fprintf(stderr,"Credits PCM mismatch: reference=%zu observed=%zu nonzero=%u compare=%d active=%d\n",
            reference,observed,nonzero,memcmp(expected,actual,observed*4),g_BootDisplayPlaybackActive);return 0;}
    printf("Original credits XA transport: %zu real FMV stereo frames match CUE PCM (%u nonzero samples)\n",observed,nonzero);
    s32 start=g_BootDisplayPlaybackStartPos,end=g_BootDisplayPlaybackEndPos;PeDiscFile file=g_BootDisplayPlaybackFile;
    for(unsigned fault=0;fault<4;++fault){
        u32 sector_size=disc->sector_size;char *buffer=g_BootDisplayPlaybackSectorBuffer;
        if(fault==0)g_CdImage=NULL;
        if(fault==1)disc->sector_size=2048;
        if(fault==2)g_BootDisplayPlaybackSectorBuffer=NULL;
        if(fault==3)g_BootDisplayPlaybackEndPos=start;
        if(BootDisplay_StartPlayback()!=-1 || g_BootDisplayPlaybackActive)return 0;
        g_CdImage=disc;disc->sector_size=sector_size;g_BootDisplayPlaybackSectorBuffer=buffer;g_BootDisplayPlaybackEndPos=end;
    }
    for(unsigned fault=0;fault<2;++fault){
        g_BootDisplayState=g_BootDisplayDone=0;
        g_BootDisplayPlaybackEndPos=start+3;g_BootDisplayPlaybackFile.size=3*2048;
        if(BootDisplay_StartPlayback())return 0;
        u64 bytes=disc->image_size;if(fault)disc->image_size=0;
        Psyz_CdPullSamples(actual,frames);disc->image_size=bytes;
        for(unsigned i=0;i<32;++i)VSync(0);
        if(!g_BootDisplayPlaybackEnded || (fault && (g_BootDisplayState!=-1 || g_BootDisplayDone!=-1)) ||
           (!fault && g_BootDisplayPlaybackCurrentPos!=start+3) || BootDisplay_WaitForCdAndRestoreCallbacks())return 0;
        g_BootDisplayPlaybackFile=file;g_BootDisplayPlaybackEndPos=end;
    }
    if(BootDisplay_SetCdFilter(-1)!=-1 || BootDisplay_SetCdFilter(256)!=-1)return 0;
    for(unsigned images=0;images<2;++images){
        if(CdRom_LoadBootAudio())return 0;
        g_BootDisplayPlaybackFilePath="FMV1/FMV000.STR";
        Boot_BuildRenderFlagTable();g_GameStateFlags=images?0x20000:0;
        credits_vblanks=credits_pcm_frames=credits_pcm_nonzero=0;
        credits_seen_images=credits_pixel_probes=credits_visible_pixels=0;credits_probe_frame=UINT32_MAX;
        VSyncCallbacks(0,credits_drive_audio);
        status=BootDisplay_RunDisplay();VSyncCallbacks(0,NULL);
        printf("Original credits full sprite flow: images=%u status=%d frame=%u timer=%d state=%d done=%d vblanks=%u pcm=%u nonzero=%u seen=%08x probes=%u pixels=%u\n",
            images,status,g_BootDisplayFrameCounter,g_BootDisplayStateTimer,g_BootDisplayState,g_BootDisplayDone,
            credits_vblanks,credits_pcm_frames,credits_pcm_nonzero,credits_seen_images,credits_pixel_probes,credits_visible_pixels);
        if(status || g_BootDisplayDone!=1 || g_BootDisplayPlaybackActive || !g_BootDisplayPlaybackEnded ||
           !credits_pcm_nonzero || g_BootDisplayFrameCounter!=(images?14157:14028) ||
           credits_pixel_probes<10 || credits_visible_pixels<1000 ||
           (images && (g_BootDisplayStateTimer!=30 || g_BootDisplayState!=7 || credits_seen_images!=0x3fffffffu)))return 0;
    }
    g_BootDisplayPlaybackFilePath="missing-credits.xa";g_GameStateFlags=0;
    if(BootDisplay_RunDisplay()!=-2 || g_BootDisplayPlaybackActive)return 0;
    u8 *workspace=g_CdLoadAreaBase;g_CdLoadAreaBase=NULL;
    if(BootDisplay_RunDisplay()!=-1 || g_BootDisplayPlaybackActive)return 0;
    g_CdLoadAreaBase=workspace;
    g_BootDisplayPlaybackFilePath="FMV1/FMV000.STR";g_GameStateFlags=0x20000;
    credits_force_bad_buffer=1;credits_vblanks=0;VSyncCallbacks(0,credits_drive_audio);
    status=BootDisplay_RunDisplay();VSyncCallbacks(0,NULL);credits_force_bad_buffer=0;
    if(status!=-3 || g_BootDisplayPlaybackActive)return 0;
    g_BootDisplayPlaybackFilePath=(char *)authored;
    puts("Original credits playback: four invalid starts, bounded EOF, read failure, cleanup and sprite-mode reentry passed");
    return 1;
}

static int original_credit_sprites(PeDisc *disc)
{
    g_CdImage=disc;glyph_pending=glyph_poll_retry=0;fx_image_io=1;
    if(Asset_OpenPeImage() || CdRom_LoadBootAudio())return 0;
    if(!SDL_Init(SDL_INIT_EVENTS))return 0;ResetGraph(0);Boot_InitMemoryLayout();
    BootDisplay_InitDisplayResources();if(!original_credit_sprite_packets())return 0;
    const u32 sprite_addresses[]={0x80123e08,0x80123f8c,0x8012403c,0x80124598,0x80124704,0x801247f4,0x80124034};
    const u32 image_addresses[]={0x8012489c,0,0x80125094,0x80124c04,0x80125464,0x80124aa4,0x80125444};
    if(memcmp(g_CdLoadAreaBase+0x4e4c,sprite_addresses,sizeof(sprite_addresses)) ||
       memcmp(g_CdLoadAreaBase+0x4e68,image_addresses,sizeof(image_addresses)))return 0;
    BootDisplayCallbacks *callbacks=&g_BootDisplaySpriteCallbacks;
    if(callbacks->init!=BootDisplay_InitDisplayResources || callbacks->step!=BootDisplay_SelectFrameBuffers ||
       callbacks->text!=BootDisplay_StepImageSprite || callbacks->glyph!=BootDisplay_QueueTintedSprite ||
       callbacks->transition!=BootDisplay_QueueFadeSprite || callbacks->present!=BootDisplay_PresentSpriteFrame || callbacks->shutdown!=BootDisplay_DisplayNoOp)return 0;
    callbacks->shutdown();callbacks=&g_BootDisplayImageCallbacks;
    if(callbacks->init!=BootDisplay_InitDisplayBuffers || callbacks->step || callbacks->text!=BootDisplay_StepImageDisplay ||
       callbacks->glyph!=BootDisplay_BrightenGlyph || callbacks->transition!=BootDisplay_BrightenImageRegion ||
       callbacks->present!=BootDisplay_PresentFrame || callbacks->shutdown!=BootDisplay_CallInitSystem)return 0;
    BootDisplay_InitDisplayResources();char *begins[2],*ends[2];RenderNativeTag *tables[2];
    for(unsigned slot=0;slot<2;++slot){g_ActiveDrawSlot=slot;BootDisplay_SelectFrameBuffers();
        begins[slot]=g_BootDisplayPrimitiveCursor;ends[slot]=g_BootDisplayPrimitiveEnd;tables[slot]=g_BootDisplayOrderingTable;
        for(unsigned i=0;i<BOOT_DISPLAY_SPRITE_CAPACITY;++i)BootDisplay_QueueTintedSprite(24,20,'A');
        if(g_BootDisplayState<0 || g_BootDisplayPrimitiveCursor!=ends[slot])return 0;
    }
    if(ends[0]!=begins[1] || ends[1]!=(char *)tables[0] || tables[0]+16!=tables[1])return 0;
    BootDisplay_QueueFadeSprite(128);if(g_BootDisplayState!=-1)return 0;

    static _Alignas(16) u8 table[69632],decoded[0xa800],rgb[0xa000];
    static u16 stage[1024*512],actual[1024*512],expected[1024*512];
    RECT whole={0,0,1024,512};unsigned states=0,draws=0,visible=0;
    MdecDiag_ExpandDecoderTable(table);
    for(unsigned index=0;index<31;++index){
        DecDCTvlcSize2(0);if(DecDCTvlc2(g_BootDisplayCreditImages[index],decoded,table)!=0)return 0;
        DecDCTReset(0);DecDCToutCallback(NULL);DecDCTin(decoded,0);DecDCTout(rgb,0x2800);if(DecDCToutSync(0))return 0;
        BootDisplay_InitDisplayResources();g_BootDisplayBufferIndex=index%2;g_BootDisplayStateTimer=index;
        for(unsigned i=0;i<1024*512;++i)stage[i]=0x1357;LoadImage(&whole,stage);DrawSync(0);
        unsigned steps=0;
        while(g_BootDisplayState!=5 && steps<512){g_ActiveDrawSlot=steps%2;BootDisplay_SelectFrameBuffers();BootDisplay_StepImageSprite();++steps;}
        if(g_BootDisplayState!=5){fprintf(stderr,"Credit sprite image %u stalled state=%d\n",index,g_BootDisplayState);return 0;}
        BootDisplayImageBuffer *screen=&g_BootDisplayImageBuffers[index%2];
        for(unsigned column=0;column<10;++column)for(unsigned y=0;y<128;++y)
            memcpy(&stage[(screen->transferY+y)*1024+screen->transferX+column*16],rgb+column*0x1000+y*32,32);
        StoreImage(&whole,actual);DrawSync(0);if(memcmp(actual,stage,sizeof(stage)))return 0;
        states+=steps;
        for(unsigned position=0;position<4;++position)for(unsigned slot=0;slot<2;++slot){
            LoadImage(&whole,stage);DrawSync(0);g_ActiveDrawSlot=slot;g_BootDisplayBufferIndex=1-index%2;
            g_BootDisplayStateTimer=position;g_BootDisplayState=7;g_BootDisplayTransition=1;
            g_BootDisplayFrameCounter=1;g_BootDisplayImageBrightness=128;
            SetDefDrawEnv(&g_RenderDrawEnvArray[slot],0,slot*224,320,224);g_RenderDrawEnvArray[slot].isbg=1;
            SetDefDispEnv(&g_RenderDispEnvArray[slot],0,(1-slot)*224,320,224);
            BootDisplay_SelectFrameBuffers();BootDisplay_StepImageSprite();BootDisplay_PresentSpriteFrame();DrawSync(0);
            if(g_ActiveDrawSlot!=(s32)(1-slot))return 0;
            StoreImage(&whole,actual);DrawSync(0);
            LoadImage(&whole,stage);DrawSync(0);PutDrawEnv(&g_RenderDrawEnvArray[slot]);
            BootDisplayDrawModePrim mode;BootDisplaySpritePrim sprite;memset(&mode,0,sizeof(mode));memset(&sprite,0,sizeof(sprite));
            BootDisplayImageBuffer *front=&g_BootDisplayImageBuffers[1-index%2];
            SetDrawTPage(&mode,0,0,GetTPage(2,0,front->x,front->y));SetSprt(&sprite);
            sprite.r0=sprite.g0=sprite.b0=128;sprite.w=160;sprite.h=120;
            sprite.x0=position%2?0:160;sprite.y0=position>=2?105:0;
            DrawPrim(&mode);DrawPrim(&sprite);DrawSync(0);StoreImage(&whole,expected);DrawSync(0);
            if(memcmp(actual,expected,sizeof(actual))){fprintf(stderr,"Credit sprite presentation index=%u pos=%u slot=%u differs\n",index,position,slot);return 0;}
            for(unsigned y=0;y<224;++y)for(unsigned x=0;x<320;++x)if(actual[(slot*224+y)*1024+x]&0x7fff)++visible;
            ++draws;
        }
    }
    fx_image_io=0;
    if(!visible || draws!=248)return 0;
    printf("Original credits sprite images: 31 images, %u decoder steps, 310 RGB15 uploads and %u full-VRAM presentations across all positions/both buffers match unmerged direct draw\n",states,draws);return 1;
}

static int original_credit_brightness(void)
{
    static struct {u8 before[64];char pixels[320*224*3];u8 after[64];} screens[2];
    const unsigned seeds[]={0,64,128,240};
    const int edges[][2]={{0,-16},{0,-7},{304,208},{319,100},{-1,100},{0,223},{0,224},{1,0}};
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;int valid=1;
    for(unsigned slot=0;slot<2;++slot)g_BootDisplayImageBuffers[slot].pixels=screens[slot].pixels;
#define BRIGHT_PUT(data,count) do {const u8 *p_=(const u8 *)(data); \
    for(size_t i_=0;i_<(size_t)(count);++i_)hash=(hash^p_[i_])*UINT64_C(1099511628211);}while(0)
#define BRIGHT_CLEAR(slot,shade) do {unsigned s_=(slot),v_=(shade); \
    memset(&screens[s_],0xa5,sizeof(screens[s_]));u8 *p_=(u8 *)screens[s_].pixels; \
    for(size_t i_=0;i_<sizeof(screens[s_].pixels);i_+=3){p_[i_]=v_;p_[i_+1]=v_/2;p_[i_+2]=(v_+17)&255;}}while(0)
#define BRIGHT_GUARDS() do {for(unsigned i_=0;i_<64;++i_)if(screens[g_BootDisplayBufferIndex].before[i_]!=0xa5 || screens[g_BootDisplayBufferIndex].after[i_]!=0xa5)valid=0;}while(0)
    for(unsigned style=0;style<4;++style){unsigned slot=style%2;g_BootDisplayBufferIndex=slot;g_BootDisplayFontStyle=style;
        for(unsigned character=0;character<256;++character){
            BRIGHT_CLEAR(slot,seeds[style]);
            BootDisplay_BrightenGlyph(100,110,character);
            for(unsigned y=0;y<16;++y)BRIGHT_PUT(screens[slot].pixels+(110+y)*960+300,48);
            BRIGHT_GUARDS();++cases;
        }
    }
    for(unsigned style=0;style<4;++style)for(unsigned edge=0;edge<8;++edge)for(unsigned character=0;character<2;++character){
        unsigned slot=style%2;int x=edges[edge][0],y=edges[edge][1];g_BootDisplayBufferIndex=slot;g_BootDisplayFontStyle=style;
        BRIGHT_CLEAR(slot,seeds[style]);
        BootDisplay_BrightenGlyph(x,y,character?'~':'A');
        int start=(y>0?y:0)*960+x*3;if(start<0)start=0;
        int end=((y+16<224?y+16:224)-1)*960+x*3+48;
        if(end>320*224*3)end=320*224*3;if(end<start)end=start;
        BRIGHT_PUT(screens[slot].pixels+start,end-start);BRIGHT_GUARDS();++cases;
    }
    for(unsigned slot=0;slot<2;++slot)for(unsigned seed=0;seed<4;++seed){
        g_BootDisplayBufferIndex=slot;BRIGHT_CLEAR(slot,seeds[seed]);
        BootDisplay_BrightenImageRegion(0);for(unsigned row=0;row<24;++row)BRIGHT_PUT(screens[slot].pixels+0x19dac+row*960,288);
        BRIGHT_GUARDS();++cases;
    }
#undef BRIGHT_CLEAR
#undef BRIGHT_PUT
    const int outside[][2]={{-1,0},{319,223},{-32768,0},{32767,223}};
    for(unsigned edge=0;edge<4;++edge){g_BootDisplayBufferIndex=edge%2;memset(&screens[edge%2],0xa5,sizeof(screens[edge%2]));
        BootDisplay_BrightenGlyph(outside[edge][0],outside[edge][1],'A');BRIGHT_GUARDS();}
#undef BRIGHT_GUARDS
    if(!valid || cases!=1096 || hash!=UINT64_C(0x62c94d8d86727434)){fprintf(stderr,"Credits brightness: cases=%u hash=%016llx guards=%d\n",cases,(unsigned long long)hash,valid);return 0;}
    puts("Original credits RGB24 brightness: 1096 USA glyph/panel cases, saturation, four styles, both buffers, row clipping and four outer buffer guards pass");return 1;
}

static int original_credit_images(PeDisc *disc)
{
    g_CdImage=disc;glyph_pending=glyph_poll_retry=0;fx_image_io=1;
    if(Asset_OpenPeImage() || CdRom_LoadBootAudio())return 0;
    if(!SDL_Init(SDL_INIT_EVENTS))return 0;ResetGraph(0);Boot_InitMemoryLayout();
    static _Alignas(16) u8 table[69632],decoded[0xa800],rgb[0xf000];
    static u16 actual[1024*512],expected[1024*512],shown[480*224];
    RECT whole={0,0,1024,512};unsigned states=0,uploads=0;
    BootDisplay_InitDisplayBuffers();if(!original_credit_brightness())return 0;
    MdecDiag_ExpandDecoderTable(table);
    for(unsigned index=0;index<31;++index){
        DecDCTvlcSize2(0);
        if(DecDCTvlc2(g_BootDisplayCreditImages[index],decoded,table)!=0)return 0;
        DecDCTReset(0);DecDCToutCallback(NULL);DecDCTin(decoded,1);DecDCTout(rgb,0x3c00);
        if(DecDCToutSync(0))return 0;
        g_ActiveDrawSlot=0;BootDisplay_InitDisplayBuffers();g_BootDisplayBufferIndex=index%2;
        g_BootDisplayStateTimer=index;
        for(unsigned i=0;i<1024*512;++i)expected[i]=0x1357;
        LoadImage(&whole,expected);DrawSync(0);
        unsigned steps=0;
        while(g_BootDisplayState!=5 && steps<512){BootDisplay_StepImageDisplay();++steps;}
        if(g_BootDisplayState!=5){fprintf(stderr,"Credits image %u stalled state=%d after=%u\n",index,g_BootDisplayState,steps);return 0;}
        BootDisplayImageBuffer *screen=&g_BootDisplayImageBuffers[index%2];
        for(unsigned column=0;column<10;++column)for(unsigned y=0;y<120;++y)
            memcpy(&expected[(screen->transferY+52+y)*1024+screen->transferX+120+column*24],rgb+column*0x1800+y*48,48);
        StoreImage(&whole,actual);DrawSync(0);
        if(memcmp(actual,expected,sizeof(actual))){fprintf(stderr,"Credits staged image %u differs from whole-frame MDEC output\n",index);return 0;}
        g_BootDisplayState=6;BootDisplay_PresentFrame();
        if(g_BootDisplayStateTimer!=(s16)(index+1) || g_BootDisplayBufferIndex!=(s32)(1-index%2) ||
           g_BootDisplayState!=(index==30?7:0))return 0;
        g_BootDisplayState=7;BootDisplay_StepImageDisplay();BootDisplay_PresentFrame();
        RECT visible={0,0,480,224};StoreImage(&visible,shown);DrawSync(0);
        for(unsigned y=0;y<224;++y)if(memcmp(shown+y*480,&expected[(screen->transferY+y)*1024+screen->transferX],960)){
            fprintf(stderr,"Credits presented image %u row %u differs\n",index,y);return 0;}
        states+=steps;uploads+=10;
    }
    u8 *workspace=g_CdLoadAreaBase;u16 main_sector=g_MainAssetLbaTbl[0];
    for(unsigned fault=0;fault<3;++fault){
        if(fault==0)g_CdLoadAreaBase=NULL;
        else g_MainAssetLbaTbl[0]=fault==1?0:65535;
        BootDisplay_InitDisplayBuffers();
        if(g_BootDisplayState!=-1 || g_BootDisplayDone!=-1)return 0;
        g_CdLoadAreaBase=workspace;g_MainAssetLbaTbl[0]=main_sector;
    }
    for(unsigned fault=0;fault<7;++fault){
        BootDisplay_InitDisplayBuffers();
        u16 words;memcpy(&words,g_BootDisplayCreditImages[0],2);
        if(fault==0)g_BootDisplayBufferIndex=2;
        if(fault==1)g_BootDisplayStateTimer=31;
        if(fault==2){u16 invalid=65535;memcpy(g_BootDisplayCreditImages[0],&invalid,2);}
        if(fault==3){u16 invalid=0;memcpy(g_BootDisplayCreditImages[0],&invalid,2);}
        if(fault==4)g_BootDisplayState=1;
        if(fault==5)g_BootDisplayImageBuffers[0].pixels=NULL;
        if(fault==6)BootDisplay_SetImageDisplay(2);
        BootDisplay_StepImageDisplay();
        memcpy(g_BootDisplayCreditImages[0],&words,2);
        if(g_BootDisplayState!=-1 || g_BootDisplayDone!=-1)return 0;
    }
    BootDisplay_InitDisplayBuffers();if(g_BootDisplayState || g_BootDisplayDone)return 0;
    fx_image_io=0;
    printf("Original credits images: 31 real images, %u decoder steps, %u RGB24 slice uploads, full staged VRAM and both presented buffers match whole-frame MDEC output; ten invalid states/workspaces fail and recover\n",states,uploads);return 1;
}

static u8 *s_AkaoReplacementBank;
static unsigned s_AkaoReplacementChecks;
static int s_AkaoReplacementValid;

static void original_akao_check_old_bank(void *message)
{
    (void)message;
    for (unsigned i=0;i<64;++i)
        if (s_AkaoReplacementBank[i]!=0x8f) s_AkaoReplacementValid=0;
    ++s_AkaoReplacementChecks;
}

static int original_akao_bank_lifetime(void)
{
    static struct AkaoReplacementFixture {
        SceneAssetBlob header;
        SceneAssetDirectory directory;
        u8 program[64];
    } scene;
    static u8 bank[0x1400],other[0x1400];
    SceneTrackRecord record={0};
    AkaoMessageHandler saved=Akao_MessageHandlers[0];
    Spu_InitHardware();
    /* Retail's cache at B4900 aliases instrument indices 80..FF in the
     * table at B2900. Stream uploads must be visible to the note opcodes. */
    AkaoInstrument resident[128];memcpy(resident,g_AkaoInstrumentTable,sizeof(resident));
    for(unsigned stream_bank=0;stream_bank<2;++stream_bank) {
        _Alignas(16) u8 file[0x40+2*sizeof(AkaoInstrument)+32]={0};
        u32 magic=0x4f414b41,size=32,end=2;
        memcpy(file,&magic,4);memcpy(file+0x14,&size,4);memcpy(file+0x1c,&end,4);
        AkaoInstrument *instruments=(AkaoInstrument *)(file+0x40);
        for(unsigned i=0;i<2;++i) {
            instruments[i].start_address=i*16;instruments[i].loop_address=i*16;
            for(unsigned note=0;note<12;++note)instruments[i].pitch[note]=4096+i*1024+note;
        }
        if(Spu_UploadStreamBlockB(stream_bank,file))return 0;
        for(unsigned i=0;i<2;++i) {
            unsigned index=128+stream_bank*16+i;u8 note=(u8)index;
            AkaoTrack track={.pc=&note,.note_pitch=0xff,.parent_track_id=1};
            Akao_PlayNote(&track,1);
            if(track.sample_header!=0x68000+(stream_bank<<13)+i*16 ||
               track.sample_data!=track.sample_header ||
               Akao_LookupPitchPeriod(index,72,0)!=4096+i*1024 ||
               memcmp(&g_AkaoInstrumentTable[index],&instruments[i],sizeof(AkaoInstrument)))return 0;
        }
    }
    if(memcmp(resident,g_AkaoInstrumentTable,sizeof(resident)))return 0;
    u32 blanks=g_BootHostVBlankCount;
    scene.header.directoryOffset=offsetof(struct AkaoReplacementFixture,directory);
    memset(scene.program,0x91,sizeof(scene.program));
    record.offset=offsetof(struct AkaoReplacementFixture,program);
    record.size=sizeof(scene.program);
    g_GameState.loaded_scene_archive=(void *)&scene;
    g_GameState.loaded_scene_assets=(void *)&scene;
    g_GameState.bank_work_base=bank;g_GameState.bank_work_end=other;
    s_AkaoReplacementBank=bank;s_AkaoReplacementChecks=0;s_AkaoReplacementValid=1;
    Akao_MessageHandlers[0]=original_akao_check_old_bank;
    int valid=1;
    for(unsigned pass=0;pass<64 && valid;++pass){
        while(g_AkaoMessageQueueCount)VSync(1);
        EnterCriticalSection();
        memset(bank,0x8f,sizeof(bank));memset(other,0x55,sizeof(other));
        g_AkaoSequencerState.primary.active_voice_mask=1;
        g_AkaoSequencerTracks[0].track.pc=bank+16;
        g_AkaoSequencerTracks[0].track.note_length=3;
        g_AkaoSequencerTracks[0].track.pan_duration=1;
        g_AkaoMessageQueue[0].opcode.word=0;g_AkaoMessageQueueCount=1;
        Akao_Cmd_F0();
        g_GameState.cd_track_phase=9;g_CdTrackRecord=&record;g_CdTrackBank=7;
        ExitCriticalSection();
        int slot=-1;
        if(CdRom_SeekToTrack(123,1,0,&slot,0)!=0 || slot!=0 ||
           g_GameState.cd_track_phase || g_AkaoMessageQueueCount ||
           s_AkaoReplacementChecks!=pass+1 || !s_AkaoReplacementValid ||
           memcmp(bank,scene.program,sizeof(scene.program)))valid=0;
        for(unsigned i=0;i<sizeof(other);++i)if(other[i]!=0x55)valid=0;
    }
    while(g_AkaoMessageQueueCount)VSync(1);
    if(g_BootHostVBlankCount!=blanks)valid=0;
    Spu_Shutdown();Akao_MessageHandlers[0]=saved;
    if(valid)puts("Original AKAO bank lifetime: 64 pending-stop replacements consume commands against old bytes before copying, preserve the other bank and do not advance VBlank");
    return valid;
}

static int original_menu_sound_commands(PeDisc *disc,PeDiscFile *exe)
{
    _Alignas(16) static u8 bank[0x1400];PeDiscFile archive;
    if(!pe_disc_find(disc,"PE.IMG",&archive) ||
       !pe_disc_read(disc,&archive,(u64)g_PeImageSectorTable[7]*2048,bank,sizeof(bank)))return 0;
    u32 directory,packed;memcpy(&directory,bank+4,4);
    if(directory>sizeof(bank)-sizeof(SceneAssetDirectory))return 0;
    memcpy(&packed,bank+directory+0x2c,4);unsigned count=packed>>22,offset=packed&0x3fffff;
    if(offset>sizeof(bank) || count>(sizeof(bank)-offset)/12)return 0;
    void (*play[])(void)={Menu_PlayConfirmSound,Menu_PlayCancelSound,Menu_PlayMoveSound,Menu_PlayErrorSound};
    Pe1GameState saved_state=g_GameState;AkaoStagedCommand saved_command=g_AkaoCmd;
    AkaoQueueEntry saved_queue[16];memcpy(saved_queue,g_AkaoMessageQueue,sizeof(saved_queue));
    int saved_count=g_AkaoMessageQueueCount;AkaoSequenceCounter saved_sequence=g_AkaoCmd24Sequence;
    g_GameState.bank_work_far_end=NULL;g_AkaoMessageQueueCount=0;
    for(unsigned i=0;i<4;++i)play[i]();
    if(g_AkaoMessageQueueCount)return 0;
    g_GameState.bank_work_far_end=bank;
    if(g_AkaoBgmHandle[0]!=bank)return 0;
    u8 expected_programs[4][sizeof(bank)];size_t program_sizes[4];
    for(unsigned i=0;i<4;++i){
        u32 payload=0,length=0;
        for(unsigned row=0;row<count;++row){
            u16 key;memcpy(&key,bank+offset+row*12+10,2);
            if(key==0x44c+i){memcpy(&payload,bank+offset+row*12+4,4);payload&=0xffffff;memcpy(&length,bank+offset+row*12,4);length&=0xffffff;break;}
        }
        if(!payload || payload>sizeof(bank)-4 || length<8 || length>sizeof(bank)-payload)return 0;
        program_sizes[i]=length-4;memcpy(expected_programs[i],bank+payload+4,length-4);
        void *stable=Asset_FindTable2CByU16Key((char *)bank,0x44c+i);
        if(!stable || stable==bank+payload || memcmp(stable,bank+payload,length) ||
           stable!=Asset_FindTable2CByU16Key((char *)bank,0x44c+i))return 0;
        u32 magic;memcpy(&magic,bank+payload,4);if(magic!=0x4f414b41)return 0;
        play[i]();AkaoQueueEntry *command=&g_AkaoMessageQueue[i];
        if(g_AkaoMessageQueueCount!=(int)i+1 || command->opcode.word!=0x24 ||
           command->arg0.sequence!=(u8 *)stable+4 || command->arg1!=0x100 ||
           command->arg2!=0x80 || command->arg3!=0x7f)return 0;
    }
    memset(bank,0xa5,sizeof(bank));
    for(unsigned i=0;i<4;++i)
        if(memcmp(g_AkaoMessageQueue[i].arg0.sequence,expected_programs[i],program_sizes[i]))return 0;
    g_GameState=saved_state;g_AkaoCmd=saved_command;g_AkaoMessageQueueCount=saved_count;
    g_AkaoCmd24Sequence=saved_sequence;memcpy(g_AkaoMessageQueue,saved_queue,sizeof(saved_queue));
    u8 exe_header[0x800];u32 exe_base;SceneRoomSectorRange ranges[2];
    if(!pe_disc_read(disc,exe,0,exe_header,sizeof(exe_header)))return 0;
    memcpy(&exe_base,exe_header+0x18,4);
    for(unsigned i=0;i<2;++i)
        if(!pe_disc_read(disc,exe,0x800ull+0x80093378u-exe_base+(98+i)*8,&ranges[i],sizeof(ranges[i])))return 0;
    size_t room_bytes=(size_t)ranges[1].roomSectors*2048;
    if(room_bytes<(size_t)ranges[0].roomSectors*2048 || room_bytes<=0x26f18)return 0;
    u8 *room=malloc(room_bytes);if(!room)return 0;
    int valid=pe_disc_read(disc,&archive,(u64)(ranges[0].start+ranges[0].scratchSectors+ranges[0].textureSectors)*2048,
                           room,(size_t)ranges[0].roomSectors*2048);
    u8 *footstep=valid?Asset_FindTable2CByU16Key((char *)room,1004):NULL;
    u8 footstep_bytes[36];
    if(!footstep || footstep==room+0x26f0c || memcmp(footstep,room+0x26f0c,sizeof(footstep_bytes))){free(room);return 0;}
    memcpy(footstep_bytes,footstep,sizeof(footstep_bytes));
    valid=pe_disc_read(disc,&archive,(u64)(ranges[1].start+ranges[1].scratchSectors+ranges[1].textureSectors)*2048,
                       room,room_bytes);
    AkaoTrack lookahead={0};lookahead.pc=footstep+12;
    if(!valid || room[0x26f18]!=0xc9 || memcmp(footstep,footstep_bytes,sizeof(footstep_bytes)) ||
       Akao_LookupSampleBankByte(&lookahead)!=0x7f){free(room);return 0;}
    free(room);
    puts("Original menu audio: canonical loaded-bank alias and all four real USA sound keys queue native AKAO commands with stable shared programs after source overwrite; absent bank stays quiet");
    return 1;
}

static int original_akao_resident_tables(PeDisc *disc,PeDiscFile *exe)
{
    const struct {u32 address;size_t size;const void *data;} tables[]={
        {0x8009b85c,96,g_AkaoOpcodeArgLengths},{0x8009b8bc,32,g_AkaoExtOpcodeArgLengths},
        {0x8009b8dc,22,g_AkaoNoteLengthTable},{0x8009b8f4,4,g_AkaoDefaultVoiceProgram},
        {0x8009b8f8,512,g_AkaoPanVolumeTable}};
    u8 bytes[1024];u32 pointers[16];size_t sizes[16];
    for(unsigned i=0;i<5;++i)
        if(!pe_disc_read(disc,exe,tables[i].address-0x80010000u+0x800,bytes,tables[i].size) ||
           memcmp(bytes,tables[i].data,tables[i].size))return 0;
    if(!pe_disc_read(disc,exe,0x8009c080u-0x80010000u+0x800,pointers,sizeof(pointers)))return 0;
    for(unsigned i=0;i<16;++i){
        u32 end=0x8009c080;
        for(unsigned j=0;j<16;++j){
            if(pointers[j]>pointers[i] && pointers[j]<end)end=pointers[j];
            if((pointers[i]==pointers[j])!=(g_AkaoSoundEntryTable[i]==g_AkaoSoundEntryTable[j]))return 0;
        }
        if(pointers[i]<0x8009baf8 || pointers[i]>=end || end-pointers[i]>sizeof(bytes))return 0;
        sizes[i]=end-pointers[i];
        if(!g_AkaoSoundEntryTable[i] || !pe_disc_read(disc,exe,pointers[i]-0x80010000u+0x800,bytes,sizes[i]) ||
           memcmp(bytes,g_AkaoSoundEntryTable[i],sizes[i]))return 0;
    }
    const unsigned periods[]={1,2,255},modes[]={0,1,4};
    int saved_mode=g_AkaoPlaybackMode;uint64_t hash=UINT64_C(14695981039346656037);unsigned frames=0;
    for(unsigned selector=0;selector<16;++selector)for(unsigned period=0;period<3;++period)
    for(unsigned mode=0;mode<3;++mode){
        void *wave=g_AkaoSoundEntryTable[selector];
        AkaoTrack voice={.pitch_lfo_table=wave,.volume_lfo_table=wave,.aux_lfo_table=wave,
            .flags=7,.expression_value=100<<16,.volume_base=100<<8,.pitch_base=0x1000,
            .pan_target=0x7f00,.panpot=0x7f00,.pitch_lfo_phase=1,.volume_lfo_phase=1,.aux_lfo_phase=1,
            .pitch_lfo_duration=periods[period],.volume_lfo_duration=periods[period]+1,
            .aux_lfo_duration=periods[period]+2,.pitch_lfo_depth=1024,.volume_lfo_target=100<<8,
            .aux_lfo_target=64<<8,.update_flags=0x13};
        g_AkaoPlaybackMode=modes[mode];
        for(unsigned tick=0;tick<512;++tick){
            Akao_SetVoiceKeyOn(&voice,0x1000);
#define RESIDENT_AUDIO_PUT(value,count) do {u32 v_=(u32)(value); \
    for(unsigned b_=0;b_<(count);++b_){hash=(hash^(u8)v_)*UINT64_C(1099511628211);v_>>=8;}}while(0)
            RESIDENT_AUDIO_PUT(voice.update_flags,4);
            const u16 values[]={voice.pitch,(u16)voice.volume_left,(u16)voice.volume_right,
                (u16)voice.pitch_lfo_value,(u16)voice.volume_lfo_value,(u16)voice.aux_lfo_value,
                voice.pitch_lfo_phase,voice.volume_lfo_phase,voice.aux_lfo_phase};
            for(unsigned i=0;i<9;++i)RESIDENT_AUDIO_PUT(values[i],2);
            const void *cursor[]={voice.pitch_lfo_table,voice.volume_lfo_table,voice.aux_lfo_table};
            for(unsigned i=0;i<3;++i){
                uintptr_t offset=(uintptr_t)cursor[i]-(uintptr_t)wave;
                if(offset>=sizes[selector])return 0;
                RESIDENT_AUDIO_PUT(offset,4);
            }
#undef RESIDENT_AUDIO_PUT
            ++frames;
        }
    }
    g_AkaoPlaybackMode=saved_mode;
    if(frames!=73728 || hash!=UINT64_C(0xb2a673818a54a74e)){
        fprintf(stderr,"Original resident audio LFO hash %016llx frames %u\n",(unsigned long long)hash,frames);return 0;}
    puts("Original resident audio: all five constant tables and sixteen aliased wave selectors match Disc 1; 73728 full USA MIPS voice/LFO states passed");
    return 1;
}

static int original_boot_display_data(PeDisc *disc)
{
    Pe1GameState saved=g_GameState;PeDisc *old_image=g_CdImage;
    u8 *workspace=g_CdLoadAreaBase;u16 first=g_PeImageSectorTable[71],last=g_PeImageSectorTable[72];
    int queued=g_AkaoMessageQueueCount,valid=1;PeDiscFile archive;u8 expected[2048];
    g_CdImage=disc;
    if(Asset_OpenPeImage() || !pe_disc_find(disc,"PE.IMG",&archive))return 0;
    g_GameState.flags&=~0x1000000u;
    memset(workspace,0xA5,BOOT_DISPLAY_WORK_SIZE);
    glyph_pending=glyph_poll_retry=0;
    if(CdRom_LoadBootAudio())valid=0;
    for(unsigned sector=first;sector<last;++sector) {
        if(!pe_disc_read(disc,&archive,(uint64_t)sector*2048,expected,sizeof(expected)) ||
           memcmp(workspace+(sector-first)*2048,expected,sizeof(expected)))valid=0;
    }
    for(unsigned byte=(last-first)*2048;byte<BOOT_DISPLAY_WORK_SIZE;++byte)
        if(workspace[byte]!=0xA5)valid=0;
    if(g_GameState.scene_audio.tracks.keys[0][0]!=-1 || g_GameState.scene_audio.tracks.keys[0][1]!=-1 ||
       g_GameState.scene_audio.tracks.keys[1][0]!=-1 || g_GameState.scene_audio.tracks.keys[1][1]!=-1 ||
       g_GameState.scene_audio.tracks.banks[0]!=-1 || g_GameState.scene_audio.tracks.banks[1]!=-1)valid=0;
    g_GameState.flags&=~0x1000000u;glyph_poll_retry=1;
    if(CdRom_LoadBootAudio()!=-2)valid=0;
    glyph_poll_retry=0;g_CdImage=NULL;g_GameState.flags&=~0x1000000u;
    if(CdRom_LoadBootAudio()!=-1)valid=0;
    g_CdImage=disc;
    g_CdLoadAreaBase=NULL;if(CdRom_LoadBootAudio()!=-3)valid=0;g_CdLoadAreaBase=workspace;
    g_PeImageSectorTable[72]=first;if(CdRom_LoadBootAudio()!=-3)valid=0;
    g_PeImageSectorTable[72]=first-1;if(CdRom_LoadBootAudio()!=-3)valid=0;
    g_PeImageSectorTable[72]=first+BOOT_DISPLAY_WORK_SIZE/2048+1;if(CdRom_LoadBootAudio()!=-3)valid=0;
    g_PeImageSectorTable[72]=last;g_GameState.pe_image_base_lba=UINT32_MAX;
    if(CdRom_LoadBootAudio()!=-3)valid=0;
    g_GameState=saved;g_CdImage=old_image;g_AkaoMessageQueueCount=queued;
    glyph_polls=0;glyph_pending=glyph_poll_retry=0;
    if(valid)puts("Original boot display data: full archive range and scratch guards match Disc 1; I/O errors and invalid extents terminate without loading code");
    return valid;
}

static int original_image_display_switch(PeDisc *disc,PeDiscFile *exe)
{
    PeDisc *previous=g_CdImage;Pe1GameState saved=g_GameState;
    static u16 pixels[1024*512],after[1024*512];RECT area={0,0,1024,512};
    for(unsigned i=0;i<1024*512;++i)pixels[i]=(u16)(i*13+0x1234);
    LoadImage(&area,pixels);DrawSync(0);g_CdImage=disc;
    if(Render_InitDisplayLists(0)!=-4 || Render_InitDisplayLists(3)!=-4 ||
       Render_InitDisplayLists(2)!=-4)return 0;
    StoreImage(&area,after);DrawSync(0);
    if(memcmp(after,pixels,sizeof(pixels)))return 0;
    g_CdImage=NULL;
    if(Render_InitDisplayLists(1)!=-1)return 0;
    g_CdImage=disc;
    if(Render_InitDisplayLists(1) || g_PeImageMountFlags!=1)return 0;
    StoreImage(&area,after);DrawSync(0);
    for(unsigned y=0;y<512;++y)for(unsigned x=0;x<1024;++x)
        if(after[y*1024+x]!=(x<320 && y<448?0:pixels[y*1024+x]))return 0;
    static const struct { AkaoMessageHandler function;u32 address; } bindings[]={
        {Akao_MessageNoop,0x8008ca7cu},
        {Seq_SelectPlaybackBank,0x8008ae94u},
        {Akao_InitPrimarySecondaryVoicesWithMode,0x8008c3e4u},
        {Seq_SelectPlaybackBankWithCountdown,0x8008b040u},
        {Seq_RestoreSecondaryStateAndSelect,0x8008afb8u},
        {Seq_StartIndexedNestedStream,0x8008b124u},
        {Spu_ManageVoicesCmd,0x8008b1d0u},
        {Seq_StartRelativeNestedStream,0x8008b168u},
        {Seq_StartIndexedNestedStreamWithDefaults,0x8008b0c8u},
        {Seq_StartDefaultNestedStream,0x8008b084u},
        {Seq_RestorePrimaryState,0x8008af08u},
        {Seq_SetPlaybackMode1AndRefreshVoices,0x8008c55cu},
        {Seq_SetPlaybackMode2AndRefreshVoices,0x8008c654u},
        {Seq_SetPlaybackMode4AndRefreshVoices,0x8008c5d8u},
        {Seq_SetGlobalD2B8AndDirtyAllTracks,0x8008c6d0u},
        {Seq_SetCurrentTrackField56,0x8008c70cu},
        {Seq_RestorePendingTracks,0x8008c814u},
        {Seq_DeactivatePendingTracks,0x8008c720u},
        {Spu_RestoreStoppedVoices,0x8008c9dcu},
        {Spu_VoiceStopAll,0x8008c8b8u},
        {Spu_SetVoiceVolumeImmediateMasked,0x8008b698u},
        {Spu_ParameterSlide,0x8008b780u},
        {Spu_SetVoicePanImmediateMasked,0x8008ba3cu},
        {Spu_SlideVoicePanMasked,0x8008bb1cu},
        {Spu_SetVoicePitchImmediateMasked,0x8008bdc8u},
        {Spu_SlideVoicePitchMasked,0x8008bea8u},
        {Spu_SetAllVoiceVolumeImmediate,0x8008b900u},
        {Spu_SlideAllVoiceVolume,0x8008b978u},
        {Spu_SetAllVoicePanImmediate,0x8008bc94u},
        {Spu_SlideAllVoicePan,0x8008bd08u},
        {Spu_SetAllVoicePitchImmediate,0x8008c030u},
        {Spu_SlideAllVoicePitch,0x8008c0a4u},
        {Seq_SetTrackPitchImmediate,0x8008b1fcu},
        {Seq_SlideTrackPitch,0x8008b2ccu},
        {Seq_TrackPitchSetup,0x8008b410u},
        {Seq_SetGlobalPitchImmediate,0x8008b580u},
        {Seq_SlideGlobalPitchToTarget,0x8008b5b8u},
        {Seq_SlideGlobalPitchFromStartToTarget,0x8008b624u},
        {Akao_SetGlobalD2D0Immediate,0x8008c16cu},
        {Akao_SlideGlobalD2D0ToTarget,0x8008c18cu},
        {Akao_SlideGlobalD2D0FromStartToTarget,0x8008c1f8u},
        {Akao_SetGlobalD2CCImmediate,0x8008c270u},
        {Akao_SlideGlobalD2CCToTarget,0x8008c290u},
        {Akao_SlideGlobalD2CCFromStartToTarget,0x8008c2fcu},
        {Akao_InitPrimarySecondaryVoices,0x8008c374u},
        {Spu_StopActiveVoices,0x8008c46cu},
    };
    u32 original[256];
    if(!pe_disc_read(disc,exe,0x8009c0c0u-0x80010000u+0x800,original,sizeof(original)))return 0;
    for(unsigned i=0;i<256;++i) {
        unsigned found=0;
        for(unsigned j=0;j<sizeof(bindings)/sizeof(bindings[0]);++j)
            if(Akao_MessageHandlers[i]==bindings[j].function && original[i]==bindings[j].address)++found;
        if(found!=1)return 0;
    }
    u8 silence[96];
    if(!pe_disc_read(disc,exe,0x8009b7fcu-0x80010000u+0x800,silence,sizeof(silence)) ||
       memcmp(silence,g_AkaoSilentLoopSample,32) || memcmp(silence+32,g_AkaoSilentEndSample,64))return 0;
    g_CdImage=previous;g_GameState=saved;
    puts("Original image display selection: valid Disc 1, missing Disc 2/image, invalid mode, full VRAM clear footprint and authored silence records passed");
    return 1;
}

static int original_boot_audio_lifecycle(void)
{
    g_CdImage=NULL; /* Restart must not depend on physical drive readiness. */
    for(unsigned cycle=0;cycle<2;++cycle) {
    Boot_SyncShutdown();
    if(!StopRCnt(0xf2000002u)){fprintf(stderr,"audio root counter stop failed\n");return 0;}
    g_AkaoTimerDeltaHist0=g_AkaoTimerDeltaHist1=g_AkaoTimerDeltaHist2=g_AkaoTimerDeltaHist3=0;
    g_AkaoTimerDeltaSum=0;
    for(unsigned i=0;i<4;++i) {
        DeliverEvent(0xf2000002u,2);
        if(g_AkaoTimerDeltaHist0<=0 || g_AkaoTimerDeltaSum!=g_AkaoTimerDeltaHist0+
           g_AkaoTimerDeltaHist1+g_AkaoTimerDeltaHist2+g_AkaoTimerDeltaHist3){fprintf(stderr,"audio timer history failed %u: %d %d %d %d sum %d\n",i,g_AkaoTimerDeltaHist0,g_AkaoTimerDeltaHist1,g_AkaoTimerDeltaHist2,g_AkaoTimerDeltaHist3,g_AkaoTimerDeltaSum);return 0;}
    }
    u8 uploaded[32];Psyz_SpuMemRead(0x1010,uploaded,sizeof(uploaded));
    if(memcmp(uploaded,g_AkaoSilentLoopSample,32)){fprintf(stderr,"audio silence upload mismatch\n");return 0;}
    /* Queue commands 20/30/34 must retain native script pointers and defaults. */
    u16 offsets[2048];u8 scripts[16];
    memset(offsets,0xff,sizeof(offsets));memset(scripts,0xa0,sizeof(scripts));
    offsets[6]=0;offsets[7]=8;
    unsigned short *saved_offsets=g_AkaoSampleOffsetTable;
    char *saved_samples=g_AkaoSampleDataBase;
    g_AkaoSampleOffsetTable=offsets;g_AkaoSampleDataBase=(char *)scripts;
    const u8 commands[]={0x20,0x30,0x34};
    for(unsigned i=0;i<3;++i) {
        AkaoQueueEntry command={0};command.opcode.id=commands[i];
        command.arg0.value=3;command.arg1=0x12;command.arg2=0x32;command.arg3=0x23;
        if(i==2) {command.arg0.sequence=scripts;command.second_sequence=scripts+8;}
        g_SpuActiveVoiceMask=0;g_AkaoSeqPendingFlags=0;
        g_AkaoMessageQueue[0]=command;g_AkaoMessageQueueCount=1;Akao_ProcessMessageQueue();
        if(g_AkaoMessageQueueCount || g_AkaoVoiceChannelTable[10].pc!=scripts ||
           g_AkaoVoiceChannelTable[11].pc!=scripts+8 ||
           g_AkaoVoiceChannelTable[10].key_off_mask!=(i==2?0x400u:3u) ||
           g_AkaoVoiceChannelTable[10].key_on_mask!=(i==0?0x12u:i==1?0x2000000u:0x1000000u))return 0;
    }
    g_AkaoSampleOffsetTable=saved_offsets;g_AkaoSampleDataBase=saved_samples;
    long event=g_AkaoTimerEventDesc;
    Menu_ResetEquipSlotState();g_SceneDispatchToken=0xa80651c8;
    Boot_Shutdown();
    if(g_SceneDispatchToken!=(int)0xa9400048u ||
       g_ScriptVariableBank[SCRIPT_VAR_ROOM]!=(s32)0xa80651c8u ||
       !(g_GameState.flags&0x100))return 0;
    /* EnableEvent accepts a valid slot even when unused; it must stay closed. */
    g_AkaoTimerDeltaHist0=-1234;
    EnableEvent(event);DeliverEvent(0xf2000002u,2);
    if(g_AkaoTimerDeltaHist0!=-1234)return 0;
    }
    puts("Original audio restart/reset: two SPU start/stop cycles, native event handles, indexed/direct stream commands and four dispatched AKAO timer ticks and closed-event soft reset passed without a drive");
    return 1;
}

static int original_script_movie(PeDisc *disc)
{
    _Alignas(16) static u8 work[BOOT_DISPLAY_WORK_SIZE+32],whole_frame[320*240*3],actual[320*240*3];
    Pe1GameState state=g_GameState;PeDisc *image=g_CdImage;u8 *workspace=g_CdLoadAreaBase;
    FieldActor *entity=g_CurrentEntity;ScriptNode *node_before=g_ScriptNodePool;u32 *cursor=g_ScriptCursor;
    CdlATV mix=g_AkaoCdVolume;int old_slot=g_ActiveDrawSlot,background=g_MenuSaveBgFadeState;
    DISPENV displays[2];DRAWENV draws[2];memcpy(displays,g_RenderDispEnvArray,sizeof(displays));memcpy(draws,g_RenderDrawEnvArray,sizeof(draws));
    PeDiscFile exe;u32 pointer=0;int valid=1;
    if(!pe_disc_find(disc,"SLUS_006.62",&exe) ||
       !pe_disc_read(disc,&exe,0x910a0+53*4-0x10000+0x800,&pointer,4) ||
       pointer!=0x80014e30 || g_ScriptQueueOpcodeHandlers[53]!=Script_LoadSceneData)return 0;
    g_CdImage=disc;g_CdLoadAreaBase=work;g_DrawEnabled.value=0;
    int number=0,*args[]={&number},*empty[]={NULL};u32 flags=g_GameState.flags;
    if(Script_LoadSceneData(NULL)!=-1 || Script_LoadSceneData(empty)!=-1)valid=0;
    number=-1;if(Script_LoadSceneData(args)!=-1)valid=0;
    number=47;if(Script_LoadSceneData(args)!=-1)valid=0;number=0;
    g_CdImage=NULL;if(Script_LoadSceneData(args)!=-1)valid=0;g_CdImage=disc;
    g_CdLoadAreaBase=NULL;if(Script_LoadSceneData(args)!=-1)valid=0;
    g_CdLoadAreaBase=work+1;if(Script_LoadSceneData(args)!=-1)valid=0;g_CdLoadAreaBase=work;
    if(g_GameState.flags!=flags || g_DrawEnabled.value)valid=0;
    unsigned rendered=0,rows=0;
    for(unsigned repeat=0;repeat<2;++repeat){
        memset(work,0xa5,sizeof(work));g_GameState.flags=0;g_GameStateFlags=0;
        g_DrawEnabled.value=0;g_ActiveDrawSlot=0;g_MenuSaveBgFadeState=0;
        g_MenuCardVideoDepthFlip=2;g_MenuCardVideoSize[0]=320;g_MenuCardVideoSize[1]=240;
        u32 script[]={53|(1u<<13),0,repeat?0x10000u:0u,32,0};FieldActor actor={0};
        ScriptNode node={.current.script=script,.active=1};g_CurrentEntity=&actor;g_ScriptNodePool=&node;
        glyph_reads=glyph_polls=0;glyph_read_retry=glyph_poll_retry=glyph_pending=1;
        int tick=VSync(-1);Script_RunQueue();
        if(glyph_reads || glyph_polls || VSync(-1)!=tick+1 || g_ScriptNodePool ||
           !(node.flags&0x10) || node.current.script!=script+5 || g_DrawEnabled.bits!=2 ||
           g_MenuCardVideoDepthFlip!=1 || g_MenuCardVideoDecodeMode!=2 || g_MovieIndex ||
           g_SeqElapsed!=1 || (g_GameState.flags&0x8200)!=0x8200 || g_MenuCardVideoDecodeBuffer0!=work)valid=0;
        glyph_read_retry=glyph_poll_retry=glyph_pending=0;
        Pe1SceneAudioState playing=g_GameState.scene_audio;flags=g_GameState.flags;
        if(Script_LoadSceneData(args)!=-2 || g_GameState.flags!=flags || memcmp(&playing,&g_GameState.scene_audio,sizeof(playing)))valid=0;
        unsigned frames=0;tick=VSync(-1);
        for(;;){
            unsigned decoded=g_MenuCardVideoDisplay.decodeIndex,region=g_MenuCardVideoDisplay.region;
            int mode=g_MenuCardVideoDecodeMode;unsigned bpp=mode==3?3:2;
            RECT area={0,region?0:240,mode==3?480:320,240};
            int running=MenuCardFirst_StepVideo();StoreImage(&area,actual);DrawSync(0);
            DecDCToutCallback(NULL);DecDCTin(g_MenuCardVideoDisplay.decode[decoded],mode);
            DecDCTout(whole_frame,320*240*bpp/4);if(DecDCToutSync(0))valid=0;
            DecDCToutCallback(MenuCardFirst_UploadVideoSlice);
            for(unsigned x=0;x<20;++x)for(unsigned y=0;y<15;++y)for(unsigned row=0;row<16;++row){
                if(memcmp(actual+((y*16+row)*320+x*16)*bpp,
                    whole_frame+((x*15+y)*256+row*16)*bpp,16*bpp))valid=0;++rows;}
            if(!frames){
                if(mode!=2 || g_MenuCardVideoDepthFlip!=2)valid=0;
                for(unsigned i=0;i<320*240;++i){u16 pixel;memcpy(&pixel,actual+i*2,2);if(pixel!=0x8000)valid=0;}
            }else if(mode!=3 || g_MenuCardVideoDepthFlip)valid=0;
            if(!g_MovieWide)valid=0;
            ++frames;++rendered;Draw_RenderFrame();
            if(!running || frames>150)break;
        }
        if(frames!=149 || VSync(-1)-tick!=596 || g_SeqElapsed!=150 || g_DrawEnabled.bits!=1 || !g_MenuCardVideoFinished)valid=0;
        DecDCToutCallback(NULL);MenuCardFirst_ResetDisplayState();
        if(g_DrawEnabled.value || g_MenuCardVideoDecodeBuffer0 || g_MenuCardVideoStreamRing)valid=0;
        for(unsigned i=0x55e00;i<sizeof(work);++i)if(work[i]!=0xa5){valid=0;break;}
        /* A failed file lookup closes the player and preserves scene/audio/env.
         * The next successful repeat must still take the same cold path. */
        char *name=g_MenuCardVideoTable[0].name;g_MenuCardVideoTable[0].name="\\ABSENT.STR;1";
        Pe1SceneAudioState audio=g_GameState.scene_audio;flags=g_GameState.flags;CdlATV before_mix=g_AkaoCdVolume;
        DISPENV before_display[2];DRAWENV before_draw[2];
        memcpy(before_display,g_RenderDispEnvArray,sizeof(before_display));memcpy(before_draw,g_RenderDrawEnvArray,sizeof(before_draw));
        if(Script_LoadSceneData(args)!=-3 || g_GameState.flags!=flags ||
           memcmp(&audio,&g_GameState.scene_audio,sizeof(audio)) || memcmp(&before_mix,&g_AkaoCdVolume,sizeof(before_mix)) ||
           memcmp(before_display,g_RenderDispEnvArray,sizeof(before_display)) || memcmp(before_draw,g_RenderDrawEnvArray,sizeof(before_draw)) ||
           g_MenuCardVideoDecodeBuffer0 || g_MenuCardVideoDecodeBuffer1 || g_MenuCardVideoSliceBuffer0 ||
           g_MenuCardVideoSliceBuffer1 || g_MenuCardVideoVlcTable || g_MenuCardVideoStreamRing)valid=0;
        g_MenuCardVideoTable[0].name=name;
    }
    g_GameState=state;g_CdImage=image;g_CdLoadAreaBase=workspace;g_CurrentEntity=entity;g_ScriptNodePool=node_before;g_ScriptCursor=cursor;
    g_AkaoCdVolume=mix;CdMix(&mix);g_ActiveDrawSlot=old_slot;g_MenuSaveBgFadeState=background;
    memcpy(g_RenderDispEnvArray,displays,sizeof(displays));memcpy(g_RenderDrawEnvArray,draws,sizeof(draws));
    if(!valid)fprintf(stderr,"original opcode 53/movie transition failed: frames=%u rows=%u\n",rendered,rows);
    else puts("Original movie opcode 53: real VM dispatch, two cold/recovery plays, RGB15-to-RGB24 first-frame flip, 298 frames/full VRAM, input/busy/missing-file errors and no executable overlay I/O passed");
    return valid;
}

static int original_movie_uploads(PeDisc *disc)
{
    _Alignas(16) static u8 arena[0x55e00], whole_frame[320*240*3], actual[320*240*3];
    PeDisc *old_image=g_CdImage;Pe1SceneAudioState audio=g_GameState.scene_audio;
    CdlATV mix=g_AkaoCdVolume;u32 flags=g_GameStateFlagsWord;
    int old_slot=g_ActiveDrawSlot,background=g_MenuSaveBgFadeState,valid=1;
    for(unsigned first=0;first<2;++first) for(unsigned wide_mode=0;wide_mode<2;++wide_mode) {
    VideoEntry *table=first?g_MenuCardVideoTable:g_MenuCardScreenVideoTable;
    VideoDisplay *display=first?&g_MenuCardVideoDisplay:&g_MenuCardScreenVideoDisplay;
    u8 wide=table[0].wide;
    u8 *buffers[]={arena};unsigned bytes_per_pixel=wide_mode?3:2;
    g_CdImage=disc;g_DrawEnabled.bits=0;g_ActiveDrawSlot=0;
    g_GameStateFlagsWord|=0x200;g_MenuSaveBgFadeState=0;
    table[0].wide=wide_mode;
    if(first)g_MenuCardVideoDepthFlip=0;else g_MenuCardScreenVideoDepthFlip=0;
    if(!(first?MenuCardFirst_InitVideoBuffers(1,buffers):MenuCard_InitVideoBuffers(1,buffers)) ||
       !(first?MenuCardFirst_OpenVideo(0):MenuCard_OpenVideo(0)))return 0;
    unsigned frames=0,bad_rows=0,black=0,sync_errors=0;int tick=VSync(-1);
    for(;;) {
        unsigned decoded=display->decodeIndex;
        unsigned region=display->region;
        RECT area={0,region?0:240,wide_mode?480:320,240};
        int running=first?MenuCardFirst_StepVideo():MenuCard_StepVideo();
        StoreImage(&area,actual);DrawSync(0);
        /* A whole transfer must agree with all twenty original slice callbacks. */
        DecDCToutCallback(NULL);
        DecDCTin(display->decode[decoded],wide_mode?3:2);
        DecDCTout(whole_frame,320*240*bytes_per_pixel/4);
        if(DecDCToutSync(0)){valid=0;++sync_errors;}
        DecDCToutCallback(first?MenuCardFirst_UploadVideoSlice:MenuCard_UploadVideoSlice);
        for(unsigned x=0;x<20;++x)for(unsigned y=0;y<15;++y)for(unsigned row=0;row<16;++row)
            if(memcmp(actual+((y*16+row)*320+x*16)*bytes_per_pixel,
                      whole_frame+((x*15+y)*256+row*16)*bytes_per_pixel,16*bytes_per_pixel)){valid=0;++bad_rows;}
        if(!frames) {
            if(wide_mode){for(unsigned i=0;i<320*240*3;++i)if(actual[i]){valid=0;++black;}}
            else for(unsigned i=0;i<320*240;++i){u16 pixel;memcpy(&pixel,actual+i*2,2);if(pixel!=0x8000){valid=0;++black;}}
        }
        ++frames;
        Draw_RenderFrame();
        if(!running || frames>150)break;
    }
    if((unsigned)(VSync(-1)-tick)!=149*4 || frames!=149 || g_SeqElapsed!=150 || (first?g_MenuCardVideoLastTime:g_MenuCardScreenVideoLastTime)!=150 ||
       (first?g_MenuCardVideoFinished:g_MenuCardScreenVideoFinished)!=1 || g_DrawEnabled.bits!=1)valid=0;
    if(!valid)fprintf(stderr,"movie case first=%u wide=%u frames=%u ticks=%u elapsed=%u last=%d done=%d active=%u rows=%u black=%u sync=%u\n",first,wide_mode,frames,(unsigned)(VSync(-1)-tick),g_SeqElapsed,first?g_MenuCardVideoLastTime:g_MenuCardScreenVideoLastTime,first?g_MenuCardVideoFinished:g_MenuCardScreenVideoFinished,g_DrawEnabled.bits,bad_rows,black,sync_errors);
    DecDCToutCallback(NULL);
    if(first)MenuCardFirst_ResetDisplayState();else MenuCard_ResetDisplayState();
    table[0].wide=wide;
    }
    /* FMV001 contains interleaved XA. Both original players must make it
     * available to the same CD mixer used by hardware audio callbacks. */
    int paused=Psyz_AudioIsPaused();Psyz_AudioPause();
    for(unsigned first=0;first<2;++first){
        u8 *buffers[]={arena};short samples[2048];unsigned nonzero=0,read_frames=0;
        g_CdImage=disc;g_DrawEnabled.bits=0;
        if(!(first?MenuCardFirst_InitVideoBuffers(1,buffers):MenuCard_InitVideoBuffers(1,buffers)) ||
           !(first?MenuCardFirst_OpenVideo(1):MenuCard_OpenVideo(1))){valid=0;break;}
        for(unsigned i=0;i<100;++i){
            size_t got=Psyz_CdPullSamples(samples,1024);read_frames+=(unsigned)got;
            for(size_t j=0;j<got*2;++j)if(samples[j])++nonzero;
        }
        if(read_frames!=102400 || !nonzero){fprintf(stderr,"Movie XA first=%u frames=%u nonzero=%u\n",first,read_frames,nonzero);valid=0;}
        DecDCToutCallback(NULL);
        if(first)MenuCardFirst_ResetDisplayState();else MenuCard_ResetDisplayState();
        if(Psyz_CdPullSamples(samples,1024)!=0){fprintf(stderr,"Movie XA continued after stop\n");valid=0;}
    }
    if(!paused)Psyz_AudioUnpause();
    g_MenuSaveBgFadeState=background;
    g_GameStateFlagsWord=flags;g_GameState.scene_audio=audio;g_ActiveDrawSlot=old_slot;
    g_AkaoCdVolume=mix;CdMix(&mix);g_CdImage=old_image;
    if(!valid)fprintf(stderr,"original movie MDEC/slice/VRAM regression\n");
    else puts("Original movies: both original players in RGB15/RGB24, 149 frames per case through real OpenVideo/StepVideo, 11920 total slice uploads, full VRAM regions and 15fps render path passed headlessly");
    return valid;
}

static int original_video_stream_errors(void)
{
    static u8 image[2352*2];
    _Alignas(16) static u8 arena[0x20000];
    PeDisc *mounted=g_CdImage;
    PeDisc disc={0};disc.stream=tmpfile();disc.sector_size=2352;
    disc.image_size=sizeof(image);
    if(!disc.stream)return 0;
    g_CdImage=&disc;g_MenuCardScreenVideoStreamRing=arena;
    VideoEntry entry={"test",0,1,300,0,0,{0}};
    g_MenuCardScreenVideoEntry=&entry;
    g_MenuCardScreenVideoSize[0]=320;g_MenuCardScreenVideoSize[1]=240;
    VideoDisplay display={0};int valid=1;
    for(unsigned test=0;test<15;++test) {
        memset(image,0,sizeof(image));
        VideoFrame first={.id=0x160,.type=0x8001,.nSectors=2,.frameCount=1,
                          .frameSize=4032,.width=320,.height=240};
        VideoFrame second=first;second.secCount=1;
        switch(test) {
        case 0:first.nSectors=0;break;
        case 1:first.nSectors=66;break;
        case 2:first.frameSize=8;break;
        case 3:first.frameSize=4033;break;
        case 4:first.width=0;break;
        case 5:first.width=321;break;
        case 6:first.height=241;break;
        case 7:first.secCount=1;break;
        case 8:second.secCount=0;break;
        case 9:second.frameCount=2;break;
        case 10:second.width=304;break;
        case 11:second.frameSize=4000;break;
        case 12:second.nSectors=3;break;
        case 13:break; /* Advertised MDEC output exceeds its 0xFA00 arena. */
        case 14:first.frameCount=0x7fffffff;break;
        }
        memcpy(image+24,&first,sizeof(first));memcpy(image+2352+24,&second,sizeof(second));
        u32 command=test==13?0x3800ffff:0x38000020;
        memcpy(image+56,&command,4);
        rewind(disc.stream);
        if(fwrite(image,1,sizeof(image),disc.stream)!=sizeof(image) || fflush(disc.stream))valid=0;
        g_MenuCardScreenVideoFile=(PeDiscFile){.sector=0,.size=4096};
        g_MenuCardScreenVideoSector=0;
        if(MenuCard_UpdateVideoFrame(&display))valid=0;
    }
    /* Image extent rejection, incomplete frame and a cursor already at EOF. */
    for(unsigned test=0;test<3;++test) {
        g_MenuCardScreenVideoFile=(PeDiscFile){.sector=0,.size=test==1?2048:4096};
        g_MenuCardScreenVideoSector=test==2?2:0;
        disc.image_size=test==0?100:sizeof(image);
        if(MenuCard_UpdateVideoFrame(&display))valid=0;
    }
    g_CdImage=NULL;g_DrawEnabled.bits=0;
    if(MenuCard_PlayVideo(-1) || MenuCard_PlayVideo(47) || MenuCard_OpenVideo(0) ||
       MenuCard_PlayVideo(0) || g_DrawEnabled.bits || (g_GameStateFlagsWord&0x200))valid=0;
    g_MenuCardScreenVideoStreamRing=NULL;g_MenuCardScreenVideoEntry=NULL;
    g_CdImage=mounted;fclose(disc.stream);
    if(!valid)fprintf(stderr,"original movie stream rejection regression\n");
    return valid;
}

/* Output goldens and yield counts come from the retail VLC_C procedure. */
static int original_video_decoder(PeDisc *disc)
{
    static const struct { const char *name; unsigned frames; uint64_t hash; unsigned chunks; } movies[]={
        {"FMV1/FMV000.STR",150,UINT64_C(0x30a713f43d9413a7),1711},
        {"FMV1/FMV001.STR",2,UINT64_C(0xef1b245b6adc85b3),1711},
        {"FMV1/FMV002.STR",2,UINT64_C(0x392138382a56d9a5),1420},
        {"FMV1/FMV003.STR",2,UINT64_C(0x8ef0d2fb8bf52fc),1674},
        {"FMV1/FMV004.STR",2,UINT64_C(0x6d2ff28b9e23591a),1581},
        {"FMV1/FMV005.STR",2,UINT64_C(0x7ef08eb0f7c6edad),1642},
        {"FMV1/FMV006.STR",2,UINT64_C(0x6b815afd94529e92),1929},
        {"FMV1/FMV006C.STR",2,UINT64_C(0xe97a75ac7bef8804),1939},
        {"FMV1/FMV007.STR",2,UINT64_C(0x392138382a56d9a5),1420},
        {"FMV1/FMV008.STR",2,UINT64_C(0xc5dd9de216b31929),1701},
        {"FMV1/FMV009.STR",2,UINT64_C(0xbb82491ab3017b5a),1947},
        {"FMV1/FMV010.STR",2,UINT64_C(0xafc44a597db8d24e),1739},
        {"FMV1/FMV011.STR",2,UINT64_C(0xb7a90cc9ea87b03d),1614},
        {"FMV1/FMV012.STR",2,UINT64_C(0x946efb22d923a428),1592},
        {"FMV1/FMV013.STR",2,UINT64_C(0x557e4491d4ccc846),2034},
        {"FMV1/FMV014.STR",2,UINT64_C(0x49efb4fe4b2d2329),1622},
        {"FMV1/FMV015.STR",2,UINT64_C(0x4cbf8a26f125337c),2055},
        {"FMV1/FMV016A.STR",2,UINT64_C(0x9de5f375d2863e0d),2130},
        {"FMV1/FMV016B.STR",2,UINT64_C(0x8c4308a3c6cda742),1905},
        {"FMV1/FMV017A.STR",2,UINT64_C(0xd7107e60165c5819),1615},
        {"FMV1/FMV017B.STR",2,UINT64_C(0xbea326aefe8b8cfa),1729},
    };
    static u8 frame[0x20000], output[0x40010], partial[0x40010];
    static u16 table[0x8800];
    _Alignas(16) static u8 stream[0x20000];
    PeDisc *old_image=g_CdImage;
    Pe1SceneAudioState old_audio=g_GameState.scene_audio;
    CdlATV old_mix=g_AkaoCdVolume;
    g_CdImage=disc;
    g_MenuCardScreenVideoStreamRing=stream;
    VideoDisplay display={0};
    MdecDiag_ExpandDecoderTable(table);
    DecDCTvlcSize2(0);
    if(DecDCTvlcSize2(2)!=0xffffff || DecDCTvlcSize2(17)!=4 ||
       DecDCTvlcSize2(256)!=34 || DecDCTvlcSize2(1)!=512 ||
       DecDCTvlcSize2(-1)!=0xffffff)return 0;
    for(unsigned movie=0;movie<sizeof(movies)/sizeof(movies[0]);++movie) {
        PeDiscFile file; u8 raw[2352]; unsigned frames=0,chunks=0,part=0,parts=0;
        u32 frame_number=0,frame_size=0; size_t stored=0;
        uint64_t hash=UINT64_C(14695981039346656037);
        if(!pe_disc_find(disc,movies[movie].name,&file))return 0;
        g_MenuCardScreenVideoFile=file;g_MenuCardScreenVideoSector=0;
        g_MenuCardScreenVideoEntry=&g_MenuCardScreenVideoTable[movie];
        g_MenuCardScreenVideoLastTime=0;g_MenuCardScreenVideoFinished=0;
        g_MenuCardScreenVideoSize[0]=320;g_MenuCardScreenVideoSize[1]=240;
        for(u32 sector=0;sector<(file.size+2047u)/2048u && frames<movies[movie].frames;++sector) {
            if(!pe_disc_read_raw(disc,&file,sector,raw))return 0;
            u32 magic;memcpy(&magic,raw+24,4);
            if(magic!=0x80010160)continue; /* XA audio is interleaved. */
            u16 index,count;u32 number,size;
            memcpy(&index,raw+28,2);memcpy(&count,raw+30,2);
            memcpy(&number,raw+32,4);memcpy(&size,raw+36,4);
            if(!index) {part=0;parts=count;frame_number=number;frame_size=size;stored=0;}
            if(index!=part || count!=parts || number!=frame_number || size!=frame_size ||
               stored+2016>sizeof(frame) || frame_size>sizeof(frame)-64)return 0;
            memcpy(frame+stored,raw+56,2016);stored+=2016;++part;
            if(part!=parts)continue;
            if(frame_size>stored || frame_size<12)return 0;
            g_MovieWide=frames&1;
            void *native_frame=MenuCard_UpdateVideoFrame(&display);
            if(native_frame!=stream+sizeof(VideoFrame) || memcmp(native_frame,frame,frame_size) ||
               g_MenuCardScreenVideoLastTime!=(s16)frame_number ||
               g_MenuCardScreenVideoFinished!=(frame_number>=(u32)g_MenuCardScreenVideoEntry->end) ||
               display.regions[0].w!=(g_MovieWide?480:320) ||
               display.regions[1].w!=display.regions[0].w || display.rect.h!=240 ||
               display.regions[0].h!=240 || display.regions[1].h!=240)return 0;
            u16 words;memcpy(&words,frame,2);size_t bytes=4+(size_t)words*4;
            if(bytes+16>sizeof(output))return 0;
            memset(output,0xa5,sizeof(output));DecDCTvlcSize2(0);
            if(DecDCTvlc2(frame,output,table)!=0)return 0;
            for(size_t i=0;i<bytes;++i)hash=(hash^output[i])*UINT64_C(1099511628211);
            for(size_t i=bytes;i<sizeof(output);++i)if(output[i]!=0xa5)return 0;
            if(frame_number<=2)for(unsigned mode=0;mode<3;++mode) {
                const int limits[]={2,17,256};unsigned calls=1;
                memset(partial,0xa5,sizeof(partial));DecDCTvlcSize2(limits[mode]);
                int result=DecDCTvlc2(frame,partial,table);
                while(result==1 && calls<10000) {result=DecDCTvlc2(NULL,NULL,table);++calls;}
                if(result || memcmp(output,partial,sizeof(output)))return 0;
                chunks+=calls;
            }
            ++frames;
        }
        if(frames!=movies[movie].frames || hash!=movies[movie].hash || chunks!=movies[movie].chunks) {
            fprintf(stderr,"VLC mismatch %s frames=%u chunks=%u hash=%llx\n",movies[movie].name,
                    frames,chunks,(unsigned long long)hash);return 0;
        }
    }
    g_MenuCardScreenVideoSector=(g_MenuCardScreenVideoFile.size+2047u)/2048;
    if(MenuCard_UpdateVideoFrame(&display))return 0;
    g_CdImage=NULL;if(MenuCard_UpdateVideoFrame(&display))return 0;
    g_CdImage=disc;g_MenuCardScreenVideoStreamRing=NULL;
    if(MenuCard_UpdateVideoFrame(&display))return 0;
    g_CdImage=old_image;g_GameState.scene_audio=old_audio;
    g_AkaoCdVolume=old_mix;CdMix(&old_mix);
    /* V3 fixture: all DC magnitude sizes/signs and three predictors, with AC escapes. */
    static const u8 version3[]={
        0x00,0x01,0x00,0x38,0x1f,0x00,0x03,0x00,0x00,0x01,0x80,0x00,0x20,0x40,0x08,0xe8,
        0x35,0x08,0x01,0x01,0xa0,0x89,0x41,0x20,0x04,0x94,0x3e,0x0a,0x81,0x40,0x4c,0x89,
        0x39,0x10,0x02,0x58,0x31,0x08,0x41,0x10,0xe0,0x26,0x28,0x08,0x41,0xf4,0xa1,0x05,
        0x10,0xa0,0x4a,0x62,0x06,0xc1,0x90,0xa7,0x72,0x10,0xc1,0xa9,0xad,0x07,0x10,0x90,
        0x09,0x83,0x08,0xc1,0xb0,0xb3,0x24,0x04,0xdc,0xda,0x9b,0x10,0x80,0x9a,0x8f,0x42,
        0xc1,0x2b,0xbf,0x0a,0x04,0xa8,0x0a,0x2d,0x10,0xbc,0x5b,0xbc,0x10,0x80,0x8b,0xc4,
        0x10,0xbc,0xbb,0xcc,0x21,0x00,0xd6,0xa9,0x43,0xf0,0x6c,0x74,0x87,0x00,0x5b,0x2a,
        0x0e,0xc1,0xbc,0xd7,0x43,0x00,0xaf,0xd6,0x10,0x7c,0xdb,0xfd,0x08,0x80,0x05,0x83,
        0x04,0xdf,0x8e,0x43,0x02,0xe0,0xcd,0x22,0xc1,0x77,0xe9,0x11,0x00,0xbe,0x26,0x11,
        0xef,0xcb,0x12,0xc1,0xbc,0xef,0x22,0x00,0x57,0x6e,0x04,0xbf,0xd6,0x4f,0x00,0xf0,
        0x3c,0x8a,0xfc,0x5e,0x4f,0x11,0xf0,0xbb,0x45,0x00,0xaf,0x5f,0xc1,0xdf,0x01,0x16,
        0x00,0xbe,0xb4,0x08,0xf7,0x25,0x45,0xf0,0xef,0xc1,0x02,0x80,0x15,0x2f,0xfc,0x7d,
        0x80,0x11,0xf8,0xdb,0x11,0x00,0x0b,0x89,0xfc,0xfb,0x91,0x11,0xf0,0x3b,0x23,0x00,
        0xd7,0x32,0xf0,0xef,0x86,0x46,0xc0,0x6f,0x8d,0x00,0x5f,0x4e,0xc1,0xbf,0x1f,0x1b,
        0x46,0x80,0xa0,0xe8,0xc2,0x11,0x02,0x5a,0x51,0x39,0x47,0x40,0xe8,0x4a,0xed,0x08,
        0x01,0x75,0x31,0x1e,0x23,0x90,0x93,0xd6,0x7c,0x04,0x00,0xde,0xdd,0x8f,0x12,0x44,
        0xd8,0x03,0x41,0x02,0x10,0x81,0x50,0x48,0x04,0xe8,0x1a,0x87,0x48,0xb0,0x64,0x92,
        0x8b,0x04,0x70,0x32,0xd3,0x48,0x04,0xe4,0x4a,0x8f,0x49,0x70,0x6c,0x15,0x24,0x01,
        0xb7,0xd8,0x95,0x04,0xa0,0x6e,0x5d,0x12,0xf0,0xea,0x98,0x49,0x01,0x6a,0xe4,0x26,
        0x04,0xaf,0x9e,0x9d,0x04,0xe0,0xaa,0x9f,0x04,0xef,0xb6,0xa1,0x09,0xc0,0x85,0x47,
        0x12,0xbc,0x3b,0x97,0x25,0x00,0xd6,0x3e,0x4a,0xf0,0x6f,0x9e,0x12,0x00,0xcb,0xaf,
        0x04,0xdf,0xfe,0xad,0x02,0xe0,0x05,0x58,0xc1,0x77,0x85,0x2c,0x00,0xb8,0x84,0x96,
        0xf0,0x5d,0x62,0x4b,0x80,0xef,0xb8,0x04,0xfb,0x3a,0x4b,0xf0,0x6f,0xa4,0x09,0x00,
        0xa5,0x78,0xc1,0xef,0x97,0x2f,0x00,0xbc,0x03,0x26,0xbf,0x57,0xc2,0x04,0xfc,0x76,
        0x13,0x00,0x0b,0x12,0xf0,0xf7,0x68,0x4c,0x80,0xef,0x64,0x02,0x7d,0x4d,0x13,0xfc,
        0x9b,0x2a,0x00,0xe0,0x96,0x99,0x7f,0x5f,0xce,0x04,0xfe,0xbe,0x04,0x00,0xca,0xd0,
        0xff,0xfe,0xd2,0x04,0xfc,0xd6,0x09,0x00,0xc5,0xa9,0xfc,0xfb,0x5b,0x13,0xf0,0xbb,
        0x26,0x00,0xd7,0xc7,0xf0,0xef,0xb0,0x4d,0xfc,0x6f,
    };
    const int limits[]={0,2,17,256};const unsigned expected_calls[]={1,55,10,1};
    for(unsigned mode=0;mode<4;++mode) {
        memcpy(frame,version3,sizeof(version3));memset(frame+sizeof(version3),0,64);
        memset(output,0xa5,sizeof(output));DecDCTvlcSize2(limits[mode]);
        int result=DecDCTvlc2(frame,output,table);unsigned calls=1;
        while(result==1 && calls<10000) {result=DecDCTvlc2(NULL,NULL,table);++calls;}
        uint64_t hash=UINT64_C(14695981039346656037);
        for(unsigned i=0;i<1028;++i)hash=(hash^output[i])*UINT64_C(1099511628211);
        if(result || calls!=expected_calls[mode] || hash!=UINT64_C(0x0fa0f433aa9048a1))return 0;
        for(unsigned i=1028;i<sizeof(output);++i)if(output[i]!=0xa5)return 0;
    }
    DecDCTvlcSize2(0);
    if(DecDCTvlc2(NULL,NULL,table)!=-1 || DecDCTvlc2(frame,NULL,table)!=-1 ||
       DecDCTvlc2(frame,output,NULL)!=-1)return 0;
    memset(frame,0,16);memset(output,0xa5,sizeof(output));
    if(DecDCTvlc2(frame,output,table)!=-1 || DecDCTvlc2(NULL,NULL,table)!=-1)return 0;
    for(unsigned i=4;i<sizeof(output);++i)if(output[i]!=0xa5)return 0;
    puts("Original VLC_C: 190 real STR frames, 21 movie hashes, incremental yield counts and V3 differential/escape fixture match USA MIPS");
    return 1;
}

static int original_video_buffers(PeDisc *disc)
{
    static u8 arena[0x55e00], second[0x16a00];
    static u16 saved[1024*512], pixels[1024*512], expected[1024*512];
    RECT whole={0,0,1024,512};
    DISPENV displays[2]; DRAWENV draws[2];
    Pe1SceneAudioState audio=g_GameState.scene_audio;
    u32 flags=g_GameStateFlagsWord;
    memcpy(displays,g_RenderDispEnvArray,sizeof(displays));
    memcpy(draws,g_RenderDrawEnvArray,sizeof(draws));
    StoreImage(&whole,saved);DrawSync(0);
    int valid=1;
    PeDiscFile archive;
    static u8 bank[133*2048];
    if(!pe_disc_find(disc,"PE.IMG",&archive) ||
       !pe_disc_read(disc,&archive,978ull*2048,bank,sizeof(bank)))return 0;
    for(unsigned i=0;i<47;++i) {
        const u8 *row=bank+0x41e10+i*20;
        u32 name; s16 speed,end; u16 x,y;
        memcpy(&name,row,4);memcpy(&speed,row+6,2);memcpy(&end,row+8,2);
        memcpy(&x,row+10,2);memcpy(&y,row+12,2);
        VideoEntry *entry=&g_MenuCardScreenVideoTable[i];
        if(name<0x8018eff0u || name-0x8018eff0u>=sizeof(bank) ||
           !memchr(bank+(name-0x8018eff0u),0,sizeof(bank)-(name-0x8018eff0u)))return 0;
        if(strcmp(entry->name,(char *)bank+(name-0x8018eff0u)) || entry->wide!=row[4] ||
           entry->speed!=speed || entry->end!=end || entry->x!=x || entry->y!=y ||
           memcmp(entry->reserved0e,row+14,6))valid=0;
    }
    static u8 first_bank[4*2048];
    if(!pe_disc_read(disc,&archive,965ull*2048,first_bank,sizeof(first_bank)))return 0;
    for(unsigned i=0;i<47;++i){
        const u8 *row=first_bank+0x1738+i*20;u32 name;s16 speed,end;u16 x,y;
        memcpy(&name,row,4);memcpy(&speed,row+6,2);memcpy(&end,row+8,2);memcpy(&x,row+10,2);memcpy(&y,row+12,2);
        VideoEntry *entry=&g_MenuCardVideoTable[i];
        if(name<0x80120D00u || name-0x80120D00u>=sizeof(first_bank) ||
           !memchr(first_bank+(name-0x80120D00u),0,sizeof(first_bank)-(name-0x80120D00u)))return 0;
        if(strcmp(entry->name,(char *)first_bank+(name-0x80120D00u)) || entry->wide!=row[4] || entry->speed!=speed ||
           entry->end!=end || entry->x!=x || entry->y!=y || memcmp(entry->reserved0e,row+14,6))valid=0;
    }
    /* Golden is the complete output of USA 0x8010BD4C in Unicorn MIPS. */
    static struct { u16 before[8], table[0x8800], after[8]; } vlc;
    memset(&vlc,0xa5,sizeof(vlc));
    MdecDiag_ExpandDecoderTable(vlc.table);
    uint64_t expanded_hash=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<sizeof(vlc.table);++i)
        expanded_hash=(expanded_hash^((u8 *)vlc.table)[i])*UINT64_C(1099511628211);
    if(expanded_hash!=UINT64_C(0x62fd6458f3443095))valid=0;
    for(unsigned i=0;i<8;++i)if(vlc.before[i]!=0xa5a5 || vlc.after[i]!=0xa5a5)valid=0;
    for(int split=1;split<=2;++split)for(int skip=0;skip<2;++skip) {
        u8 *buffers[2]={arena,second};
        memset(arena,0xa5,sizeof(arena));memset(second,0xa5,sizeof(second));
        for(unsigned i=0;i<1024*512;++i)pixels[i]=(u16)(i*37+(i>>10)*19);
        memcpy(expected,pixels,sizeof(expected));
        LoadImage(&whole,pixels);DrawSync(0);
        g_DrawEnabled.bits=0;g_SeqElapsed=123;
        g_GameStateFlagsWord=skip?0x08000000:0;
        if(MenuCard_InitVideoBuffers(0,buffers) || MenuCard_InitVideoBuffers(3,buffers) ||
           MenuCard_InitVideoBuffers(1,NULL))valid=0;
        if(!MenuCard_InitVideoBuffers(split,buffers))valid=0;
        if(g_MenuCardScreenVideoDecodeBuffer0!=arena ||
           g_MenuCardScreenVideoDecodeBuffer1!=arena+0xfa00 ||
           g_MenuCardScreenVideoStreamRing!=arena+0x1f400 ||
           g_MenuCardScreenVideoVlcTable!=(split==1?arena+0x3f400:second) ||
           g_MenuCardScreenVideoSliceBuffer0!=(split==1?arena+0x50400:second+0x11000) ||
           g_MenuCardScreenVideoSliceBuffer1!=(split==1?arena+0x53100:second+0x13d00) ||
           g_GameState.scene_audio.tracks.movie_active!=1 || g_SeqElapsed!=0 ||
           g_MenuCardScreenVideoLastTime!=-1 ||
           g_GameState.scene_audio.tracks.movie_volume!=152 ||
           MenuCard_InitVideoBuffers(split,buffers))valid=0;
        for(unsigned y=0;y<256;++y)memcpy(expected+y*1024+512,pixels+y*1024+320,192*2);
        if(!skip)for(unsigned y=0;y<64;++y)
            memcpy(expected+(y+256)*1024+512,pixels+(y+448)*1024,320*2);
        StoreImage(&whole,pixels);DrawSync(0);
        if(memcmp(pixels,expected,sizeof(pixels)))valid=0;
        for(int index=0;index<2;++index)for(int wide=0;wide<2;++wide) {
            MenuCard_SetVideoDisplay(index,wide);
            if(g_RenderDispEnvArray[index].disp.w!=320 ||
               g_RenderDispEnvArray[index].disp.y!=(index?0:240) ||
               g_RenderDispEnvArray[index].isrgb24!=wide ||
               g_RenderDrawEnvArray[index].clip.w!=320 ||
               g_RenderDrawEnvArray[index].clip.y!=(index?240:0))valid=0;
        }
        /* The movie overwrites the graphics whose backups must survive. */
        for(unsigned y=0;y<256;++y)memset(pixels+y*1024+320,0,192*2);
        if(!skip)for(unsigned y=0;y<64;++y)memset(pixels+(y+448)*1024,0,320*2);
        LoadImage(&whole,pixels);DrawSync(0);
        MenuCard_ResetDisplayState();
        if(g_DrawEnabled.bits || g_MenuCardScreenVideoDecodeBuffer0 ||
           g_MenuCardScreenVideoDecodeBuffer1 || g_MenuCardScreenVideoStreamRing ||
           g_MenuCardScreenVideoVlcTable || g_MenuCardScreenVideoSliceBuffer0 ||
           g_MenuCardScreenVideoSliceBuffer1 ||
           memcmp(displays,g_RenderDispEnvArray,sizeof(displays)) ||
           memcmp(draws,g_RenderDrawEnvArray,sizeof(draws)))valid=0;
        StoreImage(&whole,pixels);DrawSync(0);
        if(memcmp(pixels,expected,sizeof(pixels)))valid=0;
        for(unsigned i=0;i<sizeof(arena);++i)if(arena[i]!=0xa5)valid=0;
        for(unsigned i=0;i<sizeof(second);++i)if(second[i]!=0xa5)valid=0;
    }
    LoadImage(&whole,saved);DrawSync(0);
    g_GameState.scene_audio=audio;g_GameStateFlagsWord=flags;
    if(!valid)fprintf(stderr,"original movie buffer/display regression\n");
    return valid;
}

static int original_received_item_opcode(PeDisc *disc,PeDiscFile *exe)
{
    const u32 patterns[]={0,1,0x7fff,0x8000,0xffff,0x80000000,0xffffffff,
        0x010c0000,0xa5a55a5a,0x12345678,0x7fffffff,0x600000};
    const int items[]={6,68,150,1},node_flags[]={0,0x20,0x80,0xa0,0xffff};
    const u32 geom_flags[]={0,0x2000,0xffffffff};
    MenuWidgetNode nodes[24];memcpy(nodes,g_MenuWidgetNodePool,sizeof(nodes));
    MenuWidgetNode *heads[]={g_MenuWidgetActiveListHead,g_MenuWidgetFreeListHead,g_MenuWidgetCurrentNode,g_MenuWidgetSavedNode,g_MenuActiveWidget};
    MenuWidgetSimpleDescriptor simple[0x41];memcpy(simple,g_MenuWidgetSimpleDescriptors,sizeof(simple));
    MenuWidgetGridDescriptor grid[0x41];memcpy(grid,g_MenuWidgetGridDescriptors,sizeof(grid));
    MenuWidgetSavedLayout layouts[72];memcpy(layouts,g_MenuWidgetColumnLayoutTable,sizeof(layouts));
    MenuQueueEntry queue[MENU_EVENT_QUEUE_SIZE];memcpy(queue,g_MenuEventQueuePool,sizeof(queue));
    MenuQueueEntry *queue_heads[]={g_MenuEventQueueHead,g_MenuEventQueueTail,g_MenuEventQueueFreeList};
    int input[]={g_MenuInputActive,g_MenuInputPollingPaused,g_MenuInputHeldStatusMask,g_InvItemUsableFlag};
    ItemDataRecord *saved_item=g_MenuSelectedItemData;
    u32 saved_game=g_GameState.flags,saved_global=g_GameStateFlags;int saved_geom=g_GeomScroll.state.flags;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    FieldActor actor={0},*saved_actor=g_CurrentEntity;
    if(!pe_disc_read(disc,exe,0x92478-0x10000+0x800,g_MenuWidgetSimpleDescriptors,sizeof(simple)) ||
       !pe_disc_read(disc,exe,0x92888-0x10000+0x800,g_MenuWidgetGridDescriptors,sizeof(grid)))return 0;
    memset(g_MenuWidgetColumnLayoutTable,0,sizeof(layouts));g_CurrentEntity=&actor;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;int valid=1;
    if(g_ScriptQueueOpcodeHandlers[232]!=Script_ShowReceivedItem)valid=0;
    for(unsigned item=0;item<4;++item)for(unsigned game=0;game<12;++game)
    for(unsigned global=0;global<8;++global)for(unsigned flag=0;flag<5;++flag)
    for(unsigned geom=0;geom<3;++geom){
        MenuWidget_InitPool();g_MenuSelectedItemData=NULL;
        g_GameState.flags=patterns[game];g_GameStateFlags=patterns[global];g_GeomScroll.state.flags=(s32)geom_flags[geom];
        u32 program[20]={0};g_ScriptCursor=program+16;
        ScriptNode node={.flags=node_flags[flag],.active=7};g_ScriptNodePool=&node;
        int id=items[item];int *args[]={&id};u32 result=Script_ShowReceivedItem(args);
        const u32 words[]={result,g_GameState.flags,g_GameStateFlags,(u32)g_GeomScroll.state.flags,
            node.flags,(u32)node.active,(u32)((u8 *)g_ScriptCursor-(u8 *)program)};
        for(unsigned word=0;word<7;++word)for(unsigned byte=0;byte<4;++byte)
            hash=(hash^((words[word]>>(byte*8))&255))*UINT64_C(1099511628211);
        int opened=!(patterns[game]&0x1000) && (node_flags[flag]&0x20);
        if(opened){
            ItemDataRecord *data=Item_LookupBaseData(id-1);unsigned count=0;
            if(g_MenuSelectedItemData!=data || !g_MenuWidgetCurrentNode ||
               g_MenuWidgetCurrentNode->selected_base!=(data->kind<10?5:0x37))valid=0;
            for(MenuWidgetNode *widget=g_MenuWidgetActiveListHead;widget;widget=widget->next){
                if(++count>24){valid=0;break;}
                if(widget->mode==1 && widget->appearance.gradientPoints)valid=0;
            }
            if(count<(data->kind<10?5u:2u))valid=0;
        }else if(g_MenuWidgetActiveListHead || g_MenuWidgetCurrentNode || g_MenuSelectedItemData)valid=0;
        ++cases;
    }
    if(cases!=5760 || hash!=UINT64_C(0xa8b80c9059a27965)){
        fprintf(stderr,"Received item CPU mismatch: cases=%u hash=%016llx\n",cases,(unsigned long long)hash);valid=0;
    }
    MenuWidget_InitPool();g_GameState.flags=g_GameStateFlags=0;g_GeomScroll.state.flags=0;
    u32 program[]={232|(1u<<13),0,6,32,0};
    ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;
    Script_RunQueue();
    if(node.current.script!=program || node.active!=1 || node.flags!=0xa0 || g_ScriptNodePool)valid=0;
    g_ScriptNodePool=&node;Script_RunQueue();
    if(node.current.script!=program+5 || node.active || node.flags!=0x90 || g_ScriptNodePool ||
       g_GameState.flags!=0x9000 || g_GameStateFlags!=4 || g_GeomScroll.state.flags!=0x7000 ||
       !g_MenuWidgetCurrentNode || g_MenuWidgetCurrentNode->selected_base!=0x37)valid=0;
    /* Equip confirmation can close the complete menu while its callback is
     * still running. Exercise both original command paths and cancellation. */
    {
        extern int g_MenuCommandResult;
        PlayerInventoryState inventory=g_PlayerInventoryState;
        InventoryItemWorkspace workspace=g_InventoryItemWorkspace;
        BattleCmdEntry commands[4];memcpy(commands,g_BattleCmdStack,sizeof(commands));
        BattleCmdEntry *command_top=g_BattleCmdStackTop;
        FieldActor *player=g_PlayerEntity,entity={0};Combatant core={0};entity.state=(FieldActorState *)&core;
        s16 *item_ptr=g_InvItemPtr,*override=g_InvActiveListOverride;
        int limit=g_InvSlotLimit,packed=g_InvPackedListCount,bonus=g_InvBonusSlotCount;
        int battle=g_MenuBattleEquipMode,result=g_MenuCommandResult,pending=g_MenuInputPausePending;
        u8 *target=g_MenuActiveListTarget;u32 *bits=g_InvSelectionBits;
        int words=g_InvSelectionBitWords;u32 selection[2];memcpy(selection,g_InvSelectionBitStorage,sizeof(selection));
        for(unsigned armor=0;armor<2;++armor)for(unsigned confirm=0;confirm<2;++confirm){
            memset(&g_PlayerInventoryState,0,sizeof(g_PlayerInventoryState));
            g_AyaSaveState.inventory_slot_count=4;g_InvBonusSlotCount=0;
            for(unsigned j=0;j<4;++j){
                g_AyaInventoryItems[j]=256+j;
                g_InventoryRuntime.equipment[j].kind=j<2?1:9;
                g_InventoryRuntime.equipment[j].iconId=1;
            }
            g_InventoryRuntime.tracked[0]=0;g_InventoryRuntime.tracked[2]=2;
            g_InvItemPtr=g_AyaInventoryItems;g_InvSlotLimit=4;g_InvActiveListOverride=NULL;
            g_InvPackedListCount=2;g_InvPackedList[0]=armor?2:0;g_InvPackedList[1]=armor?3:1;
            g_PlayerEntity=&entity;g_MenuActiveListTarget=NULL;g_MenuBattleEquipMode=1;
            g_MenuCommandResult=0;g_MenuInputPausePending=0;g_InvItemUsableFlag=!armor;
            BattleCmd_ResetTableCursor();MenuWidget_InitPool();
            MenuWidgetNode *panel=MenuWidget_CreateSimpleNode(7,NULL,NULL,0);
            MenuWidgetNode *list=MenuWidget_CreateNode(7,panel,panel);
            list->grid_width=1;list->cursor_x=0;list->cursor_y=1;MenuWidget_SetCurrentNode(list);
            Menu_OnEquipConfirm(NULL,confirm);
            if(confirm){
                if(g_MenuCommandResult!=(armor?0x198:0x197) || g_MenuWidgetActiveListHead ||
                   g_MenuWidgetCurrentNode || g_BattleCmdStackTop!=g_BattleCmdStack+1 ||
                   g_BattleCmdStack[0].header.word!=(armor?3u:2u) ||
                   g_BattleCmdStack[0].payload.equip_restore.item_data!=&g_InventoryRuntime.equipment[armor?2:0] ||
                   g_InventoryRuntime.tracked[armor?2:0]!=(armor?3:1) ||
                   g_MenuInputPausePending!=!armor)valid=0;
            }else if(g_MenuCommandResult || g_MenuWidgetCurrentNode!=list ||
                     g_BattleCmdStackTop!=g_BattleCmdStack || g_MenuInputPausePending)valid=0;
        }
        g_PlayerInventoryState=inventory;g_InventoryItemWorkspace=workspace;
        memcpy(g_BattleCmdStack,commands,sizeof(commands));g_BattleCmdStackTop=command_top;
        g_PlayerEntity=player;g_InvItemPtr=item_ptr;g_InvActiveListOverride=override;
        g_InvSlotLimit=limit;g_InvPackedListCount=packed;g_InvBonusSlotCount=bonus;
        g_MenuBattleEquipMode=battle;g_MenuCommandResult=result;g_MenuInputPausePending=pending;
        g_MenuActiveListTarget=target;g_InvSelectionBits=bits;g_InvSelectionBitWords=words;
        memcpy(g_InvSelectionBitStorage,selection,sizeof(selection));
        if(valid)puts("Original equip confirmation: weapon/armor command, undo payload, cleared widget pool, deferred input and cancellation passed");
    }
    memcpy(g_MenuWidgetNodePool,nodes,sizeof(nodes));
    g_MenuWidgetActiveListHead=heads[0];g_MenuWidgetFreeListHead=heads[1];g_MenuWidgetCurrentNode=heads[2];
    g_MenuWidgetSavedNode=heads[3];g_MenuActiveWidget=heads[4];
    memcpy(g_MenuWidgetSimpleDescriptors,simple,sizeof(simple));memcpy(g_MenuWidgetGridDescriptors,grid,sizeof(grid));
    memcpy(g_MenuWidgetColumnLayoutTable,layouts,sizeof(layouts));memcpy(g_MenuEventQueuePool,queue,sizeof(queue));
    g_MenuEventQueueHead=queue_heads[0];g_MenuEventQueueTail=queue_heads[1];g_MenuEventQueueFreeList=queue_heads[2];
    g_MenuInputActive=input[0];g_MenuInputPollingPaused=input[1];g_MenuInputHeldStatusMask=input[2];g_InvItemUsableFlag=input[3];
    g_MenuSelectedItemData=saved_item;g_GameState.flags=saved_game;g_GameStateFlags=saved_global;
    g_GeomScroll.state.flags=saved_geom;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;g_CurrentEntity=saved_actor;
    if(valid)puts("Original received-item opcode 232: 5760 unhooked USA CPU states, real menu constructors and dispatcher yield/resume passed");
    return valid;
}

static int original_item_award_dispatcher(void)
{
    PlayerInventoryState saved_player=g_PlayerInventoryState;
    InventoryItemWorkspace saved_workspace=g_InventoryItemWorkspace;
    s16 *saved_items=g_InvItemPtr;int saved_limit=g_InvSlotLimit;
    FieldActor actor={0},*saved_actor=g_CurrentEntity;ScriptNode *saved_node=g_ScriptNodePool;
    u32 *saved_cursor=g_ScriptCursor;u32 saved_variable=g_ScriptOperandTable[7];
    const int limits[]={0,1,10};int valid=g_ScriptQueueOpcodeHandlers[167]==Script_GiveItem;
    g_CurrentEntity=&actor;
    for(unsigned cap=0;cap<3;++cap)for(unsigned occupied=0;occupied<2;++occupied)
    for(unsigned alias=0;alias<2;++alias){
        memset(&g_PlayerInventoryState,0,sizeof(g_PlayerInventoryState));
        memset(&g_InventoryItemWorkspace,0,sizeof(g_InventoryItemWorkspace));
        int limit=limits[cap];g_AyaSaveState.inventory_slot_count=limit;
        if(occupied)for(unsigned slot=0;slot<50;++slot)g_AyaInventoryItems[slot]=6;
        g_InvItemPtr=g_AyaInventoryItems;g_InvSlotLimit=limit;
        PlayerInventoryState expected=g_PlayerInventoryState;u32 failed=!limit || occupied;
        if(!failed)expected.runtime.inventory.slots[0]=6;
        g_ScriptOperandTable[7]=6;
        u32 program[]={167|(2u<<13)|(alias?((3u<<17)|(3u<<20)):0),0,
            alias?7u:6u,alias?7u:0x98765432u,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;
        Script_RunQueue();
        if(memcmp(&g_PlayerInventoryState,&expected,sizeof(expected)) ||
           g_InvItemPtr!=g_AyaInventoryItems || g_InvSlotLimit!=limit ||
           (alias?g_ScriptOperandTable[7]:program[3])!=failed || node.current.script!=program+6 ||
           node.flags!=0x90 || node.active || g_ScriptNodePool)valid=0;
    }
    g_PlayerInventoryState=saved_player;g_InventoryItemWorkspace=saved_workspace;
    g_InvItemPtr=saved_items;g_InvSlotLimit=saved_limit;g_CurrentEntity=saved_actor;
    g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;g_ScriptOperandTable[7]=saved_variable;
    if(valid)puts("Original item award dispatcher: empty/full/zero capacity and aliased script-variable outputs passed");
    return valid;
}

static int original_icon_packets(PeDisc *disc,PeDiscFile *executable)
{
    PeDiscFile archive; u8 range[4];
    if(!pe_disc_find(disc,"PE.IMG",&archive) ||
       !pe_disc_read(disc,executable,0x930D8-0x10000+0x800+4,range,sizeof(range))) return 0;
    unsigned first=range[0]|(unsigned)range[1]<<8,last=range[2]|(unsigned)range[3]<<8;
    if(last<=first || (last-first)*2048>sizeof(g_ResidentDataPack)) return 0;
    ResidentDataPack resident=g_ResidentDataPack;
    if(!pe_disc_read(disc,&archive,(uint64_t)first*2048,&g_ResidentDataPack,(last-first)*2048)) return 0;
    if(!original_received_item_opcode(disc,executable) || !original_item_award_dispatcher()){g_ResidentDataPack=resident;return 0;}
    unsigned count=(g_ResidentDataPack.directory.itemBaseData-g_ResidentDataPack.directory.glyphDescriptors)/8;
    const uint64_t hashes[]={UINT64_C(0xd4de41434c6d79f9),UINT64_C(0xbdb11c6f91760571)};
    const int positions[3][2]={{23,45},{-17,-9},{65533,65534}};
    static _Alignas(16) u8 arena[0x4000+16];
    u8 *cursor=g_DrawPacketCursor,*base=g_DrawPacketArenaBase;u32 *entry=g_DrawOrderingTableEntry;
    int x=g_TextCursorX,y=g_TextCursorY,dim=g_DrawTextDimmed;
    u32 color=g_DrawPrimColor,shaded=g_DrawColorShaded;
    if(count!=156) {g_ResidentDataPack=resident;return 0;}
    int valid=1;
    int text_mode=g_TextRenderMode,advance=g_DrawGlyphAdvance;
    for(unsigned dark=0;dark<2;++dark) {
        RenderNativeTag ordering={0xFFFFFF,0x9876};
        g_DrawPacketArenaBase=arena;g_DrawOrderingTableEntry=(u32 *)&ordering;
        Draw_SetColor(0xE1A25C);Draw_SetTextDimmed(dark);
        uint64_t hash=UINT64_C(14695981039346656037);
        for(unsigned pos=0;pos<3;++pos) for(unsigned mode=0;mode<3;++mode)
        for(unsigned glyph=0;glyph<count;++glyph) {
            memset(arena,0xA5,sizeof(RenderTexturedQuad)+16);g_DrawPacketCursor=arena;
            ordering.address=0xFFFFFF;Draw_SetCursor(positions[pos][0],positions[pos][1]);
            Draw_EmitGlyph(glyph,mode);
            RenderTexturedQuad *quad=(RenderTexturedQuad *)arena;
            if(quad->tag.length!=9 || quad->tag.address!=0xFFFFFF ||
               ordering.address!=(uintptr_t)quad || ordering.length!=0x9876 ||
               g_DrawPacketCursor!=arena+sizeof(*quad)) valid=0;
            const u8 *payload=arena+sizeof(RenderNativeTag);
            for(unsigned i=0;i<36;++i) hash=(hash^payload[i])*UINT64_C(1099511628211);
            for(size_t i=sizeof(RenderNativeTag)+36;i<sizeof(*quad)+16;++i) if(arena[i]!=0xA5) valid=0;
        }
        if(hash!=hashes[dark]) valid=0;
        g_DrawPacketCursor=arena+0x4000-sizeof(RenderTexturedQuad);
        uintptr_t head=ordering.address;Draw_EmitGlyph(123,2);
        if(ordering.address!=head || g_DrawPacketCursor!=arena+0x4000-sizeof(RenderTexturedQuad)) valid=0;
    }
    /* USA 8005EB64: all descriptor sprite payloads and draw-page commands. */
    const uint64_t sprite_hashes[]={UINT64_C(0x0215f3979ba18173),UINT64_C(0x54f0e01143881453)};
    for(unsigned dark=0;dark<2;++dark) {
        RenderNativeTag ordering={0xFFFFFF,0x9876};
        g_DrawPacketArenaBase=arena;g_DrawOrderingTableEntry=(u32 *)&ordering;
        Draw_SetColor(0xE1A25C);Draw_SetTextDimmed(dark);
        uint64_t hash=UINT64_C(14695981039346656037);
        for(unsigned pos=0;pos<3;++pos) for(unsigned glyph=0;glyph<count;++glyph) {
            memset(arena,0xA5,sizeof(RenderTexturedQuad)+16);g_DrawPacketCursor=arena;
            ordering.address=0xFFFFFF;Draw_SetCursor(positions[pos][0],positions[pos][1]);
            Draw_AllocSprite(glyph);
            RenderSpritePacket *sprite=(RenderSpritePacket *)arena;
            RenderDrawModePacket *page=(RenderDrawModePacket *)(arena+sizeof(*sprite));
            if(sprite->tag.length!=4 || sprite->tag.address!=0xFFFFFF || page->tag.length!=1 ||
               page->tag.address!=(uintptr_t)sprite || ordering.address!=(uintptr_t)page ||
               ordering.length!=0x9876 || page->command>UINT32_MAX ||
               g_DrawPacketCursor!=(u8 *)page+sizeof(*page)) valid=0;
            for(unsigned i=0;i<16;++i) hash=(hash^arena[sizeof(RenderNativeTag)+i])*UINT64_C(1099511628211);
            for(unsigned byte=0;byte<4;++byte) hash=(hash^((page->command>>(byte*8))&255))*UINT64_C(1099511628211);
            for(size_t i=sizeof(*sprite)+sizeof(*page);i<sizeof(RenderTexturedQuad)+16;++i)
                if(arena[i]!=0xA5) valid=0;
        }
        if(hash!=sprite_hashes[dark]) valid=0;
        size_t pair=sizeof(RenderSpritePacket)+sizeof(RenderDrawModePacket);
        for(unsigned missing=0;missing<=8;missing+=8) {
            ordering.address=0xFFFFFF;
            g_DrawPacketCursor=arena+0x4000-pair+missing;
            u8 *failed_cursor=g_DrawPacketCursor;
            memset(failed_cursor,0xA5,pair-missing+16);
            Draw_AllocSprite(0x77);
            if(g_DrawPacketCursor!=failed_cursor || ordering.address!=0xFFFFFF || ordering.length!=0x9876) valid=0;
            for(size_t i=0;i<pair-missing+16;++i) if(failed_cursor[i]!=0xA5) valid=0;
        }
    }
    /* USA 8005EED4: text quad bytes plus cursor/page/advance state,
     * including all six page-prefix values and control bytes FA..FF.
     * Page 1/code 0 now includes its sprite/page packet pair. */
    const uint64_t text_hashes[]={UINT64_C(0x3ee449b4194055b7),UINT64_C(0xa2b4884fca9feb47)};
    for(unsigned dark=0;dark<2;++dark) {
        RenderNativeTag ordering={0xFFFFFF,0x9876};
        g_DrawPacketArenaBase=arena;g_DrawOrderingTableEntry=(u32 *)&ordering;
        Draw_SetColor(0xE1A25C);Draw_SetTextDimmed(dark);
        uint64_t hash=UINT64_C(14695981039346656037);
        for(unsigned pos=0;pos<3;++pos) for(unsigned page=0;page<6;++page)
        for(unsigned code=0;code<256;++code) {
            memset(arena,0xA5,sizeof(RenderTexturedQuad)+16);g_DrawPacketCursor=arena;
            ordering.address=0xFFFFFF;Draw_SetCursor(positions[pos][0],positions[pos][1]);
            g_TextRenderMode=page;g_DrawGlyphAdvance=99;
            Draw_AllocTexturedQuad(code);
            RenderTexturedQuad *quad=(RenderTexturedQuad *)arena;
            int special=page==1 && code==0;
            if(special) {
                RenderSpritePacket *sprite=(RenderSpritePacket *)arena;
                RenderDrawModePacket *mode=(RenderDrawModePacket *)(arena+sizeof(*sprite));
                if(sprite->tag.length!=4 || sprite->tag.address!=0xFFFFFF || mode->tag.length!=1 ||
                   mode->tag.address!=(uintptr_t)sprite || ordering.address!=(uintptr_t)mode ||
                   g_DrawPacketCursor!=(u8 *)mode+sizeof(*mode)) valid=0;
            } else if(code<250) {
                if(quad->tag.length!=9 || quad->tag.address!=0xFFFFFF ||
                   ordering.address!=(uintptr_t)quad || g_DrawPacketCursor!=arena+sizeof(*quad)) valid=0;
            } else if(ordering.address!=0xFFFFFF || g_DrawPacketCursor!=arena) valid=0;
            if(ordering.length!=0x9876) valid=0;
            const u8 *payload=arena+sizeof(RenderNativeTag);
            for(unsigned i=0;i<(special?16u:36u);++i) hash=(hash^payload[i])*UINT64_C(1099511628211);
            if(special) {
                RenderDrawModePacket *mode=(RenderDrawModePacket *)(arena+sizeof(RenderSpritePacket));
                for(unsigned byte=0;byte<4;++byte) hash=(hash^((mode->command>>(byte*8))&255))*UINT64_C(1099511628211);
            }
            const u32 state[]={(u32)g_TextCursorX,(u32)g_TextCursorY,(u32)g_TextRenderMode,(u32)g_DrawGlyphAdvance};
            for(unsigned i=0;i<4;++i) for(unsigned byte=0;byte<4;++byte)
                hash=(hash^((state[i]>>(byte*8))&255))*UINT64_C(1099511628211);
            size_t used=special?sizeof(RenderSpritePacket)+sizeof(RenderDrawModePacket):sizeof(RenderNativeTag)+36;
            for(size_t i=used;i<sizeof(*quad)+16;++i) if(arena[i]!=0xA5) valid=0;
        }
        if(hash!=text_hashes[dark]) valid=0;
        g_TextRenderMode=0;g_DrawPacketCursor=arena+0x4000-sizeof(RenderTexturedQuad);
        uintptr_t head=ordering.address;Draw_AllocTexturedQuad(7);
        if(ordering.address!=head || g_DrawPacketCursor!=arena+0x4000-sizeof(RenderTexturedQuad)) valid=0;
    }
    /* Whole USA 8006062C execution, including nested cursor stack, number,
     * optional minus sprite, bar slices, colour reset and final draw page. */
    const uint64_t bar_hashes[]={UINT64_C(0xd0110c3a0c5a55a9),UINT64_C(0x6fe60f48763f6639)};
    const int values[]={INT_MIN,-99,-10,-1,0,1,9,10,99,100,999,INT_MAX};
    int *stack=g_TextCursorStackPtr,stack_data[28];
    memcpy(stack_data,g_TextCursorStackBottom,sizeof(stack_data));
    int font_clut=g_DrawDigitFontTpageClut,font_u=g_DrawDigitFontBaseTexU,font_v=g_DrawDigitFontBaseTexV;
    for(unsigned dark=0;dark<2;++dark) {
        RenderNativeTag ordering={0xFFFFFF,0x9876};
        g_DrawPacketArenaBase=arena;g_DrawOrderingTableEntry=(u32 *)&ordering;
        uint64_t hash=UINT64_C(14695981039346656037);
        for(unsigned font=0;font<2;++font) for(unsigned pos=0;pos<3;++pos)
        for(unsigned value=0;value<sizeof(values)/sizeof(values[0]);++value)
        for(int width=-1;width<50;++width) {
            memset(arena,0xA5,1024);g_DrawPacketCursor=arena;ordering.address=0xFFFFFF;
            Draw_SetColor(0xE1A25C);Draw_SetTextDimmed(dark);Draw_SetFontVariant(font);
            Draw_SetCursor(positions[pos][0],positions[pos][1]);
            g_TextCursorStackPtr=g_TextCursorStackBottom;
            Draw_AllocTexturedRectAlt(values[value],width);
            u8 *packet=arena;uintptr_t previous=0xFFFFFF;unsigned packets=0;
            while(packet<g_DrawPacketCursor && packets<16) {
                RenderNativeTag *tag=(RenderNativeTag *)packet;
                size_t bytes=tag->length==9?sizeof(RenderTexturedQuad):
                    tag->length==4?sizeof(RenderSpritePacket):
                    tag->length==1?sizeof(RenderDrawModePacket):0;
                if(!bytes || bytes>(size_t)(g_DrawPacketCursor-packet)) {valid=0;break;}
                if(tag->address!=previous) valid=0;
                hash=(hash^tag->length)*UINT64_C(1099511628211);
                if(tag->length==1) {
                    uintptr_t command=((RenderDrawModePacket *)packet)->command;
                    if(command>UINT32_MAX) valid=0;
                    for(unsigned b=0;b<4;++b) hash=(hash^((command>>(b*8))&255))*UINT64_C(1099511628211);
                } else for(unsigned b=0;b<tag->length*4;++b)
                    hash=(hash^packet[sizeof(RenderNativeTag)+b])*UINT64_C(1099511628211);
                previous=(uintptr_t)packet;packet+=bytes;++packets;
            }
            if(packet!=g_DrawPacketCursor || packets<8 || packets>12 ||
               ordering.address!=previous || ordering.length!=0x9876 ||
               g_TextCursorStackPtr!=g_TextCursorStackBottom) valid=0;
            for(size_t i=(size_t)(packet-arena);i<1024;++i) if(arena[i]!=0xA5) valid=0;
            const u32 state[]={(u32)g_TextCursorX,(u32)g_TextCursorY,
                (u32)((u8 *)g_TextCursorStackPtr-(u8 *)g_TextCursorStackBottom),g_DrawPrimColor,g_DrawColorShaded};
            for(unsigned i=0;i<5;++i) for(unsigned b=0;b<4;++b)
                hash=(hash^((state[i]>>(b*8))&255))*UINT64_C(1099511628211);
        }
        if(hash!=bar_hashes[dark]) {
            fprintf(stderr,"Original bar dim %u hash %016llx differs from USA MIPS\n",dark,(unsigned long long)hash);
            valid=0;
        }
    }
    /* Every aligned amount of free space across the complete bar size:
     * dropped packets must leave a valid chain and restore the cursor stack. */
    for(unsigned remaining=0;remaining<=768;remaining+=8) {
        RenderNativeTag ordering={0xFFFFFF,0x9876};
        memset(arena,0xA5,sizeof(arena));
        g_DrawPacketArenaBase=arena;g_DrawPacketCursor=arena+0x4000-remaining;
        u8 *start=g_DrawPacketCursor;
        g_DrawOrderingTableEntry=(u32 *)&ordering;g_TextCursorStackPtr=g_TextCursorStackBottom;
        Draw_SetCursor(23,45);Draw_SetFontVariant(0);Draw_SetTextDimmed(0);Draw_SetColor(0xE1A25C);
        Draw_AllocTexturedRectAlt(-99,24);
        if(g_TextCursorX!=23 || g_TextCursorY!=45 || g_TextCursorStackPtr!=g_TextCursorStackBottom ||
           g_DrawPacketCursor<start || g_DrawPacketCursor>arena+0x4000 || ordering.length!=0x9876) valid=0;
        uintptr_t previous=0xFFFFFF;u8 *packet=start;
        for(unsigned count=0;packet<g_DrawPacketCursor && count<16;++count) {
            RenderNativeTag *tag=(RenderNativeTag *)packet;
            size_t bytes=tag->length==9?sizeof(RenderTexturedQuad):
                tag->length==4?sizeof(RenderSpritePacket):tag->length==1?sizeof(RenderDrawModePacket):0;
            if(!bytes || bytes>(size_t)(g_DrawPacketCursor-packet)) {valid=0;break;}
            if(tag->address!=previous) valid=0;
            previous=(uintptr_t)packet;packet+=bytes;
        }
        if(packet!=g_DrawPacketCursor || ordering.address!=previous) valid=0;
        for(u8 *p=arena;p<start;++p) if(*p!=0xA5) valid=0;
        for(u8 *p=g_DrawPacketCursor;p<arena+sizeof(arena);++p) if(*p!=0xA5) valid=0;
    }
    g_DrawDigitFontTpageClut=font_clut;g_DrawDigitFontBaseTexU=font_u;g_DrawDigitFontBaseTexV=font_v;
    memcpy(g_TextCursorStackBottom,stack_data,sizeof(stack_data));g_TextCursorStackPtr=stack;
    if(!original_escape_chance(disc,executable) || !original_action_slot() || !original_battle_target_list() || !original_script_sprites() || !original_wireframe_renderer() || !original_wireframe_animation() || !original_textbox_renderer(disc,executable) || !original_scan_overlay(disc,executable) || !original_save_card_images(disc) || !original_hud_packet_initialization() || !original_hud_packet_updates() || !original_spell_effects(disc,executable))valid=0;
    g_TextRenderMode=text_mode;g_DrawGlyphAdvance=advance;
    g_ResidentDataPack=resident;g_DrawPacketCursor=cursor;g_DrawPacketArenaBase=base;g_DrawOrderingTableEntry=entry;
    g_TextCursorX=x;g_TextCursorY=y;g_DrawTextDimmed=dim;g_DrawPrimColor=color;g_DrawColorShaded=shaded;
    if(valid) puts("Original sprites/icon/text: 156 real descriptors, complete text and 7344 whole-bar cases match USA MIPS; 97 arena limits preserve chains/cursors");
    else fprintf(stderr,"Original icon payload/native links differ from USA MIPS 8005ED18\n");
    return valid;
}

/* Compare actual headless GPU output at every original logo VSync. */
static u16 title_expected_pixels[320*480],title_actual_pixels[320*480];
static const u8 *title_logo_pixels,*title_logo_palette;
static unsigned title_logo_frame,title_logo_failures;
static void title_logo_observe(void)
{
    unsigned frame=title_logo_frame++;
    if(frame>=480) {++title_logo_failures;return;}
    unsigned shade=frame<32?frame*4:frame<392?128:frame<424?(424-frame)*4:0;
    unsigned page=frame&1;
    for(unsigned y=0;y<64;++y) for(unsigned x=0;x<256;++x) {
        unsigned packed=title_logo_pixels[y*128+x/2];
        unsigned index=(packed>>((x&1)*4))&15;
        u16 texel=(u16)title_logo_palette[index*2]|(u16)title_logo_palette[index*2+1]<<8;
        if(!texel) continue;
        u16 color=texel&0x8000;
        for(unsigned c=0;c<3;++c) color|=(((texel>>(c*5))&31)*shade/128)<<(c*5);
        title_expected_pixels[(page*240+y+88)*320+x+32]=color;
    }
    MemcardScreenBuffer *screen=g_MemcardScreenBuffers[!page];
    for(unsigned y=0;y<2;++y) for(unsigned x=0;x<6;++x) {
        const u8 *p=screen->overlayImage+(y*6+x)*2;
        title_expected_pixels[(page*240+y+7)*320+x+6]=(u16)p[0]|(u16)p[1]<<8;
    }
    RECT area={0,0,320,480};
    StoreImage(&area,title_actual_pixels);DrawSync(0);
    if(memcmp(title_actual_pixels,title_expected_pixels,sizeof(title_actual_pixels))) {
        if(!title_logo_failures) for(unsigned i=0;i<320*480;++i)
            if(title_actual_pixels[i]!=title_expected_pixels[i]) {
                fprintf(stderr,"Logo frame %u pixel %u: %04x != %04x\n",frame,i,
                        title_actual_pixels[i],title_expected_pixels[i]);break;
            }
        ++title_logo_failures;
    }
}
static unsigned title_node_index(MemcardImageNode *node)
{
    if(!node) return 0;
    if(node<g_MemcardImageNodePool || node>=g_MemcardImageNodePool+8) return UINT_MAX;
    return (unsigned)(node-g_MemcardImageNodePool)+1;
}
static unsigned title_callback_index(uintptr_t callback)
{
    const uintptr_t functions[]={0,(uintptr_t)MenuCard_UpdateFadeCounter,(uintptr_t)MenuCard_AdvanceFade,
        (uintptr_t)MenuCard_UpdateFadeDisplay,(uintptr_t)MenuCard_DecreaseFade,(uintptr_t)MenuCard_StepFadeToImages,
        (uintptr_t)MenuCard_CreateFadeImageNode,(uintptr_t)MenuCard_AttachFadePayload,(uintptr_t)MenuCard_InitFadeForNode,
        (uintptr_t)MenuCard_BlendImage,(uintptr_t)MenuCard_BlendRotatedImage};
    for(unsigned i=0;i<sizeof(functions)/sizeof(functions[0]);++i) if(functions[i]==callback) return i;
    return UINT_MAX;
}
static int original_title_screen(PeDisc *disc)
{
    PeDiscFile archive;
    if(!pe_disc_find(disc,"PE.IMG",&archive)) return 0;
    u32 flags=g_GameState.flags,base=g_PeImageBaseLba;
    unsigned modes=g_PeImageMountFlags;
    Pe1SceneAudioState audio=g_GameState.scene_audio;
    int queued=g_AkaoMessageQueueCount;
    CdRom_BindImage(disc,&archive);
    if(Asset_OpenPeImage()!=0 || CdRom_LoadInitialImage()!=0) return 0;
    const u8 *logo=g_MemcardImageBundle+g_MemcardImageOffsets[8];
    unsigned palette_bytes=(unsigned)logo[8]|(unsigned)logo[9]<<8;
    if(logo[0]!=16 || logo[4]!=8 || palette_bytes!=44) return 0;
    title_logo_palette=logo+20;title_logo_pixels=logo+8+palette_bytes+12;
    DRAWENV old_draw;GetDrawEnv(&old_draw);
    RECT clear={0,0,320,480};ClearImage(&clear,0,0,0);DrawSync(0);
    memset(title_expected_pixels,0,sizeof(title_expected_pixels));
    for(unsigned p=0;p<2;++p) {
        MemcardScreenBuffer *screen=g_MemcardScreenBuffers[p];
        SetDefDrawEnv(&screen->draw,0,p*240,320,240);screen->draw.isbg=0;
        SetDefDispEnv(&screen->disp,0,!p*240,320,240);screen->disp.isrgb24=1;
        screen->overlay=(RECT){4,7,4,2};screen->dirty=(RECT){0};
        for(unsigned i=0;i<24;++i) screen->overlayImage[i]=(u8)(p*71+i+1);
    }
    g_MemcardScreenPage=0;PutDrawEnv(&g_MemcardScreenBuffers[0]->draw);
    title_logo_frame=title_logo_failures=0;
    VSyncCallback(title_logo_observe);MenuCard_ShowLogoFade();VSyncCallback(Boot_VsyncCallback);
    int valid=title_logo_frame==480 && !title_logo_failures && g_MemcardScreenPage==0 &&
        g_MemcardCurrentScreen==g_MemcardScreenBuffers[0] &&
        g_MemcardScreenBuffers[0]->disp.isrgb24==1 && g_MemcardScreenBuffers[1]->disp.isrgb24==1;
    /* Original two-page background restore plus per-page dirty overlays. */
    for(unsigned p=0;p<2;++p) g_MemcardScreenBuffers[p]->overlay.w=0;
    MenuCard_FlipScreenPages();
    const u8 *background=g_MemcardImageBundle+g_MemcardImageOffsets[0]+sizeof(MemcardImage);
    static u16 restored[480*204];RECT background_area={0,20,480,204};
    for(unsigned p=0;p<2;++p) {
        background_area.y=p*240+20;StoreImage(&background_area,restored);DrawSync(0);
        if(memcmp(restored,background,sizeof(restored))) valid=0;
    }
    if(g_MemcardScreenPage!=0 || g_MemcardCurrentScreen!=g_MemcardScreenBuffers[0]) valid=0;
    /* Full original node/input/fade/render sequence versus USA MIPS, with
     * pointer/function identities normalized to pool slots and callback IDs. */
    const uint64_t node_hashes[]={UINT64_C(0x82bcdaa44ad52f19),UINT64_C(0x44a261b8d9567021),
        UINT64_C(0x6f787be356e1bf1a),UINT64_C(0x6f787be356e1bf1a),
        UINT64_C(0x8473fe8902c4b46e),UINT64_C(0x12d571482a868159)};
    MemCardPortState saved_ports[2];memcpy(saved_ports,g_MemCardPortStates,sizeof(saved_ports));
    void *saved_bgm[2]={g_AkaoBgmHandle[0],g_AkaoBgmHandle[1]};g_AkaoBgmHandle[0]=g_AkaoBgmHandle[1]=NULL;
    u32 saved_pad=g_FieldPadBits;
    for(unsigned scenario=0;scenario<6;++scenario) {
        memset(g_MemCardPortStates,0,sizeof(saved_ports));
        if(scenario && scenario!=5) {
            MemCardPortState *port=&g_MemCardPortStates[scenario==3];
            port->present=1;port->managerState=15;
            if(scenario>=2) {port->slots[14].state=MEMCARD_SLOT_OCCUPIED;port->slots[14].titleStyleFlag=1;}
        }
        if(MemCard_HasAnyPresentFile()!=(scenario>=2 && scenario!=5)) valid=0;
        memset(g_MemcardImageNodePool,0,sizeof(g_MemcardImageNodePool));
        for(unsigned i=0;i<7;++i)g_MemcardImageNodePool[i].next=&g_MemcardImageNodePool[i+1];
        g_MemcardFreeImageNodes=g_MemcardImageNodePool;
        g_MemcardActiveNodeHead=g_MemcardActiveNodeTail=g_MemcardPendingNodeHead=g_MemcardPendingNodeTail=NULL;
        for(unsigned p=0;p<2;++p) {
            g_MemcardScreenBuffers[p]->overlay=(RECT){0};g_MemcardScreenBuffers[p]->dirty=(RECT){0};
            memset(g_MemcardScreenBuffers[p]->overlayImage,0,sizeof(g_MemcardScreenBuffers[p]->overlayImage));
        }
        MenuCard_CreateImageNode(1)->field14=MenuCard_CreateFadeImageNode;
        g_MemcardPrevPad=0;g_MemcardScreenTimer=1;
        uint64_t hash=UINT64_C(14695981039346656037);
        unsigned frames=scenario>=4?240:142;
        for(unsigned frame=0;frame<frames;++frame) {
            if(scenario==4 && frame==190) memset(g_MemCardPortStates,0,sizeof(saved_ports));
            if(scenario==5 && frame==100) {
                MemCardPortState *port=&g_MemCardPortStates[1];
                port->present=1;port->managerState=15;
                port->slots[14].state=MEMCARD_SLOT_OCCUPIED;port->slots[14].titleStyleFlag=1;
            }
            g_FieldPadBits=frame==40?4:frame==110 || frame==130?8:frame==120?0x20:frame==frames-2?0x20000000:0;
            g_MemcardScreenPage=!(frame&1);g_MemcardCurrentScreen=g_MemcardScreenBuffers[g_MemcardScreenPage];
            MenuCard_HandleListInput();MenuCard_RedrawImageNodes();
            MemcardImageNode *node=g_MemcardActiveNodeHead,*previous=NULL;
            while(node) {
                MemcardImageNode *next=node->next;
                if(node->freePending) {
                    if(previous)previous->next=next;else g_MemcardActiveNodeHead=next;
                    if(g_MemcardActiveNodeTail==node)g_MemcardActiveNodeTail=previous;
                    node->next=g_MemcardFreeImageNodes;g_MemcardFreeImageNodes=node;
                } else previous=node;
                node=next;
            }
            if(g_MemcardPendingNodeHead) {
                if(g_MemcardActiveNodeTail)g_MemcardActiveNodeTail->next=g_MemcardPendingNodeHead;
                else g_MemcardActiveNodeHead=g_MemcardPendingNodeHead;
                g_MemcardActiveNodeTail=g_MemcardPendingNodeTail;
                g_MemcardPendingNodeHead=g_MemcardPendingNodeTail=NULL;
            }
            ++g_MemcardScreenTimer;
            for(unsigned i=0;i<8;++i) {
                MemcardImageNode *n=&g_MemcardImageNodePool[i];
                const u32 fields[]={title_node_index(n->next),(u16)n->x|((u32)(u16)n->y<<16),
                    (u16)n->width|((u32)(u16)n->height<<16),title_callback_index((uintptr_t)n->draw),
                    title_callback_index((uintptr_t)n->update),title_callback_index((uintptr_t)n->field14),
                    n->image?(u32)((u8 *)n->image-g_MemcardImageBundle):0,
                    (u32)n->intensity,(u32)n->targetY,(u32)n->stepY,(u32)n->parameter,(u32)n->kind,(u32)n->freePending};
                for(unsigned f=0;f<13;++f) for(unsigned b=0;b<4;++b)
                    hash=(hash^((fields[f]>>(b*8))&255))*UINT64_C(1099511628211);
            }
            const u32 state[]={title_node_index(g_MemcardFreeImageNodes),title_node_index(g_MemcardActiveNodeHead),
                title_node_index(g_MemcardActiveNodeTail),title_node_index(g_MemcardPendingNodeHead),
                title_node_index(g_MemcardPendingNodeTail),(u32)g_MemcardScreenTimer,(u32)g_MemcardPrevPad};
            for(unsigned f=0;f<7;++f) for(unsigned b=0;b<4;++b)
                hash=(hash^((state[f]>>(b*8))&255))*UINT64_C(1099511628211);
            for(unsigned p=0;p<2;++p) {
                const RECT rects[]={g_MemcardScreenBuffers[p]->overlay,g_MemcardScreenBuffers[p]->dirty};
                for(unsigned r=0;r<2;++r) {
                    const u16 values[]={(u16)rects[r].x,(u16)rects[r].y,(u16)rects[r].w,(u16)rects[r].h};
                    for(unsigned f=0;f<4;++f) for(unsigned b=0;b<2;++b)
                        hash=(hash^((values[f]>>(b*8))&255))*UINT64_C(1099511628211);
                }
            }
            RECT *area=&g_MemcardCurrentScreen->overlay;
            if(area->w>0 && area->h>0) {
                size_t bytes=(size_t)area->w*area->h*3;
                if(bytes>sizeof(g_MemcardCurrentScreen->overlayImage)) {valid=0;break;}
                for(size_t b=0;b<bytes;++b)hash=(hash^g_MemcardCurrentScreen->overlayImage[b])*UINT64_C(1099511628211);
            }
        }
        if(hash!=node_hashes[scenario] || g_MemcardScreenTimer!=1003 ||
           !g_MemcardActiveNodeHead || g_MemcardActiveNodeHead->kind!=7 || g_MemcardActiveNodeHead->y!=160) {
            fprintf(stderr,"Original title nodes scenario %u hash %016llx differs from USA MIPS\n",scenario,(unsigned long long)hash);
            valid=0;
        }
    }
    for(unsigned available=0;available<5;++available) {
        memset(g_MemcardImageNodePool,0,sizeof(g_MemcardImageNodePool));
        for(unsigned i=1;i<available;++i)g_MemcardImageNodePool[i-1].next=&g_MemcardImageNodePool[i];
        g_MemcardFreeImageNodes=available?g_MemcardImageNodePool:NULL;
        g_MemcardActiveNodeHead=g_MemcardActiveNodeTail=g_MemcardPendingNodeHead=g_MemcardPendingNodeTail=NULL;
        MemcardImageNode saved_nodes[8];memcpy(saved_nodes,g_MemcardImageNodePool,sizeof(saved_nodes));
        MemcardImageNode *free=g_MemcardFreeImageNodes;
        MenuCard_CreateImageGroup();
        if(MenuCard_CreateImageNode(-1) || MenuCard_CreateImageNode(8)) valid=0;
        if(!available) {MenuCard_CreateFixedImageNode();if(MenuCard_CreateImageNode(1))valid=0;}
        if(g_MemcardFreeImageNodes!=free || g_MemcardPendingNodeHead || g_MemcardPendingNodeTail ||
           memcmp(saved_nodes,g_MemcardImageNodePool,sizeof(saved_nodes))) valid=0;
    }
    memcpy(g_MemCardPortStates,saved_ports,sizeof(saved_ports));g_FieldPadBits=saved_pad;
    g_AkaoBgmHandle[0]=saved_bgm[0];g_AkaoBgmHandle[1]=saved_bgm[1];
    PutDrawEnv(&old_draw);g_AkaoMessageQueueCount=queued;
    CdRom_BindImage(NULL,NULL);g_GameState.flags=flags;g_PeImageBaseLba=base;
    g_PeImageMountFlags=modes;g_GameState.scene_audio=audio;
    if(valid) puts("Original title screen: all 480 logo frames match full framebuffer pixels, both RGB24 backgrounds and 1048 input/node/fade/render frames match USA MIPS headlessly");
    else fprintf(stderr,"Original title screen failed: %u frames, %u pixel mismatches\n",title_logo_frame,title_logo_failures);
    return valid;
}

static int original_m123_counters(void)
{
    int (*callbacks[])(int,void *,intptr_t)={RoomM123_JointTriangleCallback,
        RoomM123_GlowBurstParticleCallback,RoomM123_JointSwirlParticleCallback,
        RoomM123_PulseRingCallback,RoomM123_WaveCallback};
    const unsigned frames[]={0,1,7,15,16,23,24,31,47,48,32767,65535};
    const int speeds[]={-32768,-1,0,4096,32767};
    const unsigned positions[]={0,4095,8191,12287,32767,65535};
    const unsigned modes[]={0,1,3};u64 hash=UINT64_C(14695981039346656037);unsigned cases=0;
    s16 old_speed=g_RoomM123SwirlSpeed;
    for(unsigned kind=0;kind<5;++kind)for(unsigned m=0;m<3;++m)
    for(unsigned state=0;state<4;++state)for(unsigned f=0;f<12;++f)
    for(unsigned speed=0;speed<5;++speed)for(unsigned pos=0;pos<6;++pos){
        _Alignas(void *) u8 bytes[64];memset(bytes,0xa5,sizeof(bytes));u16 *s=(u16 *)(bytes+8);
        if(!kind){s[0]=state;s[1]=frames[f];s[2]=positions[pos];}
        else if(kind<3){s[0]=frames[f];s[1]=positions[pos];s[2]=0x1234;}
        else{s[0]=0x1234;s[1]=0x5678;s[2]=positions[pos];s[3]=0x9abc;s[4]=state;s[5]=frames[f];}
        g_RoomM123SwirlSpeed=speeds[speed];u32 result=callbacks[kind](modes[m],bytes+8,0);
        const u32 words[]={kind,modes[m],state,frames[f],(u32)speeds[speed],positions[pos],result};
        for(unsigned i=0;i<7;++i)for(unsigned b=0;b<4;++b)hash=(hash^((words[i]>>(8*b))&255))*UINT64_C(1099511628211);
        for(unsigned i=0;i<sizeof(bytes);++i)hash=(hash^bytes[i])*UINT64_C(1099511628211);
        ++cases;
    }
    g_RoomM123SwirlSpeed=old_speed;
    if(cases!=21600 || hash!=UINT64_C(0x753a90c8398f8069)){
        fprintf(stderr,"M123 callback counters: cases %u hash %016llx\n",cases,(unsigned long long)hash);return 0;
    }
    puts("Original M123 counters: 21600 unhooked USA CPU states, five typed callbacks, signed halfword boundaries and whole-buffer guards passed");return 1;
}

static int original_subsystem_start(void)
{
    g_GameStateFlags=0xA5A5A5A5; g_FrameRngCounter=123;
    for(unsigned i=0;i<7;++i) g_BattleEnemySlots[i].active=123;
    g_BattleActionQueueCount=17; g_BattleActionIndex=11; g_BattleRemainingActions=3;
    memset(g_ScriptVariableBank,0xA5,0x200*sizeof(u32));
    memset(g_ScriptNodeActiveFlags,0xA5,sizeof(g_ScriptNodeActiveFlags));
    Script_QueueFieldSfx(1,2,3,4,5);
    Boot_InitSubsystems();
    VSyncCallback(NULL);
    if(g_GameStateFlags!=0x4000 || g_FrameRngCounter || g_ActiveDrawSlot ||
       g_BattleActionQueueCount || g_BattleActionIndex || g_BattleRemainingActions ||
       g_FieldSfxQueueCount || g_EntityFreePoolCount || g_FieldActorListHead ||
       g_PlayerEntity || g_EntitySerialCounter) {fprintf(stderr,"subsystem check failed at %d\n",__LINE__);return 0;}
    for(unsigned i=0;i<7;++i) if(g_BattleEnemySlots[i].active) {fprintf(stderr,"subsystem check failed at %d\n",__LINE__);return 0;}
    for(unsigned i=0;i<0x200;++i) if(g_ScriptVariableBank[i]) {fprintf(stderr,"subsystem check failed at %d\n",__LINE__);return 0;}
    /* USA 80034F40..80034F4C repeatedly clears only the first flag;
     * the free-list initializer later clears all 64. Preserve that order. */
    for(unsigned i=0;i<64;++i) if((u32)g_ScriptNodeActiveFlags[i]!=(i?0xA5A5A5A5u:0u))
        {fprintf(stderr,"subsystem flag reset differs at %u\n",i);return 0;}
    if(g_ScriptNodeFreeListHead || g_ScriptNodes[71].next) {fprintf(stderr,"subsystem check failed at %d\n",__LINE__);return 0;}
    uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<33;++i) for(unsigned byte=0;byte<4;++byte)
        hash=(hash^((g_DrawPrimGeneratorWords[i]>>(byte*8))&255))*UINT64_C(1099511628211);
    if(hash!=UINT64_C(0x2a702d05777cd111)) {fprintf(stderr,"subsystem check failed at %d\n",__LINE__);return 0;}
    puts("Original subsystem start: RNG warm-up, battle reset, script/entity pools and SFX reset passed");
    return 1;
}

/* Actual MIPS init/step execution, including the negative tail and the
 * boot warm-up of 2000 draws. Hashes serialize each word little-endian. */
static int original_frame_random(void)
{
    static const struct { unsigned draws; uint64_t state, stream; s32 head, tail; } cases[]={
        {0,UINT64_C(0x2cb83249daca1179),UINT64_C(0xcbf29ce484222325),64,16},
        {1,UINT64_C(0x982b0feedeac8cfd),UINT64_C(0x8346f5632112ce24),60,12},
        {5,UINT64_C(0x36a1e402d88fc5),UINT64_C(0x6cf2a888882e39ef),44,-4},
        {17,UINT64_C(0xb0944a452a2679dd),UINT64_C(0xeefebd61ca8aed13),64,-52},
        {2000,UINT64_C(0x2a702d05777cd111),UINT64_C(0x9b10472970d605e0),20,-48},
        {8192,UINT64_C(0xe822a05d2e530a78),UINT64_C(0x3d7e90f6c6b75720),4,-48},
    };
    u32 saved[33]; memcpy(saved,g_DrawPrimGeneratorWords,sizeof(saved));
    int valid=1;
    Draw_InitGpuHwRegs();
    uint64_t stream=UINT64_C(14695981039346656037);
    unsigned next=0;
    for(unsigned draws=0;draws<=8192;++draws) {
        if(draws==cases[next].draws) {
            uint64_t state=UINT64_C(14695981039346656037);
            for(unsigned i=0;i<33;++i) for(unsigned byte=0;byte<4;++byte)
                state=(state^((g_DrawPrimGeneratorWords[i]>>(byte*8))&255))*UINT64_C(1099511628211);
            if(state!=cases[next].state || stream!=cases[next].stream ||
               (s32)g_DrawPrimGeneratorWords[14]!=cases[next].head ||
               (s32)g_DrawPrimGeneratorWords[15]!=cases[next].tail) valid=0;
            if(++next==sizeof(cases)/sizeof(cases[0])) break;
        }
        u32 value=Draw_FlushPrimQueue();
        for(unsigned byte=0;byte<4;++byte)
            stream=(stream^((value>>(byte*8))&255))*UINT64_C(1099511628211);
    }
    Draw_InitGpuHwRegs();
    for(unsigned i=0;i<16;++i) {
        static const u32 prefix[]={0x17a,0x264,0x3de,0x642,0xa20,0x9,0x3d,0x441042,
            0x3e0003f,0x4b107e,0xb5c90,0x214eb,0x5989,0x1274,0x3fc00,0x450655};
        if(Draw_FlushPrimQueue()!=prefix[i]) valid=0;
    }
    const int bounds[]={-32768,-1000,-1,0,1,100,1000,32767};
    uint64_t opcode_hash=UINT64_C(14695981039346656037);
    FieldActor actor={0},*saved_actor=g_CurrentEntity;ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    g_CurrentEntity=&actor;if(g_ScriptQueueOpcodeHandlers[26]!=Script_Random)valid=0;
    for(unsigned lo=0;lo<8;++lo)for(unsigned hi=0;hi<8;++hi){
        Draw_InitGpuHwRegs();
        for(unsigned draw=0;draw<64;++draw){
            u32 before[33];memcpy(before,g_DrawPrimGeneratorWords,sizeof(before));
            int low=bounds[lo],high=bounds[hi],out=(s32)0x98765432;int *args[]={&out,&low,&high};
            u32 result=Script_Random(args),after[33];memcpy(after,g_DrawPrimGeneratorWords,sizeof(after));
            u32 words[35]={result,(u32)out};memcpy(words+2,after,sizeof(after));
            for(unsigned i=0;i<35;++i)for(unsigned b=0;b<4;++b)
                opcode_hash=(opcode_hash^((words[i]>>(b*8))&255))*UINT64_C(1099511628211);
            memcpy(g_DrawPrimGeneratorWords,before,sizeof(before));
            u32 program[]={26|(3u<<13),0,0x98765432,(u32)low,(u32)high,32,0};
            ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;Script_RunQueue();
            if(result!=1 || low!=bounds[lo] || high!=bounds[hi] || program[2]!=(u32)out ||
               memcmp(after,g_DrawPrimGeneratorWords,sizeof(after)) || node.current.script!=program+7 ||
               node.flags!=0x90 || node.active || g_ScriptNodePool)valid=0;
        }
    }
    g_CurrentEntity=saved_actor;g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;
    if(opcode_hash!=UINT64_C(0xf2f87de64cb28a19)){fprintf(stderr,"Range RNG hash %016llx\n",(unsigned long long)opcode_hash);valid=0;}
    if(valid)puts("Original random opcode 26: 4096 unhooked USA CPU draws, equal/reversed/signed ranges, complete generator state and real dispatcher passed");
    memcpy(g_DrawPrimGeneratorWords,saved,sizeof(saved));
    if(!valid) fprintf(stderr,"Original frame RNG differs from USA MIPS execution\n");
    return valid;
}

/* Goldens from executing USA EXE 800528F0/80052B2C in MIPS Unicorn.
 * 4096 draws cross seven complete table refresh boundaries. */
static int original_item_random(void)
{
    static const struct { u32 frames; uint64_t table_hash, stream_hash; } cases[] = {
        {UINT32_C(0), UINT64_C(0x72d912fe482091f1), UINT64_C(0x6a8e50bbaca9ed24)},
        {UINT32_C(60), UINT64_C(0x3d536800677af255), UINT64_C(0xa1d70666c8a26a26)},
        {UINT32_C(3600), UINT64_C(0x681d934fc438838), UINT64_C(0xce743e2b365ad07f)},
        {UINT32_C(4294967295), UINT64_C(0x30ad6f89bad09868), UINT64_C(0x32d6b1fd5d134ec)},
    };
    GameTimerEntry saved_timers[4];
    u8 saved_table[521];
    s32 saved_index=g_ItemDropRngIndex;
    int valid=1;
    memcpy(saved_timers,g_GameTimers,sizeof(saved_timers));
    memcpy(saved_table,g_ItemDropRngTable,sizeof(saved_table));
    const u32 timer_patterns[]={0,1,0x7fff,0x8000,0xffff,0x80000000,0xffffffff,
        0x010c0000,0xa5a55a5a,0x12345678,0x7fffffff,0x600000};
    uint64_t timer_hash=UINT64_C(14695981039346656037);
    if(g_ScriptQueueOpcodeHandlers[210]!=Script_GetFloorTableValue)valid=0;
    for(unsigned timer=0;timer<4;++timer)for(unsigned pattern=0;pattern<12;++pattern)
    for(unsigned target=0;target<14;++target){
        u32 raw[12];for(unsigned word=0;word<12;++word)raw[word]=timer_patterns[pattern]+word*0x01020304u;
        memcpy(g_GameTimers,raw,sizeof(raw));
        int index=timer,output=(s32)0x98765432;u32 guard=0xa55a5aa5;
        int *destinations[14]={&output,&index};
        for(unsigned i=0;i<4;++i){
            destinations[2+i*3]=(int *)&g_GameTimers[i].flags;
            destinations[3+i*3]=&g_GameTimers[i].current;
            destinations[4+i*3]=&g_GameTimers[i].limit;
        }
        int *args[]={&index,destinations[target]};u32 result=Script_GetFloorTableValue(args);
        u32 words[16]={result,(u32)output,(u32)index,guard};memcpy(words+4,g_GameTimers,sizeof(raw));
        for(unsigned word=0;word<16;++word)for(unsigned byte=0;byte<4;++byte)
            timer_hash=(timer_hash^((words[word]>>(byte*8))&255))*UINT64_C(1099511628211);
    }
    if(timer_hash!=UINT64_C(0x74d6afc47a7ad4ed))valid=0;
    FieldActor timer_actor={0},*saved_actor=g_CurrentEntity;g_CurrentEntity=&timer_actor;
    ScriptNode *saved_node=g_ScriptNodePool;u32 *saved_cursor=g_ScriptCursor;
    for(unsigned timer=0;timer<4;++timer)for(unsigned pattern=0;pattern<12;++pattern){
        u32 raw[12];for(unsigned word=0;word<12;++word)raw[word]=timer_patterns[pattern]+word*0x01020304u;
        memcpy(g_GameTimers,raw,sizeof(raw));
        u32 program[]={210|(2u<<13),0,timer,0x98765432,32,0};
        ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;
        Script_RunQueue();
        if(program[3]!=raw[timer*3+1] || memcmp(g_GameTimers,raw,sizeof(raw)) ||
           node.current.script!=program+6 || node.flags!=0x90 || node.active || g_ScriptNodePool)valid=0;
    }
    {
        /* USA 80019798 calls the original floor-selection getter. Its byte
         * domain includes inactive selection and every encoded floor index. */
        FontGlyphSelectionState saved_selection=g_FontSelectionState;
        uint64_t floor_hash=UINT64_C(14695981039346656037);
        if(g_ScriptQueueOpcodeHandlers[178]!=Script_GetFloorNumber)valid=0;
        for(unsigned active=0;active<256;++active)for(unsigned index=0;index<256;++index) {
            memset(&g_FontSelectionState,0x33,sizeof(g_FontSelectionState));
            g_FontSelectionState.active=active;g_FontSelectionState.codeIndex=index;
            g_FontSelectionState.code=0x5a;g_FontSelectionState.selected=0xa5;
            u8 before[sizeof(g_FontSelectionState)];memcpy(before,&g_FontSelectionState,sizeof(before));
            struct {u32 prefix;int output;u32 suffix;} cell={0x11112222,(s32)0x98765432,0xaaaa5555};
            int *args[]={&cell.output};u32 result=Script_GetFloorNumber(args);
            u32 words[7]={result,(u32)cell.output,cell.prefix,cell.suffix};memcpy(words+4,before,12);
            for(unsigned word=0;word<7;++word)for(unsigned byte=0;byte<4;++byte)
                floor_hash=(floor_hash^((words[word]>>(byte*8))&255))*UINT64_C(1099511628211);
            if(memcmp(before,&g_FontSelectionState,sizeof(before)))valid=0;
            u32 program[]={178|(1u<<13),0,0x98765432,32,0};
            ScriptNode node={.current.script=program,.active=1,.flags=0x80};g_ScriptNodePool=&node;
            Script_RunQueue();
            if(program[2]!=(u32)cell.output || node.current.script!=program+5 ||
               node.flags!=0x90 || node.active || g_ScriptNodePool ||
               memcmp(before,&g_FontSelectionState,sizeof(before)))valid=0;
        }
        g_FontSelectionState=saved_selection;
        if(floor_hash!=UINT64_C(0x393cb60902443125))valid=0;
        if(valid)puts("Original floor opcode178: 65,536 unhooked USA CPU byte states and 65,536 native dispatcher programs, output guards and untouched selection state passed");
    }
    g_ScriptNodePool=saved_node;g_ScriptCursor=saved_cursor;g_CurrentEntity=saved_actor;
    if(valid)puts("Original timer opcode 210: 672 unhooked USA CPU reads and output aliases plus 48 dispatcher programs passed");
    GameTime_InitCounter0(); GameTime_InitCounter1(); GameTime_InitCounter2();
    if(g_GameClockFlags!=1 || g_GameTimeTable!=0 || g_GameClockLimit!=21600000 ||
       g_GameTimers[1].flags!=1 || g_GameTimers[1].current || g_GameTimers[1].limit ||
       g_GameTimers[2].flags!=1 || g_GameTimers[2].current || g_GameTimers[2].limit) valid=0;
    for(unsigned c=0;c<sizeof(cases)/sizeof(cases[0]);++c) {
        memcpy(&g_GameTimers[0].current,&cases[c].frames,sizeof(u32));
        Inv_SetDefaultItemId();
        uint64_t table=UINT64_C(14695981039346656037);
        uint64_t stream=table;
        for(unsigned i=0;i<521;++i) table=(table^g_ItemDropRngTable[i])*UINT64_C(1099511628211);
        if(g_ItemDropRngIndex!=520 || table!=cases[c].table_hash) valid=0;
        for(unsigned i=0;i<4096;++i) {
            int byte=Inv_ScrambleGrid();
            if(byte<0 || byte>255 || g_ItemDropRngIndex!=(s32)(i%521)) valid=0;
            stream=(stream^(unsigned)byte)*UINT64_C(1099511628211);
        }
        if(stream!=cases[c].stream_hash) valid=0;
    }
    memcpy(g_GameTimers,saved_timers,sizeof(saved_timers));
    memcpy(g_ItemDropRngTable,saved_table,sizeof(saved_table));
    g_ItemDropRngIndex=saved_index;
    if(!valid) fprintf(stderr,"Original item RNG/timer reset differs from USA EXE\n");
    return valid;
}

static int script_movie_error_exit(const char *executable)
{
    const char *args[]={executable,"--script-movie-error",NULL};
    SDL_PropertiesID properties=SDL_CreateProperties();
    SDL_SetPointerProperty(properties,SDL_PROP_PROCESS_CREATE_ARGS_POINTER,(void *)args);
    SDL_SetNumberProperty(properties,SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(properties,SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,SDL_PROCESS_STDIO_NULL);
    SDL_Process *process=SDL_CreateProcessWithProperties(properties);SDL_DestroyProperties(properties);
    if(!process){fprintf(stderr,"movie error child failed to start: %s\n",SDL_GetError());return 0;}
    int status=99;int waited=SDL_WaitProcess(process,true,&status);SDL_DestroyProcess(process);
#ifdef _WIN32
    int expected=3;
#else
    int expected=-SIGABRT;
#endif
    if(!waited || status!=expected){fprintf(stderr,"movie opcode failure did not terminate VM: %d\n",status);return 0;}
    puts("Original script error: movie failure terminates dispatcher instead of retrying or continuing the scene");return 1;
}

typedef struct OriginalSpuRegisterAccess {
    unsigned operation;
    SDL_Semaphore *entered;
    SDL_AtomicInt done;
} OriginalSpuRegisterAccess;

static int SDLCALL original_spu_register_worker(void *userdata)
{
    OriginalSpuRegisterAccess *access=userdata;
    SDL_SignalSemaphore(access->entered);
    switch(access->operation){
    case 0:AkaoSpuVoice_SetVolume(0,0x1234,0x5678);break;
    case 1:AkaoSpuVoice_SetPitch(0,0x3456);break;
    case 2:AkaoSpuVoice_SetStartAddress(0,0x3000);break;
    case 3:AkaoSpuVoice_SetRepeatAddress(0,0x3800);break;
    case 4:AkaoSpuVoice_SetAdsrAttack(0,17,4);break;
    case 5:AkaoSpuVoice_SetAdsrDecayRate(0,8);break;
    case 6:AkaoSpuVoice_SetAdsrSustainLevel(0,11);break;
    case 7:AkaoSpuVoice_SetAdsrSustainRate(0,32,6);break;
    case 8:AkaoSpuVoice_SetAdsrReleaseRate(0,31,4);break;
    }
    SDL_SetAtomicInt(&access->done,1);return 0;
}

/* Register-level fixtures isolate the SPU from AKAO and the host mixer. */
static void spu_modulation_fixture(unsigned fm)
{
    static const unsigned char modulator[16] = {0,7,0x31,0x75,0x31,0x75,0x31,0x75,0x31,0x75,0x31,0x75,0x31,0x75,0x31,0x75};
    static const unsigned char carrier[16] = {2,7,0x10,0x32,0x54,0x76,0x54,0x32,0x10,0xFE,0xDC,0xBA,0x98,0xBA,0xDC,0xFE};
    Psyz_SpuReset(0);
    Psyz_SpuMemWrite(0x1000,modulator,sizeof(modulator));
    Psyz_SpuMemWrite(0x1100,carrier,sizeof(carrier));
    Psyz_SpuWrite(0x1AA,0xC000);Psyz_SpuWrite(0x180,0x3FFF);Psyz_SpuWrite(0x182,0x3FFF);
    for(unsigned v=1;v<=2;++v) {
        unsigned base=v*16, addr=v==1?0x200:0x220;
        Psyz_SpuWrite(base+0x00,v==2?0x3FFF:0);
        Psyz_SpuWrite(base+0x02,v==2?0x3FFF:0);
        Psyz_SpuWrite(base+0x04,0x1000);Psyz_SpuWrite(base+0x06,addr);
        Psyz_SpuWrite(base+0x08,0x000F);Psyz_SpuWrite(base+0x0A,0x1FC0);
        Psyz_SpuWrite(base+0x0E,addr);
    }
    Psyz_SpuWrite(0x190,fm);Psyz_SpuWrite(0x188,6);
}

static int original_spu_pitch_modulation(void)
{
    enum {FRAMES=256};
    short reference[FRAMES*2],actual[FRAMES*2],modulator[FRAMES];
    Psyz_SpuInit();
    spu_modulation_fixture(0);
    Psyz_SpuPullSamples(reference,FRAMES);
    Psyz_SpuMemRead(0x800,modulator,sizeof(modulator));
    /* Independently program the documented rate from each preceding voice's
     * post-ADSR sample. Its mixer volume is zero and must not suppress FM. */
    spu_modulation_fixture(0);
    for(unsigned frame=0;frame<FRAMES;++frame) {
        unsigned step=(unsigned)((0x1000*(modulator[frame]+32768))>>15);
        if(step>0x3FFF)step=0x3FFF;
        Psyz_SpuWrite(0x24,step);
        Psyz_SpuPullSamples(reference+frame*2,1);
    }
    spu_modulation_fixture(4);
    Psyz_SpuPullSamples(actual,FRAMES);
    if(memcmp(reference,actual,sizeof(reference))) {
        fputs("SPU FM does not match per-sample rate programming from muted preceding voice\n",stderr);
        return 0;
    }
    puts("SPU FM: 256 per-sample stereo frames match independent pitch programming; muted modulator remains effective");
    return 1;
}

static int original_spu_noise(void)
{
    static const unsigned char silence[16]={0,7};
    int valid=1;
    Psyz_SpuInit();
    for(unsigned pass=0;pass<2;++pass) {
        Psyz_SpuReset(0);Psyz_SpuMemWrite(0x1000,silence,sizeof(silence));
        Psyz_SpuWrite(0x1AA,0xFF00); /* fastest shared noise clock */
        Psyz_SpuWrite(0x14,pass?0x800:0x1000);Psyz_SpuWrite(0x16,0x200);
        Psyz_SpuWrite(0x18,0x000F);Psyz_SpuWrite(0x1A,0x1FC0);
        Psyz_SpuWrite(0x1E,0x200);Psyz_SpuWrite(0x194,2);Psyz_SpuWrite(0x188,2);
        unsigned level=0;int timer=0;
        for(unsigned frame=0;frame<256;++frame) {
            /* PSX-SPX noise LFSR/timer, independent of ADPCM pitch. */
            timer-=7;
            if(timer<0) {
                unsigned parity=((level>>15)^(level>>12)^(level>>11)^(level>>10)^1)&1;
                level=((level<<1)|parity)&0xFFFF;
                timer+=4;if(timer<0)timer+=4;
            }
            short pcm[2],sample;
            /* ENVX after a clock supplies the following output sample. */
            int envelope=Psyz_SpuRead(0x1C);
            Psyz_SpuPullSamples(pcm,1);Psyz_SpuMemRead(0x800+frame*2,&sample,2);
            int expected=((int)(s16)level*envelope)>>15;
            if(sample!=expected) {
                fprintf(stderr,"SPU noise mismatch at frame %u pitch %x: %d != %d\n",frame,pass?0x800:0x1000,sample,expected);
                valid=0;break;
            }
        }
    }
    if(valid)puts("SPU noise: 512 captured post-ADSR samples match shared LFSR clock at two independent ADPCM rates");
    return valid;
}

static int original_spu_mix_headroom(void)
{
    static const unsigned char sample[16]={0,7,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77};
    short pcm[64];
    Psyz_SpuInit();Psyz_SpuReset(0);Psyz_SpuMemWrite(0x1000,sample,sizeof(sample));
    Psyz_SpuWrite(0x1AA,0xC000);Psyz_SpuWrite(0x180,0x3FFF);Psyz_SpuWrite(0x182,0x3FFF);
    for(unsigned v=0;v<24;++v) {
        unsigned b=v*16;
        Psyz_SpuWrite(b,0x3FFF);Psyz_SpuWrite(b+2,0x3FFF);
        Psyz_SpuWrite(b+4,0x1000);Psyz_SpuWrite(b+6,0x200);
        Psyz_SpuWrite(b+8,0x000F);Psyz_SpuWrite(b+10,0x1FC0);Psyz_SpuWrite(b+14,0x200);
    }
    Psyz_SpuWrite(0x188,0xFFFF);Psyz_SpuWrite(0x18A,0xFF);
    Psyz_SpuPullSamples(pcm,32);
    if(pcm[62]!=32765 || pcm[63]!=32765) {
        fprintf(stderr,"SPU positive mix overflow: %d,%d instead of saturated bus scaled by master volume\n",pcm[62],pcm[63]);return 0;
    }
    Psyz_SpuWrite(0x180,0x2000);Psyz_SpuWrite(0x182,0x6000);
    Psyz_SpuPullSamples(pcm,32);
    if(pcm[62]!=16383 || pcm[63]!=-16384) {
        fprintf(stderr,"SPU master gain/phase applied before bus saturation: %d,%d\n",pcm[62],pcm[63]);return 0;
    }
    puts("SPU 24-voice mix: saturation before master gain, half volume and negative master phase without overflow");return 1;
}

static int original_spu_live_adsr(void)
{
    static const unsigned char sample[16] = {0,7,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11};
    short pcm[64];
    int valid = 1;
    Psyz_SpuInit();
    Psyz_SpuReset(0);
    Psyz_SpuMemWrite(0x1000, sample, sizeof(sample));
    Psyz_SpuWrite(0x1AA,0xC000);
    Psyz_SpuWrite(0x000,0x3FFF);Psyz_SpuWrite(0x002,0x3FFF);
    Psyz_SpuWrite(0x004,0x1000);Psyz_SpuWrite(0x006,0x200);
    Psyz_SpuWrite(0x008,0x000F);Psyz_SpuWrite(0x00A,0x001F);
    Psyz_SpuWrite(0x00E,0x200);Psyz_SpuWrite(0x188,1);
    Psyz_SpuPullSamples(pcm,32);
    unsigned before = Psyz_SpuRead(0x00C);
    AkaoSpuVoice_SetAdsrReleaseRate(0,0,0);
    Spu_WriteKeyOff(1);
    Psyz_SpuPullSamples(pcm,32);
    unsigned released = Psyz_SpuRead(0x00C);
    if (before < 30000 || released != 0) {
        fprintf(stderr,"Live ADSR release ignored: before=%u after=%u (expected zero after rate 0)\n",before,released);
        valid=0;
    }
    Psyz_SpuReset(0);
    Psyz_SpuMemWrite(0x1000,sample,sizeof(sample));
    Psyz_SpuWrite(0x1AA,0xC000);
    Psyz_SpuWrite(0x004,0x1000);Psyz_SpuWrite(0x006,0x200);
    Psyz_SpuWrite(0x008,0x7F0F);Psyz_SpuWrite(0x00A,0x001F);
    Psyz_SpuWrite(0x00E,0x200);Psyz_SpuWrite(0x188,1);
    Psyz_SpuPullSamples(pcm,32);
    unsigned slow = Psyz_SpuRead(0x00C);
    AkaoSpuVoice_SetAdsrAttack(0,0,0);
    Psyz_SpuPullSamples(pcm,32);
    unsigned fast = Psyz_SpuRead(0x00C);
    if (slow != 0 || fast < 30000) {
        fprintf(stderr,"Live ADSR attack ignored: slow=%u fast=%u\n",slow,fast);
        valid=0;
    }
    if(valid)puts("Original AKAO setters: live attack and release rates affect active SPU voices without a new key-on");
    return valid;
}

static int original_spu_register_serialization(void)
{
    if(!SDL_Init(SDL_INIT_EVENTS))return 0;
    const char *clock=getenv("PSYZ_AUDIO_CLOCK");char *saved=clock?SDL_strdup(clock):NULL;
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"PSYZ_AUDIO_CLOCK","video",true);
    if(Psyz_AudioInit()!=0){SDL_free(saved);return 0;}
    unsigned started=0,completed=0,early=0;
    for(unsigned operation=0;operation<9;++operation){
        OriginalSpuRegisterAccess access={.operation=operation};access.entered=SDL_CreateSemaphore(0);
        if(!access.entered)continue;
        Psyz_AudioLock();
        SDL_Thread *thread=SDL_CreateThread(original_spu_register_worker,"Original SPU register probe",&access);
        if(thread && SDL_WaitSemaphoreTimeout(access.entered,2000)){
            started|=1u<<operation;SDL_Delay(20);
            if(SDL_GetAtomicInt(&access.done))early|=1u<<operation;
        }
        Psyz_AudioUnlock();
        if(thread){int status=-1;SDL_WaitThread(thread,&status);if(status==0 && SDL_GetAtomicInt(&access.done))completed|=1u<<operation;}
        SDL_DestroySemaphore(access.entered);
    }
    Psyz_AudioDestroy();
    if(saved){SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"PSYZ_AUDIO_CLOCK",saved,true);SDL_free(saved);}
    else SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(),"PSYZ_AUDIO_CLOCK");
    int valid=started==0x1FF && completed==0x1FF && early==0;
    if(valid)puts("Original SPU register access: all nine original setters serialize with mixer transactions, without changing their register operations");
    else fprintf(stderr,"Original SPU serialization started=%x completed=%x early=%x\n",started,completed,early);
    return valid;
}

