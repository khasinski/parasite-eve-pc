#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_translate.h"
#include "pe1/draw_state.h"
#include "pe1/inventory.h"
#include "pe1/inventory_slots.h"
#include "pe1/menu_draw_list.h"
#include "pe1/menu_equipment.h"
#include "pe1/menu_inventory.h"
#include "pe1/menu_sound.h"
#include "pe1/menu_widget.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_prim.h"
#include "pe1/text.h"

enum { ACTION_MOVE = 1, ACCEPT = 0x10000, ACTION_ROW_HEIGHT = 16 };

static int s_enabled;
static MenuWidgetNode *s_panel;
static MenuWidgetNode *s_list;
static MenuWidgetNode *s_destination;
static MenuWidgetInputHandler s_original_step;
static int s_send_row;
static int s_source_row;
static int s_source_id;
static int s_send_rows;
static int s_direction;
static u8 s_label[64];
static u8 s_help[96];

static int pe_source_id(MenuWidgetNode *source) {
    if (source->selected_base == 0x33 && (s_source_row < 0 || s_source_row >= g_InvTransferItemCount))
        return -1;
    const int slot = source->selected_base == 0x33 ?
        Inv_GetWayneListItemByIndex(s_source_row) : s_source_row;
    return g_InvItemPtr && slot >= 0 && slot < g_InvSlotLimit ? g_InvItemPtr[slot] : -1;
}

static int pe_send_target(MenuWidgetNode *source) {
    return Inv_FindQuickSendVacancy(source->selected_base, s_source_row,
                                   s_destination->selected_base);
}

static int pe_send_stack(MenuWidgetNode *source, int vacancy, PeHookQuickSendStack *send) {
    send->from_mode = source->selected_base;
    send->from_index = s_source_row;
    send->vacancy = vacancy;
    return source->itemAction == Inv_RebuildWithSlotLimit &&
        PE_HOOK(PE_HOOK_QUICK_SEND_STACK, send);
}

static int pe_send_step(MenuWidgetNode *panel, unsigned int flags) {
    if (!(flags & ACCEPT)) return s_original_step(panel, flags);

    const int row = s_list->cursor_y;
    if (row < s_send_row || row >= s_send_row + s_send_rows) {
        if (row < s_send_row) return s_original_step(panel, flags);
        /* Existing action handlers continue to receive their original row index. */
        s_list->cursor_y = row - s_send_rows;
        const int handled = s_original_step(panel, flags);
        if (MenuWidget_GetCurrentNode() == s_list) s_list->cursor_y = row;
        return handled;
    }

    MenuWidgetNode *source = panel->parent;
    Inv_SelectActiveList(g_MenuActiveItemList);
    if (s_source_id <= 0 || pe_source_id(source) != s_source_id) {
        Menu_PlayErrorSound();
        return 1;
    }
    const int target = pe_send_target(source);
    PeHookQuickSendStack send = {0};
    const int stack = pe_send_stack(source, target, &send);
    const int all = row != s_send_row;
    int moved = 0;
    if (stack) {
        const int amount = all ? send.count : 1;
        if (amount > 0 && amount <= send.capacity) {
            send.amount = amount;
            pe_send_stack(source, target, &send);
            moved = send.moved;
        }
    } else if (!all && target >= 0) {
        moved = source->itemAction(source->selected_base, s_source_row,
                                   s_destination->selected_base, target);
    }
    if (!moved) {
        Menu_PlayErrorSound();
        pe_toast(pe_l10n_ui("This item cannot be sent."));
        return 1;
    }
    source->appearance.cursorTargetX = -1;
    s_destination->appearance.cursorTargetX = -1;
    MenuWidget_DestroyNode(panel);
    Menu_PlayConfirmSound();
    return 1;
}

static void pe_draw_chevron(int x, int y, int direction) {
    if (!g_DrawPacketArenaBase || !g_DrawOrderingTableEntry || !g_DrawPacketCursor ||
        g_DrawPacketCursor < g_DrawPacketArenaBase ||
        g_DrawPacketCursor > g_DrawPacketArenaBase + 0x4000 - 2 * sizeof(RenderLinePacket))
        return;
    const int back = x + (direction < 0 ? 3 : 0);
    const int tip = back + 3 * direction;
    const u8 shade = g_DrawTextDimmed ? 0x60 : 0xe0;
    RenderNativeTag *order = (RenderNativeTag *)g_DrawOrderingTableEntry;
    for (int end = 0; end < 2; end++) {
        RenderLinePacket *line = (RenderLinePacket *)g_DrawPacketCursor;
        g_DrawPacketCursor += sizeof(*line);
        SetLineF2(line);
        line->r = line->g = line->b = shade;
        line->x0 = back;
        line->y0 = y + 6 * end;
        line->x1 = tip;
        line->y1 = y + 3;
        line->tag.address = order->address;
        order->address = (uintptr_t)line;
    }
}

static int pe_send_available(MenuWidgetNode *source, int all) {
    const int target = pe_send_target(source);
    PeHookQuickSendStack send = {0};
    if (pe_send_stack(source, target, &send))
        return send.capacity >= (all ? send.count : 1);
    return !all && target >= 0;
}

static int pe_send_help(void *ctx, void *user) {
    u8 **text = ctx;
    (void)user;
    if (!s_list || MenuWidget_GetCurrentNode() != s_list || s_panel->update != pe_send_step)
        return 0;
    const int row = s_list->cursor_y;
    if (row < s_send_row || row >= s_send_row + s_send_rows) return 0;
    const int all = row != s_send_row;
    const int available = pe_send_available(s_panel->parent, all);
    const char *label = all ?
        (available ? "Send the whole stack" : "The whole stack does not fit") :
        (available ? "Send one item" : "No valid destination for this item");
    *text = pe_tr_ascii(s_help, sizeof s_help, label);
    return 0;
}

static int pe_send_draw(void *ctx, void *user) {
    PeHookItemActionRow *action = ctx;
    (void)user;
    if (action->list != s_list || action->list->parent != s_panel ||
        s_panel->update != pe_send_step)
        return 0;
    if (action->row >= s_send_row && action->row < s_send_row + s_send_rows) {
        MenuWidgetNode *source = s_panel->parent;
        const int all = action->row != s_send_row;
        if (!pe_send_available(source, all)) Draw_SetTextDimmed(1);
        const int arrowWidth = all ? 12 : 7;
        const int wordWidth = Draw_MeasureTextWidth(s_label);
        Draw_PrintCenteredTextInWidth(s_label, g_DrawTextPosX - arrowWidth);
        const int x = g_TextCursorX + ((g_DrawTextPosX - arrowWidth + wordWidth) >> 1);
        pe_draw_chevron(x, g_TextCursorY + 2, s_direction);
        if (all) pe_draw_chevron(x + 5, g_TextCursorY + 2, s_direction);
        return 1;
    }
    if (action->row >= s_send_row + s_send_rows) action->row -= s_send_rows;
    return 0;
}

static int pe_send_open(void *ctx, void *user) {
    PeHookItemActions *action = ctx;
    (void)user;
    if (!s_enabled) return 0;

    MenuWidgetNode *source = action->panel->parent;
    MenuWidgetNode *destination = source->linkedNext ? source->linkedNext : source->linkedPrevious;
    if (!destination || !source->itemAction || destination->itemAction != source->itemAction ||
        (source->itemAction != Inv_RebuildWithSlotLimit && source->itemAction != Inv_TransferItem))
        return 0;

    int row = 0;
    while (row < 3 && g_MenuItemActionStateTable[action->context][row] != ACTION_MOVE)
        row++;
    if (row == 3) return 0;

    s_panel = action->panel;
    s_list = action->list;
    s_destination = destination;
    s_original_step = s_panel->update;
    s_send_row = row;
    s_source_row = MenuWidget_GridCellIndex(source);
    s_source_id = pe_source_id(source);
    PeHookQuickSendStack send = {0};
    pe_send_stack(source, pe_send_target(source), &send);
    s_send_rows = send.count > 1 ? 2 : 1;
    int sourceX, sourceY, destinationX, destinationY;
    MenuWidget_ScreenPosition(source, &sourceX, &sourceY);
    MenuWidget_ScreenPosition(destination, &destinationX, &destinationY);
    s_direction = destinationX > sourceX ? 1 : -1;
    pe_tr_ascii(s_label, sizeof s_label, "Send");

    s_panel->update = pe_send_step;
    s_panel->visible_rows += ACTION_ROW_HEIGHT * s_send_rows;
    s_list->visible_rows += s_send_rows;
    s_list->visible_rows_mirror += s_send_rows;
    s_list->y_limit += s_send_rows;
    if (s_list->cursor_y >= row) s_list->cursor_y += s_send_rows;
    int panelX, panelY;
    MenuWidget_ScreenPosition(s_panel, &panelX, &panelY);
    const int overflow = panelY + s_panel->visible_rows - 224;
    if (overflow > 0) MenuWidget_OffsetPosition(s_panel, 0, -overflow);
    return 0;
}

void pe_quick_send_register(const char *mod_id) {
    pe_menu_register_toggle(PE_PAGE_INVENTORY, "Quick Send",
        "Send an item to the other inventory pane without choosing a slot.", &s_enabled, 0);
    pe_hook_add(PE_HOOK_ITEM_ACTIONS_READY, pe_send_open, 0, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_ITEM_ACTION_ROW, pe_send_draw, 0, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_send_help, 0, PE_PRIORITY_NORMAL, mod_id);
}
