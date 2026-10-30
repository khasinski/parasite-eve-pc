/* A model on show. See pe_model_stage.h. */
#include <stdlib.h>
#include <string.h>

#include "pe_model_stage.h"
#include "pe_present.h"

#define PE_SCREEN_W   320
#define PE_SCREEN_H   224
#define PE_TURN_AUTO  10                  /* 4096 a turn: a slow turn by itself */

void pe_stage_init(PeModelStage *s, int slot, uint32_t box_rgb) {
    memset(s, 0, sizeof *s);
    s->slot = slot;
    s->box_rgb = box_rgb;
}

void pe_stage_place(PeModelStage *s, int x, int y, int w, int h) {
    s->x = x;
    s->y = y;
    s->w = w;
    s->h = h;
}

static int pe_same_source(const PeModelSource *a, const PeModelSource *b) {
    return a->kind == b->kind && a->map == b->map && a->actor == b->actor && a->load == b->load && a->user == b->user &&
           (a->folder == b->folder || (a->folder && b->folder && strcmp(a->folder, b->folder) == 0));
}

int pe_stage_show(PeModelStage *s, const PeModelSource *src, int silhouette) {
    s->silhouette = silhouette;
    if (!src) { s->has_source = 0; s->model = NULL; return 1; }
    if (s->has_source && s->model && pe_same_source(&s->source, src)) return 1;
    s->source = *src;
    s->has_source = 1;
    s->yaw = PE_STAGE_FRONT_YAW;
    s->manual = 0;
    s->ticks = 0;
    s->model = pe_model_load(src);
    if (s->model && !pe_model_fit(s->model, &s->fit)) s->model = NULL;
    return s->model != NULL;
}

void pe_stage_fullscreen(PeModelStage *s, int on) { s->fullscreen = on != 0; }
void pe_stage_turn(PeModelStage *s, int delta) { s->manual = 1; s->yaw = (s->yaw + delta) & 4095; }
void pe_stage_pause(PeModelStage *s, int paused) { s->paused = paused != 0; }

void pe_stage_draw(PeModelStage *s) {
    const int x = s->fullscreen ? 0 : s->x, y = s->fullscreen ? 0 : s->y;
    const int w = s->fullscreen ? PE_SCREEN_W : s->w, h = s->fullscreen ? PE_SCREEN_H : s->h;
    PeImage img;
    PeView view;
    if (w <= 0 || h <= 0 || w > PE_SCREEN_W || h > PE_SCREEN_H) return;
    if (!s->rgba && !(s->rgba = malloc((size_t)PE_SCREEN_W * PE_SCREEN_H * 4))) return;
    img.w = w;
    img.h = h;
    img.rgba = s->rgba;
    if (s->model) s->model = pe_model_load(&s->source);   /* the cache may have moved it; a hit is cheap */
    if (!s->model) {
        memset(s->rgba, 0, (size_t)w * h * 4);    /* an empty box */
    } else {
        memset(&view, 0, sizeof view);
        view.w = w;
        view.h = h;
        view.yaw = s->yaw;
        view.frame = s->ticks / 2;              /* the game animates at 30 frames a second */
        view.silhouette = s->silhouette;
        view.silhouette_rgb = 0x080810u;
        if (!pe_model_render(s->model, &s->fit, &view, &img)) return;
        if (!s->manual) s->yaw = (s->yaw + PE_TURN_AUTO) & 4095;
        if (!s->paused) ++s->ticks;
    }
    s->shown = pe_present_show(s->slot, x, y, &img, PE_PRESENT_BOX, s->box_rgb) == 0;
}

void pe_stage_hide(PeModelStage *s) {
    if (s->shown) pe_present_hide(s->slot);
    s->shown = 0;
    s->has_source = 0;
    s->model = NULL;
    s->fullscreen = 0;
}

void pe_stage_free(PeModelStage *s) {
    pe_stage_hide(s);
    free(s->rgba);
    s->rgba = NULL;
}
