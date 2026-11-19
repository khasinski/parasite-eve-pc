/* Resident Evil 3 files: CD_DATA/ETC/FILEGU.PIX: the US file pages and pictures, a TIM at each 2 KB boundary (183).
 * Kept as stored in a Pictures record (pe_imp_pictures.h). Each of the 31 documents is an 8-bit
 * background then its 4-bit text pages; DOCS lists them (RE3's files aren't items, and their titles
 * are only on their pages). Read only. */
#include "pe_imp_pictures.h"
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
