#ifndef PE_STATE_THEATER_TASKS_H
#define PE_STATE_THEATER_TASKS_H
#include <stddef.h>
#include <stdint.h>
#include "pe1/fieldfx_task.h"
/* Typed prototype for one P117 context, not a raw work-area image.
 * Caller owns PM headers, actor role and whole-registry prepare/rollback. */
#define PE_STATE_TASK_CONTEXT_BYTES 16384u
int PeStateTaskContext_Capture(const FieldFxTaskContext *,unsigned char *,size_t);
int PeStateTaskContext_Validate(const unsigned char *,size_t);
int PeStateTaskContext_Restore(FieldFxTaskContext *,const unsigned char *,size_t);

#include "pe_state_world.h"
#define PE_STATE_TASKS_BYTES (320u+11u*(16u+PE_STATE_TASK_CONTEXT_BYTES))
int PeStateTasks_HeaderSupported(unsigned,unsigned,unsigned);
int PeStateTasks_CurrentSupported(unsigned);
int PeStateTasks_WalkForWorld(const PeWorldState *,const unsigned char *,const unsigned char *,const unsigned char *,unsigned char *,int);


int PeStateTasks_Constructed(const PeWorldState *,const unsigned char *,const unsigned char *);
int PeStateTaskContext_ModelRequired(const unsigned char *,size_t);
void PeStateTasks_ResetBlocker(void);
const char *PeStateTasks_Blocker(void);
#endif

