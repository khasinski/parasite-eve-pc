#ifndef PE_SRC_DC1_H
#define PE_SRC_DC1_H
#include "pe_imp.h"
#include "pe_imp_rig.h"

extern const PeImpRig g_dc1_human_rig;

/* Dino Crisis's block compression; *out is malloc'd. 0, or -1 on a bad stream. */
int dc1_lz(const uint8_t *src, uint32_t n, uint8_t **out, uint32_t *size);
int dc1_core_parse(const uint8_t *data, uint32_t size, PeImpModel *m, char *why, size_t n);
extern const PeImpReader g_dc1_core_ps1;
extern const char g_dc1_naming[];

#endif
