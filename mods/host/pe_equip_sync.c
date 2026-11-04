/*
 * Equipment cache sync: battles read Aya's gun and armor from caches (g_SavedBattleStateTail,
 * g_BattleEquipStateBlock, the armor's stat modifiers) that the game rebuilds only when a room
 * loads or the equip menu changes her gear. Anything else that edits the equipped records -- Mod
 * Catalog installs and removals, Tool transfers, the item editor -- would reach battle only after
 * the next room change (recomp play test 2026-10-20: RoF 7 + Attack All installed, the next
 * fight in the same room fired plain single shots).
 *
 * In free field, whenever the equipped gun or armor record (or which one is equipped) differs
 * from what the caches were last built from, this runs the game's own rebuild, as a room load
 * does (Battle_InitEquipLists): the armor modifiers, Aya's derived stats, the weapon list and the
 * armor list. The native build lets us call those directly instead of re-implementing them, as
 * the recomp build had to.
 *
 * One guard: a smaller Bag+ (an armor mod removed) never strands items in slots past the new bag
 * size; the bag stays as large as it was until those slots are emptied.
 *
 * A host feature (pe_equip_sync_init, pe_game.h): every mod that edits the equipped records needs it.
 */
#include <string.h>

#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe1/battle_modifiers.h"
#include "pe1/battle_saved_state.h"
#include "pe1/inventory_state.h"
#include "pe1/weapon_list_output.h"

typedef struct {
    s8 weapon_slot, armor_slot;
    ItemDataRecord weapon, armor;
} PeEquipSignature;

static PeEquipSignature s_built;
static int s_have_built;

static void pe_signature(PeEquipSignature *sig) {
    const ItemDataRecord *weapon = pe_equipped(0), *armor = pe_equipped(1);
    memset(sig, 0, sizeof *sig);
    sig->weapon_slot = g_AyaSaveState.equipped_weapon_slot;
    sig->armor_slot = g_AyaSaveState.equipped_armor_slot;
    if (weapon) sig->weapon = *weapon;
    if (armor) sig->armor = *armor;
}

/* Bag slots from `from` up to `to` (exclusive) hold something. */
static int pe_bag_slots_used(int from, int to) {
    int i;
    for (i = from; i < to && i < 50; ++i)
        if (g_AyaSaveState.inventory_items[i]) return 1;
    return 0;
}

static void pe_rebuild(void) {
    const int bonus_before = g_InvBonusSlotCount;
    const int base = g_AyaSaveState.inventory_slot_count;
    BattleCmd_LoadWeaponModifiers();
    if (g_InvBonusSlotCount < bonus_before && pe_bag_slots_used(base + g_InvBonusSlotCount, base + bonus_before))
        g_InvBonusSlotCount = bonus_before;               /* never strand items past a smaller bag */
    Inv_RecalcSlotStats();
    Inv_BuildWeaponList(0, &g_SavedBattleStateTail.weapon);
    Inv_BuildArmorList(&g_BattleEquipStateBlock);
}

static void pe_equip_sync_tick(void) {
    PeEquipSignature now;
    Combatant *aya = pe_aya_core();
    if (!pe_game_loaded() || !aya || !pe_free_field() || pe_in_battle()) return;
    /* Only while Aya's battle record points at the caches, as the game sets it up. */
    if (aya->action != (BattleAction *)&g_SavedBattleStateTail || aya->attributes != &g_BattleEquipStateBlock) return;
    pe_signature(&now);
    if (!s_have_built) {                                  /* a load or room change built them */
        s_built = now;
        s_have_built = 1;
        return;
    }
    if (memcmp(&now, &s_built, sizeof now) == 0) return;
    pe_rebuild();
    s_built = now;
    pe_log("equip_sync", "weapon_slot=%d armor_slot=%d", now.weapon_slot, now.armor_slot);
}

/* A scene start or a load rebuilds the caches itself: start comparing from there. */
static int pe_on_reset(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    s_have_built = 0;
    return 0;
}

static int pe_on_frame(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    pe_equip_sync_tick();
    return 0;
}

void pe_equip_sync_init(void) {
    pe_hook_add(PE_HOOK_FRAME, pe_on_frame, NULL, PE_PRIORITY_LATE + 50, "host");   /* after every mod's edits */
    pe_hook_add(PE_HOOK_SCENE_START, pe_on_reset, NULL, PE_PRIORITY_NORMAL, "host");
    pe_hook_add(PE_HOOK_SAVE_READ, pe_on_reset, NULL, PE_PRIORITY_NORMAL, "host");
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
/* Original SAVE_READ/SCENE_START reset semantics: learn restored caches on next tick.
 * Never dispatch a general mod hook or rebuild gameplay stats during restoration. */
void pe_equip_sync_state_invalidate(void){s_have_built=0;}
#endif
