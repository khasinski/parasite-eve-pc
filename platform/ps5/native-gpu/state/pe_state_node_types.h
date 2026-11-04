#ifndef PE_STATE_NODE_TYPES_H
#define PE_STATE_NODE_TYPES_H
#include "pe1/script_node.h"
#include <stdint.h>
enum { PE_NODE_UNKNOWN=0, PE_NODE_COORDINATE=1, PE_NODE_ACTOR=2 };
void PeStateNode_Allocating(const ScriptNode *);
void PeStateNode_Allocated(const ScriptNode *);
void PeStateNode_Reset(void);
void PeStateNode_Tag(const ScriptNode *,unsigned);
unsigned PeStateNode_Type(const ScriptNode *);
#endif
