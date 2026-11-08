#ifndef SCENE_E22_SHARED_H
#define SCENE_E22_SHARED_H

#include "common.h"
#include "pe1/gte_short_vector.h"

typedef struct SceneE22CallbackRecord {
    char pad00[0xC];
    void (*callback)(void *owner);
} SceneE22CallbackRecord;

/* Host layouts: the pointer follows the 0xC-byte pad at pointer alignment
 * (0xC on 32-bit hosts, as on the PS1; 0x10 on 64-bit hosts). */
#define SCENE_E22_PTR_ALIGN(n) (((n) + sizeof(void *) - 1) / sizeof(void *) * sizeof(void *))

PE1_STATIC_ASSERT(PE1_OFFSETOF(SceneE22CallbackRecord, callback) == SCENE_E22_PTR_ALIGN(0xC),
                  scene_e22_callback_record_callback_offset);
PE1_STATIC_ASSERT(sizeof(SceneE22CallbackRecord) == SCENE_E22_PTR_ALIGN(0xC) + sizeof(void *),
                  scene_e22_callback_record_size);

typedef struct SceneE22RangeCallbackState {
    char pad00[8];
    u8 *data;
    void (*callback)(void *owner);
    char pad10[6];
    s8 state_id;
    s8 range_value;
} SceneE22RangeCallbackState;

PE1_STATIC_ASSERT(PE1_OFFSETOF(SceneE22RangeCallbackState, data) == 8,
                  scene_e22_range_callback_data_offset);
PE1_STATIC_ASSERT(PE1_OFFSETOF(SceneE22RangeCallbackState, callback) == 8 + sizeof(void *),
                  scene_e22_range_callback_callback_offset);
PE1_STATIC_ASSERT(PE1_OFFSETOF(SceneE22RangeCallbackState, state_id) == 8 + 2 * sizeof(void *) + 6,
                  scene_e22_range_callback_state_id_offset);
PE1_STATIC_ASSERT(PE1_OFFSETOF(SceneE22RangeCallbackState, range_value) == 8 + 2 * sizeof(void *) + 7,
                  scene_e22_range_callback_range_value_offset);
PE1_STATIC_ASSERT(sizeof(SceneE22RangeCallbackState) == SCENE_E22_PTR_ALIGN(8 + 2 * sizeof(void *) + 8),
                  scene_e22_range_callback_state_size);

extern struct FieldActor *g_PlayerEntity; /* the player actor (0x8009D254) */
extern GteShortVector g_SceneE22BurstAnchor;


#include "pe1/fieldfx_task.h"
extern FieldFxTaskProgram g_SceneE22LinkBeamProgramA;
extern FieldFxTaskProgram g_SceneE22RingBurstProgram;
extern FieldFxTaskProgram g_SceneE22LinkBeamProgramB;
extern FieldFxTaskProgram g_SceneE22EmberDriftProgram;
extern FieldFxTaskProgram g_SceneE22EmberColumnProgram;
extern FieldFxTaskProgram g_SceneE22SwirlBoltProgram;
void SceneE22_ResetModule(void);

#endif /* SCENE_E22_SHARED_H */
