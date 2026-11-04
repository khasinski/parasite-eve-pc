#ifndef PE_STATE_FIELD_MODELS_H
#define PE_STATE_FIELD_MODELS_H
#include <stddef.h>
#include <stdint.h>
#define PE_STATE_FIELD_MODEL_BYTES 6144u
#define PE_STATE_FIELD_MODELS_BYTES (128u+4u*256u+3u*(64u+PE_STATE_FIELD_MODEL_BYTES))
/* Pure bounded codec: mutates only original UV offsets/words and colour words.
 * Immutable model topology/vertex identity is compared before any write. */
int PeFieldModel_Capture(const void *,size_t,unsigned char *,size_t);
int PeFieldModel_Validate(const void *,size_t,const unsigned char *,size_t);
int PeFieldModel_Restore(void *,size_t,const unsigned char *,size_t);
int PeFieldModels_Walk(const unsigned char *,unsigned char *,int);
int PeFieldModels_Ready(unsigned,int);
void *PeFieldModels_KeyPointer(uint32_t);
#endif


