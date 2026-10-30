/* Stacked items, from the Pathogen Eve port: one bag or storage slot per consumable, holding up to
 * 99 copies.
 *
 * Every list keeps plain item ids, so the game's own code still sees one copy per slot; the extra
 * copies of a slot are counted per (list, item id) here. Stackable items are the base items
 * Inv_AddItem stores as plain ids (kinds 10 and 12..15: medicine, cures, boosters, tools, cards);
 * weapons, armor and key items never stack. With pe_economy running, Junk stays out of the stacks:
 * the economy owns it.
 *
 *   add      PE_HOOK_ITEM_ADD: an item already carried joins its stack in the bag.
 *   use      PE_HOOK_ITEM_REMOVE: using, giving or discarding takes one copy off the stack.
 *   count    PE_HOOK_ITEM_COUNT and INV_CMD_COUNT_ITEM: story and shop checks see every copy.
 *   storage  PE_HOOK_STORAGE_TRANSFER: a bag stack moves into storage whole; taking from a storage
 *            stack takes one copy (onto the bag's stack when there is one).
 *   merge    PE_HOOK_INVENTORY_REBUILD: duplicates of a stackable item fold into their first slot.
 *   draw     PE_HOOK_SLOT_DRAWN: the count is drawn where a weapon shows its rounds.
 *
 * Saved by the host as the "pe_stacks.counts" record: (item, extra copies) pairs per list. With
 * the mod off, or the option switched off, the extra copies go back into free slots of their list
 * as plain items; what doesn't fit stays counted. */
#include <stdio.h>
#include <string.h>

#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_save.h"
#include "pe_services.h"
#include "pe1/battle_update.h"
#include "pe1/draw_state.h"
#include "pe1/inventory.h"
#include "pe1/inventory_queries.h"
#include "pe1/inventory_slots.h"
#include "pe1/inventory_state.h"
#include "pe1/menu_context_help.h"
#include "pe1/menu_inventory.h"
#include "pe1/text.h"

#define MOD_ID "pe_stacks"

enum { PE_STACK_MAX = 99, PE_LIST_BAG, PE_LIST_STORAGE, PE_LIST_SPECIAL, PE_LISTS = 3 };
enum { PE_ITEM_JUNK = 0x14, PE_STACK_PAIRS = 100 };

/* Extra copies (beyond the slot's own) per list and item id. */
static uint8_t s_extra[PE_LISTS][256];
static int s_enabled = 1;

/* The saved form: count[l] pairs for list l, lists in order. */
static struct {
    uint8_t count[PE_LISTS], pad;
    uint8_t pair[PE_STACK_PAIRS][2];
} s_rec;
_Static_assert(sizeof s_rec == 204, "pe_stacks.counts v1 holds these 204 bytes: raise the version for a new layout");

/* ---- lists ---- */

static s16 *pe_list(int l) {
    return l == 0 ? g_AyaInventoryItems : l == 1 ? g_WayneStorageItems : g_InvItemArray;
}
static int pe_list_size(int l) {
    int n;
    if (l == 1) return 100;
    if (l == 2) return 82;
    n = Inv_GetAyaSlotLimit();
    return n < 0 ? 0 : n > 50 ? 50 : n;
}
static int pe_list_of(const s16 *list) {
    for (int l = 0; l < PE_LISTS; ++l) if (list == pe_list(l)) return l;
    return -1;
}

static int pe_economy_junk(void) {
    const PeJunkApi *junk = pe_service_get("junk", 1);
    return junk && pe_mod_active("pe_economy") && junk->enabled();
}
static int pe_stackable(int id) {
    const ItemDataRecord *rec;
    if (id < 1 || id > 0xFF) return 0;
    if (id == PE_ITEM_JUNK && pe_economy_junk()) return 0;
    rec = Item_LookupBaseData((unsigned)(id - 1));
    return rec && (rec->kind == 10 || (rec->kind >= 12 && rec->kind <= 15));
}
static int pe_on(void) { return s_enabled && pe_save_loaded(); }

static int pe_find(int l, int id) {
    const s16 *list = pe_list(l);
    for (int i = 0, n = pe_list_size(l); i < n; ++i) if (list[i] == id) return i;
    return -1;
}
/* Copies on slot `index` of list `l` (1 for anything that is not a stack). */
static int pe_count(int l, int index) {
    int id;
    if (index < 0 || index >= pe_list_size(l)) return 1;
    id = pe_list(l)[index];
    if (id < 1 || id > 0xFF || pe_find(l, id) != index) return 1;
    return 1 + s_extra[l][id];
}
static int pe_join(int l, int id) {
    if (!pe_stackable(id) || pe_find(l, id) < 0 || 1 + s_extra[l][id] >= PE_STACK_MAX) return 0;
    s_extra[l][id]++;
    return 1;
}

/* Fold duplicate consumables into their first slot and drop counts of ids no longer there. */
static void pe_merge(int l) {
    uint8_t seen[256];
    s16 *list = pe_list(l);
    memset(seen, 0, sizeof seen);
    for (int i = 0, n = pe_list_size(l); i < n; ++i) {
        const int id = list[i];
        if (!pe_stackable(id)) continue;
        if (!seen[id]) { seen[id] = 1; continue; }
        if (1 + s_extra[l][id] < PE_STACK_MAX) { s_extra[l][id]++; list[i] = 0; }
    }
    for (int id = 1; id < 256; ++id) if (!seen[id]) s_extra[l][id] = 0;
}
static void pe_merge_all(void) { for (int l = 0; l < PE_LISTS; ++l) pe_merge(l); }

/* Extra copies of every stack back into free slots of their list, as plain items. Returns how
 * many stay counted (the list is full). */
static int pe_unstack(uint8_t extra[PE_LISTS][256]) {
    int left = 0;
    for (int l = 0; l < PE_LISTS; ++l) {
        s16 *list = pe_list(l);
        const int n = pe_list_size(l);
        for (int id = 1; id < 256; ++id) {
            for (int i = 0; i < n && extra[l][id]; ++i)
                if (!list[i]) { list[i] = (s16)id; extra[l][id]--; }
            left += extra[l][id];
        }
    }
    return left;
}

/* ---- game hooks ---- */

static int pe_on_add(void *ctx, void *user) {
    (void)user;
    if (!pe_on() || g_InvItemPtr != g_AyaInventoryItems) return 0;
    return pe_join(0, *(s32 *)ctx);   /* handled: Inv_AddItem returns success */
}

static int pe_on_remove(void *ctx, void *user) {
    PeHookItemRemove *r = ctx;
    int l, id;
    (void)user;
    if (!pe_on() || (l = pe_list_of(g_InvItemPtr)) < 0) return 0;
    if (r->index < 0 || r->index >= pe_list_size(l)) return 0;
    id = g_InvItemPtr[r->index];
    if (!pe_stackable(id) || !s_extra[l][id] || pe_find(l, id) != r->index) return 0;
    s_extra[l][id]--;
    r->result = id;
    return 1;
}

static int pe_bag_extra(int id) {
    return g_InvItemPtr == g_AyaInventoryItems && id >= 1 && id <= 0xFF && pe_find(0, id) >= 0 ? s_extra[0][id] : 0;
}
static int pe_on_count(void *ctx, void *user) {
    int **args = ctx, extra;
    (void)user;
    if (!pe_on() || !args || !args[0] || !args[1] || !(extra = pe_bag_extra(*args[0]))) return 0;
    *args[1] = Inv_CountByValue(*args[0]) + extra;
    return 1;
}
static int pe_on_command(void *ctx, void *user) {
    PeHookInvCommand *c = ctx;
    int extra;
    (void)user;
    if (!pe_on() || c->command != INV_CMD_COUNT_ITEM || !(extra = pe_bag_extra(c->value))) return 0;
    c->result = Inv_CountByValue(c->value) + extra;
    return 1;
}

static int pe_on_rebuild(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (pe_on()) pe_merge_all();
    return 0;
}

/* `copies` of `id` into storage list `l`: onto its stack, else into slot `prefer` when empty,
 * else any empty slot. Nothing moves unless all of them fit. */
static int pe_store_copies(int l, int id, int copies, int prefer) {
    s16 *list = pe_list(l);
    int at = pe_find(l, id), room;
    if (at < 0) {
        at = prefer >= 0 && prefer < pe_list_size(l) && !list[prefer] ? prefer : pe_find(l, 0);
        if (at < 0) return 0;
        room = PE_STACK_MAX;
    } else {
        room = PE_STACK_MAX - 1 - s_extra[l][id];
    }
    if (copies > room) return 0;
    if (!list[at]) { list[at] = (s16)id; s_extra[l][id] = 0; copies--; }
    s_extra[l][id] += (uint8_t)copies;
    return 1;
}

/* Storage stack on `index` -> bag slot `bag`: one copy. The bag slot's own item (all of its
 * copies) goes into storage. 1 = done, 0 = not a stack (the game's swap), -1 = no room. */
static int pe_take_one(int l, int index, int bag) {
    s16 *store = pe_list(l);
    const int id = store[index], held = g_AyaInventoryItems[bag];
    if (!pe_stackable(id) || !s_extra[l][id] || pe_find(l, id) != index) return 0;
    if (held == id) return -1;
    if (held && pe_stackable(held)) {
        if (!pe_store_copies(l, held, pe_count(0, bag), -1)) return -1;
        s_extra[0][held] = 0;
    } else if (held) {
        const int k = pe_find(l, 0);
        if (k < 0) return -1;
        store[k] = (s16)held;
    }
    s_extra[l][id]--;
    g_AyaInventoryItems[bag] = (s16)id;
    return 1;
}

static int pe_on_transfer(void *ctx, void *user) {
    PeHookStorageTransfer *t = ctx;
    s16 *store = g_InvActiveItemList;
    const ItemDataRecord *rec;
    int l, bag, at, id;
    (void)user;
    if (!pe_on() || t->from_mode == t->to_mode || !store || (l = pe_list_of(store)) < 1) return 0;
    if (t->from_mode == 0x34) {                 /* storage -> bag */
        at = t->from_index;
        bag = Inv_GetWayneListItemByIndex(t->to_index);
    } else {                                    /* bag -> storage */
        at = t->to_index;
        bag = Inv_GetWayneListItemByIndex(t->from_index);
    }
    if (bag < 0 || bag >= pe_list_size(0) || at < 0 || at >= pe_list_size(l)) return 0;
    /* The game's own refusals (equipped gear, key items) stay the game's. */
    rec = Inv_LookupItemRecordScaled(store[at]);
    if (!Inv_IsSlotEquipped(bag) || (rec && rec->kind >= 19 && rec->kind < 22)) return 0;
    if (t->from_mode == 0x34) {
        id = store[at];
        if (pe_stackable(id) && pe_find(0, id) >= 0 && pe_find(l, id) == at &&
            1 + s_extra[0][id] < PE_STACK_MAX) {
            /* the bag has a stack of it already: one copy joins it */
            if (s_extra[l][id]) s_extra[l][id]--; else store[at] = 0;
            s_extra[0][id]++;
        } else {
            const int r = pe_take_one(l, at, bag);
            if (r == 0) return 0;
            if (r < 0) { t->result = 0; return 1; }
        }
    } else {
        id = g_AyaInventoryItems[bag];
        if (pe_stackable(id) && (pe_count(0, bag) > 1 || pe_find(l, id) >= 0)) {
            /* the whole bag stack, onto the storage stack or into the chosen slot */
            if (!pe_store_copies(l, id, pe_count(0, bag), at)) { t->result = 0; return 1; }
            g_AyaInventoryItems[bag] = 0;
            s_extra[0][id] = 0;
        } else {
            const int r = pe_take_one(l, at, bag);
            if (r == 0) return 0;
            if (r < 0) { t->result = 0; return 1; }
        }
    }
    Menu_SetSwapReturnFlag();
    Inv_RebuildSelectableMask();
    t->result = 1;
    return 1;
}

static int pe_on_slot_drawn(void *ctx, void *user) {
    const PeHookSlotDrawn *s = ctx;
    int l, n;
    (void)user;
    if (!pe_on() || (l = pe_list_of(s->list)) < 0 || (n = pe_count(l, s->index)) < 2) return 0;
    Draw_StatePush();
    Draw_OffsetCursor(0x56, 5);   /* where a weapon's rounds go; consumables leave it empty */
    Draw_PrintNumberWidth3Unk(n);
    Draw_StatePop();
    return 0;
}

/* ---- saves ---- */

static void pe_pack(void) {
    unsigned k = 0, lost = 0;
    memset(&s_rec, 0, sizeof s_rec);
    for (int l = 0; l < PE_LISTS; ++l)
        for (int id = 1; id < 256; ++id) {
            if (!s_extra[l][id]) continue;
            if (k == PE_STACK_PAIRS) { lost += s_extra[l][id]; continue; }
            s_rec.pair[k][0] = (uint8_t)id;
            s_rec.pair[k][1] = s_extra[l][id];
            s_rec.count[l]++;
            k++;
        }
    if (lost) {
        pe_mod_log(MOD_ID, "more than %d stacks: %u copies not saved", PE_STACK_PAIRS, lost);
        pe_toast("Too many item stacks to save them all: some copies were lost");
    }
}
static void pe_unpack(void) {
    unsigned k = 0;
    memset(s_extra, 0, sizeof s_extra);
    for (int l = 0; l < PE_LISTS; ++l)
        for (unsigned i = 0; i < s_rec.count[l]; ++i, ++k) s_extra[l][s_rec.pair[k][0]] = s_rec.pair[k][1];
}

static int pe_on_save(void *ctx, void *user) { (void)ctx; (void)user; pe_pack(); return 0; }
static int pe_on_load(void *ctx, void *user) {
    (void)ctx; (void)user;
    pe_unpack();
    pe_merge_all();
    return 0;
}
static int pe_on_scene(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (pe_save_loaded()) pe_pack();   /* crash reports copy the record as it stands */
    return 0;
}
static int pe_on_new_game(void *ctx, void *user) {
    (void)ctx; (void)user;
    memset(s_extra, 0, sizeof s_extra);
    return 0;
}

static int pe_rec_check(void *data) {
    unsigned total = 0;
    (void)data;
    for (int l = 0; l < PE_LISTS; ++l) total += s_rec.count[l];
    if (total > PE_STACK_PAIRS) return 1;
    for (unsigned k = 0; k < total; ++k)
        if (!s_rec.pair[k][0] || !s_rec.pair[k][1] || s_rec.pair[k][1] >= PE_STACK_MAX) return 1;
    return 0;
}
/* The mod is off this session: the copies only the record holds go back as plain items. */
static void pe_rec_release(void *data) {
    int left;
    (void)data;
    pe_unpack();
    left = pe_unstack(s_extra);
    pe_pack();
    pe_mod_log(MOD_ID, "mod off: stacked copies returned to their lists, %d left (lists full)", left);
    if (left) pe_toast("Some stacked items didn't fit: turn Stacked items back on to keep them");
}
static const PeSaveRecord s_record = {
    "pe_stacks.counts", 1, &s_rec, sizeof s_rec, pe_rec_check, NULL, NULL, pe_rec_release,
};

/* ---- rows ---- */

/* Option switched off: unstack in free field as space allows. */
static void pe_stacks_tick(void) {
    if (s_enabled || !pe_save_loaded() || !pe_free_field()) return;
    for (int l = 0; l < PE_LISTS; ++l)
        for (int id = 1; id < 256; ++id)
            if (s_extra[l][id]) { (void)pe_unstack(s_extra); return; }
}

static void pe_stacks_total(char *out, unsigned size) {
    unsigned n = 0;
    for (int l = 0; l < PE_LISTS; ++l) for (int id = 1; id < 256; ++id) n += s_extra[l][id];
    snprintf(out, size, "%u", n);
}

static void pe_stacks_fill(void) {
    if (!pe_on()) return;
    for (int id = 1; id < 256; ++id)
        if (pe_stackable(id) && pe_find(0, id) >= 0) s_extra[0][id] = PE_STACK_MAX - 1;
    pe_toast("Every stack in the bag is now 99");
}

static int stacks_init(void) {
    pe_hook_add(PE_HOOK_ITEM_ADD, pe_on_add, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_ITEM_REMOVE, pe_on_remove, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_ITEM_COUNT, pe_on_count, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_INV_COMMAND, pe_on_command, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_INVENTORY_REBUILD, pe_on_rebuild, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_STORAGE_TRANSFER, pe_on_transfer, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_SLOT_DRAWN, pe_on_slot_drawn, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_SAVE_WRITE, pe_on_save, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_on_load, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_SCENE_START, pe_on_scene, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_on_new_game, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_save_register(&s_record);

    pe_menu_register_toggle(PE_PAGE_INVENTORY, "Stacked items",
                            "Up to 99 of a consumable per bag or storage slot. Off unstacks as space allows.",
                            &s_enabled, pe_stacks_tick);
    pe_menu_register_info(PE_PAGE_INVENTORY, "Stacked copies", "Copies held in stacks beyond each slot's own.",
                          pe_stacks_total);
    pe_menu_register_action(PE_PAGE_DEBUG, "Fill bag stacks", "Every stack in Aya's bag to 99.", pe_stacks_fill);
    return 0;
}

const PeMod pe_mod_pe_stacks = {MOD_ID, "PE Enhancements: stacked items", PE_ENHANCEMENTS_VERSION, NULL, stacks_init, NULL};
