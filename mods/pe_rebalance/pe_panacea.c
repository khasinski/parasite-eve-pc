/* Panacea from the recomp's pe_custom.c: Full Cure plus Medicine 4.
 * The game handles the 400 HP heal, max-HP clamp and visual effect. This
 * feature clears ailments first and redirects only item 0x19 to Medicine 4.
 * Native table lookups supply its name/description to every menu printer. */
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_translate.h"
#include "pe_mod_menu.h"
#include "pe_panacea.h"
#include "pe1/game_state.h"
#include "pe1/item_ids.h"

int g_pe_panacea = 1;
static u8 s_name[16], s_description[64];
static ItemDataRecord *s_record;
static u8 s_old_flags, s_old_icon, s_our_flags, s_our_icon;

static void pe_encode(u8 *out, const char *text) {
    while (*text) {
        char c = *text++;
        *out++ = c >= 'A' && c <= 'Z' ? 0x10 + c - 'A' :
                 c >= 'a' && c <= 'z' ? 0x30 + c - 'a' :
                 c >= '0' && c <= '9' ? c - '0' : 0x0F;
    }
    *out = 0xFF;
}

static int pe_panacea_text(void *ctx, void *user) {
    PeHookTextLookup *text = ctx;
    (void)user;
    if (g_pe_panacea && pe_game_loaded() && text->index == ITEM_ITEM25 - 1) {
        if (text->table == 8) { pe_tr_ascii(s_name,sizeof s_name,"Panacea"); *text->text = s_name; }
        if (text->table == 12) { pe_tr_ascii(s_description,sizeof s_description,"Restores 400 HP and Cures All Status"); *text->text = s_description; }
    }
    return 0;
}

static int pe_panacea_use(void *ctx, void *user) {
    int *item = ctx;
    Combatant *core;
    static const u32 masks[] = { AYA_POISON_MASK, AYA_CONFUSION_MASK, AYA_DARKNESS_MASK, AYA_STIFFNESS_MASK };
    (void)user;
    if (!g_pe_panacea || *item != ITEM_ITEM25 || !pe_game_loaded() || !(core = pe_aya_core())) return 0;
    for (unsigned i = 0; i < sizeof masks / sizeof masks[0]; ++i)
        if (((u32)core->stateFlags & masks[i]) != masks[i]) core->stateFlags &= ~masks[i];
    core->stateFlags &= ~0x1000;
    g_FieldMoveLock &= ~FIELD_MOVE_CONFUSED;
    *item = ITEM_MEDICINE_4;
    return 0;
}

static void pe_panacea_tick(void) {
    ItemDataRecord *panacea, *medicine;
    if (!g_pe_panacea) { pe_panacea_shutdown(); return; }
    if (!pe_game_loaded() || !(panacea = pe_base_item(ITEM_ITEM25))) return;
    if (panacea != s_record) {
        s_record = panacea;
        s_old_flags = panacea->flags;
        s_old_icon = panacea->iconId;
    }
    s_our_flags = (panacea->flags & ~3u) | 3u;
    panacea->flags = s_our_flags;
    medicine = pe_base_item(ITEM_MEDICINE_4);
    s_our_icon = medicine ? medicine->iconId : panacea->iconId;
    panacea->iconId = s_our_icon;
}

void pe_panacea_shutdown(void) {
    if (s_record) {
        if (s_record->flags == s_our_flags) s_record->flags = s_old_flags;
        if (s_record->iconId == s_our_icon) s_record->iconId = s_old_icon;
    }
    s_record = NULL;
}

void pe_panacea_register(const char *mod_id) {
    pe_encode(s_name, "Panacea");
    pe_encode(s_description, "Restores 400 HP and Cures All Status");
    pe_menu_register_tick(pe_panacea_tick);
    pe_hook_add(PE_HOOK_ITEM_USE, pe_panacea_use, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_TEXT_LOOKUP, pe_panacea_text, NULL, PE_PRIORITY_LATE, mod_id);
}
