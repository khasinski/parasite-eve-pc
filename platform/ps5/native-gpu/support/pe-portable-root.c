/* SPDX-License-Identifier: MIT. Selected homebrew app and sibling asset storage.
 * Raw public kernel APIs only, including before CRT. No FILE/stat layout,
 * heap, TLS, cwd, symlink or build-token dependency. */
#include <stddef.h>
#include "pe-portable-root.h"
extern int sceKernelOpen(const char *,int,unsigned);
extern long sceKernelRead(int,void *,size_t);
extern int sceKernelClose(int);
static int initialized,mode,error,asset_state;
static char root[32],app[160],user[180],storage[16];
static unsigned io_budget;
static unsigned char compare_a[4096],compare_b[4096];
static size_t len(const char*s){size_t n=0;while(s[n])n++;return n;}
static int append(char*d,size_t cap,const char*a,const char*b){size_t n=len(a),m=len(b);if(n+m>=cap)return 0;for(size_t i=0;i<n;i++)d[i]=a[i];for(size_t i=0;i<=m;i++)d[n+i]=b[i];return 1;}
static int missing(int r){return r==-2||(unsigned)r==0x80020002u;}
static int read_file(const char*path,char*data,size_t cap){int f=sceKernelOpen(path,0,0);if(f<0)return missing(f)?-2:-1;size_t n=0;int ok=1;while(n<cap){long got=sceKernelRead(f,data+n,cap-n);if(got<0||(size_t)got>cap-n){ok=0;break;}if(!got)break;n+=(size_t)got;}if(sceKernelClose(f)<0)ok=0;return ok?(int)n:-1;}
/* Exact identity, not a filename/title-ID guess. At most64MiB per binary and
 *256MiB total reads. Partial reads are accumulated; every descriptor closes. */
static int chunk(int f,unsigned char*out){size_t n=0;while(n<4096){if(!io_budget)return -1;size_t ask=4096-n;if(ask>io_budget)ask=io_budget;long got=sceKernelRead(f,out+n,ask);if(got<0||(size_t)got>ask)return -1;if(!got)break;io_budget-=(unsigned)got;n+=(size_t)got;}return(int)n;}
static int same_app(const char*path){
 int candidate=sceKernelOpen(path,0,0);if(candidate<0)return missing(candidate)?0:-1;
 int active=sceKernelOpen("/app0/eboot.bin",0,0);if(active<0){sceKernelClose(candidate);return -1;}
 int result=0;unsigned bytes=0;
 for(;;){int a=chunk(active,compare_a),b=chunk(candidate,compare_b);if(a<0||b<0){result=-1;break;}if(a!=b)break;if(!a){result=bytes>=4096?1:-1;break;}int equal=1;for(int i=0;i<a;i++)if(compare_a[i]!=compare_b[i]){equal=0;break;}if(!equal)break;bytes+=(unsigned)a;if(bytes>64u*1024u*1024u){result=-1;break;}}
 if(sceKernelClose(active)<0)result=-1;if(sceKernelClose(candidate)<0)result=-1;return result;
}
/* Primary role: app in storage/homebrew/PS5GameTest, assets beside homebrew.
 * Identity is exact and unique; neither a USB number nor a marker is assumed. */
static int disc_root(const char*base){char path[96];for(unsigned i=0;i<2;i++){if(!append(path,sizeof path,base,i?"/discs/disc2.bin":"/discs/disc1.bin"))return 0;int f=sceKernelOpen(path,0,0);if(f<0)return 0;if(sceKernelClose(f)<0)return 0;}return 1;}
int PePortable_Mode(void){
 if(initialized)return mode;initialized=1;
#ifndef PE_NATIVE_SHARE_LAYOUT
 error=5;mode=-1;return mode;
#else
 io_budget=256u*1024u*1024u;unsigned matches=0;char candidate[160],binary[180];
 append(candidate,sizeof candidate,"/data/homebrew/PS5GameTest","");append(binary,sizeof binary,candidate,"/eboot.bin");int found=same_app(binary);if(found<0){error=5;mode=-1;return mode;}if(found){matches++;append(app,sizeof app,candidate,"");append(storage,sizeof storage,"/data","");}
 for(unsigned i=0;i<8;i++){char mount[]="/mnt/usb0";mount[8]=(char)('0'+i);
  if(!append(candidate,sizeof candidate,mount,"/homebrew/PS5GameTest")||!append(binary,sizeof binary,candidate,"/eboot.bin")){error=4;mode=-1;return mode;}
  found=same_app(binary);if(found<0){error=5;mode=-1;return mode;}if(found){if(++matches>1){error=3;mode=-1;return mode;}append(app,sizeof app,candidate,"");append(storage,sizeof storage,mount,"");}
 }
 if(matches!=1){error=2;mode=-1;return mode;}
 if(!append(root,sizeof root,storage,"/parasite-eve")||!append(user,sizeof user,app,"/user")){error=4;mode=-1;return mode;}
 /* A same-USB complete disc root is authoritative. An internal app may
  * use a unique USB disc root, even when legacy internal data also exists.
  * Only required disc files are probed: optional HD packs stay unprobed. */
 if(storage[1]=='m'&&disc_root(root))asset_state=1;
 else{unsigned roots=0;char selected[32];for(unsigned i=0;i<8;i++){char mount[]="/mnt/usb0",base[32];mount[8]=(char)('0'+i);append(base,sizeof base,mount,"/parasite-eve");if(disc_root(base)){roots++;append(selected,sizeof selected,base,"");}}
  if(roots==1){append(root,sizeof root,selected,"");asset_state=1;}
  else if(roots>1)asset_state=2; /* No arbitrary first mount. Explicit paths may still be valid. */
  else if(disc_root("/data/parasite-eve")){append(root,sizeof root,"/data/parasite-eve","");asset_state=1;}
 }

 mode=1;return mode;
#endif
}
int PePortable_Error(void){(void)PePortable_Mode();return error;}
const char*PePortable_Root(void){return PePortable_Mode()==1?root:NULL;}
const char*PePortable_AppRoot(void){return PePortable_Mode()==1?app:NULL;}
const char*PePortable_UserRoot(void){return PePortable_Mode()==1?user:NULL;}
const char*PePortable_StorageRoot(void){return PePortable_Mode()==1?storage:NULL;}
int PePortable_AssetsState(void){(void)PePortable_Mode();return asset_state;}
int PePortable_ImportConfig(char *path,unsigned cap){
 if(PePortable_Mode()!=1||!path||cap<2)return 0;
 char file[200];append(file,sizeof file,app,"/use-existing-config.txt");int n=read_file(file,path,cap-1);
 if(n==-2)return 0;if(n<=0||(unsigned)n>=cap-1)return -1;
 while(n&&(path[n-1]=='\n'||path[n-1]=='\r'))n--;path[n]=0;
 if(path[0]!='/')return -1;
 for(int i=0;i<n;i++){unsigned char ch=(unsigned char)path[i];if(ch<32||ch==127||ch=='\\'||ch==':')return -1;if(ch=='/'&&path[i+1]=='.'&&(path[i+2]==0||path[i+2]=='/'||(path[i+2]=='.'&&(path[i+3]==0||path[i+3]=='/'))))return -1;}
 return 1;
}
