/* Key pocket from recomp pe_key_items.c, adapted to native query hooks.
 * Keys remain visible to story counts/removals even with a full bag or while
 * disabling the pocket. Nothing is parked outside the bag or save block. With the
 * whole mod off, a loaded save's pocket goes back into the bag (pe_key_release).
 * Saved by the host as the "pe_qol.keys" record (host/pe_save.h). The recomp's "PKEY" block is
 * still written at its old spot (the host's save window), so builds without the host's records
 * keep the pocket; saves from before, or saved by such a build since, load from it. */
#include <stdio.h>
#include "pe_inventory.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_save.h"
#include "pe_inventory_service.h"
#include "pe1/battle_update.h"
#include "pe1/text.h"

enum { PE_KEY_MAX = 32, PE_KEY_TAIL = 0x1C90 + 0x200 };
static struct { uint16_t count, pad, ids[PE_KEY_MAX]; } s_keys;
_Static_assert(sizeof s_keys == 68, "the PKEY block of older saves holds 68 bytes of this state");
static int s_enabled = 1, s_selected;
static char s_name[64];

static int pe_is_key(unsigned id) { return (id >= 0x38 && id <= 0x3E) || (id >= 0xC8 && id <= 0xDC); }
static int pe_find(unsigned id) {
    for (int i = 0; i < s_keys.count; ++i) if (s_keys.ids[i] == id) return i;
    return -1;
}
static void pe_remove(int i) {
    memmove(&s_keys.ids[i], &s_keys.ids[i + 1], (s_keys.count - i - 1) * sizeof s_keys.ids[0]);
    s_keys.ids[--s_keys.count] = 0;
}
static int pe_bag_count(unsigned id) {
    int n = 0;
    for (int i = 0; i < pe_inventory_limit(); ++i) n += (unsigned short)g_AyaInventoryItems[i] == id;
    return n;
}
static int pe_keys_take(uint16_t id) {
    if (!s_enabled || !pe_game_loaded() || !pe_is_key(id)) return 0;
    if (pe_find(id) >= 0) return 1;
    if (s_keys.count == PE_KEY_MAX) return 0;
    s_keys.ids[s_keys.count++] = id;
    pe_log("key_add", "item=0x%02X pocket=1", (unsigned)id);
    return 1;
}
static const PeKeysApi s_api = { pe_keys_take };

/* Pocketed keys back into Aya's bag as space allows (one already there just leaves the pocket).
 * Returns how many stay pocketed: the bag is full. */
static int pe_keys_return(int limit) {
    for (int k = 0; k < s_keys.count;) {
        if (pe_bag_count(s_keys.ids[k])) { pe_remove(k); continue; }
        int slot;
        for (slot = 0; slot < limit && g_AyaInventoryItems[slot]; ++slot) {}
        if (slot == limit) break;
        g_AyaInventoryItems[slot] = s_keys.ids[k]; pe_remove(k);
    }
    return s_keys.count;
}

static void pe_key_tick(void) {
    int limit, empty = 0;
    if (!pe_game_loaded()) return;
    limit = pe_inventory_limit();
    for (int i = 0; i < limit; ++i) empty += g_AyaInventoryItems[i] == 0;
    /* Found-list entries must work even with pe_economy absent or switched off. */
    if (s_enabled && !empty && g_InvActiveListOverride == g_InvActiveListItems) {
        int n = g_InvActiveListCount < 10 ? g_InvActiveListCount : 10;
        for (int i = 0; i < n; ++i)
            if (pe_keys_take((uint16_t)g_InvActiveListItems[i])) g_InvActiveListItems[i] = 0;
    }
    if (!pe_free_field()) return;
    if (s_enabled) {
        for (int i = 0; i < 50; ++i)
            if (pe_keys_take((uint16_t)g_AyaInventoryItems[i])) g_AyaInventoryItems[i] = 0;
    } else {
        pe_keys_return(limit);
    }
}

static int pe_key_add(void *ctx, void *user) {
    (void)user;
    return pe_keys_take((uint16_t)*(int *)ctx); /* handled: Inv_AddItem returns success */
}
static int pe_key_count(void *ctx, void *user) {
    int **args = ctx;
    (void)user;
    if (!pe_game_loaded() || !args || !args[0] || !args[1] || pe_find((unsigned)*args[0]) < 0) return 0;
    int n = Inv_CountByValue(*args[0]);
    *args[1] = n > 0 ? n : 1;
    return 1;
}
static int pe_key_command(void *ctx, void *user) {
    PeHookInvCommand *c = ctx;
    (void)user;
    if (!pe_game_loaded()) return 0;
    int i = pe_find((unsigned)c->value);
    if (i < 0) return 0;
    if (c->command == 1102) {
        int n = Inv_CountByValue(c->value); c->result = n > 0 ? n : 1; return 1;
    }
    if (c->command == 1112) {
        /* A compatible recomp save may also contain its temporary lent copy. */
        pe_remove(i);
        for (int slot = 0; slot < 50; ++slot)
            if (g_AyaInventoryItems[slot] == c->value) g_AyaInventoryItems[slot] = 0;
        c->result = 0;
        pe_log("key_remove", "item=0x%02X", c->value);
        return 1;
    }
    return 0;
}
/* The old block too, for builds that only read that (host/pe_save.h, the window). */
static int pe_key_save(void *ctx, void *user) {
    (void)user;
    pe_inventory_save(ctx, PE_KEY_TAIL, &s_keys, sizeof s_keys, 0x59454B50u, 0x6B657973u);
    return 0;
}
static void pe_key_reset(void *data) { (void)data; memset(&s_keys, 0, sizeof s_keys); s_selected = 0; }
static int pe_key_check(void *data) {
    (void)data;
    s_selected = 0;
    if (s_keys.count > PE_KEY_MAX) return 1;
    for (int i = 0; i < s_keys.count; ++i) {
        if (!pe_is_key(s_keys.ids[i])) return 1;
        for (int j = 0; j < i; ++j) if (s_keys.ids[i] == s_keys.ids[j]) return 1;
    }
    return 0;
}
/* Saves from before: the "PKEY" block. */
static int pe_key_upgrade(void *data, const uint8_t *old, unsigned size, unsigned version) {
    (void)data;
    if (version != 0 || size < 16 + sizeof s_keys) return 1;
    return !pe_inventory_load(old, 0, &s_keys, sizeof s_keys, 0x59454B50u, 0x6B657973u);
}
/* The mod is off this session (host/pe_save.h): nothing would let story checks see pocketed keys, so
 * they go back into the bag the moment the save loads. Keys that don't fit stay in the record. */
static void pe_key_release(void *data) {
    int left;
    (void)data;
    if (!s_keys.count) return;
    left = pe_keys_return(pe_inventory_limit());
    pe_mod_log("pe_qol", "mod off: pocketed keys returned to the bag, %d left (bag full)", left);
    if (left) pe_toast("Key items didn't fit in the bag: turn quality of life back on to use them");
}
static const PeSaveRecord s_key_record = {
    "pe_qol.keys", 1, &s_keys, sizeof s_keys, pe_key_check, pe_key_upgrade, pe_key_reset, pe_key_release,
};
static unsigned pe_key_rows(void) { return s_keys.count ? s_keys.count : 1; }
static const char *pe_key_name(unsigned i) {
    if (i >= s_keys.count) return "Pocket empty";
    const u8 *text = Text_LookupTable8(s_keys.ids[i] - 1);
    unsigned n = 0;
    while (text && *text != 0xFF && n + 1 < sizeof s_name) {
        unsigned c = *text++;
        s_name[n++] = c <= 9 ? '0' + c : c >= 0x10 && c <= 0x29 ? 'A' + c - 0x10 :
                      c >= 0x30 && c <= 0x49 ? 'a' + c - 0x30 : ' ';
    }
    s_name[n] = 0; return s_name;
}
/* For the Key Items screen (pe_key_menu.c). */
int pe_keys_enabled(void) { return s_enabled; }
int pe_keys_count(void) { return s_keys.count; }
uint16_t pe_keys_item(int i) { return i >= 0 && i < s_keys.count ? s_keys.ids[i] : 0; }

static void pe_key_total(char *out, unsigned size) { snprintf(out, size, "%u", s_keys.count); }
void pe_key_items_register(const char *mod_id) {
    pe_menu_register_toggle(PE_PAGE_INVENTORY, "Key item pocket", "Keys leave the bag. Off returns them as space allows.", &s_enabled, pe_key_tick);
    pe_menu_register_dynamic_choice(PE_PAGE_KEYS, "Key", "Story checks can use these even with a full bag.", &s_selected, PE_KEY_MAX, pe_key_rows, pe_key_name);
    pe_menu_register_info(PE_PAGE_KEYS, "Pocketed keys", "Unique key and story items stored outside the bag.", pe_key_total);
    pe_service_publish("keys", 1, &s_api, mod_id);
    pe_hook_add(PE_HOOK_ITEM_ADD, pe_key_add, NULL, PE_PRIORITY_LATE + 10, mod_id);
    pe_hook_add(PE_HOOK_ITEM_COUNT, pe_key_count, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_INV_COMMAND, pe_key_command, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SAVE_WRITE, pe_key_save, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_save_register(&s_key_record);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_qol.h"
int pe_keys_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){if((!in&&!out)||0)return 0;if(apply&&(!in||!pe_keys_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};PE_CK_U(w,s_enabled,0,1,1);unsigned count=out?s_keys.count:(unsigned)in[4]|(unsigned)in[5]<<8|(unsigned)in[6]<<16|(unsigned)in[7]<<24;PE_CK_U(w,s_keys.count,0,PE_KEY_MAX,0);PE_CK_U(w,s_keys.pad,0,65535,0);uint16_t ids[PE_KEY_MAX];for(unsigned i=0;i<PE_KEY_MAX;i++){uint32_t v=out?s_keys.ids[i]:(uint32_t)in[12+i*4]|(uint32_t)in[13+i*4]<<8|(uint32_t)in[14+i*4]<<16|(uint32_t)in[15+i*4]<<24;ids[i]=(uint16_t)v;if(i<count){if(!pe_is_key(v))return 0;for(unsigned j=0;j<i;j++)if(ids[j]==v)return 0;}PE_CK_U(w,s_keys.ids[i],0,65535,0);}PE_CK_S(w,s_selected,0,PE_KEY_MAX-1,0);return w.ok&&w.at==144u;}
#endif
