/*
 * The Mod Catalog's F1 pages (F1 > PE Enhancements > Economy, and Debug > Mod Catalog).
 * Presentation only: rules are pe_catalog.c, Dismantle / Rebuild pe_dismantle.c.
 *
 *   Mod Catalog  Item (equipped gun / armor), Effect (the board as a list: "(locked)" = seen on
 *                carried gear but not unlocked, "+TOOL" = premium, "*" = on the item), Price,
 *                Slot, Replaces, Install (press twice), Remove, Result
 *   Dismantle    Item, Gives, Dismantle (press twice), Result
 *   Rebuild      Item, Cost, Rebuild, Result
 * Moving the Effect row picks the slot an install would use (its group mate's, else the chosen
 * slot if free, else the first free one).
 */
#include <stdio.h>

#include "pe_catalog.h"
#include "pe_economy.h"
#include "pe_mod_host.h"
#include "pe_translate.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"

static int s_last = -1, s_last_target = -1; /* checkpoint-owned selection latches */
static int s_target;          /* PE_GUN / PE_ARMOR */
static int s_grid;            /* Effect row: board index */
static int s_grid_armed = -1; /* board index armed by the first Install press */
static int s_slot_choice;
static char s_slot_names[PE_CAT_MAX_MODS][32], s_target_names[2][32], s_grid_label[40];

static ItemDataRecord *pe_target(void) { return pe_cat_record(s_target); }

/* An effect is "seen" when any carried item of this type has it (shown dimmed while locked). */
static int pe_seen(int which, int e) {
    int i, m;
    for (i = 0; i < 50; ++i) {
        const ItemDataRecord *rec = pe_cat_bag_record(i);
        if (pe_cat_record_type(rec) != which) continue;
        for (m = 0; m < pe_cat_mod_count(rec); ++m) if ((rec->tailData[m] & 0x1F) == e) return 1;
    }
    return 0;
}

static int pe_target_has(int e) {
    const ItemDataRecord *rec = pe_target();
    int m;
    for (m = 0; rec && m < pe_cat_mod_count(rec); ++m) if ((rec->tailData[m] & 0x1F) == e) return 1;
    return 0;
}

static int pe_grid_effect(void) { return pe_cat_board_effect(s_target, s_grid); }

/* ---- rows ---- */

static unsigned pe_target_count(void) { return 2; }
static const char *pe_target_name(unsigned i) {
    const ItemDataRecord *rec;
    if (i > 1) return "";
    rec = pe_cat_record((int)i);
    snprintf(s_target_names[i], sizeof s_target_names[0], "%s: %s", i ? pe_l10n_ui("ARMOR") : pe_l10n_ui("GUN"),
             rec ? pe_l10n_ui(pe_cat_item_name(rec->itemId)) : pe_l10n_ui("NONE EQUIPPED"));
    return s_target_names[i];
}

static unsigned pe_grid_count(void) {
    const int n = pe_cat_board_count(s_target);
    if (s_grid < 0 || s_grid >= n) s_grid = 0;
    return (unsigned)n;
}

static const char *pe_grid_name(unsigned i) {
    const int e = pe_cat_board_effect(s_target, (int)i);
    const int unlocked = pe_cat_unlocked(s_target, e), seen = !unlocked && pe_game_loaded() && pe_seen(s_target, e);
    if (!e) return "";
    if (!unlocked && !seen) return "?";
    snprintf(s_grid_label, sizeof s_grid_label, "%s%s%s%s", pe_l10n_ui(pe_cat_effect_short(s_target, e)), seen ? pe_l10n_ui(" (locked)") : "",
             pe_cat_needs_tool(s_target, e) ? pe_l10n_ui(" +TOOL") : "", pe_target_has(e) ? " *" : "");
    return s_grid_label;
}

/* The slot an install of e would use: its group mate's, else the chosen slot if free, else the
 * first free slot, else the chosen slot. */
static int pe_install_slot(int e) {
    const ItemDataRecord *rec = pe_target();
    const int n = rec ? pe_cat_mod_count(rec) : 0, group = pe_mod_effect_group(s_target == PE_ARMOR, e);
    int chosen, m;
    if (n <= 0) return -1;
    for (m = 0; group && m < n; ++m)
        if ((rec->tailData[m] & 0x1F) && (rec->tailData[m] >> 5) == group) return m;
    chosen = s_slot_choice >= 0 && s_slot_choice < n ? s_slot_choice : 0;
    if (!(rec->tailData[chosen] & 0x1F)) return chosen;
    for (m = 0; m < n; ++m) if (!(rec->tailData[m] & 0x1F)) return m;
    return chosen;
}

static unsigned pe_slot_count(void) {
    const ItemDataRecord *rec = pe_target();
    const int n = rec ? pe_cat_mod_count(rec) : 0;
    int m;
    for (m = 0; m < n; ++m)
        snprintf(s_slot_names[m], sizeof s_slot_names[0], "%d %s", m + 1, pe_l10n_ui(pe_cat_effect_name(s_target, rec->tailData[m] & 0x1F)));
    return n > 0 ? (unsigned)n : 1;
}

static const char *pe_slot_name(unsigned i) {
    const ItemDataRecord *rec = pe_target();
    const int n = rec ? pe_cat_mod_count(rec) : 0;
    if (!rec) return s_target == PE_GUN ? "NO GUN EQUIPPED" : "NO ARMOR EQUIPPED";
    if (n <= 0) return pe_l10n_ui("NO MOD SLOTS");
    return i < (unsigned)n ? s_slot_names[i] : "";
}

static void pe_price_text(char *out, unsigned size) {
    const int e = pe_grid_effect();
    if (!e) { out[0] = '\0'; return; }
    if (pe_cat_unlocked(s_target, e))
        snprintf(out, size, "%s %d%s", pe_l10n_ui(pe_cat_effect_name(s_target, e)), pe_cat_tier_price(pe_cat_effect_tier(s_target, e)),
                 pe_cat_needs_tool(s_target, e) ? pe_l10n_ui("+TOOL") : "");
    else if (pe_game_loaded() && pe_seen(s_target, e)) snprintf(out, size, pe_l10n_ui("%s: DISMANTLE"), pe_l10n_ui(pe_cat_effect_name(s_target, e)));
    else snprintf(out, size, "%s", pe_l10n_ui("NOT FOUND YET"));
}

static void pe_replaces_text(char *out, unsigned size) {
    int taken[2];
    const ItemDataRecord *rec = pe_target();
    const int n = rec ? pe_cat_mod_count(rec) : 0, e = pe_grid_effect();
    int k;
    if (!rec || n <= 0 || s_slot_choice < 0 || s_slot_choice >= n) { snprintf(out, size, "-"); return; }
    if ((rec->tailData[s_slot_choice] & 0x1F) == e) { snprintf(out, size, "%s", pe_l10n_ui("ALREADY THERE")); return; }
    k = pe_cat_replaces(s_target, s_slot_choice, e, taken, 2);
    if (k == 2) snprintf(out, size, "%s, %s", pe_l10n_ui(pe_cat_effect_name(s_target, taken[0])), pe_l10n_ui(pe_cat_effect_name(s_target, taken[1])));
    else if (k == 1) snprintf(out, size, "%s", pe_l10n_ui(pe_cat_effect_name(s_target, taken[0])));
    else snprintf(out, size, "%s", pe_l10n_ui("NOTHING (EMPTY SLOT)"));
}

static void pe_junk_text(char *out, unsigned size) { snprintf(out, size, "%u", (unsigned)pe_loot_junk()); }
static void pe_result_text(char *out, unsigned size) { snprintf(out, size, "%s", pe_cat_last_said()); }

static void pe_gives_text(char *out, unsigned size) {
    int fresh = 0;
    const uint32_t pays = pe_dismantle_selected_payout(&fresh);
    if (!pays) snprintf(out, size, "-");
    else snprintf(out, size, pe_l10n_ui("%u Junk, %d new effect%s"), (unsigned)pays, fresh, fresh == 1 ? "" : "s");
}

static void pe_cost_text(char *out, unsigned size) {
    const int i = *pe_rebuild_choice();
    const uint32_t cost = i >= 0 && i < pe_rebuild_count() ? pe_rebuild_price_index(i) : 0;
    if (!cost) snprintf(out, size, "-");
    else snprintf(out, size, pe_l10n_ui("%u Junk"), (unsigned)cost);
}

/* ---- actions ---- */

static void pe_install(void) {
    char msg[96];
    const int e = pe_grid_effect(), tier = e > 0 ? pe_cat_effect_tier(s_target, e) : 0;
    const uint32_t price = (uint32_t)pe_cat_tier_price(tier);
    const ItemDataRecord *rec = pe_target();
    switch (pe_cat_install(s_target, s_slot_choice, e, 0)) {
    case PE_CAT_OK:
        snprintf(msg, sizeof msg, pe_l10n_ui("%s: installed %s (-%u Junk%s)%s"), pe_l10n_ui(pe_cat_item_name(rec->itemId)),
                 pe_l10n_ui(pe_cat_effect_name(s_target, e)), (unsigned)price, tier == 2 ? pe_l10n_ui(", Tool") : "",
                 pe_cat_is_bag_mod(s_target, e) ? pe_l10n_ui(". Re-equip to grow the bag") : "");
        break;
    case PE_CAT_NO_ITEM: snprintf(msg, sizeof msg, "%s", s_target == PE_GUN ? pe_l10n_ui("Equip a gun first") : pe_l10n_ui("Equip armor first")); break;
    case PE_CAT_NO_SLOTS: snprintf(msg, sizeof msg, "%s", pe_l10n_ui("This item has no mod slots")); break;
    case PE_CAT_LOCKED: snprintf(msg, sizeof msg, "%s", pe_l10n_ui("Locked: dismantle an item with this effect")); break;
    case PE_CAT_ALREADY: snprintf(msg, sizeof msg, "%s", pe_l10n_ui("Already installed there")); break;
    case PE_CAT_BAG: snprintf(msg, sizeof msg, "%s", pe_l10n_ui("Bag mods can't be replaced here")); break;
    case PE_CAT_JUNK: snprintf(msg, sizeof msg, pe_l10n_ui("Need %u Junk (have %u)"), (unsigned)price, (unsigned)pe_loot_junk()); break;
    case PE_CAT_TOOL: snprintf(msg, sizeof msg, "%s", pe_l10n_ui("Needs a Tool (or a Tool Kit)")); break;
    default: snprintf(msg, sizeof msg, "%s", pe_l10n_ui("Can't install now")); break;
    }
    pe_cat_say(msg);
}

/* Install: the first press says what would happen, the second installs. */
static void pe_install_press(void) {
    char msg[64];
    const int e = pe_grid_effect();
    if (!pe_cat_ready()) return;
    if (!pe_cat_unlocked(s_target, e)) { pe_install(); return; }   /* says why not */
    if (s_grid_armed != s_grid) {
        const int tier = pe_cat_effect_tier(s_target, e);
        s_grid_armed = s_grid;
        snprintf(msg, sizeof msg, pe_l10n_ui("Slot %d, %d Junk%s: press again"), s_slot_choice + 1, pe_cat_tier_price(tier),
                 tier == 2 ? pe_l10n_ui(" + Tool") : "");
        pe_cat_say(msg);
        return;
    }
    s_grid_armed = -1;
    pe_install();
}

static void pe_remove(void) {
    char msg[64];
    const ItemDataRecord *rec;
    int e;
    if (!pe_cat_ready()) return;
    rec = pe_target();
    e = rec && s_slot_choice >= 0 && s_slot_choice < pe_cat_mod_count(rec) ? rec->tailData[s_slot_choice] & 0x1F : 0;
    switch (pe_cat_remove(s_target, s_slot_choice, 0)) {
    case PE_CAT_OK: snprintf(msg, sizeof msg, pe_l10n_ui("%s: removed %s (stays unlocked)"), pe_l10n_ui(pe_cat_item_name(rec->itemId)), pe_l10n_ui(pe_cat_effect_name(s_target, e))); break;
    case PE_CAT_NO_ITEM: snprintf(msg, sizeof msg, "%s", s_target == PE_GUN ? pe_l10n_ui("Equip a gun first") : pe_l10n_ui("Equip armor first")); break;
    case PE_CAT_EMPTY: snprintf(msg, sizeof msg, "%s", pe_l10n_ui("That slot is empty")); break;
    case PE_CAT_BAG: snprintf(msg, sizeof msg, "%s", pe_l10n_ui("Bag mods can't be removed here")); break;
    default: return;
    }
    pe_cat_say(msg);
}

/* The Effect or Item row moved: disarm, and pick the slot an install would use. */
static void pe_grid_tick(void) {

    int slot;
    if (s_grid == s_last && s_target == s_last_target) return;
    s_last = s_grid;
    s_last_target = s_target;
    s_grid_armed = -1;
    if (pe_game_loaded() && (slot = pe_install_slot(pe_grid_effect())) >= 0) s_slot_choice = slot;
}

void pe_catalog_pages_register(void) {
    pe_menu_register_info(PE_PAGE_CATALOG, "Your Junk", "Junk you have now.", pe_junk_text);
    pe_menu_register_dynamic_choice(PE_PAGE_CATALOG, "Item", "Work on your equipped gun or your equipped armor.",
                                    &s_target, 2, pe_target_count, pe_target_name);
    pe_menu_register_dynamic_choice(PE_PAGE_CATALOG, "Effect", "Locked: dismantle gear with it to unlock. * = already on the item.",
                                    &s_grid, 24, pe_grid_count, pe_grid_name);
    pe_menu_register_info(PE_PAGE_CATALOG, "Price", "The effect's full name and what installing it costs.", pe_price_text);
    pe_menu_register_dynamic_choice(PE_PAGE_CATALOG, "Slot", "The mod slot to fill or clear.", &s_slot_choice,
                                    PE_CAT_MAX_MODS, pe_slot_count, pe_slot_name);
    pe_menu_register_info(PE_PAGE_CATALOG, "Replaces", "What installing takes out of the item.", pe_replaces_text);
    pe_menu_register_action(PE_PAGE_CATALOG, "Install", "Press twice: put the effect in the slot.", pe_install_press);
    pe_menu_register_action(PE_PAGE_CATALOG, "Remove", "Empty the slot. The effect stays unlocked.", pe_remove);
    pe_menu_register_info(PE_PAGE_CATALOG, "Result", "What the last action did.", pe_result_text);

    pe_menu_register_info(PE_PAGE_DISMANTLE, "Your Junk", "Junk you have now.", pe_junk_text);
    pe_menu_register_dynamic_choice(PE_PAGE_DISMANTLE, "Item", "A gun or armor you aren't wearing.", pe_dismantle_choice(), 50,
                                    pe_dismantle_list_count, pe_dismantle_list_name);
    pe_menu_register_info(PE_PAGE_DISMANTLE, "Gives", "Junk and new Mod Catalog effects for this item.", pe_gives_text);
    pe_menu_register_action(PE_PAGE_DISMANTLE, "Dismantle", "Press twice. Its effects go to the Mod Catalog.", pe_dismantle_run);
    pe_menu_register_info(PE_PAGE_DISMANTLE, "Result", "What the last action did.", pe_result_text);

    pe_menu_register_info(PE_PAGE_REBUILD, "Your Junk", "Junk you have now.", pe_junk_text);
    pe_menu_register_dynamic_choice(PE_PAGE_REBUILD, "Item", "Dismantled gear, newest first.", pe_rebuild_choice(),
                                    PE_REBUILD_MAX, pe_rebuild_list_count, pe_rebuild_list_name);
    pe_menu_register_info(PE_PAGE_REBUILD, "Cost", "Price to rebuild this item.", pe_cost_text);
    pe_menu_register_action(PE_PAGE_REBUILD, "Rebuild", "Buy it back, with empty mod slots.", pe_rebuild_run);
    pe_menu_register_info(PE_PAGE_REBUILD, "Result", "What the last action did.", pe_result_text);

    pe_menu_register_action(PE_PAGE_DEBUG_CATALOG, "Unlock all effects", "Debug: fill the Mod Catalog.", pe_dismantle_debug_unlock_all);
    pe_menu_register_action(PE_PAGE_DEBUG_CATALOG, "Clear catalog", "Debug: empty the Catalog and Rebuild list.", pe_dismantle_debug_clear);
    pe_menu_register_action(PE_PAGE_DEBUG_CATALOG, "Give test gun", "Debug: M9 +5, ROF3 Heat 3Cmd Steal.", pe_dismantle_debug_test_gun);
    pe_menu_register_action(PE_PAGE_DEBUG_CATALOG, "Give test armor", "Debug: N Protector, 4 armor mods.", pe_dismantle_debug_test_armor);
    pe_menu_register_tick(pe_grid_tick);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_economy.h"
int pe_catalog_pages_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){if((!in&&!out)||(0))return 0;if(apply&&(!in||!pe_catalog_pages_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};
PE_CK_S(w,s_target,0,1,0);
PE_CK_S(w,s_grid,0,31,0);
PE_CK_S(w,s_grid_armed,-1,31,0);
PE_CK_S(w,s_slot_choice,-1,127,0);

PE_CK_S(w,s_last,-1,31,0);PE_CK_S(w,s_last_target,-1,1,0);
if(apply&&w.ok){memset(s_slot_names,0,sizeof s_slot_names);memset(s_target_names,0,sizeof s_target_names);memset(s_grid_label,0,sizeof s_grid_label);}return w.ok&&w.at==24u;}
#endif
