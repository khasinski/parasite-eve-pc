/* SPDX-License-Identifier: MIT -- original implementation of standard SHA256. */
#include "pe_pack_check.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
static uint32_t rotr(uint32_t x,unsigned n){return(x>>n)|(x<<(32-n));}
static void compress(PeSha256*s,const unsigned char*p){
    static const uint32_t k[64]={
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64];for(int i=0;i<16;i++)w[i]=(uint32_t)p[i*4]<<24|(uint32_t)p[i*4+1]<<16|(uint32_t)p[i*4+2]<<8|p[i*4+3];
    for(int i=16;i<64;i++){uint32_t a=w[i-15],b=w[i-2];w[i]=(rotr(a,7)^rotr(a,18)^(a>>3))+w[i-16]+(rotr(b,17)^rotr(b,19)^(b>>10))+w[i-7];}
    uint32_t a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
    for(int i=0;i<64;i++){uint32_t t=h+(rotr(e,6)^rotr(e,11)^rotr(e,25))+((e&f)^(~e&g))+k[i]+w[i];uint32_t u=(rotr(a,2)^rotr(a,13)^rotr(a,22))+((a&b)^(a&c)^(b&c));h=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
void pe_sha256_init(PeSha256*s){static const uint32_t h[]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};memset(s,0,sizeof(*s));memcpy(s->h,h,sizeof(h));}
void pe_sha256_update(PeSha256*s,const void*data,size_t n){const unsigned char*p=data;s->bits+=(uint64_t)n*8;while(n){size_t q=64-s->used;if(q>n)q=n;memcpy(s->block+s->used,p,q);s->used+=(unsigned)q;p+=q;n-=q;if(s->used==64){compress(s,s->block);s->used=0;}}}
void pe_sha256_final(PeSha256*s,unsigned char out[32]){uint64_t bits=s->bits;unsigned char pad=128;pe_sha256_update(s,&pad,1);pad=0;while(s->used!=56)pe_sha256_update(s,&pad,1);unsigned char b[8];for(int i=0;i<8;i++)b[7-i]=(unsigned char)(bits>>(i*8));pe_sha256_update(s,b,8);for(int i=0;i<32;i++)out[i]=(unsigned char)(s->h[i/4]>>(24-(i%4)*8));}
#ifndef PE_SHA_ONLY
typedef struct{const char*name;uint64_t bytes;unsigned char sha256[32];}PePackFile;
#include "pe_native_pack_index.inc"
int pe_install_verify_native_pack(const char*root,int(*pump)(const char*,uint64_t,uint64_t,void*),void*user){
    if(!PE_NATIVE_PACK_INDEX_AVAILABLE)return 0; /* Public no-pack export fails closed. */
    unsigned char data[65536],digest[32];char path[4096];
    for(size_t i=0;i<sizeof(pe_pack_files)/sizeof(pe_pack_files[0]);i++){
        if(pump&&!pump("Verifying native HD files",i,sizeof(pe_pack_files)/sizeof(pe_pack_files[0]),user))return 0;
        int n=snprintf(path,sizeof(path),"%s/%s",root,pe_pack_files[i].name);if(n<0||(size_t)n>=sizeof(path))return 0;
        SDL_IOStream*f=SDL_IOFromFile(path,"rb");if(!f)return 0;
        if(SDL_GetIOSize(f)!=(Sint64)pe_pack_files[i].bytes){SDL_CloseIO(f);return 0;}
        PeSha256 sha;pe_sha256_init(&sha);uint64_t at=0;int ok=1;
        while(at<pe_pack_files[i].bytes){size_t bytes=pe_pack_files[i].bytes-at<sizeof(data)?(size_t)(pe_pack_files[i].bytes-at):sizeof(data);
            if(pump&&!pump("Verifying native HD files",i,sizeof(pe_pack_files)/sizeof(pe_pack_files[0]),user)){ok=0;break;}
            if(SDL_ReadIO(f,data,bytes)!=bytes){ok=0;break;}pe_sha256_update(&sha,data,bytes);at+=bytes;
        }
        if(!SDL_CloseIO(f))ok=0;if(!ok)return 0;pe_sha256_final(&sha,digest);if(memcmp(digest,pe_pack_files[i].sha256,32))return 0;
    }return 1;
}

#endif /* PE_SHA_ONLY */
