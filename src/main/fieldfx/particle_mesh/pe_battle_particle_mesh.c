#include "pe_battle_particle_mesh.h"
unsigned PeBattleParticle_Build(int enabled, PeParticleWorldPoint anchor,
    unsigned age, unsigned duration, unsigned available_triangles,
    PeParticleFace out[4]) {
    static const unsigned faces[4][3]={{0,2,1},{0,1,3},{0,3,2},{1,2,3}};
    PeParticleWorldPoint p[4];
    unsigned i,j,phase,r,fade;
    if(!enabled || !out || !duration || duration>4096 || age>=duration || available_triangles<4) return 0;
    /* Same bounded world domain used by the original short-vector projector.
     * Reserve enough room for geometry instead of wrapping coordinates. */
    if(anchor.x < -32700 || anchor.x>32700 || anchor.y < -32700 || anchor.y>32700 || anchor.z < -32700 || anchor.z>32700) return 0;
    fade=(duration-age)*255/duration;
    r=4+(duration-age)*8/duration;
    p[0]=(PeParticleWorldPoint){0,-(int32_t)(2*r),0};
    p[1]=(PeParticleWorldPoint){(int32_t)r,(int32_t)r,0};
    p[2]=(PeParticleWorldPoint){-(int32_t)r,(int32_t)r,(int32_t)r};
    p[3]=(PeParticleWorldPoint){-(int32_t)r,(int32_t)r,-(int32_t)r};
    phase=(age/3)&3; /* geometry-only repeatable spin; no original rand() calls */
    for(i=0;i<4;i++){
        int32_t x=p[i].x,z=p[i].z;
        if(phase==1){p[i].x=-z;p[i].z=x;}
        else if(phase==2){p[i].x=-x;p[i].z=-z;}
        else if(phase==3){p[i].x=z;p[i].z=-x;}
        p[i].x+=anchor.x;p[i].y+=anchor.y;p[i].z+=anchor.z;
    }
    for(i=0;i<4;i++){
        for(j=0;j<3;j++)out[i].vertices[j]=p[faces[i][j]];
        out[i].rgba[0]=255;out[i].rgba[1]=(uint8_t)(160-i*18);out[i].rgba[2]=32;out[i].rgba[3]=(uint8_t)fade;
    }
    return 4;
}
