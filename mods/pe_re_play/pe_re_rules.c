/* Room rules from the imported records. See pe_re_rules.h. */
#include <stdio.h>
#include <string.h>

#include "pe_re_rules.h"
#include "pe_re_session.h"

int pe_re_has(const PeReProgress *p, uint16_t item) {
    for (int i = 0; i < p->held; i++) if (p->pocket[i].item == item && p->pocket[i].count) return 1;
    return 0;
}

int pe_re_give(PeReProgress *p, uint16_t item, uint16_t count) {
    for (int i = 0; i < p->held; i++)
        if (p->pocket[i].item == item) { p->pocket[i].count = (uint16_t)(p->pocket[i].count + (count ? count : 1)); return 0; }
    if (p->held >= PE_RE_POCKET_MAX) return -1;
    p->pocket[p->held].item = item, p->pocket[p->held++].count = count ? count : 1;
    return 0;
}

void pe_re_item_name(const PeImpRoomLogic *items, uint16_t item, char *out, size_t n) {
    uint32_t count = 0;
    if (items && pe_imp_logic_section(items, PE_IMP_ITEM_NAME, &count, NULL))
        for (uint32_t i = 0; i < count; i++) {
            PeImpItemName name;
            if (!pe_imp_item_name(items, i, &name) && name.item == item && name.name[0]) {
                snprintf(out, n, "%.*s", (int)sizeof name.name, name.name);
                return;
            }
        }
    snprintf(out, n, "item %u", (unsigned)item);
}

int pe_re_message(const PeImpRoomLogic *room, uint32_t index, char *out, size_t n) {
    uint32_t records = 0, bytes = 0;
    const uint8_t *text = room ? pe_imp_logic_section(room, PE_IMP_LOGIC_TEXT, &records, &bytes) : NULL;
    if (!text || index >= records || !n) return -1;
    uint32_t at = 0;
    for (uint32_t k = 0; k < index && at < bytes; k++) {   /* NUL-separated, in order */
        while (at < bytes && text[at]) at++;
        at++;
    }
    size_t w = 0;
    int space = 1;                                         /* drop leading breaks, fold runs */
    for (; at < bytes && text[at] && w + 1 < n; at++) {
        const char c = (char)text[at];
        if (c == '\n' || c == '#' || c == ' ') {
            if (!space) out[w++] = ' ', space = 1;
        } else {
            out[w++] = c, space = 0;
        }
    }
    while (w && out[w - 1] == ' ') w--;
    out[w] = 0;
    return w ? 0 : -1;
}

static int unlocked(const PeReProgress *p, uint8_t id) { return (p->unlocked[id >> 3] >> (id & 7)) & 1; }
static void unlock(PeReProgress *p, uint8_t id) { p->unlocked[id >> 3] |= (uint8_t)(1u << (id & 7)); }

PeReDoorAnswer pe_re_door_try(PeReProgress *p, const PeImpDoor *d, const PeImpRoomLogic *items, char *say, size_t n) {
    char key[48];
    if (n) *say = 0;
    if ((!d->lock_id && !d->lock_type) || unlocked(p, d->lock_id)) return PE_RE_DOOR_OPEN;
    if (d->lock_type == PE_RE_LOCK_BOLT) {
        unlock(p, d->lock_id);
        snprintf(say, n, "Unlocked the door.");
        return PE_RE_DOOR_UNLOCKED;
    }
    if (d->lock_type == PE_RE_LOCK_OTHER_SIDE) {
        snprintf(say, n, "It's locked from the other side.");
        return PE_RE_DOOR_LOCKED;
    }
    pe_re_item_name(items, d->lock_type, key, sizeof key);
    if (pe_re_has(p, d->lock_type)) {
        unlock(p, d->lock_id);
        snprintf(say, n, "Used the %s.", key);
        return PE_RE_DOOR_UNLOCKED;
    }
    snprintf(say, n, "It's locked. It needs the %s.", key);
    return PE_RE_DOOR_LOCKED;
}

static uint32_t taken_key(const PeImpRoomLogic *room, uint16_t flag) {
    return room->stage << 24 | (room->room & 0xFF) << 16 | flag;
}
static int was_taken(const PeReProgress *p, uint32_t key) {
    for (int i = 0; i < p->ntaken; i++) if (p->taken[i] == key) return 1;
    return 0;
}

int pe_re_pickup_at(const PeImpRoomLogic *room, const PeReProgress *p, double x, double z, PeImpPickup *out) {
    uint32_t n = 0;
    if (!room || !pe_imp_logic_section(room, PE_IMP_LOGIC_PICK, &n, NULL)) return -1;
    for (uint32_t i = 0; i < n; i++) {
        PeImpPickup pk;
        if (pe_imp_logic_pickup(room, i, &pk) || !pk.item || was_taken(p, taken_key(room, pk.flag))) continue;
        if (pe_re_in_quad(pk.zone.x, pk.zone.z, x, z)) { if (out) *out = pk; return (int)i; }
    }
    return -1;
}

int pe_re_take(PeReProgress *p, const PeImpRoomLogic *room, const PeImpPickup *pk, const PeImpRoomLogic *items,
               char *say, size_t n) {
    char name[48];
    pe_re_item_name(items, pk->item, name, sizeof name);
    if (p->ntaken >= PE_RE_TAKEN_MAX || pe_re_give(p, pk->item, pk->count)) {
        snprintf(say, n, "No room for the %s.", name);
        return -1;
    }
    p->taken[p->ntaken++] = taken_key(room, pk->flag);
    if (pk->count > 1) snprintf(say, n, "Picked up %u %s.", (unsigned)pk->count, name);
    else snprintf(say, n, "Picked up the %s.", name);
    return 0;
}

int pe_re_message_at(const PeImpRoomLogic *room, double x, double z) {
    uint32_t n = 0;
    if (!room || !pe_imp_logic_section(room, PE_IMP_LOGIC_TRIG, &n, NULL)) return -1;
    for (uint32_t i = 0; i < n; i++) {
        PeImpTrigger t;
        if (pe_imp_logic_trigger(room, i, &t) || t.zone.sce != PE_RE_SCE_MESSAGE || !(t.zone.sat & PE_RE_SAT_CHECK))
            continue;
        if (pe_re_in_quad(t.zone.x, t.zone.z, x, z)) return t.data[0] | t.data[1] << 8;
    }
    return -1;
}
