#ifndef PE_SRC_RE2_H
#define PE_SRC_RE2_H
#include "pe_imp.h"
#include "pe_imp_rig.h"

extern const PeImpRig g_re2_human_rig;

int re2_pld_parse(const uint8_t *data, uint32_t size, PeImpModel *m, char *why, size_t n);
/* A room object: the MD1 mesh and 8-bit TIM at those offsets of a room file, as a one-bone model. */
int re2_skeleton_mesh(const uint8_t *data, uint32_t size, uint32_t emr, uint32_t md1, uint32_t tim, PeImpModel *m,
                      char *why, size_t n);
/* RE2 moves: an EDD (per move u16 frames, u16 offset; frames u32, low 12 bits the key) at edd..edd_end
 * over the key frames of the EMR at emr (key frames up to kf_end); NULL if they don't read */
PeImpAnimation *re2_moves(const uint8_t *data, uint32_t size, uint32_t emr, uint32_t edd, uint32_t edd_end, uint32_t kf_end);
extern const PeImpReader g_re2_emd_ps1, g_re2_player_moves_ps1, g_re2_weapons_ps1, g_re2_effects_ps1, g_re2_core_effects_ps1;
int re2_object_parse(const uint8_t *data, uint32_t size, uint32_t md1, uint32_t tim, PeImpModel *m, char *why, size_t n);
extern const PeImpReader g_re2_pld_ps1;
extern const PeImpReader g_re2_sound_ps1, g_re2_enemy_sound_ps1, g_re2_voice_ps1;
extern const PeImpReader g_re2_music_ps1;
extern const PeImpReader g_re2_rooms_ps1, g_re2_logic_ps1, g_re2_door_ps1, g_re2_items_ps1, g_re2_item_pictures_ps1, g_re2_movies_ps1, g_re2_player_movies_ps1, g_re2_files_ps1, g_re2_maps_ps1, g_re2_objects_ps1, g_re2_door_models_ps1, g_re2_rbj_ps1;
extern const char g_re2_naming[];

/* RE2's enemy kinds (placement kind -> model) and known strengths, into the importer's registry
 * (pe_src_re2_enemies.c). 0, or -1 if any was refused. */
int pe_re2_enemies_register(void);

#endif
