/* Resident Evil 2 room backgrounds. Read only.
 *   COMMON/BSS/ROOM*.BSS   one file per room: each camera view is an MDEC frame (320 x 240) at the start of a
 *                          0x10000-byte slot; the room's ID is "room/<file number>" ("room/" + the digits
 *                          of its name), one ImageSet with a picture per view. */
#include <stdio.h>
#include <string.h>

#include "pe_src_re2.h"

static int read_room(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char digits[16] = "";
    size_t k = 0;
    for (const char *c = base; *c && *c != '.' && k + 1 < sizeof digits; c++)
        if ((*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'F' && k)) digits[k++] = (char)(*c >= 'A' ? *c + 32 : *c);
    digits[k] = 0;
    if (!k) return 0;                                         /* not a room file */
    char id[32], name[48];
    snprintf(id, sizeof id, "room/%s", digits);
    snprintf(name, sizeof name, "Room %c-%s", digits[0], digits + 1);
    int rc = pe_imp_emit_mdec_slots(sink, id, name, data, size, 0x10000, 320, 240);
    if (rc < 0 && why && n) snprintf(why, n, "RE2: couldn't catalog %s", base);
    return rc < 0 ? -1 : 0;
}

const PeImpReader g_re2_rooms_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "bss-mdec",
    .version = 1, .produces = PE_IMP_IMAGESET, .match = "COMMON/BSS/ROOM*.BSS",
    .tags = "room background", .read = read_room
};
