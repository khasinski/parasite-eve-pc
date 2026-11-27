#ifndef PE_CATALOG_MENU_VIEW_H
#define PE_CATALOG_MENU_VIEW_H

#include "pe1/menu_widget.h"

/* All geometry is in the game's 320x224 menu area. Row height includes
 * the list renderer's two-pixel text inset and the 12-pixel glyphs. */
enum { PE_CAT_SLOT_ROWS = 4, PE_CAT_BOARD_ROWS = 8, PE_CAT_ROW_HEIGHT = 14 };
void pe_catalog_view_layout(MenuWidgetNode *list, int visible, int width);
void pe_catalog_view_text(const char *text, int offset, int width);
void pe_catalog_view_slot(int which, int slot);
void pe_catalog_view_effect(int which, int slot, int effect);
void pe_catalog_view_remove(int which, int slot);
void pe_catalog_view_stats(MenuWidgetNode *node, int which, const MenuWidgetNode *slots);
void pe_catalog_view_detail(MenuWidgetNode *node, int which, int slot, int effect, int review);
const char *pe_catalog_view_problem(int code);

#endif
