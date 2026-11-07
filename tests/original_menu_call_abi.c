/* Real menu adapters and inventory renderer, with only drawing/assets isolated. */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pe1/aya.h"
#include "pe1/inventory_queries.h"
#include "pe1/inventory_state.h"
#include "pe1/menu_item_rows.h"
#include "pe1/menu_memcard_display.h"
#include "pe1/save.h"
#include "pe1/psyq_cd.h"
#include "pe1/text.h"

void Menu_DrawArmorEquipItem(int index);

_Static_assert(_Generic(&Inv_GetActiveListItem, short (*)(int):1, default:0),
               "Active-list values must be sign-extended from the declared return type");
_Static_assert(_Generic(((MenuWidgetNode *)0)->draw,
               void (*)(MenuWidgetNode *):1, default:0), "Widget draw callback needs its node");
_Static_assert(_Generic(&MemCard_DrawProgress,
               void (*)(MenuWidgetNode *):1, default:0), "Progress callback needs its node");

SaveRuntimeState g_SaveRuntimeState;
InventoryItemWorkspace g_InventoryItemWorkspace;
int g_InvActiveListCount;
static ItemDataRecord bases[255], *drawn;
static unsigned draws;
static u8 names[255];
ItemDataRecord *Item_LookupBaseData(unsigned index) {
    assert(index<255);return &bases[index];
}
void *Text_LookupTable8(unsigned index) { assert(index<255);return &names[index]; }
void Menu_DrawSlotRow(ItemDataRecord *record,u8 *text) {
    assert(record && text);drawn=record;++draws;
}

static void row_indices(void) {
    g_InvActiveListCount=10;
    for(int i=0;i<10;++i) {
        g_InvActiveListItems[i]=(s16)(0x100+i);
        g_InventoryRuntime.equipment[i].itemId=(u8)(i+1);
    }
    for(int i=0;i<10;++i) {
        drawn=NULL;Menu_DrawArmorEquipItem(i);
        assert(drawn==&g_InventoryRuntime.equipment[i]);
    }
    unsigned before=draws;
    for(unsigned i=0;i<4;++i) {
        int index=(int[]){-1,10,INT_MIN,INT_MAX}[i];
        Inv_ShowItemDescriptionPanel(index);
        Menu_DrawArmorEquipItem(index);
    }
    assert(draws==before);
    g_InvActiveListCount=2;Menu_DrawArmorEquipItem(2);assert(draws==before);
    g_InvActiveListCount=10;
    for(int id=0x200;id<=0x208;++id) {
        g_KeyItemRecords[id-0x200].itemId=1;
        g_InvActiveListItems[3]=(s16)id;
        Menu_DrawArmorEquipItem(3);
        assert(drawn==&g_KeyItemRecords[id-0x200]);
    }
    before=draws;
    for(int id=0x209;id<=SHRT_MAX;++id) {
        g_InvActiveListItems[3]=(s16)id;Menu_DrawArmorEquipItem(3);
    }
    assert(draws==before);
}

static void pointer_identity(void) {
    MenuWidgetNode first={0},second={0};
    uintptr_t low=0x1234;
    uintptr_t high=sizeof(uintptr_t)>4 ? (uintptr_t)UINT64_C(0x100000000) : 0;
    MenuWidgetNode *one=(MenuWidgetNode *)(high|low);
    MenuWidgetNode *two=(MenuWidgetNode *)((high*2)|low);
    first.children[0]=one;first.next=&second;second.children[0]=two;
    g_MenuWidgetActiveListHead=&first;
    assert(MenuWidget_FindByFieldValue(one)==&first);
    if(high)assert(MenuWidget_FindByFieldValue(two)==&second);
    assert(MenuWidget_FindByFieldValue((MenuWidgetNode *)(high|0x5678))==NULL);
    g_MenuWidgetActiveListHead=NULL;
}
int main(void) {
    row_indices();pointer_identity();
    /* The image-backed transfer is synchronous, even for polling callers. */
    assert(DsDataSync(0)==0 && DsDataSync(1)==0 && DsDataSync(-1)==0);
    assert(DsGetSector(NULL,8)==0 && DsGetSector2(NULL,8)==0);
    u32 sector[588];
    assert(DsGetSector(sector,-1)==0 && DsGetSector2(sector,INT_MAX)==0);
    puts("Menu ABI: row forwarding, bounds, key IDs and full-width child pointers passed");
    return 0;
}
