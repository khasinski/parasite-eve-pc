#ifndef PE1_MODEL_FILL_LIGHTING_H
#define PE1_MODEL_FILL_LIGHTING_H
#include <stdint.h>
/* Optional colour-only treatment of actual lit Render_DrawObject models.
 * Not character classification, specular/rim lighting, reflection or geometry.
 * 0 original,1 neutral soft fill,2 fixed world-direction fill.
 */
typedef struct CpeModelFillFrame {
    float rotation[3][3];
    int preset;
    int directional;
} CpeModelFillFrame;
void Render_SetModelFillLighting(int preset);
int Render_GetModelFillLighting(void);
void CpeModelFillPrepare(CpeModelFillFrame *frame,const int16_t *rotation,int preset);
uint32_t CpeModelFillColour(uint32_t colour,const int16_t *normal,const CpeModelFillFrame *frame);
#endif
