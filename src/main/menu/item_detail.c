#include "common.h"
#include "pe1/inventory.h"
#include "pe1/inventory_slots.h"
#include "pe1/menu_dialog.h"
#include "pe1/menu_inventory.h"
#include "pe1/menu_state.h"
#include "pe1/menu_widget.h"
#include "pe1/text.h"

extern int g_MenuActionTextBase;
extern int g_MenuActiveItemList;
extern int g_MenuActiveItemSlot;
extern MenuConfirmCallback g_MenuConfirmCallback;
extern int g_MenuEquipMode;
extern int g_MenuItemPrimListHandles[2];

void Draw_SetTextDimmed(int arg0);
void Draw_FlushPrimList(int arg0);

void Menu_CloseActionSubmenu(int arg0) {
    MenuWidget_DestroyNode(MenuWidget_FindByModeAndSelectedBase(1, arg0 + 0x29));

    if (arg0 != 0) {
        MenuWidget_RestoreSavedCurrentNode();
    }
}

void Menu_DrawItemLabel(MenuWidgetNode *arg0) {
    int idx;

    u8 *label;
    int cut;

    idx = arg0->selected_base - 0x29;
    Draw_OffsetCursor(0, 0xA);
    Draw_SetTextDimmed(0);
    label = g_MenuItemLabelBuffer + (idx << 6);
    cut = Menu_LineBreakOffset(label, 64);
    if (cut >= 0) { /* a language pack's line break: the rest on the next line */
        label[cut] = 0xFF;
        Draw_PrintCenteredText(label);
        label[cut] = 0xF1;
        Draw_OffsetCursor(0, 0xE);
        Draw_PrintCenteredText(label + cut + 4);
    } else {
        Draw_PrintCenteredText(label);
    }
    if (g_MenuItemPrimListHandles[idx] != 0) {
        Draw_OffsetCursor(0, 0xE);
        Draw_FlushPrimList(g_MenuItemPrimListHandles[idx]);
    }
}

/* The yes/no confirmation dialog, the item detail panel and its discard
 * confirmation (main_tu_evidence G0397). The dialog handler compiles the
 * same under the panel's -fno-expensive-optimizations. */

/* Closes the dialog on a choice or cancel and reports it to
 * g_MenuConfirmCallback. */
int Menu_ConfirmDialogHandler(MenuWidgetNode *node, unsigned int flags) {
    int index;

    if (flags & 0x10000) {
        index = MenuWidget_GridCellIndex(MenuWidget_GetChild(node, 0));
        switch (index) {
        case 0:
            MenuWidget_DestroyNode(node);
            if (((MenuWidgetNode *)node)->selected_base == 0x2A) {
                MenuWidget_RestoreSavedCurrentNode();
            }
            if (g_MenuConfirmCallback != 0) {
                g_MenuConfirmCallback(node, 1);
            }
            Menu_PlayConfirmSound();
            break;
        case 1:
            MenuWidget_DestroyNode(node);
            if (((MenuWidgetNode *)node)->selected_base == 0x2A) {
                MenuWidget_RestoreSavedCurrentNode();
            }
            if (g_MenuConfirmCallback != 0) {
                g_MenuConfirmCallback(node, 0);
            }
            Menu_PlayCancelSound();
            break;
        }
        return 1;
    }

    if (flags & 0x40) {
        MenuWidget_DestroyNode(node);
        if (((MenuWidgetNode *)node)->selected_base == 0x2A) {
            MenuWidget_RestoreSavedCurrentNode();
        }
        if (g_MenuConfirmCallback != 0) {
            g_MenuConfirmCallback(node, 0);
        }
        Menu_PlayCancelSound();
    }
    return 1;
}

void Menu_StepItemDetailPanel(void) {
    MenuWidgetNode *parent, *child;
    MenuWidgetNode *panel;
    u8 *label;
    MenuConfirmCallback confirm;
    int width;
    Inv_SelectActiveList(g_MenuActiveItemList);
    parent = MenuWidget_GetCurrentNode();
    label = Inv_LookupActiveListDisplayData(g_MenuActiveItemSlot);
    {
        MenuWidgetNode *result = MenuWidget_CreateSimpleNode(0x29, parent, 0, 1);
        int mode = 0x29;
        parent = result;
        child = MenuWidget_CreateNode(mode, parent, parent);
    }
    parent->draw = Menu_DrawItemLabel;
    panel = parent;
    panel->update = Menu_ConfirmDialogHandler;
    child->draw = Menu_DrawActionOptionList;
    g_MenuActionTextBase = 5;
    MenuWidget_SetCurrentNode(child);
    child->cursor_x = 1;
    confirm = Menu_OnItemDiscardConfirm;
    Inv_SelectActiveList(g_MenuActiveItemList);
    if (label) Text_CopyFFTerminatedBytes(g_MenuItemLabelBuffer, label);
    else g_MenuItemLabelBuffer[0] = 0xFF;
    Text_AppendFFTerminatedBytes(g_MenuItemLabelBuffer, Text_LookupTable4(4));
    g_MenuItemPrimListHandles[0] = 0;
    width = Draw_MeasureTextWidth(g_MenuItemLabelBuffer) < 120 ? 120 : Draw_MeasureTextWidth(g_MenuItemLabelBuffer);
    panel->grid_width = width + 20;
    panel->visible_rows = 50;
    panel->x = (300 - width) >> 1;
    child->x = (panel->grid_width - 128) >> 1;
    child->y = panel->visible_rows - 20;
    g_MenuConfirmCallback = confirm;
}

/* Discard confirmation: on "yes" remove the selected item, close the
 * dialog and refresh the slot counts of the open inventory panels. */
void Menu_OnItemDiscardConfirm(MenuWidgetNode *arg0, s32 arg1) {
    MenuWidgetNode *dialog = arg0;
    MenuWidgetNode *panel;

    if (arg1 != 0) {
        Inv_SelectActiveList(g_MenuActiveItemList);
        Inv_RemoveActiveListItem(g_MenuActiveItemSlot);
        Inv_RebuildSelectableMask();
        MenuWidget_DestroyNode(dialog->parent->parent);
        panel = MenuWidget_FindByModeAndSelectedBase(2, 1);
        if (panel != 0) {
            Draw_SetPrimCallback(panel, Inv_GetAyaSlotLimit());
        }
        if (g_MenuEquipMode == 2) {
            panel = MenuWidget_FindByModeAndSelectedBase(2, 0x33);
            if (panel != 0) {
                Draw_SetPrimCallback(panel, Inv_TransferItemAlt2(0x3803FE));
            }
        }
    }
}
