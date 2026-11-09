/* Native equipment-menu presentation for the economy's added armor effects.
 * Retail effects and weapon icons continue through the game's own renderer. */
#include "pe_catalog_mod.h"
#include "pe_mod_host.h"
#include "pe1/draw_state.h"
#include "pe1/inventory.h"
#include "pe1/inventory_slots.h"
#include "pe1/menu_item_record.h"
#include "pe1/menu_state.h"
#include "pe1/menu_widget.h"

static const char *const s_names[] = { "Bargain", "Scavenger", "Lucky Find", "Salvager" };
/* Three normal-font glyphs fit each 36-pixel native armor effect cell. */
static u8 s_badges[][4] = {
    {0x11,0x21,0x16,0xFF}, /* BRG */
    {0x22,0x12,0x25,0xFF}, /* SCV */
    {0x1B,0x12,0x1A,0xFF}, /* LCK */
    {0x22,0x1B,0x25,0xFF}, /* SLV */
};
static int pe_custom_effect(int effect) {
    return effect >= PE_FX_BARGAIN && effect <= PE_FX_SALVAGER;
}
static int pe_draw_effect(void *ctx, void *user) {
    PeHookEquipEffect *effect = ctx;
    (void)user;
    if (!effect->armor || !pe_custom_effect(effect->effect)) return 0;
    int x = g_TextCursorX, y = g_TextCursorY;
    g_TextCursorX += 2;
    g_TextCursorY += 2;
    Draw_PrintRawText(s_badges[effect->effect - PE_FX_BARGAIN]);
    g_TextCursorX = x; g_TextCursorY = y;
    return 1; /* suppress the out-of-range retail sprite */
}
static int pe_effect_help(void *ctx, void *user) {
    MenuWidgetNode *node = MenuWidget_GetCurrentNode();
    ItemDataRecord *item;
    (void)user;
    if (!node || g_InvItemUsableFlag || (node->selected_base != 6 && node->selected_base != 11)) return 0;
    item = node->selected_base == 6 ? g_MenuActionItemData :
        Inv_LookupActiveListData(Inv_RestoreSelection(1));
    int slot = MenuWidget_GridCellIndex(node);
    if (!item || slot < 0 || slot >= item->tailCount) return 0;
    int effect = item->tailData[slot] & 31;
    if (!pe_custom_effect(effect)) return 0;
    static u8 text[16];
    const char *name = s_names[effect - PE_FX_BARGAIN];
    unsigned n = 0;
    for (; *name && n < sizeof text - 1; ++name) {
        char c = *name;
        text[n++] = c >= 'A' && c <= 'Z' ? c - 'A' + 0x10 :
                    c >= 'a' && c <= 'z' ? c - 'a' + 0x30 : 0x0F;
    }
    text[n] = 0xFF;
    *(u8 **)ctx = text;
    return 0;
}
void pe_armor_effect_display_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_EQUIP_EFFECT, pe_draw_effect, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_effect_help, NULL, PE_PRIORITY_NORMAL, mod_id);
}
