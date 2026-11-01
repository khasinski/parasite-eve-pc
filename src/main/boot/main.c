#ifdef PE_PS5_GPU_NATIVE
#include "pe1/particle_augment.h"
#include "pe1/dialogue_alpha.h"
#include "pe1/planar_shadow.h"
#endif
#if defined(PE_PS5_GPU_NATIVE) && defined(PE_MODS)
#include "../../../mods/host/pe_mod_menu.h"
#include "../../../mods/host/pe_mod_host.h"
static int host_open_assists;
#endif
#include "common.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#if PE_NATIVE_STATE_CPU_COMPONENTS
static void Host_StateUiRegister(void);
static bool Host_StateChord(const SDL_Event *event);
#endif
#include <psyz/audio.h>
#include <psyz/gpu.h>
#include <psyz/overlay_sdl3.h>
#include <psyz/system.h>
#include <psyz/video.h>
#include "pe1/boot.h"
#include "pe1/boot_disc_check.h"
#include "pe1/boot_display.h"
#include "pe1/boot_services.h"
#include "pe1/asset_table.h"
#include "pe1/aya.h"
#include "pe1/crash_report.h"
#include "pe1/game_state_types.h"
#include "pe1/field_bg_load.h"
#include "pe1/game_state.h"
#include "pe1/game_timers.h"
#include "pe1/pad_state.h"
#include "pe1/psyq_gpu.h"
#include "pe1/save.h"
#include "pe1/save_tail_state.h"
#include "pe1/scene_dispatch.h"
#include "pe_hooks.h"
#ifdef PE_PS5_GPU_NATIVE
#include "pe1/model_fill_lighting.h"
#endif

#ifdef PE_PS5
/* Local staged PsyZ status; no upstream public ABI change. */
extern int Psyz_PS5PlatformReady(void);
extern const char *Psyz_PS5PlatformInitError(void);
#endif

/* Desktop preferences belong to the original boot module. The modal loop
 * never ticks the game, PS1 timers, scripts, or audio. */

static struct {
    unsigned scale;
    unsigned volume;
    int aspect;
    int dither;
    int geometry_correction;
    int frame_interpolation;
    int antialiasing;
    int model_filtering;
    int widescreen;
    int dynamic_lighting;
    int effect_geometry_correction;
#ifdef PE_PS5_GPU_NATIVE
    int fsr1,fsr1_sharpness,model_fill_lighting,movie_blur,movie_presentation,battle_debris_3d,gameplay_fill,movie_cleanup,sprite_filtering;
    int show_fps;
    int menu_style,menu_text_size,menu_pointer,hd_models,particle_lighting,dialogue_text,triangle_menu_font,player_shadow;
    char hd_model_directory[4096];
#endif
    int hd_backgrounds;
#ifdef PE_PS5
    char hd_title_directory[4096];
    int hd_field_materials;
    char hd_field_directory[4096];
#endif
    int fullscreen;
    /* 2..PSYZ_SPEED_MAX, used while the speed binding is held (or toggled on) */
    unsigned speed_factor;
    /* 1: the speed binding switches the speed-up on and off instead of holding it */
    int speed_toggle;
    unsigned analog_run_threshold;
    /* 16 pad buttons, then the speed hold (unbound by default) */
    int keys[17];
    int buttons[17];
    char discs[2][4096];
    char user_file[4096];
    int enabled;
} host_options;
#ifdef PE_PS5_GPU_NATIVE
#include "native_experience.h"
#endif

#ifdef PE_PS5
#ifndef PE_PS5_DATA_ROOT
#define PE_PS5_DATA_ROOT "/data/parasite-eve"
#endif
static char host_data_root[4096];

/* PS5 application launches cannot depend on changing the process cwd. */
static int Host_DataPath(char *out, size_t capacity, const char *path) {
    int n = snprintf(out, capacity, "%s%s%s", path[0] == '/' ? "" : host_data_root,
                     path[0] == '/' ? "" : "/", path);
    if (n < 0 || (size_t)n >= capacity) {
        if (capacity) out[0] = '\0';
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PS5 path too long: %.200s", path);
        return 0;
    }
    return 1;
}

static int Host_InitPaths(void) {
    SDL_Environment *env = SDL_GetEnvironment();
    const char *root = SDL_GetEnvironmentVariable(env, "PE_DATA_HOME");
    if (!root || !*root) root = PE_PS5_DATA_ROOT;
    if (root[0] != '/' || strlen(root) >= sizeof(host_data_root)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PS5 data root must be an absolute path: %.200s", root);
        return 0;
    }
    strcpy(host_data_root, root);
    size_t length = strlen(host_data_root);
    while (length > 1 && host_data_root[length - 1] == '/') host_data_root[--length] = '\0';
    const char *config = SDL_GetEnvironmentVariable(env, "PE_CONFIG_HOME");
    char directory[4096];
    if (!config || !*config) config = host_data_root;
    if (!Host_DataPath(directory, sizeof(directory), config)) return 0;
    int n = snprintf(host_options.user_file, sizeof(host_options.user_file),
                     "%s/parasite-eve.toml", directory);
    if (n < 0 || (size_t)n >= sizeof(host_options.user_file)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PS5 preferences path too long");
        return 0;
    }
    SDL_Log("PS5 paths data=%s preferences=%s", host_data_root, host_options.user_file);
    return 1;
}

static int Host_OpenDisc(PeDisc *disc, const char *path) {
    char absolute[8192];
    if (!Host_DataPath(absolute, sizeof(absolute), path)) return 0;
    SDL_Log("PS5 disc open path=%s", absolute);
    int ok = pe_disc_open(disc, absolute);
    if (!ok) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PS5 disc open failed: %s", absolute);
    return ok;
}
#else
#define Host_OpenDisc pe_disc_open
#endif

static int Host_SaveOptions(void);
#ifdef PE_PS5_GPU_NATIVE
static void Host_NativeApplicationShutdown(void);
#endif

#define HOST_BINDING_COUNT 17
#define HOST_SPEED_BINDING 16
#define HOST_ANALOG_RUN_ROW (HOST_BINDING_COUNT + 1)
#define HOST_SPEED_MODE_ROW (HOST_BINDING_COUNT + 2)
#define HOST_UNBOUND_KEY SDL_SCANCODE_UNKNOWN
#define HOST_UNBOUND_BUTTON -1

static const char *const host_button_names[HOST_BINDING_COUNT] = {
    "L2", "R2", "L1", "R1",
    "Triangle", "Circle", "Cross", "Square",
    "Select", "L3", "R3", "Start",
    "Up", "Right", "Down", "Left",
    "Speed"
};

/* speed_factor while the speed binding is held; every start is 1x. */
static unsigned host_speed = 1;

static void Host_SetSpeed(unsigned speed) {
    if (speed != host_speed || speed != Psyz_VideoGetSpeed()) {
        host_speed = speed;
        Psyz_VideoSetSpeed(speed);
    }
}

static int host_speed_key_held;
static struct { SDL_JoystickID id; int held; } host_speed_pads[16];
/* Toggle mode: the binding was down at the last event, and whether the speed-up is on. */
static int host_speed_was_held, host_speed_toggled;

static void Host_ResetSpeedHold(void) {
    host_speed_key_held = 0;
    host_speed_was_held = host_speed_toggled = 0;
    memset(host_speed_pads, 0, sizeof(host_speed_pads));
    Host_SetSpeed(1);
}

typedef enum HostSettingsPage {
#ifdef PE_PS5_GPU_NATIVE
    HOST_PAGE_GRAPHICS=0, HOST_PAGE_MODS=1, HOST_PAGE_CHEATS=2, HOST_PAGE_STATES=3,
    HOST_PAGE_AUDIO=4, HOST_PAGE_CONTROLS=5, HOST_PAGE_IMAGES=6
#else
    HOST_PAGE_GRAPHICS = 0,
    HOST_PAGE_CONTROLS = 1,
    HOST_PAGE_AUDIO = 2,
    HOST_PAGE_IMAGES = 3,
    HOST_PAGE_MODS = 4
#endif
} HostSettingsPage;

typedef enum HostBindDevice {
    HOST_BIND_KEYBOARD = 0,
    HOST_BIND_CONTROLLER = 1
} HostBindDevice;

#if PE_DESKTOP_MOD_DOWNLOADS
/* Versioned desktop background packs. Only a fully verified staging directory
 * can replace the active pointer; failed/cancelled downloads keep the old pack. */
/* curl imports WinSock and WinAPI; keep their historical typedefs separate
 * from the original PS1 types used throughout this translation unit. */
#ifdef _WIN32
#define RECT HostWinRect
#define byte HostWinByte
#define u_long HostWinULong
#ifdef __MINGW32__
/* MinGW deliberately undefines u_long in _bsd_types.h. Supply its socket
 * aliases here so it cannot replace the game's pointer-width word. */
#ifndef _BSDTYPES_DEFINED
#define _BSDTYPES_DEFINED
typedef unsigned long HostWinULong;
typedef unsigned long long u_int64;
#endif
#endif
#endif
#include <curl/curl.h>
#ifdef _WIN32
#undef RECT
#undef byte
#undef u_long
#undef DrawText
/* WinSock's IPv4 member alias also collides with the PS1 kernel's TCB. */
#undef s_addr
#endif
#include <cJSON.h>
#include <miniz.h>
#ifndef PE_VERSION
#define PE_VERSION "development"
#endif
#ifndef PE_BUILD_ID
#define PE_BUILD_ID "development"
#endif
static struct {
    SDL_Thread *thread;
    SDL_AtomicInt busy,done,progress,cancel;
    char root[4096],active[4096],version[32],archive_sha[65],message[180],result_message[180],result_path[4096],result_version[32],result_sha[65];
    int enabled,development,verify_only,success,result_activate;
} host_hd;

typedef struct {uint32_t h[8];uint64_t bytes;unsigned used;unsigned char block[64];} HdSha;
static uint32_t HdRor(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
static void HdShaBlock(HdSha *s,const unsigned char *b){
    static const uint32_t k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64];for(unsigned i=0;i<16;++i)w[i]=(uint32_t)b[i*4]<<24|(uint32_t)b[i*4+1]<<16|(uint32_t)b[i*4+2]<<8|b[i*4+3];
    for(unsigned i=16;i<64;++i){uint32_t x=w[i-15],y=w[i-2];w[i]=w[i-16]+(HdRor(x,7)^HdRor(x,18)^(x>>3))+w[i-7]+(HdRor(y,17)^HdRor(y,19)^(y>>10));}
    uint32_t a=s->h[0],c=s->h[2],b0=s->h[1],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
    for(unsigned i=0;i<64;++i){uint32_t t=h+(HdRor(e,6)^HdRor(e,11)^HdRor(e,25))+((e&f)^(~e&g))+k[i]+w[i];uint32_t u=(HdRor(a,2)^HdRor(a,13)^HdRor(a,22))+((a&b0)^(a&c)^(b0&c));h=g;g=f;f=e;e=d+t;d=c;c=b0;b0=a;a=t+u;}
    s->h[0]+=a;s->h[1]+=b0;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
static void HdShaInit(HdSha *s){*s=(HdSha){{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19},0,0,{0}};}
static void HdShaAdd(HdSha *s,const void *ptr,size_t n){const unsigned char *p=ptr;s->bytes+=n;while(n){size_t k=64-s->used;if(k>n)k=n;memcpy(s->block+s->used,p,k);s->used+=(unsigned)k;p+=k;n-=k;if(s->used==64){HdShaBlock(s,s->block);s->used=0;}}}
static void HdShaEnd(HdSha *s,char out[65]){uint64_t bits=s->bytes*8;unsigned char pad[128]={0x80};size_t n=s->used<56?56-s->used:120-s->used;HdShaAdd(s,pad,n);for(unsigned i=0;i<8;++i)pad[i]=(unsigned char)(bits>>(56-i*8));HdShaAdd(s,pad,8);for(unsigned i=0;i<8;++i)snprintf(out+i*8,9,"%08x",s->h[i]);}
static int HdHashFile(const char *path,char out[65]){SDL_IOStream *f=SDL_IOFromFile(path,"rb");if(!f)return 0;unsigned char block[65536];HdSha s;HdShaInit(&s);size_t n;while((n=SDL_ReadIO(f,block,sizeof(block)))){if(SDL_GetAtomicInt(&host_hd.cancel)){SDL_CloseIO(f);return 0;}HdShaAdd(&s,block,n);}int ok=SDL_GetIOStatus(f)==SDL_IO_STATUS_EOF;SDL_CloseIO(f);HdShaEnd(&s,out);return ok;}
static int HdPath(char *out,size_t n,const char *a,const char *b){int k=snprintf(out,n,"%s/%s",a,b);if(k<0||(size_t)k>=n){if(n)out[0]=0;return 0;}return 1;}
static int HdHex(const char *s){if(!s||strlen(s)!=64)return 0;for(unsigned i=0;i<64;++i)if(!strchr("0123456789abcdef",s[i]))return 0;return 1;}
static int HdName(const char *s){
    if(!s)return 0;if(!strcmp(s,"manifest.json")||!strcmp(s,"title.webp")||!strcmp(s,"title.png"))return 1;
    size_t n=strlen(s);if(n<20||n>25)return 0;for(unsigned i=0;i<16;++i)if(!strchr("0123456789abcdef",s[i]))return 0;
    return !strcmp(s+16,".webp")||!strcmp(s+16,".png")||!strcmp(s+16,".ref.png")||!strcmp(s+16,"-wide.bin");
}
static const char *HdString(const cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);return cJSON_IsString(v)?v->valuestring:NULL;}
static cJSON *HdJsonFile(const char *path,size_t limit){size_t n=0;char *data=SDL_LoadFile(path,&n);if(!data)return NULL;cJSON *j=n<=limit?cJSON_ParseWithLength(data,n):NULL;SDL_free(data);return j;}
static int HdSavePointer(const char *directory,const char *version,const char *sha){
    cJSON *j=cJSON_CreateObject();if(!j)return 0;cJSON_AddStringToObject(j,"directory",directory);cJSON_AddStringToObject(j,"version",version);cJSON_AddStringToObject(j,"sha256",sha);char *text=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(!text)return 0;
    char path[4096]={0},tmp[4096]={0},name[96];
    snprintf(name,sizeof(name),"active.new-%016llx-%08x.json",(unsigned long long)SDL_GetPerformanceCounter(),(unsigned)SDL_rand(0x7fffffff));
    int ok=HdPath(path,sizeof(path),host_hd.root,"active.json")&&HdPath(tmp,sizeof(tmp),host_hd.root,name)&&SDL_SaveFile(tmp,text,strlen(text))&&SDL_RenamePath(tmp,path);
    if(!ok&&*tmp)SDL_RemovePath(tmp);cJSON_free(text);return ok;
}
static int HdAllow(cJSON *files,const char *name,const char *sha){if(!HdName(name)||!HdHex(sha)||!strcmp(name,"manifest.json"))return 0;const cJSON *old=cJSON_GetObjectItemCaseSensitive(files,name);if(old)return cJSON_IsString(old)&&!strcmp(old->valuestring,sha);return cJSON_AddStringToObject(files,name,sha)!=NULL;}
static cJSON *HdFiles(const cJSON *manifest){
    cJSON *files=cJSON_CreateObject();const cJSON *atlases=cJSON_GetObjectItemCaseSensitive(manifest,"atlases"),*extras=cJSON_GetObjectItemCaseSensitive(manifest,"extras"),*r;
    if(!files||!cJSON_IsArray(atlases)||cJSON_GetArraySize(atlases)<1||cJSON_GetArraySize(atlases)>8192)goto fail;
    cJSON_ArrayForEach(r,atlases)if(!HdAllow(files,HdString(r,"file"),HdString(r,"atlas_sha256"))||!HdAllow(files,HdString(r,"reference_file"),HdString(r,"reference_sha256")))goto fail;
    if(extras&&!cJSON_IsArray(extras))goto fail;cJSON_ArrayForEach(r,extras)if(!HdAllow(files,HdString(r,"file"),HdString(r,"sha256")))goto fail;
    if(cJSON_GetArraySize(files)>16384)goto fail;return files;
fail:cJSON_Delete(files);return NULL;
}
static int HdVerify(const char *directory){
    char path[4096],actual[65];if(!HdPath(path,sizeof(path),directory,"manifest.json"))return 0;cJSON *manifest=HdJsonFile(path,8*1024*1024);if(!manifest)return 0;cJSON *files=HdFiles(manifest);cJSON_Delete(manifest);if(!files)return 0;const cJSON *r;int ok=1,n=0,total=cJSON_GetArraySize(files);
    cJSON_ArrayForEach(r,files){if(SDL_GetAtomicInt(&host_hd.cancel)||!HdPath(path,sizeof(path),directory,r->string)||!HdHashFile(path,actual)||strcmp(actual,r->valuestring)){ok=0;break;}SDL_SetAtomicInt(&host_hd.progress,97+(++n*2/total));}
    cJSON_Delete(files);return ok;
}
static size_t HdZipRead(void *opaque,mz_uint64 at,void *buffer,size_t n){SDL_IOStream *f=opaque;return SDL_SeekIO(f,(Sint64)at,SDL_IO_SEEK_SET)<0?0:SDL_ReadIO(f,buffer,n);}
static SDL_EnumerationResult HdRemoveEntry(void *unused,const char *directory,const char *name){(void)unused;char p[4096];if(HdPath(p,sizeof(p),directory,name))SDL_RemovePath(p);return SDL_ENUM_CONTINUE;}
static void HdClean(const char *directory){if(!*directory)return;SDL_EnumerateDirectory(directory,HdRemoveEntry,NULL);SDL_RemovePath(directory);}
static int HdUnpack(const char *archive,const char *directory,const char *version){
    SDL_IOStream *io=SDL_IOFromFile(archive,"rb");if(!io)return 0;Sint64 size=SDL_GetIOSize(io);mz_zip_archive zip={0};zip.m_pRead=HdZipRead;zip.m_pIO_opaque=io;int started=0,ok=0;void *bytes=NULL;cJSON *manifest=NULL,*files=NULL,*seen=NULL;
    if(size<0||!mz_zip_reader_init(&zip,(mz_uint64)size,0))goto end;started=1;
    int count=(int)mz_zip_reader_get_num_files(&zip),index=mz_zip_reader_locate_file(&zip,"manifest.json",NULL,MZ_ZIP_FLAG_CASE_SENSITIVE);mz_zip_archive_file_stat stat;
    if(count<2||count>16385||index<0||!mz_zip_reader_file_stat(&zip,(mz_uint)index,&stat)||stat.m_uncomp_size>8*1024*1024)goto end;
    size_t length=0;bytes=mz_zip_reader_extract_to_heap(&zip,(mz_uint)index,&length,0);if(!bytes)goto end;manifest=cJSON_ParseWithLength(bytes,length);mz_free(bytes);bytes=NULL;
    if(!manifest||!HdString(manifest,"version")||strcmp(HdString(manifest,"version"),version)||!(files=HdFiles(manifest))||count!=cJSON_GetArraySize(files)+1)goto end;
    seen=cJSON_CreateObject();if(!seen)goto end;mz_uint64 total=0;
    for(int i=0;i<count;++i){
        if(SDL_GetAtomicInt(&host_hd.cancel)||!mz_zip_reader_file_stat(&zip,(mz_uint)i,&stat)||!HdName(stat.m_filename)||mz_zip_reader_is_file_a_directory(&zip,(mz_uint)i)||mz_zip_reader_is_file_encrypted(&zip,(mz_uint)i)||!mz_zip_reader_is_file_supported(&zip,(mz_uint)i)||((stat.m_external_attr>>16)&0170000)==0120000||stat.m_uncomp_size>64*1024*1024||cJSON_GetObjectItemCaseSensitive(seen,stat.m_filename))goto end;
        if(strcmp(stat.m_filename,"manifest.json")&&!cJSON_GetObjectItemCaseSensitive(files,stat.m_filename))goto end;
        total+=stat.m_uncomp_size;if(total>UINT64_C(2147483648))goto end;
        if(!cJSON_AddBoolToObject(seen,stat.m_filename,1))goto end;
        bytes=mz_zip_reader_extract_to_heap(&zip,(mz_uint)i,&length,0);char path[4096];
        if(!bytes||!HdPath(path,sizeof(path),directory,stat.m_filename)||!SDL_SaveFile(path,bytes,length))goto end;
        mz_free(bytes);bytes=NULL;SDL_SetAtomicInt(&host_hd.progress,96+(i*2/count));
    }
    ok=HdVerify(directory);
end:if(bytes)mz_free(bytes);cJSON_Delete(seen);cJSON_Delete(files);cJSON_Delete(manifest);if(started)mz_zip_reader_end(&zip);SDL_CloseIO(io);return ok;
}
typedef struct {SDL_IOStream *file;char data[16385];size_t used;uint64_t expected,written;HdSha sha;} HdTransfer;
static size_t HdWrite(char *p,size_t a,size_t b,void *opaque){HdTransfer *t=opaque;if(a&&b>SIZE_MAX/a)return 0;size_t n=a*b;if(SDL_GetAtomicInt(&host_hd.cancel))return 0;
    if(t->file){if(t->written>t->expected||n>t->expected-t->written)return 0;size_t k=SDL_WriteIO(t->file,p,n);HdShaAdd(&t->sha,p,k);t->written+=k;return k;}
    if(n>sizeof(t->data)-1-t->used)return 0;memcpy(t->data+t->used,p,n);t->used+=n;t->data[t->used]=0;return n;
}
static int HdProgress(void *opaque,curl_off_t total,curl_off_t now,curl_off_t upload_total,curl_off_t uploaded){(void)total;(void)upload_total;(void)uploaded;HdTransfer *t=opaque;if(t->file&&t->expected)SDL_SetAtomicInt(&host_hd.progress,(int)((uint64_t)now*95/t->expected));return SDL_GetAtomicInt(&host_hd.cancel);}
static int HdRequest(const char *url,HdTransfer *transfer,const char *download_id){
    CURL *curl=curl_easy_init();if(!curl)return 0;char ua[160],version[100],platform[100],id[128],build[100];snprintf(ua,sizeof(ua),"ParasiteEve/%s (%s; WebP; build %s)",PE_VERSION,SDL_GetPlatform(),PE_BUILD_ID);snprintf(version,sizeof(version),"X-PE-Version: %s",PE_VERSION);snprintf(platform,sizeof(platform),"X-PE-Platform: %s",SDL_GetPlatform());snprintf(id,sizeof(id),"X-PE-Download-ID: %s",download_id);
    snprintf(build,sizeof(build),"X-PE-Build: %s",PE_BUILD_ID);
    struct curl_slist *headers=NULL;headers=curl_slist_append(headers,version);headers=curl_slist_append(headers,platform);headers=curl_slist_append(headers,id);headers=curl_slist_append(headers,build);
    curl_easy_setopt(curl,CURLOPT_URL,url);curl_easy_setopt(curl,CURLOPT_USERAGENT,ua);curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"https,http");
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(curl,CURLOPT_TIMEOUT,7200L);curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,1024L);curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,60L);curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,HdWrite);curl_easy_setopt(curl,CURLOPT_WRITEDATA,transfer);curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,HdProgress);curl_easy_setopt(curl,CURLOPT_XFERINFODATA,transfer);curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);curl_easy_setopt(curl,CURLOPT_FAILONERROR,1L);
    CURLcode result=curl_easy_perform(curl);long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);curl_slist_free_all(headers);curl_easy_cleanup(curl);return result==CURLE_OK&&status==200;
}
static int HdWorker(void *unused){
    (void)unused;char archive[4096]={0},staging[4096]={0},final[4096],actual[65],url[512],id[80];cJSON *catalog=NULL;host_hd.success=0;host_hd.result_activate=0;
    SDL_strlcpy(host_hd.result_message,"Download failed. Your previous pack is unchanged.",sizeof(host_hd.result_message));
    if(host_hd.verify_only){host_hd.success=HdVerify(host_hd.active);SDL_strlcpy(host_hd.result_message,host_hd.success?"Installed pack verified.":"Verification failed. Download a fresh copy.",sizeof(host_hd.result_message));goto end;}
    const char *base=SDL_getenv("PE_HD_SERVER");if(!base||!*base)base="https://pe.polanka.ovh";
    unsigned local_port=0;char trailing;int local=sscanf(base,"http://127.0.0.1:%u%c",&local_port,&trailing)==1&&local_port>0&&local_port<=65535;
    if(strcmp(base,"https://pe.polanka.ovh")&&!local){SDL_strlcpy(host_hd.result_message,"Invalid mod server override.",sizeof(host_hd.result_message));goto end;}
    snprintf(id,sizeof(id),"pe-%016llx-%08x",(unsigned long long)SDL_GetPerformanceCounter(),(unsigned)SDL_rand(0x7fffffff));
    snprintf(url,sizeof(url),"%s/api/mods/upscale/latest",base);HdTransfer transfer={0};
    if(!HdRequest(url,&transfer,id)||!(catalog=cJSON_ParseWithLength(transfer.data,transfer.used))){SDL_strlcpy(host_hd.result_message,"Could not fetch the mod catalog. Previous pack kept.",sizeof(host_hd.result_message));goto end;}
    const char *format=HdString(catalog,"format");if(!format||strcmp(format,"webp-reference-png")){SDL_strlcpy(host_hd.result_message,"This pack needs a newer game build.",sizeof(host_hd.result_message));goto end;}
    const char *version=HdString(catalog,"version"),*sha=HdString(catalog,"sha256"),*remote=HdString(catalog,"url");const cJSON *size=cJSON_GetObjectItemCaseSensitive(catalog,"size");
    if(!version||strlen(version)>20||!strchr(version,'.')||!HdHex(sha)||!remote||!cJSON_IsNumber(size)||size->valuedouble<1||size->valuedouble>2147483648.0)goto end;
    for(const char *p=version;*p;++p)if(!isdigit((unsigned char)*p)&&*p!='.')goto end;
    snprintf(url,sizeof(url),"%s/upscale-v%s.zip",base,version);if(strcmp(remote,url))goto end;
    if(!host_hd.development&&*host_hd.active&&!strcmp(version,host_hd.version)&&!strcmp(sha,host_hd.archive_sha)&&HdVerify(host_hd.active)) {
        host_hd.success=1;SDL_strlcpy(host_hd.result_message,"Background pack is up to date and verified.",sizeof(host_hd.result_message));goto end;
    }
    char name[100],stage_name[100],archive_name[100];
    snprintf(stage_name,sizeof(stage_name),"install-%s.partial",id);snprintf(archive_name,sizeof(archive_name),"download-%s.partial.zip",id);
    snprintf(name,sizeof(name),"v%s-%.16s-%s",version,sha,id);
    if(!HdPath(final,sizeof(final),host_hd.root,name)||!HdPath(staging,sizeof(staging),host_hd.root,stage_name)||!HdPath(archive,sizeof(archive),host_hd.root,archive_name))goto end;
    HdClean(staging);if(!SDL_CreateDirectory(staging))goto end;
    memset(&transfer,0,sizeof(transfer));transfer.expected=(uint64_t)size->valuedouble;HdShaInit(&transfer.sha);transfer.file=SDL_IOFromFile(archive,"wb");if(!transfer.file)goto end;
    int fetched=HdRequest(url,&transfer,id);int closed=SDL_CloseIO(transfer.file);transfer.file=NULL;HdShaEnd(&transfer.sha,actual);
    if(!fetched||!closed||transfer.written!=transfer.expected){SDL_strlcpy(host_hd.result_message,"Download interrupted. Previous pack kept.",sizeof(host_hd.result_message));goto end;}
    if(strcmp(actual,sha)){SDL_strlcpy(host_hd.result_message,"Download checksum mismatch. Previous pack kept.",sizeof(host_hd.result_message));goto end;}
    SDL_SetAtomicInt(&host_hd.progress,96);
    if(!HdUnpack(archive,staging,version)){SDL_strlcpy(host_hd.result_message,"Pack validation failed. Previous pack kept.",sizeof(host_hd.result_message));goto end;}
    if(!SDL_CompareAndSwapAtomicInt(&host_hd.cancel,0,2)||!SDL_RenamePath(staging,final))goto end;
    staging[0]=0;if(!HdSavePointer(final,version,sha)){HdClean(final);goto end;}
    SDL_strlcpy(host_hd.result_path,final,sizeof(host_hd.result_path));SDL_strlcpy(host_hd.result_version,version,sizeof(host_hd.result_version));SDL_strlcpy(host_hd.result_sha,sha,sizeof(host_hd.result_sha));host_hd.success=1;host_hd.result_activate=1;
    SDL_strlcpy(host_hd.result_message,host_hd.development?"Installed. Local preview overrides this pack.":"Installed. Choose 2x or higher in Graphics.",sizeof(host_hd.result_message));
end:cJSON_Delete(catalog);if(*archive)SDL_RemovePath(archive);HdClean(staging);if(SDL_GetAtomicInt(&host_hd.cancel)==1){host_hd.success=0;SDL_strlcpy(host_hd.result_message,"Cancelled. Your previous pack is unchanged.",sizeof(host_hd.result_message));}if(host_hd.success)SDL_SetAtomicInt(&host_hd.progress,100);SDL_SetAtomicInt(&host_hd.done,1);return 0;
}
static const char *Host_HdStatus(void){return *host_hd.message?host_hd.message:"Download optional HD backgrounds from pe.polanka.ovh.";}
static int Host_HdBusy(void){return SDL_GetAtomicInt(&host_hd.busy);}
static int Host_HdPercent(void){return SDL_GetAtomicInt(&host_hd.progress);}
static const char *Host_HdValue(int row){if(row==0)return host_hd.development?(host_hd.enabled?"Local preview":"Off"):(host_hd.enabled?"On":"Off");if(row==1)return Host_HdBusy()?"Cancel download":"Download / update";return *host_hd.active?"Verify files":"Not installed";}
void Render_RefreshHdBackgrounds(void);
void MenuCard_RefreshHdBackground(void);
static void Host_HdApply(void) {
    if (!host_hd.development && *host_hd.active)
        SDL_setenv_unsafe("PE_HD_BACKGROUNDS",host_hd.active,1);
    Psyz_GpuSetSpriteAtlasesEnabled(host_hd.enabled);
    Render_RefreshHdBackgrounds();
    MenuCard_RefreshHdBackground();
}
static void Host_HdPoll(void){if(!host_hd.thread||!SDL_GetAtomicInt(&host_hd.done))return;SDL_WaitThread(host_hd.thread,NULL);host_hd.thread=NULL;SDL_SetAtomicInt(&host_hd.busy,0);SDL_strlcpy(host_hd.message,host_hd.result_message,sizeof(host_hd.message));if(host_hd.success&&host_hd.result_activate){SDL_strlcpy(host_hd.active,host_hd.result_path,sizeof(host_hd.active));SDL_strlcpy(host_hd.version,host_hd.result_version,sizeof(host_hd.version));SDL_strlcpy(host_hd.archive_sha,host_hd.result_sha,sizeof(host_hd.archive_sha));host_hd.enabled=1;host_options.hd_backgrounds=1;Host_HdApply();if(host_options.enabled)Host_SaveOptions();}}
static void Host_HdAction(int row){
    Host_HdPoll();if(Host_HdBusy()){if(row==1)SDL_CompareAndSwapAtomicInt(&host_hd.cancel,0,1);return;}
    if(row==0){if(!*host_hd.active){SDL_strlcpy(host_hd.message,"Download the background pack first.",sizeof(host_hd.message));return;}host_hd.enabled=!host_hd.enabled;host_options.hd_backgrounds=host_hd.enabled;Host_HdApply();SDL_strlcpy(host_hd.message,host_options.scale<2?"Choose 2x or higher in Graphics for HD.":"Background setting applied.",sizeof(host_hd.message));return;}
    if(row==2&&host_hd.development){SDL_strlcpy(host_hd.message,"Local preview: integrity checks are disabled.",sizeof(host_hd.message));return;}
    if(row==2&&!*host_hd.active){SDL_strlcpy(host_hd.message,"No installed background pack to verify.",sizeof(host_hd.message));return;}
    host_hd.verify_only=row==2;host_hd.success=0;SDL_SetAtomicInt(&host_hd.cancel,0);SDL_SetAtomicInt(&host_hd.done,0);SDL_SetAtomicInt(&host_hd.progress,0);SDL_SetAtomicInt(&host_hd.busy,1);SDL_strlcpy(host_hd.message,row==2?"Verifying installed files...":"Checking latest background pack...",sizeof(host_hd.message));host_hd.thread=SDL_CreateThread(HdWorker,"HD backgrounds",NULL);if(!host_hd.thread){SDL_SetAtomicInt(&host_hd.busy,0);SDL_strlcpy(host_hd.message,"Could not start download.",sizeof(host_hd.message));}
}
static void Host_HdInit(void){
    const char *slash=strrchr(host_options.user_file,'/');const char *back=strrchr(host_options.user_file,'\\');if(back&&(!slash||back>slash))slash=back;if(!slash)return;
    int n=snprintf(host_hd.root,sizeof(host_hd.root),"%.*s/mods/upscale",(int)(slash-host_options.user_file),host_options.user_file);if(n<0||(size_t)n>=sizeof(host_hd.root)||!SDL_CreateDirectory(host_hd.root))return;
    char path[4096];if(HdPath(path,sizeof(path),host_hd.root,"active.json")){cJSON *j=HdJsonFile(path,16384);const char *directory=HdString(j,"directory"),*version=HdString(j,"version"),*sha=HdString(j,"sha256");if(directory&&version&&sha&&strlen(directory)<sizeof(host_hd.active)&&strlen(version)<sizeof(host_hd.version)&&HdHex(sha)&&!strncmp(directory,host_hd.root,strlen(host_hd.root))&&directory[strlen(host_hd.root)]=='/'){SDL_strlcpy(host_hd.active,directory,sizeof(host_hd.active));SDL_strlcpy(host_hd.version,version,sizeof(host_hd.version));SDL_strlcpy(host_hd.archive_sha,sha,sizeof(host_hd.archive_sha));host_hd.enabled=host_options.hd_backgrounds;}cJSON_Delete(j);}
    const char *dev=SDL_getenv("PE_HD_BACKGROUNDS");if(dev&&*dev){host_hd.development=1;host_hd.enabled=1;SDL_strlcpy(host_hd.active,dev,sizeof(host_hd.active));}
    else if(*host_hd.active)SDL_setenv_unsafe("PE_HD_BACKGROUNDS",host_hd.active,1);
    Psyz_GpuSetSpriteAtlasesEnabled(host_hd.enabled);curl_global_init(CURL_GLOBAL_DEFAULT);
}
static void Host_HdStop(void){if(host_hd.thread){SDL_CompareAndSwapAtomicInt(&host_hd.cancel,0,1);SDL_WaitThread(host_hd.thread,NULL);host_hd.thread=NULL;}curl_global_cleanup();}

static int Host_HdCli(int verify) {
    const char *override=SDL_getenv("PE_CONFIG_HOME");char *pref=NULL;
    if(!override||!*override){pref=SDL_GetPrefPath("","Parasite Eve");override=pref;}
    if(!override||snprintf(host_options.user_file,sizeof(host_options.user_file),"%s/parasite-eve.toml",override)>=(int)sizeof(host_options.user_file)){SDL_free(pref);return 1;}
    SDL_CreateDirectory(override);SDL_free(pref);Host_HdInit();Host_HdAction(verify?2:1);
    int cancel_at=0;const char *cancel_env=SDL_getenv("PE_HD_TEST_CANCEL_AT");const char *test_server=SDL_getenv("PE_HD_SERVER");
    if(cancel_env&&test_server&&!strncmp(test_server,"http://127.0.0.1:",17))cancel_at=atoi(cancel_env);
    int last=-1;while(Host_HdBusy()){int n=Host_HdPercent();if(cancel_at>0&&cancel_at<96&&n>=cancel_at){SDL_CompareAndSwapAtomicInt(&host_hd.cancel,0,1);cancel_at=0;}if(n!=last){printf("Background pack: %d%%\n",n);fflush(stdout);last=n;}SDL_Delay(25);Host_HdPoll();}
    puts(Host_HdStatus());int result=host_hd.success?0:1;Host_HdStop();return result;
}

#else
/* PS5 CPU: local title presentation only; field atlases remain unavailable. */
static struct { int development, enabled; char active[4096], version[32]; } host_hd;
#ifdef PE_PS5
void MenuCard_RefreshHdBackground(void);
/* Private PS5 CPU API: never grants GPU atlas or geometry-scale capability. */
int Psyz_CpuFieldMaterialAvailable(void);
int Psyz_CpuGeometryAvailable(void);
int Psyz_CpuGeometryConfigure(unsigned scale);
unsigned Psyz_CpuGeometryRequested(void);
unsigned Psyz_CpuGeometryEffective(void);
int Psyz_CpuFieldMaterialConfigure(int enabled,const char *directory);
void Render_RefreshHdBackgrounds(void);
static int Host_LocalTitlePath(const char *path) {
    /* Trusted explicit local paths, never URLs or traversal aliases. */
    if (!path || path[0]!='/' || strlen(path)>=sizeof(host_hd.active) ||
        strstr(path,"/../") || strstr(path,"/./") || strchr(path,'\\')) return 0;
    size_t n=strlen(path);
    return !(n>=3 && !strcmp(path+n-3,"/..")) && !(n>=2 && !strcmp(path+n-2,"/."));
}
#ifdef PE_PS5_GPU_NATIVE
/* Downloads are a desktop capability. Native PS5 consumes the same unpacked
 * atlas files from a configured local directory; it never needs active.json. */
static const char *Host_GpuHdDirectory(void) {
    if (*host_options.hd_field_directory) return host_options.hd_field_directory;
    if (*host_options.hd_title_directory) return host_options.hd_title_directory;
    return SDL_getenv("PE_HD_BACKGROUNDS");
}
static int Host_GpuHdPackPresent(const char *directory) {
    char filename[4096]; unsigned char marker=0;
    if (!Host_LocalTitlePath(directory)) return 0;
    int n=snprintf(filename,sizeof(filename),"%s/manifest.json",directory);
    if(n<0 || (size_t)n>=sizeof(filename)) return 0;
    SDL_IOStream *file=SDL_IOFromFile(filename,"rb");
    if(!file) return 0;
    int present=SDL_ReadIO(file,&marker,1)==1 && marker=='{';
    SDL_CloseIO(file);
    return present; /* Presence only; individual atlases remain renderer-validated. */
}
static const char *Host_HdStatus(void) {
    if (!Host_GpuHdPackPresent(Host_GpuHdDirectory()))
        return "Local HD pack not found. Set hd_field_directory to the unpacked pack; downloads are desktop only.";
    return host_hd.enabled ? "Local GPU HD requested. Use 2x or higher; actual atlas loads are logged."
                           : "Local pack found; HD is off. Toggle GPU HD backgrounds. No download is needed.";
}
static int Host_HdBusy(void) { return 0; }
static int Host_HdPercent(void) { return 0; }
static const char *Host_HdValue(int row) {
    if(row==0) return host_hd.enabled ? "Requested" : "Off";
    if(row==1) return Host_GpuHdPackPresent(Host_GpuHdDirectory()) ? "Found / reload" : "Not found / reload";
    return "Desktop only / local copy";
}
static void Host_HdApply(void) {
    const char *directory=Host_GpuHdDirectory();
    int present=Host_GpuHdPackPresent(directory);
    host_hd.enabled=host_options.hd_backgrounds && present;
    if(present) SDL_strlcpy(host_hd.active,directory,sizeof(host_hd.active));
    else host_hd.active[0]=0;
    /* Publish before refresh: mesh_entry and menu-card both read this SDL env. */
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"PE_HD_BACKGROUNDS",host_hd.active,true);
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"PE_PS5_HD_TITLE",host_hd.enabled?"1":"0",true);
    Psyz_GpuSetSpriteAtlasesEnabled(host_hd.enabled);
    if (Psyz_PS5PlatformReady()) DrawSync(0);
    Render_RefreshHdBackgrounds();
    Psyz_VideoSetRgb24Backdrop(NULL,NULL,0,0,0,0);
    if(host_hd.enabled) MenuCard_RefreshHdBackground();
    SDL_Log("[PS5 GPU HD] directory=%s manifest_present=%d requested=%d atlas_enabled=%d download_capability=0 rendered=unobserved",
        present?host_hd.active:"",present,host_options.hd_backgrounds,host_hd.enabled);
}
#else
static const char *Host_HdStatus(void) {
 #ifdef PE_PS5_GPU_NATIVE
    return host_hd.enabled ? "Native GPU atlases requested from the local pack; console validation pending."
                           : "Original GPU rendering; local HD background pack is optional.";
 #endif
    if (!Psyz_CpuFieldMaterialAvailable()) return "CPU field HD unavailable on this renderer. Title HD is separate.";
    if (!host_options.hd_field_materials) return "CPU field HD off. Geometry scale and widescreen are separate.";
    if (!Host_LocalTitlePath(host_options.hd_field_directory)) return "CPU field HD needs a local hd_field_directory. Original pixels retained.";
    return "Ordered field HD requested at 1x or scaled geometry. Admitted tiles use 2x material samples; see coverage logs.";
}
static int Host_HdBusy(void) { return 0; }
static int Host_HdPercent(void) { return 0; }
static const char *Host_HdValue(int row) {
#ifdef PE_PS5_GPU_NATIVE
    if (row==0) return host_hd.enabled ? "GPU atlases requested" : "Original";
 #else
    if (row==0) return host_hd.enabled ? "Title HD only" : "Original";
 #endif
    if (row==1) return !Psyz_CpuFieldMaterialAvailable() ? "Unavailable" :
        !host_options.hd_field_materials ? "Off" :
        Host_LocalTitlePath(host_options.hd_field_directory) ? "Requested (experimental)" : "Needs pack directory";
    return *host_options.hd_field_directory ? "Local / no download" : "Set hd_field_directory";
}
static void Host_HdApply(void) {
    const char *directory=*host_options.hd_title_directory ? host_options.hd_title_directory
        : SDL_getenv("PE_HD_BACKGROUNDS");
    host_hd.enabled=host_options.hd_backgrounds && Host_LocalTitlePath(directory);
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"PE_PS5_HD_TITLE",host_hd.enabled?"1":"0",true);
#ifdef PE_PS5_GPU_NATIVE
    Psyz_GpuSetSpriteAtlasesEnabled(host_hd.enabled);
 #else
    Psyz_GpuSetSpriteAtlasesEnabled(0); /* CPU path has no GPU atlas. */
 #endif
#ifdef PE_PS5_GPU_NATIVE
    if (Psyz_PS5PlatformReady()) DrawSync(0);
 #else
    if(Psyz_CpuFieldMaterialAvailable())DrawSync(0);
 #endif
    Psyz_CpuFieldMaterialConfigure(host_options.hd_field_materials,
        Host_LocalTitlePath(host_options.hd_field_directory)?host_options.hd_field_directory:"");
    Render_RefreshHdBackgrounds();
    Psyz_VideoSetRgb24Backdrop(NULL,NULL,0,0,0,0);
    if (host_hd.enabled) {
        SDL_strlcpy(host_hd.active,directory,sizeof(host_hd.active));
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"PE_HD_BACKGROUNDS",host_hd.active,true);
        MenuCard_RefreshHdBackground();
    } else host_hd.active[0]=0;
}
#endif
static void Host_HdPoll(void) {}
static void Host_HdAction(int row) {
    #ifdef PE_PS5_GPU_NATIVE
    if(row==0) host_options.hd_backgrounds=!host_options.hd_backgrounds;
    else if(row!=1) return; /* Reload local pack; no unsupported download action. */
#else
    if (row==0) host_options.hd_backgrounds=!host_options.hd_backgrounds;
    else if (row==1) {
        if (!host_options.hd_field_materials && !Psyz_CpuFieldMaterialAvailable()) return;
        host_options.hd_field_materials=!host_options.hd_field_materials;
    } else return;
#endif
    Host_HdApply();
    SDL_Log("[V2 DIRECT DISPLAY toggle] title_HD=%d field_HD=%d",host_options.hd_backgrounds,host_options.hd_field_materials);
    if (host_options.enabled) Host_SaveOptions();
}
static void Host_HdInit(void) { Host_HdApply(); }
static void Host_HdStop(void) {
    Psyz_CpuFieldMaterialConfigure(0,"");
    Psyz_VideoSetRgb24Backdrop(NULL,NULL,0,0,0,0);
}
static int Host_HdCli(int verify) { (void)verify; puts(Host_HdStatus()); return EXIT_FAILURE; }
#else
static const char *Host_HdStatus(void) { return "HD backgrounds are unavailable on this platform."; }
static int Host_HdBusy(void) { return 0; }
static int Host_HdPercent(void) { return 0; }
static const char *Host_HdValue(int row) { (void)row; return "Unavailable"; }
static void Host_HdApply(void) { host_hd.enabled=0; Psyz_GpuSetSpriteAtlasesEnabled(0); }
static void Host_HdPoll(void) {}
static void Host_HdAction(int row) { (void)row; }
static void Host_HdInit(void) { Host_HdApply(); }
static void Host_HdStop(void) {}
static int Host_HdCli(int verify) { (void)verify; puts(Host_HdStatus()); return EXIT_FAILURE; }
#endif /* PE_PS5 local title */
#endif
#ifdef PE_PS5_GPU_NATIVE
#include "installer/pe_install_view.h"
#endif
#ifdef PE_PS5_GPU_NATIVE
struct HostOverlayState;
static void Host_RegistryGoTo(struct HostOverlayState*,int,const char*);
static int Host_RegistryRouteScope(const char*,int*);
static int Host_RegistryBack(struct HostOverlayState *state);
static const char *Host_RegistryTitle(int page);
static int Host_ModernRowCount(int page);
static const char *Host_ModernRowLabel(int page,int row);
static const char *Host_ModernRowHelp(int page,int row);
static void Host_ModernRowValue(int page,int row,char *out,unsigned size);
static int Host_ModernRowKind(int page,int row); /*0info,1action,2choice*/
static int Host_ModernAction(int page,int row,int direction,int activate);
static void Host_OpenSettingsTab(int page);
static int Host_NativePickMissingDisc(unsigned identity);
static void Host_CharacterPackAction(int choose);
static int host_requested_settings_page=-1,host_last_settings_page;
static int host_registry_open_pending,host_registry_open_scope;
static char host_registry_open_path[96];
static int host_saved_rows[7],host_saved_scrolls[7];
static const char *host_pending_mod_page;
#endif
#include "settings_overlay.h"
#ifdef PE_PS5_GPU_NATIVE
static void Host_RegistryClose(void){s_overlayState.open=0;}
#endif

static char *Host_Trim(char *s) {
    while (isspace((unsigned char)*s)) {
        ++s;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return s;
}

/* Preferences use TOML tables, integers, booleans and single-line strings.
 * Unknown keys remain harmless; malformed supported values are diagnosed. */
static int Host_String(char *text, char *out, size_t size) {
    char quote = *text++;
    if (quote != '"' && quote != '\'') {
        return 0;
    }

    size_t n = 0;
    while (*text && *text != quote) {
        unsigned char c = (unsigned char)*text++;
        if (c < 32) {
            return 0;
        }
        if (quote == '"' && c == '\\') {
            c = (unsigned char)*text++;
            if (c == 'n') {
                c = '\n';
            } else if (c == 'r') {
                c = '\r';
            } else if (c == 't') {
                c = '\t';
            } else if (c != '"' && c != '\\') {
                return 0;
            }
        }
        if (n + 1 >= size) {
            return 0;
        }
        out[n++] = (char)c;
    }
    if (*text++ != quote) {
        return 0;
    }
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    if (*text && *text != '#') {
        return 0;
    }
    out[n] = '\0';
    return 1;
}

static int Host_ParseOptionValue(const char *section, const char *key, char *value) {
#ifdef PE_PS5_GPU_NATIVE
    if(!strcmp(section,"mods")&&!strcmp(key,"hd_model_directory")){char decoded[4096];if(!Host_String(value,decoded,sizeof decoded)||(*decoded&&!Host_LocalTitlePath(decoded)))return 0;SDL_strlcpy(host_options.hd_model_directory,decoded,sizeof host_options.hd_model_directory);return 1;}
#endif
#ifdef PE_PS5
    if (!strcmp(section,"mods") && !strcmp(key,"hd_field_directory")) {
        char decoded[4096];
        if (!Host_String(value,decoded,sizeof(decoded)) || (*decoded && !Host_LocalTitlePath(decoded))) return 0;
        SDL_strlcpy(host_options.hd_field_directory,decoded,sizeof(host_options.hd_field_directory));return 1;
    }
    if (!strcmp(section,"mods") && !strcmp(key,"hd_title_directory")) {
        char decoded[4096];
        if (!Host_String(value,decoded,sizeof(decoded)) ||
            (*decoded && !Host_LocalTitlePath(decoded))) return 0;
        SDL_strlcpy(host_options.hd_title_directory,decoded,sizeof(host_options.hd_title_directory));
        return 1;
    }
#endif
    if (!strcmp(section, "discs")) {
        int slot = !strcmp(key, "disc1") ? 0 : !strcmp(key, "disc2") ? 1 : -1;
        if (slot >= 0) {
            char decoded[4096];
            if (!Host_String(value, decoded, sizeof(decoded))) {
                return 0;
            }
            memcpy(host_options.discs[slot], decoded, strlen(decoded) + 1);
        }
        return 1;
    }

    int v = 0;
    if (!strncmp(value, "true", 4) &&
        (!value[4] || isspace((unsigned char)value[4]) || value[4] == '#')) {
        v = 1;
    } else if (!strncmp(value, "false", 5) &&
               (!value[5] || isspace((unsigned char)value[5]) || value[5] == '#')) {
        v = 0;
    } else {
        char *end = NULL;
        errno = 0;
        long parsed = strtol(value, &end, 10);
        end = Host_Trim(end);
        if (errno || end == value || (*end && *end != '#') || parsed < -1 || parsed > INT_MAX) {
            return 0;
        }
        v = (int)parsed;
    }
    /* -1 only marks an unbound controller speed binding. */
    if (v < 0 && (strcmp(section, "controller") || strcmp(key, host_button_names[HOST_SPEED_BINDING]))) {
        return 0;
    }

    #ifdef PE_PS5_GPU_NATIVE
    {int handled=Host_VisualParse(section,key,v);if(handled>=0)return handled;}
    #endif
    if (!strcmp(section, "controls")) {
        if (!strcmp(key, "field_run_threshold")) {
            if (v > 100) return 0;
            host_options.analog_run_threshold = (unsigned)v;
        }
    } else if (!strcmp(section, "graphics")) {
        if (!strcmp(key, "internal_resolution")) {
            if (v < 1 || v > 8
#if defined(PE_PS5) && !defined(PE_PS5_GPU_NATIVE)
                || !(v==1 || v==2 || v==4 || v==6 || v==8)
#endif
            ) {
                return 0;
            }
            if (v > 1 && (v & 1)) {
                ++v;
            }
            host_options.scale = (unsigned)v;
        } else if (!strcmp(key, "fullscreen")) {
            if (v > 1) {
                return 0;
            }
            host_options.fullscreen = v;
        } else if (!strcmp(key, "square_pixels")) {
            if (v > 1) {
                return 0;
            }
            host_options.aspect = v;
        } else if (!strcmp(key, "disable_dithering")) {
            if (v > 1) {
                return 0;
            }
            host_options.dither = v;
        } else if (!strcmp(key, "antialiasing")) {
            if (v > 1) return 0;
            host_options.antialiasing = v;
        } else if (!strcmp(key, "model_filtering")) {
            if (v > 1) return 0;
            host_options.model_filtering = v;
        } else if (!strcmp(key, "widescreen")) {
            if (v > 1) return 0;
            host_options.widescreen = v;
#ifdef PE_PS5_GPU_NATIVE
        } else if (!strcmp(key, "gameplay_fill")) {
            if(v>1)return 0;host_options.gameplay_fill=v;
        } else if (!strcmp(key, "battle_debris_3d")) {
            if(v>2)return 0;host_options.battle_debris_3d=v; /* Legacy booltrue maps to Hybrid1. */
        } else if (!strcmp(key, "movie_presentation")) {
            if(v>2)return 0;host_options.movie_presentation=v==2?1:v;
            if(v==2)SDL_Log("[Movie preference migration] saved_mode=2 next_mode=1 reason=Off-is-original-aspect-plus-blur other_settings=preserved");
        } else if (!strcmp(key, "sprite_filtering")) {
            if(v>1)return 0;host_options.sprite_filtering=v;
        } else if (!strcmp(key, "movie_cleanup")) {
            if(v>3)return 0;host_options.movie_cleanup=v;
        } else if (!strcmp(key, "movie_blur")) {
            if(v>1)return 0;host_options.movie_blur=v;
        } else if (!strcmp(key, "fsr1")) {
            if(v>1)return 0;host_options.fsr1=v;
        } else if (!strcmp(key, "fsr1_sharpness")) {
            if(v>2)return 0;host_options.fsr1_sharpness=v;
        } else if (!strcmp(key, "triangle_menu_font")) {
            if(v>1)return 0;host_options.triangle_menu_font=v;
        } else if (!strcmp(key, "dialogue_text")) {
            if(v>1)return 0;host_options.dialogue_text=v;
        } else if (!strcmp(key, "particle_lighting")) {
            if(v>1)return 0;host_options.particle_lighting=v;
        } else if (!strcmp(key, "player_shadow")) {
            if(v>1)return 0;host_options.player_shadow=v;
        } else if (!strcmp(key, "model_fill_lighting")) {
            if(v>2)return 0;host_options.model_fill_lighting=v;
#endif
        } else if (!strcmp(key, "frame_interpolation")) {
            if(v>1)return 0;host_options.frame_interpolation=v;
        } else if (!strcmp(key, "geometry_correction")) {
            if (v > 1) {
                return 0;
            }
            host_options.geometry_correction = v;
        } else if (!strcmp(key, "dynamic_lighting")) {
            if (v > 1) {
                return 0;
            }
            host_options.dynamic_lighting = v;
        } else if (!strcmp(key, "effect_geometry_correction")) {
            if (v > 1) {
                return 0;
            }
            host_options.effect_geometry_correction = v;
        }
#ifdef PE_PS5
    } else if (!strcmp(section,"mods") && !strcmp(key,"hd_field_materials")) {
        if(v<0||v>1)return 0;host_options.hd_field_materials=v;
#endif
    } else if (!strcmp(section,"mods") && !strcmp(key,"hd_backgrounds")) {
        if(v<0||v>1)return 0;host_options.hd_backgrounds=v;
#ifdef PE_PS5_GPU_NATIVE
    } else if(!strcmp(section,"mods")&&!strcmp(key,"hd_models")){if(v<0||v>1)return 0;host_options.hd_models=v;
    } else if(!strcmp(section,"interface")){
        if(!strcmp(key,"show_fps")){if(v<0||v>1)return 0;host_options.show_fps=v;}
        else if(!strcmp(key,"menu_style")){if(v<0||v>1)return 0;host_options.menu_style=v;}
        else if(!strcmp(key,"menu_text_size")){if(v<0||v>1)return 0;host_options.menu_text_size=v;}
        else if(!strcmp(key,"menu_pointer")){if(v<0||v>2)return 0;host_options.menu_pointer=v;}
#endif
    } else if (!strcmp(section, "audio") && !strcmp(key, "volume")) {
        if (v > 100) {
            return 0;
        }
        host_options.volume = (unsigned)v;
    } else if (!strcmp(section, "game") && !strcmp(key, "speed")) {
        if (v < 2 || v > PSYZ_SPEED_MAX) {
            return 0;
        }
        host_options.speed_factor = (unsigned)v;
    } else if (!strcmp(section, "game") && !strcmp(key, "speed_toggle")) {
        if (v > 1) {
            return 0;
        }
        host_options.speed_toggle = v;
    } else if (!strcmp(section, "keyboard") || !strcmp(section, "controller")) {
        for (unsigned i = 0; i < HOST_BINDING_COUNT; ++i) {
            if (!strcmp(key, host_button_names[i])) {
                int speed = i == HOST_SPEED_BINDING;
                if (!strcmp(section, "keyboard")) {
                    if (!(speed && v == HOST_UNBOUND_KEY) &&
                        (v <= SDL_SCANCODE_UNKNOWN || v >= SDL_SCANCODE_COUNT || v == SDL_SCANCODE_ESCAPE)) {
                        return 0;
                    }
                    host_options.keys[i] = v;
                } else {
                    if (!(speed && v == HOST_UNBOUND_BUTTON) && (v < 0 || v > SDL_GAMEPAD_BUTTON_COUNT + 1)) {
                        return 0;
                    }
                    host_options.buttons[i] = v;
                }
            }
        }
    }
    return 1;
}

/* SDL paths are UTF-8; the Windows CRT's narrow fopen uses the local ANSI
 * code page. Keep the existing TOML parser/writer on a Unicode FILE stream. */
static FILE *Host_OptionsFile(const char *path,const char *mode) {
#ifdef _WIN32
    wchar_t *wide_path=(wchar_t *)SDL_iconv_string("UTF-16LE","UTF-8",path,strlen(path)+1);
    wchar_t *wide_mode=(wchar_t *)SDL_iconv_string("UTF-16LE","UTF-8",mode,strlen(mode)+1);
    FILE *file=wide_path&&wide_mode?_wfopen(wide_path,wide_mode):NULL;
    SDL_free(wide_path);SDL_free(wide_mode);return file;
#else
    return fopen(path,mode);
#endif
}

static void Host_LoadOptions(const char *path) {
    FILE *file = Host_OptionsFile(path, "r");
#ifdef PE_PS5
    int error = file ? 0 : errno;
    SDL_Log("PS5 preferences open path=%s success=%d errno=%d", path, file != NULL, error);
    errno = error;
#endif
    if (!file) {
        return;
    }

    char line[9000];
    char section[64] = "";
    unsigned number = 0;
    while (fgets(line, sizeof(line), file)) {
        ++number;
        char *s = Host_Trim(line);
        if (!*s || *s == '#') {
            continue;
        }
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (!end || (size_t)(end - s - 1) >= sizeof(section)) {
                fprintf(stderr, "%s:%u: invalid preference\n", path, number);
                continue;
            }
            *end = '\0';
            snprintf(section, sizeof(section), "%s", Host_Trim(s + 1));
            continue;
        }

        char *end = strchr(s, '=');
        if (!end) {
            fprintf(stderr, "%s:%u: invalid preference\n", path, number);
            continue;
        }
        *end = '\0';
        char *key = Host_Trim(s);
        char *value = Host_Trim(end + 1);
        if (!Host_ParseOptionValue(section, key, value)) {
            fprintf(stderr, "%s:%u: invalid preference\n", path, number);
        }
    }
    fclose(file);
}

static void Host_WriteString(FILE *file, const char *value) {
    fputc('"', file);
    for (const unsigned char *s = (const unsigned char *)value; *s; ++s) {
        if (*s == '\\' || *s == '"') {
            fputc('\\', file);
        }
        if (*s == '\n') {
            fputs("\\n", file);
        } else if (*s == '\r') {
            fputs("\\r", file);
        } else if (*s == '\t') {
            fputs("\\t", file);
        } else {
            fputc(*s, file);
        }
    }
    fputc('"', file);
}

static int Host_SaveOptions(void) {
#ifdef PE_PS5_GPU_NATIVE
    /* Preserve requested preference even when memory admission falls back. */
#endif
    if (!host_options.enabled || !*host_options.user_file) {
        return 0;
    }

    char temporary[4120];
    snprintf(temporary, sizeof(temporary), "%s.tmp", host_options.user_file);
    FILE *file = Host_OptionsFile(temporary, "w");
    if (!file) {
        return 0;
    }

    #ifdef PE_PS5_GPU_NATIVE
    Host_VisualWrite(file);
    #endif
    fprintf(
        file,
        "# Parasite Eve desktop preferences\n"
        "[graphics]\n"
        "internal_resolution = %u\n"
        "fullscreen = %s\n"
        "square_pixels = %s\n"
        "disable_dithering = %s\n"
        "geometry_correction = %s\n"
        "frame_interpolation = %s\n"
        "antialiasing = %s\n"
        "model_filtering = %s\n"
        "widescreen = %s\n"
        "dynamic_lighting = %s\n"
        "effect_geometry_correction = %s\n"
#ifdef PE_PS5_GPU_NATIVE
        "sprite_filtering = %s\nmovie_cleanup = %u\ngameplay_fill = %s\nfsr1 = %s\nfsr1_sharpness = %u\nmodel_fill_lighting = %u\nparticle_lighting = %s\ndialogue_text = %s\ntriangle_menu_font = %s\nmovie_blur = %s\nmovie_presentation = %u\nbattle_debris_3d = %u\nplayer_shadow = %u\n"
#endif
        "\n"
        "[audio]\n"
        "volume = %u\n\n"
        "[game]\n"
        "speed = %u\n"
        "speed_toggle = %s\n\n"
        "[controls]\n"
        "field_run_threshold = %u\n\n"
        "[discs]\n"
        "disc1 = ",
        host_options.scale,
        host_options.fullscreen ? "true" : "false",
        host_options.aspect ? "true" : "false",
        host_options.dither ? "true" : "false",
        host_options.geometry_correction ? "true" : "false",
        host_options.frame_interpolation ? "true" : "false",
        host_options.antialiasing ? "true" : "false",
        host_options.model_filtering ? "true" : "false",
        host_options.widescreen ? "true" : "false",
        host_options.dynamic_lighting ? "true" : "false",
        host_options.effect_geometry_correction ? "true" : "false",
#ifdef PE_PS5_GPU_NATIVE
        host_options.sprite_filtering?"true":"false",(unsigned)host_options.movie_cleanup,host_options.gameplay_fill ? "true" : "false",host_options.fsr1 ? "true" : "false",(unsigned)host_options.fsr1_sharpness,(unsigned)host_options.model_fill_lighting,host_options.particle_lighting?"true":"false",host_options.dialogue_text?"true":"false",host_options.triangle_menu_font?"true":"false",host_options.movie_presentation==1?"true":"false",(unsigned)host_options.movie_presentation,(unsigned)host_options.battle_debris_3d,(unsigned)host_options.player_shadow,
#endif
        host_options.volume,
        host_options.speed_factor,
        host_options.speed_toggle ? "true" : "false",
        host_options.analog_run_threshold
    );
    Host_WriteString(file, host_options.discs[0]);
    fputs("\ndisc2 = ", file);
    Host_WriteString(file, host_options.discs[1]);

    fprintf(file,"\n\n[mods]\nhd_backgrounds = %s\n",host_options.hd_backgrounds?"true":"false");
#ifdef PE_PS5
    fprintf(file,"hd_field_materials = %s\n",host_options.hd_field_materials?"true":"false");
    fputs("hd_field_directory = ",file);Host_WriteString(file,host_options.hd_field_directory);fputc('\n',file);
    fputs("hd_title_directory = ",file);
    Host_WriteString(file,host_options.hd_title_directory);
    fputc('\n',file);
#endif
#ifdef PE_PS5_GPU_NATIVE
    fprintf(file,"hd_models = %s\nhd_model_directory = ",host_options.hd_models?"true":"false");Host_WriteString(file,host_options.hd_model_directory);
    fprintf(file,"\n\n[interface]\nshow_fps = %d\nmenu_style = %d\nmenu_text_size = %d\nmenu_pointer = %d\n",host_options.show_fps,host_options.menu_style,host_options.menu_text_size,host_options.menu_pointer);
#endif
    fputs("\n[keyboard]\n", file);
    for (unsigned i = 0; i < HOST_BINDING_COUNT; ++i) {
        fprintf(file, "%s = %d\n", host_button_names[i], host_options.keys[i]);
    }
    fputs("\n[controller]\n", file);
    for (unsigned i = 0; i < HOST_BINDING_COUNT; ++i) {
        fprintf(file, "%s = %d\n", host_button_names[i], host_options.buttons[i]);
    }

    int failed = ferror(file);
    if (fclose(file)) {
        failed = 1;
    }
    if (failed || !SDL_RenamePath(temporary, host_options.user_file)) {
        SDL_RemovePath(temporary);
        return 0;
    }
    return 1;
}

/* Bundled desktop launches have no useful writable working directory.
 * Keep PS1 card files alongside the per-user preferences, without changing
 * relative image paths or the working directory used by headless replays. */
static void Host_CardDiagnostic(const char *operation, const char *path) {
#ifdef PE_PS5
    static unsigned count;
    int error = errno;
    if (count++ < 32)
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "PS5 card failure operation=%s path=%.700s errno=%d SDL=%.400s",
                     operation, path, error, SDL_GetError());
    errno = error;
#else
    (void)operation; (void)path;
#endif
}
static int Host_CardPath(char *dst, const char *src, int capacity) {
    if (strlen(src) < 5 || src[0] != 'b' || src[1] != 'u' || src[4] != ':' ||
        src[2] < '0' || src[2] > '1' || src[3] < '0' || src[3] > '3') {
        Host_CardDiagnostic("invalid-device", src);
        return -1;
    }
    const char *slash = strrchr(host_options.user_file, '/');
    const char *backslash = strrchr(host_options.user_file, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
    if (!slash) {
        Host_CardDiagnostic("preferences-no-parent", host_options.user_file);
        return -1;
    }
    if (capacity <= 0) {
        Host_CardDiagnostic("invalid-capacity", src);
        return 0;
    }

    int n = snprintf(
        dst,
        (size_t)capacity,
        "%.*s/memory-cards/%.4s",
        (int)(slash - host_options.user_file),
        host_options.user_file,
        src
    );
    if (n < 0 || n >= capacity) { Host_CardDiagnostic("directory-format-truncated", src); dst[0] = '\0'; return 0; }
#ifdef _WIN32
    /* SDL's Win32 calls also need the extended prefix for long paths. Leave
     * short portable paths unchanged, including their usual slash spelling. */
    if (n >= 200 && dst[1] == ':' && (dst[2] == '/' || dst[2] == '\\')) {
        if (n + 4 >= capacity) { dst[0] = '\0'; return 0; }
        memmove(dst + 4, dst, n + 1); memcpy(dst, "\\\\?\\", 4); n += 4;
    } else if (n >= 200 && dst[0] == '\\' && dst[1] == '\\' && strncmp(dst, "\\\\?\\", 4)) {
        if (n + 6 >= capacity) { dst[0] = '\0'; return 0; }
        memmove(dst + 8, dst + 2, n - 1); memcpy(dst, "\\\\?\\UNC\\", 8); n += 6;
    }
    if (!strncmp(dst, "\\\\?\\", 4)) {
        for (char *p = dst; *p; ++p) if (*p == '/') *p = '\\';
        /* SDL's recursive mkdir treats a leading UNC separator as an empty
         * parent. Create ancestors individually, preserving the volume root. */
        char *parent = dst + 4;
        if (!strncmp(parent, "UNC\\", 4)) {
            parent = strchr(parent + 4, '\\');
            if (parent) parent = strchr(parent + 1, '\\');
            if (parent) ++parent;
        } else if (strlen(parent) >= 3 && parent[1] == ':') parent += 3;
        for (char *p = parent; p && *p; ++p) if (*p == '\\') {
            *p = '\0';
            int created = SDL_CreateDirectory(dst);
            *p = '\\';
            if (!created) { dst[0] = '\0'; return 0; }
        }
    }
#endif
    if (!SDL_CreateDirectory(dst)) { Host_CardDiagnostic("SDL_CreateDirectory", dst); dst[0] = '\0'; return 0; }

    const char *name = src + 5;
    if (*name == '*') {
        name = "";
    }
    char separator = '/';
#ifdef _WIN32
    if (!strncmp(dst, "\\\\?\\", 4)) separator = '\\';
#endif
    int added = snprintf(dst + n, (size_t)(capacity - n), "%c%s", separator, name);
    if (added < 0 || added >= capacity - n) {
        Host_CardDiagnostic("filename-format-truncated", src);
        dst[0] = '\0';
    }
    return 0;
}

/* Import each legacy save once, retaining every legacy file and never replacing a
 * card already in the user folder. The marker lists the legacy files earlier passes
 * considered ("bu00/<name>" lines), so saves copied beside the game after the first
 * launch are still imported while a slot deleted after its import stays deleted.
 * A 0.7 marker is empty: files that arrived before it was written count as listed. */
#define HOST_CARD_MARKER_HEADER "# Legacy saves already considered for import\n"

typedef struct HostCardImport {
    int ok;
    unsigned count, kept, rejected;
    char *created[128];
    const char *listed;     /* the marker's list, "\n"-delimited; NULL when none */
    SDL_Time before;        /* an empty 0.7 marker's write time, else 0 */
    char *added;            /* entries considered by this pass, "\n"-delimited */
    size_t added_length;
} HostCardImport;

static int Host_CardImportListed(const HostCardImport *import, const char *entry) {
    return (import->listed && strstr(import->listed, entry)) || (import->added && strstr(import->added, entry));
}

/* Record `entry` ("\nbu00/<name>\n") as considered by this pass. */
static int Host_CardImportRecord(HostCardImport *import, const char *entry) {
    size_t length = strlen(entry + 1);
    char *grown = SDL_realloc(import->added, import->added_length + length + 2);
    if (!grown) return 0;
    if (!import->added_length) grown[import->added_length++] = '\n';
    memcpy(grown + import->added_length, entry + 1, length + 1);
    import->added = grown;
    import->added_length += length;
    return 1;
}

static int Host_IsCardImage(const char *name) {
    const char *dot = strrchr(name, '.');
    static const char *const images[] = {".srm", ".mcr", ".mcd", ".gme", ".mcs", ".mc", ".psv", ".vmp"};
    for (unsigned i = 0; dot && i < sizeof(images) / sizeof(images[0]); ++i)
        if (!SDL_strcasecmp(dot, images[i])) return 1;
    return 0;
}

static SDL_EnumerationResult SDLCALL Host_ImportCard(void *userdata, const char *dirname, const char *name) {
    HostCardImport *import = userdata;
    char source[8192], target[4096], device[64], temporary[4160], entry[64];
    snprintf(source, sizeof(source), "%s%s", dirname, name);
    if (strlen(name) != 20 || strncmp(name, "BASLUS-00662", 11)) {
        if (Host_IsCardImage(name)) {
            ++import->rejected;
            fprintf(stderr, "Memory cards: %s is a memory card image, not a PsyZ save; "
                            "open it in the save editor to import its saves.\n", source);
        }
        return SDL_ENUM_CONTINUE;
    }
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(source, &info) || info.type != SDL_PATHTYPE_FILE || info.size != 8192) {
        ++import->rejected;
        fprintf(stderr, "Memory cards: %s is not an 8192-byte PsyZ save; not imported.\n", source);
        return SDL_ENUM_CONTINUE;
    }
    /* Port is encoded by the legacy directory supplied to EnumerateDirectory. */
    size_t length = strlen(dirname);
    unsigned port = length >= 4 && dirname[length - 3] == '1';
    snprintf(entry, sizeof(entry), "\nbu%u0/%s\n", port, name);
    if (Host_CardImportListed(import, entry)) return SDL_ENUM_CONTINUE;
    if (!Host_CardImportRecord(import, entry)) { import->ok = 0; return SDL_ENUM_CONTINUE; }
    SDL_Time arrived = info.create_time > info.modify_time ? info.create_time : info.modify_time;
    if (import->before && arrived <= import->before) return SDL_ENUM_CONTINUE;
    snprintf(device, sizeof(device), "bu%u0:%s", port, name);
    Host_CardPath(target, device, sizeof(target));
    if (!*target) { Host_CardDiagnostic("import-card-callback", device); import->ok = 0; return SDL_ENUM_CONTINUE; }
    if (SDL_GetPathInfo(target, &info)) {
        ++import->kept;
        fprintf(stderr, "Memory cards: kept %s; the save of the same name in %s was not imported.\n", target, dirname);
        return SDL_ENUM_CONTINUE;
    }
    if (import->count >= 128) { Host_CardDiagnostic("import-count-limit", source); import->ok = 0; return SDL_ENUM_CONTINUE; }
    size_t bytes = 0;
    void *data = SDL_LoadFile(source, &bytes);
    if (!data || bytes != 8192) { Host_CardDiagnostic("import-load-or-size", source); SDL_free(data); import->ok = 0; return SDL_ENUM_CONTINUE; }
    const char *last = strrchr(target, '/');
    const char *backslash = strrchr(target, '\\');
    if (backslash && (!last || backslash > last)) last = backslash;
    snprintf(temporary, sizeof(temporary), "%.*s.pe-save-import-%llu.tmp",
             last ? (int)(last - target + 1) : 0, target, (unsigned long long)SDL_GetTicksNS());
    SDL_IOStream *io = SDL_IOFromFile(temporary, "wbx");
    if (!io) { Host_CardDiagnostic("import-exclusive-open", temporary); SDL_free(data); import->ok = 0; return SDL_ENUM_CONTINUE; }
    int written = SDL_WriteIO(io, data, bytes) == bytes && SDL_FlushIO(io);
    if (!written) Host_CardDiagnostic("import-write-or-flush", temporary);
    if (!SDL_CloseIO(io)) { Host_CardDiagnostic("import-close", temporary); written = 0; }
    SDL_free(data);
    if (!written || !SDL_RenamePath(temporary, target)) {
        if (written) Host_CardDiagnostic("import-rename", target);
        SDL_RemovePath(temporary);
        import->ok = 0;
    } else {
        char *created = SDL_strdup(target);
        if (!created) { Host_CardDiagnostic("import-track-allocation", target); SDL_RemovePath(target); import->ok = 0; }
        else import->created[import->count++] = created;
    }
    return SDL_ENUM_CONTINUE;
}

static int Host_InstallCardPaths(const char *base) {
    char marker[4096], card[4096];
    if (Host_CardPath(card, "bu00:*", sizeof(card)) || !*card) { Host_CardDiagnostic("install-card-callback", "bu00:*"); return 0; }
    snprintf(marker, sizeof(marker), "%.*s.legacy-imported", (int)strlen(card) - 5, card);
    HostCardImport import = {.ok = 1};
    SDL_PathInfo info;
    size_t bytes = 0;
    char *list = NULL;
    if (SDL_GetPathInfo(marker, &info)) {
        if (!(list = SDL_LoadFile(marker, &bytes))) return 0;
        if (!bytes) import.before = info.modify_time;
    }
    /* "\n" + list + "\n" lets every entry be found as "\n<entry>\n". */
    char *listed = SDL_malloc(bytes + 3);
    if (!listed) { SDL_free(list); return 0; }
    listed[0] = '\n';
    memcpy(listed + 1, list ? list : "", bytes);
    listed[bytes + 1] = '\n';
    listed[bytes + 2] = '\0';
    import.listed = listed;

#ifdef PE_PS5
    char *cwd = SDL_malloc(strlen(host_data_root) + 2);
    if (!cwd) { Host_CardDiagnostic("import-root-allocation", host_data_root); SDL_free(listed); SDL_free(list); return 0; }
    snprintf(cwd, strlen(host_data_root) + 2, "%s/", host_data_root);
    if (base && base[0] != '/') base = NULL;
#else
    char *cwd = SDL_GetCurrentDirectory();
#endif
    const char *roots[] = {cwd, base};
    for (unsigned root = 0; root < 2; ++root) if (roots[root]) {
        for (unsigned port = 0; port < 2; ++port) {
            char source[8192];
            snprintf(source, sizeof(source), "%sbu%u0", roots[root], port);
            if (SDL_GetPathInfo(source, &info) && info.type == SDL_PATHTYPE_DIRECTORY &&
                !SDL_EnumerateDirectory(source, Host_ImportCard, &import)) { Host_CardDiagnostic("import-enumerate", source); import.ok = 0; }
        }
    }
    SDL_free(cwd);
    /* Rewrite the marker when this pass considered anything, and always replace an
     * empty 0.7 marker or a missing one, so the list format takes over. */
    if (import.ok && (!list || !bytes || import.added_length)) {
        size_t header = strlen(HOST_CARD_MARKER_HEADER);
        int has_header = bytes >= header && !memcmp(list, HOST_CARD_MARKER_HEADER, header);
        size_t extra = import.added_length ? import.added_length - 1 : 0;
        size_t total = (has_header ? 0 : header) + bytes + extra;
        char *text = SDL_malloc(total + 1);
        if (!text) import.ok = 0;
        else {
            size_t at = 0;
            if (!has_header) { memcpy(text, HOST_CARD_MARKER_HEADER, header); at = header; }
            if (bytes) { memcpy(text + at, list, bytes); at += bytes; }
            if (extra) { memcpy(text + at, import.added + 1, extra); at += extra; }
            /* Replace the list whole: a torn write would bring deleted slots back. */
            char temporary[4160];
            snprintf(temporary, sizeof(temporary), "%s.tmp", marker);
            if (!SDL_SaveFile(temporary, text, at) || !SDL_RenamePath(temporary, marker)) {
                SDL_RemovePath(temporary);
                import.ok = 0;
            }
            SDL_free(text);
        }
    }
    for (unsigned i = 0; i < import.count; ++i) {
        if (!import.ok) SDL_RemovePath(import.created[i]);
        SDL_free(import.created[i]);
    }
    SDL_free(import.added);
    SDL_free(listed);
    SDL_free(list);
    if (!import.ok) return 0;
    if (import.count || import.kept)
        fprintf(stderr, "Memory cards: imported %u old save(s); kept %u current save(s) over older copies.\n",
                import.count, import.kept);
    fprintf(stderr, "Memory cards: %.*s (bu00 is card 1, bu10 card 2)\n", (int)strlen(card) - 6, card);
    Psyz_AdjustPathCB(Host_CardPath);
    return 1;
}

static void Host_InitOptions(int enabled) {
    host_options.enabled = enabled;
    host_options.scale = 1;
    host_options.geometry_correction = 1;
    host_options.antialiasing = 0;
    host_options.model_filtering = 0;
    host_options.dynamic_lighting = 0;
    host_options.effect_geometry_correction = 0;
#ifdef PE_PS5_GPU_NATIVE
    host_options.frame_interpolation = 1; /* Owner requested display-only 60 Hz; saved explicit choice loads afterward. */
    host_options.widescreen = 1; /* Genuine wide default only when no saved choice. */
#else
    host_options.frame_interpolation = 0;
    host_options.widescreen = 0;
#endif
#ifdef PE_PS5_GPU_NATIVE
    host_options.show_fps=1; /* Temporary owner-requested display counter. */
    host_options.player_shadow=0; /* Optional experimental shadow remains Original, even in fresh Modern. */
    host_options.triangle_menu_font=0;host_options.dialogue_text=0;host_options.particle_lighting=0;host_options.menu_style=1;host_options.menu_text_size=1;host_options.menu_pointer=2;host_options.hd_models=0;SDL_strlcpy(host_options.hd_model_directory,"/mnt/usb0/parasite-eve-data/mods/characters/duck-models",sizeof host_options.hd_model_directory);
    host_options.sprite_filtering=0;host_options.movie_cleanup=0;host_options.gameplay_fill=1;host_options.fsr1=0;host_options.fsr1_sharpness=1;host_options.model_fill_lighting=0;host_options.battle_debris_3d=0;host_options.movie_blur=1;host_options.movie_presentation=0; /* New setting absent: Stretch16:9; legacy blur flag cannot override it. */
#endif
#ifdef PE_PS5
    host_options.hd_backgrounds = 0; /* Original until explicitly selected. */
    host_options.hd_title_directory[0]=0;
    host_options.hd_field_materials=0;
    host_options.hd_field_directory[0]=0;
#else
    host_options.hd_backgrounds = 1;
#endif
    host_options.volume = 100;
    host_options.speed_factor = 2;
    host_options.speed_toggle = 0;
    host_options.analog_run_threshold = 85;
    Boot_InitHostBindings();
#ifdef PE_PS5
    /* Public ScePad mapping exposes touchpad; there is no physical Back. */
    Psyz_SetGamepadBinding_SDL3(8, SDL_GAMEPAD_BUTTON_TOUCHPAD);
#endif
    for (unsigned i = 0; i < 16; ++i) {
        host_options.keys[i] = Psyz_GetKeyboardBinding_SDL3(i);
        host_options.buttons[i] = Psyz_GetGamepadBinding_SDL3(i);
    }
    host_options.keys[HOST_SPEED_BINDING] = HOST_UNBOUND_KEY;
    host_options.buttons[HOST_SPEED_BINDING] = HOST_UNBOUND_BUTTON;
    #ifdef PE_PS5_GPU_NATIVE
    /* Fresh Modern baseline. Saved individual choices load afterward. */
    host_options.scale=8;host_options.antialiasing=1;host_options.dynamic_lighting=1;host_options.effect_geometry_correction=1;host_options.movie_cleanup=2;host_options.fsr1=1;host_options.fsr1_sharpness=0;host_options.model_fill_lighting=2;host_options.model_filtering=1;host_options.hd_backgrounds=1;host_options.hd_models=1;
    host_options.dialogue_text=1;host_options.triangle_menu_font=1;host_options.battle_debris_3d=0;host_options.particle_lighting=1;
    host_options.dither=1;host_options.movie_presentation=1;
    #endif
    if (!enabled) {
#ifdef PE_PS5_GPU_NATIVE
        Host_VisualLoaded();
#endif
        return;
    }

#ifdef PE_PS5
    char path[4096];
    if (Host_DataPath(path, sizeof(path), "parasite-eve.toml") &&
        strcmp(path, host_options.user_file)) Host_LoadOptions(path);
    Host_LoadOptions(host_options.user_file);
#else
    const char *base = SDL_GetBasePath();
    char path[4096];
    if (base) {
        snprintf(path, sizeof(path), "%sparasite-eve.toml", base);
        Host_LoadOptions(path);
#ifdef __APPLE__
        /* SDL returns Contents/Resources for a bundled macOS executable. */
        if (strstr(base, ".app/Contents/Resources/")) {
            snprintf(path, sizeof(path), "%s../../../parasite-eve.toml", base);
            Host_LoadOptions(path);
        }
#endif
    }

    /* Development/portable launches may keep the file in the working directory. */
    if (!base || strcmp(base, "./")) {
        Host_LoadOptions("parasite-eve.toml");
    }

    const char *override = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PE_CONFIG_HOME");
    if (override && *override) {
        SDL_CreateDirectory(override);
        snprintf(host_options.user_file, sizeof(host_options.user_file), "%s/parasite-eve.toml", override);
    } else {
#if defined(__linux__) && !defined(__ANDROID__)
        const char *root = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "XDG_CONFIG_HOME");
        if (root && *root) {
            snprintf(path, sizeof(path), "%s/parasite-eve", root);
        } else {
            root = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "HOME");
            if (root) {
                snprintf(path, sizeof(path), "%s/.config/parasite-eve", root);
            }
        }
        if (root) {
            SDL_CreateDirectory(path);
            snprintf(host_options.user_file, sizeof(host_options.user_file), "%s/parasite-eve.toml", path);
        }
#else
        char *pref = SDL_GetPrefPath("", "Parasite Eve");
        if (pref) {
            snprintf(host_options.user_file, sizeof(host_options.user_file), "%sparasite-eve.toml", pref);
            SDL_free(pref);
        }
#endif
    }

    if (*host_options.user_file) {
        Host_LoadOptions(host_options.user_file);
    }
    if (*host_options.user_file && !Host_InstallCardPaths(base)) {
        fprintf(stderr, "Cannot prepare user memory cards; legacy cards remain in use.\n");
    }
#endif
#ifdef PE_PS5_GPU_NATIVE
    Host_VisualLoaded();
#endif
}

/* Selection ownership is host_options; the renderer may still bound a genuine
 * wide view to available room artwork. Report these as different states. */
static void Host_TraceAspectSelection(const char *phase, int force) {
#if defined(PE_PS5_GPU_NATIVE)
    static int last_aspect=-1, last_wide=-1;
    int requested=host_options.widescreen ? PSYZ_ASPECT_DISPLAY : host_options.aspect;
    int effective_aspect=Psyz_VideoGetAspectMode();
    int effective_wide=Psyz_VideoGetWidescreen();
    if (force || requested!=last_aspect || host_options.widescreen!=last_wide ||
        requested!=effective_aspect || !!host_options.widescreen!=!!effective_wide) {
        SDL_Log("[GPU aspect selection] phase=%s requested_aspect=%d requested_wide=%d committed=%d runtime_aspect=%d runtime_wide=%d viewport=next_prepared_present",
            phase,requested,host_options.widescreen,
            requested==effective_aspect && !!host_options.widescreen==!!effective_wide,
            effective_aspect,effective_wide);
        Psyz_GpuTraceAspectOnce(phase);
        last_aspect=requested;last_wide=host_options.widescreen;
    }
#else
    (void)phase; (void)force;
#endif
}

static void Host_ApplyOptions(SDL_Window *window) {
#ifdef PE_PS5_GPU_NATIVE
    Psyz_VideoSetShutdownCallback(Host_NativeApplicationShutdown);
    Host_VisualSafety();
#endif
#ifdef PE_PS5_GPU_NATIVE
    /* Native PS5 output owns the entire television; not a gameplay aspect toggle. */
    host_options.fullscreen = 1;
#endif
#if defined(PE_PS5) && !defined(PE_PS5_GPU_NATIVE)
    if(Psyz_CpuGeometryConfigure(host_options.scale)<0)host_options.scale=1;
    host_options.geometry_correction = 0;
    host_options.antialiasing = 0;
    host_options.model_filtering = 0;
    host_options.fullscreen = 0;
#endif
    g_FieldAnalogRunThreshold = host_options.analog_run_threshold;
#if defined(PE_PS5) && !defined(PE_PS5_GPU_NATIVE)
    Psyz_VideoSetInternalResolution(1); /* GPU-only resolution stays native. */
#else
    Psyz_VideoSetInternalResolution(host_options.scale);
#endif
    Psyz_VideoSetAspectMode(host_options.widescreen ? PSYZ_ASPECT_DISPLAY : (PsyzAspectMode)host_options.aspect);
    Psyz_VideoSetDitheringMode((PsyzDitherMode)host_options.dither);
    Psyz_GpuSetFrameInterpolation(host_options.frame_interpolation);
    Psyz_VideoSetGeometryCorrection(host_options.geometry_correction != 0);
    Psyz_VideoSetAntialiasing(host_options.antialiasing != 0);
    Psyz_VideoSetModelFiltering(host_options.model_filtering != 0);
    Psyz_VideoSetDynamicLighting(host_options.dynamic_lighting != 0);
    Psyz_VideoSetEffectGeometryCorrection(host_options.effect_geometry_correction != 0);
    /* An allocation failure keeps the last working renderer configuration.
     * Before window creation the getters do not yet reflect pending options. */
    if (window) {
#ifndef PE_PS5_GPU_NATIVE
        host_options.scale = Psyz_VideoGetInternalResolution();
        host_options.antialiasing = Psyz_VideoGetAntialiasing();
        host_options.geometry_correction = Psyz_VideoGetGeometryCorrection();
        host_options.model_filtering = Psyz_VideoGetModelFiltering();
#endif
        host_options.dynamic_lighting = Psyz_VideoGetDynamicLighting();
        host_options.effect_geometry_correction = Psyz_VideoGetEffectGeometryCorrection();
    }
    Psyz_VideoSetWidescreen(host_options.widescreen != 0);
#ifdef PE_PS5_GPU_NATIVE
    static int appliedBackgrounds=-1;
    if(appliedBackgrounds!=host_options.hd_backgrounds){Host_HdApply();appliedBackgrounds=host_options.hd_backgrounds;}
    if(Psyz_GpuSetHdModels(host_options.hd_models,host_options.hd_model_directory)<0){host_options.hd_models=0;Psyz_GpuSetHdModels(0,NULL);SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"HD player texture root rejected; original models retained");}
    Psyz_VideoSetFsr1(host_options.fsr1,host_options.fsr1_sharpness);
    Psyz_VideoSetSpriteFiltering(host_options.sprite_filtering);
    if(window)host_options.sprite_filtering=Psyz_VideoGetSpriteFiltering();
    Psyz_VideoSetMovieCleanup(host_options.movie_cleanup);
    Psyz_VideoSetGameplayFill(host_options.gameplay_fill);
    Psyz_VideoSetMoviePresentation(host_options.movie_presentation);
    Psyz_VideoSetMovieBlur(host_options.movie_presentation==1);
    FieldFx_SetParticleAugment(host_options.battle_debris_3d);
    FieldFx_SetParticleLighting(host_options.particle_lighting);
    PeDialogue_SetEnabled(host_options.dialogue_text);
    PeMenuFont_SetEnabled(host_options.triangle_menu_font);
    static int lastTriangleFont=-1;if(lastTriangleFont!=host_options.triangle_menu_font){SDL_Log("[Triangle menu font] requested=%d configured=%d prepared=%d policy=canonical-font-alpha/original-vertex-tint; original-FT4-CLUT-preserved; pixels=unobserved",host_options.triangle_menu_font,PeMenuFont_GetEnabled(),PeMenuFont_GetEffective());lastTriangleFont=host_options.triangle_menu_font;}
    static int lastDialogue=-1;if(lastDialogue!=host_options.dialogue_text){SDL_Log("[Dialogue font] requested=%d configured=%d prepared=%d scope=supported Latin dialogue; pixels=unobserved",host_options.dialogue_text,PeDialogue_GetEnabled(),PeDialogue_GetEffective());lastDialogue=host_options.dialogue_text;}
    static int lastParticleLights=-1;if(lastParticleLights!=host_options.particle_lighting){SDL_Log("[Particle lights] requested=%d configured=%d extra_geometry=%d eligible=%d scope=source-known sparks/flame on actors; pixels=unobserved",host_options.particle_lighting,FieldFx_GetParticleLighting(),FieldFx_GetParticleAugment(),FieldFx_GetParticleLighting()&&FieldFx_GetParticleAugment()!=0);lastParticleLights=host_options.particle_lighting;}
    PeShadow_SetStyle(host_options.player_shadow);host_options.player_shadow=PeShadow_GetStyle();
    static int lastShadowStyle=-1;if(lastShadowStyle!=host_options.player_shadow){SDL_Log("[Player shadow] configured=%d 0Original-1ProjectedMesh scope=player/known-horizontal-floor fixed-light fallback=original pixels=unobserved",host_options.player_shadow);lastShadowStyle=host_options.player_shadow;}
    Render_SetModelFillLighting(host_options.model_fill_lighting);
    static int lastModelFill=-1;
    if(lastModelFill!=host_options.model_fill_lighting){
        SDL_Log("[Model fill] requested=%d scope=lit models/props; colour only; original GTE lighting retained; rendered=unobserved",host_options.model_fill_lighting);
        lastModelFill=host_options.model_fill_lighting;
    }
#endif
    Host_TraceAspectSelection("apply", 0);
    Psyz_AudioSetOutputVolume(host_options.volume);
    for (unsigned i = 0; i < 16; ++i) {
        Psyz_SetKeyboardBinding_SDL3(i, (SDL_Scancode)host_options.keys[i]);
        Psyz_SetGamepadBinding_SDL3(i, host_options.buttons[i]);
    }
    if (window) {
        Psyz_SetFullscreen_SDL3(host_options.fullscreen != 0);
    }
}

typedef struct {
    SDL_AtomicInt done;
    char *paths[2];
    int failed;
} HostDiscDialog;

static void SDLCALL Host_DiscChosen(void *userdata, const char *const *files, int filter) {
    HostDiscDialog *dialog = userdata;
    (void)filter;
    if (!files) {
        dialog->failed = 1;
    } else {
        for (unsigned i = 0; files[i]; ++i) {
            if (i >= 2) {
                dialog->failed = 1;
                break;
            }
            if (strlen(files[i]) >= sizeof(host_options.discs[i])) {
                dialog->failed = 1;
                break;
            }
            dialog->paths[i] = SDL_strdup(files[i]);
            if (!dialog->paths[i]) {
                dialog->failed = 1;
            }
        }
    }
    SDL_SetAtomicInt(&dialog->done, 1);
}

static int Host_ChooseDiscs(const char **paths) {
#ifdef PE_PS5
    (void)paths;
    fprintf(stderr, "Configure disc paths in parasite-eve.toml or pass --disc/--disc2.\n");
    return -1;
#endif
    static const SDL_DialogFileFilter filters[] = {
        {"Parasite Eve - select both Disc 1 and Disc 2 CUE images", "cue;bin"}
    };
    HostDiscDialog dialog = {0};
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return -1;
    }
    SDL_ShowOpenFileDialog(Host_DiscChosen, &dialog, NULL, filters, 1, NULL, true);
    while (!SDL_GetAtomicInt(&dialog.done)) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }

    int result = dialog.failed ? -1 : dialog.paths[0] ? 1 : 0;
    if (result == 1) {
        for (unsigned i = 0; i < 2; ++i) {
            snprintf(
                host_options.discs[i],
                sizeof(host_options.discs[i]),
                "%s",
                dialog.paths[i] ? dialog.paths[i] : ""
            );
            paths[i] = *host_options.discs[i] ? host_options.discs[i] : NULL;
        }
    }
    for (unsigned i = 0; i < 2; ++i) {
        SDL_free(dialog.paths[i]);
    }
    return result;
}

/* Main owns the two streams; adding an absent volume never replaces one that
 * the original asynchronous archive reader might still be using. */
static PeDisc *host_images;
#ifdef PE_PS5_GPU_NATIVE
static int host_shutdown_complete;
static void Host_NativeApplicationShutdown(void){
    if(host_shutdown_complete)return;host_shutdown_complete=1;
    SDL_Log("[Native quit] stage=game-cleanup-begin");
    pe_mod_panel_set_native_close(NULL);s_overlayState.open=0;
    Host_HdStop();
    if(g_BootDisplayPlaybackActive)BootDisplay_WaitForCdAndRestoreCallbacks();
#ifdef PE_MODS
    extern void pe_mods_shutdown_now(void);pe_mods_shutdown_now();
#endif
    Psyz_AudioDestroy();CdRom_BindImage(NULL,NULL);
    g_CdImageVolumes[0]=g_CdImageVolumes[1]=NULL;g_CdImageRequest=NULL;
    g_BootMovieSkipConfirm=NULL;g_BootSceneStarted=NULL;
    if(host_images){pe_disc_close(&host_images[0]);pe_disc_close(&host_images[1]);host_images=NULL;}
    free(g_BootHostHealthEvents);g_BootHostHealthEvents=NULL;g_BootHostHealthEventCount=g_BootHostHealthApplied=0;
    free(g_BootHostPadEvents);g_BootHostPadEvents=NULL;g_BootHostPadEventCount=0;
    SDL_Log("[Native quit] stage=game-cleanup-end");
}
#endif

static int Host_SelectImage(unsigned identity, char *path, size_t size) {
#ifdef PE_PS5
    (void)identity; (void)path; (void)size;
    fprintf(stderr, "Disc image missing; configure both disc paths before startup.\n");
    return -1;
#endif
    static const SDL_DialogFileFilter filters[] = {
        {"Parasite Eve USA CUE/BIN image", "cue;bin"}
    };
    HostDiscDialog dialog = {0};
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return -1;
    }
    SDL_PropertiesID props = SDL_CreateProperties();
    if (!props) {
        return -1;
    }

    char title[100];
    snprintf(title, sizeof(title), "Parasite Eve - select USA Disc %u", identity);
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, (void *)filters);
    SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 1);
    SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title);
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, Psyz_GetWindow_SDL3());
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE, Host_DiscChosen, &dialog, props);
    while (!SDL_GetAtomicInt(&dialog.done)) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    SDL_DestroyProperties(props);

    int result = dialog.failed ? -1 : dialog.paths[0] ? 1 : 0;
    if (result == 1) {
        if (strlen(dialog.paths[0]) >= size) {
            result = -1;
        } else {
            snprintf(path, size, "%s", dialog.paths[0]);
        }
    }
    SDL_free(dialog.paths[0]);
    SDL_free(dialog.paths[1]);
    return result;
}

static int Host_InstallImage(unsigned identity, const char *path) {
    if (identity < 1 || identity > 2 || !host_images || g_CdImageVolumes[identity - 1]) {
        return 0;
    }
    unsigned slot = host_images[0].stream ? 1 : 0;
    if (host_images[slot].stream) {
        return 0;
    }

    PeDisc *image = &host_images[slot];
    unsigned found = 0;
    if (!Host_OpenDisc(image, path)) {
        return 0;
    }

    /* Inspection deliberately leaves the bound image and original tables alone. */
    if (Asset_ValidatePeImage(image, &found) || found != identity) {
        pe_disc_close(image);
        return 0;
    }

    g_CdImageVolumes[identity - 1] = image;
    snprintf(host_options.discs[identity - 1], sizeof(host_options.discs[identity - 1]), "%s", path);
    Host_SaveOptions();
    return 1;
}

static int Host_AddImage(unsigned identity, int required) {
    if (identity < 1 || identity > 2 || !host_images) {
        return 0;
    }
    if (g_CdImageVolumes[identity - 1]) {
        return 1;
    }
#ifdef PE_PS5
    (void)required;
    fprintf(stderr, "Disc %u is unavailable; configure both disc images before console startup.\n", identity);
    return 0;
#endif

    Psyz_PauseHostClock_SDL3(true);
    int paused = Psyz_AudioIsPaused();
    Psyz_AudioPause();
    int result = 0;

    char title[80];
    char message[300];
    char path[4096];
    snprintf(title, sizeof(title), "Parasite Eve - Disc %u required", identity);
    snprintf(
        message,
        sizeof(message),
        "Select the Parasite Eve (USA) Disc %u CUE or BIN image. The current image will remain mounted.",
        identity
    );
    if (required) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, message, Psyz_GetWindow_SDL3());
    }

    for (;;) {
        int selected = Host_SelectImage(identity, path, sizeof(path));
        if (selected > 0 && Host_InstallImage(identity, path)) {
            result = 1;
            break;
        }
        if (selected > 0) {
            snprintf(
                message,
                sizeof(message),
                "This is not a valid Parasite Eve (USA) Disc %u image. Please select Disc %u.",
                identity,
                identity
            );
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, Psyz_GetWindow_SDL3());
            continue;
        }
        if (!required) {
            break;
        }
        snprintf(
            message,
            sizeof(message),
            "Parasite Eve (USA) Disc %u is needed to continue. Choose its image, or quit the game. Unsaved progress is lost if you quit.",
            identity
        );
        SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose image"},
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit game"}
        };
        SDL_MessageBoxData box = {
            .flags = SDL_MESSAGEBOX_WARNING,
            .window = Psyz_GetWindow_SDL3(),
            .title = title,
            .message = message,
            .numbuttons = 2,
            .buttons = buttons
        };
        int choice = 0;
        if (!SDL_ShowMessageBox(&box, &choice) || choice != 1) {
            break;
        }
    }

    SDL_ResetKeyboard();
    Psyz_PauseHostClock_SDL3(false);
    if (!paused) {
        Psyz_AudioUnpause();
    }
    return result;
}

static int Host_RequestImage(unsigned identity) {
    return Host_AddImage(identity, 1);
}

static void Host_OverlayRenderFallback(
    SDL_Renderer *renderer,
    SDL_Window *window,
    HostOverlayState *state,
    SDL_Texture **texture,
    int *texW,
    int *texH
) {
    int w = 0, h = 0;
    OverlayLayout layout;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w <= 0) {
        w = 640;
    }
    if (h <= 0) {
        h = 600;
    }
    OverlayComputeStateLayout(state, w, h, &layout);
    s_overlayLayout = layout;
    if (OverlayFontReady() && OverlaySurfaceResize(&s_overlaySurface, layout.w, layout.h)) {
        if (state->prompt) {
            OverlayPaintPrompt(&s_overlaySurface, state, &layout);
        } else {
            OverlayPaint(&s_overlaySurface, state, &layout);
        }
        if (*texture == NULL || *texW != layout.w || *texH != layout.h) {
            if (*texture != NULL) {
                SDL_DestroyTexture(*texture);
            }
            *texture = SDL_CreateTexture(
                renderer,
                SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_STREAMING,
                layout.w,
                layout.h
            );
            *texW = layout.w;
            *texH = layout.h;
            if (*texture != NULL) {
                SDL_SetTextureBlendMode(*texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
            }
        }
        if (*texture != NULL) {
            SDL_UpdateTexture(*texture, NULL, s_overlaySurface.pixels, layout.w * 4);
        }
    }
    SDL_SetRenderDrawColor(renderer, 12, 14, 18, 255);
    SDL_RenderClear(renderer);
    if (*texture != NULL) {
        SDL_FRect dst = {
            (float)layout.x, (float)layout.y, (float)layout.w, (float)layout.h
        };
        SDL_RenderTexture(renderer, *texture, NULL, &dst);
    }
    SDL_RenderPresent(renderer);
#ifdef PE_PS5
    SDL_UpdateWindowSurface(window);
#endif
}

static void Host_SwitchSettingsPage(HostOverlayState *state, int nextPage) {
    int normalized = (nextPage % OVERLAY_PAGE_COUNT + OVERLAY_PAGE_COUNT) % OVERLAY_PAGE_COUNT;
    if (state->page != normalized) {
#ifdef PE_PS5_GPU_NATIVE
        state->page_rows[state->page]=state->row;state->page_scrolls[state->page]=state->scroll;
        state->page=normalized;state->row=state->page_rows[normalized];state->scroll=state->page_scrolls[normalized];
        if(state->row>=Host_SettingsTotalRowCount(normalized))state->row=0;
        Host_EnsureControlsRowVisible(state);
#else
        state->page = normalized;state->row = 0;state->scroll = 0;state->bind_device=HOST_BIND_KEYBOARD;
#endif
        state->capturing_binding = 0;
        state->status = NULL;
        state->dirty = 1;
    }
}

#ifdef PE_PS5_GPU_NATIVE
static int Host_SettingsBack(HostOverlayState *state){if(Host_RegistryBack(state))return 1;if(state->page==OVERLAY_PAGE_AUDIO){Host_SwitchSettingsPage(state,OVERLAY_PAGE_GRAPHICS);return 1;}return 0;}
#endif

static void Host_ApplySettingsAction(
    HostOverlayState *state,
    int action,
    int activate_only,
    SDL_Window *game
) {
    int pureCount = Host_SettingsPureRowCount(state->page);
    if (state->row >= pureCount) {
        if (activate_only) {
            if (state->row == pureCount
#ifdef PE_PS5_GPU_NATIVE
                ||state->row==pureCount+1
#endif
            ) {
#ifdef PE_PS5_GPU_NATIVE
                if(state->row==pureCount+1&&Host_SettingsBack(state))return;
#endif
                state->open = 0;
            } else {
                state->quit = 1;
                state->open = 0;
            }
        }
        return;
    }

#ifdef PE_PS5_GPU_NATIVE
    if(state->page==HOST_PAGE_GRAPHICS&&state->row==1){if(activate_only)Host_SwitchSettingsPage(state,OVERLAY_PAGE_AUDIO);return;}
    if(state->page==HOST_PAGE_MODS||state->page==HOST_PAGE_CHEATS||state->page==HOST_PAGE_STATES||(state->page==HOST_PAGE_IMAGES&&state->row>=3)){
        state->status=NULL;
        if(Host_ModernAction(state->page,state->row,action,activate_only))state->open=0;
        state->dirty=1;return;
    }
#endif
    int declCount = 0;
    const OverlaySetting *settings = OverlayPageSettings(state->page, &declCount);
    if (settings != NULL && state->row >= 0 && state->row < declCount) {
        const OverlaySetting *setting = &settings[state->row];
#ifdef PE_PS5_GPU_NATIVE
        if((setting->set==SetFsrStatus||setting->set==SetDialogueStatus||setting->set==SetTriangleFontStatus))return;
#endif
        int index = setting->get(); /* User intent is relative to the currently displayed value. */
        if (state->page == OVERLAY_PAGE_AUDIO
#ifndef PE_PS5_GPU_NATIVE
            
#else
            &&0
#endif
        ) {
            index = ClampIndex(index + action, setting->count);
        } else {
            index = (index + action + setting->count) % setting->count;
        }
        #ifdef PE_PS5_GPU_NATIVE
        /* Restore other Modern choices, then apply only this explicitly edited field. */
        if(state->page==OVERLAY_PAGE_AUDIO&&setting->set!=SetShowFps&&!host_presentation)SetPresentation(1);
        #endif
        setting->set(index);
#ifdef PE_PS5
        SDL_Log("[V2 DIRECT DISPLAY toggle] %s requested=%s stored_index=%d",setting->label,setting->names[index],setting->get());
#endif
    } else if (state->page == HOST_PAGE_MODS) {
#ifdef PE_PS5_GPU_NATIVE
#ifdef PE_MODS
        if(state->row==4){
            if(activate_only){host_open_assists=1;state->open=0;}
            return;
        }
#endif
        if(state->row==5){
            host_options.battle_debris_3d=(host_options.battle_debris_3d+action+3)%3;
            FieldFx_SetParticleAugment(host_options.battle_debris_3d);
            SDL_Log("[3D falling debris] requested=%d scope=FallingSprite+BurstOrb-debris2-3-render-only 0Original1Hybrid2Geometry-only-original-on-failure",host_options.battle_debris_3d);
            state->status=NULL;state->dirty=1;return;
        }
        if(state->row==3){
            host_options.movie_presentation=host_options.movie_presentation==0?1:0;
            host_options.movie_blur=host_options.movie_presentation==1;
            Psyz_VideoSetMoviePresentation(host_options.movie_presentation);
            Psyz_VideoSetMovieBlur(host_options.movie_blur);
            SDL_Log("[Movie presentation] mode=%d 0Stretch16:9-1Aspect+blur-2Originalbars scope=RGB24",host_options.movie_presentation);
            state->status=NULL;state->dirty=1;return;
        }
#endif
#ifdef PE_PS5
        if(activate_only || state->row<=1)Host_HdAction(state->row);
#else
        if(activate_only || state->row==0)Host_HdAction(state->row);
#endif
        state->status=Host_HdStatus();state->dirty=1;return;
    } else if (state->page == HOST_PAGE_IMAGES) {
        if (!activate_only) {
            return;
        }
        if (g_CdImageVolumes[state->row]) {
            state->status = "This image is already available.";
        } else {
            state->status = Host_AddImage((unsigned)state->row + 1, 0)
                                ? "Image added and remembered."
                                : "No image added.";
        }
    } else if (state->page == HOST_PAGE_CONTROLS && state->row == HOST_ANALOG_RUN_ROW) {
        int threshold = (int)host_options.analog_run_threshold + action * 5;
        if (threshold < 0) threshold = 0;
        if (threshold > 100) threshold = 100;
        host_options.analog_run_threshold = (unsigned)threshold;
    } else if (state->page == HOST_PAGE_CONTROLS && state->row == HOST_SPEED_MODE_ROW) {
        host_options.speed_toggle = !host_options.speed_toggle;
        Host_ResetSpeedHold();
    } else if (state->row == 0) {
        state->bind_device = (state->bind_device == HOST_BIND_CONTROLLER)
                                 ? HOST_BIND_KEYBOARD
                                 : HOST_BIND_CONTROLLER;
    } else if (state->row == HOST_SPEED_BINDING + 1 && !activate_only) {
        int factor = (int)host_options.speed_factor + action;
        if (factor < 2 || factor > PSYZ_SPEED_MAX) {
            return;
        }
        host_options.speed_factor = (unsigned)factor;
    } else if (activate_only) {
        state->capturing_binding = 1;
        state->status = NULL;
    } else {
        return;
    }
    state->dirty = 1;
    Host_ApplyOptions(game);
}

/* Only the speed binding can be left unbound. */
static void Host_ClearSpeedBinding(HostOverlayState *state) {
    if (state->page != HOST_PAGE_CONTROLS || state->row != HOST_SPEED_BINDING + 1) {
        return;
    }
    if (state->bind_device == HOST_BIND_KEYBOARD) {
        host_options.keys[HOST_SPEED_BINDING] = HOST_UNBOUND_KEY;
    } else {
        host_options.buttons[HOST_SPEED_BINDING] = HOST_UNBOUND_BUTTON;
    }
    state->status = NULL;
    state->dirty = 1;
}

/* The headless control console uses the same actions as the actual menu. */
static void Host_ControlHd(int enabled,const char *directory) {
    host_hd.development=0;
    SDL_strlcpy(host_hd.active,directory,sizeof(host_hd.active));
    host_hd.enabled=enabled;
    Host_HdApply();
}

static void Host_ControlGraphics(int geometry, int antialiasing, int filtering) {
    const int values[3] = {geometry, antialiasing, filtering};
    int (*const getters[3])(void) = {GetGeometryCorrection, GetAntialiasing, GetModelFiltering};
    HostOverlayState state = {.open=1, .page=HOST_PAGE_GRAPHICS};
    SDL_Window *window=Psyz_GetWindow_SDL3();
    /* Native Graphics has additional rows: find the actual option identity. */
    for (int i=0;i<3;++i) {
        for(int row=0;row<COUNT_OF(kGraphics);++row) {
            if(kGraphics[row].get!=getters[i])continue;
            state.row=row;
            if(kGraphics[row].get()!=values[i])Host_ApplySettingsAction(&state,1,0,window);
            break;
        }
    }
}

static void Host_ProcessSettingsPointer(
    HostOverlayState *state,
    float windowX,
    float windowY,
    int click,
    SDL_Window *game,
    SDL_Window *fallbackWindow
) {
    SDL_Window *targetWin = fallbackWindow ? fallbackWindow : game;
    float density = targetWin ? SDL_GetWindowPixelDensity(targetWin) : 1.0f;
    if (density <= 0.0f) {
        density = 1.0f;
    }
    float scale = s_overlayLayout.scale > 0.0f ? s_overlayLayout.scale : 1.0f;
    float x = (windowX * density - (float)s_overlayLayout.x) / scale;
    float y = (windowY * density - (float)s_overlayLayout.y) / scale;

    for (int p = 0; p < OVERLAY_PAGE_COUNT; p++) {
        OverlayRect tab = OverlayTabRect(p);
        if (x >= tab.x && x < tab.x + tab.w && y >= tab.y && y < tab.y + tab.h) {
            if (click) {
                Host_SwitchSettingsPage(state, p);
            }
            return;
        }
    }

    int totalRows = Host_SettingsTotalRowCount(state->page);
    int hitRow = -1;
    for (int r = 0; r < totalRows; r++) {
        OverlayRect rect = OverlayRowRect(state, r);
        if (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h) {
            hitRow = r;
            break;
        }
    }
    if (hitRow >= 0 && state->row != hitRow) {
        state->row = hitRow;
        state->dirty = 1;
    }
    if (!click) {
        return;
    }
    int designH = OverlayDesignHeight(Host_SettingsVisibleRowCount(state->page));
    if (x < 0.0f || x >= (float)OVERLAY_PANEL_W || y < 0.0f || y >= (float)designH) {
        state->open = 0;
        return;
    }
    if (state->page == HOST_PAGE_CONTROLS && hitRow == HOST_SPEED_BINDING + 1 &&
        x < (float)(OVERLAY_PAD + OVERLAY_SPEED_STEP_W)) {
        int action = x < (float)(OVERLAY_PAD + OVERLAY_SPEED_STEP_CENTRE) ? -1 : 1;
        Host_ApplySettingsAction(state, action, 0, game);
        return;
    }
    if (hitRow >= 0) {
        int pureCount = Host_SettingsPureRowCount(state->page);
        int isStepper = hitRow < pureCount &&
                        !(state->page == HOST_PAGE_CONTROLS && hitRow >= 1 && hitRow != HOST_ANALOG_RUN_ROW &&
                          hitRow != HOST_SPEED_MODE_ROW) &&
                        state->page != HOST_PAGE_IMAGES &&
                        !(state->page==HOST_PAGE_MODS && hitRow>0
#ifdef PE_PS5_GPU_NATIVE
                          && hitRow!=3 && hitRow!=5
#endif
                          );
#ifdef PE_PS5_GPU_NATIVE
        if(hitRow<pureCount&&(state->page==HOST_PAGE_MODS||state->page==HOST_PAGE_CHEATS||state->page==HOST_PAGE_STATES||state->page==HOST_PAGE_IMAGES)){
            int kind=Host_ModernRowKind(state->page,hitRow);if(!kind)return;isStepper=kind==2;
        }
#endif
        int action = isStepper ? (x < OverlayValueCentre() ? -1 : 1) : 1;
        #ifdef PE_PS5_GPU_NATIVE
        if(state->page==HOST_PAGE_GRAPHICS&&hitRow==1)isStepper=0;
        #endif
        Host_ApplySettingsAction(state, action, !isStepper, game);
    }
}

static void Host_AnswerPrompt(HostOverlayState *state, int skip) {
    state->skip = skip;
    state->open = 0;
}

/* Row 0 Yes, 1 No; -1 inside the panel between rows, -2 outside it. */
static int Host_PromptRowAt(SDL_Window *game, float windowX, float windowY) {
    float density = game ? SDL_GetWindowPixelDensity(game) : 1.0f;
    if (density <= 0.0f) {
        density = 1.0f;
    }
    float scale = s_overlayLayout.scale > 0.0f ? s_overlayLayout.scale : 1.0f;
    float x = (windowX * density - (float)s_overlayLayout.x) / scale;
    float y = (windowY * density - (float)s_overlayLayout.y) / scale;

    if (x < 0.0f || x >= (float)PROMPT_PANEL_W || y < 0.0f ||
        y >= (float)OverlayPromptDesignHeight()) {
        return -2;
    }
    for (int r = 0; r < PROMPT_ROWS; r++) {
        OverlayRect rect = OverlayPromptRowRect(r);
        if (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h) {
            return r;
        }
    }
    return -1;
}

/* Skip-movie question: Esc, Circle or a click outside keep watching. */
static void Host_ProcessPromptEvent(
    HostOverlayState *state,
    const SDL_Event *event,
    SDL_Window *game
) {
    if (event->type == SDL_EVENT_KEY_DOWN) {
        int repeat = event->key.repeat != 0;
        switch (event->key.scancode) {
        case SDL_SCANCODE_ESCAPE:
            if (!repeat) {
                Host_AnswerPrompt(state, 0);
            }
            break;
        case SDL_SCANCODE_UP:
        case SDL_SCANCODE_DOWN:
        case SDL_SCANCODE_W:
        case SDL_SCANCODE_S:
            state->row ^= 1;
            state->dirty = 1;
            break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE:
            if (!repeat) {
                Host_AnswerPrompt(state, state->row == 0);
            }
            break;
        default:
            break;
        }
    } else if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch (event->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            state->row ^= 1;
            state->dirty = 1;
            break;
        case SDL_GAMEPAD_BUTTON_SOUTH:
        case SDL_GAMEPAD_BUTTON_START:
            Host_AnswerPrompt(state, state->row == 0);
            break;
        case SDL_GAMEPAD_BUTTON_EAST:
            Host_AnswerPrompt(state, 0);
            break;
        default:
            break;
        }
    } else if (event->type == SDL_EVENT_MOUSE_MOTION) {
        int row = Host_PromptRowAt(game, event->motion.x, event->motion.y);
        if (row >= 0 && row != state->row) {
            state->row = row;
            state->dirty = 1;
        }
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
               event->button.button == SDL_BUTTON_LEFT) {
        int row = Host_PromptRowAt(game, event->button.x, event->button.y);
        if (row >= 0) {
            Host_AnswerPrompt(state, row == 0);
        } else if (row == -2) {
            Host_AnswerPrompt(state, 0);
        }
    }
}

static void Host_ProcessSettingsEvent(
    HostOverlayState *state,
    const SDL_Event *event,
    SDL_Window *game,
    SDL_Window *fallbackWindow
) {
    if (event->type == SDL_EVENT_QUIT) {
        state->quit = 1;
        state->open = 0;
        return;
    }
    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        if (fallbackWindow && event->window.windowID == SDL_GetWindowID(fallbackWindow)) {
            state->open = 0;
        } else {
            state->quit = 1;
            state->open = 0;
        }
        return;
    }
    if (state->prompt) {
        Host_ProcessPromptEvent(state, event, game);
        return;
    }

    if (state->capturing_binding) {
        if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE) {
            state->capturing_binding = 0;
            state->dirty = 1;
            return;
        }
        int binding = -1;
        if (state->bind_device == HOST_BIND_KEYBOARD &&
            event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
            binding = event->key.scancode;
        } else if (state->bind_device == HOST_BIND_CONTROLLER &&
                   event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            binding = event->gbutton.button;
        } else if (state->bind_device == HOST_BIND_CONTROLLER &&
                   event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
                   event->gaxis.value > 16000) {
            if (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
                binding = SDL_GAMEPAD_BUTTON_COUNT;
            } else if (event->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                binding = SDL_GAMEPAD_BUTTON_COUNT + 1;
            }
        }
        if (binding >= 0 &&
            (state->bind_device == HOST_BIND_CONTROLLER || binding > SDL_SCANCODE_UNKNOWN)) {
            int keyboard = state->bind_device == HOST_BIND_KEYBOARD;
            int *bindings = keyboard ? host_options.keys : host_options.buttons;
            int target = state->row - 1;
            int unbound = keyboard ? HOST_UNBOUND_KEY : HOST_UNBOUND_BUTTON;
            state->capturing_binding = 0;
            state->dirty = 1;
            /* Swap duplicates, keeping every original button reachable. An
             * unbound speed binding has nothing to give back in a swap. */
            for (int i = 0; i < HOST_BINDING_COUNT; ++i) {
                if (i != target && bindings[i] == binding && bindings[target] == unbound) {
                    static char message[64];
                    snprintf(message, sizeof(message), "Already used by %s.", host_button_names[i]);
                    state->status = message;
                    return;
                }
            }
            for (int i = 0; i < HOST_BINDING_COUNT; ++i) {
                if (i != target && bindings[i] == binding) {
                    bindings[i] = bindings[target];
                }
            }
            bindings[target] = binding;
            Host_ApplyOptions(game);
        }
        return;
    }

    int totalRows = Host_SettingsTotalRowCount(state->page);
    if (event->type == SDL_EVENT_KEY_DOWN) {
        int repeat = event->key.repeat != 0;
        switch (event->key.scancode) {
        case SDL_SCANCODE_ESCAPE:
            if (!repeat) {
#ifdef PE_PS5_GPU_NATIVE
                if(Host_SettingsBack(state))break;
#endif
                state->open = 0;
            }
            break;
        case SDL_SCANCODE_TAB:
        case SDL_SCANCODE_PAGEDOWN:
            if (!repeat) {
                Host_SwitchSettingsPage(state, state->page + 1);
            }
            break;
        case SDL_SCANCODE_PAGEUP:
            if (!repeat) {
                Host_SwitchSettingsPage(state, state->page - 1);
            }
            break;
        case SDL_SCANCODE_UP:
            state->row = (state->row + totalRows - 1) % totalRows;
            Host_EnsureControlsRowVisible(state);
            state->dirty = 1;
            break;
        case SDL_SCANCODE_DOWN:
            state->row = (state->row + 1) % totalRows;
            Host_EnsureControlsRowVisible(state);
            state->dirty = 1;
            break;
        case SDL_SCANCODE_LEFT:
            if (!repeat) {
                Host_ApplySettingsAction(state, -1, 0, game);
            }
            break;
        case SDL_SCANCODE_RIGHT:
            if (!repeat) {
                Host_ApplySettingsAction(state, 1, 0, game);
            }
            break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE:
            if (!repeat) {
                Host_ApplySettingsAction(state, 1, 1, game);
            }
            break;
        case SDL_SCANCODE_DELETE:
        case SDL_SCANCODE_BACKSPACE:
            Host_ClearSpeedBinding(state);
            break;
        default:
            break;
        }
    } else if (event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
               (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY)) {
        int vertical=event->gaxis.axis==SDL_GAMEPAD_AXIS_LEFTY;
        int *held=vertical?&state->stick_y:&state->stick_x;
        int value=event->gaxis.value;
        int next=value>16000?1:value < -16000?-1:(value>-8000&&value<8000?0:*held);
        if(next!=*held){*held=next;if(next){
            if(vertical){state->row=(state->row+totalRows+next)%totalRows;Host_EnsureControlsRowVisible(state);state->dirty=1;}
            else Host_ApplySettingsAction(state,next,0,game);
        }}
    } else if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch (event->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_EAST:
#ifdef PE_PS5_GPU_NATIVE
            if(Host_SettingsBack(state))break;
#endif
        case SDL_GAMEPAD_BUTTON_START:
            state->open = 0;
            break;
        case SDL_GAMEPAD_BUTTON_WEST:
            Host_ClearSpeedBinding(state);
            break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            Host_SwitchSettingsPage(state, state->page - 1);
            break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            Host_SwitchSettingsPage(state, state->page + 1);
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            state->row = (state->row + totalRows - 1) % totalRows;
            Host_EnsureControlsRowVisible(state);
            state->dirty = 1;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            state->row = (state->row + 1) % totalRows;
            Host_EnsureControlsRowVisible(state);
            state->dirty = 1;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            Host_ApplySettingsAction(state, -1, 0, game);
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            Host_ApplySettingsAction(state, 1, 0, game);
            break;
        case SDL_GAMEPAD_BUTTON_SOUTH:
            Host_ApplySettingsAction(state, 1, 1, game);
            break;
        default:
            break;
        }
    } else if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        if (Host_SettingsPureRowCount(state->page)>Host_SettingsVisibleSettingCount(state->page) && event->wheel.y != 0.0f) {
            int step = event->wheel.y > 0.0f ? -1 : 1;
            if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                step = -step;
            }
            int nextScroll = state->scroll+step;
            if(state->page==OVERLAY_PAGE_CONTROLS)nextScroll=Host_ClampControlsScroll(nextScroll);
            else {
                int maximum=Host_SettingsPureRowCount(state->page)-Host_SettingsVisibleSettingCount(state->page);
                if(maximum<0)maximum=0;if(nextScroll<0)nextScroll=0;if(nextScroll>maximum)nextScroll=maximum;
            }
            if (nextScroll != state->scroll) {
                state->scroll = nextScroll;
#ifdef PE_PS5_GPU_NATIVE
                if(state->row<Host_SettingsPureRowCount(state->page)&&OverlayVisibleSlotForRow(state,state->row)<0)state->row=state->page==HOST_PAGE_CONTROLS?1+nextScroll:nextScroll;
#endif
                state->dirty = 1;
            }
        }
    } else if (event->type == SDL_EVENT_MOUSE_MOTION) {
        Host_ProcessSettingsPointer(state, event->motion.x, event->motion.y, 0, game, fallbackWindow);
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
               event->button.button == SDL_BUTTON_LEFT) {
        Host_ProcessSettingsPointer(state, event->button.x, event->button.y, 1, game, fallbackWindow);
    }
}

void CPE_StateUi_AfterModalClosed(void);
static void Host_Settings(SDL_Window *game) {
#ifdef PE_PS5_GPU_NATIVE
    Psyz_GpuResetInterpolation();
#endif
#ifdef PE_PS5_GPU_NATIVE
    /* Native SDL surface flags do not describe output ownership. */
    host_options.fullscreen = 1;
#else
    if (game) {
        host_options.fullscreen = (SDL_GetWindowFlags(game) & SDL_WINDOW_FULLSCREEN) != 0;
    }
#endif

    int useGpuOverlay = (game != NULL && s_overlayGpuDevice != NULL);
    SDL_Window *fallbackWindow = NULL;
    SDL_Renderer *fallbackRenderer = NULL;
    SDL_Texture *fallbackTexture = NULL;
    int fallbackTexW = 0, fallbackTexH = 0;

    if (!useGpuOverlay) {
#ifdef PE_PS5
        fallbackWindow = game;
        SDL_Surface *surface = game ? SDL_GetWindowSurface(game) : NULL;
        fallbackRenderer = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
        if (!fallbackRenderer) {
            fprintf(stderr, "Cannot open console settings: %s\n", SDL_GetError());
            return;
        }
#else
        fallbackWindow = SDL_CreateWindow(
            "Parasite Eve - Settings", 640, 600, SDL_WINDOW_HIGH_PIXEL_DENSITY
        );
        if (!fallbackWindow) {
            return;
        }
        fallbackRenderer = SDL_CreateRenderer(fallbackWindow, NULL);
        if (!fallbackRenderer) {
            SDL_DestroyWindow(fallbackWindow);
            return;
        }
#endif
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_EVENT_WINDOW_FIRST, SDL_EVENT_WINDOW_LAST);
    }

    Psyz_PauseHostClock_SDL3(true);
    int paused = Psyz_AudioIsPaused();
    Psyz_AudioPause();
    /* The panel draws at normal speed; the menu takes the hold's release. */
    Host_ResetSpeedHold();

    s_overlayState = (HostOverlayState){
        .open = 1,
#ifdef PE_PS5_GPU_NATIVE
        .page=host_requested_settings_page>=0?host_requested_settings_page:host_last_settings_page,
#else
        .page = HOST_PAGE_GRAPHICS,
#endif
        .row = 0,
        .bind_device = HOST_BIND_KEYBOARD,
        .capturing_binding = 0,
        .quit = 0,
        .dirty = 1,
        .status = NULL
    };

#ifdef PE_PS5_GPU_NATIVE
    host_requested_settings_page=-1;
    memcpy(s_overlayState.page_rows,host_saved_rows,sizeof host_saved_rows);memcpy(s_overlayState.page_scrolls,host_saved_scrolls,sizeof host_saved_scrolls);
    s_overlayState.row=s_overlayState.page_rows[s_overlayState.page];s_overlayState.scroll=s_overlayState.page_scrolls[s_overlayState.page];
    s_overlayState.bind_device=HOST_BIND_CONTROLLER;Host_EnsureControlsRowVisible(&s_overlayState);
#endif
    #ifdef PE_PS5_GPU_NATIVE
    pe_mod_panel_set_native_close(Host_RegistryClose);
    if(host_registry_open_pending){host_registry_open_pending=0;Host_RegistryGoTo(&s_overlayState,host_registry_open_scope,host_registry_open_path);}
    #endif
    while (s_overlayState.open) {
        int hd_was_busy=Host_HdBusy();Host_HdPoll();
        if(hd_was_busy || Host_HdBusy())s_overlayState.dirty=1;
        /* Each unified tab supplies its own selected-row context help. */
        if (useGpuOverlay) {
            Host_OverlayRefreshGPU(s_overlayGpuDevice, game, &s_overlayState);
            Psyz_VideoVSync(0);
        } else {
            Host_OverlayRenderFallback(
                fallbackRenderer,
                fallbackWindow,
                &s_overlayState,
                &fallbackTexture,
                &fallbackTexW,
                &fallbackTexH
            );
            SDL_Event event;
            if (SDL_WaitEventTimeout(&event, 20)) {
                Host_ProcessSettingsEvent(&s_overlayState, &event, game, fallbackWindow);
            }
        }
    }

#ifdef PE_PS5_GPU_NATIVE
    s_overlayState.page_rows[s_overlayState.page]=s_overlayState.row;s_overlayState.page_scrolls[s_overlayState.page]=s_overlayState.scroll;
    memcpy(host_saved_rows,s_overlayState.page_rows,sizeof host_saved_rows);memcpy(host_saved_scrolls,s_overlayState.page_scrolls,sizeof host_saved_scrolls);host_last_settings_page=s_overlayState.page;
#endif
    #ifdef PE_PS5_GPU_NATIVE
    pe_mod_panel_set_native_close(NULL);
    pe_menu_save_settings(); /* Includes dynamic choices normalized while painting. */
    #endif
    int quit = s_overlayState.quit;
    s_overlayState.open = 0;
    Host_TraceAspectSelection("menu_resume", 1);

    if (!Host_SaveOptions()) {
#ifdef PE_PS5
        fprintf(stderr, "Could not save preferences; changes apply for this session.\n");
#else
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "Parasite Eve",
            "Could not save preferences. Changes apply for this session.",
            fallbackWindow ? fallbackWindow : game
        );
#endif
    }
    if (fallbackTexture != NULL) {
        SDL_DestroyTexture(fallbackTexture);
    }
    if (fallbackRenderer != NULL) {
        SDL_DestroyRenderer(fallbackRenderer);
    }
    if (fallbackWindow != NULL) {
#ifndef PE_PS5
        SDL_DestroyWindow(fallbackWindow);
#endif
    }

    /* Avoid a held menu key leaking into the resumed original pad poll. */
    SDL_ResetKeyboard();
    Psyz_PauseHostClock_SDL3(false);
    if (!paused) {
        Psyz_AudioUnpause();
    }
#if defined(PE_PS5_GPU_NATIVE) && defined(PE_MODS)
    if(host_open_assists){
        host_open_assists=0;
        if(!quit)pe_mod_panel_open_page(host_pending_mod_page?host_pending_mod_page:"Playtest assists");host_pending_mod_page=NULL;
    }
#endif
#if PE_NATIVE_STATE_CPU_COMPONENTS
    if(!quit)CPE_StateUi_AfterModalClosed();
#endif
    if (quit) {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&event);
    }
}

/* g_BootMovieSkipConfirm: the movie, game clock and audio stay paused until
 * the question is answered. No keeps watching. */
#ifdef PE_PS5_GPU_NATIVE
static void Host_OpenSettingsTab(int page){
    if(s_install.active||s_overlayState.prompt||s_overlayState.capturing_binding)return;
    if(s_overlayState.open){Host_SwitchSettingsPage(&s_overlayState,page);return;}
    host_requested_settings_page=page;Host_Settings(Psyz_GetWindow_SDL3());
}
int CPE_HostOpenCheatsTab(void){Host_OpenSettingsTab(HOST_PAGE_CHEATS);return 1;}
int CPE_HostOpenRegistryPage(const char *page){
    int tab=HOST_PAGE_MODS;
    if(page&&strlen(page)>=sizeof host_registry_open_path)return 0;
    int scope=Host_RegistryRouteScope(page,&tab);
    if(s_overlayState.open){Host_SwitchSettingsPage(&s_overlayState,tab);Host_RegistryGoTo(&s_overlayState,scope,page?page:"");return 1;}
    host_registry_open_pending=1;host_registry_open_scope=scope;SDL_strlcpy(host_registry_open_path,page?page:"",sizeof host_registry_open_path);
    Host_OpenSettingsTab(tab);return 1;
}
#endif
static int Host_ConfirmMovieSkip(void) {
    SDL_Window *game = Psyz_GetWindow_SDL3();
    int paused = Psyz_AudioIsPaused();
    int skip = 0;
    int quit = 0;

    Psyz_PauseHostClock_SDL3(true);
    Psyz_AudioPause();
    Host_ResetSpeedHold();
    if (game != NULL && s_overlayGpuDevice != NULL) {
        s_overlayState = (HostOverlayState){
            .open = 1,
            .row = 1,
            .dirty = 1,
            .prompt = 1
        };
        while (s_overlayState.open) {
            Host_OverlayRefreshGPU(s_overlayGpuDevice, game, &s_overlayState);
            Psyz_VideoVSync(0);
        }
        skip = s_overlayState.skip;
        quit = s_overlayState.quit;
        s_overlayState = (HostOverlayState){0};
    } else {
#ifdef PE_PS5
        SDL_Surface *surface = game ? SDL_GetWindowSurface(game) : NULL;
        SDL_Renderer *renderer = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
        SDL_Texture *texture = NULL;
        int texW = 0, texH = 0;
        if (renderer) {
            s_overlayState = (HostOverlayState){.open = 1, .row = 1, .dirty = 1, .prompt = 1};
            while (s_overlayState.open) {
                Host_OverlayRenderFallback(renderer, game, &s_overlayState, &texture, &texW, &texH);
                SDL_Event event;
                if (SDL_WaitEventTimeout(&event, 20))
                    Host_ProcessSettingsEvent(&s_overlayState, &event, game, NULL);
            }
            skip = s_overlayState.skip;
            quit = s_overlayState.quit;
            s_overlayState = (HostOverlayState){0};
            SDL_DestroyTexture(texture);
            SDL_DestroyRenderer(renderer);
        }
#else
        SDL_MessageBoxButtonData buttons[] = {
            {0, 1, "Yes"},
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No"}
        };
        SDL_MessageBoxData box = {
            .flags = SDL_MESSAGEBOX_INFORMATION,
            .window = game,
            .title = "Parasite Eve - Skip cutscene",
            .message = "Do you want to skip the cutscene?",
            .numbuttons = 2,
            .buttons = buttons
        };
        int choice = 0;
        skip = SDL_ShowMessageBox(&box, &choice) && choice == 1;
#endif
    }

    /* Avoid the answering key leaking into the resumed original pad poll. */
    SDL_ResetKeyboard();
    Psyz_PauseHostClock_SDL3(false);
    if (!paused) {
        Psyz_AudioUnpause();
    }
    if (quit) {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&event);
    }
    return skip;
}

static void Host_PauseForFocus(SDL_Window *game) {
    int paused = Psyz_AudioIsPaused();
    Psyz_PauseHostClock_SDL3(true);
    Psyz_AudioPause();

    SDL_Event pending;
    while (SDL_WaitEvent(&pending)) {
        if (pending.type == SDL_EVENT_WINDOW_FOCUS_GAINED &&
            pending.window.windowID == SDL_GetWindowID(game)) {
            break;
        }
        if (pending.type == SDL_EVENT_QUIT ||
            (pending.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
             pending.window.windowID == SDL_GetWindowID(game))) {
            SDL_PushEvent(&pending);
            break;
        }
    }

    SDL_ResetKeyboard();
    /* A release while unfocused is never seen: drop back to 1x. */
    Host_ResetSpeedHold();
    Psyz_PauseHostClock_SDL3(false);
    if (!paused) {
        Psyz_AudioUnpause();
    }
}

/* The game runs at speed_factor while the bound key, button or trigger is held. */
static bool Host_SpeedHoldEvent(const SDL_Event *event) {
    int key = host_options.keys[HOST_SPEED_BINDING];
    int button = host_options.buttons[HOST_SPEED_BINDING];
    SDL_JoystickID device = 0;
    int held = 0, consumed = 0;
    /* The Mods panel can reset PsyZ's speed while consuming release events. */
    if (Psyz_VideoGetSpeed() != host_speed) Host_ResetSpeedHold();
    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
        if (key == HOST_UNBOUND_KEY || (int)event->key.scancode != key) return false;
        if (!event->key.repeat) host_speed_key_held = event->type == SDL_EVENT_KEY_DOWN;
        consumed = 1;
    } else if (event->type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (unsigned i = 0; i < 16; ++i)
            if (host_speed_pads[i].id == event->gdevice.which)
                host_speed_pads[i].id = host_speed_pads[i].held = 0;
    } else if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
               event->type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
        if (button < 0 || event->gbutton.button != button) return false;
        device = event->gbutton.which;
        held = event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        consumed = 1;
    } else if (event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        int axis = button == SDL_GAMEPAD_BUTTON_COUNT ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER
                 : button == SDL_GAMEPAD_BUTTON_COUNT + 1 ? SDL_GAMEPAD_AXIS_RIGHT_TRIGGER : -1;
        if (axis < 0 || event->gaxis.axis != axis) return false;
        device = event->gaxis.which;
        int was_held = 0;
        for (unsigned i = 0; i < 16; ++i)
            if (host_speed_pads[i].id == device) was_held = host_speed_pads[i].held;
        held = event->gaxis.value > (was_held ? 8000 : 16000);
    } else return false;
    if (device) {
        unsigned slot;
        for (slot = 0; slot < 16; ++slot) if (host_speed_pads[slot].id == device) break;
        if (slot == 16 && held)
            for (slot = 0; slot < 16; ++slot) if (!host_speed_pads[slot].id) break;
        if (slot < 16) {
            host_speed_pads[slot].id = held ? device : 0;
            host_speed_pads[slot].held = held;
        }
    }
    int any = host_speed_key_held;
    for (unsigned i = 0; i < 16; ++i) any |= host_speed_pads[i].held;
    if (host_options.speed_toggle) {
        if (any && !host_speed_was_held) host_speed_toggled = !host_speed_toggled;
        host_speed_was_held = any;
        any = host_speed_toggled;
    }
    Host_SetSpeed(any ? host_options.speed_factor : 1);
    return consumed; /* axis and removal events still reach the pad backend */
}

static bool Host_SettingsEvent(const SDL_Event *event) {
#ifdef PE_PS5_GPU_NATIVE
    if(s_install.active)return Host_InstallerEvent(event)!=0;
#endif
    SDL_Window *game = Psyz_GetWindow_SDL3();
#if PE_NATIVE_STATE_CPU_COMPONENTS
    if(!s_overlayState.prompt&&!s_overlayState.capturing_binding && Host_StateChord(event))return true;
#endif
    if (s_overlayState.open) {
        if (event->type == SDL_EVENT_WINDOW_RESIZED ||
            event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
            event->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
            event->type == SDL_EVENT_GAMEPAD_ADDED ||
            event->type == SDL_EVENT_GAMEPAD_REMOVED) {
            s_overlayState.dirty = 1;
            return false;
        }
        Host_ProcessSettingsEvent(&s_overlayState, event, game, NULL);
        return true;
    }
    if (game && event->type == SDL_EVENT_WINDOW_FOCUS_LOST &&
        event->window.windowID == SDL_GetWindowID(game)) {
        Host_PauseForFocus(game);
        return true;
    }
#ifdef PE_PS5
    if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
        (event->gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK ||
         event->gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK)) {
        SDL_Gamepad *pad = SDL_GetGamepadFromID(event->gbutton.which);
        if (pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK) &&
            SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK)) {
            Host_Settings(game);
            return true;
        }
    }
#endif
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE) {
        if (!event->key.repeat) {
            Host_Settings(game);
        }
        return true;
    }
    return Host_SpeedHoldEvent(event);
}

static void Host_InstallSettingsOverlay(void) {
    static int installed;if(installed)return;installed=1;
    s_prevOverlayInit = Psyz_OverlayInit_SDL3GPU(Host_OverlayInitGPU);
    s_prevOverlayRender = Psyz_OverlayRender_SDL3GPU(Host_OverlayRenderGPU);
    s_prevOverlayDestroy = Psyz_OverlayDestroyCB(Host_OverlayDestroyGPU);
    Psyz_SetOverlayEventFilter_SDL3(Host_SettingsEvent);
}

#ifdef PE_PS5_GPU_NATIVE
#include "installer/pe_native_install_ui.h"

/* Explicit in-session asset setup, reusing the existing controller browser and
 * validated read-only subset adapter. The caller already owns the paused menu. */
static void Host_CharacterPackAction(int choose){
    static char status[256];
    if(s_install.active)return;
    HostOverlayState previous=s_overlayState;memset(&s_install,0,sizeof s_install);s_install.active=1;
    SDL_strlcpy(s_install.characters,Psyz_GpuHdModelsRoot(),sizeof s_install.characters);
    s_overlayState.installer=1;s_overlayState.dirty=1;
    int selected=1;
    if(choose){
        if(!pe_install_character_pack_hint(s_install.characters))InstallFindCharacters();
        InstallBrowse(4);
        while(!s_install.cancel&&s_install.phase==PE_IV_BROWSER)InstallFrame();
        selected=s_install.browser_selected&&!s_install.cancel;
    }
    int committed=0;status[0]=0;
    if(selected&&!s_install.cancel){
        s_install.phase=PE_IV_PROGRESS;s_overlayState.dirty=1;InstallFrame();
        if(InstallCheckCharacters(s_install.characters,NULL)&&!s_install.cancel){
            if(Psyz_GpuReloadHdModels(s_install.characters)==0){
                SDL_strlcpy(host_options.hd_model_directory,s_install.characters,sizeof host_options.hd_model_directory);
                int saved=Host_SaveOptions();committed=1;
                if(!saved)SDL_strlcpy(status,"Character pack reloaded for this launch; settings could not be saved",sizeof status);
                else SDL_strlcpy(status,host_options.hd_models?"Character pack checked and reloaded; waiting for exact matches":"Character pack checked and reloaded; HD models remain Off",sizeof status);
            }else SDL_strlcpy(status,"Character pack reload failed; previous pack retained",sizeof status);
        }else if(!s_install.cancel)SDL_strlcpy(status,s_install.status[0]?s_install.status:"Choose a valid USB native character texture folder",sizeof status);
    }
    if(!status[0])SDL_strlcpy(status,"Character pack check cancelled; previous pack retained",sizeof status);
    SDL_Log("[HD models setup] choose=%d committed=%d requested=%d root=%s result=%s",choose,committed,host_options.hd_models,host_options.hd_model_directory,status);
    s_install.active=0;s_overlayState=previous;s_overlayState.status=status;s_overlayState.dirty=1;SDL_ResetKeyboard();pe_toast(status);
}

#endif

#define STATE_FLAG_IMAGE_BUSY 0x00100000U
#define STATE_FLAG_READY 0x00000100U

#ifndef PE_HOST_SETTINGS_TEST
/* Appends to a crash report section; text past its end is dropped. */
static void Host_CrashAppend(char *out, size_t size, size_t *used, const char *format, ...) {
    if (*used >= size) return;
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(out + *used, size - *used, format, args);
    va_end(args);
    if (n > 0) *used += (size_t)n < size - *used ? (size_t)n : size - *used - 1;
}

/* File name only: reports are shared, and folders can hold the user's name. */
static const char *Host_CrashFileName(const char *path) {
    const char *name = path;
    for (const char *s = path; *s; ++s) {
        if (*s == '/' || *s == '\\') name = s + 1;
    }
    return *name ? name : "(not set)";
}

/* The room number, decoded as mods/host/pe_game.c does (-1: the token is not
 * a room). Only reads values. */
static int Host_CrashRoom(void) {
    const unsigned token = (unsigned)g_SceneDispatchToken;
    int room = 0;
    for (int i = 2; i < 5 && room >= 0; ++i) {
        const char c = (char)g_Base32CharTable[(token >> ((5 - i) * 5 + 2)) & 0x1Fu];
        room = c >= '0' && c <= '9' ? room * 10 + (c - '0') : -1;
    }
    return room;
}

/* Crash report: what the game was doing and every desktop setting. Runs
 * inside the crash handler, so it only reads values. */
static void Host_DescribeCrash(char *out, size_t size) {
    const unsigned token = (unsigned)g_SceneDispatchToken;
    const int room = Host_CrashRoom();
    const unsigned speed = Psyz_VideoGetSpeed();
    size_t used = 0;
    out[0] = 0;
    Host_CrashAppend(out, size, &used,
                     "Game: room %d, scene token 0x%08X (current 0x%08X), disc %u, VBlank %u, game frame %u\r\n"
                     "Game state: flags 0x%08X / 0x%08X, %s\r\n"
                     "Aya: HP %u/%u, level %u, EXP %u\r\n"
                     "Story: day %d on the save card (progress %u), story day %d (next %d), day flags 0x%02X\r\n",
                     room, token, (unsigned)g_SceneDispatchCur, (unsigned)g_PeImageMountFlags,
                     (unsigned)g_BootHostVBlankCount, g_GlobalFrameCounter[0],
                     (unsigned)g_GameStateFlags, (unsigned)g_GameStateFlagsWord,
                     (g_GameStateFlags & 2) ? "in battle" : "not in battle",
                     (unsigned)g_AyaSaveState.current_hp, (unsigned)g_AyaSaveState.max_hp,
                     (unsigned)g_AyaSaveState.level + 1u, (unsigned)g_AyaSaveState.total_exp,
                     Save_ClassifyPlaytime((int)g_PlayTimeFrameCounter), (unsigned)g_PlayTimeFrameCounter,
                     g_GameState.current_story_day, g_GameState.pending_story_day,
                     (unsigned)g_GameState.story_day_flags);
    if (speed > 1) {
        Host_CrashAppend(out, size, &used, "Speed-up: ON at the crash, the game was running at %ux\r\n", speed);
    } else {
        Host_CrashAppend(out, size, &used, "Speed-up: off at the crash (normal speed)\r\n");
    }

    /* The same names as the settings screen (settings_overlay.h). */
    Host_CrashAppend(out, size, &used,
                     "\r\n== Settings ==\r\n"
                     "Settings file: %s\r\n"
#ifdef PE_PS5
                     "Graphics: requested CPU geometry %ux (last prepared %ux), fullscreen %s, aspect ratio %s, "
#else
                     "Graphics: render resolution %ux (renderer %ux), fullscreen %s, aspect ratio %s, "
#endif
                     "dithering %s, geometry correction %s, antialiasing %s, model textures %s, "
                     "dynamic lighting %s, effect geometry %s\r\n"
                     "Audio: master volume %u%%\r\n"
                     "Controls: speed-up %ux while %s, field run threshold %u\r\n"
                     "Discs: disc 1 %s, disc 2 %s\r\n",
                     host_options.enabled && *host_options.user_file ? Host_CrashFileName(host_options.user_file)
                                                                      : "none (defaults, not saved)",
                     host_options.scale,
#ifdef PE_PS5
                     Psyz_CpuGeometryEffective(),
#else
                     Psyz_VideoGetInternalResolution(),
#endif
                     kSwitchNames[host_options.fullscreen != 0],
                     kAspectNames[host_options.aspect != 0], kDitherNames[host_options.dither != 0],
                     kSwitchNames[host_options.geometry_correction != 0], kAntialiasNames[host_options.antialiasing != 0],
                     kFilterNames[host_options.model_filtering != 0],
                     kSwitchNames[host_options.dynamic_lighting != 0],
                     kSwitchNames[host_options.effect_geometry_correction != 0],
                     host_options.volume, host_options.speed_factor,
                     host_options.speed_toggle ? "toggled on" : "held",
                     host_options.analog_run_threshold, Host_CrashFileName(host_options.discs[0]),
                     Host_CrashFileName(host_options.discs[1]));
    Host_CrashAppend(out, size, &used, "Keyboard:");
    for (unsigned i = 0; i < HOST_BINDING_COUNT; ++i) {
        const int key = host_options.keys[i];
        const char *name = key != HOST_UNBOUND_KEY ? SDL_GetScancodeName((SDL_Scancode)key) : NULL;
        Host_CrashAppend(out, size, &used, "%s %s = %s", i ? "," : "", host_button_names[i],
                         name && *name ? name : "none");
    }
    Host_CrashAppend(out, size, &used, "\r\nController:");
    for (unsigned i = 0; i < HOST_BINDING_COUNT; ++i) {
        const int button = host_options.buttons[i];
        const char *name = button == SDL_GAMEPAD_BUTTON_COUNT       ? "Left trigger"
                           : button == SDL_GAMEPAD_BUTTON_COUNT + 1 ? "Right trigger"
                           : button != HOST_UNBOUND_BUTTON ? SDL_GetGamepadStringForButton((SDL_GamepadButton)button)
                                                           : NULL;
        Host_CrashAppend(out, size, &used, "%s %s = %s", i ? "," : "", host_button_names[i],
                         name && *name ? name : "none");
    }
    Host_CrashAppend(out, size, &used, "\r\n");
}

/* Crash report room snapshot: what a save made at the start of the current
 * room would hold, file offsets 0x100 up to the checksum at 0x1C8C (Aya,
 * items, key items, equipment, storage, then script variables, day and Aya's
 * battle record), and with mods the save's free tail from 0x1C90 (each mod's
 * per-save data, filled by the mods' save write handlers into a blank card
 * block; its layout depends on the mods loaded). Taken whole each time a room
 * starts outside a battle, on the game thread, so a save can be rebuilt from
 * any report and the crash handler only copies it out. Two copies: a crash
 * while one is being taken reports the other, never a part of one.
 * Save_BuildHeader fills the header fields as the save menu does; the game's
 * own copy of them is put back afterwards. Save_SerializeTailData refuses when
 * Aya's two record links are not the saved records, which only a damaged
 * record does, so those links are set back for the copy and restored. */
typedef struct HostRoomSnapshot {
    u8 data[0x12E4 + 0x8A8];
#ifdef PE_MODS
    u8 card[0x2000];
    int have_mod_tail;
#endif
    int room, story_day, links_fixed;
    u32 vblank;
} HostRoomSnapshot;

static HostRoomSnapshot host_room_snapshots[2];
static SDL_AtomicInt host_room_snapshot = {-1}; /* the complete copy, -1: none yet */

static void Host_TakeRoomSnapshot(void) {
    static SaveRuntimeState header;
    if (g_GameStateFlags & 2) return; /* a battle: the room's own snapshot stays */
    HostRoomSnapshot *snapshot = &host_room_snapshots[SDL_GetAtomicInt(&host_room_snapshot) == 0];

    memcpy(&header, &g_SaveRuntimeState, sizeof(header));
    Save_BuildHeader();
    memcpy(snapshot->data, &g_SaveRuntimeState, 0x12E4);
    memcpy(&g_SaveRuntimeState, &header, sizeof(header));

    u8 *cursor = g_SaveIoCursor;
    BattleStateTail *action = g_AyaBattleState.action;
    BattleAttributes *attributes = g_AyaBattleState.attributes;
    snapshot->links_fixed = (action && action != &g_SavedBattleStateTail) ||
                            (attributes && attributes != &g_BattleEquipStateBlock);
    if (snapshot->links_fixed) {
        g_AyaBattleState.action = &g_SavedBattleStateTail;
        g_AyaBattleState.attributes = &g_BattleEquipStateBlock;
    }
    g_SaveIoCursor = snapshot->data + 0x12E4;
    const int have_tail = Save_SerializeTailData() == 0;
    g_SaveIoCursor = cursor;
    g_AyaBattleState.action = action;
    g_AyaBattleState.attributes = attributes;
    if (!have_tail) return;
#ifdef PE_MODS
    memset(snapshot->card, 0, sizeof(snapshot->card));
    snapshot->have_mod_tail = pe_mod_snapshot_save_tail(snapshot->card) == 0;
#endif
    snapshot->room = Host_CrashRoom();
    snapshot->story_day = g_GameState.current_story_day;
    snapshot->vblank = g_BootHostVBlankCount;
    SDL_SetAtomicInt(&host_room_snapshot,(int)(snapshot - host_room_snapshots));
}

static void Host_CrashHex(char *out, size_t size, size_t *used, const u8 *data, size_t count, unsigned address) {
    for (size_t i = 0; i < count && *used + 1 < size; ++i) {
        const char *end = i % 32 == 31 || i + 1 == count ? "\r\n" : "";
        if (i % 32) {
            Host_CrashAppend(out, size, used, " %02X%s", data[i], end);
        } else {
            Host_CrashAppend(out, size, used, "%04X: %02X%s", address + (unsigned)i, data[i], end);
        }
    }
}

static void Host_DescribeSaveData(char *out, size_t size) {
    const int index = SDL_GetAtomicInt(&host_room_snapshot);
    size_t used = 0;
    out[0] = 0;
    Host_CrashAppend(out, size, &used, "\r\n== Room snapshot ==\r\n");
    if (index < 0) {
        Host_CrashAppend(out, size, &used, "None: no room had started yet, so there is no save data.\r\n");
        return;
    }
    const HostRoomSnapshot *snapshot = &host_room_snapshots[index];
    Host_CrashAppend(out, size, &used,
                     "Taken when room %d started, story day %d, %u seconds before the crash. The save data below "
                     "is all from that moment.\r\n%s",
                     snapshot->room, snapshot->story_day, (unsigned)((g_BootHostVBlankCount - snapshot->vblank) / 60u),
                     snapshot->links_fixed ? "Note: Aya's battle record links were damaged; written as the saved records.\r\n" : "");
    Host_CrashAppend(out, size, &used, "\r\n== Save data (save file bytes from 0x0100) ==\r\n");
    Host_CrashHex(out, size, &used, snapshot->data, sizeof(snapshot->data), 0x100u);
#ifdef PE_MODS
    Host_CrashAppend(out, size, &used, "\r\n== Save tail (save file bytes from 0x1C90) ==\r\n");
    if (snapshot->have_mod_tail) {
        Host_CrashHex(out, size, &used, snapshot->card + 0x1C90, sizeof(snapshot->card) - 0x1C90, 0x1C90u);
    } else {
        Host_CrashAppend(out, size, &used,
                         "Not available: mod code was running when the room started, so the mods' save data was not taken.\r\n");
    }
#endif
}

static void Host_PrintHelp(const char *program_name) {
    printf(
        "Usage: %s [--disc <USA Disc 1 CUE/BIN>] [--disc2 <USA Disc 2 CUE/BIN>] [--choose-discs] "
        "[--geometry-correction 0|1] [--antialiasing 0|1] [--model-filtering 0|1] [--widescreen 0|1] [--frame-interpolation 0|1] "
        "[--dynamic-lighting 0|1] [--effect-geometry-correction 0|1] "
        "[--headless | --headless-gpu] [--internal-resolution 1..8] [--frame-limit N] [--control-at N] "
        "[--dump-vram PATH] [--dump-frame PATH] [--dump-audio PATH] [--pad-at VBLANK:BUTTON_MASK ...] "
        "[--test-crash-after N | --test-abort-after N]\n",
        program_name
    );
    printf("--download-backgrounds / --verify-backgrounds manage the optional HD pack without opening a window.\n");
    printf("Without --disc, opens a native multi-select CUE/BIN dialog. --choose-discs selects new images. ESC opens desktop settings.\n");
    printf("--headless uses native-resolution software rendering; --headless-gpu keeps the actual GPU window hidden and supports internal scaling.\n");
    printf("--dump-frame writes the rendered display as RGB888 PPM at each control/terminal boundary.\n");
    printf("--dump-audio writes stereo 44100 Hz signed-16-bit little-endian PCM; requires headless and PSYZ_AUDIO_CLOCK=video.\n");
    printf("Headless editor capture: lighting-export PATH writes the original posed Aya mesh and PATH.vram (requires --dump-vram).\n");
    printf("Headless control: --control-at N pauses for stdin commands: advance FRAMES BUTTON_MASK, graphics PGXP AA FILTER (0/1; advances 8 blanks), or quit (requires --frame-limit).\n");
    printf("Headless fixtures only: --diagnostic-hp-at VBLANK:EXPECTED_HP:VALUE (requires --frame-limit; output is marked coverage-only)\n");
    printf("--test-crash-after N / --test-abort-after N crash on purpose at VBlank N to check the crash report (its path is printed).\n");
}

static int Host_ParsePadEventArg(const char *spec, int argc) {
    char *end = NULL;
    char *mask_end = NULL;
    errno = 0;
    unsigned long tick = strtoul(spec, &end, 10);
    if (!tick || tick > INT_MAX || errno || *end != ':') {
        fprintf(stderr, "Invalid input frame\n");
        return 0;
    }
    unsigned long buttons = strtoul(end + 1, &mask_end, 0);
    if (mask_end == end + 1 || *mask_end || errno || buttons > 65535 ||
        (g_BootHostPadEventCount && tick < g_BootHostPadEvents[g_BootHostPadEventCount - 1].vblank)) {
        fprintf(stderr, "Invalid input buttons/order\n");
        return 0;
    }
    if (!g_BootHostPadEvents) {
        /* There cannot be more events than command-line arguments. */
        g_BootHostPadEvents = calloc((size_t)argc, sizeof(*g_BootHostPadEvents));
        if (!g_BootHostPadEvents) {
            fprintf(stderr, "Cannot allocate input schedule\n");
            return 0;
        }
    }
    g_BootHostPadEvents[g_BootHostPadEventCount++] = (BootHostPadEvent){(u32)tick, (u16)buttons};
    return 1;
}

static int Host_ParseHealthEventArg(const char *cursor, int argc) {
    unsigned long values[3];
    for (unsigned field = 0; field < 3; ++field) {
        char *end = NULL;
        errno = 0;
        if (*cursor < '0' || *cursor > '9') {
            fprintf(stderr, "Invalid diagnostic HP event\n");
            return 0;
        }
        values[field] = strtoul(cursor, &end, 10);
        if (errno ||
            (field ? values[field] > USHRT_MAX : (!values[field] || values[field] > INT_MAX)) ||
            *end != (field == 2 ? '\0' : ':')) {
            fprintf(stderr, "Invalid diagnostic HP event\n");
            return 0;
        }
        cursor = end + 1;
    }
    if (g_BootHostHealthEventCount &&
        values[0] <= g_BootHostHealthEvents[g_BootHostHealthEventCount - 1].vblank) {
        fprintf(stderr, "Diagnostic HP events must have strictly increasing frames\n");
        return 0;
    }
    if (!g_BootHostHealthEvents) {
        g_BootHostHealthEvents = calloc((size_t)argc, sizeof(*g_BootHostHealthEvents));
        if (!g_BootHostHealthEvents) {
            fprintf(stderr, "Cannot allocate diagnostic HP schedule\n");
            return 0;
        }
    }
    g_BootHostHealthEvents[g_BootHostHealthEventCount++] = (BootHostHealthEvent){
        (u32)values[0],
        (u16)values[1],
        (u16)values[2]
    };
    return 1;
}

int main(int argc, char **argv) {
#ifdef PE_PS5
    if (!Host_InitPaths()) return EXIT_FAILURE;
#endif
    int mode;
    u32 token;
    Pe1GameState *state;
    PeDisc images[2] = {{0}};
#ifdef PE_PS5_GPU_NATIVE
    host_images=images; /* Also own early-open failure cleanup, before image admission. */
#endif
    const char *paths[2] = {NULL, NULL};
    int result = EXIT_FAILURE;
    int interactive = 1;
    int choose_discs = 0;

    const char *environment_headless = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS");
    if (environment_headless && *environment_headless) {
        interactive = 0;
    }
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--headless") || !strcmp(argv[i], "--headless-gpu") || !strcmp(argv[i], "--help") || !strcmp(argv[i], "--download-backgrounds") || !strcmp(argv[i], "--verify-backgrounds")) {
            interactive = 0;
        }
    }

    CrashReport_Install(interactive, Host_DescribeCrash);
    CrashReport_SetSaveData(Host_DescribeSaveData);
    g_BootSceneStarted = Host_TakeRoomSnapshot;
#ifdef PE_MODS
    CrashReport_SetMods(pe_mod_crash_describe);
#endif
    Host_InitOptions(interactive);
#ifdef PE_PS5
    SDL_Log("[CPE NATIVE GPU launch] SDL_GPU/static RADV requested; normal display preferences; controls/audio/cheats/paths preserved; exclusive launch log");
#endif
#ifdef PE_PS5
    if (!Host_InstallCardPaths(SDL_GetBasePath())) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PS5 memory card path setup failed: %s", host_options.user_file);
        return EXIT_FAILURE;
    }
#ifndef PE_PS5_GPU_NATIVE
    /* The legacy CPU renderer still cannot enable GPU-only features. */
    host_options.geometry_correction = host_options.antialiasing = host_options.model_filtering = 0;
    host_options.widescreen = 0;
#endif
#endif
    if(argc==2 && (!strcmp(argv[1],"--download-backgrounds") || !strcmp(argv[1],"--verify-backgrounds")))return Host_HdCli(!strcmp(argv[1],"--verify-backgrounds"));
    unsigned internal_resolution = host_options.scale;
    int resolution_argument = 0;
    int headless_argument = 0;
    unsigned effect_arguments = 0;

    for (int arg = 1; arg < argc; ++arg) {
        if (!strcmp(argv[arg], "--help")) {
            Host_PrintHelp(argv[0]);
            return EXIT_SUCCESS;
        }
        if (!strcmp(argv[arg], "--choose-discs")) {
            choose_discs = 1;
        } else if (!strcmp(argv[arg], "--headless") || !strcmp(argv[arg], "--headless-gpu")) {
            if (headless_argument++) {
                fprintf(stderr, "Duplicate or conflicting headless mode\n");
                return EXIT_FAILURE;
            }
            const char *backend = !strcmp(argv[arg], "--headless-gpu") ? "gpu" : "1";
            if (!SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS", backend, true)) {
                return EXIT_FAILURE;
            }
        } else if (!strcmp(argv[arg], "--internal-resolution")) {
            if (resolution_argument++ || arg + 1 >= argc) {
                fprintf(stderr, "Missing or duplicate internal resolution\n");
                return EXIT_FAILURE;
            }
            char *end = NULL;
            errno = 0;
            const char *value = argv[++arg];
            unsigned long scale = strtoul(value, &end, 10);
            if (*value < '1' || *value > '9' || *end || errno || scale > PSYZ_INTERNAL_RES_MAX
#ifdef PE_PS5
                || !(scale==1 || scale==2 || scale==4 || scale==6 || scale==8)
#endif
            ) {
                fprintf(stderr, "Invalid internal resolution\n");
                return EXIT_FAILURE;
            }
            internal_resolution = (unsigned)scale;
        } else if (!strcmp(argv[arg], "--geometry-correction") ||
                   !strcmp(argv[arg], "--antialiasing") ||
                   !strcmp(argv[arg], "--model-filtering") ||
                   !strcmp(argv[arg], "--widescreen") ||
                   !strcmp(argv[arg], "--frame-interpolation") ||
                   !strcmp(argv[arg], "--dynamic-lighting") ||
                   !strcmp(argv[arg], "--effect-geometry-correction")) {
            unsigned effect = !strcmp(argv[arg], "--geometry-correction") ? 1u :
                              !strcmp(argv[arg], "--antialiasing") ? 2u :
                              !strcmp(argv[arg], "--model-filtering") ? 4u :
                              !strcmp(argv[arg], "--widescreen") ? 8u :
                              !strcmp(argv[arg], "--frame-interpolation") ? 16u :
                              !strcmp(argv[arg], "--dynamic-lighting") ? 32u : 64u;
            if ((effect_arguments & effect) || arg + 1 >= argc ||
                (strcmp(argv[arg+1], "0") && strcmp(argv[arg+1], "1"))) {
                fprintf(stderr, "Missing, invalid or duplicate graphics effect (use 0 or 1)\n");
                return EXIT_FAILURE;
            }
            effect_arguments |= effect;
            int enabled = argv[++arg][0] == '1';
            if (effect == 1u) host_options.geometry_correction = enabled;
            else if (effect == 2u) host_options.antialiasing = enabled;
            else if (effect == 4u) host_options.model_filtering = enabled;
            else if(effect==8u) host_options.widescreen = enabled;
            else if (effect == 16u) host_options.frame_interpolation=enabled;
            else if (effect == 32u) host_options.dynamic_lighting = enabled;
            else host_options.effect_geometry_correction = enabled;
        } else if (!strcmp(argv[arg], "--frame-limit")) {
            char *end = NULL;
            errno = 0;
            unsigned long limit = arg + 1 < argc ? strtoul(argv[++arg], &end, 10) : 0;
            if (!limit || limit > INT_MAX || errno || *end) {
                fprintf(stderr, "Invalid frame limit\n");
                return EXIT_FAILURE;
            }
            g_BootHostVBlankLimit = (u32)limit;
        } else if (!strcmp(argv[arg], "--control-at")) {
            char *end = NULL;
            errno = 0;
            unsigned long tick = arg + 1 < argc ? strtoul(argv[++arg], &end, 10) : 0;
            if (!tick || tick > INT_MAX || errno || *end || g_BootHostControlAt) {
                fprintf(stderr, "Invalid control boundary\n");
                return EXIT_FAILURE;
            }
            g_BootHostControlAt = (u32)tick;
        } else if (!strcmp(argv[arg], "--test-crash-after") || !strcmp(argv[arg], "--test-abort-after")) {
            char *end = NULL;
            errno = 0;
            int abort_crash = !strcmp(argv[arg], "--test-abort-after");
            unsigned long tick = arg + 1 < argc ? strtoul(argv[++arg], &end, 10) : 0;
            if (!tick || tick > INT_MAX || errno || *end || g_BootHostCrashAt) {
                fprintf(stderr, "Invalid or duplicate test crash VBlank\n");
                return EXIT_FAILURE;
            }
            g_BootHostCrashAt = (u32)tick;
            g_BootHostCrashAbort = abort_crash;
        } else if (!strcmp(argv[arg], "--dump-vram")) {
            if (arg + 1 >= argc || g_BootHostVramPath) {
                fprintf(stderr, "Missing or duplicate capture path\n");
                return EXIT_FAILURE;
            }
            g_BootHostVramPath = argv[++arg];
        } else if (!strcmp(argv[arg], "--dump-frame")) {
            if (arg + 1 >= argc || g_BootHostFramePath) {
                fprintf(stderr, "Missing or duplicate rendered capture path\n");
                return EXIT_FAILURE;
            }
            g_BootHostFramePath = argv[++arg];
        } else if (!strcmp(argv[arg], "--dump-audio")) {
            if (arg + 1 >= argc || g_BootHostAudioPath) {
                fprintf(stderr, "Missing or duplicate audio capture path\n");
                return EXIT_FAILURE;
            }
            g_BootHostAudioPath = argv[++arg];
        } else if (!strcmp(argv[arg], "--pad-at")) {
            if (arg + 1 >= argc) {
                fprintf(stderr, "Invalid input schedule\n");
                return EXIT_FAILURE;
            }
            if (!Host_ParsePadEventArg(argv[++arg], argc)) {
                return EXIT_FAILURE;
            }
        } else if (!strcmp(argv[arg], "--diagnostic-hp-at")) {
            if (arg + 1 >= argc) {
                fprintf(stderr, "Invalid diagnostic HP schedule\n");
                return EXIT_FAILURE;
            }
            if (!Host_ParseHealthEventArg(argv[++arg], argc)) {
                return EXIT_FAILURE;
            }
        } else if (!strcmp(argv[arg], "--disc-env")) {
            if (paths[0] || arg+1>=argc || !(paths[0]=SDL_GetEnvironmentVariable(SDL_GetEnvironment(),argv[++arg])) || !*paths[0]) {
                fprintf(stderr,"Missing image environment variable\n");return EXIT_FAILURE;
            }
        } else if (!strcmp(argv[arg], "--disc") || !strcmp(argv[arg], "--disc2")) {
            int slot = !strcmp(argv[arg], "--disc2");
            if (paths[slot] || arg + 1 >= argc) {
                fprintf(stderr, "Missing or duplicate image argument\n");
                return EXIT_FAILURE;
            }
            paths[slot] = argv[++arg];
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[arg]);
            return EXIT_FAILURE;
        }
    }

#ifdef PE_PS5
    char capture_paths[3][4096];
    const char **capture_targets[] = {&g_BootHostVramPath, &g_BootHostFramePath, &g_BootHostAudioPath};
    for (unsigned i = 0; i < 3; ++i) if (*capture_targets[i]) {
        if (!Host_DataPath(capture_paths[i], sizeof(capture_paths[i]), *capture_targets[i])) return EXIT_FAILURE;
        *capture_targets[i] = capture_paths[i];
    }
#endif
#ifdef PE_PS5_GPU_NATIVE
    if(interactive){int first_run=Host_NativeFirstRun();if(first_run<0)return EXIT_FAILURE;if(!first_run)return EXIT_SUCCESS;if(first_run==2){
        int geo=host_options.geometry_correction,aa=host_options.antialiasing,filter=host_options.model_filtering,wide=host_options.widescreen;
        Host_LoadOptions(host_options.user_file);if(effect_arguments&1)host_options.geometry_correction=geo;if(effect_arguments&2)host_options.antialiasing=aa;if(effect_arguments&4)host_options.model_filtering=filter;if(effect_arguments&8)host_options.widescreen=wide;
        if(!resolution_argument)internal_resolution=host_options.scale;}}
#endif
    if (interactive) {
        if (!paths[0] && !paths[1]) {
            for (unsigned i = 0; i < 2; ++i) {
                if (*host_options.discs[i]) {
                    paths[i] = host_options.discs[i];
                }
            }
        }
        if (choose_discs || (!paths[0] && !paths[1])) {
            int selected = Host_ChooseDiscs(paths);
            if (selected <= 0) {
                return selected < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
            }
        }
    }
    if (!paths[0] && !paths[1]) {
        fprintf(stderr, "Supply --disc or --disc2 with a USA CUE/BIN image\n");
        return EXIT_FAILURE;
    }

    const char *headless_mode = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS");
    if ((host_options.antialiasing || host_options.model_filtering) &&
        headless_mode && !strcmp(headless_mode, "1")) {
        fprintf(stderr, "Graphics enhancements require --headless-gpu\n");
        return EXIT_FAILURE;
    }
    if (internal_resolution > 1 && headless_mode && !strcmp(headless_mode, "1")) {
        fprintf(stderr, "Software headless supports only internal resolution 1; use --headless-gpu\n");
        return EXIT_FAILURE;
    }
    if (g_BootHostVramPath && !g_BootHostVBlankLimit) {
        fprintf(stderr, "VRAM capture requires --frame-limit\n");
        return EXIT_FAILURE;
    }
    if (g_BootHostFramePath && !g_BootHostVBlankLimit) {
        fprintf(stderr, "Rendered capture requires --frame-limit\n");
        return EXIT_FAILURE;
    }
    if (g_BootHostFramePath && g_BootHostVramPath && !strcmp(g_BootHostFramePath, g_BootHostVramPath)) {
        fprintf(stderr, "Capture paths must differ\n");
        return EXIT_FAILURE;
    }
    if (g_BootHostAudioPath) {
        const char *clock = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_AUDIO_CLOCK");
        if (!g_BootHostVBlankLimit || !headless_mode ||
            (strcmp(headless_mode, "1") && strcmp(headless_mode, "gpu")) ||
            !clock || strcmp(clock, "video")) {
            fprintf(stderr, "Audio capture requires headless mode, --frame-limit and PSYZ_AUDIO_CLOCK=video\n");
            return EXIT_FAILURE;
        }
        if ((g_BootHostVramPath && !strcmp(g_BootHostAudioPath, g_BootHostVramPath)) ||
            (g_BootHostFramePath && !strcmp(g_BootHostAudioPath, g_BootHostFramePath))) {
            fprintf(stderr, "Capture paths must differ\n");
            return EXIT_FAILURE;
        }
    }
    if (g_BootHostControlAt) {
        const char *headless = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS");
        if (!headless || (strcmp(headless, "1") && strcmp(headless, "gpu")) ||
            !g_BootHostVBlankLimit || g_BootHostControlAt >= g_BootHostVBlankLimit) {
            fprintf(stderr, "Control requires headless mode and a later frame limit\n");
            return EXIT_FAILURE;
        }
        if (g_BootHostPadEventCount &&
            g_BootHostPadEvents[g_BootHostPadEventCount - 1].vblank > g_BootHostControlAt) {
            fprintf(stderr, "Input schedule extends beyond control boundary\n");
            return EXIT_FAILURE;
        }
    }
    if (g_BootHostHealthEventCount) {
        const char *headless = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS");
        if (!headless || strcmp(headless, "1") || !g_BootHostVBlankLimit ||
            g_BootHostHealthEvents[g_BootHostHealthEventCount - 1].vblank > g_BootHostVBlankLimit) {
            fprintf(stderr, "Diagnostic HP schedule requires headless mode and a frame limit covering every event\n");
            return EXIT_FAILURE;
        }
    }

mount_images:
    for (unsigned slot = 0; slot < 2; ++slot) {
        if (!paths[slot]) {
            continue;
        }
        if (!Host_OpenDisc(&images[slot], paths[slot])) {
            fprintf(stderr, "Cannot open image: %s\n", images[slot].error);
            goto image_error;
        }
        CdRom_BindImage(&images[slot], NULL);
        int mounted = Asset_OpenPeImage();
        unsigned identity = g_PeImageMountFlags;
        if (mounted || (identity != 1 && identity != 2) || g_CdImageVolumes[identity - 1]) {
            fprintf(stderr, "Invalid or duplicate USA disc image (status %d)\n", mounted);
            goto image_error;
        }
        g_CdImageVolumes[identity - 1] = &images[slot];
    }
    CdRom_BindImage(g_CdImageVolumes[0] ? g_CdImageVolumes[0] : g_CdImageVolumes[1], NULL);
    goto images_ready;

image_error:
    CdRom_BindImage(NULL, NULL);
    g_CdImageVolumes[0] = g_CdImageVolumes[1] = NULL;
    pe_disc_close(&images[0]);
    pe_disc_close(&images[1]);
#ifdef PE_PS5
    goto shutdown;
#endif
    if (!interactive) {
        goto shutdown;
    }
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR,
        "Parasite Eve",
        "Cannot mount these images. Select the USA Disc 1 and Disc 2 CUE files together. Cancel to quit.",
        NULL
    );
    {
        int selected = Host_ChooseDiscs(paths);
        if (selected > 0) {
            goto mount_images;
        }
        if (!selected) {
            result = EXIT_SUCCESS;
        }
        goto shutdown;
    }

images_ready:;
    host_options.scale = internal_resolution;
    g_BootHostGraphicsOptions = Host_ControlGraphics;
    g_BootHostHdOptions = Host_ControlHd;
    {
        /* Keep CLI/headless disc names accurate in reports too. Copy both
         * before replacing picker storage: selection can be reversed. */
        char ordered[2][4096] = {{0}};
        for (unsigned i = 0; i < 2; ++i) {
            const char *path = NULL;
            for (unsigned j = 0; j < 2; ++j) {
                if (g_CdImageVolumes[i] == &images[j]) {
                    path = paths[j];
                }
            }
            snprintf(ordered[i], sizeof(ordered[i]), "%s", path ? path : "");
        }
        memcpy(host_options.discs, ordered, sizeof(ordered));
    }
    if(!interactive) {
        const char *config_home=SDL_getenv("PE_CONFIG_HOME");
        if(config_home&&*config_home) {
            int n=snprintf(host_options.user_file,sizeof(host_options.user_file),"%s/parasite-eve.toml",config_home);
            if(n>0&&(size_t)n<sizeof(host_options.user_file))Host_HdInit();
        }
    }
    if (interactive) {
        host_images = images;
        g_CdImageRequest = Host_RequestImage;
        g_BootMovieSkipConfirm = Host_ConfirmMovieSkip;
        host_options.scale = internal_resolution;
        Host_HdInit();
        Host_InstallSettingsOverlay();
        Host_ApplyOptions(NULL);
        Host_SaveOptions();
    }

    g_BootHostActive = 1;
    PE_HOOK(PE_HOOK_INIT, (&(PeHookInit){interactive, host_options.user_file}));
#if PE_NATIVE_STATE_CPU_COMPONENTS
    Host_StateUiRegister();
#endif
    if (
#if defined(PE_PS5) && !defined(PE_PS5_GPU_NATIVE)
        Psyz_CpuGeometryConfigure(internal_resolution)<0 || Psyz_VideoSetInternalResolution(1)<0
#else
        Psyz_VideoSetInternalResolution(internal_resolution)<0
#endif
    ) {
        fprintf(stderr, "Cannot select internal resolution\n");
        goto shutdown;
    }
    if (!interactive) {
        if (internal_resolution > 1 || (effect_arguments & 1u) ||
            host_options.antialiasing || host_options.model_filtering)
            Psyz_VideoSetGeometryCorrection(host_options.geometry_correction != 0);
        Psyz_GpuSetFrameInterpolation(host_options.frame_interpolation);
        Psyz_VideoSetAntialiasing(host_options.antialiasing);
        Psyz_VideoSetModelFiltering(host_options.model_filtering);
        Psyz_VideoSetWidescreen(host_options.widescreen);
        Psyz_VideoSetDynamicLighting(host_options.dynamic_lighting != 0);
        Psyz_VideoSetEffectGeometryCorrection(host_options.effect_geometry_correction != 0);
    }
    /* Widescreen reads background texels to find where a room's picture
     * ends (mesh_list.c); kept on so switching it on mid-game works. */
    Psyz_GpuSetVramCopy(1);
    Boot_InitSystem();
#ifdef PE_PS5
    if (!Psyz_PS5PlatformReady()) {
        fprintf(stderr, "PS5 platform initialization failed: %s\n",
                Psyz_PS5PlatformInitError());
        goto shutdown;
    }
#endif
    if (
#if defined(PE_PS5) && !defined(PE_PS5_GPU_NATIVE)
        (internal_resolution>1 && !Psyz_CpuGeometryAvailable()) || Psyz_CpuGeometryRequested()!=internal_resolution
#elif defined(PE_PS5_GPU_NATIVE)
        Psyz_VideoGetInternalResolution()!=internal_resolution
#else
        (!interactive || resolution_argument) &&
        Psyz_VideoGetInternalResolution() != internal_resolution
#endif
    ) {
        fprintf(stderr, "Requested internal resolution unavailable\n");
        goto shutdown;
    }
    if (interactive) {
        SDL_Window *window = Psyz_GetWindow_SDL3();
        Host_ApplyOptions(window);
        Host_SaveOptions();
    }
    mode = 0;

    for (;;) {
        state = &g_GameState;
        Boot_SyncShutdown();

        int image_status = Asset_OpenPeImage();
        if (image_status != 0) {
            fprintf(stderr, "Game image mount failed: %d\n", image_status);
            goto shutdown;
        }

        Boot_InitGameState();
        Boot_InitSubsystems();
        int startup_status = Scene_LoadSceneData();
        if (startup_status < 0) {
            fprintf(stderr, "Scene startup failed: %d\n", startup_status);
            goto shutdown;
        }

        g_SceneDispatchToken = 0xA9400048;

        do {
            if (g_GameState.flags & STATE_FLAG_IMAGE_BUSY) {
                int display_status = Render_InitDisplayLists(mode);
                if (display_status < 0) {
                    fprintf(stderr, "Game image selection failed: %d\n", display_status);
                    goto shutdown;
                }
                g_GameState.flags &= ~STATE_FLAG_IMAGE_BUSY;
            }

            Scene_LoadFieldBg();
            token = g_SceneDispatchToken;
            g_SceneDispatchCur = token;

            switch (token) {
            case 0xA8000048: {
                int overlay_status = Boot_LoadOverlayTables();
                if (overlay_status < 0) {
                    fprintf(stderr, "Boot assets failed: %d\n", overlay_status);
                    goto shutdown;
                }
                if (FxCommon_RunFrameLoop() < 0) {
                    fprintf(stderr, "Effect scene startup failed\n");
                    goto shutdown;
                }
                g_GameState.flags |= 1;
                break;
            }
            case 0xAA108448:
                if ((g_GameState.display_list_modes & 2) == 0) {
                    mode = 2;
                    g_GameState.flags |= STATE_FLAG_IMAGE_BUSY;
                    break;
                }
                if (CdRom_LoadBootAudio() < 0) {
                    fprintf(stderr, "Boot display assets failed\n");
                    goto shutdown;
                }
                if (BootDisplay_RunDisplay() < 0) {
                    fprintf(stderr, "Boot display failed\n");
                    goto shutdown;
                }
                g_SceneDispatchToken = 0xA80830C8;
                g_GameState.flags |= 1;
                break;
            case 0xA9400048: {
                int title_status = CdRom_LoadInitialImage();
                if (title_status < 0) {
                    fprintf(stderr, "Title assets failed: %d\n", title_status);
                    goto shutdown;
                }
                int title_choice = MenuCard_RunListScreen();
                if (title_choice == PE_NEW_GAME_TITLE || title_choice == PE_NEW_GAME_TUTORIAL)
                    PE_HOOK(PE_HOOK_NEW_GAME, (&(PeHookNewGame){title_choice}));
                Draw_InitDisplay(title_choice);
                g_GameState.flags |= 3;
                break;
            }
            default: {
                int frame_status = Boot_RunFrame();
                if (frame_status < 0) {
                    fprintf(stderr, "Scene frame startup failed: %d\n", frame_status);
                    goto shutdown;
                }
                break;
            }
            }

            if (g_SceneDispatchToken != 0xA80651C8 && g_SceneDispatchToken != 0xA8065248 &&
                g_SceneDispatchToken != 0xA80652C8 && g_SceneDispatchToken != 0xA80660C8 &&
                g_SceneDispatchToken != 0xA8066148 && g_SceneDispatchToken != 0xA80661C8 &&
                g_SceneDispatchToken != 0xA8066348) {
                if (g_PlayTimeFrameCounter < 600) {
                    if ((g_GameState.display_list_modes & 1) == 0) {
                        mode = 1;
                        g_GameState.flags |= STATE_FLAG_IMAGE_BUSY;
                    }
                } else if ((g_GameState.display_list_modes & 2) == 0) {
                    mode = 2;
                    g_GameState.flags |= STATE_FLAG_IMAGE_BUSY;
                }
            }
        } while ((state->flags & STATE_FLAG_READY) == 0);

        VSync(0);
        SetDispMask(0);
        state->flags &= ~STATE_FLAG_READY;
    }

shutdown:
#ifdef PE_PS5_GPU_NATIVE
    Psyz_VideoSetShutdownCallback(Host_NativeApplicationShutdown);
    Psyz_VideoShutdown();
    return result; /* Existing CRT exit/constructors/loader teardown remain authoritative. */
#else
    Host_HdStop();
    if (g_BootDisplayPlaybackActive) {
        BootDisplay_WaitForCdAndRestoreCallbacks();
    }
    Psyz_AudioDestroy();
    CdRom_BindImage(NULL, NULL);
    g_CdImageVolumes[0] = g_CdImageVolumes[1] = NULL;
    g_CdImageRequest = NULL;
    g_BootMovieSkipConfirm = NULL;
    g_BootSceneStarted = NULL;
    host_images = NULL;
    pe_disc_close(&images[0]);
    pe_disc_close(&images[1]);
    free(g_BootHostHealthEvents);
    g_BootHostHealthEvents = NULL;
    g_BootHostHealthEventCount = g_BootHostHealthApplied = 0;
    free(g_BootHostPadEvents);
    g_BootHostPadEvents = NULL;
    g_BootHostPadEventCount = 0;
    return result;
#endif
}
#endif


#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "state_ui.h"
/* Scoped addon only: merge into manager's latest boot main, never replace it.
 * Platform GPU metadata separately rejects every paused modal/focus/background. */
unsigned CPE_StateHostReasonBits(void){
#if defined(PE_PS5_GPU_NATIVE) && defined(PE_MODS)
    extern int pe_mod_state_idle(void);unsigned bits=0;
    const char *headless=SDL_GetEnvironmentVariable(SDL_GetEnvironment(),"PSYZ_HEADLESS");
    if(headless&&headless[0]&&strcmp(headless,"0"))bits|=1u;
    if(host_open_assists)bits|=2u;if(s_overlayState.open)bits|=4u;if(s_install.active)bits|=8u;if(s_state_preparing)bits|=16u;
    if(!host_options.enabled)bits|=32u;if(host_options.battle_debris_3d<0||host_options.battle_debris_3d>2||host_options.battle_debris_3d!=FieldFx_GetParticleAugment())bits|=64u;
    if(Psyz_AudioIsPaused())bits|=128u;if(g_BootHostControlAt)bits|=256u;if(g_BootHostVBlankLimit)bits|=512u;
    if(g_BootHostPadEventCount)bits|=1024u;if(g_BootHostHealthEventCount)bits|=2048u;if(!pe_mod_state_idle())bits|=4096u;if(!Psyz_GpuHdModelsStateSafe())bits|=16384u;if(!g_CdImageVolumes[0]||!g_CdImageVolumes[1])bits|=32768u;
    return bits;
#else
    return 8192u;
#endif
}
const char *CPE_StateHostReasonText(unsigned bits){
    if(bits&32768u)return "Add both USA disc images in System before checkpointing";
    if(bits&16384u)return "HD character texture pack: waiting for a valid character texture index";
    if(bits&64u)return "Particle mode has not applied; reselect Extra particle geometry in Advanced";
    if(bits&1u)return "Headless mode cannot capture gameplay";
    if(bits&2u)return "Finish opening the Cheats menu";
    if(bits&4u)return "Close Settings and resume gameplay";
    if(bits&8u)return "Finish or cancel the installer operation";
    if(bits&16u)return "Wait for disc identity verification";
    if(bits&32u)return "Interactive gameplay is required";
    if(bits&128u)return "Resume gameplay audio after closing pause menus";
    if(bits&(256u|512u|1024u|2048u))return "Remove diagnostic input/frame-limit launch options";
    if(bits&4096u)return pe_mod_state_blocker();
    if(bits)return "This build has no native checkpoint host";
    return "Host ready; game owners are checked at the CPU boundary";
}
int CPE_StateHostIdle(void){return CPE_StateHostReasonBits()==0;}
#endif
#ifdef PE_PS5_GPU_NATIVE
#include "native_tabs.h"
#endif
