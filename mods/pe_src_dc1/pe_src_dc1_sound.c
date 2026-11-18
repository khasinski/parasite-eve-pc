/* Verified US ST60E room archive: a VAB header and three embedded SPU sounds. */
#include <stdio.h>
#include "pe_imp_spu.h"

static int read_st60e(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink,
                      char *why, size_t n)
{
    enum { HEADER = 0x800, BODY = 0x1800, BODY_SIZE = 26832 };
    (void)path;
    if (size >= BODY + BODY_SIZE)
        return pe_imp_vab_read(sink, "st60e", data + HEADER, BODY - HEADER,
                               data + BODY, BODY_SIZE, why, n);
    if (why && n) snprintf(why, n, "truncated Dino Crisis ST60E sound bank");
    return -1;
}

const PeImpReader g_dc1_sound_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "dc1", .platform = "ps1", .format = "st60e-vab",
    .version = 1, .produces = PE_IMP_SOUND, .match = "PSX/DATA/ST60E.DAT",
    .tags = "sound sample room", .read = read_st60e
};
