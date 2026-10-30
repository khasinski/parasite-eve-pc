#ifndef PE_DISC_ASSETS_H
#define PE_DISC_ASSETS_H

/* Read-only access to game data on the player's own disc, for host features that show game assets
 * (the Bestiary viewer). Reads go through the port's disc reader on the bound image (g_CdImage,
 * pe_disc_read: a locked seek and read, safe beside the game's own reads and streamed music), never
 * through the emulated CD drive. Nothing is cached here; callers keep what they decode. */

#include <stdint.h>

/* A room's three runs of sectors, as Scene_LoadRoom reads them: the scratch blob and the texture
 * blob (both SceneAssetBlobs whose TIM records go to VRAM), then the room data (models, animation
 * rows, scripts). Buffers are malloc'd; sizes in bytes (0 if the run is empty). */
typedef struct PeRoomBlobs {
    uint8_t *scratch, *texture, *data;
    uint32_t scratch_size, texture_size, data_size;
} PeRoomBlobs;

/* Room `map` (its number: M001 is 1), from the bound disc's PE.IMG and the room table read at mount
 * (g_RoomSectorTable). 1 on success; 0 (and a log line) if there is no disc, no such room, or a read
 * fails. */
int pe_disc_room(int map, PeRoomBlobs *out);
/* Own copy of a playable Aya bundle (10, 12 or 13); free with pe_disc_room_free. */
int pe_disc_player(int bank, PeRoomBlobs *out);
void pe_disc_room_free(PeRoomBlobs *room);

#endif
