#include "pe1/script_queue.h"
#include "pe1/game_timers.h"
#include "pe1/game_state.h"
#include "pe1/draw_wipe_rect.h"
#include "pe1/save.h"
#include "pe1/menu_queue.h"
#include "pe1/textbox.h"
#include "pe1/text.h"
#include "pe1/menu_flags.h"
#include "pe1/memcard.h"
#include "pe1/menu_inventory.h"
#include "pe1/menu_inventory_root.h"
#include "pe1/menu_dialog.h"
#include "pe1/menu_context_help.h"
void Menu_FullInit(void);
void MemCard_ClearDelayedCallback(void);
int MemCard_HasDelayedCallback(void);
#include "pe1/aya.h"
#include "pe1/resident_tables.h"
#include "pe1/cdrom.h"
#include "pe1/battle.h"
#include "pe1/battle_update.h"
#include "pe1/battle_cmd.h"
#include "pe1/player_entity.h"
#include "pe1/stat_modifiers.h"
#include "pe1/inventory.h"
#include "pe1/inventory_slots.h"
#include "pe1/inventory_state.h"
#include "pe1/inventory_sort.h"
#include "pe1/menu_bonus_stats.h"
#include "pe1/menu_equipment.h"
#include "pe1/weapon_list_output.h"
#include "pe1/battle_modifiers.h"
#include "pe1/battle_saved_state.h"
#include "pe1/menu_background.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
int Aya_StatBinarySearch(int value,int *table);
int Aya_StatOverflowQuery(int kind,int value);
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)

static int original_item_messages(void)
{
    int saved_window=g_SaveMetadataWindowIndex;u8 saved_buffer[32];memcpy(saved_buffer,g_InvItemMessageBuffer,32);
    ResidentDataPack saved_pack=g_ResidentDataPack;
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0,maximum=0;
    const int modes[]={0,1,2,3,INT32_MAX};
    for(int item=0;item<256;++item)for(unsigned mode=0;mode<5;++mode)for(unsigned window=0;window<2;++window){
        g_SaveMetadataWindowIndex=window;memset(g_InvItemMessageBuffer,0xA5,32);
        CHECK(Inv_GetItemEffectData(item,modes[mode])==g_InvItemMessageBuffer);
        unsigned length=0;while(length<32 && g_InvItemMessageBuffer[length]!=255)++length;
        CHECK(length<32);if(length+1>maximum)maximum=length+1;
        for(unsigned i=0;i<32;++i)hash=(hash^g_InvItemMessageBuffer[i])*UINT64_C(1099511628211);
        ++cases;
    }
    CHECK(hash==UINT64_C(0x443a865aea5e972f) && maximum==26);
    const int invalid_items[]={-1,INT32_MIN,INT32_MAX,256};
    for(unsigned i=0;i<4;++i)for(unsigned window=0;window<2;++window){
        g_SaveMetadataWindowIndex=window;memset(g_InvItemMessageBuffer,0xA5,32);
        CHECK(Inv_GetItemEffectData(invalid_items[i],0)==g_InvItemMessageBuffer && g_InvItemMessageBuffer[0]==255);
        CHECK(Inv_GetItemEffectData(1,INT32_MIN)==g_InvItemMessageBuffer && g_InvItemMessageBuffer[0]==255);
    }
    /* Controlled tables exercise output boundaries without replacing lookups. */
    memset(&g_ResidentDataPack,0,sizeof(g_ResidentDataPack));g_ResidentDataPack.directory.text=0x30;
    TextTableDirectory *directory=(TextTableDirectory *)(g_ResidentDataPack.bytes+0x30);
    directory->header.tableOffsets[0]=0x20;directory->header.tableOffsets[1]=0xD0;
    TextOffsetTable *general=(TextOffsetTable *)(g_ResidentDataPack.bytes+0x50);
    TextOffsetTable *names=(TextOffsetTable *)(g_ResidentDataPack.bytes+0x100);
    general->header.count=20;names->header.count=1;names->header.offsets[0]=0x300;
    for(unsigned i=16;i<20;++i)general->header.offsets[i]=0x4B0;
    const unsigned name_lengths[]={0,1,28,29,30,31,32,33,64},suffix_lengths[]={0,1,2,30,31,32,64};
    for(unsigned n=0;n<9;++n)for(unsigned suffix=0;suffix<7;++suffix)for(unsigned window=0;window<2;++window){
        unsigned a=name_lengths[n],b=suffix_lengths[suffix];
        memset(g_ResidentDataPack.bytes+0x400,0x11,a);g_ResidentDataPack.bytes[0x400+a]=255;
        memset(g_ResidentDataPack.bytes+0x500,0x22,b);g_ResidentDataPack.bytes[0x500+b]=255;
        g_SaveMetadataWindowIndex=window;memset(g_InvItemMessageBuffer,0xA5,32);
        CHECK(Inv_GetItemEffectData(1,0)==g_InvItemMessageBuffer);
        if(a+b+1<=32){
            for(unsigned i=0;i<a+b;++i)CHECK(g_InvItemMessageBuffer[i]==(window?(i<b?0x22:0x11):(i<a?0x11:0x22)));
            CHECK(g_InvItemMessageBuffer[a+b]==255);
            for(unsigned i=a+b+1;i<32;++i)CHECK(g_InvItemMessageBuffer[i]==0xA5);
        }else CHECK(g_InvItemMessageBuffer[0]==255);
    }
    g_ResidentDataPack.bytes[0x500]=0x22;g_ResidentDataPack.bytes[0x501]=255;
    unsigned end=sizeof(g_ResidentDataPack.bytes);names->header.offsets[0]=end-8-0x100;
    memset(g_ResidentDataPack.bytes+end-8,0x11,8);
    for(unsigned window=0;window<2;++window){g_SaveMetadataWindowIndex=window;
        CHECK(Inv_GetItemEffectData(1,0)==g_InvItemMessageBuffer && g_InvItemMessageBuffer[0]==255);}
    g_ResidentDataPack.bytes[end-1]=255;
    for(unsigned window=0;window<2;++window){g_SaveMetadataWindowIndex=window;
        CHECK(Inv_GetItemEffectData(1,0)==g_InvItemMessageBuffer && g_InvItemMessageBuffer[8]==255);}
    g_ResidentDataPack.bytes[end-1]=0x11;
    names->header.offsets[0]=end-0x100;
    CHECK(Inv_GetItemEffectData(1,0)==g_InvItemMessageBuffer && g_InvItemMessageBuffer[0]==255);
    general->header.offsets[19]=end-8-0x50;
    CHECK(Inv_GetItemEffectData(0,0)==g_InvItemMessageBuffer && g_InvItemMessageBuffer[0]==255);
    general->header.count=18;
    CHECK(Inv_GetItemEffectData(1,2)==g_InvItemMessageBuffer && g_InvItemMessageBuffer[0]==255);
    g_ResidentDataPack=saved_pack;g_SaveMetadataWindowIndex=saved_window;memcpy(g_InvItemMessageBuffer,saved_buffer,32);
    printf("Original item messages: %u USA MIPS buffers, all 255 authored names, prefix/suffix order, longest %u bytes, full 32-byte output and malformed/boundary guards passed\n",cases,maximum);
    return 0;
}

static int pending_command_stack(void)
{
    ItemDataRecord records[3] = {0};
    BattleCmd_InitTableCursor();
    for (unsigned i = 0; i < 12; ++i) {
        BattleCmdEntry *entry = BattleCmd_AllocSlot();
        CHECK(entry == g_BattleCmdStack + (i < 4 ? i : 3));
        entry->header.word = i;
        entry->payload.ammo_restore.item_data0 = &records[0];
        entry->payload.ammo_restore.item_data1 = &records[1];
        entry->payload.ammo_restore.item_data2 = &records[2];
        entry->payload.ammo_restore.ammo2 = 100 + i;
        unsigned count = i < 4 ? i + 1 : 4;
        CHECK(g_BattleCmdStackTop == g_BattleCmdStack + count);
        for (unsigned j = 0; j < count; ++j) {
            CHECK(g_BattleCmdStack[j].header.word == i + 1 - count + j);
            CHECK(g_BattleCmdStack[j].payload.ammo_restore.item_data0 == &records[0]);
            CHECK(g_BattleCmdStack[j].payload.ammo_restore.item_data1 == &records[1]);
            CHECK(g_BattleCmdStack[j].payload.ammo_restore.item_data2 == &records[2]);
            CHECK(g_BattleCmdStack[j].payload.ammo_restore.ammo2 == (int)(101 + i - count + j));
        }
    }
    BattleCmd_InitTableCursor();
    CHECK(g_BattleCmdStackTop == g_BattleCmdStack);
    return 0;
}

static int menu_fade_parameters(void)
{
    for (int initial = 0; initial < 256; ++initial) {
        for (int threshold = 0; threshold < 256; ++threshold) {
            unsigned char expected[16] = {0};
            int count = 1;
            while (count < 16 && expected[count - 1] < threshold) {
                expected[count] = (initial * 256 + (256 - initial) * expected[count - 1]) / 256;
                ++count;
            }
            Menu_ComputeGammaLut(initial, threshold);
            CHECK(g_MenuSaveBgFadeLutLen == count);
            CHECK(!memcmp(expected, g_MenuSaveBgFadeLut, (size_t)count));
        }
    }
    for (int value = -1024; value <= 1024; ++value) {
        Menu_ClampRange(value);
        int expected = (unsigned)value % 512;
        CHECK(g_MenuItemContextFlag == (expected ? expected : 511));
    }
    Menu_SaveBgInitFade();
    return 0;
}

extern DRAWENV g_RenderDrawEnvArray[2];

static int menu_fade_pixels(void)
{
    static u16 source[8192], destination[8192];
    u16 *oldSource = g_GameState.save_background_source;
    u16 *oldDestination = g_GameState.save_background_destination;
    g_GameState.save_background_source = source;
    g_GameState.save_background_destination = destination;
    Menu_SaveBgInitFade();
    g_MenuSaveBgFadeHeight = 32;
    const int tints[] = {0, 72, 255};
    for (unsigned tint = 0; tint < 3; ++tint) {
        g_MenuSaveBgFadeTint = tints[tint];
        for (int frame = 0; frame < g_MenuSaveBgFadeLutLen; ++frame) {
            g_MenuSaveBgFadeStep = 0;
            g_MenuSaveBgFadeIndex = frame;
            unsigned level = g_MenuSaveBgFadeLut[frame];
            for (unsigned block = 0; block < 8; ++block) {
                for (unsigned i = 0; i < 8192; ++i) source[i] = block * 8192 + i;
                Menu_SaveBgApplyFadeStep();
                for (unsigned i = 0; i < 8192; ++i) {
                    unsigned pixel = source[i], expected = pixel & 0x8000;
                    if (pixel) {
                        for (unsigned channel = 0; channel < 3; ++channel) {
                            unsigned component = (pixel >> (channel * 5)) & 31;
                            unsigned scaled = (component * (65536 - level * 256) +
                                level * tints[tint] * (32 - component)) / 65536;
                            expected |= scaled << (channel * 5);
                        }
                    }
                    CHECK(destination[i] == expected);
                }
                unsigned shade = level * tints[tint] / 256;
                for (unsigned slot = 0; slot < 2; ++slot) {
                    CHECK(g_RenderDrawEnvArray[slot].r0 == shade);
                    CHECK(g_RenderDrawEnvArray[slot].g0 == shade);
                    CHECK(g_RenderDrawEnvArray[slot].b0 == shade);
                }
            }
        }
    }
    g_MenuSaveBgFadeStep = 1;
    Menu_SaveBgApplyFadeStep();
    CHECK(g_MenuSaveBgFadeState == 7 && g_MenuSaveBgFadeStep == 0);
    g_MenuSaveBgFadeIndex = 0;
    g_MenuSaveBgFadeStep = -1;
    Menu_SaveBgApplyFadeStep();
    CHECK(g_MenuSaveBgFadeState == 0 && g_MenuSaveBgFadeIndex == 0);
    CHECK(!memcmp(source, destination, sizeof(source)));
    g_GameState.save_background_source = oldSource;
    g_GameState.save_background_destination = oldDestination;
    Menu_SaveBgInitFade();
    return 0;
}

static unsigned armor_third(unsigned value)
{
    if(value>999)value=999;
    if(value>=85)return (value*249/208+402)/10;
    unsigned root=0; while((root+1)*(root+1)<=value*3000)++root;
    return root/10;
}
static int new_game_inventory(void)
{
    for(unsigned carry=0;carry<2;++carry) {
        memset(&g_PlayerInventoryState,0,sizeof(g_PlayerInventoryState));
        memset(g_InventoryRuntime.storage,0xa5,sizeof(g_InventoryRuntime.storage));
        memset(g_InventoryRuntime.specialStorage,0xa5,sizeof(g_InventoryRuntime.specialStorage));
        s16 override[2]={1,6};
        Inv_SetActiveListOverride(override,2);
        Inv_BuildItemGridFromCategory();
        CHECK(g_InvActiveListOverride==NULL);
        for(unsigned i=0;i<100;++i)CHECK(g_InventoryRuntime.storage[i]==0);
        for(unsigned i=0;i<82;++i)CHECK(g_InventoryRuntime.specialStorage[i]==0);
        for(unsigned i=0;i<9;++i) {
            ItemDataRecord expected=*Item_LookupBaseData(g_InvCategoryBaseItemId-1+i%3);
            expected.baseStats[2]=0; expected.bonusStats[2]=999;
            CHECK(!memcmp(&expected,&g_InvSpecialItemRecords[i],sizeof(expected)));
        }
        if(carry) {
            g_InventoryRuntime.equipment[120]=*Item_LookupBaseData(0x43);
            g_InventoryRuntime.equipment[121]=*Item_LookupBaseData(0x95);
            g_InventoryRuntime.equipment[120].flags|=16;
            g_InventoryRuntime.equipment[121].flags|=16;
            g_InventoryRuntime.specialStorage[0]=0x178;
            g_InventoryRuntime.specialStorage[1]=0x179;
        }
        Inv_InitNewGameInventory();
        AyaLevelStats *stats=Aya_LookupLevelStats(0);
        CHECK(g_AyaSaveState.current_hp==stats->hp && g_AyaSaveState.max_hp==stats->hp);
        CHECK(g_AyaSaveState.inventory_slot_count==stats->inventoryCapacity);
        for(unsigned i=0;i<7;++i)
            CHECK(g_AyaSaveState.stats.levels[i]==*(u16 *)Aya_GetStatGrowthTable(i));
        CHECK(g_AyaSaveState.parasite_spell_flags==1 && g_AyaSaveState.level==0 && g_AyaSaveState.total_exp==0);
        CHECK(g_AyaSaveState.equipped_weapon_slot==0 && g_AyaSaveState.equipped_armor_slot==1);
        CHECK(g_AyaEquippedWeaponSlot==&g_AyaSaveState.equipped_weapon_slot);
        CHECK(g_AyaEquippedArmorSlot==&g_AyaSaveState.equipped_armor_slot);
        CHECK(g_WayneStorageItems==g_InventoryRuntime.storage);
        for(unsigned i=0;i<128;++i) {
            CHECK(Inv_LookupItemRecord(0x100+i)==&g_InventoryRuntime.equipment[i]);
            CHECK(Inv_LookupItemRecordScaled(0x100+i)==&g_InventoryRuntime.equipment[i]);
        }
        for(unsigned i=0;i<9;++i) {
            CHECK(Inv_LookupItemRecord(0x200+i)==&g_KeyItemRecords[i]);
            CHECK(Inv_LookupItemRecordScaled(0x200+i)==&g_KeyItemRecords[i]);
        }
        const int invalid_ids[]={-1,0,0x180,0x1ff,0x209,0x10000};
        for(unsigned i=0;i<sizeof(invalid_ids)/sizeof(*invalid_ids);++i) {
            CHECK(!Inv_LookupItemRecord(invalid_ids[i]));
            CHECK(!Inv_LookupItemRecordScaled(invalid_ids[i]));
        }
        /* Both saved choices must retain their own list and selection. */
        Inv_SelectActiveList(0);Inv_RememberSelection(0,1);
        Inv_SetActiveListOverride(g_WayneStorageItems,100);Inv_SelectActiveList(1);
        Inv_RememberSelection(1,73);
        CHECK(g_InvSavedSelectionIndex0==1 && g_InvSavedSelectionIndex1==73);
        CHECK(!g_InvSavedSelectionFromStorage0 && g_InvSavedSelectionFromStorage1);
        Inv_RememberSelection(2,99);
        CHECK(Inv_RestoreSelection(0)==1 && g_InvItemPtr==g_AyaInventoryItems);
        CHECK(g_InvSelectionBitWords==2 && g_InvSlotLimit==Inv_GetAyaSlotLimit());
        CHECK(Inv_RestoreSelection(1)==73 && g_InvItemPtr==g_WayneStorageItems);
        CHECK(g_InvSelectionBitWords==4 && g_InvSlotLimit==100);
        CHECK(Inv_RestoreSelection(2)==-1 && g_InvItemPtr==g_WayneStorageItems);
        Inv_SetActiveListOverride(NULL,0);
        CHECK(Inv_RestoreSelection(1)==73 && g_InvItemPtr==g_AyaInventoryItems);
        CHECK(g_InvSelectionBitWords==2 && g_InvSlotLimit==Inv_GetAyaSlotLimit());
        const s16 expected[]={0x100,0x101,0x102,0x200,6};
        CHECK(!memcmp(g_AyaSaveState.inventory_items,expected,sizeof(expected)));
        CHECK(g_InvSpecialItemRecords[0].ammo==Item_LookupBaseData(0)->ammo);
        CHECK(!g_InvSpecialItemRecords[1].ammo && !g_InvSpecialItemRecords[2].ammo);
        CHECK(g_MenuItemContextFlag==61 && g_AyaSaveState.menu_clamp_value==61);
        CHECK(g_MenuSaveBgFadeState==0 && g_MenuSaveBgFadeStep==0 && g_MenuSaveBgFadeHeight==32 && g_MenuSaveBgFadeTint==72);
        unsigned value=0, length=0;
        for(unsigned i=0;i<16;++i) {
            CHECK(g_MenuSaveBgFadeLut[i]==value);
            length=i+1;
            if(value>=255)break;
            value=(144*256+112*value)/256;
        }
        CHECK(g_MenuSaveBgFadeLutLen==length);
        if(carry) {
            CHECK(g_AyaSaveState.inventory_items[5]==0x178 && g_AyaSaveState.inventory_items[6]==0x179);
            CHECK(!g_InventoryRuntime.specialStorage[0] && !g_InventoryRuntime.specialStorage[1]);
        }
    }
    ItemDataRecord saved_equipment[40],saved_keys[9];
    memcpy(saved_equipment,g_InventoryRuntime.equipment,sizeof(saved_equipment));
    memcpy(saved_keys,g_KeyItemRecords,sizeof(saved_keys));
    for(unsigned i=0;i<40;++i) {
        memset(&g_InventoryRuntime.equipment[i],0,sizeof(ItemDataRecord));
        g_InventoryRuntime.equipment[i].kind=i%22;
        g_InventoryRuntime.equipment[i].tailCount=i%3;
    }
    for(unsigned i=0;i<9;++i) {
        memset(&g_KeyItemRecords[i],0,sizeof(ItemDataRecord));
        g_KeyItemRecords[i].kind=19+i%3;g_KeyItemRecords[i].tailCount=i%3;
    }
    s16 items[100];
    for(unsigned i=0;i<100;++i) {
        items[i]=i<40?0x100+i:i<49?0x200+i-40:i<89?i-48:0;
    }
    Inv_SetActiveListOverride(items,100);Inv_SelectActiveList(1);
    for(unsigned i=0;i<100;++i) {
        ItemDataRecord *record=i<40?&g_InventoryRuntime.equipment[i]:
            i<49?&g_KeyItemRecords[i-40]:i<89?Item_LookupBaseData(i-49):NULL;
        CHECK(Inv_FindIndexByData(record)==(record?(int)i:89));
        CHECK(Inv_GetActiveListItemType(i)==(record?record->kind:0));
        if(i<40) {
            u8 flags=record->flags;
            record->flags|=ITEM_DATA_FLAG_GENERIC_DESCRIPTION;
            CHECK(Inv_LookupActiveListDisplayData(i)==g_CursorRenderMetadataWindows[record->kind==ITEM_KIND_ARMOR].text);
            record->flags=flags;
        } else if(i<49) {
            CHECK(Inv_LookupActiveListDisplayData(i)==Text_LookupTable8(g_InvCategoryBaseItemId+i-41));
        } else if(i<89) {
            CHECK(Inv_LookupActiveListDisplayData(i)==Text_LookupTable8(i-49));
        } else CHECK(!Inv_LookupActiveListDisplayData(i));
    }
    CHECK(!Inv_GetActiveListItemType(-1) && !Inv_GetActiveListItemType(100));
    /* Old BEEB0 is an interior address of the equipment pool, not a
     * separate remap table. Count authored IDs through canonical records. */
    for(unsigned i=0;i<40;++i)g_InventoryRuntime.equipment[i].itemId=(i%7)+1;
    u8 last_item=g_InventoryRuntime.equipment[127].itemId;
    g_InventoryRuntime.equipment[127].itemId=9;items[98]=0x17f;items[99]=-1;
    for(int id=-1;id<0x20a;++id) {
        int expected=0;
        if(id>=g_InvCategoryBaseItemId && id<g_InvCategoryBaseItemId+3)
            expected=g_KeyItemRecords[id-g_InvCategoryBaseItemId].ammo;
        else for(unsigned i=0;i<100;++i) {
            int value=items[i];
            if(value>=0x100 && value<0x180)value=g_InventoryRuntime.equipment[value-0x100].itemId;
            expected+=value==id;
        }
        CHECK(Inv_CountByValue(id)==expected);
    }
    g_InventoryRuntime.equipment[127].itemId=last_item;items[98]=items[99]=0;
    uint64_t filter_hash=UINT64_C(14695981039346656037);
    for(unsigned kind=0;kind<32;++kind) {
        int mask=(int)(1u<<kind);
        for(int excluded=-1;excluded<100;excluded+=25) {
            if(excluded<0)Inv_BuildFilteredPackedList(mask);
            else Inv_BuildFilteredPackedListExcluding(mask,excluded);
            CHECK(g_InvPackedListCount>=0 && g_InvPackedListCount<=100 && !Inv_GetPackedListCursor());
            unsigned used=g_InvPackedListCount;
            for(unsigned byte=0;byte<4;++byte)
                filter_hash=(filter_hash^((used>>(8*byte))&255))*UINT64_C(1099511628211);
            for(unsigned i=0;i<used;++i) {
                unsigned slot=Inv_GetPackedListItem(i);CHECK(slot<100);
                for(unsigned byte=0;byte<2;++byte)
                    filter_hash=(filter_hash^((slot>>(8*byte))&255))*UINT64_C(1099511628211);
            }
            CHECK(!Inv_GetPackedListItem(-1) && !Inv_GetPackedListItem(used));
        }
    }
    /* All 160 outputs from the original USA 800542A0/800543CC in Unicorn
     * MIPS, with real resident base items and these same native record fields. */
    CHECK(filter_hash==UINT64_C(0x7d954f5aec037d21));
    Inv_BuildFilteredPackedList(-1);CHECK(g_InvPackedListCount==100);
    for(unsigned i=0;i<100;++i)CHECK(Inv_GetPackedListItem(i)==(int)i);
    /* The original removal routine must preserve a native list pointer and
     * free only the equipment record belonging to a removed equipment ID. */
    for(unsigned i=0;i<40;++i) {
        g_InventoryRuntime.equipment[i].iconId=0xA5;
        CHECK(Inv_RemoveActiveListItem(i)==(int)(0x100+i));
        CHECK(!items[i] && !g_InventoryRuntime.equipment[i].iconId);
    }
    for(unsigned i=40;i<100;++i) {
        int value=items[i];
        CHECK(Inv_RemoveActiveListItem(i)==value && !items[i]);
    }
    for(unsigned i=0;i<9;++i)CHECK(g_KeyItemRecords[i].kind==19+i%3 && g_KeyItemRecords[i].tailCount==i%3);
    CHECK(&g_InvBaseCapacity==&g_AyaSaveState.inventory_slot_count);
    memcpy(g_InventoryRuntime.equipment,saved_equipment,sizeof(saved_equipment));
    memcpy(g_KeyItemRecords,saved_keys,sizeof(saved_keys));
    CHECK(g_InvTransferItemList==g_InvPackedList+50);
    CHECK(g_InvItemArray==g_InventoryRuntime.specialStorage);
    for(unsigned special=0;special<2;++special) {
        memset(g_InventoryRuntime.specialStorage,0,sizeof(g_InventoryRuntime.specialStorage));
        CHECK(Inv_StepScrollDisplay2(special)==(special?80:100));
        CHECK(g_InvActiveItemList==(special?g_InventoryRuntime.specialStorage:g_InventoryRuntime.storage));
        CHECK(g_InvActiveListOverride==g_InvActiveItemList && g_InvOverrideSlotLimit==(special?80:100));
        const int ids[]={0x100,0x17f,0x200,0x208,1,0xff,0,0x180,0x209};
        for(unsigned i=0;i<sizeof(ids)/sizeof(*ids);++i) {
            g_InvActiveItemList[i]=ids[i];
            CHECK(Inv_LookupItemData(i)==Inv_LookupItemRecord(ids[i]));
        }
    }
    g_InvActiveListCount=10;
    for(unsigned i=0;i<10;++i) {
        g_InvActiveListItems[i]=0x40+i;CHECK(Menu_GetBattleCountEntry(i)==(int)(0x40+i));
    }
    CHECK(!Menu_GetBattleCountEntry(-1) && !Menu_GetBattleCountEntry(10));
    g_InvActiveListCount=0;
    Inv_SetActiveListOverride(NULL,0);Inv_SelectActiveList(0);
    puts("Original inventory: New Game/carryover, canonical records, descriptions, lookup and 100-slot category filters passed");
    return 0;
}
static int inventory_ammo_rollback(void)
{
    SaveRuntimeState runtime=g_SaveRuntimeState;InventoryItemWorkspace workspace=g_InventoryItemWorkspace;
    int packed_count=g_InvPackedListCount,packed_cursor=g_InvPackedListCursor;
    CHECK(g_AyaHpCurrent==&g_AyaSaveState.current_hp && g_AyaHpMax==&g_AyaSaveState.max_hp);
    CHECK((void *)g_AyaSaveTotalExp==&g_AyaSaveState.total_exp && g_AyaInventorySlotCount==&g_AyaSaveState.inventory_slot_count);
    CHECK(g_AyaParasiteSpellFlags==&g_AyaSaveState.parasite_spell_flags && g_AyaSaveLevel==&g_AyaSaveState.level);
    CHECK((void *)g_AyaBonusPoints==&g_AyaSaveState.bonus_points && g_AyaStatAgility==g_AyaSaveState.stats.levels);
    int bonus=g_InvBonusSlotCount;FieldActor *player=g_PlayerEntity;g_PlayerEntity=NULL;
    memset(&g_SaveRuntimeState,0,sizeof(g_SaveRuntimeState));memset(&g_InventoryItemWorkspace,0,sizeof(g_InventoryItemWorkspace));
    CHECK((u8 *)&g_InvFallbackAmmoRecord==(u8 *)g_InvPackedList+0xA8);
    CHECK(&g_InvFallbackAmmoRecord+1==g_KeyItemRecords);
    CHECK((u8 *)g_KeyItemRecords==(u8 *)g_InvPackedList+sizeof(g_InvPackedList));
    const unsigned ammo_values[]={0,1,30,999},capacities[]={0,30,1001};
    const int amounts[]={-1000,-31,-1,0,1,31,1000};
    uint64_t transfer_hash=UINT64_C(14695981039346656037);
    for(unsigned la=0;la<4;++la)for(unsigned ra=0;ra<4;++ra)
    for(unsigned lc=0;lc<3;++lc)for(unsigned rc=0;rc<3;++rc)
    for(unsigned lr=0;lr<2;++lr)for(unsigned rr=0;rr<2;++rr)for(unsigned amount=0;amount<7;++amount) {
        ItemDataRecord *records[]={&g_InvCompareSlotLeft,&g_InvCompareSlotRight};
        unsigned ammo[]={ammo_values[la],ammo_values[ra]},caps[]={capacities[lc],capacities[rc]},reserves[]={lr*17,rr*29};
        for(unsigned side=0;side<2;++side) {
            ItemDataRecord *record=records[side];memset(record,0,sizeof(*record));
            record->ammo=ammo[side];record->reserveAmmo=reserves[side];
            record->baseStats[2]=caps[side]<256?caps[side]:1;
            record->bonusStats[2]=caps[side]<256?0:caps[side]-1;
        }
        u32 result=Inv_SpendAmmo(amounts[amount]);
        for(unsigned byte=0;byte<4;++byte)transfer_hash=(transfer_hash^((result>>(byte*8))&255))*UINT64_C(1099511628211);
        for(unsigned side=0;side<2;++side)for(unsigned byte=0;byte<sizeof(ItemDataRecord);++byte)
            transfer_hash=(transfer_hash^((u8 *)records[side])[byte])*UINT64_C(1099511628211);
    }
    /* Full record images and status from 4,032 executions of USA 80056B24. */
    CHECK(transfer_hash==UINT64_C(0xb52387d83f546f0d));
    g_AyaSaveState.inventory_slot_count=2;g_InvBonusSlotCount=0;
    g_AyaSaveState.equipped_weapon_slot=0;g_AyaSaveState.equipped_armor_slot=-1;
    g_AyaInventoryItems[0]=0x100;g_AyaInventoryItems[1]=0x101;
    for(unsigned reserve=0;reserve<=7;reserve+=7) {
        ItemDataRecord *left=&g_InventoryRuntime.equipment[0],*right=&g_InventoryRuntime.equipment[1];
        memset(left,0,sizeof(*left));left->kind=1;left->baseStats[2]=30;left->ammo=10;
        *right=*left;right->ammo=5;
        g_KeyItemRecords[0].ammo=100;g_KeyItemRecords[0].bonusStats[2]=999;
        Inv_ResetActiveList();Inv_RememberSelection(0,0);Inv_RememberSelection(1,1);
        Inv_InitWayneStorage();CHECK(D_8009D084==D_800A1FE8);
        ItemDataRecord saved_left=*left,saved_right=*right;
        left->ammo=65535;right->kind=22;Inv_BuildStorageDisplay();
        CHECK(!memcmp(left,&saved_left,sizeof(*left)) && !memcmp(right,&saved_right,sizeof(*right)));
        Inv_BuildDisplayFromList(0,0,0,1);
        CHECK(g_InvCompareLeftItem==left && g_InvCompareRightItem==right);
        CHECK(Inv_SpendAmmo(4)==0 && g_InvCompareSlotLeft.ammo==6 && g_InvCompareSlotRight.ammo==9);
        CHECK(left->ammo==10 && right->ammo==5);
        g_InvCompareSlotLeft.reserveAmmo=reserve;g_InvCompareSlotLeft.tailData[10]=1;
        BattleCmd_InitTableCursor();Menu_ClearCommandResult();Inv_StepScrollDisplay();
        CHECK(left->ammo==6 && right->ammo==9 && g_KeyItemRecords[0].ammo==100+reserve);
        CHECK(g_BattleCmdStackTop==g_BattleCmdStack+1 && g_BattleCmdStack[0].header.word==4);
        CHECK(g_BattleCmdStack[0].payload.ammo_restore.item_data0==left &&
              g_BattleCmdStack[0].payload.ammo_restore.item_data1==right);
        CHECK(g_BattleCmdStack[0].payload.ammo_restore.item_data2==(reserve?&g_KeyItemRecords[0]:NULL));
        BattleCmd_UndoPending();
        CHECK(g_BattleCmdStackTop==g_BattleCmdStack && left->ammo==10 && right->ammo==5 && g_KeyItemRecords[0].ammo==100);
    }
    Inv_ResetActiveList();g_InvPackedListCount=1;g_InvPackedList[0]=1;
    g_InventoryRuntime.tracked[0]=0;BattleCmd_InitTableCursor();Menu_ClearCommandResult();
    Menu_OpenItemUsePanelAtIndex(0);
    CHECK(g_InventoryRuntime.tracked[0]==1 && g_BattleCmdStackTop==g_BattleCmdStack+1);
    CHECK(g_BattleCmdStack[0].header.word==2 &&
          g_BattleCmdStack[0].payload.equip_restore.item_data==&g_InventoryRuntime.equipment[0]);
    BattleCmd_UndoPending();CHECK(g_InventoryRuntime.tracked[0]==0 && g_BattleCmdStackTop==g_BattleCmdStack);
    g_SaveRuntimeState=runtime;g_InventoryItemWorkspace=workspace;g_InvBonusSlotCount=bonus;g_PlayerEntity=player;
    g_InvPackedListCount=packed_count;g_InvPackedListCursor=packed_cursor;
    Inv_ResetActiveList();
    puts("Original inventory snapshots and ammo comparison/commit/rollback passed with optional reserve pool");
    return 0;
}
static int inventory_sort_comparisons(void)
{
    ItemDataRecord equipment[40],keys[9];
    memcpy(equipment,g_InventoryRuntime.equipment,sizeof(equipment));memcpy(keys,g_KeyItemRecords,sizeof(keys));
    for(unsigned i=0;i<40;++i) {
        ItemDataRecord *record=&g_InventoryRuntime.equipment[i];memset(record,0,sizeof(*record));
        record->itemId=(i*7+3)%255;record->kind=i%22;record->bonusStats[0]=i%16;
    }
    for(unsigned i=0;i<9;++i) {
        ItemDataRecord *record=&g_KeyItemRecords[i];memset(record,0,sizeof(*record));
        record->itemId=150+i;record->kind=19+i%3;record->bonusStats[0]=i%16;
    }
    s16 ids[90];
    for(unsigned i=0;i<90;++i)ids[i]=i<40?0x100+i:i<49?0x200+i-40:i<89?i-48:0;
    u8 *ranks[]={g_InvSortRankTableA,g_InvSortRankTableB,g_InvSortRankTableC,g_InvSortRankTableD};
    u8 *types[]={g_InvTypeRankTableA,g_InvTypeRankTableB};
    uint64_t hash=UINT64_C(14695981039346656037);g_InvSortLookup=Inv_LookupData;
    for(unsigned rank=0;rank<4;++rank)for(unsigned type=0;type<2;++type) {
        g_InvSortRankTable=ranks[rank];g_InvSortTypeRankTable=types[type];
        for(unsigned left=0;left<90;++left)for(unsigned right=0;right<90;++right) {
            int result=Inv_CompareItemsForSort(&ids[left],&ids[right]);
            CHECK(result>=-1 && result<=1);
            for(unsigned byte=0;byte<4;++byte)hash=(hash^(((u32)result>>(byte*8))&255))*UINT64_C(1099511628211);
        }
    }
    /* 64,800 comparisons by the actual USA comparator 8005AFFC and lookup
     * 800532B4 in Unicorn MIPS, using the same records and real resident data. */
    CHECK(hash==UINT64_C(0x47d2f17ffb5d3535));
    s16 slots[2]={0,0x100},empty=0,valid=1;
    s16 *old_items=g_InvItemPtr;int old_limit=g_InvSlotLimit;
    g_InvItemPtr=slots;g_InvSlotLimit=2;g_InvSortLookup=Inv_LookupActiveListData;
    CHECK(Inv_CompareItemsForSort(&empty,&valid)==1);
    CHECK(Inv_CompareItemsForSort(&valid,&empty)==-1);
    CHECK(!Inv_CompareItemsForSort(&empty,&empty));
    g_InvItemPtr=old_items;g_InvSlotLimit=old_limit;g_InvSortLookup=Inv_LookupData;
    memcpy(g_InventoryRuntime.equipment,equipment,sizeof(equipment));memcpy(g_KeyItemRecords,keys,sizeof(keys));
    SaveRuntimeState before=g_SaveRuntimeState;
    int old_bonus=g_InvBonusSlotCount;g_InvBonusSlotCount=0;
    for(unsigned special=0;special<2;++special)for(unsigned mode=0;mode<(special?2u:1u);++mode)
    for(unsigned key=0;key<(special?3u:2u);++key) {
        g_SaveRuntimeState=before;
        s16 *store=special?g_InventoryRuntime.specialStorage:g_InventoryRuntime.storage;
        unsigned count=special?82:100;
        for(unsigned i=0;i<count;++i)store[i]=count-i;
        Inv_StepScrollDisplay2(special);
        unsigned expected[0x209]={0};
        for(unsigned i=0;i<count;++i)++expected[(unsigned)store[i]];
        int weapon=g_AyaInventoryItems[g_AyaSaveState.equipped_weapon_slot];
        int armor=g_AyaInventoryItems[g_AyaSaveState.equipped_armor_slot];
        g_InvWeaponSortStatIndex=g_InvAmmoSortStatIndex=0;
        if(special)Inv_SortInventoryItems(mode!=0,key);
        else Inv_SortContainerItems(key!=0);
        for(unsigned i=0;i<count;++i) {
            unsigned id=(u16)store[i];CHECK(id<0x209 && expected[id]);--expected[id];
        }
        for(unsigned i=0;i<0x209;++i)CHECK(!expected[i]);
        CHECK(g_AyaInventoryItems[g_AyaSaveState.equipped_weapon_slot]==weapon);
        CHECK(g_AyaInventoryItems[g_AyaSaveState.equipped_armor_slot]==armor);
        CHECK(g_InvTransferItemCount>=0 && g_InvTransferItemCount<=50);
        for(int i=0;i<g_InvTransferItemCount;++i)
            CHECK(g_InvTransferItemList[i]>=0 && g_InvTransferItemList[i]<g_AyaSaveState.inventory_slot_count);
    }
    /* Invalid serialized fields must not escape through sort-table indexes
     * or shifts. Keep the IDs intact; load validation rejects these saves. */
    for (unsigned kind = 24; kind <= 255; kind += kind == 24 ? 7 : kind == 31 ? 1 : 223) {
        memset(&g_SaveRuntimeState, 0, sizeof(g_SaveRuntimeState));
        g_AyaSaveState.inventory_slot_count = 10;
        g_AyaSaveState.equipped_weapon_slot = -128;
        g_AyaSaveState.equipped_armor_slot = 127;
        g_AyaInventoryItems[0] = 0x100; g_AyaInventoryItems[1] = 0x101;
        g_InventoryRuntime.equipment[0] = (ItemDataRecord){.iconId=1,.itemId=0x78,.kind=kind};
        g_InventoryRuntime.equipment[1] = (ItemDataRecord){.iconId=1,.itemId=0x44,.kind=1};
        CHECK(!Inv_ValidateSavedInventory(&g_SaveRuntimeState));
        Inv_SortInventoryByMode(1, 0);
        CHECK(g_AyaSaveState.equipped_weapon_slot == -1 && g_AyaSaveState.equipped_armor_slot == -1);
        unsigned seen[2] = {0};
        for (unsigned slot = 0; slot < 10; ++slot) {
            int id = g_AyaInventoryItems[slot];
            if (id == 0x100) ++seen[0]; else if (id == 0x101) ++seen[1]; else CHECK(!id);
        }
        CHECK(seen[0] == 1 && seen[1] == 1);
        s16 missing = 0x180, existing = 0x101;
        CHECK(Inv_CompareItemsByStatForSort(&missing, &existing) == 1);
        if (kind == 255) break;
    }
    g_SaveRuntimeState=before;g_InvBonusSlotCount=old_bonus;
    Inv_SetActiveListOverride(NULL,0);Inv_SelectActiveList(0);
    puts("Original inventory sorting: 64,800 USA MIPS comparisons, empty records and full inventory/storage sorting passed");
    return 0;
}
static int script_item_awards(void)
{
    static const u32 ids[]={0,1,2,3,6,9,27,63,64,68,149,150,255,256,383,384,
        511,512,520,521,32767,32768,0xffffffff,0x80000000};
    const int limits[]={0,1,10,50};
    PlayerInventoryState saved_player=g_PlayerInventoryState;
    InventoryItemWorkspace saved_workspace=g_InventoryItemWorkspace;
    s16 *saved_items=g_InvItemPtr;int saved_limit=g_InvSlotLimit,saved_bonus=g_InvBonusSlotCount;
    u32 *saved_selection=g_InvSelectionBits;int saved_words=g_InvSelectionBitWords;
    u32 saved_bits[2];memcpy(saved_bits,g_InvSelectionBitStorage,sizeof(saved_bits));
    uint64_t hash=UINT64_C(14695981039346656037);unsigned cases=0;
    CHECK(sizeof(PlayerInventoryState)==0x1224 && sizeof(InventoryItemWorkspace)==0x1e8);
    g_InvBonusSlotCount=0;
    for(unsigned item=0;item<sizeof(ids)/sizeof(ids[0]);++item)for(unsigned cap=0;cap<4;++cap)
    for(unsigned scenario=0;scenario<6;++scenario)for(unsigned alias=0;alias<2;++alias){
        int limit=limits[cap];s16 custom[50]={0};
        memset(&g_PlayerInventoryState,0,sizeof(g_PlayerInventoryState));
        memset(&g_InventoryItemWorkspace,0,sizeof(g_InventoryItemWorkspace));
        g_AyaSaveState.inventory_slot_count=limit;
        if(scenario>=1 && scenario<=3){
            for(unsigned slot=0;slot<50;++slot)g_AyaInventoryItems[slot]=6;
            if(scenario>=2 && limit)g_AyaInventoryItems[limit/2]=0;
        }
        if(scenario==3)for(unsigned record=0;record<128;++record)g_InventoryRuntime.equipment[record].iconId=1;
        if(scenario==4 && limit)g_AyaInventoryItems[0]=512;
        if(scenario==5){for(unsigned slot=0;slot<50;++slot)custom[slot]=6;if(limit)custom[limit/2]=0;}
        for(unsigned category=0;category<3;++category){
            g_KeyItemRecords[category].bonusStats[2]=999;
            g_KeyItemRecords[category].ammo=scenario==4?998:0;
        }
        g_InvItemPtr=scenario==5?custom:g_AyaInventoryItems;g_InvSlotLimit=limit;
        g_InvSelectionBits=NULL;g_InvSelectionBitWords=0;
        g_InvSelectionBitStorage[0]=0xdeadbeef;g_InvSelectionBitStorage[1]=0xa55a5aa5;
        int id=(s32)ids[item],output=(s32)0x98765432;int *args[]={&id,alias?&id:&output};
        u32 result=Script_GiveItem(args);
        CHECK(g_InvItemPtr==custom || g_InvItemPtr==g_AyaInventoryItems);
        CHECK(!g_InvSelectionBits || g_InvSelectionBits==g_InvSelectionBitStorage);
        const u32 words[]={result,(u32)id,(u32)output,g_InvItemPtr==custom,(u32)g_InvSlotLimit,
            g_InvSelectionBits!=NULL,(u32)g_InvSelectionBitWords};
        for(unsigned word=0;word<7;++word)for(unsigned byte=0;byte<4;++byte)
            hash=(hash^((words[word]>>(byte*8))&255))*UINT64_C(1099511628211);
        const void *blocks[]={&g_PlayerInventoryState,&g_InventoryItemWorkspace,custom,g_InvSelectionBitStorage};
        const size_t sizes[]={sizeof(g_PlayerInventoryState),sizeof(g_InventoryItemWorkspace),sizeof(custom),8};
        for(unsigned block=0;block<4;++block)for(size_t byte=0;byte<sizes[block];++byte)
            hash=(hash^((const u8 *)blocks[block])[byte])*UINT64_C(1099511628211);
        ++cases;
    }
    g_PlayerInventoryState=saved_player;g_InventoryItemWorkspace=saved_workspace;
    g_InvItemPtr=saved_items;g_InvSlotLimit=saved_limit;g_InvBonusSlotCount=saved_bonus;
    g_InvSelectionBits=saved_selection;g_InvSelectionBitWords=saved_words;
    memcpy(g_InvSelectionBitStorage,saved_bits,sizeof(saved_bits));
    if(cases!=1152 || hash!=UINT64_C(0xc4267725611c01f9)){
        fprintf(stderr,"Item award CPU mismatch: cases=%u hash=%016llx\n",cases,(unsigned long long)hash);return 1;
    }
    puts("Original item opcode 167: 1152 unhooked USA CPU insertions and aliases passed");
    return 0;
}

static int item_addition(void)
{
    memset(&g_PlayerInventoryState,0,sizeof(g_PlayerInventoryState));
    g_AyaSaveState.inventory_slot_count=10;
    Inv_SelectActiveList(0);
    g_InvSpecialItemRecords[0].ammo=0;
    g_InvSpecialItemRecords[0].baseStats[2]=0;
    g_InvSpecialItemRecords[0].bonusStats[2]=999;
    const int initial[]={0x44,0x96,0x3f,1,6};
    for(unsigned i=0;i<5;++i)CHECK(Inv_AddItem(initial[i])==0);
    CHECK(g_InvItemPtr[0]==0x100 && g_InvItemPtr[1]==0x101);
    CHECK(!memcmp(&g_InventoryRuntime.equipment[0],Item_LookupBaseData(0x43),sizeof(ItemDataRecord)));
    CHECK(!memcmp(&g_InventoryRuntime.equipment[1],Item_LookupBaseData(0x95),sizeof(ItemDataRecord)));
    CHECK(g_InvItemPtr[2]==0x102 && g_InvItemPtr[3]==0x200 && g_InvItemPtr[4]==6);
    CHECK(g_InvSpecialItemRecords[0].ammo==Item_LookupBaseData(0)->ammo);
    for(unsigned i=5;i<10;++i)CHECK(Inv_AddItem(6)==0);
    CHECK(Inv_AddItem(6)==1 && Inv_AddItem(0x44)==1);
    unsigned before=g_InvSpecialItemRecords[0].ammo;
    CHECK(Inv_AddItem(1)==0);
    CHECK(g_InvSpecialItemRecords[0].ammo==before+Item_LookupBaseData(0)->ammo);
    g_InvSpecialItemRecords[0].ammo=998;
    CHECK(Inv_AddItem(1)==0 && g_InvSpecialItemRecords[0].ammo==999);
    g_InvItemPtr[9]=0;
    for(unsigned i=0;i<128;++i)g_InventoryRuntime.equipment[i].iconId=1;
    CHECK(Inv_AddItem(0x44)==1 && g_InvItemPtr[9]==0);
    CHECK(Inv_AddItem(0x203)==0 && g_InvItemPtr[9]==0x203);
    puts("Original item insertion: starting equipment/items/ammo, full inventory, ammo cap and full record pool passed");
    return 0;
}
static int battle_defaults(PeDisc *disc, PeDiscFile *exe)
{
    unsigned char original[0x70], tail[0x18];
    CHECK(pe_disc_read(disc,exe,0x10928-0x10000+0x800,original,sizeof(original)));
    CHECK(pe_disc_read(disc,exe,0x10998-0x10000+0x800,tail,sizeof(tail)));
    for(unsigned pass=0;pass<2;++pass) {
        memset(&g_AyaBattleState,0xa5,sizeof(g_AyaBattleState));
        memset(&g_SavedBattleStateTail,0xa5,sizeof(g_SavedBattleStateTail));
        memset(&g_BattleEquipStateBlock,0xa5,sizeof(g_BattleEquipStateBlock));
        Battle_SaveAyaState();
        const unsigned char *native=(const unsigned char *)&g_AyaBattleState;
        CHECK(!memcmp(native,original,0x1c));
        CHECK(!memcmp(native+PE1_OFFSETOF(Combatant,maxHP),original+0x1c,0x68-0x1c));
        CHECK(!g_AyaBattleState.action && !g_AyaBattleState.attributes);
        for(unsigned i=0x68;i<sizeof(original);++i)CHECK(original[i]==0);
        CHECK(!memcmp(&g_SavedBattleStateTail,tail,sizeof(tail)));
        CHECK(g_BattleEquipStateBlock.parameterWord.raw==0 && g_BattleEquipStateBlock.effectFlags==0);
    }
    puts("Original battle defaults: native reset matches USA executable fields and tail");
    return 0;
}
static int equipment_init(void)
{
    unsigned pairs=0;
    g_PlayerEntity=NULL;
    memset(&g_AyaBattleState,0x5a,sizeof(g_AyaBattleState));
    unsigned char prefix[sizeof(g_AyaBattleState.words)];
    memcpy(prefix,g_AyaBattleState.words,sizeof(prefix));
    for(unsigned w=0;w<255;++w) {
        ItemDataRecord *weapon=Item_LookupBaseData(w);
        if(!weapon || weapon->kind<1 || weapon->kind>8)continue;
        for(unsigned a=0;a<255;++a) {
            ItemDataRecord *armor=Item_LookupBaseData(a);
            if(!armor || armor->kind!=9)continue;
            g_InventoryRuntime.equipment[0]=*weapon;
            g_InventoryRuntime.equipment[1]=*armor;
            g_AyaSaveState.inventory_slot_count=2;
            g_AyaSaveState.inventory_items[0]=0x100;
            g_AyaSaveState.inventory_items[1]=0x101;
            g_AyaSaveState.equipped_weapon_slot=0;
            g_AyaSaveState.equipped_armor_slot=1;
            g_SavedBattleStateTail.words[5]=0x12345678;
            AyaBattleState *out=NULL;
            Battle_InitEquipLists(&out);
            CHECK(out==&g_AyaBattleState);
            CHECK(out->action==&g_SavedBattleStateTail);
            CHECK(out->attributes==&g_BattleEquipStateBlock);
            CHECK(!memcmp(prefix,out->words,sizeof(prefix)));
            CHECK(out->action->words[5]==0x12345678);
            CHECK(out->action->weapon.kind==weapon->kind);
            unsigned attack=weapon->baseStats[0]+weapon->bonusStats[0];
            if(attack>999)attack=999;
            unsigned hit=0;
            for(unsigned e=0;e<weapon->tailCount;++e) {
                unsigned effect=weapon->tailData[e]&31;
                if(effect==16 || effect==17)hit=effect-15;
            }
            if(hit==1)attack/=2;
            CHECK(out->action->weapon.attack==attack);
            CHECK(out->attributes->parameterWord.fields.third==
                  armor_third(armor->baseStats[2]+armor->bonusStats[2]));
            CHECK(g_InvItemPtr==g_AyaSaveState.inventory_items);
            ++pairs;
        }
    }
    CHECK(pairs==86*51);
    printf("Original equipment initialization: %u real weapon/armor pairs passed\n",pairs);
    return 0;
}
static int armor_stats(void)
{
    ItemDataRecord *item=&g_InventoryRuntime.equipment[0];
    memset(item,0,sizeof(*item)); item->kind=9;
    g_AyaSaveState.inventory_slot_count=1; g_AyaSaveState.equipped_armor_slot=0;
    g_AyaSaveState.inventory_items[0]=0x100;
    for(unsigned value=0;value<=1200;++value) {
        item->bonusStats[0]=item->bonusStats[1]=item->bonusStats[2]=value;
        BattleAttributes out={0}; Inv_BuildArmorList(&out);
        CHECK(out.parameterWord.fields.first==(value>999?999:value));
        CHECK(out.parameterWord.fields.second==(value>999?999:value));
        CHECK(out.parameterWord.fields.third==armor_third(value) && out.effectFlags==0);
    }
    unsigned real_armors=0;
    for(unsigned id=0;id<255;++id) {
        ItemDataRecord *base=Item_LookupBaseData(id);
        if(!base || base->kind!=9)continue;
        CHECK(base->tailCount<=sizeof(base->tailData)); *item=*base;
        BattleAttributes out={0}; Inv_BuildArmorList(&out);
        CHECK(out.parameterWord.fields.third==armor_third(base->baseStats[2]+base->bonusStats[2]));
        ++real_armors;
    }
    CHECK(real_armors>0);
    const unsigned flags[]={0,1,2,4,8,16,0x4000,0x8000,0x20,0x40,0x80,0x800,0x2000,0x10000,0x200,0x20000,0x400,0x1000};
    memset(item,0,sizeof(*item)); item->kind=9; item->tailCount=1;
    for(unsigned effect=0;effect<18;++effect) {
        item->tailData[0]=effect;
        BattleAttributes out={0}; Inv_BuildArmorList(&out);
        CHECK(out.effectFlags==flags[effect]);
        for(unsigned stat=0;stat<7;++stat) {
            int expected=stat==0 && effect==11?3:stat==1 && effect==12?2:
                (stat==5 && effect==13)||(stat==1 && effect==15)?-2:0;
            CHECK(g_StatScaleBase[stat]==expected);
        }
        CHECK(g_InvBonusSlotCount==(effect>=8 && effect<=10 ? 1<<(effect-8):0));
    }
    /* Later effects win; modifier refresh restores the selected storage list. */
    item->tailCount=4; item->tailData[0]=8; item->tailData[1]=10; item->tailData[2]=12; item->tailData[3]=15;
    s16 storage[3]={0}; Inv_SetActiveListOverride(storage,3); Inv_SelectActiveList(1);
    BattleCmd_LoadWeaponModifiers();
    CHECK(g_InvBonusSlotCount==4 && g_StatScaleBase[1]==-2);
    CHECK(g_InvItemPtr==storage && g_InvSlotLimit==3);
    Inv_ResetActiveList(); g_AyaSaveState.equipped_armor_slot=-1;
    BattleAttributes out={0}; Inv_BuildArmorList(&out);
    CHECK(out.parameterWord.raw==0 && out.effectFlags==0 && g_InvBonusSlotCount==0);
    printf("Original armor parameters: %u real records, 1201 values and modifier/reset cases passed\n",real_armors);
    return 0;
}
static int weapon_stats(void)
{
    ItemDataRecord *item=&g_InventoryRuntime.equipment[0];
    memset(item,0,sizeof(*item)); item->kind=1;
    g_AyaSaveState.inventory_slot_count=1; g_InvBonusSlotCount=0;
    g_AyaSaveState.inventory_items[0]=0x100; g_AyaSaveState.equipped_weapon_slot=0;
    for(int value=0;value<=1200;++value) {
        item->bonusStats[0]=value; item->bonusStats[1]=value; item->bonusStats[2]=value;
        item->ammo=value%1024;
        WeaponListOutput out={0}; Inv_BuildWeaponList(0,&out);
        unsigned capped=value>999?999:value, square=capped*22500;
        CHECK(out.attack==capped && out.packed.bits.power==capped);
        CHECK(out.kind==1 && out.packed.bits.category==1 && out.packed.bits.count==item->ammo);
        CHECK((u32)out.range*out.range<=square && (u32)(out.range+1)*(out.range+1)>square);
        CHECK(out.effects.word==0x11);
    }
    unsigned real_weapons=0;
    for(unsigned id=0;id<255;++id) {
        ItemDataRecord *base=Item_LookupBaseData(id);
        if(!base || base->kind<1 || base->kind>8) continue;
        CHECK(base->tailCount<=sizeof(base->tailData));
        *item=*base;
        WeaponListOutput out={0}; Inv_BuildWeaponList(0,&out);
        unsigned range=base->baseStats[1]+base->bonusStats[1]; if(range>999)range=999;
        unsigned square=range*22500;
        CHECK((u32)out.range*out.range<=square && (u32)(out.range+1)*(out.range+1)>square);
        CHECK(out.kind==base->kind);
        ++real_weapons;
    }
    CHECK(real_weapons>0);
    printf("Original weapon parameters: %u real weapon records and 1201 boundary values passed\n",real_weapons);
    memset(item,0,sizeof(*item)); item->kind=1;
    item->bonusStats[0]=100; item->tailCount=1;
    for(unsigned effect=1;effect<=20;++effect) {
        item->tailData[0]=effect;
        WeaponListOutput out={0}; Inv_BuildWeaponList(0,&out);
        CHECK(out.attack==(effect==16?50:100));
        if(effect<=5) CHECK(out.effects.bits.actions==g_WeaponEffectCodes[effect]);
        if(effect>=6 && effect<=8) CHECK(out.effects.bits.mode==effect-5);
        if(effect==16 || effect==17) CHECK(out.effects.bits.hitState==effect-15);
        if(effect>=19) CHECK(out.effects.bits.slot==effect-17);
    }
    return 0;
}
static int item_lookups(void)
{
    unsigned base_count=(g_ResidentDataPack.directory.statGrowth-g_ResidentDataPack.directory.itemBaseData)/sizeof(ItemDataRecord);
    CHECK(base_count>0 && base_count<=255);
    ItemDataRecord *base=(ItemDataRecord *)(g_ResidentDataPack.bytes+g_ResidentDataPack.directory.itemBaseData);
    for(unsigned i=0;i<base_count;++i) CHECK(Item_LookupBaseData(i)==&base[i]);
    CHECK(Item_LookupBaseData(base_count)==NULL && Item_LookupBaseData(UINT32_MAX)==NULL);
    g_AyaSaveState.inventory_slot_count=1; g_InvBonusSlotCount=0; Inv_ResetActiveList();
    for(int id=-1;id<=0x209;++id) {
        ItemDataRecord *expected=NULL;
        if(id>=1 && (unsigned)id<=base_count) expected=&base[id-1];
        else if(id>=0x100 && id<0x180) expected=&g_InventoryRuntime.equipment[id-0x100];
        else if(id>=0x200 && id<=0x208) expected=&g_KeyItemRecords[id-0x200];
        g_AyaSaveState.inventory_items[0]=id;
        CHECK(Inv_LookupData(id)==expected && Inv_LookupActiveListData(0)==expected);
    }
    CHECK(Inv_LookupActiveListData(-1)==NULL && Inv_LookupActiveListData(1)==NULL);
    s16 override[]={0x100,0x208}; Inv_SetActiveListOverride(override,2); Inv_SelectActiveList(1);
    CHECK(Inv_LookupActiveListData(0)==&g_InventoryRuntime.equipment[0]);
    CHECK(Inv_LookupActiveListData(1)==&g_KeyItemRecords[8]);
    Inv_ResetActiveList();
    return 0;
}
static int inventory_selection(void)
{
    g_AyaSaveState.equipped_weapon_slot=3; g_AyaSaveState.equipped_armor_slot=7;
    CHECK(g_InventoryRuntime.tracked[0]==3 && g_InventoryRuntime.tracked[2]==7);
    g_InventoryRuntime.tracked[0]=9; g_InventoryRuntime.tracked[2]=11;
    CHECK(g_AyaSaveState.equipped_weapon_slot==9 && g_AyaSaveState.equipped_armor_slot==11);
    for(unsigned i=0;i<50;++i) {
        g_AyaSaveState.inventory_items[i]=i+1;
        CHECK(g_InventoryRuntime.slots[i]==i+1);
        g_InventoryRuntime.slots[i]=i+0x100;
        CHECK(g_AyaSaveState.inventory_items[i]==i+0x100);
    }
    g_AyaSaveState.inventory_slot_count=10; g_InvBonusSlotCount=2;
    Inv_ResetActiveList();
    CHECK(g_InvItemPtr==g_AyaSaveState.inventory_items && g_InvSlotLimit==12);
    CHECK(g_InvSelectionBits==g_InvSelectionBitStorage && g_InvSelectionBitWords==2);
    CHECK(!Inv_IsActiveListOverrideSelected());
    s16 storage[100]={0};
    Inv_SetActiveListOverride(storage,100); Inv_SelectActiveList(1);
    CHECK(g_InvItemPtr==storage && g_InvSlotLimit==100 && Inv_IsActiveListOverrideSelected());
    CHECK(g_InvSelectionBits==g_InvStorageSelectionBits && g_InvSelectionBitWords==4);
    Inv_SelectActiveList(0);
    CHECK(g_InvItemPtr==g_InventoryRuntime.slots && g_InvSlotLimit==12);
    Inv_SelectActiveList(1); Inv_ResetActiveList();
    CHECK(!g_InvActiveListOverride && !g_InvOverrideSlotLimit && !Inv_IsActiveListOverrideSelected());
    Inv_SelectActiveList(1); CHECK(g_InvItemPtr==g_AyaSaveState.inventory_items);
    return 0;
}
static int recalc_stats(void)
{
    BattleEntity player={0}; Combatant core={0};
    player.core=&core; g_PlayerEntity=(FieldActor *)&player;
    const int modifiers[]={-3,0,5};
    for(int level=0;level<99;++level) for(unsigned variant=0;variant<3;++variant) {
        for(unsigned kind=0;kind<7;++kind) {
            int *growth=Aya_GetStatGrowthTable(kind);
            g_AyaSaveState.stats.levels[kind]=growth[level];
            g_StatScaleBase[kind]=modifiers[variant];
        }
        g_AyaSaveState.level=level;
        g_AyaSaveState.current_hp=core.curHP=core.hpMirror=9999;
        g_InvItemPtr=g_AyaSaveState.inventory_items;
        g_InvBonusSlotCount=variant*10;
        Inv_RecalcSlotStats();
        AyaLevelStats *base=Aya_LookupLevelStats(level);
        AyaLevelStats *scaled=Aya_LookupLevelStats(level+modifiers[variant]);
        int hp=(20+modifiers[variant])*base->hp/20;
        CHECK(core.maxHP==hp && core.curHP==hp && core.hpMirror==hp);
        CHECK(g_AyaSaveState.max_hp==hp && g_AyaSaveState.current_hp==hp);
        CHECK(core.stat1E==scaled->offense && core.stat20==scaled->defense);
        CHECK(core.maxAtk==scaled->battleMaxAtk && core.atbStep==scaled->battleAtbStep && core.atbRate==scaled->battleAtbRate);
        CHECK(core.statusStep3C==scaled->statusStep3C && core.statusStep3E==scaled->statusStep3E && core.stat22==scaled->battleStat22);
        CHECK(core.stat26==base->inventoryCapacity && core.field04.fieldId04==level+1);
        int capacity=base->inventoryCapacity<51?base->inventoryCapacity:50;
        CHECK(g_AyaSaveState.inventory_slot_count==capacity);
        capacity+=g_InvBonusSlotCount; if(capacity>50)capacity=50;
        CHECK(g_InvSlotLimit==capacity);
    }
    core.curHP=core.hpMirror=g_AyaSaveState.current_hp=1;
    Inv_RecalcSlotStats();
    CHECK(core.curHP==1 && core.hpMirror==1 && g_AyaSaveState.current_hp==1);
    s16 override[2]={0}; g_InvItemPtr=override; g_InvSlotLimit=2;
    Inv_SetAyaSlotCount(100); CHECK(g_AyaSaveState.inventory_slot_count==50 && g_InvSlotLimit==2);
    BattleAction action={0};core.action=&action;
    ItemDataRecord record={0},*record_pointer=&record;
    const unsigned ammo_values[]={0,1,1023,1024,65535};
    for(unsigned kind=0;kind<22;++kind)for(unsigned i=0;i<5;++i) {
        record.kind=kind;record.ammo=ammo_values[i];action.attackWord=0xA5A55A5A;
        Menu_ClearCommandResult();Inv_SetActiveList(5,&record_pointer);
        unsigned category=kind>0 && kind<8?(kind>4?kind-4:1):kind>=19?kind-18:0;
        CHECK(action.attackWord==((0xA5A55A5Au&0xFFCFFC00u)|(record.ammo&1023u)|((category&3u)<<20)));
        CHECK(!Menu_GetCommandResult());
    }
    core.action=NULL;
    g_PlayerEntity=NULL; g_AyaSaveState.current_hp=123;
    Inv_RecalcSlotStats(); CHECK(g_AyaSaveState.current_hp==123);
    g_InvItemPtr=NULL;
    return 0;
}
static unsigned delayed_calls;
static void delayed_probe(void) { ++delayed_calls; }
static int original_menu_bootstrap(void)
{
    memset(&g_SaveRuntimeState,0xA5,sizeof(g_SaveRuntimeState));
    MemCard_SetDelayedCallback(delayed_probe);
    CHECK(MemCard_HasDelayedCallback() && g_MemCardDelayedCallbackTimer==1);
    Menu_FullInit();
    /* Complete serialized image from USA Menu_FullInit 800527C8 in MIPS.
     * Only the three BIOS bzero calls use host memory-service semantics. */
    uint64_t hash=UINT64_C(14695981039346656037);
    for(size_t i=0;i<sizeof(g_SaveRuntimeState);++i)
        hash=(hash^((u8 *)&g_SaveRuntimeState)[i])*UINT64_C(1099511628211);
    CHECK(hash==UINT64_C(0x0b3a33783621c7a8));
    CHECK(&g_AyaSaveState.blend_color==&g_SaveRuntimeState.drawBlendColor);
    CHECK((u8 *)&g_PlayerInventoryState==(u8 *)&g_SaveRuntimeState+0x20);
    CHECK(!MemCard_HasDelayedCallback() && !g_MemCardDelayedCallbackTimer && !delayed_calls);
    CHECK(g_GameState.flags&0x40000000);
    CHECK(g_MenuWidgetFreeListHead==g_MenuWidgetNodePool && !g_MenuWidgetCurrentNode &&
          !g_MenuWidgetActiveListHead && !g_MenuWidgetNodePool[23].next);
    for(unsigned i=0;i<23;++i) CHECK(g_MenuWidgetNodePool[i].next==g_MenuWidgetNodePool+i+1);
    CHECK(g_SaveMetadataWindowIndex==1 && g_SaveMetadataWindowLength==8 &&
          g_SaveMetadataCursor==g_SaveRuntimeState.windows.metadata[1].text);
    CHECK(g_SaveRuntimeState.cursorMetadata[0].text[0]==255 &&
          g_SaveRuntimeState.cursorMetadata[1].text[0]==255);
    CHECK(g_DrawBlendColorPair[0]==0x404040 && g_DrawBlendColorPair[1]==0x404040);
    CHECK(g_DrawBaseY==8 && g_TextboxKernGlyph0F==1);
    CHECK(g_GameTimers[0].current==0 && g_GameTimers[1].current==0 &&
          g_GameTimers[2].current==0 && g_GameTimers[3].current==0);
    CHECK(g_TextboxNameGlyphs.count==3 && g_TextboxNameGlyphs.glyphs[0]==0x10 &&
          g_TextboxNameGlyphs.glyphs[1]==0x48 && g_TextboxNameGlyphs.glyphs[2]==0x30);
    MemCard_SetDelayedCallback(delayed_probe);
    MemCard_DelayedCallback(); CHECK(!delayed_calls && g_MemCardDelayedCallbackTimer==2);
    MemCard_DelayedCallback(); CHECK(!delayed_calls && g_MemCardDelayedCallbackTimer==3);
    MemCard_DelayedCallback(); CHECK(delayed_calls==1 && !MemCard_HasDelayedCallback());
    MemCard_DelayedCallback(); CHECK(delayed_calls==1);
    puts("Original full menu initialization with real resident data and delayed callback passed");
    return 0;
}

int main(void)
{
    CHECK(pending_command_stack() == 0);
    CHECK(menu_fade_parameters() == 0);
    CHECK(menu_fade_pixels() == 0);
    const char *path=getenv("PE_TEST_DISC");
    if(!path || !*path) return getenv("PE_REQUIRE_GAME_ASSETS")?1:77;
    PeDisc disc={0}; PeDiscFile archive, exe;
    unsigned char resident_range[4];
    CHECK(pe_disc_open(&disc,path));
    CHECK(pe_disc_find(&disc,"PE.IMG",&archive));
    CHECK(pe_disc_find(&disc,"SLUS_006.62",&exe));
    const struct {const void *data;unsigned address,size;} menu_tables[]={
        {g_InvSortRankTableA,0x923F8,24},
        {g_InvSortRankTableB,0x92410,24},
        {g_InvSortRankTableC,0x92428,24},
        {g_InvSortRankTableD,0x92440,24},
        {g_InvTypeRankTableA,0x92458,16},
        {g_InvTypeRankTableB,0x92468,16},
        {g_MenuStatusPanelPoints,0x92258,64},
        {g_MenuStatsListPanelPoints,0x92298,32},
        {g_MenuBlendColorPanelPoints,0x922D4,32},
        {g_MenuExpReviewPanelPoints,0x922F4,32},
        {g_MenuLevelUpPanelPoints,0x92314,56},
        {g_MenuContextHelpTextIds,0x9234C,8},
        {g_MenuRenamePanelPointsWindow0,0x92354,44},
        {g_MenuRenamePanelPointsWindow1,0x92380,32},
        {g_MenuRenamePreviewPanelPoints,0x923A0,32},
        {g_MenuModSlotIconIndexOffsetByView,0x922B8,4},
        {g_MenuModSlotFirstGlyphs,0x922BC,8},
        {g_MenuModSlotSecondGlyphs,0x922C4,8},
        {g_MenuModSlotThirdGlyphs,0x922CC,4},
        {g_MenuItemActionStateTable,0x92234,36},
        {g_MenuEquipSlotGlyphs,0x923C0,16},
        {g_MenuSortToggleGlyphs,0x922D0,4},
        {g_InvItemTableRanges,0x923D8,32},
    };
    for(unsigned i=0;i<sizeof(menu_tables)/sizeof(*menu_tables);++i) {
        u8 authored[64];unsigned size=menu_tables[i].size;
        CHECK(size<=sizeof(authored));
        CHECK(pe_disc_read(&disc,&exe,menu_tables[i].address-0x10000+0x800,authored,size));
        CHECK(!memcmp(authored,menu_tables[i].data,size));
    }

    /* Serialized USA sector-table entries 2..3 identify the resident bank.
     * Read the data coordinates directly; no parallel room runtime is needed. */
    CHECK(pe_disc_read(&disc,&exe,0x930d8-0x10000+0x800+2*2,
                       resident_range,sizeof(resident_range)));
    unsigned first=resident_range[0] | (unsigned)resident_range[1]<<8;
    unsigned last=resident_range[2] | (unsigned)resident_range[3]<<8;
    CHECK(last>first);
    size_t resident_bytes=(size_t)(last-first)*2048;
    CHECK(resident_bytes<=sizeof(g_ResidentDataPack));
    CHECK((uint64_t)last*2048<=archive.size);
    memset(&g_ResidentDataPack,0,sizeof(g_ResidentDataPack));
    CHECK(pe_disc_read(&disc,&archive,(uint64_t)first*2048,
                       &g_ResidentDataPack,resident_bytes));
    CHECK(g_ResidentDataPack.directory.statGrowth>=0);
    CHECK((unsigned)g_ResidentDataPack.directory.statGrowth+7*512<=resident_bytes);
    CHECK(g_ResidentDataPack.directory.levelStats>=0);
    CHECK((unsigned)g_ResidentDataPack.directory.levelStats+99*sizeof(AyaLevelStats)<=resident_bytes);
    CHECK(original_item_messages()==0);
    unsigned queries=0;
    for(int kind=0;kind<7;++kind) {
        int *table=Aya_GetStatGrowthTable(kind);
        for(unsigned i=1;i<99;++i) CHECK(table[i]>=table[i-1]);
        for(int value=0;value<=32767;++value) {
            int expected=0; while(expected<98 && value>=table[expected+1]) ++expected;
            int fraction=48,distance=0;
            if(expected<98) {
                int span=table[expected+1]-table[expected];
                fraction=span?(int)(((int64_t)value-table[expected])*49/span):0;
                distance=table[expected+1]-value;
            }
            int level=-1,sub=-1,dist=-1,dist_sub=-1;
            Aya_StatQueryLevelAndSubLevel(kind,value,&level,&sub);
            Aya_StatQueryDistanceAndSubLevel(kind,value,&dist,&dist_sub);
            CHECK(level==expected && sub==(fraction>=49?48:fraction));
            CHECK(dist==distance && dist_sub==fraction);
            CHECK(Aya_StatBinarySearch(value,table)==expected);
            CHECK(Aya_StatOverflowQuery(kind,value)==(expected?value-table[expected-1]:0));
            ++queries;
        }
        Aya_StatQueryLevelAndSubLevel(kind,0,NULL,NULL);
        Aya_StatQueryDistanceAndSubLevel(kind,0,NULL,NULL);
    }
    CHECK(Aya_LookupLevelStats(-1)==Aya_LookupLevelStats(0));
    CHECK(Aya_LookupLevelStats(99)==Aya_LookupLevelStats(98));
    ItemDataRecord key_records[9];
    CHECK(pe_disc_read(&disc,&exe,0xa1e64-0x10000+0x800,key_records,sizeof(key_records)));
    memcpy(g_KeyItemRecords,key_records,sizeof(key_records));
    u8 effect_codes[6];
    CHECK(pe_disc_read(&disc,&exe,0x923d0-0x10000+0x800,effect_codes,sizeof(effect_codes)));
    CHECK(!memcmp(effect_codes,g_WeaponEffectCodes,sizeof(effect_codes)));
    CHECK(battle_defaults(&disc,&exe)==0);
    CHECK(item_lookups()==0);
    CHECK(item_addition()==0);
    CHECK(script_item_awards()==0);
    CHECK(new_game_inventory()==0);
    CHECK(inventory_sort_comparisons()==0);
    CHECK(inventory_ammo_rollback()==0);
    CHECK(weapon_stats()==0);
    CHECK(armor_stats()==0);
    CHECK(equipment_init()==0);
    CHECK(inventory_selection()==0);
    CHECK(recalc_stats()==0);
    CHECK(original_menu_bootstrap()==0);
    puts("Original player stat recalculation: 297 level/modifier combinations and inventory capacity passed");
    pe_disc_close(&disc);
    printf("Original stat queries: %u values across seven real growth tables, level/sublevel/distance/overflow passed\n",queries);
    return 0;
}
