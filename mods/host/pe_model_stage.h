#ifndef PE_MODEL_STAGE_H
#define PE_MODEL_STAGE_H

/* A model on show: what a screen uses to put a turning 3D model somewhere on the game screen (the
 * Bestiary, the Wardrobe, test pages). It loads the model (pe_model.h), fits it once, turns it
 * (slowly by itself until turned by hand), plays its idle animation (pausable), draws it
 * (pe_model_view.h) and hands the picture to the image presenter (pe_present.h), each frame.
 * Full screen draws it over the whole game screen instead of its box. One stage per presenter slot. */

/* Authored models face the viewer at this yaw (4096 units per turn). */
#define PE_STAGE_FRONT_YAW 3072

#include <stdint.h>
#include "pe_model_view.h"
#include "pe_services.h"

typedef struct PeModelStage {
    int slot;                     /* presenter slot (0 .. PE_PRESENT_SLOTS - 1) */
    uint32_t box_rgb;             /* the background */
    int x, y, w, h;               /* its box on the game screen */
    int fullscreen, paused, manual, silhouette;
    int yaw, ticks;
    int has_source, shown;
    PeModelSource source;
    const PeModel *model;
    PeViewFit fit;
    uint8_t *rgba;                /* the picture (allocated on first use, full-screen size) */
} PeModelStage;

void pe_stage_init(PeModelStage *s, int slot, uint32_t box_rgb);
void pe_stage_place(PeModelStage *s, int x, int y, int w, int h);       /* game coordinates */
/* What to show: a model source (NULL: an empty box), drawn as a silhouette or not. A new model starts
 * facing front, turning by itself; showing the same one again keeps its turn. 0 if it didn't load
 * (the box shows empty). */
int pe_stage_show(PeModelStage *s, const PeModelSource *src, int silhouette);
void pe_stage_fullscreen(PeModelStage *s, int on);
void pe_stage_turn(PeModelStage *s, int delta);                         /* by hand: 4096 a turn */
void pe_stage_pause(PeModelStage *s, int paused);
void pe_stage_draw(PeModelStage *s);                                    /* once a frame */
void pe_stage_hide(PeModelStage *s);                                    /* and forget the model */
void pe_stage_free(PeModelStage *s);

#endif
