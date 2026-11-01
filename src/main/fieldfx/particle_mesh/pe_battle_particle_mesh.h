/* Independently authored procedural geometry; no texture/model assets. */
#ifndef PE_BATTLE_PARTICLE_MESH_H
#define PE_BATTLE_PARTICLE_MESH_H
#include <stdint.h>
#ifndef PE_BATTLE_PARTICLE_PROTOTYPE
#define PE_BATTLE_PARTICLE_PROTOTYPE 0
#endif
typedef struct { int32_t x,y,z; } PeParticleWorldPoint;
typedef struct { PeParticleWorldPoint vertices[3]; uint8_t rgba[4]; } PeParticleFace;
/* A render-only augmentation. Returns4 faces or0. Input world coordinates are
 * original sprite state, NOT projected screen pixels. Caller keeps original
 * sprites, timing, damage and RNG untouched. No hidden allocations/state. */
unsigned PeBattleParticle_Build(int enabled, PeParticleWorldPoint anchor,
    unsigned age, unsigned duration, unsigned available_triangles,
    PeParticleFace out[4]);
#endif
