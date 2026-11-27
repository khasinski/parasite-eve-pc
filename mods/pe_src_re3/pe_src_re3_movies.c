/* Resident Evil 3 movies: each STR file (CD_DATA/ZMOVIE/<name>.STR) is copied once into the store as a Movie (pe_imp_movie.c):
 * its MDEC frames and its CD-XA sound sectors. Read only; raw .bin images only (an .iso has no
 * sound sectors). */
#include "pe_src_re3.h"

static int read_movie(const uint8_t *str, uint32_t size, const uint8_t *xa, uint32_t xa_size, const char *path,
                      PeImpSink *sink, char *why, size_t n) {
    return pe_imp_emit_str(sink, str, size, xa, xa_size, path, why, n);
}

const PeImpReader g_re3_movies_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "ps1", .format = "str",
    .version = 1, .produces = PE_IMP_MOVIE, .match = "CD_DATA/ZMOVIE/*.STR",
    .tags = "movie", .read_pair = read_movie, .xa = PE_IMP_XA_BOTH
};
