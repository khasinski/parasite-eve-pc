/* Ammo types: separate ammo per weapon type (service "ammo_types", host/ammo_types_api.h).
 *
 * Parasite Eve's ordinary guns all draw Bullets from one pool. A scheme (an imported game's, or a
 * mod's) lists ammo types; while it is in force each type has its own pool record among the game's
 * nine (PE_HOOK_AMMO_POOL routes a gun, and an ammo item, to its type's record). The first type of
 * each native pool keeps that pool's record (so its bag stack stays the game's own); the others
 * take the spare records 3-8.
 *
 *   Pooling      (a scheme comes into force) splits each native pool among its types: the split
 *                remembered from the last time the scheme was on, adjusted by what was picked up
 *                (to the type of the gun in hand) or spent (taken proportionally) since; else all to
 *                the type of the gun in hand.
 *   Unpooling    (it goes out of force: the game being played changed, F1 > Ammo is off, or the
 *                mod is off) merges each type back into its native pool; past 999 the rest is kept
 *                as overflow and given back when pooling again. Rounds loaded in guns never move.
 *   Safe point   both only happen in the free field, outside battle.
 *   Saving       the "pe_ammo_types.state" record keeps the split; the card's own pool counts
 *                (0x10CC of the state image) are always written merged, so the save reads right
 *                with this mod off or absent. */
#include <stdio.h>
#include <string.h>

#include "ammo_types_api.h"
#include "pe_game.h"
#include "pe_hooks.h"
#include "pe_mod_api.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_save.h"
#include "re_play_api.h"
#include "pe1/inventory.h"
#include "pe1/inventory_state.h"

#define MOD_ID "pe_ammo_types"
#define PAGE "Ammo"
#define SCHEMES_MAX 8
#define POOLS 3
#define RECORDS 9
#define SAVE_COUNTS_AT (0x100 + 0x10CC)   /* the state image's pool counts in the card block */

typedef struct {
    uint8_t on;                  /* a scheme was in force (rounds[] are its live split) */
    uint8_t count;
    uint16_t reserved;
    uint32_t scheme;             /* hash of the scheme id the split belongs to */
    uint32_t type[RECORDS];      /* hash of each type id, in the scheme's order */
    uint16_t rounds[RECORDS];    /* each type's rounds */
    uint16_t overflow[POOLS];    /* rounds past 999 kept when unpooled */
    uint16_t merged[POOLS];      /* each native pool's total when last unpooled */
} AmmoState;

static AmmoState s_st;
static int s_enabled = 1;
static const PeAmmoScheme *s_schemes[SCHEMES_MAX];
static int s_scheme_count;
static const char *s_request;
static const PeAmmoScheme *s_live;          /* in force now */
static int s_record[RECORDS];               /* the pool record of each live type */
static int s_settle = 1;                    /* a load or New Game: clean the spare records first */
static int s_fill_due;                      /* fill() asked for while a switch was pending */
static const PeAmmoScheme *wanted(void);
static int add_to_record(int record, int rounds);

static uint32_t hash(const char *s) {
    uint32_t h = 2166136261u;
    for (; s && *s; ++s) h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

static ItemDataRecord *pool_record(int index) { return &g_KeyItemRecords[index]; }

/* ---- the bag's ammo stacks (item value 0x200 + record) ---- */
static int bag_find(int value) {
    const int n = Inv_GetAyaSlotLimit();
    for (int i = 0; i < n && i < 50; ++i)
        if (g_AyaInventoryItems[i] == value) return i;
    return -1;
}
static void stack_show(int record) {
    if (bag_find(0x200 + record) >= 0) return;
    const int free_slot = bag_find(0);
    if (free_slot >= 0) g_AyaInventoryItems[free_slot] = (s16)(0x200 + record);
}
static void stack_hide(int record) {
    const int at = bag_find(0x200 + record);
    if (at >= 0) g_AyaInventoryItems[at] = 0;
}

/* ---- schemes ---- */
static const PeAmmoScheme *scheme_named(const char *id) {
    for (int i = 0; id && i < s_scheme_count; ++i)
        if (!strcmp(s_schemes[i]->id, id)) return s_schemes[i];
    return NULL;
}

/* Each type's pool record: the native pool's own for its first type, spares 3-8 for the rest. */
static int assign_records(const PeAmmoScheme *s, int out[RECORDS]) {
    int native_taken[POOLS] = {0}, spare = POOLS;
    for (int i = 0; i < s->count; ++i) {
        const int p = s->types[i].native_pool;
        if (p >= POOLS) return -1;
        if (!native_taken[p]) native_taken[p] = 1, out[i] = p;
        else if (spare < RECORDS) out[i] = spare++;
        else return -1;
    }
    return 0;
}

static int api_register(const PeAmmoScheme *s) {
    int rec[RECORDS];
    if (!s || !s->id || !s->types || s->count < 1 || s->count > PE_AMMO_TYPES_MAX || assign_records(s, rec)) {
        pe_mod_log(MOD_ID, "scheme %s refused (types, native pools or spare records)", s && s->id ? s->id : "?");
        return -1;
    }
    if (scheme_named(s->id)) return 0;
    if (s_scheme_count == SCHEMES_MAX) return -1;
    s_schemes[s_scheme_count++] = s;
    pe_mod_log(MOD_ID, "scheme %s: %d ammo types", s->id, s->count);
    return 0;
}

static int live_type_of_kind(unsigned kind) {
    for (int i = 0; s_live && i < s_live->count; ++i)
        if (kind < 8 && (s_live->types[i].weapon_kinds >> kind & 1)) return i;
    return -1;
}

/* Where new rounds of native pool p go: the type of the gun in hand when it fires from p, else
 * p's first type. */
static int live_target(int p) {
    const ItemDataRecord *gun = pe_equipped(0);
    const int held = gun ? live_type_of_kind(gun->kind) : -1;
    if (held >= 0 && s_live->types[held].native_pool == p) return held;
    for (int i = 0; i < s_live->count; ++i)
        if (s_live->types[i].native_pool == p) return i;
    return -1;
}

/* ---- pooling and unpooling ---- */
static void unpool(void) {
    unsigned total[POOLS] = {0};
    if (!s_live) return;
    for (int i = 0; i < s_live->count; ++i) {
        s_st.rounds[i] = pool_record(s_record[i])->ammo;
        total[s_live->types[i].native_pool] += s_st.rounds[i];
    }
    for (int i = 0; i < s_live->count; ++i)
        if (s_record[i] >= POOLS) {
            pool_record(s_record[i])->ammo = 0;
            stack_hide(s_record[i]);
        }
    for (int p = 0; p < POOLS; ++p) {
        int used = 0;
        for (int i = 0; i < s_live->count; ++i) used |= s_live->types[i].native_pool == p;
        if (!used) continue;
        total[p] += s_st.overflow[p];
        const unsigned kept = total[p] > 999 ? 999 : total[p];
        s_st.overflow[p] = (uint16_t)(total[p] - kept);
        s_st.merged[p] = (uint16_t)kept;
        pool_record(p)->ammo = (u16)kept;
        if (kept) stack_show(p);
    }
    pe_mod_log(MOD_ID, "%s ammo merged back: Bullets %u, Rockets %u, Maeda's %u (over 999 kept: %u)", s_live->id,
               (unsigned)pool_record(0)->ammo, (unsigned)pool_record(1)->ammo, (unsigned)pool_record(2)->ammo,
               (unsigned)(s_st.overflow[0] + s_st.overflow[1] + s_st.overflow[2]));
    s_st.on = 0;
    s_live = NULL;
}

static void pool(const PeAmmoScheme *s) {
    int remembered = s_st.scheme == hash(s->id) && s_st.count == s->count;
    unsigned rounds[RECORDS] = {0};
    for (int i = 0; remembered && i < s->count; ++i) remembered = s_st.type[i] == hash(s->types[i].id);
    if (assign_records(s, s_record)) return;
    s_live = s;
    for (int p = 0; p < POOLS; ++p) {
        int target = -1, have = 0;
        for (int i = 0; i < s->count; ++i)
            if (s->types[i].native_pool == p) have = 1;
        if (!have) continue;
        target = live_target(p);
        const unsigned now = pool_record(p)->ammo + s_st.overflow[p];
        s_st.overflow[p] = 0;
        if (remembered) {
            unsigned split = 0;
            for (int i = 0; i < s->count; ++i)
                if (s->types[i].native_pool == p) rounds[i] = s_st.rounds[i], split += s_st.rounds[i];
            if (now >= split) {
                if (target >= 0) rounds[target] += now - split;       /* picked up while off */
            } else {
                unsigned spend = split - now;                         /* spent while off: proportionally */
                const unsigned whole = spend;
                for (int i = 0; i < s->count && spend; ++i)
                    if (s->types[i].native_pool == p) {
                        unsigned cut = split ? (unsigned)((unsigned long long)rounds[i] * whole / split) : 0;
                        if (cut > rounds[i]) cut = rounds[i];
                        if (cut > spend) cut = spend;
                        rounds[i] -= cut, spend -= cut;
                    }
                for (int i = 0; i < s->count && spend; ++i)
                    if (s->types[i].native_pool == p) {
                        const unsigned cut = rounds[i] < spend ? rounds[i] : spend;
                        rounds[i] -= cut, spend -= cut;
                    }
            }
        } else if (target >= 0) {
            rounds[target] = now;
        }
    }
    for (int i = 0; i < s->count; ++i) {
        const int p = s->types[i].native_pool;
        const unsigned kept = rounds[i] > 999 ? 999 : rounds[i];
        s_st.overflow[p] = (uint16_t)(s_st.overflow[p] + rounds[i] - kept);
        pool_record(s_record[i])->ammo = (u16)kept;
        s_st.type[i] = hash(s->types[i].id);
        s_st.rounds[i] = (uint16_t)kept;
        if (kept) stack_show(s_record[i]);
    }
    s_st.on = 1, s_st.count = (uint8_t)s->count, s_st.scheme = hash(s->id);
    pe_mod_log(MOD_ID, "%s ammo in force: %d types (%s)", s->id, s->count, remembered ? "remembered split" : "to the gun in hand");
}

/* ---- what should be in force ---- */
static const PeAmmoScheme *wanted(void) {
    if (!s_enabled || !pe_mod_active(MOD_ID)) return NULL;
    if (s_request) return scheme_named(s_request);
    const PeRePlayApi *re = pe_service_get(RE_PLAY_SERVICE, RE_PLAY_SERVICE_VERSION);
    if (re && re->struct_size >= offsetof(PeRePlayApi, playing_game) + sizeof re->playing_game && re->playing_game)
        return scheme_named(re->playing_game());
    return NULL;
}

static int safe_point(void) { return pe_save_loaded() && pe_free_field() && !pe_in_battle(); }

static int on_frame(void *ctx, void *user) {
    (void)ctx, (void)user;
    if (!safe_point()) return 0;
    if (s_settle) {          /* after a load or New Game the native pools hold the merged counts */
        s_settle = 0;
        s_live = NULL;
        for (int r = POOLS; r < RECORDS; ++r) pool_record(r)->ammo = 0, stack_hide(r);
    }
    const PeAmmoScheme *want = wanted();
    if (s_live && s_live != want) unpool();
    if (!s_live && want) pool(want);
    if (s_fill_due) {
        const int rounds = s_fill_due;
        s_fill_due = 0;
        if (s_live) for (int i = 0; i < s_live->count; ++i) add_to_record(s_record[i], rounds);
        else for (int p = 0; p < POOLS; ++p) add_to_record(p, rounds);
    }
    if (s_live)
        for (int i = 0; i < s_live->count; ++i) {
            s_st.rounds[i] = pool_record(s_record[i])->ammo;
            if (s_st.rounds[i]) stack_show(s_record[i]);
        }
    return 0;
}

/* ---- the game's lookups ---- */
static int on_ammo_pool(void *ctx, void *user) {
    PeHookAmmoPool *q = ctx;
    (void)user;
    if (!s_live || !q->item) return 0;
    const unsigned kind = q->item->kind;
    int type = -1;
    if (kind >= 1 && kind <= 7) type = live_type_of_kind(kind);
    else if (kind >= 16 && kind <= 18) type = live_target((int)kind - 16);
    if (type >= 0) q->pool = s_record[type];
    return 0;
}

/* The card's own counts: always merged, so the save reads right without this mod. */
static int on_save_write(void *ctx, void *user) {
    uint8_t *block = ctx;
    (void)user;
    if (!s_live || !block) return 0;
    unsigned total[POOLS] = {0};
    for (int p = 0; p < POOLS; ++p) total[p] = pool_record(p)->ammo;
    for (int i = 0; i < s_live->count; ++i)
        if (s_record[i] >= POOLS) total[s_live->types[i].native_pool] += pool_record(s_record[i])->ammo;
    for (int k = 0; k < 6; ++k) {
        const unsigned v = k < POOLS ? (total[k] > 999 ? 999 : total[k]) : 0;
        block[SAVE_COUNTS_AT + 2 * k] = (uint8_t)v;
        block[SAVE_COUNTS_AT + 2 * k + 1] = (uint8_t)(v >> 8);
    }
    return 0;
}
static int on_save_read(void *ctx, void *user) {
    (void)ctx, (void)user;
    s_live = NULL, s_settle = 1;
    return 0;
}
static int on_new_game(void *ctx, void *user) {
    (void)ctx, (void)user;
    s_live = NULL, s_settle = 1;
    return 0;
}

/* ---- the save record ---- */
static int state_check(void *data) {
    AmmoState *st = data;
    if (st->count > RECORDS) return 1;
    for (int p = 0; p < POOLS; ++p)
        if (st->overflow[p] > 60000u) st->overflow[p] = 0;
    st->on &= 1u;
    return 0;
}
/* The mod is off this session: the card already holds the merged counts; the spare stacks go. */
static void state_release(void *data) {
    (void)data;
    for (int r = POOLS; r < RECORDS; ++r) pool_record(r)->ammo = 0, stack_hide(r);
}
static const PeSaveRecord s_record_desc = {
    "pe_ammo_types.state", 1, &s_st, sizeof s_st, state_check, NULL, NULL, state_release
};

/* ---- the service ---- */
static void api_request(const char *id) { s_request = id; }
static const char *api_active(void) { return s_live ? s_live->id : NULL; }
static int live_index(const char *type_id) {
    for (int i = 0; s_live && type_id && i < s_live->count; ++i)
        if (!strcmp(s_live->types[i].id, type_id)) return i;
    return -1;
}
static int api_rounds(const char *type_id) {
    const int i = live_index(type_id);
    return i < 0 ? -1 : pool_record(s_record[i])->ammo;
}
static int add_to_record(int record, int rounds) {
    ItemDataRecord *r = pool_record(record);
    const int room = 999 - (int)r->ammo;
    const int n = rounds < 0 ? 0 : rounds > room ? room : rounds;
    r->ammo = (u16)(r->ammo + n);
    if (r->ammo) stack_show(record);
    return n;
}
static int api_add(const char *type_id, int rounds) {
    const int i = live_index(type_id);
    return i < 0 ? -1 : add_to_record(s_record[i], rounds);
}
static void api_fill(int rounds) {
    if (wanted() != s_live || s_settle) { s_fill_due += rounds; return; }   /* after the pending switch */
    if (s_live) for (int i = 0; i < s_live->count; ++i) add_to_record(s_record[i], rounds);
    else for (int p = 0; p < POOLS; ++p) add_to_record(p, rounds);
}
static const PeAmmoTypesApi s_api = {sizeof s_api, api_register, api_request, api_active, api_rounds, api_add, api_fill};

/* ---- F1 > Ammo ---- */
static void text_in_force(char *out, unsigned size) {
    snprintf(out, size, "%s", s_live ? (s_live->name ? s_live->name : s_live->id) : "Parasite Eve's own pools");
}
static void text_rounds(char *out, unsigned size) {
    size_t at = 0;
    out[0] = 0;
    if (!s_live) { snprintf(out, size, "-"); return; }
    for (int i = 0; i < s_live->count && at + 1 < size; ++i) {
        const int n = snprintf(out + at, size - at, "%s%s %u", i ? ", " : "", s_live->types[i].label,
                               (unsigned)pool_record(s_record[i])->ammo);
        if (n < 0) break;
        at += (size_t)n;
    }
}

static int ammo_init(void) {
    pe_service_publish(PE_AMMO_TYPES_SERVICE, PE_AMMO_TYPES_SERVICE_VERSION, &s_api, MOD_ID);
    pe_save_register(&s_record_desc);
    pe_hook_add(PE_HOOK_FRAME, on_frame, NULL, PE_PRIORITY_EARLY, MOD_ID);
    pe_hook_add(PE_HOOK_AMMO_POOL, on_ammo_pool, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_SAVE_WRITE, on_save_write, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_SAVE_READ, on_save_read, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_NEW_GAME, on_new_game, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_menu_begin(PAGE);
    pe_menu_top_page(PAGE, "Separate ammo per weapon type in imported games, as Resident Evil has it.", 55);
    pe_menu_register_toggle(0, "Separate ammo per weapon type",
                            "In an imported game, each type of gun has its own ammo. Off: every gun shares Parasite Eve's Bullets.",
                            &s_enabled, NULL);
    pe_menu_register_info(0, "In force", "Whose ammo types are in use now.", text_in_force);
    pe_menu_register_info(0, "Rounds", "Each ammo type's rounds now (not counting loaded magazines).", text_rounds);
    pe_menu_begin(0);
    return 0;
}

const PeMod pe_mod_pe_ammo_types = {MOD_ID, "Ammo types", "0.1", NULL, ammo_init, NULL};
