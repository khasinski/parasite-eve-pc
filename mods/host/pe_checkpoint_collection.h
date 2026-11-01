/* Source-bound checkpoint owners, no account-profile file writes. */
#ifndef PE_CHECKPOINT_COLLECTION_H
#define PE_CHECKPOINT_COLLECTION_H
#include "pe_checkpoint_wire.h"
#include <string.h>
#define PE_COLLECTION_STATE_BYTES 1024u
int pe_profile_checkpoint_walk(const unsigned char*,unsigned char*,int);
int pe_bestiary_checkpoint_walk(const unsigned char*,unsigned char*,int);
int pe_wardrobe_checkpoint_walk(const unsigned char*,unsigned char*,int);
int pe_skin_checkpoint_walk(const unsigned char*,unsigned char*,int);
int pe_bestiary_menu_checkpoint_walk(const unsigned char*,unsigned char*,int);
int pe_wardrobe_menu_checkpoint_walk(const unsigned char*,unsigned char*,int);
const char*pe_bestiary_menu_checkpoint_blocker(void);
const char*pe_collection_checkpoint_blocker(void);
int pe_collection_checkpoint_ready(void);
int pe_collection_checkpoint_walk(const unsigned char*,unsigned char*,size_t,int);
#endif
