/* Bad optional packs must be rejected before their offsets reach the game. */
#include <assert.h>
#include <stdio.h>
#include "../mods/pe_wardrobe/pe_skin_loader.c"
Pe1GameState g_GameState;
FieldActor *g_PlayerEntity;
static void put32(u8 *p,u32 v) { memcpy(p,&v,4); }
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
    puts("Skin containers: bounds, long dress scene rows and restoration passed");
    return 0;
}
