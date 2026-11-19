/* Resident Evil 2 files: COMMON/DATA/FILES.TIM: the file pages and pictures (216 TIMs), kept as
 * stored in a Pictures record (pe_imp_pictures.h). Each of the 25 documents is an 8-bit background
 * then its 4-bit text pages, in the game's file order: document k is item 0x68 + k (Chris's diary to
 * the second hint file, as in biohazard-utils' item list), so DOCS names each by its item. Read only. */
#include <stdio.h>
#include <string.h>

#include "pe_imp_pictures.h"
#include "pe_imp_rdt.h"
#include "pe_src_re2.h"

static int read_files(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    (void)path;
    return pe_imp_emit_documents(sink, "files/all", "Files", data, size, 2048, 0x68, why, n);
}

const PeImpReader g_re2_files_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "files",
    .version = 3, .produces = PE_IMP_PICTURES, .match = "COMMON/DATA/FILES.TIM",
    .tags = "files", .read = read_files
};

/* COMMON/DATA/MAPS.PIX: the 20 map pages, 256 x 256 at 4 bits a pixel, without a palette. Their
 * colours are in COMMON/DATA/TEX.TIM's palette (19 rows of 32 = 38 palettes of 16, the map screen's
 * room colours: blue, red, green and highlighted versions). The pages get palette 16 (rooms blue on
 * black, found by eye); all 38 are kept as PALS. Without TEX.TIM: a grey placeholder (GREY). */
static int find_tex(PeImpDisc *d, const char *path) {
    char sibling[PE_IMP_PATH_MAX];
    const char *slash = strrchr(path, '/');
    if (!slash || snprintf(sibling, sizeof sibling, "%.*s/TEX.TIM", (int)(slash - path), path) >= (int)sizeof sibling)
        return -1;
    return pe_imp_disc_find(d, sibling);
}

static int read_maps(const uint8_t *data, uint32_t size, const uint8_t *tex, uint32_t tex_size, const char *path,
                     PeImpSink *sink, char *why, size_t n) {
    (void)path;
    enum { MAP_PALETTE = 16 };
    uint32_t cw = tex && tex_size >= 20 ? (uint32_t)(tex[16] | tex[17] << 8) : 0, ch = tex && tex_size >= 20 ? (uint32_t)(tex[18] | tex[19] << 8) : 0;
    if (!tex || tex[0] != 0x10 || tex[4] != 8 || cw != 32 || ch * 2 <= MAP_PALETTE || tex_size < 20 + 2 * cw * ch)
        return pe_imp_emit_pages4(sink, "maps/all", "Maps", data, size, why, n);     /* not the palette we know */
    const uint8_t *pals = tex + 20;
    return pe_imp_emit_pages4_palette(sink, "maps/all", "Maps", data, size, pals + 32 * MAP_PALETTE, pals, ch * 2, why, n);
}

const PeImpReader g_re2_maps_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "maps",
    .version = 2, .produces = PE_IMP_PICTURES, .match = "COMMON/DATA/MAPS.PIX",
    .tags = "maps", .read_pair = read_maps, .find_companion = find_tex
};

/* Effects (biohazard-utils' EmbeddedEffect, MIT, Ted John: an effect is an id, its EFF data and its
 * sprite TIM). A room's effect sprite sheets are RDT section 20 (TIMs back to back), its effect ids
 * section 18 (8 bytes, 0xFF unused): picture k is the room's effect k. "effects/<stage><room>-<player>".
 * The EFF data (section 19) is kept as stored in the room's RDT0 (biohazard-utils doesn't decode it
 * either). */
static int read_effects(const uint8_t *rdt, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    uint32_t stage, room, player, at, len;
    if (pe_imp_re_room_name(PE_IMP_RE2, path, &stage, &room, &player) || pe_imp_rdt_section(rdt, size, 20, &at, &len) || len < 20)
        return 0;
    char id[40], name[48];
    snprintf(id, sizeof id, "effects/%x%02x-%u", (unsigned)stage + 1, (unsigned)room, (unsigned)player);
    snprintf(name, sizeof name, "Room %u-%02X effects", (unsigned)stage + 1, (unsigned)room);
    return pe_imp_emit_tims(sink, id, name, rdt + at, len, 4, why, n);
}

const PeImpReader g_re2_effects_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "rdt-effects",
    .version = 1, .produces = PE_IMP_PICTURES, .match = "PL?/RDT/ROOM*.RDT",
    .tags = "effects", .read = read_effects, .path_key = 1
};
