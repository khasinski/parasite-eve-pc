#ifndef PE_SRC_RE3_H
#define PE_SRC_RE3_H
#include "pe_imp.h"
#include "pe_imp_rig.h"

extern const PeImpRig g_re3_human_rig;

/* 0; -1 with a reason; -2 when the file isn't one of the people (nothing to read, no error) */
int re3_pld_parse(const uint8_t *data, uint32_t size, PeImpModel *m, char *why, size_t n);
extern const PeImpReader g_re3_pld_ps1;
extern const char g_re3_naming[];

#endif
