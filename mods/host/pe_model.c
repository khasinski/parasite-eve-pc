/* Decoded game models. See pe_model.h. Mirrors the decomp bridge's renderer
 * (tools/pe_assets/render_models.py: records, upload_tims, model, relocate, anim_frame_at), which
 * renders every Bestiary form correctly; every read here is bounds-checked. */
#include <stdlib.h>
#include <string.h>

#include "pe_disc_assets.h"
#include "pe_model.h"
#include "pe_mod_host.h"

#define PE_VRAM_W   1024
#define PE_VRAM_H   512
#define PE_CACHE    4
#define PE_DIR_MODELS 0x0C               /* container directory: model records */
#define PE_DIR_ROWS   0x10               /*                      animation rows */
#define PE_DIR_TIMS   0x28               /*                      TIM records */
/* Enemy animation rows: 0 / 1 are hit reactions, 2 the idle (little motion, loops, no travel),
 * 4 the walk, 6 and up attacks (measured over the Bestiary's rooms). */
#define PE_IDLE_ROW   2

typedef struct { const uint8_t *p; uint32_t n; } PeBuf;

static int pe_has(PeBuf b, uint32_t at, uint32_t len) { return at <= b.n && len <= b.n - at; }
static uint32_t pe_u8(PeBuf b, uint32_t at) { return pe_has(b, at, 1) ? b.p[at] : 0; }
static uint32_t pe_u16(PeBuf b, uint32_t at) { return pe_has(b, at, 2) ? (uint32_t)b.p[at] | (uint32_t)b.p[at + 1] << 8 : 0; }
static int32_t pe_s16(PeBuf b, uint32_t at) { return (int16_t)pe_u16(b, at); }
static uint32_t pe_u32(PeBuf b, uint32_t at) { return pe_u16(b, at) | pe_u16(b, at + 2) << 16; }

/* A container's records of one kind: (offset, id, group) triples, 12 bytes each. */
static int pe_records(PeBuf b, uint32_t field, uint32_t *at, uint32_t *count) {
    const uint32_t dir = pe_u32(b, 4), desc = pe_u32(b, dir + field);
    if (!pe_has(b, dir, field + 4)) return 0;
    *at = desc & 0x3FFFFFu;
    *count = desc >> 22;
    return pe_has(b, *at, *count * 12u);
}

/* ---- textures: a container's TIM records into the private VRAM ---- */
static void pe_put_rect(uint16_t *vram, PeBuf b, uint32_t src, uint32_t rect, uint32_t h) {
    const uint32_t x = (rect >> 10) & 0x7FF, y = rect >> 21, w = rect & 0x3FF;
    uint32_t row, col;
    for (row = 0; row < h; ++row)
        for (col = 0; col < w; ++col) {
            const uint32_t p = src + 2 * (row * w + col);
            if (pe_has(b, p, 2) && y + row < PE_VRAM_H && x + col < PE_VRAM_W)
                vram[(y + row) * PE_VRAM_W + x + col] = (uint16_t)pe_u16(b, p);
        }
}

static void pe_upload_tims(uint16_t *vram, PeBuf b) {
    uint32_t dir, desc, at, n, i;
    if (b.n < 8) return;
    dir = pe_u32(b, 4);
    if (!pe_has(b, dir, PE_DIR_TIMS + 4)) return;
    desc = pe_u32(b, dir + PE_DIR_TIMS);
    at = desc & 0x3FFFFFu;
    n = desc >> 22;
    if (!pe_has(b, at, n * 20u)) return;
    for (i = 0; i < n; ++i) {
        const uint32_t r = at + 20 * i, src = pe_u32(b, r + 4), rect = pe_u32(b, r + 8);
        const uint32_t sec = pe_u32(b, r + 12), rect2 = pe_u32(b, r + 16);
        pe_put_rect(vram, b, src & 0xFFFFFFu, rect, (src >> 24) ? (src >> 24) : 256u);
        if (sec & 0xFFFFFFu) pe_put_rect(vram, b, (src & 0xFFFFFFu) + (sec & 0xFFFFFFu), rect2, sec >> 24);
    }
}

/* ---- the model block (Render_SetupEntityPrims' layout) ---- */
static int pe_parse(PeBuf b, uint32_t off, PeModel *m) {
    const uint32_t parts = pe_u8(b, off + 2), verts = pe_u16(b, off + 6), cmds = pe_u16(b, off + 0x18);
    uint32_t pk[4], faces = 0, at = off + 0x1C, i, k;
    for (k = 0; k < 4; ++k) { pk[k] = pe_u16(b, off + 8 + 2 * k); faces += pk[k]; }
    if (!parts || !verts || parts > 128 || !pe_has(b, off, 0x1C)) return 0;
    m->parts = (int)parts;
    m->verts = (int)verts;
    m->faces = (int)faces;
    m->commands = (int)cmds;
    m->part = calloc(parts, sizeof *m->part);
    m->vert = calloc(verts, sizeof *m->vert);
    m->face = calloc(faces ? faces : 1, sizeof *m->face);
    m->command = calloc(cmds ? cmds : 1, 1);
    if (!m->part || !m->vert || !m->face || !m->command) return 0;
    if (!pe_has(b, at, 12 * parts)) return 0;
    for (i = 0; i < parts; ++i, at += 12) {            /* vertex start, count, visible, pad, z offset */
        m->part[i].vertex_start = (uint16_t)pe_u16(b, at);
        m->part[i].vertex_count = (uint16_t)pe_u16(b, at + 2);
        m->part[i].visible = (uint8_t)(pe_u8(b, at + 4) == 1);   /* as the audit and Render_* read it */
        m->part[i].offset_z = (int32_t)pe_u32(b, at + 8);
    }
    if (!pe_has(b, at, 8 * verts)) return 0;
    for (i = 0; i < verts; ++i, at += 8)
        for (k = 0; k < 3; ++k) m->vert[i][k] = (int16_t)pe_s16(b, at + 2 * k);
    at += 4 * verts;                                   /* per-vertex lookup words */
    if (!pe_has(b, at, 12 * faces)) return 0;
    for (i = 0; i < faces; ++i, at += 12) {            /* descriptors: four vertex indices */
        PeModelFace *f = &m->face[i];
        for (k = 0; k < 4; ++k) f->v[k] = (uint16_t)pe_u16(b, at + 4 + 2 * k);
        f->quad = (uint8_t)(i < pk[0] || (i >= pk[0] + pk[1] && i < pk[0] + pk[1] + pk[2]));
        f->textured = (uint8_t)(i < pk[0] + pk[1]);
        for (k = 0; k < (f->quad ? 4u : 3u); ++k)
            if (f->v[k] >= verts) return 0;
    }
    at += (2 + 2 * parts + 1) * 8;                     /* bounds */
    if (!pe_has(b, at, cmds)) return 0;
    memcpy(m->command, b.p + at, cmds);
    at += (cmds + 3) & ~3u;
    for (i = 0; i < pk[0] + pk[1]; ++i) {              /* texture records: u v clut | u v tpage | u v | u v */
        PeModelFace *f = &m->face[i];
        const uint32_t len = i < pk[0] ? 16u : 12u;
        if (!pe_has(b, at, len)) return 0;
        for (k = 0; k < (f->quad ? 4u : 3u); ++k) { f->u[k] = (uint8_t)pe_u8(b, at + 4 * k); f->t[k] = (uint8_t)pe_u8(b, at + 4 * k + 1); }
        f->clut = (uint16_t)pe_u16(b, at + 2);
        f->tpage = (uint16_t)pe_u16(b, at + 6);
        at += len;
    }
    return 1;
}

/* Render_InitPrimBlock: move the faces' pages / palettes to where the entity's TIMs went. */
static void pe_relocate(PeModel *m, uint32_t x, uint32_t y, uint32_t palette_row) {
    const int half = y == 128;
    const uint32_t page = (((x >> 6) - 8) * 2) + (y >> 7) + ((y >> 8) * 30);
    const uint32_t po = x != 960 ? page >> 1 : ((page << 16) & 0xFFFFFFFFu) >> 17;
    const uint32_t pal = palette_row * 64 - 0x7080;
    int i, k;
    if (x == 960 && m->faces && m->face[0].textured && m->face[0].tpage == 31) return;   /* already placed */
    for (i = 0; i < m->faces; ++i) {
        PeModelFace *f = &m->face[i];
        if (!f->textured) continue;
        if (half) {
            const uint32_t add = f->t[0] < 128 ? po : po + 1;
            for (k = 0; k < (f->quad ? 4 : 3); ++k) f->t[k] = (uint8_t)(f->t[k] + 128);
            f->tpage = (uint16_t)(f->tpage + add);
        } else {
            f->tpage = (uint16_t)(f->tpage + po);
        }
        f->clut = (uint16_t)(f->clut + pal);
    }
}

/* An animation row's byte length (the skin loader's pe_row_length; Anim_DecodeBoneRotations*). */
static uint32_t pe_row_length(PeBuf b, uint32_t off) {
    const uint32_t flags = pe_u8(b, off), last = pe_u8(b, off + 1), frames = pe_u8(b, off + 2);
    const uint32_t stride = ((frames >> 1) + 1u) * 4u, bstride = (flags & 3u) == 2u ? stride : ((frames >> 2) + 1u) * 4u;
    uint32_t cur = off + 12u, k;
    for (k = 0; k < 3; ++k) cur += pe_u16(b, cur) ? 4u : stride;
    for (k = 0; k < (last + 1u) * 3u; ++k) {
        const int constant = (flags & 3u) == 2u ? pe_u16(b, cur) != 0 : pe_u8(b, cur) != 0;
        cur += constant ? 4u : bstride;
        if (cur - off > 0x10000u) return 0;
    }
    return pe_has(b, off, cur - off) ? cur - off : 0;
}

static int pe_decode(const PeRoomBlobs *room, int slot, int player, PeModel *out) {
    const PeBuf data = {room->data, room->data_size};
    uint32_t at, n, i, off = 0, trailing = 0, row = 0, row_id = 0xFFFFFFFFu;
    int found = 0;
    memset(out, 0, sizeof *out);
    if (!pe_records(data, PE_DIR_MODELS, &at, &n)) return 0;
    for (i = 0; i < n; ++i) {
        const uint32_t w = pe_u32(data, at + 12 * i + 4);
        if ((int)(w >> 24) == slot) { off = w & 0xFFFFFFu; trailing = pe_u32(data, at + 12 * i + 8); found = 1; }
    }
    if (!found || !pe_parse(data, off, out)) { pe_model_free(out); return 0; }
    if ((trailing >> 8) & 0xF)
        pe_relocate(out, (trailing >> 6) & 0x3C0, (trailing >> 9) & 0x180, ((trailing >> 18) & 0xFF) + 0x1C0);
    out->vram = calloc((size_t)PE_VRAM_W * PE_VRAM_H, sizeof *out->vram);
    if (!out->vram) { pe_model_free(out); return 0; }
    pe_upload_tims(out->vram, (PeBuf){room->scratch, room->scratch_size});
    pe_upload_tims(out->vram, (PeBuf){room->texture, room->texture_size});
    if (pe_records(data, PE_DIR_ROWS, &at, &n))        /* the idle row; else this slot's lowest row */
        for (i = 0; i < n; ++i) {
            const uint32_t w = pe_u32(data, at + 12 * i + 4), group = pe_u8(data, at + 12 * i + 11), id = w >> 24;
            if ((!player && (int)group != slot) || row_id == (player ? 0x15u : PE_IDLE_ROW)) continue;
            if (id == (player ? 0x15u : PE_IDLE_ROW) || id < row_id) { row_id = id; row = w & 0xFFFFFFu; }
        }
    if (row_id != 0xFFFFFFFFu) {
        const uint32_t len = pe_row_length(data, row);
        if (len && (out->anim = malloc(len))) { memcpy(out->anim, data.p + row, len); out->anim_size = len; }
    }
    return 1;
}

int pe_model_decode(const PeRoomBlobs *room, int slot, PeModel *out) { return pe_decode(room, slot, 0, out); }
int pe_model_decode_player(const PeRoomBlobs *room, PeModel *out) { return pe_decode(room, 0, 1, out); }

void pe_model_free(PeModel *m) {
    free(m->part); free(m->vert); free(m->face); free(m->command); free(m->vram); free(m->anim);
    memset(m, 0, sizeof *m);
}

/* ---- animation (Anim_DecodeBoneRotationsByte / Short): one frame ---- */
int pe_model_frames(const PeModel *m) {
    const int f = m->anim ? m->anim[2] : 0;
    return f > 0 ? f : 1;
}

void pe_model_frame(const PeModel *m, int frame, int translation[3], uint16_t (*angles)[3]) {
    const PeBuf b = {m->anim, m->anim_size};
    int k, p;
    uint32_t cur = 12, flags, last, frames, stride, bstride;
    memset(translation, 0, 3 * sizeof *translation);
    memset(angles, 0, (size_t)m->parts * sizeof *angles);
    if (!m->anim) return;
    flags = b.p[0]; last = b.p[1]; frames = b.p[2];
    if (frame >= (int)frames) frame = frames ? (int)frames - 1 : 0;
    if (frame < 0) frame = 0;
    stride = ((frames >> 1) + 1) * 4;
    bstride = (flags & 3) == 2 ? stride : ((frames >> 2) + 1) * 4;
    for (k = 0; k < 3; ++k) {
        const int constant = pe_s16(b, cur) != 0;
        translation[k] = pe_s16(b, constant ? cur + 2 : cur + 2 + 2 * (uint32_t)frame);
        cur += constant ? 4 : stride;
    }
    for (p = 0; p <= (int)last; ++p)
        for (k = 0; k < 3; ++k) {            /* a constant channel is 4 bytes; a sampled one a value a frame */
            const int shorts = (flags & 3) == 2;
            const int constant = shorts ? pe_s16(b, cur) != 0 : pe_u8(b, cur) != 0;
            const uint16_t a = shorts ? (uint16_t)pe_u16(b, constant ? cur + 2 : cur + 2 + 2 * (uint32_t)frame)
                                      : (uint16_t)(pe_u8(b, constant ? cur + 1 : cur + 1 + (uint32_t)frame) * 16);
            cur += constant ? 4 : bstride;
            if (p < m->parts) angles[p][k] = a;
        }
}

/* ---- loading, with a small cache ---- */
static struct { PeModelSource src; char folder[160]; PeModel model; const PeModel *external; unsigned used; } s_cache[PE_CACHE];
static unsigned s_tick;

static int pe_same(const PeModelSource *a, const PeModelSource *b) {
    return a->kind == b->kind && a->map == b->map && a->actor == b->actor && a->load == b->load && a->user == b->user &&
           (a->folder == b->folder || (a->folder && b->folder && strcmp(a->folder, b->folder) == 0));
}

const PeModel *pe_model_load(const PeModelSource *src) {
    int i, slot = 0;
    if (!src || (src->folder && strlen(src->folder) >= sizeof s_cache[0].folder)) return NULL;
    for (i = 0; i < PE_CACHE; ++i)
        if (s_cache[i].used && pe_same(&s_cache[i].src, src)) {
            s_cache[i].used = ++s_tick;
            return s_cache[i].external ? s_cache[i].external : &s_cache[i].model;
        }
    for (i = 1; i < PE_CACHE; ++i) if (s_cache[i].used < s_cache[slot].used) slot = i;   /* least recent */
    pe_model_free(&s_cache[slot].model);
    s_cache[slot].used = 0;
    s_cache[slot].external = NULL;
    if (src->kind == PE_MODEL_CALLBACK && src->load) {
        s_cache[slot].external = src->load(src->user);   /* the provider owns it */
        if (!s_cache[slot].external) return NULL;
    } else if (src->kind == PE_MODEL_ROOM) {
        PeRoomBlobs room;
        if (!pe_disc_room(src->map, &room)) return NULL;
        i = pe_model_decode(&room, src->actor, &s_cache[slot].model);
        pe_disc_room_free(&room);
        if (!i) { pe_mod_log(NULL, "model: M%03d slot %d did not decode", src->map, src->actor); return NULL; }
    } else if (src->kind == PE_MODEL_SKIN) {
        if (!pe_model_skin_load(src, &s_cache[slot].model)) return NULL;
    } else {
        return NULL;
    }
    s_cache[slot].src = *src;
    if (src->folder) {
        strcpy(s_cache[slot].folder, src->folder);
        s_cache[slot].src.folder = s_cache[slot].folder;
    }
    s_cache[slot].used = ++s_tick;
    return s_cache[slot].external ? s_cache[slot].external : &s_cache[slot].model;
}

void pe_model_release_all(void) {
    int i;
    for (i = 0; i < PE_CACHE; ++i) { pe_model_free(&s_cache[i].model); memset(&s_cache[i], 0, sizeof s_cache[i]); }
}
