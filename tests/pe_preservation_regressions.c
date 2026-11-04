#include <windows.h>
#define main original_registry_main
#include "pe_mod_registry_test.c"
#undef main
#include "pe_content_api.h"
#include "pe_economy_policy.h"
#include "pe_extension_store.h"
#include "pe_api_native.h"
#include "pe_inventory_api.h"
#include "pe_shop_api.h"
#include "pe_save.h"
#include "pe_save_admission.h"
#include "pe1/inventory_state.h"
#include "pe_services.h"
#include "pe_profile_transactions.h"
#include "pe_file_io.h"
SaveRuntimeState g_SaveRuntimeState;
s16*g_InvActiveListOverride;int g_InvOverrideSlotLimit;int g_InvSavedSelectionFromStorage[2],g_InvSavedSelectionIndex[2];

static ItemDataRecord s_native[256];
static unsigned char opaque[24];
static PeExtensionSection opaque_section={"mod_c/opaque",2,1,sizeof opaque,opaque};
static int opaque_registered;
static uint32_t money=1000;
static int tool_events,tool_notifications;
static PeHookFn guard_hook;
int pe_hook_dispatch(int id,void*ctx){if(id==PE_HOOK_EQUIPMENT_GUARD)return guard_hook?guard_hook(ctx,NULL):0;++tool_events;return 0;}
void Menu_CreateNotificationDialog(int a,int b){++tool_notifications;}

static int spending_fail,reenter,modifier_bad,notice_count;
static char card_path[4096],profile_path[4096];
static unsigned profile_bits;
static int promote_fail;
static unsigned promotions;
static int admit(const char*k,unsigned b){return !strcmp(k,"pe1/outfit")&&b==1;}
static unsigned owned(const char*k){return profile_bits;}
static int promote(const char*k,unsigned b){
 if(promote_fail)return 0;char temp[4160];snprintf(temp,sizeof temp,"%s.tmp",profile_path);
 FILE*f=fopen(temp,"wb");if(!f)return 0;unsigned bits=profile_bits|b;
 int ok=fwrite(&bits,1,sizeof bits,f)==sizeof bits&&pe_profile_flush(f);if(fclose(f))ok=0;
 if(!ok||!pe_profile_replace(temp,profile_path))return 0;profile_bits=bits;++promotions;return 1;
}
static PeProfileTransactionAdapter profile_adapter={sizeof(PeProfileTransactionAdapter),"mod_a/profile",1,admit,owned,promote};
static PeSaveRecord cash={"fixture.cash",1,&money,sizeof money,NULL,NULL,NULL,NULL};
int pe_mod_active(const char *id){return !id||pe_mod_is_running(id);}
int pe_game_loaded(void){return 1;}
int pe_save_loaded(void){return 1;}
int pe_in_battle(void){return 0;}
int pe_free_field(void){return 1;}
void pe_toast(const char *s){(void)s;}
int pe_hook_add(int i,PeHookFn f,void*u,int p,const char*m){if(i==PE_HOOK_EQUIPMENT_GUARD)guard_hook=f;return 0;}
int Inv_GetAyaSlotLimit(void){return 20;}
ItemDataRecord*pe_base_item(unsigned id){return id<256?&s_native[id]:NULL;}

static int enabled(void){return 1;}
static uint32_t balance(void){return money;}
static int spend(uint32_t n){
    if(reenter){uint32_t id=0;assert(pe_inventory_grant("mod_a/gun",&id)!=PE_REG_OK);PeShopQuote q;assert(pe_economy_shop_quote("mod_c/offer",&q)!=PE_REG_OK);}
    if(spending_fail||money<n)return 0;money-=n;return 1;
}
static void give(uint32_t n){money+=n;}
static PeJunkApi junk={enabled,balance,spend,give};
const void*pe_service_get(const char*key,int version){return !strcmp(key,"junk")&&version==1?&junk:NULL;}
static PeRegResult modifier(const PePayoutContext*c,PePayoutContribution*out){
    PePayoutResult v;assert(pe_economy_policy_preview(c,&v)==PE_REG_WRONG_PHASE);
    uint32_t gen=pe_reg_generation();pe_registry_reset();assert(pe_reg_generation()==gen);
    out->add_junk=0;out->factor_10000=modifier_bad?40001:15000;return PE_REG_OK;
}
static void observer(const PePayoutContext*c,const PePayoutResult*r){++notice_count;assert(pe_economy_policy_notify(c,r)==PE_REG_WRONG_PHASE);}
static void providers(const int order[3]){
    for(int j=0;j<3;++j){int i=order[j];as(i==0?"mod_a":i==1?"mod_b":"mod_c");assert(pe_reg_batch("base",1,NULL)==PE_REG_OK);
        if(i==0){if(!strcmp(s_current,"mod_a")&&!g_pe_profile_transactions_api.add(&profile_adapter)){/* six-order registry-only reruns already have their adapter */}
PeContentDef d={0};d.api=(PeApiHeader){1,sizeof d};strcpy(d.key,"mod_a/gun");strcpy(d.name,"Test gun");d.revision=1;d.kind=1;d.native_item=0x44;d.base[0]=42;d.capacity=d.count=2;assert(pe_content_register("base",&d)==PE_REG_PENDING);}
        if(i==1){PeEffectDef d={0};d.api=(PeApiHeader){1,sizeof d};strcpy(d.key,"mod_b/heat");strcpy(d.name,"Heat");d.revision=1;d.native_effect=9;d.group=0;d.removable=1;assert(pe_effect_register("base",&d)==PE_REG_PENDING);
            PeCatalogEntryDef e={0};e.api=(PeApiHeader){1,sizeof e};strcpy(e.key,"mod_b/catalog");strcpy(e.name,"Heat");strcpy(e.effect,d.key);assert(pe_catalog_entry_register("base",&e)==PE_REG_PENDING);}
        if(i==2){if(!opaque_registered){assert(pe_extension_register(&opaque_section)==0);opaque_registered=1;}PeShopOfferDef d={0};d.api=(PeApiHeader){1,sizeof d};strcpy(d.key,"mod_c/offer");strcpy(d.name,"Gun");strcpy(d.target,"mod_a/gun");d.kind=PE_OFFER_CONTENT;d.junk_price=25;d.stock=2;assert(pe_shop_offer_register("base",&d)==PE_REG_PENDING);}
        PeEconomyPolicyDef p={0};p.api=(PeApiHeader){1,sizeof p};snprintf(p.key,48,"%s/policy",s_current);strcpy(p.name,"Policy");p.kind=i<2?PE_POLICY_MODIFIER:PE_POLICY_OBSERVER;p.modifier=i<2?modifier:NULL;p.observer=i==2?observer:NULL;assert(pe_economy_policy_register("base",&p)==PE_REG_PENDING);
    }as(NULL);pe_registry_seal();
}
static void setup(const char*dir){
    snprintf(card_path,sizeof card_path,"%s/BASLUS-00662000000AA",dir);snprintf(profile_path,sizeof profile_path,"%s/owned.ini",dir);
    pe_registry_reset();{const PeApiNative binding={&g_InventoryRuntime,pe_game_loaded,pe_in_battle,Inv_GetAyaSlotLimit,pe_base_item,g_InvSavedSelectionIndex,g_InvSavedSelectionFromStorage,&g_InvOverrideSlotLimit,&g_InvActiveListOverride};assert(pe_api_native_bind(&binding));}pe_extension_install();pe_inventory_api_install();pe_shop_api_install();pe_profile_transactions_install();assert(pe_save_register(&cash)==0);
    int order[]={0,1,2};providers(order);pe_save_new_game();pe_extension_new_game();pe_shop_api_loaded();money=1000;
    s_native[0x44].iconId=1;s_native[0x44].kind=1;s_native[0x44].itemId=0x44;
    g_AyaSaveState.equipped_weapon_slot=g_AyaSaveState.equipped_armor_slot=-1;
}
static void save(unsigned char*block,int commit){
    assert(pe_extension_set_save_path(card_path)==0);memcpy(block+0x100,&g_SaveRuntimeState,sizeof g_SaveRuntimeState);pe_inventory_api_before_save();assert(pe_extension_stage()==0);assert(pe_save_try_write(block)==0);if(commit){pe_extension_committed();pe_profile_transactions_committed();}else pe_extension_cancel();
}
static PeRegResult debug_buy(const PeShopQuote*q,uint32_t*out){PeRegResult r=pe_economy_shop_buy(q,out);fprintf(stderr,"buy result %d wallet %u token %llu\n",r,money,(unsigned long long)q->token);return r;}
static DWORD WINAPI worker(void*p){PeShopQuote q;assert(pe_economy_shop_quote("mod_c/offer",&q)==PE_REG_WRONG_THREAD);uint32_t id;assert(pe_inventory_grant("mod_a/gun",&id)==PE_REG_WRONG_THREAD);return 0;}
#ifdef PE_API_FIXTURE_MAIN /* another fixture includes this file and brings its own main */
#define main PE_API_FIXTURE_MAIN
#endif
int existing_extension_tests_main(int argc,char**argv){
    assert(argc==3);char run[4096];snprintf(run,sizeof run,"%s/run-%lu",argv[2],GetCurrentProcessId());assert(CreateDirectoryA(run,NULL));setup(run);const char*test=argv[1];
    if(!strcmp(test,"registration-orders")){
        int permutations[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
        for(int p=0;p<6;++p){pe_registry_reset();providers(permutations[p]);assert(pe_content_count()==1&&pe_effect_count()==1&&pe_catalog_entry_count()==1&&pe_shop_offer_count()==1);}
    }else if(!strcmp(test,"typed-reference")){
        pe_registry_reset();as("mod_a");assert(pe_reg_batch("base",1,NULL)==PE_REG_OK);PeRegEntry fake={PE_REG_CONTENT,"mod_a/gun",NULL,0,NULL,0,0};assert(pe_reg_submit("base",&fake)==PE_REG_PENDING);
        as("mod_c");assert(pe_reg_batch("base",1,NULL)==PE_REG_OK);PeShopOfferDef d={0};d.api=(PeApiHeader){1,sizeof d};strcpy(d.key,"mod_c/offer");strcpy(d.name,"Bad");strcpy(d.target,"mod_a/gun");d.kind=1;assert(pe_shop_offer_register("base",&d)==PE_REG_PENDING);as(NULL);pe_registry_seal();assert(!pe_shop_offer_find(d.key));
    }else if(!strcmp(test,"policy")){
        PePayoutContext c={{1,sizeof c},pe_reg_generation(),1,100000,0,1,0};PePayoutResult v={0};assert(pe_economy_policy_preview(&c,&v)==PE_REG_OK&&v.junk==20);assert(pe_economy_policy_notify(&c,&v)==PE_REG_OK&&notice_count==1);assert(pe_economy_policy_notify(&c,&v)==PE_REG_CONFLICT);modifier_bad=1;v.junk=123;assert(pe_economy_policy_preview(&c,&v)==PE_REG_BAD_ARGUMENT&&v.junk==123);
    }else if(!strcmp(test,"inventory-effects")){
        uint32_t id;assert(pe_inventory_grant("mod_a/gun",&id)==PE_REG_OK&&id==1);assert(g_InventoryRuntime.equipment[0].baseStats[0]==42);assert(pe_equipment_effect_apply(id,"mod_b/heat",0)==PE_REG_OK);assert(pe_equipment_effect_apply(id,"mod_b/heat",1)==PE_REG_CONFLICT);assert(pe_equipment_effect_remove(id,0)==PE_REG_OK);assert(pe_inventory_native_guard(256)==0);PeInventoryInstance v;assert(pe_inventory_inspect(id,&v)==PE_REG_OK&&v.state==1);s_stopped[0]="mod_a";assert(pe_inventory_inspect(id,&v)==PE_REG_OK&&v.state==2);assert(pe_inventory_destroy(id)==PE_REG_UNAVAILABLE);s_stopped[0]=NULL;assert(pe_inventory_destroy(id)==PE_REG_OK&&!g_InventoryRuntime.equipment[0].iconId);
    }else if(!strcmp(test,"purchase")){
        PeShopQuote q;assert(pe_economy_shop_quote("mod_c/offer",&q)==PE_REG_OK);uint32_t id=0;assert(debug_buy(&q,&id)==PE_REG_OK&&id==1&&money==975);assert(debug_buy(&q,&id)==PE_REG_CONFLICT&&money==975);assert(pe_economy_shop_quote("mod_c/offer",&q)==PE_REG_OK&&q.stock_left==1);spending_fail=1;assert(debug_buy(&q,&id)==PE_REG_CONFLICT&&money==975);spending_fail=0;reenter=1;assert(debug_buy(&q,&id)==PE_REG_OK&&id==2&&money==950);assert(pe_economy_shop_quote("mod_c/offer",&q)==PE_REG_CAPACITY);
    }else if(!strcmp(test,"stale-capacity")){
        PeShopQuote q;uint32_t id;assert(pe_economy_shop_quote("mod_c/offer",&q)==PE_REG_OK);g_AyaSaveState.inventory_items[0]=6;assert(debug_buy(&q,&id)==PE_REG_CONFLICT&&money==1000);for(int i=0;i<20;++i)g_AyaSaveState.inventory_items[i]=6;assert(pe_economy_shop_quote("mod_c/offer",&q)==PE_REG_OK);assert(debug_buy(&q,&id)==PE_REG_CAPACITY&&money==1000);
    }else if(!strcmp(test,"save-load")){
        PeShopQuote q;uint32_t id;assert(pe_economy_shop_quote("mod_c/offer",&q)==PE_REG_OK);assert(debug_buy(&q,&id)==PE_REG_OK);unsigned char block[8192]={0};save(block,1);assert(pe_extension_validate_load(card_path,block)==0);pe_save_on_read(block);pe_extension_apply_load();pe_inventory_api_loaded();pe_shop_api_loaded();PeInventoryInstance v;assert(pe_inventory_inspect(id,&v)==PE_REG_OK&&v.state==1&&money==975);assert(pe_economy_shop_quote("mod_c/offer",&q)==PE_REG_OK&&q.stock_left==1);
        unsigned char old[8192]={0};assert(pe_extension_validate_load(card_path,old)==0);pe_save_on_read(old);pe_extension_apply_load();pe_inventory_api_loaded();pe_shop_api_loaded();assert(pe_inventory_inspect(id,&v)==PE_REG_UNAVAILABLE);
    }else if(!strcmp(test,"cancelled-save")){
        uint32_t id;assert(pe_inventory_grant("mod_a/gun",&id)==PE_REG_OK);unsigned char block[8192]={0};save(block,0);save(block,1);assert(pe_extension_validate_load(card_path,block)==0);
    }else if(!strcmp(test,"discovery")){
        unsigned state=99;assert(!pe_catalog_entry_visible("mod_b/catalog"));assert(pe_catalog_entry_state("mod_b/catalog",&state)==PE_REG_OK&&state==0);assert(pe_catalog_entry_discover("mod_b/catalog",1)==PE_REG_OK);assert(pe_catalog_entry_visible("mod_b/catalog"));assert(pe_catalog_entry_state("mod_b/catalog",&state)==PE_REG_OK&&state==3);
    }else if(!strcmp(test,"pending-rollback")){
        unsigned char baseline[8192]={0};save(baseline,1);
        assert(g_pe_profile_transactions_api.buy("mod_a/profile","pe1/outfit",1,25));assert(money==975&&!profile_bits);
        assert(g_pe_profile_transactions_api.pending("mod_a/profile","pe1/outfit")==1);
        assert(!g_pe_profile_transactions_api.buy("mod_a/profile","pe1/outfit",1,25)&&money==975);
        assert(pe_extension_validate_load(card_path,baseline)==0);pe_save_on_read(baseline);pe_extension_apply_load();pe_profile_transactions_loaded();
        assert(money==1000&&!profile_bits&&!g_pe_profile_transactions_api.pending("mod_a/profile","pe1/outfit"));
    }else if(!strcmp(test,"receipt-recovery")){
        assert(g_pe_profile_transactions_api.buy("mod_a/profile","pe1/outfit",1,25));promote_fail=1;unsigned char b[8192]={0};save(b,1);assert(!profile_bits&&money==975);
        assert(pe_extension_validate_load(card_path,b)==0);pe_save_on_read(b);pe_extension_apply_load();promote_fail=0;pe_profile_transactions_loaded();assert(profile_bits==1&&money==975);
    }else if(!strcmp(test,"receipt-provider-off")){
        assert(g_pe_profile_transactions_api.buy("mod_a/profile","pe1/outfit",1,25));promote_fail=1;unsigned char b[8192]={0};save(b,1);s_stopped[0]="mod_a";
        assert(pe_extension_validate_load(card_path,b)==0);pe_save_on_read(b);pe_extension_apply_load();promote_fail=0;pe_profile_transactions_loaded();assert(!profile_bits);s_stopped[0]=NULL;pe_profile_transactions_loaded();assert(profile_bits==1&&money==975);
    }else if(!strcmp(test,"identity-refusal")){
        uint32_t id;assert(pe_inventory_grant("mod_a/gun",&id)==PE_REG_OK);
        unsigned char b[8192]={0};save(b,1);
        unsigned pool=0x100+offsetof(SaveRuntimeState,player.runtime.inventory)+offsetof(InventoryRuntime,equipment);
        b[pool+offsetof(ItemDataRecord,iconId)]^=1;
        assert(pe_extension_validate_load(card_path,b)<0);
        PeInventoryInstance instance;assert(pe_inventory_inspect(id,&instance)==PE_REG_OK&&instance.state==PE_INSTANCE_ACTIVE);
    }else if(!strcmp(test,"receipt-replay")){
        assert(g_pe_profile_transactions_api.buy("mod_a/profile","pe1/outfit",1,25));promote_fail=1;
        char file[4096];snprintf(file,sizeof file,"%s/receipt.pec",run);
        unsigned char b[8192]={0};memcpy(b+0x100,&g_SaveRuntimeState,sizeof g_SaveRuntimeState);
        assert(!pe_extension_set_save_path(card_path));pe_inventory_api_before_save();assert(!pe_extension_stage());assert(!pe_save_try_write(b));
        assert(!pe_checkpoint_write(file,b));pe_extension_cancel();promote_fail=0;
        for(int i=0;i<6;i++){
            assert(!pe_checkpoint_read(file,card_path,b));pe_save_on_read(b);pe_extension_apply_load();pe_profile_transactions_loaded();
            assert(profile_bits==1&&money==975&&promotions==1);
        }
    }else if(!strcmp(test,"save-io-failure")){
        assert(g_pe_profile_transactions_api.buy("mod_a/profile","pe1/outfit",1,25));char dir[4096];strcpy(dir,card_path);char*slash=strrchr(dir,'/');assert(slash);strcpy(slash+1,".pe-ext");FILE*f=fopen(dir,"wb");assert(f);fclose(f);
        assert(pe_extension_set_save_path(card_path)==0);assert(pe_extension_stage()<0);assert(!profile_bits&&money==975);
    }else if(!strcmp(test,"missing-companion")){
        uint32_t id;assert(pe_inventory_grant("mod_a/gun",&id)==PE_REG_OK);unsigned char b[8192]={0};save(b,1);
        WIN32_FIND_DATAA data;char pattern[4224],file[4224];char base[4096];strcpy(base,card_path);char*slash=strrchr(base,'/');assert(slash);*slash=0;snprintf(pattern,sizeof pattern,"%s/.pe-ext/*.pex",base);HANDLE h=FindFirstFileA(pattern,&data);assert(h!=INVALID_HANDLE_VALUE);snprintf(file,sizeof file,"%s/.pe-ext/%s",base,data.cFileName);FindClose(h);assert(DeleteFileA(file));
        assert(pe_extension_validate_load(card_path,b)<0);PeInventoryInstance v;assert(pe_inventory_inspect(id,&v)==PE_REG_OK&&v.state==1);
    }else if(!strcmp(test,"adopt-native")){
        ItemDataRecord*r=&g_InventoryRuntime.equipment[5];r->iconId=1;r->itemId=0x44;r->kind=1;r->tailCapacity=r->tailCount=2;g_AyaSaveState.inventory_items[3]=261;
        uint32_t id,id2;assert(pe_inventory_adopt(261,&id)==PE_REG_OK);assert(pe_inventory_adopt(261,&id2)==PE_REG_OK&&id2==id);assert(pe_equipment_effect_apply(id,"mod_b/heat",0)==PE_REG_OK);
        PeInventoryInstance v;assert(pe_inventory_inspect(id,&v)==PE_REG_OK&&v.state==PE_INSTANCE_ACTIVE);
        unsigned char b[8192]={0};save(b,1);r->baseStats[0]++;assert(pe_extension_validate_load(card_path,b)==0);pe_save_on_read(b);pe_extension_apply_load();pe_inventory_api_loaded();assert(pe_inventory_inspect(id,&v)==PE_REG_OK&&v.state==PE_INSTANCE_UNCERTAIN);assert(pe_inventory_destroy(id)==PE_REG_UNAVAILABLE);
    }else if(!strcmp(test,"unknown-preservation")){
        memset(opaque,42,sizeof opaque);assert(pe_extension_enable(&opaque_section)==PE_REG_OK);
        unsigned char first[8192]={0};save(first,1);opaque_section.version=1;
        assert(pe_extension_validate_load(card_path,first)==0);pe_save_on_read(first);pe_extension_apply_load();assert(pe_extension_enable(&opaque_section)==PE_REG_UNSAVEABLE);
        s_stopped[0]="mod_c";memset(opaque,99,sizeof opaque);unsigned char second[8192]={0};save(second,1);
        unsigned char link[20];unsigned ver,n;assert(pe_save_peek(second,"pe_host.extlink",&ver,link,sizeof link,&n)==1);
        unsigned gen=link[4]|(unsigned)link[5]<<8|(unsigned)link[6]<<16|(unsigned)link[7]<<24;char file[4224],base[4096];strcpy(base,card_path);char*slash=strrchr(base,'/');assert(slash);*slash=0;
        snprintf(file,sizeof file,"%s/.pe-ext/BASLUS-00662000000AA.%08x.pex",base,gen);FILE*f=fopen(file,"rb");assert(f);unsigned char b[8192];size_t size=fread(b,1,sizeof b,f);fclose(f);assert(size>96+64);
        assert(!strcmp((char*)b+96,"mod_c/opaque")&&b[96+48]==2);unsigned off=b[96+52]|(unsigned)b[96+53]<<8|(unsigned)b[96+54]<<16|(unsigned)b[96+55]<<24;assert(off+24<=size);for(int i=0;i<24;++i)assert(b[off+i]==42);
    }else if(!strcmp(test,"ledger-capacity")){
        uint32_t id=0;for(int i=0;i<33;++i){ItemDataRecord*r=&g_InventoryRuntime.equipment[i];r->iconId=1;r->itemId=0x44;r->kind=1;g_InventoryRuntime.storage[i]=256+i;PeRegResult got=pe_inventory_adopt(256+i,&id);assert(got==(i<32?PE_REG_OK:PE_REG_CAPACITY));}
        assert(id==32);assert(pe_inventory_destroy(1)==PE_REG_OK);assert(pe_inventory_adopt(288,&id)==PE_REG_OK&&id==33);
    }else if(!strcmp(test,"ordinal-conflict")){
        pe_registry_reset();as("mod_b");assert(pe_reg_batch("base",1,NULL)==PE_REG_OK);PeEffectDef d={0};d.api=(PeApiHeader){1,sizeof d};strcpy(d.key,"mod_b/heat");strcpy(d.name,"Heat");d.revision=1;d.native_effect=9;d.group=0;assert(pe_effect_register("base",&d)==PE_REG_PENDING);
        for(int i=0;i<2;++i){PeCatalogEntryDef e={0};e.api=(PeApiHeader){1,sizeof e};snprintf(e.key,48,"mod_b/entry%d",i);strcpy(e.name,"Heat");strcpy(e.effect,d.key);e.ordinal=1;assert(pe_catalog_entry_register("base",&e)==PE_REG_PENDING);}as(NULL);pe_registry_seal();assert(!pe_catalog_entry_count()&&!pe_effect_count());
    }else if(!strcmp(test,"runtime-soak")){
        LARGE_INTEGER a,b,f;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&a);
        for(int i=0;i<10000;++i){uint32_t id;assert(pe_inventory_grant("mod_a/gun",&id)==PE_REG_OK);assert(id==(uint32_t)i+1);assert(pe_equipment_effect_apply(id,"mod_b/heat",0)==PE_REG_OK);assert(pe_equipment_effect_remove(id,0)==PE_REG_OK);assert(pe_inventory_destroy(id)==PE_REG_OK);}
        QueryPerformanceCounter(&b);printf("10000 grant/apply/remove/destroy cycles: %.3f seconds\n",(double)(b.QuadPart-a.QuadPart)/f.QuadPart);assert(money==1000&&!g_AyaSaveState.inventory_items[0]);
    }else if(!strcmp(test,"retention")){
        uint32_t id;assert(pe_inventory_grant("mod_a/gun",&id)==PE_REG_OK);unsigned char b[8192]={0};for(int i=0;i<6;++i)save(b,1);
        char base[4096],pattern[4224];strcpy(base,card_path);char*slash=strrchr(base,'/');assert(slash);*slash=0;snprintf(pattern,sizeof pattern,"%s/.pe-ext/*.pex",base);WIN32_FIND_DATAA data;HANDLE h=FindFirstFileA(pattern,&data);assert(h!=INVALID_HANDLE_VALUE);int count=0;do{++count;}while(FindNextFileA(h,&data));FindClose(h);assert(count==2&&pe_extension_validate_load(card_path,b)==0);
    }else if(!strcmp(test,"definition-revision")){
        uint32_t first,second;assert(pe_inventory_grant("mod_a/gun",&first)==PE_REG_OK);
        pe_registry_reset();as("mod_a");assert(pe_reg_batch("base",1,NULL)==PE_REG_OK);PeContentDef d={0};d.api=(PeApiHeader){1,sizeof d};strcpy(d.key,"mod_a/gun");strcpy(d.name,"Revision 2");d.revision=2;d.kind=1;d.native_item=0x44;d.capacity=d.count=2;assert(pe_content_register("base",&d)==PE_REG_PENDING);as(NULL);pe_registry_seal();
        assert(pe_inventory_grant("mod_a/gun",&second)==PE_REG_OK);PeInventoryInstance old,new;assert(pe_inventory_inspect(first,&old)==PE_REG_OK&&old.revision==1);assert(pe_inventory_inspect(second,&new)==PE_REG_OK&&new.revision==2);
    }else if(!strcmp(test,"thread")){HANDLE h=CreateThread(NULL,0,worker,NULL,0,NULL);assert(h);WaitForSingleObject(h,INFINITE);CloseHandle(h);}
    else return 2;
    printf("PASS %s\n",test);return 0;
}




int main(int argc,char **argv){return existing_extension_tests_main(argc,argv);}
