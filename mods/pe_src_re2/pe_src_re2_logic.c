/* Resident Evil 2 room logic: each room file (PL?/RDT/ROOM*.RDT) becomes a RoomLogic record (pe_imp_logic.h), what the
 * room runtime reads: doors, trigger zones, pickups, camera zones, enemy placements, and the room's
 * scripts, messages and collision as they are. Read only. */
#include "pe_imp_rdt.h"
#include "pe_src_re2.h"

static const char *const k_players[2] = {"Leon", "Claire"};

static int read_logic(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    return pe_imp_emit_re_logic(sink, PE_IMP_RE2, path, data, size, k_players, why, n);
}

const PeImpReader g_re2_logic_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "rdt-logic",
    .version = 6, .produces = PE_IMP_ROOMLOGIC | PE_IMP_SOUND, .match = "PL?/RDT/ROOM*.RDT",
    .tags = "room logic", .read = read_logic, .path_key = 1
};
