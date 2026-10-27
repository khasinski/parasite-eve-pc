    if(argc==2 && !strcmp(argv[1],"--save-safety"))return native_save_safety();
    if(argc==2 && !strcmp(argv[1],"--all-room-loads"))return original_all_room_loads();
    if(argc==2 && !strcmp(argv[1],"--anim-event-completion")){
        RenderAnimationDataHeader animation={0};animation.frame_count=8;
        void *old_animation=g_GameState.bank_rows[0][2];
        u32 old_flags=g_GameStateFlags;g_GameStateFlags=2;
        g_GameState.bank_rows[0][2]=&animation;
        unsigned cases=0;
        for(unsigned kind=0;kind<3;++kind)for(unsigned mode=0;mode<2;++mode)
        for(unsigned slot=0;slot<6;++slot)for(unsigned finished=0;finished<2;++finished)
        for(unsigned active=0;active<2;++active){
            EntityAnimEventCore core={0};BattleEntity entity={0},parent={0};
            core.kind=kind==0?0:kind==1?2:4;core.baseMode=2;
            core.records[slot].state=finished?4:0;
            core.active=active?&core.records[slot]:NULL;
            entity.core=&core;entity.parent=kind?&parent:NULL;
            BattleEntity *actor=kind?&parent:&entity;actor->actionMode=mode;
            actor->animStep=123;actor->animFrame=0;
            EntityAnimEventCore before=core;
            int result=Entity_TriggerAnimEvent(&entity,slot);
            if(result!=(int)finished || actor->actionMode!=(finished?2:mode) ||
               actor->animStep!=(finished?65536:123))return 1;
            if(finished){if(active)before.records[slot].state=0;before.active=NULL;}
            if(memcmp(&core,&before,sizeof(core)))return 1;
            ++cases;
        }
        g_GameState.bank_rows[0][2]=old_animation;g_GameStateFlags=old_flags;
        printf("Original animation event completion: %u parent/self, idle, active/null and finished/pending cases passed\n",cases);
        return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--sewer-twelve"))return original_sewer_twelve_assets()?0:1;
    if(argc==2 && !strcmp(argv[1],"--hospital-spark"))return original_hospital_spark_particle_states() && original_hospital_spark_assets()?0:1;
    if(argc==2 && !strcmp(argv[1],"--chrysler-particles"))return original_chrysler_particle_assets()?0:1;
    if(argc==2 && !strcmp(argv[1],"--floor-walker"))return original_floor_walker_states() && original_timed_spawn_states() && original_floor_walker_assets()?0:1;
    if(argc==2 && !strcmp(argv[1],"--hospital-eruption")){
        if(!original_hospital_eruption_states() || !original_hospital_homing_flight_states() || !original_hospital_homing_throw_states())return 1;
        /* An animation may finish before its effect's final slot. No active
         * event must leave cues untouched and let those slots finish. */
        FieldFxObject lingering={0};FieldActor lingering_actor={0};FieldActorState lingering_state={0};
        lingering.prefix.actor=&lingering_actor;lingering_actor.state=&lingering_state;
        lingering_state.control10.command_value=120;lingering.prefix.asset_type=37;
        lingering.state.slot_count=1;FieldFxHandler empty_handlers[]={NULL};
        if(FieldFx_UpdateSlots(&lingering,empty_handlers))return 1;
        RoomEruptionActor eruption={.collisionState=1};u8 signal[4]={1,0,0,0};
        if(g_RoomEruptionUpdateList[0](&lingering,signal,&eruption) || eruption.cooldown || lingering_state.core_flags || signal[1])return 1;
        FieldFxObjectPrefix lingering_owner={.actor=&lingering_actor};FieldFxTaskContext lingering_context={0};
        g_FieldFxOwner=&lingering_owner;g_FieldFxTaskContext=&lingering_context;
        RoomHomingModel interrupted={.armed=1};
        if(RoomEffect_HomingProjectileThrow(1,&interrupted,0)!=1 || interrupted.state!=-1 || !interrupted.released)return 1;
        lingering_actor.state=NULL;interrupted=(RoomHomingModel){.armed=1};
        if(RoomEffect_HomingProjectileThrow(1,&interrupted,0)!=1 || interrupted.state!=-1 || !interrupted.released)return 1;
        g_FieldFxOwner=NULL;g_FieldFxTaskContext=NULL;g_FieldFxScriptState=NULL;
        /* Exercise the native callback ABI, full pointer storage and guards. */
        FieldFxTaskContext context={0};FieldFxObjectPrefix owner={0};
        FieldActor actor={0};FieldActorState state={0};RoomHomingModel model={0};
        owner.actor=&actor;actor.state=&state;
        g_FieldFxTaskContext=&context;g_FieldFxOwner=&owner;
        for(unsigned i=0;i<7;++i)context.variables[i]=(s16)(0x1230+i);
        RoomEffect_HomingProjectileConfigure(0,0x12345678,-17,0);
        RoomEffect_HomingProjectileConfigure(1,-12345,4567,0);
        RoomEffect_HomingProjectileConfigure(2,71,93,0);
        g_RoomFloorY->y=-123;
        if(RoomEffect_HomingProjectileThrow(0,&model,0) || model.speed!=0x12345678 ||
           model.drag!=-17 || model.lift!=-12345 || model.gravity!=4567 || model.floor!=-123 ||
           model.damping!=8 || model.bounce!=10 || model.scriptArg0!=71 || model.scriptArg1!=93 ||
           model.soundHandle!=-1 || model.matrixIndex!=26 || state.core_flags!=0x40000000)return 1;
        RoomHomingModel *stored=NULL;memcpy(&stored,&context.variables[1],sizeof(stored));
        if(stored!=&model || context.variables[0]!=0x1230 || context.variables[5]!=0x1235 || context.variables[6]!=0x1236)return 1;
        RoomEffect_HomingProjectileConfigure(0,-7,8,13);RoomEffect_HomingProjectileConfigure(1,9,-10,17);
        if(g_RoomHomingLaunch.damping!=13 || g_RoomHomingLaunch.bounce!=17)return 1;
        RoomHomingLaunch launch=g_RoomHomingLaunch;RoomEffect_HomingProjectileConfigure(3,1,2,3);
        if(memcmp(&launch,&g_RoomHomingLaunch,sizeof(launch)))return 1;
        for(unsigned age=0;age<16;++age){g_FieldFxCurrentAge=age;
            if(RoomEffect_HomingProjectileSpark(1,NULL,0)!=(age>=9))return 1;}
        g_FieldFxTaskContext=NULL;g_FieldFxOwner=NULL;g_RoomFloorY->y=0;
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe,archive;
        if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path) || !pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        const unsigned rooms[]={141,146,328};
        for(unsigned index=0;index<3;++index){
            SceneRoomSectorRange range;u8 module[14084];size_t bytes=rooms[index]==146?14084:12780;
            if(!pe_disc_read(&disc,&exe,0x800ull+0x80093378u-0x80010000u+(rooms[index]-1)*8,&range,8) ||
               !pe_disc_read(&disc,&archive,(u64)(range.start+range.scratchSectors+range.textureSectors)*2048+8,module,bytes))return 1;
            if(memcmp(module+(rooms[index]==146?0x34d8:0x306c),g_RoomEruptionScript,sizeof(int)*18) ||
               memcmp(module+(rooms[index]==146?0x35dc:0x3110),g_RoomHomingScript,sizeof(g_RoomHomingScript)))return 1;
            if(rooms[index]==146){
                uint64_t hash=UINT64_C(14695981039346656037);for(size_t j=0;j<bytes;++j)hash=(hash^module[j])*UINT64_C(1099511628211);
                if(hash!=UINT64_C(0x4354368d130c77fb) || memcmp(module+0x3590,g_RoomScript__falling,sizeof(g_RoomScript__falling)))return 1;
            }
            char name[16];snprintf(name,sizeof(name),"m%04ui",rooms[index]);u32 token=Text_ParseBase32Id((signed char *)name);
            for(unsigned pass=0;pass<2;++pass){
                memset(&g_RoomHomingLaunch,0xa5,sizeof(g_RoomHomingLaunch));
                RoomFxSpritePacket *packets[]={&g_RoomEruptionGlowPacket,&g_RoomEruptionDebrisPacket,&g_RoomEruptionShadowPacket,&g_RoomEruptionRingPacket};
                for(unsigned i=0;i<4;++i)memset(packets[i],0xa5,sizeof(*packets[i]));
                RoomFxSpritePacket *falling[]={&g_RoomFallingBurstShadow,&g_RoomFallingBurstShimmer,&g_RoomFallingBurstSprite,&g_RoomFallingBurstPulse,&g_RoomFallingBurstParticle};
                if(rooms[index]==146)for(unsigned i=0;i<5;++i)memset(falling[i],0xa5,sizeof(*falling[i]));
                int loaded=Scene_LoadRoom(token);
                if(loaded || g_FieldFxTaskPrograms[81]!=&g_RoomHomingProgram || g_PmCmdHandlerTable[37]!=&g_RoomGroundEruptionHandlers){fprintf(stderr,"Eruption room%u pass%u load%d\n",rooms[index],pass,loaded);return 1;}
                if(rooms[index]==146){
                    if(g_PmCmdHandlerTable[40]!=&g_RoomFallingBurstHandlers)return 1;
                    for(unsigned i=0;i<5;++i)for(unsigned j=0;j<sizeof(*falling[i]);++j)if(((u8 *)falling[i])[j])return 1;
                }else if(g_FieldFxTaskPrograms[37]!=&g_RoomDroppedFlareProgram)return 1;
                for(unsigned i=0;i<sizeof(g_RoomHomingLaunch);++i)if(((u8 *)&g_RoomHomingLaunch)[i])return 1;
                for(unsigned i=0;i<4;++i)for(unsigned j=0;j<sizeof(*packets[i]);++j)if(((u8 *)packets[i])[j])return 1;
                if(rooms[index]==146 && pass==0 && !original_hospital_object_cycles())return 1;
            }
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Hospital eruption: real M141/M146/M328 scripts, all three descriptors per room and repeated reset; homing native pointer guards, launch parameters and spark lifetime passed");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--hospital-falling")){
        if(!original_hospital_falling_states())return 1;
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe,archive;
        if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path) || !pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        const unsigned rooms[]={137,140,147,324,327};
        for(unsigned index=0;index<5;++index){
            SceneRoomSectorRange range;u8 module[8108];
            if(!pe_disc_read(&disc,&exe,0x800ull+0x80093378u-0x80010000u+(rooms[index]-1)*8,&range,8) ||
               !pe_disc_read(&disc,&archive,(u64)(range.start+range.scratchSectors+range.textureSectors)*2048+8,module,sizeof(module)))return 1;
            uint64_t hash=UINT64_C(14695981039346656037);for(unsigned j=0;j<sizeof(module);++j)hash=(hash^module[j])*UINT64_C(1099511628211);
            if(hash!=UINT64_C(0x54c61f96a7dcbbc1) || memcmp(module+0x1eb0,g_RoomScript__falling,sizeof(g_RoomScript__falling)) ||
               memcmp(module+0x1efc,g_RoomDroppedFlareScript,36))return 1;
            char name[16];snprintf(name,sizeof(name),"m%04ui",rooms[index]);
            for(unsigned pass=0;pass<2;++pass){
                memset(&g_RoomFallingBurstSprite,0xa5,sizeof(g_RoomFallingBurstSprite));g_RoomDroppedFlareArgs[0]=123;
                if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)name)) || g_PmCmdHandlerTable[40]!=&g_RoomFallingBurstHandlers ||
                   g_FieldFxTaskPrograms[37]!=&g_RoomDroppedFlareProgram || g_RoomDroppedFlareArgs[0])return 1;
                const u8 *packet=(const void *)&g_RoomFallingBurstSprite;for(unsigned j=0;j<sizeof(g_RoomFallingBurstSprite);++j)if(packet[j])return 1;
            }
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original five hospital modules: complete USA 8108-byte modules, original scripts, both native descriptors, real assets, geometry and per-entry reset passed twice");return 0;
    }

    if(argc==2 && !strcmp(argv[1],"--m123-ai")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe,archive;SceneRoomSectorRange range;
        if(!pe_disc_open(&disc,path) || !pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive) ||
           !pe_disc_read(&disc,&exe,0x800ull+0x80093378u-0x80010000u+122*8,&range,8))return 1;
        size_t bytes=(size_t)range.roomSectors*2048;SceneAssetView *room=malloc(bytes);
        int ok=room && pe_disc_read(&disc,&archive,(u64)(range.start+range.scratchSectors+range.textureSectors)*2048,room,bytes) && original_m123_ai(room,bytes);
        free(room);pe_disc_close(&disc);return ok?0:1;
    }
    if(argc==2 && !strcmp(argv[1],"--m318-states"))return original_m318_numeric() && original_m318_pool_states()?0:1;
    if(argc==2 && !strcmp(argv[1],"--m318")){
        if(!original_m318_numeric() || !original_m318_pool_states())return 1;
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)"m0318i")))return 1;
        int ok=original_m318_programs(&disc);CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);return ok?0:1;
    }
    if(argc==2 && !strcmp(argv[1],"--m318-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        u32 token=Text_ParseBase32Id((signed char *)"m0318i");
        const struct {void *data;size_t size;} zero[]={
            {&g_RoomM318ActorParams,8},
            {&g_RoomM318EmberBurstParams,8},
            {&g_RoomM318RisingColumnParams,16},
            {&g_RoomM318ScriptArg2,4},
            {&g_RoomM318StarFlarePhase,4},
            {&g_RoomScriptArg,4},
            {&g_RoomM318FanSweepAnchor,8},
            {&g_RoomM318ShakeSweepAnchor,8},
            {&g_RoomM318RisingColumnAnchor,8},
            {&g_RoomM318CrossFlashAnchor,8},
            {&g_RoomM318EmberFountainAnchor,8},
        };
        FieldFxTaskProgram *programs[]={&g_RoomM318FanSweepEffects,&g_RoomM318ShakeSweepEffects,
            &g_RoomM318ShakeBurstEffects,&g_RoomM318EmberBurstEffects,&g_RoomM318RisingColumnEffects,
            &g_RoomM318EmberFountainEffects,&g_RoomM318FlashEffects,&g_RoomM318TwinRibbonEffects};
        for(unsigned pass=0;pass<2;++pass){
            FieldActor dummy={0};g_RoomM318EmberSpiralModel=g_RoomM318FanSweepSparkPool=&dummy;
            for(unsigned i=0;i<sizeof(zero)/sizeof(*zero);++i)memset(zero[i].data,0xa5,zero[i].size);
            for(unsigned i=0;i<8;++i)g_FieldFxTaskPrograms[83+i]=NULL;
            g_PmCmdHandlerTable[75]=g_PmCmdHandlerTable[70]=NULL;
            int status=Scene_LoadRoom(token);
            if(status || g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2] || g_PmCmdHandlerTable[70]!=&g_RoomActorClassTable[0] ||
               g_RoomM318EmberSpiralModel || g_RoomM318FanSweepSparkPool){fprintf(stderr,"M318 real load pass%u status%d\n",pass,status);return 1;}
            for(unsigned i=0;i<8;++i)if(g_FieldFxTaskPrograms[83+i]!=programs[i])return 1;
            for(unsigned i=0;i<sizeof(zero)/sizeof(*zero);++i)for(unsigned j=0;j<zero[i].size;++j)if(((u8 *)zero[i].data)[j])return 1;
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M318 loader: real CUE I/O, native geometry, all ten registrations and mutable-state reset passed twice");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m318-data")){
        if(!original_m318_numeric() || !original_m318_pool_states() || !original_m318_gate())return 1;
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe;if(!pe_disc_open(&disc,path) || !pe_disc_find(&disc,"SLUS_006.62",&exe))return 1;
        int ok=original_m318_assets(&disc,&exe);pe_disc_close(&disc);return ok?0:1;
    }
    if(argc==2 && !strcmp(argv[1],"--m256")){
        if(!original_m256_states() || !original_m256_swing())return 1;
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe;if(!pe_disc_open(&disc,path) || !pe_disc_find(&disc,"SLUS_006.62",&exe))return 1;
        int ok=original_m256_assets(&disc,&exe);pe_disc_close(&disc);return ok?0:1;
    }
    if(argc==2 && !strcmp(argv[1],"--m256-states"))return original_m256_states() && original_m256_swing()?0:1;
    if(argc==2 && !strcmp(argv[1],"--m404")){
        if(!original_m404_states() || !original_m404_contact())return 1;
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe;if(!pe_disc_open(&disc,path) || !pe_disc_find(&disc,"SLUS_006.62",&exe))return 1;
        int ok=original_m404_assets(&disc,&exe);pe_disc_close(&disc);return ok?0:1;
    }
    if(argc==2 && !strcmp(argv[1],"--m404-states"))return original_m404_states() && original_m404_contact()?0:1;
    if(argc==2 && !strcmp(argv[1],"--m350")){
        if(!original_m350_states())return 1;
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe;if(!pe_disc_open(&disc,path) || !pe_disc_find(&disc,"SLUS_006.62",&exe))return 1;
        int ok=original_m350_assets(&disc,&exe);pe_disc_close(&disc);return ok?0:1;
    }
    if(argc==2 && !strcmp(argv[1],"--m256-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        u32 token=Text_ParseBase32Id((signed char *)"m0256i");
        for(unsigned pass=0;pass<2;++pass){
            FieldActor dummy={0};g_RoomM256TrailModel=g_RoomM256PulseModel=&dummy;
            g_RoomM256ScatterConfig=g_RoomM256TrailConfig=g_RoomM256TrailTurnScale=123;
            memset(g_RoomM256TrailHistory,0xa5,sizeof(g_RoomM256TrailHistory));g_RoomM256PulseBrightness=123;
            memset(&g_RoomM256PulseAngles,0xa5,sizeof(g_RoomM256PulseAngles));memset(&g_RoomM256PulseCenter,0xa5,sizeof(g_RoomM256PulseCenter));
            g_FieldFxTaskPrograms[46]=g_FieldFxTaskPrograms[47]=NULL;g_PmCmdHandlerTable[75]=g_PmCmdHandlerTable[76]=NULL;
            int status=Scene_LoadRoom(token);
            if(status || g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2] || g_PmCmdHandlerTable[76]!=&g_RoomM256SwingClass ||
               g_FieldFxTaskPrograms[46]!=&g_RoomM256ScatterEffects || g_FieldFxTaskPrograms[47]!=&g_RoomM256TrailEffects ||
               g_RoomM256TrailModel || g_RoomM256PulseModel || g_RoomM256ScatterConfig || g_RoomM256TrailConfig || g_RoomM256TrailTurnScale || g_RoomM256PulseBrightness){
                fprintf(stderr,"M256 real load pass%u status%d\n",pass,status);return 1;}
            const struct {void *data;size_t size;} zero[]={ {g_RoomM256TrailHistory,sizeof(g_RoomM256TrailHistory)}, {&g_RoomM256PulseAngles,sizeof(g_RoomM256PulseAngles)}, {&g_RoomM256PulseCenter,sizeof(g_RoomM256PulseCenter)} };
            for(unsigned i=0;i<3;++i)for(unsigned j=0;j<zero[i].size;++j)if(((u8 *)zero[i].data)[j])return 1;
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M256 loader: real CUE I/O, native geometry, all four registrations and all mutable state resets passed twice");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m404-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        u32 token=Text_ParseBase32Id((signed char *)"m0404i");
        for(unsigned pass=0;pass<2;++pass){
            g_RoomScriptArg=0x12345678;g_FieldFxTaskPrograms[48]=g_FieldFxTaskPrograms[49]=NULL;g_PmCmdHandlerTable[75]=NULL;
            int status=Scene_LoadRoom(token);
            if(status || g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2] ||
               g_FieldFxTaskPrograms[48]!=&g_RoomM404SinkingDropEffects || g_FieldFxTaskPrograms[49]!=&g_RoomM404DebrisBurstEffects || g_RoomScriptArg){
                fprintf(stderr,"M404 real load pass%u status%d\n",pass,status);return 1;}
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M404 loader: real CUE I/O, native geometry, all three descriptor registrations and argument reset passed twice");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m221-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe,archive;SceneRoomSectorRange range;u8 module[22804];
        if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        if(!pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive))return 1;
        const unsigned rooms[]={203,221,223,224,229,231,233,234,242,246};
        for(unsigned ri=0;ri<sizeof(rooms)/sizeof(rooms[0]);++ri){
            unsigned room=rooms[ri],combined=room==231 || room==234;
            unsigned class0=room==203 || room==224 || room==233 || room==242 || room==246;
            unsigned size=combined?22804:20828;
            if(!pe_disc_read(&disc,&exe,0x800+(0x80093378-0x80010000)+(room-1)*8,&range,8))return 1;
            u32 offset=(range.start+range.scratchSectors+range.textureSectors)*2048u;
            if(!pe_disc_read(&disc,&archive,offset+8,module,size))return 1;
            uint64_t hash=UINT64_C(14695981039346656037);
            for(unsigned i=0;i<size;++i)hash=(hash^module[i])*UINT64_C(1099511628211);
            if(hash!=(combined?UINT64_C(0x43eec2de603026ac):UINT64_C(0xa112e74d849b5626)))return 1;
            if(memcmp(module+((combined?0x80194824:0x80194078)-0x8018eff0),g_RoomSparkRingProgram.script,36) ||
               memcmp(module+((combined?0x8019489c:0x801940f0)-0x8018eff0),g_RoomBurstSparkProgram.script,32) ||
               memcmp(module+((combined?0x80194844:0x80194098)-0x8018eff0),g_RoomSparkRingColorRamp,32) ||
               g_RoomSparkRingProgram.table.sizes[0]!=12 || g_RoomBurstSparkProgram.table.sizes[0]!=4 ||
               (combined && memcmp(module+(0x801947c8-0x8018eff0),g_RoomSoundBurstProgram.script,36)))return 1;
            char name[16];snprintf(name,sizeof(name),"m%04ui",room);
            for(unsigned pass=0;pass<2;++pass){
                g_PmCmdHandlerTable[70]=g_PmCmdHandlerTable[75]=NULL;
                g_FieldFxTaskPrograms[31]=g_FieldFxTaskPrograms[57]=g_FieldFxTaskPrograms[95]=NULL;
                g_RoomSparkSlot=123;g_RoomScriptArgs[0]=11;g_RoomScriptArgs[1]=22;g_RoomScriptArgs[2]=33;
                int status=Scene_LoadRoom(Text_ParseBase32Id((signed char *)name));
                if(status || g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2] ||
                   g_PmCmdHandlerTable[70]!=(class0?&g_RoomActorClassTable[0]:NULL) ||
                   g_FieldFxTaskPrograms[57]!=&g_RoomSparkRingProgram ||
                   g_FieldFxTaskPrograms[95]!=&g_RoomBurstSparkProgram || g_RoomSparkSlot ||
                   g_FieldFxTaskPrograms[31]!=(combined?&g_RoomSoundBurstProgram:NULL) ||
                   (combined && (g_RoomScriptArgs[0] || g_RoomScriptArgs[1] || g_RoomScriptArgs[2]))){
                    fprintf(stderr,"Spark module M%u reload%u status%d failed\n",room,pass,status);return 1;}
                intptr_t slot=g_RoomBurstSparkProgram.initialize(0,55,0,0);
                if(slot!=(intptr_t)&g_RoomSparkSlot || g_RoomSparkSlot!=55)return 1;
                if(combined && g_RoomSoundBurstProgram.initialize(0,3,-4,5)!=(intptr_t)g_RoomScriptArgs)return 1;
            }
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        FieldActor actor={0},player={0};FieldActorState actor_state={0};RenderMatrix transforms[18]={0};
        struct {u64 before;u8 value;u8 padding[7];u64 after;} marker={0};
        FieldFxObjectPrefix owner={0};FieldFxTaskContext context={0};FieldFxTaskSlot task={0};
        _Alignas(void *) u8 arena[8192];RoomSparkRingState ring={0};int count;
        marker.before=marker.after=UINT64_C(0x13579bdf2468ace0);
        actor.state=&actor_state;actor.render_object.matrices=transforms;
        player.render_object.matrices=transforms;g_PlayerEntity=&player;
        actor_state.action_state=&marker.value;owner.actor=&actor;task.end=(char *)arena;
        g_FieldFxOwner=&owner;g_FieldFxTaskContext=&context;g_FieldFxActiveTaskSlot=&task;g_FieldFxCurrentAge=0;
        const u8 initial[]={0,1,2,255};
        for(unsigned enabled=0;enabled<2;++enabled)for(unsigned i=0;i<4;++i){
            context.flags=enabled;actor_state.core_flags=0x13579bdf;
            marker.value=initial[i];
            if(RoomEffect_SparkRingController(0,&ring,0)<=0 ||
               marker.value!=(enabled && initial[i]==1?2:initial[i]))return 1;
            marker.value=initial[i];count=99;
            if(RoomEffect_BurstSparkSpawner(0,&count,0)<=0 || count ||
               marker.value!=(enabled && initial[i]==1?2:initial[i]) ||
               actor_state.core_flags!=0x13579bdf || marker.before!=UINT64_C(0x13579bdf2468ace0) ||
               marker.after!=marker.before)return 1;
        }
        context.flags=0;g_RoomFloorY->y=1000;
        if(RoomEffect_SparkRingController(0,&ring,0)<=0)return 1;
        for(unsigned frame=1;frame<=52;++frame){g_FieldFxCurrentAge=frame;if(RoomEffect_SparkRingController(1,&ring,0))return 1;}
        if(ring.state!=2 || RoomEffect_SparkRingController(1,&ring,0)!=2)return 1;
        if(RoomEffect_BurstSparkSpawner(0,&count,0)<=0)return 1;
        for(unsigned frame=1;frame<=8;++frame){g_FieldFxCurrentAge=frame;int result=RoomEffect_BurstSparkSpawner(1,&count,0);if(result!=(frame==8?2:0))return 1;}
        if(count!=3)return 1;
        RoomSparkRingParticle particle={0};particle.timer=23;
        if(RoomEffect_SparkRingParticle(1,&particle,0)!=1)return 1;
        const int states[]={1,2,3,4},limits[]={6,16,12,16};
        for(unsigned i=0;i<4;++i){RoomBurstSpark spark={0};spark.state=states[i];spark.timer=limits[i]-1;
            if(RoomEffect_BurstSparkParticle(1,&spark,0)!=1)return 1;}
        g_PlayerEntity=NULL;g_FieldFxOwner=NULL;g_FieldFxTaskContext=NULL;g_FieldFxActiveTaskSlot=NULL;
        puts("Original Museum spark modules: all ten rooms, complete hashes/scripts/palette, descriptors, reloads and native controllers passed");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m273-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe,archive;SceneRoomSectorRange range;u8 module[0xc000];
        if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return fprintf(stderr,"M273 load audit failed at %d\n",__LINE__),1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return fprintf(stderr,"M273 load audit failed at %d\n",__LINE__),1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        if(!pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive))return fprintf(stderr,"M273 load audit failed at %d\n",__LINE__),1;
        if(!pe_disc_read(&disc,&exe,0x800+(0x80093378-0x80010000)+272*8,&range,8))return fprintf(stderr,"M273 load audit failed at %d\n",__LINE__),1;
        u32 offset=(range.start+range.scratchSectors+range.textureSectors)*2048u;
        if(!pe_disc_read(&disc,&archive,offset,module,sizeof(module)))return fprintf(stderr,"M273 load audit failed at %d\n",__LINE__),1;
        const unsigned slots[]={82,75,184,73,185,183,81,186,167,72};
        FieldFxTaskProgram *programs[]={&g_RoomM273GrabPlacementEffects,&g_RoomM273GrabEffects,&g_RoomM273HandTrailEffects,&g_RoomM273SweepEffects,&g_RoomM273SwayEffects};
        const u32 programs_ps1[]={0x8019ab94,0x8019ac40,0x8019ace4,0x8019ad98,0x8019ae24};
        const unsigned script_bytes[]={32,48,44,56,48};
        for(unsigned pass=0;pass<2;++pass){
            for(unsigned i=0;i<10;++i)if(slots[i]<85)g_PmCmdHandlerTable[slots[i]]=NULL;else g_FieldFxTaskPrograms[slots[i]-85]=NULL;
            memset(&g_RoomM273HandTrailState,0xa5,sizeof(g_RoomM273HandTrailState));
            memset(&g_RoomM273SweepState,0xa5,sizeof(g_RoomM273SweepState));
            memset(&g_RoomM273SwayStorage,0xa5,sizeof(g_RoomM273SwayStorage));
            int loaded=Scene_LoadRoom(Text_ParseBase32Id((signed char *)"m0273i"));
            fprintf(stderr,"M273 load=%d slots82=%d 81=%d 75=%d 73=%d 72=%d effects=%d,%d,%d,%d,%d\n",loaded,
                g_PmCmdHandlerTable[82]==&g_RoomM273OscillationClass,g_PmCmdHandlerTable[81]==&g_RoomM273GrabClass,
                g_PmCmdHandlerTable[75]==&g_RoomActorClassTable[2],g_PmCmdHandlerTable[73]==&g_RoomActorClassTable[4],
                g_PmCmdHandlerTable[72]==&g_RoomActorClassTable[3],g_FieldFxTaskPrograms[82]==programs[0],
                g_FieldFxTaskPrograms[98]==programs[1],g_FieldFxTaskPrograms[99]==programs[2],g_FieldFxTaskPrograms[100]==programs[3],g_FieldFxTaskPrograms[101]==programs[4]);
            if(loaded ||
               g_PmCmdHandlerTable[82]!=&g_RoomM273OscillationClass || g_PmCmdHandlerTable[81]!=&g_RoomM273GrabClass ||
               g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2] || g_PmCmdHandlerTable[73]!=&g_RoomActorClassTable[4] ||
               g_PmCmdHandlerTable[72]!=&g_RoomActorClassTable[3] || g_FieldFxTaskPrograms[82]!=programs[0] ||
               g_FieldFxTaskPrograms[98]!=programs[1] || g_FieldFxTaskPrograms[99]!=programs[2] ||
               g_FieldFxTaskPrograms[100]!=programs[3] || g_FieldFxTaskPrograms[101]!=programs[4])return fprintf(stderr,"M273 load audit failed at %d\n",__LINE__),1;
            RoomM273BossState hand={0};RoomM273DropQueue sweep={0};RoomM273SwayStorage sway={0};
            if(memcmp(&hand,&g_RoomM273HandTrailState,sizeof(hand)) || memcmp(&sweep,&g_RoomM273SweepState,sizeof(sweep)) || memcmp(&sway,&g_RoomM273SwayStorage,sizeof(sway)))return fprintf(stderr,"M273 load audit failed at %d\n",__LINE__),1;
            for(unsigned i=0;i<5;++i){
                u32 script;memcpy(&script,module+(programs_ps1[i]-0x8018efe8)+52,4);
                if(memcmp(programs[i]->script,module+(script-0x8018efe8),script_bytes[i]))return fprintf(stderr,"M273 load audit failed at %d\n",__LINE__),1;
            }
            for(unsigned angle=0;angle<4096;++angle){
                u32 sine; if(!pe_disc_read(&disc,&exe,0x800+(0x800966ec-0x80010000)+angle*4,&sine,4))return fprintf(stderr,"M273 load audit failed at %d\n",__LINE__),1;
                if(g_RoomM273Trig[angle].word!=(s32)sine)return fprintf(stderr,"M273 load audit failed at %d\n",__LINE__),1;
            }
        }
        /* Invoke through the actual PM method types: the parameters are
         * native addresses, so ASan/function also checks the dispatch ABI. */
        FieldActor boss={0},player={0};FieldActor *saved_player=g_PlayerEntity;
        g_PlayerEntity=&player;boss.pos_x=3*65536;boss.pos_z=4*65536;
        RoomOscillationEffect oscillation={0};oscillation.actor=&boss;
        s32 angle=-1,distance=-1,x=6*65536,z=8*65536,measured=-1;
        g_RoomM273OscillationClass.init();
        g_RoomM273OscillationClass.initialize((PmSlotHeader *)&oscillation);
        g_RoomM273OscillationClass.send((PmSlotHeader *)&oscillation,0,4,(intptr_t)&angle,(intptr_t)&distance,0);
        g_RoomM273OscillationClass.execute((PmSlotHeader *)&oscillation);
        g_RoomM273OscillationClass.send((PmSlotHeader *)&oscillation,0,2,(intptr_t)&x,(intptr_t)&z,(intptr_t)&measured);
        if(distance!=5 || measured!=5 || angle<0 || angle>=4096)return fprintf(stderr,"M273 native PM address operands failed\n"),1;
        g_RoomM273OscillationClass.stop((PmSlotHeader *)&oscillation);
        g_RoomM273OscillationClass.unload();
        RoomSelectionState grab={0};grab.actor=&boss;s32 signal=0;
        g_RoomM273GrabClass.init();g_RoomM273GrabClass.initialize((PmSlotHeader *)&grab);
        g_RoomM273GrabClass.send((PmSlotHeader *)&grab,1,0x19,(intptr_t)&signal,0,0);
        g_RoomM273GrabClass.send((PmSlotHeader *)&grab,0,0x1d,3,1,0);
        if(signal!=1 || grab.placement.signal!=&signal || grab.placement.phase_countdown!=3 ||
           grab.placement.pending_action!=module[0x8019a8e1-0x8018efe8])return fprintf(stderr,"M273 native grab tail failed\n"),1;
        g_RoomM273GrabClass.execute((PmSlotHeader *)&grab);g_RoomM273GrabClass.stop((PmSlotHeader *)&grab);g_RoomM273GrabClass.unload();
        if(signal!=0 || grab.status!=4)return fprintf(stderr,"M273 native grab release failed\n"),1;
        /* An event may expire while its visual task is still running. Keep
         * all four controllers live through a missing event acknowledgement. */
        FieldActorState boss_state={0};RenderMatrix matrices[45]={0};
        boss.state=&boss_state;boss.render_object.matrices=matrices;
        signal=3;grab.status=0;
        g_RoomM273GrabClass.stop((PmSlotHeader *)&grab);
        if(signal!=0 || grab.status!=4 || boss_state.action_state!=NULL)
            return fprintf(stderr,"M273 expired grab release failed\n"),1;
        u8 grab_event=2;boss_state.action_state=&grab_event;signal=3;grab.status=0;
        g_RoomM273GrabClass.stop((PmSlotHeader *)&grab);
        if(signal!=0 || grab.status!=4 || grab_event!=4)
            return fprintf(stderr,"M273 live grab release acknowledgement failed\n"),1;
        boss_state.action_state=NULL;
        FieldFxObjectPrefix owner={0};owner.actor=&boss;
        FieldFxObjectPrefix *saved_owner=g_FieldFxOwner;g_FieldFxOwner=&owner;
        FieldFxTaskProgram *controlled[]={&g_RoomM273GrabEffects,&g_RoomM273HandTrailEffects,&g_RoomM273SweepEffects,&g_RoomM273SwayEffects};
        for(unsigned i=0;i<4;++i){
            if(controlled[i]->table.callbacks[7](0,NULL,0)<0 ||
               controlled[i]->table.callbacks[7](1,NULL,0)!=0 || boss_state.action_state!=NULL)
                return fprintf(stderr,"M273 expired event controller %u failed\n",i),1;
        }
        g_FieldFxOwner=saved_owner;
        g_PlayerEntity=saved_player;
        puts("Original M273 complete descriptor set, authored task scripts, sine table and full work-area reload passed");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m245-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe,archive;SceneRoomSectorRange range;u8 module[27556];
        if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        if(!pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive))return 1;
        const unsigned rooms[]={245,397};
        for(unsigned r=0;r<2;++r){
            if(!pe_disc_read(&disc,&exe,0x800+(0x80093378-0x80010000)+(rooms[r]-1)*8,&range,8))return 1;
            u32 offset=(range.start+range.scratchSectors+range.textureSectors)*2048u;
            if(!pe_disc_read(&disc,&archive,offset+8,module,sizeof(module)))return 1;
            uint64_t hash=UINT64_C(14695981039346656037);
            for(unsigned i=0;i<sizeof(module);++i)hash=(hash^module[i])*UINT64_C(1099511628211);
            if(hash!=UINT64_C(0x727e58a0299805cf) ||
               memcmp(module+0x66bc,g_RoomSeekingTrailProgram.script,36) ||
               memcmp(module+0x6768,g_RoomScatterTrailProgram.script,60))return 1;
            for(unsigned pass=0;pass<2;++pass){
                char name[8];snprintf(name,sizeof(name),"m%04ui",rooms[r]);
                g_PmCmdHandlerTable[70]=g_PmCmdHandlerTable[75]=NULL;
                g_FieldFxTaskPrograms[55]=g_FieldFxTaskPrograms[56]=NULL;
                memset(g_RoomSeekingTrailHistory,0xa5,sizeof(g_RoomSeekingTrailHistory));
                memset(g_RoomScatterTrailHistory,0xa5,sizeof(g_RoomScatterTrailHistory));
                memset(g_RoomModelBurstLayer,0xa5,sizeof(g_RoomModelBurstLayer));
                memset(g_RoomScatterBurstLayer,0xa5,sizeof(g_RoomScatterBurstLayer));
                memset(&g_RoomModelBurstAnchor,0xa5,sizeof(g_RoomModelBurstAnchor));
                memset(&g_RoomScatterBurstAnchor,0xa5,sizeof(g_RoomScatterBurstAnchor));
                g_RoomModelBurstAsset=g_RoomScatterBurstAsset=module;
                RenderColor sampled;FieldFx_SampleColorTrack(g_RoomModelBurstColors,&sampled,19);
                FieldFx_SampleColorTrack(g_RoomScatterBurstColors,&sampled,31);
                if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)name)) ||
                   g_PmCmdHandlerTable[70]!=&g_RoomActorClassTable[0] || g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2] ||
                   g_FieldFxTaskPrograms[55]!=&g_RoomSeekingTrailProgram || g_FieldFxTaskPrograms[56]!=&g_RoomScatterTrailProgram ||
                   g_RoomModelBurstAsset || g_RoomScatterBurstAsset ||
                   memcmp(module+0x6708,g_RoomModelBurstColors,40) || memcmp(module+0x67cc,g_RoomScatterBurstColors,40))return 1;
                const struct {void *p;size_t n;} zero[]={
                    {g_RoomSeekingTrailHistory,sizeof(g_RoomSeekingTrailHistory)}, {g_RoomScatterTrailHistory,sizeof(g_RoomScatterTrailHistory)},
                    {g_RoomModelBurstLayer,sizeof(g_RoomModelBurstLayer)}, {g_RoomScatterBurstLayer,sizeof(g_RoomScatterBurstLayer)},
                    {&g_RoomModelBurstAnchor,sizeof(g_RoomModelBurstAnchor)}, {&g_RoomScatterBurstAnchor,sizeof(g_RoomScatterBurstAnchor)}};
                for(unsigned i=0;i<6;++i)for(size_t j=0;j<zero[i].n;++j)if(((u8 *)zero[i].p)[j])return 1;
                if(g_RoomSeekingTrailProgram.initialize(0,1,2,3)!=(intptr_t)g_RoomSeekingTrailHistory ||
                   g_RoomScatterTrailProgram.initialize(0,3,2,1)!=(intptr_t)g_RoomSeekingTrailHistory)return 1;
            }
        }
        FieldActor actor={0},player={0};FieldActorState actor_state={0};Combatant player_state={0};RenderMatrix transforms[18]={0};
        struct {u64 before;u8 value;u8 padding[7];u64 after;} marker={UINT64_C(0x1234567887654321),1,{0},UINT64_C(0x1234567887654321)};
        FieldFxObjectPrefix owner={0};FieldFxTaskContext context={0};FieldFxTaskSlot task={0};_Alignas(void *) char arena[8192];
        actor.state=&actor_state;actor.render_object.matrices=transforms;actor_state.action_state=&marker.value;
        player.state=(FieldActorState *)&player_state;player.render_object.matrices=transforms;owner.actor=&actor;
        g_FieldFxOwner=&owner;g_FieldFxTaskContext=&context;g_FieldFxActiveTaskSlot=&task;task.end=arena;g_PlayerEntity=&player;
        int (*controllers[2])(int,void *,intptr_t)={RoomEffect_SeekingTrailController,RoomEffect_ScatterTrailController};
        int (*particles[2])(int,void *,intptr_t)={RoomEffect_SeekingTrailParticle,RoomEffect_ScatterTrailParticle};
        for(unsigned kind=0;kind<2;++kind){
            u16 slot=99;marker.value=1;context.flags=1;context.variables[0]=1;actor_state.core_flags=0x01000000;player_state.stateFlags=0;
            if(controllers[kind](0,&slot,0)<=0 || slot || marker.value!=2)return 1;
            g_FieldFxCurrentAge=1;if(controllers[kind](1,&slot,0))return 1;
            g_FieldFxCurrentAge=8;if(controllers[kind](1,&slot,0)!=2)return 1;
            RoomSeekingTrail trail={0};if(particles[kind](1,&trail,0) || trail.state!=1 || trail.slot!=(kind?1:0))return 1;
            trail.fade=1;if(particles[kind](1,&trail,0) || !(player_state.stateFlags&0x4000) || (actor_state.core_flags&0xff000000)!=0xbf000000)return 1;
            if(marker.before!=UINT64_C(0x1234567887654321) || marker.after!=marker.before)return 1;
        }
        int (*bursts[2])(int,void *,intptr_t)={RoomEffect_ModelBurstController,RoomEffect_ScatterBurstController};
        int (*sprites[2])(int,void *,intptr_t)={RoomEffect_ModelBurstParticle,RoomEffect_ScatterBurstParticle};
        for(unsigned kind=0;kind<2;++kind){
            s16 state=123;if(bursts[kind](0,&state,0)<=0 || state)return 1;
            for(unsigned age=1;age<=48;++age){g_FieldFxCurrentAge=age;if(bursts[kind](1,&state,0)!=(age==48?1:0))return 1;}
            RoomModelBurstParticle sprite={0};sprite.vx=(u16)-4;sprite.vy=9;
            for(unsigned age=1;age<=16;++age)if(sprites[kind](1,&sprite,0)!=(age==16?1:0))return 1;
            if(sprite.x!=(u16)-64 || sprite.y!=144 || sprite.z!=384)return 1;
        }
        g_FieldFxSpriteParams.palette=3;u16 pixels[16],saved[16],rotated[16];
        for(unsigned i=0;i<16;++i)pixels[i]=0x4000+i;
        RECT rect={48,g_FieldFxPaletteTable[3],16,1};LoadImage(&rect,pixels);DrawSync(0);
        FieldFx_LoadRotatedClutRow(3,-1,saved);DrawSync(0);if(memcmp(saved,pixels,sizeof(pixels)))return 1;
        FieldFx_LoadRotatedClutRow(3,1,saved);DrawSync(0);StoreImage(&rect,rotated);DrawSync(0);
        if(rotated[0]!=pixels[0])return 1;
        for(unsigned i=1;i<16;++i)if(rotated[i%15+1]!=pixels[i])return 1;
        g_FieldFxOwner=NULL;g_FieldFxTaskContext=NULL;g_FieldFxActiveTaskSlot=NULL;g_PlayerEntity=NULL;
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M245/M397 complete module, authored scripts/colors, native bindings/reloads, both trail hits, all controller lifetimes and CLUT rotation passed");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m205-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe,archive;SceneRoomSectorRange range;u8 module[15704];
        if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        if(!pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive))return 1;
        const unsigned rooms[]={205,243,406,410};
        for(unsigned room_index=0;room_index<sizeof(rooms)/sizeof(rooms[0]);++room_index){
        unsigned room_number=rooms[room_index];char room_name[8];
        snprintf(room_name,sizeof(room_name),"m%04ui",room_number);
        if(!pe_disc_read(&disc,&exe,0x800+(0x80093378-0x80010000)+(room_number-1)*8,&range,8))return 1;
        u32 offset=(range.start+range.scratchSectors+range.textureSectors)*2048u;
        if(!pe_disc_read(&disc,&archive,offset+8,module,sizeof(module)))return 1;
        uint64_t hash=UINT64_C(14695981039346656037);
        for(unsigned i=0;i<sizeof(module);++i)hash=(hash^module[i])*UINT64_C(1099511628211);
        if(hash!=UINT64_C(0x16c6af58d5917c29))return 1;
        if(memcmp(module+(0x80192ce0-0x8018eff0),g_RoomSoundBurstProgram.script,36) ||
            g_RoomSoundBurstProgram.table.sizes[0]!=16)return 1;
        for(unsigned pass=0;pass<2;++pass){
            g_PmCmdHandlerTable[70]=NULL;g_PmCmdHandlerTable[73]=NULL;g_PmCmdHandlerTable[75]=NULL;g_FieldFxTaskPrograms[31]=NULL;
            g_RoomScriptArgs[0]=1;g_RoomScriptArgs[1]=2;g_RoomScriptArgs[2]=3;
            if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)room_name)) ||
               g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2] ||
               g_FieldFxTaskPrograms[31]!=&g_RoomSoundBurstProgram ||
               g_RoomScriptArgs[0] || g_RoomScriptArgs[1] || g_RoomScriptArgs[2])return 1;
            if((room_number==406 || room_number==410) && g_PmCmdHandlerTable[70]!=&g_RoomActorClassTable[0])return 1;
            if(room_number==410 && g_PmCmdHandlerTable[73]!=&g_RoomActorClassTable[4])return 1;
            intptr_t args=g_RoomSoundBurstProgram.initialize(0,3,-4,5);
            if(args!=(intptr_t)g_RoomScriptArgs || g_RoomScriptArgs[0]!=3 ||
               g_RoomScriptArgs[1]!=-4 || g_RoomScriptArgs[2]!=5)return 1;
        }
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        /* Retail writes through state+0x18, not the low byte of core_flags.
         * Separate native pointers and guards catch that distinction. */
        FieldActor actor={0};FieldActorState actor_state={0};RenderMatrix transforms[10]={0};
        struct {u64 before;u8 value;u8 padding[7];u64 after;} marker={0};
        FieldFxObjectPrefix owner={0};FieldFxTaskContext context={0};FieldFxTaskSlot task={0};
        _Alignas(void *) u8 arena[4096];RoomSoundBurstState burst={0};
        marker.before=marker.after=UINT64_C(0x13579bdf2468ace0);
        actor.state=&actor_state;actor.render_object.matrices=transforms;
        actor_state.action_state=&marker.value;owner.actor=&actor;task.end=(char *)arena;
        g_FieldFxOwner=&owner;g_FieldFxTaskContext=&context;g_FieldFxActiveTaskSlot=&task;
        g_FieldFxCurrentAge=0;burst.soundHandle=-1;
        const u8 initial[]={0,1,2,255};
        for(unsigned enabled=0;enabled<2;++enabled)for(unsigned i=0;i<4;++i){
            context.flags=enabled;marker.value=initial[i];actor_state.core_flags=0x13579bdf;
            if(FieldFx_InitCallbackList((char *)arena,sizeof(RoomSoundBurstParticle),24,RoomEffect_SoundBurstParticle)<=0 ||
               g_RoomSoundBurstProgram.table.callbacks[0](1,&burst,0)!=0 ||
               marker.value!=(enabled && initial[i]==1?2:initial[i]) ||
               actor_state.core_flags!=0x13579bdf || marker.before!=UINT64_C(0x13579bdf2468ace0) ||
               marker.after!=marker.before)return 1;
        }
        g_FieldFxOwner=NULL;g_FieldFxTaskContext=NULL;g_FieldFxActiveTaskSlot=NULL;
        puts("Original M205/M243/M406/M410 complete modules, descriptors, script, reloads and guarded native action-state pointer passed");return 0;
    }
    if(argc==2 && (!strcmp(argv[1],"--m269-load") || !strcmp(argv[1],"--m411-load"))){
        const unsigned room=!strcmp(argv[1],"--m411-load")?411:269;
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe,archive;SceneRoomSectorRange range;u8 module[13140];
        if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        if(!pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive) ||
            !pe_disc_read(&disc,&exe,0x800+(0x80093378-0x80010000)+(room-1)*8,&range,8))return 1;
        u32 offset=(range.start+range.scratchSectors+range.textureSectors)*2048u;
        if(!pe_disc_read(&disc,&archive,offset+8,module,sizeof(module)))return 1;
        uint64_t hash=UINT64_C(14695981039346656037);
        for(unsigned i=0;i<sizeof(module);++i)hash=(hash^module[i])*UINT64_C(1099511628211);
        if(hash!=UINT64_C(0xf77cd2091472d470))return 1;
        const unsigned slots[]={77,137,138,139},addresses[]={0x801921c8,0x80192228,0x801922a8,0x801922f8};
        const u32 methods[]={0x8018f058,0x8018f060,0x8018f084,0x8018f130,0x8018f138,0x8018f6bc,0x8018f7f8};
        if(memcmp(module+(addresses[0]-0x8018eff0),methods,sizeof(methods)) ||
            memcmp(module+(0x80192280-0x8018eff0),g_RoomGrabTrailTrack,40))return 1;
        FieldFxTaskProgram *programs[]={&g_RoomGroundSweepProgram,&g_RoomOrbitTrailProgram,&g_RoomRisingSprayProgram};
        for(unsigned i=0;i<3;++i){
            u32 pc;memcpy(&pc,module+(addresses[i+1]-0x8018eff0+52),4);
            if(memcmp(module+(pc-0x8018eff0),programs[i]->script,i==2?24:32))return 1;
        }
        for(unsigned pass=0;pass<2;++pass){
            g_PmCmdHandlerTable[77]=NULL;
            for(unsigned i=0;i<3;++i)g_FieldFxTaskPrograms[52+i]=NULL;
            g_RoomGrabSweepSide=123;g_RoomGrabSlot=456;RoomLib_PairA=789;RoomLib_PairB=321;
            memset(&g_RoomGrabOrbitCentre,0xa5,sizeof(g_RoomGrabOrbitCentre));
            memset(g_RoomGrabTrailTrack,0xa5,8);
            for(unsigned i=12;i<40;i+=8)memset(g_RoomGrabTrailTrack+i,0xa5,4);
            char name[16];snprintf(name,sizeof(name),"m%04ui",room);
            if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)name)) ||
                g_PmCmdHandlerTable[77]!=&g_RoomGrabHandlers || g_RoomGrabSweepSide || g_RoomGrabSlot ||
                RoomLib_PairA || RoomLib_PairB || g_RoomGrabOrbitCentre.x || g_RoomGrabOrbitCentre.y || g_RoomGrabOrbitCentre.z ||
                memcmp(module+(0x80192280-0x8018eff0),g_RoomGrabTrailTrack,40))return 1;
            for(unsigned i=0;i<3;++i)if(g_FieldFxTaskPrograms[52+i]!=programs[i])return 1;
            struct {u64 before;RoomMotionTrigger object;u64 after;} guarded={0};
            guarded.before=guarded.after=UINT64_C(0x13579bdf2468ace0);
            RoomMotionTrigger *o=&guarded.object;FieldActor owner={0},source={0};s32 completion=-1;
            o->probe_actor=&owner;source.type_id=30;source.sub_id=4;g_FieldActorListHead=&source;
            g_RoomGrabHandlers.initialize((PmSlotHeader *)o);
            if(o->flag03!=255 || o->flag1A || o->activated || o->callback!=RoomEffect_GrabAwaitTarget)return 1;
            g_RoomGrabHandlers.send((PmSlotHeader *)o,1,0x19,(intptr_t)&completion,0,0);
            g_RoomGrabHandlers.send((PmSlotHeader *)o,0,0x200,30,4,0);
            if(completion!=1 || o->completion_state!=&completion || o->source_actor!=&source)return 1;
            owner.mode=0;g_RoomGrabHandlers.execute((PmSlotHeader *)o);
            if(o->callback!=RoomEffect_GrabAwaitTarget)return 1;
            owner.mode=0x10;g_RoomGrabHandlers.execute((PmSlotHeader *)o);
            if(o->callback!=RoomEffect_GrabStartOnProximity)return 1;
            FieldActor *old_player=g_PlayerEntity, player={0};g_PlayerEntity=&player;player.mode=0x12;
            g_RoomGrabHandlers.execute((PmSlotHeader *)o);g_PlayerEntity=old_player;
            if(o->state!=4 || completion || guarded.before!=UINT64_C(0x13579bdf2468ace0) || guarded.after!=guarded.before)return 1;
            g_FieldActorListHead=NULL;
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        printf("Original M%03u: all four real CUE descriptors, authored scripts and track, reload reset, native completion pointer and callback lifecycle guards passed\n",room);return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m186-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        PeDiscFile exe,archive;if(!pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive))return 1;
        const unsigned rooms[]={186,385,388,389};
        for(unsigned room=0;room<4;++room){
            u8 module[0x53bc];
            if(!original_chrysler_read(&disc,&exe,&archive,rooms[room],module,sizeof(module)))return 1;
            uint64_t hash=UINT64_C(14695981039346656037);
            for(unsigned i=0;i<sizeof(module);++i)hash=(hash^module[i])*UINT64_C(1099511628211);
            if(hash!=UINT64_C(0xbff23bf6f78c6128) ||
                memcmp(module+(0x80194230u-0x8018eff0u),g_RoomOrbitSetScript,29*4) ||
                memcmp(module+(0x80194180u-0x8018eff0u),RoomLib_TwelveEffectTable,60))return 1;
            int twelve_script[4];memcpy(twelve_script,module+(0x801941bcu-0x8018eff0u),sizeof(twelve_script));
            for(unsigned pass=0;pass<2;++pass){
                RoomFxSpritePacket *packets[]={&g_RoomOrbitSpritePacket,&g_RoomOrbitParticlePacket,
                    &g_RoomOrbitBurstPacket,&g_RoomOrbitShadowPacket,
                    &RoomLib_TwelveEffectPrimaryPacket,&RoomLib_TwelveEffectSecondaryPacket};
                for(unsigned i=0;i<6;++i)memset(packets[i],0xa5,sizeof(*packets[i]));
                g_PmCmdHandlerTable[36]=g_PmCmdHandlerTable[11]=g_PmCmdHandlerTable[70]=g_PmCmdHandlerTable[75]=NULL;
                char name[16];snprintf(name,sizeof(name),"m%04ui",rooms[room]);
                if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)name)) ||
                    g_PmCmdHandlerTable[36]!=&g_RoomOrbitSetHandlers || g_PmCmdHandlerTable[11]!=&g_RoomTwelveElementHandlers ||
                    g_PmCmdHandlerTable[70]!=&g_RoomActorClassTable[0] || g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2])return 1;
                for(unsigned i=0;i<6;++i)for(unsigned j=0;j<sizeof(*packets[i]);++j)if(((u8 *)packets[i])[j])return 1;
                if(!original_orbit_set_cycles() || !original_twelve_element_cycles(twelve_script))return 1;
            }
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M186/M385/M388/M389: complete identical USA modules, all four descriptors, six-packet reset and both authored effect classes passed twice per room");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m188-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        const unsigned rooms[]={188,390};
        FieldFxTaskProgram *programs[]={&g_RoomSparkBarrageProgram124,&g_RoomSparkBarrageProgram125,&g_RoomSparkBarrageProgram127};
        const unsigned indices[]={39,40,42};
        PeDiscFile exe,archive;
        if(!pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive))return 1;
        for(unsigned room=0;room<2;++room){
            SceneRoomSectorRange range;u8 module[0x7010];
            if(!pe_disc_read(&disc,&exe,0x800ull+0x80093378u-0x80010000u+(rooms[room]-1)*8,&range,8) ||
               !pe_disc_read(&disc,&archive,(u64)(range.start+range.scratchSectors+range.textureSectors)*2048,module,sizeof(module)))return 1;
            uint64_t hash=UINT64_C(14695981039346656037);
            for(unsigned i=8;i<sizeof(module);++i)hash=(hash^module[i])*UINT64_C(1099511628211);
            if(hash!=UINT64_C(0xfd4854c55ef9da6b))return 1;
            const u32 script_addresses[]={0x80195e40u,0x80195ebcu,0x80195f60u};
            const unsigned script_sizes[]={36,36,24};
            for(unsigned i=0;i<3;++i)if(memcmp(module+(script_addresses[i]-0x8018efe8u),programs[i]->script,script_sizes[i]))return 1;
            const u32 track_addresses[]={0x80195e64u,0x80195ee0u,0x80195f00u,0x80195f78u};
            u8 *tracks[]={g_RoomBouncingSparkColors,g_RoomWaveSparkColors,g_RoomCometSparkColors,g_RoomLiftedSparkColors};
            const unsigned lengths[]={32,32,40,32};
            for(unsigned i=0;i<4;++i)for(unsigned offset=8;offset<lengths[i];offset+=8)
                if(memcmp(module+(track_addresses[i]-0x8018efe8u)+offset,tracks[i]+offset,4))return 1;
        }
        for(unsigned room=0;room<2;++room)for(unsigned pass=0;pass<2;++pass){
            g_RoomScriptArgA=123;g_RoomScriptArgB=456;
            memset(&g_RoomCometSparkTrail,0xa5,sizeof(g_RoomCometSparkTrail));
            u8 *tracks[]={g_RoomBouncingSparkColors,g_RoomWaveSparkColors,g_RoomCometSparkColors,g_RoomLiftedSparkColors};
            const unsigned lengths[]={32,32,40,32};
            for(unsigned i=0;i<4;++i){memset(tracks[i],0xa5,8);for(unsigned j=12;j+4<=lengths[i];j+=8)memset(tracks[i]+j,0xa5,4);}
            for(unsigned i=0;i<3;++i)g_FieldFxTaskPrograms[indices[i]]=NULL;
            char name[7];snprintf(name,sizeof(name),"m%04ui",rooms[room]);
            if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)name)))return 1;
            for(unsigned i=0;i<3;++i)if(g_FieldFxTaskPrograms[indices[i]]!=programs[i])return 1;
            if(g_PmCmdHandlerTable[70]!=&g_RoomActorClassTable[0] || g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2] || g_PmCmdHandlerTable[73]!=&g_RoomActorClassTable[4] || g_RoomScriptArgA || g_RoomScriptArgB)return 1;
            for(unsigned i=0;i<sizeof(g_RoomCometSparkTrail);++i)if(((u8 *)&g_RoomCometSparkTrail)[i])return 1;
            for(unsigned i=0;i<4;++i){for(unsigned j=0;j<8;++j)if(tracks[i][j])return 1;for(unsigned j=12;j+4<=lengths[i];j+=8)for(unsigned k=0;k<4;++k)if(tracks[i][j+k])return 1;}
            if(!original_spark_barrage_cycles())return 1;
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M188/M390 loader: all six descriptors, per-entry reset and authored task cycles passed twice per room");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m187-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        PeDiscFile exe,archive;SceneRoomSectorRange range;u8 module[0x10d4];
        if(!pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive) ||
            !pe_disc_read(&disc,&exe,0x800ull+0x80093378u-0x80010000u+186*8,&range,8) ||
            !pe_disc_read(&disc,&archive,(u64)(range.start+range.scratchSectors+range.textureSectors)*2048,module,sizeof(module)))return 1;
        uint64_t hash=UINT64_C(14695981039346656037);
        for(unsigned i=8;i<sizeof(module);++i)hash=(hash^module[i])*UINT64_C(1099511628211);
        if(hash!=UINT64_C(0x20d34cc7504f29a2) || memcmp(module+(0x8019000cu-0x8018efe8u),g_RoomOrbitSetScript,29*4) ||
            g_RoomOrbitSetSpawnLayout[0]!=sizeof(RoomOrbitSetState) || g_RoomOrbitSetSpawnLayout[1]!=140 ||
            g_RoomOrbitSetSpawnLayout[2]!=218 || g_RoomOrbitSetSpawnLayout[3]!=24)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        for(unsigned pass=0;pass<2;++pass){
            RoomFxSpritePacket *packets[]={&g_RoomOrbitSpritePacket,&g_RoomOrbitParticlePacket,
                &g_RoomOrbitBurstPacket,&g_RoomOrbitShadowPacket};
            for(unsigned i=0;i<4;++i)memset(packets[i],0xa5,sizeof(*packets[i]));
            g_PmCmdHandlerTable[36]=NULL;
            if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)"m0187i")) ||
                g_PmCmdHandlerTable[36]!=&g_RoomOrbitSetHandlers)return 1;
            for(unsigned i=0;i<4;++i)for(unsigned j=0;j<sizeof(*packets[i]);++j)
                if(((u8 *)packets[i])[j])return 1;
            if(!original_orbit_set_cycles())return 1;
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M187 loader: real USA assets, complete orbit descriptor and per-entry reset passed twice");return 0;
    }
    if(argc==2 && (!strcmp(argv[1],"--m418-cycles-draw") || !strcmp(argv[1],"--m418-slots-draw"))){
        int extra_slots=!strcmp(argv[1],"--m418-slots-draw");
        /* Controlled scripts exercise dormant callbacks; retail scripts stay untouched. */
        static int centre_script[]={0x10000,0x10001,-1};
        static int orbit_script[]={0x10000,0x10007,-1};
        const char *path=getenv("PE_TEST_DISC2");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!SDL_Init(SDL_INIT_EVENTS)||!pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        /* Pm_InitScene is skipped: each cycle below installs its own guarded bank.
         * Still load the original shared and room effect TIMs before drawing. */
        if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)"m0418i"))||Scene_StreamTexturePage())return 1;DrawSync(0);
        g_GameStateFlags|=0x80;g_GameState.flags|=8;
        int texture_status=1;
        for(unsigned attempt=0;attempt<10 && texture_status>0;attempt++)
            texture_status=Asset_LoadTimTextures(1);
        if(texture_status)return 1;
        DrawSync(0);
        static RenderGpuTag ordering[4096];static u16 pixels[320*224];RECT area={0,0,320,224};DRAWENV draw;SetDefDrawEnv(&draw,0,0,320,224);draw.isbg=0;draw.dtd=0;PutDrawEnv(&draw);
        static struct {_Alignas(void *) u8 data[1048576];u8 guard[32];} packets;
        GteMatrix view={.m={{4096,0,0},{0,4096,0},{0,0,4096}}};int projection=256;
        g_GeomScroll.words.viewMatrix=&view;g_GeomScroll.words.projectionDistance=&projection;
        g_GameStateFlags=0x80;g_GameState.flags&=~8u;g_GameState.tim_load_state=0;g_RoomFloorY->y=0;
        PolygonVertex polygon[]={{0,-4000,0,-4000},{0,4000,0,-4000},{0,4000,0,4000},{0,-4000,0,4000}};g_FloorPolygonVertices=polygon;g_FloorPolygonVertexCount=4;
        FieldFx_InitTexturePageTable();Psyz_GteCtrlWrite(26,256);Psyz_GteCtrlWrite(29,1365);Psyz_GteCtrlWrite(30,1024);gte_SetGeomOffset(160,112);
        for(unsigned program=0;program<(extra_slots?2u:4u);program++)for(unsigned variant=0;variant<2;variant++){
            SceneE08_ResetModule();struct {PmPrimarySlot slots[11];u8 guard[32];} bank={0};memset(bank.guard,0xa5,32);
            FieldActor actor={0},player={0};FieldActorState state={0};Combatant player_state={0};u8 action=1;
            RenderMatrix matrices[64]={0};actor.render_object.matrices=matrices;actor.state=&state;state.action_state=&action;
            actor.type_id=6;actor.sub_id=0;state.control10.command_value=120;state.core_flags=0x01000000;
            actor.mode=11;actor.action=64;actor.pos_y=-200*65536;actor.pos_z=1024*65536;
            RenderMatrix player_matrix[64]={0};player.render_object.matrices=player_matrix;
            player.state=(void *)&player_state;player.pos_z=3000*65536;player.pos_x=-800*65536;
            player.render_object.hit_cylinder.radius=63;for(unsigned j=0;j<64;j++){for(unsigned k=0;k<3;k++)player_matrix[j].rotation[k][k]=4096;player_matrix[j].translation[0]=-800+(j%7)*8;player_matrix[j].translation[1]=-200-(int)j*4;player_matrix[j].translation[2]=3000+(j%5)*8;}
            RenderAnimationDataHeader player_animation={.frame_count=10};for(unsigned j=0;j<48;j++)g_GameState.bank_rows[0][j]=&player_animation;
            player_state.curHP=100;player_state.actionMode12=0;g_ActiveActor=&player_state;g_PlayerEntity=&player;g_FieldActorListHead=&actor;
            /* Keep every ribbon joint beyond the 500-unit depth bias. At 1024
             * the first joint was clipped before the final segment was read,
             * hiding the arm-glow stack overread from this real-asset cycle. */
            for(unsigned j=0;j<64;j++){for(unsigned k=0;k<3;k++)matrices[j].rotation[k][k]=4096;matrices[j].translation[1]=-200;matrices[j].translation[2]=3000;}
            g_PmTables=(PmTables){g_PmCmdHandlerArray,bank.slots,NULL};srand(variant+1);
            int handle=Scene_LoadRoomAssets(extra_slots?48:46+program,&actor);if(handle!=0)return 1;
            if(extra_slots)bank.slots[handle].effect.state.commands=program?orbit_script:centre_script;
            if(!extra_slots && program==3 && Pm_SendCmd(handle,0,1,variant?256:128,0,0))return 1;
            unsigned frame=0,seen=0,drawn=0,pixel_frames=0;
            for(;frame<900;frame++){
                if(!extra_slots && program==2 && frame==10 && Pm_SendCmd(handle,0,1,variant,0,0))return 1;
                ClearOTagR(ordering,4096);uintptr_t terminal=ordering[0].address;
                memset(packets.guard,0xa5,32);g_ActiveDrawSlot=frame&1;g_DrawPacketOffset=0;
                D_800B0E38.ordering[g_ActiveDrawSlot]=ordering;D_800B0E38.packets[g_ActiveDrawSlot]=(char *)packets.data;
                if(Pm_Start(handle)||Pm_Exec(handle))return 1;
                if(g_DrawPacketOffset<0||g_DrawPacketOffset>sizeof(packets.data))return 1;drawn+=g_DrawPacketOffset!=0;
                uintptr_t link=(uintptr_t)&ordering[4095];unsigned links=0;
                while(link && link!=0xffffff){if(link==terminal){RenderGpuTag *end=(void *)link;if(end->address!=0xffffff||end->length!=4)return 1;break;}int is_ot=link>=(uintptr_t)ordering&&link+sizeof(RenderGpuTag)<=(uintptr_t)ordering+sizeof(ordering)&&(link-(uintptr_t)ordering)%sizeof(RenderGpuTag)==0;
                    int is_packet=link>=(uintptr_t)packets.data&&link+sizeof(RenderGpuTag)<=(uintptr_t)packets.data+g_DrawPacketOffset&&link%_Alignof(RenderGpuTag)==0;
                    if((!is_ot&&!is_packet)||++links>4097u+(unsigned)g_DrawPacketOffset/sizeof(RenderGpuTag))return 1;link=((RenderGpuTag *)link)->address;}
                ClearImage(&area,0,0,0);DrawOTag(&ordering[4095]);DrawSync(0);
                if(frame==12||frame==24||frame==36||frame==48||frame==60||frame==80||frame==120||frame==240||frame==500||frame==800){
                    StoreImage(&area,pixels);DrawSync(0);unsigned visible=0;uint64_t hash=UINT64_C(14695981039346656037);
                    for(unsigned j=0;j<320*224;j++){visible+=(pixels[j]&0x7fff)!=0;hash=(hash^(pixels[j]&255))*UINT64_C(1099511628211);hash=(hash^(pixels[j]>>8))*UINT64_C(1099511628211);}pixel_frames+=visible!=0;
                    fprintf(stderr,"GPU program%u variant%u frame%u visible%u hash%016llx\n",program,variant,frame,visible,(unsigned long long)hash);

                }
                for(unsigned j=0;j<32;j++)if(packets.guard[j]!=0xa5)return 1;
                for(unsigned i=0;i<64;i++)if(bank.slots[handle].effect.slots[i].flag==1)seen|=1u<<bank.slots[handle].effect.slots[i].handler_id;
                if(g_AkaoMessageQueueCount<0||g_AkaoMessageQueueCount>16)return 1;g_AkaoMessageQueueCount=0;
                for(unsigned i=0;i<32;i++)if(bank.guard[i]!=0xa5)return 1;
                if(!bank.slots[handle].header.state)break;
            }
            fprintf(stderr,"E08 cycle program%u variant%u frame%u state%u seen%x action%u drawn%u\n",program,variant,frame,bank.slots[handle].header.state,seen,action,drawn);fflush(stderr);
            unsigned expected_seen=extra_slots?(program?0x81u:3u):program==0?7u:program==1?15u:program==2?(variant?0x37du:0x201u):0x3fu;
            if(seen!=expected_seen || action!=4 || !drawn || !pixel_frames)return 1;
            if(!extra_slots && program==2 && variant && !(player_state.stateFlags&0x4000)){fprintf(stderr,"E08 player flag missing: %08x\n",player_state.stateFlags);return 1;}
            if(bank.slots[handle].header.state)return 1;
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        if(extra_slots){puts("Original E08 dormant slots: four controlled cycles, original callbacks, GPU and nonblack VRAM passed");return 0;}
        puts("Original E08 rendering: eight real-asset cycles, GPU chains, packet guards, nonblack VRAM and expiry passed");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m418-cycles-update")){
        const char *path=getenv("PE_TEST_DISC2");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!SDL_Init(SDL_INIT_EVENTS)||!pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)"m0418i")))return 1;
        GteMatrix view={.m={{4096,0,0},{0,4096,0},{0,0,4096}}};int projection=256;
        g_GeomScroll.words.viewMatrix=&view;g_GeomScroll.words.projectionDistance=&projection;
        g_GameStateFlags=0x80;g_GameState.flags&=~8u;g_GameState.tim_load_state=0;g_RoomFloorY->y=0;
        PolygonVertex polygon[]={{0,-4000,0,-4000},{0,4000,0,-4000},{0,4000,0,4000},{0,-4000,0,4000}};g_FloorPolygonVertices=polygon;g_FloorPolygonVertexCount=4;
        FieldFx_InitTexturePageTable();Psyz_GteCtrlWrite(26,256);Psyz_GteCtrlWrite(29,1365);Psyz_GteCtrlWrite(30,1024);gte_SetGeomOffset(160,112);
        for(unsigned program=0;program<4;program++)for(unsigned variant=0;variant<2;variant++){
            SceneE08_ResetModule();struct {PmPrimarySlot slots[11];u8 guard[32];} bank={0};memset(bank.guard,0xa5,32);
            FieldActor actor={0},player={0};FieldActorState state={0};Combatant player_state={0};u8 action=1;
            RenderMatrix matrices[64]={0};actor.render_object.matrices=matrices;actor.state=&state;state.action_state=&action;
            actor.type_id=6;actor.sub_id=0;state.control10.command_value=120;state.core_flags=0x01000000;
            actor.mode=11;actor.action=64;actor.pos_y=-200*65536;actor.pos_z=1024*65536;
            RenderMatrix player_matrix={.rotation={{4096,0,0},{0,4096,0},{0,0,4096}}};player.render_object.matrices=&player_matrix;
            player.state=(void *)&player_state;player.pos_z=3000*65536;player.pos_x=-3000*65536;
            player.render_object.hit_cylinder.radius=63;player_matrix.translation[0]=-3000;player_matrix.translation[2]=3000;
            RenderAnimationDataHeader player_animation={.frame_count=10};for(unsigned j=0;j<48;j++)g_GameState.bank_rows[0][j]=&player_animation;
            player_state.curHP=100;player_state.actionMode12=0;g_ActiveActor=&player_state;g_PlayerEntity=&player;g_FieldActorListHead=&actor;
            for(unsigned j=0;j<64;j++){for(unsigned k=0;k<3;k++)matrices[j].rotation[k][k]=4096;matrices[j].translation[1]=-200;matrices[j].translation[2]=1024;}
            g_PmTables=(PmTables){g_PmCmdHandlerArray,bank.slots,NULL};srand(variant+1);
            int handle=Scene_LoadRoomAssets(46+program,&actor);if(handle!=0)return 1;
            if(program==3 && Pm_SendCmd(handle,0,1,variant?256:128,0,0))return 1;
            unsigned frame=0,seen=0;
            for(;frame<900;frame++){
                if(program==2 && frame==10 && Pm_SendCmd(handle,0,1,variant,0,0))return 1;
                if(Pm_Exec(handle))return 1;
                for(unsigned i=0;i<64;i++)if(bank.slots[handle].effect.slots[i].flag==1)seen|=1u<<bank.slots[handle].effect.slots[i].handler_id;
                if(g_AkaoMessageQueueCount<0||g_AkaoMessageQueueCount>16)return 1;g_AkaoMessageQueueCount=0;
                for(unsigned i=0;i<32;i++)if(bank.guard[i]!=0xa5)return 1;
                if(!bank.slots[handle].header.state)break;
            }
            fprintf(stderr,"E08 cycle program%u variant%u frame%u state%u seen%x action%u\n",program,variant,frame,bank.slots[handle].header.state,seen,action);fflush(stderr);
            unsigned expected_seen=program==0?7u:program==1?15u:program==2?(variant?0x37du:0x201u):0x3fu;
            if(seen!=expected_seen || action!=4)return 1;
            if(program==2 && variant && !(player_state.stateFlags&0x4000)){fprintf(stderr,"E08 player flag missing: %08x\n",player_state.stateFlags);return 1;}
            if(bank.slots[handle].header.state)return 1;
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original E08 scripts: eight real-asset update cycles, authored slots, expiry, owner/player flags and bank guards passed");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m418-load")){
        extern FieldGlowSprite g_SceneE08GlowRingsSprite;
        extern FieldRingGeometry g_SceneE08RingEffectRings[3];
        extern u8 g_SceneE08RingEffectSource[0x800];
        extern char *g_SceneE08GlowModel;
        const char *path=getenv("PE_TEST_DISC2");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile archive;u8 authored[0xa700];
        if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        if(!pe_disc_find(&disc,"PE.IMG",&archive) ||
           !pe_disc_read(&disc,&archive,(95562u+33u+146u)*2048u,authored,sizeof(authored)))return 1;
        const u32 descriptors[4]={0x801986ac,0x80198760,0x80198914,0x80198a48};
        const u32 methods[4][7]={
                {0x8018f240u,0x8018f248u,0x8018f274u,0x8018f2a0u,0x8018f2c8u,0x8018f338u,0x8018f3bcu},
                {0x8018fedcu,0x8018fee4u,0x8018ff10u,0x8018ff3cu,0x8018ff64u,0x8018ffd4u,0x80190058u},
                {0x801913d8u,0x801913e0u,0x8019140cu,0x80191438u,0x80191460u,0x801914d0u,0x80191554u},
                {0x80193ae4u,0x80193aecu,0x80193b18u,0x80193b44u,0x80193b6cu,0x80193bdcu,0x80193c60u}
        };
        const u32 scripts[4]={0x801986c8,0x8019877c,0x80198930,0x80198a64};
        const unsigned words[4]={12,26,35,28};
        for(unsigned i=0;i<4;i++)if(memcmp(authored+(descriptors[i]-0x8018efe8),methods[i],28))return 1;
        for(unsigned pass=0;pass<2;pass++){
            memset(&g_SceneE08GlowRingsSprite,0xa5,sizeof(g_SceneE08GlowRingsSprite));
            memset(g_SceneE08RingEffectRings,0xa5,sizeof(g_SceneE08RingEffectRings));
            memset(g_SceneE08RingEffectSource,0xa5,sizeof(g_SceneE08RingEffectSource));
            g_SceneE08GlowModel=(char *)(uintptr_t)1;
            for(unsigned i=0;i<4;i++)g_PmCmdHandlerTable[46+i]=NULL;
            if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)"m0418i")) ||
               g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2] ||
               g_PmCmdHandlerTable[70]!=&g_RoomActorClassTable[0] || g_SceneE08GlowModel)return 1;
            for(unsigned j=0;j<sizeof(g_SceneE08GlowRingsSprite);j++)if(((u8 *)&g_SceneE08GlowRingsSprite)[j])return 1;
            for(unsigned j=0;j<sizeof(g_SceneE08RingEffectRings);j++)if(((u8 *)g_SceneE08RingEffectRings)[j])return 1;
            for(unsigned j=0;j<sizeof(g_SceneE08RingEffectSource);j++)if(g_SceneE08RingEffectSource[j])return 1;
            for(unsigned i=0;i<4;i++){
                FieldFxObject object={0};PmSlotHeader *slot=(PmSlotHeader *)&object;
                FieldActor owner={0};FieldActorState core={0};u8 action=3;
                core.core_flags=0xffffffffu;core.action_state=&action;owner.state=&core;object.prefix.actor=&owner;
                PmCommand *command=&g_SceneE08Classes[i];
                if(g_PmCmdHandlerTable[46+i]!=command || !command->init || !command->initialize ||
                   !command->send || !command->start || !command->execute || !command->stop || !command->unload)return 1;
                command->init();command->initialize(slot);
                if(!object.state.commands || memcmp(object.state.commands,authored+(scripts[i]-0x8018efe8),words[i]*4))return 1;
                intptr_t value=(intptr_t)(uintptr_t)&object;
                if(command->send(slot,0,5,value,0,0) || object.state.vars[5]!=value ||
                   command->start(slot) || command->stop(slot) || slot->state!=4 ||
                   action!=4 || core.core_flags!=0xc0ffffffu)return 1;
                command->unload();
            }
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M418 loader: real Disc2 assets, all four PM classes/scripts, native method ABI and reset passed twice");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m420-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe,archive;const unsigned rooms[]={419,420};
        if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        if(!pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive))return 1;
        for(unsigned room=0;room<2;++room){
            SceneRoomSectorRange range;u8 module[25524];
            if(!pe_disc_read(&disc,&exe,0x800ull+0x80093378u-0x80010000u+(rooms[room]-1)*8,&range,8) ||
               !pe_disc_read(&disc,&archive,(u64)(range.start+range.scratchSectors+range.textureSectors)*2048+8,module,sizeof(module)))return 1;
            uint64_t hash=UINT64_C(14695981039346656037);
            for(unsigned i=0;i<sizeof(module);++i)hash=(hash^module[i])*UINT64_C(1099511628211);
            if(hash!=UINT64_C(0x71dedd7623ab6b6f) ||
               memcmp(module+(0x80194da0-0x8018eff0),g_RoomDroppedFlareProgram.script,36))return 1;
            char name[16];snprintf(name,sizeof(name),"m%04ui",rooms[room]);
            for(unsigned pass=0;pass<2;++pass){
                memset(g_RoomDroppedFlareArgs,0xa5,sizeof(g_RoomDroppedFlareArgs));
                memset(&g_RoomMotionGlyph,0xa5,sizeof(g_RoomMotionGlyph));memset(g_RoomMotionGlyphs,0xa5,sizeof(g_RoomMotionGlyphs));
                memset(g_RoomMotionTrailA__motion,0xa5,sizeof(g_RoomMotionTrailA__motion));memset(g_RoomMotionTrailB,0xa5,sizeof(g_RoomMotionTrailB));
                g_PmCmdHandlerTable[12]=g_PmCmdHandlerTable[75]=NULL;g_FieldFxTaskPrograms[37]=NULL;
                if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)name)) ||
                   g_PmCmdHandlerTable[12]!=&g_RoomMotionHandlers || g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2] ||
                   g_FieldFxTaskPrograms[37]!=&g_RoomDroppedFlareProgram)return 1;
#define M420_ZERO(value) do {for(size_t z=0;z<sizeof(value);++z)if(((u8 *)&(value))[z])return 1;}while(0)
                M420_ZERO(g_RoomDroppedFlareArgs);M420_ZERO(g_RoomMotionGlyph);M420_ZERO(g_RoomMotionGlyphs);
                M420_ZERO(g_RoomMotionTrailA__motion);M420_ZERO(g_RoomMotionTrailB);
#undef M420_ZERO
                FieldFxObject object={0};g_RoomMotionHandlers.initialize((PmSlotHeader *)&object);
                if(!object.state.commands || memcmp(object.state.commands,module+(0x80194c78-0x8018eff0),120))return 1;
            }
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M419/M420: complete fingerprints, authored scripts, three descriptor banks and repeated motion/flare reset passed");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m424-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe,archive;const unsigned rooms[]={424};
        const uint64_t hashes[]={UINT64_C(0x4a95de649799bd0a)};
        if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        if(!pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive))return 1;
        for(unsigned room=0;room<1;++room){
            SceneRoomSectorRange range;u8 module[7856];
            if(!pe_disc_read(&disc,&exe,0x800ull+0x80093378u-0x80010000u+(rooms[room]-1)*8,&range,8) ||
               !pe_disc_read(&disc,&archive,(u64)(range.start+range.scratchSectors+range.textureSectors)*2048+8,module,sizeof(module)))return 1;
            uint64_t hash=UINT64_C(14695981039346656037);
            for(unsigned i=0;i<sizeof(module);++i)hash=(hash^module[i])*UINT64_C(1099511628211);
            if(hash!=hashes[room] ||
               memcmp(module+(0x80190d88-0x8018eff0),g_RoomScript__paired,sizeof(g_RoomScript__paired)) ||
               memcmp(module+(0x80190df0-0x8018eff0),g_RoomPairedGlowColorTable__paired,24) ||
               memcmp(module+(0x80190e08-0x8018eff0),g_RoomOrbitFlareEffects.script,60))return 1;
            char name[16];snprintf(name,sizeof(name),"m%04ui",rooms[room]);
            for(unsigned pass=0;pass<2;++pass){
                memset(g_RoomScriptArgs,0xa5,sizeof(g_RoomScriptArgs));
                memset(&g_RoomPairedGlowSprite,0xa5,sizeof(g_RoomPairedGlowSprite));g_RoomPairedGlowPacket__paired=(void *)(uintptr_t)1;
                g_PmCmdHandlerTable[35]=NULL;g_FieldFxTaskPrograms[30]=NULL;
                if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)name)) ||
                   g_PmCmdHandlerTable[35]!=&g_RoomPairedGlowHandlers ||
                   g_FieldFxTaskPrograms[30]!=&g_RoomOrbitFlareEffects ||
                   g_RoomPairedGlowPacket__paired)return 1;
#define M424_ZERO(value) do {for(size_t z=0;z<sizeof(value);++z)if(((u8 *)&(value))[z])return 1;}while(0)
                M424_ZERO(g_RoomScriptArgs);M424_ZERO(g_RoomPairedGlowSprite);
#undef M424_ZERO
            }
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M424: complete fingerprint, all scripts and colors, both descriptor banks and repeated reset passed");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m422-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe,archive;const unsigned rooms[]={421,422,423};
        const uint64_t hashes[]={UINT64_C(0x1559a982b8ab47ed),UINT64_C(0x9599ca34b6e3b84d),UINT64_C(0xfb51213a1d82d780)};
        if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        if(!pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive))return 1;
        for(unsigned room=0;room<3;++room){
            SceneRoomSectorRange range;u8 module[11256];
            if(!pe_disc_read(&disc,&exe,0x800ull+0x80093378u-0x80010000u+(rooms[room]-1)*8,&range,8) ||
               !pe_disc_read(&disc,&archive,(u64)(range.start+range.scratchSectors+range.textureSectors)*2048+8,module,sizeof(module)))return 1;
            uint64_t hash=UINT64_C(14695981039346656037);
            for(unsigned i=0;i<sizeof(module);++i)hash=(hash^module[i])*UINT64_C(1099511628211);
            if(hash!=hashes[room] ||
               memcmp(module+(0x80191a70-0x8018eff0),g_RoomScript__paired,sizeof(g_RoomScript__paired)) ||
               memcmp(module+(0x80191ad8-0x8018eff0),g_RoomPairedGlowColorTable__paired,24) ||
               memcmp(module+(0x80191af0-0x8018eff0),g_RoomOrbitFlareEffects.script,60) ||
               memcmp(module+(0x80191b64-0x8018eff0),g_RoomDroppedFlareProgram.script,36))return 1;
            char name[16];snprintf(name,sizeof(name),"m%04ui",rooms[room]);
            for(unsigned pass=0;pass<2;++pass){
                memset(g_RoomScriptArgs,0xa5,sizeof(g_RoomScriptArgs));
                memset(g_RoomDroppedFlareArgs,0xa5,sizeof(g_RoomDroppedFlareArgs));
                memset(&g_RoomPairedGlowSprite,0xa5,sizeof(g_RoomPairedGlowSprite));g_RoomPairedGlowPacket__paired=(void *)(uintptr_t)1;
                g_PmCmdHandlerTable[35]=NULL;g_FieldFxTaskPrograms[30]=g_FieldFxTaskPrograms[37]=NULL;
                if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)name)) ||
                   g_PmCmdHandlerTable[35]!=&g_RoomPairedGlowHandlers ||
                   g_FieldFxTaskPrograms[30]!=&g_RoomOrbitFlareEffects ||
                   g_FieldFxTaskPrograms[37]!=&g_RoomDroppedFlareProgram || g_RoomPairedGlowPacket__paired)return 1;
#define M422_ZERO(value) do {for(size_t z=0;z<sizeof(value);++z)if(((u8 *)&(value))[z])return 1;}while(0)
                M422_ZERO(g_RoomScriptArgs);M422_ZERO(g_RoomDroppedFlareArgs);M422_ZERO(g_RoomPairedGlowSprite);
#undef M422_ZERO
            }
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M421/M422/M423: complete fingerprints, all scripts and colors, three descriptor banks and repeated reset passed");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m412-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe,archive;SceneRoomSectorRange range;u8 module[26436];
        if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        if(!pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive) ||
           !pe_disc_read(&disc,&exe,0x800+(0x80093378-0x80010000)+411*8,&range,8) ||
           !pe_disc_read(&disc,&archive,(u64)(range.start+range.scratchSectors+range.textureSectors)*2048+8,module,sizeof(module)))return 1;
        uint64_t hash=UINT64_C(14695981039346656037);
        for(unsigned i=0;i<sizeof(module);++i)hash=(hash^module[i])*UINT64_C(1099511628211);
        if(hash!=UINT64_C(0x2ad156414cc16b89) ||
           memcmp(module+(0x801954ec-0x8018eff0),g_RoomEruptionScript,18*sizeof(int)) ||
           memcmp(module+(0x801955dc-0x8018eff0),g_RoomOrbitFlareEffects.script,60) ||
           memcmp(module+(0x80195650-0x8018eff0),g_RoomHomingScript,sizeof(g_RoomHomingScript)))return 1;
        for(unsigned pass=0;pass<2;++pass){
            memset(g_RoomScriptArgs,0xa5,sizeof(g_RoomScriptArgs));
            memset(&g_RoomHomingLaunch,0xa5,sizeof(g_RoomHomingLaunch));
            memset(&g_RoomEruptionGlowPacket,0xa5,sizeof(g_RoomEruptionGlowPacket));
            memset(&g_RoomEruptionDebrisPacket,0xa5,sizeof(g_RoomEruptionDebrisPacket));
            memset(&g_RoomEruptionShadowPacket,0xa5,sizeof(g_RoomEruptionShadowPacket));
            memset(&g_RoomEruptionRingPacket,0xa5,sizeof(g_RoomEruptionRingPacket));
            g_PmCmdHandlerTable[37]=g_PmCmdHandlerTable[75]=NULL;
            g_FieldFxTaskPrograms[30]=g_FieldFxTaskPrograms[81]=NULL;
            if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)"m0412i")) ||
               g_PmCmdHandlerTable[37]!=&g_RoomGroundEruptionHandlers ||
               g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2] ||
               g_FieldFxTaskPrograms[30]!=&g_RoomOrbitFlareEffects ||
               g_FieldFxTaskPrograms[81]!=&g_RoomHomingProgram)return 1;
#define M412_ZERO(value) do {for(size_t z=0;z<sizeof(value);++z)if(((u8 *)&(value))[z])return 1;}while(0)
            M412_ZERO(g_RoomScriptArgs);M412_ZERO(g_RoomHomingLaunch);M412_ZERO(g_RoomEruptionGlowPacket);
            M412_ZERO(g_RoomEruptionDebrisPacket);M412_ZERO(g_RoomEruptionShadowPacket);M412_ZERO(g_RoomEruptionRingPacket);
#undef M412_ZERO
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M412: complete module fingerprint, three authored scripts, four native descriptor banks and repeated packet/argument reset passed");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m417-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe,archive;const unsigned rooms[]={413,416,417,437};
        if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        if(!pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive))return 1;
        for(unsigned room=0;room<4;++room){
            SceneRoomSectorRange range;u8 module[16904];
            if(!pe_disc_read(&disc,&exe,0x800ull+0x80093378u-0x80010000u+(rooms[room]-1)*8,&range,8) ||
               !pe_disc_read(&disc,&archive,(u64)(range.start+range.scratchSectors+range.textureSectors)*2048+8,module,sizeof(module)))return 1;
            uint64_t hash=UINT64_C(14695981039346656037);
            for(unsigned i=0;i<sizeof(module);++i)hash=(hash^module[i])*UINT64_C(1099511628211);
            if(hash!=UINT64_C(0xd5dc4febd9f6ddad) ||
               memcmp(module+(0x80193198-0x8018eff0),g_RoomJointSprayProgram.script,32))return 1;
            char name[16];snprintf(name,sizeof(name),"m%04ui",rooms[room]);
            for(unsigned pass=0;pass<2;++pass){
                memset(g_RoomJointSprayArgs,0xa5,sizeof(g_RoomJointSprayArgs));
                g_PmCmdHandlerTable[70]=g_PmCmdHandlerTable[75]=NULL;g_FieldFxTaskPrograms[50]=NULL;
                if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)name)) ||
                   g_PmCmdHandlerTable[70]!=&g_RoomActorClassTable[0] ||
                   g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2] ||
                   g_FieldFxTaskPrograms[50]!=&g_RoomJointSprayProgram ||
                   g_RoomJointSprayArgs[0] || g_RoomJointSprayArgs[1] ||
                   g_RoomJointSprayProgram.table.callbacks[0]!=RoomEffect_JointSprayController ||
                   g_RoomJointSprayProgram.table.sizes[0]!=4)return 1;
                intptr_t result=g_RoomJointSprayProgram.initialize(0,47,17,0);
                if(result!=(intptr_t)g_RoomJointSprayArgs || g_RoomJointSprayArgs[0]!=47 ||
                   g_RoomJointSprayArgs[1]!=17)return 1;
            }
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M413/M416/M417/M437: complete module fingerprints, authored script, both actor classes, task bank50, repeated argument reset and full native pointer passed");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m436-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe,archive;SceneRoomSectorRange range;u8 module[13728];
        if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        if(!pe_disc_find(&disc,"SLUS_006.62",&exe) || !pe_disc_find(&disc,"PE.IMG",&archive) ||
           !pe_disc_read(&disc,&exe,0x800+(0x80093378-0x80010000)+435*8,&range,8) ||
           !pe_disc_read(&disc,&archive,(u64)(range.start+range.scratchSectors+range.textureSectors)*2048+8,module,sizeof(module)))return 1;
        uint64_t hash=UINT64_C(14695981039346656037);
        for(unsigned i=0;i<sizeof(module);++i)hash=(hash^module[i])*UINT64_C(1099511628211);
        if(hash!=UINT64_C(0x600398f8d9a0ab3c))return 1;
        for(unsigned pass=0;pass<2;++pass){
            g_PmCmdHandlerTable[70]=NULL;g_PmCmdHandlerTable[75]=NULL;
            if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)"m0436i")) ||
               g_PmCmdHandlerTable[70]!=&g_RoomActorClassTable[0] ||
               g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2])return 1;
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M436 loader: complete USA actor module, both classes passed twice");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m430-load")){
        const char *path=getenv("PE_TEST_DISC2");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile archive;u8 authored[0x1874];
        if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        if(!pe_disc_find(&disc,"PE.IMG",&archive) ||
           !pe_disc_read(&disc,&archive,(98764u+33u+90u)*2048u,authored,sizeof(authored)))return 1;
        const u32 original_callback=0x8018f750,original_initializer=0x801906ec;
        if(memcmp(authored+0x183c,&original_callback,4) || memcmp(authored+0x186c,&original_initializer,4))return 1;
        for(unsigned pass=0;pass<2;++pass){
            memset(g_SceneE20EffectSlots,0xa5,sizeof(g_SceneE20EffectSlots));
            g_SceneE20HoverOrbHitCooldown=123;g_SceneE20HoverOrbModel=(void *)(uintptr_t)1;
            g_FieldFxTaskPrograms[103]=NULL;
            if(Scene_LoadRoom(Text_ParseBase32Id((signed char *)"m0430i")) ||
               g_FieldFxTaskPrograms[103]!=&g_SceneE20HoverOrbProgram)return 1;
            for(unsigned i=0;i<sizeof(g_SceneE20EffectSlots);++i)if(((u8 *)g_SceneE20EffectSlots)[i])return 1;
            if(g_SceneE20HoverOrbHitCooldown || g_SceneE20HoverOrbModel ||
               g_SceneE20HoverOrbProgram.table.callbacks[0]!=SceneE20_HoverOrbController ||
               g_SceneE20HoverOrbProgram.table.sizes[0]!=sizeof(SceneE20TrailEffect) ||
               g_SceneE20HoverOrbProgram.initialize!=SceneE20_CommandEffectSlot ||
               memcmp(g_SceneE20HoverOrbProgram.script,authored+0x17f8,32) ||
               memcmp(g_SceneE20FlareColorTrack,authored+0x181c,32))return 1;
            for(unsigned i=0;i<4;++i){
                if(SceneE20_CommandEffectSlot(2,i,123+(int)i,-456-(int)i)!=(intptr_t)&g_SceneE20EffectSlots[i] ||
                   g_SceneE20EffectSlots[i].position.x!=123+(int)i ||
                   g_SceneE20EffectSlots[i].position.z!=-456-(int)i ||
                   g_SceneE20EffectSlots[i].pending!=1)return 1;
            }
            if(SceneE20_CommandEffectSlot(4,3,25,0)!=(intptr_t)&g_SceneE20EffectSlots[3] ||
               g_SceneE20EffectSlots[3].duration!=25 || SceneE20_CommandEffectSlot(0,-1,0,0) ||
               SceneE20_CommandEffectSlot(0,4,0,0))return 1;
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M430 loader: real Disc2 assets, complete E20 descriptor/script/color track and per-entry reset passed twice");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m350-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        u32 token=Text_ParseBase32Id((signed char *)"m0350i");
        const unsigned indices[]={70,71,73,74,75,76};
        FieldFxTaskProgram *programs[]={&g_RoomM350HandEffects,&g_RoomM350OrbEffects,&g_RoomM350AreaEffects,&g_RoomM350DropEffects,&g_RoomM350RushEffects,&g_RoomM350BodyEffects};
        for(unsigned pass=0;pass<2;++pass){
            FieldActor dummy={0};g_RoomM350HandActor=&dummy;
            struct {void *data;size_t size;} mutable[]={
                {g_RoomM350HandPoints,sizeof(g_RoomM350HandPoints)},
                {&g_RoomM350OrbData,sizeof(g_RoomM350OrbData)},{&g_RoomM350AreaData,sizeof(g_RoomM350AreaData)},
                {&g_RoomEffectTrailPositions,sizeof(g_RoomEffectTrailPositions)},{&g_RoomM350RushData,sizeof(g_RoomM350RushData)}
            };
            for(unsigned i=0;i<5;++i)memset(mutable[i].data,0xa5,mutable[i].size);
            g_RoomM350HandSweepRepeats=123;g_RoomM350HandSweepDone=g_RoomM350HandSweepHit=1;
            g_RoomM350HandSweepTrapCorners[2].z=g_RoomM350HandSweepTrapCorners[3].z=123;
            for(unsigned i=0;i<6;++i)g_FieldFxTaskPrograms[indices[i]]=NULL;
            int status=Scene_LoadRoom(token);if(status){fprintf(stderr,"M350 load pass%u status%d\n",pass,status);return 1;}
            for(unsigned i=0;i<6;++i)if(g_FieldFxTaskPrograms[indices[i]]!=programs[i])return 1;
            if(g_PmCmdHandlerTable[70]!=&g_RoomActorClassTable[0] || g_PmCmdHandlerTable[75]!=&g_RoomActorClassTable[2] || g_PmCmdHandlerTable[73]!=&g_RoomActorClassTable[4])return 1;
            for(unsigned i=0;i<5;++i)for(unsigned j=0;j<mutable[i].size;++j)if(((u8 *)mutable[i].data)[j])return 1;
            if(g_RoomM350HandActor || g_RoomM350HandSweepRepeats || g_RoomM350HandSweepDone || g_RoomM350HandSweepHit || g_RoomM350HandSweepTrapCorners[2].z!=-4096 || g_RoomM350HandSweepTrapCorners[3].z!=-4096)return 1;
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M350 loader: real USA assets, native geometry, all nine descriptor registrations and per-entry state reset passed twice");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m349-load")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc,path))return 1;
        ResetGraph(0);CdRom_BindImage(&disc,NULL);if(Asset_OpenPeImage()!=0)return 1;
        Boot_InitGameState();D_8009CDC8='m';fx_image_io=1;
        u32 token=Text_ParseBase32Id((signed char *)"m0349i");
        for(unsigned pass=0;pass<2;++pass){
            memset(&g_RoomM349FlareParams,0xa5,sizeof(g_RoomM349FlareParams));
            int status=Scene_LoadRoom(token);
            if(status || g_FieldFxTaskPrograms[96]!=&g_RoomM349FlareEffects){fprintf(stderr,"M349 load pass%u status%d\n",pass,status);return 1;}
            const u8 *params=(const void *)&g_RoomM349FlareParams;
            for(unsigned i=0;i<sizeof(g_RoomM349FlareParams);++i)if(params[i])return 1;
        }
        CdRom_BindImage(NULL,NULL);pe_disc_close(&disc);
        puts("Original M349 loader: real USA assets, native geometry, command181 registration and per-entry state reset passed twice");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--m349")){
        if(!original_m349_states())return 1;
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};PeDiscFile exe;if(!pe_disc_open(&disc,path) || !pe_disc_find(&disc,"SLUS_006.62",&exe))return 1;
        int ok=original_m349_assets(&disc,&exe);pe_disc_close(&disc);return ok?0:1;
    }

    if(argc==2 && !strcmp(argv[1],"--akao-bank-lifetime"))return original_akao_bank_lifetime()?0:161;
    if(argc==2 && !strcmp(argv[1],"--akao-queue-backpressure")){
        /* Force both boundary cases before the real timer consumes the queue.
         * Run separately: hardware initialization owns the global driver. */
        Spu_InitHardware();
        if(Psyz_AudioInit()<0)return 1;
        u32 blanks=g_BootHostVBlankCount;
        for(unsigned i=0;i<128;++i){
            for(;;){
                EnterCriticalSection();
                if(!g_AkaoMessageQueueCount)break;
                ExitCriticalSection();if(!Psyz_AudioWaitForTimer())VSync(1);
            }
            memset(g_AkaoMessageQueue,0,sizeof(g_AkaoMessageQueue));
            g_AkaoMessageQueueCount=(i&1)?15:16;
            memset(&g_AkaoCmd,0,sizeof(g_AkaoCmd));
            g_AkaoCmd.opcode=(i&1)?0x99:0;
            ExitCriticalSection();
            int queued=Akao_EnqueueStagedCommand();
            EnterCriticalSection();
            /* The real consumer may drain the newly enqueued command before
             * this thread resumes. Consumed slots retain their contents. */
            int valid=queued==0 && (g_AkaoMessageQueueCount==0 ||
                g_AkaoMessageQueueCount==((i&1)?2:1));
            if((i&1) && (g_AkaoMessageQueue[0].opcode.word!=0x9B ||
                         g_AkaoMessageQueue[1].opcode.word!=0x9D))valid=0;
            ExitCriticalSection();
            if(!valid)return 160;
        }
        for(;;){
            EnterCriticalSection();
            int pending=g_AkaoMessageQueueCount;
            ExitCriticalSection();
            if(!pending)break;
            if(!Psyz_AudioWaitForTimer())VSync(1);
        }
        if(g_BootHostVBlankCount!=blanks)return 160;
        Spu_Shutdown();Psyz_AudioDestroy();
        puts("Original AKAO queue: 128 full/single and 15-entry/paired boundaries drain through the real timer without advancing VBlank");
        return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--attack-effect-status-gate"))return original_attack_effect_status_gate()?0:158;
    if(argc==2 && !strcmp(argv[1],"--tool-equipped-donor"))return original_tool_equipped_donor()?0:157;

    if(argc==3 && !strcmp(argv[1],"--native-movie-skip"))return original_native_movie_skip(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--native-control"))return original_native_control(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--native-diagnostic-hp-arguments"))return native_diagnostic_hp_arguments(argv[2])?0:1;
    if(argc==2 && !strcmp(argv[1],"--texture-pages")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!pe_disc_open(&disc,path))return 1;
        int result=original_scene_texture_pages(&disc);pe_disc_close(&disc);return result?0:156;
    }

    if(argc==3 && !strcmp(argv[1],"--native-stage"))return original_native_bootstrap(argv[2],2);
    if(argc==3 && !strcmp(argv[1],"--native-battle-entry"))return original_native_bootstrap(argv[2],3);
    if(argc==3 && !strcmp(argv[1],"--native-alligator-entry"))return original_native_bootstrap(argv[2],13);
    if(argc==3 && !strcmp(argv[1],"--native-m028"))return original_native_bootstrap(argv[2],12);
    if(argc==3 && !strcmp(argv[1],"--native-m023"))return original_native_bootstrap(argv[2],11);
    if(argc==3 && !strcmp(argv[1],"--native-m022"))return original_native_bootstrap(argv[2],10);
    if(argc==3 && !strcmp(argv[1],"--native-m017"))return original_native_bootstrap(argv[2],9);
    if(argc==3 && !strcmp(argv[1],"--native-m020-items"))return original_native_bootstrap(argv[2],8);
    if(argc==3 && !strcmp(argv[1],"--native-rat"))return original_native_bootstrap(argv[2],7);
    if(argc==3 && !strcmp(argv[1],"--native-hallway"))return original_native_bootstrap(argv[2],6);
    if(argc==3 && !strcmp(argv[1],"--native-backstage"))return original_native_bootstrap(argv[2],5);
    if(argc==3 && !strcmp(argv[1],"--native-melissa"))return original_native_bootstrap(argv[2],4);
    if(argc==3 && !strcmp(argv[1],"--native-graphics-controls"))return original_native_graphics_controls(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--native-field-control"))return original_native_bootstrap(argv[2],1);
    if(argc==2 && !strcmp(argv[1],"--spu-pitch-modulation"))return original_spu_pitch_modulation()?0:1;
    if(argc==2 && !strcmp(argv[1],"--spu-noise"))return original_spu_noise()?0:1;
    if(argc==2 && !strcmp(argv[1],"--spu-mix-headroom"))return original_spu_mix_headroom()?0:1;
    if(argc==2 && !strcmp(argv[1],"--spu-live-adsr"))return original_spu_live_adsr()?0:1;
    if(argc==2 && !strcmp(argv[1],"--spu-register-serialization"))return original_spu_register_serialization()?0:1;
    if(argc==3 && !strcmp(argv[1],"--native-tutorial"))return original_native_tutorial(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--native-bootstrap"))return original_native_bootstrap(argv[2],0);
    if(argc==2 && !strcmp(argv[1],"--credits-video-clock")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!pe_disc_open(&disc,path))return 1;
        int result=original_credit_video_clock(&disc);pe_disc_close(&disc);return result?0:155;
    }
    if(argc==2 && !strcmp(argv[1],"--credits-playback")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!pe_disc_open(&disc,path))return 1;
        int result=original_credit_playback(&disc);pe_disc_close(&disc);return result?0:155;
    }
    if(argc==2 && !strcmp(argv[1],"--fx-effects")){if(!SDL_Init(SDL_INIT_EVENTS))return 43;ResetGraph(0);return original_fx_shader_packets() && original_fx_blend() && original_fx_uncompressed_order() && original_fx_shader_pixels() && original_fx_shader_guards()?0:147;}
    if(argc==2 && !strcmp(argv[1],"--credits-sprites")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!pe_disc_open(&disc,path))return 1;
        int result=original_credit_sprites(&disc);pe_disc_close(&disc);return result?0:154;
    }
    if(argc==2 && !strcmp(argv[1],"--credits-images")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!pe_disc_open(&disc,path))return 1;
        int result=original_credit_images(&disc);pe_disc_close(&disc);return result?0:153;
    }
    if(argc==2 && !strcmp(argv[1],"--credits-text")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!pe_disc_open(&disc,path))return 1;
        int result=original_credit_text(&disc);pe_disc_close(&disc);return result?0:152;
    }
    if(argc==2 && !strcmp(argv[1],"--fx-step")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!pe_disc_open(&disc,path))return 1;
        int result=original_fx_scene_init(&disc) && original_fx_controller();pe_disc_close(&disc);return result?0:151;
    }
    if(argc==2 && !strcmp(argv[1],"--callback-list-storage"))return callback_list_storage()?0:123;
    if(argc==2 && !strcmp(argv[1],"--fx-frames")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!pe_disc_open(&disc,path))return 1;
        int result=original_fx_scene_init(&disc) && original_fx_frame_loop(&disc);pe_disc_close(&disc);return result?0:150;
    }
    if(argc==2 && !strcmp(argv[1],"--fx-scene")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!pe_disc_open(&disc,path))return 1;
        int result=original_fx_scene_init(&disc);pe_disc_close(&disc);return result?0:149;
    }
    if(argc==2 && !strcmp(argv[1],"--seeker-particles"))return original_seeker_particle_lifetimes()?0:123;
    if(argc==2 && !strcmp(argv[1],"--motion-program"))return original_motion_program()?0:123;
    if(argc==2 && !strcmp(argv[1],"--fx-motion")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!pe_disc_open(&disc,path))return 1;
        int result=original_fx_camera_motion(&disc);pe_disc_close(&disc);return result?0:148;
    }
    if(argc==2 && !strcmp(argv[1],"--fx-models")){
        const char *path=getenv("PE_TEST_DISC");if(!path || !*path)return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
        PeDisc disc={0};if(!pe_disc_open(&disc,path))return 1;
        int result=original_fx_model_render_assets(&disc) && original_fx_poly_render_assets(&disc);pe_disc_close(&disc);return result?0:146;
    }
    if(argc==2 && !strcmp(argv[1],"--fx-display")){if(!SDL_Init(SDL_INIT_EVENTS))return 43;ResetGraph(0);return original_gte_register_transfers() && original_fx_fog() && original_fx_display()?0:145;}
    if(argc==2 && !strcmp(argv[1],"--fx-camera"))return original_fx_node_pool() && original_fx_culling() && original_fx_camera_matrices()?0:143;
    if(argc==2 && !strcmp(argv[1],"--script-movie-error")){
        u32 script[]={53|(1u<<13),0,47,32,0};FieldActor actor={0};
        ScriptNode node={.current.script=script,.active=1};
        g_CurrentEntity=&actor;g_ScriptNodePool=&node;g_GameStateFlags=0;g_CdImage=NULL;
        Script_RunQueue();return 99;
    }
    if(argc==2 && !strcmp(argv[1],"--glint"))return original_positional_audio_projection() && original_glint_lifecycle()?0:141;
