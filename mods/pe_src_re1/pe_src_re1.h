#ifndef PE_SRC_RE1_H
#define PE_SRC_RE1_H
#include "pe_imp.h"
#include "pe_imp_rig.h"

extern const PeImpRig g_re1_human_rig;

int re1_emd_parse(const uint8_t *data, uint32_t size, PeImpModel *m, char *why, size_t n);
extern const PeImpReader g_re1_emd_ps1;
extern const PeImpReader g_re1_sound_ps1;
extern const char g_re1_naming[];

#endif
