/* CPE original optional stylized model fill. Original game lighting remains
 * authoritative and is not evaluated by this helper. No GTE state is touched. */
#include "pe1/model_fill_lighting.h"
#include <math.h>
#include <string.h>
static int selected_preset; /* off unless explicitly enabled */
void Render_SetModelFillLighting(int preset) {
    selected_preset=preset>=0&&preset<=2?preset:0;
}
int Render_GetModelFillLighting(void){return selected_preset;}
void CpeModelFillPrepare(CpeModelFillFrame *f,const int16_t *rotation,int preset) {
    memset(f,0,sizeof(*f));f->preset=preset>=0&&preset<=2?preset:0;
    if(f->preset!=2||!rotation)return;
    float n[3]={0};
    for(int r=0;r<3;r++)for(int c=0;c<3;c++){
        const float v=rotation[r*3+c]/4096.0f;
        f->rotation[r][c]=v;n[r]+=v*v;
    }
    // For a uniform orthogonal transform, normalizing R*N is equivalent to
    // the inverse-transpose direction. Decline nonuniform/sheared matrices
    // rather than pretend the same formula is a correct normal transform.
    float mean=(n[0]+n[1]+n[2])/3.0f;
    if(mean<1e-8f||!isfinite(mean))return;
    for(int r=0;r<3;r++)if(fabsf(n[r]-mean)>mean*0.02f)return;
    for(int r=0;r<3;r++)for(int c=r+1;c<3;c++){
        float dot=0;for(int k=0;k<3;k++)dot+=f->rotation[r][k]*f->rotation[c][k];
        if(fabsf(dot)>mean*0.02f)return;
    }
    f->directional=1;
}
uint32_t CpeModelFillColour(uint32_t colour,const int16_t *normal,const CpeModelFillFrame *f) {
    if(!f||!f->preset)return colour; // exact off path, no normal access/math
    unsigned r=colour&255,g=(colour>>8)&255,b=(colour>>16)&255;
    unsigned brightest=r>g?r:g;if(b>brightest)brightest=b;
    if(!brightest||brightest==255)return colour;
    unsigned strength=40; // modest neutral fill; fallback for invalid normals
    if(f->preset==2&&f->directional&&normal){
        float world[3]={0};
        for(int row=0;row<3;row++)for(int col=0;col<3;col++)world[row]+=f->rotation[row][col]*normal[col];
        float len2=world[0]*world[0]+world[1]*world[1]+world[2]*world[2];
        if(len2>1e-8f&&isfinite(len2)){
            // Fixed world-space key (0,-0.8,-0.6), not a camera-facing rim.
            float ndl=(-0.8f*world[1]-0.6f*world[2])/sqrtf(len2);
            ndl=ndl<0?0:ndl>1?1:ndl;
            strength=24+(unsigned)(40.0f*ndl+0.5f);
        }
    }
    // One shared multiplicative lift preserves hue/zero channels and fades.
    // The brightest channel limits headroom; no new light appears at black.
    unsigned headroom=255-brightest;
    r+=(r*headroom*strength+32768u)>>16;
    g+=(g*headroom*strength+32768u)>>16;
    b+=(b*headroom*strength+32768u)>>16;
    return (colour&0xff000000u)|(r>255?255:r)|((g>255?255:g)<<8)|((b>255?255:b)<<16);
}
