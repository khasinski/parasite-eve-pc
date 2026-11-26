/* Resident Evil files: <dir>/ITEM_M2/FILE*.TIM: each a file page or a file's picture, one record each.
 * Kept as stored in a Pictures record (pe_imp_pictures.h); which pages make up which document is
 * not known yet. Read only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_pictures.h"
#include "pe_imp_rdt.h"
#include "pe_src_re1.h"

static int read_files(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    const char *base = strrchr(path, '/');
    char stem[24] = "", id[40], name[48];
    base = base ? base + 1 : path;
    size_t k = 0;
    for (const char *c = base; *c && *c != '.' && k + 1 < sizeof stem; c++) stem[k++] = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
    stem[k] = 0;
    snprintf(id, sizeof id, "files/%s", stem);
    snprintf(name, sizeof name, "File picture %s", stem);
    return pe_imp_emit_tims(sink, id, name, data, size, 0, why, n);
}

const PeImpReader g_re1_files_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re1", .platform = "ps1", .format = "files",
    .version = 1, .produces = PE_IMP_PICTURES, .match = "*/ITEM_M2/FILE*.TIM",
    .tags = "files", .read = read_files
};

/* <dir>/ITEM_M2/MAP*.TIM: the maps, one 8-bit TIM each with its own palette: MAP01 to MAP0B the
 * floor maps (256 x 112), MAP00 and MAP0C to MAP0E the area overviews (256 x 160) the map screen
 * shows as more of the game opens up. One Pictures record each, "maps/<name>". */
static int read_maps(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    const char *base = strrchr(path, '/');
    char stem[24] = "", id[40], name[48];
    base = base ? base + 1 : path;
    size_t k = 0;
    for (const char *c = base; *c && *c != '.' && k + 1 < sizeof stem; c++) stem[k++] = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
    stem[k] = 0;
    snprintf(id, sizeof id, "maps/%s", stem);
    snprintf(name, sizeof name, "Map %s", stem + (k > 3 ? 3 : k));
    return pe_imp_emit_tims(sink, id, name, data, size, 0, why, n);
}

const PeImpReader g_re1_maps_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re1", .platform = "ps1", .format = "maps",
    .version = 1, .produces = PE_IMP_PICTURES, .match = "*/ITEM_M2/MAP*.TIM",
    .tags = "maps", .read = read_maps
};

/* <dir>/ITEM_M2/FILEM*.PIX: the file text pages, a 64-byte palette (16 colours used) then 4-bit
 * pixels 512 wide, cut into 512 x 192 pages (FILEM_00 is the Botany Book's first page) */
static int read_text(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    const char *base = strrchr(path, '/');
    char stem[24] = "", id[40], name[48];
    base = base ? base + 1 : path;
    size_t k = 0;
    for (const char *c = base; *c && *c != '.' && k + 1 < sizeof stem; c++) stem[k++] = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
    stem[k] = 0;
    snprintf(id, sizeof id, "files/%s", stem);
    snprintf(name, sizeof name, "File text %s", stem);
    return pe_imp_emit_pix4(sink, id, name, data, size, 64, 512, 192, why, n);
}

const PeImpReader g_re1_file_text_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re1", .platform = "ps1", .format = "file-text",
    .version = 1, .produces = PE_IMP_PICTURES, .match = "*/ITEM_M2/FILEM*.PIX",
    .tags = "files", .read = read_text
};

/* <dir>/DATA/CORE00.ETM: the common effects' sprite sheets (8 TIMs back to back; their EFF data is
 * CORE00.ESP, kept as stored, as biohazard-utils leaves EFF data undecoded): "effects/core". */
static int read_core_effects(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    (void)path;
    return pe_imp_emit_tims(sink, "effects/core", "Common effects", data, size, 4, why, n);
}

const PeImpReader g_re1_core_effects_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re1", .platform = "ps1", .format = "core-effects",
    .version = 1, .produces = PE_IMP_PICTURES, .match = "*/DATA/CORE00.ETM",
    .tags = "effects", .read = read_core_effects
};

/* A room's effect sprite sheets (biohazard-utils' Rdt1: ESPIDs, ESPEFF and ESPTIM, the offset table's
 * entries 13 to 15): 8 effect ids (0xFF unused), and per id in use a u32 offset of its TIM in the
 * room file (-1 none). The TIMs, in id order, become "effects/<stage><room>-<player>"; the EFF data stays as
 * stored in the room's RDT0. */
static uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int read_room_effects(const uint8_t *rdt, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    uint32_t stage, room, player;
    if (pe_imp_re_room_name(PE_IMP_RE1, path, &stage, &room, &player) || size < 0x48 + 19 * 4) return 0;
    uint32_t ids = rd32(rdt + 0x48 + 4 * 13), tims = rd32(rdt + 0x48 + 4 * 15);
    if (!ids || !tims || ids + 8 > size || tims >= size) return 0;
    uint8_t *packed = malloc(size);
    uint32_t total = 0;
    if (!packed) { if (why && n) snprintf(why, n, "out of memory"); return -1; }
    for (uint32_t k = 0, used = 0; k < 8; k++) {
        if (rdt[ids + k] == 0xFF) continue;
        if (tims + 4ull * (used + 1) > size) break;
        uint32_t at = rd32(rdt + tims + 4 * used++);               /* one entry per id in use */
        if (at == 0xFFFFFFFFu || at >= size) continue;
        uint32_t t = pe_imp_tim_size(rdt + at, size - at);
        if (!t || total + ((t + 3) & ~3u) > size) continue;
        memcpy(packed + total, rdt + at, t);
        memset(packed + total + t, 0, ((t + 3) & ~3u) - t);
        total += (t + 3) & ~3u;
    }
    int rc = 0;
    if (total) {
        char id[40], name[48];
        snprintf(id, sizeof id, "effects/%x%02x-%u", (unsigned)stage + 1, (unsigned)room, (unsigned)player);
        snprintf(name, sizeof name, "Room %u-%02X effects", (unsigned)stage + 1, (unsigned)room);
        rc = pe_imp_emit_tims(sink, id, name, packed, total, 4, why, n);
    }
    free(packed);
    return rc;
}

const PeImpReader g_re1_room_effects_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re1", .platform = "ps1", .format = "rdt-effects",
    .version = 1, .produces = PE_IMP_PICTURES, .match = "*/STAGE?/ROOM*.RDT",
    .tags = "effects", .read = read_room_effects, .path_key = 1
};
