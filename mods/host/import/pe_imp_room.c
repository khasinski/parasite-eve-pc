/* PS1 RDT camera and PRI records, and BSS mask atlases. Format references:
 * https://github.com/pmandin/reevengi-tools/wiki (RDT, ARD and BSS). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_room.h"

enum { ROOM_WIDTH = 320, ROOM_HEIGHT = 240, ROOM_HEADER = 40, ROOM_HEADER_COLLISION = 44,
       TIM_MAX = 256 * 256 * 2 + 2048 };
typedef struct { const uint8_t *data; uint32_t size; } Bytes;
typedef struct {
    const uint8_t *pixels, *palette;
    uint32_t width, height, mode, size;
} Tim;

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }
static int32_t signed32(uint32_t v) { return v <= INT32_MAX ? (int32_t)v : -1 - (int32_t)(UINT32_MAX - v); }
static int32_t signed16(uint32_t v) { return v <= INT16_MAX ? (int32_t)v : (int32_t)v - 65536; }

static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static Bytes tail(Bytes b, uint32_t off) {
    if (off >= b.size) return (Bytes){0};
    return (Bytes){b.data + off, b.size - off};
}

static int find_nonempty(PeImpDisc *disc, const char *path) {
    int i = pe_imp_disc_find(disc, path);
    return i >= 0 && pe_imp_disc_size(disc, i) ? i : -1;
}

int pe_imp_re_room_companion(PeImpDisc *disc, const char *path, unsigned game) {
    const char *slash = strrchr(path, '/'), *ext = strrchr(path, '.');
    const char *base = slash ? slash + 1 : path;
    char sibling[PE_IMP_PATH_MAX];
    if (!ext || ext <= base || (size_t)(ext - path) + 6 > sizeof sibling) return -1;
    if (game == PE_IMP_RE3) {
        snprintf(sibling, sizeof sibling, "%.*s.ARD", (int)(ext - path), path);
        return find_nonempty(disc, sibling);
    }
    /* RE1 shares BSS across Chris/Jill. RE2 retail usually has one player per disc. */
    for (int player = 0; player <= 1; player++) {
        if (game == PE_IMP_RE1)
            snprintf(sibling, sizeof sibling, "%.*s%d.RDT", (int)(ext - path), path, player);
        else
            snprintf(sibling, sizeof sibling, "PL%d/RDT/%.*s%d.RDT", player, (int)(ext - base), base, player);
        int index = find_nonempty(disc, sibling);
        if (index >= 0) return index;
    }
    return -1;
}

static Bytes ard_rdt(Bytes ard) {
    /* Entry 8 is the RDT. Entries start after the directory and have sector alignment. */
    if (ard.size < 2048 || le32(ard.data + 4) < 9 || le32(ard.data + 4) > 255) return (Bytes){0};
    uint32_t pos = 2048;
    for (unsigned i = 0; i <= 8; i++) {
        uint32_t size = le32(ard.data + 8 + i * 8);
        if (pos > ard.size || size > ard.size - pos) return (Bytes){0};
        if (i == 8) return (Bytes){ard.data + pos, size};
        pos += (size + 2047u) & ~2047u;
    }
    return (Bytes){0};
}

int pe_imp_re3_ard_rdt(const uint8_t *ard, uint32_t size, const uint8_t **rdt, uint32_t *rdt_size) {
    Bytes b = ard_rdt((Bytes){ard, size});
    *rdt = b.data, *rdt_size = b.size;
    return b.data && b.size ? 0 : -1;
}

static const uint8_t *camera(Bytes rdt, unsigned game, uint32_t slot) {
    int re1 = game == PE_IMP_RE1;
    if (rdt.size < (re1 ? 148u : 40u) || slot >= rdt.data[1]) return NULL;
    uint32_t offset = re1 ? 148 : le32(rdt.data + 36);
    uint32_t stride = re1 ? 44 : 32;
    if (offset < (re1 ? 148u : 40u) || offset > rdt.size || (slot + 1) * stride > rdt.size - offset) return NULL;
    return rdt.data + offset + slot * stride;
}

/* Walk the PRI groups and normalize their rectangles. Return the exact raw size so the importer
 * can keep the original packed records in the store. */
static uint32_t read_pri(Bytes pri, int re1, PeImpRoomView *view) {
    if (pri.size < 4) return 0;
    uint32_t groups = le16(pri.data), count = 0, pos = 4 + 8 * groups;
    if (!groups || groups > PE_IMP_ROOM_MASK_MAX || pos > pri.size) return 0;
    for (uint32_t g = 0; g < groups; g++) {
        const uint8_t *group = pri.data + 4 + g * 8;
        uint32_t tiles = le16(group);
        if (tiles > PE_IMP_ROOM_MASK_MAX - count) return 0;
        for (uint32_t t = 0; t < tiles; t++) {
            if (pri.size - pos < 8) return 0;
            const uint8_t *p = pri.data + pos;
            uint32_t square = re1 ? p[7] : le16(p + 6);
            uint32_t stride = square ? 8 : 12;
            if (pri.size - pos < stride) return 0;
            uint32_t w = square ? square : le16(p + 8), h = square ? square : le16(p + 10);
            if (!w || !h || w > ROOM_WIDTH || h > 256) return 0;
            if (view) view->masks[count] = (PeImpRoomMask){
                signed16(le16(group + 4)) + p[2], signed16(le16(group + 6)) + p[3],
                p[0], p[1], (uint16_t)w, (uint16_t)h, (uint16_t)le16(p + 4)
            };
            count++;
            pos += stride;
        }
    }
    if (!count || (!re1 && count != le16(pri.data + 2))) return 0;
    if (view) view->mask_count = count;
    return pos;
}

static int tim_read(Bytes bytes, unsigned game, Tim *tim) {
    if (bytes.size < 20 || le32(bytes.data) != 0x10) return -1;
    uint32_t flags = le32(bytes.data + 4), mode = flags & 7, pos = 8;
    if (flags & ~15u || mode > 2 || (mode < 2 && !(flags & 8))) return -1;
    const uint8_t *palette = NULL;
    if (flags & 8) {
        const uint8_t *clut = bytes.data + pos;
        uint32_t len = le32(clut), colors = mode == 0 ? 16 : 256;
        if (len < 12 + colors * 2 || len > bytes.size - pos || le16(clut + 8) < colors ||
            !le16(clut + 10)) return -1;
        palette = clut + 12;
        pos += len;
    }
    if (bytes.size - pos < 12) return -1;
    const uint8_t *image = bytes.data + pos;
    uint32_t len = le32(image), words = le16(image + 8), height = le16(image + 10);
    uint32_t width = words << (2 - mode);
    if (!words || width > 256 || !height || height > 256) return -1;
    uint32_t pixel_bytes = words * height * 2;
    /* BSS mask TIMs can declare just the pixels in the image block length. */
    if (game != PE_IMP_RE1 && len == pixel_bytes) len += 12;
    if (len < 12 + pixel_bytes || len > bytes.size - pos) return -1;
    *tim = (Tim){image + 12, palette, width, height, mode, pos + len};
    return 0;
}

/* The compressed atlas immediately follows the MDEC data. A trailer after the atlas holds
 * its offset and the count of additional files. A zero count is followed by slot padding. */
static Bytes bss_atlas(Bytes frame, uint32_t *frame_size) {
    uint32_t end = *frame_size;
    if (end < 20 || le32(frame.data + end - 4) != 1) return (Bytes){0};
    uint32_t offset = le32(frame.data + end - 8);
    if (offset < 12 || offset >= end - 8 || offset % 4) return (Bytes){0};
    *frame_size = offset;
    return (Bytes){frame.data + offset, end - 8 - offset};
}

typedef struct {
    const uint8_t *cam;
    Bytes pri, atlas;
    uint32_t slot, offset;
} RoomFrame;

int pe_imp_emit_re_room(PeImpSink *sink, unsigned game, const char *path,
                        const uint8_t *bss, uint32_t bss_size, const uint8_t *rdt_data, uint32_t rdt_size) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *prefix = game == PE_IMP_RE3 ? "R" : "ROOM";
    size_t prefix_size = strlen(prefix);
    if (strncmp(base, prefix, prefix_size) || strlen(base + prefix_size) != 7 ||
        strcmp(base + prefix_size + 3, ".BSS")) return 0;
    char digits[4] = {0};
    for (size_t i = 0; i < 3; i++) {
        char c = base[prefix_size + i];
        if ('0' <= c && c <= '9') digits[i] = c;
        else if ('A' <= c && c <= 'F') digits[i] = (char)(c - 'A' + 'a');
        else return 0;
    }
    int re1 = game == PE_IMP_RE1;
    Bytes rdt = {rdt_data, rdt_size};
    if (game == PE_IMP_RE3) rdt = ard_rdt(rdt);
    uint32_t slot_size = re1 ? 0x8000 : 0x10000, count = 0;
    uint32_t sizes[PE_IMP_IMAGESET_VIEWS], extras[PE_IMP_IMAGESET_VIEWS];
    RoomFrame frames[PE_IMP_IMAGESET_VIEWS] = {0};
    for (uint32_t slot = 0; slot < PE_IMP_IMAGESET_VIEWS && slot * slot_size + 12 <= bss_size; slot++) {
        uint32_t offset = slot * slot_size, len = bss_size - offset;
        Bytes frame = {bss + offset, len < slot_size ? len : slot_size};
        uint32_t version = le16(frame.data + 6);
        if (!le16(frame.data) || le16(frame.data + 2) != 0x3800 || (version != 2 && version != 3)) continue;
        len = frame.size;
        while (len > 12 && !frame.data[len - 1]) len--;
        len = (len + 3) & ~3u;
        if (len > frame.size) len = frame.size;
        Bytes atlas = re1 ? (Bytes){0} : bss_atlas(frame, &len);
        const uint8_t *cam = camera(rdt, game, slot);
        Bytes pri = {0};
        if (cam) {
            pri = tail(rdt, le32(cam + (re1 ? 0 : 28)));
            pri.size = read_pri(pri, re1, NULL);
            if (re1 && pri.size) {
                atlas = tail(rdt, le32(cam + 4));
                Tim tim;
                atlas.size = tim_read(atlas, game, &tim) ? 0 : tim.size;
            }
        }
        if (!pri.size) atlas.size = 0;
        frames[count] = (RoomFrame){cam, pri, atlas, slot, offset};
        sizes[count] = len;
        extras[count] = ROOM_HEADER + pri.size + atlas.size;
        count++;
    }
    if (!count) return 0;
    PeImpImageSet *set = pe_imp_imageset_alloc_extra(PE_IMP_IMAGESET_MDEC, ROOM_WIDTH, ROOM_HEIGHT,
                                                     count, sizes, extras);
    if (!set) return -1;
    uint8_t *out = set->payload;
    for (uint32_t i = 0; i < count; i++) {
        memcpy(out, bss + frames[i].offset, sizes[i]);
        out += sizes[i];
    }
    for (uint32_t i = 0; i < count; i++) {
        RoomFrame *frame = &frames[i];
        /* RMV1: slot, source game (zero if no camera), projection, eye/target, PRI byte count. */
        memcpy(out, "RMV1", 4);
        out[4] = (uint8_t)frame->slot;
        const uint8_t *cam = frame->cam;
        if (cam) {
            out[5] = (uint8_t)game;
            put32(out + 8, le32(cam + (re1 ? 40 : 0)));
            memcpy(out + 12, cam + (re1 ? 8 : 4), 24);
        }
        put32(out + 36, frame->pri.size);
        if (frame->pri.size) memcpy(out + ROOM_HEADER, frame->pri.data, frame->pri.size);
        if (frame->atlas.size) memcpy(out + ROOM_HEADER + frame->pri.size, frame->atlas.data, frame->atlas.size);
        out += extras[i];
    }
    char id[32], name[48];
    snprintf(id, sizeof id, "room/%s", digits);
    snprintf(name, sizeof name, "Room %c-%s", digits[0], digits + 1);
    return pe_imp_emit(sink, PE_IMP_IMAGESET, id, name, set) ? -1 : 0;
}

static int room_data(const PeImpImageSet *set, uint32_t frame, Bytes *data, Bytes *pri, Bytes *atlas) {
    if (frame >= set->count) return -1;
    data->data = pe_imp_imageset_extra(set, frame, &data->size);
    if (!data->data) return 1;
    if (data->size < ROOM_HEADER || memcmp(data->data, "RMV", 3) ||
        (data->data[3] != '1' && data->data[3] != '2') ||
        data->data[4] >= PE_IMP_IMAGESET_VIEWS || data->data[5] > PE_IMP_RE3) return -1;
    /* The collision preview stored an explicit atlas size and appended the SCA block. */
    int has_collision = data->data[3] == '2';
    uint32_t header = has_collision ? ROOM_HEADER_COLLISION : ROOM_HEADER;
    uint32_t len = le32(data->data + 36);
    if (header > data->size || len > data->size - header) return -1;
    uint32_t remaining = data->size - header - len;
    uint32_t atlas_size = has_collision ? le32(data->data + 40) : remaining;
    if (atlas_size > remaining) return -1;
    *pri = (Bytes){data->data + header, len};
    *atlas = (Bytes){pri->data + len, atlas_size};
    return 0;
}

int pe_imp_room_view(const PeImpImageSet *set, uint32_t frame, PeImpRoomView *view) {
    Bytes data, pri, atlas;
    int rc = room_data(set, frame, &data, &pri, &atlas);
    view->slot = frame;
    view->game = view->projection = view->mask_count = 0;
    memset(view->eye, 0, sizeof view->eye);
    memset(view->target, 0, sizeof view->target);
    if (rc) return rc < 0 ? -1 : 0;
    const uint8_t *p = data.data;
    view->slot = p[4];
    view->game = p[5];
    view->projection = le32(p + 8);
    for (unsigned i = 0; i < 3; i++) {
        view->eye[i] = signed32(le32(p + 12 + 4 * i));
        view->target[i] = signed32(le32(p + 24 + 4 * i));
    }
    if (pri.size && read_pri(pri, view->game == PE_IMP_RE1, view) != pri.size) return -1;
    return 0;
}

/* RE2 uses a length header and RE3 uses a count of literal/back-reference commands.
 * Matches can overlap and must be expanded byte by byte. */
static uint32_t unpack_atlas(Bytes src, unsigned game, uint8_t *out) {
    int re3 = game == PE_IMP_RE3;
    uint32_t pos = re3 ? 4 : 6, written = 0;
    if (src.size < pos) return 0;
    uint32_t header = le32(src.data);
    uint32_t limit = re3 ? TIM_MAX : header, commands = re3 ? header : src.size;
    if (limit > TIM_MAX || commands > src.size - (re3 ? 4u : 0u)) return 0;
    while (commands && (re3 || written < limit)) {
        commands--;
        if (pos >= src.size) return 0;
        uint32_t op = src.data[pos++], count, distance = 0;
        if (re3 && (op & 0x80)) {
            count = op & 0x7f;
        } else if (!re3 && (op & 0x10)) {
            if (op == 0xff) break;
            count = 32 - (op & 31);
            if (count == 16) {
                if (pos >= src.size) return 0;
                count += src.data[pos++];
            }
        } else {
            if (pos >= src.size) return 0;
            uint32_t next = src.data[pos++];
            if (re3) {
                count = (op >> 3) + 2;
                distance = ((op & 7) * 256 + next) + 4;
            } else {
                count = (op & 15) + 3;
                distance = 2048 - ((op & 0xe0) * 8 + next);
                if (count == 18) {
                    if (pos >= src.size) return 0;
                    count += src.data[pos++];
                }
            }
        }
        if (count > limit - written) return 0;
        if (distance) {
            if (distance > written) return 0;
            for (uint32_t i = 0; i < count; i++) out[written + i] = out[written + i - distance];
        } else {
            if (count > src.size - pos) return 0;
            memcpy(out + written, src.data + pos, count);
            pos += count;
        }
        written += count;
    }
    return written;
}

static uint32_t tim_pixel(const Tim *tim, uint32_t x, uint32_t y) {
    uint32_t i = y * tim->width + x;
    if (tim->mode == 0) return le16(tim->palette + 2 * ((tim->pixels[i / 2] >> ((i & 1) * 4)) & 15));
    if (tim->mode == 1) return le16(tim->palette + 2 * tim->pixels[i]);
    return le16(tim->pixels + 2 * i);
}

static uint8_t expand_5(uint32_t channel) {
    channel &= 31;
    return (uint8_t)((channel << 3) | (channel >> 2));
}

int pe_imp_room_foreground(const PeImpImageSet *set, uint32_t frame, const PeImpRoomView *view, uint8_t *rgba) {
    Bytes data, pri, atlas;
    int rc = room_data(set, frame, &data, &pri, &atlas);
    if (rc) return rc;
    if (!atlas.size || !view->mask_count) return 1;
    if (view->mask_count > PE_IMP_ROOM_MASK_MAX || set->width != ROOM_WIDTH || set->height != ROOM_HEIGHT ||
        view->game != data.data[5] || view->slot != data.data[4]) return -1;

    uint8_t *decoded = NULL;
    if (view->game != PE_IMP_RE1) {
        decoded = malloc(TIM_MAX);
        if (!decoded) return -1;
        atlas = (Bytes){decoded, unpack_atlas(atlas, view->game, decoded)};
    }
    Tim tim;
    if (tim_read(atlas, view->game, &tim)) {
        free(decoded);
        return -1;
    }
    uint16_t *depth = malloc(ROOM_WIDTH * ROOM_HEIGHT * sizeof *depth);
    if (!depth) {
        free(decoded);
        return -1;
    }
    memset(depth, 0xff, ROOM_WIDTH * ROOM_HEIGHT * sizeof *depth);
    memset(rgba, 0, ROOM_WIDTH * ROOM_HEIGHT * 4);
    for (uint32_t i = 0; i < view->mask_count; i++) {
        const PeImpRoomMask *m = &view->masks[i];
        for (uint32_t row = 0; row < m->height && m->v + row < tim.height; row++) {
            int64_t y = (int64_t)m->y + row;
            if (y < 0 || y >= ROOM_HEIGHT) continue;
            for (uint32_t col = 0; col < m->width && m->u + col < tim.width; col++) {
                int64_t x = (int64_t)m->x + col;
                if (x < 0 || x >= ROOM_WIDTH) continue;
                uint32_t color = tim_pixel(&tim, m->u + col, m->v + row);
                uint32_t pixel = (uint32_t)(y * ROOM_WIDTH + x);
                if (!color || m->depth > depth[pixel]) continue;
                depth[pixel] = m->depth;
                uint8_t *dst = rgba + pixel * 4;
                dst[0] = expand_5(color);
                dst[1] = expand_5(color >> 5);
                dst[2] = expand_5(color >> 10);
                dst[3] = 255;
            }
        }
    }
    free(depth);
    free(decoded);
    return 0;
}
