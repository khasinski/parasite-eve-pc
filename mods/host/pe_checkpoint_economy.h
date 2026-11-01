/* SPDX-License-Identifier: MIT. Source-bound Economy checkpoint owners. */
#ifndef PE_CHECKPOINT_ECONOMY_H
#define PE_CHECKPOINT_ECONOMY_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define PE_ECONOMY_STATE_BYTES 4096u
static uint32_t pe_econ_word(const unsigned char*p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int pe_loot_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_catalog_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_requisition_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_refund_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_junk_counter_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_catalog_pages_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_dismantle_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_requisition_menu_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_catalog_menu_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_wayne_menu_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_field_wayne_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_smoke_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_discard_checkpoint_walk(const unsigned char *,unsigned char *,int);
int pe_economy_checkpoint_ready(void);
int pe_economy_checkpoint_walk(const unsigned char *,unsigned char *,size_t,int);
#endif
