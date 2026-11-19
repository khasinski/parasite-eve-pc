/* Resident Evil 2 files: COMMON/DATA/FILES.TIM: the file pages and pictures (216 TIMs), kept as
 * stored in a Pictures record (pe_imp_pictures.h). Each of the 25 documents is an 8-bit background
 * then its 4-bit text pages, in the game's file order: document k is item 0x68 + k (Chris's diary to
 * the second hint file, as in biohazard-utils' item list), so DOCS names each by its item. Read only. */
#include <stdio.h>
#include <string.h>

#include "pe_imp_pictures.h"
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
