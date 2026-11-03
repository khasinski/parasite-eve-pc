/* Bad optional packs must be rejected before their offsets reach the game. */
#include <assert.h>
#include <stdio.h>
#include "../mods/pe_wardrobe/pe_skin_loader.c"
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
    puts("Skin containers: directory, animation bounds and truncated rows passed");
    return 0;
}
