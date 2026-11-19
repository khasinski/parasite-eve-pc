/* Dino Crisis US disc archives pair a type-3 header with a type-4 SPU body. */
#include <stdio.h>
#include <string.h>
#include "pe_imp_spu.h"
#include "pe_src_dc1.h"
#include "pe_src_dc1_archive.h"

static int bad(char *why, size_t n, const char *message)
{
    if (why && n) snprintf(why, n, "%s", message);
    return -1;
}

static int read_gian(const Dc1Block *header, const Dc1Block *body, const char *bank,
                     PeImpSink *sink, char *why, size_t n)
{
    if (header->size < 12 || body->size % PE_IMP_SPU_BLOCK)
        return bad(why, n, "invalid Gian header or SPU body size");

    const uint8_t *meta = header->data;
    unsigned count = meta[10] | (unsigned)meta[11] << 8;
    if (!count || count > 254) return bad(why, n, "invalid Gian waveform count");

    unsigned wave = 0;
    uint32_t start = 0;
    for (uint32_t at = 0; at < body->size; at += PE_IMP_SPU_BLOCK) {
        uint8_t flags = body->data[at + 1];
        if (flags != 3 && flags != 7) continue;
        uint32_t end = at + PE_IMP_SPU_BLOCK;
        if (++wave > count) return bad(why, n, "Gian has more waveform terminators than declared");
        if (pe_imp_spu_emit(sink, bank, wave, body->data + start, end - start, why, n))
            return -1;
        start = end;
    }
    if (wave != count || start != body->size)
        return bad(why, n, "Gian waveform count or final terminator does not match the body");
    return 0;
}

static int read_archive(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink,
                        char *why, size_t n)
{
    Dc1Archive archive;
    if (!dc1_archive_open(&archive, data, size)) return 0;

    char stem[24];
    if (pe_imp_sound_bank_name(path, stem)) return bad(why, n, "invalid Dino Crisis archive name");

    Dc1Block block, header = {0};
    unsigned bank_number = 0;
    int rc;
    while ((rc = dc1_archive_next(&archive, &block)) > 0) {
        if (header.data && block.type != 4)
            return bad(why, n, "Dino Crisis sound header has no adjacent SPU body");
        if (block.type == 3) {
            header = block;
            continue;
        }
        if (block.type != 4) continue;
        if (!header.data || header.size < 4 || header.address != block.address || header.tag != block.tag)
            return bad(why, n, "Dino Crisis sound header and body do not match");

        char bank[32];
        ++bank_number;
        snprintf(bank, sizeof bank, "%s/bank%02u", stem, bank_number);
        if (!memcmp(header.data, "Gian", 4)) {
            if (read_gian(&header, &block, bank, sink, why, n)) return -1;
        } else if (!memcmp(header.data, "pBAV", 4)) {
            /* Preserve the IDs from the original one-archive reader. */
            const char *id = bank_number == 1 && !strcmp(stem, "st60e") ? stem : bank;
            if (pe_imp_vab_read(sink, id, header.data, header.size, block.data, block.size, why, n))
                return -1;
        } else {
            return bad(why, n, "unrecognized Dino Crisis sound header");
        }
        header.data = NULL;
    }
    if (rc < 0 || header.data) return bad(why, n, "truncated Dino Crisis sound archive");
    return 0;
}

/* Register after the specific CORE0? costume reader. The verified costume
 * archives have no type-4 banks. */
const PeImpReader g_dc1_sound_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "dc1", .platform = "ps1", .format = "dat-spu",
    .version = 1, .produces = PE_IMP_SOUND, .match = "PSX/DATA/*.DAT",
    .tags = "sound sample", .read = read_archive, .path_key = 1
};
