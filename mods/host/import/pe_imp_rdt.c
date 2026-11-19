/* Resident Evil 1, 2 and 3 rooms: doors, trigger zones, items and camera switch zones (pe_imp_rdt.h).
 *
 * RE2/RE3: an RDT starts with 8 bytes of counts, then 23 u32 section offsets. Section 8 is the
 * camera switch zones (RVD: 20-byte records, flag, from, to, four corners, ending with flag 0xFFFF).
 * Section 16 (and, in RE2, 17) is a script: a table of u16 offsets to its procedures (the first
 * offset / 2 is how many), each a run of instructions ending with evt_end (opcode 1).
 * RE1: 19 u32 section offsets at 0x48. Section 0 is the camera zones (u16 to, u16 from, four
 * corners; to 9 marks the camera's own area; ends with 0xFFFF). Sections 6 and 7 (init, main) are
 * scripts: a chain of procedures, each a u16 length (counting itself) then its instructions, ending
 * with a length of 0. Section 8, the event scripts, has its own instruction set and isn't walked. */
#include <stdio.h>
#include <string.h>

#include "pe_imp_rdt.h"

/* biohazard-utils Bio2ConstantTable.cs g_instructionSizes (MIT, Ted John) */
static const uint8_t k_re2_sizes[] = {
    1, 2, 1, 4, 4, 2, 4, 4, 1, 4, 3, 1, 1, 6, 2, 4,
    2, 4, 2, 4, 6, 2, 2, 6, 2, 2, 2, 6, 1, 4, 1, 1,
    1, 4, 4, 6, 4, 3, 6, 4, 1, 2, 1, 6, 20, 38, 3, 4,
    1, 1, 8, 8, 4, 3, 12, 4, 3, 8, 16, 32, 2, 3, 6, 4,
    8, 10, 1, 4, 22, 5, 10, 2, 16, 8, 2, 3, 5, 22, 22, 4,
    4, 6, 6, 6, 22, 6, 4, 8, 4, 4, 2, 2, 3, 2, 2, 2,
    14, 4, 2, 1, 16, 2, 1, 28, 40, 30, 6, 4, 1, 4, 6, 2,
    1, 1, 16, 8, 4, 22, 3, 4, 6, 1, 16, 16, 6, 6, 6, 6,
    2, 3, 3, 1, 2, 6, 1, 1, 3, 1, 6, 6, 8, 24, 24};
/* biohazard-utils Bio3ConstantTable.cs g_instructionSizes (MIT, Ted John) */
static const uint8_t k_re3_sizes[] = {
    1, 2, 1, 2, 4, 2, 4, 4, 2, 1, 3, 1, 1, 6, 4, 2,
    4, 2, 4, 2, 4, 6, 2, 2, 6, 2, 4, 2, 1, 4, 4, 4,
    6, 4, 4, 1, 2, 4, 6, 1, 1, 8, 6, 2, 4, 4, 6, 2,
    6, 6, 6, 4, 10, 6, 3, 2, 2, 16, 16, 3, 1, 2, 2, 3,
    4, 3, 3, 6, 6, 4, 11, 3, 4, 1, 1, 1, 4, 4, 6, 1,
    2, 1, 2, 3, 4, 8, 8, 6, 6, 8, 2, 6, 2, 1, 3, 2,
    22, 32, 40, 20, 28, 10, 2, 22, 30, 14, 16, 2, 4, 4, 4, 2,
    16, 18, 22, 24, 5, 2, 3, 12, 6, 4, 2, 6, 1, 24, 2, 40,
    4, 8, 10, 1, 4, 2, 1, 1, 4, 2, 1, 1, 1, 1, 4, 2};

/* biohazard-utils Bio1ConstantTable.cs _instructionSizes1 (MIT, Ted John); 0 = variable, not walked */
static const uint8_t k_re1_sizes[] = {
    2, 2, 2, 2, 4, 4, 4, 6, 4, 2, 2, 4, 26, 18, 2, 8,
    2, 2, 10, 4, 4, 2, 2, 10, 26, 4, 2, 22, 6, 2, 4, 28,
    14, 14, 4, 2, 4, 4, 0, 2, 6, 2, 12, 4, 2, 4, 0, 4,
    12, 4, 4, 2, 8, 4, 4, 4, 4, 2, 4, 6, 6, 12, 2, 6,
    16, 4, 4, 4, 2, 2, 44, 14, 2, 2, 2, 2, 4, 2, 4, 2,
    2};

/* the opcodes this reader decodes, per game */
typedef struct { uint8_t aot, aot4, door, door4, item, item4, enemy; } Ops;
static const Ops k_re2_ops = {0x2C, 0x67, 0x3B, 0x68, 0x4E, 0x69, 0x44};
static const Ops k_re3_ops = {0x63, 0x64, 0x61, 0x62, 0x67, 0x68, 0x7D};

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }
static int16_t s16(const uint8_t *p) { uint32_t v = le16(p); return (int16_t)(v < 0x8000 ? (int)v : (int)v - 0x10000); }

uint32_t pe_imp_scd_size(unsigned game, uint8_t op) {
    if (game == PE_IMP_RE1) return op < sizeof k_re1_sizes ? k_re1_sizes[op] : 0;
    if (game == PE_IMP_RE2) return op < sizeof k_re2_sizes ? k_re2_sizes[op] : 0;
    if (game == PE_IMP_RE3) return op < sizeof k_re3_sizes ? k_re3_sizes[op] : 0;
    return 0;
}

static int section(unsigned game, const uint8_t *rdt, uint32_t size, unsigned index, uint32_t *offset,
                   uint32_t *length) {
    uint32_t table = game == PE_IMP_RE1 ? 0x48 : 8, count = game == PE_IMP_RE1 ? 19 : 23;
    if (index >= count || size < table + count * 4) return -1;
    uint32_t at = le32(rdt + table + index * 4), end = size;
    if (!at || at >= size || at < table + count * 4) return -1;
    for (unsigned i = 0; i < count; i++) {
        uint32_t o = le32(rdt + table + i * 4);
        if (o > at && o < end) end = o;
    }
    *offset = at, *length = end - at;
    return 0;
}

int pe_imp_rdt_section(const uint8_t *rdt, uint32_t size, unsigned index, uint32_t *offset, uint32_t *length) {
    return section(PE_IMP_RE2, rdt, size, index, offset, length);
}

/* RE1's chain of length-prefixed procedures */
static int walk_re1(const uint8_t *rdt, uint32_t size, PeImpScdVisit visit, void *ctx, uint32_t *bad) {
    uint32_t proc = 0;
    for (unsigned s = 6; s <= 7; s++) {
        uint32_t off, len;
        if (section(PE_IMP_RE1, rdt, size, s, &off, &len)) continue;
        uint32_t p = off, stop = off + len;
        while (p + 2 <= stop) {
            uint32_t n = le16(rdt + p);
            if (!n) break;
            if (n < 2 || n > stop - p) { (*bad)++; break; }
            uint32_t q = p + 2, end = p + n;
            while (q < end) {
                uint8_t op = rdt[q];
                uint32_t k = pe_imp_scd_size(PE_IMP_RE1, op);
                if (!k || q + k > end) { (*bad)++; break; }
                if (visit) visit(ctx, proc, q, rdt + q, k);
                q += k;
            }
            proc++;
            p = end;
        }
    }
    return (int)proc;
}

int pe_imp_scd_walk(unsigned game, const uint8_t *rdt, uint32_t size, PeImpScdVisit visit, void *ctx,
                    uint32_t *unreadable) {
    if (game == PE_IMP_RE1) {
        uint32_t bad = 0;
        int procs = size >= 0x94 ? walk_re1(rdt, size, visit, ctx, &bad) : -1;
        if (unreadable) *unreadable = bad;
        return procs;
    }
    if (game != PE_IMP_RE2 && game != PE_IMP_RE3) return -1;
    static const unsigned k_sections[2][2] = {{16, 17}, {16, 16}};
    const unsigned *sections = k_sections[game == PE_IMP_RE3];
    uint32_t proc = 0, bad = 0;
    for (unsigned s = 0; s < (game == PE_IMP_RE2 ? 2u : 1u); s++) {
        uint32_t off, len;
        if (section(game, rdt, size, sections[s], &off, &len) || len < 2) continue;
        uint32_t count = le16(rdt + off) / 2;
        if (!count || count * 2 > len) { bad++; continue; }
        for (uint32_t k = 0; k < count; k++) {
            uint32_t start = le16(rdt + off + 2 * k);
            uint32_t end = k + 1 < count ? le16(rdt + off + 2 * (k + 1)) : len;
            if (start >= len || end > len || end < start) { bad++; continue; }
            uint32_t p = off + start, stop = off + end;
            while (p < stop) {
                uint8_t op = rdt[p];
                uint32_t n = pe_imp_scd_size(game, op);
                if (!n || p + n > stop) { bad++; break; }
                if (visit) visit(ctx, proc, p, rdt + p, n);
                p += n;
                if (op == 1) break;                         /* evt_end */
            }
            proc++;
        }
    }
    if (unreadable) *unreadable = bad;
    return (int)proc;
}

/* zone fields after the opcode: id, sce, sat, floor, super, then a rectangle or four corners */
static const uint8_t *zone(const uint8_t *ins, int four, PeImpRdtZone *z) {
    memset(z, 0, sizeof *z);
    z->id = ins[1], z->sce = ins[2], z->sat = ins[3], z->floor = ins[4], z->super = ins[5];
    const uint8_t *p = ins + 6;
    if (four) {
        z->corners = 4;
        for (int i = 0; i < 4; i++) z->x[i] = s16(p + 4 * i), z->z[i] = s16(p + 4 * i + 2);
        return p + 16;
    }
    int x = s16(p), zz = s16(p + 2), w = (int)le16(p + 4), d = (int)le16(p + 6);
    int xs[4] = {x, x + w, x + w, x}, zs[4] = {zz, zz, zz + d, zz + d};
    for (int i = 0; i < 4; i++)
        z->x[i] = (int16_t)(xs[i] > 32767 ? 32767 : xs[i]), z->z[i] = (int16_t)(zs[i] > 32767 ? 32767 : zs[i]);
    return p + 8;
}

typedef struct { const Ops *ops; PeImpRdt *out; } Walk;

/* RE1 zones: id, then a rectangle right after it */
static void zone1(const uint8_t *ins, PeImpRdtZone *z) {
    memset(z, 0, sizeof *z);
    z->id = ins[1];
    int x = s16(ins + 2), zz = s16(ins + 4), w = (int)le16(ins + 6), d = (int)le16(ins + 8);
    int xs[4] = {x, x + w, x + w, x}, zs[4] = {zz, zz, zz + d, zz + d};
    for (int i = 0; i < 4; i++)
        z->x[i] = (int16_t)(xs[i] > 32767 ? 32767 : xs[i]), z->z[i] = (int16_t)(zs[i] > 32767 ? 32767 : zs[i]);
}

static void visit1(void *ctx, uint32_t proc, uint32_t at, const uint8_t *ins, uint32_t size) {
    (void)proc, (void)size;
    PeImpRdt *r = ((Walk *)ctx)->out;
    r->instructions++;
    switch (ins[0]) {
    case 0x0C:                                                   /* door_aot_set, 26 bytes */
        if (r->doors < PE_IMP_RDT_DOORS) {
            PeImpRdtDoor *d = &r->door[r->doors];
            memset(d, 0, sizeof *d);
            zone1(ins, &d->zone);
            d->zone.sce = ins[10];                               /* "special" */
            d->animation = ins[12], d->lock_id = ins[14];
            unsigned t = ins[15];
            d->stage = (uint8_t)((t >> 5) ? (t >> 5) - 1 : 0xFF), d->room = (uint8_t)(t & 31);
            d->next_x = s16(ins + 16), d->next_y = s16(ins + 18), d->next_z = s16(ins + 20);
            d->next_dir = s16(ins + 22), d->lock_type = ins[24], d->free = ins[25], d->at = at;
        }
        r->doors++;
        break;
    case 0x0D:                                                   /* aot_set, 18 bytes */
        if (r->aots < PE_IMP_RDT_AOTS) {
            PeImpRdtAot *a = &r->aot[r->aots];
            zone1(ins, &a->zone);
            a->zone.sce = ins[10];
            memcpy(a->data, ins + 11, 6);
            a->at = at;
        }
        r->aots++;
        break;
    case 0x18:                                                   /* item_aot_set, 26 bytes */
        if (r->items < PE_IMP_RDT_ITEMS) {
            PeImpRdtItem *it = &r->item[r->items];
            memset(it, 0, sizeof *it);
            zone1(ins, &it->zone);
            it->item = ins[10], it->count = ins[11], it->at = at;
        }
        r->items++;
        break;
    case 0x1B:                                                   /* an enemy */
        r->enemies++;
        break;
    default:
        break;
    }
}

static void visit(void *ctx, uint32_t proc, uint32_t at, const uint8_t *ins, uint32_t size) {
    (void)proc, (void)size;
    Walk *w = ctx;
    PeImpRdt *r = w->out;
    const Ops *o = w->ops;
    uint8_t op = ins[0];
    r->instructions++;
    if (op == o->door || op == o->door4) {
        if (r->doors < PE_IMP_RDT_DOORS) {
            PeImpRdtDoor *d = &r->door[r->doors];
            const uint8_t *p = zone(ins, op == o->door4, &d->zone);
            d->next_x = s16(p), d->next_y = s16(p + 2), d->next_z = s16(p + 4), d->next_dir = s16(p + 6);
            p += 8;
            d->stage = p[0], d->room = p[1], d->camera = p[2], d->next_floor = p[3], d->texture = p[4];
            d->animation = p[5], d->sound = p[6], d->lock_id = p[7], d->lock_type = p[8], d->free = p[9];
            d->at = at;
        }
        r->doors++;
    } else if (op == o->item || op == o->item4) {
        if (r->items < PE_IMP_RDT_ITEMS) {
            PeImpRdtItem *it = &r->item[r->items];
            const uint8_t *p = zone(ins, op == o->item4, &it->zone);
            it->item = (uint16_t)le16(p), it->count = (uint16_t)le16(p + 2), it->flag = (uint16_t)le16(p + 4);
            it->model = p[6], it->action = p[7], it->at = at;
        }
        r->items++;
    } else if (op == o->aot || op == o->aot4) {
        if (r->aots < PE_IMP_RDT_AOTS) {
            PeImpRdtAot *a = &r->aot[r->aots];
            const uint8_t *p = zone(ins, op == o->aot4, &a->zone);
            memcpy(a->data, p, 6);
            a->at = at;
        }
        r->aots++;
    } else if (op == o->enemy) {
        r->enemies++;
    }
}

int pe_imp_rdt_read(unsigned game, const uint8_t *rdt, uint32_t size, PeImpRdt *out, char *why, size_t n) {
    memset(out, 0, sizeof *out);
    out->game = game;
    if ((game != PE_IMP_RE1 && game != PE_IMP_RE2 && game != PE_IMP_RE3) ||
        size < (game == PE_IMP_RE1 ? 0x94u : 8u + 23 * 4)) {
        if (why && n) snprintf(why, n, "RDT: not a Resident Evil room");
        return -1;
    }
    Walk w = {game == PE_IMP_RE3 ? &k_re3_ops : &k_re2_ops, out};
    int procs = pe_imp_scd_walk(game, rdt, size, game == PE_IMP_RE1 ? visit1 : visit, &w, &out->unreadable);
    if (procs < 0) {
        if (why && n) snprintf(why, n, "RDT: the scripts can't be read");
        return -1;
    }
    out->procedures = (uint32_t)procs;
    uint32_t off, len;
    if (!section(game, rdt, size, game == PE_IMP_RE1 ? 0 : 8, &off, &len)) {
        for (uint32_t p = off; p + 20 <= off + len; p += 20) {
            uint32_t flag = le16(rdt + p);
            if (flag == 0xffff) break;
            if (out->cuts < PE_IMP_RDT_CUTS) {
                PeImpRdtCut *c = &out->cut[out->cuts];
                if (game == PE_IMP_RE1)                          /* u16 to (9: the camera's own area), u16 from */
                    c->flag = flag == 9 ? 1 : 0, c->to = (uint8_t)flag, c->from = rdt[p + 2];
                else
                    c->flag = (uint16_t)flag, c->from = rdt[p + 2], c->to = rdt[p + 3];
                for (int i = 0; i < 4; i++) c->x[i] = s16(rdt + p + 4 + 4 * i), c->z[i] = s16(rdt + p + 6 + 4 * i);
            }
            out->cuts++;
        }
    }
    return 0;
}
