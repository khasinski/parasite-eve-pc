#include "pe_state_files.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#define NAME_BYTES 34u
static unsigned sequence;
static uint32_t crc(const unsigned char *p,size_t n){uint32_t c=~0u;for(size_t i=0;i<n;i++){c^=p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(int32_t)(c&1));}return~c;}
static void put(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static uint32_t get(const unsigned char *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int root_valid(const char *r){if(!r||!*r||strlen(r)>250)return 0;const char *part=r;for(const char *p=r;;p++){if(*p=='/'||*p=='\\'||!*p){size_t n=p-part;if((n==1&&part[0]=='.')||(n==2&&part[0]=='.'&&part[1]=='.'))return 0;part=p+1;if(!*p)break;}}return r[0]=='/'||(r[0]&&r[1]==':'&&(r[2]=='/'||r[2]=='\\'));}
static int name_valid(const char *n){if(!n||strlen(n)!=NAME_BYTES||memcmp(n,"pe-state-",9)||n[25]!='-')return 0;for(unsigned i=9;i<34;i++){if(i==25)continue;if(!((n[i]>='0'&&n[i]<='9')||(n[i]>='a'&&n[i]<='f')))return 0;}return 1;}
static int path(char out[320],const char *root,const char *name,const char *suffix){return root_valid(root)&&name_valid(name)&&snprintf(out,320,"%s/%s%s",root,name,suffix)>0&&strlen(out)<319;}
static int write_all(int fd,const unsigned char *p,size_t n){while(n){ssize_t done=write(fd,p,n);if(done<0&&errno==EINTR)continue;if(done<=0)return 0;p+=done;n-=(size_t)done;}return 1;}
static int read_all(int fd,unsigned char *p,size_t n){while(n){ssize_t done=read(fd,p,n);if(done<0&&errno==EINTR)continue;if(done<=0)return 0;p+=done;n-=(size_t)done;}unsigned char extra;ssize_t done;do{done=read(fd,&extra,1);}while(done<0&&errno==EINTR);return done==0;}
static int safe_open(const char *p){
#ifdef O_NOFOLLOW
    int fd=open(p,O_RDONLY|O_NOFOLLOW);
#else
    return -1; /* Explicitly unsupported rather than following an unknown link. */
#endif
#ifdef O_NOFOLLOW
    if(fd<0)return -1;struct stat s;if(fstat(fd,&s)||!S_ISREG(s.st_mode)){close(fd);return -1;}return fd;
#endif
}
int PeStateFiles_WriteUnique(const char *root,const unsigned char *data,size_t bytes,const PeStateIdentity *identity,char *name,size_t cap){PeStateInfo info;if(!root_valid(root)||!data||!name||cap<=NAME_BYTES||PeState_Inspect(data,bytes,identity,&info)||info.kind!=PE_STATE_PARTIAL)return PE_STATE_INVALID;name[0]=0;if(mkdir(root,0700)&&errno!=EEXIST)return PE_STATE_INCOMPLETE;time_t now=time(0);if(now==(time_t)-1)return PE_STATE_INCOMPLETE;for(unsigned attempt=0;attempt<64;attempt++){char id[35],payload[320],commit[320];snprintf(id,sizeof id,"pe-state-%016llx-%08x",(unsigned long long)(uint64_t)now,++sequence);if(!path(payload,root,id,".data")||!path(commit,root,id,".commit"))return PE_STATE_INVALID;int fd=open(payload,O_CREAT|O_EXCL|O_WRONLY,0600);if(fd<0){if(errno==EEXIST)continue;return PE_STATE_INCOMPLETE;}int ok=write_all(fd,data,bytes)&&fsync(fd)==0;if(close(fd))ok=0;if(!ok){unlink(payload);return PE_STATE_INCOMPLETE;}unsigned char marker[16];memcpy(marker,"PECOM01!",8);put(marker+8,(uint32_t)bytes);put(marker+12,crc(data,bytes));fd=open(commit,O_CREAT|O_EXCL|O_WRONLY,0600);if(fd<0){int collision=errno==EEXIST;unlink(payload);if(collision)continue;return PE_STATE_INCOMPLETE;}ok=write_all(fd,marker,sizeof marker)&&fsync(fd)==0;if(close(fd))ok=0;if(!ok){unlink(commit);unlink(payload);return PE_STATE_INCOMPLETE;}memcpy(name,id,sizeof id);return PE_STATE_OK;}return PE_STATE_BOUNDS;}
int PeStateFiles_Read(const char *root,const char *name,unsigned char *out,size_t cap,size_t *used,const PeStateIdentity *identity){if(used)*used=0;char payload[320],commit[320];if(!out||!used||!path(payload,root,name,".data")||!path(commit,root,name,".commit"))return PE_STATE_INVALID;int fd=safe_open(commit);if(fd<0)return PE_STATE_INCOMPLETE;unsigned char marker[16];int ok=read_all(fd,marker,sizeof marker);if(close(fd))ok=0;if(!ok||memcmp(marker,"PECOM01!",8))return PE_STATE_INVALID;size_t bytes=get(marker+8);if(bytes<PE_STATE_HEADER_BYTES||bytes>PE_STATE_MAX_BYTES||bytes>cap)return PE_STATE_BOUNDS;fd=safe_open(payload);if(fd<0)return PE_STATE_INCOMPLETE;ok=read_all(fd,out,bytes);if(close(fd))ok=0;if(!ok||crc(out,bytes)!=get(marker+12))return PE_STATE_CHECKSUM;PeStateInfo info;int result=PeState_Inspect(out,bytes,identity,&info);if(result)return result;if(info.kind!=PE_STATE_PARTIAL)return PE_STATE_INCOMPATIBLE;*used=bytes;return PE_STATE_OK;}
