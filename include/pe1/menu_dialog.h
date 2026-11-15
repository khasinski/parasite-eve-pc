#ifndef PE1_MENU_DIALOG_H
#define PE1_MENU_DIALOG_H

#include "pe1/inventory_queries.h"
#include "pe1/menu_confirm_callback.h"
#include "pe1/menu_inventory.h"

void Menu_DrawNotificationText(MenuWidgetNode *node);
void Menu_DrawTwoLineDialogText(MenuWidgetNode *node);
void Menu_DrawNotificationDialogContent(MenuWidgetNode *node);
int Menu_HandleDeferredCallbackInput(MenuWidgetNode *node, unsigned int flags);
void Menu_SetDeferredCallback(void (*callback)(void));
/* Opens the info panel announcing a newly learned parasite spell. */
void Menu_CreateParasiteSpellInfoPanel(int spell);

/* Where a box line a language pack split (PE_TEXT_LINE_BREAK) ends: the offset of its line break,
 * or -1 when it has none with text after it (at most `size` bytes are read). */
int Menu_LineBreakOffset(const u8 *text, int size);

/* Two adjacent 64-byte, 0xFF-terminated notification text buffers. */
extern u8 g_MenuTwoLineDialogText[2][64];

/* Item-action dialogs use two label buffers and their optional primitive lists. */
extern u8 g_MenuItemLabelBuffer[128];
extern int g_MenuItemPrimListHandles[2];
extern int g_MenuActionTextBase;
extern int g_MenuActiveItemList, g_MenuActiveItemSlot;
/* Action state per item-action context and option: 0 tests the active item,
 * 1 is always enabled, 2 needs a selectable slot, 3 follows the disabled flag. */
extern int g_MenuItemActionStateTable[][3];
void *Inv_LookupActiveListDisplayData(int index);
void Menu_DrawItemLabel(MenuWidgetNode *node);
int Menu_ConfirmDialogHandler(MenuWidgetNode *node, unsigned int flags);
void Menu_DrawActionOptionList(MenuWidgetNode *node);
void Menu_OnItemDiscardConfirm(MenuWidgetNode *node, int confirmed);
void Menu_StepItemDetailPanel(void);

void Menu_DrawContextHelpText(MenuWidgetNode *panel);
int Menu_IsMemCardSlotSelectable(int port);
void Menu_DrawMemCardPortList(MenuWidgetNode *node);
/* Save slot list of the memory card screen. */
extern int g_SaveSelectedSlot;
/* State of the save slot picked on the memory card screen. */
extern int g_SaveSelectedSlotState;
int Menu_StepSaveSelect(MenuWidgetNode *node, unsigned int flags);
void Menu_DrawSaveSlotList(MenuWidgetNode *node);
void Menu_DrawSaveSlotPortLabel(MenuWidgetNode *node);
int Menu_IsMemCardSlotCompatible(int slot);
extern void (*g_MenuDeferredCallback)(void);

void Menu_OpenStartupMemCardDialog(void);

#endif
