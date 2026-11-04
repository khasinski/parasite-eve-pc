#include <string.h>
#include <stdio.h>
#include "pe_inventory_internal.h"
#include "pe_extension_store.h"
#include "pe_game.h"
#include "pe_api_native.h"
#include "pe_mod_host.h"
#include "pe_services.h"
#include "pe_legacy_guard.h"
#include "pe1/inventory_state.h"
#define DICT 16u
#define INST (DICT + 12u * 52u)
#define ATT (INST + 32u * 16u)
static unsigned char s_ledger[2048];
static PeExtensionSection s_section = {"pe_host/content", 1, 1, sizeof s_ledger, s_ledger};
static uint32_t rd(const unsigned char *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void wr(unsigned char *p, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        p[i] = v >> (8 * i);
}
static uint32_t hash(const void *data, unsigned n) {
    const unsigned char *p = data;
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
static uint32_t fingerprint(const ItemDataRecord *r) {
    unsigned char b[8] = {r->reserved02[0], r->reserved02[1], r->itemId,       r->kind,
                          r->tailCapacity,  r->baseStats[0],  r->baseStats[1], r->baseStats[2]};
    return hash(b, sizeof b);
}
static uint32_t stamp(void) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 128; ++i) {
        uint32_t f = fingerprint(&PE_API_INV.equipment[i]);
        h = (h ^ PE_API_INV.equipment[i].iconId) * 16777619u;
        h = (h ^ f) * 16777619u;
    }
    return h;
}
static int valid(void) {
    if (!s_ledger[0]) {
        for (unsigned i = 0; i < sizeof s_ledger; ++i)
            if (s_ledger[i]) return 0;
        return 1;
    }
    if (memcmp(s_ledger, "PEC1", 4) || s_ledger[12] > 12 || s_ledger[13] > 32 || s_ledger[14] > 16 ||
        s_ledger[15])
        return 0;
    for (int i = 0; i < s_ledger[12]; ++i) {
        unsigned char *d = s_ledger + DICT + i * 52;
        if (!d[0] || !memchr(d, 0, 48) || !d[50] || d[50] > 2) return 0;
    }
    for (int i = 0; i < s_ledger[13]; ++i) {
        unsigned char *p = s_ledger + INST + i * 16;
        if (!rd(p) || rd(s_ledger + 4) <= rd(p) || p[7] > 1 || p[4] >= s_ledger[12] || p[5] >= 128 ||
            p[6] < 1 || p[6] > 4 || s_ledger[DICT + p[4] * 52 + 50] != 1)
            return 0;
        for (int j = 0; j < i; ++j) {
            unsigned char *q = s_ledger + INST + j * 16;
            if (rd(p) == rd(q) || p[5] == q[5]) return 0;
        }
    }
    for (int i = 0; i < s_ledger[14]; ++i) {
        unsigned char *p = s_ledger + ATT + i * 32;
        int found = 0;
        for (int j = 0; j < s_ledger[13]; ++j)
            found |= rd(s_ledger + INST + j * 16) == rd(p);
        if (!found || p[4] >= s_ledger[12] || s_ledger[DICT + p[4] * 52 + 50] != 2 || p[5] >= 10 || p[7] > 1)
            return 0;
        for (int j = 0; j < i; ++j) {
            unsigned char *q = s_ledger + ATT + j * 32;
            if (rd(p) == rd(q) && p[5] == q[5]) return 0;
        }
    }
    return 1;
}
static int dictionary(const char *key, unsigned kind, unsigned revision) {
    for (int i = 0; i < s_ledger[12]; ++i)
        if (s_ledger[DICT + i * 52 + 50] == kind &&
            (s_ledger[DICT + i * 52 + 48] | (unsigned)s_ledger[DICT + i * 52 + 49] << 8) == revision &&
            !strcmp((char *)s_ledger + DICT + i * 52, key))
            return i;
    return -1;
}
static int instance(uint32_t id) {
    for (int i = 0; i < s_ledger[13]; ++i)
        if (rd(s_ledger + INST + i * 16) == id) return i;
    return -1;
}
static PeRegResult gate(void) {
    PeRegResult r = pe_reg_access();
    if (r != PE_REG_OK) return r;
    if (!pe_api_native_loaded() || pe_api_native_in_battle()) return PE_REG_WRONG_PHASE;
    if (!pe_extension_available(&s_section) || !valid()) return PE_REG_UNSAVEABLE;
    return PE_REG_OK;
}
static void init(void) {
    if (!s_ledger[0]) {
        memcpy(s_ledger, "PEC1", 4);
        wr(s_ledger + 4, 1);
    }
}
static int add_dict(const char *key, unsigned revision, unsigned kind) {
    int i = dictionary(key, kind, revision);
    if (i >= 0) return i;
    i = s_ledger[12]++;
    unsigned char *d = s_ledger + DICT + i * 52;
    memset(d, 0, 52);
    strcpy((char *)d, key);
    d[48] = revision;
    d[49] = revision >> 8;
    d[50] = kind;
    return i;
}
static int pe_guard_hook(void *ctx, void *user) {
    const PeHookEquipmentGuard *g = ctx;
    if (!g) return 1;
    if (g->kind == 0) return !pe_inventory_native_guard(g->value);
    if (g->kind == 1) return !pe_mod_equipment_record_guard(g->record);
    if (g->kind == 2) return !pe_mod_tool_guard(g->selection);
    return 1;
}
void pe_inventory_api_install(void) {
    pe_legacy_guard_bind(pe_mod_equipment_record_guard);
    pe_hook_add(PE_HOOK_EQUIPMENT_GUARD, pe_guard_hook, NULL, PE_PRIORITY_EARLY, NULL);
    (void)pe_extension_register(&s_section);
}
void pe_inventory_api_loaded(void) {
    if (!pe_api_native()->inventory || !valid()) return;
    uint32_t current = stamp();
    for (int i = 0; i < s_ledger[13]; ++i) {
        unsigned char *p = s_ledger + INST + i * 16;
        ItemDataRecord *r = &PE_API_INV.equipment[p[5]];
        if (!r->iconId) p[6] = PE_INSTANCE_ORPHANED;
        else if (rd(s_ledger + 8) != current || rd(p + 8) != fingerprint(r)) p[6] = PE_INSTANCE_UNCERTAIN;
    }
}
void pe_inventory_api_before_save(void) {
    if (pe_api_native_loaded() && valid() && s_ledger[0]) wr(s_ledger + 8, stamp());
}
PeRegResult pe_inventory_prepare_grant(const char *key, PeInventoryGrantPlan *out) {
    PeRegResult r = gate();
    if (r != PE_REG_OK) return r;
    if (!key || !out) return PE_REG_BAD_ARGUMENT;
    ItemDataRecord native;
    if ((r = pe_content_preview(key, &native)) != PE_REG_OK) return r;
    const PeContentDef *d = pe_content_find(key);
    int di = dictionary(key, 1, d->revision);
    if (s_ledger[13] == 32 || (di < 0 && s_ledger[12] == 12)) return PE_REG_CAPACITY;
    uint32_t next = s_ledger[0] ? rd(s_ledger + 4) : 1;
    if (!next || next == UINT32_MAX) return PE_REG_CAPACITY;
    int bag = -1, pool = -1, limit = pe_api_native()->bag_limit();
    if (limit < 0 || limit > 50) return PE_REG_BAD_ARGUMENT;
    for (int i = 0; i < limit; ++i)
        if (!PE_API_INV.slots[i]) {
            bag = i;
            break;
        }
    for (int i = 0; i < 128; ++i)
        if (!PE_API_INV.equipment[i].iconId) {
            int used = 0;
            for (int j = 0; j < s_ledger[13]; ++j)
                used |= s_ledger[INST + j * 16 + 5] == i;
            if (!used) {
                pool = i;
                break;
            }
        }
    if (bag < 0 || pool < 0) return PE_REG_CAPACITY;
    if ((r = pe_extension_enable(&s_section)) != PE_REG_OK) return r;
    PeInventoryGrantPlan plan = {0};
    plan.bag = bag;
    plan.pool = pool;
    plan.instance = s_ledger[13];
    plan.dict = di;
    plan.id = next;
    plan.revision = d->revision;
    strcpy(plan.key, key);
    memcpy(plan.record, &native, 32);
    *out = plan;
    return PE_REG_OK;
}
uint32_t pe_inventory_commit_grant(const PeInventoryGrantPlan *p) {
    init();
    int di = add_dict(p->key, p->revision, 1);
    unsigned char *i = s_ledger + INST + s_ledger[13]++ * 16;
    memset(i, 0, 16);
    wr(i, p->id);
    i[4] = di;
    i[5] = p->pool;
    i[6] = PE_INSTANCE_ACTIVE;
    memcpy(&PE_API_INV.equipment[p->pool], p->record, 32);
    wr(i + 8, fingerprint(&PE_API_INV.equipment[p->pool]));
    PE_API_INV.slots[p->bag] = 0x100 + p->pool;
    wr(s_ledger + 4, p->id + 1);
    wr(s_ledger + 8, stamp());
    return p->id;
}
PeRegResult pe_inventory_grant(const char *key, uint32_t *out) {
    if (!out) return PE_REG_BAD_ARGUMENT;
    PeInventoryGrantPlan p;
    PeRegResult r = pe_inventory_prepare_grant(key, &p);
    if (r == PE_REG_OK) *out = pe_inventory_commit_grant(&p);
    return r;
}
PeRegResult pe_inventory_inspect(uint32_t id, PeInventoryInstance *out) {
    PeRegResult r = pe_reg_access();
    if (r != PE_REG_OK) return r;
    if (!out || !id) return PE_REG_BAD_ARGUMENT;
    if (!pe_api_native_loaded()) return PE_REG_UNAVAILABLE;
    if (!valid()) return PE_REG_UNSAVEABLE;
    int at = instance(id);
    if (at < 0) return PE_REG_UNAVAILABLE;
    unsigned char *p = s_ledger + INST + at * 16, *d = s_ledger + DICT + p[4] * 52;
    PeInventoryInstance v = {0};
    v.id = id;
    strcpy(v.key, (char *)d);
    v.revision = d[48] | d[49] << 8;
    v.pool = p[5];
    v.state = p[6];
    ItemDataRecord *n = &PE_API_INV.equipment[v.pool];
    if (!n->iconId) v.state = PE_INSTANCE_ORPHANED;
    else if (fingerprint(n) != rd(p + 8)) v.state = PE_INSTANCE_UNCERTAIN;
    else if (v.state == PE_INSTANCE_ACTIVE && !(p[7] & 1) && !pe_content_find(v.key))
        v.state = PE_INSTANCE_UNAVAILABLE;
    *out = v;
    return PE_REG_OK;
}
PeRegResult pe_inventory_destroy(uint32_t id) {
    PeRegResult r = gate();
    if (r != PE_REG_OK) return r;
    PeInventoryInstance v;
    if ((r = pe_inventory_inspect(id, &v)) != PE_REG_OK) return r;
    if (v.state != PE_INSTANCE_ACTIVE) return PE_REG_UNAVAILABLE;
    int value = 0x100 + v.pool;
    for (int i = 0; i < 50; ++i)
        if (PE_API_INV.slots[i] == value && (PE_API_INV.tracked[0] == i || PE_API_INV.tracked[2] == i))
            return PE_REG_CONFLICT;
    if ((r = pe_extension_enable(&s_section)) != PE_REG_OK) return r;
    for (int i = 0; i < 50; ++i)
        if (PE_API_INV.slots[i] == value) PE_API_INV.slots[i] = 0;
    for (int i = 0; i < 100; ++i)
        if (PE_API_INV.storage[i] == value) PE_API_INV.storage[i] = 0;
    for (int i = 0; i < 82; ++i)
        if (PE_API_INV.specialStorage[i] == value) PE_API_INV.specialStorage[i] = 0;
    memset(&PE_API_INV.equipment[v.pool], 0, 32);
    int at = instance(id);
    memmove(s_ledger + INST + at * 16, s_ledger + INST + (at + 1) * 16, (s_ledger[13] - at - 1) * 16);
    --s_ledger[13];
    memset(s_ledger + INST + s_ledger[13] * 16, 0, 16);
    for (int i = 0; i < s_ledger[14];)
        if (rd(s_ledger + ATT + i * 32) == id) {
            memmove(s_ledger + ATT + i * 32, s_ledger + ATT + (i + 1) * 32, (s_ledger[14] - i - 1) * 32);
            --s_ledger[14];
            memset(s_ledger + ATT + s_ledger[14] * 32, 0, 32);
        } else ++i;
    wr(s_ledger + 8, stamp());
    return PE_REG_OK;
}
int pe_inventory_native_guard(int value) {
    if (value < 256 || value >= 384) return 1;
    /* Legacy operations have no custom identity/provenance contract. Native fallback use,
     * equipping, sorting and storage transfers remain available; destructive/Tools/Catalog paths
     * are refused for tracked records. Use the typed APIs to destroy/edit them explicitly. */
    if (!valid()) return 0;
    for (int i = 0; i < s_ledger[13]; ++i)
        if (s_ledger[INST + i * 16 + 5] == value - 256) return 0;
    return 1;
}
static PeRegResult target(uint32_t id, PeInventoryInstance *v, ItemDataRecord **rec) {
    PeRegResult r = gate();
    if (r != PE_REG_OK) return r;
    if ((r = pe_inventory_inspect(id, v)) != PE_REG_OK) return r;
    if (v->state != PE_INSTANCE_ACTIVE) return PE_REG_UNAVAILABLE;
    *rec = &PE_API_INV.equipment[v->pool];
    return PE_REG_OK;
}
PeRegResult pe_equipment_effect_apply(uint32_t id, const char *key, unsigned slot) {
    PeInventoryInstance v;
    ItemDataRecord *rec;
    PeRegResult r = target(id, &v, &rec);
    if (r != PE_REG_OK) return r;
    const PeEffectDef *d = pe_effect_find(key);
    if (!d) return PE_REG_UNAVAILABLE;
    if (slot >= rec->tailCount || slot >= 10 || d->armor != (rec->kind == 9)) return PE_REG_BAD_ARGUMENT;
    if ((rec->tailData[slot] & 31u)) return PE_REG_CONFLICT; /* no silent replacement or deletion */
    if (d->armor && d->native_effect >= 17 && !pe_mod_is_running("pe_economy")) return PE_REG_UNAVAILABLE;
    if (d->armor && d->native_effect >= 8 && d->native_effect <= 10)
        return PE_REG_UNSUPPORTED; /* native bag shrink safety requires a separate adapter */
    for (int i = 0; i < rec->tailCount && i < 10; ++i)
        if ((rec->tailData[i] & 31u) && ((rec->tailData[i] & 31u) == d->native_effect ||
                                         (d->group && (rec->tailData[i] >> 5) == d->group)))
            return PE_REG_CONFLICT;
    int di = dictionary(key, 2, d->revision);
    if (s_ledger[14] == 16 || (di < 0 && s_ledger[12] == 12)) return PE_REG_CAPACITY;
    if ((r = pe_extension_enable(&s_section)) != PE_REG_OK) return r;
    di = add_dict(key, d->revision, 2);
    unsigned char *p = s_ledger + ATT + s_ledger[14]++ * 32;
    memset(p, 0, 32);
    wr(p, id);
    p[4] = di;
    p[5] = slot;
    p[6] = (d->group << 5) | d->native_effect;
    p[7] = d->removable;
    rec->tailData[slot] = p[6];
    return PE_REG_OK;
}
PeRegResult pe_equipment_effect_remove(uint32_t id, unsigned slot) {
    PeInventoryInstance v;
    ItemDataRecord *rec;
    PeRegResult r = target(id, &v, &rec);
    if (r != PE_REG_OK) return r;
    if (slot >= rec->tailCount || slot >= 10) return PE_REG_BAD_ARGUMENT;
    int at = -1;
    for (int i = 0; i < s_ledger[14]; ++i)
        if (rd(s_ledger + ATT + i * 32) == id && s_ledger[ATT + i * 32 + 5] == slot) {
            at = i;
            break;
        }
    if (at < 0) return PE_REG_CONFLICT; /* built-in provenance is never removable via this API */
    unsigned char *p = s_ledger + ATT + at * 32;
    const char *key = (char *)s_ledger + DICT + p[4] * 52;
    if (!pe_effect_find(key) || !p[7] || rec->tailData[slot] != p[6]) return PE_REG_UNAVAILABLE;
    if ((r = pe_extension_enable(&s_section)) != PE_REG_OK) return r;
    rec->tailData[slot] = 0;
    memmove(p, p + 32, (s_ledger[14] - at - 1) * 32);
    --s_ledger[14];
    memset(s_ledger + ATT + s_ledger[14] * 32, 0, 32);
    return PE_REG_OK;
}

int pe_mod_equipment_guard(int value) {
    return pe_inventory_native_guard(value);
}
int pe_mod_equipment_record_guard(const ItemDataRecord *record) {
    if (pe_reg_callback_active()) return 0;
    if (!pe_api_native()->inventory) return 0;
    uintptr_t p = (uintptr_t)record, base = (uintptr_t)PE_API_INV.equipment;
    if (p >= base && p < base + sizeof PE_API_INV.equipment && (p - base) % sizeof(ItemDataRecord) == 0)
        return pe_inventory_native_guard(256 + (int)((p - base) / sizeof(ItemDataRecord)));
    return 1;
}

const PeInventoryActionsApi g_pe_inventory_actions_api = {sizeof(PeInventoryActionsApi), pe_inventory_grant,
                                                          pe_inventory_inspect, pe_inventory_destroy,
                                                          pe_inventory_adopt};
const PeEquipmentEffectsApi g_pe_equipment_effects_api = {
    sizeof(PeEquipmentEffectsApi), pe_effect_register,        pe_effect_find, pe_effect_count, pe_effect_at,
    pe_equipment_effect_apply,     pe_equipment_effect_remove};

PeRegResult pe_inventory_adopt(int value, uint32_t *out) {
    PeRegResult r = gate();
    if (r != PE_REG_OK) return r;
    if (!out || value < 256 || value >= 384) return PE_REG_BAD_ARGUMENT;
    int pool = value - 256;
    ItemDataRecord *rec = &PE_API_INV.equipment[pool];
    if (!rec->iconId || rec->kind < 1 || rec->kind > 9 || rec->tailCount > 10 || rec->tailCapacity > 10)
        return PE_REG_UNSUPPORTED;
    for (int i = 0; i < s_ledger[13]; ++i)
        if (s_ledger[INST + i * 16 + 5] == pool) {
            *out = rd(s_ledger + INST + i * 16);
            return PE_REG_OK;
        }
    int found = 0;
    for (int i = 0; i < 50; ++i)
        found |= PE_API_INV.slots[i] == value;
    for (int i = 0; i < 100; ++i)
        found |= PE_API_INV.storage[i] == value;
    for (int i = 0; i < 82; ++i)
        found |= PE_API_INV.specialStorage[i] == value;
    if (!found) return PE_REG_UNAVAILABLE;
    char key[48];
    snprintf(key, sizeof key, "pe_host/native-%02x", rec->itemId);
    int di = dictionary(key, 1, 1);
    uint32_t id = s_ledger[0] ? rd(s_ledger + 4) : 1;
    if (!id || id == UINT32_MAX || s_ledger[13] == 32 || (di < 0 && s_ledger[12] == 12))
        return PE_REG_CAPACITY;
    if ((r = pe_extension_enable(&s_section)) != PE_REG_OK) return r;
    init();
    di = add_dict(key, 1, 1);
    unsigned char *p = s_ledger + INST + s_ledger[13]++ * 16;
    memset(p, 0, 16);
    wr(p, id);
    p[4] = di;
    p[5] = pool;
    p[6] = PE_INSTANCE_ACTIVE;
    p[7] = 1;
    wr(p + 8, fingerprint(rec));
    wr(s_ledger + 4, id + 1);
    wr(s_ledger + 8, stamp());
    *out = id;
    return PE_REG_OK;
}

int pe_mod_tool_guard(unsigned selection) {
    if (!pe_api_native()->inventory) return 0;
    if (selection >= 2) return 0;
    for (unsigned which = 0; which < 2; ++which) {
        const s16 *list = PE_API_INV.slots;
        int limit = pe_api_native()->bag_limit();
        if (pe_api_native()->from_storage[which] && (*pe_api_native()->override_items)) {
            list = (*pe_api_native()->override_items);
            limit = (*pe_api_native()->override_limit);
        }
        int slot = pe_api_native()->selection[which];
        if (slot >= 0 && slot < limit && !pe_inventory_native_guard(list[slot])) return 0;
    }
    return 1;
}

const PeEquipmentGuardApi g_pe_equipment_guard_api = {sizeof(PeEquipmentGuardApi),
                                                      pe_mod_equipment_record_guard};

int pe_inventory_snapshot_ledger(unsigned char*out,unsigned size,const unsigned char*native,unsigned native_size){
    const unsigned at=offsetof(SaveRuntimeState,player.runtime.inventory);
    if(!out||size!=sizeof s_ledger||!native||native_size<at+sizeof(InventoryRuntime)||!valid())return -1;
    memcpy(out,s_ledger,sizeof s_ledger);if(!out[0])return 0;
    uint32_t h=2166136261u;
    const unsigned pool=at+offsetof(InventoryRuntime,equipment);
    for(unsigned i=0;i<128;++i){ItemDataRecord record;memcpy(&record,native+pool+i*sizeof record,sizeof record);
        h=(h^record.iconId)*16777619u;h=(h^fingerprint(&record))*16777619u;
    }
    wr(out+8,h);return 0;
}
