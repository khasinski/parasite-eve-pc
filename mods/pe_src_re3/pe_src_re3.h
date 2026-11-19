#ifndef PE_SRC_RE3_H
#define PE_SRC_RE3_H
#include "pe_imp.h"
#include "pe_imp_rig.h"

extern const PeImpRig g_re3_human_rig;

/* 0; -1 with a reason; -2 when the file isn't one of the people (nothing to read, no error) */
int re3_pld_parse(const uint8_t *data, uint32_t size, PeImpModel *m, char *why, size_t n);
extern const PeImpReader g_re3_pld_ps1;
extern const PeImpReader g_re3_sound_ps1, g_re3_bgm_ps1, g_re3_room_music_ps1, g_re3_rooms_ps1, g_re3_logic_ps1, g_re3_door_ps1, g_re3_items_ps1, g_re3_item_pictures_ps1, g_re3_movies_ps1, g_re3_files_ps1, g_re3_maps_ps1, g_re3_objects_ps1, g_re3_door_models_ps1;
extern const char g_re3_naming[];

/* Enemies (pe_src_re3_emd.c, pe_src_re3_tones.c, pe_src_re3_enemies.c) */
#include "pe_imp_enemy.h"
/* an EMD and its TIM -> the Model (bound parts skinned) and the Animation; 0, or -1 with a reason */
int re3_emd_parse(const uint8_t *emd, uint32_t emd_size, const uint8_t *tim, uint32_t tim_size,
                  PeImpModel *m, PeImpAnimation *anim, char *why, size_t n);
int re3_tim_parse(const uint8_t *data, uint32_t size, PeImpModel *m, char *why, size_t n);
int re3_tone_count(const uint8_t *vh, uint32_t vh_size);          /* -1: not an RE3 bank */
double re3_tone_rate(int note, int centre, int fine);
PeImpSound *re3_tone_sound(const uint8_t *vh, uint32_t vh_size, const uint8_t *vb, uint32_t vb_size, int t,
                           char *why, size_t n);
extern const PeImpReader g_re3_emd_pc, g_re3_emd_ps1, g_re3_room_anims_ps1, g_re3_tones_pc;
extern const PeImpEnemyProfile g_re3_nemesis;

/* RE3's compact sound banks (pe_src_re3_music.c): the standard VAB header's size for the compact
 * header at h (0 if it doesn't match vb_size samples), and that header rebuilt into v. */
uint32_t re3_bank_size(const uint8_t *h, uint32_t avail, uint32_t vb_size, char *why, size_t n);
int re3_bank_rebuild(const uint8_t *h, uint32_t vb_size, uint8_t *v, char *why, size_t n);

#endif
