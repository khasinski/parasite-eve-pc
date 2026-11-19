#ifndef PE_SRC_RE1_H
#define PE_SRC_RE1_H
#include "pe_imp.h"
#include "pe_imp_rig.h"

extern const PeImpRig g_re1_human_rig;

int re1_emd_parse(const uint8_t *data, uint32_t size, PeImpModel *m, char *why, size_t n);
extern const PeImpReader g_re1_emd_ps1, g_re1_emd_npc_ps1, g_re1_emd_alt_ps1;
extern const PeImpReader g_re1_sound_ps1, g_re1_music_ps1, g_re1_rooms_ps1, g_re1_logic_ps1, g_re1_door_ps1, g_re1_items_ps1, g_re1_item_icons_ps1, g_re1_movies_ps1, g_re1_files_ps1, g_re1_objects_ps1, g_re1_file_text_ps1, g_re1_door_models_ps1, g_re1_room_anims_ps1, g_re1_maps_ps1, g_re1_monsters_ps1;
/* RE1 moves: the EMR at emr (s16 angles) and the EDD at edd, inside data[0, size) */
PeImpAnimation *re1_moves(const uint8_t *data, uint32_t size, uint32_t emr, uint32_t edd);
extern const char g_re1_naming[];

#endif
