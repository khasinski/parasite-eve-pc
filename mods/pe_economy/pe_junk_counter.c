/* Inventory-only Junk display. The economy owns the balance; this feature
 * owns just its saved visibility preference and native menu drawing. */
#include "pe_economy.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe1/draw_state.h"
#include "pe1/menu_widget.h"

static int s_enabled = 1;

static int pe_draw_junk(void *ctx, void *user) {
    MenuWidgetNode *header = ctx;
    MenuWidgetNode *list;
    (void)user;
    if (!s_enabled || !header || !pe_game_loaded() || !pe_loot_enabled()) return 0;
    list = MenuWidget_FindByModeAndSelectedBase(2, 1);
    if (!list || !list->parent) return 0;

    /* Native header 0x1B is the right-hand Quantity box (x=154, w=126).
     * Use the free half of the inventory row, following its live position
     * while the native header adjusts for short bags and scrolling. */
    int left = list->parent->x;
    int width = header->x - left - 8;
    if (width < 90) return 0;
    /* An odd bag's last row has only a left cell, and on the last page the native header moves up
     * into its free right half (Menu_DrawAmmoTypeHeader), beside that item. Stay on the row it
     * left, where an even bag's header sits. */
    int top = header->y;
    if (list->has_scroll && list->y_limit - list->visible_rows - list->scroll_y == 0) top += 16;
    int saved_x = g_TextCursorX, saved_y = g_TextCursorY;
    int saved_dim = g_DrawTextDimmed;
    static u8 label[] = { 0x19, 0x44, 0x3D, 0x3A, 0xFF }; /* Junk */
    uint32_t junk = pe_loot_junk();
    if (junk > 99999u) junk = 99999u;
    int digits = 1;
    for (uint32_t n = junk; n >= 10; n /= 10) ++digits;

    Draw_SetTextDimmed(0);
    g_TextCursorX = left + 10;
    g_TextCursorY = top + 2;
    Draw_PrintRawText(label);
    g_TextCursorX = left + width - 10 - digits * 5;
    g_TextCursorY = top + 6;
    uint32_t divisor = 1;
    for (int i = 1; i < digits; ++i) divisor *= 10;
    for (; divisor; divisor /= 10) {
        Draw_EmitDigitSprite((junk / divisor) % 10);
        g_TextCursorX += 5;
    }
    /* Match the Quantity panel's native background, emitted after its text. */
    g_TextCursorX = left;
    g_TextCursorY = top;
    g_DrawTextDimmed = header->draw_state;
    Draw_AllocColorGradient(width, header->visible_rows,
                            header->appearance.gradientPoints, header->cursor_x);
    g_TextCursorX = saved_x;
    g_TextCursorY = saved_y;
    g_DrawTextDimmed = saved_dim;
    return 0;
}

void pe_junk_counter_register(const char *mod_id) {
    pe_menu_register_toggle(PE_PAGE_INVENTORY, "Junk counter",
        "Show Junk beside Quantity in Aya's inventory.", &s_enabled, NULL);
    pe_hook_add(PE_HOOK_INVENTORY_HEADER, pe_draw_junk, NULL, PE_PRIORITY_NORMAL, mod_id);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_economy.h"
int pe_junk_counter_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){if((!in&&!out)||(0))return 0;if(apply&&(!in||!pe_junk_counter_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};
PE_CK_U(w,s_enabled,0,1,1);

if(apply&&w.ok){}return w.ok&&w.at==4u;}
#endif
