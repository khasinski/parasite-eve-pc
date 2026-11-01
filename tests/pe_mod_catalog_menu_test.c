/* Production controller, viewport model, renderer and host screen helpers.
 * Only the game widget allocator/drawer and economy boundary are simulated. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/host/pe_main_menu.c"
#include "../mods/pe_economy/pe_catalog_menu_state.c"
#include "../mods/pe_economy/pe_catalog_menu_view.c"
#include "../mods/pe_economy/pe_catalog_menu.c"
#include "../mods/pe_economy/pe_catalog_effect_icons.c"
#include "../mods/pe_economy/pe_armor_effect_display.c"

static size_t frame_bytes, max_frame_bytes;
_Alignas(RenderTexturedQuad) static unsigned char packet_arena[0x4000];
static RenderNativeTag ordering;
static int force_large_icon, effect_sprites, custom_badges;
static PeHookFn custom_icon;
int g_InvItemUsableFlag;
ItemDataRecord *g_MenuActionItemData;
int pe_in_battle(void) { return 0; }
const char *pe_mod_current(void) { return NULL; }
int pe_mod_active(const char *m) { (void)m; return 1; }
int g_MenuItemContextFlag, g_MenuBattleStatusOverlayActive;
MenuWidgetNode *g_MenuWidgetActiveListHead, *g_MenuActiveWidget;
u8 g_MenuItemLabelBuffer[128];
int g_MenuItemPrimListHandles[2], g_MenuActionTextBase;
MenuConfirmCallback g_MenuConfirmCallback;
void Menu_DrawItemLabel(MenuWidgetNode *n) { (void)n; }
void Menu_DrawActionOptionList(MenuWidgetNode *n) { (void)n; }
int Menu_ConfirmDialogHandler(MenuWidgetNode *n, unsigned int f) { (void)n;(void)f;return 1; }
static int font_advance=7;
int Draw_MeasureTextWidth(u8 *t) { int n=0;while(*t++!=0xFF)++n;return n*font_advance; }
unsigned char *g_DrawPacketCursor, *g_DrawPacketArenaBase;
unsigned int g_DrawPrimColor, g_DrawColorShaded, *g_DrawOrderingTableEntry;
int g_DrawTextDimmed, g_TextCursorX, g_TextCursorY, g_DrawTextPosX, g_MenuListRowHeight;
DrawGlyphDescriptor *Draw_LookupGlyphDescriptor(int i) {
    static DrawGlyphDescriptor d={.width=25,.height=12},large={.width=200,.height=40};
    return force_large_icon&&i<0x80?&large:&d;
}
void Draw_OffsetCursor(int x,int y) { g_TextCursorX+=x;g_TextCursorY+=y; }
static int stat_sprites;
void Draw_AllocSprite(int i) {
    ++stat_sprites;if(i>=0x23&&i<=0x46)++effect_sprites;
    frame_bytes+=sizeof(RenderSpritePacket)+sizeof(RenderDrawModePacket);
}

static MenuWidgetNode pool[24], *current;
static unsigned char used[24];
static MenuWidgetNode *allocate(MenuWidgetNode *parent,MenuWidgetNode *owner) {
    for(int i=0;i<24;++i)if(!used[i]) {
        used[i]=1;MenuWidgetNode *n=&pool[i];memset(n,0,sizeof *n);n->parent=parent;
        n->next=g_MenuWidgetActiveListHead;g_MenuWidgetActiveListHead=n;
        if(owner){int k;for(k=0;k<4;++k)if(!owner->children[k]){owner->children[k]=n;break;}assert(k<4);}
        return n;
    }
    assert(!"native widget pool exhausted");return NULL;
}
MenuWidgetNode *MenuWidget_CreateSimpleNode(int mode,MenuWidgetNode *parent,MenuWidgetNode *owner,int x) {
    MenuWidgetNode *n=allocate(parent,owner);n->mode=1;n->selected_base=mode;n->cursor_x=x;n->visible_rows=20;return n;
}
MenuWidgetNode *MenuWidget_CreateNode(int mode,MenuWidgetNode *parent,MenuWidgetNode *owner) {
    MenuWidgetNode *n=allocate(parent,owner);n->mode=2;n->selected_base=mode;n->x=n->y=6;
    n->grid_width=n->x_limit=1;n->visible_rows=n->visible_rows_mirror=8;n->disabled=12;n->draw_state=68;n->layout_flags=0x10;return n;
}
void MenuWidget_DestroyNode(MenuWidgetNode *n) {
    for(int i=0;i<4;++i)if(n->children[i])MenuWidget_DestroyNode(n->children[i]);
    for(int i=0;i<24;++i)for(int j=0;j<4;++j)if(pool[i].children[j]==n)pool[i].children[j]=NULL;
    MenuWidgetNode **p=&g_MenuWidgetActiveListHead;while(*p&&*p!=n)p=&(*p)->next;if(*p)*p=n->next;
    used[n-pool]=0;
}
MenuWidgetNode *MenuWidget_GetCurrentNode(void) { return current; }
void MenuWidget_SetCurrentNode(MenuWidgetNode *n) { current=n; }
int MenuWidget_GridCellIndex(MenuWidgetNode *n) { return n->cursor_y; }
MenuWidgetSimpleDescriptor *MenuWidget_LookupSimpleDescriptor(unsigned int i) { static MenuWidgetSimpleDescriptor d={28,24,252,0};(void)i;return &d; }
void Draw_SetPrimCallback(MenuWidgetNode *n,int rows) {
    int old=n->visible_rows;n->y_limit=(rows+n->grid_width-1)/n->grid_width;n->visible_rows=n->y_limit<n->visible_rows_mirror?n->y_limit:n->visible_rows_mirror;
    n->parent->visible_rows+=n->disabled*(n->visible_rows-old);
    if(n->y_limit>n->visible_rows_mirror&&!n->popup_node){n->popup_node=allocate(n->parent,n->parent);n->popup_node->mode=3;}
    else if(n->y_limit<=n->visible_rows_mirror&&n->popup_node){MenuWidget_DestroyNode(n->popup_node);n->popup_node=NULL;}
}
static int inject_row=-1,inject_top,inject_adjust;
int Menu_StepScrollCursor(MenuWidgetNode *n,unsigned int b) {
    if(inject_row>=0){n->cursor_y=inject_row;n->scroll_y=inject_top;n->scroll_adjust=inject_adjust;inject_row=-1;return 1;}
    if(b&0x1000)n->cursor_y=n->cursor_y? n->cursor_y-1:n->y_limit-1;
    if(b&0x4000)n->cursor_y=(n->cursor_y+1)%n->y_limit;
    return 1;
}
static int print_limit,print_count,type_labels;
static int name_queries,price_queries,tool_queries;
static u8 last_text[128];
void Draw_SetTextDimmed(int d) { g_DrawTextDimmed=d; }
void Draw_PrintRawText(u8 *text) {
    assert(Draw_MeasureTextWidth(text)<=print_limit);++print_count;
    int copy=0;do {last_text[copy]=text[copy];}while(text[copy++]!=0xFF&&copy<127);
    for(u8 *p=text;*p!=0xFF;++p)frame_bytes+=sizeof(RenderTexturedQuad);
    u8 word[32];pe_text_from_ascii(word,sizeof word,"Weapon");if(!memcmp(word,text,7))type_labels|=1;
    pe_text_from_ascii(word,sizeof word,"Armor");if(!memcmp(word,text,6))type_labels|=2;
}
void MenuWidget_DrawList(MenuWidgetNode *n,void (*draw)(int)) {
    for(int row=0;row<n->visible_rows;++row) {
        g_DrawTextPosX=n->draw_state;g_TextCursorX=2;g_TextCursorY=2;print_limit=n->draw_state-4;
        for(int col=0;col<n->grid_width;++col)draw((n->scroll_y+row)*n->grid_width+col);
    }
}
void Menu_DrawItemList(MenuWidgetNode *n) { (void)n; }
void Menu_PlayErrorSound(void) {}
void Menu_PlayConfirmSound(void) {}
void Menu_PlayCancelSound(void) {}
void Menu_PlayMoveSound(void) {}
int Menu_GetBattleEquipMode(void) { return 0; }
int pe_game_loaded(void) { return 1; }
int pe_loot_enabled(void) { return 1; }
static unsigned junk=100,price=10;
uint32_t pe_loot_junk(void) { return junk; }
static ItemDataRecord records[2]={{.iconId=1,.itemId=0x45,.kind=1,.tailCount=10,.tailData={2,9}},
                                 {.iconId=1,.itemId=0x97,.kind=9,.tailCount=10,.tailData={8,17}}};
static int slot_count=10,board_count=20,rule=PE_CAT_OK,installs,removes,removed_count=2,all_locked;
ItemDataRecord *pe_cat_record(int w) { return &records[w]; }
const char *pe_cat_item_name(int id) { return id==0x97?"N Protector":"M9"; }
const char *pe_cat_effect_name(int w,int e) { (void)w;(void)e;++name_queries;return "A deliberately very long custom effect name that exceeds the pane"; }
const char *pe_cat_effect_short(int w,int e) { (void)w;(void)e;return "CUSTOM"; }
int pe_cat_slots(int w) { (void)w;return slot_count; }
int pe_cat_slot_effect(int w,int slot) { return slot<slot_count?records[w].tailData[slot]&31:0; }
int pe_cat_board_count(int w) { (void)w;return board_count; }
int pe_cat_board_effect(int w,int row) { (void)w;return row>=0&&row<board_count?row+1:0; }
int pe_cat_unlocked(int w,int e) { (void)w;(void)e;return !all_locked; }
uint32_t pe_cat_price(int w,int e) { (void)w;(void)e;++price_queries;return price; }
int pe_cat_needs_tool(int w,int e) { (void)w;(void)e;++tool_queries;return 0; }
int pe_cat_replaces(int w,int slot,int e,int *out,int max) { (void)w;(void)slot;(void)e;int n=removed_count<max?removed_count:max;for(int i=0;i<n;++i)out[i]=i+1;return n; }
int pe_cat_install(int w,int slot,int e,int dry) { if(rule)return rule;if(!dry){++installs;junk-=price;records[w].tailData[slot]=(unsigned char)e;}return PE_CAT_OK; }
int pe_cat_remove(int w,int slot,int dry) { if(rule)return rule;if(!dry){++removes;records[w].tailData[slot]=0;}return PE_CAT_OK; }
void pe_log(const char *t,const char *f,...) { (void)t;(void)f; }
void pe_mod_log(const char *t,const char *f,...) { (void)t;(void)f; }
void pe_toast(const char *text) { (void)text; }
int pe_hook_add(int i,PeHookFn f,void *u,int p,const char *m) {
    (void)u;(void)p;(void)m;if(i==PE_HOOK_EQUIP_EFFECT)custom_icon=f;return 0;
}
int pe_hook_dispatch(int id,void *ctx) {
    if(id==PE_HOOK_EQUIP_EFFECT&&custom_icon){int done=custom_icon(ctx,NULL);custom_badges+=done;return done;}return 0;
}
int Inv_RestoreSelection(unsigned int n) { (void)n;return 0; }
ItemDataRecord *Inv_LookupActiveListData(int n) { (void)n;return &records[1]; }

static void send(unsigned buttons) { assert(cat_input(current,buttons)); }
static void answer(int yes) {
    assert(pe_confirm_open()&&s_confirm_choice->cursor_x==1); /* No is the default. */
    MenuWidget_DestroyNode(s_confirm_choice->parent);pe_confirm_done(NULL,yes);
}
static void visible(MenuWidgetNode *n) { assert(n->cursor_y>=n->scroll_y&&n->cursor_y<n->scroll_y+n->visible_rows); }
static void draw_all(void) {
    frame_bytes=0;g_DrawPacketCursor=packet_arena;
    cat_draw_selector(s_cat.selector);cat_draw_board(s_screen.list);cat_draw_slots(s_cat.slots);cat_draw_stats(s_cat.stats);cat_draw_detail(s_cat.detail);
    size_t bytes=frame_bytes+(size_t)(g_DrawPacketCursor-packet_arena);
    if(bytes>max_frame_bytes)max_frame_bytes=bytes;
    /* 16 KB native arena: leave at least 6 KB for frames, draw areas,
     * the title, clock and Aya's icon rail, which are drawn afterwards. */
    if(bytes>0x2800)fprintf(stderr,"Catalog body over budget: %zu bytes\n",bytes);
    assert(bytes<=0x2800);
}
int main(void) {
    g_DrawPacketArenaBase=g_DrawPacketCursor=packet_arena;g_DrawOrderingTableEntry=(unsigned int *)&ordering;
    pe_armor_effect_display_register("test");
    /* Model bounds independent of engine limits, including a future 40-slot layout. */
    for(int count=0;count<=40;++count)for(int row=-2;row<45;++row) {
        PeCatalogCursor c={row,99};pe_catalog_cursor_clamp(&c,count,5);
        if(!count)assert(!c.row&&!c.top);else assert(c.row>=c.top&&c.row<c.top+5&&c.row<count);
    }
    MenuWidgetNode from={0};cat_open(&from);assert(current==s_cat.selector);
    assert(s_cat.selector->grid_width==2&&s_cat.selector->visible_rows==1);
    cat_draw_selector(s_cat.selector);assert(type_labels==3);
    send(0x2000);assert(s_cat.which==PE_ARMOR&&s_cat.selector->cursor_x==PE_ARMOR);
    send(PE_MENU_CROSS);assert(current==s_cat.slots);
    send(0x2000);assert(current->cursor_y==0); /* Remove is visibly first. */
    rule=PE_CAT_BAG;send(PE_MENU_CROSS);assert(!s_cat.review);rule=PE_CAT_OK;
    send(0x4000);send(PE_MENU_CROSS);
    assert(s_cat.review&&s_cat.ask_which==PE_ARMOR&&s_cat.before.itemId==0x97);
    send(PE_MENU_CROSS);answer(0);assert(!installs&&!removes);
    send(PE_MENU_CIRCLE);send(PE_MENU_CIRCLE);assert(current==s_cat.selector);
    send(0x8000);assert(s_cat.which==PE_GUN&&s_cat.selector->cursor_x==PE_GUN);
    send(PE_MENU_CROSS);assert(current==s_cat.slots);
    assert(s_cat.slots!=s_screen.list&&s_cat.slots->visible_rows==PE_CAT_SLOT_ROWS&&s_screen.list->visible_rows==8);
    for(int i=0;i<24;++i)if(used[i]&&pool[i].mode==1){assert(pool[i].x>=0&&pool[i].x+pool[i].grid_width<=320);assert(pool[i].y+pool[i].visible_rows<=224);}
    send(0x1000);assert(s_cat.slots->cursor_y==9&&s_cat.slots->scroll_y==6);visible(s_cat.slots);
    send(0x4000);assert(s_cat.slots->cursor_y==0&&s_cat.slots->scroll_y==0);
    send(0x1000);send(0x2000);assert(current==s_screen.list);
    send(0x1000);assert(current->cursor_y==20&&current->scroll_y==13);visible(current);
    int board_top=current->scroll_y;
    send(0x8000);assert(current==s_cat.slots&&current->cursor_y==9&&current->scroll_y==6);
    send(4);assert(s_cat.which==1);send(8);assert(s_cat.which==0&&s_cat.slots->cursor_y==9&&s_screen.list->scroll_y==board_top);
    send(0x2000);inject_row=9;inject_top=1;inject_adjust=7;send(0x4000);assert(current->scroll_y==2&&!current->scroll_adjust);
    PeCatalogCursor remembered=s_cat.gear[0].slots;
    slot_count=3;cat_draw_board(s_screen.list);
    assert(s_cat.slots->cursor_y==2&&s_cat.slots->scroll_y==0);
    assert(s_cat.slots->parent->visible_rows==PE_CAT_SLOT_ROWS*PE_CAT_ROW_HEIGHT+8);
    slot_count=10;cat_draw_board(s_screen.list);s_cat.gear[0].slots=remembered;cat_refresh();
    draw_all();assert(print_count>0&&stat_sprites>0); /* Long labels must fit each native pane. */
    /* Both lock states and every Catalog viewport stay below the frame budget. */
    assert(effect_sprites>0);
    for(int advance=3;advance<=7;advance+=4)for(int lock=0;lock<2;++lock) {
        font_advance=advance;all_locked=lock;
        for(int row=0;row<cat_board_count();++row) {
            cat_rows(s_screen.list,cat_board_count(),(PeCatalogCursor){row,row});cat_save();draw_all();
            unsigned char *title=NULL;cat_help(&title,NULL);assert(title);
            int n=0;while(title[n]!=0xFF)++n;assert(n<=48&&Draw_MeasureTextWidth(title)<=278);
        }
    }
    font_advance=7;
    /* Unknown entries are opaque in every presentation path, not merely dimmed. */
    all_locked=1;
    int icons_before=effect_sprites,badges_before=custom_badges,prints_before=print_count;
    int names_before=name_queries,prices_before=price_queries,tools_before=tool_queries;
    g_DrawTextPosX=128;print_limit=124;
    pe_catalog_view_effect(PE_GUN,0,1);
    unsigned char mystery[8];pe_text_from_ascii(mystery,sizeof mystery,"????");
    assert(!memcmp(last_text,mystery,5)&&print_count==prints_before+1);
    pe_catalog_view_effect(PE_ARMOR,0,17);assert(!memcmp(last_text,mystery,5));
    pe_catalog_view_detail(s_cat.detail,PE_ARMOR,0,17,0);assert(!memcmp(last_text,mystery,5));
    assert(effect_sprites==icons_before&&custom_badges==badges_before);
    assert(name_queries==names_before&&price_queries==prices_before&&tool_queries==tools_before);
    unsigned char *hidden_title=NULL;cat_help(&hidden_title,NULL);assert(hidden_title&&!memcmp(hidden_title,mystery,5));
    send(PE_MENU_CROSS);assert(!s_cat.review&&!s_cat.ask_valid&&!installs&&!removes);
    /* Re-discovery reveals the normal icon/cost, and revocation cancels review. */
    all_locked=0;pe_catalog_view_effect(PE_GUN,0,1);assert(effect_sprites>icons_before&&price_queries>prices_before);
    send(PE_MENU_CROSS);assert(s_cat.review);
    all_locked=1;cat_draw_board(s_screen.list);assert(!s_cat.review&&!s_cat.ask_valid&&!s_cat.removed);
    all_locked=0;removed_count=10;send(PE_MENU_CROSS);assert(s_cat.review&&s_cat.review_count==16&&!installs);
    for(int row=0;row<s_cat.review_count;++row){cat_rows(s_screen.list,s_cat.review_count,(PeCatalogCursor){row,row});draw_all();}
    cat_rows(s_screen.list,s_cat.review_count,(PeCatalogCursor){0,0});
    send(0x1000);assert(current->cursor_y==15);visible(current);draw_all();
    send(PE_MENU_CROSS);answer(0);assert(!s_cat.review&&!installs&&junk==100);
    for(int i=PE_CAT_LOCKED;i<=PE_CAT_TOOL;++i){rule=i;send(PE_MENU_CROSS);assert(!s_cat.review&&!installs);}
    rule=PE_CAT_OK;send(PE_MENU_CROSS);send(PE_MENU_CROSS);answer(1);assert(installs==1&&junk==90);
    cat_answer(1);assert(installs==1); /* no duplicate callback spend */
    send(PE_MENU_CROSS);send(PE_MENU_CROSS);records[0].bonusStats[0]++;answer(1);assert(installs==1);
    send(PE_MENU_CROSS);send(PE_MENU_CROSS);price++;answer(1);assert(installs==1);price--;
    send(0x80);assert(!s_cat.review&&removes==0); /* Square is deliberately inert. */
    s_cat.gear[s_cat.which].board=(PeCatalogCursor){0,0};cat_refresh();cat_focus(s_screen.list);
    send(PE_MENU_CROSS);assert(s_cat.review&&!s_cat.ask_effect);draw_all();
    send(PE_MENU_CROSS);answer(1);assert(removes==1&&junk==90);
    send(PE_MENU_CROSS);send(PE_MENU_CIRCLE);assert(!s_cat.review&&current==s_screen.list);
    send(PE_MENU_CIRCLE);send(PE_MENU_CIRCLE);send(PE_MENU_CIRCLE);assert(!s_screen.panel&&current==&from);
    for(int i=0;i<24;++i)assert(!used[i]); /* All panels and scroll-arrow nodes released. */
    slot_count=0;cat_open(&from);draw_all();send(PE_MENU_CROSS);send(PE_MENU_CROSS);assert(current==s_cat.slots);send(PE_MENU_CIRCLE);send(PE_MENU_CIRCLE);
    for(int i=0;i<24;++i)assert(!used[i]);
    /* Simulate a teardown that bypasses Circle and then a late callback. */
    slot_count=10;cat_open(&from);send(PE_MENU_CROSS);send(0x2000);send(PE_MENU_CROSS);assert(s_cat.removed);
    while(g_MenuWidgetActiveListHead)MenuWidget_DestroyNode(g_MenuWidgetActiveListHead);
    cat_frame(NULL,NULL);assert(!s_cat.removed&&!s_cat.ask_valid);cat_answer(1);assert(installs==1);
    for(int i=0;i<13;++i)MenuWidget_CreateSimpleNode(1,NULL,NULL,0);
    cat_open(&from);assert(!cat_alive()); /* Reserve native confirmation capacity. */
    while(g_MenuWidgetActiveListHead)MenuWidget_DestroyNode(g_MenuWidgetActiveListHead);
    print_limit=124;g_TextCursorX=10;g_TextCursorY=20;g_DrawTextDimmed=1;
    int before_custom=custom_badges,before_sprites=effect_sprites;
    for(int e=17;e<=20;++e)pe_catalog_effect_icon(PE_ARMOR,e,0,60);
    assert(custom_badges==before_custom+4&&effect_sprites==before_sprites);
    assert(g_TextCursorX==10&&g_TextCursorY==20&&g_DrawTextDimmed==1);
    force_large_icon=1;g_DrawPacketCursor=packet_arena;pe_catalog_effect_icon(PE_GUN,1,0,16);
    assert(g_DrawPacketCursor-packet_arena==sizeof(RenderTexturedQuad));
    assert(g_TextCursorX==10&&g_TextCursorY==20);force_large_icon=0;
    printf("Catalog body packet peak: %zu / 16384 bytes (6 KB reserved for native chrome).\n",max_frame_bytes);
    puts("Catalog UI passed: native bounds, independent wrapping/scroll, gear memory, long labels, full review, cancel, stale confirmation, rules boundary, cleanup and empty slots.");
    return 0;
}
