/* Remembered bag sorting from recomp pe_bag_sort.c. The native game does the
 * sorting and keeps equipped slots attached to their items. Only pointer-free
 * setting IDs are saved, as the host's "pe_qol.sort" record (host/pe_save.h). The recomp's "SORT"
 * v1 block is still written at its old spot (the host's save window) for builds without the
 * host's records; saves from before, or saved by such a build since, load from it. */
#include "pe_inventory.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_save.h"
#include "pe1/inventory_sort.h"
#include "pe1/inventory_state.h"

enum { PE_SORT_TAIL = 0x1C90 + 0x260 };
static struct {
    uint32_t mode, rank_ptr, type_ptr, weapon_stat, armor_stat, row_hidden;
} s_sort;
_Static_assert(sizeof s_sort == 24, "the SORT block of older saves holds 24 bytes of this state");
static s16 s_last[50];
static int s_seen, s_sorting;
static const char *const s_modes[] = { "Manual", "Auto" };

/* The PS1 values are disk identifiers only, never dereferenced. */
static u8 *pe_rank(uint32_t id) {
    switch (id) {
    case 0x800923F8u: return g_InvSortRankTableA;
    case 0x80092410u: return g_InvSortRankTableB;
    case 0x80092428u: return g_InvSortRankTableC;
    case 0x80092440u: return g_InvSortRankTableD;
    default: return NULL;
    }
}
static u8 *pe_type(uint32_t id) {
    if (id == 0x80092458u) return g_InvTypeRankTableA;
    if (id == 0x80092468u) return g_InvTypeRankTableB;
    return NULL;
}
static void pe_capture_settings(void) {
    static const uint32_t ranks[] = { 0x800923F8u, 0x80092410u, 0x80092428u, 0x80092440u };
    s_sort.rank_ptr = ranks[0]; s_sort.type_ptr = 0x80092458u;
    for (unsigned i = 0; i < 4; ++i)
        if (pe_rank(ranks[i]) == g_InvSortRankTable) s_sort.rank_ptr = ranks[i];
    if (g_InvSortTypeRankTable == g_InvTypeRankTableB) s_sort.type_ptr = 0x80092468u;
    s_sort.weapon_stat = g_InvWeaponSortStatIndex >= 0 && g_InvWeaponSortStatIndex <= 2 ? g_InvWeaponSortStatIndex : 0;
    s_sort.armor_stat = g_InvAmmoSortStatIndex >= 0 && g_InvAmmoSortStatIndex <= 2 ? g_InvAmmoSortStatIndex : 0;
}
static void pe_snapshot(void) { memcpy(s_last, g_AyaInventoryItems, sizeof s_last); s_seen = 1; }
static int pe_sort_chosen(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (!s_sorting && pe_game_loaded()) {
        pe_capture_settings(); s_sort.mode = 1;
        pe_log("bag_sort", "auto rank=0x%08X type=0x%08X weapon=%u armor=%u",
            s_sort.rank_ptr, s_sort.type_ptr, s_sort.weapon_stat, s_sort.armor_stat);
        pe_snapshot();
    }
    return 0;
}
static int pe_same_items(void) {
    for (int i = 0; i < 50; ++i) {
        int before = 0, after = 0;
        for (int j = 0; j < 50; ++j) {
            before += s_last[j] == s_last[i];
            after += g_AyaInventoryItems[j] == s_last[i];
        }
        if (before != after) return 0;
    }
    return 1;
}
static int pe_sort_mode(void) { return s_sort.mode == 1; }
int pe_bag_sort_auto(void) { return pe_sort_mode(); }
static int pe_set_sort_mode(int mode) {
    if (!pe_game_loaded() || mode < 0 || mode > 1) return 0;
    if (!pe_rank(s_sort.rank_ptr) || !pe_type(s_sort.type_ptr)) pe_capture_settings();
    s_sort.mode = mode; s_seen = 0;
    return 1;
}
static void pe_sort_tick(void) {
    if (!pe_game_loaded()) { s_seen = 0; return; }
    int changed = s_seen && memcmp(s_last, g_AyaInventoryItems, sizeof s_last);
    if (g_GameStateFlags & 4u) {
        if (s_sort.mode && changed && pe_same_items()) {
            s_sort.mode = 0; pe_log("bag_sort", "manual (items moved)");
        }
        pe_snapshot(); return;
    }
    if (!pe_free_field()) return;
    if (!s_sort.mode || (s_seen && !changed)) { pe_snapshot(); return; }
    int slot = g_AyaSaveState.equipped_weapon_slot;
    /* The stock sorter dereferences this slot before sorting. */
    if (slot < 0 || slot >= pe_inventory_limit() || !g_AyaInventoryItems[slot]) return;
    if (!pe_rank(s_sort.rank_ptr) || !pe_type(s_sort.type_ptr)) pe_capture_settings();
    g_InvSortRankTable = pe_rank(s_sort.rank_ptr);
    g_InvSortTypeRankTable = pe_type(s_sort.type_ptr);
    g_InvWeaponSortStatIndex = s_sort.weapon_stat;
    g_InvAmmoSortStatIndex = s_sort.armor_stat;
    s_sorting = 1;
    Inv_SortInventoryByMode(-1, 0); /* retain all four remembered settings */
    s_sorting = 0;
    pe_snapshot();
}
static int pe_sort_save(void *ctx, void *user) {
    (void)user;
    s_sort.row_hidden = 0; /* Menu hiding is transient; never persist it. */
    pe_inventory_save(ctx, PE_SORT_TAIL, &s_sort, sizeof s_sort, 0x54524F53u, 0x736F7274u);   /* the window */
    return 0;
}
static void pe_sort_reset(void *data) { (void)data; memset(&s_sort, 0, sizeof s_sort); s_seen = s_sorting = 0; }
static int pe_sort_check(void *data) {
    (void)data;
    s_seen = s_sorting = 0;
    s_sort.row_hidden = 0;
    return s_sort.mode > 1 || s_sort.weapon_stat > 2 || s_sort.armor_stat > 2 ||
           !pe_rank(s_sort.rank_ptr) || !pe_type(s_sort.type_ptr);
}
/* Saves from before: the "SORT" block. */
static int pe_sort_upgrade(void *data, const uint8_t *old, unsigned size, unsigned version) {
    (void)data;
    if (version != 0 || size < 16 + sizeof s_sort) return 1;
    return !pe_inventory_load(old, 0, &s_sort, sizeof s_sort, 0x54524F53u, 0x736F7274u);
}
static const PeSaveRecord s_sort_record = {
    "pe_qol.sort", 1, &s_sort, sizeof s_sort, pe_sort_check, pe_sort_upgrade, pe_sort_reset,
};
void pe_bag_sort_register(const char *mod_id) {
    pe_menu_register_live_choice(PE_PAGE_INVENTORY, "Bag sort", "Auto remembers the game's Sort. Moving items manually switches it off.",
        s_modes, 2, pe_sort_mode, pe_set_sort_mode);
    pe_menu_register_tick(pe_sort_tick);
    pe_hook_add(PE_HOOK_INVENTORY_SORTED, pe_sort_chosen, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SAVE_WRITE, pe_sort_save, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_save_register(&s_sort_record);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_qol.h"
int pe_sort_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){if((!in&&!out)||s_sorting)return 0;if(apply&&(!in||!pe_sort_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};PE_CK_U(w,s_sort.mode,0,1,0);PE_CK_U(w,s_sort.rank_ptr,0,UINT32_MAX,0);PE_CK_U(w,s_sort.type_ptr,0,UINT32_MAX,0);PE_CK_U(w,s_sort.weapon_stat,0,2,0);PE_CK_U(w,s_sort.armor_stat,0,2,0);PE_CK_U(w,s_sort.row_hidden,0,1,0);uint32_t rank=out?s_sort.rank_ptr:(uint32_t)in[4]|(uint32_t)in[5]<<8|(uint32_t)in[6]<<16|(uint32_t)in[7]<<24,type=out?s_sort.type_ptr:(uint32_t)in[8]|(uint32_t)in[9]<<8|(uint32_t)in[10]<<16|(uint32_t)in[11]<<24;unsigned mode=out?s_sort.mode:(unsigned)in[0]|(unsigned)in[1]<<8|(unsigned)in[2]<<16|(unsigned)in[3]<<24;if((rank&&!pe_rank(rank))||(type&&!pe_type(type))||(mode&&(!rank||!type)))return 0;for(unsigned i=0;i<50;i++){PE_CK_S(w,s_last[i],-32768,32767,0);}PE_CK_U(w,s_seen,0,1,0);uint32_t liveRank=0,liveType=0;static const uint32_t roles[]={0x800923F8u,0x80092410u,0x80092428u,0x80092440u};for(unsigned i=0;i<4;i++)if(g_InvSortRankTable==pe_rank(roles[i]))liveRank=i+1;if(g_InvSortRankTable&&!liveRank)return 0;if(g_InvSortTypeRankTable==g_InvTypeRankTableA)liveType=1;else if(g_InvSortTypeRankTable==g_InvTypeRankTableB)liveType=2;else if(g_InvSortTypeRankTable)return 0;PE_CK_U(w,liveRank,0,4,0);PE_CK_U(w,liveType,0,2,0);PE_CK_S(w,g_InvWeaponSortStatIndex,0,2,0);PE_CK_S(w,g_InvAmmoSortStatIndex,0,2,0);if(apply&&w.ok){g_InvSortRankTable=liveRank?pe_rank(roles[liveRank-1]):0;g_InvSortTypeRankTable=liveType==1?g_InvTypeRankTableA:liveType==2?g_InvTypeRankTableB:0;}return w.ok&&w.at==244u;}
#endif
