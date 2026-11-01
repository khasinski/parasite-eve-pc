#ifndef PE1_PARTICLE_AUGMENT_H
#define PE1_PARTICLE_AUGMENT_H
struct RenderBouncingSprite;
struct RenderNativeTag;
enum { PE_PARTICLE_ORIGINAL=0, PE_PARTICLE_HYBRID=1, PE_PARTICLE_GEOMETRY=2 };
void FieldFx_SetParticleAugment(int enabled);
int FieldFx_GetParticleAugment(void);
void FieldFx_ParticleBeginFrame(void);
void FieldFx_ParticleEndFrame(void);
void FieldFx_QueueFallingParticle(const struct RenderBouncingSprite *state,unsigned age);
void FieldFx_FlushParticleAugment(void);
/* Additional authentic phase2 callbacks; scalar fields, no layout alias. */
void FieldFx_QueueDampedSpark(int x,int y,int z,unsigned age,unsigned variant);
void FieldFx_QueueRisingSpark(int x,int y,int z,unsigned age,unsigned r,unsigned g,unsigned b);
/* Explicit room-world fields: no cast between unrelated particle layouts. */
void FieldFx_QueueBurstOrbDebris(int x,int y,int z,unsigned age,unsigned state);
void FieldFx_BeginFallingOriginal(void);
void FieldFx_EndFallingOriginal(void);
void FieldFx_LinkFallingOriginal(struct RenderNativeTag *entry,struct RenderNativeTag *primitive);
/* Hybrid-only world flame accent; never captures/suppresses the original. */
void FieldFx_BeginEmberGlow(int x,int y,int z,unsigned age);
void FieldFx_EndEmberGlow(void);
/* Actual M005 program117 linked billboard; no floor inference. */
void FieldFx_BeginCrossFlareGlow(int x,int y,int z,unsigned age,unsigned joint);
void FieldFx_EndCrossFlareGlow(void);
/* Authored P115 controller / P118 JointGlow additive billboard only. */
void FieldFx_BeginAuthoredFlareGlow(unsigned program,int x,int y,int z,unsigned age,unsigned joint);
void FieldFx_EndAuthoredFlareGlow(void);
/* Only wraps the authored BurstOrb state0 floor quad; Hybrid additive accent. */
void FieldFx_BeginBurstGroundGlow(int x,int y,int z);
void FieldFx_EndBurstGroundGlow(void);
/* Checkpoint ownership: completed draw queues are derived, mode is exact. */
int FieldFx_ParticleStateIdle(void);
int FieldFx_ParticleStateWalk(const unsigned char *in,unsigned char *out);
/* Optional source-known particle point-light gain on live actor model colours. */
void FieldFx_SetParticleLighting(int mode);
int FieldFx_GetParticleLighting(void);
int FieldFx_ParticleLightStateIdle(void);
void FieldFx_ApplyParticleLights(void);
void FieldFx_RestoreParticleLights(void);


#endif
