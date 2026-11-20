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
#include <stdlib.h>
#include <string.h>

#include "pe_imp_rdt.h"
#include "pe_imp_spu.h"

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

/* an enemy placement, kept as its instruction for the encounter work to read */
static void keep_enemy(PeImpRdt *r, const uint8_t *ins, uint32_t size) {
    if (r->enemies < PE_IMP_RDT_ENEMIES) {
        memset(r->enemy[r->enemies], 0, PE_IMP_LOGIC_ENEM_BYTES);
        memcpy(r->enemy[r->enemies], ins, size < PE_IMP_LOGIC_ENEM_BYTES ? size : PE_IMP_LOGIC_ENEM_BYTES);
    }
    r->enemies++;
}

static void visit1(void *ctx, uint32_t proc, uint32_t at, const uint8_t *ins, uint32_t size) {
    (void)proc;
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
    case 0x1B:                                                   /* an enemy, 22 bytes */
        keep_enemy(r, ins, size);
        break;
    default:
        break;
    }
}

static void visit(void *ctx, uint32_t proc, uint32_t at, const uint8_t *ins, uint32_t size) {
    (void)proc;
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
        keep_enemy(r, ins, size);
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

/* sections kept as they are, per game: {tag, section index} */
typedef struct { uint32_t tag; unsigned index; } Raw;
static const Raw k_re1_raw[] = {{PE_IMP_LOGIC_COLL, 1}, {PE_IMP_LOGIC_MSG0, 11}, {PE_IMP_LOGIC_SCD0, 6},
                                {PE_IMP_LOGIC_SCD1, 7}, {PE_IMP_LOGIC_SCD2, 8}};
static const Raw k_re2_raw[] = {{PE_IMP_LOGIC_COLL, 6}, {PE_IMP_LOGIC_MSG0, 13}, {PE_IMP_LOGIC_MSG1, 14},
                                {PE_IMP_LOGIC_SCD0, 16}, {PE_IMP_LOGIC_SCD1, 17}};
static const Raw k_re3_raw[] = {{PE_IMP_LOGIC_COLL, 6}, {PE_IMP_LOGIC_MSG0, 13}, {PE_IMP_LOGIC_MSG1, 14},
                                {PE_IMP_LOGIC_SCD0, 16}};

static uint32_t min32(uint32_t a, uint32_t b) { return a < b ? a : b; }

/* The room's messages as text (TEXT). The character table and RE2/RE3's codes are biohazard-utils'
 * (Room/Msg.cs, MIT, Ted John): a message list is u16 offsets (the first / 2 = the count); a byte
 * below 96 is a character; F9 p green on (p 1) / off, FA p start, FB p a yes/no question, FC line
 * break, FD p page break, FE end. RE1 (not in biohazard-utils; read from the disc) uses the same table
 * in its message section (11) with its own codes: 01 p the end, 02 a line break, 03 p a page break,
 * 04 p a pause or cue, 79 a full stop, 74-77 the letters of "S.T.A.R.S."; its entries starting 04
 * are cues, not text. The English list: RE1 section 11, RE2 14, RE3 13 (the US release's English). */
static const char *const k_chars[96] = {
    " ", ".", "", "", "", "(", ")", "", "", "“", "”", "", "0", "1", "2", "3",
    "4", "5", "6", "7", "8", "9", ":", "", ",", "\"", "!", "?", "", "A", "B", "C",
    "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S",
    "T", "U", "V", "W", "X", "Y", "Z", "[", "/", "]", "'", "-", "", "a", "b", "c",
    "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s",
    "t", "u", "v", "w", "x", "y", "z", "", "", "", "", "", "", "", "", "",
};

static void put(char **o, size_t *len, size_t *cap, const char *s) {
    size_t k = strlen(s);
    if (!*o) return;
    if (*len + k + 1 > *cap) {
        size_t c = (*cap ? *cap * 2 : 1024) + k;
        char *g = realloc(*o, c);
        if (!g) { free(*o), *o = NULL; return; }
        *o = g, *cap = c;
    }
    memcpy(*o + *len, s, k), *len += k;
}

static void add_text(PeImpLogicBuilder *b, unsigned game, const uint8_t *rdt, uint32_t size) {
    uint32_t off, len;
    unsigned index = game == PE_IMP_RE1 ? 11 : game == PE_IMP_RE2 ? 14 : 13;
    if (section(game, rdt, size, index, &off, &len) || len < 2) return;
    const uint8_t *m = rdt + off;
    uint32_t count = le16(m) / 2;
    if (!count || 2ull * count > len) return;
    size_t olen = 0, cap = 1024;
    char *o = malloc(cap), hex[8];
    for (uint32_t i = 0; i < count && o; i++) {
        uint32_t at = le16(m + 2 * i), end = i + 1 < count ? le16(m + 2 * i + 2) : len;
        if (end > len || at > end) at = end = 0;
        int text = !(game == PE_IMP_RE1 && at < end && m[at] == 0x04);
        for (uint32_t k = at; text && k < end; k++) {
            unsigned c = m[k];
            if (game == PE_IMP_RE1) {                                    /* RE1's own codes (read from the disc) */
                static const char *const k_stars[4] = {"S.", "T.", "A.", "R."};
                if (c == 0x01) break;                                       /* 01 p: the end */
                if (c == 0x02) put(&o, &olen, &cap, "\n");
                else if (c == 0x03) put(&o, &olen, &cap, "#"), k++;      /* 03 p: a page break */
                else if (c == 0x04) k++;                                    /* 04 p: a pause or cue */
                else if (c == 0x79) put(&o, &olen, &cap, ".");
                else if (c >= 0x74 && c <= 0x77) put(&o, &olen, &cap, k_stars[c - 0x74]);   /* "S.T.A.R.S." */
                else if (c < 96) put(&o, &olen, &cap, k_chars[c]);
                continue;
            }
            if (c == 0xFE) break;
            if (c == 0xF9 && k + 1 < end) put(&o, &olen, &cap, m[++k] == 1 ? "{" : "}");
            else if (c == 0xFA) k++;
            else if (c == 0xFB && k + 1 < end) snprintf(hex, sizeof hex, "@%02X", m[++k]), put(&o, &olen, &cap, hex);
            else if (c == 0xFC) put(&o, &olen, &cap, "\n");
            else if (c == 0xFD) put(&o, &olen, &cap, "#"), k++;
            else if (c < 96) put(&o, &olen, &cap, k_chars[c]);
        }
        if (o) put(&o, &olen, &cap, ""), o[olen++] = 0;               /* each ends in NUL */
    }
    if (o) pe_imp_logic_add(b, PE_IMP_LOGIC_TEXT, count, o, (uint32_t)olen);
    free(o);
}

/* RE3's room archive (.ARD): its other entries but the music (0, 1) and the RDT (8), packed as
 * u32 count, then per entry u32 index, u32 type, u32 size and the bytes, padded to 4 */
static void add_archive(PeImpLogicBuilder *b, const uint8_t *ard, uint32_t size) {
    if (size < 2048 + 16) return;
    uint32_t entries = le32(ard + 4);
    if (entries < 9 || entries > 255 || 8 + entries * 8 > 2048) return;
    uint32_t pos = 2048, total = 4, count = 0;
    uint8_t *packed = malloc((size_t)size + 4 + entries * 16);
    if (!packed) return;
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t len = le32(ard + 8 + i * 8), type = le32(ard + 12 + i * 8);
        if (pos > size || len > size - pos) break;
        if (i >= 2 && i != 8) {
            uint8_t *e = packed + total;
            for (int k = 0; k < 4; k++)
                e[k] = (uint8_t)(i >> 8 * k), e[4 + k] = (uint8_t)(type >> 8 * k), e[8 + k] = (uint8_t)(len >> 8 * k);
            memcpy(e + 12, ard + pos, len);
            uint32_t padded = (len + 3) & ~3u;
            memset(e + 12 + len, 0, padded - len);
            total += 12 + padded, count++;
        }
        pos += (len + 2047) & ~2047u;
    }
    for (int k = 0; k < 4; k++) packed[k] = (uint8_t)(count >> 8 * k);
    if (count) pe_imp_logic_add(b, PE_IMP_LOGIC_ARDS, count, packed, total);
    free(packed);
}

static PeImpRoomLogic *build_logic(unsigned game, uint32_t player, uint32_t stage, uint32_t room,
                                   const uint8_t *rdt, uint32_t size, const uint8_t *ard, uint32_t ard_size,
                                   char *why, size_t n) {
    PeImpRdt *r = malloc(sizeof *r);
    if (!r) { if (why && n) snprintf(why, n, "out of memory"); return NULL; }
    if (pe_imp_rdt_read(game, rdt, size, r, why, n)) { free(r); return NULL; }
    PeImpLogicBuilder *b = pe_imp_logic_begin(game, player, stage, room);
    pe_imp_logic_add_door(b, r->door, min32(r->doors, PE_IMP_RDT_DOORS));
    pe_imp_logic_add_trigger(b, r->aot, min32(r->aots, PE_IMP_RDT_AOTS));
    pe_imp_logic_add_pickup(b, r->item, min32(r->items, PE_IMP_RDT_ITEMS));
    pe_imp_logic_add_origins(b, r->door, min32(r->doors, PE_IMP_RDT_DOORS), r->aot, min32(r->aots, PE_IMP_RDT_AOTS),
                             r->item, min32(r->items, PE_IMP_RDT_ITEMS));
    pe_imp_logic_add_camera_zone(b, r->cut, min32(r->cuts, PE_IMP_RDT_CUTS));
    uint32_t enemies = min32(r->enemies, PE_IMP_RDT_ENEMIES);
    if (enemies) pe_imp_logic_add(b, PE_IMP_LOGIC_ENEM, enemies, r->enemy, enemies * PE_IMP_LOGIC_ENEM_BYTES);
    const Raw *raw = game == PE_IMP_RE1 ? k_re1_raw : game == PE_IMP_RE2 ? k_re2_raw : k_re3_raw;
    size_t raws = game == PE_IMP_RE1 ? sizeof k_re1_raw / sizeof *raw
                : game == PE_IMP_RE2 ? sizeof k_re2_raw / sizeof *raw : sizeof k_re3_raw / sizeof *raw;
    for (size_t k = 0; k < raws; k++) {
        uint32_t off, len;
        if (!section(game, rdt, size, raw[k].index, &off, &len))
            pe_imp_logic_add(b, raw[k].tag, 0, rdt + off, len);
    }
    add_text(b, game, rdt, size);
    pe_imp_logic_add(b, PE_IMP_LOGIC_RDT0, 0, rdt, size);              /* the whole room, offsets intact */
    if (ard) add_archive(b, ard, ard_size);
    free(r);
    PeImpRoomLogic *l = pe_imp_logic_finish(b);
    if (!l && why && n) snprintf(why, n, "RDT: the room is too large to keep");
    return l;
}

PeImpRoomLogic *pe_imp_rdt_logic(unsigned game, uint32_t player, uint32_t stage, uint32_t room,
                                 const uint8_t *rdt, uint32_t size, char *why, size_t n) {
    return build_logic(game, player, stage, room, rdt, size, NULL, 0, why, n);
}

int pe_imp_re_room_name(unsigned game, const char *path, uint32_t *stage, uint32_t *room, uint32_t *player) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *prefix = game == PE_IMP_RE3 ? "R" : "ROOM";
    size_t skip = strlen(prefix), digits = game == PE_IMP_RE3 ? 3 : 4;
    if (strncmp(base, prefix, skip)) return -1;
    unsigned v[4] = {0};
    for (size_t i = 0; i < digits; i++) {
        char c = base[skip + i];
        if (c >= '0' && c <= '9') v[i] = (unsigned)(c - '0');
        else if (c >= 'A' && c <= 'F') v[i] = (unsigned)(c - 'A' + 10);
        else return -1;
    }
    if (base[skip + digits] != '.' || !v[0]) return -1;
    *stage = v[0] - 1, *room = v[1] << 4 | v[2], *player = game == PE_IMP_RE3 ? 0 : v[3];
    return 0;
}

/* the room's sound bank (RE1 sections 17/18, RE2 1/2: a VAB header and its samples) as Sounds in
 * bank "room<stage><room>"; 0 if there is none */
static int emit_room_sounds(PeImpSink *sink, unsigned game, uint32_t stage, uint32_t room, const uint8_t *rdt,
                            uint32_t size, char *why, size_t n) {
    uint32_t vh, vh_len, vb, vb_len;
    unsigned hi = game == PE_IMP_RE1 ? 17 : 1, bi = game == PE_IMP_RE1 ? 18 : 2;
    if (game == PE_IMP_RE3 || section(game, rdt, size, hi, &vh, &vh_len) || section(game, rdt, size, bi, &vb, &vb_len) ||
        vh_len < 32 || memcmp(rdt + vh, "pBAV", 4))
        return 0;
    uint32_t programs = le16(rdt + vh + 18), waves = le16(rdt + vh + 22);
    uint32_t header = 32 + 2048 + programs * 512 + 512, samples = 0;
    if (!waves || programs > 128 || waves > 254 || header > vh_len) return 0;
    for (uint32_t i = 1; i <= waves; i++) samples += le16(rdt + vh + 32 + 2048 + programs * 512 + 2 * i) * 8u;
    if (samples > vb_len) return 0;
    char bank[24];
    snprintf(bank, sizeof bank, "room%x%02x", (unsigned)stage + 1, (unsigned)room);
    return pe_imp_vab_read(sink, bank, rdt + vh, header, rdt + vb, samples, why, n);
}

static int emit_logic(PeImpSink *sink, unsigned game, const char *path, const uint8_t *rdt, uint32_t size,
                      const uint8_t *ard, uint32_t ard_size, const char *const players[2], char *why, size_t n) {
    uint32_t stage, room, player;
    if (pe_imp_re_room_name(game, path, &stage, &room, &player) || player > 1) return 0;   /* not a room file */
    PeImpRoomLogic *l = build_logic(game, player, stage, room, rdt, size, ard, ard_size, why, n);
    if (!l) return -1;
    if (emit_room_sounds(sink, game, stage, room, rdt, size, why, n)) { free(l); return -1; }
    char id[32], name[48];
    if (game == PE_IMP_RE3) {
        snprintf(id, sizeof id, "logic/%x%02x", (unsigned)stage + 1, (unsigned)room);
        snprintf(name, sizeof name, "Room %u-%02X", (unsigned)stage + 1, (unsigned)room);
    } else {
        snprintf(id, sizeof id, "logic/%x%02x-%u", (unsigned)stage + 1, (unsigned)room, (unsigned)player);
        snprintf(name, sizeof name, "Room %u-%02X (%s)", (unsigned)stage + 1, (unsigned)room,
                 players && players[player] ? players[player] : player ? "player 2" : "player 1");
    }
    if (pe_imp_emit(sink, PE_IMP_ROOMLOGIC, id, name, l)) {
        if (why && n) snprintf(why, n, "couldn't catalog %s", id);
        return -1;
    }
    return 0;
}

int pe_imp_emit_re_logic(PeImpSink *sink, unsigned game, const char *path, const uint8_t *rdt, uint32_t size,
                         const char *const players[2], char *why, size_t n) {
    return emit_logic(sink, game, path, rdt, size, NULL, 0, players, why, n);
}

int pe_imp_emit_re3_logic(PeImpSink *sink, const char *path, const uint8_t *ard, uint32_t size, char *why, size_t n) {
    const uint8_t *rdt;
    uint32_t rdt_size;
    if (pe_imp_re3_ard_rdt(ard, size, &rdt, &rdt_size)) return 0;          /* an archive without a room */
    return emit_logic(sink, PE_IMP_RE3, path, rdt, rdt_size, ard, size, NULL, why, n);
}
