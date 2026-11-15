/* Export the same C fixtures for the isolated browser review surface. */
#include "bp_ui.h"
#include <stdio.h>
static void quoted(const char *s) {
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    putchar('"');
    for (; *p; ++p) {
        if (*p == '"' || *p == '\\') { putchar('\\'); putchar(*p); }
        else if (*p < 32 || *p == '<' || *p == '>') printf("\\u%04x", (unsigned)*p);
        else putchar(*p);
    }
    putchar('"');
}
static void field(const char *key, const char *value) { quoted(key); putchar(':'); quoted(value); }
int main(void) {
    const BpUiCatalog *c = bp_ui_demo_catalog();
    size_t p, r, d;
    if (!bp_ui_validate(c,NULL,0)) return 1;
    printf("{\"tabs\":[");
    for (p = 0; p < c->tab_count; ++p) {
        if (p) putchar(','); putchar('{'); field("id",c->tabs[p].id); putchar(',');
        field("label",c->tabs[p].label); putchar(','); field("page",c->tabs[p].page); putchar(','); field("group",c->tabs[p].group); putchar('}');
    }
    printf("],\"pages\":[");
    for (p = 0; p < c->page_count; ++p) {
        const BpUiPage *page = &c->pages[p];
        if (p) putchar(','); putchar('{');
        BpUiController ui; bp_ui_init(&ui,c,page->id,7);
        field("id",page->id); putchar(','); field("title",page->title);
        putchar(','); field("context",page->context); putchar(','); field("wallet",page->wallet);
        putchar(','); field("previous",bp_ui_category_target(&ui,-1)); putchar(','); field("next",bp_ui_category_target(&ui,1));
        putchar(','); field("tab",page->tab_id); putchar(','); field("subject",page->subject);
        printf(",\"rows\":[");
        for (r = 0; r < page->row_count; ++r) {
            const BpUiRow *row = &page->rows[r];
            if (r) putchar(','); putchar('{');
            field("id",row->id); putchar(','); field("label",row->label);
            putchar(','); field("value",row->value); putchar(','); field("target",row->target);
            putchar(','); field("question",row->question); putchar(','); field("reason",row->reason);
            printf(",\"state\":%d,\"action\":%d,\"kind\":%d,\"preview\":",(int)row->state,(int)row->action,(int)row->kind);
            if (row->preview) {
                const BpUiPreview *v=row->preview;putchar('{');field("title",v->title);putchar(',');field("before",v->before);putchar(',');field("after",v->after);putchar(',');
                field("benefit",v->benefit);putchar(',');field("note",v->note);putchar(',');field("cost",v->cost);putchar(',');field("remaining",v->remaining);putchar('}');
            } else printf("null");
            printf(",\"details\":[");
            for (d = 0; d < row->detail_count; ++d) { if (d) putchar(','); quoted(row->details[d].text); }
            printf("]}");
        }
        printf("]}");
    }
    puts("]}");
    return 0;
}
