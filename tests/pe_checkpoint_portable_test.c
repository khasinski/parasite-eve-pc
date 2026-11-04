/* Production host integration without a game process. */
#define main save_fixture_main
#include "pe_mod_save_test.c"
#undef main
#include "pe_extension_store.h"
#include "pe_save_admission.h"
#include "pe_save_emission.h"
#ifdef _WIN32
#include <windows.h>
#endif
PeRegResult pe_reg_access(void){return PE_REG_OK;}
int pe_reg_readonly_begin(void){return 1;}
void pe_reg_readonly_end(void){}
PeRegResult pe_reg_registration_access(void){return PE_REG_OK;}
static int stacks_running=1;
int pe_mod_is_running(const char *id){return strcmp(id,"pe_stacks")!=0||stacks_running;}
static int migrate(void *out,const void *old,unsigned size,unsigned version){
    if(size!=4||version!=1)return -1;unsigned v;memcpy(&v,old,4);v+=10;memcpy(out,&v,4);return 0;
}
static int reject(const void *data){(void)data;return -1;}
void pe_profile_transactions_committed(void){}
int main(int argc,char **argv){
    assert(argc==2);char binding[1024],checkpoint[1024];
    snprintf(binding,sizeof binding,"%s/BASLUS-00662PEQUICKSAVE000A",argv[1]);
    snprintf(checkpoint,sizeof checkpoint,"%s/test.pec",argv[1]);
    unsigned char block[8192]={0},restored[8192],old[8192];
    unsigned original[2]={321,99},v,n,got[2];
    pe_save_install();assert(!pe_save_register(&s_junk_rec));
    tail_begin(block);tail_add(block,fnv("pe_economy.loot"),1,original,sizeof original);tail_end(block);
    game_load(block);game_save();
    assert(pe_save_peek(s_card,"pe_economy.loot",&v,got,sizeof got,&n)==1);
    assert(v==1&&!memcmp(got,original,sizeof got));
    pe_extension_install();
    static unsigned data=123;
    static PeExtensionSection section={"junk_mod/data",1,1,sizeof data,&data};
    assert(!pe_extension_register(&section));
    static unsigned char reserve[204]={1,0,0,0,1,49};
    static const PeExtensionSection stacks={"pe_stacks/counts",1,1,sizeof reserve,reserve};
    s_current="pe_stacks";assert(!pe_extension_register(&stacks));s_current="junk_mod";
    pe_save_new_game();pe_extension_new_game();data=123;
    assert(pe_extension_enable(&section)==PE_REG_OK);
    reserve[0]=1;reserve[4]=1;reserve[5]=49;assert(pe_extension_enable(&stacks)==PE_REG_OK);
    assert(!pe_extension_set_save_path(binding));assert(!pe_extension_stage());
    memset(block,0,sizeof block);assert(!pe_save_try_write(block));
    assert(!pe_checkpoint_write(checkpoint,block));pe_extension_cancel();
    /* Companion generation was removed; checkpoint is independently loadable. */
    memset(restored,0xcc,sizeof restored);
    assert(!pe_checkpoint_read(checkpoint,binding,restored));
    assert(!memcmp(block,restored,sizeof block));
    pe_save_on_read(restored);pe_extension_apply_load();assert(data==123);
#ifdef _WIN32
    /* Publication fails after staging; the previous checkpoint stays readable. */
    assert(!pe_extension_set_save_path(binding));assert(!pe_extension_stage());
    unsigned char next[8192]={0};assert(!pe_save_try_write(next));
    assert(SetFileAttributesA(checkpoint,FILE_ATTRIBUTE_READONLY));
    assert(pe_checkpoint_write(checkpoint,next)<0);
    assert(SetFileAttributesA(checkpoint,FILE_ATTRIBUTE_NORMAL));pe_extension_cancel();
    assert(!pe_checkpoint_read(checkpoint,binding,restored));assert(!memcmp(block,restored,8192));
#endif
    /* Simulate a new reader: migration uses a candidate, rejecting it preserves old bytes. */
    section.version=2;section.upgrade=migrate;
    assert(!pe_checkpoint_read(checkpoint,binding,restored));
    pe_save_on_read(restored);pe_extension_apply_load();assert(data==133);
    section.check=reject;
    assert(!pe_checkpoint_read(checkpoint,binding,restored));
    pe_save_on_read(restored);pe_extension_apply_load();
    assert(!pe_extension_available(&section));
    assert(!pe_extension_set_save_path(binding));assert(!pe_extension_stage());
    unsigned char preserved[8192]={0};assert(!pe_save_try_write(preserved));
    /* Rejected v1 section remains opaque through an actual subsequent save. */
    char retry[1024];snprintf(retry,sizeof retry,"%s/rejected.pec",argv[1]);
    assert(!pe_checkpoint_write(retry,preserved));pe_extension_cancel();
    section.version=1;section.upgrade=NULL;section.check=NULL;
    assert(!pe_checkpoint_read(retry,binding,restored));
    pe_save_on_read(restored);pe_extension_apply_load();assert(data==123);
    /* Missing provider: refill a full bag as space opens, even without an anchor. */
    stacks_running=0;
    short bag[2]={2,2},storage[1]={2},special[1]={2};
    short *lists[3]={bag,storage,special};unsigned sizes[3]={2,1,1};
    pe_extension_refill_stacks(lists,sizes);
    for(int i=0;i<49;++i){bag[0]=0;pe_extension_refill_stacks(lists,sizes);assert(bag[0]==1);}
    bag[0]=0;pe_extension_refill_stacks(lists,sizes);assert(!bag[0]);
    /* Legacy PEMC-only reserves also work with no provider callbacks. */
    const PeSaveRecord *legacy=pe_save_host_stack_record();assert(legacy);
    unsigned char old_counts[204]={1,0,0,0,1,49};
    tail_begin(block);tail_add(block,fnv("pe_stacks.counts"),1,old_counts,204);tail_end(block);
    assert(!pe_save_prepare_load(block));pe_save_on_read(block);
    bag[0]=bag[1]=2;
    for(int i=0;i<49;i++){bag[0]=0;pe_save_refill_legacy_stacks(lists,sizes);assert(bag[0]==1);}
    bag[0]=0;pe_save_refill_legacy_stacks(lists,sizes);assert(!bag[0]);
    /* A damaged file never writes the caller's candidate. */
    memcpy(old,restored,sizeof old);
    FILE *f=fopen(checkpoint,"r+b");assert(f);assert(!fseek(f,64,SEEK_SET));fputc(0xff,f);fclose(f);
    assert(pe_checkpoint_read(checkpoint,binding,restored)<0);
    assert(!memcmp(old,restored,sizeof old));
    puts("PASS: rejected migration preserved, independent checkpoint round trip, corruption refusal, section migration, missing-provider reserve conservation");
    return 0;
}
