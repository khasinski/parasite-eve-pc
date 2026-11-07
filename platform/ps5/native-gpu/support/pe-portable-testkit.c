/* SPDX-License-Identifier: MIT
 * Copies one explicitly supplied authentic test save unchanged. Never probes
 * other slots, truncates an existing file, migrates console cards or deletes. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "pe-portable-testkit.h"
#include "installer/pe_pack_check.h"
extern int sceKernelOpen(const char *,int,unsigned);
extern long sceKernelRead(int,void *,size_t);
extern long sceKernelWrite(int,const void *,size_t);
extern int sceKernelFsync(int);
extern int sceKernelClose(int);
int PePortable_SeedTestCard(const char *source,const char *destination){
#ifndef PE_NATIVE_TESTKIT_SHA256
 /* No private test save or fingerprint is published. An absent optional
  * source reports absent; an unconfigured present source fails closed. */
 int input=sceKernelOpen(source,0,0);
 if(input<0)return input==-2||(unsigned)input==0x80020002u?0:-1;
 (void)sceKernelClose(input);return -1;
#else
 /* Caller supplies a 32-byte initializer for their own authorized seed. */
 static const unsigned char expected[32]=PE_NATIVE_TESTKIT_SHA256;
 int input=sceKernelOpen(source,0,0);if(input<0)return input==-2||(unsigned)input==0x80020002u?0:-1;
 unsigned char *data=malloc(8193);if(!data){sceKernelClose(input);return -1;}size_t at=0;int ok=1;
 while(at<8193){long n=sceKernelRead(input,data+at,8193-at);if(n<0||(size_t)n>8193-at){ok=0;break;}if(!n)break;at+=(size_t)n;}
 if(sceKernelClose(input)<0)ok=0;if(!ok||at!=8192){free(data);return -1;}
 PeSha256 sha;unsigned char digest[32];pe_sha256_init(&sha);pe_sha256_update(&sha,data,8192);pe_sha256_final(&sha,digest);if(memcmp(digest,expected,32)){free(data);return -1;}
 /* WRONLY|CREAT|EXCL, no TRUNC and no append to an existing card. */
 int output=sceKernelOpen(destination,0xa01,0666);
 if(output<0){free(data);return output==-17||(unsigned)output==0x80020011u?2:-1;}
 at=0;while(at<8192){long n=sceKernelWrite(output,data+at,8192-at);if(n<=0||(size_t)n>8192-at){ok=0;break;}at+=(size_t)n;}
 if(sceKernelFsync(output)<0)ok=0;if(sceKernelClose(output)<0)ok=0;free(data);
 /* On failure a newly created partial file may remain. Never delete a path
  * that another process/user could have replaced; report failure honestly. */
 return ok&&at==8192?1:-1;
#endif
}
