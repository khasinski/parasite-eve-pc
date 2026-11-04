/*
 * Parasite Energy rebalance, ported from the psxrecomp build (src/mods/pe_pe_balance.c; bridge
 * findings pe-parasite-energy-rebalance-2026-10-15.md). F1 > PE Enhancements > Items & Parasite Energy.
 *
 * Rules: Vanilla / Rebalanced (default) / Custom (any row changed).
 *
 *   - PE heals scale with Aya's max HP (Heal 1 / 2 / 3: 15 / 30 / 65%, never below the vanilla
 *     30 / 60 / 280), and Detox / Scan cost 60 / 30 PE (vanilla 100 / 50). Both at the entry of
 *     Battle_ApplySpellEffect (PE_HOOK_SPELL, in battle and from the menu): the extra HP is added
 *     before the game's own heal (it clamps to max HP after), and the PE difference is refunded
 *     before the game subtracts its fixed cost (PE Light armor, effect flag 0x200, takes a third
 *     off both, as the game does).
 *   - Scan summary: vanilla Scan only flashes one "Susceptible to ..." line per weakness and never
 *     shows resistances; Rebalanced adds one on-screen line with both.
 *
 * Half-bar turns use pe_atb_items.c; Panacea uses pe_panacea.c and pe_combine.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_atb_items.h"
#include "pe_panacea.h"

enum { RULES_VANILLA, RULES_REBALANCED, RULES_CUSTOM };
static const char *const s_rules_names[] = { "Vanilla", "Rebalanced", "Custom" };
static int s_rules = RULES_REBALANCED, s_rules_shown = -1;
static int s_heal1, s_heal2, s_heal3, s_cost_detox, s_cost_scan, s_scan_summary;

typedef struct { const char *label, *desc; int vanilla, rebalanced, min, max, step; int *value; int toggle; } PeRow;
static PeRow s_rows[] = {
    { "Panacea", "Combine Medicine 4 and Full Cure for a healing cure.", 0, 1, 0, 1, 1, &g_pe_panacea, 1 },
    { "Half-bar turns", "Gold bar: chosen actions at half.", 0, 1, 0, 1, 1, &g_pe_half_enabled, 1 },
    { "Half bar: Items", "Use items from a half bar.", 0, 1, 0, 1, 1, &g_pe_half_allow[0], 1 },
    { "Half bar: PE quick powers", "Quick powers cost half a bar.", 0, 1, 0, 1, 1, &g_pe_half_allow[1], 1 },
    { "Half bar: change weapon", "Swap weapons from a half bar.", 0, 1, 0, 1, 1, &g_pe_half_allow[2], 1 },
    { "Half bar: change armor", "Swap armor from a half bar.", 0, 0, 0, 1, 1, &g_pe_half_allow[3], 1 },
    { "Half bar: escape", "Try to escape from a half bar.", 0, 0, 0, 1, 1, &g_pe_half_allow[4], 1 },
    { "Heal 1 % of max HP", "0 = vanilla (+30 HP).", 0, 15, 0, 100, 5, &s_heal1, 0 },
    { "Heal 2 % of max HP", "0 = vanilla (+60 HP).", 0, 30, 0, 100, 5, &s_heal2, 0 },
    { "Heal 3 % of max HP", "0 = vanilla (+280 HP).", 0, 65, 0, 100, 5, &s_heal3, 0 },
    { "Detox PE cost", "Vanilla 100.", 100, 60, 0, 300, 10, &s_cost_detox, 0 },
    { "Scan PE cost", "Vanilla 50.", 50, 30, 0, 300, 10, &s_cost_scan, 0 },
    { "Scan: weak & strong", "Scan lists weaknesses and resists.", 0, 1, 0, 1, 1, &s_scan_summary, 1 },
};
#define PE_ROWS ((int)(sizeof s_rows / sizeof s_rows[0]))

static int s_quick[20];
static const struct { int index; const char *label; int rebalanced; } s_powers[] = {
    {0, "Heal 1", 1}, {1, "Heal 2", 1}, {2, "Heal 3", 0}, {3, "Detox", 1}, {4, "Medic", 1},
    {5, "Barrier", 0}, {6, "Energy Shot", 0}, {7, "Scan", 1}, {8, "Slow", 1}, {9, "Haste", 0},
    {10, "Confuse", 1}, {11, "Gene Heal", 0}, {12, "Preraise", 0}, {18, "Full Recover", 0},
    {19, "Liberate", 0},
};
#define PE_POWERS ((int)(sizeof s_powers / sizeof s_powers[0]))
static const char *const s_quick_names[] = { "Full bar", "Half bar" };

static void pe_sync_quick(void) {
    uint32_t mask = 0;
    for (int i = 0; i < 20; ++i) if (s_quick[i]) mask |= 1u << i;
    g_pe_quick_mask = mask;
}

static void pe_apply(int rules) {
    int i;
    if (rules == RULES_CUSTOM) return;
    for (i = 0; i < PE_ROWS; ++i) *s_rows[i].value = rules == RULES_VANILLA ? s_rows[i].vanilla : s_rows[i].rebalanced;
    for (i = 0; i < PE_POWERS; ++i) s_quick[s_powers[i].index] = rules == RULES_VANILLA ? 0 : s_powers[i].rebalanced;
}

static int pe_matches(int rules) {
    int i;
    for (i = 0; i < PE_ROWS; ++i)
        if (*s_rows[i].value != (rules == RULES_VANILLA ? s_rows[i].vanilla : s_rows[i].rebalanced)) return 0;
    for (i = 0; i < PE_POWERS; ++i)
        if (s_quick[s_powers[i].index] != (rules == RULES_VANILLA ? 0 : s_powers[i].rebalanced)) return 0;
    return 1;
}

static void pe_balance_tick(void) {
    if (s_rules != s_rules_shown) { pe_apply(s_rules); s_rules_shown = s_rules; }
    else if (s_rules != RULES_CUSTOM && !pe_matches(s_rules)) s_rules = s_rules_shown = RULES_CUSTOM;
    else if (s_rules == RULES_CUSTOM && pe_matches(RULES_REBALANCED)) s_rules = s_rules_shown = RULES_REBALANCED;
    else if (s_rules == RULES_CUSTOM && pe_matches(RULES_VANILLA)) s_rules = s_rules_shown = RULES_VANILLA;
    pe_sync_quick();
}

/* Scan summary, from the target's statusFlags2 pairs (0 average, 1 resists, 2 weak). */
static void pe_scan_summary(BattleEntity *target) {
    static const struct { int shift; const char *name; } pairs[] = {
        { 14, "Heat" }, { 16, "Cold" }, { 0, "Poison" }, { 2, "Acid" }, { 4, "Tranq" },
        { 10, "Confuse" }, { 8, "Slow" }, { 12, "Steal" },
    };
    const EnemyCombatant *core;
    char weak[96] = "", strong[96] = "", text[200];
    uint32_t s2;
    unsigned i;
    if (!target || target == pe_player() || !(core = (const EnemyCombatant *)target->core)) return;
    s2 = core->statusFlags2;
    for (i = 0; i < sizeof pairs / sizeof pairs[0]; ++i) {
        const unsigned v = (s2 >> pairs[i].shift) & 3u;
        char *dst = v == 2u ? weak : v == 1u ? strong : NULL;
        if (!dst) continue;
        if (dst[0]) strncat(dst, ", ", 95 - strlen(dst));
        strncat(dst, pairs[i].name, 95 - strlen(dst));
    }
    snprintf(text, sizeof text, "Scan  Weak: %s   Strong: %s", weak[0] ? weak : "none", strong[0] ? strong : "none");
    pe_toast(text);
    pe_log("scan", "status2=0x%05X weak=%s strong=%s", (unsigned)(s2 & 0x3FFFFu), weak, strong);
}

static int pe_on_spell(void *ctx, void *user) {
    static const int vanilla_heal[3] = { 30, 60, 280 };
    const PeHookSpell *spell = (const PeHookSpell *)ctx;
    Combatant *core = pe_aya_core();
    int vanilla_cost = 0, cost = 0;
    (void)user;
    if (!core || (pe_in_battle() && g_ActiveActor != core)) return 0;
    if (spell->spell == 7u && s_scan_summary) pe_scan_summary(spell->target);
    if (!core) return 0;
    if (spell->spell <= 2u) {                          /* heals: a share of max HP, never below vanilla */
        const int pct = spell->spell == 0 ? s_heal1 : spell->spell == 1 ? s_heal2 : s_heal3;
        if (pct > 0) {
            const int want = core->maxHP * pct / 100;
            if (want > vanilla_heal[spell->spell]) core->curHP = (u16)(core->curHP + (want - vanilla_heal[spell->spell]));
        }
    }
    if (spell->spell == 3u) { vanilla_cost = 100; cost = s_cost_detox; }
    if (spell->spell == 7u) { vanilla_cost = 50; cost = s_cost_scan; }
    if (vanilla_cost && cost != vanilla_cost) {        /* refund the difference before the game charges */
        int32_t original = vanilla_cost * 0x10000, desired = cost * 0x10000;
        if (core->attributes && (core->attributes->effectFlags & 0x200u)) {
            original = original * 2 / 3;
            desired = desired * 2 / 3;
        }
        core->exp_or_acc = (s32)((u32)core->exp_or_acc + (u32)(original - desired));
    }
    return 0;
}

static int pe_power_cost(void *ctx, void *user) {
    PeHookPowerCost *query = ctx;
    (void)user;
    /* Vanilla leaves the game's value (and other mods' changes) untouched. */
    if (query->power == 3 && s_cost_detox != 100) *query->cost = s_cost_detox;
    if (query->power == 7 && s_cost_scan != 50) *query->cost = s_cost_scan;
    return 0;
}

#define PE_PAGE PE_PAGE_PE_BALANCE
void pe_pe_balance_register(const char *mod_id) {
    int i;
    pe_apply(RULES_REBALANCED);
    pe_menu_register_choice(PE_PAGE, "Rules", "Vanilla, Rebalanced, or Custom.", &s_rules, s_rules_names, 3);
    for (i = 0; i < PE_ROWS; ++i) {
        if (s_rows[i].toggle)
            pe_menu_register_toggle(PE_PAGE, s_rows[i].label, s_rows[i].desc, s_rows[i].value, NULL);
        else
            pe_menu_register_int(PE_PAGE, s_rows[i].label, s_rows[i].desc, s_rows[i].value,
                                 s_rows[i].min, s_rows[i].max, s_rows[i].step);
    }
    for (i = 0; i < PE_POWERS; ++i)
        pe_menu_register_choice(PE_PAGE_QUICK_POWERS, s_powers[i].label,
            "Half bar costs less ATB; shooting powers still cost a full bar.",
            &s_quick[s_powers[i].index], s_quick_names, 2);
    pe_sync_quick();
    /* Restore the selected preset after all saved individual rows have loaded. */
    pe_apply(s_rules);
    s_rules = pe_matches(RULES_REBALANCED) ? RULES_REBALANCED : pe_matches(RULES_VANILLA) ? RULES_VANILLA : RULES_CUSTOM;
    s_rules_shown = s_rules;
    pe_menu_register_tick(pe_balance_tick);
    pe_hook_add(PE_HOOK_POWER_COST, pe_power_cost, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SPELL, pe_on_spell, NULL, PE_PRIORITY_NORMAL, mod_id);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_rebalance.h"
int pe_rb_pebalance_walk(const unsigned char*in,unsigned char*out,int apply){if((!in&&!out)||(0))return 0;if(apply&&(!in||!pe_rb_pebalance_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};
PE_CK_S(w,s_rules,0,2,1);PE_CK_S(w,s_rules_shown,-1,2,0);for(int i=0;i<PE_ROWS;i++){int value=*s_rows[i].value;PE_CK_S(w,value,s_rows[i].min,s_rows[i].max,1);}for(int i=0;i<20;i++)PE_CK_S(w,s_quick[i],0,1,1);
return pe_rb_finish(&w);}
#endif
