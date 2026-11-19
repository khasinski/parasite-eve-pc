/* Resident Evil 2 files: COMMON/DATA/FILES.TIM: the file pages and pictures (216 TIMs), kept as
 * stored in a Pictures record (pe_imp_pictures.h). Each of the 25 documents is an 8-bit background
 * then its 4-bit text pages, in the game's file order (Chris's diary first); DOCS lists them. Read only. */
#include "pe_imp_pictures.h"
#include "pe_src_re2.h"

static int read_files(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    (void)path;
    return pe_imp_emit_documents(sink, "files/all", "Files", data, size, 2048, why, n);
}

const PeImpReader g_re2_files_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "files",
    .version = 2, .produces = PE_IMP_PICTURES, .match = "COMMON/DATA/FILES.TIM",
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
