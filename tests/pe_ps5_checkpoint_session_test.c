/* Tests production PS5 session selection with the real runtime transaction engine.
 * Only platform/game I/O boundaries are fakes; no console is required. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "../platform/ps5/native-gpu/state/pe_state_session.c"
static unsigned char disk[65536],output[65536],scratch[65536];static size_t disk_size;
static int value=100,portable_commits,prepare_bad,restore_fail,hostbits;
void SDL_Log(const char *fmt,...){(void)fmt;}
int CPE_StateHostIdle(void){return !hostbits;}
unsigned CPE_StateHostReasonBits(void){return hostbits;}
const char *CPE_StateHostReasonText(unsigned b){(void)b;return "busy";}
int PeStateScript_TransientIdle(void){return 1;}
int PeStateCheckpoint_Boundary(void){return 0;}
const char *PeStateTasks_Blocker(void){return "busy";}
int PeStateAssets_NativeDescriptors(uint32_t *n){*n=0;return 1;}
int PeStateAssets_Get(PeScriptAsset *a){memset(a,0,sizeof *a);return 1;}
int PeStateTheater_RoomSupported(uint32_t a,uint32_t b){(void)a;(void)b;return 1;}
uint64_t PeStateBoundary_SceneEpoch(void){return 1;}
unsigned PeStateBoundary_InvalidationReason(void){return 1;}
int PeStateBoundary_RequestWithCancel(PeStateBoundaryWork a,PeStateBoundaryWork b,void *c){(void)a;(void)b;(void)c;return 0;}
static int begin(void *ctx,uint64_t *e){(void)ctx;*e=1;return 1;}
static int unchanged(void *ctx,uint64_t e){(void)ctx;return e==1;}
static void end(void *ctx){(void)ctx;}
PeStateBarrier PeStateBoundary_CpuBarrier(void){return (PeStateBarrier){0,begin,unchanged,end};}
int Psyz_StateKernelBegin(void){return 1;}void Psyz_StateKernelEnd(void){}
int Psyz_StateAudioBegin(void){return 1;}void Psyz_StateAudioEnd(void){}
int Psyz_StateKernelOwned(void){return 1;}int Psyz_StateAudioOwned(void){return 1;}
void PeWorld_Init(PeWorldState *w){memset(w,0,sizeof *w);}
int PeWorld_BindLoadedScene(PeWorldState *w){(void)w;return -1;}
int PeBattleNumeric_Bind(PeBattleNumeric *b){(void)b;return -1;}
#define OWNER(fn) int fn(PeStateRegistry *r){(void)r;return -1;}
OWNER(PeState_RegisterGameGlobals) OWNER(PeState_RegisterScriptAssetIdentity)
OWNER(PeState_RegisterCheckpointOwners) OWNER(PeState_RegisterBiosRng)
OWNER(PeState_RegisterAkaoTracks) OWNER(PeState_RegisterProducerState)
OWNER(PeState_RegisterAudioSubset) OWNER(PeState_RegisterRoomObject)
OWNER(PeState_RegisterGeometry) OWNER(PeState_RegisterNativeVram) OWNER(PeState_RegisterNumericGte)
int PeState_RegisterWorldSubset(PeStateRegistry *r,PeWorldState *w){(void)r;(void)w;return -1;}
int PeState_RegisterRenderGraph(PeStateRegistry *r,PeWorldState *w){(void)r;(void)w;return -1;}
int PeState_RegisterBattleNumeric(PeStateRegistry *r,PeBattleNumeric *w){(void)r;(void)w;return -1;}
void pe_mod_state_after_restore(void){}
void pe_profile_transactions_committed(void){}
int pe_checkpoint_capture_portable(unsigned char *p,unsigned cap,unsigned *n){assert(cap>=8224);memset(p,0,8224);memcpy(p,"PECP",4);*n=8224;return 0;}
int pe_checkpoint_prepare_portable(const unsigned char *p,unsigned n){return prepare_bad||n!=8224||memcmp(p,"PECP",4)?-1:0;}
int pe_checkpoint_commit_portable(void){portable_commits++;value=77;return 0;}
int PeStateFiles_WriteUnique(const char *root,const unsigned char *p,size_t n,const PeStateIdentity *id,char *name,size_t cap){(void)root;(void)id;assert(cap>=35&&n<=sizeof disk);memcpy(disk,p,n);disk_size=n;strcpy(name,"pe-state-0000000000000001-00000001");return 0;}
int PeStateFiles_Read(const char *root,const char *name,unsigned char *p,size_t cap,size_t *n,const PeStateIdentity *id){(void)root;(void)name;if(disk_size>cap)return PE_STATE_BOUNDS;memcpy(p,disk,disk_size);*n=disk_size;
 if(disk_size>=8&&!memcmp(p,"PEHYB001",8)){unsigned a,b;return pe_port_inspect(p,disk_size,id,&a,&b);}return 0;}
static int capture(void *ctx,unsigned char *out,size_t cap,size_t *n){(void)ctx;if(cap<4)return 0;memcpy(out,&value,4);*n=4;return 1;}
static int validate(void *ctx,const unsigned char *p,size_t n){(void)ctx;(void)p;return n==4;}
static int restore(void *ctx,const unsigned char *p,size_t n){(void)ctx;if(n!=4||restore_fail)return 0;memcpy(&value,p,4);return 1;}
int main(void){
 PeStateIdentity id={{1},{2}};PeCpuStateSession s;PeCpuStateSession_Init(&s,&id);
 s.initialized=1;s.world.scene=1;s.registry.count=1;
 s.registry.modules[0]=(PeStateModule){PE_STATE_PERSISTENT,1,4,capture,0,validate,restore,0};
 s.pending=3;s.output=output;s.outputBytes=sizeof output;service(&s);assert(!s.result&&disk_size>8224+80);
 value=999;s.pending=4;s.output=output;s.outputBytes=sizeof output;s.scratch=scratch;s.scratchBytes=sizeof scratch;service(&s);
 assert(!s.result&&!s.portableRestored&&value==100&&!portable_commits);
 s.identity.build[0]=3;value=999;s.pending=4;s.output=output;s.scratch=scratch;service(&s);
 assert(!s.result&&s.portableRestored&&value==77&&portable_commits==1);
 s.identity=id;hostbits=4096;value=999;s.pending=4;s.output=output;s.scratch=scratch;service(&s);
 assert(!s.result&&s.portableRestored&&value==77&&portable_commits==2);
 hostbits=0;prepare_bad=1;value=999;s.pending=4;s.output=output;s.scratch=scratch;service(&s);
 assert(s.result&&value==999&&portable_commits==2);prepare_bad=0;
 disk[100]^=1;s.pending=4;s.output=output;s.scratch=scratch;service(&s);assert(s.result==PE_STATE_CHECKSUM&&value==999);disk[100]^=1;
 s.identity.disc[0]=9;s.pending=4;s.output=output;s.scratch=scratch;service(&s);assert(s.result==PE_STATE_INCOMPATIBLE&&value==999);s.identity=id;
 s.initialized=1;s.world.scene=1;restore_fail=1;s.pending=4;s.output=output;s.scratch=scratch;service(&s);
 assert(s.result==PE_STATE_ROLLBACK_FAILED&&portable_commits==2);restore_fail=0;
 memcpy(disk,"PEQST001",8);s.pending=4;s.output=output;s.scratch=scratch;service(&s);assert(s.result&&portable_commits==2);
 puts("PASS PS5 exact restore, changed-build/mod fallback, required-data refusal, corrupt/disc refusal, rollback failure, legacy partial refusal");return 0;
}
