#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pe_inventory.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_save.h"
#include "pe_inventory_service.h"
#include "pe1/inventory_sort.h"
#include "pe1/inventory_state.h"
#include "pe1/battle_cmd.h"

SaveRuntimeState g_SaveRuntimeState;
GameStateBlock g_GameStateBlock;
Pe1GameState g_GameState;
InventoryItemWorkspace g_InventoryItemWorkspace;
s16 *g_InvItemPtr, *g_InvActiveListOverride;
int g_InvSlotLimit, g_InvActiveListCount;
s16 g_InvActiveListItems[10];
u32 *g_InvSelectionBits, g_InvSelectionBitStorage[4];
int g_InvSelectionBitWords;
int g_InvOverrideSlotLimit, g_InvPackedListCount, g_MenuBattleEquipMode;
u32 g_InvStorageSelectionBits[4];
int g_MenuLayoutLocked, g_StatBaseTable[4];
s16 *g_InvActiveItemList;
extern const PeMod pe_mod_pe_stacks;
/* Unused equipment-transfer paths in the stock translation unit. */
int Inv_CheckFreeSlotCapacity(int n) { (void)n;assert(0);return 0; }
void Inv_CompactActiveListSlots(void) { assert(0); }
BattleCmdEntry *BattleCmd_AllocSlot(void) { assert(0);return NULL; }
void Inv_SetActiveList(int mode,void *slot) { (void)mode;(void)slot;assert(0); }
int Inv_GetBonusSlotCount(void) { assert(0);return 0; }
void Menu_CreateNotificationDialog(int a,int b) { (void)a;(void)b;assert(0); }
int Inv_TransferItemAlt2(int mask) { (void)mask;assert(0);return 0; }
int Inv_GetWayneListItemByIndex(int i) { return i; }
int Inv_IsSlotEquipped(int i) { (void)i;return 1; }
void Menu_SetSwapReturnFlag(void) {}
void Draw_StatePush(void) {}
void Draw_OffsetCursor(int x,int y) { (void)x;(void)y; }
void Draw_PrintNumberWidth3Unk(int n) { (void)n; }
void Draw_StatePop(void) {}
static ItemDataRecord s_base[256];
static int loaded = 1, sort_calls, *pocket_enabled, *stacks_enabled;
static PeMenuGet get_sort;
static PeMenuSet set_sort;
static PeMenuChoiceCount key_count;
static PeMenuPerFrame ticks[4];
static int tick_count;
static const PeKeysApi *keys;
static const PeStacksApi *stacks;
static struct { int id; PeHookFn fn; } hooks[24];
static int hook_count;
int pe_game_loaded(void) { return loaded; }
int pe_save_loaded(void) { return loaded; }
int pe_free_field(void) { return !(g_GameStateFlags & 0x2006u) && !(g_GameStateFlagsWord & 0xB600u); }   /* as host/pe_game.c */
int pe_in_battle(void) { return (g_GameStateFlags & 2) != 0; }
int Inv_GetAyaSlotLimit(void) { return 10; }
ItemDataRecord *Item_LookupBaseData(unsigned index) { return index < 255 ? &s_base[index+1] : NULL; }
ItemDataRecord *Inv_LookupData(int value) {
    if (value >= 0x100 && value < 0x180) return &g_InventoryRuntime.equipment[value-0x100];
    if (value >= 0x200 && value < 0x209) return &g_KeyItemRecords[value-0x200];
    return value > 0 && value <= 255 ? &s_base[value] : NULL;
}
ItemDataRecord *Inv_LookupActiveListData(int slot) {
    return slot >= 0 && slot < g_InvSlotLimit ? Inv_LookupData(g_InvItemPtr[slot]) : NULL;
}
int Inv_CountByValue(int id) {
    int n=0;
    for(int i=0;i<g_InvSlotLimit;++i) n+=g_InvItemPtr[i]==id;
    return n;
}
void Inv_RebuildSelectableMask(void) { ++sort_calls; pe_hook_dispatch(PE_HOOK_INVENTORY_REBUILD, NULL); }
void *Text_LookupTable8(unsigned index) { static u8 name[]={0x10,0xFF};(void)index;return name; }
void pe_log(const char *tag,const char *fmt,...) { (void)tag;(void)fmt; }
int pe_hook_add(int id,PeHookFn fn,void *u,int p,const char *m) {
    (void)u;(void)p;(void)m;hooks[hook_count].id=id;hooks[hook_count++].fn=fn;return 0;
}
/* As the host: records go back before the mods' load handlers and into the save after theirs. */
int pe_hook_dispatch(int id,void *ctx) {
    int handled=0;
    if(id==PE_HOOK_SAVE_READ) pe_save_on_read(ctx);
    if(id==PE_HOOK_NEW_GAME) pe_save_new_game();
    for(int i=0;i<hook_count;++i) if(hooks[i].id==id) handled|=hooks[i].fn(ctx,NULL);
    if(id==PE_HOOK_SAVE_WRITE) pe_save_on_write(ctx);
    return handled;
}
const char *pe_mod_current(void) { return "pe_qol"; }
int pe_mod_active(const char *m) { (void)m;return 1; }
void pe_mod_log(const char *m,const char *fmt,...) { (void)m;(void)fmt; }
void pe_toast(const char *t) { (void)t; }
const void *pe_service_get(const char *name,int version) {
    (void)version;
    if(!strcmp(name,"stacks")) return stacks;
    return NULL;
}
int pe_service_publish(const char *name,int version,const void *api,const char *mod) {
    (void)mod;assert(version==1);
    if(!strcmp(name,"keys")) { keys=api; return 0; }
    if(!strcmp(name,"stacks")) { stacks=api; return 0; }
    assert(0);return -1;
}
int pe_menu_register_tick(PeMenuPerFrame fn) { ticks[tick_count++]=fn;return 0; }
int pe_menu_register_toggle(const char *p,const char *l,const char *d,int *v,PeMenuPerFrame t) {
    (void)p;(void)d;
    if(!strcmp(l,"Stacked items")) stacks_enabled=v;
    else pocket_enabled=v;
    return pe_menu_register_tick(t);
}
int pe_menu_register_action(const char *p,const char *l,const char *d,PeMenuAction a) {
    (void)p;(void)l;(void)d;(void)a;return 0;
}
int pe_menu_register_dynamic_choice(const char *p,const char *l,const char *d,int *v,unsigned max,
        PeMenuChoiceCount c,PeMenuChoiceName n) {
    (void)p;(void)l;(void)d;(void)v;(void)max;(void)n;key_count=c;return 0;
}
int pe_menu_register_info(const char *p,const char *l,const char *d,PeMenuText t) {
    (void)p;(void)l;(void)d;(void)t;return 0;
}
int pe_menu_register_live_choice(const char *p,const char *l,const char *d,const char *const *names,
        unsigned count,PeMenuGet get,PeMenuSet set) {
    (void)p;(void)l;(void)d;(void)names;assert(count==2);get_sort=get;set_sort=set;return 0;
}
static void reset(void) {
    PeHookNewGame c={PE_NEW_GAME_TITLE};pe_hook_dispatch(PE_HOOK_NEW_GAME,&c);
    memset(&g_SaveRuntimeState,0,sizeof g_SaveRuntimeState);
    g_AyaSaveState.equipped_weapon_slot=0;g_AyaSaveState.equipped_armor_slot=-1;
    g_GameStateFlags=0;g_GameStateFlagsWord=0;loaded=1;*pocket_enabled=1;
    g_InvItemPtr=g_AyaInventoryItems;g_InvSlotLimit=10;g_InvActiveListOverride=NULL;
    g_InvSortRankTable=g_InvSortRankTableA;g_InvSortTypeRankTable=g_InvTypeRankTableA;
    g_InvWeaponSortStatIndex=g_InvAmmoSortStatIndex=0;
}
/* A record's bytes in a saved tail (pe_save.h layout), NULL if it isn't there. */
static const unsigned char *record(const unsigned char *card,const char *name,unsigned *size) {
    unsigned id=0x811C9DC5u;
    for(const char *c=name;*c;++c) id=(id^(unsigned char)*c)*0x01000193u;
    const unsigned char *t=card+0x1C90;
    if(memcmp(t,"PEMC",4)) return NULL;
    for(unsigned at=20,total=t[6]|t[7]<<8;at<total;) {   /* records short of the window */
        unsigned rid=pe_inventory_word(t+at),n=t[at+6]|t[at+7]<<8;
        if(rid==id){*size=n;return t+at+12;}
        at+=12+n;
    }
    return NULL;
}
static unsigned checksum(const unsigned char *p,unsigned n,unsigned seed) {
    for(unsigned i=0;i<n;++i)seed=seed*33+p[i];return seed;
}
static void old_block(unsigned char *card,unsigned at,const void *data,unsigned size,unsigned magic,unsigned seed) {
    unsigned header[]={magic,1,size,checksum(data,size,seed)};
    memcpy(card+at,header,16);memcpy(card+at+16,data,size);
}
int main(void) {
    unsigned char card[8192],bad[8192];s16 full[50];const unsigned char *saved;unsigned size=0;
    pe_save_install();pe_key_items_register("pe_qol");pe_bag_sort_register("pe_qol");pe_mod_pe_stacks.init();reset();
    assert(!get_sort());
    for(int i=1;i<256;++i){s_base[i].itemId=i;s_base[i].kind=10;}
    s_base[0x0B].kind=12;s_base[0x0C].kind=13;
    g_AyaInventoryItems[1]=0xC8;g_AyaInventoryItems[2]=0x38;g_AyaInventoryItems[3]=0xDD;
    ticks[0]();assert(!g_AyaInventoryItems[1]&&!g_AyaInventoryItems[2]&&g_AyaInventoryItems[3]==0xDD);
    for(int i=0;i<10;++i)g_AyaInventoryItems[i]=6;
    memcpy(full,g_AyaInventoryItems,sizeof full);
    int key=0xC8,result=0;int *args[]={&key,&result};
    assert(pe_hook_dispatch(PE_HOOK_ITEM_COUNT,args)&&result==1);
    assert(!memcmp(full,g_AyaInventoryItems,sizeof full)); /* never park equipment */
    PeHookInvCommand command={1102,0x38,0};assert(pe_hook_dispatch(PE_HOOK_INV_COMMAND,&command)&&command.result==1);
    command.command=1112;assert(pe_hook_dispatch(PE_HOOK_INV_COMMAND,&command)&&command.result==0);
    result=0;key=0x38;assert(!pe_hook_dispatch(PE_HOOK_ITEM_COUNT,args));
    int pickup=0xC9;assert(pe_hook_dispatch(PE_HOOK_ITEM_ADD,&pickup));assert(!memcmp(full,g_AyaInventoryItems,sizeof full));
    assert(keys->take(0xC9));assert(!keys->take(6)); /* unique keys, no ordinary items */
    g_InvActiveListOverride=g_InvActiveListItems;g_InvActiveListCount=2;g_InvActiveListItems[0]=0xCA;g_InvActiveListItems[1]=7;
    g_GameStateFlags=4;ticks[0]();assert(!g_InvActiveListItems[0]&&g_InvActiveListItems[1]==7);
    g_GameStateFlags=0;*pocket_enabled=0;ticks[0]();
    key=0xC8;result=0;assert(pe_hook_dispatch(PE_HOOK_ITEM_COUNT,args)&&result==1); /* disabled but still full */
    g_AyaInventoryItems[0]=0;ticks[0]();assert(g_AyaInventoryItems[0]==0xC8);
    memset(card,0xA5,sizeof card);pe_hook_dispatch(PE_HOOK_SAVE_WRITE,card);
    assert((saved=record(card,"pe_qol.keys",&size))&&size==68);
    assert(pe_inventory_word(card+0x1E90)==0x59454B50u&&!memcmp(card+0x1EA0,saved,68)); /* and the old PKEY block, for older builds */
    assert(card[0x1C8F]==0xA5); /* nothing before the tail is touched */
    reset();pe_hook_dispatch(PE_HOOK_SAVE_READ,card);assert(keys->take(0xCA));
    /* A recomp PKEY v1 block can be loaded byte-for-byte. */
    struct {u16 count,pad,ids[32];} old_keys={1,0,{0xD1}};
    memset(card,0,sizeof card);old_block(card,0x1E90,&old_keys,sizeof old_keys,0x59454B50u,0x6B657973u);
    pe_hook_dispatch(PE_HOOK_SAVE_READ,card);key=0xD1;result=0;
    assert(pe_hook_dispatch(PE_HOOK_ITEM_COUNT,args)&&result==1);
    memcpy(bad,card,sizeof bad);bad[0x1EA0]^=1;pe_hook_dispatch(PE_HOOK_SAVE_READ,bad);
    assert(!pe_hook_dispatch(PE_HOOK_ITEM_COUNT,args));
    reset();
    for (int id=0x38; id<=0x3E; ++id) assert(keys->take(id));
    for (int id=0xC8; id<=0xDC; ++id) assert(keys->take(id));
    assert(!keys->take(0xDD) && !keys->take(0x16));
    assert(key_count()==28);
    assert(keys->take(0xC8) && key_count()==28); /* duplicate ownership cannot grow */
    memset(card,0,sizeof card);pe_hook_dispatch(PE_HOOK_SAVE_WRITE,card);
    assert((saved=record(card,"pe_qol.keys",&size))&&saved[0]==28);
    memset(card,0,sizeof card);old_keys.ids[0]=0xDD;old_block(card,0x1E90,&old_keys,sizeof old_keys,0x59454B50u,0x6B657973u);
    pe_hook_dispatch(PE_HOOK_SAVE_READ,card);assert(key_count()==1); /* empty placeholder after invalid data */
    reset();
    /* Exercise the real native sorter, not an imitation. */
    g_InventoryRuntime.equipment[0].itemId=0x44;g_InventoryRuntime.equipment[0].kind=1;g_InventoryRuntime.equipment[0].baseStats[0]=10;
    g_InventoryRuntime.equipment[1].itemId=0x95;g_InventoryRuntime.equipment[1].kind=9;
    g_AyaInventoryItems[0]=6;g_AyaInventoryItems[1]=0x101;g_AyaInventoryItems[4]=0x100;
    g_WayneStorageItems[0]=99;
    g_AyaSaveState.equipped_weapon_slot=4;g_AyaSaveState.equipped_armor_slot=1;
    g_GameStateFlags=4;Inv_SortInventoryByMode(1,0);assert(get_sort());
    assert(g_AyaInventoryItems[g_AyaSaveState.equipped_weapon_slot]==0x100);
    assert(g_AyaInventoryItems[g_AyaSaveState.equipped_armor_slot]==0x101);
    ticks[1]();assert(get_sort()); /* native sort isn't mistaken for manual movement */
    g_GameStateFlags=0;g_AyaInventoryItems[9]=7;int before=sort_calls;ticks[1]();assert(sort_calls==before+1);
    assert(g_AyaInventoryItems[g_AyaSaveState.equipped_weapon_slot]==0x100);
    assert(g_AyaInventoryItems[g_AyaSaveState.equipped_armor_slot]==0x101);
    before=sort_calls;ticks[1]();assert(sort_calls==before); /* unchanged bag */
    g_GameStateFlags=2;g_AyaInventoryItems[9]=8;ticks[1]();assert(sort_calls==before);
    g_GameStateFlags=0;g_GameStateFlagsWord=0x8000;ticks[1]();assert(sort_calls==before);
    g_GameStateFlagsWord=0;ticks[1]();assert(sort_calls==before+1);
    g_GameStateFlags=4;ticks[1]();s16 temp=g_AyaInventoryItems[0];g_AyaInventoryItems[0]=g_AyaInventoryItems[1];g_AyaInventoryItems[1]=temp;
    ticks[1]();assert(!get_sort());
    g_GameStateFlags=0;assert(set_sort(1));ticks[1]();assert(get_sort());
    memset(card,0,sizeof card);pe_hook_dispatch(PE_HOOK_SAVE_WRITE,card);
    assert((saved=record(card,"pe_qol.sort",&size))&&size==24);
    assert(pe_inventory_word(card+0x1EF0)==0x54524F53u&&!memcmp(card+0x1F00,saved,24)); /* and the old SORT block */
    assert(pe_inventory_word(saved+4)==0x800923F8u); /* original table ID, never a host pointer */
    struct {uint32_t mode,rank,type,weapon,armor,hidden;} old_sort={1,0x80092428u,0x80092468u,2,1,0};
    memset(card,0,sizeof card);old_block(card,0x1EF0,&old_sort,sizeof old_sort,0x54524F53u,0x736F7274u);
    pe_hook_dispatch(PE_HOOK_SAVE_READ,card);assert(get_sort());ticks[1]();
    assert(g_InvSortRankTable==g_InvSortRankTableC&&g_InvSortTypeRankTable==g_InvTypeRankTableB);
    assert(g_InvWeaponSortStatIndex==2&&g_InvAmmoSortStatIndex==1);
    assert(g_WayneStorageItems[0]==99);
    memset(card,0,sizeof card);old_sort.rank=0x12345678;old_block(card,0x1EF0,&old_sort,sizeof old_sort,0x54524F53u,0x736F7274u);
    pe_hook_dispatch(PE_HOOK_SAVE_READ,card);assert(!get_sort());
    reset();key=0xD1;assert(!pe_hook_dispatch(PE_HOOK_ITEM_COUNT,args));
    /* Issue #49: Tool & Super Tool transfers consume 1 copy off a stack instead of wiping the stack. */
    reset();
    *stacks_enabled=1;
    g_InventoryRuntime.tracked[0]=1;g_InventoryRuntime.tracked[2]=-1;
    for(int i=2;i<9;++i) g_AyaInventoryItems[i]=0x0B; /* 7 Tools in bag */
    for(int i=0;i<4;++i) g_WayneStorageItems[i]=0x0C; /* 4 Super Tools in storage */
    g_MenuLayoutLocked=1;
    g_StatBaseTable[0]=1;g_StatBaseTable[1]=0;g_StatBaseTable[2]=0;g_StatBaseTable[3]=1;
    Inv_RebuildSelectableMask();
    assert(g_AyaInventoryItems[2]==0x0B && !g_AyaInventoryItems[3]);
    assert(stacks->count(0,2)==7 && stacks->total(0,0x0B)==7 && g_StatBaseTable[0]==7);
    assert(g_WayneStorageItems[0]==0x0C && !g_WayneStorageItems[1]);
    assert(stacks->count(1,0)==4 && stacks->total(1,0x0C)==4 && g_StatBaseTable[3]==4);
    assert(g_StatBaseTable[1]==0 && g_StatBaseTable[2]==0);
    for(int left=6;left>=0;--left) {
        g_InventoryRuntime.equipment[0]=(ItemDataRecord){.iconId=1,.itemId=0x44,.kind=1,.bonusStats={5,0,0}};
        g_InventoryRuntime.equipment[1]=(ItemDataRecord){.iconId=1,.itemId=0x45,.kind=1};
        g_AyaInventoryItems[0]=0x100;g_AyaInventoryItems[1]=0x101;
        g_InvItemPtr=g_AyaInventoryItems;
        Inv_RememberSelection(0,0);Inv_RememberSelection(1,1);
        Inv_TransferItemBetweenLists(0,-1,0,g_StatBaseTable[0]);
        Inv_RebuildSelectableMask();
        assert(g_InventoryRuntime.equipment[1].bonusStats[0]==5);
        assert(stacks->total(0,0x0B)==left);
        if(left>0) {
            assert(g_AyaInventoryItems[2]==0x0B && stacks->count(0,2)==left && g_StatBaseTable[0]==left);
        } else {
            assert(g_AyaInventoryItems[2]==0);
        }
    }
    /* Super Tool from Wayne's crate (flags = 3: Super Tool + storage). */
    g_InventoryRuntime.equipment[0]=(ItemDataRecord){.iconId=1,.itemId=0x44,.kind=1,.bonusStats={0,7,0}};
    g_InventoryRuntime.equipment[1]=(ItemDataRecord){.iconId=1,.itemId=0x45,.kind=1};
    g_AyaInventoryItems[0]=0x100;g_AyaInventoryItems[1]=0x101;
    g_InvItemPtr=g_AyaInventoryItems;
    Inv_RememberSelection(0,0);Inv_RememberSelection(1,1);
    Inv_TransferItemBetweenLists(0,-1,3,g_StatBaseTable[3]);
    Inv_RebuildSelectableMask();
    assert(g_InventoryRuntime.equipment[1].bonusStats[1]==7 && g_AyaInventoryItems[0]==0x100);
    assert(g_WayneStorageItems[0]==0x0C && stacks->count(1,0)==3 && g_StatBaseTable[3]==3);
    g_MenuLayoutLocked=0;
    puts("Passed native inventory tests: pocket/full-bag story counts and consumption, disabled-pocket draining, pickup/reward handling, recomp-compatible saves, checksum/ID validation, real native auto-sort, equipped items, manual override, battle/script guards, and stacked Tool/Super Tool consumption.");
    return 0;
}
