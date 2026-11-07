#ifdef NDEBUG
#undef NDEBUG /* These runtime checks must also execute in release CI builds. */
#endif
#include <assert.h>
#include <stdio.h>
#ifndef _WIN32
#include <sys/types.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe1/game_state.h"
#include "pe1/pad_state.h"
#include "pe1/scene_room.h"
#include "pe_checkpoint_qol.h"
Pe1GameState g_GameState;
SceneRoomSectorRange g_RoomSectorTable[SCENE_ROOM_COUNT];
int g_SceneDispatchCur;
u32 g_ScriptVariableBank[512];
static int loaded = 1, battle, *mode, free_field = 1;
static int *always_run, *run_speed, *walk_speed, *combat_speed, *entry_speed, *repeat_interact;

static PeHookFn on_player_move, on_pad_map, on_actor_step, on_frame;
u32 g_FieldPadBits, g_FieldPadPressed;
static BattleEntity player;
PadPacket g_PadPackets[2];
s32 g_FieldRenderFlagTable[32];
int g_BattleModeState, g_FieldMoveLock;
BattleEntity *pe_player(void) { return &player; }
int pe_free_field(void) { return free_field; }
void pe_mod_log(const char *id,const char *fmt,...) { (void)id;(void)fmt; }
int pe_menu_register_toggle(const char *p,const char *l,const char *d,int *v,PeMenuPerFrame tick) {
    (void)p;(void)d;(void)tick;
    if(!strcmp(l,"Always run"))always_run=v;
    else {assert(!strcmp(l,"Repeat interact"));repeat_interact=v;}
    return 0;
}
int pe_menu_register_tick(PeMenuPerFrame tick) { (void)tick; assert(0);return 0; }
void pe_move_register(const char *);
static void movement_regression(void) {
    pe_move_register("pe_qol");
    assert(on_pad_map && on_actor_step && on_player_move);
    assert(repeat_interact && on_frame && !*repeat_interact);
    /* Only cooked action edges repeat; raw pad, held bits and other actions
     * stay intact. Existing game mapping has already selected action bit8. */
    loaded=free_field=1;battle=0;
    g_FieldPadBits=0x110;g_FieldPadPressed=0x10;
    for(unsigned i=0;i<32;++i){on_frame(NULL,NULL);assert(g_FieldPadPressed==0x10);}
    *repeat_interact=1;
    g_FieldPadPressed=0x110;on_frame(NULL,NULL);assert(g_FieldPadPressed==0x110);
    PadPacket raw=g_PadPackets[0];
    for(unsigned i=1;i<=32;++i){
        g_FieldPadPressed=0x10;on_frame(NULL,NULL);
        assert(g_FieldPadPressed==(i%8?0x10:0x110));
        assert(g_FieldPadBits==0x110 && !memcmp(&raw,&g_PadPackets[0],sizeof raw));
    }
    /* Flag0x4 enables collision response; flag0x1 blocks player input. */
    g_FieldMoveLock=4;
    for(unsigned i=1;i<=8;++i){g_FieldPadPressed=0;on_frame(NULL,NULL);assert(g_FieldPadPressed==(i==8?0x100:0));}
    g_FieldMoveLock=0;
    for(unsigned guard=0;guard<6;++guard){
        loaded=free_field=1;battle=0;g_FieldMoveLock=0;*repeat_interact=1;g_FieldPadBits=0x100;
        g_FieldPadPressed=0x100;on_frame(NULL,NULL);
        for(unsigned i=0;i<7;++i){g_FieldPadPressed=0;on_frame(NULL,NULL);assert(!g_FieldPadPressed);}
        if(guard==0)loaded=0;
        if(guard==1)free_field=0;
        if(guard==2)battle=1;
        if(guard==3)*repeat_interact=0;
        if(guard==4)g_FieldPadBits=0;
        if(guard==5)g_FieldMoveLock=1;
        on_frame(NULL,NULL);assert(!g_FieldPadPressed);
        loaded=free_field=1;battle=0;g_FieldMoveLock=0;*repeat_interact=1;g_FieldPadBits=0x100;
        for(unsigned i=1;i<=8;++i){g_FieldPadPressed=0;on_frame(NULL,NULL);assert(g_FieldPadPressed==(i==8?0x100:0));}
    }
#if PE_NATIVE_STATE_CPU_COMPONENTS
    unsigned char state[20],bad[20];
    loaded=free_field=1;battle=0;g_FieldMoveLock=0;*repeat_interact=1;g_FieldPadBits=0x100;
    g_FieldPadPressed=0x100;on_frame(NULL,NULL);
    for(unsigned i=0;i<7;++i){g_FieldPadPressed=0;on_frame(NULL,NULL);}
    assert(pe_move_checkpoint_walk(NULL,state,0));
    assert((state[0]>>2)==7 && (state[0]&2));
    g_FieldPadPressed=0;on_frame(NULL,NULL);assert(g_FieldPadPressed==0x100);
    assert(pe_move_checkpoint_walk(state,NULL,0)); /* validation is read-only */
    g_FieldPadPressed=0;on_frame(NULL,NULL);assert(!g_FieldPadPressed);
    assert(pe_move_checkpoint_walk(state,NULL,1));
    g_FieldPadPressed=0;on_frame(NULL,NULL);assert(g_FieldPadPressed==0x100);
    /* An invalid later field cannot partially restore the saved repeat phase. */
    memcpy(bad,state,sizeof bad);bad[16]=99;
    assert(!pe_move_checkpoint_walk(bad,NULL,1));
    g_FieldPadPressed=0;on_frame(NULL,NULL);assert(!g_FieldPadPressed);
    memcpy(bad,state,sizeof bad);bad[0]|=0x20;
    assert(!pe_move_checkpoint_walk(bad,NULL,0));
    *repeat_interact=0;on_frame(NULL,NULL);
    assert(!pe_move_checkpoint_walk(state,NULL,1)); /* configuration differs */
    assert(pe_move_checkpoint_walk(NULL,state,0) && state[0]<=1);
    assert(pe_move_checkpoint_walk(state,NULL,1)); /* legacy word1 */
    *always_run=0;
    assert(pe_move_checkpoint_walk(NULL,state,0) && state[0]==0);
    assert(pe_move_checkpoint_walk(state,NULL,1)); /* legacy word0 */
    *always_run=2;assert(!pe_move_checkpoint_walk(NULL,state,0));
    *always_run=0;
#endif
    *repeat_interact=0;g_FieldPadBits=g_FieldPadPressed=0;
    *always_run=*run_speed=*walk_speed=*combat_speed=*entry_speed=0;
    memset(&player,0,sizeof(player));
    g_PadPackets[0].header=0x4100;g_PadPackets[0].buttons=0xFFFF;
    g_FieldRenderFlagTable[0]=0x4000;
    // Actual Central Park encounter: the game sets 3x for its entry script.
    battle=1;free_field=0;g_BattleModeState=7;player.actionMode=4;
    for(int factor=0x10000;factor<=0x30000;factor+=0x4000) {
        player.moveFactor=factor;assert(player.moveFactor==factor);
        int local=factor;PeHookPlayerMove step={&player,&local,1,1};
        on_player_move(&step,NULL);assert(local==factor && player.moveFactor==factor);
    }
    // Each speed affects only a local input calculation; it cannot survive
    // into a menu, room reload or cutscene, even if the script uses that speed.
    battle=0;free_field=1;player.moveFactor=0x10000;
    for(int speed=0;speed<8;++speed) {
        const int factors[]={0x10000,0x14000,0x18000,0x1C000,0x20000,0x24000,0x28000,0x30000};
        *walk_speed=speed;int local=player.moveFactor;
        PeHookPlayerMove step={&player,&local,0,0};on_player_move(&step,NULL);
        assert(local==factors[speed] && player.moveFactor==0x10000);
        player.moveFactor=factors[speed];battle=1;free_field=0;
        assert(player.moveFactor==factors[speed]);
        battle=0;free_field=1;player.moveFactor=0x10000;
    }
    *run_speed=5;*walk_speed=3;*combat_speed=2;
    int local=0x10000;PeHookPlayerMove step={&player,&local,1,0};
    on_player_move(&step,NULL);assert(local==0x24000);
    local=0x10000;step.combat=1;battle=1;free_field=0;g_BattleModeState=0;
    on_player_move(&step,NULL);assert(local==0x18000);
    local=0x30000;on_player_move(&step,NULL);assert(local==0x30000);
    local=0x10000;step.actor=&step;on_player_move(&step,NULL);assert(local==0x10000);
    step.actor=&player;loaded=0;on_player_move(&step,NULL);assert(local==0x10000);loaded=1;
    battle=0;free_field=1;
    /* Run mapping is local even if the original map equals a previous mod
     * value. It follows remapping immediately; disabling needs no cleanup. */
    const int masks[]={0x4000,0x100,0,0xF0};
    for(unsigned m=0;m<4;++m)for(int enabled=0;enabled<2;++enabled)
    for(int field=0;field<2;++field)for(int ready=0;ready<2;++ready)
    for(int held=0;held<2;++held) {
        g_FieldRenderFlagTable[0]=masks[m];*always_run=enabled;free_field=field;loaded=ready;
        int mask=masks[m];PeHookPadMap pad={held?(unsigned)masks[m]:0,&mask};
        on_pad_map(&pad,NULL);
        assert(mask==(enabled && field && ready ? (pad.buttons & (unsigned)masks[m] ? 0 : 0xF0) : masks[m]));
        assert(g_FieldRenderFlagTable[0]==masks[m]);
    }
    loaded=1;free_field=0;battle=1;g_BattleModeState=7;
    memset(&player,0,sizeof(player));player.actionMode=5;player.animStep=0x10000;
    player.motionX=100;player.motionZ=-80;
    const int entry_factors[]={0x10000,0x18000,0x20000,0x30000,0x40000};
    for(int choice=0;choice<5;++choice) {
        *entry_speed=choice;
        int speed=100,anim=0x10000;
        BattleEntity before=player;
        PeHookActorStep step={&player,&speed,NULL};on_actor_step(&step,NULL);
        assert(speed==100*entry_factors[choice]/0x10000);
        step.speed=NULL;step.anim=&anim;on_actor_step(&step,NULL);
        assert(anim==entry_factors[choice] && !memcmp(&player,&before,sizeof(player)));
        anim=entry_factors[choice]+1;on_actor_step(&step,NULL);assert(anim==entry_factors[choice]+1);
        /* Exiting the phase cannot retain a previous mod step, even when
         * a script deliberately chooses the exact same value. */
        g_BattleModeState=0;anim=entry_factors[choice];on_actor_step(&step,NULL);
        assert(anim==entry_factors[choice]);g_BattleModeState=7;
    }
    *entry_speed=4;
    for(unsigned guard=0;guard<5;++guard) {
        loaded=guard!=0;battle=guard!=1;g_BattleModeState=guard==2?0:7;
        player.actionMode=guard==3?4:5;
        int speed=100;PeHookActorStep step={guard==4?(void *)&step:&player,&speed,NULL};
        on_actor_step(&step,NULL);assert(speed==100);
    }
    loaded=1;battle=1;player.actionMode=5;
    player.facingAngle=0x800;
    int speed=100;PeHookActorStep entry_step={&player,&speed,NULL};on_actor_step(&entry_step,NULL);
    assert(speed==100);
    player.facingAngle=0;speed=INT32_MAX;on_actor_step(&entry_step,NULL);
    assert(speed==INT32_MAX);
    speed=INT32_MIN;on_actor_step(&entry_step,NULL);assert(speed==INT32_MIN);
    battle=0;free_field=1;
    puts("Passed movement options: eight local input speeds, run/combat selection, scripts, transitions and non-player actors preserve original actor factors");
}
static PeHookFn on_random;
int pe_game_loaded(void) { return loaded; }
int pe_in_battle(void) { return battle; }
int Text_EncodeBase32(char *out, unsigned value) { (void)value; strcpy(out,"M0001"); return 0; }
int Text_ParseMapNumber(signed char *s) { (void)s; return 1; }
int pe_menu_register_choice(const char *p, const char *l, const char *d, int *v,
                            const char *const *n, unsigned c) {
    (void)p; (void)d; (void)n;
    if(!strcmp(l,"Run speed")) { assert(c==8);run_speed=v; }
    else if(!strcmp(l,"Walk speed")) { assert(c==8);walk_speed=v; }
    else if(!strcmp(l,"Combat speed")) { assert(c==8);combat_speed=v; }
    else if(!strcmp(l,"Battle start speed")) { assert(c==5);entry_speed=v; }
    else { assert(c==4);mode=v; }
    return 0;
}
int pe_hook_add(int id, PeHookFn fn, void *u, int p, const char *m) {
    (void)u; (void)p; (void)m;
    if(id==PE_HOOK_SCRIPT_RANDOM) on_random=fn;
    else if(id==PE_HOOK_PLAYER_MOVE) on_player_move=fn;
    else if(id==PE_HOOK_PAD_MAP) on_pad_map=fn;
    else if(id==PE_HOOK_ACTOR_STEP) on_actor_step=fn;
    else if(id==PE_HOOK_FRAME) on_frame=fn;
    else assert(id==PE_HOOK_PAD_READ);
    return 0;
}
void pe_battle_frequency_register(const char *);
static unsigned word(const unsigned char *p) { unsigned v; memcpy(&v,p,4); return v; }
static int seek64(FILE *f, long long offset) {
#ifdef _WIN32
    return _fseeki64(f, offset, SEEK_SET);
#else
    return fseeko(f, (off_t)offset, SEEK_SET);
#endif
}
static void user_data(FILE *f, unsigned lba, void *out, size_t bytes) {
    unsigned char *p = out;
    while (bytes) {
        size_t n = bytes > 2048 ? 2048 : bytes;
        assert(!seek64(f, (long long)lba * 2352 + 24));
        assert(fread(p,1,n,f) == n); p += n; bytes -= n; ++lba;
    }
}
static void real_rooms(const char *disc_path, const char *exe_path) {
    FILE *disc = fopen(disc_path,"rb"), *exe = fopen(exe_path,"rb");
    unsigned char pvd[2048], header[32], table[450*8];
    unsigned img = 0, checked = 0, matches = 0;
    assert(disc && exe);
    user_data(disc,16,pvd,sizeof pvd);
    unsigned root_lba = word(pvd+158), root_size = word(pvd+166);
    unsigned char *root = malloc(root_size); assert(root);
    user_data(disc,root_lba,root,root_size);
    for (size_t p=0; p+34 < root_size;) {
        if (!root[p]) { p = (p/2048+1)*2048; continue; }
        unsigned n = root[p]; assert(n <= root_size-p);
        if (root[p+32] >= 6 && !memcmp(root+p+33,"PE.IMG",6)) img = word(root+p+2);
        p += n;
    }
    free(root); assert(img);
    assert(fread(header,1,sizeof header,exe) == sizeof header);
    assert(!fseek(exe,(long)(0x80093378u-word(header+24)+0x800),SEEK_SET));
    assert(fread(table,1,sizeof table,exe) == sizeof table); fclose(exe);
    *mode = 1;
    for (unsigned i=0; i<450; ++i) {
        unsigned bits = word(table+i*8+4), sectors = bits >> 20;
        if (!sectors) continue;
        size_t bytes = (size_t)sectors * 2048;
        unsigned char *room = malloc(bytes); assert(room);
        user_data(disc,img+word(table+i*8)+(bits&255)+((bits>>8)&4095),room,bytes);
        g_GameState.loaded_scene_assets = (SceneAssetView *)room;
        g_RoomSectorTable[0].roomSectors = sectors;
        for (size_t p=0; p+20 <= bytes; p+=4) {
            if ((word(room+p)&0x1FFFu) != 26) continue;
            int start=0, end=(int)word(room+p+16), before=end;
            PeHookScriptRandom roll = {&start,&end,(const unsigned *)(room+p+20)};
            on_random(&roll,NULL);
            if (end != before) { assert(end == before/2); ++matches; }
        }
        ++checked; free(room);
    }
    fclose(disc); assert(matches);
    printf("Original disc data: %u room blobs checked; %u encounter-roll sequences matched.\n",checked,matches);
}
int main(int argc, char **argv) {
    unsigned code[512] = {0};
    pe_battle_frequency_register("pe_qol");
    g_GameState.loaded_scene_assets=(SceneAssetView *)code;
    g_RoomSectorTable[0].roomSectors=1;
    code[0]=26|(3<<13)|(3<<17); code[2]=2; code[4]=256;
    code[5]=9|(4<<13)|(3<<23); code[7]=10; code[9]=2;
    code[11]=5|(2<<13); code[15]=0x55|(1<<13);
    int start=0,end=256; PeHookScriptRandom roll={&start,&end,code+5};
    assert(*mode==0); on_random(&roll,NULL); assert(end==256);
    *mode=1; on_random(&roll,NULL); assert(end==128 && code[4]==256);
    *mode=2; end=256; on_random(&roll,NULL); assert(end==64);
    *mode=3; end=256; on_random(&roll,NULL); assert(end==1);
    *mode=0; end=256; on_random(&roll,NULL); assert(end==256);
    *mode=1; code[15]=0; on_random(&roll,NULL); assert(end==256); code[15]=0x55|(1<<13);
    code[7]=9; on_random(&roll,NULL); assert(end==256); code[7]=10;
    battle=1; on_random(&roll,NULL); assert(end==256); battle=0;
    loaded=0; on_random(&roll,NULL); assert(end==256); loaded=1;
    g_ScriptVariableBank[10]=0x80000000u; on_random(&roll,NULL); assert(end==256); g_ScriptVariableBank[10]=0;
    roll.next=code+1; on_random(&roll,NULL); assert(end==256);
    roll.next=code+511; on_random(&roll,NULL); assert(end==256);
    roll.next=code+5; end=2; *mode=2; on_random(&roll,NULL); assert(end==1);
    puts("Passed frequency modes, Normal restoration, unrelated RNG rejection, truncation bounds, and battle/skip guards.");
    if (argc==3) real_rooms(argv[1],argv[2]);
    movement_regression();
    return 0;
}
