#include "common.h"
#include "pe_hooks.h"
#include "pe1/aya.h"
#include "pe1/draw_state.h"
#include "pe1/inventory.h"
#include "pe1/inventory_queries.h"
#include "pe1/inventory_slots.h"
#include "pe1/inventory_state.h"
#include "pe1/menu_inventory.h"
#include "pe1/menu_context_help.h"
#include "pe1/menu_item_rows.h"
#include "pe1/save.h"
#include "pe1/text.h"

/* Shared native state; external variables have one storage definition. */
s32 g_InvActiveListSlotCount;
s16 *g_InvActiveItemList;
s32 g_InvTransferItemCount;

extern int g_InvActiveListCount;

extern int g_InvActiveListCount;
short g_InvActiveListItems[10];

int Menu_GetBattleCount(void) {
    return g_InvActiveListCount;
}

int Menu_GetBattleCountEntry(int arg0) {
    if (arg0 >= 0 && arg0 < g_InvActiveListCount) {
        return g_InvActiveListItems[arg0];
    }
    return 0;
}

void Inv_ShowItemDescriptionPanel(s32 index) {
    int value;
    ItemDataRecord *entry;
    u8 *text;

    if ((unsigned)index >= 10u || index >= g_InvActiveListCount) return;
    value = g_InvActiveListItems[index];
    if (value < 0x200) {
        if ((unsigned int)(value - 0x100) < 0x80U) {
            entry = &g_InventoryRuntime.equipment[value - 0x100];
        } else if ((unsigned int)(value - 1) < 0xFFU) {
            entry = Item_LookupBaseData(value - 1);
        } else if ((unsigned int)(value - 0x200) < 9U) {
            entry = &g_KeyItemRecords[value - 0x200];
        } else {
            entry = 0;
        }
        if (entry != 0) {
            Menu_DrawSlotRow(entry, Text_LookupTable8(entry->itemId - 1));
        }
    } else {
        if (value >= 0x209) return;
        entry = &g_KeyItemRecords[value - 0x200];
        if (entry->flags & ITEM_DATA_FLAG_GENERIC_DESCRIPTION) {
            text = g_CursorRenderDataBlock;
            if (entry->kind == 9) {
                text = g_CursorRenderDataBlock + 0x10;
            }
        } else {
            text = Text_LookupTable8(entry->itemId - 1);
        }
        Menu_DrawSlotRow(entry, text);
    }
}

#define LIST_ACTIVE 13
#define LIST_AYA 14

static inline void SwapItems(u16 *a, u16 *b) {
    *a ^= *b;
    *b ^= *a;
    *a ^= *b;
}

static inline int IsEquippedSlot(int slot) {
    int equipped = 0;
    if (g_InvItemPtr == g_AyaInventoryItems &&
        (g_EquippedWeaponSlot == slot || g_EquippedArmorSlot == slot)) {
        equipped = 1;
    }
    return equipped;
}

/* Swaps an item between Aya's inventory and the active (pending) list, or
 * inside one of them. Returns 0 when the slot cannot move. */
int Inv_TransferItem(int fromList, int fromSlot, int toList, int toSlot) {
    int result = 1;

    g_InvItemPtr = g_AyaInventoryItems;
    g_InvSlotLimit = Inv_GetAyaSlotLimit();
    g_InvSelectionBits = g_InvSelectionBitStorage;
    g_InvSelectionBitWords = 2;
    if (fromList == LIST_ACTIVE && toList == fromList) {
        u16 *table = g_InvActiveListItems;
        SwapItems(&table[fromSlot], &table[toSlot]);
        Inv_RebuildSelectableMask();
    } else if (fromList == LIST_AYA && toList == fromList) {
        int slot;
        SwapItems((u16 *)&g_InvItemPtr[fromSlot], (u16 *)&g_InvItemPtr[toSlot]);
        slot = g_EquippedWeaponSlot;
        if (slot == fromSlot) {
            g_EquippedWeaponSlot = toSlot;
        } else if (slot == toSlot) {
            g_EquippedWeaponSlot = fromSlot;
        }
        slot = g_EquippedArmorSlot;
        if (slot == fromSlot) {
            g_EquippedArmorSlot = toSlot;
        } else if (slot == toSlot) {
            g_EquippedArmorSlot = fromSlot;
        }
        Inv_RebuildSelectableMask();
    } else if (fromList == LIST_ACTIVE) {
        s16 *item;
        if (Inv_IsSlotSelectable(toSlot) && !IsEquippedSlot(toSlot)) {
            {
                s16 *table = g_InvActiveListItems;
                item = &table[fromSlot];
            }
            /* Pending rewards may contain equipment moved out of Aya's
             * inventory. Resolve its encoded record, not the base-item index. */
            ItemDataRecord *record = Inv_LookupItemRecord(*item);
            if (record == NULL) return 0;
            if (record->kind >= 16 && record->kind < 19) {
                result = Inv_CanAddActiveListItemToAya(fromSlot) == 0;
            } else {
                u16 *table = g_InvActiveListItems;
                SwapItems(&table[fromSlot], (u16 *)&g_InvItemPtr[toSlot]);
            }
            Inv_RebuildSelectableMask();
        } else {
            result = 0;
        }
    } else {
        if (Inv_IsSlotSelectable(fromSlot) && !IsEquippedSlot(fromSlot)) {
            SwapItems((u16 *)&g_InvItemPtr[fromSlot], (u16 *)&g_InvActiveListItems[toSlot]);
            Inv_RebuildSelectableMask();
        } else {
            result = 0;
        }
    }
    return result;
}

int Inv_CanAddActiveListItemToAya(int index) {
    int slot_index;
    ItemDataRecord *data;
    int saved_item_id;
    int item_id;
    int result;
    slot_index = index;
    if (slot_index >= 0 && slot_index < g_InvActiveListCount) {
        item_id = g_InvActiveListItems[slot_index];
        saved_item_id = item_id;
        if (item_id - 0x100U < 0x80U) {
            data = &g_InventoryRuntime.equipment[item_id - 0x100];
        } else if (item_id - 1U < 0xFFU) {
            data = Item_LookupBaseData(item_id - 1);
        } else if (saved_item_id - 0x200U < 9U) {
            data = &g_KeyItemRecords[saved_item_id - 0x200];
        } else {
            data = 0;
        }

        g_InvItemPtr = g_AyaInventoryItems;
        g_InvSlotLimit = Inv_GetAyaSlotLimit();
        g_InvSelectionBits = g_InvSelectionBitStorage;
        g_InvSelectionBitWords = 2;

        result = Inv_WriteSlotById(data);
        if (result == 0) {
            g_InvActiveListItems[slot_index] = 0;
        }
    } else {
        result = 1;
    }
    return result;
}

static inline int FindFreeActiveSlot(void) {
    s16 *p = g_InvItemPtr, *end = p + g_InvSlotLimit;

    if (p < end) {
        do {
            if (!*p)
                break;
            p++;
        } while (p < end);
        if (p < g_InvItemPtr + g_InvSlotLimit)
            return p - g_InvItemPtr;
    }
    return -1;
}

/* Transfer pending IDs, then rebuild the selectable-item mask. */
void Inv_TransferItemAlt(void) {
    int i = 0;
    g_InvItemPtr = g_AyaInventoryItems;
    g_InvSlotLimit = Inv_GetAyaSlotLimit();
    g_InvSelectionBits = g_InvSelectionBitStorage;
    g_InvSelectionBitWords = 2;
    for (; i < g_InvActiveListCount; i++) {
        int id = g_InvActiveListItems[i];
        ItemDataRecord *item = Inv_LookupItemRecord(id);
        /* Taking a reward leaves an empty entry in the pending list. */
        if (!item)
            continue;
        if (item->kind >= 16 && item->kind < 19) {
            if (!Inv_CanAddActiveListItemToAya(i))
                g_InvActiveListItems[i] = 0;
        } else if (id < 512) {
            int slot = FindFreeActiveSlot();
            if (slot >= 0) {
                g_InvItemPtr[slot] = id;
                g_InvActiveListItems[i] = 0;
            }
        }
    }
    Inv_RebuildSelectableMask();
}

static inline int ActiveItemKind(int index) {
    ItemDataRecord *item;
    if (index >= 0 && index < g_InvSlotLimit)
        item = Inv_LookupItemRecord(g_InvItemPtr[index]);
    else
        item = 0;
    return item ? item->kind : 0;
}
static inline int FindFreeEquipmentRecord(void) {
    int result;
    ItemDataRecord *p = g_InventoryRuntime.equipment;
    ItemDataRecord *end;
    /* Preserve retail's independent base reload beside the cached bound. */
    end = p + 128;

    if (p < g_InventoryRuntime.equipment + 128) {
        do {
            if (!p->iconId)
                break;
            p++;
        } while (p < end);
        if (p < g_InventoryRuntime.equipment + 128) {
            int index = p - g_InventoryRuntime.equipment;
            result = index;
        } else {
            result = -1;
        }
    } else {
        result = -1;
    }
    return result;
}

/* Replace base equipment IDs with private, mutable equipment records. */
void Inv_ClearDisplaySlots(void) {
    int i = 0;
    g_InvActiveListOverride = 0;
    g_InvOverrideSlotLimit = 0;
    g_InvItemPtr = g_InventoryRuntime.slots;
    g_InvSlotLimit = Inv_GetAyaSlotLimit();
    g_InvSelectionBits = g_InvSelectionBitStorage;
    g_InvSelectionBitWords = 2;
    for (; i < g_InvSlotLimit; i++) {
        if ((unsigned)(ActiveItemKind(i) - 1) < 9 &&
            (unsigned)((u16)g_InvItemPtr[i] - 256) >= 128) {
            int record = FindFreeEquipmentRecord();
            if (record >= 0) {
                ItemDataRecord *item = &g_InventoryRuntime.equipment[record];
                *item = *Item_LookupBaseData(g_InvItemPtr[i] - 1);
                g_InvItemPtr[i] = record + 256;
            }
        }
    }
}

/* Select a storage list, adding the special item ID when needed. */
int Inv_StepScrollDisplay2(int special) {
    if (special) {
        int i, found;
        g_InvActiveItemList = g_InvItemArray;
        for (i = 0; i < 82; i++)
            if (g_InvActiveItemList[i] == 516)
                break;
        found = i < 82;
        if (!found) {
            for (i = 0; i < g_InvSlotLimit; i++) {
                ItemDataRecord *item;
                if (i >= 0 && i < g_InvSlotLimit)
                    item = Inv_LookupItemRecord(g_InvItemPtr[i]);
                else
                    item = 0;
                if (item && item->kind == 6)
                    break;
            }
            if (i < g_InvSlotLimit) {
                /* If slots 0..80 are occupied, retail writes slot 81 unchecked. */
                for (i = 0; i < 81; i++)
                    if (!g_InvActiveItemList[i])
                        break;
                if (i < 82) {
                    g_InvActiveItemList[i] = 516;
                    found = 1;
                }
            }
        }
        g_InvActiveListSlotCount = 80 + found;
    } else {
        g_InvActiveItemList = g_WayneStorageItems;
        g_InvActiveListSlotCount = 100;
    }
    g_InvActiveListOverride = g_InvActiveItemList;
    return g_InvOverrideSlotLimit = g_InvActiveListSlotCount;
}

void Menu_DrawArmorItemDetail(int slot) {
    void *display;
    ItemDataRecord *entry;
    ItemDataRecord *candidate;
    int value;
    unsigned int bit;

    value = g_InvActiveItemList[slot];
    if (value != 0) {
        bit = g_InvSelectionBits[slot >> 5] & (1 << (slot & 0x1F));
        Draw_SetTextDimmed(bit == 0);

        if ((unsigned int)(value - 0x100) < 0x80U) {
            candidate = &g_InventoryRuntime.equipment[value - 0x100];
        } else if ((unsigned int)(value - 1) < 0xFFU) {
            candidate = Item_LookupBaseData(value - 1);
        } else if ((unsigned int)(value - 0x200) < 9U) {
            candidate = &g_KeyItemRecords[value - 0x200];
        } else {
            candidate = 0;
        }

        entry = candidate;

        if ((entry->flags & ITEM_DATA_FLAG_GENERIC_DESCRIPTION) != 0) {
            display = g_CursorRenderDataBlock;
            if (entry->kind == ITEM_KIND_ARMOR) {
                display = g_CursorRenderDataBlock + 0x10;
            }
        } else {
            display = Text_LookupTable8(entry->itemId - 1);
        }

        Menu_DrawSlotRow(entry, display);
        PE_HOOK(PE_HOOK_SLOT_DRAWN, (&(PeHookSlotDrawn){g_InvActiveItemList, slot}));
    }
}

/* Wayne-list item lookup, kind filtering, filtered-slot lookup, equip
 * eligibility and inventory swaps. */

ItemDataRecord *Inv_LookupItemData(int index) {
    int value = g_InvActiveItemList[index];
    int saved_value = value;
    ItemDataRecord *entry;

    if (value - 0x100U < 0x80U) {
        entry = &g_InventoryRuntime.equipment[value - 0x100];
    } else if (value - 1U < 0xFFU) {
        entry = Item_LookupBaseData(value - 1);
    } else if (saved_value - 0x200U < 9U) {
        entry = &g_KeyItemRecords[saved_value - 0x200];
    } else {
        entry = 0;
    }

    return entry;
}

static inline int WayneSlot(int index) {
    int result;
    if (index >= 0 && index < g_InvTransferItemCount) result = g_InvTransferItemList[index];
    else result = 0;
    return result;
}
static inline void SwapSlots(s16 *a, s16 *b) {
    *a ^= *b;
    *b ^= *a;
    *a ^= *b;
}
static inline void SwapAyaSlots(int firstSlot, int secondSlot) {
    int from = firstSlot;
    int to = secondSlot;
    SwapSlots(&g_InvItemPtr[from], &g_InvItemPtr[to]);
    if (g_InventoryRuntime.tracked[0] == from) g_InventoryRuntime.tracked[0] = to;
    else if (g_InventoryRuntime.tracked[0] == to) g_InventoryRuntime.tracked[0] = from;
    if (g_InventoryRuntime.tracked[2] == from) g_InventoryRuntime.tracked[2] = to;
    else if (g_InventoryRuntime.tracked[2] == to) g_InventoryRuntime.tracked[2] = from;
}
static inline ItemDataRecord *LookupTrackedItem(int index) {
    int value, saved;
    ItemDataRecord *result;

    if (index >= 0 && index < g_InvSlotLimit) {
        value = g_InvItemPtr[index];
        saved = value;
        if (value - 0x100U < 0x80U) {
            result = &g_InventoryRuntime.equipment[value - 0x100];
        } else {
            if (value - 1U < 0xFFU) {
                return Item_LookupBaseData(value - 1);
            }
            if (saved - 0x200U < 9U) {
                result = &g_KeyItemRecords[saved - 0x200];
            } else {
                result = 0;
            }
        }
        /* Keep the item-ID result merge distinct from an invalid list index. */
        return result;
    }
    return 0;
}

static inline ItemDataRecord *LookupActiveItem(int index) {
    if (index >= 0 && index < g_InvSlotLimit) return Inv_LookupItemRecordScaled(g_InvItemPtr[index]);
    return 0;
}

static inline int ItemKind(int slot) {
    ItemDataRecord *item = LookupActiveItem(slot);
    if (item) return item->kind;
    return 0;
}
/* Historical name: collect kind-filtered slots, rebuild selection, then append
 * empty slots. Return the final row count consumed by inventory menus.
 * The C variable shift requires item kinds below 32. */
int Inv_TransferItemAlt2(int mask) {
    s16 *out = g_InvTransferItemList;
    int i;
    g_InvItemPtr = g_AyaInventoryItems;
    g_InvSlotLimit = Inv_GetAyaSlotLimit();
    g_InvSelectionBits = g_InvSelectionBitStorage;
    g_InvSelectionBitWords = 2;
    for (i = 0; i < g_InvSlotLimit; i++) {
        if ((mask >> ItemKind(i)) & 1) *out++ = i;
    }
    g_InvTransferItemCount = out - g_InvTransferItemList;
    Inv_RebuildSelectableMask();
    for (i = 0; i < g_InvSlotLimit; i++) {
        if (g_InvItemPtr[i] == 0) *out++ = i;
    }
    g_InvTransferItemCount = out - g_InvTransferItemList;
    return g_InvTransferItemCount;
}

int Inv_GetWayneListItemByIndex(int index) {
    if (index >= 0 && index < g_InvTransferItemCount) {
        return g_InvTransferItemList[index];
    }
    return 0;
}

/* Historical name: tests whether a slot may be acted on, excluding equipped
 * weapon/armor slots in Aya's list. An unresolved item remains allowed. */
int Inv_IsSlotEquipped(int index) {
    ItemDataRecord *data;
    int allowed = 1;
    int equipped, kind, count, i;
    data = LookupTrackedItem(index);
    if (data != 0) {
        allowed = 0;
        if (!(data->flags & 0x20)) {
            equipped = 0;
            if (g_InvItemPtr == g_AyaInventoryItems &&
                (g_InventoryRuntime.tracked[0] == index || g_InventoryRuntime.tracked[2] == index))
                equipped = 1;
            if (!equipped) allowed = 1;
        }
        if (data->kind == 8) {
            kind = 8;
            count = 0;
            for (i = 0; i < g_InvSlotLimit; i++) {
                if (Inv_GetActiveListItemType(i) == kind) count++;
            }
            allowed &= count >= 2;
        }
    }
    return allowed;
}

/* Historical name: swaps inventory/storage entries after eligibility checks. */
int Inv_RebuildWithSlotLimit(int fromMode, int fromIndex, int toMode, int toIndex) {
    int success = 1;
    int from;
    ItemDataRecord *item;
    g_InvItemPtr = g_AyaInventoryItems;
    g_InvSlotLimit = Inv_GetAyaSlotLimit();
    g_InvSelectionBits = g_InvSelectionBitStorage;
    g_InvSelectionBitWords = 2;
    PeHookStorageTransfer hook = {fromMode, fromIndex, toMode, toIndex, 0};
    if (PE_HOOK(PE_HOOK_STORAGE_TRANSFER, &hook)) return hook.result;
    if (fromMode == 0x34 && toMode == fromMode) {
        SwapSlots(&g_InvActiveItemList[fromIndex], &g_InvActiveItemList[toIndex]);
        Inv_RebuildSelectableMask();
    } else if (fromMode == 0x33 && toMode == fromMode) {
        int first, second;
        first = WayneSlot(fromIndex);
        second = WayneSlot(toIndex);
        SwapAyaSlots(first, second);
        Inv_RebuildSelectableMask();
    } else if (fromMode == 0x34) {
        from = WayneSlot(toIndex);
        if (Inv_IsSlotEquipped(from) &&
            ((item = Inv_LookupItemRecordScaled(g_InvActiveItemList[fromIndex])) == NULL ||
             item->kind < 19 || item->kind >= 22)) {
            SwapSlots(&g_InvActiveItemList[fromIndex], &g_InvItemPtr[from]);
            Menu_SetSwapReturnFlag();
            Inv_RebuildSelectableMask();
        } else success = 0;
    } else {
        from = WayneSlot(fromIndex);
        if (Inv_IsSlotEquipped(from) &&
            ((item = Inv_LookupItemRecordScaled(g_InvActiveItemList[toIndex])) == NULL ||
             item->kind < 19 || item->kind >= 22)) {
            SwapSlots(&g_InvItemPtr[from], &g_InvActiveItemList[toIndex]);
            Menu_SetSwapReturnFlag();
            Inv_RebuildSelectableMask();
        } else success = 0;
    }
    return success;
}
