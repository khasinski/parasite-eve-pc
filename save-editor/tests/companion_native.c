/* Native oracle: actual PEMC reader and PEEX loader, with the existing save-test
 * boundary stubs. Run by the Rust suite through PE_COMPANION_ORACLE. */
#define main existing_save_test_main
#include "../../tests/pe_mod_save_test.c"
#undef main
#include "pe_extension_store.h"
PeRegResult pe_reg_registration_access(void) { return PE_REG_OK; }
PeRegResult pe_reg_access(void) { return PE_REG_OK; }
int pe_mod_is_running(const char *id) { (void)id; return 1; }
void pe_profile_transactions_committed(void) {}
int main(int argc,char **argv) {
    assert(argc==2);
    unsigned char block[8192],counts[204]={0};
    FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(block,1,8192,f)==8192);fclose(f);
    s_current=NULL;pe_extension_install();
    s_current="pe_stacks";
    PeExtensionSection stack={"pe_stacks/counts",1,1,sizeof counts,counts};
    assert(pe_extension_register(&stack)==0);
    s_current=NULL;
    assert(pe_extension_validate_load(argv[1],block)==0);
    pe_extension_apply_load();
    assert(counts[0]==1 && counts[1]==1 && counts[4]==6 && counts[5]==16 && counts[6]==6 && counts[7]==3);
    puts("Native game companion loader accepted editor output and exact stack counts.");
    return 0;
}
