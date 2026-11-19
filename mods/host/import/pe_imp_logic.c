/* RoomLogic records (pe_imp_logic.h): building, reading, saving, loading. Little-endian, bounded. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_logic.h"

#define HEADER 28u                                   /* "RML1", version, game, player, stage, room, sections */

struct PeImpLogicBuilder {
    uint32_t game, player, stage, room, sections, size, cap;
    uint8_t *data;
    int failed;
};

static uint32_t get16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | get16(p + 2) << 16; }
static int16_t gets16(const uint8_t *p) { uint32_t v = get16(p); return (int16_t)(v < 0x8000 ? (int)v : (int)v - 0x10000); }
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xffff), put16(p + 2, v >> 16); }

PeImpLogicBuilder *pe_imp_logic_begin(uint32_t game, uint32_t player, uint32_t stage, uint32_t room) {
    PeImpLogicBuilder *b = calloc(1, sizeof *b);
    if (b) b->game = game, b->player = player, b->stage = stage, b->room = room;
    return b;
}

int pe_imp_logic_add(PeImpLogicBuilder *b, uint32_t tag, uint32_t records, const void *bytes, uint32_t size) {
    if (!b || b->failed) return -1;
    uint32_t padded = (size + 3) & ~3u, need = 12 + padded;
    if (size > PE_IMP_ROOMLOGIC_MAX || b->size + need > PE_IMP_ROOMLOGIC_MAX) { b->failed = 1; return -1; }
    if (b->size + need > b->cap) {
        uint32_t cap = b->cap ? b->cap : 4096;
        while (cap < b->size + need) cap *= 2;
        uint8_t *g = realloc(b->data, cap);
        if (!g) { b->failed = 1; return -1; }
        b->data = g, b->cap = cap;
    }
    uint8_t *p = b->data + b->size;
    put32(p, tag), put32(p + 4, records), put32(p + 8, size);
    if (size) memcpy(p + 12, bytes, size);
    memset(p + 12 + size, 0, padded - size);
    b->size += need, b->sections++;
    return 0;
}

static void put_zone(uint8_t *p, const PeImpZone *z) {
    p[0] = z->id, p[1] = z->sce, p[2] = z->sat, p[3] = z->floor, p[4] = z->super, p[5] = z->corners;
    for (int i = 0; i < 4; i++) put16(p + 6 + 2 * i, (uint16_t)z->x[i]), put16(p + 14 + 2 * i, (uint16_t)z->z[i]);
}

static void get_zone(const uint8_t *p, PeImpZone *z) {
    z->id = p[0], z->sce = p[1], z->sat = p[2], z->floor = p[3], z->super = p[4], z->corners = p[5];
    for (int i = 0; i < 4; i++) z->x[i] = gets16(p + 6 + 2 * i), z->z[i] = gets16(p + 14 + 2 * i);
}

/* adds n records of `bytes` each, packed by `pack` */
static int add_records(PeImpLogicBuilder *b, uint32_t tag, uint32_t n, uint32_t bytes,
                       void (*pack)(uint8_t *, const void *, uint32_t), const void *src) {
    if (!n) return 0;
    if (n > PE_IMP_ROOMLOGIC_MAX / bytes) { if (b) b->failed = 1; return -1; }
    uint8_t *tmp = calloc(n, bytes);
    if (!tmp) { if (b) b->failed = 1; return -1; }
    for (uint32_t i = 0; i < n; i++) pack(tmp + i * bytes, src, i);
    int rc = pe_imp_logic_add(b, tag, n, tmp, n * bytes);
    free(tmp);
    return rc;
}

static void pack_door(uint8_t *p, const void *src, uint32_t i) {
    const PeImpDoor *d = (const PeImpDoor *)src + i;
    put_zone(p, &d->zone);
    put16(p + 22, (uint16_t)d->next_x), put16(p + 24, (uint16_t)d->next_y), put16(p + 26, (uint16_t)d->next_z);
    put16(p + 28, (uint16_t)d->next_dir);
    p[30] = d->stage, p[31] = d->room, p[32] = d->camera, p[33] = d->next_floor, p[34] = d->texture;
    p[35] = d->animation, p[36] = d->sound, p[37] = d->lock_id, p[38] = d->lock_type, p[39] = d->free;
}
static void pack_trigger(uint8_t *p, const void *src, uint32_t i) {
    const PeImpTrigger *t = (const PeImpTrigger *)src + i;
    put_zone(p, &t->zone);
    memcpy(p + 22, t->data, 6);
}
static void pack_pickup(uint8_t *p, const void *src, uint32_t i) {
    const PeImpPickup *k = (const PeImpPickup *)src + i;
    put_zone(p, &k->zone);
    put16(p + 22, k->item), put16(p + 24, k->count), put16(p + 26, k->flag);
    p[28] = k->model, p[29] = k->action;
}
static void pack_camera_zone(uint8_t *p, const void *src, uint32_t i) {
    const PeImpCameraZone *c = (const PeImpCameraZone *)src + i;
    put16(p, c->flag), p[2] = c->from, p[3] = c->to;
    for (int k = 0; k < 4; k++) put16(p + 4 + 2 * k, (uint16_t)c->x[k]), put16(p + 12 + 2 * k, (uint16_t)c->z[k]);
}

int pe_imp_logic_add_door(PeImpLogicBuilder *b, const PeImpDoor *d, uint32_t n) {
    return add_records(b, PE_IMP_LOGIC_DOOR, n, PE_IMP_LOGIC_DOOR_BYTES, pack_door, d);
}
int pe_imp_logic_add_trigger(PeImpLogicBuilder *b, const PeImpTrigger *t, uint32_t n) {
    return add_records(b, PE_IMP_LOGIC_TRIG, n, PE_IMP_LOGIC_TRIG_BYTES, pack_trigger, t);
}
int pe_imp_logic_add_pickup(PeImpLogicBuilder *b, const PeImpPickup *k, uint32_t n) {
    return add_records(b, PE_IMP_LOGIC_PICK, n, PE_IMP_LOGIC_PICK_BYTES, pack_pickup, k);
}
int pe_imp_logic_add_camera_zone(PeImpLogicBuilder *b, const PeImpCameraZone *c, uint32_t n) {
    return add_records(b, PE_IMP_LOGIC_CAMZ, n, PE_IMP_LOGIC_CAMZ_BYTES, pack_camera_zone, c);
}

/* the sections are well formed: each inside the payload, record sizes as their tag says */
static int sections_ok(const uint8_t *p, uint32_t size, uint32_t sections) {
    uint32_t at = 0;
    for (uint32_t s = 0; s < sections; s++) {
        if (size - at < 12) return 0;
        uint32_t tag = get32(p + at), records = get32(p + at + 4), bytes = get32(p + at + 8);
        uint32_t padded = (bytes + 3) & ~3u;
        if (bytes > size || padded > size - at - 12) return 0;
        uint32_t each = tag == PE_IMP_LOGIC_DOOR ? PE_IMP_LOGIC_DOOR_BYTES : tag == PE_IMP_LOGIC_TRIG ? PE_IMP_LOGIC_TRIG_BYTES
                      : tag == PE_IMP_LOGIC_PICK ? PE_IMP_LOGIC_PICK_BYTES : tag == PE_IMP_LOGIC_CAMZ ? PE_IMP_LOGIC_CAMZ_BYTES
                      : tag == PE_IMP_LOGIC_ENEM ? PE_IMP_LOGIC_ENEM_BYTES : 0;
        if (each && (uint64_t)records * each != bytes) return 0;
        at += 12 + padded;
    }
    return at == size;
}

PeImpRoomLogic *pe_imp_logic_finish(PeImpLogicBuilder *b) {
    if (!b) return NULL;
    PeImpRoomLogic *l = NULL;
    if (!b->failed && (l = malloc(sizeof *l + b->size + 4))) {
        l->game = b->game, l->player = b->player, l->stage = b->stage, l->room = b->room, l->size = b->size;
        if (b->size) memcpy(l->payload, b->data, b->size);
    }
    free(b->data);
    free(b);
    return l;
}

const uint8_t *pe_imp_logic_section(const PeImpRoomLogic *l, uint32_t tag, uint32_t *records, uint32_t *bytes) {
    uint32_t at = 0;
    while (l->size - at >= 12) {
        uint32_t t = get32(l->payload + at), r = get32(l->payload + at + 4), n = get32(l->payload + at + 8);
        uint32_t padded = (n + 3) & ~3u;
        if (padded > l->size - at - 12) break;
        if (t == tag) {
            if (records) *records = r;
            if (bytes) *bytes = n;
            return l->payload + at + 12;
        }
        at += 12 + padded;
    }
    if (records) *records = 0;
    if (bytes) *bytes = 0;
    return NULL;
}

static const uint8_t *record(const PeImpRoomLogic *l, uint32_t tag, uint32_t each, uint32_t i) {
    uint32_t records, bytes;
    const uint8_t *p = pe_imp_logic_section(l, tag, &records, &bytes);
    return p && i < records && (uint64_t)(i + 1) * each <= bytes ? p + i * each : NULL;
}

int pe_imp_logic_door(const PeImpRoomLogic *l, uint32_t i, PeImpDoor *d) {
    const uint8_t *p = record(l, PE_IMP_LOGIC_DOOR, PE_IMP_LOGIC_DOOR_BYTES, i);
    if (!p) return -1;
    memset(d, 0, sizeof *d);
    get_zone(p, &d->zone);
    d->next_x = gets16(p + 22), d->next_y = gets16(p + 24), d->next_z = gets16(p + 26), d->next_dir = gets16(p + 28);
    d->stage = p[30], d->room = p[31], d->camera = p[32], d->next_floor = p[33], d->texture = p[34];
    d->animation = p[35], d->sound = p[36], d->lock_id = p[37], d->lock_type = p[38], d->free = p[39];
    return 0;
}
int pe_imp_logic_trigger(const PeImpRoomLogic *l, uint32_t i, PeImpTrigger *t) {
    const uint8_t *p = record(l, PE_IMP_LOGIC_TRIG, PE_IMP_LOGIC_TRIG_BYTES, i);
    if (!p) return -1;
    memset(t, 0, sizeof *t);
    get_zone(p, &t->zone);
    memcpy(t->data, p + 22, 6);
    return 0;
}
int pe_imp_logic_pickup(const PeImpRoomLogic *l, uint32_t i, PeImpPickup *k) {
    const uint8_t *p = record(l, PE_IMP_LOGIC_PICK, PE_IMP_LOGIC_PICK_BYTES, i);
    if (!p) return -1;
    memset(k, 0, sizeof *k);
    get_zone(p, &k->zone);
    k->item = (uint16_t)get16(p + 22), k->count = (uint16_t)get16(p + 24), k->flag = (uint16_t)get16(p + 26);
    k->model = p[28], k->action = p[29];
    return 0;
}
int pe_imp_logic_camera_zone(const PeImpRoomLogic *l, uint32_t i, PeImpCameraZone *c) {
    const uint8_t *p = record(l, PE_IMP_LOGIC_CAMZ, PE_IMP_LOGIC_CAMZ_BYTES, i);
    if (!p) return -1;
    c->flag = (uint16_t)get16(p), c->from = p[2], c->to = p[3];
    for (int k = 0; k < 4; k++) c->x[k] = gets16(p + 4 + 2 * k), c->z[k] = gets16(p + 12 + 2 * k);
    return 0;
}

static FILE *open_logic(const char *folder, const char *mode) {
    char path[PE_IMP_PATH_MAX];
    if (snprintf(path, sizeof path, "%s/roomlogic.bin", folder) >= (int)sizeof path) return NULL;
    return fopen(path, mode);
}

static int bad(char *why, size_t n) {
    if (why && n) snprintf(why, n, "invalid or unreadable roomlogic.bin");
    return -1;
}

static uint32_t count_sections(const PeImpRoomLogic *l) {
    uint32_t at = 0, n = 0;
    while (l->size - at >= 12) {
        uint32_t padded = (get32(l->payload + at + 8) + 3) & ~3u;
        if (padded > l->size - at - 12) return UINT32_MAX;
        at += 12 + padded, n++;
    }
    return at == l->size ? n : UINT32_MAX;
}

int pe_imp_logic_save(const PeImpRoomLogic *l, const char *folder, char *why, size_t n) {
    uint32_t sections = count_sections(l);
    if (l->size > PE_IMP_ROOMLOGIC_MAX || sections == UINT32_MAX || !sections_ok(l->payload, l->size, sections))
        return bad(why, n);
    FILE *f = open_logic(folder, "wb");
    if (!f) return bad(why, n);
    uint8_t h[HEADER] = {'R', 'M', 'L', '1'};
    put32(h + 4, PE_IMP_ROOMLOGIC_VERSION), put32(h + 8, l->game), put32(h + 12, l->player);
    put32(h + 16, l->stage), put32(h + 20, l->room), put32(h + 24, sections);
    int ok = fwrite(h, sizeof h, 1, f) == 1 && (!l->size || fwrite(l->payload, 1, l->size, f) == l->size);
    if (fclose(f)) ok = 0;
    return ok ? 0 : bad(why, n);
}

int pe_imp_logic_load(const char *folder, PeImpRoomLogic **out, char *why, size_t n) {
    *out = NULL;
    FILE *f = open_logic(folder, "rb");
    if (!f) return bad(why, n);
    uint8_t h[HEADER];
    PeImpRoomLogic *l = NULL;
    int ok = 0;
    if (fread(h, sizeof h, 1, f) == 1 && !memcmp(h, "RML1", 4) && get32(h + 4) == PE_IMP_ROOMLOGIC_VERSION &&
        !fseek(f, 0, SEEK_END)) {
        long end = ftell(f);
        if (end >= (long)HEADER && (unsigned long)(end - (long)HEADER) <= PE_IMP_ROOMLOGIC_MAX &&
            !fseek(f, (long)HEADER, SEEK_SET)) {
            uint32_t size = (uint32_t)(end - (long)HEADER);
            l = malloc(sizeof *l + size + 4);
            if (l && (!size || fread(l->payload, 1, size, f) == size)) {
                l->game = get32(h + 8), l->player = get32(h + 12), l->stage = get32(h + 16), l->room = get32(h + 20);
                l->size = size;
                ok = sections_ok(l->payload, size, get32(h + 24));
            }
        }
    }
    fclose(f);
    if (!ok) {
        free(l);
        return bad(why, n);
    }
    *out = l;
    return 0;
}
