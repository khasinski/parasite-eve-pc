/* Resident Evil room objects and floor items: a room file's sections 2 (props) and 3 (items) list
 * (u32 model offset, u32 texture offset) pairs, header bytes 2 and 3 of them. A model is a PS1 TMD
 * (u32 0x41, u32 flags 0, u32 objects; per object 7 u32: vertices, count, normals, count,
 * primitives, count, scale, offsets from the object table), one part per object; the texture an 8-bit
 * TIM with palettes of 256. Primitives read (mode, words): 0x34/0x36 textured smooth triangle
 * (6: u0 v0 clut, u1 v1 page, u2 v2, n0 v0, n1 v1, n2 v2), 0x24 textured flat triangle (5: the
 * same uv words, n0 v0, v1 v2), 0x3C textured smooth quad (8: four uv words, four n/v pairs). The
 * rare others (untextured, unlit) are passed over. Each becomes a one-bone Model
 * "object/<stage><room>-<n>" or "item/<stage><room>-<n>". Read only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_rdt.h"
#include "pe_src_re1.h"

typedef struct { const uint8_t *b; uint32_t size; } Buf;
static int ok(const Buf *x, uint64_t at, uint64_t len) { return at <= x->size && len <= x->size - at; }
static unsigned u8at(const Buf *x, uint32_t at) { return x->b[at]; }
static unsigned u16at(const Buf *x, uint32_t at) { return x->b[at] | x->b[at + 1] << 8; }
static uint32_t u32at(const Buf *x, uint32_t at) { return u16at(x, at) | (uint32_t)u16at(x, at + 2) << 16; }
static int16_t s16at(const Buf *x, uint32_t at) { return (int16_t)u16at(x, at); }

#define NEED(at, len) do { if (!ok(&x, (at), (len))) goto bad; } while (0)
#define MAX_PRIMS 4096u

static int tmd_parse(const uint8_t *data, uint32_t size, uint32_t tmd, uint32_t tim, PeImpModel *m) {
    Buf x = {data, size};
    PeImpFace *tri = NULL, *quad = NULL;
    memset(m, 0, sizeof *m);
    m->bones = 1;
    m->parent = malloc(sizeof *m->parent), m->rel = calloc(1, sizeof *m->rel), m->pos = calloc(1, sizeof *m->pos);
    if (!m->parent || !m->rel || !m->pos) goto bad;
    m->parent[0] = -1;
    NEED(tmd, 12);
    uint32_t objs = u32at(&x, tmd + 8), faces = 0;
    if (u32at(&x, tmd) != 0x41 || u32at(&x, tmd + 4) != 0 || !objs || objs > PE_IMP_MODEL_PARTS_MAX) goto bad;
    uint32_t ot = tmd + 12;
    NEED(ot, (uint64_t)28 * objs);
    m->parts = objs;
    m->part = calloc(objs, sizeof *m->part);
    if (!m->part) goto bad;
    for (uint32_t o = 0; o < objs; o++) {                              /* one part per TMD object */
        uint32_t ob = ot + 28 * o;
        uint32_t vt = ot + u32at(&x, ob), nv = u32at(&x, ob + 4), nt = ot + u32at(&x, ob + 8), nn = u32at(&x, ob + 12);
        uint32_t pt = ot + u32at(&x, ob + 16), np = u32at(&x, ob + 20);
        uint32_t tris = 0, quads = 0;
        if (np > MAX_PRIMS || nv > 65535 || nn > 65535) goto bad;
        NEED(vt, (uint64_t)8 * nv);
        NEED(nt, (uint64_t)8 * nn);
        tri = calloc(np ? np : 1, sizeof *tri), quad = calloc(np ? np : 1, sizeof *quad);
        if (!tri || !quad) goto bad;
        uint32_t q = pt;
        for (uint32_t k = 0; k < np; k++) {
            NEED(q, 4);
            uint32_t ilen = u8at(&x, q + 1), flag = u8at(&x, q + 2), mode = u8at(&x, q + 3), p = q + 4;
            NEED(p, (uint64_t)4 * ilen);
            q = p + 4 * ilen;
            int smooth = (mode & ~2u) == 0x34 && ilen == 6, flat = mode == 0x24 && ilen == 5, four = mode == 0x3C && ilen == 8;
            if (flag || !(smooth || flat || four)) continue;           /* untextured or unlit: passed over */
            uint32_t corners = four ? 4 : 3, nidx[4], vidx[4];
            PeImpFace *f = four ? &quad[quads] : &tri[tris];
            memset(f, 0, sizeof *f);
            for (uint32_t j = 0; j < corners; j++)
                f->uv[j][0] = (uint8_t)u8at(&x, p + 4 * j), f->uv[j][1] = (uint8_t)u8at(&x, p + 4 * j + 1);
            f->clut = (uint16_t)u16at(&x, p + 2), f->page = (uint16_t)u16at(&x, p + 6);
            uint32_t idx = p + 4 * (four ? 4 : 3);
            if (flat) {
                nidx[0] = nidx[1] = nidx[2] = u16at(&x, idx);
                vidx[0] = u16at(&x, idx + 2), vidx[1] = u16at(&x, idx + 4), vidx[2] = u16at(&x, idx + 6);
            } else {
                for (uint32_t j = 0; j < corners; j++) nidx[j] = u16at(&x, idx + 4 * j), vidx[j] = u16at(&x, idx + 4 * j + 2);
            }
            for (uint32_t j = 0; j < corners; j++) {
                if (vidx[j] >= nv || nidx[j] >= nn) goto bad;
                for (int c = 0; c < 3; c++) {
                    f->v[j][c] = s16at(&x, vt + 8 * vidx[j] + 2 * (uint32_t)c);
                    f->n[j][c] = s16at(&x, nt + 8 * nidx[j] + 2 * (uint32_t)c);
                }
            }
            if (four) quads++;
            else tris++;
        }
        if ((faces += tris + quads) > PE_IMP_MODEL_FACES_MAX) goto bad;
        m->part[o].tri = tri, m->part[o].tris = tris, m->part[o].quad = quad, m->part[o].quads = quads;
        tri = quad = NULL;
    }
    if (!faces) goto bad;
    /* texture: an 8-bit TIM with palettes of 256 */
    NEED(tim, 20);
    uint32_t tflag = u32at(&x, tim + 4);
    if ((tflag & 3) != 1 || !(tflag & 8)) goto bad;
    uint32_t ln = u32at(&x, tim + 8), cw = u16at(&x, tim + 16), ch = u16at(&x, tim + 18);
    if (cw != 256 || !ch || ch > 256) goto bad;
    NEED((uint64_t)tim + 20, (uint64_t)2 * cw * ch);
    m->pal_rows = ch;
    m->pal = calloc(ch, sizeof *m->pal);
    if (!m->pal) goto bad;
    for (uint32_t r = 0; r < ch; r++)
        for (uint32_t c = 0; c < 256; c++) m->pal[r][c] = (uint16_t)u16at(&x, tim + 20 + 2 * cw * r + 2 * c);
    uint64_t at = (uint64_t)tim + 8 + ln;
    NEED(at, 12);
    uint32_t w = 2u * u16at(&x, (uint32_t)at + 8), h = u16at(&x, (uint32_t)at + 10);
    if (!w || !h || w > 1024 || h > 1024) goto bad;
    NEED(at + 12, (uint64_t)w * h);
    m->w = w, m->h = h;
    m->image = malloc((size_t)w * h);
    if (!m->image) goto bad;
    memcpy(m->image, data + at + 12, (size_t)w * h);
    char quiet[120];
    if (!pe_imp_model_check(m, quiet, sizeof quiet)) return 0;
bad:
    free(tri), free(quad);
    pe_imp_model_free(m);
    return -1;
}

static int read_objects(const uint8_t *rdt, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    uint32_t stage, room, player;
    if (pe_imp_re_room_name(PE_IMP_RE1, path, &stage, &room, &player) || size < 0x48 + 19 * 4) return 0;
    for (int kind = 0; kind < 2; kind++) {                             /* 0: props (section 2), 1: items (3) */
        uint32_t table = rdt[0x48 + 4 * (2 + kind)] | (uint32_t)rdt[0x49 + 4 * (2 + kind)] << 8 |
                         (uint32_t)rdt[0x4A + 4 * (2 + kind)] << 16 | (uint32_t)rdt[0x4B + 4 * (2 + kind)] << 24;
        uint32_t count = rdt[2 + kind];
        if (!table || table >= size || (uint64_t)table + 8ull * count > size) continue;
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t *e = rdt + table + 8 * i;
            uint32_t tmd = e[0] | (uint32_t)e[1] << 8 | (uint32_t)e[2] << 16 | (uint32_t)e[3] << 24;
            uint32_t tim = e[4] | (uint32_t)e[5] << 8 | (uint32_t)e[6] << 16 | (uint32_t)e[7] << 24;
            if (!tmd || tmd >= size || !tim || tim >= size) continue;
            PeImpModel *m = calloc(1, sizeof *m);
            if (!m) { if (why && n) snprintf(why, n, "out of memory"); return -1; }
            if (tmd_parse(rdt, size, tmd, tim, m)) { free(m); continue; }
            char id[40], name[48];
            snprintf(id, sizeof id, "%s/%x%02x-%u", kind ? "item" : "object", (unsigned)stage + 1, (unsigned)room, (unsigned)i);
            snprintf(name, sizeof name, "Room %u-%02X %s %u", (unsigned)stage + 1, (unsigned)room, kind ? "item" : "object", (unsigned)i);
            if (pe_imp_emit(sink, PE_IMP_MODEL, id, name, m)) {
                if (why && n) snprintf(why, n, "couldn't catalog %s", id);
                return -1;
            }
        }
    }
    return 0;
}

const PeImpReader g_re1_objects_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re1", .platform = "ps1", .format = "rdt-objects",
    .version = 1, .produces = PE_IMP_MODEL, .match = "*/STAGE?/ROOM*.RDT",
    .tags = "room object", .read = read_objects, .path_key = 1
};

/* <dir>/ITEM_M1/DOOR*.DOR, the door transitions: u32 offsets of the animation, the model (a TMD of
 * the door and its frame) and its texture (an 8-bit TIM). The model becomes "doormodel/<name>"; the
 * whole file, animation included, is the DoorScene (pe_src_re1_door.c). */
static int read_door_model(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    if (size < 12) return 0;
    uint32_t tmd = data[4] | (uint32_t)data[5] << 8 | (uint32_t)data[6] << 16 | (uint32_t)data[7] << 24;
    uint32_t tim = data[8] | (uint32_t)data[9] << 8 | (uint32_t)data[10] << 16 | (uint32_t)data[11] << 24;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char stem[24] = "", id[40], name[48];
    size_t k = 0;
    for (const char *c = base + 4; *c && *c != '.' && k + 1 < sizeof stem; c++) stem[k++] = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
    stem[k] = 0;
    PeImpModel *m = calloc(1, sizeof *m);
    if (!m) { if (why && n) snprintf(why, n, "out of memory"); return -1; }
    if (strncmp(base, "DOOR", 4) || !k || tmd_parse(data, size, tmd, tim, m)) { free(m); return 0; }
    snprintf(id, sizeof id, "doormodel/%s", stem);
    snprintf(name, sizeof name, "Door model %s", stem);
    if (pe_imp_emit(sink, PE_IMP_MODEL, id, name, m)) {
        if (why && n) snprintf(why, n, "couldn't catalog %s", id);
        return -1;
    }
    return 0;
}

const PeImpReader g_re1_door_models_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re1", .platform = "ps1", .format = "door-model",
    .version = 1, .produces = PE_IMP_MODEL, .match = "*/ITEM_M1/DOOR*.DOR",
    .tags = "door", .read = read_door_model, .path_key = 1
};
