#include "pe_state_node_types.h"
#include <string.h>
static struct { unsigned short seq; unsigned char kind,pending; } tags[72];
static unsigned slot(const ScriptNode *n){uintptr_t a=(uintptr_t)n,b=(uintptr_t)g_ScriptNodes;return a>=b&&a-b<sizeof g_ScriptNodes&&(a-b)%sizeof *n==0?(unsigned)((a-b)/sizeof *n):72;}
void PeStateNode_Reset(void){memset(tags,0,sizeof tags);}
void PeStateNode_Tag(const ScriptNode *n,unsigned kind){unsigned i=slot(n);if(i<72){tags[i].seq=n->seq;tags[i].kind=kind<=PE_NODE_ACTOR?kind:PE_NODE_UNKNOWN;tags[i].pending=tags[i].kind;}}
unsigned PeStateNode_Type(const ScriptNode *n){unsigned i=slot(n);return i<72&&tags[i].seq==n->seq?tags[i].kind:PE_NODE_UNKNOWN;}

/* AllocNode retains target18 storage. Preserve its proven last producer kind,
 * then stamp the new node generation; never infer coordinate vs pointer bits. */
void PeStateNode_Allocating(const ScriptNode *n){unsigned i=slot(n);if(i<72)tags[i].pending=tags[i].seq==n->seq?tags[i].kind:PE_NODE_UNKNOWN;}
void PeStateNode_Allocated(const ScriptNode *n){unsigned i=slot(n);if(i<72){tags[i].seq=n->seq;tags[i].kind=tags[i].pending;}}
