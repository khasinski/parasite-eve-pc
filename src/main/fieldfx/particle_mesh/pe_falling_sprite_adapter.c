/* Source-only adapter to the actual game's 3D sprite state. Not registered. */
#include "pe1/render_object.h"
#include "pe_battle_particle_mesh.h"
unsigned PeBattleParticle_FromFallingSprite(int enabled, const RenderBouncingSprite *s,
    unsigned age, unsigned available_triangles, PeParticleFace out[4]) {
    if(!PE_BATTLE_PARTICLE_PROTOTYPE || !s || s->duration<=0) return 0;
    return PeBattleParticle_Build(enabled,(PeParticleWorldPoint){s->x,s->y,s->z},age,
        (unsigned)s->duration,available_triangles,out);
}
