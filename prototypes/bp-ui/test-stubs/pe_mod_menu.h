#ifndef BP_TEST_MENU_H
#define BP_TEST_MENU_H
#define PE_MENU_PAGE "PE Enhancements"
typedef int (*PeMenuGet)(void);typedef int (*PeMenuSet)(int);typedef unsigned(*PeMenuChoiceCount)(void);typedef const char *(*PeMenuChoiceName)(unsigned);typedef void(*PeMenuAction)(void);typedef void(*PeMenuText)(char*,unsigned);
void pe_menu_begin(const char *);
int pe_menu_bind_setting(const char *,int *,int,int);
void pe_menu_save_settings(void);
int pe_menu_confirm(const char *,const char *);
int pe_menu_register_info(const char*,const char*,const char*,PeMenuText);
int pe_menu_register_action(const char*,const char*,const char*,PeMenuAction);
int pe_menu_register_choice(const char*,const char*,const char*,int*,const char *const*,unsigned);
int pe_menu_register_live_choice(const char*,const char*,const char*,const char *const*,unsigned,PeMenuGet,PeMenuSet);
int pe_menu_register_live_dynamic_choice(const char*,const char*,const char*,unsigned,PeMenuChoiceCount,PeMenuChoiceName,PeMenuGet,PeMenuSet);
#endif
