#ifndef PE_IMP_DOOR_H
#define PE_IMP_DOOR_H
/* Resident Evil door transition files as DoorScene records (pe_imp_door.c). */
#include "pe_imp_logic.h"
#include "pe_imp_room.h"                          /* PE_IMP_RE1, PE_IMP_RE2, PE_IMP_RE3 */

/* Emits a door file (RE1 .DOR, RE2 .DO2, RE3 .DO1) as "door/<name>" ("door/<stage>-<name>" for RE3),
 * and RE2's door sounds as Sounds in bank "door<name>". 0 (also for a file that isn't a door's),
 * or -1 with a reason. */
int pe_imp_emit_re_door(PeImpSink *sink, unsigned game, const char *path, const uint8_t *data, uint32_t size,
                        char *why, size_t n);

#endif
