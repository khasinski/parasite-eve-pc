/* Resident Evil 3 door transitions: each door file (CD_DATA/STAGE?/DOOR*.DO1) becomes a DoorScene record (pe_imp_door.c), what
 * the room runtime plays between rooms. Read only. */
#include "pe_imp_door.h"
#include "pe_src_re3.h"

static int read_door(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    return pe_imp_emit_re_door(sink, PE_IMP_RE3, path, data, size, why, n);
}

const PeImpReader g_re3_door_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "ps1", .format = "door",
    .version = 1, .produces = PE_IMP_DOORSCENE, .match = "CD_DATA/STAGE?/DOOR*.DO1",
    .tags = "door transition", .read = read_door
};
