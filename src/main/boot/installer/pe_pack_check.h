/* SPDX-License-Identifier: MIT */
#ifndef PE_PACK_CHECK_H
#define PE_PACK_CHECK_H
#include <stddef.h>
#include <stdint.h>
typedef struct{uint32_t h[8];uint64_t bits;unsigned used;unsigned char block[64];}PeSha256;
void pe_sha256_init(PeSha256 *);
void pe_sha256_update(PeSha256 *,const void *,size_t);
void pe_sha256_final(PeSha256 *,unsigned char out[32]);
int pe_install_verify_native_pack(const char *,int(*)(const char *,uint64_t,uint64_t,void *),void *);
#endif
