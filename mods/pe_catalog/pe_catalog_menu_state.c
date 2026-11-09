#include "pe_catalog_menu_state.h"

void pe_catalog_cursor_clamp(PeCatalogCursor *c, int count, int visible) {
    if (count <= 0 || visible <= 0) { c->row = c->top = 0; return; }
    if (c->row < 0) c->row = 0;
    if (c->row >= count) c->row = count - 1;
    int last = count > visible ? count - visible : 0;
    if (c->top < 0) c->top = 0;
    if (c->top > last) c->top = last;
    if (c->row < c->top) c->top = c->row;
    if (c->row >= c->top + visible) c->top = c->row - visible + 1;
}
