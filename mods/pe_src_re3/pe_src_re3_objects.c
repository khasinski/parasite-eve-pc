/* Resident Evil 3 room objects: the 3D props a room places. A room file's section 10 lists them,
 * header byte 2 objects of (u32 texture offset, u32 mesh offset), as in RE2. A mesh starts with a
 * small header whose first u16 is the offset of an MD2 block (the player models' mesh format, see
 * pe_src_re3_pld.c); its other fields look like a tiny skeleton and aren't read yet. The texture is
 * an 8-bit TIM with palettes of 256. Each object becomes a one-bone Model "object/<stage><room>-<n>",
 * one part per MD2 object, vertices as stored (an object whose pieces move separately would need
 * that skeleton). Read only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_rdt.h"
#include "pe_src_re3.h"

typedef struct { const uint8_t *b; uint32_t size; } Buf;
static int ok(const Buf *x, uint64_t at, uint64_t len) { return at <= x->size && len <= x->size - at; }
static unsigned u8at(const Buf *x, uint32_t at) { return x->b[at]; }
static unsigned u16at(const Buf *x, uint32_t at) { return x->b[at] | x->b[at + 1] << 8; }
static uint32_t u32at(const Buf *x, uint32_t at) { return u16at(x, at) | (uint32_t)u16at(x, at + 2) << 16; }
static int16_t s16at(const Buf *x, uint32_t at) { return (int16_t)u16at(x, at); }

#define NEED(at, len) do { if (!ok(&x, (at), (len))) goto bad; } while (0)

static int object_parse(const uint8_t *data, uint32_t size, uint32_t mesh, uint32_t tim, PeImpModel *m) {
    Buf x = {data, size};
    memset(m, 0, sizeof *m);
    m->bones = 1;
    m->parent = malloc(sizeof *m->parent), m->rel = calloc(1, sizeof *m->rel), m->pos = calloc(1, sizeof *m->pos);
    if (!m->parent || !m->rel || !m->pos) goto bad;
    m->parent[0] = -1;
    NEED(mesh, 2);
    uint32_t md2 = mesh + u16at(&x, mesh);
    NEED(md2, 8);
    uint32_t objs = u32at(&x, md2 + 4), base = md2 + 8;
    if (!objs || objs > PE_IMP_MODEL_PARTS_MAX) goto bad;
    NEED(base, (uint64_t)24 * objs);
    /* the skeleton: an EMR header at mesh + 4 (u16 tree offset, key frames offset, bones, key frame
     * size; bone offsets s16 x, y, z from + 8; per bone u16 child count, offset of its child bytes,
     * from the tree). Used when it names one bone per MD2 object (663 of 712); else one bone. */
    NEED(mesh + 4, 8);
    uint32_t emr = mesh + 4, tree = emr + u16at(&x, emr), nb = u16at(&x, emr + 4);
    if (nb == objs && nb > 1 && nb <= PE_IMP_MODEL_BONES_MAX && ok(&x, emr + 8, 6ull * nb) && ok(&x, tree, 4ull * nb)) {
        int16_t *parent = malloc(nb * sizeof *parent);
        int32_t (*rel)[3] = calloc(nb, sizeof *rel), (*pos)[3] = calloc(nb, sizeof *pos);
        int good = parent && rel && pos;
        for (uint32_t b = 0; good && b < nb; b++) parent[b] = -1;
        for (uint32_t b = 0; good && b < nb; b++) {
            uint32_t kids = u16at(&x, tree + 4 * b), at = tree + u16at(&x, tree + 4 * b + 2);
            if (!ok(&x, at, kids)) { good = 0; break; }
            for (uint32_t k = 0; k < kids; k++) {
                uint32_t c = u8at(&x, at + k);
                if (c >= nb || c <= b) { good = 0; break; }               /* children come after their parent */
                parent[c] = (int16_t)b;
            }
        }
        for (uint32_t b = 0; good && b < nb; b++)
            for (int c = 0; c < 3; c++) {
                rel[b][c] = s16at(&x, emr + 8 + 6 * b + 2 * (uint32_t)c);
                pos[b][c] = (parent[b] >= 0 ? pos[parent[b]][c] : 0) + rel[b][c];
            }
        if (good) {
            free(m->parent), free(m->rel), free(m->pos);
            m->bones = nb, m->parent = parent, m->rel = rel, m->pos = pos;
        } else {
            free(parent), free(rel), free(pos);
        }
    }
    m->parts = objs;
    m->part = calloc(objs, sizeof *m->part);
    if (!m->part) goto bad;
    uint32_t total = 0;
    for (uint32_t i = 0; i < objs; i++) {
        uint32_t at = base + 24 * i;
        uint32_t vo = u32at(&x, at), no = u32at(&x, at + 4), vc = u32at(&x, at + 8), to = u32at(&x, at + 12), qo = u32at(&x, at + 16);
        uint32_t tc = u16at(&x, at + 20), qc = u16at(&x, at + 22);
        if (vc > 256 || (total += tc + qc) > PE_IMP_MODEL_FACES_MAX) goto bad;
        NEED((uint64_t)base + vo, (uint64_t)8 * vc);
        NEED((uint64_t)base + no, (uint64_t)8 * vc);
        NEED((uint64_t)base + to, (uint64_t)12 * tc);
        NEED((uint64_t)base + qo, (uint64_t)16 * qc);
        for (int kind = 0; kind < 2; kind++) {
            uint32_t fc = kind ? qc : tc, k = kind ? 4 : 3;
            PeImpFace *f = fc ? calloc(fc, sizeof *f) : NULL;
            if (fc && !f) goto bad;
            if (kind) m->part[i].quad = f, m->part[i].quads = fc;
            else m->part[i].tri = f, m->part[i].tris = fc;
            static const uint8_t uv_at[2][4] = {{0, 4, 8, 0}, {0, 4, 8, 12}};
            for (uint32_t q = 0; q < fc; q++) {
                uint32_t a = base + (kind ? qo + 16 * q : to + 12 * q), idx[4];
                if (kind) idx[0] = u8at(&x, a + 10), idx[1] = u8at(&x, a + 11), idx[2] = u8at(&x, a + 14), idx[3] = u8at(&x, a + 15);
                else idx[0] = u8at(&x, a + 7), idx[1] = u8at(&x, a + 10), idx[2] = u8at(&x, a + 11);
                for (uint32_t j = 0; j < k; j++) {
                    if (idx[j] >= vc) goto bad;
                    for (int c = 0; c < 3; c++) {
                        f[q].v[j][c] = s16at(&x, base + vo + 8 * idx[j] + 2 * (uint32_t)c);
                        f[q].n[j][c] = s16at(&x, base + no + 8 * idx[j] + 2 * (uint32_t)c);
                    }
                    f[q].uv[j][0] = (uint8_t)u8at(&x, a + uv_at[kind][j]);
                    f[q].uv[j][1] = (uint8_t)u8at(&x, a + uv_at[kind][j] + 1);
                }
                f[q].clut = (uint16_t)u16at(&x, a + 2);
                f[q].page = (uint16_t)u8at(&x, a + 6);
            }
        }
    }
    /* texture: an 8-bit TIM with palettes of 256 */
    NEED(tim, 20);
    uint32_t flag = u32at(&x, tim + 4);
    if ((flag & 3) != 1 || !(flag & 8)) goto bad;
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
    pe_imp_model_free(m);
    return -1;
}

static int read_objects(const uint8_t *ard, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    uint32_t stage, room, player, at, len;
    const uint8_t *rdt;
    uint32_t rdt_size;
    if (pe_imp_re_room_name(PE_IMP_RE3, path, &stage, &room, &player) || pe_imp_re3_ard_rdt(ard, size, &rdt, &rdt_size) ||
        rdt_size < 8 || pe_imp_rdt_section(rdt, rdt_size, 10, &at, &len))
        return 0;
    uint32_t count = rdt[2];
    if ((uint64_t)count * 8 > len) count = len / 8;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = rdt + at + 8 * i;
        uint32_t tim = e[0] | (uint32_t)e[1] << 8 | (uint32_t)e[2] << 16 | (uint32_t)e[3] << 24;
        uint32_t mesh = e[4] | (uint32_t)e[5] << 8 | (uint32_t)e[6] << 16 | (uint32_t)e[7] << 24;
        if (!mesh || mesh >= rdt_size || !tim || tim >= rdt_size) continue;    /* an empty slot */
        PeImpModel *m = calloc(1, sizeof *m);
        if (!m) { if (why && n) snprintf(why, n, "out of memory"); return -1; }
        if (object_parse(rdt, rdt_size, mesh, tim, m)) { free(m); continue; }  /* not one we read */
        char id[40], name[48];
        snprintf(id, sizeof id, "object/%x%02x-%u", (unsigned)stage + 1, (unsigned)room, (unsigned)i);
        snprintf(name, sizeof name, "Room %u-%02X object %u", (unsigned)stage + 1, (unsigned)room, (unsigned)i);
        if (pe_imp_emit(sink, PE_IMP_MODEL, id, name, m)) {
            if (why && n) snprintf(why, n, "couldn't catalog %s", id);
            return -1;
        }
    }
    return 0;
}

const PeImpReader g_re3_objects_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "ps1", .format = "rdt-objects",
    .version = 2, .produces = PE_IMP_MODEL, .match = "CD_DATA/STAGE?/R*.ARD",
    .tags = "room object", .read = read_objects, .path_key = 1
};
