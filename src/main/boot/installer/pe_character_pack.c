/* SPDX-License-Identifier: MIT. Original validator for the existing PEDUCK01 format. */
#include "pe_character_pack.h"
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ENTRY_BYTES 282u
#define MAX_ENTRIES 512u
#define IMAGE_LIMIT (4u*1024u*1024u)
#define PACK_LIMIT (40u*1024u*1024u) /* Separate cumulative encoded/decoded work caps; NOT resident GPU cache. */
static unsigned u16(const unsigned char*p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t u32(const unsigned char*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t be32(const unsigned char*p){return(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int reject(PeCharacterPackReport*r,const char*why){SDL_strlcpy(r->error,why,sizeof r->error);SDL_Log("[Character pack] validation=failed reason=%s",why);return 0;}
static int join(char out[4096],const char*root,const char*leaf){int n=snprintf(out,4096,"%s/%s",root,leaf);return n>0&&n<4096;}
static int exact(SDL_IOStream*io,void*data,size_t bytes){size_t at=0;while(at<bytes){size_t n=SDL_ReadIO(io,(unsigned char*)data+at,bytes-at);if(!n)return 0;at+=n;}return 1;}
static int header(const unsigned char h[16]){unsigned n=u32(h+8);return !memcmp(h,"PEDUCK01",8)&&n&&n<=MAX_ENTRIES&&u16(h+12)==ENTRY_BYTES&&!h[14]&&!h[15];}
int pe_install_character_pack_hint(const char*root){char path[4096];unsigned char h[16];if(!root||!join(path,root,"model-index.bin"))return 0;SDL_IOStream*f=SDL_IOFromFile(path,"rb");if(!f)return 0;int ok=exact(f,h,sizeof h)&&header(h);if(!SDL_CloseIO(f))ok=0;return ok;}
static int filename(const unsigned char*e){const char*n=(const char*)e+26;const char*end=memchr(n,0,256);if(!end||end==n||end-n<5||strcmp(end-4,".png")||strstr(n,".."))return 0;for(const unsigned char*p=(const unsigned char*)n;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='_'||*p=='.'))return 0;return 1;}
int pe_install_verify_character_pack(const char*root,int(*pump)(const char*,uint64_t,uint64_t,void*),void*user,PeCharacterPackReport*r){
 PeCharacterPackReport local;if(!r)r=&local;memset(r,0,sizeof*r);char path[4096];unsigned char h[16];unsigned char*index=NULL,*encoded=NULL;SDL_IOStream*f=NULL;SDL_Surface*image=NULL;int ok=0;unsigned count=0;
 if(!root||strlen(root)>3900||!join(path,root,"model-index.bin"))return reject(r,"Character folder path is too long.");
 f=SDL_IOFromFile(path,"rb");if(!f)return reject(r,"Cannot open model-index.bin; choose the prepared native character subset or Original.");
 if(!exact(f,h,sizeof h)||!header(h)){reject(r,"Unsupported character index; use a prepared native subset of at most512 images.");goto done;}
 count=u32(h+8);index=SDL_malloc((size_t)count*ENTRY_BYTES);if(!index){reject(r,"Not enough memory for the bounded character index.");goto done;}
 if(!exact(f,index,(size_t)count*ENTRY_BYTES)){reject(r,"Character index is truncated or unreadable.");goto done;}
 unsigned char extra;if(SDL_ReadIO(f,&extra,1)||SDL_GetIOStatus(f)!=SDL_IO_STATUS_EOF){reject(r,"Character index has extra data or a read error.");goto done;}
 if(!SDL_CloseIO(f)){f=NULL;reject(r,"Character index close failed.");goto done;}f=NULL;
 for(unsigned i=0;i<count;i++){
  unsigned char*e=index+(size_t)i*ENTRY_BYTES;unsigned factor=e[0]?2:4,x=u16(e+2),y=u16(e+4),w=u16(e+6),height=u16(e+8);
  if(e[0]>1||e[1]>=(e[0]?256:16)||!w||!height||x+w>256||y+height>256||x%factor||w%factor||!filename(e)){reject(r,"Character index contains an invalid image name or source region.");goto done;}
  for(unsigned j=0;j<i;j++)if(!SDL_strcasecmp((char*)index+(size_t)j*ENTRY_BYTES+26,(char*)e+26)){reject(r,"Character index repeats an image name or case alias.");goto done;}
 }
 encoded=SDL_malloc(IMAGE_LIMIT);if(!encoded){reject(r,"Not enough memory for one bounded character image.");goto done;}
 for(unsigned i=0;i<count;i++){
  if(pump&&!pump("Verifying optional character textures",i,count,user)){reject(r,"Character texture verification cancelled; files were not changed.");goto done;}
  if(!join(path,root,(char*)index+(size_t)i*ENTRY_BYTES+26)){reject(r,"Character image path is too long.");goto done;}
  f=SDL_IOFromFile(path,"rb");if(!f){reject(r,"A character image referenced by the index is missing or unreadable.");goto done;}
  if(!exact(f,encoded,33)||memcmp(encoded,"\x89PNG\r\n\x1a\n",8)||be32(encoded+8)!=13||memcmp(encoded+12,"IHDR",4)||!be32(encoded+16)||!be32(encoded+20)||be32(encoded+16)>2048||be32(encoded+20)>1024){reject(r,"Character image must be a complete bounded PNG (up to2048x1024).");goto done;}
  size_t n=33;while(n<IMAGE_LIMIT){size_t q=SDL_ReadIO(f,encoded+n,IMAGE_LIMIT-n);if(!q)break;n+=q;}
  if(n==IMAGE_LIMIT){reject(r,"Character image exceeds the4MiB subset limit.");goto done;}
  if(SDL_GetIOStatus(f)!=SDL_IO_STATUS_EOF){reject(r,"Character image read failed.");goto done;}
  if(!SDL_CloseIO(f)){f=NULL;reject(r,"Character image close failed.");goto done;}f=NULL;
  if(r->encoded_bytes>PACK_LIMIT-n){reject(r,"Prepared character subset exceeds40MiB; select a smaller native subset.");goto done;}
  uint64_t decoded=(uint64_t)be32(encoded+16)*be32(encoded+20)*4u;
  if(decoded>IMAGE_LIMIT){reject(r,"A decoded character image exceeds4MiB; choose a smaller prepared image.");goto done;}
  if(decoded>PACK_LIMIT-r->decoded_bytes){reject(r,"Character subset exceeds40MiB cumulative decoded validation work; choose a smaller subset.");goto done;}
  SDL_IOStream*memory=SDL_IOFromConstMem(encoded,n);image=memory?SDL_LoadPNG_IO(memory,true):NULL;
  const unsigned char*entry=index+(size_t)i*ENTRY_BYTES;unsigned rw=u16(entry+6),rh=u16(entry+8);
  if(!image||image->w<=0||image->h<=0||(unsigned)image->w<rw||(unsigned)image->h<rh||image->w>2048||image->h>1024||(unsigned)image->w%rw||(unsigned)image->h%rh||(unsigned)image->w/rw!=(unsigned)image->h/rh||(unsigned)image->w/rw>8||(uint64_t)image->w*(uint64_t)image->h*4u!=decoded){reject(r,"Character PNG must match its indexed region at an equal integer scale from1x to8x.");goto done;}
  SDL_DestroySurface(image);image=NULL;r->encoded_bytes+=n;r->decoded_bytes+=decoded;r->images++;
 }
 if(pump&&!pump("Character textures verified",count,count,user)){reject(r,"Character texture verification cancelled.");goto done;}
 SDL_Log("[Character pack] validation=ready images=%u encoded_bytes=%llu decoded_work_bytes=%llu authenticity=not-certified render_hits=unobserved",r->images,(unsigned long long)r->encoded_bytes,(unsigned long long)r->decoded_bytes);ok=1;
 done:if(f)SDL_CloseIO(f);if(image)SDL_DestroySurface(image);SDL_free(encoded);SDL_free(index);return ok;
}
