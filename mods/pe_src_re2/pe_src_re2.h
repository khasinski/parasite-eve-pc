#ifndef PE_SRC_RE2_H
#define PE_SRC_RE2_H
#include "pe_imp.h"
#include "pe_imp_rig.h"

extern const PeImpRig g_re2_human_rig;

int re2_pld_parse(const uint8_t *data, uint32_t size, PeImpModel *m, char *why, size_t n);
extern const PeImpReader g_re2_pld_ps1;
extern const PeImpReader g_re2_sound_ps1;
extern const PeImpReader g_re2_music_ps1;
extern const PeImpReader g_re2_rooms_ps1, g_re2_logic_ps1;
extern const char g_re2_naming[];

#endif
