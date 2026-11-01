/* SPDX-License-Identifier: MIT. Reviewed QOL private owners; no save callbacks. */
#ifndef PE_CHECKPOINT_QOL_H
#define PE_CHECKPOINT_QOL_H
#include <stddef.h>
#define PE_CHECKPOINT_QOL_BYTES 512u
int pe_qol_checkpoint_ready(void);
int pe_qol_checkpoint_walk(const unsigned char *,unsigned char *,size_t,int);
int pe_move_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_encounters_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_frequency_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_keys_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_sort_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_inventory_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_key_menu_checkpoint_ready(void);
void pe_key_menu_checkpoint_reset(void);
#endif
