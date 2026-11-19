/* Resident Evil 2 room objects: the 3D props a room places (typewriters, boxes, items on the floor,
 * doors that swing). A room file's section 10 lists them, header byte 2 objects of (u32 texture
 * offset, u32 mesh offset); each mesh is an MD1 of one part with an 8-bit TIM, the same formats as
 * the player models, so they're read with re2_object_parse. Each becomes a one-bone Model
 * "object/<stage><room>-<n>" (the room's position for it is set by its script). Read only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_rdt.h"
#include "pe_src_re2.h"

static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int read_objects(const uint8_t *rdt, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    uint32_t stage, room, player, at, len;
    if (pe_imp_re_room_name(PE_IMP_RE2, path, &stage, &room, &player) || size < 8 ||
        pe_imp_rdt_section(rdt, size, 10, &at, &len))
        return 0;
    uint32_t count = rdt[2];
    if ((uint64_t)count * 8 > len) count = len / 8;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t tim = le32(rdt + at + 8 * i), md1 = le32(rdt + at + 8 * i + 4);
        if (!md1 || md1 >= size || !tim || tim >= size) continue;      /* an empty slot */
        PeImpModel *m = calloc(1, sizeof *m);
        if (!m) { if (why && n) snprintf(why, n, "out of memory"); return -1; }
        char quiet[120];
        if (re2_object_parse(rdt, size, md1, tim, m, quiet, sizeof quiet)) { free(m); continue; }   /* not one we read */
        char id[40], name[48];
        snprintf(id, sizeof id, "object/%x%02x-%u", (unsigned)stage + 1, (unsigned)room, (unsigned)i);
        snprintf(name, sizeof name, "Room %u-%02X object %u", (unsigned)stage + 1, (unsigned)room, (unsigned)i);
        if (pe_imp_emit(sink, PE_IMP_MODEL, id, name, m)) {
            if (why && n) snprintf(why, n, "couldn't catalog %s", id);
            return -1;
        }
    }
    return 0;
}

const PeImpReader g_re2_objects_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "rdt-objects",
    .version = 1, .produces = PE_IMP_MODEL, .match = "PL?/RDT/ROOM*.RDT",
    .tags = "room object", .read = read_objects, .path_key = 1
};
