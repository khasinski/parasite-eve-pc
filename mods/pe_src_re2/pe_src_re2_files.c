/* Resident Evil 2 files: COMMON/DATA/FILES.TIM: the file pages and pictures, a TIM at each 2 KB boundary (191).
 * Kept as stored in a Pictures record (pe_imp_pictures.h); which pages make up which document is
 * not known yet. Read only. */
#include "pe_imp_pictures.h"
#include "pe_src_re2.h"

static int read_files(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    (void)path;
    return pe_imp_emit_tims(sink, "files/all", "Files", data, size, 2048, why, n);
}

const PeImpReader g_re2_files_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "files",
    .version = 1, .produces = PE_IMP_PICTURES, .match = "COMMON/DATA/FILES.TIM",
    .tags = "files", .read = read_files
};

/* COMMON/DATA/MAPS.PIX: the 20 map pages, 256 x 256 at 4 bits a pixel, with no palette on the disc
 * (it isn't stored plainly anywhere; the pages get a placeholder grey palette, marked GREY) */
static int read_maps(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    (void)path;
    return pe_imp_emit_pages4(sink, "maps/all", "Maps", data, size, why, n);
}

const PeImpReader g_re2_maps_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "maps",
    .version = 1, .produces = PE_IMP_PICTURES, .match = "COMMON/DATA/MAPS.PIX",
    .tags = "maps", .read = read_maps
};
