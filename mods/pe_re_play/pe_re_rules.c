/* Room rules from the imported records. See pe_re_rules.h. */
#include <stdio.h>
#include <string.h>

#include "pe_re_rules.h"
#include "pe_re_session.h"

int pe_re_has(const PeReProgress *p, uint16_t item) {
    for (int i = 0; i < p->held; i++) if (p->pocket[i].item == item && p->pocket[i].count) return 1;
    return 0;
}

/* Two of an item share a slot when either comes in a count (ammo, ribbons); what comes one at a time
 * (keys, jewels, herbs) takes a slot each, so a script's item_lost takes one of them, not both. */
static int stacks(uint16_t held, uint16_t count) { return held > 1 || count > 1; }

int pe_re_give(PeReProgress *p, uint16_t item, uint16_t count) {
    for (int i = 0; i < p->held; i++)
        if (p->pocket[i].item == item && stacks(p->pocket[i].count, count)) { p->pocket[i].count = (uint16_t)(p->pocket[i].count + (count ? count : 1)); return 0; }
    if (p->held >= PE_RE_POCKET_SLOTS) return -1;
    p->pocket[p->held].item = item, p->pocket[p->held++].count = count ? count : 1;
    return 0;
}

static void drop_slot(PeReHeld *list, int *n, int i) {           /* Sort_item: the rest move up */
    memmove(&list[i], &list[i + 1], sizeof *list * (size_t)(*n - i - 1));
    (*n)--;
}

int pe_re_lose(PeReProgress *p, uint16_t item, uint16_t count) {
    for (int i = 0; i < p->held; i++)
        if (p->pocket[i].item == item) {
            if (count && p->pocket[i].count > count) p->pocket[i].count = (uint16_t)(p->pocket[i].count - count);
            else drop_slot(p->pocket, &p->held, i);
            return 0;
        }
    return -1;
}

int pe_re_move_ammo(PeReProgress *p, const char *game, int (*credit)(const char *, uint16_t, unsigned)) {
    int moved = 0;
    if (!credit) return 0;
    for (int i = p->held - 1; i >= 0; i--) {
        const PeReHeld h = p->pocket[i];
        if (h.count && credit(game, h.item, h.count) == 1 && !pe_re_lose(p, h.item, h.count)) moved++;
    }
    return moved;
}

int pe_re_box_put(PeReProgress *p, int i) {
    if (i < 0 || i >= p->held) return -1;
    const PeReHeld h = p->pocket[i];
    int k = 0;
    while (k < p->boxed && !(p->box[k].item == h.item && stacks(p->box[k].count, h.count))) k++;
    if (k == p->boxed) {
        if (p->boxed >= PE_RE_BOX_MAX) return -1;
        p->box[p->boxed++] = h;
    } else {
        p->box[k].count = (uint16_t)(p->box[k].count + h.count);
    }
    drop_slot(p->pocket, &p->held, i);
    return 0;
}

int pe_re_box_take(PeReProgress *p, int i) {
    if (i < 0 || i >= p->boxed || pe_re_give(p, p->box[i].item, p->box[i].count)) return -1;
    drop_slot(p->box, &p->boxed, i);
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

/* A lock's state is the game's own door-lock flag (RE2: Key_flg, flag group 31, bit lock id & 0x3F,
 * which Sce_Door sets and room scripts read: SCE_AT.C), kept also in the progress list older saves
 * hold. Either one open means open. */
static int unlocked(const PeReProgress *p, uint8_t id) {
    return ((p->unlocked[id >> 3] >> (id & 7)) & 1) || pe_re_flag(&p->flags, PE_RE_FG_KEY, id & 0x3Fu);
}
static void unlock(PeReProgress *p, uint8_t id) {
    p->unlocked[id >> 3] |= (uint8_t)(1u << (id & 7));
    pe_re_flag_set(&p->flags, PE_RE_FG_KEY, id & 0x3Fu, 1);
}

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

int pe_re_zone_kind(const PeReScdRun *run, uint8_t id, int sce, uint8_t data[6]) {
    for (int i = run && run->ran ? run->nreset - 1 : -1; i >= 0; i--)
        if (run->reset[i].id == id) {
            if (data) memcpy(data, run->reset[i].data, 6);
            return run->reset[i].sce;
        }
    return sce;
}

static int live(const PeReScdRun *run, uint32_t at) { return !run || pe_re_scd_live(run, at); }

int pe_re_pickup_gone(const PeImpRoomLogic *room, const PeReScdRun *run, const PeReProgress *p, const PeImpPickup *pk) {
    return !pk->item || was_taken(p, taken_key(room, pk->flag)) ||
           (run && run->ran && pe_re_flag(&p->flags, p->item_group ? p->item_group : PE_RE_FG_ITEM, pk->flag));
}

int pe_re_pickup_at(const PeImpRoomLogic *room, const PeReScdRun *run, const PeReProgress *p, double x, double z,
                    PeImpPickup *out) {
    uint32_t n = 0;
    if (!room || !pe_imp_logic_section(room, PE_IMP_LOGIC_PICK, &n, NULL)) return -1;
    for (uint32_t i = 0; i < n; i++) {
        PeImpPickup pk;
        /* item_aot_set isn't guarded in RE2's scripts: the engine skips an item whose flag is set */
        if (pe_imp_logic_pickup(room, i, &pk) || !live(run, pk.at) || pe_re_pickup_gone(room, run, p, &pk) ||
            pe_re_zone_kind(run, pk.zone.id, pk.zone.sce, NULL) != pk.zone.sce)
            continue;
        if (pe_re_in_quad(pk.zone.x, pk.zone.z, x, z)) { if (out) *out = pk; return (int)i; }
    }
    return -1;
}

int pe_re_take_equipment_or_item(PeReProgress *p, const PeImpRoomLogic *room, const PeImpPickup *pk,
                                 const PeImpRoomLogic *items, char *say, size_t n, const char *game,
                                 PeRePickupRoute weapon, PeRePickupRoute ammo) {
    char name[48];
    pe_re_item_name(items, pk->item, name, sizeof name);
    if (pe_re_pickup_gone(room, NULL, p, pk)) return -1;
    int routed = -1;
    int weapon_granted = 0;
    if (p->ntaken < PE_RE_TAKEN_MAX) {
        routed = weapon ? weapon(game, pk->item, pk->count) : 0;
        weapon_granted = routed > 0;
        if (!routed && ammo) routed = ammo(game, pk->item, pk->count ? pk->count : 1);
    }
    if (routed < 0 || (!routed && pe_re_give(p, pk->item, pk->count))) {
        snprintf(say, n, "No room for the %s.", name);
        return -1;
    }
    p->taken[p->ntaken++] = taken_key(room, pk->flag);
    pe_re_flag_set(&p->flags, p->item_group ? p->item_group : PE_RE_FG_ITEM, pk->flag, 1);
    if (pk->count > 1 && !weapon_granted) snprintf(say, n, "Picked up %u %s.", (unsigned)pk->count, name);
    else snprintf(say, n, "Picked up the %s.", name);
    return 0;
}

int pe_re_take_routed(PeReProgress *p, const PeImpRoomLogic *room, const PeImpPickup *pk,
                      const PeImpRoomLogic *items, char *say, size_t n, const char *game,
                      PeRePickupRoute credit) {
    return pe_re_take_equipment_or_item(p, room, pk, items, say, n, game, NULL, credit);
}

int pe_re_take(PeReProgress *p, const PeImpRoomLogic *room, const PeImpPickup *pk, const PeImpRoomLogic *items,
               char *say, size_t n) {
    return pe_re_take_routed(p, room, pk, items, say, n, NULL, NULL);
}

/* A message zone: a trigger of kind 4, or any live zone an aot_reset turned into one (a door that
 * now only says it's locked). */
int pe_re_message_at(const PeImpRoomLogic *room, const PeReScdRun *run, double x, double z) {
    uint32_t n = 0;
    if (!room) return -1;
    pe_imp_logic_section(room, PE_IMP_LOGIC_TRIG, &n, NULL);
    for (uint32_t i = 0; i < n; i++) {
        PeImpTrigger t;
        uint8_t data[6];
        if (pe_imp_logic_trigger(room, i, &t) || !live(run, t.at) || !(t.zone.sat & PE_RE_SAT_CHECK)) continue;
        memcpy(data, t.data, sizeof data);
        if (pe_re_zone_kind(run, t.zone.id, t.zone.sce, data) != PE_RE_SCE_MESSAGE) continue;
        if (pe_re_in_quad(t.zone.x, t.zone.z, x, z)) return data[0] | data[1] << 8;
    }
    pe_imp_logic_section(room, PE_IMP_LOGIC_DOOR, &n, NULL);
    for (uint32_t i = 0; i < n; i++) {
        PeImpDoor d;
        uint8_t data[6];
        if (pe_imp_logic_door(room, i, &d) || !live(run, d.at)) continue;
        if (pe_re_zone_kind(run, d.zone.id, d.zone.sce, data) != PE_RE_SCE_MESSAGE) continue;
        if (pe_re_in_quad(d.zone.x, d.zone.z, x, z)) return data[0] | data[1] << 8;
    }
    return -1;
}
