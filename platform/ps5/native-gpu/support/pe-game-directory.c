/* Independent game-SDK ABI adapter. Native DIR stays opaque in the other TU.
 * Enumeration is read-only; no save data, mode bits or filenames are changed. */
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <SDL3/SDL_log.h>
/* stderr dup2 is refused by this title; use its existing captured SDL sink. */
static void pe_directory_log(const char *fmt,...) {
    static unsigned budget=96;
    if(!budget)return;
    --budget;
    int saved=errno;
    char text[1024];va_list ap;va_start(ap,fmt);vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);
    SDL_Log("%s",text);errno=saved;
}
void *pe_native_directory_open(const char *,int *);
int pe_native_directory_next(void *,char [256],unsigned *,uint32_t *,int *);
int pe_native_directory_close(void *,int *);
typedef struct { void *native; struct dirent entry; } PeGameDirectory;
DIR *pe_game_opendir(const char *path) {
    int error, prior=errno;
    PeGameDirectory *d=calloc(1,sizeof *d);
    if(!d){errno=ENOMEM;return NULL;}
    d->native=pe_native_directory_open(path,&error);
    if(!d->native){free(d);errno=error;return NULL;}
    errno=prior;return (DIR *)d;
}
struct dirent *pe_game_readdir(DIR *opaque) {
    PeGameDirectory *d=(PeGameDirectory *)opaque;
    unsigned type;uint32_t inode;int error,prior=errno;char name[256];
    if(!d){errno=EBADF;return NULL;}
    if(!pe_native_directory_next(d->native,name,&type,&inode,&error)){
        errno=error?error:prior;return NULL;
    }
    memset(&d->entry,0,sizeof d->entry);
    d->entry.d_fileno=inode;d->entry.d_type=(unsigned char)type;
    d->entry.d_namlen=(unsigned char)strlen(name);
    d->entry.d_reclen=sizeof d->entry;
    memcpy(d->entry.d_name,name,(size_t)d->entry.d_namlen+1);
    errno=prior;return &d->entry;
}
int __wrap_closedir(DIR *opaque) {
    PeGameDirectory *d=(PeGameDirectory *)opaque;int r,error,prior=errno;
    if(!d){errno=EBADF;return -1;}
    r=pe_native_directory_close(d->native,&error);free(d);
    errno=r<0?error:prior;return r;
}
/* libc's scandir calls its own refused opendir internally, beyond linker wrap.
 * Build its result from the same isolated stream instead of calling libc. */
int __wrap_scandir(const char *path,struct dirent ***out,
    int (*filter)(const struct dirent *),
    int (*compare)(const struct dirent **,const struct dirent **)) {
    DIR *d;struct dirent *e,**list=NULL,**grown;size_t count=0,capacity=0,seen=0,regular=0,unknown=0;int saved=0;
    pe_directory_log("[native directory] scandir entered path=%.700s",path?path:"(null)");
    if(!out){errno=EINVAL;return -1;}*out=NULL;
    if(!(d=pe_game_opendir(path))){
        int error=errno;
        pe_directory_log("[native directory] scandir open failed path=%s errno=%d\n",path?path:"(null)",error);
        errno=error;return -1;
    }
    for(;;){
        errno=0;e=pe_game_readdir(d);
        if(!e){saved=errno;break;}
        ++seen;if(e->d_type==DT_REG)++regular;else if(e->d_type==DT_UNKNOWN)++unknown;
        if(filter&&!filter(e))continue;
        if(count==INT_MAX){saved=EOVERFLOW;break;}
        if(count==capacity){
            size_t next=capacity?capacity*2:16;
            if(next>SIZE_MAX/sizeof *list){saved=ENOMEM;break;}
            grown=realloc(list,next*sizeof *list);
            if(!grown){saved=ENOMEM;break;}list=grown;capacity=next;
        }
        list[count]=malloc(sizeof **list);
        if(!list[count]){saved=ENOMEM;break;}
        memcpy(list[count++],e,sizeof **list);
    }
    if(__wrap_closedir(d)<0&&!saved)saved=errno;
    if(saved){while(count)free(list[--count]);free(list);pe_directory_log("[native directory] scandir read failed path=%s errno=%d\n",path,saved);errno=saved;return -1;}
    /* Stable bounded O(n log n) sorting; exact callback type, no qsort cast or
     * global comparator state shared between directory readers. */
    if(compare && count>1){
        struct dirent **work=malloc(count*sizeof *work);
        if(!work){while(count)free(list[--count]);free(list);errno=ENOMEM;return -1;}
        for(size_t width=1;width<count;width*=2){
            for(size_t start=0;start<count;start+=2*width){
                size_t mid=start+width<count?start+width:count;
                size_t end=mid+width<count?mid+width:count,a=start,b=mid,k=start;
                while(a<mid&&b<end){
                    const struct dirent *left=list[a],*right=list[b];
                    if(compare(&left,&right)<=0)work[k++]=list[a++];
                    else work[k++]=list[b++];
                }
                while(a<mid)work[k++]=list[a++];
                while(b<end)work[k++]=list[b++];
            }
            memcpy(list,work,count*sizeof *list);
        }
        free(work);
    }
    *out=list;
    pe_directory_log("[native directory] scandir path=%s entries=%zu seen=%zu regular=%zu unknown_type=%zu provider=ps5platform-getdents copied-game-ABI\n",path,count,seen,regular,unknown);
    return (int)count;
}
