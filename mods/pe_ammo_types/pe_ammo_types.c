/* Ammo types: separate ammo per weapon type (service "ammo_types", host/ammo_types_api.h).
 *
 * Parasite Eve's ordinary guns all draw Bullets from one pool. A scheme (an imported game's, or a
 * mod's) lists ammo types; while it is in force each type keeps its own count, and PE_HOOK_AMMO_POOL
 * routes a gun, and an ammo item, to the pool record that holds its type's count.
 *
 *   Records      the game has nine (0 Bullets, 1 Rockets, 2 Maeda's, 3-8 spare). The first type of
 *                each native pool keeps that pool's record (so its bag stack stays the game's own).
 *                The other types share the six spares, bound on demand: the gun in hand first, then
 *                the other guns in the bag, then types already bound, then types holding rounds. An
 *                unbound type's count waits in this mod's state; binding moves it into a record and
 *                unbinding moves it back, so a scheme may have more types than there are records.
 *   Pooling      (a scheme comes into force) splits each native pool among its types: the split
 *                remembered from the last time the scheme was on (also when the scheme has since
 *                grown by appended types), adjusted by what was picked up (to the type of the gun in
 *                hand) or spent (taken proportionally) since; else all to the type of the gun in hand.
 *   Unpooling    (it goes out of force: the game being played changed, F1 > Ammo is off, or the
 *                mod is off) merges each type back into its native pool; past 999 the rest is kept
 *                as overflow and given back when pooling again. Rounds loaded in guns never move.
 *   Safe point   both only happen in the free field, outside battle. Binding can happen any frame.
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
#include "pe1/aya.h"
#include "pe1/inventory.h"
#include "pe1/inventory_state.h"

#define MOD_ID "pe_ammo_types"
#define PAGE "Ammo"
#define SCHEMES_MAX 8
#define POOLS 3
#define RECORDS 9
#define TYPES PE_AMMO_TYPES_MAX
#define SAVE_COUNTS_AT (0x100 + 0x10CC)   /* the state image's pool counts in the card block */

typedef struct {
    uint8_t on;                  /* a scheme was in force (rounds[] are its live split) */
    uint8_t count;
    uint16_t reserved;
    uint32_t scheme;             /* hash of the scheme id the split belongs to */
    uint16_t type[TYPES];        /* 16-bit hash of each type id, in the scheme's order */
    uint16_t rounds[TYPES];      /* each type's rounds (an unbound type's count lives only here) */
    uint16_t overflow[POOLS];    /* rounds past 999 kept when unpooled */
    uint16_t merged[POOLS];      /* each native pool's total when last unpooled */
} AmmoState;

typedef struct {                 /* version 1: nine types, 32-bit type hashes */
    uint8_t on, count;
    uint16_t reserved;
    uint32_t scheme, type[RECORDS];
    uint16_t rounds[RECORDS], overflow[POOLS], merged[POOLS];
} AmmoStateV1;

static AmmoState s_st;
static int s_enabled = 1;
static const PeAmmoScheme *s_schemes[SCHEMES_MAX];
static PeAmmoWeaponType s_weapon_types[SCHEMES_MAX];
static int s_scheme_count;
static const char *s_request;
static const PeAmmoScheme *s_live;          /* in force now */
static int s_bind[TYPES];                   /* each live type's pool record, or -1: unbound */
static int s_owner[RECORDS];                /* the live type in each spare record, or -1 */
static int s_settle = 1;                    /* a load or New Game: clean the spare records first */
static int s_fill_due;                      /* fill() asked for while a switch was pending */
static const PeAmmoScheme *wanted(void);
static int add_to_type(int type, int rounds);

static uint32_t hash(const char *s) {
    uint32_t h = 2166136261u;
    for (; s && *s; ++s) h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}
static uint16_t hash16_of(uint32_t h) { return (uint16_t)(h ^ (h >> 16)); }
static uint16_t hash16(const char *s) { return hash16_of(hash(s)); }

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

static int api_register(const PeAmmoScheme *s) {
    int valid = s && s->id && s->types && s->count >= 1 && s->count <= TYPES;
    for (int i = 0; valid && i < s->count; ++i) valid = s->types[i].id && s->types[i].native_pool < POOLS;
    if (!valid) {
        pe_mod_log(MOD_ID, "scheme %s refused (types or native pools)", s && s->id ? s->id : "?");
        return -1;
    }
    if (scheme_named(s->id)) return 0;
    if (s_scheme_count == SCHEMES_MAX) return -1;
    s_schemes[s_scheme_count++] = s;
    pe_mod_log(MOD_ID, "scheme %s: %d ammo types", s->id, s->count);
    return 0;
}

static int api_register_weapon_scheme(const PeAmmoScheme *s, PeAmmoWeaponType weapon_type) {
    if (!weapon_type || api_register(s)) return -1;
    for (int i = 0; i < s_scheme_count; ++i) {
        if (s_schemes[i] != s) continue;
        s_weapon_types[i] = weapon_type;
        return 0;
    }
    return -1;
}

static int live_type_of_weapon(const ItemDataRecord *weapon) {
    if (!s_live || !weapon) return -1;
    for (int i = 0; i < s_scheme_count; ++i) {
        if (s_schemes[i] != s_live || !s_weapon_types[i]) continue;
        const char *id = s_weapon_types[i](weapon);
        for (int j = 0; id && j < s_live->count; ++j)
            if (!strcmp(id, s_live->types[j].id)) return j;
        break;
    }
    const unsigned kind = weapon->kind;
    for (int i = 0; s_live && i < s_live->count; ++i)
        if (kind < 8 && (s_live->types[i].weapon_kinds >> kind & 1)) return i;
    return -1;
}

/* Where new rounds of native pool p go: the type of the gun in hand when it fires from p, else
 * p's first type. */
static int live_target(int p) {
    const ItemDataRecord *gun = pe_equipped(0);
    const int held = live_type_of_weapon(gun);
    if (held >= 0 && s_live->types[held].native_pool == p) return held;
    for (int i = 0; i < s_live->count; ++i)
        if (s_live->types[i].native_pool == p) return i;
    return -1;
}

/* ---- counts and binding ---- */
static int first_of_pool(int type) {
    for (int i = 0; i < type; ++i)
        if (s_live->types[i].native_pool == s_live->types[type].native_pool) return 0;
    return 1;
}
static unsigned count_of(int type) {
    return s_bind[type] >= 0 ? pool_record(s_bind[type])->ammo : s_st.rounds[type];
}
static void set_count(int type, unsigned rounds) {
    if (rounds > 999) rounds = 999;
    if (s_bind[type] >= 0) {
        pool_record(s_bind[type])->ammo = (u16)rounds;
        if (rounds) stack_show(s_bind[type]);
    }
    s_st.rounds[type] = (uint16_t)rounds;
}
static void unbind(int type) {
    const int r = s_bind[type];
    if (r < POOLS) return;                          /* a native pool's first type keeps its record */
    s_st.rounds[type] = pool_record(r)->ammo;
    pool_record(r)->ammo = 0;
    stack_hide(r);
    s_owner[r] = -1;
    s_bind[type] = -1;
}
static int bind_to(int type, int r) {
    s_owner[r] = type;
    s_bind[type] = r;
    pool_record(r)->ammo = s_st.rounds[type];
    if (s_st.rounds[type]) stack_show(r);
    return r;
}
/* A record for this type now, evicting the least wanted spare type (never the gun in hand's). */
static int ensure_bound(int type) {
    if (s_bind[type] >= 0) return s_bind[type];
    const int held = live_type_of_weapon(pe_equipped(0));
    int pick = -1;
    for (int r = POOLS; r < RECORDS; ++r)
        if (s_owner[r] < 0) return bind_to(type, r);
    for (int r = RECORDS - 1; r >= POOLS; --r)
        if (s_owner[r] != held) { pick = r; break; }
    if (pick < 0) return -1;
    unbind(s_owner[pick]);
    return bind_to(type, pick);
}
/* Bind the spares to the types wanted most: the gun in hand, the bag's other guns, those bound
 * already, then those holding rounds. Cheap: nine records at most. */
static void rebind(void) {
    int want[TYPES], n = 0, seen[TYPES] = {0};
    const int spares = RECORDS - POOLS;
#define WANT(t) do { const int t_ = (t); if (t_ >= 0 && !seen[t_] && !first_of_pool(t_) && n < spares) seen[t_] = 1, want[n++] = t_; } while (0)
    WANT(live_type_of_weapon(pe_equipped(0)));
    const int slots = Inv_GetAyaSlotLimit();
    for (int i = 0; i < slots && i < 50; ++i) {
        const int v = (u16)g_AyaInventoryItems[i];
        if (v < 0x100 || v >= 0x180) continue;
        const ItemDataRecord *item = &g_InventoryRuntime.equipment[v - 0x100];
        if (item->kind >= 1 && item->kind <= 8) WANT(live_type_of_weapon(item));
    }
    for (int r = POOLS; r < RECORDS; ++r) WANT(s_owner[r]);
    for (int t = 0; t < s_live->count; ++t)
        if (s_st.rounds[t]) WANT(t);
#undef WANT
    for (int r = POOLS; r < RECORDS; ++r)
        if (s_owner[r] >= 0 && !seen[s_owner[r]]) unbind(s_owner[r]);
    for (int k = 0; k < n; ++k)
        if (s_bind[want[k]] < 0) ensure_bound(want[k]);
}
static void clear_binding(void) {
    for (int t = 0; t < TYPES; ++t) s_bind[t] = -1;
    for (int r = 0; r < RECORDS; ++r) s_owner[r] = -1;
}

/* ---- pooling and unpooling ---- */
static void unpool(void) {
    unsigned total[POOLS] = {0};
    if (!s_live) return;
    for (int i = 0; i < s_live->count; ++i) {
        s_st.rounds[i] = (uint16_t)count_of(i);
        total[s_live->types[i].native_pool] += s_st.rounds[i];
    }
    for (int r = POOLS; r < RECORDS; ++r)
        if (s_owner[r] >= 0) pool_record(r)->ammo = 0, stack_hide(r);
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
    clear_binding();
    s_st.on = 0;
    s_live = NULL;
}

static void pool(const PeAmmoScheme *s) {
    int remembered = s_st.scheme == hash(s->id) && s_st.count > 0 && s_st.count <= s->count;
    unsigned rounds[TYPES] = {0};
    for (int i = 0; remembered && i < s_st.count; ++i) remembered = s_st.type[i] == hash16(s->types[i].id);
    clear_binding();
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
            for (int i = 0; i < s_st.count; ++i)
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
        s_st.type[i] = hash16(s->types[i].id);
        s_st.rounds[i] = (uint16_t)kept;
        if (first_of_pool(i)) {                          /* the native record: its own pool's count */
            s_owner[p] = i;
            s_bind[i] = p;
            pool_record(p)->ammo = (u16)kept;
            if (kept) stack_show(p);
        }
    }
    rebind();
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
    if (s_live && !s_settle) {   /* keep the bound counts and the spares current, any frame */
        for (int i = 0; i < s_live->count; ++i)
            if (s_bind[i] >= 0) s_st.rounds[i] = pool_record(s_bind[i])->ammo;
        rebind();
    }
    if (!safe_point()) return 0;
    if (s_settle) {          /* after a load or New Game the native pools hold the merged counts */
        s_settle = 0;
        s_live = NULL;
        clear_binding();
        for (int r = POOLS; r < RECORDS; ++r) pool_record(r)->ammo = 0, stack_hide(r);
    }
    const PeAmmoScheme *want = wanted();
    if (s_live && s_live != want) unpool();
    if (!s_live && want) pool(want);
    if (s_fill_due) {
        const int rounds = s_fill_due;
        s_fill_due = 0;
        if (s_live) for (int i = 0; i < s_live->count; ++i) add_to_type(i, rounds);
        else for (int p = 0; p < POOLS; ++p) {
            ItemDataRecord *r = pool_record(p);
            const int room = 999 - (int)r->ammo;
            r->ammo = (u16)(r->ammo + (rounds > room ? room : rounds));
            if (r->ammo) stack_show(p);
        }
    }
    if (s_live)
        for (int i = 0; i < s_live->count; ++i)
            if (s_bind[i] >= 0) {
                s_st.rounds[i] = pool_record(s_bind[i])->ammo;
                if (s_st.rounds[i]) stack_show(s_bind[i]);
            }
    return 0;
}

/* ---- the game's lookups ---- */
static int on_ammo_pool(void *ctx, void *user) {
    PeHookAmmoPool *q = ctx;
    (void)user;
    if (!s_live || s_settle || !q->item) return 0;
    const unsigned kind = q->item->kind;
    int type = -1;
    if (kind >= 1 && kind <= 7) type = live_type_of_weapon(q->item);
    else if (kind >= 16 && kind <= 18) type = live_target((int)kind - 16);
    const int r = type >= 0 ? ensure_bound(type) : -1;
    if (r >= 0) q->pool = r;
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
        if (!first_of_pool(i)) total[s_live->types[i].native_pool] += count_of(i);
    for (int k = 0; k < 6; ++k) {
        const unsigned v = k < POOLS ? (total[k] > 999 ? 999 : total[k]) : 0;
        block[SAVE_COUNTS_AT + 2 * k] = (uint8_t)v;
        block[SAVE_COUNTS_AT + 2 * k + 1] = (uint8_t)(v >> 8);
    }
    for (int i = 0; i < s_live->count; ++i) s_st.rounds[i] = (uint16_t)count_of(i);
    return 0;
}
static int on_save_read(void *ctx, void *user) {
    (void)ctx, (void)user;
    s_live = NULL, s_settle = 1;
    clear_binding();
    return 0;
}
static int on_new_game(void *ctx, void *user) {
    (void)ctx, (void)user;
    s_live = NULL, s_settle = 1;
    clear_binding();
    return 0;
}

/* ---- the save record ---- */
static int state_check(void *data) {
    AmmoState *st = data;
    if (st->count > TYPES) return 1;
    for (int p = 0; p < POOLS; ++p)
        if (st->overflow[p] > 60000u) st->overflow[p] = 0;
    for (int i = 0; i < TYPES; ++i)
        if (st->rounds[i] > 999) st->rounds[i] = 999;
    st->on &= 1u;
    return 0;
}
/* Version 1 held nine types with 32-bit hashes: the same split, folded to 16-bit hashes. */
static int state_upgrade(void *data, const uint8_t *old, unsigned size, unsigned version) {
    AmmoState *st = data;
    AmmoStateV1 v1;
    if (version != 1 || size < sizeof v1) return 1;
    memcpy(&v1, old, sizeof v1);
    if (v1.count > RECORDS) return 1;
    memset(st, 0, sizeof *st);
    st->on = v1.on, st->count = v1.count, st->scheme = v1.scheme;
    for (int i = 0; i < RECORDS; ++i) st->type[i] = hash16_of(v1.type[i]), st->rounds[i] = v1.rounds[i];
    for (int p = 0; p < POOLS; ++p) st->overflow[p] = v1.overflow[p], st->merged[p] = v1.merged[p];
    return 0;
}
/* The mod is off this session: the card already holds the merged counts; the spare stacks go. */
static void state_release(void *data) {
    (void)data;
    for (int r = POOLS; r < RECORDS; ++r) pool_record(r)->ammo = 0, stack_hide(r);
}
static const PeSaveRecord s_record_desc = {
    "pe_ammo_types.state", 2, &s_st, sizeof s_st, state_check, state_upgrade, NULL, state_release
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
    return i < 0 ? -1 : (int)count_of(i);
}
static int add_to_type(int type, int rounds) {
    const unsigned have = count_of(type);
    const int room = 999 - (int)have;
    const int n = rounds < 0 ? 0 : rounds > room ? room : rounds;
    set_count(type, have + (unsigned)n);
    return n;
}
static int api_add(const char *type_id, int rounds) {
    const int i = live_index(type_id);
    return i < 0 ? -1 : add_to_type(i, rounds);
}
static int api_take(const char *type_id, int rounds) {
    const int i = live_index(type_id);
    if (i < 0) return -1;
    const unsigned have = count_of(i);
    const unsigned n = rounds <= 0 ? 0 : (unsigned)rounds > have ? have : (unsigned)rounds;
    set_count(i, have - n);
    return (int)n;
}
static void api_fill(int rounds) {
    if (wanted() != s_live || s_settle) { s_fill_due += rounds; return; }   /* after the pending switch */
    if (s_live) for (int i = 0; i < s_live->count; ++i) add_to_type(i, rounds);
    else for (int p = 0; p < POOLS; ++p) {
        ItemDataRecord *r = pool_record(p);
        const int room = 999 - (int)r->ammo;
        r->ammo = (u16)(r->ammo + (rounds < 0 ? 0 : rounds > room ? room : rounds));
        if (r->ammo) stack_show(p);
    }
}
static const PeAmmoTypesApi s_api = {sizeof s_api, api_register, api_request, api_active,
                                   api_rounds, api_add, api_fill, api_register_weapon_scheme, api_take};

/* ---- F1 > Ammo ---- */
static void text_in_force(char *out, unsigned size) {
    snprintf(out, size, "%s", s_live ? (s_live->name ? s_live->name : s_live->id) : "Parasite Eve's own pools");
}
static void text_rounds(char *out, unsigned size) {
    size_t at = 0;
    out[0] = 0;
    if (!s_live) { snprintf(out, size, "-"); return; }
    for (int i = 0; i < s_live->count && at + 1 < size; ++i) {
        const int n = snprintf(out + at, size - at, "%s%s %u", i ? ", " : "", s_live->types[i].label, count_of(i));
        if (n < 0) break;
        at += (size_t)n;
    }
}

static int ammo_init(void) {
    clear_binding();
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

const PeMod pe_mod_pe_ammo_types = {MOD_ID, "Ammo types", "0.2", NULL, ammo_init, NULL};
