/*
 * Weapon damage per second in the game's own menu, ported from the psxrecomp build
 * (src/mods/pe_weapon_power.c).
 *
 * When a gun is highlighted, the menu title bar shows its name (Menu_DrawContextHelpText ->
 * Draw_PrintRawText(Text_LookupTableC(itemId - 1))). PE_HOOK_HELP_TEXT hands us that line before
 * it is printed; for a gun's name we swap in a copy with "  DPS n" appended.
 *
 * Damage, from Battle_StepEnemyMovement:
 *   per shot = (Aya attack / 5 + gun attack) * shotPercent[shots] / 100
 *   a turn is 1, 2 or 3 volleys (mod effects 19 / 20), with reloads: capacity C gives
 *   ceil(C / shots) firing commands plus one reload per C rounds (melee never reloads);
 *   plus the rebalance's gun bonus and installed-RoF volley % (pe_mod_effects.c).
 * Time: Aya's gauge fills 0..9000 by scaledOffense a battle frame, set to stat22 * 4 / 10 after
 * a gun attack, so a turn takes C / (stat22 * 0.4) seconds with C = 9000 / battle frames a
 * second. C is measured (a clean empty-to-full fill of the gauge in battle, timed in VBlanks:
 * game time, whatever the host speed); until then 300 (the battle loop's 30 frames a second).
 *
 * The gun's record: the equipped gun if the name matches it, else the first matching gun in
 * Aya's bag, else the item's base data.
 */
#include <stdint.h>
#include <stdio.h>
#include <psyz/video.h>

#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_effects.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe1/menu_context_help.h"
#include "pe1/weapon_list_output.h"

#define PE_ITEM_COUNT 0xEBu
#define PE_TEXT_MAX 64
#define PE_GAUGE_FULL 9000u
#define PE_VBLANKS_X10_PER_SEC 599u /* NTSC 59.94 */
#define PE_DEFAULT_CLOCK_X10 3000u  /* C x10: 9000 / 30 fps */

static int s_show_power = 1;
static u8 s_text[PE_TEXT_MAX];
static uint32_t s_clock_x10 = PE_DEFAULT_CLOCK_X10, s_clock_samples;

/* Item id whose name lives at `text`, or 0 (the last hit is checked first). */
static unsigned pe_item_for_name(const u8 *text) {
    static unsigned s_last;
    unsigned id;
    if (s_last && Text_LookupTableC(s_last - 1u) == text) return s_last;
    for (id = 1; id <= PE_ITEM_COUNT; ++id)
        if (Text_LookupTableC(id - 1u) == text) return s_last = id;
    return 0;
}

static unsigned pe_put_number(u8 *out, unsigned n, unsigned value) {
    char digits[6];
    unsigned count = 0;
    do { digits[count++] = (char)(value % 10u); value /= 10u; } while (value && count < 6);
    while (count && n < PE_TEXT_MAX - 1) out[n++] = (u8)digits[--count];
    return n;
}

static const ItemDataRecord *pe_record_for_gun(unsigned id) {
    const ItemDataRecord *rec = pe_equipped(0);
    int slot;
    if (rec && rec->itemId == id) return rec;
    for (slot = 0; slot < 50; ++slot) {
        const int v = g_AyaSaveState.inventory_items[slot];
        if (v >= 0x100 && v < 0x180 && g_InventoryRuntime.equipment[v - 0x100].itemId == id)
            return &g_InventoryRuntime.equipment[v - 0x100];
    }
    return pe_base_item(id);
}

/* Damage per second x10 (sustained: commands, reloads, Aya's turn speed). */
static unsigned pe_dps_x10(const ItemDataRecord *rec) {
    static const uint8_t shot_percent[11] = { 0, 100, 60, 41, 0, 25, 0, 18, 0, 0, 13 };
    int attack = rec->baseStats[0] + rec->bonusStats[0], capacity, installed_rof = 0, count, i, volley, per_shot;
    unsigned shots = 1, commands = 1, aya_attack = 0, stat22 = 50;
    const ItemDataRecord *base = pe_base_item(rec->itemId);
    Combatant *core = pe_aya_core();
    uint64_t turn_x10;
    if (attack > 999) attack = 999;
    attack += pe_fx_attack_bonus(rec->itemId);
    capacity = rec->baseStats[2] + rec->bonusStats[2];
    if (capacity > 999) capacity = 999;
    count = rec->tailCount > 10 ? 10 : rec->tailCount;
    for (i = 0; i < count; ++i) {
        const unsigned effect = rec->tailData[i] & 31u;
        if (effect >= 1 && effect <= 5) shots = g_WeaponEffectCodes[effect];
        if (effect >= 3 && effect <= 5) installed_rof = !pe_rec_has_effect(base, (int)effect);
        if (effect == 16) attack >>= 1;
        if (effect == 19) commands = 2;
        if (effect == 20) commands = 3;
    }
    if (shots > 10 || shot_percent[shots] == 0) shots = 1;
    if (core) {
        aya_attack = (u16)core->stat1E;
        if (core->stat22 >= 1 && core->stat22 < 1000) stat22 = core->stat22;
    }
    volley = installed_rof && shots >= 5u ? pe_fx_rof_volley_pct() : 0;
    per_shot = volley > 0 ? ((int)(aya_attack / 5u) + attack) * volley / (int)(shots * 100u)
                          : ((int)(aya_attack / 5u) + attack) * shot_percent[shots] / 100;
    if (per_shot <= 0) return 0;
    if (rec->kind == 8 || capacity <= 0) {
        turn_x10 = (uint64_t)commands * shots * (unsigned)per_shot * 10u;
    } else {
        const unsigned cycle = ((unsigned)capacity + shots - 1u) / shots + 1u; /* + the reload */
        turn_x10 = (uint64_t)commands * (unsigned)capacity * (unsigned)per_shot * 10u / cycle;
    }
    return (unsigned)(turn_x10 * stat22 * 4u / s_clock_x10);
}

/* Every frame: time a steady fill of Aya's gauge from empty to full, in VBlanks. */
static void pe_clock_tick(void) {
    static uint32_t start, last_change;
    static unsigned prev_gauge, rate;
    static int timing, clean;
    const uint32_t vblanks = (uint32_t)Psyz_VideoVSync(-1);
    Combatant *core = pe_aya_core();
    unsigned gauge, fill;
    if (!pe_game_loaded() || !pe_in_battle() || !core) { timing = 0; return; }
    gauge = core->atb;
    fill = core->scaledOffense;
    if (gauge < prev_gauge || (gauge == 0 && !timing)) {  /* emptied: an action */
        timing = 1; clean = 1; start = vblanks; last_change = vblanks; rate = fill;
    } else if (timing) {
        if (gauge != prev_gauge) last_change = vblanks;
        if ((core->stateFlags & 0x1C0) || fill != rate || fill == 0) clean = 0; /* Haste / Slow / new rate */
        if (vblanks - last_change > 6u) clean = 0;                              /* paused (a menu) */
        if (gauge >= PE_GAUGE_FULL) {
            if (clean && vblanks > start) {
                const uint32_t c_x10 = (uint32_t)((uint64_t)(vblanks - start) * rate * 100u / PE_VBLANKS_X10_PER_SEC);
                if (c_x10 >= 500u && c_x10 <= 20000u) {
                    s_clock_x10 = s_clock_samples ? (s_clock_x10 * 3u + c_x10) / 4u : c_x10;
                    ++s_clock_samples;
                }
            }
            timing = 0;
        }
    }
    prev_gauge = gauge;
}

static int pe_on_help_text(void *ctx, void *user) {
    static const u8 label[] = { 0x0F, 0x0F, 0x13, 0x1F, 0x22, 0x0F }; /* "  DPS " */
    u8 **text = (u8 **)ctx;
    const ItemDataRecord *base;
    unsigned id, n = 0, i, dps;
    (void)user;
    if (!s_show_power || !pe_game_loaded() || !*text) return 0;
    if (!(id = pe_item_for_name(*text))) return 0;
    base = pe_base_item(id);
    if (!base || base->kind < 1 || base->kind > 8) return 0;  /* guns only */
    while (n < PE_TEXT_MAX - 12 && (*text)[n] != 0xFF) { s_text[n] = (*text)[n]; ++n; }
    for (i = 0; i < sizeof label; ++i) s_text[n++] = label[i];
    dps = pe_dps_x10(pe_record_for_gun(id));
    if (dps < 100u) {                                  /* one decimal below 10: "2.7" */
        n = pe_put_number(s_text, n, dps / 10u);
        s_text[n++] = 0x2F;                            /* '.' */
        n = pe_put_number(s_text, n, dps % 10u);
    } else {
        n = pe_put_number(s_text, n, (dps + 5u) / 10u);
    }
    s_text[n] = 0xFF;
    *text = s_text;
    return 0;
}

static void pe_turn_text(char *out, unsigned size) {
    const unsigned tenths = s_clock_x10 * 10u / (50u * 4u);
    snprintf(out, size, "%u.%u s%s", tenths / 10u, tenths % 10u, s_clock_samples ? "" : " (not measured yet)");
}

void pe_weapon_power_register(const char *mod_id) {
    pe_menu_register_toggle(PE_PAGE_COMBAT, "Weapon power (DPS)", "Shows gun damage per second in the item menu.",
                            &s_show_power, NULL);
    pe_menu_register_info(PE_PAGE_DEBUG, "DPS turn clock", "A turn after an attack at Active Time 1, measured in battle.",
                          pe_turn_text);
    pe_menu_register_tick(pe_clock_tick);
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_on_help_text, NULL, PE_PRIORITY_NORMAL, mod_id);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_rebalance.h"
int pe_rb_power_walk(const unsigned char*in,unsigned char*out,int apply){if((!in&&!out)||(0))return 0;if(apply&&(!in||!pe_rb_power_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};
PE_CK_S(w,s_show_power,0,1,1);/* Calibration and its function-static VSync stopwatch are display-only live measurements. Keep them, not stale clock epochs; text is regenerated. */
return pe_rb_finish(&w);}
#endif
