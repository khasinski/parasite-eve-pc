#ifndef PE_MODEL_H
#define PE_MODEL_H

/* A decoded game model, for host features that show one (the Bestiary viewer). Data only: decoding
 * reads no game state and draws nothing (pe_model_view.h draws). The formats follow the game's own
 * (Render_SetupEntityPrims, Render_InitPrimBlock, Anim_DecodeBoneRotations*) and the decomp bridge's
 * renderer (tools/pe_assets/render_models.py), which this mirrors. */

#include <stdint.h>
#include "pe_services.h"

typedef struct PeModelPart {
    uint16_t vertex_start, vertex_count;
    uint8_t visible;                     /* 0: the game doesn't draw it (parked or spare geometry) */
    int32_t offset_z;                    /* the part's distance from its parent (bone length) */
} PeModelPart;

typedef struct PeModelFace {
    uint16_t v[4];                       /* vertex indices; v[3] used by quads */
    uint8_t quad, textured;
    uint8_t u[4], t[4];                  /* texture coordinates (u, v) per corner */
    uint16_t clut, tpage;                /* palette and texture page, as the GPU reads them */
} PeModelFace;

typedef struct PeModel {
    int parts, verts, faces, commands;
    PeModelPart *part;
    int16_t (*vert)[3];
    PeModelFace *face;
    int8_t *command;                     /* hierarchy: part index, -1 push, -2 pop */
    uint16_t *vram;                      /* a private 1024 x 512 copy of the room's textures */
    uint8_t *anim;                       /* the idle animation row (a copy), or NULL */
    uint32_t anim_size;
} PeModel;

/* Load a model (PE_MODEL_ROOM: room and bank slot on the bound disc; PE_MODEL_SKIN: standalone
 * skin folder or native player bank, decoded with player idle; PE_MODEL_CALLBACK: the
 * provider's loader). The last few are kept, so loading again is cheap; the model stays valid until
 * pe_model_release or until it falls out of that cache. NULL (and a log line) on failure. */
const PeModel *pe_model_load(const PeModelSource *src);
void pe_model_release_all(void);

/* Decode a model from a room's blobs (for tests and pe_model_load). 1 on success. */
struct PeRoomBlobs;
int pe_model_decode(const struct PeRoomBlobs *room, int slot, PeModel *out);
/* Player bundles have slot 0 and idle row 0x15, independent of enemy row grouping. */
int pe_model_decode_player(const struct PeRoomBlobs *room, PeModel *out);
/* Internal source loader, isolated in pe_model_skin.c; never equips or uploads to game VRAM. */
int pe_model_skin_load(const PeModelSource *src, PeModel *out);
void pe_model_free(PeModel *m);

/* The idle animation: its frame count (at least 1), and one frame's root translation and per-part
 * angles (4096 = a full turn), as the game decodes them. */
int pe_model_frames(const PeModel *m);
void pe_model_frame(const PeModel *m, int frame, int translation[3], uint16_t (*angles)[3]);

#endif
