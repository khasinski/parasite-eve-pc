/* PS1 disc images: a .cue (its first data track), a raw .bin (2352-byte sectors, mode 1 or mode 2
 * form 1) or a cooked .iso (2048). Files come from the ISO 9660 file system, any depth, with
 * limits on depth, count and directory size so a broken or hostile image can't loop or exhaust
 * memory. stdio only (PC and PS5). */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_rofs.h"

#define RAW 2352u
#define COOKED 2048u
#define MAX_DEPTH 32
#define MAX_FILES 65536
#define MAX_DIRS 8192
#define MAX_DIR_BYTES (1u << 20)
#define NAME_MAX_LEN 128

static const unsigned char k_sync[12] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};

typedef struct { char name[NAME_MAX_LEN]; uint32_t lba, size; int arch; } File;

#define ROFS_MAX 32                                   /* rofs1.dat .. rofs32.dat */
struct PeImpDisc {
    FILE *f;
    uint32_t sector, count;
    File *file;
    int files, cap;
    char label[256];
    /* a PC game folder (pe_imp_rofs.c): archives instead of sectors; lba = the entry's offset */
    int rofs;
    char *arch[ROFS_MAX];
};

static int fail(char *why, size_t n, const char *fmt, const char *a) {
    if (why && n) snprintf(why, n, fmt, a ? a : "");
    return -1;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static const char *base_name(const char *p) {
    const char *s = strrchr(p, '/'), *b = strrchr(p, '\\');
    if (b && (!s || b > s)) s = b;
    return s ? s + 1 : p;
}

static int ends_with(const char *s, const char *tail) {
    size_t a = strlen(s), b = strlen(tail);
    if (a < b) return 0;
    for (size_t i = 0; i < b; i++)
        if (tolower((unsigned char)s[a - b + i]) != tolower((unsigned char)tail[i])) return 0;
    return 1;
}

/* The .bin of the cue's first data track, beside the cue. */
static int cue_bin(const char *cue, char *out, size_t size, char *why, size_t n) {
    FILE *f = fopen(cue, "rb");
    if (!f) return fail(why, n, "can't open %s", base_name(cue));
    char text[65536];
    size_t len = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[len] = 0;
    char file[512] = "";
    for (char *line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        while (*line == ' ' || *line == '\t') line++;
        if (!strncmp(line, "FILE ", 5) || !strncmp(line, "file ", 5)) {
            const char *s = line + 5;
            while (*s == ' ') s++;
            size_t k = 0;
            if (*s == '"') {
                s++;
                while (*s && *s != '"' && k + 1 < sizeof file) file[k++] = *s++;
            } else {
                while (*s && *s != ' ' && k + 1 < sizeof file) file[k++] = *s++;
            }
            file[k] = 0;
        } else if ((!strncmp(line, "TRACK ", 6) || !strncmp(line, "track ", 6)) && file[0]) {
            const char *mode = strchr(line + 6, ' ');
            while (mode && *mode == ' ') mode++;
            if (mode && (!strncmp(mode, "MODE", 4) || !strncmp(mode, "mode", 4))) {
                if (strstr(file, "..") || strchr(file, ':') || file[0] == '/' || file[0] == '\\')
                    return fail(why, n, "%s names a file outside its folder", base_name(cue));
                size_t dir = (size_t)(base_name(cue) - cue);
                if (dir + strlen(file) + 1 > size) return fail(why, n, "path too long", NULL);
                memcpy(out, cue, dir);
                strcpy(out + dir, file);
                return 0;
            }
        }
    }
    return fail(why, n, "%s has no data track", base_name(cue));
}

static int sectors(PeImpDisc *d, uint32_t lba, uint32_t count, uint8_t *out, char *why, size_t n) {
    if (lba > d->count || count > d->count - lba) return fail(why, n, "%s", "sectors outside the image");
    uint8_t s[RAW];
    for (uint32_t i = 0; i < count; i++) {
        if (fseek(d->f, (long)((lba + i) * (long long)d->sector), SEEK_SET) ||
            fread(s, 1, d->sector, d->f) != d->sector)
            return fail(why, n, "%s", "read error in the image");
        if (d->sector == COOKED) {
            memcpy(out + (size_t)i * COOKED, s, COOKED);
            continue;
        }
        if (memcmp(s, k_sync, 12)) return fail(why, n, "%s", "a sector has no sync pattern");
        memcpy(out + (size_t)i * COOKED, s + (s[15] == 2 ? 24 : 16), COOKED);
    }
    return 0;
}

void pe_imp_disc_close(PeImpDisc *d);
static int add_file(PeImpDisc *d, const char *name, uint32_t lba, uint32_t size) {
    if (d->files >= MAX_FILES) return -1;
    if (d->files == d->cap) {
        int cap = d->cap ? d->cap * 2 : 256;
        File *f = realloc(d->file, (size_t)cap * sizeof *f);
        if (!f) return -1;
        d->file = f, d->cap = cap;
    }
    File *f = &d->file[d->files++];
    snprintf(f->name, sizeof f->name, "%s", name);
    f->lba = lba, f->size = size;
    return 0;
}

typedef struct { char path[NAME_MAX_LEN]; uint32_t lba, size; int depth; } Dir;

static int walk(PeImpDisc *d, char *why, size_t n) {
    uint8_t pvd[COOKED];
    if (sectors(d, 16, 1, pvd, why, n)) return -1;
    if (memcmp(pvd + 1, "CD001", 5)) return fail(why, n, "%s has no ISO 9660 file system", d->label);
    Dir *todo = malloc(MAX_DIRS * sizeof *todo);
    uint32_t *seen = malloc(MAX_DIRS * sizeof *seen);
    uint8_t *buf = malloc(MAX_DIR_BYTES);
    int ntodo = 0, nseen = 0, rc = 0;
    if (!todo || !seen || !buf) { rc = fail(why, n, "%s", "out of memory"); goto done; }
    todo[ntodo++] = (Dir){"", le32(pvd + 156 + 2), le32(pvd + 156 + 10), 0};
    while (ntodo && !rc) {
        Dir dir = todo[--ntodo];
        int dup = 0;
        for (int i = 0; i < nseen; i++) dup |= seen[i] == dir.lba;
        if (dup || dir.depth > MAX_DEPTH) continue;
        if (nseen >= MAX_DIRS) { rc = fail(why, n, "%s has too many folders", d->label); break; }
        seen[nseen++] = dir.lba;
        if (dir.size > MAX_DIR_BYTES) { rc = fail(why, n, "%s has an oversized folder", d->label); break; }
        uint32_t secs = (dir.size + COOKED - 1) / COOKED;
        if (sectors(d, dir.lba, secs, buf, why, n)) { rc = -1; break; }
        uint32_t at = 0, end = secs * COOKED;
        while (at < end) {
            uint32_t len = buf[at];
            if (!len) { at = (at / COOKED + 1) * COOKED; continue; }
            if (len < 34 || at + len > end || 33u + buf[at + 32] > len) {
                rc = fail(why, n, "%s has a broken folder record", d->label);
                break;
            }
            const uint8_t *rec = buf + at;
            at += len;
            uint32_t elba = le32(rec + 2), esize = le32(rec + 10);
            unsigned flags = rec[25], nl = rec[32];
            if (nl == 1 && (rec[33] == 0 || rec[33] == 1)) continue;          /* . and .. */
            char name[NAME_MAX_LEN], full[NAME_MAX_LEN];
            unsigned k = 0;
            for (unsigned i = 0; i < nl && k + 1 < sizeof name; i++) {
                unsigned char c = rec[33 + i];
                if (c == ';') break;
                name[k++] = (char)(c > 32 && c < 127 && c != '/' && c != '\\' ? toupper(c) : '_');
            }
            name[k] = 0;
            if (!k || snprintf(full, sizeof full, "%s%s%s", dir.path, dir.path[0] ? "/" : "", name) >= (int)sizeof full)
                continue;
            if (flags & 2) {
                if (ntodo >= MAX_DIRS) { rc = fail(why, n, "%s has too many folders", d->label); break; }
                todo[ntodo] = (Dir){"", elba, esize, dir.depth + 1};
                snprintf(todo[ntodo].path, sizeof todo[ntodo].path, "%s", full);
                ntodo++;
            } else if (add_file(d, full, elba, esize)) {
                rc = fail(why, n, "%s has too many files", d->label);
                break;
            }
        }
    }
done:
    free(todo), free(seen), free(buf);
    return rc;
}

static PeImpDisc *pe_imp_rofs_open(const char *path, char *why, size_t n);

PeImpDisc *pe_imp_disc_open(const char *path, char *why, size_t n) {
    char bin[PE_IMP_PATH_MAX];
    if (pe_imp_rofs_path(path)) return pe_imp_rofs_open(path, why, n);
    if (ends_with(path, ".cue")) {
        if (cue_bin(path, bin, sizeof bin, why, n)) return NULL;
    } else if (snprintf(bin, sizeof bin, "%s", path) >= (int)sizeof bin) {
        fail(why, n, "%s", "path too long");
        return NULL;
    }
    PeImpDisc *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    snprintf(d->label, sizeof d->label, "%s", base_name(path));
    d->f = fopen(bin, "rb");
    if (!d->f) {
        fail(why, n, "can't open %s", base_name(bin));
        free(d);
        return NULL;
    }
    unsigned char head[12] = {0};
    size_t got = fread(head, 1, 12, d->f);
    d->sector = got == 12 && !memcmp(head, k_sync, 12) ? RAW : COOKED;
    if (fseek(d->f, 0, SEEK_END)) goto bad;
    long long size = ftell(d->f);
    if (size <= 0 || size % d->sector || size / d->sector > 0x7fffffffLL / RAW) {
        fail(why, n, "%s is not whole sectors", d->label);
        goto bad;
    }
    d->count = (uint32_t)(size / d->sector);
    if (walk(d, why, n)) goto bad;
    return d;
bad:
    pe_imp_disc_close(d);
    return NULL;
}

/* A PC game's folder of rofs<N>.dat archives (RE3 PC 1999), named by any one of them: each archive
 * is one folder ("DATA/SOUND"), its files listed as "DATA/SOUND/E_34.VH" like a disc's. */
static PeImpDisc *pe_imp_rofs_open(const char *path, char *why, size_t n) {
    PeImpDisc *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    size_t dir = (size_t)(base_name(path) - path);
    char folder[256] = "PC";
    if (dir > 1) {                                   /* the folder's own name labels the source */
        char tmp[PE_IMP_PATH_MAX];
        snprintf(tmp, sizeof tmp, "%.*s", (int)(dir - 1), path);
        snprintf(folder, sizeof folder, "%s", base_name(tmp));
    }
    snprintf(d->label, sizeof d->label, "%s (PC)", folder);
    d->rofs = 1;
    for (int a = 0; a < ROFS_MAX; a++) {
        char arch[PE_IMP_PATH_MAX], names[2][32];
        PeImpRofsEntry *e = NULL;
        int count = 0;
        if (dir + 16 >= sizeof arch) break;
        snprintf(arch, sizeof arch, "%.*srofs%d.dat", (int)dir, path, a + 1);
        if (pe_imp_rofs_list(arch, names, &e, &count, NULL, 0)) continue;   /* gaps are fine */
        if (!(d->arch[a] = malloc(strlen(arch) + 1))) { free(e); goto bad; }
        strcpy(d->arch[a], arch);
        for (int i = 0; i < count; i++) {
            char full[NAME_MAX_LEN];
            snprintf(full, sizeof full, "%s/%s/%s", names[0], names[1], e[i].name);
            for (char *c = full; *c; c++) *c = (char)toupper((unsigned char)*c);
            if (add_file(d, full, e[i].offset, e[i].size)) { free(e); fail(why, n, "%s has too many files", d->label); goto bad; }
            d->file[d->files - 1].arch = a;
        }
        free(e);
    }
    if (!d->files) { fail(why, n, "%s: no readable rofs archives", d->label); goto bad; }
    return d;
bad:
    pe_imp_disc_close(d);
    return NULL;
}

void pe_imp_disc_close(PeImpDisc *d) {
    if (!d) return;
    if (d->f) fclose(d->f);
    for (int a = 0; a < ROFS_MAX; a++) free(d->arch[a]);
    free(d->file);
    free(d);
}

int pe_imp_disc_files(const PeImpDisc *d) { return d->files; }
const char *pe_imp_disc_name(const PeImpDisc *d, int i) { return i >= 0 && i < d->files ? d->file[i].name : ""; }
uint32_t pe_imp_disc_size(const PeImpDisc *d, int i) { return i >= 0 && i < d->files ? d->file[i].size : 0; }
const char *pe_imp_disc_label(const PeImpDisc *d) { return d->label; }
uint32_t pe_imp_disc_lba(const PeImpDisc *d, int i) { return i >= 0 && i < d->files ? d->file[i].lba : 0; }
uint32_t pe_imp_disc_count(const PeImpDisc *d) { return d->count; }
int pe_imp_disc_sectors(PeImpDisc *d, uint32_t lba, uint32_t count, uint8_t *out, char *why, size_t n) {
    if (d->rofs) return fail(why, n, "%s", "a PC game folder has no sectors");
    return sectors(d, lba, count, out, why, n);
}

int pe_imp_disc_find(const PeImpDisc *d, const char *path) {
    for (int i = 0; i < d->files; i++) {
        const char *a = d->file[i].name, *b = path;
        while (*a && *b && toupper((unsigned char)*a) == toupper((unsigned char)(*b == '\\' ? '/' : *b))) a++, b++;
        if (!*a && !*b) return i;
    }
    return -1;
}

int pe_imp_disc_read(PeImpDisc *d, int i, uint8_t **data, uint32_t *size, char *why, size_t n) {
    *data = NULL, *size = 0;
    if (i < 0 || i >= d->files) return fail(why, n, "%s", "no such file");
    File *f = &d->file[i];
    if (f->size > PE_IMP_FILE_MAX) return fail(why, n, "%s is too large", f->name);
    if (d->rofs) return pe_imp_rofs_read(d->arch[f->arch], f->lba, f->size, data, size, why, n);
    uint32_t secs = (f->size + COOKED - 1) / COOKED;
    uint8_t *buf = malloc((size_t)secs * COOKED + 1);
    if (!buf) return fail(why, n, "%s", "out of memory");
    if (sectors(d, f->lba, secs, buf, why, n)) { free(buf); return -1; }
    *data = buf, *size = f->size;
    return 0;
}

int pe_imp_disc_xa(PeImpDisc *d, int i, uint8_t **data, uint32_t *size, char *why, size_t n) {
    *data = NULL, *size = 0;
    if (i < 0 || i >= d->files) return fail(why, n, "%s", "no such file");
    if (d->rofs) return fail(why, n, "%s", "a PC game folder has no streamed CD audio");
    if (d->sector != RAW) return fail(why, n, "%s", "streamed audio needs a raw .bin image, not a .iso");
    File *f = &d->file[i];
    uint32_t secs = (f->size + COOKED - 1) / COOKED;
    if (f->lba > d->count || secs > d->count - f->lba) return fail(why, n, "%s", "sectors outside the image");
    if ((uint64_t)secs * PE_IMP_XA_RECORD > PE_IMP_FILE_MAX) return fail(why, n, "%s is too large", f->name);
    uint8_t *buf = malloc((size_t)(secs ? secs : 1) * PE_IMP_XA_RECORD), s[RAW];
    if (!buf) return fail(why, n, "%s", "out of memory");
    uint32_t used = 0;
    for (uint32_t k = 0; k < secs; k++) {
        if (fseek(d->f, (long)((f->lba + k) * (long long)RAW), SEEK_SET) || fread(s, 1, RAW, d->f) != RAW) {
            free(buf);
            return fail(why, n, "%s", "read error in the image");
        }
        if (memcmp(s, k_sync, 12)) { free(buf); return fail(why, n, "%s", "a sector has no sync pattern"); }
        /* mode 2, form 2 (submode bit 5), audio (bit 2) */
        if (s[15] != 2 || (s[18] & 0x24) != 0x24) continue;
        memcpy(buf + used, s + 16, 4);
        memcpy(buf + used + 4, s + 24, 2304);
        used += PE_IMP_XA_RECORD;
    }
    *data = buf, *size = used;
    return 0;
}
