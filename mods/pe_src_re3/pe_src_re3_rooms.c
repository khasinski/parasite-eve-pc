#include <stdio.h>

#include "pe_imp_room.h"
#include "pe_src_re3.h"

static int find_room_data(PeImpDisc *disc, const char *path) {
    return pe_imp_re_room_companion(disc, path, PE_IMP_RE3);
}

static int read_room(const uint8_t *bss, uint32_t size, const uint8_t *rdt, uint32_t rdt_size,
                     const char *path, PeImpSink *sink, char *why, size_t n) {
    int rc = pe_imp_emit_re_room(sink, PE_IMP_RE3, path, bss, size, rdt, rdt_size);
    if (rc < 0 && why && n) snprintf(why, n, "RE3: couldn't catalog %s", path);
    return rc;
}

const PeImpReader g_re3_rooms_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "ps1", .format = "bss-mdec",
    .version = 2, .produces = PE_IMP_IMAGESET, .match = "CD_DATA/STAGE?/R*.BSS",
    .tags = "room background", .find_companion = find_room_data, .read_pair = read_room
};
