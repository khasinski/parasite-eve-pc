/* Resident Evil 3 files: CD_DATA/ETC/FILEGU.PIX: the US file pages and pictures, a TIM at each 2 KB boundary (183).
 * Kept as stored in a Pictures record (pe_imp_pictures.h). Each of the 31 documents is an 8-bit
 * background then its 4-bit text pages; DOCS lists them (RE3's files aren't items, and their titles
 * are only on their pages). Read only. */
#include <stdio.h>

#include "pe_imp_pictures.h"
#include "pe_imp_rdt.h"
#include "pe_src_re3.h"

static int read_files(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    (void)path;
    return pe_imp_emit_documents(sink, "files/all", "Files", data, size, 2048, 0, why, n);
}

const PeImpReader g_re3_files_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "ps1", .format = "files",
    .version = 3, .produces = PE_IMP_PICTURES, .match = "CD_DATA/ETC/FILEGU.PIX",
    .tags = "files", .read = read_files
};

/* CD_DATA/ETC/MAP_U.MAP: the US maps, a section table, the room and label tables, and the map
 * pictures (18 TIMs: 9 maps and their legends). The pictures, and the whole file for the tables. */
static int read_maps(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    (void)path;
    return pe_imp_emit_tims_ex(sink, "maps/all", "Maps", data, size, 4, 1, why, n);
}

const PeImpReader g_re3_maps_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "ps1", .format = "maps",
    .version = 1, .produces = PE_IMP_PICTURES, .match = "CD_DATA/ETC/MAP_U.MAP",
    .tags = "maps", .read = read_maps
};

/* Effects (biohazard-utils' EmbeddedEffect: an id, its EFF data, its sprite TIM). A room's effect
 * sprite sheets are its RDT's section 20 (TIMs back to back), its effect ids section 17 (16 bytes,
 * 0xFF unused): picture k is the room's effect k, "effects/<stage><room>". The EFF data (section 18)
 * stays as stored in the room's RDT0. CD_DATA/ETC/CORE00.TIM is the common effects' sheet (their
 * EFF data is CORE00.ESP, kept as stored): "effects/core". */
static int read_effects(const uint8_t *ard, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    uint32_t stage, room, player, at, len;
    const uint8_t *rdt;
    uint32_t rdt_size;
    if (pe_imp_re_room_name(PE_IMP_RE3, path, &stage, &room, &player) || pe_imp_re3_ard_rdt(ard, size, &rdt, &rdt_size) ||
        pe_imp_rdt_section(rdt, rdt_size, 20, &at, &len) || len < 20)
        return 0;
    char id[40], name[48];
    snprintf(id, sizeof id, "effects/%x%02x", (unsigned)stage + 1, (unsigned)room);
    snprintf(name, sizeof name, "Room %u-%02X effects", (unsigned)stage + 1, (unsigned)room);
    return pe_imp_emit_tims(sink, id, name, rdt + at, len, 4, why, n);
}

const PeImpReader g_re3_effects_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "ps1", .format = "rdt-effects",
    .version = 1, .produces = PE_IMP_PICTURES, .match = "CD_DATA/STAGE?/R*.ARD",
    .tags = "effects", .read = read_effects, .path_key = 1
};

static int read_core_effects(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    (void)path;
    return pe_imp_emit_tims(sink, "effects/core", "Common effects", data, size, 4, why, n);
}

const PeImpReader g_re3_core_effects_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "ps1", .format = "core-effects",
    .version = 1, .produces = PE_IMP_PICTURES, .match = "CD_DATA/ETC/CORE00.TIM",
    .tags = "effects", .read = read_core_effects
};
