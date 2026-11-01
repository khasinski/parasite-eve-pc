#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pe_catalog.h"
#include "pe_economy.h"
#include "pe_services.h"
SaveRuntimeState g_SaveRuntimeState;
GameStateBlock g_GameStateBlock;
Pe1GameState g_GameState;
InventoryItemWorkspace g_InventoryItemWorkspace;
static ItemDataRecord bases[256];
int g_InvBonusSlotCount;
static unsigned junk=1000;
static int enabled=1;
int pe_game_loaded(void) { return 1; }
int pe_loot_enabled(void) { return enabled; }
uint32_t pe_loot_price(uint32_t v) { return v; }
uint32_t pe_loot_junk(void) { return junk; }
void pe_loot_give_junk(uint32_t n) { junk+=n; }
int pe_loot_spend_junk(uint32_t n) { if(junk<n)return 0;junk-=n;return 1; }
int pe_loot_armor_has(int e) { (void)e;return 0; }
ItemDataRecord *pe_base_item(unsigned id) { return id<256?&bases[id]:NULL; }
ItemDataRecord *pe_equipped(int armor) { return pe_cat_bag_record(armor?g_AyaSaveState.equipped_armor_slot:g_AyaSaveState.equipped_weapon_slot); }
void pe_toast(const char *text) { (void)text; }
void pe_log(const char *tag,const char *fmt,...) { (void)tag;(void)fmt; }
static int stack_extra;
static int test_take(int l,int slot) {
    if(l==0 && g_AyaSaveState.inventory_items[slot] && stack_extra>0){--stack_extra;return 1;}
    if(l==0)g_AyaSaveState.inventory_items[slot]=0;
    else g_WayneStorageItems[slot]=0;
    return 1;
}
static const PeStacksApi test_stacks={.take=test_take};
const void *pe_service_get(const char *n,int v) { return (!strcmp(n,"stacks")&&v==1&&stack_extra>=0)?&test_stacks:NULL; }
int main(void) {
    g_AyaSaveState.inventory_slot_count=10;
    g_AyaSaveState.equipped_weapon_slot=0;g_AyaSaveState.equipped_armor_slot=1;
    bases[0x45]=(ItemDataRecord){.iconId=1,.itemId=0x45,.kind=1,.tailCount=3,.tailCapacity=3};
    ItemDataRecord *gun=&g_InventoryRuntime.equipment[0],*armor=&g_InventoryRuntime.equipment[1];
    *gun=bases[0x45];*armor=(ItemDataRecord){.iconId=17,.itemId=0x97,.kind=9,.tailCount=3,.tailCapacity=3};
    g_AyaSaveState.inventory_items[0]=0x100;g_AyaSaveState.inventory_items[1]=0x101;
    PeCatalogState *cat=pe_catalog_state();cat->bits[0]=(1u<<1)|(1u<<2)|(1u<<20);cat->bits[1]=1u<<12;
    gun->tailData[0]=0x21;
    ItemDataRecord before=*gun;unsigned balance=junk;
    assert(pe_cat_install(0,1,2,1)==PE_CAT_OK && !memcmp(&before,gun,sizeof before) && junk==balance);
    assert(pe_cat_install(0,1,2,0)==PE_CAT_OK && gun->tailData[0]==0 && gun->tailData[1]==0x22 && junk==balance-30);
    assert(pe_cat_install(0,2,20,0)==PE_CAT_TOOL);
    g_AyaSaveState.inventory_items[2]=0x16;stack_extra=2;balance=junk;
    assert(pe_cat_install(0,2,20,0)==PE_CAT_OK && gun->tailData[2]==0x74 && g_AyaSaveState.inventory_items[2]==0x16 && stack_extra==1 && junk==balance-80);
    gun->tailData[2]=0;stack_extra=0;balance=junk;
    assert(pe_cat_install(0,2,20,0)==PE_CAT_OK && gun->tailData[2]==0x74 && g_AyaSaveState.inventory_items[2]==0 && junk==balance-80);
    armor->tailData[0]=0xa9;
    assert(pe_cat_remove(1,0,0)==PE_CAT_BAG && pe_cat_install(1,0,12,0)==PE_CAT_BAG);
    enabled=0;before=*gun;balance=junk;
    assert(pe_cat_install(0,0,1,0)==PE_CAT_OFF && !memcmp(&before,gun,sizeof before) && junk==balance);
    enabled=1;
    /* Dismantle an unequipped weapon and rebuild the exact bonuses and slots. */
    g_InventoryRuntime.equipment[2]=bases[0x45];
    g_InventoryRuntime.equipment[2].bonusStats[0]=23;
    g_InventoryRuntime.equipment[2].tailData[0]=0x21;
    g_AyaSaveState.inventory_items[2]=0x102;
    assert(pe_dismantle_list_count()==1);*pe_dismantle_choice()=0;
    pe_dismantle_run();assert(g_AyaSaveState.inventory_items[2]==0x102);
    pe_dismantle_run();assert(!g_AyaSaveState.inventory_items[2] && pe_rebuild_count()==1);
    char msg[96];balance=junk;unsigned price=pe_rebuild_price_index(0);
    assert(pe_rebuild_index(0,msg,sizeof msg) && junk==balance-price && !pe_rebuild_count());
    ItemDataRecord *rebuilt=pe_cat_bag_record(2);
    assert(rebuilt && rebuilt->itemId==0x45 && rebuilt->kind==1 && rebuilt->bonusStats[0]==23 && rebuilt->tailCount==3);
    for(int i=0;i<10;++i)assert(rebuilt->tailData[i]==0);
    /* A damaged/imported catalog must not manufacture an invalid game record. */
    cat->rebuild_count=1;cat->rebuild[0]=(PeRebuildEntry){.id=0x45,.slots=255};
    balance=junk;ItemDataRecord saved=*rebuilt;
    assert(!pe_rebuild_index(0,msg,sizeof msg));
    assert(junk==balance && cat->rebuild_count==1 && !g_AyaSaveState.inventory_items[3]);
    assert(!memcmp(&saved,rebuilt,sizeof saved));
    puts("Catalog: dry run, groups, premium Tools, Bag protection, disabled parity and rebuild passed");
    return 0;
}
