/* F1 > PE Enhancements > Economy: the playstyle picker, its preview, Apply and the option pages.
 * Everything here edits a draft. Only Apply reaches pe_econ_store, and the rows are live rows, so
 * nothing lands in pe_mods.ini: these settings belong to the save. Bridge findings D1 section 5,
 * D2 section 6. Ranges are provisional, for playtesting. */
#include <stdio.h>
#include <string.h>

#include "pe_econ_store.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"

void pe_loot_econ_context(PeEconContext *out);   /* pe_loot.c */
int pe_loot_enabled(void);

#define MOD_ID "pe_economy"
#define CHANCE_UNIT 100   /* rows show whole percents; the settings hold ten-thousandths */

static PeEconConfig s_draft;
static PeEconConfig s_custom;   /* the last edits, kept while browsing presets */
static int s_chose_custom;      /* the picker stays on Custom even if its settings match a preset */

/* What a new-path setting keeps as its price: never changed by choosing a preset. */
static uint16_t kept_price(void) {
    const PeEconConfig *active = pe_econ_store_active();
    PeEconContext context;
    if (active) return active->price_pct;
    pe_loot_econ_context(&context);
    return context.enhanced_price_pct;
}

static void draft_sync(void) {
    const PeEconState *state = pe_econ_store_state();
    s_chose_custom = 0;
    s_draft = state->config;
    s_custom = state->config;
    if (!state->initialized || !s_custom.algorithm) pe_econ_builtin(1, kept_price(), &s_custom);
    s_custom.engine = PE_ECON_CONFIGURABLE;
    memset(s_custom.key, 0, sizeof s_custom.key);
    if (s_draft.engine == PE_ECON_LEGACY) pe_econ_builtin(0, kept_price(), &s_draft);
}

static int on_save_changed(void *ctx, void *user) {
    (void)ctx; (void)user;
    draft_sync();
    if (pe_econ_store_active() && !pe_loot_enabled()) pe_toast("Junk economy is off, so this playstyle's rewards are paused.");
    return 0;
}

/* ---- the picker: built-ins, provider presets, then Custom ---- */
static unsigned provider_count(void) {
    const int n = pe_economy_policy_count(PE_POLICY_PRESET);
    return n > 0 ? (unsigned)n : 0u;
}
static unsigned pick_count(void) { return (unsigned)pe_econ_builtin_count() + provider_count() + 1u; }
static const char *pick_name(unsigned i) {
    const unsigned builtins = (unsigned)pe_econ_builtin_count();
    if (i < builtins) return pe_econ_builtin_name((int)i);
    if (i < builtins + provider_count()) return pe_economy_policy_at(PE_POLICY_PRESET, (int)(i - builtins))->name;
    return "Custom";
}
static int pick_of(const PeEconConfig *c) {
    const int builtins = pe_econ_builtin_count();
    int match;
    if (c->engine == PE_ECON_LEGACY) return 0;
    for (unsigned i = 0; c->key[0] && i < provider_count(); ++i)
        if (!strcmp(pe_economy_policy_at(PE_POLICY_PRESET, (int)i)->key, c->key)) return builtins + (int)i;
    match = pe_econ_match_builtin(c);
    return match >= 0 ? match : builtins + (int)provider_count();
}
static int get_pick(void) { return s_chose_custom ? (int)pick_count() - 1 : pick_of(&s_draft); }
static int set_pick(int v) {
    const int builtins = pe_econ_builtin_count();
    PeEconConfig next;
    if (v < 0 || (unsigned)v >= pick_count()) return 0;
    if (v < builtins) {
        if (pe_econ_builtin(v, kept_price(), &next) != PE_ECON_OK) return 0;
    } else if ((unsigned)(v - builtins) < provider_count()) {
        const PeEconomyPolicyDef *def = pe_economy_policy_at(PE_POLICY_PRESET, v - builtins);
        /* A refused preset can still be shown: Apply gives the reason. */
        (void)pe_econ_from_preset(def->key, &def->preset, kept_price(), &next);
    } else {
        next = s_custom;
    }
    s_draft = next;
    s_chose_custom = (unsigned)v == pick_count() - 1u;
    return 1;
}

/* ---- option rows: every edit makes the draft Custom ---- */
static void edit_done(void) {
    s_chose_custom = 0;
    memset(s_draft.key, 0, sizeof s_draft.key);
    s_custom = s_draft;
}
static void edit_begin(void) {
    if (s_draft.engine == PE_ECON_LEGACY) s_draft = s_custom;
}

#define INT_ROW(field) \
    static int get_##field(void) { return s_draft.engine == PE_ECON_LEGACY ? s_custom.field : s_draft.field; } \
    static int set_##field(int v) { edit_begin(); s_draft.field = (uint16_t)v; edit_done(); return 1; }
#define CHANCE_ROW(field) \
    static int get_##field(void) { return (s_draft.engine == PE_ECON_LEGACY ? s_custom.field : s_draft.field) / CHANCE_UNIT; } \
    static int set_##field(int v) { edit_begin(); s_draft.field = (uint16_t)(v * CHANCE_UNIT); edit_done(); return 1; }
#define FLAG_ROW(name, flag) \
    static int get_##name(void) { return ((s_draft.engine == PE_ECON_LEGACY ? s_custom.flags : s_draft.flags) & (flag)) != 0; } \
    static int set_##name(int v) { \
        edit_begin(); \
        s_draft.flags = (uint16_t)(v ? s_draft.flags | (flag) : s_draft.flags & ~(unsigned)(flag)); \
        edit_done(); \
        return 1; \
    }

INT_ROW(income) INT_ROW(boss_income) INT_ROW(steal_junk) INT_ROW(performance_pct)
INT_ROW(item_guarantee) INT_ROW(super_value) INT_ROW(duper_value)
CHANCE_ROW(payment_chance) CHANCE_ROW(item_chance) CHANCE_ROW(super_chance) CHANCE_ROW(duper_chance)
FLAG_ROW(f_income, PE_ECON_F_INCOME) FLAG_ROW(f_performance, PE_ECON_F_PERFORMANCE) FLAG_ROW(f_steal, PE_ECON_F_EMPTY_STEAL)
FLAG_ROW(f_item, PE_ECON_F_BONUS_ITEM) FLAG_ROW(f_super, PE_ECON_F_SUPER_DROP) FLAG_ROW(f_duper, PE_ECON_F_DUPER_DROP)
FLAG_ROW(f_redeem_super, PE_ECON_F_SUPER_REDEEM) FLAG_ROW(f_redeem_duper, PE_ECON_F_DUPER_REDEEM)

static int get_route(void) { return s_draft.engine == PE_ECON_LEGACY ? s_custom.route : s_draft.route; }
static int set_route(int v) { edit_begin(); s_draft.route = (uint8_t)v; edit_done(); return 1; }

/* ---- what the player reads ---- */
static void target_of(PeEconConfig *out) {
    *out = s_draft;
    if (out->engine == PE_ECON_CONFIGURABLE) out->price_pct = kept_price();
}
static void preview_now(PeEconPreview *out) {
    PeEconConfig target;
    PeEconContext context;
    target_of(&target);
    pe_loot_econ_context(&context);
    pe_econ_preview(pe_econ_store_state(), &target, &context, out);
}

static void current_text(char *out, unsigned size) {
    const PeEconStoreStatus status = pe_econ_store_status();
    if (!pe_game_loaded()) snprintf(out, size, "load a game");
    else if (status == PE_ECON_STORE_DAMAGED) snprintf(out, size, "Enhanced (damaged)");
    else if (status == PE_ECON_STORE_UNSUPPORTED) snprintf(out, size, "Enhanced (locked)");
    else if (pe_econ_store_active() && !pe_loot_enabled())
        snprintf(out, size, "%s (paused)", pick_name((unsigned)pick_of(&pe_econ_store_state()->config)));
    else snprintf(out, size, "%s", pick_name((unsigned)pick_of(&pe_econ_store_state()->config)));
}
static void summary_text(char *out, unsigned size) {
    const PeEconConfig *c = pe_econ_store_active();
    if (c && !pe_loot_enabled()) snprintf(out, size, "Paused");
    else if (!c) snprintf(out, size, "Battle Junk + rares");
    else if (!(c->flags & (PE_ECON_F_INCOME | PE_ECON_F_BONUS_ITEM | PE_ECON_F_SUPER_DROP | PE_ECON_F_DUPER_DROP)))
        snprintf(out, size, "Normal drops only");
    else {
        const char *const words[] = {c->flags & PE_ECON_F_INCOME ? "Junk" : NULL, c->flags & PE_ECON_F_BONUS_ITEM ? "Rares" : NULL,
                                     c->flags & (PE_ECON_F_SUPER_DROP | PE_ECON_F_DUPER_DROP) ? "Bundles" : NULL};
        unsigned used = 0;
        out[0] = 0;
        for (unsigned i = 0; i < 3; ++i) {
            if (words[i] && used < size) {
                const int n = snprintf(out + used, size - used, "%s%s", used ? " " : "", words[i]);
                if (n > 0) used += (unsigned)n;
            }
        }
    }
}
static void change_text(char *out, unsigned size) {
    PeEconPreview p;
    if (!pe_game_loaded()) { snprintf(out, size, "-"); return; }
    preview_now(&p);
    if (p.result != PE_ECON_OK || (!pe_loot_enabled() && p.transition != PE_ECON_NO_CHANGE)) { snprintf(out, size, "Can't apply"); return; }
    snprintf(out, size, "%s", p.transition == PE_ECON_NO_CHANGE ? "No change"
                              : p.transition == PE_ECON_ENTER_NEW ? "Leaves Enhanced"
                              : p.transition == PE_ECON_RETURN_ENHANCED ? "Back to Enhanced" : "Progress kept");
}
static void redeem_text(char *out, unsigned size) {
    PeEconPreview p;
    if (!pe_game_loaded()) { snprintf(out, size, "-"); return; }
    preview_now(&p);
    if (p.redeem_super + p.redeem_duper) snprintf(out, size, "%u for %u Junk", (unsigned)(p.redeem_super + p.redeem_duper), (unsigned)p.redeem_junk);
    else snprintf(out, size, "None");
}
static void price_text(char *out, unsigned size) {
    PeEconPreview p;
    if (!pe_game_loaded()) { snprintf(out, size, "-"); return; }
    preview_now(&p);
    if (p.price_from != p.price_to) snprintf(out, size, "%u%% to %u%%", (unsigned)p.price_from, (unsigned)p.price_to);
    else snprintf(out, size, "%u%%", (unsigned)p.price_to);
}
static void carried_text(char *out, unsigned size) {
    PeEconPreview p;
    if (!pe_game_loaded()) { snprintf(out, size, "-"); return; }
    preview_now(&p);
    snprintf(out, size, "%s", p.freezes_legacy_carried ? "Waits for Enhanced" : "Not affected");
}

static void apply_now(void) {
    PeEconConfig target;
    PeEconPreview p;
    PeEconResult r;
    if (!pe_game_loaded()) { pe_toast("Load a game first"); return; }
    if (pe_econ_store_status() == PE_ECON_STORE_DAMAGED) { pe_toast(pe_econ_reason(PE_ECON_BAD_STATE)); return; }
    if (pe_econ_store_status() == PE_ECON_STORE_UNSUPPORTED) { pe_toast(pe_econ_reason(PE_ECON_STORE_REFUSED)); return; }
    target_of(&target);
    preview_now(&p);
    if (p.result != PE_ECON_OK) { pe_toast(pe_econ_reason(p.result)); return; }
    if (p.transition == PE_ECON_NO_CHANGE) { pe_toast("Already in use"); return; }
    if (!pe_loot_enabled()) { pe_toast("Turn Junk economy on first"); return; }   /* a paused save doesn't change */
    r = pe_econ_store_apply(&target);
    if (r != PE_ECON_OK) { pe_toast(pe_econ_reason(r)); return; }
    pe_mod_log(MOD_ID, "applied %s", pick_name((unsigned)pick_of(&target)));
    draft_sync();
    if (target.engine == PE_ECON_CONFIGURABLE && !pe_mod_active("pe_rewards") &&
        (target.flags & (PE_ECON_F_INCOME | PE_ECON_F_BONUS_ITEM | PE_ECON_F_SUPER_DROP | PE_ECON_F_DUPER_DROP)))
        pe_toast("Applied. Battle rewards need the rewards mod, which is off.");
    else pe_toast("Economy settings applied. Save your game to keep them.");
}

static const char *const s_off_on[] = {"Off", "On"};
static const char *const s_routes[] = {"Counter", "Bag"};

static void flag_row(const char *page, const char *label, const char *description, PeMenuGet get, PeMenuSet set) {
    pe_menu_register_live_choice(page, label, description, s_off_on, 2, get, set);
}

void pe_econ_menu_register(const char *mod_id) {
    pe_menu_register_info(PE_PAGE_ECONOMY, "Current playstyle", "What this save uses. Its rewards pause while Junk economy is off.", current_text);
    pe_menu_register_info(PE_PAGE_ECONOMY, "Added rewards", "The rewards this playstyle adds. Normal enemy drops are never removed.", summary_text);
    pe_menu_register_live_dynamic_choice(PE_PAGE_ECONOMY, "Choose playstyle", "Browse freely. Nothing changes until Apply.", 16, pick_count, pick_name, get_pick, set_pick);
    pe_menu_register_info(PE_PAGE_ECONOMY, "Apply would", "What Apply does to this save's reward progress.", change_text);
    pe_menu_register_info(PE_PAGE_ECONOMY, "Bundles redeemed", "Held Super and Duper Junk, in the bag and trunk, that become Junk after Apply.", redeem_text);
    pe_menu_register_info(PE_PAGE_ECONOMY, "Shop prices", "Base price level. Bargain still applies on top.", price_text);
    pe_menu_register_info(PE_PAGE_ECONOMY, "Enhanced's held rare", "A rare drop that didn't fit waits for Enhanced. It isn't lost or paid here.", carried_text);
    pe_menu_register_action(PE_PAGE_ECONOMY, "Apply playstyle", "Use the playstyle chosen above. Only works in free field.", apply_now);

    pe_menu_register_live_choice(PE_PAGE_ECON_STORAGE, "Normal Junk goes to", "Bag keeps Junk as items. Extra income and redemption need Counter.", s_routes, 2, get_route, set_route);

    flag_row(PE_PAGE_ECON_INCOME, "Extra battle Junk", "Junk on top of normal drops for won battles.", get_f_income, set_f_income);
    pe_menu_register_live_int(PE_PAGE_ECON_INCOME, "Junk per battle", "Paid for an ordinary won battle.", 0, 100, 1, get_income, set_income);
    pe_menu_register_live_int(PE_PAGE_ECON_INCOME, "Junk per boss", "Paid for a won boss battle.", 0, 200, 1, get_boss_income, set_boss_income);
    pe_menu_register_live_int(PE_PAGE_ECON_INCOME, "Payment chance %", "Chance per won battle of getting that payment.", 0, 100, 1, get_payment_chance, set_payment_chance);
    flag_row(PE_PAGE_ECON_INCOME, "Few-hits bonus", "Pays more when Aya took fewer than 4 hits.", get_f_performance, set_f_performance);
    pe_menu_register_live_int(PE_PAGE_ECON_INCOME, "Bonus per hit avoided (%)", "Added to the payment for each hit under 4.", 0, 100, 5, get_performance_pct, set_performance_pct);
    flag_row(PE_PAGE_ECON_INCOME, "Empty steal Junk", "Pays when stealing from an enemy with nothing to steal.", get_f_steal, set_f_steal);
    pe_menu_register_live_int(PE_PAGE_ECON_INCOME, "Empty steal amount", "Junk for each empty steal.", 0, 20, 1, get_steal_junk, set_steal_junk);

    flag_row(PE_PAGE_ECON_BONUS, "Bonus rare items", "Extra rewards. Off doesn't remove normal enemy drops.", get_f_item, set_f_item);
    pe_menu_register_live_int(PE_PAGE_ECON_BONUS, "Rare chance %", "Chance per won battle.", 0, 100, 1, get_item_chance, set_item_chance);
    pe_menu_register_live_int(PE_PAGE_ECON_BONUS, "Rare guarantee", "Won battles until one is certain. 0: never guaranteed.", 0, 200, 1, get_item_guarantee, set_item_guarantee);
    flag_row(PE_PAGE_ECON_BONUS, "Super Junk drops", "Separate from rare items.", get_f_super, set_f_super);
    pe_menu_register_live_int(PE_PAGE_ECON_BONUS, "Super Junk chance %", "Chance per won battle. With Duper, at most 100%.", 0, 100, 1, get_super_chance, set_super_chance);
    flag_row(PE_PAGE_ECON_BONUS, "Duper Junk drops", "Separate from rare items.", get_f_duper, set_f_duper);
    pe_menu_register_live_int(PE_PAGE_ECON_BONUS, "Duper Junk chance %", "Chance per won battle. With Super, at most 100%.", 0, 100, 1, get_duper_chance, set_duper_chance);

    flag_row(PE_PAGE_ECON_REDEEM, "Redeem Super Junk", "Bundles in the bag and trunk, Wayne's too, become Junk. Off keeps the item.", get_f_redeem_super, set_f_redeem_super);
    pe_menu_register_live_int(PE_PAGE_ECON_REDEEM, "Super Junk value", "Kept while redemption is off.", 0, 500, 5, get_super_value, set_super_value);
    flag_row(PE_PAGE_ECON_REDEEM, "Redeem Duper Junk", "Bundles in the bag and trunk, Wayne's too, become Junk. Off keeps the item.", get_f_redeem_duper, set_f_redeem_duper);
    pe_menu_register_live_int(PE_PAGE_ECON_REDEEM, "Duper Junk value", "Kept while redemption is off.", 0, 500, 5, get_duper_value, set_duper_value);

    draft_sync();
    pe_hook_add(PE_HOOK_SAVE_READ, on_save_changed, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, on_save_changed, NULL, PE_PRIORITY_LATE, mod_id);
}
