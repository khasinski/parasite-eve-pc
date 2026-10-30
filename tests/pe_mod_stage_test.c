/* Model stage, with the model and the presenter stubbed: box and full-screen placement, the slow turn
 * that stops once turned by hand, pause, the same model keeping its turn, an empty box, hide. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pe_model_stage.h"
#include "pe_present.h"

static PeModel s_model;
static int s_loads, s_last_yaw, s_last_frame, s_shows, s_hides, s_sx, s_sy, s_sw, s_sh, s_silhouette;
static uint8_t s_first_alpha;

const PeModel *pe_model_load(const PeModelSource *src) { ++s_loads; return src && src->map ? &s_model : NULL; }
int pe_model_fit(const PeModel *m, PeViewFit *fit) { (void)m; memset(fit, 0, sizeof *fit); fit->radius = 1; fit->y1 = 1; return 1; }
int pe_model_render(const PeModel *m, const PeViewFit *fit, const PeView *v, PeImage *out) {
    (void)m; (void)fit;
    s_last_yaw = v->yaw; s_last_frame = v->frame; s_silhouette = v->silhouette;
    out->rgba[3] = 255;
    return 1;
}
int pe_present_show(int slot, int x, int y, const PeImage *img, PePresentMode mode, uint32_t rgb) {
    (void)slot; (void)mode; (void)rgb;
    ++s_shows; s_sx = x; s_sy = y; s_sw = img->w; s_sh = img->h; s_first_alpha = img->rgba[3];
    return 0;
}
void pe_present_hide(int slot) { (void)slot; ++s_hides; }

int main(void) {
    PeModelStage s;
    PeModelSource rat = {PE_MODEL_ROOM, 5, 12, NULL, NULL, NULL}, parrot = {PE_MODEL_ROOM, 7, 14, NULL, NULL, NULL};
    int i, yaw;
    pe_stage_init(&s, 2, 0x101828);
    pe_stage_place(&s, 160, 30, 120, 96);
    assert(pe_stage_show(&s, &rat, 0));
    pe_stage_draw(&s);
    assert(s_sx == 160 && s_sy == 30 && s_sw == 120 && s_sh == 96 && s_first_alpha == 255);
    for (i = 0; i < 9; ++i) pe_stage_draw(&s);
    assert(s_last_yaw > 0 && s_last_frame > 0);                    /* turns and animates by itself */

    pe_stage_fullscreen(&s, 1);                                    /* over the whole game screen */
    pe_stage_draw(&s);
    assert(s_sx == 0 && s_sy == 0 && s_sw == 320 && s_sh == 224);
    pe_stage_fullscreen(&s, 0);

    pe_stage_turn(&s, 512);                                        /* by hand: no more turning by itself */
    pe_stage_draw(&s); yaw = s_last_yaw;
    pe_stage_draw(&s);
    assert(s_last_yaw == yaw);

    pe_stage_pause(&s, 1);                                         /* paused: the frame holds */
    pe_stage_draw(&s); i = s_last_frame;
    pe_stage_draw(&s);
    assert(s_last_frame == i);
    pe_stage_pause(&s, 0);

    i = s_loads;
    assert(pe_stage_show(&s, &rat, 1));                            /* the same model: keeps its turn */
    pe_stage_draw(&s);
    assert(s_last_yaw == yaw && s_silhouette == 1);
    assert(pe_stage_show(&s, &parrot, 0));                         /* a new one: faces front again */
    pe_stage_draw(&s);
    assert(s_last_yaw == PE_STAGE_FRONT_YAW);
    (void)i;

    assert(pe_stage_show(&s, NULL, 0));                            /* nothing: an empty box */
    pe_stage_draw(&s);
    assert(s_first_alpha == 0 && s_sw == 120);

    pe_stage_hide(&s);
    assert(s_hides == 1 && !s.fullscreen);
    pe_stage_free(&s);
    puts("Stage checks passed: box and full screen, turning by itself and by hand, pause, same model, empty box, hide.");
    return 0;
}
