/* Resident Evil 3 room logic: each room file (CD_DATA/STAGE?/R*.ARD) becomes a RoomLogic record (pe_imp_logic.h), what the
 * room runtime reads: doors, trigger zones, pickups, camera zones, enemy placements, and the room's
 * scripts, messages and collision as they are. Read only. */
#include "pe_imp_rdt.h"
#include "pe_src_re3.h"

static const char *const k_players[2] = {"Jill", NULL};

static int read_logic(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    const uint8_t *rdt;
    uint32_t rdt_size;
    if (pe_imp_re3_ard_rdt(data, size, &rdt, &rdt_size)) return 0;          /* an archive without a room */
    return pe_imp_emit_re_logic(sink, PE_IMP_RE3, path, rdt, rdt_size, k_players, why, n);
}

const PeImpReader g_re3_logic_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "ps1", .format = "rdt-logic",
    .version = 1, .produces = PE_IMP_ROOMLOGIC, .match = "CD_DATA/STAGE?/R*.ARD",
    .tags = "room logic", .read = read_logic, .path_key = 1
};
