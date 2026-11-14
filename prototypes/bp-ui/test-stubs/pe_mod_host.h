#ifndef BP_TEST_HOST_H
#define BP_TEST_HOST_H
const void *pe_service_get(const char *,int);
int pe_mod_active(const char *);
int pe_mod_data_available(const char *);
void pe_mod_log(const char *,const char *,...);
void pe_toast(const char *);
#endif
