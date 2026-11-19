/* Bad optional packs must be rejected before their offsets reach the game; rows a skin can't play
 * are held on its idle pose. */
#include <assert.h>
#include <stdio.h>
#include "../mods/pe_wardrobe/pe_skin_rows.h"
Pe1GameState g_GameState;
FieldActor *g_PlayerEntity;
void pe_log(const char *tag,const char *fmt,...) { (void)tag;(void)fmt; }   /* the hold rows log */
static void put32(u8 *p,u32 v) { memcpy(p,&v,4); }
/* A one-frame row of constant channels: bone b's axis a holds value (b*3+a)&0xFF. */
static u32 make_row(u8 *p,unsigned bones,int shrt,unsigned bias) {
    u32 at=12;memset(p,0,12);p[0]=shrt?2:0;p[1]=(u8)(bones-1);p[2]=1;
    for(unsigned k=0;k<3;++k,at+=4){p[at]=1;p[at+1]=0;p[at+2]=(u8)k;p[at+3]=0;}
    for(unsigned k=0;k<bones*3;++k,at+=4){
        p[at]=1;p[at+1]=0;p[at+2]=0;p[at+3]=0;
        if(shrt){p[at+2]=(u8)((k+bias)*16);p[at+3]=(u8)(((k+bias)*16)>>8);}else p[at+1]=(u8)(k+bias);
    }
    return at;
}
int main(void) {
    u8 data[512]={0};PeBlob b={.data=data,.size=sizeof data};
    put32(data+4,8);assert(pe_dir(&b));
    put32(data+4,UINT32_MAX-15u);assert(!pe_dir(&b));
    put32(data+4,sizeof data-sizeof(SceneAssetDirectory)+1u);assert(!pe_dir(&b));
    u8 row[384]={0};row[1]=29;
    PeSkin skin={0};skin.rooms=b;skin.nrooms=1;
    put32(data+8,pe_fingerprint(row));
    put32(data+12,UINT32_MAX-3u);put32(data+16,16);
    assert(pe_room_copy(&skin,row)==NULL);
    put32(data+12,128);put32(data+16,sizeof row);
    memcpy(data+128,row,sizeof row);
    assert(pe_room_copy(&skin,row)==data+128);
    put32(data+16,12);assert(pe_room_copy(&skin,row)==NULL);
    put32(data+16,0);assert(pe_room_copy(&skin,row)==NULL);
    /* A 23-part, single-frame scene row must match its retargeted copy. */
    memset(row,0,sizeof row);row[1]=22;
    assert(pe_row_length(row,sizeof row)==300);
    assert(pe_fingerprint(row)!=0);
    put32(data+8,pe_fingerprint(row));put32(data+12,128);put32(data+16,372);
    memset(data+128,0,384);data[129]=28;
    assert(pe_room_copy(&skin,row)==data+128);
    /* A bundle's jacket row 28 must not overwrite the long dress scene row. */
    u8 bundle[512]={0};
    SceneAssetBlob *blob=(SceneAssetBlob *)bundle;blob->directoryOffset=16;
    SceneAssetDirectory *dir=(SceneAssetDirectory *)(bundle+16);
    dir->bankRootEntries=80;dir->bankRowEntries=(2u<<22)|96;
    SceneBankAssetRecord *root=(SceneBankAssetRecord *)(bundle+80);
    SceneBankAssetRecord *rec=(SceneBankAssetRecord *)(bundle+96);
    root->source.offsetAndId=160;
    rec[0].source.offsetAndId=(2u<<24)|200;
    rec[1].source.offsetAndId=(28u<<24)|220;
    g_SceneAreaType=11;g_GameState.bank_rows[0][28]=row;
    pe_point_bundle(bundle);
    assert(g_GameState.bank_slots[0]==bundle+160);
    assert(g_GameState.bank_rows[0][2]==bundle+200);
    assert(g_GameState.bank_rows[0][28]==row);
    RenderObjectHeader model={0};model.part_count=23;
    g_GameState.bank_slots[0]=&model;
    pe_room_rows(&skin);
    assert(g_GameState.bank_rows[0][28]==row && !s_replaced[28]);
    model.part_count=29;pe_room_rows(&skin);
    assert(g_GameState.bank_rows[0][28]==data+128);
    assert(s_replaced[28]==row);
    FieldActor player={0};g_PlayerEntity=&player;player.action_data=row;
    pe_running_row(&skin);
    assert(player.action_data==data+128);
    /* A different frame count must not replace an animation already running. */
    player.action_data=row;data[130]=1;
    pe_running_row(&skin);assert(player.action_data==row);data[130]=0;
    s_skin[0]=skin;s_skin[0].ok=1;s_skins=1;
    pe_restore_rows();
    assert(g_GameState.bank_rows[0][28]==row && !s_replaced[28]);
    g_SceneAreaType=10;pe_point_bundle(bundle);
    assert(g_GameState.bank_rows[0][28]==bundle+220);
    /* Issue #220: a jacket room row (29 bones) on the worn evening dress (31 parts) leaves two parts
     * undecoded, compounding into a stretched gown; its fitted copy drives all 31. */
    {
        static u8 idle[1024],jacket[1024],longer[1024],shrt[1024];
        const u32 nidle=make_row(idle,31,0,100),njacket=make_row(jacket,29,0,0),nlonger=make_row(longer,33,0,0);
        const u32 nshort=make_row(shrt,29,1,0);
        RenderObjectHeader gown={0};gown.part_count=31;
        FieldActor actor={0};
        memset(&g_GameState,0,sizeof g_GameState);memset(s_replaced,0,sizeof s_replaced);s_skins=0;
        g_GameState.bank_slots[0]=&gown;g_GameState.bank_rows[0][0x15]=idle;
        g_GameState.bank_rows[0][31]=jacket;g_GameState.bank_rows[0][32]=longer;g_GameState.bank_rows[0][33]=shrt;
        g_GameState.bank_rows[0][34]=idle;
        actor.model_state.model_header=&gown;actor.action_data=jacket;g_PlayerEntity=&actor;
        assert(pe_row_length(idle,sizeof idle)==nidle&&pe_row_length(jacket,sizeof jacket)==njacket);
        assert(pe_fit_rows());
        u8 *fit=g_GameState.bank_rows[0][31];
        assert(fit!=jacket&&s_replaced[31]==jacket&&actor.action_data==fit);
        assert(fit[1]==30&&pe_row_length(fit,0x10000)==njacket+24&&fit[0]==jacket[0]&&!memcmp(fit+2,jacket+2,njacket-2));
        for(unsigned k=29*3;k<31*3;++k)assert(fit[24+k*4]==1&&fit[24+k*4+1]==(u8)(k+100));   /* idle's rest pose */
        u8 *cut=g_GameState.bank_rows[0][32];
        assert(cut!=longer&&cut[1]==30&&pe_row_length(cut,0x10000)==nlonger-24&&!memcmp(cut+2,longer+2,nlonger-26));
        u8 *half=g_GameState.bank_rows[0][33];
        assert(half[1]==30&&pe_row_length(half,0x10000)==nshort+24);
        for(unsigned k=29*3;k<31*3;++k)assert((half[24+k*4+2]|half[24+k*4+3]<<8)==(k+100)*16u);
        assert(g_GameState.bank_rows[0][34]==idle&&!s_replaced[34]);                /* rows that fit stay */
        assert(!pe_fit_rows()&&g_GameState.bank_rows[0][31]==fit&&actor.action_data==fit);   /* settled */
        actor.action_data=jacket;assert(pe_fit_rows()&&actor.action_data==fit);  /* one copy per row */
        pe_restore_rows();
        assert(g_GameState.bank_rows[0][31]==jacket&&g_GameState.bank_rows[0][32]==longer&&
               g_GameState.bank_rows[0][33]==shrt&&!s_fitting);
        g_GameState.bank_rows[0][0x15]=jacket;                 /* no idle row of her parts: identity */
        memset(longer,7,12);make_row(longer,29,0,50);g_GameState.bank_rows[0][31]=longer;
        assert(pe_fit_rows());fit=g_GameState.bank_rows[0][31];
        for(unsigned k=29*3;k<31*3;++k)assert(fit[24+k*4]==1&&fit[24+k*4+1]==0);
        gown.part_count=29;assert(pe_fit_row(jacket)==jacket);
    }

    /* A game row with no copy and the wrong bone count gets the skin's idle pose held, with the
     * game row's frames, hit values and starting position; fitting rows stay. */
    memset(&g_GameState,0,sizeof g_GameState);g_PlayerEntity=&player;
    for(int i=0;i<PE_ROWS_PER_BANK;++i)g_GameState.bank_rows[0][i]=NULL;
    memset(s_replaced,0,sizeof s_replaced);
    static u8 skinmodel[1024];RenderObjectHeader header={0};header.part_count=29;
    put32(skinmodel+4,16);put32(skinmodel+16+0x10,(1u<<22)|128u);put32(skinmodel+128+4,256u|(u32)PE_IDLE_ROW<<24);
    u8 *idle=skinmodel+256;idle[0]=1;idle[1]=28;idle[2]=1;
    for(int c=0;c<90;++c){idle[12+4*c]=1;if(c<3)idle[12+4*c+2]=(u8)(7+c);else idle[12+4*c+1]=(u8)c;}
    assert(pe_row_length(idle,0x10000u)==372);
    static u8 game[2048];game[0]=1;game[1]=22;game[2]=60;for(int i=4;i<12;++i)game[i]=(u8)(0x10+i);
    u32 at=12;game[at+2]=(u8)(-500&0xff);game[at+3]=(u8)((u16)-500>>8);at+=((60>>1)+1)*4;   /* varying X */
    for(int c=1;c<3+23*3;++c){game[at]=1;at+=4;}
    assert(pe_row_length(game,0x10000u)==at);
    static u8 fits[372];memcpy(fits,idle,372);
    PeSkin *k=&s_skin[0];memset(k,0,sizeof *k);k->ok=1;k->model.data=skinmodel;k->model.size=sizeof skinmodel;s_skins=1;
    g_GameState.bank_slots[0]=&header;g_GameState.bank_rows[0][33]=game;g_GameState.bank_rows[0][5]=fits;
    pe_guard_rows(k);
    u8 *hold=g_GameState.bank_rows[0][33];
    assert(hold==s_hold[33] && s_replaced[33]==game && g_GameState.bank_rows[0][5]==fits);
    assert(pe_row_length(hold,0x10000u)==372 && hold[1]==28 && hold[2]==60 && !memcmp(hold+3,game+3,9));
    s16 got[90],want[90];assert(pe_first_samples(hold,got,90)==90 && pe_first_samples(idle,want,90)==90);
    assert(got[0]==-500 && got[1]==0 && !memcmp(got+3,want+3,87*sizeof(s16)));
    pe_guard_rows(k);assert(g_GameState.bank_rows[0][33]==hold && s_replaced[33]==game);   /* steady */
    player.action_data=game;
    pe_running_row(k);assert(player.action_data==hold);   /* the slot's hold, same frames */
    g_GameState.bank_rows[0][33]=NULL;memset(s_replaced,0,sizeof s_replaced);player.action_data=game;
    pe_running_row(k);assert(player.action_data==s_hold[PE_ROWS_PER_BANK]);
    g_GameState.bank_rows[0][33]=hold;s_replaced[33]=game;pe_restore_rows();assert(g_GameState.bank_rows[0][33]==game);
    puts("Skin containers: bounds, long dress scene rows, fitted rows, restoration and unfit rows held passed");
    return 0;
}
