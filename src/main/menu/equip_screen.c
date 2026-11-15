#include "common.h"
#include "pe_hooks.h"
#include "pe1/draw_level_bar.h"
#include "pe1/draw_state.h"
#include "pe1/inventory.h"
#include "pe1/inventory_slots.h"
#include "pe1/menu_context_help.h"
#include "pe1/menu_equipment.h"
#include "pe1/menu_inventory.h"
#include "pe1/menu_item_rows.h"
#include "pe1/menu_sound.h"
#include "pe1/menu_widget.h"
#include "pe1/text.h"

/* Inventory/equip list screen: creation, list input, the item-count header,
 * the per-item action submenu and its input step. Contiguous at 0x80044274
 * and joined by private data and helper calls. */

#define NULL ((void *)0)

void Menu_OpenItemActionSubmenu(MenuWidgetNode *, int, int);

int g_MenuItemActionContext;
int g_MenuSelectionLocked;
int g_MenuActionSubmenuOpen;
int g_MenuActiveItemSlot;
int g_MenuItemActionDisabled;
int g_MenuEquipMode;
int g_MenuActiveItemList;
int g_AyaFullInvHold;
int g_InvSelectedItemIndex;
int g_InvSwapSourceList;
int g_InvSwapTargetIndex;
int g_InvAmmoSpendActiveList;
extern int g_InvSelectedItemIndex;
extern int g_InvSwapSourceList;
extern int g_InvSwapTargetIndex;
extern int g_MenuActionSubmenuOpen;
extern int g_MenuActiveItemList;
extern int g_MenuActiveItemSlot;
extern int g_MenuEquipMode;
int g_MenuEquipSwapSource;
extern int g_MenuItemActionContext;
extern int g_MenuItemActionDisabled;

void Menu_CreateEquipScreen(MenuWidgetNode *arg0) {
    s32 entryIndex;
    MenuWidgetNode *currentNode;
    MenuWidgetNode *node;
    MenuWidgetNode *parent;

    MenuWidget_CreateSimpleNode(0x1B, 0, 0, 0)->draw = Menu_DrawAmmoTypeHeader;
    parent = MenuWidget_CreateSimpleNode(1, arg0, 0, 0);
    node = MenuWidget_CreateNode(1, parent, parent);
    currentNode = node;
    parent->update = Menu_InventoryInputHandler;
    node->draw = Menu_DrawSelectableEquipSlotList;
    MenuWidget_SetCurrentNode(currentNode);
    node->itemAction = (MenuWidgetItemAction)Inv_SwapSlots;
    node->refreshItems = Menu_RebuildSelectableMask;
    Inv_RebuildSelectableMask();
    g_InvSwapTargetIndex = -1;
    g_InvSelectedItemIndex = -1;
    Draw_SetPrimCallback(node, Inv_GetAyaSlotLimit());
    g_MenuActionSubmenuOpen = 0;
    if (g_MenuEquipSwapSource != 0) {
        entryIndex = g_MenuEquipSwapSource - 1;
        node->cursor_x = (entryIndex & 1);
        node->cursor_y = ((entryIndex >> 1) & 0x7F);
        node->scroll_y = (entryIndex >> 8);
    }
}

void Menu_OnInventoryItemConfirm(s32 arg0) {
    s32 temp_v0;
    s32 var_s0;
    s32 var_s0_2;
    MenuWidgetNode *temp_s0;
    MenuWidgetNode *temp_s0_2;
    MenuWidgetNode *temp_s1;
    MenuWidgetNode *temp_s1_2;

    g_InvAmmoSpendActiveList = Inv_IsActiveListOverrideSelected();
    g_InvSelectedItemIndex = arg0;
    temp_v0 = Inv_BuildCompatibleWeaponBitset(arg0);
    if (temp_v0 == 0) {
        g_InvSelectedItemIndex = -1;
        Inv_RebuildSelectableMask();
        return;
    }
    if (temp_v0 == 1) {
        temp_s0 = MenuWidget_FindByModeAndSelectedBase(2, 0xD);
        temp_s1 = MenuWidget_FindByModeAndSelectedBase(2, 0xE);
        if ((temp_s0 != NULL) && (MenuWidget_GridCellIndex(temp_s0) != 0) && (temp_s1 != NULL)) {
            temp_s0->cursor_x = -1;
            Inv_SelectActiveList(0);
            var_s0 = 0;
            while ((var_s0 < Inv_GetAyaSlotLimit()) && (Inv_TestSelectionBit(var_s0) == 0)) {
                var_s0 += 1;
            }
            temp_s1->cursor_x = 0;
            temp_s1->cursor_y = (var_s0 & -(var_s0 < Inv_GetAyaSlotLimit()));
            MenuWidget_SetCurrentNode(temp_s1);
        }
        g_InvSwapSourceList = g_InvAmmoSpendActiveList;
        Inv_InitSlotDisplay(&g_InvSwapSourceList, &g_InvSwapTargetIndex);
        Menu_CreateAmmoSpendPanel(MenuWidget_GetCurrentNode());
        return;
    }
    temp_s0_2 = MenuWidget_FindByModeAndSelectedBase(2, 0xD);
    temp_s1_2 = MenuWidget_FindByModeAndSelectedBase(2, 0xE);
    if ((temp_s0_2 != NULL) && (MenuWidget_GridCellIndex(temp_s0_2) != 0) && (temp_s1_2 != NULL)) {
        temp_s0_2->cursor_x = -1;
        Inv_SelectActiveList(0);
        var_s0_2 = 0;
        while ((var_s0_2 < Inv_GetAyaSlotLimit()) && (Inv_TestSelectionBit(var_s0_2) == 0)) {
            var_s0_2 += 1;
        }
        temp_s1_2->cursor_x = 0;
        temp_s1_2->cursor_y = (var_s0_2 & -(var_s0_2 < Inv_GetAyaSlotLimit()));
        MenuWidget_SetCurrentNode(temp_s1_2);
    }
}

int Menu_InventoryInputHandler(MenuWidgetNode *root, unsigned int flags) {
    MenuWidgetNode *child;
    MenuWidgetNode *node;
    ItemDataRecord *item;
    int index;
    int active_list_flag;
    int submenu_flag;
    int selected_base;
    int result;
    int action_result;
    int value;
    unsigned int flags_saved = flags;
    int handled = 0;
    int (*action)(int);

    /* Both the initial child index and the return value start at zero. */
    child = MenuWidget_GetChild(root, handled);
    active_list_flag = 0;
    if (root->selected_base == 0xD || root->selected_base == 0x34) active_list_flag = 1;
    Inv_SelectActiveList(active_list_flag);

    if (g_MenuActionSubmenuOpen != 0) {
        if (flags_saved & 0x10000) {
            index = MenuWidget_GridCellIndex(child);
            item = Inv_LookupActiveListData(index);
            if (Inv_TestSelectionBit(index)) {
                if (item != 0) {
                    action = Inv_RemoveActiveListItem;
                    if (item->kind < 10) action = Inv_ClearActiveListSlot;
                    action_result = action(index);
                } else {
                    action_result = 0;
                }
                result = action_result;
                Inv_SetActiveList(0, &result);
                if (g_AyaFullInvHold) Inv_AddItem(g_AyaFullInvHold);
                g_MenuActionSubmenuOpen = 0;
                Menu_PlayConfirmSound();
            } else {
                Menu_PlayErrorSound();
            }
            return handled;
        }
        if (flags_saved & 0x40) {
            Inv_SetActiveList(9, 0);
            Menu_PlayCancelSound();
        }
        return handled;
    }

    if (flags_saved & 0x10000) {
        if (root->selected_base == 0x33) {
            index = Inv_GetWayneListItemByIndex(MenuWidget_GridCellIndex(child));
        } else {
            index = MenuWidget_GridCellIndex(child);
        }

        if (g_InvSelectedItemIndex >= 0) {
            g_InvSwapSourceList = Inv_IsActiveListOverrideSelected();
            g_InvSwapTargetIndex = index;
            if (Inv_TestSelectionBit(index)) Menu_CreateAmmoSpendPanel(child);
        } else if (MenuWidget_GridCellIndex(child) >= 0 &&
                   Inv_LookupActiveListData(index) != 0 && g_MenuSelectionLocked == 0) {
            if (child->appearance.cursorTargetX >= 0) child->appearance.cursorTargetX = -1;
            submenu_flag = root->selected_base == 0xD || root->selected_base == 0x34;
            Menu_OpenItemActionSubmenu(child, submenu_flag, index);
        } else {
            value = Inv_GetActiveListItemType(index);
            if (value >= 0x10 && Inv_GetActiveListItemType(index) < 0x13) {
                if (Inv_CanAddActiveListItemToAya(index)) {
                    handled = 1;
                    Menu_PlayErrorSound();
                    return handled;
                }
            } else {
                child->appearance.cursorTargetX = child->cursor_x;
                child->target_y = child->cursor_y;
            }
        }
        handled = 1;
        Menu_PlayConfirmSound();
        return handled;
    }

    if (flags_saved & 0x40) {
        if (g_InvSelectedItemIndex >= 0) {
            Inv_RebuildSelectableMask();
            g_InvSelectedItemIndex = -1;
        } else if (child->appearance.cursorTargetX < 0) {
            selected_base = root->selected_base;
            if (selected_base == 0xD || selected_base == 0xE) {
                child->cursor_x = -1;
                node = MenuWidget_FindByModeAndSelectedBase(2, 0xC);
                MenuWidget_SetCurrentNode(node);
            } else if (selected_base == 1) {
                MenuWidget_DestroyNode(root);
                MenuWidget_DestroyNode(MenuWidget_FindByModeAndSelectedBase(1, 0x1B));
                Menu_CreateBonusPointAllocationView();
            } else if (selected_base == 0x33 || selected_base == 0x34) {
                child->cursor_x = -1;
                child = MenuWidget_FindByModeAndSelectedBase(2, 0x32);
                child->cursor_x = 0;
                MenuWidget_SetCurrentNode(child);
            }
        }
        Menu_PlayCancelSound();
        handled = 1;
        return handled;
    }

    return handled;
}

void Menu_DrawAmmoTypeHeader(MenuWidgetNode *node) {
    MenuWidgetNode *list;
    MenuWidgetSimpleDescriptor *desc;
    int page_delta;
    int target_y;

    list = MenuWidget_FindByModeAndSelectedBase(2, 1);
    desc = MenuWidget_LookupSimpleDescriptor(1);
    target_y = (list->visible_rows << 4) + 4;
    MenuWidget_OffsetPosition(node, 0, (desc->y + target_y) - node->y);

    if (list->has_scroll != 0) {
        page_delta = (list->y_limit - list->visible_rows) - list->scroll_y;
        switch (page_delta) {
        case 0:
            MenuWidget_OffsetPosition(node, 0, list->scroll_adjust - 0x10);
            break;
        case 1:
            if (list->scroll_adjust < 0) {
                MenuWidget_OffsetPosition(node, 0, list->scroll_adjust);
            }
            break;
        }
    }

    Draw_OffsetCursor(0xA, 2);
    Draw_PrintRawText(Text_LookupTable4(0x25));
    Draw_OffsetCursor(0x46, 4);
    Draw_PrintNumberWidth2Unk(Inv_CountTotal());
    Draw_AllocSprite(0x4C);
    Draw_OffsetCursor(5, 0);
    Draw_PrintNumberWidth2Unk(Inv_GetAyaSlotLimit() - g_InvOverflowSlotCount);   /* 11/10, not 11/11 */
    PE_HOOK(PE_HOOK_INVENTORY_HEADER, node);
}

void Menu_OpenItemActionSubmenu(MenuWidgetNode *arg0, s32 arg1, s32 arg2) {
    s32 var_s0;
    s32 temp_s0;
    s32 temp_v0;
    s32 var_s0_2;
    s32 var_s2;
    MenuWidgetNode *temp_a0;
    ItemDataRecord *temp_s0_2;
    MenuWidgetNode *temp_s1;
    MenuWidgetNode *temp_v0_2;

    g_MenuActiveItemList = arg1;
    Inv_SelectActiveList(arg1);
    g_MenuActiveItemSlot = arg2;
    temp_v0 = Inv_GetActiveListItemType(arg2);
    if (temp_v0 != 0xA) {
        if (((temp_v0 - 0xC) < 2U) || ((temp_v0 - 0xE) < 2U) || ((temp_v0 - 8) < 2U)) {
            g_MenuItemActionContext = 2;
        } else {
            g_MenuItemActionContext = 1;
        }
    } else {
        g_MenuItemActionContext = 0;
    }
    var_s0 = 2;
    if (g_MenuItemActionContext == var_s0) {
        var_s0 = 3;
    }
    temp_v0_2 = MenuWidget_CreateSimpleNode(var_s0, arg0, 0, 1);
    temp_s1 = MenuWidget_CreateNode(var_s0, temp_v0_2, temp_v0_2);
    temp_a0 = temp_s1;
    temp_v0_2->update = Menu_StepEquipScreen;
    temp_s1->draw = Menu_DrawContextActionList;
    MenuWidget_SetCurrentNode(temp_a0);
    var_s0_2 = 0;
    if ((g_MenuItemActionContext == 1) && ((Battle_IsInputAllowedWrapped() == 0) || (Inv_BuildCompatibleWeaponBitset(g_MenuActiveItemSlot) == 0) || (Battle_GetStateFlag1() != 0))) {
        var_s0_2 = 1;
    }
    g_MenuItemActionDisabled = var_s0_2;
    Inv_RebuildSelectableMask();
    {
        PeHookItemActions actions = {temp_v0_2, temp_s1, g_MenuItemActionContext, arg1, arg2};
        PE_HOOK(PE_HOOK_ITEM_ACTIONS, &actions);
        (void)actions;
    }
    if (g_MenuItemActionContext == 0) {
        var_s2 = 0;
        temp_s0 = g_MenuActiveItemSlot;
        temp_s0_2 = Inv_LookupActiveListData(temp_s0);
        if ((Inv_TestSelectionBit(temp_s0) != 0) && ((g_MenuEquipMode != 1) || (temp_s0_2->kind != 0xA) || (*(u8 *)temp_s0_2->bonusStats < 4U))) { /* kind 0xA keeps a level in the low bonus byte */
            var_s2 = 1;
        }
        if (var_s2 == 0) {
            temp_s1->cursor_y = 1;
            return;
        }
    }
block_21:
    if (g_MenuItemActionDisabled != 0) {
block_22:
        temp_s1->cursor_y = 1;
    }
}

int Menu_StepEquipScreen(MenuWidgetNode *node, unsigned int flags)
{
    int handled = 0;
    Inv_SelectActiveList(g_MenuActiveItemList);
    if (flags & 0x10000) {
        switch (g_MenuItemActionStateTable[g_MenuItemActionContext][
            MenuWidget_GridCellIndex(MenuWidget_GetChild(node, 0))]) {
        case 0: {
            int allowed = 0;
            int index = g_MenuActiveItemSlot;
            ItemDataRecord *item = Inv_LookupActiveListData(index);
            /* Kind 10 uses a byte at +0x0E, not the equipment signed bonus. */
            if (Inv_TestSelectionBit(index)) {
                if (g_MenuEquipMode != 1 || item->kind != 10 ||
                    *(u8 *)&item->bonusStats[0] < 4)
                    allowed = 1;
            }
            if (allowed) {
                MenuWidget_DestroyNode(node);
                if (g_MenuEquipMode == 2) {
                    MenuWidget_NavScrollTo(51);
                    MenuWidget_NavScrollTo(52);
                }
                if (Menu_GetBattleEquipMode())
                    MenuWidget_DestroyNode(MenuWidget_GetCurrentNode()->parent);
                Inv_BuildEquipSlotDisplay(g_MenuActiveItemSlot);
                Menu_PlayConfirmSound();
            } else {
                Menu_PlayErrorSound();
            }
            break;
        }
        case 1: {
            MenuWidgetNode *parent = node->parent;
            parent->appearance.cursorTargetX = parent->cursor_x;
            parent->target_y = parent->cursor_y;
            if (!g_MenuEquipMode) {
                Inv_ClearSelectionBitset();
                Inv_SetSelectionBit(parent->x_limit * parent->cursor_y + parent->cursor_x);
            }
            MenuWidget_DestroyNode(node);
            Menu_PlayConfirmSound();
            break;
        }
        case 2:
            if (Inv_IsSlotSelectable(g_MenuActiveItemSlot)) {
                if (Inv_IsActiveListOverrideSelected() || g_MenuActiveItemSlot != g_InventoryRuntime.tracked[2] ||
                    Inv_GetActiveSlotCount(0))
                    Menu_StepItemDetailPanel();
                else
                    Menu_CreateNotificationDialog(29, 0);
                Menu_PlayConfirmSound();
            } else {
                Menu_PlayErrorSound();
            }
            break;
        case 3:
            if (!g_MenuItemActionDisabled) {
                MenuWidget_DestroyNode(node);
                Menu_OnInventoryItemConfirm(g_MenuActiveItemSlot);
                Menu_PlayConfirmSound();
            } else {
                Menu_PlayErrorSound();
            }
            break;
        }
        handled = 1;
    } else if (flags & 0x40) {
        MenuWidget_DestroyNode(node);
        Menu_PlayCancelSound();
        handled = 1;
    }
    return handled;
}

/* Original USA data at 0x80092234. */
int g_MenuItemActionStateTable[3][3] = {{0,1,2},{3,1,2},{1,2,0}};
