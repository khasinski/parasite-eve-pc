/* The player's Parasite Eve disc images, read as the bridge's render_models.py does: the image and
 * room sector tables come from the disc's own executable (RAM 0x800930D8 and 0x80093378, as a
 * save state would hold them), PE.IMG's first sector from the file system. Rooms on disc 2 are
 * read with disc 1's tables and base sector, exactly as the bridge does, so the rows found (and
 * so rooms.bin) are the same. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_pe.h"

#define RAM_SIZE 0x200000u
#define IMAGE_TABLE 0x930D8u
#define ROOM_TABLE 0x93378u
#define BLOB_MAX (64u << 20)

/* A PE source: these disc images (below), or the game's own open discs (the import mod). */
struct PeImpPe {
    PeImpPeOps ops;
    void *user;
    PeImpRoomRow *kept;           /* the room rows, once scanned (slow: every room, both discs) */
    int nkept, have_kept;
};

typedef struct {
    PeImpDisc *disc[2];
    uint8_t *ram;                 /* 2 MB, as build_ram: zeros, the executable, PE.IMG's sector */
    uint32_t base;
} Images;

void pe_imp_buf_free(PeImpBuf *b) {
    if (b) free(b->b), b->b = NULL, b->n = 0;
}

static uint32_t ram32(const Images *pe, uint32_t at) {
    if (at + 4 > RAM_SIZE) return 0;
    return pe->ram[at] | pe->ram[at + 1] << 8 | pe->ram[at + 2] << 16 | (uint32_t)pe->ram[at + 3] << 24;
}
static uint32_t ram16(const Images *pe, uint32_t at) { return at + 2 > RAM_SIZE ? 0 : pe->ram[at] | pe->ram[at + 1] << 8; }

static int build_ram(Images *pe, char *why, size_t n) {
    PeImpDisc *d = pe->disc[0];
    int cnf = pe_imp_disc_find(d, "SYSTEM.CNF");
    uint8_t *text = NULL, *exe = NULL;
    uint32_t size, esize;
    if (cnf < 0 || pe_imp_disc_read(d, cnf, &text, &size, why, n)) {
        if (why && n && cnf < 0) snprintf(why, n, "the Parasite Eve disc has no SYSTEM.CNF");
        return -1;
    }
    char name[64] = "";
    const char *p = strstr((const char *)text, "cdrom:");
    if (p) {
        p += 6;
        while (*p == '\\' || *p == '/') p++;
        size_t k = 0;
        while (p < (const char *)text + size && *p && *p != ';' && *p != '\r' && *p != '\n' && *p != ' ' && k + 1 < sizeof name)
            name[k++] = (char)(*p == '\\' ? '/' : *p), p++;
        name[k] = 0;
    }
    free(text);
    int e = name[0] ? pe_imp_disc_find(d, name) : -1;
    if (e < 0 || pe_imp_disc_read(d, e, &exe, &esize, why, n)) {
        if (why && n && e < 0) snprintf(why, n, "the Parasite Eve disc's executable isn't there");
        return -1;
    }
    int rc = -1;
    if (esize >= 0x800 + 32) {
        uint32_t base = exe[24] | exe[25] << 8 | exe[26] << 16 | (uint32_t)exe[27] << 24;
        uint32_t len = exe[28] | exe[29] << 8 | exe[30] << 16 | (uint32_t)exe[31] << 24;
        uint32_t o = base & 0x1FFFFF;
        if (len <= esize - 0x800 && o + len <= RAM_SIZE && (pe->ram = calloc(1, RAM_SIZE))) {
            memcpy(pe->ram + o, exe + 0x800, len);
            rc = 0;
        }
    }
    free(exe);
    if (rc) { if (why && n) snprintf(why, n, "the Parasite Eve executable is not what it should be"); return -1; }
    int img = -1;
    for (int i = 0; i < pe_imp_disc_files(d) && img < 0; i++) {
        const char *f = pe_imp_disc_name(d, i);
        size_t l = strlen(f);
        if (l >= 6 && !strcmp(f + l - 6, "PE.IMG")) img = i;
    }
    if (img < 0) { if (why && n) snprintf(why, n, "PE.IMG not found: is this the Parasite Eve disc?"); return -1; }
    pe->base = pe_imp_disc_lba(d, img);
    pe->ram[0xB0DD8] = (uint8_t)pe->base, pe->ram[0xB0DD9] = (uint8_t)(pe->base >> 8);
    pe->ram[0xB0DDA] = (uint8_t)(pe->base >> 16), pe->ram[0xB0DDB] = (uint8_t)(pe->base >> 24);
    return 0;
}

PeImpPe *pe_imp_pe_from(const PeImpPeOps *ops, void *user) {
    PeImpPe *pe = calloc(1, sizeof *pe);
    if (!pe) { if (ops->close) ops->close(user); return NULL; }
    pe->ops = *ops, pe->user = user;
    return pe;
}

void pe_imp_pe_close(PeImpPe *pe) {
    if (!pe) return;
    if (pe->ops.close) pe->ops.close(pe->user);
    if (pe->have_kept) pe_imp_room_rows_free(pe->kept, pe->nkept);
    free(pe);
}

int pe_imp_pe_kept_rows(PeImpPe *pe, PeImpRoomRow **rows, int *count) {
    if (!pe->have_kept) return 0;
    PeImpRoomRow *r = calloc((size_t)(pe->nkept ? pe->nkept : 1), sizeof *r);
    if (!r) return 0;
    for (int i = 0; i < pe->nkept; i++) {
        r[i].fp = pe->kept[i].fp, r[i].data.n = pe->kept[i].data.n;
        if (!(r[i].data.b = malloc(r[i].data.n ? r[i].data.n : 1))) { pe_imp_room_rows_free(r, i); return 0; }
        memcpy(r[i].data.b, pe->kept[i].data.b, r[i].data.n);
    }
    *rows = r, *count = pe->nkept;
    return 1;
}

void pe_imp_pe_keep_rows(PeImpPe *pe, PeImpRoomRow *rows, int count) {
    if (pe->have_kept) pe_imp_room_rows_free(pe->kept, pe->nkept);
    pe->kept = rows, pe->nkept = count, pe->have_kept = 1;
}

void pe_imp_pe_set_rooms(PeImpPe *pe, int rooms) { pe->ops.rooms = rooms; }
int pe_imp_pe_rooms(const PeImpPe *pe) { return pe->ops.rooms; }
int pe_imp_pe_image(PeImpPe *pe, int idx, PeImpBuf *out) { return pe->ops.image(pe->user, idx, out); }
int pe_imp_pe_room(PeImpPe *pe, int map, int disc, PeImpBuf *out) { return pe->ops.room(pe->user, map, disc, out); }

static void images_close(void *user) {
    Images *im = user;
    if (!im) return;
    pe_imp_disc_close(im->disc[0]);
    pe_imp_disc_close(im->disc[1]);
    free(im->ram);
    free(im);
}

static int read_run(PeImpDisc *d, uint64_t lba, uint64_t count, PeImpBuf *out) {
    out->b = NULL, out->n = 0;
    if (count * 2048 > BLOB_MAX || lba > 0xFFFFFFFFu) return -1;
    if (!count) { out->b = malloc(1); return out->b ? 0 : -1; }
    out->b = malloc((size_t)count * 2048);
    if (!out->b || pe_imp_disc_sectors(d, (uint32_t)lba, (uint32_t)count, out->b, NULL, 0)) {
        pe_imp_buf_free(out);
        return -1;
    }
    out->n = (uint32_t)(count * 2048);
    return 0;
}

static int images_image(void *user, int idx, PeImpBuf *out) {
    Images *pe = user;
    uint32_t s0 = ram16(pe, IMAGE_TABLE + 2u * (uint32_t)idx), s1 = ram16(pe, IMAGE_TABLE + 2u * (uint32_t)idx + 2);
    if (idx < 0 || s1 < s0) { out->b = NULL, out->n = 0; return -1; }
    return read_run(pe->disc[0], (uint64_t)pe->base + s0, s1 - s0, out);
}

static int images_room(void *user, int map, int disc, PeImpBuf *out) {
    Images *pe = user;
    out->b = NULL, out->n = 0;
    if (map < 1 || disc < 1 || disc > 2 || !pe->disc[disc - 1]) return -1;
    uint32_t start = ram32(pe, ROOM_TABLE + (uint32_t)(map - 1) * 8), packed = ram32(pe, ROOM_TABLE + (uint32_t)(map - 1) * 8 + 4);
    uint64_t lba = (uint64_t)pe->base + start + (packed & 255) + ((packed >> 8) & 4095);
    return read_run(pe->disc[disc - 1], lba, packed >> 20, out);
}

/* Rooms 1-999, as the bridge scans them (past the room table it reads whatever follows it in
 * the executable, which the game's own table can't do: see pe_imp_pe_set_rooms). */
PeImpPe *pe_imp_pe_open(const char *disc1, const char *disc2, char *why, size_t n) {
    Images *im = calloc(1, sizeof *im);
    if (!im) return NULL;
    if (!(im->disc[0] = pe_imp_disc_open(disc1, why, n)) || build_ram(im, why, n) ||
        (disc2 && !(im->disc[1] = pe_imp_disc_open(disc2, why, n)))) {
        images_close(im);
        return NULL;
    }
    PeImpPeOps ops = {images_image, images_room, images_close, 999};
    return pe_imp_pe_from(&ops, im);
}
